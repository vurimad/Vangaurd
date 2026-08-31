#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::nanovanguard
{
    inline constexpr u32 MaximumCommands = 64;
    inline constexpr u32 MaximumOptionsPerCommand = 24;
    inline constexpr u32 MaximumParsedOptions = 32;
    inline constexpr u32 MaximumPositionals = 32;

    enum class ExitCode : i32
    {
        Success = 0,
        UsageError = 2,
        InvalidProject = 10,
        OperationFailed = 20,
        Cancelled = 30,
        InternalFailure = 70
    };

    enum class OutputFormat : u8
    {
        Human,
        JsonLines
    };

    enum class OptionValue : u8
    {
        None,
        Required
    };

    using WriteCallback = bool (*)(const char* text, u32 length, void* userData) noexcept;

    class Output final
    {
    public:
        Output(WriteCallback callback, void* userData = nullptr) noexcept;

        [[nodiscard]] bool Write(const char* text) noexcept;
        [[nodiscard]] bool Write(const char* text, u32 length) noexcept;
        [[nodiscard]] bool IsGood() const noexcept
        {
            return m_good;
        }

    private:
        WriteCallback m_callback = nullptr;
        void* m_userData = nullptr;
        bool m_good = true;
    };

    struct OptionDescriptor
    {
        const char* longName = nullptr;
        char shortName = '\0';
        OptionValue value = OptionValue::None;
        const char* valueName = nullptr;
        const char* summary = nullptr;
        bool repeatable = false;
    };

    struct ParsedOption
    {
        const OptionDescriptor* descriptor = nullptr;
        const char* value = nullptr;
    };

    class Invocation final
    {
    public:
        [[nodiscard]] bool HasOption(const char* longName) const noexcept;
        [[nodiscard]] const char* Option(const char* longName) const noexcept;
        [[nodiscard]] const char* Option(const char* longName, u32 occurrence) const noexcept;
        [[nodiscard]] u32 OptionCount(const char* longName) const noexcept;
        [[nodiscard]] const char* Positional(u32 index) const noexcept;
        [[nodiscard]] u32 PositionalCount() const noexcept
        {
            return m_positionalCount;
        }
        [[nodiscard]] OutputFormat Format() const noexcept
        {
            return m_format;
        }

    private:
        ParsedOption m_options[MaximumParsedOptions]{};
        const char* m_positionals[MaximumPositionals]{};
        u32 m_optionCount = 0;
        u32 m_positionalCount = 0;
        OutputFormat m_format = OutputFormat::Human;

        friend class CommandRegistry;
    };

    using CommandHandler = ExitCode (*)(const Invocation& invocation, Output& output) noexcept;

    struct CommandDescriptor
    {
        const char* name = nullptr;
        const char* parent = nullptr;
        const char* summary = nullptr;
        const char* positionalUsage = nullptr;
        const OptionDescriptor* options = nullptr;
        u32 optionCount = 0;
        u32 minimumPositionals = 0;
        u32 maximumPositionals = 0;
        CommandHandler handler = nullptr;
    };

    enum class RegistrationResult : u8
    {
        Success,
        InvalidDescriptor,
        DuplicateCommand,
        MissingParent,
        CapacityExceeded,
        RegistrySealed
    };

    class CommandRegistry final
    {
    public:
        [[nodiscard]] RegistrationResult Register(const CommandDescriptor& descriptor) noexcept;
        [[nodiscard]] ExitCode Dispatch(i32 argumentCount, const char* const* arguments, Output& output) noexcept;
        [[nodiscard]] bool PrintHelp(const char* commandPath, Output& output) const noexcept;
        [[nodiscard]] u32 Count() const noexcept
        {
            return m_count;
        }

    private:
        struct Record
        {
            CommandDescriptor descriptor;
            char path[128]{};
        };

        [[nodiscard]] const Record* FindPath(const char* path) const noexcept;
        [[nodiscard]] const Record* FindChild(const char* parent, const char* name) const noexcept;
        [[nodiscard]] const OptionDescriptor* FindOption(const Record& command, const char* name, u32 length) const noexcept;
        [[nodiscard]] const OptionDescriptor* FindOption(const Record& command, char shortName) const noexcept;
        [[nodiscard]] bool PrintRootHelp(Output& output) const noexcept;
        [[nodiscard]] bool PrintCommandHelp(const Record& command, Output& output) const noexcept;

        Record m_commands[MaximumCommands]{};
        u32 m_count = 0;
        bool m_sealed = false;
    };
} // namespace vanguard::nanovanguard
