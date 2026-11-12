#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/libshell/parser.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/libshell/session.hpp>
#include <tinyos/libshell/stream.hpp>

namespace tinyos::libshell
{
    // Result of running one command line.
    struct ExecutionResult
    {
        Status status;
        size_t commands_run;

        // Set when a command was not found, so the caller can print a useful
        // diagnostic instead of a bare exit code.
        const char* failed_command;
    };

    // Execution engine limits.
    inline constexpr size_t MaxPipelineBytes = 1024;

    // Redirection backend. The engine needs to open a file for `>`/`>>`/`<`,
    // but the shell must stay free of kernel dependencies so it can be tested on
    // the host. The host supplies callbacks; the kernel wires them to the VFS.
    struct RedirectOps
    {
        // Opens `path` for writing, truncating when `append` is false. On success
        // returns a non-null stream and sets `handle`. The stream must stay valid
        // until close() is called.
        bool (*open_write)(const char* path, bool append, Stream& stream_out, void** handle, void* user);

        // Opens `path` for reading. Reading is not implemented by the engine; the
        // callback exists so redirection is refused explicitly rather than
        // silently ignored.
        bool (*open_read)(const char* path, void** handle, void* user);

        void (*close)(void* handle, void* user);

        void* user;
    };

    // Runs one parsed command line against `session`.
    //
    // The session is passed by reference because commands such as `cd`, `set`
    // and `unset` change it; passing it by value forced `cd` to refuse with
    // "needs the session object", which was honest but useless.
    ExecutionResult execute(const char* line, Session& session, const Stream& out, const Stream& err,
        const RedirectOps* redirects = nullptr);

    // Convenience overload used by host tests: no redirection support.
    ExecutionResult execute_line(const char* line, Session& session, const Stream& out, const Stream& err);
}
