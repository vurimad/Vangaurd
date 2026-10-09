#include <vanguard/editor/editor_extension.hpp>

#include <vanguard/projects/project.hpp>

namespace
{
    namespace ctr = vanguard::containers;
    namespace editor = vanguard::editor;

    [[nodiscard]] vanguard::u32 BoundedCStringLength(const char* const text, const vanguard::u32 maximum) noexcept
    {
        if (text == nullptr)
            return maximum + 1;
        vanguard::u32 length = 0;
        while (length <= maximum && text[length] != '\0')
            ++length;
        return length;
    }

    void ClearFailure(editor::EditorExtensionFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    bool Fail(editor::EditorExtensionFailure* const failure, const editor::EditorExtensionFailureCode code,
              const editor::EditorExtensionDescriptor* const descriptor = nullptr,
              const editor::EditorExtensionId related = {}, const char* const technicalName = nullptr) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = code;
            failure->extension = descriptor != nullptr ? descriptor->id : editor::EditorExtensionId{};
            failure->relatedExtension = related;
            failure->technicalName = technicalName != nullptr ? technicalName : (descriptor != nullptr ? descriptor->technicalName : nullptr);
        }
        return false;
    }

    [[nodiscard]] bool ValidSelectionPolicy(const editor::EditorExtensionSelectionPolicy policy) noexcept
    {
        constexpr vanguard::u8 valid = static_cast<vanguard::u8>(editor::EditorExtensionSelectionPolicy::BuiltIn) |
                                       static_cast<vanguard::u8>(editor::EditorExtensionSelectionPolicy::ProjectSelectable);
        return (static_cast<vanguard::u8>(policy) & static_cast<vanguard::u8>(~static_cast<unsigned int>(valid))) == 0;
    }

    [[nodiscard]] vanguard::i32 FindById(const ctr::ArraySpan<const editor::EditorExtensionDescriptor> descriptors,
                                         const editor::EditorExtensionId id) noexcept
    {
        for (vanguard::u32 index = 0; index < descriptors.Size(); ++index)
            if (descriptors[index].id == id)
                return static_cast<vanguard::i32>(index);
        return -1;
    }

    [[nodiscard]] vanguard::i32 FindByName(const ctr::ArraySpan<const editor::EditorExtensionDescriptor> descriptors,
                                           const ctr::StringView technicalName) noexcept
    {
        for (vanguard::u32 index = 0; index < descriptors.Size(); ++index)
        {
            const editor::EditorExtensionDescriptor& descriptor = descriptors[index];
            const vanguard::u32 length = BoundedCStringLength(descriptor.technicalName, vanguard::projects::MaximumTechnicalNameBytes);
            if (length <= vanguard::projects::MaximumTechnicalNameBytes && ctr::StringView(descriptor.technicalName, length) == technicalName)
                return static_cast<vanguard::i32>(index);
        }
        return -1;
    }

    [[nodiscard]] bool NameLess(const editor::EditorExtensionDescriptor& left, const editor::EditorExtensionDescriptor& right) noexcept
    {
        const vanguard::u32 leftLength = BoundedCStringLength(left.technicalName, vanguard::projects::MaximumTechnicalNameBytes);
        const vanguard::u32 rightLength = BoundedCStringLength(right.technicalName, vanguard::projects::MaximumTechnicalNameBytes);
        return ctr::StringView(left.technicalName, leftLength) < ctr::StringView(right.technicalName, rightLength);
    }

    [[nodiscard]] const editor::EditorExtensionDescriptor* SmallestRemaining(
        const ctr::ArraySpan<const editor::EditorExtensionDescriptor> descriptors, const bool* const emitted) noexcept
    {
        const editor::EditorExtensionDescriptor* smallest = nullptr;
        for (vanguard::u32 index = 0; index < descriptors.Size(); ++index)
        {
            if (emitted[index])
                continue;
            const editor::EditorExtensionDescriptor& descriptor = descriptors[index];
            if (smallest == nullptr || NameLess(descriptor, *smallest))
                smallest = &descriptor;
        }
        return smallest;
    }
} // namespace

namespace vanguard::editor
{
    bool EditorExtensionCatalog::Initialize(const containers::ArraySpan<const EditorExtensionDescriptor> descriptors,
                                            EditorExtensionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        m_descriptors = {};
        m_catalogOrder.Clear();
        m_status = {};

        if (descriptors.Size() > MaximumEditorExtensions)
        {
            m_status.state = EditorExtensionCatalogState::Failed;
            return Fail(failure, EditorExtensionFailureCode::CapacityExceeded);
        }
        if (!descriptors.Empty() && descriptors.Data() == nullptr)
        {
            m_status.state = EditorExtensionCatalogState::Failed;
            return Fail(failure, EditorExtensionFailureCode::InvalidDescriptor);
        }

        m_status.descriptorCount = descriptors.Size();
        for (u32 index = 0; index < descriptors.Size(); ++index)
        {
            const EditorExtensionDescriptor& descriptor = descriptors[index];
            const u32 technicalLength = BoundedCStringLength(descriptor.technicalName, projects::MaximumTechnicalNameBytes);
            const u32 displayLength = BoundedCStringLength(descriptor.displayName, MaximumEditorExtensionDisplayNameBytes);
            if (!descriptor.id.IsValid() || technicalLength == 0 || technicalLength > projects::MaximumTechnicalNameBytes ||
                !projects::IsTechnicalName(containers::StringView(descriptor.technicalName, technicalLength)) ||
                displayLength == 0 || displayLength > MaximumEditorExtensionDisplayNameBytes || descriptor.activate == nullptr ||
                descriptor.deactivate == nullptr || !ValidSelectionPolicy(descriptor.selection) ||
                descriptor.requiredExtensions.Size() > MaximumEditorExtensionDependencies ||
                (!descriptor.requiredExtensions.Empty() && descriptor.requiredExtensions.Data() == nullptr))
            {
                m_status.state = EditorExtensionCatalogState::Failed;
                return Fail(failure, EditorExtensionFailureCode::InvalidDescriptor, &descriptor);
            }

            for (u32 previous = 0; previous < index; ++previous)
            {
                if (descriptors[previous].id == descriptor.id)
                {
                    m_status.state = EditorExtensionCatalogState::Failed;
                    return Fail(failure, EditorExtensionFailureCode::DuplicateIdentity, &descriptor, descriptors[previous].id);
                }
                const u32 previousLength = BoundedCStringLength(descriptors[previous].technicalName, projects::MaximumTechnicalNameBytes);
                if (containers::StringView(descriptors[previous].technicalName, previousLength) ==
                    containers::StringView(descriptor.technicalName, technicalLength))
                {
                    m_status.state = EditorExtensionCatalogState::Failed;
                    return Fail(failure, EditorExtensionFailureCode::DuplicateTechnicalName, &descriptor, descriptors[previous].id);
                }
            }

            for (u32 dependencyIndex = 0; dependencyIndex < descriptor.requiredExtensions.Size(); ++dependencyIndex)
            {
                const EditorExtensionId dependency = descriptor.requiredExtensions[dependencyIndex];
                if (!dependency.IsValid())
                {
                    m_status.state = EditorExtensionCatalogState::Failed;
                    return Fail(failure, EditorExtensionFailureCode::InvalidDescriptor, &descriptor, dependency);
                }
                if (dependency == descriptor.id)
                {
                    m_status.state = EditorExtensionCatalogState::Failed;
                    return Fail(failure, EditorExtensionFailureCode::SelfDependency, &descriptor, dependency);
                }
                for (u32 previousDependency = 0; previousDependency < dependencyIndex; ++previousDependency)
                    if (descriptor.requiredExtensions[previousDependency] == dependency)
                    {
                        m_status.state = EditorExtensionCatalogState::Failed;
                        return Fail(failure, EditorExtensionFailureCode::DuplicateDependency, &descriptor, dependency);
                    }
            }
        }

        for (u32 index = 0; index < descriptors.Size(); ++index)
        {
            const EditorExtensionDescriptor& descriptor = descriptors[index];
            for (const EditorExtensionId dependency : descriptor.requiredExtensions)
                if (FindById(descriptors, dependency) < 0)
                {
                    m_status.state = EditorExtensionCatalogState::Failed;
                    return Fail(failure, EditorExtensionFailureCode::MissingDependency, &descriptor, dependency);
                }
        }

        u32 incoming[MaximumEditorExtensions]{};
        bool emitted[MaximumEditorExtensions]{};
        for (u32 index = 0; index < descriptors.Size(); ++index)
            incoming[index] = descriptors[index].requiredExtensions.Size();

        for (u32 outputIndex = 0; outputIndex < descriptors.Size(); ++outputIndex)
        {
            i32 candidate = -1;
            for (u32 index = 0; index < descriptors.Size(); ++index)
            {
                if (emitted[index] || incoming[index] != 0)
                    continue;
                if (candidate < 0 || NameLess(descriptors[index], descriptors[static_cast<u32>(candidate)]))
                    candidate = static_cast<i32>(index);
            }
            if (candidate < 0)
            {
                m_status.state = EditorExtensionCatalogState::Failed;
                const EditorExtensionDescriptor* const cycle = SmallestRemaining(descriptors, emitted);
                return Fail(failure, EditorExtensionFailureCode::DependencyCycle, cycle);
            }

            const u32 selected = static_cast<u32>(candidate);
            emitted[selected] = true;
            m_catalogOrder.PushBack(static_cast<u16>(selected));
            const EditorExtensionId selectedId = descriptors[selected].id;
            for (u32 dependent = 0; dependent < descriptors.Size(); ++dependent)
            {
                if (emitted[dependent])
                    continue;
                for (const EditorExtensionId dependency : descriptors[dependent].requiredExtensions)
                    if (dependency == selectedId)
                    {
                        --incoming[dependent];
                        break;
                    }
            }
        }

        m_descriptors = descriptors;
        m_status.state = EditorExtensionCatalogState::Ready;
        return true;
    }

    bool EditorExtensionCatalog::ResolveSelection(const containers::ArraySpan<const containers::String> projectSelections,
                                                  EditorExtensionSelection& selection, EditorExtensionFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        selection.m_order.Clear();
        if (m_status.state != EditorExtensionCatalogState::Ready)
            return Fail(failure, EditorExtensionFailureCode::CatalogNotReady);
        if (projectSelections.Size() > projects::MaximumPlugins)
            return Fail(failure, EditorExtensionFailureCode::CapacityExceeded);

        bool selected[MaximumEditorExtensions]{};
        for (u32 index = 0; index < m_descriptors.Size(); ++index)
            selected[index] = HasEditorExtensionPolicy(m_descriptors[index].selection, EditorExtensionSelectionPolicy::BuiltIn);

        for (u32 selectionIndex = 0; selectionIndex < projectSelections.Size(); ++selectionIndex)
        {
            const containers::String& requested = projectSelections[selectionIndex];
            if (!projects::IsTechnicalName(requested))
                return Fail(failure, EditorExtensionFailureCode::InvalidProjectSelection, nullptr, {}, requested.AsChar());
            for (u32 previous = 0; previous < selectionIndex; ++previous)
                if (projectSelections[previous] == requested)
                    return Fail(failure, EditorExtensionFailureCode::DuplicateProjectSelection, nullptr, {}, requested.AsChar());

            const i32 found = FindByName(m_descriptors, requested);
            if (found < 0)
                return Fail(failure, EditorExtensionFailureCode::UnknownProjectSelection, nullptr, {}, requested.AsChar());
            const u32 foundIndex = static_cast<u32>(found);
            const EditorExtensionDescriptor& descriptor = m_descriptors[foundIndex];
            if (!HasEditorExtensionPolicy(descriptor.selection, EditorExtensionSelectionPolicy::ProjectSelectable))
                return Fail(failure, EditorExtensionFailureCode::ProjectSelectionNotAllowed, &descriptor);
            selected[foundIndex] = true;
        }

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (u32 index = 0; index < m_descriptors.Size(); ++index)
            {
                if (!selected[index])
                    continue;
                for (const EditorExtensionId dependency : m_descriptors[index].requiredExtensions)
                {
                    const i32 found = FindById(m_descriptors, dependency);
                    if (found >= 0 && !selected[static_cast<u32>(found)])
                    {
                        selected[static_cast<u32>(found)] = true;
                        changed = true;
                    }
                }
            }
        }

        for (const u16 index : m_catalogOrder)
            if (selected[index])
                selection.m_order.PushBack(&m_descriptors[index]);
        return true;
    }

    const EditorExtensionDescriptor* EditorExtensionCatalog::Lookup(const EditorExtensionId id) const noexcept
    {
        if (m_status.state != EditorExtensionCatalogState::Ready)
            return nullptr;
        const i32 index = FindById(m_descriptors, id);
        return index >= 0 ? &m_descriptors[static_cast<u32>(index)] : nullptr;
    }

    const EditorExtensionDescriptor* EditorExtensionCatalog::Lookup(const containers::StringView technicalName) const noexcept
    {
        if (m_status.state != EditorExtensionCatalogState::Ready)
            return nullptr;
        const i32 index = FindByName(m_descriptors, technicalName);
        return index >= 0 ? &m_descriptors[static_cast<u32>(index)] : nullptr;
    }
} // namespace vanguard::editor
