#include <vanguard/nanovanguard/command_line.hpp>

#include <vanguard/containers/containers.hpp>

namespace
{
    [[nodiscard]] vanguard::u32 Length(const char* const text) noexcept
    {
        if (text == nullptr)
            return 0;
        vanguard::u32 length = 0;
        while (text[length] != '\0')
            ++length;
        return length;
    }

    [[nodiscard]] bool Equal(const char* left, const char* right) noexcept
    {
        if (left == nullptr || right == nullptr)
            return left == right;
        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }
        return *left == '\0' && *right == '\0';
    }

    [[nodiscard]] bool Equal(const char* const left, const vanguard::u32 leftLength, const char* const right) noexcept
    {
        if (left == nullptr || right == nullptr || Length(right) != leftLength)
            return false;
        for (vanguard::u32 index = 0; index < leftLength; ++index)
            if (left[index] != right[index])
                return false;
        return true;
    }

    [[nodiscard]] bool IsValidName(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return false;
        for (const char* cursor = name; *cursor != '\0'; ++cursor)
        {
            const bool alpha = (*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z');
            const bool digit = *cursor >= '0' && *cursor <= '9';
            if (!alpha && !digit && *cursor != '-')
                return false;
        }
        return true;
    }

    [[nodiscard]] bool AppendPath(char* const destination, const vanguard::u32 capacity, const char* const parent, const char* const name) noexcept
    {
        vanguard::u32 written = 0;
        if (parent != nullptr && parent[0] != '\0')
        {
            while (parent[written] != '\0')
            {
                if (written + 1 >= capacity)
                    return false;
                destination[written] = parent[written];
                ++written;
            }
            if (written + 1 >= capacity)
                return false;
            destination[written++] = ' ';
        }
        for (vanguard::u32 index = 0; name[index] != '\0'; ++index)
        {
            if (written + 1 >= capacity)
                return false;
            destination[written++] = name[index];
        }
        destination[written] = '\0';
        return true;
    }

    [[nodiscard]] bool IsExactOption(const char* const argument, const char* const option) noexcept
    {
        return Equal(argument, option);
    }

    [[nodiscard]] vanguard::nanovanguard::OutputFormat PreselectUsageFormat(
        const vanguard::i32 argumentCount, const char* const* const arguments) noexcept
    {
        using namespace vanguard::nanovanguard;
        if (argumentCount <= 1 || arguments == nullptr)
            return OutputFormat::Human;

        bool sawFormat = false;
        bool jsonLines = false;
        for (vanguard::i32 index = 1; index < argumentCount; ++index)
        {
            const char* const argument = arguments[index];
            if (argument == nullptr)
                return OutputFormat::Human;
            const char* value = nullptr;
            if (IsExactOption(argument, "--format"))
            {
                if (index + 1 >= argumentCount || arguments[index + 1] == nullptr || arguments[index + 1][0] == '\0')
                    return OutputFormat::Human;
                value = arguments[++index];
            }
            else
            {
                constexpr char prefix[] = "--format=";
                bool matches = true;
                for (vanguard::u32 character = 0; character < sizeof(prefix) - 1u; ++character)
                    if (argument[character] == '\0' || argument[character] != prefix[character])
                    {
                        matches = false;
                        break;
                    }
                if (!matches)
                    continue;
                value = argument + sizeof(prefix) - 1u;
                if (value[0] == '\0')
                    return OutputFormat::Human;
            }
            if (sawFormat)
                return OutputFormat::Human;
            sawFormat = true;
            if (Equal(value, "jsonl"))
                jsonLines = true;
            else if (!Equal(value, "human"))
                return OutputFormat::Human;
        }
        return sawFormat && jsonLines ? OutputFormat::JsonLines : OutputFormat::Human;
    }

    [[nodiscard]] bool WriteUsageError(vanguard::nanovanguard::Output& output,
                                       const vanguard::nanovanguard::OutputFormat format,
                                       const char* const operation, const char* const message,
                                       const char* const detail = nullptr) noexcept
    {
        using namespace vanguard::nanovanguard;
        if (format == OutputFormat::JsonLines)
        {
            SemanticEvent event;
            event.event = SemanticEventKind::UsageError;
            event.operation = operation != nullptr && operation[0] != '\0' ? operation : "command-line";
            if (!event.AddString("code", "usage-error") || !event.AddString("message", message) ||
                (detail != nullptr && !event.AddString("detail", detail)))
                return false;
            return WriteSemanticEvent(output, format, event);
        }
        bool written = output.Write("nanovanguard: ") && output.Write(message);
        if (detail != nullptr)
            written = written && output.Write(": ") && output.Write(detail);
        return written && output.Write("\nTry 'nanovanguard help' for usage.\n");
    }

    [[nodiscard]] vanguard::nanovanguard::ExitCode UsageFailure(
        vanguard::nanovanguard::Output& output, const vanguard::nanovanguard::OutputFormat format,
        const char* const message, const char* const detail = nullptr,
        const char* const operation = "command-line") noexcept
    {
        return WriteUsageError(output, format, operation, message, detail) ? vanguard::nanovanguard::ExitCode::UsageError
                                                                           : vanguard::nanovanguard::ExitCode::InternalFailure;
    }

    [[nodiscard]] bool MakeOperationToken(const char* const commandPath, char* const destination,
                                          const vanguard::u32 capacity) noexcept
    {
        if (commandPath == nullptr || destination == nullptr || capacity == 0u)
            return false;
        vanguard::u32 written = 0u;
        for (; commandPath[written] != '\0'; ++written)
        {
            if (written + 1u >= capacity)
                return false;
            destination[written] = commandPath[written] == ' ' ? '-' : commandPath[written];
        }
        destination[written] = '\0';
        return written != 0u;
    }
} // namespace

namespace vanguard::nanovanguard
{
    Output::Output(const WriteCallback callback, void* const userData) noexcept : m_callback(callback), m_userData(userData), m_good(callback != nullptr) {}

    bool Output::Write(const char* const text) noexcept
    {
        return Write(text, Length(text));
    }

    bool Output::Write(const char* const text, const u32 length) noexcept
    {
        if (!m_good || text == nullptr)
            return false;
        if (length == 0)
            return true;
        m_good = m_callback(text, length, m_userData);
        return m_good;
    }

    bool SemanticEvent::AddString(const char* const name, const char* const value) noexcept
    {
        if (fieldCount >= MaximumSemanticEventFields || name == nullptr || name[0] == '\0' || value == nullptr)
            return false;
        fields[fieldCount++] = {name, SemanticFieldKind::String, value, 0u, false};
        return true;
    }

    bool SemanticEvent::AddUnsigned(const char* const name, const u64 value) noexcept
    {
        if (fieldCount >= MaximumSemanticEventFields || name == nullptr || name[0] == '\0')
            return false;
        fields[fieldCount++] = {name, SemanticFieldKind::Unsigned, nullptr, value, false};
        return true;
    }

    bool SemanticEvent::AddBoolean(const char* const name, const bool value) noexcept
    {
        if (fieldCount >= MaximumSemanticEventFields || name == nullptr || name[0] == '\0')
            return false;
        fields[fieldCount++] = {name, SemanticFieldKind::Boolean, nullptr, 0u, value};
        return true;
    }

    const char* SemanticEventToken(const SemanticEventKind event) noexcept
    {
        switch (event)
        {
        case SemanticEventKind::Status:
            return "status";
        case SemanticEventKind::Progress:
            return "progress";
        case SemanticEventKind::Diagnostic:
            return "diagnostic";
        case SemanticEventKind::Completed:
            return "completed";
        case SemanticEventKind::Failed:
            return "failed";
        case SemanticEventKind::Cancelled:
            return "cancelled";
        case SemanticEventKind::UsageError:
            return "usage-error";
        }
        return "unknown";
    }

    namespace
    {
        void AppendJsonString(containers::String& destination, const char* const value) noexcept
        {
            destination.Append('"');
            for (const char* cursor = value; cursor != nullptr && *cursor != '\0'; ++cursor)
            {
                const char character = *cursor;
                if (character == '"' || character == '\\')
                {
                    destination.Append('\\');
                    destination.Append(character);
                }
                else if (character == '\n')
                    destination.Append("\\n", 2u);
                else if (character == '\r')
                    destination.Append("\\r", 2u);
                else if (character == '\t')
                    destination.Append("\\t", 2u);
                else if (static_cast<u8>(character) >= 0x20u)
                    destination.Append(character);
            }
            destination.Append('"');
        }
    } // namespace

    bool WriteSemanticEvent(Output& output, const OutputFormat format, const SemanticEvent& event) noexcept
    {
        if (event.operation == nullptr || event.operation[0] == '\0' || event.fieldCount > MaximumSemanticEventFields)
            return false;
        const char* const eventToken = SemanticEventToken(event.event);
        if (format == OutputFormat::JsonLines)
        {
            containers::String line;
            line.Append("{\"schema\":1,\"event\":", 20u);
            AppendJsonString(line, eventToken);
            line.Append(",\"operation\":", 13u);
            AppendJsonString(line, event.operation);
            for (u32 index = 0; index < event.fieldCount; ++index)
            {
                const SemanticField& field = event.fields[index];
                if (field.name == nullptr || field.name[0] == '\0')
                    return false;
                line.Append(',');
                AppendJsonString(line, field.name);
                line.Append(':');
                if (field.kind == SemanticFieldKind::String)
                {
                    if (field.stringValue == nullptr)
                        return false;
                    AppendJsonString(line, field.stringValue);
                }
                else if (field.kind == SemanticFieldKind::Unsigned)
                    line.Append(containers::String::Printf("%llu", static_cast<unsigned long long>(field.unsignedValue)));
                else
                    line.Append(containers::StringView(field.booleanValue ? "true" : "false"));
            }
            line.Append("}\n", 2u);
            return output.Write(line.AsChar(), line.Length());
        }

        containers::String line;
        line.Append('[').Append(containers::StringView(event.operation)).Append(containers::StringView("] "))
            .Append(containers::StringView(eventToken));
        for (u32 index = 0; index < event.fieldCount; ++index)
        {
            const SemanticField& field = event.fields[index];
            if (field.name == nullptr || field.name[0] == '\0')
                return false;
            line.Append(' ').Append(containers::StringView(field.name)).Append('=');
            if (field.kind == SemanticFieldKind::String)
            {
                if (field.stringValue == nullptr)
                    return false;
                line.Append(containers::StringView(field.stringValue));
            }
            else if (field.kind == SemanticFieldKind::Unsigned)
                line.Append(containers::String::Printf("%llu", static_cast<unsigned long long>(field.unsignedValue)));
            else
                line.Append(containers::StringView(field.booleanValue ? "true" : "false"));
        }
        line.Append('\n');
        return output.Write(line.AsChar(), line.Length());
    }

    bool Invocation::HasOption(const char* const longName) const noexcept
    {
        return OptionCount(longName) != 0;
    }

    const char* Invocation::Option(const char* const longName) const noexcept
    {
        return Option(longName, 0);
    }

    const char* Invocation::Option(const char* const longName, const u32 occurrence) const noexcept
    {
        u32 found = 0;
        for (u32 index = 0; index < m_optionCount; ++index)
        {
            if (!Equal(m_options[index].descriptor->longName, longName))
                continue;
            if (found++ == occurrence)
                return m_options[index].value;
        }
        return nullptr;
    }

    u32 Invocation::OptionCount(const char* const longName) const noexcept
    {
        u32 count = 0;
        for (u32 index = 0; index < m_optionCount; ++index)
            if (Equal(m_options[index].descriptor->longName, longName))
                ++count;
        return count;
    }

    const char* Invocation::Positional(const u32 index) const noexcept
    {
        return index < m_positionalCount ? m_positionals[index] : nullptr;
    }

    RegistrationResult CommandRegistry::Register(const CommandDescriptor& descriptor) noexcept
    {
        if (m_sealed)
            return RegistrationResult::RegistrySealed;
        if (!IsValidName(descriptor.name) || descriptor.summary == nullptr || descriptor.summary[0] == '\0' ||
            descriptor.optionCount > MaximumOptionsPerCommand || (descriptor.optionCount != 0 && descriptor.options == nullptr) ||
            descriptor.minimumPositionals > descriptor.maximumPositionals)
            return RegistrationResult::InvalidDescriptor;
        if (descriptor.parent != nullptr && descriptor.parent[0] != '\0' && FindPath(descriptor.parent) == nullptr)
            return RegistrationResult::MissingParent;
        for (u32 optionIndex = 0; optionIndex < descriptor.optionCount; ++optionIndex)
        {
            const OptionDescriptor& option = descriptor.options[optionIndex];
            if (!IsValidName(option.longName) || option.summary == nullptr || option.summary[0] == '\0' ||
                (option.value == OptionValue::Required && (option.valueName == nullptr || option.valueName[0] == '\0')))
                return RegistrationResult::InvalidDescriptor;
            for (u32 other = 0; other < optionIndex; ++other)
                if (Equal(option.longName, descriptor.options[other].longName) ||
                    (option.shortName != '\0' && option.shortName == descriptor.options[other].shortName))
                    return RegistrationResult::InvalidDescriptor;
        }
        if (m_count == MaximumCommands)
            return RegistrationResult::CapacityExceeded;
        Record candidate{};
        candidate.descriptor = descriptor;
        if (!AppendPath(candidate.path, sizeof(candidate.path), descriptor.parent, descriptor.name))
            return RegistrationResult::InvalidDescriptor;
        if (FindPath(candidate.path) != nullptr)
            return RegistrationResult::DuplicateCommand;
        m_commands[m_count++] = candidate;
        return RegistrationResult::Success;
    }

    const CommandRegistry::Record* CommandRegistry::FindPath(const char* const path) const noexcept
    {
        if (path == nullptr)
            return nullptr;
        for (u32 index = 0; index < m_count; ++index)
            if (Equal(m_commands[index].path, path))
                return &m_commands[index];
        return nullptr;
    }

    const CommandRegistry::Record* CommandRegistry::FindChild(const char* const parent, const char* const name) const noexcept
    {
        char path[128]{};
        if (!AppendPath(path, sizeof(path), parent, name))
            return nullptr;
        return FindPath(path);
    }

    const OptionDescriptor* CommandRegistry::FindOption(const Record& command, const char* const name, const u32 length) const noexcept
    {
        for (u32 index = 0; index < command.descriptor.optionCount; ++index)
            if (Equal(name, length, command.descriptor.options[index].longName))
                return &command.descriptor.options[index];
        return nullptr;
    }

    const OptionDescriptor* CommandRegistry::FindOption(const Record& command, const char shortName) const noexcept
    {
        for (u32 index = 0; index < command.descriptor.optionCount; ++index)
            if (command.descriptor.options[index].shortName == shortName)
                return &command.descriptor.options[index];
        return nullptr;
    }

    ExitCode CommandRegistry::Dispatch(const i32 argumentCount, const char* const* const arguments, Output& output) noexcept
    {
        m_sealed = true;
        const OutputFormat usageFormat = PreselectUsageFormat(argumentCount, arguments);
        if (argumentCount <= 1 || arguments == nullptr)
            return PrintRootHelp(output) ? ExitCode::Success : ExitCode::InternalFailure;
        if (Equal(arguments[1], "help") || Equal(arguments[1], "--help") || Equal(arguments[1], "-h"))
        {
            if (argumentCount == 2)
                return PrintRootHelp(output) ? ExitCode::Success : ExitCode::InternalFailure;
            char path[128]{};
            u32 written = 0;
            for (i32 index = 2; index < argumentCount; ++index)
            {
                const u32 length = Length(arguments[index]);
                if (length == 0 || arguments[index][0] == '-' || written + length + (written != 0 ? 1u : 0u) >= sizeof(path))
                    return UsageFailure(output, usageFormat, "invalid help command path");
                if (written != 0)
                    path[written++] = ' ';
                for (u32 character = 0; character < length; ++character)
                    path[written++] = arguments[index][character];
                path[written] = '\0';
            }
            if (!PrintHelp(path, output))
                return UsageFailure(output, usageFormat, "unknown command", path);
            return ExitCode::Success;
        }

        const Record* command = nullptr;
        char resolvedPath[128]{};
        i32 cursor = 1;
        while (cursor < argumentCount && arguments[cursor] != nullptr && arguments[cursor][0] != '-')
        {
            const Record* const child = FindChild(resolvedPath, arguments[cursor]);
            if (child == nullptr)
                break;
            command = child;
            const u32 pathLength = Length(child->path);
            for (u32 index = 0; index <= pathLength; ++index)
                resolvedPath[index] = child->path[index];
            ++cursor;
        }
        if (command == nullptr)
            return UsageFailure(output, usageFormat, "unknown command", arguments[1]);
        char usageOperation[128]{};
        if (!MakeOperationToken(command->path, usageOperation, sizeof(usageOperation)))
            return ExitCode::InternalFailure;
        if (command->descriptor.handler == nullptr)
        {
            if (cursor < argumentCount && !Equal(arguments[cursor], "--help") && !Equal(arguments[cursor], "-h"))
                return UsageFailure(output, usageFormat, "unknown subcommand", arguments[cursor], usageOperation);
            return PrintCommandHelp(*command, output) ? ExitCode::Success : ExitCode::InternalFailure;
        }

        Invocation invocation;
        while (cursor < argumentCount)
        {
            const char* const argument = arguments[cursor];
            if (argument == nullptr)
                return UsageFailure(output, usageFormat, "null command-line argument", nullptr, usageOperation);
            if (Equal(argument, "--help") || Equal(argument, "-h"))
                return PrintCommandHelp(*command, output) ? ExitCode::Success : ExitCode::InternalFailure;
            if (argument[0] == '-' && argument[1] != '\0')
            {
                const OptionDescriptor* option = nullptr;
                const char* value = nullptr;
                if (argument[1] == '-')
                {
                    const char* name = argument + 2;
                    u32 nameLength = 0;
                    while (name[nameLength] != '\0' && name[nameLength] != '=')
                        ++nameLength;
                    option = FindOption(*command, name, nameLength);
                    if (name[nameLength] == '=')
                        value = name + nameLength + 1;
                }
                else if (argument[2] == '\0')
                    option = FindOption(*command, argument[1]);
                if (option == nullptr)
                    return UsageFailure(output, usageFormat, "unknown option", argument, usageOperation);
                if (!option->repeatable && invocation.OptionCount(option->longName) != 0)
                    return UsageFailure(output, usageFormat, "duplicate option", option->longName, usageOperation);
                if (option->value == OptionValue::Required)
                {
                    if (value == nullptr && cursor + 1 < argumentCount)
                        value = arguments[++cursor];
                    if (value == nullptr || value[0] == '\0')
                        return UsageFailure(output, usageFormat, "missing option value", option->longName, usageOperation);
                }
                else if (value != nullptr)
                    return UsageFailure(output, usageFormat, "option does not accept a value", option->longName, usageOperation);
                if (invocation.m_optionCount == MaximumParsedOptions)
                    return UsageFailure(output, usageFormat, "too many options", nullptr, usageOperation);
                invocation.m_options[invocation.m_optionCount++] = {option, value};
            }
            else
            {
                if (invocation.m_positionalCount == MaximumPositionals)
                    return UsageFailure(output, usageFormat, "too many positional arguments", nullptr, usageOperation);
                invocation.m_positionals[invocation.m_positionalCount++] = argument;
            }
            ++cursor;
        }
        if (invocation.m_positionalCount < command->descriptor.minimumPositionals || invocation.m_positionalCount > command->descriptor.maximumPositionals)
            return UsageFailure(output, usageFormat, "incorrect number of positional arguments", command->path, usageOperation);
        const char* const format = invocation.Option("format");
        if (format != nullptr)
        {
            if (Equal(format, "human"))
                invocation.m_format = OutputFormat::Human;
            else if (Equal(format, "jsonl"))
                invocation.m_format = OutputFormat::JsonLines;
            else
                return UsageFailure(output, usageFormat, "unsupported output format", format, usageOperation);
        }
        return command->descriptor.handler(invocation, output);
    }

    bool CommandRegistry::PrintHelp(const char* const commandPath, Output& output) const noexcept
    {
        if (commandPath == nullptr || commandPath[0] == '\0')
            return PrintRootHelp(output);
        const Record* const command = FindPath(commandPath);
        return command != nullptr && PrintCommandHelp(*command, output);
    }

    bool CommandRegistry::PrintRootHelp(Output& output) const noexcept
    {
        bool written = output.Write("nanovanguard - Vanguard project and build orchestration\n\nUsage:\n  nanovanguard <command> [options]\n\nCommands:\n");
        for (u32 index = 0; index < m_count; ++index)
        {
            if (m_commands[index].descriptor.parent != nullptr && m_commands[index].descriptor.parent[0] != '\0')
                continue;
            written = written && output.Write("  ") && output.Write(m_commands[index].descriptor.name) && output.Write("\n      ") &&
                      output.Write(m_commands[index].descriptor.summary) && output.Write("\n");
        }
        return written && output.Write("\nUse 'nanovanguard help <command>' for command help.\n");
    }

    bool CommandRegistry::PrintCommandHelp(const Record& command, Output& output) const noexcept
    {
        bool written = output.Write(command.descriptor.summary) && output.Write("\n\nUsage:\n  nanovanguard ") && output.Write(command.path);
        if (command.descriptor.positionalUsage != nullptr && command.descriptor.positionalUsage[0] != '\0')
            written = written && output.Write(" ") && output.Write(command.descriptor.positionalUsage);
        if (command.descriptor.optionCount != 0)
            written = written && output.Write(" [options]");
        written = written && output.Write("\n");
        bool hasChildren = false;
        for (u32 index = 0; index < m_count; ++index)
            if (Equal(m_commands[index].descriptor.parent, command.path))
                hasChildren = true;
        if (hasChildren)
        {
            written = written && output.Write("\nSubcommands:\n");
            for (u32 index = 0; index < m_count; ++index)
            {
                if (!Equal(m_commands[index].descriptor.parent, command.path))
                    continue;
                written = written && output.Write("  ") && output.Write(m_commands[index].descriptor.name) && output.Write("\n      ") &&
                          output.Write(m_commands[index].descriptor.summary) && output.Write("\n");
            }
        }
        if (command.descriptor.optionCount != 0)
        {
            written = written && output.Write("\nOptions:\n");
            for (u32 index = 0; index < command.descriptor.optionCount; ++index)
            {
                const OptionDescriptor& option = command.descriptor.options[index];
                written = written && output.Write("  --") && output.Write(option.longName);
                if (option.value == OptionValue::Required)
                    written = written && output.Write(" <") && output.Write(option.valueName) && output.Write(">");
                written = written && output.Write("\n      ") && output.Write(option.summary) && output.Write("\n");
            }
        }
        return written && output.Write("\n  -h, --help\n      Show this help.\n");
    }
} // namespace vanguard::nanovanguard
