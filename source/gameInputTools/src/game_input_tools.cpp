#include <vanguard/game_input_tools/game_input_tools.hpp>

#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    namespace git = game_input_tools;
    namespace gi = game_input;

    struct Token
    {
        const u8* data = nullptr;
        u32 size = 0;
        u32 column = 0;
    };
    struct Line
    {
        Token tokens[24]{};
        u32 count = 0;
        u32 number = 0;
    };

    [[nodiscard]] bool Equal(const Token token, const char* text) noexcept
    {
        u32 index = 0;
        while (index < token.size && text[index] != '\0' && token.data[index] == static_cast<u8>(text[index]))
            ++index;
        return index == token.size && text[index] == '\0';
    }
    [[nodiscard]] bool Identifier(const Token token) noexcept
    {
        if (token.size == 0 || token.size >= gi::MaximumNameBytes)
            return false;
        for (u32 index = 0; index < token.size; ++index)
        {
            const u8 value = token.data[index];
            if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_' || value == '-' ||
                  value == '.' || value == '/'))
                return false;
        }
        return true;
    }
    [[nodiscard]] bool Copy(char (&destination)[gi::MaximumNameBytes], const Token token) noexcept
    {
        if (!Identifier(token))
            return false;
        for (u32 index = 0; index < token.size; ++index)
            destination[index] = static_cast<char>(token.data[index]);
        destination[token.size] = '\0';
        return true;
    }
    [[nodiscard]] u64 Id(const Token token) noexcept
    {
        u64 hash = 14695981039346656037ull;
        for (u32 index = 0; index < token.size; ++index)
        {
            hash ^= token.data[index];
            hash *= 1099511628211ull;
        }
        return hash != 0 ? hash : 1;
    }
    [[nodiscard]] bool ParseBool(const Token token, bool& value) noexcept
    {
        if (Equal(token, "0") || Equal(token, "false"))
        {
            value = false;
            return true;
        }
        if (Equal(token, "1") || Equal(token, "true"))
        {
            value = true;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool ParseU32(const Token token, u32& value) noexcept
    {
        if (token.size == 0)
            return false;
        u64 result = 0;
        for (u32 index = 0; index < token.size; ++index)
        {
            if (token.data[index] < '0' || token.data[index] > '9')
                return false;
            result = result * 10u + (token.data[index] - '0');
            if (result > 0xffffffffull)
                return false;
        }
        value = static_cast<u32>(result);
        return true;
    }
    [[nodiscard]] bool ParseI16(const Token token, i16& value) noexcept
    {
        if (token.size == 0)
            return false;
        u32 offset = token.data[0] == '-' || token.data[0] == '+' ? 1u : 0u;
        if (offset == token.size)
            return false;
        i32 result = 0;
        for (u32 index = offset; index < token.size; ++index)
        {
            if (token.data[index] < '0' || token.data[index] > '9')
                return false;
            result = result * 10 + token.data[index] - '0';
            if (result > 32768)
                return false;
        }
        if (offset != 0 && token.data[0] == '-')
            result = -result;
        if (result < -32768 || result > 32767)
            return false;
        value = static_cast<i16>(result);
        return true;
    }
    [[nodiscard]] bool ParseF32(const Token token, f32& value) noexcept
    {
        if (token.size == 0)
            return false;
        u32 index = 0;
        bool negative = false;
        if (token.data[index] == '-' || token.data[index] == '+')
        {
            negative = token.data[index] == '-';
            if (++index == token.size)
                return false;
        }
        f64 result = 0.0;
        bool digit = false;
        while (index < token.size && token.data[index] >= '0' && token.data[index] <= '9')
        {
            digit = true;
            result = result * 10.0 + static_cast<f64>(token.data[index++] - '0');
        }
        if (index < token.size && token.data[index] == '.')
        {
            ++index;
            f64 place = 0.1;
            while (index < token.size && token.data[index] >= '0' && token.data[index] <= '9')
            {
                digit = true;
                result += static_cast<f64>(token.data[index++] - '0') * place;
                place *= 0.1;
            }
        }
        if (!digit)
            return false;
        i32 exponent = 0;
        if (index < token.size && (token.data[index] == 'e' || token.data[index] == 'E'))
        {
            ++index;
            bool exponentNegative = false;
            if (index < token.size && (token.data[index] == '-' || token.data[index] == '+'))
            {
                exponentNegative = token.data[index] == '-';
                ++index;
            }
            if (index == token.size)
                return false;
            bool exponentDigit = false;
            while (index < token.size && token.data[index] >= '0' && token.data[index] <= '9')
            {
                exponentDigit = true;
                exponent = exponent * 10 + token.data[index++] - '0';
                if (exponent > 38)
                    return false;
            }
            if (!exponentDigit)
                return false;
            if (exponentNegative)
                exponent = -exponent;
        }
        if (index != token.size)
            return false;
        while (exponent > 0)
        {
            result *= 10.0;
            --exponent;
        }
        while (exponent < 0)
        {
            result *= 0.1;
            ++exponent;
        }
        if (negative)
            result = -result;
        if (result > 3.402823466e+38 || result < -3.402823466e+38)
            return false;
        value = static_cast<f32>(result);
        return value == value;
    }
    [[nodiscard]] bool ValidUtf8(const containers::ArraySpan<const u8> source) noexcept
    {
        for (u32 index = 0; index < source.Size();)
        {
            const u8 first = source[index++];
            if (first < 0x80)
                continue;
            u32 continuation = 0;
            u32 codepoint = 0;
            if ((first & 0xe0u) == 0xc0u)
            {
                continuation = 1;
                codepoint = first & 0x1fu;
                if (codepoint < 2)
                    return false;
            }
            else if ((first & 0xf0u) == 0xe0u)
            {
                continuation = 2;
                codepoint = first & 0x0fu;
            }
            else if ((first & 0xf8u) == 0xf0u)
            {
                continuation = 3;
                codepoint = first & 0x07u;
            }
            else
                return false;
            if (index + continuation > source.Size())
                return false;
            for (u32 count = 0; count < continuation; ++count)
            {
                const u8 next = source[index++];
                if ((next & 0xc0u) != 0x80u)
                    return false;
                codepoint = (codepoint << 6u) | (next & 0x3fu);
            }
            if ((continuation == 2 && codepoint < 0x800u) || (continuation == 3 && codepoint < 0x10000u) || codepoint > 0x10ffffu ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                return false;
        }
        return true;
    }

    class Cursor final
    {
    public:
        explicit Cursor(const containers::ArraySpan<const u8> source) noexcept : m_source(source) {}
        [[nodiscard]] bool Next(Line& line) noexcept
        {
            while (m_offset < m_source.Size())
            {
                ++m_line;
                line = {};
                line.number = m_line;
                const u32 begin = m_offset;
                while (m_offset < m_source.Size() && m_source[m_offset] != '\n')
                    ++m_offset;
                const u32 end = m_offset;
                if (m_offset < m_source.Size())
                    ++m_offset;
                u32 cursor = begin;
                while (cursor < end)
                {
                    while (cursor < end && (m_source[cursor] == ' ' || m_source[cursor] == '\t' || m_source[cursor] == '\r'))
                        ++cursor;
                    if (cursor == end || m_source[cursor] == '#')
                        break;
                    if (line.count == 24)
                    {
                        m_overflow = true;
                        return true;
                    }
                    Token& token = line.tokens[line.count++];
                    token.data = m_source.Data() + cursor;
                    token.column = cursor - begin + 1u;
                    while (cursor < end && m_source[cursor] != ' ' && m_source[cursor] != '\t' && m_source[cursor] != '\r' && m_source[cursor] != '#')
                    {
                        ++cursor;
                        ++token.size;
                    }
                    if (cursor < end && m_source[cursor] == '#')
                        break;
                }
                if (line.count != 0 || m_overflow)
                    return true;
            }
            return false;
        }
        [[nodiscard]] bool Overflowed() const noexcept
        {
            return m_overflow;
        }

    private:
        containers::ArraySpan<const u8> m_source;
        u32 m_offset = 0;
        u32 m_line = 0;
        bool m_overflow = false;
    };

    struct ContextSource
    {
        gi::ContextDescriptor value;
        char name[gi::MaximumNameBytes]{};
        bool initial = false;
    };
    struct ActionSource
    {
        gi::ActionDescriptor value;
        char name[gi::MaximumNameBytes]{};
    };
    struct BindingSource
    {
        gi::BindingDescriptor value;
        gi::Control modifiers[gi::MaximumBindingModifiers]{};
        u8 modifierCount = 0;
    };
    struct Document
    {
        Document() noexcept
            : contexts(memory::pools::Assets::GetInstance()), actions(memory::pools::Assets::GetInstance()), bindings(memory::pools::Assets::GetInstance()),
              initial(memory::pools::Assets::GetInstance())
        {
            contexts.Reserve(gi::MaximumContexts);
            actions.Reserve(gi::MaximumActions);
            bindings.Reserve(gi::MaximumBindings);
        }
        containers::DynamicArray<ContextSource> contexts;
        containers::DynamicArray<ActionSource> actions;
        containers::DynamicArray<BindingSource> bindings;
        containers::DynamicArray<gi::ContextId> initial;
    };

    void Fail(git::SourceDiagnostic& diagnostic, const git::SourceResult result, const Line& line, const u32 token, const char* message) noexcept
    {
        diagnostic.result = result;
        diagnostic.line = line.number;
        diagnostic.column = token < line.count ? line.tokens[token].column : 1;
        diagnostic.message = message;
    }
    [[nodiscard]] bool ParseLayer(const Token token, gi::ContextLayer& value) noexcept
    {
        if (Equal(token, "player"))
            value = gi::ContextLayer::Player;
        else if (Equal(token, "ui"))
            value = gi::ContextLayer::UserInterface;
        else if (Equal(token, "debug"))
            value = gi::ContextLayer::Debug;
        else
            return false;
        return true;
    }
    [[nodiscard]] bool ParseActionType(const Token token, gi::ActionValueType& value) noexcept
    {
        if (Equal(token, "button"))
            value = gi::ActionValueType::Button;
        else if (Equal(token, "axis1d"))
            value = gi::ActionValueType::Axis1D;
        else if (Equal(token, "axis2d"))
            value = gi::ActionValueType::Axis2D;
        else
            return false;
        return true;
    }
    [[nodiscard]] bool ParseComponent(const Token token, gi::AxisComponent& value) noexcept
    {
        if (Equal(token, "scalar"))
            value = gi::AxisComponent::Scalar;
        else if (Equal(token, "x"))
            value = gi::AxisComponent::X;
        else if (Equal(token, "y"))
            value = gi::AxisComponent::Y;
        else
            return false;
        return true;
    }
    [[nodiscard]] bool ParseControlType(const Token token, gi::ControlType& value) noexcept
    {
        if (Equal(token, "key"))
            value = gi::ControlType::Key;
        else if (Equal(token, "mouse_button"))
            value = gi::ControlType::MouseButton;
        else if (Equal(token, "mouse_dx"))
            value = gi::ControlType::MouseDeltaX;
        else if (Equal(token, "mouse_dy"))
            value = gi::ControlType::MouseDeltaY;
        else if (Equal(token, "wheel_x"))
            value = gi::ControlType::MouseWheelX;
        else if (Equal(token, "wheel_y"))
            value = gi::ControlType::MouseWheelY;
        else if (Equal(token, "gamepad_button"))
            value = gi::ControlType::GamepadButton;
        else if (Equal(token, "gamepad_axis"))
            value = gi::ControlType::GamepadAxis;
        else
            return false;
        return true;
    }

    [[nodiscard]] git::SourceResult Parse(const containers::ArraySpan<const u8> source, Document& document, git::SourceDiagnostic& diagnostic) noexcept
    {
        if (source.Empty() || source.Data() == nullptr)
            return git::SourceResult::InvalidArgument;
        if (!ValidUtf8(source))
            return git::SourceResult::InvalidEncoding;
        bool header = false;
        Cursor first(source);
        Line line;
        while (first.Next(line))
        {
            if (first.Overflowed())
            {
                Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 23, "too many fields");
                return diagnostic.result;
            }
            if (Equal(line.tokens[0], "vinput"))
            {
                u32 version = 0;
                if (header || line.count != 2 || !ParseU32(line.tokens[1], version))
                {
                    Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 0, "invalid or duplicate vinput header");
                    return diagnostic.result;
                }
                if (version != 1)
                {
                    Fail(diagnostic, git::SourceResult::UnsupportedVersion, line, 1, "unsupported source mapping version");
                    return diagnostic.result;
                }
                header = true;
                continue;
            }
            if (!header)
            {
                Fail(diagnostic, git::SourceResult::UnsupportedVersion, line, 0, "vinput header must precede declarations");
                return diagnostic.result;
            }
            if (Equal(line.tokens[0], "context"))
            {
                if (line.count != 5)
                {
                    Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 0, "context requires four fields");
                    return diagnostic.result;
                }
                if (document.contexts.Size() >= gi::MaximumContexts)
                    return git::SourceResult::LimitExceeded;
                ContextSource record;
                bool initial = false;
                if (!Copy(record.name, line.tokens[1]))
                {
                    Fail(diagnostic, git::SourceResult::InvalidIdentifier, line, 1, "invalid context identifier");
                    return diagnostic.result;
                }
                record.value.id = Id(line.tokens[1]);
                record.value.name = record.name;
                if (!ParseLayer(line.tokens[2], record.value.layer))
                {
                    Fail(diagnostic, git::SourceResult::InvalidEnum, line, 2, "invalid context layer");
                    return diagnostic.result;
                }
                if (!ParseI16(line.tokens[3], record.value.priority) || !ParseBool(line.tokens[4], initial))
                {
                    Fail(diagnostic, git::SourceResult::InvalidNumber, line, 3, "invalid context priority or active flag");
                    return diagnostic.result;
                }
                record.initial = initial;
                document.contexts.PushBack(record);
                continue;
            }
            if (Equal(line.tokens[0], "action"))
            {
                if (line.count != 16)
                {
                    Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 0, "action requires fifteen fields");
                    return diagnostic.result;
                }
                if (document.actions.Size() >= gi::MaximumActions)
                    return git::SourceResult::LimitExceeded;
                ActionSource record;
                u32 multiTap = 0;
                if (!Copy(record.name, line.tokens[1]))
                {
                    Fail(diagnostic, git::SourceResult::InvalidIdentifier, line, 1, "invalid action identifier");
                    return diagnostic.result;
                }
                record.value.id = Id(line.tokens[1]);
                record.value.name = record.name;
                if (!ParseActionType(line.tokens[2], record.value.valueType))
                {
                    Fail(diagnostic, git::SourceResult::InvalidEnum, line, 2, "invalid action type");
                    return diagnostic.result;
                }
                if (!ParseI16(line.tokens[3], record.value.priority) || !ParseF32(line.tokens[4], record.value.holdSeconds) ||
                    !ParseF32(line.tokens[5], record.value.tapMaximumSeconds) || !ParseU32(line.tokens[6], multiTap) || multiTap > 255 ||
                    !ParseF32(line.tokens[7], record.value.multiTapMaximumDownSeconds) || !ParseF32(line.tokens[8], record.value.multiTapMaximumGapSeconds) ||
                    !ParseF32(line.tokens[9], record.value.repeatDelaySeconds) || !ParseF32(line.tokens[10], record.value.repeatIntervalSeconds) ||
                    !ParseF32(line.tokens[11], record.value.radialDeadzoneInner) || !ParseF32(line.tokens[12], record.value.radialDeadzoneOuter) ||
                    !ParseF32(line.tokens[13], record.value.sensitivity) || !ParseBool(line.tokens[14], record.value.toggle) ||
                    !ParseBool(line.tokens[15], record.value.consumeControl))
                {
                    Fail(diagnostic, git::SourceResult::InvalidNumber, line, 3, "invalid action numeric field");
                    return diagnostic.result;
                }
                record.value.multiTapCount = static_cast<u8>(multiTap);
                document.actions.PushBack(record);
                continue;
            }
            if (!Equal(line.tokens[0], "curve") && !Equal(line.tokens[0], "binding"))
            {
                Fail(diagnostic, git::SourceResult::UnknownDirective, line, 0, "unknown input mapping directive");
                return diagnostic.result;
            }
        }
        if (!header)
            return git::SourceResult::UnsupportedVersion;

        for (ContextSource& context : document.contexts)
        {
            context.value.name = context.name;
            if (context.initial)
                document.initial.PushBack(context.value.id);
        }
        Cursor second(source);
        while (second.Next(line))
        {
            if (Equal(line.tokens[0], "curve"))
            {
                if (line.count != 4)
                {
                    Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 0, "curve requires three fields");
                    return diagnostic.result;
                }
                ActionSource* action = nullptr;
                const u64 actionId = Id(line.tokens[1]);
                for (ActionSource& candidate : document.actions)
                    if (candidate.value.id == actionId)
                    {
                        action = &candidate;
                        break;
                    }
                if (action == nullptr)
                {
                    Fail(diagnostic, git::SourceResult::UnknownAction, line, 1, "curve references unknown action");
                    return diagnostic.result;
                }
                f32 input = 0.0f, output = 0.0f;
                if (!ParseF32(line.tokens[2], input) || !ParseF32(line.tokens[3], output))
                {
                    Fail(diagnostic, git::SourceResult::InvalidNumber, line, 2, "invalid curve point");
                    return diagnostic.result;
                }
                gi::ResponseCurve& curve = action->value.responseCurve;
                if (curve.count >= gi::MaximumResponseCurvePoints)
                    return git::SourceResult::LimitExceeded;
                u32 position = curve.count;
                while (position != 0 && curve.points[position - 1u].input > input)
                {
                    curve.points[position] = curve.points[position - 1u];
                    --position;
                }
                if ((position != 0 && curve.points[position - 1u].input == input) || (position != curve.count && curve.points[position].input == input))
                {
                    Fail(diagnostic, git::SourceResult::DuplicateCurvePoint, line, 2, "duplicate curve input");
                    return diagnostic.result;
                }
                curve.points[position] = {input, output};
                ++curve.count;
                continue;
            }
            if (!Equal(line.tokens[0], "binding"))
                continue;
            if (line.count < 11 || ((line.count - 11u) & 1u) != 0 || line.count > 19)
            {
                Fail(diagnostic, git::SourceResult::InvalidFieldCount, line, 0, "invalid binding field or modifier count");
                return diagnostic.result;
            }
            if (document.bindings.Size() >= gi::MaximumBindings)
                return git::SourceResult::LimitExceeded;
            BindingSource record;
            u32 code = 0;
            bool overridable = false;
            if (!Identifier(line.tokens[1]) || !Identifier(line.tokens[2]) || !Identifier(line.tokens[3]))
            {
                Fail(diagnostic, git::SourceResult::InvalidIdentifier, line, 1, "invalid binding, context, or action identifier");
                return diagnostic.result;
            }
            record.value.id = Id(line.tokens[1]);
            record.value.context = Id(line.tokens[2]);
            record.value.action = Id(line.tokens[3]);
            if (!ParseControlType(line.tokens[4], record.value.control.type) || !ParseU32(line.tokens[5], code) || code > 65535 ||
                !ParseComponent(line.tokens[6], record.value.component))
            {
                Fail(diagnostic, git::SourceResult::InvalidEnum, line, 4, "invalid binding control or component");
                return diagnostic.result;
            }
            record.value.control.code = static_cast<u16>(code);
            if (!ParseF32(line.tokens[7], record.value.scale) || !ParseF32(line.tokens[8], record.value.pressThreshold) ||
                !ParseF32(line.tokens[9], record.value.releaseThreshold) || !ParseBool(line.tokens[10], overridable))
            {
                Fail(diagnostic, git::SourceResult::InvalidNumber, line, 7, "invalid binding numeric field");
                return diagnostic.result;
            }
            record.value.overridable = overridable;
            record.modifierCount = static_cast<u8>((line.count - 11u) / 2u);
            for (u32 modifier = 0; modifier < record.modifierCount; ++modifier)
            {
                u32 modifierCode = 0;
                if (!ParseControlType(line.tokens[11u + modifier * 2u], record.modifiers[modifier].type) ||
                    !ParseU32(line.tokens[12u + modifier * 2u], modifierCode) || modifierCode > 65535)
                {
                    Fail(diagnostic, git::SourceResult::InvalidEnum, line, 11u + modifier * 2u, "invalid modifier control");
                    return diagnostic.result;
                }
                record.modifiers[modifier].code = static_cast<u16>(modifierCode);
            }
            document.bindings.PushBack(record);
        }
        for (ActionSource& action : document.actions)
            action.value.name = action.name;
        for (BindingSource& binding : document.bindings)
            binding.value.modifiers = {binding.modifiers, binding.modifierCount};
        return git::SourceResult::Success;
    }

    bool Discover(const assets::BuildRequest&, assets::DependencyCollector&, void*) noexcept
    {
        return true;
    }
    bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void*) noexcept
    {
        if (context.IsCancellationRequested() || !context.request.settings.Empty())
            return false;
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter output(bytes);
        if (git::CompileSourceMapping(context.request.source.content, output) != git::SourceResult::Success)
            return false;
        return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes.Data(),
                             bytes.Size()) == assets::Result::Success;
    }
} // namespace

namespace vanguard::game_input_tools
{
    const char* ToString(const SourceResult result) noexcept
    {
        switch (result)
        {
        case SourceResult::Success:
            return "Success";
        case SourceResult::InvalidArgument:
            return "InvalidArgument";
        case SourceResult::InvalidEncoding:
            return "InvalidEncoding";
        case SourceResult::UnsupportedVersion:
            return "UnsupportedVersion";
        case SourceResult::UnknownDirective:
            return "UnknownDirective";
        case SourceResult::InvalidFieldCount:
            return "InvalidFieldCount";
        case SourceResult::InvalidIdentifier:
            return "InvalidIdentifier";
        case SourceResult::InvalidEnum:
            return "InvalidEnum";
        case SourceResult::InvalidNumber:
            return "InvalidNumber";
        case SourceResult::DuplicateCurvePoint:
            return "DuplicateCurvePoint";
        case SourceResult::UnknownAction:
            return "UnknownAction";
        case SourceResult::LimitExceeded:
            return "LimitExceeded";
        case SourceResult::MappingValidationFailure:
            return "MappingValidationFailure";
        case SourceResult::WriteFailure:
            return "WriteFailure";
        }
        return "Unknown";
    }

    SourceResult CompileSourceMapping(const containers::ArraySpan<const u8> source, filesystem::IFile& output, SourceDiagnostic* const diagnostic) noexcept
    {
        SourceDiagnostic local;
        Document document;
        SourceResult result = Parse(source, document, local);
        if (result == SourceResult::Success)
        {
            game_input::MappingBuildDescription description;
            // Source records contain owned names, so descriptors are gathered contiguously here without borrowing parser tokens.
            containers::DynamicArray<game_input::ContextDescriptor> contexts(memory::pools::Assets::GetInstance());
            containers::DynamicArray<game_input::ActionDescriptor> actions(memory::pools::Assets::GetInstance());
            containers::DynamicArray<game_input::BindingDescriptor> bindings(memory::pools::Assets::GetInstance());
            contexts.Reserve(document.contexts.Size());
            actions.Reserve(document.actions.Size());
            bindings.Reserve(document.bindings.Size());
            for (const ContextSource& record : document.contexts)
                contexts.PushBack(record.value);
            for (const ActionSource& record : document.actions)
                actions.PushBack(record.value);
            for (const BindingSource& record : document.bindings)
                bindings.PushBack(record.value);
            if (contexts.Size() != document.contexts.Size() || actions.Size() != document.actions.Size() || bindings.Size() != document.bindings.Size())
                result = SourceResult::LimitExceeded;
            else
            {
                description.contexts = contexts;
                description.actions = actions;
                description.bindings = bindings;
                description.initialContexts = document.initial;
                const game_input::MappingResult cooked = game_input::CookMapping(description, output);
                if (cooked != game_input::MappingResult::Success)
                {
                    result = cooked == game_input::MappingResult::IoFailure ? SourceResult::WriteFailure : SourceResult::MappingValidationFailure;
                    local.result = result;
                    local.message = game_input::ToString(cooked);
                }
            }
        }
        if (local.result == SourceResult::Success && result != SourceResult::Success)
            local.result = result;
        if (diagnostic != nullptr)
            *diagnostic = local;
        return result;
    }

    assets::CompilerDescriptor MakeMappingCompilerDescriptor() noexcept
    {
        return {MappingCompilerId,
                "vanguard.input_mapping.compiler",
                MappingCompilerVersion,
                SourceMappingResourceType,
                game_input::MappingResourceType,
                Discover,
                Compile,
                nullptr};
    }
    assets::Result RegisterMappingCompiler(assets::BuildSystem& buildSystem) noexcept
    {
        return buildSystem.RegisterCompiler(MakeMappingCompilerDescriptor());
    }
} // namespace vanguard::game_input_tools
