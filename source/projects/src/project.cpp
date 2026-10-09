#include <vanguard/projects/project.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    namespace project = vanguard::projects;
    namespace ctr = vanguard::containers;
    namespace ser = vanguard::serialization;

    enum Field : u32
    {
        Id,
        Name,
        TechnicalName,
        EngineMin,
        EngineMax,
        Assets,
        Derived,
        Intermediate,
        Saved,
        Builds,
        Config,
        PluginsRoot,
        Cooking,
        Packaging,
        EditorWorld,
        RuntimeWorld,
        Input,
        FieldCount
    };
    constexpr u32 RequiredMask = (1u << FieldCount) - 1u;

    void Fail(project::Diagnostic* d, project::Result r, u32 line, u32 column, const char* field, const char* message) noexcept
    {
        if (d)
            *d = {r, line, column, field, message};
    }

    bool Hex(char c, u8& v) noexcept
    {
        if (c >= '0' && c <= '9')
            v = static_cast<u8>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = static_cast<u8>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = static_cast<u8>(c - 'A' + 10);
        else
            return false;
        return true;
    }

    bool ParseU16(ctr::StringView s, u16& value) noexcept
    {
        if (s.Empty())
            return false;
        u32 number = 0;
        for (char c : s)
        {
            if (c < '0' || c > '9')
                return false;
            number = number * 10u + static_cast<u32>(c - '0');
            if (number > 65535)
                return false;
        }
        value = static_cast<u16>(number);
        return true;
    }

    bool ParseVersion(ctr::StringView s, u16& major, u16& minor, u16& patch, bool allowWildcard, bool& wildcard) noexcept
    {
        const u32 first = s.Find('.');
        if (first == ctr::StringView::npos)
            return false;
        const u32 second = s.Find('.', first + 1);
        if (second == ctr::StringView::npos || s.Find('.', second + 1) != ctr::StringView::npos)
            return false;
        if (!ParseU16(s.Slice(0, first), major) || !ParseU16(s.Slice(first + 1, second), minor))
            return false;
        const ctr::StringView tail = s.SubView(second + 1);
        wildcard = allowWildcard && tail == "x";
        return wildcard || ParseU16(tail, patch);
    }

    u32 Utf8ScalarCount(ctr::StringView text) noexcept
    {
        u32 count = 0;
        for (u32 index = 0; index < text.Length(); ++index)
        {
            if ((static_cast<u8>(text[index]) & 0xC0u) != 0x80u)
                ++count;
        }
        return count;
    }

    bool SafeRelative(ctr::StringView s) noexcept
    {
        if (s.Empty() || s[0] == '/' || s[0] == '\\' || s.Back() == '/' || (s.Length() > 1 && s[1] == ':'))
            return false;
        u32 start = 0;
        for (u32 i = 0; i <= s.Length(); ++i)
        {
            if (i != s.Length() && s[i] == '\\')
                return false;
            if (i != s.Length() && s[i] != '/')
                continue;
            const ctr::StringView part = s.Slice(start, i);
            if (part.Empty() || part == "." || part == "..")
                return false;
            start = i + 1;
        }
        return true;
    }

    bool ResourceIdentity(ctr::StringView identity) noexcept
    {
        if (identity.Empty())
            return true;
        if (identity.Length() > 1024 || !ser::IsValidUtf8(identity))
            return false;
        for (char c : identity)
        {
            if (static_cast<u8>(c) < 0x20u)
                return false;
        }
        return true;
    }

    bool GetKey(ctr::StringView key, Field& field, const char*& name) noexcept
    {
#define VG_FIELD(text, value)                                                                                                                                  \
    if (key == text)                                                                                                                                           \
    {                                                                                                                                                          \
        field = value;                                                                                                                                         \
        name = text;                                                                                                                                           \
        return true;                                                                                                                                           \
    }
        // clang-format off
        VG_FIELD("project.id", Id)
        VG_FIELD("project.name", Name)
        VG_FIELD("project.technicalName", TechnicalName)
        VG_FIELD("engine.minimum", EngineMin)
        VG_FIELD("engine.maximum", EngineMax)
        VG_FIELD("paths.assets", Assets)
        VG_FIELD("paths.derivedData", Derived)
        VG_FIELD("paths.intermediate", Intermediate)
        VG_FIELD("paths.saved", Saved)
        VG_FIELD("paths.builds", Builds)
        VG_FIELD("paths.config", Config)
        VG_FIELD("paths.plugins", PluginsRoot)
        VG_FIELD("policy.cooking", Cooking)
        VG_FIELD("policy.packaging", Packaging)
        VG_FIELD("startup.editorWorld", EditorWorld)
        VG_FIELD("startup.runtimeWorld", RuntimeWorld)
        VG_FIELD("startup.input", Input)
        // clang-format on
#undef VG_FIELD
        return false;
    }

    void Assign(project::ProjectDescriptor& p, Field f, ctr::String&& value) noexcept
    {
        switch (f)
        {
        case Name:
            p.name = static_cast<ctr::String&&>(value);
            break;
        case TechnicalName:
            p.technicalName = static_cast<ctr::String&&>(value);
            break;
        case Assets:
            p.assets = static_cast<ctr::String&&>(value);
            break;
        case Derived:
            p.derivedData = static_cast<ctr::String&&>(value);
            break;
        case Intermediate:
            p.intermediate = static_cast<ctr::String&&>(value);
            break;
        case Saved:
            p.saved = static_cast<ctr::String&&>(value);
            break;
        case Builds:
            p.builds = static_cast<ctr::String&&>(value);
            break;
        case Config:
            p.config = static_cast<ctr::String&&>(value);
            break;
        case PluginsRoot:
            p.pluginsRoot = static_cast<ctr::String&&>(value);
            break;
        case Cooking:
            p.cookingPolicy = static_cast<ctr::String&&>(value);
            break;
        case Packaging:
            p.packagingPolicy = static_cast<ctr::String&&>(value);
            break;
        case EditorWorld:
            p.editorWorld = static_cast<ctr::String&&>(value);
            break;
        case RuntimeWorld:
            p.runtimeWorld = static_cast<ctr::String&&>(value);
            break;
        case Input:
            p.input = static_cast<ctr::String&&>(value);
            break;
        default:
            break;
        }
    }

} // namespace

namespace vanguard::projects
{
    bool ParseProjectId(const containers::StringView text, ProjectId& projectId) noexcept
    {
        if (text.Length() != ProjectIdTextLength || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
            return false;
        u8 bytes[16]{};
        u32 output = 0;
        for (u32 index = 0; index < text.Length();)
        {
            if (text[index] == '-')
            {
                ++index;
                continue;
            }
            if (index + 1u >= text.Length() || output == 16u)
                return false;
            u8 high = 0;
            u8 low = 0;
            if (!Hex(text[index], high) || !Hex(text[index + 1u], low))
                return false;
            bytes[output++] = static_cast<u8>((high << 4u) | low);
            index += 2u;
        }
        if (output != 16u)
            return false;
        ProjectId parsed;
        for (u32 part = 0; part < 4; ++part)
            parsed.parts[part] = (static_cast<u32>(bytes[part * 4u]) << 24u) |
                                 (static_cast<u32>(bytes[part * 4u + 1u]) << 16u) |
                                 (static_cast<u32>(bytes[part * 4u + 2u]) << 8u) |
                                 bytes[part * 4u + 3u];
        if (!parsed.IsValid())
            return false;
        projectId = parsed;
        return true;
    }

    bool FormatProjectId(const ProjectId& projectId, char* const destination, const u32 capacity) noexcept
    {
        if (!projectId.IsValid() || destination == nullptr || capacity < ProjectIdTextBufferSize)
            return false;
        constexpr char hex[] = "0123456789abcdef";
        u32 at = 0;
        for (u32 part = 0; part < 4; ++part)
            for (i32 shift = 28; shift >= 0; shift -= 4)
            {
                destination[at++] = hex[(projectId.parts[part] >> shift) & 15u];
                if (at == 8u || at == 13u || at == 18u || at == 23u)
                    destination[at++] = '-';
            }
        destination[ProjectIdTextLength] = '\0';
        return at == ProjectIdTextLength;
    }

    bool IsTechnicalName(const containers::StringView name) noexcept
    {
        if (name.Empty() || name.Length() > MaximumTechnicalNameBytes || name.Front() == '-' || name.Back() == '-')
            return false;
        bool lastDash = false;
        for (const char c : name)
        {
            const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!valid || (c == '-' && lastDash))
                return false;
            lastDash = c == '-';
        }
        return true;
    }

    bool ProjectId::IsValid() const noexcept
    {
        return parts[0] || parts[1] || parts[2] || parts[3];
    }

    Result Validate(const ProjectDescriptor& p, Diagnostic* d) noexcept
    {
        if (d)
            *d = {};
        if (!p.id.IsValid())
        {
            Fail(d, Result::InvalidValue, 0, 0, "project.id", "project identity is zero");
            return Result::InvalidValue;
        }
        if (p.name.Empty() || !ser::IsValidUtf8(p.name) || Utf8ScalarCount(p.name) > 128)
        {
            Fail(d, Result::InvalidValue, 0, 0, "project.name", "project display name must contain 1-128 Unicode scalar values");
            return Result::InvalidValue;
        }
        if (!IsTechnicalName(p.technicalName))
        {
            Fail(d, Result::InvalidValue, 0, 0, "project.technicalName", "invalid technical name");
            return Result::InvalidValue;
        }
        if (p.engine.maximumMajor != p.engine.minimumMajor || p.engine.maximumMinor < p.engine.minimumMinor ||
            (p.engine.maximumMinor == p.engine.minimumMinor && !p.engine.maximumPatchWildcard && p.engine.maximumPatch < p.engine.minimumPatch))
        {
            Fail(d, Result::InvalidValue, 0, 0, "engine.maximum", "invalid engine compatibility range");
            return Result::InvalidValue;
        }
        const ctr::String* paths[]{&p.assets, &p.derivedData, &p.intermediate, &p.saved, &p.builds, &p.config, &p.pluginsRoot};
        for (const ctr::String* path : paths)
            if (!SafeRelative(*path))
            {
                Fail(d, Result::InvalidValue, 0, 0, "paths", "project roots must be normalized relative paths");
                return Result::InvalidValue;
            }
        for (u32 i = 0; i < 7; ++i)
            for (u32 j = i + 1; j < 7; ++j)
                if (*paths[i] == *paths[j])
                {
                    Fail(d, Result::InvalidValue, 0, 0, "paths", "project roots must be distinct");
                    return Result::InvalidValue;
                }
        if (p.targets.Empty() || p.targets.Size() > MaximumTargets)
        {
            Fail(d, Result::InvalidValue, 0, 0, "target", "at least one bounded target is required");
            return Result::InvalidValue;
        }
        for (u32 i = 0; i < p.targets.Size(); ++i)
        {
            if (!IsTechnicalName(p.targets[i]))
            {
                Fail(d, Result::InvalidValue, 0, 0, "target", "invalid target identifier");
                return Result::InvalidValue;
            }
            for (u32 j = 0; j < i; ++j)
                if (p.targets[i] == p.targets[j])
                {
                    Fail(d, Result::InvalidValue, 0, 0, "target", "duplicate target");
                    return Result::InvalidValue;
                }
        }
        if (p.plugins.Size() > MaximumPlugins)
        {
            Fail(d, Result::LimitExceeded, 0, 0, "plugin", "too many plugins");
            return Result::LimitExceeded;
        }
        for (u32 i = 0; i < p.plugins.Size(); ++i)
        {
            if (!IsTechnicalName(p.plugins[i]))
            {
                Fail(d, Result::InvalidValue, 0, 0, "plugin", "invalid plugin identifier");
                return Result::InvalidValue;
            }
            for (u32 j = 0; j < i; ++j)
                if (p.plugins[i] == p.plugins[j])
                {
                    Fail(d, Result::InvalidValue, 0, 0, "plugin", "duplicate plugin");
                    return Result::InvalidValue;
                }
        }
        if (!IsTechnicalName(p.cookingPolicy) || !IsTechnicalName(p.packagingPolicy))
        {
            Fail(d, Result::InvalidValue, 0, 0, "policy", "invalid policy identifier");
            return Result::InvalidValue;
        }
        if (!ResourceIdentity(p.editorWorld) || !ResourceIdentity(p.runtimeWorld) || !ResourceIdentity(p.input))
        {
            Fail(d, Result::InvalidValue, 0, 0, "startup", "invalid startup resource identity");
            return Result::InvalidValue;
        }
        return Result::Success;
    }

    Result Parse(ctr::StringView text, ProjectDescriptor& output, Diagnostic* d) noexcept
    {
        if (d)
            *d = {};
        if (text.Empty())
        {
            Fail(d, Result::InvalidSyntax, 1, 1, nullptr, "empty project document");
            return Result::InvalidSyntax;
        }
        if (text.Length() > MaximumProjectFileBytes)
        {
            Fail(d, Result::FileTooLarge, 0, 0, nullptr, "project document exceeds size limit");
            return Result::FileTooLarge;
        }
        ser::TextDocumentReader reader(text);
        if (!reader.IsUtf8Valid())
        {
            Fail(d, Result::InvalidUtf8, 0, 0, nullptr, "document is not valid UTF-8");
            return Result::InvalidUtf8;
        }

        ser::TextDocumentHeader documentHeader;
        ser::TextFailure textFailure;
        const ser::Result headerResult = reader.ReadHeader(documentHeader, &textFailure);
        if (headerResult == ser::Result::EndOfStream)
        {
            Fail(d, Result::MissingField, 1, 1, nullptr, "missing project header");
            return Result::MissingField;
        }
        if (headerResult != ser::Result::Success || documentHeader.kind != "vproject" ||
            documentHeader.version.major != ProjectSchemaMajor || documentHeader.version.minor != ProjectSchemaMinor)
        {
            const u32 line = headerResult == ser::Result::Success ? documentHeader.line : textFailure.line;
            Fail(d, Result::UnsupportedVersion, line != 0 ? line : 1, 1, nullptr, "expected vproject 1.0 header");
            return Result::UnsupportedVersion;
        }

        ProjectDescriptor candidate;
        u32 mask = 0;
        ctr::String decoded;
        for (;;)
        {
            ser::TextAssignment assignment;
            const ser::Result assignmentResult = reader.ReadAssignment(assignment, decoded, &textFailure);
            if (assignmentResult == ser::Result::EndOfStream)
                break;
            if (assignmentResult != ser::Result::Success)
            {
                if (assignmentResult == ser::Result::LimitExceeded ||
                    textFailure.code == ser::TextErrorCode::AllocationFailed)
                {
                    Fail(d, Result::LimitExceeded, textFailure.line, textFailure.column, nullptr,
                         "project string allocation failed");
                    return Result::LimitExceeded;
                }
                const char* message = "expected field assignment";
                switch (textFailure.code)
                {
                case ser::TextErrorCode::ExpectedQuotedValue:
                    message = "field value must be quoted";
                    break;
                case ser::TextErrorCode::UnterminatedQuotedValue:
                    message = "unterminated quoted value";
                    break;
                case ser::TextErrorCode::UnexpectedTrailingText:
                    message = "unexpected text after field value";
                    break;
                case ser::TextErrorCode::InvalidEscape:
                    message = "invalid string escape";
                    break;
                default:
                    break;
                }
                Fail(d, Result::InvalidSyntax, textFailure.line, textFailure.column, nullptr, message);
                return Result::InvalidSyntax;
            }
            const ctr::StringView key = assignment.key;
            const u32 lineNo = assignment.line;
            const u32 valueColumn = assignment.valueColumn;
            if (key == "target")
            {
                if (candidate.targets.Size() == MaximumTargets)
                {
                    Fail(d, Result::LimitExceeded, lineNo, 1, "target", "too many targets");
                    return Result::LimitExceeded;
                }
                candidate.targets.PushBack(static_cast<ctr::String&&>(decoded));
                continue;
            }
            if (key == "plugin")
            {
                if (candidate.plugins.Size() == MaximumPlugins)
                {
                    Fail(d, Result::LimitExceeded, lineNo, 1, "plugin", "too many plugins");
                    return Result::LimitExceeded;
                }
                candidate.plugins.PushBack(static_cast<ctr::String&&>(decoded));
                continue;
            }
            Field field{};
            const char* fieldName = nullptr;
            if (!GetKey(key, field, fieldName))
            {
                Fail(d, Result::UnknownField, lineNo, 1, nullptr, "unknown project field");
                return Result::UnknownField;
            }
            const u32 bit = 1u << field;
            if (mask & bit)
            {
                Fail(d, Result::DuplicateField, lineNo, 1, fieldName, "duplicate scalar field");
                return Result::DuplicateField;
            }
            mask |= bit;
            bool wild = false;
            if (field == Id)
            {
                if (!ParseProjectId(decoded, candidate.id))
                {
                    Fail(d, Result::InvalidValue, lineNo, valueColumn, fieldName, "invalid UUID");
                    return Result::InvalidValue;
                }
            }
            else if (field == EngineMin)
            {
                if (!ParseVersion(decoded, candidate.engine.minimumMajor, candidate.engine.minimumMinor, candidate.engine.minimumPatch, false, wild))
                {
                    Fail(d, Result::InvalidValue, lineNo, valueColumn, fieldName, "invalid semantic version");
                    return Result::InvalidValue;
                }
            }
            else if (field == EngineMax)
            {
                if (!ParseVersion(decoded, candidate.engine.maximumMajor, candidate.engine.maximumMinor, candidate.engine.maximumPatch, true,
                                  candidate.engine.maximumPatchWildcard))
                {
                    Fail(d, Result::InvalidValue, lineNo, valueColumn, fieldName, "invalid semantic version range");
                    return Result::InvalidValue;
                }
            }
            else
                Assign(candidate, field, static_cast<ctr::String&&>(decoded));
        }
        if (mask != RequiredMask)
        {
            Fail(d, Result::MissingField, 0, 0, nullptr, "required project field is missing");
            return Result::MissingField;
        }
        const Result valid = Validate(candidate, d);
        if (valid != Result::Success)
            return valid;
        output = static_cast<ProjectDescriptor&&>(candidate);
        return Result::Success;
    }

    Result Read(filesystem::IFile& file, ProjectDescriptor& p, Diagnostic* d) noexcept
    {
        if (!file.IsReader())
        {
            Fail(d, Result::InvalidArgument, 0, 0, nullptr, "file is not readable");
            return Result::InvalidArgument;
        }
        const u64 size = file.GetSize();
        if (size > MaximumProjectFileBytes)
        {
            Fail(d, Result::FileTooLarge, 0, 0, nullptr, "project file exceeds size limit");
            return Result::FileTooLarge;
        }
        ctr::String text;
        if (!text.Resize(static_cast<u32>(size)))
        {
            Fail(d, Result::LimitExceeded, 0, 0, nullptr, "project buffer allocation failed");
            return Result::LimitExceeded;
        }
        if (size)
            file.Serialize(text.AsChar(), static_cast<usize>(size));
        if (file.HasErrors())
        {
            Fail(d, Result::IoFailure, 0, 0, nullptr, "project file read failed");
            return Result::IoFailure;
        }
        return Parse(text, p, d);
    }

    Result Write(filesystem::IFile& file, const ProjectDescriptor& p, Diagnostic* d) noexcept
    {
        if (d)
            *d = {};
        if (!file.IsWriter())
        {
            Fail(d, Result::InvalidArgument, 0, 0, nullptr, "file is not writable");
            return Result::InvalidArgument;
        }
        const Result valid = Validate(p, d);
        if (valid != Result::Success)
            return valid;
        ctr::String text;
        const auto appendBlankLine = [&]() noexcept {
            if (!text.Reserve(text.Length() + 1u))
                return false;
            text.Append('\n');
            return true;
        };
        const auto appendField = [&](const ctr::StringView key, const ctr::StringView value) noexcept {
            return ser::AppendTextAssignment(text, key, value) == ser::Result::Success;
        };
        if (ser::AppendTextDocumentHeader(text, "vproject", {ProjectSchemaMajor, ProjectSchemaMinor}) !=
                ser::Result::Success ||
            !appendBlankLine())
        {
            Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
            return Result::LimitExceeded;
        }
        char id[ProjectIdTextBufferSize]{};
        if (!FormatProjectId(p.id, id, ProjectIdTextBufferSize))
        {
            Fail(d, Result::InvalidValue, 0, 0, "project.id", "project identity is zero");
            return Result::InvalidValue;
        }
        if (!appendField("project.id", id) || !appendField("project.name", p.name) ||
            !appendField("project.technicalName", p.technicalName) || !appendBlankLine())
        {
            Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
            return Result::LimitExceeded;
        }
        ctr::String min = ctr::String::Printf("%u.%u.%u", p.engine.minimumMajor, p.engine.minimumMinor, p.engine.minimumPatch);
        ctr::String max = p.engine.maximumPatchWildcard ? ctr::String::Printf("%u.%u.x", p.engine.maximumMajor, p.engine.maximumMinor)
                                                        : ctr::String::Printf("%u.%u.%u", p.engine.maximumMajor, p.engine.maximumMinor, p.engine.maximumPatch);
        if (!appendField("engine.minimum", min) || !appendField("engine.maximum", max) || !appendBlankLine() ||
            !appendField("paths.assets", p.assets) || !appendField("paths.derivedData", p.derivedData) ||
            !appendField("paths.intermediate", p.intermediate) || !appendField("paths.saved", p.saved) ||
            !appendField("paths.builds", p.builds) || !appendField("paths.config", p.config) ||
            !appendField("paths.plugins", p.pluginsRoot) || !appendBlankLine())
        {
            Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
            return Result::LimitExceeded;
        }
        for (const ctr::String& target : p.targets)
            if (!appendField("target", target))
            {
                Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
                return Result::LimitExceeded;
            }
        for (const ctr::String& plugin : p.plugins)
            if (!appendField("plugin", plugin))
            {
                Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
                return Result::LimitExceeded;
            }
        if (!appendBlankLine() || !appendField("policy.cooking", p.cookingPolicy) ||
            !appendField("policy.packaging", p.packagingPolicy) || !appendBlankLine() ||
            !appendField("startup.editorWorld", p.editorWorld) || !appendField("startup.runtimeWorld", p.runtimeWorld) ||
            !appendField("startup.input", p.input))
        {
            Fail(d, Result::LimitExceeded, 0, 0, nullptr, "canonical project buffer allocation failed");
            return Result::LimitExceeded;
        }
        if (text.Length() > MaximumProjectFileBytes)
        {
            Fail(d, Result::FileTooLarge, 0, 0, nullptr, "canonical project document exceeds size limit");
            return Result::FileTooLarge;
        }
        file.Serialize(text.AsChar(), text.Length());
        file.Flush();
        if (file.HasErrors())
        {
            Fail(d, Result::IoFailure, 0, 0, nullptr, "project file write failed");
            return Result::IoFailure;
        }
        return Result::Success;
    }
} // namespace vanguard::projects
