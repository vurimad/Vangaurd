#include <vanguard/nanovanguard/project_commands.hpp>

#include <vanguard/nanovanguard/project_platform.hpp>

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/projects/project_workspace.hpp>
#include <vanguard/assets/source_database.hpp>

namespace
{
    using namespace vanguard;
    namespace containers = vanguard::containers;
    namespace filesystem = vanguard::filesystem;
    namespace nano = vanguard::nanovanguard;
    namespace platform = vanguard::nanovanguard::platform;
    namespace projects = vanguard::projects;

    struct ResolvedProject final
    {
        containers::String root;
        containers::String file;
    };

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

    void Assign(containers::String& destination, const containers::StringView source) noexcept
    {
        destination.Clear();
        destination.Append(source);
    }

    void Join(const containers::StringView directory, const containers::StringView leaf, containers::String& output) noexcept
    {
        Assign(output, directory);
        if (!output.Empty() && output.Back() != '/' && output.Back() != '\\')
            output.Append('\\');
        output.Append(leaf);
    }

    [[nodiscard]] bool Parent(const containers::StringView path, containers::String& output) noexcept
    {
        u32 separator = containers::StringView::npos;
        for (u32 index = 0; index < path.Length(); ++index)
            if (path[index] == '/' || path[index] == '\\')
                separator = index;
        if (separator == containers::StringView::npos || separator == 0)
            return false;
        Assign(output, path.Slice(0, separator));
        return true;
    }

    [[nodiscard]] bool ValidDestinationLeaf(const containers::StringView name) noexcept
    {
        if (name.Empty() || name == "." || name == ".." || name.Back() == ' ' || name.Back() == '.')
            return false;
        for (const char character : name)
        {
            if (static_cast<u8>(character) < 0x20u || character == '/' || character == '\\' || character == ':' || character == '*' || character == '?' ||
                character == '"' || character == '<' || character == '>' || character == '|')
                return false;
        }
        return true;
    }

    [[nodiscard]] bool DeriveTechnicalName(const containers::StringView displayName, containers::String& technicalName) noexcept
    {
        technicalName.Clear();
        bool separator = false;
        for (const char character : displayName)
        {
            char output = character;
            if (output >= 'A' && output <= 'Z')
                output = static_cast<char>(output - 'A' + 'a');
            const bool accepted = (output >= 'a' && output <= 'z') || (output >= '0' && output <= '9');
            if (accepted)
            {
                if (separator && !technicalName.Empty())
                    technicalName.Append('-');
                technicalName.Append(output);
                separator = false;
            }
            else
            {
                if (static_cast<u8>(output) >= 0x80u)
                    return false;
                separator = true;
            }
        }
        return !technicalName.Empty() && technicalName.Length() <= 64;
    }

    void SetProjectId(const containers::ArraySpan<const u8> bytes, projects::ProjectId& id) noexcept
    {
        for (u32 part = 0; part < 4; ++part)
            id.parts[part] = (static_cast<u32>(bytes[part * 4]) << 24u) | (static_cast<u32>(bytes[part * 4 + 1]) << 16u) |
                             (static_cast<u32>(bytes[part * 4 + 2]) << 8u) | bytes[part * 4 + 3];
    }

    [[nodiscard]] bool GenerateProjectId(projects::ProjectId& id) noexcept
    {
        u8 bytes[16]{};
        if (!platform::GenerateRandomBytes(bytes))
            return false;
        bytes[6] = static_cast<u8>((bytes[6] & 0x0fu) | 0x40u);
        bytes[8] = static_cast<u8>((bytes[8] & 0x3fu) | 0x80u);
        SetProjectId(bytes, id);
        return true;
    }

    [[nodiscard]] bool ResolveProject(const char* const argument, ResolvedProject& result) noexcept
    {
        containers::String absolute;
        if (argument == nullptr || !platform::MakeAbsolutePath(argument, absolute))
            return false;
        const platform::PathKind kind = platform::GetPathKind(absolute);
        if (kind == platform::PathKind::Directory)
        {
            result.root = static_cast<containers::String&&>(absolute);
            return platform::FindProjectFile(result.root, result.file);
        }
        if (kind != platform::PathKind::File || !Parent(absolute, result.root))
            return false;
        result.file = static_cast<containers::String&&>(absolute);
        return true;
    }

    [[nodiscard]] const char* ResultName(const projects::Result result) noexcept
    {
        switch (result)
        {
        case projects::Result::Success:
            return "success";
        case projects::Result::InvalidArgument:
            return "invalid-argument";
        case projects::Result::IoFailure:
            return "io-failure";
        case projects::Result::FileTooLarge:
            return "file-too-large";
        case projects::Result::InvalidUtf8:
            return "invalid-utf8";
        case projects::Result::InvalidSyntax:
            return "invalid-syntax";
        case projects::Result::UnsupportedVersion:
            return "unsupported-version";
        case projects::Result::UnknownField:
            return "unknown-field";
        case projects::Result::DuplicateField:
            return "duplicate-field";
        case projects::Result::MissingField:
            return "missing-field";
        case projects::Result::InvalidValue:
            return "invalid-value";
        case projects::Result::LimitExceeded:
            return "limit-exceeded";
        }
        return "unknown";
    }

    void AppendJsonString(containers::String& output, const containers::StringView text) noexcept
    {
        output.Append('"');
        for (const char character : text)
        {
            if (character == '"' || character == '\\')
            {
                output.Append('\\');
                output.Append(character);
            }
            else if (character == '\n')
                output.Append("\\n", 2);
            else if (character == '\r')
                output.Append("\\r", 2);
            else if (character == '\t')
                output.Append("\\t", 2);
            else
                output.Append(character);
        }
        output.Append('"');
    }

    [[nodiscard]] nano::ExitCode ReportProjectFailure(nano::Output& output, const nano::OutputFormat format, const char* const operation,
                                                      const char* const message, const projects::Diagnostic* const diagnostic = nullptr) noexcept
    {
        containers::String line;
        if (format == nano::OutputFormat::JsonLines)
        {
            line.Append(containers::StringView("{\"schema\":1,\"event\":\"project-error\",\"operation\":"));
            AppendJsonString(line, operation);
            line.Append(containers::StringView(",\"code\":"));
            AppendJsonString(line, diagnostic != nullptr ? ResultName(diagnostic->result) : "operation-failed");
            line.Append(containers::StringView(",\"message\":"));
            AppendJsonString(line, message);
            if (diagnostic != nullptr)
            {
                line.Append(containers::String::Printf(",\"line\":%u,\"column\":%u", diagnostic->line, diagnostic->column));
                if (diagnostic->field != nullptr)
                {
                    line.Append(containers::StringView(",\"field\":"));
                    AppendJsonString(line, diagnostic->field);
                }
            }
            line.Append("}\n", 2);
        }
        else
        {
            line.Append(containers::StringView("nanovanguard: "))
                .Append(containers::StringView(operation))
                .Append(containers::StringView(" failed: "))
                .Append(containers::StringView(message));
            if (diagnostic != nullptr && diagnostic->line != 0)
                line.Append(containers::String::Printf(" (line %u, column %u)", diagnostic->line, diagnostic->column));
            line.Append('\n');
        }
        return output.Write(line.AsChar(), line.Length()) ? nano::ExitCode::InvalidProject : nano::ExitCode::InternalFailure;
    }

    [[nodiscard]] bool LoadProject(const ResolvedProject& resolved, projects::ProjectDescriptor& project, projects::Diagnostic& diagnostic) noexcept
    {
        containers::String contents;
        if (!platform::ReadFile(resolved.file, contents))
        {
            diagnostic = {projects::Result::IoFailure, 0, 0, nullptr, "could not read project document"};
            return false;
        }
        return projects::Parse(contents, project, &diagnostic) == projects::Result::Success;
    }

    [[nodiscard]] bool ValidateLayout(const ResolvedProject& resolved, const projects::ProjectDescriptor& project, containers::String& failure) noexcept
    {
        projects::ProjectWorkspace workspace;
        projects::Diagnostic diagnostic;
        const filesystem::AbsolutePath projectFile = filesystem::AbsolutePath::ParseFilePath(resolved.file);
        if (projects::ResolveWorkspace(projectFile, project, workspace, &diagnostic) != projects::Result::Success)
        {
            failure = diagnostic.message != nullptr ? diagnostic.message : "project workspace resolution failed";
            return false;
        }
        const filesystem::AbsolutePath* roots[]{&workspace.assets, &workspace.derivedData, &workspace.intermediate, &workspace.saved, &workspace.builds, &workspace.config, &workspace.plugins};
        for (const filesystem::AbsolutePath* const root : roots)
        {
            if (platform::GetPathKind(root->AsStringView()) != platform::PathKind::Directory)
            {
                failure = "required project directory is missing or has the wrong type: ";
                failure.Append(root->AsStringView());
                return false;
            }
        }
        return true;
    }

    void Rollback(const containers::StringView projectFile, containers::StaticArray<containers::String, 20>& directories) noexcept
    {
        if (!projectFile.Empty() && platform::GetPathKind(projectFile) == platform::PathKind::File)
            static_cast<void>(platform::DeleteFile(projectFile));
        for (u32 index = directories.Size(); index > 0; --index)
            static_cast<void>(platform::RemoveEmptyDirectory(directories[index - 1]));
    }

    [[nodiscard]] bool CreateDirectoryTracked(const containers::StringView parent, const containers::StringView relative,
                                              containers::StaticArray<containers::String, 20>& directories) noexcept
    {
        containers::String path;
        Join(parent, relative, path);
        if (!platform::CreateDirectory(path))
            return false;
        directories.PushBack(static_cast<containers::String&&>(path));
        return true;
    }

    nano::ExitCode CreateProject(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        const containers::StringView displayName(invocation.Positional(0));
        projects::ProjectDescriptor project;
        Assign(project.name, displayName);
        const char* const explicitTechnicalName = invocation.Option("technical-name");
        if (explicitTechnicalName != nullptr)
            project.technicalName = explicitTechnicalName;
        else if (!DeriveTechnicalName(displayName, project.technicalName))
            return ReportProjectFailure(output, invocation.Format(), "project create", "could not derive a technical name; provide --technical-name");
        project.engine = {0, 1, 0, 0, 1, true, 0};
        if (!GenerateProjectId(project.id))
            return ReportProjectFailure(output, invocation.Format(), "project create", "secure project identity generation failed");
        const u32 targetCount = invocation.OptionCount("target");
        if (targetCount == 0)
            project.targets.PushBack("windows-x64");
        else
            for (u32 index = 0; index < targetCount; ++index)
                project.targets.PushBack(invocation.Option("target", index));

        projects::Diagnostic diagnostic;
        if (projects::Validate(project, &diagnostic) != projects::Result::Success)
            return ReportProjectFailure(output, invocation.Format(), "project create", diagnostic.message, &diagnostic);

        const char* const destinationArgument = invocation.Option("destination");
        if (destinationArgument == nullptr && !ValidDestinationLeaf(displayName))
            return ReportProjectFailure(output, invocation.Format(), "project create",
                                        "the display name is not a safe default directory name; provide --destination");
        containers::String destination;
        if (!platform::MakeAbsolutePath(destinationArgument != nullptr ? containers::StringView(destinationArgument) : displayName, destination))
            return ReportProjectFailure(output, invocation.Format(), "project create", "could not resolve destination path");
        if (platform::GetPathKind(destination) != platform::PathKind::Missing)
            return ReportProjectFailure(output, invocation.Format(), "project create", "destination already exists");

        u8 randomSuffix[8]{};
        if (!platform::GenerateRandomBytes(randomSuffix))
            return ReportProjectFailure(output, invocation.Format(), "project create", "secure staging-name generation failed");
        constexpr char hexadecimal[] = "0123456789abcdef";
        containers::String staging(destination);
        staging.Append(containers::StringView(".nanovanguard-staging-"));
        for (const u8 byte : randomSuffix)
        {
            staging.Append(hexadecimal[byte >> 4u]);
            staging.Append(hexadecimal[byte & 15u]);
        }
        const platform::PathKind stagingKind = platform::GetPathKind(staging);
        const bool stagingCreated = stagingKind == platform::PathKind::Missing && platform::CreateDirectory(staging);
        if (!stagingCreated)
        {
            containers::String failure = containers::String::Printf(
                "could not create private staging directory (pathKind=%u, systemError=%u): ", static_cast<u32>(stagingKind), platform::GetLastErrorCode());
            failure.Append(staging);
            return ReportProjectFailure(output, invocation.Format(), "project create", failure.AsChar());
        }

        containers::StaticArray<containers::String, 20> directories;
        directories.PushBack(staging);
        const containers::StringView roots[]{"Assets", "Config", "Plugins", "DerivedData", "Intermediate", "Saved", "Builds"};
        const containers::StringView assetChildren[]{"Worlds", "Meshes", "Textures", "Materials", "Shaders", "Audio", "Prefabs"};
        const containers::StringView configChildren[]{"Project", "Cooking", "Packaging"};
        bool created = true;
        for (const containers::StringView root : roots)
            created = created && CreateDirectoryTracked(staging, root, directories);
        containers::String assetsRoot;
        Join(staging, "Assets", assetsRoot);
        for (const containers::StringView child : assetChildren)
            created = created && CreateDirectoryTracked(assetsRoot, child, directories);
        containers::String configRoot;
        Join(staging, "Config", configRoot);
        for (const containers::StringView child : configChildren)
            created = created && CreateDirectoryTracked(configRoot, child, directories);

        containers::String projectFile;
        containers::String fileName(project.technicalName);
        fileName.Append(".vproject", 9);
        Join(staging, fileName, projectFile);
        containers::DynamicArray<u8> bytes(memory::pools::Filesystem::GetInstance());
        if (created)
        {
            filesystem::MemoryFileWriter writer(bytes);
            created = projects::Write(writer, project, &diagnostic) == projects::Result::Success && platform::WriteFileDurable(projectFile, bytes);
        }

        ResolvedProject staged;
        staged.root = staging;
        staged.file = projectFile;
        projects::ProjectDescriptor verified;
        containers::String layoutFailure;
        created = created && LoadProject(staged, verified, diagnostic) && ValidateLayout(staged, verified, layoutFailure);
        if (!created || !platform::PublishDirectory(staging, destination))
        {
            Rollback(projectFile, directories);
            const char* const reason = !created ? "staged project validation failed" : "atomic project publication failed";
            return ReportProjectFailure(output, invocation.Format(), "project create", reason,
                                        diagnostic.result != projects::Result::Success ? &diagnostic : nullptr);
        }

        containers::String publishedFileName;
        Join(destination, fileName, publishedFileName);
        containers::String line;
        if (invocation.Format() == nano::OutputFormat::JsonLines)
        {
            line.Append(containers::StringView("{\"schema\":1,\"event\":\"project-created\",\"name\":"));
            AppendJsonString(line, project.name);
            line.Append(containers::StringView(",\"technicalName\":"));
            AppendJsonString(line, project.technicalName);
            line.Append(containers::StringView(",\"root\":"));
            AppendJsonString(line, destination);
            line.Append(containers::StringView(",\"file\":"));
            AppendJsonString(line, publishedFileName);
            line.Append("}\n", 2);
        }
        else
            line.Append(containers::StringView("Created Vanguard project '"))
                .Append(project.name)
                .Append(containers::StringView("' at\n  "))
                .Append(destination)
                .Append('\n');
        return output.Write(line.AsChar(), line.Length()) ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    nano::ExitCode InspectProject(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        ResolvedProject resolved;
        if (!ResolveProject(invocation.Positional(0), resolved))
            return ReportProjectFailure(output, invocation.Format(), "project inspect", "could not resolve exactly one .vproject document");
        projects::ProjectDescriptor project;
        projects::Diagnostic diagnostic;
        if (!LoadProject(resolved, project, diagnostic))
            return ReportProjectFailure(output, invocation.Format(), "project inspect", diagnostic.message, &diagnostic);
        containers::String line;
        if (invocation.Format() == nano::OutputFormat::JsonLines)
        {
            line.Append(containers::StringView("{\"schema\":1,\"event\":\"project-inspected\",\"name\":"));
            AppendJsonString(line, project.name);
            line.Append(containers::StringView(",\"technicalName\":"));
            AppendJsonString(line, project.technicalName);
            line.Append(containers::StringView(",\"root\":"));
            AppendJsonString(line, resolved.root);
            line.Append(containers::String::Printf(",\"targetCount\":%u,\"pluginCount\":%u}\n", project.targets.Size(), project.plugins.Size()));
        }
        else
        {
            line.Append(containers::StringView("Project: "))
                .Append(project.name)
                .Append(containers::StringView("\nTechnical name: "))
                .Append(project.technicalName)
                .Append(containers::StringView("\nRoot: "))
                .Append(resolved.root)
                .Append(containers::StringView("\nProject file: "))
                .Append(resolved.file)
                .Append(containers::StringView("\nTargets:"));
            for (const containers::String& target : project.targets)
                line.Append(containers::StringView("\n  - ")).Append(target);
            line.Append('\n');
        }
        return output.Write(line.AsChar(), line.Length()) ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    nano::ExitCode ValidateProject(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        const char* const level = invocation.Option("level");
        if (level != nullptr && !Equal(level, "document") && !Equal(level, "layout"))
            return ReportProjectFailure(output, invocation.Format(), "project validate", "supported levels are document and layout");
        ResolvedProject resolved;
        if (!ResolveProject(invocation.Positional(0), resolved))
            return ReportProjectFailure(output, invocation.Format(), "project validate", "could not resolve exactly one .vproject document");
        projects::ProjectDescriptor project;
        projects::Diagnostic diagnostic;
        if (!LoadProject(resolved, project, diagnostic))
            return ReportProjectFailure(output, invocation.Format(), "project validate", diagnostic.message, &diagnostic);
        containers::String layoutFailure;
        const bool checkLayout = level == nullptr || Equal(level, "layout");
        if (checkLayout && !ValidateLayout(resolved, project, layoutFailure))
            return ReportProjectFailure(output, invocation.Format(), "project validate", layoutFailure.AsChar());
        containers::String line;
        if (invocation.Format() == nano::OutputFormat::JsonLines)
        {
            line.Append(containers::StringView("{\"schema\":1,\"event\":\"project-valid\",\"level\":"));
            AppendJsonString(line, checkLayout ? "layout" : "document");
            line.Append(containers::StringView(",\"file\":"));
            AppendJsonString(line, resolved.file);
            line.Append("}\n", 2);
        }
        else
            line.Append(containers::StringView("Project is valid ("))
                .Append(containers::StringView(checkLayout ? "layout" : "document"))
                .Append(containers::StringView("): "))
                .Append(resolved.file)
                .Append('\n');
        return output.Write(line.AsChar(), line.Length()) ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    inline constexpr nano::OptionDescriptor CreateOptions[]{
        {"destination", 'd', nano::OptionValue::Required, "path", "Create the project at this path.", false},
        {"technical-name", 'n', nano::OptionValue::Required, "identifier", "Set the stable technical project name.", false},
        {"target", 't', nano::OptionValue::Required, "triplet", "Add a supported target triplet.", true},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    nano::ExitCode InspectSources(const nano::Invocation& invocation, nano::Output& output) noexcept
    {
        ResolvedProject resolved;
        if (!ResolveProject(invocation.Positional(0), resolved))
            return ReportProjectFailure(output, invocation.Format(), "sources inspect", "could not resolve exactly one .vproject document");
        projects::ProjectDescriptor project;
        projects::Diagnostic diagnostic;
        if (!LoadProject(resolved, project, diagnostic))
            return ReportProjectFailure(output, invocation.Format(), "sources inspect", diagnostic.message, &diagnostic);
        projects::ProjectWorkspace workspace;
        if (projects::ResolveWorkspace(filesystem::AbsolutePath::CreateFilePath(resolved.file), project, workspace, &diagnostic) != projects::Result::Success)
            return ReportProjectFailure(output, invocation.Format(), "sources inspect", diagnostic.message, &diagnostic);
        // Absolute-path reads use the native manager, without installing a
        // global manager, resource loader, or editor service.
        filesystem::Manager files(workspace.root, workspace.root, workspace.derivedData);
        assets::SourceDatabase sources;
        const assets::SourceRoot root{workspace.assets, false};
        filesystem::ScanResult scanFailure;
        if (sources.Rescan(files, {&root, 1}, &scanFailure) != assets::SourceDatabaseResult::Success)
            return ReportProjectFailure(output, invocation.Format(), "sources inspect", "source inventory failed; no partial inventory was published");
        for (const assets::SourceRecord& record : sources.GetResources())
        {
            containers::String line;
            if (invocation.Format() == nano::OutputFormat::JsonLines)
            {
                line.Append("{\"schema\":1,\"event\":\"source-inspected\",\"path\":");
                AppendJsonString(line, record.path.AsStringView());
                line.Append(containers::String::Printf(",\"issues\":%u,\"metadataResult\":%u,\"importState\":%u}\n", record.issues, static_cast<u32>(record.metadataResult), static_cast<u32>(record.importState)));
            }
            else
            {
                line.Append(record.path.AsStringView());
                line.Append(containers::String::Printf(" issues=%u metadata=%u import=%u\n", record.issues, static_cast<u32>(record.metadataResult), static_cast<u32>(record.importState)));
            }
            if (!output.Write(line.AsChar(), line.Length()))
                return nano::ExitCode::InternalFailure;
        }
        const containers::String summary = invocation.Format() == nano::OutputFormat::JsonLines
            ? containers::String::Printf("{\"schema\":1,\"event\":\"sources-inspected\",\"count\":%u}\n", sources.GetResources().Count())
            : containers::String::Printf("Sources: %u (importers not composed)\n", sources.GetResources().Count());
        return output.Write(summary.AsChar(), summary.Length()) ? nano::ExitCode::Success : nano::ExitCode::InternalFailure;
    }

    inline constexpr nano::OptionDescriptor InspectOptions[]{
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
    inline constexpr nano::OptionDescriptor ValidateOptions[]{
        {"level", 'l', nano::OptionValue::Required, "document|layout", "Select the cumulative validation level.", false},
        {"format", '\0', nano::OptionValue::Required, "human|jsonl", "Select stable human or JSON Lines output.", false}};
} // namespace

namespace vanguard::nanovanguard
{
    bool RegisterProjectCommands(CommandRegistry& registry) noexcept
    {
        if (registry.Register({"inspect", "sources", "Inspect shared project source inventory without importing.", "<project>", InspectOptions, static_cast<u32>(sizeof(InspectOptions) / sizeof(InspectOptions[0])), 1, 1, &InspectSources}) != RegistrationResult::Success)
            return false;
        return registry.Register({"create", "project", "Create and atomically publish a Vanguard project.", "<name>", CreateOptions,
                                  static_cast<u32>(sizeof(CreateOptions) / sizeof(CreateOptions[0])), 1, 1, &CreateProject}) == RegistrationResult::Success &&
               registry.Register({"inspect", "project", "Inspect a Vanguard project document.", "<project>", InspectOptions,
                                  static_cast<u32>(sizeof(InspectOptions) / sizeof(InspectOptions[0])), 1, 1, &InspectProject}) ==
                   RegistrationResult::Success &&
               registry.Register({"validate", "project", "Validate a Vanguard project document and layout.", "<project>", ValidateOptions,
                                  static_cast<u32>(sizeof(ValidateOptions) / sizeof(ValidateOptions[0])), 1, 1, &ValidateProject}) ==
                   RegistrationResult::Success;
    }
} // namespace vanguard::nanovanguard
