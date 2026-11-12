#include <tinyos/core/memory.hpp>
#include <tinyos/libk/path.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/exec.hpp>

namespace
{
    using tinyos::libshell::Environment;
    using tinyos::libshell::ExecutionResult;
    using tinyos::libshell::ParsedCommand;
    using tinyos::libshell::ParsedList;
    using tinyos::libshell::Parser;
    using tinyos::libshell::RedirectOps;
    using tinyos::libshell::Session;
    using tinyos::libshell::Status;
    using tinyos::libshell::Stream;

    const char* env_lookup_callback(const char* name, void* user)
    {
        const auto* environment = static_cast<const Environment*>(user);
        if (environment == nullptr)
        {
            return nullptr;
        }

        return environment->get(name);
    }

    // Runs one command with its redirections applied.
    Status run_command(const ParsedCommand& command, Session& session, const Stream& default_out,
        const Stream& default_err, const RedirectOps* redirects, const char** failed_command)
    {
        const tinyos::libshell::Command* descriptor = tinyos::libshell::find(command.argv[0]);
        if (descriptor == nullptr)
        {
            if (failed_command != nullptr)
            {
                *failed_command = command.argv[0];
            }

            default_err.write(command.argv[0]);
            default_err.line(": command not found");
            return Status::NotFound;
        }

        // An unimplemented command must say why it cannot run rather than
        // pretending to succeed. This is the runtime half of the honest
        // availability model (defect D21).
        if (!tinyos::libshell::is_available(descriptor->availability))
        {
            // Built as one string: the previous version called line() for each
            // fragment, which put a newline after "not available yet (" and left
            // the reason on its own line.
            default_err.write(command.argv[0]);
            default_err.write(": not available yet (");
            default_err.write(tinyos::libshell::availability_name(descriptor->availability));
            default_err.line(")");
            return Status::Unavailable;
        }

        if ((descriptor->required_capabilities & ~session.capabilities) != 0)
        {
            default_err.write(command.argv[0]);
            default_err.line(": permission denied");
            return Status::Denied;
        }

        // Redirection. Output redirection swaps the stream the command writes
        // to; a failure is reported and the command is not run, because
        // executing with unintended output destination is worse than refusing.
        Stream out = default_out;
        Stream err = default_err;
        void* handle = nullptr;
        bool opened = false;

        if (command.redirect_out != nullptr)
        {
            if (redirects == nullptr || redirects->open_write == nullptr)
            {
                default_err.line("redirection is not available in this context");
                return Status::Io;
            }

            if (!redirects->open_write(command.redirect_out, command.append_out, out, &handle, redirects->user))
            {
                default_err.write(command.redirect_out);
                default_err.line(": cannot open for writing");
                return Status::Io;
            }

            opened = true;
        }

        if (command.redirect_in != nullptr)
        {
            // Reading from a file is not implemented yet; say so instead of
            // running the command against the terminal.
            default_err.line("input redirection is not implemented yet");
            if (opened && redirects != nullptr && redirects->close != nullptr)
            {
                redirects->close(handle, redirects->user);
            }
            return Status::Io;
        }

        if (command.redirect_error != nullptr)
        {
            if (redirects == nullptr || redirects->open_write == nullptr)
            {
                default_err.line("stderr redirection is not available in this context");
                if (opened && redirects->close != nullptr)
                {
                    redirects->close(handle, redirects->user);
                }
                return Status::Io;
            }

            void* error_handle = nullptr;
            if (!redirects->open_write(command.redirect_error, false, err, &error_handle, redirects->user))
            {
                default_err.write(command.redirect_error);
                default_err.line(": cannot open for writing");
                if (opened && redirects->close != nullptr)
                {
                    redirects->close(handle, redirects->user);
                }
                return Status::Io;
            }

            // The error handle is closed through the same path as the output
            // handle by reusing `handle` when no output redirect was requested.
            if (!opened)
            {
                handle = error_handle;
                opened = true;
            }
        }

        tinyos::libshell::Context context = {};
        context.argv = command.argv;
        context.argc = command.argc;
        context.out = out;
        context.err = err;
        context.session = &session;
        context.interactive = session.interactive && command.redirect_out == nullptr;

        const Status status = descriptor->handler(context);

        if (opened && redirects != nullptr && redirects->close != nullptr)
        {
            redirects->close(handle, redirects->user);
        }

        return status;
    }

    // Runs one pipeline. A pipeline is executed left to right; true concurrent
    // execution needs processes, so multi-stage pipelines currently report that
    // rather than silently running only the first stage.
    Status run_pipeline(const tinyos::libshell::ParsedPipeline& pipeline, Session& session,
        const Stream& out, const Stream& err, const RedirectOps* redirects, const char** failed_command,
        size_t& commands_run)
    {
        if (pipeline.command_count == 0)
        {
            return Status::Ok;
        }

        if (pipeline.command_count > 1)
        {
            err.line("pipelines need processes and are not available yet");
            return Status::Unavailable;
        }

        ++commands_run;
        return run_command(pipeline.commands[0], session, out, err, redirects, failed_command);
    }
}

namespace tinyos::libshell
{
    ExecutionResult execute(const char* line, Session& session, const Stream& out, const Stream& err,
        const RedirectOps* redirects)
    {
        ExecutionResult result = { Status::Ok, 0, nullptr };

        // The parser owns fixed internal buffers, so it lives on the stack of
        // this call. A longer lifetime would require allocation.
        Parser parser;
        ParsedList list = {};

        const ParseStatus parsed = parser.parse(line, list, env_lookup_callback, &session.env);
        if (parsed == ParseStatus::Empty)
        {
            return result;
        }

        if (parsed != ParseStatus::Ok)
        {
            err.write("syntax error: ");
            err.line(parse_status_name(parsed));
            result.status = Status::Usage;
            return result;
        }

        Status last = Status::Ok;
        bool skip_next = false;

        for (int index = 0; index < list.pipeline_count; ++index)
        {
            // '&&' runs only after success, '||' only after failure.
            if (skip_next)
            {
                skip_next = false;
                continue;
            }

            last = run_pipeline(list.pipelines[index], session, out, err, redirects, &result.failed_command,
                result.commands_run);

            const tinyos::libshell::TokenKind separator = list.separators[index + 1];
            if (separator == tinyos::libshell::TokenKind::And && last != Status::Ok)
            {
                skip_next = true;
            }
            else if (separator == tinyos::libshell::TokenKind::Or && last == Status::Ok)
            {
                skip_next = true;
            }
        }

        result.status = last;
        return result;
    }

    ExecutionResult execute_line(const char* line, Session& session, const Stream& out, const Stream& err)
    {
        return execute(line, session, out, err, nullptr);
    }
}
