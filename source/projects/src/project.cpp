#include <vanguard/projects/project.hpp>

namespace
{
    using namespace vanguard;
    namespace project = vanguard::projects;
    namespace ctr = vanguard::containers;

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

    bool ParseId(ctr::StringView s, project::ProjectId& id) noexcept
    {
        if (s.Length() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
            return false;
        u8 bytes[16]{};
        u32 out = 0;
        for (u32 i = 0; i < s.Length();)
        {
            if (s[i] == '-')
            {
                ++i;
                continue;
            }
            if (i + 1 >= s.Length() || out == 16)
                return false;
            u8 high, low;
            if (!Hex(s[i], high) || !Hex(s[i + 1], low))
                return false;
            bytes[out++] = static_cast<u8>((high << 4u) | low);
            i += 2;
        }
        if (out != 16)
            return false;
        for (u32 p = 0; p < 4; ++p)
            id.parts[p] = (static_cast<u32>(bytes[p * 4]) << 24u) | (static_cast<u32>(bytes[p * 4 + 1]) << 16u) | (static_cast<u32>(bytes[p * 4 + 2]) << 8u) |
                          bytes[p * 4 + 3];
        return id.IsValid();
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

    bool ValidUtf8(ctr::StringView s) noexcept
    {
        for (u32 i = 0; i < s.Length();)
        {
            const u8 first = static_cast<u8>(s[i++]);
            if (first < 0x80)
                continue;
            u32 count = 0, code = 0, minimum = 0;
            if ((first & 0xE0) == 0xC0)
            {
                count = 1;
                code = first & 0x1F;
                minimum = 0x80;
            }
            else if ((first & 0xF0) == 0xE0)
            {
                count = 2;
                code = first & 0x0F;
                minimum = 0x800;
            }
            else if ((first & 0xF8) == 0xF0)
            {
                count = 3;
                code = first & 0x07;
                minimum = 0x10000;
            }
            else
                return false;
            if (i + count > s.Length())
                return false;
            while (count--)
            {
                const u8 next = static_cast<u8>(s[i++]);
                if ((next & 0xC0) != 0x80)
                    return false;
                code = (code << 6) | (next & 0x3F);
            }
            if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                return false;
        }
        return true;
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

    bool AppendCodepoint(ctr::String& out, u32 code) noexcept
    {
        if (code <= 0x7F)
            return out.Append(static_cast<char>(code)), true;
        char bytes[4];
        u32 count;
        if (code <= 0x7FF)
        {
            bytes[0] = static_cast<char>(0xC0 | (code >> 6));
            bytes[1] = static_cast<char>(0x80 | (code & 0x3F));
            count = 2;
        }
        else if (code <= 0xFFFF)
        {
            bytes[0] = static_cast<char>(0xE0 | (code >> 12));
            bytes[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            bytes[2] = static_cast<char>(0x80 | (code & 0x3F));
            count = 3;
        }
        else
        {
            bytes[0] = static_cast<char>(0xF0 | (code >> 18));
            bytes[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            bytes[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            bytes[3] = static_cast<char>(0x80 | (code & 0x3F));
            count = 4;
        }
        out.Append(bytes, count);
        return true;
    }

    bool DecodeString(ctr::StringView source, ctr::String& output) noexcept
    {
        output.Clear();
        for (u32 i = 0; i < source.Length(); ++i)
        {
            char c = source[i];
            if (c != '\\')
            {
                if (static_cast<u8>(c) < 0x20u)
                    return false;
                output.Append(c);
                continue;
            }
            if (++i >= source.Length())
                return false;
            c = source[i];
            if (c == '"' || c == '\\')
                output.Append(c);
            else if (c == 'n')
                output.Append('\n');
            else if (c == 'r')
                output.Append('\r');
            else if (c == 't')
                output.Append('\t');
            else if (c == 'u')
            {
                if (i + 4 >= source.Length())
                    return false;
                u32 code = 0;
                for (u32 n = 0; n < 4; ++n)
                {
                    u8 v;
                    if (!Hex(source[++i], v))
                        return false;
                    code = (code << 4) | v;
                }
                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    if (i + 6 >= source.Length() || source[i + 1] != '\\' || source[i + 2] != 'u')
                        return false;
                    i += 2;
                    u32 low = 0;
                    for (u32 n = 0; n < 4; ++n)
                    {
                        u8 v;
                        if (!Hex(source[++i], v))
                            return false;
                        low = (low << 4) | v;
                    }
                    if (low < 0xDC00 || low > 0xDFFF)
                        return false;
                    code = 0x10000u + ((code - 0xD800u) << 10u) + (low - 0xDC00u);
                }
                else if (code >= 0xDC00 && code <= 0xDFFF)
                    return false;
                AppendCodepoint(output, code);
            }
            else
                return false;
        }
        return ValidUtf8(output);
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

    bool IsTechnicalName(ctr::StringView s) noexcept
    {
        if (s.Empty() || s.Length() > 64 || s.Front() == '-' || s.Back() == '-')
            return false;
        bool lastDash = false;
        for (char c : s)
        {
            const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!valid || (c == '-' && lastDash))
                return false;
            lastDash = c == '-';
        }
        return true;
    }

    bool ResourceIdentity(ctr::StringView identity) noexcept
    {
        if (identity.Empty())
            return true;
        if (identity.Length() > 1024 || !ValidUtf8(identity))
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

    void Escape(ctr::String& out, ctr::StringView s) noexcept
    {
        for (char c : s)
        {
            if (c == '"' || c == '\\')
            {
                out.Append('\\');
                out.Append(c);
            }
            else if (c == '\n')
                out.Append("\\n", 2);
            else if (c == '\r')
                out.Append("\\r", 2);
            else if (c == '\t')
                out.Append("\\t", 2);
            else
                out.Append(c);
        }
    }
    void Line(ctr::String& out, const char* key, ctr::StringView value) noexcept
    {
        out.Append(key, static_cast<u32>(ctr::StringView(key).Length())).Append(" = \"", 4);
        Escape(out, value);
        out.Append("\"\n", 2);
    }
} // namespace

namespace vanguard::projects
{
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
        if (p.name.Empty() || !ValidUtf8(p.name) || Utf8ScalarCount(p.name) > 128)
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
        if (!ValidUtf8(text))
        {
            Fail(d, Result::InvalidUtf8, 0, 0, nullptr, "document is not valid UTF-8");
            return Result::InvalidUtf8;
        }
        ProjectDescriptor candidate;
        u32 mask = 0, lineNo = 0, offset = 0;
        bool header = false;
        while (offset < text.Length())
        {
            u32 end = offset;
            while (end < text.Length() && text[end] != '\n')
                ++end;
            ctr::StringView line = text.Slice(offset, end);
            if (!line.Empty() && line.Back() == '\r')
                line.TrimBack(1);
            offset = end + 1;
            ++lineNo;
            while (!line.Empty() && (line.Front() == ' ' || line.Front() == '\t'))
                line.TrimFront(1);
            while (!line.Empty() && (line.Back() == ' ' || line.Back() == '\t'))
                line.TrimBack(1);
            if (line.Empty() || line.Front() == '#')
                continue;
            if (!header)
            {
                const u32 comment = line.Find('#');
                if (comment != ctr::StringView::npos)
                {
                    line = line.Slice(0, comment);
                    while (!line.Empty() && (line.Back() == ' ' || line.Back() == '\t'))
                        line.TrimBack(1);
                }
                if (line != "vproject 1.0")
                {
                    Fail(d, Result::UnsupportedVersion, lineNo, 1, nullptr, "expected vproject 1.0 header");
                    return Result::UnsupportedVersion;
                }
                header = true;
                continue;
            }
            const u32 equal = line.Find('=');
            if (equal == ctr::StringView::npos)
            {
                Fail(d, Result::InvalidSyntax, lineNo, 1, nullptr, "expected field assignment");
                return Result::InvalidSyntax;
            }
            ctr::StringView key = line.Slice(0, equal);
            while (!key.Empty() && (key.Back() == ' ' || key.Back() == '\t'))
                key.TrimBack(1);
            ctr::StringView value = line.SubView(equal + 1);
            while (!value.Empty() && (value.Front() == ' ' || value.Front() == '\t'))
                value.TrimFront(1);
            if (value.Empty() || value.Front() != '"')
            {
                Fail(d, Result::InvalidSyntax, lineNo, equal + 2, nullptr, "field value must be quoted");
                return Result::InvalidSyntax;
            }
            u32 closing = ctr::StringView::npos;
            bool escaped = false;
            for (u32 index = 1; index < value.Length(); ++index)
            {
                const char c = value[index];
                if (c == '"' && !escaped)
                {
                    closing = index;
                    break;
                }
                if (c == '\\' && !escaped)
                    escaped = true;
                else
                    escaped = false;
            }
            if (closing == ctr::StringView::npos)
            {
                Fail(d, Result::InvalidSyntax, lineNo, equal + 2, nullptr, "unterminated quoted value");
                return Result::InvalidSyntax;
            }
            ctr::StringView trailing = value.SubView(closing + 1);
            while (!trailing.Empty() && (trailing.Front() == ' ' || trailing.Front() == '\t'))
                trailing.TrimFront(1);
            if (!trailing.Empty() && trailing.Front() != '#')
            {
                Fail(d, Result::InvalidSyntax, lineNo, equal + closing + 2, nullptr, "unexpected text after field value");
                return Result::InvalidSyntax;
            }
            value = value.Slice(1, closing);
            ctr::String decoded;
            if (!DecodeString(value, decoded))
            {
                Fail(d, Result::InvalidSyntax, lineNo, equal + 2, nullptr, "invalid string escape");
                return Result::InvalidSyntax;
            }
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
                if (!ParseId(decoded, candidate.id))
                {
                    Fail(d, Result::InvalidValue, lineNo, equal + 2, fieldName, "invalid UUID");
                    return Result::InvalidValue;
                }
            }
            else if (field == EngineMin)
            {
                if (!ParseVersion(decoded, candidate.engine.minimumMajor, candidate.engine.minimumMinor, candidate.engine.minimumPatch, false, wild))
                {
                    Fail(d, Result::InvalidValue, lineNo, equal + 2, fieldName, "invalid semantic version");
                    return Result::InvalidValue;
                }
            }
            else if (field == EngineMax)
            {
                if (!ParseVersion(decoded, candidate.engine.maximumMajor, candidate.engine.maximumMinor, candidate.engine.maximumPatch, true,
                                  candidate.engine.maximumPatchWildcard))
                {
                    Fail(d, Result::InvalidValue, lineNo, equal + 2, fieldName, "invalid semantic version range");
                    return Result::InvalidValue;
                }
            }
            else
                Assign(candidate, field, static_cast<ctr::String&&>(decoded));
        }
        if (!header)
        {
            Fail(d, Result::MissingField, 1, 1, nullptr, "missing project header");
            return Result::MissingField;
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
        ctr::String text("vproject 1.0\n\n");
        char id[37];
        constexpr char hex[] = "0123456789abcdef";
        u32 at = 0;
        for (u32 part = 0; part < 4; ++part)
            for (i32 shift = 28; shift >= 0; shift -= 4)
            {
                id[at++] = hex[(p.id.parts[part] >> shift) & 15];
                if (at == 8 || at == 13 || at == 18 || at == 23)
                    id[at++] = '-';
            }
        id[36] = '\0';
        Line(text, "project.id", id);
        Line(text, "project.name", p.name);
        Line(text, "project.technicalName", p.technicalName);
        text.Append('\n');
        ctr::String min = ctr::String::Printf("%u.%u.%u", p.engine.minimumMajor, p.engine.minimumMinor, p.engine.minimumPatch);
        ctr::String max = p.engine.maximumPatchWildcard ? ctr::String::Printf("%u.%u.x", p.engine.maximumMajor, p.engine.maximumMinor)
                                                        : ctr::String::Printf("%u.%u.%u", p.engine.maximumMajor, p.engine.maximumMinor, p.engine.maximumPatch);
        Line(text, "engine.minimum", min);
        Line(text, "engine.maximum", max);
        text.Append('\n');
        Line(text, "paths.assets", p.assets);
        Line(text, "paths.derivedData", p.derivedData);
        Line(text, "paths.intermediate", p.intermediate);
        Line(text, "paths.saved", p.saved);
        Line(text, "paths.builds", p.builds);
        Line(text, "paths.config", p.config);
        Line(text, "paths.plugins", p.pluginsRoot);
        text.Append('\n');
        for (const ctr::String& target : p.targets)
            Line(text, "target", target);
        for (const ctr::String& plugin : p.plugins)
            Line(text, "plugin", plugin);
        text.Append('\n');
        Line(text, "policy.cooking", p.cookingPolicy);
        Line(text, "policy.packaging", p.packagingPolicy);
        text.Append('\n');
        Line(text, "startup.editorWorld", p.editorWorld);
        Line(text, "startup.runtimeWorld", p.runtimeWorld);
        Line(text, "startup.input", p.input);
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
