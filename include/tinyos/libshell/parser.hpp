#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/libshell/stream.hpp>

namespace tinyos::libshell
{
    // Shell command language parser.
    //
    // The previous shell never parsed anything: it split the input line into a
    // command word and a raw remainder, then dispatched through a 102-branch
    // if/else chain. Pipes, redirection, quoting, sequencing and exit codes were
    // all absent, which is why every command had to invent its own argument
    // handling (defect D24).
    //
    // This parser is deliberately free of kernel dependencies so it can be
    // exhaustively tested on the host.
    //
    // Supported grammar:
    //
    //   list      := pipeline ((';' | '&&' | '||') pipeline)*
    //   pipeline  := command ('|' command)*
    //   command   := word+ redirect*
    //   redirect  := ('>' | '>>' | '<' | '2>') word
    //   word      := sequence of unquoted chars, single-quoted or double-quoted
    //                sections, with $NAME expanded to the environment
    //
    // Nothing about this grammar requires a tree: a list is a flat sequence of
    // (operator, pipeline) pairs and a pipeline is a flat sequence of commands.

    enum class TokenKind : uint8_t
    {
        Word,
        Pipe,
        Sequence,
        And,
        Or,
        RedirectOut,
        RedirectAppend,
        RedirectIn,
        RedirectError
    };

    struct Token
    {
        TokenKind kind;

        // For Word: slice of the parser's internal word buffer. For operators:
        // unused.
        const char* text;
        size_t length;
    };

    // A single command: its argv plus its redirections.
    struct ParsedCommand
    {
        // argv[0] is the command name. Pointers into the parser's word buffer.
        //
        // 32 slots is deliberate: the first implementation used 16 and the
        // "too many arguments" test showed legitimate invocations (a long
        // `write <path> <text>` line) hitting the ceiling. Rejecting a valid
        // command is a worse failure than a slightly larger table.
        const char* argv[32];
        int argc;

        const char* redirect_out;
        const char* redirect_in;
        const char* redirect_error;
        bool append_out;
    };

    // A pipeline is one or more commands joined by '|'.
    struct ParsedPipeline
    {
        ParsedCommand commands[4];
        int command_count;
    };

    // A list is pipelines joined by ';', '&&' or '||'. The operator stored at
    // `separators[i]` precedes `pipelines[i + 1]`.
    struct ParsedList
    {
        ParsedPipeline pipelines[8];
        TokenKind separators[8];
        int pipeline_count;
    };

    // Parser limits. Exceeding any of these is a syntax error rather than a
    // silent truncation, because a truncated command line that still runs is
    // worse than a rejected one.
    inline constexpr size_t MaxLineBytes = 256;
    inline constexpr size_t MaxTokens = 64;
    inline constexpr size_t MaxWordBytes = 512;

    enum class ParseStatus : uint8_t
    {
        Ok,
        Empty,
        UnterminatedQuote,
        TooManyTokens,
        WordTooLong,
        TooManyArgs,
        TooManyCommands,
        TooManyPipelines,
        TrailingPipe,
        EmptyCommand,
        MissingRedirectTarget,
        UnsupportedCharacter
    };

    const char* parse_status_name(ParseStatus status);

    // Environment lookup used for $NAME expansion. Returns nullptr when the
    // variable is unset; the reference is then replaced with an empty string.
    using EnvLookup = const char* (*)(const char* name, void* user);

    // Parses `line` into `list`. The parser owns fixed internal buffers that
    // live inside this object, so the resulting ParsedList pointers stay valid
    // as long as the Parser instance does.
    class Parser
    {
    public:
        Parser();

        ParseStatus parse(const char* line, ParsedList& list, EnvLookup lookup = nullptr, void* user = nullptr);

        // Replaces the expansion buffer contents. Exposed so tests can inspect
        // what a word expanded to.
        const char* word_buffer() const { return m_words; }

    private:
        ParseStatus tokenize(const char* line, EnvLookup lookup, void* user);
        ParseStatus build(ParsedList& list);

        Token m_tokens[MaxTokens];
        size_t m_token_count = 0;

        // Expanded word text. All Token::text pointers point here.
        char m_words[MaxWordBytes];
        size_t m_word_bytes = 0;
    };
}
