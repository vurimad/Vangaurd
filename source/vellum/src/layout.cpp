#include <vanguard/vellum/instance_storage.hpp>
#include <vanguard/vellum/layout_storage.hpp>

#include <bit>
#include <cmath>
#include <limits>

namespace vanguard::vellum::detail
{
    namespace
    {
        inline constexpr u16 AllDirtyBits = (1u << DirtyDomainCount) - 1u;
        inline constexpr u16 ActiveDirtyBits = (1u << 1u) | (1u << 2u);
        inline constexpr u8 ForceMeasureSubtree = static_cast<u8>(LayoutForce::MeasureSubtree);
        inline constexpr u8 ForceArrangeSubtree = static_cast<u8>(LayoutForce::ArrangeSubtree);
        inline constexpr u8 ForcePresentationSubtree = static_cast<u8>(LayoutForce::PresentationSubtree);

        inline constexpr InvalidationDomain PresentationDomains =
            InvalidationDomain::Paint | InvalidationDomain::HitTest | InvalidationDomain::Navigation;
        inline constexpr InvalidationDomain LayoutDomains = InvalidationDomain::Measure | InvalidationDomain::Arrange;

        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure != nullptr)
            {
                failure->code = code;
                failure->message = message;
            }
            return false;
        }

        [[nodiscard]] constexpr u16 DomainBit(const u32 domainIndex) noexcept
        {
            return static_cast<u16>(1u << domainIndex);
        }

        [[nodiscard]] constexpr u16 DomainMask(const InvalidationDomain domains) noexcept
        {
            return static_cast<u16>(domains) & AllDirtyBits;
        }

        [[nodiscard]] bool IsLayoutGated(const InstanceState& instance, u32 slotIndex) noexcept;

        [[nodiscard]] DirtyFrontierStats& FrontierStats(RuntimeStats& stats, const u32 domainIndex) noexcept
        {
            switch (domainIndex)
            {
            case 0:
                return stats.textShapeDirty;
            case 1:
                return stats.measureDirty;
            case 2:
                return stats.arrangeDirty;
            case 3:
                return stats.paintDirty;
            case 4:
                return stats.hitTestDirty;
            default:
                return stats.navigationDirty;
            }
        }

        [[nodiscard]] NodeRow* ResolveRow(InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity)
                return nullptr;
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            if (slot.state != SlotState::Live || slot.row >= instance.denseCount)
                return nullptr;
            NodeRow& row = instance.rows[slot.row];
            return row.slot == slotIndex ? &row : nullptr;
        }

        [[nodiscard]] const NodeRow* ResolveRow(const InstanceState& instance, const u32 slotIndex) noexcept
        {
            return ResolveRow(const_cast<InstanceState&>(instance), slotIndex);
        }

        [[nodiscard]] bool CanMarkDirty(const InstanceState& instance, const u32 slotIndex,
                                        const InvalidationDomain domains, bool& layoutGated) noexcept
        {
            layoutGated = false;
            if (slotIndex >= instance.nodeCapacity)
                return false;
            const NodeSlot& slot = instance.nodeSlots[slotIndex];
            if (slot.state != SlotState::Live)
                return false;
            const u16 mask = DomainMask(domains);
            if ((mask & ActiveDirtyBits) == 0)
                return true;
            layoutGated = IsLayoutGated(instance, slotIndex);
            for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
            {
                const u16 bit = DomainBit(domain);
                if ((mask & bit) == 0 || !IsActiveLayoutDirtyDomain(domain) || layoutGated || (slot.queuedMask & bit) != 0)
                    continue;
                if (instance.dirtyFrontiers[domain].Full())
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ApplyDirty(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex,
                                      const InvalidationDomain domains, const bool layoutGated) noexcept
        {
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            const u16 mask = DomainMask(domains);
            slot.dirtyMask |= mask;
            for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
            {
                const u16 bit = DomainBit(domain);
                if ((mask & bit) == 0 || !IsActiveLayoutDirtyDomain(domain) || layoutGated || (slot.queuedMask & bit) != 0)
                    continue;
                FixedRing<WorkRef>& frontier = instance.dirtyFrontiers[domain];
                if (!frontier.Push({slotIndex, slot.generation}))
                    return false;
                slot.queuedMask |= bit;
                DirtyFrontierStats& stats = FrontierStats(runtime.stats, domain);
                ++stats.current;
                if (stats.current > stats.peak)
                    stats.peak = stats.current;
            }
            return true;
        }

        [[nodiscard]] bool PopDirty(Runtime::Impl& runtime, InstanceState& instance, const u32 domainIndex, WorkRef& work) noexcept
        {
            FixedRing<WorkRef>& frontier = instance.dirtyFrontiers[domainIndex];
            const WorkRef* const front = frontier.Front();
            if (front == nullptr)
                return false;
            work = *front;
            frontier.Pop();
            DirtyFrontierStats& stats = FrontierStats(runtime.stats, domainIndex);
            if (stats.current != 0)
                --stats.current;
            if (work.slot < instance.nodeCapacity)
            {
                NodeSlot& slot = instance.nodeSlots[work.slot];
                if (slot.generation == work.generation)
                    slot.queuedMask &= static_cast<u16>(~DomainBit(domainIndex));
            }
            return true;
        }

        [[nodiscard]] bool IsPositiveInfinity(const f32 value) noexcept
        {
            return std::isinf(value) && value > 0.0f;
        }

        [[nodiscard]] bool IsValidMinimumExtent(const f32 value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        [[nodiscard]] bool IsValidMaximumExtent(const f32 value) noexcept
        {
            return (std::isfinite(value) && value >= 0.0f) || IsPositiveInfinity(value);
        }

        [[nodiscard]] bool IsValidConstraints(const LayoutConstraints& constraints) noexcept
        {
            return IsValidMinimumExtent(constraints.minimum.width) && IsValidMinimumExtent(constraints.minimum.height) &&
                   IsValidMaximumExtent(constraints.maximum.width) && IsValidMaximumExtent(constraints.maximum.height) &&
                   constraints.minimum.width <= constraints.maximum.width && constraints.minimum.height <= constraints.maximum.height;
        }

        [[nodiscard]] bool IsValidRect(const Rect& rect) noexcept
        {
            return std::isfinite(rect.position.x) && std::isfinite(rect.position.y) && std::isfinite(rect.size.width) &&
                   std::isfinite(rect.size.height) && rect.size.width >= 0.0f && rect.size.height >= 0.0f;
        }

        [[nodiscard]] f32 ClampExtent(const f32 value, const f32 minimum, const f32 maximum) noexcept
        {
            f32 result = value < minimum ? minimum : value;
            if (!IsPositiveInfinity(maximum) && result > maximum)
                result = maximum;
            return result;
        }

        [[nodiscard]] Size ClampSize(const Size size, const LayoutConstraints& constraints) noexcept
        {
            return {ClampExtent(size.width, constraints.minimum.width, constraints.maximum.width),
                    ClampExtent(size.height, constraints.minimum.height, constraints.maximum.height)};
        }

        [[nodiscard]] bool IsLayoutGated(const InstanceState& instance, const u32 slotIndex) noexcept
        {
            if (slotIndex >= instance.nodeCapacity || instance.nodeSlots[slotIndex].detached)
                return true;
            const NodeRow* row = ResolveRow(instance, slotIndex);
            if (row == nullptr)
                return true;
            u32 parent = row->parent;
            while (parent != InvalidIndex)
            {
                const NodeRow* const parentRow = ResolveRow(instance, parent);
                if (parentRow == nullptr)
                    return true;
                const u32 parentRowIndex = static_cast<u32>(parentRow - instance.rows);
                if (instance.coreProperties[parentRowIndex].visibility == Visibility::Collapsed)
                    return true;
                parent = parentRow->parent;
            }
            return false;
        }

        [[nodiscard]] StackPanelPayload* ResolveStackPayload(InstanceState& instance, const NodeRow& row) noexcept
        {
            if (row.kind != NodeKind::StackPanel || row.payloadIndex >= instance.stackPanelCount)
                return nullptr;
            StackPanelPayload& payload = instance.stackPanels[row.payloadIndex];
            return payload.ownerSlot == row.slot ? &payload : nullptr;
        }

        [[nodiscard]] const StackPanelPayload* ResolveStackPayload(const InstanceState& instance, const NodeRow& row) noexcept
        {
            return ResolveStackPayload(const_cast<InstanceState&>(instance), row);
        }

        [[nodiscard]] SpacerPayload* ResolveSpacerPayload(InstanceState& instance, const NodeRow& row) noexcept
        {
            if (row.kind != NodeKind::Spacer || row.payloadIndex >= instance.spacerCount)
                return nullptr;
            SpacerPayload& payload = instance.spacers[row.payloadIndex];
            return payload.ownerSlot == row.slot ? &payload : nullptr;
        }

        [[nodiscard]] const SpacerPayload* ResolveSpacerPayload(const InstanceState& instance, const NodeRow& row) noexcept
        {
            return ResolveSpacerPayload(const_cast<InstanceState&>(instance), row);
        }

        struct WritableProperty
        {
            InstanceState* instance = nullptr;
            NodeSlot* slot = nullptr;
            NodeRow* row = nullptr;
            const PropertyDescriptor* descriptor = nullptr;
        };

        [[nodiscard]] bool ResolveWritableProperty(Runtime::Impl& runtime, const PropertyHandle property, const PropertyType expectedType,
                                                   WritableProperty& target, Failure* const failure) noexcept
        {
            target = {};
            if (!property.IsValid())
                return Fail(failure, FailureCode::InvalidArgument, "Vellum property handle is invalid");
            InstanceState* const instance = ResolveInstance(runtime, property.node.instance);
            if (instance == nullptr)
                return Fail(failure, FailureCode::StaleHandle, "Vellum property references a stale instance");
            NodeSlot* const slot = ResolveNodeSlot(*instance, property.node.local);
            if (slot == nullptr)
                return Fail(failure, FailureCode::StaleHandle, "Vellum property references a stale node");
            NodeRow* const row = ResolveRow(*instance, property.node.local.slot);
            if (row == nullptr)
                return Fail(failure, FailureCode::StaleHandle, "Vellum property node is not live");
            const PropertySchema* const schema = GetPropertySchema(row->kind);
            if (schema == nullptr || property.slot >= schema->count)
                return Fail(failure, FailureCode::PropertyNotFound, "Vellum property slot is not valid for this node kind");
            const PropertyDescriptor& descriptor = schema->descriptors[property.slot];
            if (property.expectedType != descriptor.type || expectedType != descriptor.type)
                return Fail(failure, FailureCode::PropertyTypeMismatch, "Vellum property value type does not match its descriptor");
            target = {instance, slot, row, &descriptor};
            return true;
        }

        [[nodiscard]] f32* ResolveFloatStorage(InstanceState& instance, NodeRow& row, const PropertyStorage storage) noexcept
        {
            const u32 rowIndex = static_cast<u32>(&row - instance.rows);
            switch (storage)
            {
            case PropertyStorage::CoreOpacity:
                return &instance.coreProperties[rowIndex].opacity;
            case PropertyStorage::StackSpacing:
            {
                StackPanelPayload* const payload = ResolveStackPayload(instance, row);
                return payload != nullptr ? &payload->spacing : nullptr;
            }
            case PropertyStorage::SpacerWidth:
            {
                SpacerPayload* const payload = ResolveSpacerPayload(instance, row);
                return payload != nullptr ? &payload->width : nullptr;
            }
            case PropertyStorage::SpacerHeight:
            {
                SpacerPayload* const payload = ResolveSpacerPayload(instance, row);
                return payload != nullptr ? &payload->height : nullptr;
            }
            default:
                return nullptr;
            }
        }

        [[nodiscard]] bool RectEqual(const Rect& left, const Rect& right) noexcept
        {
            return left == right;
        }

        template<class Type>
        [[nodiscard]] bool CanonicalEqual(const PropertyDescriptor& descriptor, const Type& left, const Type& right) noexcept
        {
            return descriptor.equality == PropertyEquality::ExactCanonical && left == right;
        }
    } // namespace

    const PropertySchema* GetPropertySchema(const NodeKind kind) noexcept
    {
        static constexpr PropertyDescriptor stackProperties[] = {
            {PropertyId::Visibility, PropertyType::Visibility, PropertyStorage::CoreVisibility, PresentationDomains,
             PropertyTransition::Visibility, PropertyCanonicalization::Exact, PropertyEquality::ExactCanonical},
            {PropertyId::Opacity, PropertyType::Float, PropertyStorage::CoreOpacity, InvalidationDomain::Paint, PropertyTransition::Static,
             PropertyCanonicalization::UnitFiniteFloat, PropertyEquality::ExactCanonical},
            {PropertyId::PanelOrientation, PropertyType::Orientation, PropertyStorage::StackOrientation, LayoutDomains,
             PropertyTransition::Static, PropertyCanonicalization::Exact, PropertyEquality::ExactCanonical},
            {PropertyId::PanelSpacing, PropertyType::Float, PropertyStorage::StackSpacing, LayoutDomains, PropertyTransition::Static,
             PropertyCanonicalization::NonNegativeFiniteFloat, PropertyEquality::ExactCanonical}};
        static constexpr PropertyDescriptor spacerProperties[] = {
            {PropertyId::Visibility, PropertyType::Visibility, PropertyStorage::CoreVisibility, PresentationDomains,
             PropertyTransition::Visibility, PropertyCanonicalization::Exact, PropertyEquality::ExactCanonical},
            {PropertyId::Opacity, PropertyType::Float, PropertyStorage::CoreOpacity, InvalidationDomain::Paint, PropertyTransition::Static,
             PropertyCanonicalization::UnitFiniteFloat, PropertyEquality::ExactCanonical},
            {PropertyId::Width, PropertyType::Float, PropertyStorage::SpacerWidth, LayoutDomains, PropertyTransition::Static,
             PropertyCanonicalization::NonNegativeFiniteFloat, PropertyEquality::ExactCanonical},
            {PropertyId::Height, PropertyType::Float, PropertyStorage::SpacerHeight, LayoutDomains, PropertyTransition::Static,
             PropertyCanonicalization::NonNegativeFiniteFloat, PropertyEquality::ExactCanonical}};
        static constexpr PropertySchema stackSchema{NodeKind::StackPanel, stackProperties,
                                                    static_cast<u16>(sizeof(stackProperties) / sizeof(stackProperties[0]))};
        static constexpr PropertySchema spacerSchema{NodeKind::Spacer, spacerProperties,
                                                     static_cast<u16>(sizeof(spacerProperties) / sizeof(spacerProperties[0]))};
        if (kind == NodeKind::StackPanel)
            return &stackSchema;
        if (kind == NodeKind::Spacer)
            return &spacerSchema;
        return nullptr;
    }

    bool CanonicalizePropertyFloat(const PropertyDescriptor& descriptor, const f32 input, f32& output) noexcept
    {
        if (!std::isfinite(input))
            return false;
        if (descriptor.canonicalization == PropertyCanonicalization::UnitFiniteFloat)
        {
            output = input < 0.0f ? 0.0f : (input > 1.0f ? 1.0f : input);
            if (output == 0.0f)
                output = 0.0f;
            return true;
        }
        if (descriptor.canonicalization != PropertyCanonicalization::NonNegativeFiniteFloat || input < 0.0f)
            return false;
        output = input == 0.0f ? 0.0f : input;
        return true;
    }

    bool IsNodeLayoutGated(const InstanceState& instance, const u32 slotIndex) noexcept
    {
        return IsLayoutGated(instance, slotIndex);
    }

    bool MarkDirty(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex, const InvalidationDomain domains) noexcept
    {
        bool layoutGated = false;
        if (!CanMarkDirty(instance, slotIndex, domains, layoutGated))
            return false;
        return ApplyDirty(runtime, instance, slotIndex, domains, layoutGated);
    }

    void CompactDirtyWork(Runtime::Impl& runtime, InstanceState& instance) noexcept
    {
        for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
        {
            FixedRing<WorkRef>& frontier = instance.dirtyFrontiers[domain];
            const u32 oldCount = frontier.count;
            u32 keptCount = 0;
            for (u32 offset = 0; offset < oldCount; ++offset)
            {
                const WorkRef* const front = frontier.Front();
                if (front == nullptr)
                    break;
                const WorkRef work = *front;
                frontier.Pop();
                bool keep = false;
                if (work.slot < instance.nodeCapacity)
                {
                    NodeSlot& slot = instance.nodeSlots[work.slot];
                    const bool exactLive = slot.generation == work.generation && slot.state == SlotState::Live;
                    keep = exactLive && !IsLayoutGated(instance, work.slot);
                    if (!keep && slot.generation == work.generation)
                    {
                        const u16 bit = DomainBit(domain);
                        slot.queuedMask &= static_cast<u16>(~bit);
                        if (!exactLive)
                        {
                            slot.dirtyMask &= static_cast<u16>(~bit);
                            slot.layoutForceMask = 0;
                        }
                    }
                }
                if (keep)
                {
                    if (frontier.Push(work))
                        ++keptCount;
                }
            }
            const u32 dropped = oldCount - keptCount;
            DirtyFrontierStats& stats = FrontierStats(runtime.stats, domain);
            stats.current = stats.current >= dropped ? stats.current - dropped : 0;
        }
    }

    void ReleaseDirtyWork(Runtime::Impl& runtime, InstanceState& instance) noexcept
    {
        for (u32 domain = 0; domain < DirtyDomainCount; ++domain)
        {
            FixedRing<WorkRef>& frontier = instance.dirtyFrontiers[domain];
            DirtyFrontierStats& stats = FrontierStats(runtime.stats, domain);
            stats.current = stats.current >= frontier.count ? stats.current - frontier.count : 0;
            frontier.head = 0;
            frontier.count = 0;
        }
    }

    void ResetNodeDirtyState(InstanceState& instance, const u32 slotIndex) noexcept
    {
        if (slotIndex >= instance.nodeCapacity)
            return;
        NodeSlot& slot = instance.nodeSlots[slotIndex];
        slot.dirtyMask = 0;
        slot.queuedMask = 0;
        slot.layoutForceMask = 0;
    }

    bool InitializeDenseLayoutState(Runtime::Impl&, InstanceState& instance, const u32 rowIndex, const u32 slotIndex,
                                    const NodeKind kind) noexcept
    {
        if (rowIndex >= instance.nodeCapacity || slotIndex >= instance.nodeCapacity)
            return false;
        NodeRow& row = instance.rows[rowIndex];
        instance.coreProperties[rowIndex] = {};
        instance.measureCaches[rowIndex] = {};
        instance.geometries[rowIndex] = {};
        if (kind == NodeKind::StackPanel)
        {
            if (instance.stackPanelCount >= instance.nodeCapacity)
                return false;
            const u32 payloadIndex = instance.stackPanelCount++;
            instance.stackPanels[payloadIndex] = {};
            instance.stackPanels[payloadIndex].ownerSlot = slotIndex;
            row.payloadIndex = payloadIndex;
            return true;
        }
        if (kind == NodeKind::Spacer)
        {
            if (instance.spacerCount >= instance.nodeCapacity)
                return false;
            const u32 payloadIndex = instance.spacerCount++;
            instance.spacers[payloadIndex] = {};
            instance.spacers[payloadIndex].ownerSlot = slotIndex;
            row.payloadIndex = payloadIndex;
            return true;
        }
        return false;
    }

    bool ApplyInitialProperty(InstanceState& instance, const u32 rowIndex, const PropertyId id, const PropertyType type,
                              const u32 canonicalBits) noexcept
    {
        if (rowIndex >= instance.denseCount)
            return false;
        NodeRow& row = instance.rows[rowIndex];
        const PropertySchema* const schema = GetPropertySchema(row.kind);
        if (schema == nullptr)
            return false;
        const PropertyDescriptor* descriptor = nullptr;
        for (u16 index = 0; index < schema->count; ++index)
        {
            if (schema->descriptors[index].id == id)
            {
                descriptor = &schema->descriptors[index];
                break;
            }
        }
        if (descriptor == nullptr || descriptor->type != type)
            return false;

        if (type == PropertyType::Float)
        {
            const f32 value = std::bit_cast<f32>(canonicalBits);
            f32 canonical = 0.0f;
            if (!CanonicalizePropertyFloat(*descriptor, value, canonical) || std::bit_cast<u32>(canonical) != canonicalBits)
                return false;
            f32* const storage = ResolveFloatStorage(instance, row, descriptor->storage);
            if (storage == nullptr)
                return false;
            *storage = canonical;
            return true;
        }
        if (type == PropertyType::Visibility)
        {
            if (descriptor->storage != PropertyStorage::CoreVisibility || canonicalBits > static_cast<u32>(Visibility::Collapsed))
                return false;
            instance.coreProperties[rowIndex].visibility = static_cast<Visibility>(canonicalBits);
            return true;
        }
        if (type == PropertyType::Orientation)
        {
            if (descriptor->storage != PropertyStorage::StackOrientation || canonicalBits > static_cast<u32>(Orientation::Vertical))
                return false;
            StackPanelPayload* const payload = ResolveStackPayload(instance, row);
            if (payload == nullptr)
                return false;
            payload->orientation = static_cast<Orientation>(canonicalBits);
            return true;
        }
        return false;
    }

    void RemoveDenseLayoutState(Runtime::Impl&, InstanceState& instance, const u32 rowIndex) noexcept
    {
        if (rowIndex >= instance.denseCount)
            return;
        const NodeRow& row = instance.rows[rowIndex];
        if (row.kind == NodeKind::StackPanel && row.payloadIndex < instance.stackPanelCount)
        {
            const u32 last = instance.stackPanelCount - 1u;
            if (row.payloadIndex != last)
            {
                instance.stackPanels[row.payloadIndex] = instance.stackPanels[last];
                const u32 movedOwner = instance.stackPanels[row.payloadIndex].ownerSlot;
                NodeRow* const movedRow = ResolveRow(instance, movedOwner);
                if (movedRow != nullptr)
                    movedRow->payloadIndex = row.payloadIndex;
            }
            instance.stackPanels[last] = {};
            --instance.stackPanelCount;
        }
        else if (row.kind == NodeKind::Spacer && row.payloadIndex < instance.spacerCount)
        {
            const u32 last = instance.spacerCount - 1u;
            if (row.payloadIndex != last)
            {
                instance.spacers[row.payloadIndex] = instance.spacers[last];
                const u32 movedOwner = instance.spacers[row.payloadIndex].ownerSlot;
                NodeRow* const movedRow = ResolveRow(instance, movedOwner);
                if (movedRow != nullptr)
                    movedRow->payloadIndex = row.payloadIndex;
            }
            instance.spacers[last] = {};
            --instance.spacerCount;
        }
        instance.coreProperties[rowIndex] = {};
        instance.measureCaches[rowIndex] = {};
        instance.geometries[rowIndex] = {};
    }

    void MoveDenseLayoutState(InstanceState& instance, const u32 destinationRow, const u32 sourceRow) noexcept
    {
        if (destinationRow >= instance.nodeCapacity || sourceRow >= instance.nodeCapacity || destinationRow == sourceRow)
            return;
        instance.coreProperties[destinationRow] = instance.coreProperties[sourceRow];
        instance.measureCaches[destinationRow] = instance.measureCaches[sourceRow];
        instance.geometries[destinationRow] = instance.geometries[sourceRow];
        instance.coreProperties[sourceRow] = {};
        instance.measureCaches[sourceRow] = {};
        instance.geometries[sourceRow] = {};
    }

    void MarkStructuralCreate(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex, const u32 parentSlot) noexcept
    {
        if (slotIndex >= instance.nodeCapacity)
            return;
        NodeSlot& slot = instance.nodeSlots[slotIndex];
        slot.layoutForceMask = ForceMeasureSubtree | ForceArrangeSubtree | ForcePresentationSubtree;
        const InvalidationDomain initialDomains = LayoutDomains | PresentationDomains;
        if (!slot.detached)
            (void)MarkDirty(runtime, instance, slotIndex, initialDomains);
        else
            slot.dirtyMask |= DomainMask(initialDomains);
        if (parentSlot != InvalidIndex)
            (void)MarkDirty(runtime, instance, parentSlot, LayoutDomains | PresentationDomains);
    }

    void MarkStructuralDestroy(Runtime::Impl& runtime, InstanceState& instance, const u32 parentSlot) noexcept
    {
        if (parentSlot != InvalidIndex)
            (void)MarkDirty(runtime, instance, parentSlot, LayoutDomains | PresentationDomains);
    }

    void MarkStructuralReparent(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex, const u32 oldParentSlot,
                                const u32 newParentSlot, const bool oldDetached, const bool newDetached,
                                const bool oldLayoutGated) noexcept
    {
        if (oldParentSlot != InvalidIndex && oldParentSlot == newParentSlot)
            (void)MarkDirty(runtime, instance, oldParentSlot, InvalidationDomain::Arrange | PresentationDomains);
        else
        {
            if (oldParentSlot != InvalidIndex)
                (void)MarkDirty(runtime, instance, oldParentSlot, LayoutDomains | PresentationDomains);
            if (newParentSlot != InvalidIndex)
                (void)MarkDirty(runtime, instance, newParentSlot, LayoutDomains | PresentationDomains);
        }
        if (slotIndex >= instance.nodeCapacity)
            return;
        NodeSlot& slot = instance.nodeSlots[slotIndex];
        if (oldDetached && !newDetached)
        {
            slot.layoutForceMask |= ForceMeasureSubtree | ForceArrangeSubtree | ForcePresentationSubtree;
            (void)MarkDirty(runtime, instance, slotIndex, LayoutDomains | PresentationDomains);
        }
        else if (!oldDetached && newDetached)
        {
            slot.layoutForceMask |= ForcePresentationSubtree;
            (void)MarkDirty(runtime, instance, slotIndex, PresentationDomains);
        }
        else if (!newDetached)
        {
            slot.layoutForceMask |= ForceArrangeSubtree | ForcePresentationSubtree;
            InvalidationDomain movedDomains = InvalidationDomain::Arrange | PresentationDomains;
            if (oldLayoutGated)
            {
                slot.layoutForceMask |= ForceMeasureSubtree;
                movedDomains = movedDomains | InvalidationDomain::Measure;
            }
            (void)MarkDirty(runtime, instance, slotIndex, movedDomains);
        }
    }

    bool ResolveProperty(const Runtime::Impl& runtime, const VellumNodeHandle node, const PropertyId id, PropertyHandle& property,
                         Failure* const failure) noexcept
    {
        ClearFailure(failure);
        property = {};
        if (!node.IsValid() || id == PropertyId::Invalid)
            return Fail(failure, FailureCode::InvalidArgument, "Vellum property resolution input is invalid");
        const InstanceState* const instance = ResolveInstance(runtime, node.instance);
        if (instance == nullptr || ResolveNodeSlot(*instance, node.local) == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum property resolution references a stale node");
        const NodeRow* const row = ResolveRow(*instance, node.local.slot);
        if (row == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum property resolution node is not live");
        const PropertySchema* const schema = GetPropertySchema(row->kind);
        if (schema == nullptr)
            return Fail(failure, FailureCode::WrongNodeKind, "Vellum node kind has no property schema");
        for (u16 index = 0; index < schema->count; ++index)
        {
            const PropertyDescriptor& descriptor = schema->descriptors[index];
            if (descriptor.id == id)
            {
                property = {node, index, descriptor.type};
                return true;
            }
        }
        return Fail(failure, FailureCode::PropertyNotFound, "Vellum property is not supported by this node kind");
    }

    bool WriteProperty(Runtime::Impl& runtime, const PropertyHandle property, const f32 value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        WritableProperty target;
        if (!ResolveWritableProperty(runtime, property, PropertyType::Float, target, failure))
            return false;
        f32 canonical = 0.0f;
        if (!CanonicalizePropertyFloat(*target.descriptor, value, canonical))
            return Fail(failure, FailureCode::InvalidArgument, "Vellum float property value is invalid");
        f32* const storage = ResolveFloatStorage(*target.instance, *target.row, target.descriptor->storage);
        if (storage == nullptr)
            return Fail(failure, FailureCode::InternalError, "Vellum float property storage is invalid");
        if (CanonicalEqual(*target.descriptor, *storage, canonical))
            return true;
        if (!MarkDirty(runtime, *target.instance, target.row->slot, target.descriptor->invalidation))
            return Fail(failure, FailureCode::InternalError, "Vellum dirty frontier capacity invariant was violated");
        if (target.descriptor->storage == PropertyStorage::StackSpacing)
            target.slot->layoutForceMask |= ForceArrangeSubtree;
        *storage = canonical;
        return true;
    }

    bool WriteProperty(Runtime::Impl& runtime, const PropertyHandle property, const Visibility value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (value > Visibility::Collapsed)
            return Fail(failure, FailureCode::InvalidArgument, "Vellum visibility value is invalid");
        WritableProperty target;
        if (!ResolveWritableProperty(runtime, property, PropertyType::Visibility, target, failure))
            return false;
        const u32 rowIndex = static_cast<u32>(target.row - target.instance->rows);
        Visibility& storage = target.instance->coreProperties[rowIndex].visibility;
        const Visibility previous = storage;
        if (CanonicalEqual(*target.descriptor, previous, value))
            return true;

        InvalidationDomain selfDomains = target.descriptor->invalidation;
        const bool collapsedTransition = target.descriptor->transition == PropertyTransition::Visibility &&
                                         (previous == Visibility::Collapsed || value == Visibility::Collapsed);
        if (collapsedTransition)
            selfDomains = selfDomains | LayoutDomains;
        const u32 parentSlot = target.row->parent;
        bool selfGated = false;
        bool parentGated = false;
        if (!CanMarkDirty(*target.instance, target.row->slot, selfDomains, selfGated) ||
            (collapsedTransition && parentSlot != InvalidIndex &&
             !CanMarkDirty(*target.instance, parentSlot, LayoutDomains, parentGated)))
            return Fail(failure, FailureCode::InternalError, "Vellum dirty frontier capacity invariant was violated");

        if (!ApplyDirty(runtime, *target.instance, target.row->slot, selfDomains, selfGated))
            return Fail(failure, FailureCode::InternalError, "Vellum dirty frontier capacity invariant was violated");
        if (collapsedTransition && parentSlot != InvalidIndex)
        {
            if (!ApplyDirty(runtime, *target.instance, parentSlot, LayoutDomains, parentGated))
                return Fail(failure, FailureCode::InternalError, "Vellum dirty frontier capacity invariant was violated");
        }
        if (collapsedTransition)
            target.slot->layoutForceMask |= ForceMeasureSubtree | ForceArrangeSubtree;
        if ((previous == Visibility::Visible) != (value == Visibility::Visible))
            target.slot->layoutForceMask |= ForcePresentationSubtree;
        storage = value;
        return true;
    }

    bool WriteProperty(Runtime::Impl& runtime, const PropertyHandle property, const Orientation value, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (value > Orientation::Vertical)
            return Fail(failure, FailureCode::InvalidArgument, "Vellum orientation value is invalid");
        WritableProperty target;
        if (!ResolveWritableProperty(runtime, property, PropertyType::Orientation, target, failure))
            return false;
        if (target.descriptor->storage != PropertyStorage::StackOrientation)
            return Fail(failure, FailureCode::PropertyTypeMismatch, "Vellum orientation property storage is invalid");
        StackPanelPayload* const payload = ResolveStackPayload(*target.instance, *target.row);
        if (payload == nullptr)
            return Fail(failure, FailureCode::InternalError, "Vellum stack-panel property storage is invalid");
        if (CanonicalEqual(*target.descriptor, payload->orientation, value))
            return true;
        if (!MarkDirty(runtime, *target.instance, target.row->slot, target.descriptor->invalidation))
            return Fail(failure, FailureCode::InternalError, "Vellum dirty frontier capacity invariant was violated");
        target.slot->layoutForceMask |= ForceMeasureSubtree | ForceArrangeSubtree;
        payload->orientation = value;
        return true;
    }

    namespace
    {
        [[nodiscard]] f32 AddExtent(const f32 left, const f32 right) noexcept
        {
            constexpr f32 maximum = std::numeric_limits<f32>::max();
            return right > maximum - left ? maximum : left + right;
        }

        [[nodiscard]] LayoutConstraints ChildConstraints(const LayoutConstraints& parent, const Orientation orientation) noexcept
        {
            const f32 unbounded = std::numeric_limits<f32>::infinity();
            if (orientation == Orientation::Horizontal)
                return {{0.0f, parent.minimum.height}, {unbounded, parent.maximum.height}};
            return {{parent.minimum.width, 0.0f}, {parent.maximum.width, unbounded}};
        }

        void AccumulateDesired(LayoutFrame& frame, const StackPanelPayload& panel, const Size desired) noexcept
        {
            if (panel.orientation == Orientation::Horizontal)
            {
                if (frame.participatingChildren != 0)
                    frame.accumulated.width = AddExtent(frame.accumulated.width, panel.spacing);
                frame.accumulated.width = AddExtent(frame.accumulated.width, desired.width);
                if (desired.height > frame.accumulated.height)
                    frame.accumulated.height = desired.height;
            }
            else
            {
                if (frame.participatingChildren != 0)
                    frame.accumulated.height = AddExtent(frame.accumulated.height, panel.spacing);
                frame.accumulated.height = AddExtent(frame.accumulated.height, desired.height);
                if (desired.width > frame.accumulated.width)
                    frame.accumulated.width = desired.width;
            }
            ++frame.participatingChildren;
        }

        [[nodiscard]] bool CommitMeasure(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex,
                                         const LayoutConstraints& constraints, const Size desired) noexcept
        {
            NodeRow* const row = ResolveRow(instance, slotIndex);
            if (row == nullptr)
                return false;
            const u32 rowIndex = static_cast<u32>(row - instance.rows);
            MeasureCache& cache = instance.measureCaches[rowIndex];
            const bool changed = !cache.valid || cache.desired != desired;
            cache.constraints = constraints;
            cache.desired = desired;
            cache.valid = true;
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            slot.dirtyMask &= static_cast<u16>(~DomainBit(1));
            slot.layoutForceMask &= static_cast<u8>(~ForceMeasureSubtree);
            ++runtime.stats.measuredNodes;
            if (!changed)
                return true;
            if (!MarkDirty(runtime, instance, slotIndex, InvalidationDomain::Arrange))
                return false;
            if (row->parent != InvalidIndex && !MarkDirty(runtime, instance, row->parent, LayoutDomains))
                return false;
            return true;
        }

        [[nodiscard]] bool MeasureSubtree(Runtime::Impl& runtime, InstanceState& instance, const u32 rootSlot,
                                          const LayoutConstraints& constraints, const bool forceSubtree,
                                          Failure* const failure) noexcept
        {
            if (rootSlot >= instance.nodeCapacity || instance.layoutScratch == nullptr)
                return Fail(failure, FailureCode::InternalError, "Vellum measure root is invalid");
            u32 stackCount = 1;
            instance.layoutScratch[0] = {};
            instance.layoutScratch[0].slot = rootSlot;
            instance.layoutScratch[0].constraints = constraints;
            instance.layoutScratch[0].forceSubtree = forceSubtree ||
                                                        (instance.nodeSlots[rootSlot].layoutForceMask & ForceMeasureSubtree) != 0;

            while (stackCount != 0)
            {
                LayoutFrame& frame = instance.layoutScratch[stackCount - 1u];
                NodeRow* const row = ResolveRow(instance, frame.slot);
                if (row == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum measure traversal encountered a stale node");
                const u32 rowIndex = static_cast<u32>(row - instance.rows);
                CoreProperties& core = instance.coreProperties[rowIndex];

                if (!frame.entered)
                {
                    frame.entered = true;
                    if (core.visibility == Visibility::Collapsed)
                    {
                        if (!CommitMeasure(runtime, instance, frame.slot, frame.constraints, {}))
                            return Fail(failure, FailureCode::InternalError, "Vellum measure invalidation invariant was violated");
                        --stackCount;
                        continue;
                    }
                    if (row->kind == NodeKind::Spacer)
                    {
                        const SpacerPayload* const spacer = ResolveSpacerPayload(instance, *row);
                        if (spacer == nullptr)
                            return Fail(failure, FailureCode::InternalError, "Vellum spacer measure payload is invalid");
                        const Size desired = ClampSize({spacer->width, spacer->height}, frame.constraints);
                        if (!CommitMeasure(runtime, instance, frame.slot, frame.constraints, desired))
                            return Fail(failure, FailureCode::InternalError, "Vellum measure invalidation invariant was violated");
                        --stackCount;
                        continue;
                    }
                    if (row->kind != NodeKind::StackPanel)
                        return Fail(failure, FailureCode::WrongNodeKind, "Vellum node kind has no measure policy");
                    frame.nextChild = row->firstChild;
                    frame.pendingChild = InvalidIndex;
                    frame.accumulated = {};
                    frame.participatingChildren = 0;
                }

                const StackPanelPayload* const panel = ResolveStackPayload(instance, *row);
                if (panel == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum stack-panel measure payload is invalid");
                if (frame.pendingChild != InvalidIndex)
                {
                    const NodeRow* const childRow = ResolveRow(instance, frame.pendingChild);
                    if (childRow == nullptr)
                        return Fail(failure, FailureCode::InternalError, "Vellum measure child became stale during traversal");
                    const MeasureCache& childCache = instance.measureCaches[childRow - instance.rows];
                    if (!childCache.valid)
                        return Fail(failure, FailureCode::InternalError, "Vellum measured child did not publish a cache");
                    AccumulateDesired(frame, *panel, childCache.desired);
                    frame.pendingChild = InvalidIndex;
                    continue;
                }

                if (frame.nextChild == InvalidIndex)
                {
                    const Size desired = ClampSize(frame.accumulated, frame.constraints);
                    if (!CommitMeasure(runtime, instance, frame.slot, frame.constraints, desired))
                        return Fail(failure, FailureCode::InternalError, "Vellum measure invalidation invariant was violated");
                    --stackCount;
                    continue;
                }

                const u32 childSlotIndex = frame.nextChild;
                NodeRow* const childRow = ResolveRow(instance, childSlotIndex);
                if (childRow == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum measure child topology is invalid");
                frame.nextChild = childRow->nextSibling;
                const u32 childRowIndex = static_cast<u32>(childRow - instance.rows);
                if (instance.coreProperties[childRowIndex].visibility == Visibility::Collapsed)
                    continue;

                const LayoutConstraints childConstraints = ChildConstraints(frame.constraints, panel->orientation);
                const MeasureCache& childCache = instance.measureCaches[childRowIndex];
                const NodeSlot& childSlot = instance.nodeSlots[childSlotIndex];
                const bool childNeedsMeasure = frame.forceSubtree || !childCache.valid || childCache.constraints != childConstraints ||
                                               (childSlot.dirtyMask & DomainBit(1)) != 0 ||
                                               (childSlot.layoutForceMask & ForceMeasureSubtree) != 0;
                if (!childNeedsMeasure)
                {
                    ++runtime.stats.cleanNodeDiscoveries;
                    AccumulateDesired(frame, *panel, childCache.desired);
                    continue;
                }
                if (stackCount >= runtime.config.maximumHierarchyDepth)
                    return Fail(failure, FailureCode::CapacityExceeded, "Vellum measure traversal exceeds the configured hierarchy depth");
                frame.pendingChild = childSlotIndex;
                LayoutFrame& childFrame = instance.layoutScratch[stackCount++];
                childFrame = {};
                childFrame.slot = childSlotIndex;
                childFrame.constraints = childConstraints;
                childFrame.forceSubtree = frame.forceSubtree || (childSlot.layoutForceMask & ForceMeasureSubtree) != 0;
            }
            return true;
        }

        [[nodiscard]] bool PublishGeometry(Runtime::Impl& runtime, InstanceState& instance, const u32 slotIndex, const Rect& rect,
                                           const u64 revision) noexcept
        {
            NodeRow* const row = ResolveRow(instance, slotIndex);
            if (row == nullptr)
                return false;
            const u32 rowIndex = static_cast<u32>(row - instance.rows);
            Geometry& geometry = instance.geometries[rowIndex];
            const bool changed = geometry.generation == 0 || !RectEqual(geometry.rect, rect);
            if (changed && !MarkDirty(runtime, instance, slotIndex, PresentationDomains))
                return false;
            if (changed)
            {
                geometry.rect = rect;
                geometry.generation = revision;
            }
            NodeSlot& slot = instance.nodeSlots[slotIndex];
            slot.dirtyMask &= static_cast<u16>(~DomainBit(2));
            slot.layoutForceMask &= static_cast<u8>(~ForceArrangeSubtree);
            ++runtime.stats.arrangedNodes;
            return true;
        }

        [[nodiscard]] f32 MinimumExtent(const f32 left, const f32 right) noexcept
        {
            return left < right ? left : right;
        }

        [[nodiscard]] f32 AddCoordinate(const f32 base, const f32 offset) noexcept
        {
            constexpr f64 maximum = static_cast<f64>(std::numeric_limits<f32>::max());
            constexpr f64 minimum = -maximum;
            const f64 sum = static_cast<f64>(base) + static_cast<f64>(offset);
            if (sum > maximum)
                return std::numeric_limits<f32>::max();
            if (sum < minimum)
                return -std::numeric_limits<f32>::max();
            return static_cast<f32>(sum);
        }

        [[nodiscard]] bool ArrangeSubtree(Runtime::Impl& runtime, InstanceState& instance, const u32 rootSlot, const Rect rootRect,
                                          const u64 revision, const bool forceSubtree, Failure* const failure) noexcept
        {
            u32 stackCount = 1;
            instance.layoutScratch[0] = {};
            instance.layoutScratch[0].slot = rootSlot;
            instance.layoutScratch[0].rect = rootRect;
            instance.layoutScratch[0].forceSubtree = forceSubtree ||
                                                        (instance.nodeSlots[rootSlot].layoutForceMask & ForceArrangeSubtree) != 0;

            while (stackCount != 0)
            {
                LayoutFrame& frame = instance.layoutScratch[stackCount - 1u];
                NodeRow* const row = ResolveRow(instance, frame.slot);
                if (row == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum arrange traversal encountered a stale node");
                const u32 rowIndex = static_cast<u32>(row - instance.rows);
                CoreProperties& core = instance.coreProperties[rowIndex];

                if (!frame.entered)
                {
                    frame.entered = true;
                    if (core.visibility == Visibility::Collapsed)
                    {
                        const Rect collapsedRect{frame.rect.position, {}};
                        if (!PublishGeometry(runtime, instance, frame.slot, collapsedRect, revision))
                            return Fail(failure, FailureCode::InternalError, "Vellum geometry invalidation invariant was violated");
                        --stackCount;
                        continue;
                    }

                    const Geometry& previousGeometry = instance.geometries[rowIndex];
                    const bool geometryChanged = previousGeometry.generation == 0 || !RectEqual(previousGeometry.rect, frame.rect);
                    frame.forceSubtree = frame.forceSubtree || geometryChanged ||
                                         (instance.nodeSlots[frame.slot].layoutForceMask & ForceArrangeSubtree) != 0;
                    if (!PublishGeometry(runtime, instance, frame.slot, frame.rect, revision))
                        return Fail(failure, FailureCode::InternalError, "Vellum geometry invalidation invariant was violated");

                    if (row->kind == NodeKind::Spacer)
                    {
                        --stackCount;
                        continue;
                    }
                    if (row->kind != NodeKind::StackPanel)
                        return Fail(failure, FailureCode::WrongNodeKind, "Vellum node kind has no arrange policy");
                    frame.nextChild = row->firstChild;
                    frame.accumulated = {};
                    frame.participatingChildren = 0;
                }

                const StackPanelPayload* const panel = ResolveStackPayload(instance, *row);
                if (panel == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum stack-panel arrange payload is invalid");
                if (frame.nextChild == InvalidIndex)
                {
                    --stackCount;
                    continue;
                }

                const u32 childSlotIndex = frame.nextChild;
                NodeRow* const childRow = ResolveRow(instance, childSlotIndex);
                if (childRow == nullptr)
                    return Fail(failure, FailureCode::InternalError, "Vellum arrange child topology is invalid");
                frame.nextChild = childRow->nextSibling;
                const u32 childRowIndex = static_cast<u32>(childRow - instance.rows);
                const CoreProperties& childCore = instance.coreProperties[childRowIndex];
                if (childCore.visibility == Visibility::Collapsed)
                    continue;

                Rect childRect;
                childRect.position = frame.rect.position;
                {
                    const MeasureCache& childMeasure = instance.measureCaches[childRowIndex];
                    if (!childMeasure.valid)
                        return Fail(failure, FailureCode::InvalidState, "Vellum arrange requires measured child geometry");
                    if (panel->orientation == Orientation::Horizontal)
                    {
                        f32 cursor = frame.accumulated.width;
                        if (frame.participatingChildren != 0)
                            cursor = AddExtent(cursor, panel->spacing);
                        cursor = MinimumExtent(cursor, frame.rect.size.width);
                        const f32 remaining = cursor < frame.rect.size.width ? frame.rect.size.width - cursor : 0.0f;
                        childRect.position.x = AddCoordinate(childRect.position.x, cursor);
                        childRect.size.width = MinimumExtent(childMeasure.desired.width, remaining);
                        childRect.size.height = MinimumExtent(childMeasure.desired.height, frame.rect.size.height);
                        frame.accumulated.width = AddExtent(cursor, childRect.size.width);
                    }
                    else
                    {
                        f32 cursor = frame.accumulated.height;
                        if (frame.participatingChildren != 0)
                            cursor = AddExtent(cursor, panel->spacing);
                        cursor = MinimumExtent(cursor, frame.rect.size.height);
                        const f32 remaining = cursor < frame.rect.size.height ? frame.rect.size.height - cursor : 0.0f;
                        childRect.position.y = AddCoordinate(childRect.position.y, cursor);
                        childRect.size.height = MinimumExtent(childMeasure.desired.height, remaining);
                        childRect.size.width = MinimumExtent(childMeasure.desired.width, frame.rect.size.width);
                        frame.accumulated.height = AddExtent(cursor, childRect.size.height);
                    }
                    ++frame.participatingChildren;
                }

                const Geometry& childGeometry = instance.geometries[childRowIndex];
                const NodeSlot& childSlot = instance.nodeSlots[childSlotIndex];
                const bool childGeometryChanged = childGeometry.generation == 0 || !RectEqual(childGeometry.rect, childRect);
                const bool childNeedsArrange = frame.forceSubtree || childGeometryChanged ||
                                               (childSlot.dirtyMask & DomainBit(2)) != 0 ||
                                               (childSlot.layoutForceMask & ForceArrangeSubtree) != 0;
                if (!childNeedsArrange)
                {
                    ++runtime.stats.cleanNodeDiscoveries;
                    continue;
                }
                if (stackCount >= runtime.config.maximumHierarchyDepth)
                    return Fail(failure, FailureCode::CapacityExceeded, "Vellum arrange traversal exceeds the configured hierarchy depth");
                LayoutFrame& childFrame = instance.layoutScratch[stackCount++];
                childFrame = {};
                childFrame.slot = childSlotIndex;
                childFrame.rect = childRect;
                childFrame.forceSubtree = frame.forceSubtree || childGeometryChanged ||
                                          (childSlot.layoutForceMask & ForceArrangeSubtree) != 0;
            }
            return true;
        }
    } // namespace

    bool MeasureInstance(Runtime::Impl& runtime, const VellumInstanceHandle handle, const LayoutConstraints& constraints, Size& desired,
                         Failure* const failure) noexcept
    {
        ClearFailure(failure);
        desired = {};
        if (!IsValidConstraints(constraints))
            return Fail(failure, FailureCode::InvalidArgument, "Vellum layout constraints are invalid");
        InstanceState* const instance = ResolveInstance(runtime, handle);
        if (instance == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum measure references a stale instance");
        if (instance->rootSlot == InvalidIndex || ResolveRow(*instance, instance->rootSlot) == nullptr)
            return Fail(failure, FailureCode::InvalidState, "Vellum instance has no live layout root");

        NodeRow* rootRow = ResolveRow(*instance, instance->rootSlot);
        const u32 rootRowIndex = static_cast<u32>(rootRow - instance->rows);
        MeasureCache& rootCache = instance->measureCaches[rootRowIndex];
        NodeSlot& rootSlot = instance->nodeSlots[instance->rootSlot];
        if (!rootCache.valid || rootCache.constraints != constraints || (rootSlot.dirtyMask & DomainBit(1)) != 0 ||
            (rootSlot.layoutForceMask & ForceMeasureSubtree) != 0)
        {
            if (!MeasureSubtree(runtime, *instance, instance->rootSlot, constraints,
                                (rootSlot.layoutForceMask & ForceMeasureSubtree) != 0, failure))
                return false;
        }

        WorkRef work;
        while (PopDirty(runtime, *instance, 1, work))
        {
            if (work.slot >= instance->nodeCapacity)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            NodeSlot& slot = instance->nodeSlots[work.slot];
            if (slot.generation != work.generation || slot.state != SlotState::Live)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            if ((slot.dirtyMask & DomainBit(1)) == 0)
                continue;
            if (IsLayoutGated(*instance, work.slot))
            {
                if (!slot.detached)
                    ++runtime.stats.collapsedDescendantsSkipped;
                continue;
            }
            NodeRow* const row = ResolveRow(*instance, work.slot);
            if (row == nullptr)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            const u32 rowIndex = static_cast<u32>(row - instance->rows);
            MeasureCache& cache = instance->measureCaches[rowIndex];
            if (!cache.valid && work.slot != instance->rootSlot)
            {
                if (row->parent == InvalidIndex || !MarkDirty(runtime, *instance, row->parent, InvalidationDomain::Measure))
                    return Fail(failure, FailureCode::InternalError, "Vellum measure cache has no constrained ancestor");
                continue;
            }
            const LayoutConstraints nodeConstraints = work.slot == instance->rootSlot ? constraints : cache.constraints;
            if (!MeasureSubtree(runtime, *instance, work.slot, nodeConstraints,
                                (slot.layoutForceMask & ForceMeasureSubtree) != 0, failure))
                return false;
        }

        rootRow = ResolveRow(*instance, instance->rootSlot);
        if (rootRow == nullptr)
            return Fail(failure, FailureCode::InternalError, "Vellum layout root became stale during measure");
        const MeasureCache& settledRoot = instance->measureCaches[rootRow - instance->rows];
        if (!settledRoot.valid)
            return Fail(failure, FailureCode::InternalError, "Vellum layout root did not publish a measure cache");
        desired = settledRoot.desired;
        return true;
    }

    bool ArrangeInstance(Runtime::Impl& runtime, const VellumInstanceHandle handle, const Rect& rect, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsValidRect(rect))
            return Fail(failure, FailureCode::InvalidArgument, "Vellum arrange rectangle is invalid");
        InstanceState* const instance = ResolveInstance(runtime, handle);
        if (instance == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum arrange references a stale instance");
        if (instance->rootSlot == InvalidIndex || ResolveRow(*instance, instance->rootSlot) == nullptr)
            return Fail(failure, FailureCode::InvalidState, "Vellum instance has no live layout root");
        if (!instance->dirtyFrontiers[1].Empty())
            return Fail(failure, FailureCode::InvalidState, "Vellum arrange requires MeasureInstance to settle pending measure work first");

        NodeRow* const rootRow = ResolveRow(*instance, instance->rootSlot);
        const u32 rootRowIndex = static_cast<u32>(rootRow - instance->rows);
        if (!instance->measureCaches[rootRowIndex].valid)
            return Fail(failure, FailureCode::InvalidState, "Vellum arrange requires a measured root");
        NodeSlot& rootSlot = instance->nodeSlots[instance->rootSlot];
        const Geometry& rootGeometry = instance->geometries[rootRowIndex];
        Rect effectiveRootRect = rect;
        if (instance->coreProperties[rootRowIndex].visibility == Visibility::Collapsed)
            effectiveRootRect.size = {};
        const bool rootNeedsArrange = rootGeometry.generation == 0 || !RectEqual(rootGeometry.rect, effectiveRootRect) ||
                                      (rootSlot.dirtyMask & DomainBit(2)) != 0 ||
                                      (rootSlot.layoutForceMask & ForceArrangeSubtree) != 0;
        if (!rootNeedsArrange && instance->dirtyFrontiers[2].Empty())
            return true;
        if (instance->nextGeometryRevision == ~u64{0})
            return Fail(failure, FailureCode::CapacityExceeded, "Vellum geometry revision space is exhausted");
        const u64 revision = instance->nextGeometryRevision++;

        if (rootNeedsArrange &&
            !ArrangeSubtree(runtime, *instance, instance->rootSlot, rect, revision,
                            (rootSlot.layoutForceMask & ForceArrangeSubtree) != 0, failure))
            return false;

        WorkRef work;
        while (PopDirty(runtime, *instance, 2, work))
        {
            if (work.slot >= instance->nodeCapacity)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            NodeSlot& slot = instance->nodeSlots[work.slot];
            if (slot.generation != work.generation || slot.state != SlotState::Live)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            if ((slot.dirtyMask & DomainBit(2)) == 0)
                continue;
            if (IsLayoutGated(*instance, work.slot))
            {
                if (!slot.detached)
                    ++runtime.stats.collapsedDescendantsSkipped;
                continue;
            }
            NodeRow* const row = ResolveRow(*instance, work.slot);
            if (row == nullptr)
            {
                ++runtime.stats.staleWorkRejected;
                continue;
            }
            const Geometry& geometry = instance->geometries[row - instance->rows];
            if (geometry.generation == 0 && work.slot != instance->rootSlot)
            {
                if (row->parent == InvalidIndex || !MarkDirty(runtime, *instance, row->parent, InvalidationDomain::Arrange))
                    return Fail(failure, FailureCode::InternalError, "Vellum arrange node has no arranged ancestor");
                continue;
            }
            const Rect nodeRect = work.slot == instance->rootSlot ? rect : geometry.rect;
            if (!ArrangeSubtree(runtime, *instance, work.slot, nodeRect, revision,
                                (slot.layoutForceMask & ForceArrangeSubtree) != 0, failure))
                return false;
        }
        return true;
    }

    bool GetGeometry(const Runtime::Impl& runtime, const VellumNodeHandle node, Geometry& geometry, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        geometry = {};
        const InstanceState* const instance = ResolveInstance(runtime, node.instance);
        const NodeSlot* const slot = instance != nullptr ? ResolveNodeSlot(*instance, node.local) : nullptr;
        if (slot == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum geometry query references a stale node");
        if (slot->detached)
            return Fail(failure, FailureCode::InvalidState, "Vellum detached node has no committed surface geometry");
        if (IsNodeLayoutGated(*instance, node.local.slot))
            return Fail(failure, FailureCode::InvalidState, "Vellum node is gated by a collapsed ancestor and has no current surface geometry");
        const NodeRow* const row = ResolveRow(*instance, node.local.slot);
        if (row == nullptr)
            return Fail(failure, FailureCode::StaleHandle, "Vellum geometry query node is not live");
        const Geometry& stored = instance->geometries[row - instance->rows];
        if (stored.generation == 0)
            return Fail(failure, FailureCode::InvalidState, "Vellum node has no committed arranged geometry");
        geometry = stored;
        return true;
    }
} // namespace vanguard::vellum::detail
