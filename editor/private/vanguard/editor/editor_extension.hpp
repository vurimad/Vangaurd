#pragma once

#include <vanguard/containers/containers.hpp>

namespace vanguard::editor
{
    inline constexpr u32 MaximumEditorExtensions = 256;
    inline constexpr u32 MaximumEditorExtensionDependencies = 32;
    inline constexpr u32 MaximumEditorExtensionDisplayNameBytes = 128;

    struct EditorExtensionId
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorExtensionId, EditorExtensionId) noexcept = default;
    };

    struct EditorExtensionOwner final
    {
        EditorExtensionId extension;
        u64 activationGeneration = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return extension.IsValid() && activationGeneration != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(EditorExtensionOwner, EditorExtensionOwner) noexcept = default;
    };

    enum class EditorExtensionSelectionPolicy : u8
    {
        DependencyOnly = 0,
        BuiltIn = 1u << 0u,
        ProjectSelectable = 1u << 1u
    };

    [[nodiscard]] constexpr EditorExtensionSelectionPolicy operator|(const EditorExtensionSelectionPolicy left,
                                                                     const EditorExtensionSelectionPolicy right) noexcept
    {
        return static_cast<EditorExtensionSelectionPolicy>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasEditorExtensionPolicy(const EditorExtensionSelectionPolicy value,
                                                          const EditorExtensionSelectionPolicy flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    struct EditorExtensionActivationContext;
    using EditorExtensionActivateFunction = bool (*)(EditorExtensionActivationContext& context, const void* descriptorData,
                                                      void*& privateState) noexcept;
    using EditorExtensionDeactivateFunction = void (*)(EditorExtensionActivationContext& context, const void* descriptorData,
                                                        void* privateState) noexcept;

    struct EditorExtensionDescriptor
    {
        EditorExtensionId id;
        const char* technicalName = nullptr;
        const char* displayName = nullptr;
        containers::ArraySpan<const EditorExtensionId> requiredExtensions;
        EditorExtensionSelectionPolicy selection = EditorExtensionSelectionPolicy::DependencyOnly;
        EditorExtensionActivateFunction activate = nullptr;
        EditorExtensionDeactivateFunction deactivate = nullptr;
        const void* data = nullptr;
    };

    enum class EditorExtensionFailureCode : u8
    {
        None,
        CatalogNotReady,
        InvalidDescriptor,
        DuplicateIdentity,
        DuplicateTechnicalName,
        DuplicateDependency,
        MissingDependency,
        SelfDependency,
        DependencyCycle,
        CapacityExceeded,
        InvalidProjectSelection,
        DuplicateProjectSelection,
        UnknownProjectSelection,
        ProjectSelectionNotAllowed,
        WrongThread,
        WrongPhase,
        ActivationFailure,
        CleanupFailure
    };

    struct EditorExtensionFailure
    {
        EditorExtensionFailureCode code = EditorExtensionFailureCode::None;
        EditorExtensionId extension;
        EditorExtensionId relatedExtension;
        // Borrowed from caller-supplied project/catalog storage. The failure must
        // not outlive that proven-lifetime backing.
        const char* technicalName = nullptr;
    };

    enum class EditorExtensionCatalogState : u8
    {
        Empty,
        Ready,
        Failed
    };

    struct EditorExtensionCatalogStatus
    {
        EditorExtensionCatalogState state = EditorExtensionCatalogState::Empty;
        u32 descriptorCount = 0;
    };

    class EditorExtensionSelection final
    {
    public:
        // Returned descriptor pointers borrow the catalog backing supplied to the
        // owning EditorExtensionCatalog::Initialize call.
        [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor* const> GetActivationOrder() const noexcept
        {
            return {m_order.TypedData(), m_order.Size()};
        }

        [[nodiscard]] u32 Size() const noexcept { return m_order.Size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_order.Empty(); }

    private:
        friend class EditorExtensionCatalog;
        containers::StaticArray<const EditorExtensionDescriptor*, MaximumEditorExtensions> m_order;
    };

    class EditorExtensionCatalog final
    {
    public:
        // The descriptor table and all descriptor-owned strings/dependency spans are
        // borrowed. They must outlive this catalog and every selection/failure that
        // refers to them.
        [[nodiscard]] bool Initialize(containers::ArraySpan<const EditorExtensionDescriptor> descriptors,
                                      EditorExtensionFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool ResolveSelection(containers::ArraySpan<const containers::String> projectSelections,
                                            EditorExtensionSelection& selection, EditorExtensionFailure* failure = nullptr) const noexcept;

        [[nodiscard]] const EditorExtensionDescriptor* Lookup(EditorExtensionId id) const noexcept;
        [[nodiscard]] const EditorExtensionDescriptor* Lookup(containers::StringView technicalName) const noexcept;
        [[nodiscard]] containers::ArraySpan<const EditorExtensionDescriptor> GetDescriptors() const noexcept { return m_descriptors; }
        [[nodiscard]] EditorExtensionCatalogStatus GetStatus() const noexcept { return m_status; }

    private:
        containers::ArraySpan<const EditorExtensionDescriptor> m_descriptors;
        containers::StaticArray<u16, MaximumEditorExtensions> m_catalogOrder;
        EditorExtensionCatalogStatus m_status;
    };
} // namespace vanguard::editor
