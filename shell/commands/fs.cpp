// Filesystem commands.
//
// These are the commands that were already real in the old shell: RAMFS browse,
// read, write, create, copy, move, remove and chmod. The bodies are ported to
// the Stream/Context interface rather than the 1499 direct drivers::vga:: calls,
// and path handling now goes through libk::path::resolve instead of the shell's
// private resolver with a different length limit (defect D13).

#include <tinyos/kernel/vfs/ramfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/libk/path.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/shell/commands/commands.hpp>

namespace
{
    using tinyos::libshell::Availability;
    using tinyos::libshell::Category;
    using tinyos::libshell::Context;
    using tinyos::libshell::Risk;
    using tinyos::libshell::Status;

    // Capability is a plain enum, so its enumerators are namespace-scoped rather
    // than nested in the type.
    using tinyos::libshell::CapabilityFileRead;
    using tinyos::libshell::CapabilityFileWrite;
    using tinyos::libshell::CapabilityNone;

    constexpr size_t PathBuffer = tinyos::libk::path::MaxPathBufferBytes;

    // Upper bound for the assembled text of `write`. Kept local so this module
    // does not depend on the execution engine's buffer size.
    constexpr size_t MaxWriteText = 512;

    // Resolves one argument against the session's working directory.
    const char* resolve_arg(Context& context, int index, char* buffer, size_t capacity)
    {
        const char* raw = context.arg(index);
        if (raw == nullptr)
        {
            return nullptr;
        }

        if (!tinyos::libk::path::resolve(context.cwd(), raw, buffer, capacity))
        {
            return nullptr;
        }

        return buffer;
    }

    void write_unsigned(tinyos::libshell::Stream& stream, size_t value)
    {
        char digits[21];
        size_t position = sizeof(digits);
        digits[--position] = '\0';

        do
        {
            digits[--position] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value != 0 && position > 0);

        stream.write(&digits[position]);
    }

    void write_mode(tinyos::libshell::Stream& stream, uint16_t mode)
    {
        char text[8];
        size_t position = 0;
        text[position++] = '0';
        for (int shift = 6; shift >= 0; shift -= 3)
        {
            text[position++] = static_cast<char>('0' + ((mode >> shift) & 7));
        }
        text[position] = '\0';
        stream.write(text);
    }

    void print_node(Context& context, const tinyos::kernel::vfs::Node* node)
    {
        context.out.write(node != nullptr && node->directory ? "[dir]  " : "[file] ");
        context.out.write(node != nullptr && node->name != nullptr ? node->name : "invalid");

        if (node != nullptr && !node->directory)
        {
            context.out.write(" (");
            write_unsigned(context.out, node->size);
            context.out.write(node->writable ? " bytes, writable" : " bytes");
            context.out.write(")");
        }

        context.out.put_char('\n');
    }

    // Recursive listing with an explicit depth ceiling. The old print_tree() had
    // no limit, so a cycle in the node graph hung instead of reporting an error
    // (defect D23).
    void print_tree(Context& context, const tinyos::kernel::vfs::Node* node, size_t depth)
    {
        constexpr size_t MaxDepth = 16;

        if (node == nullptr)
        {
            return;
        }

        if (depth > MaxDepth)
        {
            context.err.line("fsmap: maximum depth reached, stopping");
            return;
        }

        for (size_t index = 0; index < depth; ++index)
        {
            context.out.write("  ");
        }

        context.out.write(node->name != nullptr ? node->name : "invalid");
        if (node->directory)
        {
            context.out.write("/");
        }

        context.out.put_char('\n');
        if (!node->directory)
        {
            return;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            for (size_t index = 0; index <= depth; ++index)
            {
                context.out.write("  ");
            }
            context.out.line("<permission denied>");
            return;
        }

        for (size_t index = 0; index < tinyos::kernel::vfs::child_count(node); ++index)
        {
            print_tree(context, tinyos::kernel::vfs::child_at(node, index), depth + 1);
        }
    }

    Status cmd_files(Context& context)
    {
        char path[PathBuffer];

        // No argument lists the working directory.
        const char* target = context.argc > 1 ? resolve_arg(context, 1, path, sizeof(path)) : context.cwd();
        if (target == nullptr)
        {
            context.err.line("files: invalid path");
            return Status::Usage;
        }

        const auto* node = tinyos::kernel::vfs::find(target);
        if (node == nullptr)
        {
            context.err.write(target);
            context.err.line(": not found");
            return Status::NotFound;
        }

        if (!node->directory)
        {
            print_node(context, node);
            return Status::Ok;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            context.err.write(target);
            context.err.line(": permission denied");
            return Status::Denied;
        }

        for (size_t index = 0; index < tinyos::kernel::vfs::child_count(node); ++index)
        {
            print_node(context, tinyos::kernel::vfs::child_at(node, index));
        }

        return Status::Ok;
    }

    Status cmd_fsmap(Context& context)
    {
        const auto* root = tinyos::kernel::vfs::root();
        if (root == nullptr)
        {
            context.err.line("fsmap: VFS not ready");
            return Status::Io;
        }

        print_tree(context, root, 0);
        return Status::Ok;
    }

    Status cmd_show(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("usage: show <path>");
            return Status::Usage;
        }

        const auto* node = tinyos::kernel::vfs::find(target);
        const char* data = nullptr;
        size_t size = 0;
        if (!tinyos::kernel::vfs::read_file(node, data, size))
        {
            context.err.write(target);
            context.err.line(": not readable");
            return Status::Io;
        }

        context.out.write(data, size);
        if (size == 0 || data[size - 1] != '\n')
        {
            context.out.put_char('\n');
        }

        return Status::Ok;
    }

    Status cmd_describe(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("usage: describe <path>");
            return Status::Usage;
        }

        const auto* node = tinyos::kernel::vfs::find(target);
        if (node == nullptr)
        {
            context.err.write(target);
            context.err.line(": not found");
            return Status::NotFound;
        }

        context.out.write("Name     : ");
        context.out.line(node->name != nullptr ? node->name : "invalid");
        context.out.write("Type     : ");
        context.out.line(node->directory ? "directory" : "file");
        context.out.write("Size     : ");
        write_unsigned(context.out, node->size);
        context.out.put_char('\n');
        context.out.write("Mode     : ");
        write_mode(context.out, tinyos::kernel::vfs::access_mode(node));
        context.out.put_char('\n');
        context.out.write("Writable : ");
        context.out.line(node->writable ? "yes" : "no");

        // Ownership is not modelled yet; saying so is better than implying 0 is
        // meaningful (defect D1 tracks the missing uid/gid).
        context.out.line("Owner    : not tracked yet (no uid/gid on VFS nodes)");

        return Status::Ok;
    }

    Status cmd_mkdir(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("usage: mkdir <path>");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::create_directory(target))
        {
            context.err.write(target);
            context.err.line(": cannot create directory");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_touch(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("usage: touch <path>");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::create_file(target))
        {
            context.err.write(target);
            context.err.line(": cannot create file");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_write(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr || context.argc < 3)
        {
            context.err.line("usage: write <path> <text>");
            return Status::Usage;
        }

        // Concatenate the remaining arguments with single spaces, so
        // `write /notes.txt hello world` stores "hello world".
        char text[MaxWriteText];
        size_t used = 0;
        for (int index = 2; index < context.argc; ++index)
        {
            const char* argument = context.argv[index];
            const size_t length = tinyos::libk::text::length(argument);
            if (used + length + 2 >= sizeof(text))
            {
                context.err.line("write: text too long");
                return Status::Usage;
            }

            if (used != 0)
            {
                text[used++] = ' ';
            }

            for (size_t offset = 0; offset < length; ++offset)
            {
                text[used++] = argument[offset];
            }
        }
        text[used] = '\0';

        if (!tinyos::kernel::vfs::write_file(target, text, used))
        {
            context.err.write(target);
            context.err.line(": cannot write");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_copy(Context& context)
    {
        char source[PathBuffer];
        char destination[PathBuffer];

        const char* from = resolve_arg(context, 1, source, sizeof(source));
        const char* to = resolve_arg(context, 2, destination, sizeof(destination));
        if (from == nullptr || to == nullptr)
        {
            context.err.line("usage: copy <source> <destination>");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::copy_file(from, to))
        {
            context.err.line("copy: failed");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_move(Context& context)
    {
        char source[PathBuffer];
        char destination[PathBuffer];

        const char* from = resolve_arg(context, 1, source, sizeof(source));
        const char* to = resolve_arg(context, 2, destination, sizeof(destination));
        if (from == nullptr || to == nullptr)
        {
            context.err.line("usage: move <source> <destination>");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::move(from, to))
        {
            context.err.line("move: failed");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_remove(Context& context)
    {
        char path[PathBuffer];
        const char* target = resolve_arg(context, 1, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("usage: remove <path>");
            return Status::Usage;
        }

        // Removing the root would detach the whole tree; refuse explicitly.
        if (tinyos::libk::text::equals(target, "/"))
        {
            context.err.line("remove: refusing to remove the root directory");
            return Status::Denied;
        }

        if (!tinyos::kernel::vfs::remove(target))
        {
            context.err.write(target);
            context.err.line(": cannot remove");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_chmod(Context& context)
    {
        char path[PathBuffer];
        if (context.argc < 3)
        {
            context.err.line("usage: chmod <octal-mode> <path>");
            return Status::Usage;
        }

        uint16_t mode = 0;
        if (!tinyos::libk::text::parse_octal(context.argv[1], mode))
        {
            context.err.write(context.argv[1]);
            context.err.line(": invalid octal mode");
            return Status::Usage;
        }

        const char* target = resolve_arg(context, 2, path, sizeof(path));
        if (target == nullptr)
        {
            context.err.line("chmod: invalid path");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::set_access_mode(target, mode))
        {
            context.err.write(target);
            context.err.line(": cannot change mode");
            return Status::Io;
        }

        return Status::Ok;
    }

    Status cmd_pwd(Context& context)
    {
        context.out.line(context.cwd());
        return Status::Ok;
    }

    // Changes the session's working directory.
    //
    // This previously reported "needs the session object (M2c)" because Context
    // carried the cwd by value. Context now holds a Session pointer, so the
    // command does the real job: resolve the argument, verify the target is a
    // directory the caller may enter, and move the session.
    Status cmd_cd(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("cd: no session attached");
            return Status::Failed;
        }

        char path[PathBuffer];

        // No argument goes to $HOME, falling back to the root.
        const char* raw = context.argc > 1 ? context.argv[1] : context.session->env.get("HOME");
        if (raw == nullptr || raw[0] == '\0')
        {
            raw = "/";
        }

        if (!tinyos::libk::path::resolve(context.cwd(), raw, path, sizeof(path)))
        {
            context.err.write(raw);
            context.err.line(": invalid path");
            return Status::Usage;
        }

        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr)
        {
            context.err.write(path);
            context.err.line(": not found");
            return Status::NotFound;
        }

        if (!node->directory)
        {
            context.err.write(path);
            context.err.line(": not a directory");
            return Status::Usage;
        }

        if (!tinyos::kernel::vfs::can_enter_directory(node))
        {
            context.err.write(path);
            context.err.line(": permission denied");
            return Status::Denied;
        }

        tinyos::libshell::set_session_cwd(*context.session, path);
        return Status::Ok;
    }

    Status cmd_set(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("set: no session attached");
            return Status::Failed;
        }

        // No arguments lists the environment, which is more useful than a usage
        // error and matches what `env` does.
        if (context.argc == 1)
        {
            for (size_t index = 0; index < context.session->env.count(); ++index)
            {
                const char* name = context.session->env.name_at(index);
                const char* value = context.session->env.value_at(index);
                if (name == nullptr)
                {
                    continue;
                }

                context.out.write(name);
                context.out.put_char('=');
                context.out.line(value != nullptr ? value : "");
            }

            return Status::Ok;
        }

        // Accept both `set NAME value` and `set NAME=value`.
        const char* assignment = context.argv[1];
        const char* equals = tinyos::libk::text::find(assignment, "=");
        if (equals != nullptr)
        {
            char name[tinyos::libshell::MaxEnvName];
            const size_t name_length = static_cast<size_t>(equals - assignment);
            if (name_length >= sizeof(name))
            {
                context.err.line("set: variable name too long");
                return Status::Usage;
            }

            for (size_t index = 0; index < name_length; ++index)
            {
                name[index] = assignment[index];
            }
            name[name_length] = '\0';

            if (!context.session->env.set(name, equals + 1))
            {
                context.err.line("set: cannot store variable");
                return Status::Failed;
            }

            return Status::Ok;
        }

        if (context.argc < 3)
        {
            context.err.line("usage: set <name> <value>  or  set <name>=<value>");
            return Status::Usage;
        }

        if (!context.session->env.set(context.argv[1], context.argv[2]))
        {
            context.err.line("set: cannot store variable");
            return Status::Failed;
        }

        return Status::Ok;
    }

    Status cmd_unset(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("unset: no session attached");
            return Status::Failed;
        }

        if (context.argc < 2)
        {
            context.err.line("usage: unset <name>");
            return Status::Usage;
        }

        // Unsetting a variable that does not exist is not an error, matching
        // shell convention.
        context.session->env.unset(context.argv[1]);
        return Status::Ok;
    }

    const char* const FilesAliases[] = { "ls", "dir", nullptr };
    const char* const ShowAliases[] = { "cat", nullptr };

    const tinyos::libshell::Command FsCommands[] = {
        { "files", FilesAliases, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_files, "list a directory", "files [path]", "files /system" },
        { "fsmap", nullptr, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_fsmap, "show the whole VFS tree", "fsmap", "fsmap" },
        { "show", ShowAliases, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_show, "print a file", "show <path>", "show /readme.txt" },
        { "describe", nullptr, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_describe, "show node metadata", "describe <path>", "describe /system" },
        { "mkdir", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_mkdir, "create a directory", "mkdir <path>", "mkdir /system/logs" },
        { "touch", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_touch, "create an empty file", "touch <path>", "touch /notes.txt" },
        { "write", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_write, "overwrite a file with text", "write <path> <text>", "write /notes.txt hello" },
        { "copy", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_copy, "copy a file", "copy <source> <destination>", "copy /a.txt /b.txt" },
        { "move", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_move, "move or rename a file", "move <source> <destination>", "move /a.txt /b.txt" },
        { "remove", nullptr, Category::Files, Availability::Real, Risk::Dangerous, CapabilityFileWrite,
            cmd_remove, "remove a file or empty directory", "remove <path>", "remove /notes.txt" },
        { "chmod", nullptr, Category::Files, Availability::Real, Risk::WritesState, CapabilityFileWrite,
            cmd_chmod, "change an access mode", "chmod <octal-mode> <path>", "chmod 0644 /notes.txt" },
        { "pwd", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_pwd, "print the working directory", "pwd", "pwd" },
        { "cd", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_cd, "change the working directory", "cd [path]", "cd /system" },
        { "set", nullptr, Category::Shell, Availability::Real, Risk::WritesState, CapabilityNone,
            cmd_set, "set or list environment variables", "set [name value]", "set EDITOR nano" },
        { "unset", nullptr, Category::Shell, Availability::Real, Risk::WritesState, CapabilityNone,
            cmd_unset, "remove an environment variable", "unset <name>", "unset EDITOR" }
    };
}

namespace tinyos::shell::commands
{
    void register_fs_commands()
    {
        tinyos::libshell::register_table(FsCommands, sizeof(FsCommands) / sizeof(FsCommands[0]));
    }
}
