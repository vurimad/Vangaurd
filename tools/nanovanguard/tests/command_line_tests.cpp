#include "../../../tests/support/scratch_directory.hpp"

#include <vanguard/nanovanguard/command_line.hpp>
#include <vanguard/nanovanguard/nanovanguard.hpp>
#include <vanguard/nanovanguard/project_platform.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

namespace
{
    using namespace vanguard;
    namespace nano = vanguard::nanovanguard;

    struct Buffer
    {
        char data[16384]{};
        u32 length = 0;
    };

    [[nodiscard]] bool Capture(const char* const text, const u32 length, void* const userData) noexcept
    {
        Buffer& buffer = *static_cast<Buffer*>(userData);
        if (length > sizeof(buffer.data) - buffer.length - 1)
            return false;
        for (u32 index = 0; index < length; ++index)
            buffer.data[buffer.length++] = text[index];
        buffer.data[buffer.length] = '\0';
        return true;
    }

    [[nodiscard]] bool Equal(const char* left, const char* right) noexcept
    {
        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }
        return *left == '\0' && *right == '\0';
    }

    [[nodiscard]] bool Contains(const char* text, const char* token) noexcept
    {
        for (; *text != '\0'; ++text)
        {
            const char* left = text;
            const char* right = token;
            while (*left != '\0' && *right != '\0' && *left == *right)
            {
                ++left;
                ++right;
            }
            if (*right == '\0')
                return true;
        }
        return false;
    }

    nano::ExitCode Execute(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        if (!Equal(invocation.Positional(0), "fixture"))
            return nano::ExitCode::OperationFailed;
        if (!Equal(invocation.Option("target"), "windows"))
            return nano::ExitCode::OperationFailed;
        return output.Write("executed\n") ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    void Join(const vanguard::containers::StringView root, const vanguard::containers::StringView leaf, vanguard::containers::String& output) noexcept
    {
        output.Clear();
        output.Append(root);
        if (!output.Empty() && output.Back() != '\\' && output.Back() != '/')
            output.Append('\\');
        output.Append(leaf);
    }

    void CleanupProject(const vanguard::containers::StringView root) noexcept
    {
        namespace platform = vanguard::nanovanguard::platform;
        vanguard::containers::String path;
        Join(root, "project-cardinal.vproject", path);
        if (platform::GetPathKind(path) == platform::PathKind::File)
            static_cast<void>(platform::DeleteFile(path));
        const char* const assetChildren[]{"Worlds", "Meshes", "Textures", "Materials", "Shaders", "Audio", "Prefabs"};
        for (const char* const child : assetChildren)
        {
            vanguard::containers::String relative("Assets\\");
            relative.Append(vanguard::containers::StringView(child));
            Join(root, relative, path);
            if (platform::GetPathKind(path) == platform::PathKind::Directory)
                static_cast<void>(platform::RemoveEmptyDirectory(path));
        }
        const char* const configChildren[]{"Project", "Cooking", "Packaging"};
        for (const char* const child : configChildren)
        {
            vanguard::containers::String relative("Config\\");
            relative.Append(vanguard::containers::StringView(child));
            Join(root, relative, path);
            if (platform::GetPathKind(path) == platform::PathKind::Directory)
                static_cast<void>(platform::RemoveEmptyDirectory(path));
        }
        const char* const roots[]{"Assets", "Config", "Plugins", "DerivedData", "Intermediate", "Saved", "Builds"};
        for (const char* const child : roots)
        {
            Join(root, child, path);
            if (platform::GetPathKind(path) == platform::PathKind::Directory)
                static_cast<void>(platform::RemoveEmptyDirectory(path));
        }
        if (platform::GetPathKind(root) == platform::PathKind::Directory)
            static_cast<void>(platform::RemoveEmptyDirectory(root));
    }
} // namespace

int main()
{
    namespace nano = vanguard::nanovanguard;
    namespace platform = vanguard::nanovanguard::platform;
    if (!vanguard::memory::Initialize() || !vanguard::containers::Initialize())
        return 1;
    constexpr nano::OptionDescriptor options[]{{"target", 't', nano::OptionValue::Required, "platform", "Select the target platform.", false}};
    nano::CommandRegistry registry;
    if (registry.Register({"project", nullptr, "Manage projects.", "<subcommand>", nullptr, 0, 0, 0, nullptr}) != nano::RegistrationResult::Success ||
        registry.Register({"validate", "project", "Validate a project.", "<project>", options, 1, 1, 1, &Execute}) != nano::RegistrationResult::Success)
        return 2;

    Buffer buffer;
    nano::Output output(&Capture, &buffer);
    const char* valid[]{"nanovanguard", "project", "validate", "fixture", "--target=windows"};
    if (registry.Dispatch(5, valid, output) != nano::ExitCode::Success || !Contains(buffer.data, "executed"))
        return 3;

    buffer = {};
    const char* duplicate[]{"nanovanguard", "project", "validate", "fixture", "-t", "windows", "--target", "linux"};
    if (registry.Dispatch(8, duplicate, output) != nano::ExitCode::UsageError || !Contains(buffer.data, "duplicate option"))
        return 4;

    buffer = {};
    const char* help[]{"nanovanguard", "help", "project", "validate"};
    if (registry.Dispatch(4, help, output) != nano::ExitCode::Success || !Contains(buffer.data, "--target <platform>"))
        return 5;

    buffer = {};
    const char* unknown[]{"nanovanguard", "project", "validate", "fixture", "--unknown"};
    if (registry.Dispatch(5, unknown, output) != nano::ExitCode::UsageError || !Contains(buffer.data, "unknown option"))
        return 6;

    buffer = {};
    const char* unknownSubcommand[]{"nanovanguard", "project", "unknown"};
    if (registry.Dispatch(3, unknownSubcommand, output) != nano::ExitCode::UsageError || !Contains(buffer.data, "unknown subcommand"))
        return 7;

    const vanguard::filesystem::AbsolutePath scratch = vanguard::tests::ScratchDirectory("nanovanguard_command_line_tests");
    const vanguard::filesystem::AbsolutePath projectRootPath = scratch.AddDirPath("project");
    vanguard::containers::String projectRoot(projectRootPath.AsChar());
    CleanupProject(projectRoot);

    nano::CommandRegistry builtins;
    if (!nano::RegisterBuiltinCommands(builtins))
        return 9;

    buffer = {};
    const char* rootHelp[]{"nanovanguard", "help"};
    if (builtins.Dispatch(2, rootHelp, output) != nano::ExitCode::Success || !Contains(buffer.data, "sources"))
        return 10;

    buffer = {};
    const char* sourcesHelp[]{"nanovanguard", "help", "sources"};
    if (builtins.Dispatch(3, sourcesHelp, output) != nano::ExitCode::Success || !Contains(buffer.data, "inspect"))
        return 11;

    buffer = {};
    const char* jsonUsage[]{"nanovanguard", "project", "validate", "--format", "jsonl"};
    if (builtins.Dispatch(5, jsonUsage, output) != nano::ExitCode::UsageError ||
        !Contains(buffer.data, "{\"schema\":1,\"event\":\"usage-error\",\"operation\":\"project-validate\"") ||
        Contains(buffer.data, "nanovanguard: "))
        return 12;

    buffer = {};
    const char* duplicateFormat[]{"nanovanguard", "project", "validate", "fixture", "--format", "jsonl", "--format", "human"};
    if (builtins.Dispatch(8, duplicateFormat, output) != nano::ExitCode::UsageError ||
        !Contains(buffer.data, "nanovanguard: duplicate option") || Contains(buffer.data, "\"event\":\"usage-error\""))
        return 13;

    buffer = {};
    const char* invalidFormat[]{"nanovanguard", "project", "validate", "fixture", "--format", "yaml"};
    if (builtins.Dispatch(6, invalidFormat, output) != nano::ExitCode::UsageError ||
        !Contains(buffer.data, "nanovanguard: unsupported output format") || Contains(buffer.data, "\"event\":\"usage-error\""))
        return 14;

    // E2.6 P5 protocol parity: both renderers consume one semantic event. Keep
    // this below dispatch parsing so the proof covers the stable event writer,
    // not two independently assembled command-result strings.
    Buffer humanBuffer;
    Buffer jsonBuffer;
    nano::Output humanOutput(&Capture, &humanBuffer);
    nano::Output jsonOutput(&Capture, &jsonBuffer);
    nano::SemanticEvent terminal;
    terminal.event = nano::SemanticEventKind::Completed;
    terminal.operation = "asset-build";
    if (!terminal.AddString("reason", "success") || !terminal.AddString("output", "main") ||
        !terminal.AddUnsigned("count", 7u) || !terminal.AddBoolean("current", true) ||
        !nano::WriteSemanticEvent(humanOutput, nano::OutputFormat::Human, terminal) ||
        !nano::WriteSemanticEvent(jsonOutput, nano::OutputFormat::JsonLines, terminal) ||
        !Contains(humanBuffer.data, "[asset-build] completed reason=success output=main count=7 current=true") ||
        !Contains(jsonBuffer.data,
                  "{\"schema\":1,\"event\":\"completed\",\"operation\":\"asset-build\",\"reason\":\"success\",\"output\":\"main\",\"count\":7,\"current\":true}"))
        return 24;

    buffer = {};
    const char* create[]{"nanovanguard",       "project",  "create",      "Project Cardinal", "--destination",
                         projectRoot.AsChar(), "--target", "windows-x64", "--format",         "jsonl"};
    const nano::ExitCode createResult = builtins.Dispatch(10, create, output);
    if (createResult != nano::ExitCode::Success || !Contains(buffer.data, "project-created") ||
        platform::GetPathKind(projectRoot) != platform::PathKind::Directory)
    {
        std::fprintf(stderr, "[nanovanguardTests] create failed: exit=%d output=%s\n", static_cast<int>(createResult), buffer.data);
        CleanupProject(projectRoot);
        return 15;
    }

    buffer = {};
    const char* inspect[]{"nanovanguard", "project", "inspect", projectRoot.AsChar(), "--format", "jsonl"};
    if (builtins.Dispatch(6, inspect, output) != nano::ExitCode::Success || !Contains(buffer.data, "project-inspected"))
    {
        CleanupProject(projectRoot);
        return 16;
    }
    buffer = {};
    const char* validate[]{"nanovanguard", "project", "validate", projectRoot.AsChar(), "--format", "jsonl"};
    if (builtins.Dispatch(6, validate, output) != nano::ExitCode::Success || !Contains(buffer.data, "project-valid"))
    {
        CleanupProject(projectRoot);
        return 17;
    }

    buffer = {};
    const char* sourcesInspect[]{"nanovanguard", "sources", "inspect", projectRoot.AsChar(), "--format", "jsonl"};
    if (builtins.Dispatch(6, sourcesInspect, output) != nano::ExitCode::Success ||
        !Contains(buffer.data, "{\"schema\":1,\"event\":\"completed\",\"operation\":\"sources-inspect\"") ||
        Contains(buffer.data, "\"issues\":0") || Contains(buffer.data, "\"metadataResult\":"))
    {
        CleanupProject(projectRoot);
        return 18;
    }

    buffer = {};
    if (builtins.Dispatch(10, create, output) != nano::ExitCode::InvalidProject || !Contains(buffer.data, "destination already exists"))
    {
        CleanupProject(projectRoot);
        return 19;
    }

    vanguard::containers::String builds;
    Join(projectRoot, "Builds", builds);
    if (!platform::RemoveEmptyDirectory(builds))
    {
        CleanupProject(projectRoot);
        return 20;
    }
    buffer = {};
    if (builtins.Dispatch(6, validate, output) != nano::ExitCode::InvalidProject || !Contains(buffer.data, "required project directory"))
    {
        CleanupProject(projectRoot);
        return 21;
    }
    buffer = {};
    const char* documentValidate[]{"nanovanguard", "project", "validate", projectRoot.AsChar(), "--level", "document"};
    if (builtins.Dispatch(6, documentValidate, output) != nano::ExitCode::Success)
    {
        CleanupProject(projectRoot);
        return 22;
    }

    CleanupProject(projectRoot);
    if (platform::GetPathKind(projectRoot) != platform::PathKind::Missing)
        return 23;
    return 0;
}
