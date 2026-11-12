// Host unit tests for the shell execution engine.
//
// These cover the behaviour the old shell had none of: exit codes, `&&`/`||`
// gating, argument delivery, unavailable-command reporting and redirection
// refusal. Everything here runs without a kernel.

#include <cstdio>
#include <cstring>

#include <tinyos/libshell/exec.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/libshell/stream.hpp>

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void check(bool condition, const char* description)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::printf("  FAIL  %s\n", description);
        }
    }

    using tinyos::libshell::Availability;
    using tinyos::libshell::Category;
    using tinyos::libshell::Context;
    using tinyos::libshell::Risk;
    using tinyos::libshell::Session;
    using tinyos::libshell::Status;

    // Capability is a plain enum, so its enumerators are namespace-scoped.
    using tinyos::libshell::CapabilityAdmin;
    using tinyos::libshell::CapabilityNone;

    // Records how many times each probe ran, so sequencing can be verified.
    int g_alpha_runs = 0;
    int g_beta_runs = 0;
    Status g_alpha_result = Status::Ok;

    Status alpha_handler(Context& context)
    {
        ++g_alpha_runs;
        context.out.line("alpha");
        return g_alpha_result;
    }

    Status beta_handler(Context& context)
    {
        ++g_beta_runs;
        context.out.line("beta");
        return Status::Ok;
    }

    // Echoes argv[1] so argument delivery can be asserted.
    Status echo_handler(Context& context)
    {
        if (context.argc < 2)
        {
            context.err.line("echo: missing argument");
            return Status::Usage;
        }

        context.out.line(context.argv[1]);
        return Status::Ok;
    }

    // Reports the context fields it received.
    Status context_handler(Context& context)
    {
        context.out.write(context.cwd());
        context.out.put_char(' ');
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(context.uid()));
        context.out.write(buffer);
        context.out.put_char('\n');
        return Status::Ok;
    }

    Status unavailable_handler(Context&)
    {
        return Status::Ok;
    }

    Status admin_handler(Context& context)
    {
        context.out.line("admin-ran");
        return Status::Ok;
    }

    const tinyos::libshell::Command CommandTable[] = {
        { "alpha", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            alpha_handler, "first probe", "alpha", nullptr },
        { "beta", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            beta_handler, "second probe", "beta", nullptr },
        { "echo", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            echo_handler, "echo argument", "echo <text>", "echo hi" },
        { "ctx", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            context_handler, "report context", "ctx", nullptr },
        { "netconfig", nullptr, Category::Devices, Availability::NeedsNetworking, Risk::WritesState,
            CapabilityNone, unavailable_handler, "needs networking", nullptr, nullptr },
        { "secret", nullptr, Category::Security, Availability::Real, Risk::WritesState, CapabilityAdmin,
            admin_handler, "needs admin", "secret", nullptr }
    };

    void reset_state()
    {
        tinyos::libshell::reset_registry();
        tinyos::libshell::register_table(CommandTable, sizeof(CommandTable) / sizeof(CommandTable[0]));
        g_alpha_runs = 0;
        g_beta_runs = 0;
        g_alpha_result = Status::Ok;
    }

    void init_session(Session& session)
    {
        // Use the library's own initialiser so tests exercise the same defaults
        // the shell runs with, instead of a parallel copy that can drift.
        tinyos::libshell::initialize_session(session);
        session.interactive = false;
    }

    // Small helper bundling a capture buffer and its stream.
    struct Capture
    {
        char text[512];
        tinyos::libshell::BufferState state;
        tinyos::libshell::Stream stream;

        Capture()
        {
            std::memset(text, 0, sizeof(text));
            stream = tinyos::libshell::buffer_stream(state, text, sizeof(text));
        }

        const char* data() const { return state.buffer; }
    };

    void test_basic_execution()
    {
        std::printf("basic execution\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        const auto result = tinyos::libshell::execute_line("alpha", session, out.stream, err.stream);
        check(result.status == Status::Ok, "alpha succeeds");
        check(g_alpha_runs == 1, "alpha ran once");
        check(result.commands_run == 1, "one command counted");
        check(std::strcmp(out.data(), "alpha\n") == 0, "alpha wrote to out");
        check(err.data()[0] == '\0', "nothing on err");
    }

    void test_arguments()
    {
        std::printf("arguments\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        tinyos::libshell::execute_line("echo hello", session, out.stream, err.stream);
        check(std::strcmp(out.data(), "hello\n") == 0, "argument delivered");

        out.state.size = 0;
        out.text[0] = '\0';
        tinyos::libshell::execute_line("echo 'two words'", session, out.stream, err.stream);
        check(std::strcmp(out.data(), "two words\n") == 0, "quoted argument delivered as one");

        // Missing required argument must produce a usage error, not silence.
        out.state.size = 0;
        out.text[0] = '\0';
        const auto result = tinyos::libshell::execute_line("echo", session, out.stream, err.stream);
        check(result.status == Status::Usage, "missing argument reports usage");
        check(err.data()[0] != '\0', "missing argument explains itself");
    }

    void test_unknown_and_unavailable()
    {
        std::printf("unknown and unavailable\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        const auto unknown = tinyos::libshell::execute_line("nosuchcommand", session, out.stream, err.stream);
        check(unknown.status == Status::NotFound, "unknown command is NotFound");
        check(unknown.failed_command != nullptr &&
            std::strcmp(unknown.failed_command, "nosuchcommand") == 0, "failed command reported");
        check(std::strstr(err.data(), "command not found") != nullptr, "not-found message");

        err.state.size = 0;
        err.text[0] = '\0';
        const auto unavailable = tinyos::libshell::execute_line("netconfig", session, out.stream, err.stream);
        check(unavailable.status == Status::Unavailable, "planned command is unavailable");
        // The message must name the reason instead of a bare refusal.
        check(std::strstr(err.data(), "needs-networking") != nullptr, "unavailable reason names the blocker");
    }

    void test_capability_gate()
    {
        std::printf("capability gate\n");

        reset_state();
        Session session;
        init_session(session);
        session.capabilities = 0;
        Capture out;
        Capture err;

        const auto denied = tinyos::libshell::execute_line("secret", session, out.stream, err.stream);
        check(denied.status == Status::Denied, "missing capability denies");
        check(std::strstr(err.data(), "permission denied") != nullptr, "denial explained");
        check(std::strcmp(out.data(), "") == 0, "denied command produced no output");

        session.capabilities = CapabilityAdmin;
        err.state.size = 0;
        err.text[0] = '\0';
        const auto allowed = tinyos::libshell::execute_line("secret", session, out.stream, err.stream);
        check(allowed.status == Status::Ok, "granted capability allows");
        check(std::strcmp(out.data(), "admin-ran\n") == 0, "command ran with capability");
    }

    void test_sequencing_and_exit_codes()
    {
        std::printf("sequencing\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        // ';' runs both regardless of status.
        g_alpha_result = Status::Failed;
        tinyos::libshell::execute_line("alpha ; beta", session, out.stream, err.stream);
        check(g_alpha_runs == 1 && g_beta_runs == 1, "sequence runs both");

        // '&&' skips the right side after a failure.
        reset_state();
        g_alpha_result = Status::Failed;
        tinyos::libshell::execute_line("alpha && beta", session, out.stream, err.stream);
        check(g_alpha_runs == 1 && g_beta_runs == 0, "and skips after failure");

        // '&&' runs the right side after success.
        reset_state();
        g_alpha_result = Status::Ok;
        tinyos::libshell::execute_line("alpha && beta", session, out.stream, err.stream);
        check(g_alpha_runs == 1 && g_beta_runs == 1, "and runs after success");

        // '||' runs the right side after a failure.
        reset_state();
        g_alpha_result = Status::Failed;
        tinyos::libshell::execute_line("alpha || beta", session, out.stream, err.stream);
        check(g_alpha_runs == 1 && g_beta_runs == 1, "or runs after failure");

        // '||' skips the right side after success.
        reset_state();
        g_alpha_result = Status::Ok;
        tinyos::libshell::execute_line("alpha || beta", session, out.stream, err.stream);
        check(g_alpha_runs == 1 && g_beta_runs == 0, "or skips after success");
    }

    void test_context_plumbing()
    {
        std::printf("context plumbing\n");

        reset_state();
        Session session;
        init_session(session);
        std::strcpy(session.cwd, "/home/alice");
        session.uid = 1001;
        Capture out;
        Capture err;

        tinyos::libshell::execute_line("ctx", session, out.stream, err.stream);
        check(std::strcmp(out.data(), "/home/alice 1001\n") == 0, "cwd and uid reach the handler");

        // A second session with different state must not see the first's cwd.
        Session other;
        init_session(other);
        std::strcpy(other.cwd, "/tmp");
        other.uid = 1002;

        out.state.size = 0;
        out.text[0] = '\0';
        tinyos::libshell::execute_line("ctx", other, out.stream, err.stream);
        check(std::strcmp(out.data(), "/tmp 1002\n") == 0, "second session isolated from first");
    }

    void test_environment_expansion()
    {
        std::printf("environment expansion\n");

        reset_state();
        Session session;
        init_session(session);
        session.env.set("USER", "alice");
        Capture out;
        Capture err;

        tinyos::libshell::execute_line("echo $USER", session, out.stream, err.stream);
        check(std::strcmp(out.data(), "alice\n") == 0, "environment expanded");
    }

    void test_pipelines_refused_honestly()
    {
        std::printf("pipelines\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        // Multi-stage pipelines need processes. The engine must say so rather
        // than silently running only the first stage.
        const auto result = tinyos::libshell::execute_line("alpha | beta", session, out.stream, err.stream);
        check(result.status == Status::Unavailable, "pipeline refused as unavailable");
        check(std::strstr(err.data(), "pipeline") != nullptr, "pipeline refusal names the reason");
        check(g_alpha_runs == 0 && g_beta_runs == 0, "no stage ran");
    }

    void test_redirection_refused_without_backend()
    {
        std::printf("redirection\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        const auto result = tinyos::libshell::execute_line("echo hi > out.txt", session, out.stream, err.stream);
        check(result.status == Status::Io, "redirect without backend is an io error");
        check(std::strstr(err.data(), "redirection is not available") != nullptr, "refusal explained");

        // Input redirection is not implemented; it must say so.
        err.state.size = 0;
        err.text[0] = '\0';
        const auto input = tinyos::libshell::execute_line("echo hi < in.txt", session, out.stream, err.stream);
        check(input.status == Status::Io, "input redirect refused");
        check(std::strstr(err.data(), "not implemented") != nullptr, "input refusal explained");
    }

    void test_syntax_errors()
    {
        std::printf("syntax errors\n");

        reset_state();
        Session session;
        init_session(session);
        Capture out;
        Capture err;

        const auto result = tinyos::libshell::execute_line("echo 'unterminated", session, out.stream, err.stream);
        check(result.status == Status::Usage, "syntax error is a usage error");
        check(std::strstr(err.data(), "syntax error") != nullptr, "syntax error labelled");

        // An empty line is a no-op, not an error.
        const auto empty = tinyos::libshell::execute_line("   ", session, out.stream, err.stream);
        check(empty.status == Status::Ok && empty.commands_run == 0, "empty line is ok");

        // A null line must not crash.
        const auto null_line = tinyos::libshell::execute_line(nullptr, session, out.stream, err.stream);
        check(null_line.status == Status::Ok && null_line.commands_run == 0, "null line is ok");
    }

    // Session mutation: `cd` and `set` now write through the Session pointer
    // instead of refusing with "needs the session object".
    void test_session_mutation()
    {
        std::printf("session mutation\n");

        tinyos::libshell::Session session;
        tinyos::libshell::initialize_session(session);

        check(std::strcmp(session.cwd, "/") == 0, "default cwd is root");
        check(session.uid == 0, "default uid is root");
        check(!session.authenticated, "default session is not authenticated");
        check(tinyos::libshell::is_root(session), "default session is root");

        // set_session_cwd keeps $PWD in step.
        tinyos::libshell::set_session_cwd(session, "/system");
        check(std::strcmp(session.cwd, "/system") == 0, "cwd changed");
        const char* pwd = session.env.get("PWD");
        check(pwd != nullptr && std::strcmp(pwd, "/system") == 0, "PWD follows cwd");

        // An invalid path must be ignored rather than corrupting the session.
        tinyos::libshell::set_session_cwd(session, "relative");
        check(std::strcmp(session.cwd, "/system") == 0, "relative path rejected");

        // Two initialised sessions are independent.
        tinyos::libshell::Session other;
        tinyos::libshell::initialize_session(other);
        tinyos::libshell::set_session_cwd(other, "/tmp");
        check(std::strcmp(session.cwd, "/system") == 0, "second session does not disturb the first");
    }

    void test_environment_container()
    {
        using tinyos::libshell::Environment;
        Environment env;
        env.clear();

        check(env.get("MISSING") == nullptr, "missing var returns null");
        check(env.set("A", "1"), "set A");
        check(std::strcmp(env.get("A"), "1") == 0, "get A");
        check(env.set("A", "2"), "overwrite A");
        check(std::strcmp(env.get("A"), "2") == 0, "overwrite reflected");
        check(env.count() == 1, "overwrite does not grow count");
        check(env.unset("A"), "unset A");
        check(env.get("A") == nullptr, "unset removed");
        check(!env.set(nullptr, "x"), "null name rejected");
        check(!env.set("", "x"), "empty name rejected");
    }

    void test_buffer_stream()
    {
        std::printf("buffer stream\n");

        char small[8];
        tinyos::libshell::BufferState state;
        const auto stream = tinyos::libshell::buffer_stream(state, small, sizeof(small));

        stream.write("abcdefghij");
        // Truncation is reported and the buffer stays a valid string.
        check(state.truncated, "truncation reported");
        check(small[sizeof(small) - 1] == '\0', "buffer terminated");
        check(std::strlen(small) == sizeof(small) - 1, "filled to capacity minus terminator");

        char exact[4];
        tinyos::libshell::BufferState exact_state;
        const auto exact_stream = tinyos::libshell::buffer_stream(exact_state, exact, sizeof(exact));
        exact_stream.write("abc");
        check(!exact_state.truncated, "exact fit is not truncated");
        check(std::strcmp(exact, "abc") == 0, "exact content");

        // A null buffer must not crash.
        tinyos::libshell::BufferState null_state;
        const auto null_stream = tinyos::libshell::buffer_stream(null_state, nullptr, 0);
        null_stream.write("x");
        check(null_state.truncated, "null buffer records truncation");
    }
}

int main()
{
    test_basic_execution();
    test_arguments();
    test_unknown_and_unavailable();
    test_capability_gate();
    test_sequencing_and_exit_codes();
    test_context_plumbing();
    test_environment_expansion();
    test_pipelines_refused_honestly();
    test_redirection_refused_without_backend();
    test_syntax_errors();
    test_session_mutation();
    test_environment_container();
    test_buffer_stream();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
