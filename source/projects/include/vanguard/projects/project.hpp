#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::projects
{
    inline constexpr u16 ProjectSchemaMajor = 1;
    inline constexpr u16 ProjectSchemaMinor = 0;
    inline constexpr u32 MaximumProjectFileBytes = 64u * 1024u;
    inline constexpr u32 MaximumTargets = 16;
    inline constexpr u32 MaximumPlugins = 128;

    struct ProjectId
    {
        u32 parts[4]{};
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] friend bool operator==(const ProjectId&, const ProjectId&) noexcept = default;
    };

    struct EngineVersionRange
    {
        u16 minimumMajor = 0, minimumMinor = 0, minimumPatch = 0;
        u16 maximumMajor = 0, maximumMinor = 0;
        bool maximumPatchWildcard = false;
        u16 maximumPatch = 0;
    };

    struct ProjectDescriptor
    {
        ProjectDescriptor() noexcept : targets(memory::pools::Tools::GetInstance()), plugins(memory::pools::Tools::GetInstance()) {}

        ProjectId id;
        containers::String name;
        containers::String technicalName;
        EngineVersionRange engine;
        containers::String assets{"Assets"};
        containers::String derivedData{"DerivedData"};
        containers::String intermediate{"Intermediate"};
        containers::String saved{"Saved"};
        containers::String builds{"Builds"};
        containers::String config{"Config"};
        containers::String pluginsRoot{"Plugins"};
        containers::DynamicArray<containers::String> targets;
        containers::DynamicArray<containers::String> plugins;
        containers::String cookingPolicy{"default"};
        containers::String packagingPolicy{"default"};
        containers::String editorWorld;
        containers::String runtimeWorld;
        containers::String input;
    };

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        IoFailure,
        FileTooLarge,
        InvalidUtf8,
        InvalidSyntax,
        UnsupportedVersion,
        UnknownField,
        DuplicateField,
        MissingField,
        InvalidValue,
        LimitExceeded
    };

    struct Diagnostic
    {
        Result result = Result::Success;
        u32 line = 0;
        u32 column = 0;
        const char* field = nullptr;
        const char* message = nullptr;
    };

    [[nodiscard]] Result Parse(containers::StringView text, ProjectDescriptor& project, Diagnostic* diagnostic = nullptr) noexcept;
    [[nodiscard]] Result Read(filesystem::IFile& file, ProjectDescriptor& project, Diagnostic* diagnostic = nullptr) noexcept;
    [[nodiscard]] Result Write(filesystem::IFile& file, const ProjectDescriptor& project, Diagnostic* diagnostic = nullptr) noexcept;
    [[nodiscard]] Result Validate(const ProjectDescriptor& project, Diagnostic* diagnostic = nullptr) noexcept;
} // namespace vanguard::projects
