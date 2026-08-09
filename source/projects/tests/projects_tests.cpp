#include <vanguard/projects/project.hpp>

#include <cstdio>

namespace
{
    namespace containers = vanguard::containers;
    namespace filesystem = vanguard::filesystem;
    namespace memory = vanguard::memory;
    namespace projects = vanguard::projects;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[projectsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    projects::ProjectDescriptor MakeProject()
    {
        projects::ProjectDescriptor project;
        project.id = {{0x7f71a8a7u, 0x9be84cebu, 0x897dc837u, 0xbd3e0fd6u}};
        project.name = "Project Cardinal";
        project.technicalName = "project-cardinal";
        project.engine = {0, 1, 0, 0, 1, true, 0};
        project.targets.PushBack("windows-x64");
        project.plugins.PushBack("example-plugin");
        project.editorWorld = "worlds/editor-start";
        project.runtimeWorld = "worlds/cardinal-start";
        project.input = "input/default";
        return project;
    }

    bool Equal(const projects::ProjectDescriptor& left, const projects::ProjectDescriptor& right)
    {
        if (!(left.id == right.id) || left.name != right.name || left.technicalName != right.technicalName ||
            left.engine.minimumMajor != right.engine.minimumMajor || left.engine.minimumMinor != right.engine.minimumMinor ||
            left.engine.minimumPatch != right.engine.minimumPatch || left.engine.maximumMajor != right.engine.maximumMajor ||
            left.engine.maximumMinor != right.engine.maximumMinor ||
            left.engine.maximumPatchWildcard != right.engine.maximumPatchWildcard ||
            left.engine.maximumPatch != right.engine.maximumPatch || left.assets != right.assets || left.derivedData != right.derivedData ||
            left.intermediate != right.intermediate || left.saved != right.saved || left.builds != right.builds ||
            left.config != right.config || left.pluginsRoot != right.pluginsRoot || left.cookingPolicy != right.cookingPolicy ||
            left.packagingPolicy != right.packagingPolicy || left.editorWorld != right.editorWorld ||
            left.runtimeWorld != right.runtimeWorld || left.input != right.input || left.targets.Size() != right.targets.Size() ||
            left.plugins.Size() != right.plugins.Size())
            return false;

        for (vanguard::u32 index = 0; index < left.targets.Size(); ++index)
            if (left.targets[index] != right.targets[index])
                return false;
        for (vanguard::u32 index = 0; index < left.plugins.Size(); ++index)
            if (left.plugins[index] != right.plugins[index])
                return false;
        return true;
    }

    bool EqualBytes(const containers::DynamicArray<vanguard::u8>& left, const containers::DynamicArray<vanguard::u8>& right)
    {
        if (left.Size() != right.Size())
            return false;
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
            if (left[index] != right[index])
                return false;
        return true;
    }

    containers::StringView TextView(const containers::DynamicArray<vanguard::u8>& bytes)
    {
        return {reinterpret_cast<const char*>(bytes.Data()), bytes.Size()};
    }

    containers::String Text(const containers::DynamicArray<vanguard::u8>& bytes)
    {
        containers::String result;
        result.Append(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        return result;
    }
} // namespace

int main()
{
    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");

    projects::ProjectDescriptor source = MakeProject();
    projects::Diagnostic diagnostic;
    Check(projects::Validate(source, &diagnostic) == projects::Result::Success, "valid descriptor accepted");

    containers::DynamicArray<vanguard::u8> firstBytes(memory::pools::Filesystem::GetInstance());
    {
        filesystem::MemoryFileWriter writer(firstBytes);
        Check(projects::Write(writer, source, &diagnostic) == projects::Result::Success, "canonical document write");
    }

    projects::ProjectDescriptor parsed;
    Check(projects::Parse(TextView(firstBytes), parsed, &diagnostic) == projects::Result::Success, "canonical document parse");
    Check(Equal(source, parsed), "write and parse preserve the complete descriptor");

    containers::DynamicArray<vanguard::u8> secondBytes(memory::pools::Filesystem::GetInstance());
    {
        filesystem::MemoryFileWriter writer(secondBytes);
        Check(projects::Write(writer, parsed, &diagnostic) == projects::Result::Success, "second canonical write");
    }
    Check(EqualBytes(firstBytes, secondBytes), "canonical output is byte deterministic");

    {
        filesystem::MemoryFileReader reader(firstBytes, 0);
        projects::ProjectDescriptor read;
        Check(projects::Read(reader, read, &diagnostic) == projects::Result::Success && Equal(source, read),
              "IFile read path preserves the descriptor");
    }

    containers::String commented = Text(firstBytes);
    commented.Append("# trailing document comment\n", 28);
    projects::ProjectDescriptor commentedProject;
    Check(projects::Parse(commented, commentedProject, &diagnostic) == projects::Result::Success, "whole-line comments are accepted");

    const char* const escapedDocument = "vproject 1.0 # schema\n"
                                        "project.id = \"7f71a8a7-9be8-4ceb-897d-c837bd3e0fd6\"\n"
                                        "project.name = \"Cardinal \\uD83E\\uDE78 \\\"Blood\\\"\" # escaped Unicode and quote\n"
                                        "project.technicalName = \"project-cardinal\"\n"
                                        "engine.minimum = \"0.1.0\"\nengine.maximum = \"0.1.x\"\n"
                                        "paths.assets = \"Assets\"\npaths.derivedData = \"DerivedData\"\n"
                                        "paths.intermediate = \"Intermediate\"\npaths.saved = \"Saved\"\npaths.builds = \"Builds\"\n"
                                        "paths.config = \"Config\"\npaths.plugins = \"Plugins\"\n"
                                        "target = \"windows-x64\" # host target\n"
                                        "policy.cooking = \"default\"\npolicy.packaging = \"default\"\n"
                                        "startup.editorWorld = \"\"\nstartup.runtimeWorld = \"\"\nstartup.input = \"\"\n";
    projects::ProjectDescriptor escaped;
    Check(projects::Parse(escapedDocument, escaped, &diagnostic) == projects::Result::Success,
          "JSON escapes, surrogate pairs, and trailing comments parse");

    projects::ProjectDescriptor sentinel = MakeProject();
    sentinel.name = "unchanged";
    containers::String duplicate = Text(firstBytes);
    duplicate.Append("project.name = \"duplicate\"\n", 27);
    Check(projects::Parse(duplicate, sentinel, &diagnostic) == projects::Result::DuplicateField && sentinel.name == "unchanged" &&
              diagnostic.line != 0,
          "duplicate scalar rejection is transactional and located");

    containers::String unknown = Text(firstBytes);
    unknown.Append("future.field = \"value\"\n", 23);
    Check(projects::Parse(unknown, sentinel, &diagnostic) == projects::Result::UnknownField, "unknown fields are rejected");
    Check(projects::Parse("vproject 1.0\n", sentinel, &diagnostic) == projects::Result::MissingField,
          "missing required fields are rejected");

    const char invalidUtf8[]{
        'v', 'p', 'r', 'o', 'j', 'e', 'c', 't', ' ', '1', '.', '0', '\n', static_cast<char>(0xC0), static_cast<char>(0xAF)};
    Check(projects::Parse(containers::StringView(invalidUtf8, sizeof(invalidUtf8)), sentinel, &diagnostic) == projects::Result::InvalidUtf8,
          "malformed UTF-8 is rejected");

    projects::ProjectDescriptor invalid = MakeProject();
    invalid.assets = "../Assets";
    Check(projects::Validate(invalid, &diagnostic) == projects::Result::InvalidValue, "parent traversal is rejected");
    invalid = MakeProject();
    invalid.assets = "Content\\Assets";
    Check(projects::Validate(invalid, &diagnostic) == projects::Result::InvalidValue, "non-canonical path separators are rejected");
    invalid = MakeProject();
    invalid.plugins.PushBack("example-plugin");
    Check(projects::Validate(invalid, &diagnostic) == projects::Result::InvalidValue, "duplicate plugins are rejected");
    invalid = MakeProject();
    invalid.engine.maximumPatchWildcard = false;
    invalid.engine.maximumPatch = 0;
    invalid.engine.minimumPatch = 1;
    Check(projects::Validate(invalid, &diagnostic) == projects::Result::InvalidValue, "descending patch ranges are rejected");

    containers::String oversized;
    Check(oversized.Resize(projects::MaximumProjectFileBytes + 1), "oversized test allocation");
    Check(projects::Parse(oversized, sentinel, &diagnostic) == projects::Result::FileTooLarge,
          "direct parsing enforces the document size bound");

    if (g_failures == 0)
        std::printf("Vanguard project document conformance passed.\n");
    return g_failures == 0 ? 0 : 1;
}
