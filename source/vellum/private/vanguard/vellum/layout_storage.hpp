#pragma once

#include <vanguard/vellum/fixed_storage.hpp>
#include <vanguard/vellum/runtime_internal.hpp>

namespace vanguard::vellum::detail
{
    inline constexpr u32 DirtyDomainCount = 6;

    [[nodiscard]] constexpr bool IsActiveLayoutDirtyDomain(const u32 domainIndex) noexcept
    {
        return domainIndex == 1u || domainIndex == 2u;
    }

    enum class LayoutForce : u8
    {
        None = 0,
        MeasureSubtree = 1u << 0u,
        ArrangeSubtree = 1u << 1u,
        PresentationSubtree = 1u << 2u
    };

    struct WorkRef
    {
        u32 slot = InvalidIndex;
        u32 generation = 0;
    };

    struct CoreProperties
    {
        f32 opacity = 1.0f;
        Visibility visibility = Visibility::Visible;
    };

    struct StackPanelPayload
    {
        u32 ownerSlot = InvalidIndex;
        f32 spacing = 0.0f;
        Orientation orientation = Orientation::Horizontal;
    };

    struct SpacerPayload
    {
        u32 ownerSlot = InvalidIndex;
        f32 width = 0.0f;
        f32 height = 0.0f;
    };

    struct MeasureCache
    {
        LayoutConstraints constraints;
        Size desired;
        bool valid = false;
    };

    struct LayoutFrame
    {
        LayoutConstraints constraints;
        Rect rect;
        Size accumulated;
        u32 slot = InvalidIndex;
        u32 nextChild = InvalidIndex;
        u32 pendingChild = InvalidIndex;
        u32 participatingChildren = 0;
        bool entered = false;
        bool forceSubtree = false;
    };

    enum class PropertyStorage : u8
    {
        CoreVisibility,
        CoreOpacity,
        StackOrientation,
        StackSpacing,
        SpacerWidth,
        SpacerHeight
    };

    enum class PropertyTransition : u8
    {
        Static,
        Visibility
    };

    enum class PropertyCanonicalization : u8
    {
        Exact,
        NonNegativeFiniteFloat,
        UnitFiniteFloat
    };

    enum class PropertyEquality : u8
    {
        ExactCanonical
    };

    struct PropertyDescriptor
    {
        PropertyId id = PropertyId::Invalid;
        PropertyType type = PropertyType::Invalid;
        PropertyStorage storage = PropertyStorage::CoreVisibility;
        InvalidationDomain invalidation = InvalidationDomain::None;
        PropertyTransition transition = PropertyTransition::Static;
        PropertyCanonicalization canonicalization = PropertyCanonicalization::Exact;
        PropertyEquality equality = PropertyEquality::ExactCanonical;
    };

    struct PropertySchema
    {
        NodeKind kind = NodeKind::Invalid;
        const PropertyDescriptor* descriptors = nullptr;
        u16 count = 0;
    };

    struct InstanceState;

    [[nodiscard]] const PropertySchema* GetPropertySchema(NodeKind kind) noexcept;
    [[nodiscard]] bool CanonicalizePropertyFloat(const PropertyDescriptor& descriptor, f32 input, f32& output) noexcept;
    [[nodiscard]] bool InitializeDenseLayoutState(Runtime::Impl& runtime, InstanceState& instance, u32 rowIndex, u32 slotIndex,
                                                  NodeKind kind) noexcept;
    [[nodiscard]] bool ApplyInitialProperty(InstanceState& instance, u32 rowIndex, PropertyId id, PropertyType type,
                                            u32 canonicalBits) noexcept;
    void RemoveDenseLayoutState(Runtime::Impl& runtime, InstanceState& instance, u32 rowIndex) noexcept;
    void MoveDenseLayoutState(InstanceState& instance, u32 destinationRow, u32 sourceRow) noexcept;
    void CompactDirtyWork(Runtime::Impl& runtime, InstanceState& instance) noexcept;
    void ReleaseDirtyWork(Runtime::Impl& runtime, InstanceState& instance) noexcept;
    void ResetNodeDirtyState(InstanceState& instance, u32 slotIndex) noexcept;
    [[nodiscard]] bool IsNodeLayoutGated(const InstanceState& instance, u32 slotIndex) noexcept;

    [[nodiscard]] bool MarkDirty(Runtime::Impl& runtime, InstanceState& instance, u32 slotIndex, InvalidationDomain domains) noexcept;
    void MarkStructuralCreate(Runtime::Impl& runtime, InstanceState& instance, u32 slotIndex, u32 parentSlot) noexcept;
    void MarkStructuralDestroy(Runtime::Impl& runtime, InstanceState& instance, u32 parentSlot) noexcept;
    void MarkStructuralReparent(Runtime::Impl& runtime, InstanceState& instance, u32 slotIndex, u32 oldParentSlot, u32 newParentSlot,
                                bool oldDetached, bool newDetached, bool oldLayoutGated) noexcept;

    [[nodiscard]] bool ResolveProperty(const Runtime::Impl& runtime, VellumNodeHandle node, PropertyId id, PropertyHandle& property,
                                       Failure* failure) noexcept;
    [[nodiscard]] bool WriteProperty(Runtime::Impl& runtime, PropertyHandle property, f32 value, Failure* failure) noexcept;
    [[nodiscard]] bool WriteProperty(Runtime::Impl& runtime, PropertyHandle property, Visibility value, Failure* failure) noexcept;
    [[nodiscard]] bool WriteProperty(Runtime::Impl& runtime, PropertyHandle property, Orientation value, Failure* failure) noexcept;
    [[nodiscard]] bool MeasureInstance(Runtime::Impl& runtime, VellumInstanceHandle handle, const LayoutConstraints& constraints, Size& desired,
                                       Failure* failure) noexcept;
    [[nodiscard]] bool ArrangeInstance(Runtime::Impl& runtime, VellumInstanceHandle handle, const Rect& rect, Failure* failure) noexcept;
    [[nodiscard]] bool GetGeometry(const Runtime::Impl& runtime, VellumNodeHandle node, Geometry& geometry, Failure* failure) noexcept;
} // namespace vanguard::vellum::detail
