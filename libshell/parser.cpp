#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/parser.hpp>

namespace
{
    using tinyos::libshell::ParseStatus;
    using tinyos::libshell::TokenKind;
    using tinyos::libshell::MaxTokens;
    using tinyos::libshell::MaxWordBytes;

    bool is_space(char value)
    {
        return value == ' ' || value == '\t';
    }

    bool is_name_start(char value)
    {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
    }

    bool is_name_char(char value)
    {
        return is_name_start(value) || (value >= '0' && value <= '9');
    }
}

namespace tinyos::libshell
{
    const char* parse_status_name(ParseStatus status)
    {
        switch (status)
        {
        case ParseStatus::Ok:
            return "ok";
        case ParseStatus::Empty:
            return "empty command line";
        case ParseStatus::UnterminatedQuote:
            return "unterminated quote";
        case ParseStatus::TooManyTokens:
            return "too many tokens";
        case ParseStatus::WordTooLong:
            return "word too long";
        case ParseStatus::TooManyArgs:
            return "too many arguments";
        case ParseStatus::TooManyCommands:
            return "too many commands in pipeline";
        case ParseStatus::TooManyPipelines:
            return "too many pipelines in list";
        case ParseStatus::TrailingPipe:
            return "trailing pipe";
        case ParseStatus::EmptyCommand:
            return "empty command";
        case ParseStatus::MissingRedirectTarget:
            return "missing redirect target";
        case ParseStatus::UnsupportedCharacter:
            return "unsupported character";
        }

        return "parse error";
    }

    Parser::Parser()
    {
        m_words[0] = '\0';
    }

    ParseStatus Parser::parse(const char* line, ParsedList& list, EnvLookup lookup, void* user)
    {
        m_token_count = 0;
        m_word_bytes = 0;
        m_words[0] = '\0';

        if (line == nullptr)
        {
            return ParseStatus::Empty;
        }

        // Reject over-long lines outright instead of clipping them: a clipped
        // command line is still executed, which is worse than refusing it.
        if (tinyos::libk::text::length(line) >= MaxLineBytes)
        {
            return ParseStatus::WordTooLong;
        }

        const ParseStatus tokenized = tokenize(line, lookup, user);
        if (tokenized != ParseStatus::Ok)
        {
            return tokenized;
        }

        if (m_token_count == 0)
        {
            return ParseStatus::Empty;
        }

        return build(list);
    }

    ParseStatus Parser::tokenize(const char* line, EnvLookup lookup, void* user)
    {
        auto push_token = [this](TokenKind kind, const char* text, size_t length) -> bool {
            if (m_token_count >= MaxTokens)
            {
                return false;
            }

            m_tokens[m_token_count].kind = kind;
            m_tokens[m_token_count].text = text;
            m_tokens[m_token_count].length = length;
            ++m_token_count;
            return true;
        };

        auto push_operator = [&push_token](TokenKind kind) -> bool {
            return push_token(kind, nullptr, 0);
        };

        // Appends one byte to the current word, refusing to overflow the shared
        // buffer.
        auto append_byte = [this](char value) -> bool {
            if (m_word_bytes + 1 >= MaxWordBytes)
            {
                return false;
            }

            m_words[m_word_bytes] = value;
            ++m_word_bytes;
            m_words[m_word_bytes] = '\0';
            return true;
        };

        const char* cursor = line;
        bool in_word = false;
        size_t word_start = 0;

        auto begin_word = [&]() {
            if (!in_word)
            {
                in_word = true;
                word_start = m_word_bytes;
            }
        };

        auto end_word = [&]() -> ParseStatus {
            if (!in_word)
            {
                return ParseStatus::Ok;
            }

            in_word = false;
            const char* text = m_words + word_start;
            const size_t length = m_word_bytes - word_start;

            // Terminate this word so it can be treated as a C string by argv.
            if (m_word_bytes + 1 >= MaxWordBytes)
            {
                return ParseStatus::WordTooLong;
            }
            m_words[m_word_bytes] = '\0';
            ++m_word_bytes;
            m_words[m_word_bytes] = '\0';

            if (!push_token(TokenKind::Word, text, length))
            {
                return ParseStatus::TooManyTokens;
            }

            return ParseStatus::Ok;
        };

        while (*cursor != '\0')
        {
            const char value = *cursor;

            if (is_space(value))
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                ++cursor;
                continue;
            }

            // Operators. Checked before words so "a|b" splits without spaces.
            if (value == '|')
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (cursor[1] == '|')
                {
                    if (!push_operator(TokenKind::Or))
                    {
                        return ParseStatus::TooManyTokens;
                    }
                    cursor += 2;
                }
                else
                {
                    if (!push_operator(TokenKind::Pipe))
                    {
                        return ParseStatus::TooManyTokens;
                    }
                    ++cursor;
                }
                continue;
            }

            if (value == '&' && cursor[1] == '&')
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (!push_operator(TokenKind::And))
                {
                    return ParseStatus::TooManyTokens;
                }
                cursor += 2;
                continue;
            }

            if (value == ';')
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (!push_operator(TokenKind::Sequence))
                {
                    return ParseStatus::TooManyTokens;
                }
                ++cursor;
                continue;
            }

            if (value == '>')
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (cursor[1] == '>')
                {
                    if (!push_operator(TokenKind::RedirectAppend))
                    {
                        return ParseStatus::TooManyTokens;
                    }
                    cursor += 2;
                }
                else
                {
                    if (!push_operator(TokenKind::RedirectOut))
                    {
                        return ParseStatus::TooManyTokens;
                    }
                    ++cursor;
                }
                continue;
            }

            if (value == '<')
            {
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (!push_operator(TokenKind::RedirectIn))
                {
                    return ParseStatus::TooManyTokens;
                }
                ++cursor;
                continue;
            }

            if (value == '2' && cursor[1] == '>')
            {
                // "2>" is stderr redirection. It must be recognised before the
                // digit is treated as the start of a word.
                const ParseStatus status = end_word();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }

                if (!push_operator(TokenKind::RedirectError))
                {
                    return ParseStatus::TooManyTokens;
                }
                cursor += 2;
                continue;
            }

            if (value == '\'')
            {
                // Single quotes: literal, no expansion.
                begin_word();
                ++cursor;
                bool closed = false;
                while (*cursor != '\0')
                {
                    if (*cursor == '\'')
                    {
                        closed = true;
                        ++cursor;
                        break;
                    }

                    if (!append_byte(*cursor))
                    {
                        return ParseStatus::WordTooLong;
                    }
                    ++cursor;
                }

                if (!closed)
                {
                    return ParseStatus::UnterminatedQuote;
                }
                continue;
            }

            if (value == '"')
            {
                // Double quotes: literal except $NAME expansion.
                begin_word();
                ++cursor;
                bool closed = false;
                while (*cursor != '\0')
                {
                    if (*cursor == '"')
                    {
                        closed = true;
                        ++cursor;
                        break;
                    }

                    if (*cursor == '$')
                    {
                        ++cursor;
                        const char* name_start = cursor;
                        size_t name_length = 0;
                        if (is_name_start(*cursor))
                        {
                            while (is_name_char(*cursor))
                            {
                                ++cursor;
                                ++name_length;
                            }

                            char name[64];
                            if (name_length >= sizeof(name))
                            {
                                return ParseStatus::WordTooLong;
                            }

                            for (size_t index = 0; index < name_length; ++index)
                            {
                                name[index] = name_start[index];
                            }
                            name[name_length] = '\0';

                            const char* replacement = lookup != nullptr ? lookup(name, user) : nullptr;
                            if (replacement != nullptr)
                            {
                                for (size_t index = 0; replacement[index] != '\0'; ++index)
                                {
                                    if (!append_byte(replacement[index]))
                                    {
                                        return ParseStatus::WordTooLong;
                                    }
                                }
                            }

                            continue;
                        }

                        // A lone '$' is literal.
                        if (!append_byte('$'))
                        {
                            return ParseStatus::WordTooLong;
                        }
                        continue;
                    }

                    if (!append_byte(*cursor))
                    {
                        return ParseStatus::WordTooLong;
                    }
                    ++cursor;
                }

                if (!closed)
                {
                    return ParseStatus::UnterminatedQuote;
                }
                continue;
            }

            if (value == '\\')
            {
                begin_word();
                ++cursor;
                if (*cursor == '\0')
                {
                    // A trailing backslash is an error rather than a silent drop.
                    return ParseStatus::UnsupportedCharacter;
                }

                if (!append_byte(*cursor))
                {
                    return ParseStatus::WordTooLong;
                }
                ++cursor;
                continue;
            }

            if (value == '$')
            {
                begin_word();
                ++cursor;
                if (is_name_start(*cursor))
                {
                    const char* name_start = cursor;
                    size_t name_length = 0;
                    while (is_name_char(*cursor))
                    {
                        ++cursor;
                        ++name_length;
                    }

                    char name[64];
                    if (name_length >= sizeof(name))
                    {
                        return ParseStatus::WordTooLong;
                    }

                    for (size_t index = 0; index < name_length; ++index)
                    {
                        name[index] = name_start[index];
                    }
                    name[name_length] = '\0';

                    const char* replacement = lookup != nullptr ? lookup(name, user) : nullptr;
                    if (replacement != nullptr)
                    {
                        for (size_t index = 0; replacement[index] != '\0'; ++index)
                        {
                            if (!append_byte(replacement[index]))
                            {
                                return ParseStatus::WordTooLong;
                            }
                        }
                    }

                    continue;
                }

                if (!append_byte('$'))
                {
                    return ParseStatus::WordTooLong;
                }
                continue;
            }

            begin_word();
            if (!append_byte(value))
            {
                return ParseStatus::WordTooLong;
            }
            ++cursor;
        }

        return end_word();
    }

    ParseStatus Parser::build(ParsedList& list)
    {
        list.pipeline_count = 0;
        list.separators[0] = TokenKind::Sequence;
        list.pipelines[0].command_count = 0;

        ParsedPipeline* pipeline = &list.pipelines[0];
        ParsedCommand* command = nullptr;

        auto start_command = [&]() -> ParseStatus {
            if (pipeline->command_count >= static_cast<int>(sizeof(pipeline->commands) / sizeof(pipeline->commands[0])))
            {
                return ParseStatus::TooManyCommands;
            }

            command = &pipeline->commands[pipeline->command_count];
            command->argc = 0;
            command->redirect_out = nullptr;
            command->redirect_in = nullptr;
            command->redirect_error = nullptr;
            command->append_out = false;
            ++pipeline->command_count;
            return ParseStatus::Ok;
        };

        auto start_pipeline = [&](TokenKind separator) -> ParseStatus {
            if (list.pipeline_count + 1 >= static_cast<int>(sizeof(list.pipelines) / sizeof(list.pipelines[0])))
            {
                return ParseStatus::TooManyPipelines;
            }

            ++list.pipeline_count;
            list.separators[list.pipeline_count] = separator;
            pipeline = &list.pipelines[list.pipeline_count];
            pipeline->command_count = 0;
            return start_command();
        };

        ParseStatus status = start_command();
        if (status != ParseStatus::Ok)
        {
            return status;
        }

        for (size_t index = 0; index < m_token_count; ++index)
        {
            const Token& token = m_tokens[index];

            switch (token.kind)
            {
            case TokenKind::Word:
            {
                // A word following a redirect operator is its target.
                if (index > 0)
                {
                    const TokenKind previous = m_tokens[index - 1].kind;
                    if (previous == TokenKind::RedirectOut || previous == TokenKind::RedirectAppend)
                    {
                        command->redirect_out = token.text;
                        command->append_out = previous == TokenKind::RedirectAppend;
                        break;
                    }

                    if (previous == TokenKind::RedirectIn)
                    {
                        command->redirect_in = token.text;
                        break;
                    }

                    if (previous == TokenKind::RedirectError)
                    {
                        command->redirect_error = token.text;
                        break;
                    }
                }

                if (command->argc >= static_cast<int>(sizeof(command->argv) / sizeof(command->argv[0])))
                {
                    return ParseStatus::TooManyArgs;
                }

                command->argv[command->argc] = token.text;
                ++command->argc;
                break;
            }

            case TokenKind::Pipe:
            {
                // A pipe must follow a non-empty command and precede another.
                if (command == nullptr || command->argc == 0)
                {
                    return ParseStatus::EmptyCommand;
                }

                status = start_command();
                if (status != ParseStatus::Ok)
                {
                    return status;
                }
                break;
            }

            case TokenKind::RedirectOut:
            case TokenKind::RedirectAppend:
            case TokenKind::RedirectIn:
            case TokenKind::RedirectError:
            {
                if (command == nullptr)
                {
                    return ParseStatus::EmptyCommand;
                }

                // The target is validated by the next iteration; a trailing
                // redirect is caught after the loop.
                break;
            }

            case TokenKind::Sequence:
            case TokenKind::And:
            case TokenKind::Or:
            {
                if (command == nullptr || command->argc == 0)
                {
                    return ParseStatus::EmptyCommand;
                }

                status = start_pipeline(token.kind);
                if (status != ParseStatus::Ok)
                {
                    return status;
                }
                break;
            }
            }
        }

        // A redirect or pipe at the very end is diagnosed specifically rather
        // than as a generic empty command, so the message names the real
        // mistake. The empty-command check must come last: a trailing pipe
        // leaves one behind and would otherwise mask the actual cause.
        const TokenKind last = m_tokens[m_token_count - 1].kind;
        if (last == TokenKind::RedirectOut || last == TokenKind::RedirectAppend ||
            last == TokenKind::RedirectIn || last == TokenKind::RedirectError)
        {
            return ParseStatus::MissingRedirectTarget;
        }

        if (last == TokenKind::Pipe)
        {
            return ParseStatus::TrailingPipe;
        }

        if (command == nullptr || command->argc == 0)
        {
            return ParseStatus::EmptyCommand;
        }

        ++list.pipeline_count;
        return ParseStatus::Ok;
    }
}
