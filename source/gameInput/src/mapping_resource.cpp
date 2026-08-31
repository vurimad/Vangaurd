#include <vanguard/game_input/mapping_resource.hpp>

#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    namespace gi = game_input;
    namespace ser = vanguard::serialization;

    constexpr ser::Version FileVersion{1, 0};
    constexpr u32 MappingSection = ser::MakeFourCC('M', 'A', 'P', 'S');
    constexpr u32 BodyVersion = 1;
    using ByteArray = containers::DynamicArray<u8>;

    [[nodiscard]] gi::MappingResult Convert(const ser::Result result) noexcept
    {
        switch (result)
        {
        case ser::Result::Success:
            return gi::MappingResult::Success;
        case ser::Result::InvalidMagic:
            return gi::MappingResult::InvalidMagic;
        case ser::Result::UnsupportedVersion:
            return gi::MappingResult::UnsupportedVersion;
        case ser::Result::IntegrityFailure:
            return gi::MappingResult::IntegrityFailure;
        case ser::Result::LimitExceeded:
        case ser::Result::Overflow:
            return gi::MappingResult::LimitExceeded;
        case ser::Result::IoFailure:
        case ser::Result::WrongStreamMode:
        case ser::Result::EndOfStream:
            return gi::MappingResult::IoFailure;
        default:
            return gi::MappingResult::InvalidLayout;
        }
    }

    [[nodiscard]] gi::MappingResult Convert(const gi::Result result) noexcept
    {
        switch (result)
        {
        case gi::Result::Success:
            return gi::MappingResult::Success;
        case gi::Result::DuplicateId:
            return gi::MappingResult::DuplicateId;
        case gi::Result::LimitExceeded:
            return gi::MappingResult::LimitExceeded;
        case gi::Result::InvalidArgument:
        case gi::Result::InvalidControl:
        case gi::Result::InvalidMapping:
            return gi::MappingResult::InvalidArgument;
        default:
            return gi::MappingResult::MappingFailure;
        }
    }

    [[nodiscard]] bool CopyName(char (&destination)[gi::MaximumNameBytes], const char* source) noexcept
    {
        if (source == nullptr || source[0] == '\0')
            return false;
        u32 index = 0;
        while (source[index] != '\0' && index + 1u < gi::MaximumNameBytes)
        {
            destination[index] = source[index];
            ++index;
        }
        if (source[index] != '\0')
            return false;
        destination[index] = '\0';
        for (++index; index < gi::MaximumNameBytes; ++index)
            destination[index] = '\0';
        return true;
    }

    [[nodiscard]] bool ValidStoredName(const char (&name)[gi::MaximumNameBytes]) noexcept
    {
        if (name[0] == '\0')
            return false;
        for (u32 index = 1; index < gi::MaximumNameBytes; ++index)
            if (name[index] == '\0')
                return true;
        return false;
    }

    [[nodiscard]] bool WriteControl(ser::BinaryWriter& writer, const gi::Control& control) noexcept
    {
        return writer.WriteU8(static_cast<u8>(control.type)) && writer.WriteU16(control.code) && writer.WriteU64(control.device);
    }
    [[nodiscard]] bool ReadControl(ser::BinaryReader& reader, gi::Control& control) noexcept
    {
        u8 type = 0;
        if (!reader.ReadU8(type) || !reader.ReadU16(control.code) || !reader.ReadU64(control.device) || type > static_cast<u8>(gi::ControlType::GamepadAxis))
            return false;
        control.type = static_cast<gi::ControlType>(type);
        return true;
    }

    [[nodiscard]] bool WriteContext(ser::BinaryWriter& writer, const gi::ContextDescriptor& descriptor) noexcept
    {
        char name[gi::MaximumNameBytes]{};
        return CopyName(name, descriptor.name) && writer.WriteU64(descriptor.id) && writer.WriteBytes(name, sizeof(name)) &&
               writer.WriteU8(static_cast<u8>(descriptor.layer)) && writer.WriteI16(descriptor.priority);
    }
    [[nodiscard]] bool ReadContext(ser::BinaryReader& reader, gi::MappingContextRecord& record) noexcept
    {
        u8 layer = 0;
        if (!reader.ReadU64(record.descriptor.id) || !reader.ReadBytes(record.name, sizeof(record.name)) || !reader.ReadU8(layer) ||
            !reader.ReadI16(record.descriptor.priority) || !ValidStoredName(record.name) || layer >= static_cast<u8>(gi::ContextLayer::Count))
            return false;
        record.descriptor.name = record.name;
        record.descriptor.layer = static_cast<gi::ContextLayer>(layer);
        return true;
    }

    [[nodiscard]] bool WriteAction(ser::BinaryWriter& writer, const gi::ActionDescriptor& descriptor) noexcept
    {
        char name[gi::MaximumNameBytes]{};
        if (!CopyName(name, descriptor.name) || !writer.WriteU64(descriptor.id) || !writer.WriteBytes(name, sizeof(name)) ||
            !writer.WriteU8(static_cast<u8>(descriptor.valueType)) || !writer.WriteI16(descriptor.priority) || !writer.WriteF32(descriptor.holdSeconds) ||
            !writer.WriteF32(descriptor.tapMaximumSeconds) || !writer.WriteF32(descriptor.multiTapMaximumDownSeconds) ||
            !writer.WriteF32(descriptor.multiTapMaximumGapSeconds) || !writer.WriteF32(descriptor.repeatDelaySeconds) ||
            !writer.WriteF32(descriptor.repeatIntervalSeconds) || !writer.WriteF32(descriptor.radialDeadzoneInner) ||
            !writer.WriteF32(descriptor.radialDeadzoneOuter) || !writer.WriteF32(descriptor.sensitivity) || !writer.WriteU8(descriptor.responseCurve.count))
            return false;
        for (const gi::ResponseCurvePoint& point : descriptor.responseCurve.points)
            if (!writer.WriteF32(point.input) || !writer.WriteF32(point.output))
                return false;
        return writer.WriteU8(descriptor.multiTapCount) && writer.WriteBool(descriptor.toggle) && writer.WriteBool(descriptor.consumeControl);
    }
    [[nodiscard]] bool ReadAction(ser::BinaryReader& reader, gi::MappingActionRecord& record) noexcept
    {
        u8 type = 0;
        gi::ActionDescriptor& descriptor = record.descriptor;
        if (!reader.ReadU64(descriptor.id) || !reader.ReadBytes(record.name, sizeof(record.name)) || !reader.ReadU8(type) ||
            !reader.ReadI16(descriptor.priority) || !reader.ReadF32(descriptor.holdSeconds) || !reader.ReadF32(descriptor.tapMaximumSeconds) ||
            !reader.ReadF32(descriptor.multiTapMaximumDownSeconds) || !reader.ReadF32(descriptor.multiTapMaximumGapSeconds) ||
            !reader.ReadF32(descriptor.repeatDelaySeconds) || !reader.ReadF32(descriptor.repeatIntervalSeconds) ||
            !reader.ReadF32(descriptor.radialDeadzoneInner) || !reader.ReadF32(descriptor.radialDeadzoneOuter) || !reader.ReadF32(descriptor.sensitivity) ||
            !reader.ReadU8(descriptor.responseCurve.count) || !ValidStoredName(record.name) || type > static_cast<u8>(gi::ActionValueType::Axis2D))
            return false;
        for (gi::ResponseCurvePoint& point : descriptor.responseCurve.points)
            if (!reader.ReadF32(point.input) || !reader.ReadF32(point.output))
                return false;
        if (!reader.ReadU8(descriptor.multiTapCount) || !reader.ReadBool(descriptor.toggle) || !reader.ReadBool(descriptor.consumeControl))
            return false;
        descriptor.name = record.name;
        descriptor.valueType = static_cast<gi::ActionValueType>(type);
        return true;
    }

    [[nodiscard]] bool WriteBinding(ser::BinaryWriter& writer, const gi::BindingDescriptor& descriptor) noexcept
    {
        if (!writer.WriteU64(descriptor.id) || !writer.WriteU64(descriptor.context) || !writer.WriteU64(descriptor.action) ||
            !WriteControl(writer, descriptor.control) || !writer.WriteU8(static_cast<u8>(descriptor.component)) || !writer.WriteF32(descriptor.scale) ||
            !writer.WriteF32(descriptor.pressThreshold) || !writer.WriteF32(descriptor.releaseThreshold) ||
            !writer.WriteU8(static_cast<u8>(descriptor.modifiers.Size())) || !writer.WriteBool(descriptor.overridable))
            return false;
        for (u32 index = 0; index < gi::MaximumBindingModifiers; ++index)
        {
            const gi::Control control = index < descriptor.modifiers.Size() ? descriptor.modifiers[index] : gi::Control{};
            if (!WriteControl(writer, control))
                return false;
        }
        return true;
    }
    [[nodiscard]] bool ReadBinding(ser::BinaryReader& reader, gi::MappingBindingRecord& record) noexcept
    {
        u8 component = 0;
        gi::BindingDescriptor& descriptor = record.descriptor;
        if (!reader.ReadU64(descriptor.id) || !reader.ReadU64(descriptor.context) || !reader.ReadU64(descriptor.action) ||
            !ReadControl(reader, descriptor.control) || !reader.ReadU8(component) || !reader.ReadF32(descriptor.scale) ||
            !reader.ReadF32(descriptor.pressThreshold) || !reader.ReadF32(descriptor.releaseThreshold) || !reader.ReadU8(record.modifierCount) ||
            !reader.ReadBool(descriptor.overridable) || component > static_cast<u8>(gi::AxisComponent::Y) || record.modifierCount > gi::MaximumBindingModifiers)
            return false;
        for (gi::Control& control : record.modifiers)
            if (!ReadControl(reader, control))
                return false;
        descriptor.component = static_cast<gi::AxisComponent>(component);
        descriptor.modifiers = {record.modifiers, record.modifierCount};
        return true;
    }

    template <typename Descriptor, typename IdFunction> void SortIndices(u16* indices, const u32 count, const Descriptor* descriptors, IdFunction id) noexcept
    {
        for (u32 index = 0; index < count; ++index)
            indices[index] = static_cast<u16>(index);
        for (u32 index = 1; index < count; ++index)
        {
            const u16 value = indices[index];
            u32 destination = index;
            while (destination != 0 && id(descriptors[value]) < id(descriptors[indices[destination - 1u]]))
            {
                indices[destination] = indices[destination - 1u];
                --destination;
            }
            indices[destination] = value;
        }
    }
} // namespace

namespace vanguard::game_input
{
    const char* ToString(const MappingResult result) noexcept
    {
        switch (result)
        {
        case MappingResult::Success:
            return "Success";
        case MappingResult::InvalidArgument:
            return "InvalidArgument";
        case MappingResult::InvalidState:
            return "InvalidState";
        case MappingResult::InvalidMagic:
            return "InvalidMagic";
        case MappingResult::UnsupportedVersion:
            return "UnsupportedVersion";
        case MappingResult::InvalidLayout:
            return "InvalidLayout";
        case MappingResult::IntegrityFailure:
            return "IntegrityFailure";
        case MappingResult::LimitExceeded:
            return "LimitExceeded";
        case MappingResult::DuplicateId:
            return "DuplicateId";
        case MappingResult::MappingFailure:
            return "MappingFailure";
        case MappingResult::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    MappingFile::MappingFile() noexcept
        : m_contexts(memory::pools::Input::GetInstance()), m_actions(memory::pools::Input::GetInstance()), m_bindings(memory::pools::Input::GetInstance()),
          m_initialContexts(memory::pools::Input::GetInstance())
    {
    }

    MappingResult MappingFile::Open(filesystem::IFile& reader, const MappingReadLimits& limits) noexcept
    {
        Close();
        if (limits.maximumFileSize == 0 || limits.maximumContexts > MaximumContexts || limits.maximumActions > MaximumActions ||
            limits.maximumBindings > MaximumBindings)
            return MappingResult::InvalidArgument;
        ser::BinaryReader input(reader);
        ser::DocumentHeader header;
        ser::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 4;
        MappingResult result = Convert(ser::ReadDocumentHeader(input, MappingMagic, {1, 0, 0}, documentLimits, header));
        if (result != MappingResult::Success)
            return result;
        containers::DynamicArray<ser::SectionDescriptor> sections(memory::pools::Serialization::GetInstance());
        result = Convert(ser::ReadSectionTable(input, header, documentLimits, sections));
        if (result != MappingResult::Success || sections.Size() != 1)
            return MappingResult::InvalidLayout;
        const ser::SectionDescriptor& section = sections[0];
        if (section.id != MappingSection || section.version != FileVersion || section.codec != ser::Codec::None || section.logicalSize != section.storedSize ||
            section.storedSize > limits.maximumFileSize || section.storedSize > static_cast<u64>(~u32{0}))
            return MappingResult::InvalidLayout;
        ByteArray body(memory::pools::Serialization::GetInstance());
        body.Resize(static_cast<u32>(section.storedSize));
        if (body.Size() != section.storedSize || !input.Seek(section.offset) || !input.ReadBytes(body.Data(), body.Size()))
            return MappingResult::IoFailure;
        if (ser::Crc64(body.Data(), body.Size()) != section.storedCrc64)
            return MappingResult::IntegrityFailure;
        filesystem::MemoryFileReader bodyFile(body, 0);
        ser::BinaryReader data(bodyFile);
        u32 version = 0, contextCount = 0, actionCount = 0, bindingCount = 0, initialCount = 0;
        if (!data.ReadU32(version) || version != BodyVersion || !data.ReadU32(contextCount) || !data.ReadU32(actionCount) || !data.ReadU32(bindingCount) ||
            !data.ReadU32(initialCount) || contextCount > limits.maximumContexts || actionCount > limits.maximumActions ||
            bindingCount > limits.maximumBindings || initialCount > limits.maximumInitialContexts)
            return MappingResult::InvalidLayout;
        m_contexts.Resize(contextCount);
        m_actions.Resize(actionCount);
        m_bindings.Resize(bindingCount);
        m_initialContexts.Resize(initialCount);
        if (m_contexts.Size() != contextCount || m_actions.Size() != actionCount || m_bindings.Size() != bindingCount ||
            m_initialContexts.Size() != initialCount)
        {
            Close();
            return MappingResult::LimitExceeded;
        }
        for (MappingContextRecord& record : m_contexts)
            if (!ReadContext(data, record))
            {
                Close();
                return MappingResult::InvalidLayout;
            }
        for (MappingActionRecord& record : m_actions)
            if (!ReadAction(data, record))
            {
                Close();
                return MappingResult::InvalidLayout;
            }
        for (MappingBindingRecord& record : m_bindings)
            if (!ReadBinding(data, record))
            {
                Close();
                return MappingResult::InvalidLayout;
            }
        for (ContextId& context : m_initialContexts)
            if (!data.ReadU64(context))
            {
                Close();
                return MappingResult::InvalidLayout;
            }
        if (!data.IsGood() || data.GetRemaining() != 0)
        {
            Close();
            return MappingResult::InvalidLayout;
        }
        m_open = true;
        ActionMap validator;
        result = Install(validator, true);
        if (result != MappingResult::Success)
        {
            Close();
            return result;
        }
        return MappingResult::Success;
    }

    void MappingFile::Close() noexcept
    {
        m_contexts.Clear();
        m_actions.Clear();
        m_bindings.Clear();
        m_initialContexts.Clear();
        m_open = false;
    }
    bool MappingFile::IsOpen() const noexcept
    {
        return m_open;
    }
    containers::ArraySpan<const MappingContextRecord> MappingFile::GetContexts() const noexcept
    {
        return m_contexts;
    }
    containers::ArraySpan<const MappingActionRecord> MappingFile::GetActions() const noexcept
    {
        return m_actions;
    }
    containers::ArraySpan<const MappingBindingRecord> MappingFile::Bindings() const noexcept
    {
        return m_bindings;
    }
    containers::ArraySpan<const ContextId> MappingFile::GetInitialContexts() const noexcept
    {
        return m_initialContexts;
    }

    MappingResult MappingFile::Install(ActionMap& destination, const bool compile) const noexcept
    {
        if (!m_open)
            return MappingResult::InvalidState;
        ActionMap candidate;
        if (candidate.GetLastResult() == Result::InvalidState)
            return MappingResult::LimitExceeded;
        for (const MappingContextRecord& record : m_contexts)
        {
            const MappingResult result = Convert(candidate.RegisterContext(record.descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const MappingActionRecord& record : m_actions)
        {
            const MappingResult result = Convert(candidate.RegisterAction(record.descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const MappingBindingRecord& record : m_bindings)
        {
            const MappingResult result = Convert(candidate.RegisterBinding(record.descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const ContextId context : m_initialContexts)
        {
            const MappingResult result = Convert(candidate.PushContext(context));
            if (result != MappingResult::Success)
                return result;
        }
        const MappingResult result = compile ? Convert(candidate.Compile()) : MappingResult::Success;
        if (result == MappingResult::Success)
            destination = static_cast<ActionMap&&>(candidate);
        return result;
    }

    resources::ResourceTypeId MappingResource::GetType() const noexcept
    {
        return MappingResourceType;
    }
    MappingResult MappingResource::Open(const void* const data, const usize size, const MappingReadLimits& limits) noexcept
    {
        if (data == nullptr || size == 0 || size > static_cast<usize>(~u32{0}))
            return MappingResult::InvalidArgument;
        filesystem::MemoryFileReader reader(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        return m_file.Open(reader, limits);
    }
    const MappingFile& MappingResource::GetFile() const noexcept
    {
        return m_file;
    }

    MappingResult CookMapping(const MappingBuildDescription& description, filesystem::IFile& output) noexcept
    {
        if (description.contexts.Size() > MaximumContexts || description.actions.Size() > MaximumActions || description.bindings.Size() > MaximumBindings ||
            description.initialContexts.Size() > MaximumContextStackDepth * static_cast<u32>(ContextLayer::Count))
            return MappingResult::LimitExceeded;
        ActionMap validator;
        for (const ContextDescriptor& descriptor : description.contexts)
        {
            const MappingResult result = Convert(validator.RegisterContext(descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const ActionDescriptor& descriptor : description.actions)
        {
            const MappingResult result = Convert(validator.RegisterAction(descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const BindingDescriptor& descriptor : description.bindings)
        {
            const MappingResult result = Convert(validator.RegisterBinding(descriptor));
            if (result != MappingResult::Success)
                return result;
        }
        for (const ContextId context : description.initialContexts)
        {
            const MappingResult result = Convert(validator.PushContext(context));
            if (result != MappingResult::Success)
                return result;
        }
        MappingResult result = Convert(validator.Compile());
        if (result != MappingResult::Success)
            return result;

        u16 contextOrder[MaximumContexts]{}, actionOrder[MaximumActions]{}, bindingOrder[MaximumBindings]{};
        SortIndices(contextOrder, description.contexts.Size(), description.contexts.Data(), [](const ContextDescriptor& value) { return value.id; });
        SortIndices(actionOrder, description.actions.Size(), description.actions.Data(), [](const ActionDescriptor& value) { return value.id; });
        SortIndices(bindingOrder, description.bindings.Size(), description.bindings.Data(), [](const BindingDescriptor& value) { return value.id; });
        ByteArray body(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter bodyFile(body);
        ser::BinaryWriter data(bodyFile);
        if (!data.WriteU32(BodyVersion) || !data.WriteU32(description.contexts.Size()) || !data.WriteU32(description.actions.Size()) ||
            !data.WriteU32(description.bindings.Size()) || !data.WriteU32(description.initialContexts.Size()))
            return MappingResult::IoFailure;
        for (u32 index = 0; index < description.contexts.Size(); ++index)
            if (!WriteContext(data, description.contexts[contextOrder[index]]))
                return MappingResult::IoFailure;
        for (u32 index = 0; index < description.actions.Size(); ++index)
            if (!WriteAction(data, description.actions[actionOrder[index]]))
                return MappingResult::IoFailure;
        for (u32 index = 0; index < description.bindings.Size(); ++index)
            if (!WriteBinding(data, description.bindings[bindingOrder[index]]))
                return MappingResult::IoFailure;
        for (const ContextId context : description.initialContexts)
            if (!data.WriteU64(context))
                return MappingResult::IoFailure;
        if (!data.Flush())
            return MappingResult::IoFailure;

        ser::DocumentHeader header;
        header.magic = MappingMagic;
        header.version = FileVersion;
        header.flags = ser::DocumentFlags::Deterministic;
        header.sectionCount = 1;
        header.sectionTableOffset = ser::DocumentHeader::WireSize + body.Size();
        header.fileSize = header.sectionTableOffset + ser::SectionDescriptor::WireSize;
        ser::SectionDescriptor section;
        section.id = MappingSection;
        section.version = FileVersion;
        section.offset = ser::DocumentHeader::WireSize;
        section.storedSize = body.Size();
        section.logicalSize = body.Size();
        section.storedCrc64 = ser::Crc64(body.Data(), body.Size());
        ser::BinaryWriter writer(output);
        result = Convert(ser::WriteDocumentHeader(writer, header));
        if (result != MappingResult::Success || !writer.WriteBytes(body.Data(), body.Size()))
            return MappingResult::IoFailure;
        result = Convert(ser::WriteSectionDescriptor(writer, section));
        return result == MappingResult::Success && writer.Flush() ? MappingResult::Success : MappingResult::IoFailure;
    }
} // namespace vanguard::game_input
