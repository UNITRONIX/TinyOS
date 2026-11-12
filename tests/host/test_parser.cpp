// Host unit tests for the shell command language parser.
//
// The previous shell had no parser at all: it isolated one command word and
// passed the raw remainder to a 102-branch if/else chain, so pipes,
// redirection, quoting, sequencing and exit codes did not exist. These tests
// pin the grammar in libshell/parser.hpp.

#include <cstdio>
#include <cstring>

#include <tinyos/libshell/parser.hpp>

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

    using tinyos::libshell::ParsedList;
    using tinyos::libshell::ParseStatus;
    using tinyos::libshell::Parser;
    using tinyos::libshell::TokenKind;

    const char* lookup_env(const char* name, void* user)
    {
        const auto* table = static_cast<const char* const*>(user);
        if (table == nullptr)
        {
            return nullptr;
        }

        for (size_t index = 0; table[index] != nullptr; index += 2)
        {
            if (std::strcmp(table[index], name) == 0)
            {
                return table[index + 1];
            }
        }

        return nullptr;
    }

    // Parses and returns the first command of the first pipeline, or nullptr.
    const tinyos::libshell::ParsedCommand* first_command(Parser& parser, ParsedList& list, const char* line,
        ParseStatus& status, const char* const* env = nullptr)
    {
        status = parser.parse(line, list, env != nullptr ? lookup_env : nullptr,
            const_cast<void*>(static_cast<const void*>(env)));
        if (status != ParseStatus::Ok || list.pipeline_count == 0 || list.pipelines[0].command_count == 0)
        {
            return nullptr;
        }

        return &list.pipelines[0].commands[0];
    }

    void test_simple_commands()
    {
        std::printf("simple commands\n");

        Parser parser;
        ParsedList list;
        ParseStatus status = ParseStatus::Ok;

        const auto* command = first_command(parser, list, "echo hello world", status);
        check(command != nullptr, "simple command parses");
        check(command != nullptr && command->argc == 3, "three arguments");
        check(command != nullptr && std::strcmp(command->argv[0], "echo") == 0, "argv[0] is name");
        check(command != nullptr && std::strcmp(command->argv[2], "world") == 0, "argv[2] correct");

        command = first_command(parser, list, "   ls   ", status);
        check(command != nullptr && command->argc == 1, "leading and trailing spaces ignored");

        command = first_command(parser, list, "cmd", status);
        check(command != nullptr && command->argc == 1, "single word");
    }

    void test_empty_and_invalid()
    {
        std::printf("empty and invalid\n");

        Parser parser;
        ParsedList list;

        check(parser.parse("", list) == ParseStatus::Empty, "empty line");
        check(parser.parse("   \t  ", list) == ParseStatus::Empty, "whitespace only");
        check(parser.parse(nullptr, list) == ParseStatus::Empty, "null line");

        check(parser.parse("echo 'unterminated", list) == ParseStatus::UnterminatedQuote, "unterminated single quote");
        check(parser.parse("echo \"unterminated", list) == ParseStatus::UnterminatedQuote, "unterminated double quote");
        check(parser.parse("echo |", list) == ParseStatus::TrailingPipe, "trailing pipe");
        check(parser.parse("| echo", list) == ParseStatus::EmptyCommand, "leading pipe");
        check(parser.parse("echo >", list) == ParseStatus::MissingRedirectTarget, "trailing redirect");
        check(parser.parse("echo ;", list) == ParseStatus::EmptyCommand, "trailing sequence");
        check(parser.parse("echo \\", list) == ParseStatus::UnsupportedCharacter, "trailing backslash");
    }

    void test_quoting()
    {
        std::printf("quoting\n");

        Parser parser;
        ParsedList list;
        ParseStatus status = ParseStatus::Ok;

        const auto* command = first_command(parser, list, "echo 'hello world'", status);
        check(command != nullptr && command->argc == 2, "single quotes make one argument");
        check(command != nullptr && std::strcmp(command->argv[1], "hello world") == 0, "single quoted content");

        command = first_command(parser, list, "echo \"a b\"", status);
        check(command != nullptr && command->argc == 2, "double quotes make one argument");
        check(command != nullptr && std::strcmp(command->argv[1], "a b") == 0, "double quoted content");

        command = first_command(parser, list, "echo a\"b\"c", status);
        check(command != nullptr && std::strcmp(command->argv[1], "abc") == 0, "quotes concatenate");

        command = first_command(parser, list, "echo 'a|b;c'", status);
        check(command != nullptr && command->argc == 2, "operators inside quotes are literal");
        check(command != nullptr && std::strcmp(command->argv[1], "a|b;c") == 0, "quoted operators preserved");

        command = first_command(parser, list, "echo a\\ b", status);
        check(command != nullptr && std::strcmp(command->argv[1], "a b") == 0, "escaped space");
    }

    void test_variable_expansion()
    {
        std::printf("variable expansion\n");

        const char* const env[] = { "USER", "alice", "HOME", "/home/alice", nullptr };
        Parser parser;
        ParsedList list;
        ParseStatus status = ParseStatus::Ok;

        const auto* command = first_command(parser, list, "echo $USER", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "alice") == 0, "bare expansion");

        command = first_command(parser, list, "echo ${no}", status, env);
        // There is no brace syntax: '$' followed by '{' is not a variable name,
        // so the dollar stays literal and the whole word is unchanged. This is
        // the documented subset, not an accident.
        check(command != nullptr && std::strcmp(command->argv[1], "${no}") == 0,
            "braces are not expansion syntax");

        command = first_command(parser, list, "echo pre$USER", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "prealice") == 0, "prefix expansion");

        command = first_command(parser, list, "echo $UNSET", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "") == 0, "unset expands to empty");

        command = first_command(parser, list, "echo '$USER'", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "$USER") == 0, "single quotes suppress expansion");

        command = first_command(parser, list, "echo \"$HOME/x\"", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "/home/alice/x") == 0, "expansion inside quotes");

        command = first_command(parser, list, "echo a$", status, env);
        check(command != nullptr && std::strcmp(command->argv[1], "a$") == 0, "lone dollar is literal");
    }

    void test_pipelines()
    {
        std::printf("pipelines\n");

        Parser parser;
        ParsedList list;

        check(parser.parse("a | b | c", list) == ParseStatus::Ok, "three-stage pipeline parses");
        check(list.pipeline_count == 1, "one pipeline in list");
        check(list.pipelines[0].command_count == 3, "three commands in pipeline");
        check(std::strcmp(list.pipelines[0].commands[0].argv[0], "a") == 0, "stage 0");
        check(std::strcmp(list.pipelines[0].commands[2].argv[0], "c") == 0, "stage 2");

        check(parser.parse("a|b", list) == ParseStatus::Ok, "pipeline without spaces");
        check(list.pipelines[0].command_count == 2, "pipe splits without spaces");

        check(parser.parse("a | b c | d", list) == ParseStatus::Ok, "pipeline with arguments");
        check(list.pipelines[0].commands[1].argc == 2, "stage keeps its arguments");
    }

    void test_sequencing_and_exit_codes()
    {
        std::printf("sequencing\n");

        Parser parser;
        ParsedList list;

        check(parser.parse("a ; b", list) == ParseStatus::Ok, "sequence parses");
        check(list.pipeline_count == 2, "sequence yields two pipelines");
        check(list.separators[1] == TokenKind::Sequence, "sequence separator recorded");

        check(parser.parse("a && b", list) == ParseStatus::Ok, "and parses");
        check(list.pipeline_count == 2 && list.separators[1] == TokenKind::And, "and separator");

        check(parser.parse("a || b", list) == ParseStatus::Ok, "or parses");
        check(list.pipeline_count == 2 && list.separators[1] == TokenKind::Or, "or separator");

        check(parser.parse("a && b || c ; d", list) == ParseStatus::Ok, "mixed operators");
        check(list.pipeline_count == 4, "four pipelines");
        check(list.separators[1] == TokenKind::And, "first operator");
        check(list.separators[2] == TokenKind::Or, "second operator");
        check(list.separators[3] == TokenKind::Sequence, "third operator");

        check(parser.parse("a & b", list) == ParseStatus::Ok, "single ampersand is part of a word");
    }

    void test_redirection()
    {
        std::printf("redirection\n");

        Parser parser;
        ParsedList list;

        check(parser.parse("echo hi > out.txt", list) == ParseStatus::Ok, "output redirect parses");
        const auto& command = list.pipelines[0].commands[0];
        check(command.argc == 2, "redirect target is not an argument");
        check(command.redirect_out != nullptr && std::strcmp(command.redirect_out, "out.txt") == 0,
            "redirect target captured");
        check(!command.append_out, "plain redirect does not append");

        check(parser.parse("echo hi >> log.txt", list) == ParseStatus::Ok, "append redirect parses");
        check(list.pipelines[0].commands[0].append_out, "append flag set");

        check(parser.parse("cat < in.txt", list) == ParseStatus::Ok, "input redirect parses");
        check(list.pipelines[0].commands[0].redirect_in != nullptr, "input target captured");

        check(parser.parse("cmd 2> err.txt", list) == ParseStatus::Ok, "stderr redirect parses");
        check(list.pipelines[0].commands[0].redirect_error != nullptr, "stderr target captured");
        check(list.pipelines[0].commands[0].argc == 1, "stderr target not an argument");

        check(parser.parse("a | b > out", list) == ParseStatus::Ok, "redirect after pipeline stage");
        check(list.pipelines[0].commands[1].redirect_out != nullptr, "second stage redirect captured");
    }

    void test_limits_are_errors()
    {
        std::printf("limits\n");

        Parser parser;
        ParsedList list;

        // Too many arguments in one command: 40 words against a 32-slot argv.
        // The count stays below MaxTokens (64) so this exercises the argv limit
        // rather than the token limit.
        char many[256];
        size_t position = 0;
        for (int index = 0; index < 40; ++index)
        {
            many[position++] = 'w';
            many[position++] = ' ';
        }
        many[position] = '\0';
        check(parser.parse(many, list) == ParseStatus::TooManyArgs, "too many arguments rejected");

        // Just under the limit must still succeed, so the ceiling is not
        // accidentally lowered below what real commands need.
        char fits[256];
        position = 0;
        for (int index = 0; index < 20; ++index)
        {
            fits[position++] = 'w';
            fits[position++] = ' ';
        }
        fits[position] = '\0';
        check(parser.parse(fits, list) == ParseStatus::Ok, "twenty arguments accepted");

        // Over-long line is refused, not clipped.
        char long_line[tinyos::libshell::MaxLineBytes + 8];
        for (size_t index = 0; index < sizeof(long_line) - 1; ++index)
        {
            long_line[index] = 'a';
        }
        long_line[sizeof(long_line) - 1] = '\0';
        check(parser.parse(long_line, list) == ParseStatus::WordTooLong, "over-long line rejected");
    }

    void test_status_names()
    {
        check(std::strcmp(tinyos::libshell::parse_status_name(ParseStatus::Ok), "ok") == 0, "ok name");
        check(tinyos::libshell::parse_status_name(ParseStatus::TrailingPipe)[0] != '\0', "error names present");
    }
}

int main()
{
    test_simple_commands();
    test_empty_and_invalid();
    test_quoting();
    test_variable_expansion();
    test_pipelines();
    test_sequencing_and_exit_codes();
    test_redirection();
    test_limits_are_errors();
    test_status_names();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
