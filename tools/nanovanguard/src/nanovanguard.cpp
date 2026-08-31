#include <vanguard/nanovanguard/command_line.hpp>
#include <vanguard/nanovanguard/project_commands.hpp>

namespace
{
    using namespace vanguard;
    namespace nano = vanguard::nanovanguard;

    nano::ExitCode Version(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        if (invocation.Format() == nano::OutputFormat::JsonLines)
            return output.Write("{\"schema\":1,\"event\":\"version\",\"tool\":\"nanovanguard\",\"version\":\"0.1.0\"}\n") ? nano::ExitCode::Success
                                                                                                                          : nano::ExitCode::InternalFailure;
        return output.Write("nanovanguard 0.1.0\n") ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    inline constexpr nano::OptionDescriptor VersionOptions[]{
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
} // namespace

namespace vanguard::nanovanguard
{
    bool RegisterBuiltinCommands(CommandRegistry& registry) noexcept
    {
        return registry.Register({"version", nullptr, "Print nanovanguard version and protocol information.", nullptr, VersionOptions, 1, 0, 0, &Version}) ==
                   RegistrationResult::Success &&
               registry.Register({"project", nullptr, "Create, inspect, validate, migrate, and register Vanguard projects.", "<subcommand>", nullptr, 0, 0, 0,
                                  nullptr}) == RegistrationResult::Success &&
               RegisterProjectCommands(registry);
    }
} // namespace vanguard::nanovanguard
