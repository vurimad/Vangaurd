#include <vanguard/editor/editor_surfaces.hpp>

#include <vanguard/concurrency/thread.hpp>

#include <atomic>
#include <cmath>

namespace
{
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;

    std::atomic<vanguard::u32> g_surfaceGeneration{0};
    std::atomic<vanguard::u32> g_contributionGeneration{0};
    std::atomic<vanguard::u32> g_commandBridgeGeneration{0};

    void ClearFailure(editor::EditorUiFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] bool Fail(editor::EditorUiFailure* const failure, const editor::EditorUiFailureCode code,
                            const char* const message, const editor::EditorUiSurfaceId surface = {},
                            const editor::EditorUiContributionId contribution = {}) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = code;
            failure->message = message;
            failure->surface = surface;
            failure->contribution = contribution;
        }
        return false;
    }

    [[nodiscard]] vanguard::u32 ReserveGeneration(std::atomic<vanguard::u32>& counter) noexcept
    {
        vanguard::u32 current = counter.load(std::memory_order_relaxed);
        for (;;)
        {
            if (current == ~vanguard::u32{0})
                return 0;
            if (counter.compare_exchange_weak(current, current + 1u, std::memory_order_relaxed, std::memory_order_relaxed))
                return current + 1u;
        }
    }

    [[nodiscard]] bool ValidLabel(const char* const label) noexcept
    {
        if (label == nullptr || label[0] == '\0')
            return false;
        vanguard::u32 length = 0;
        while (label[length] != '\0')
            if (++length >= editor::MaximumEditorUiSurfaceStringBytes)
                return false;
        return true;
    }

    void CopyLabel(char (&output)[editor::MaximumEditorUiSurfaceStringBytes], const char* const label) noexcept
    {
        output[0] = '\0';
        if (label == nullptr)
            return;
        vanguard::u32 index = 0;
        while (label[index] != '\0')
        {
            output[index] = label[index];
            ++index;
        }
        output[index] = '\0';
    }

    [[nodiscard]] bool ValidSurfaceKind(const detail::EditorUiSurfaceKind kind) noexcept
    {
        return static_cast<vanguard::u32>(kind) <= static_cast<vanguard::u32>(detail::EditorUiSurfaceKind::ContextMenu);
    }

    [[nodiscard]] bool ValidContributionKind(const detail::EditorUiContributionKind kind) noexcept
    {
        return static_cast<vanguard::u32>(kind) <= static_cast<vanguard::u32>(detail::EditorUiContributionKind::Separator);
    }

    [[nodiscard]] bool ValidPlacement(const detail::EditorUiPlacementRelation relation) noexcept
    {
        return static_cast<vanguard::u32>(relation) <= static_cast<vanguard::u32>(detail::EditorUiPlacementRelation::After);
    }

    [[nodiscard]] bool ValidMissingAnchorPolicy(const detail::EditorUiMissingAnchorPolicy policy) noexcept
    {
        return static_cast<vanguard::u32>(policy) <= static_cast<vanguard::u32>(detail::EditorUiMissingAnchorPolicy::FallbackAppend);
    }
} // namespace

namespace vanguard::editor::detail
{
    EditorSurfaceGenerationSource GetProductionEditorSurfaceGenerationSource() noexcept
    {
        return {&g_surfaceGeneration, &g_contributionGeneration};
    }

    bool EditorUiCommandBridgeSlot::Register(const EditorUiCommandBridge& bridge, EditorUiCommandBridgeToken& token,
                                             EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        token = {};
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread,
                        "editor surface command bridge registration requires the main thread");
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor surface command bridge registration requires a ready UI service");
        if (!bridge.IsValid())
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                        "editor surface command bridge descriptor is invalid");
        if (IsAttached())
            return Fail(failure, EditorUiFailureCode::InUse,
                        "editor surface command bridge is already registered");
        const u32 generation = ReserveGeneration(g_commandBridgeGeneration);
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted,
                        "editor surface command bridge generation space is exhausted");
        m_bridge = bridge;
        m_token = {generation};
        token = m_token;
        return true;
    }

    bool EditorUiCommandBridgeSlot::Unregister(const EditorUiCommandBridgeToken token,
                                               EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread,
                        "editor surface command bridge removal requires the main thread");
        if (m_uiState == nullptr || *m_uiState != EditorUiState::Ready)
            return Fail(failure, EditorUiFailureCode::WrongPhase,
                        "editor surface command bridge removal requires a ready UI service");
        if (!token.IsValid() || token != m_token || !IsAttached())
            return Fail(failure, EditorUiFailureCode::InvalidHandle,
                        "editor surface command bridge token is stale or invalid");
        m_bridge = {};
        m_token = {};
        return true;
    }

    EditorSurfaceRegistryCore::EditorSurfaceRegistryCore(const EditorUiState& uiState,
                                                         const EditorSurfaceGenerationSource generations) noexcept
        : m_uiState(&uiState), m_generations(generations)
    {
    }

    bool EditorSurfaceRegistryCore::InitializeBuiltIns(EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread, "editor surface initialization requires the main thread");
        if (m_initialized)
            return true;
        for (const SurfaceRecord& surface : m_surfaces)
            if (surface.active)
                return Fail(failure, EditorUiFailureCode::WrongPhase, "editor surface registry cannot initialize over live surface state");
        for (const ContributionRecord& contribution : m_contributions)
            if (contribution.active)
                return Fail(failure, EditorUiFailureCode::WrongPhase, "editor surface registry cannot initialize over live contribution state");

        u32 surfaceGenerations[2]{};
        for (u32& generation : surfaceGenerations)
        {
            generation = ReserveSurfaceGeneration();
            if (generation == 0)
                return Fail(failure, EditorUiFailureCode::GenerationExhausted, "editor surface generation space is exhausted");
        }

        constexpr u32 BuiltInContributionCount = 17;
        u32 contributionGenerations[BuiltInContributionCount]{};
        for (u32& generation : contributionGenerations)
        {
            generation = ReserveContributionGeneration();
            if (generation == 0)
                return Fail(failure, EditorUiFailureCode::GenerationExhausted, "editor surface contribution generation space is exhausted");
        }

        SurfaceRecord& menu = m_surfaces[0];
        menu.token = {0, surfaceGenerations[0]};
        menu.desc = {EditorMainMenuSurfaceId, EditorUiSurfaceKind::MainMenu};
        menu.active = true;
        menu.reachable = true;
        menu.builtIn = true;

        SurfaceRecord& toolbar = m_surfaces[1];
        toolbar.token = {1, surfaceGenerations[1]};
        toolbar.desc = {EditorMainToolbarSurfaceId, EditorUiSurfaceKind::MainToolbar};
        toolbar.active = true;
        toolbar.reachable = true;
        toolbar.builtIn = true;

        struct BuiltInContribution final
        {
            EditorUiContributionId id;
            EditorUiSurfaceId surface;
            EditorUiContributionKind kind;
            EditorUiContributionId parent;
            i32 order;
            EditorCommandId command;
            const char* label;
            u32 depth;
            bool structuralAnchor;
        };
        constexpr BuiltInContribution builtIns[]{
            {EditorMainMenuFileAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 100, {}, "File", 1, true},
            {EditorMainMenuEditAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 200, {}, "Edit", 1, true},
            {EditorMainMenuViewAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 300, {}, "View", 1, true},
            {EditorMainMenuWindowAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 350, {}, "Window", 1, true},
            {EditorMainMenuToolsAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 400, {}, "Tools", 1, true},
            {EditorMainMenuHelpAnchorId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu, {}, 500, {}, "Help", 1, true},
            {EditorMainMenuLayoutSubmenuId, EditorMainMenuSurfaceId, EditorUiContributionKind::Submenu,
             EditorMainMenuWindowAnchorId, 100, {}, "Layout", 2, false},
            {EditorMainMenuLayoutSaveAsId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 100, SaveCurrentLayoutAsCommandId, "Save Current Layout As...", 2, false},
            {EditorMainMenuLayoutSaveId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 110, SaveLayoutCommandId, "Save Layout", 2, false},
            {EditorMainMenuLayoutSwitchId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 200, SwitchLayoutCommandId, "Switch Layout...", 2, false},
            {EditorMainMenuLayoutRevertId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 210, RevertLayoutCommandId, "Revert Layout", 2, false},
            {EditorMainMenuLayoutRenameId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 300, RenameLayoutCommandId, "Rename Layout...", 2, false},
            {EditorMainMenuLayoutDuplicateId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 310, DuplicateLayoutCommandId, "Duplicate Layout...", 2, false},
            {EditorMainMenuLayoutDeleteId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 320, DeleteLayoutCommandId, "Delete Layout...", 2, false},
            {EditorMainMenuLayoutResetDefaultId, EditorMainMenuSurfaceId, EditorUiContributionKind::Command,
             EditorMainMenuLayoutSubmenuId, 400, ResetDefaultLayoutCommandId, "Reset to Default Layout", 2, false},
            {EditorMainToolbarStartAnchorId, EditorMainToolbarSurfaceId, EditorUiContributionKind::Separator,
             {}, -10000, {}, nullptr, 0, true},
            {EditorMainToolbarEndAnchorId, EditorMainToolbarSurfaceId, EditorUiContributionKind::Separator,
             {}, 10000, {}, nullptr, 0, true}};

        for (u32 index = 0; index < BuiltInContributionCount; ++index)
        {
            ContributionRecord& record = m_contributions[index];
            record.token = {index, contributionGenerations[index]};
            record.desc.id = builtIns[index].id;
            record.desc.surface = builtIns[index].surface;
            record.desc.kind = builtIns[index].kind;
            record.desc.parent = builtIns[index].parent;
            record.desc.placement = EditorUiPlacementRelation::Append;
            record.desc.missingAnchor = EditorUiMissingAnchorPolicy::Required;
            record.desc.order = builtIns[index].order;
            record.desc.command = builtIns[index].command;
            record.desc.label = record.label;
            CopyLabel(record.label, builtIns[index].label);
            if (builtIns[index].label == nullptr)
                record.desc.label = nullptr;
            record.depth = builtIns[index].depth;
            record.active = true;
            record.reachable = true;
            record.builtInAnchor = builtIns[index].structuralAnchor;
        }

        m_initialized = true;
        return true;
    }

    void EditorSurfaceRegistryCore::Reset() noexcept
    {
        for (SurfaceRecord& surface : m_surfaces)
            surface = {};
        for (ContributionRecord& contribution : m_contributions)
            contribution = {};
        m_pendingRequests.Clear();
        m_committedRequest = {};
        m_hasCommittedRequest = false;
        m_fallbackPlacementCount = 0;
        m_lastFallbackContribution = {};
        m_commandFailureCount = 0;
        m_repeatedCommandFailureCount = 0;
        m_lastCommandFailureContribution = {};
        m_lastCommandFailureCommand = {};
        m_lastCommandFailureCode = EditorCommandFailureCode::None;
        m_initialized = false;
    }

    bool EditorSurfaceRegistryCore::RegisterSurface(const EditorExtensionOwner& owner, const EditorUiSurfaceDesc& desc,
                                                    EditorUiSurfaceToken& output, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = {};
        if (!CanMutateTopology(failure, desc.id))
            return false;
        if (!m_initialized)
            return Fail(failure, EditorUiFailureCode::NotReady, "editor surface registry built-ins are not initialized", desc.id);
        if (!owner.IsValid())
            return Fail(failure, EditorUiFailureCode::OwnerRevoked, "editor surface owner is invalid", desc.id);
        if (!desc.id.IsValid() || !ValidSurfaceKind(desc.kind) || desc.kind != EditorUiSurfaceKind::ContextMenu)
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "owned editor surface descriptor must describe a context menu", desc.id);
        for (const SurfaceRecord& existing : m_surfaces)
            if (existing.active && existing.desc.id == desc.id)
                return Fail(failure, EditorUiFailureCode::DuplicateIdentity, "editor surface identity is already registered", desc.id);

        const u32 index = FindSurfaceSlot();
        if (index == ~u32{0})
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor surface capacity is exhausted", desc.id);
        const u32 generation = ReserveSurfaceGeneration();
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted, "editor surface generation space is exhausted", desc.id);

        SurfaceRecord& surface = m_surfaces[index];
        surface = {};
        surface.token = {index, generation};
        surface.desc = desc;
        surface.owner = owner;
        surface.active = true;
        surface.reachable = true;
        output = surface.token;
        return true;
    }

    bool EditorSurfaceRegistryCore::UnregisterSurface(const EditorExtensionOwner& owner, const EditorUiSurfaceToken token,
                                                      EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!CanMutateTopology(failure))
            return false;
        SurfaceRecord* const surface = FindSurface(token);
        if (surface == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor surface token is invalid");
        if (!owner.IsValid() || !(surface->owner == owner))
            return Fail(failure, EditorUiFailureCode::OwnerMismatch, "editor surface token owner does not match the registration", surface->desc.id);
        for (const ContributionRecord& contribution : m_contributions)
            if (contribution.active && contribution.desc.surface == surface->desc.id)
                return Fail(failure, EditorUiFailureCode::InUse, "editor surface still owns contributions", surface->desc.id,
                            contribution.desc.id);

        CancelContextMenuRequestsForSurface(surface->token);
        *surface = {};
        return true;
    }

    bool EditorSurfaceRegistryCore::RegisterContribution(const EditorExtensionOwner& owner, const EditorUiContributionDesc& desc,
                                                         EditorUiContributionToken& output, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        output = {};
        if (!CanMutateTopology(failure, desc.surface, desc.id))
            return false;
        if (!m_initialized)
            return Fail(failure, EditorUiFailureCode::NotReady, "editor surface registry built-ins are not initialized", desc.surface, desc.id);
        if (!owner.IsValid())
            return Fail(failure, EditorUiFailureCode::OwnerRevoked, "editor surface contribution owner is invalid", desc.surface, desc.id);
        if (!desc.id.IsValid() || !desc.surface.IsValid() || !ValidContributionKind(desc.kind) || !ValidPlacement(desc.placement) ||
            !ValidMissingAnchorPolicy(desc.missingAnchor))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor surface contribution descriptor is invalid", desc.surface, desc.id);
        if (desc.parent == desc.id || desc.sibling == desc.id)
            return Fail(failure, EditorUiFailureCode::HierarchyViolation,
                        "editor surface contribution cannot place itself as a parent or sibling anchor", desc.surface, desc.id);
        for (const ContributionRecord& existing : m_contributions)
            if (existing.active && existing.desc.id == desc.id)
                return Fail(failure, EditorUiFailureCode::DuplicateIdentity, "editor surface contribution identity is already registered",
                            desc.surface, desc.id);

        const SurfaceRecord* const surface = FindSurface(desc.surface);
        if (surface == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor surface contribution targets an unknown surface", desc.surface, desc.id);
        if (surface->owner.IsValid() && !(surface->owner == owner))
            return Fail(failure, EditorUiFailureCode::OwnerMismatch, "editor surface contribution targets a surface owned by another extension",
                        desc.surface, desc.id);
        if (surface->desc.kind == EditorUiSurfaceKind::MainToolbar)
        {
            if (desc.kind == EditorUiContributionKind::Submenu)
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor toolbar surfaces do not accept submenu contributions",
                            desc.surface, desc.id);
        }
        else if (desc.kind == EditorUiContributionKind::Tool)
        {
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                        "editor tool contributions require the main toolbar surface", desc.surface, desc.id);
        }

        switch (desc.kind)
        {
        case EditorUiContributionKind::Command:
            if (!desc.command.IsValid() || desc.icon != InvalidEditorResourceId || !ValidLabel(desc.label))
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor command contribution requires a command and bounded label only",
                            desc.surface, desc.id);
            break;
        case EditorUiContributionKind::Tool:
            if (desc.icon == InvalidEditorResourceId || !ValidLabel(desc.label))
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor,
                            "editor toolbar tool contribution requires an icon and bounded label",
                            desc.surface, desc.id);
            break;
        case EditorUiContributionKind::Submenu:
            if (desc.command.IsValid() || desc.icon != InvalidEditorResourceId || !ValidLabel(desc.label))
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor submenu contribution requires only a bounded label",
                            desc.surface, desc.id);
            break;
        case EditorUiContributionKind::Separator:
            if (desc.command.IsValid() || desc.icon != InvalidEditorResourceId || desc.label != nullptr)
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor separator contribution cannot carry command, icon or label data",
                            desc.surface, desc.id);
            break;
        }

        u32 depth = desc.kind == EditorUiContributionKind::Submenu ? 1u : 0u;
        if (desc.parent.IsValid())
        {
            const ContributionRecord* const parent = FindContribution(desc.parent);
            if (parent == nullptr)
                return Fail(failure, EditorUiFailureCode::MissingAnchor, "editor surface contribution parent is missing", desc.surface, desc.id);
            if (parent->desc.surface != desc.surface || parent->desc.kind != EditorUiContributionKind::Submenu)
                return Fail(failure, EditorUiFailureCode::HierarchyViolation, "editor surface contribution parent is not a container on the target surface",
                            desc.surface, desc.id);
            if (!IsPlacementOwnerAllowed(owner, *parent))
                return Fail(failure, EditorUiFailureCode::OwnerMismatch, "editor surface contribution cannot target another extension's private parent",
                            desc.surface, desc.id);
            depth = parent->depth + (desc.kind == EditorUiContributionKind::Submenu ? 1u : 0u);
            if (desc.kind == EditorUiContributionKind::Submenu && depth > MaximumEditorUiSurfaceDepth)
                return Fail(failure, EditorUiFailureCode::HierarchyViolation, "editor surface contribution exceeds maximum hierarchy depth",
                            desc.surface, desc.id);
        }

        EditorUiContributionId resolvedSibling = desc.sibling;
        EditorUiPlacementRelation resolvedPlacement = desc.placement;
        bool fallbackPlacement = false;
        if (desc.placement == EditorUiPlacementRelation::Append)
        {
            if (desc.sibling.IsValid())
                return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "append placement cannot name a sibling anchor", desc.surface, desc.id);
            resolvedSibling = {};
        }
        else
        {
            const ContributionRecord* const sibling = desc.sibling.IsValid() ? FindContribution(desc.sibling) : nullptr;
            if (sibling == nullptr)
            {
                if (desc.missingAnchor == EditorUiMissingAnchorPolicy::Required)
                    return Fail(failure, EditorUiFailureCode::MissingAnchor, "required editor surface sibling anchor is missing", desc.surface, desc.id);
                resolvedSibling = {};
                resolvedPlacement = EditorUiPlacementRelation::Append;
                fallbackPlacement = true;
            }
            else
            {
                if (sibling->desc.surface != desc.surface || sibling->desc.parent != desc.parent)
                    return Fail(failure, EditorUiFailureCode::HierarchyViolation,
                                "editor surface sibling anchor must share the target surface and parent", desc.surface, desc.id);
                if (!IsPlacementOwnerAllowed(owner, *sibling))
                    return Fail(failure, EditorUiFailureCode::OwnerMismatch,
                                "editor surface contribution cannot target another extension's private sibling", desc.surface, desc.id);
            }
        }

        const u32 index = FindContributionSlot();
        if (index == ~u32{0})
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor surface contribution capacity is exhausted", desc.surface, desc.id);
        const u32 generation = ReserveContributionGeneration();
        if (generation == 0)
            return Fail(failure, EditorUiFailureCode::GenerationExhausted, "editor surface contribution generation space is exhausted",
                        desc.surface, desc.id);

        ContributionRecord& contribution = m_contributions[index];
        contribution = {};
        contribution.token = {index, generation};
        contribution.desc = desc;
        contribution.desc.sibling = resolvedSibling;
        contribution.desc.placement = resolvedPlacement;
        contribution.owner = owner;
        contribution.depth = depth;
        contribution.active = true;
        contribution.reachable = true;
        contribution.fallbackPlacement = fallbackPlacement;
        if (desc.label != nullptr)
        {
            CopyLabel(contribution.label, desc.label);
            contribution.desc.label = contribution.label;
        }
        if (fallbackPlacement)
        {
            if (m_fallbackPlacementCount != ~u32{0})
                ++m_fallbackPlacementCount;
            m_lastFallbackContribution = desc.id;
        }
        output = contribution.token;
        return true;
    }

    bool EditorSurfaceRegistryCore::UnregisterContribution(const EditorExtensionOwner& owner, const EditorUiContributionToken token,
                                                           EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!CanMutateTopology(failure))
            return false;
        ContributionRecord* const contribution = FindContribution(token);
        if (contribution == nullptr)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor surface contribution token is invalid");
        if (!owner.IsValid() || !(contribution->owner == owner))
            return Fail(failure, EditorUiFailureCode::OwnerMismatch, "editor surface contribution owner does not match the registration",
                        contribution->desc.surface, contribution->desc.id);
        for (const ContributionRecord& child : m_contributions)
            if (child.active && child.desc.parent == contribution->desc.id)
                return Fail(failure, EditorUiFailureCode::InUse, "editor surface contribution still owns child contributions",
                            contribution->desc.surface, contribution->desc.id);
        for (const ContributionRecord& sibling : m_contributions)
            if (sibling.active && sibling.desc.sibling == contribution->desc.id)
                return Fail(failure, EditorUiFailureCode::InUse, "editor surface contribution remains a live sibling anchor",
                            contribution->desc.surface, contribution->desc.id);
        *contribution = {};
        return true;
    }

    void EditorSurfaceRegistryCore::RevokeOwner(const EditorExtensionOwner& owner) noexcept
    {
        if (!owner.IsValid())
            return;

        for (SurfaceRecord& surface : m_surfaces)
            if (surface.active && surface.owner == owner)
                surface.reachable = false;

        for (ContributionRecord& contribution : m_contributions)
            if (contribution.active && contribution.owner == owner)
                contribution.reachable = false;

        CancelContextMenuRequestsForOwner(owner);
    }

    bool EditorSurfaceRegistryCore::ResolveSurface(const EditorUiSurfaceId id, EditorUiSurfaceToken& token, EditorExtensionOwner& owner,
                                                   EditorUiSurfaceKind& kind) const noexcept
    {
        token = {};
        owner = {};
        kind = EditorUiSurfaceKind::ContextMenu;
        const SurfaceRecord* const surface = FindSurface(id);
        if (surface == nullptr)
            return false;
        token = surface->token;
        owner = surface->owner;
        kind = surface->desc.kind;
        return true;
    }

    void EditorSurfaceRegistryCore::VisitSurfaces(const EditorUiSurfaceVisitor visitor, void* const userData) const noexcept
    {
        if (visitor == nullptr)
            return;
        bool visited[MaximumEditorUiSurfaces]{};
        for (u32 emitted = 0; emitted < MaximumEditorUiSurfaces; ++emitted)
        {
            const SurfaceRecord* selected = nullptr;
            u32 selectedIndex = 0;
            for (u32 index = 0; index < MaximumEditorUiSurfaces; ++index)
            {
                const SurfaceRecord& candidate = m_surfaces[index];
                if (visited[index] || !candidate.active || !candidate.reachable)
                    continue;
                if (selected == nullptr || candidate.desc.id.value < selected->desc.id.value)
                {
                    selected = &candidate;
                    selectedIndex = index;
                }
            }
            if (selected == nullptr)
                break;
            visited[selectedIndex] = true;
            visitor({selected->token, selected->desc.id, selected->desc.kind, selected->owner, selected->builtIn}, userData);
        }
    }

    void EditorSurfaceRegistryCore::VisitContributions(const EditorUiSurfaceId surface, const EditorUiContributionVisitor visitor,
                                                       void* const userData) const noexcept
    {
        if (visitor == nullptr || FindSurface(surface) == nullptr)
            return;
        bool emitted[MaximumEditorUiContributions]{};
        VisitChildren(surface, {}, visitor, userData, emitted);
    }

    bool EditorSurfaceRegistryCore::QueryContributionPresentation(const EditorUiContributionView& contribution,
                                                                  const EditorUiCommandBridge& bridge,
                                                                  EditorCommandPresentation& presentation) noexcept
    {
        presentation = {};
        presentation.state = EditorCommandState::Disabled();
        if ((contribution.kind != EditorUiContributionKind::Command && contribution.kind != EditorUiContributionKind::Tool) ||
            !contribution.command.IsValid() || !bridge.IsValid())
        {
            RecordCommandFailure(contribution, EditorCommandFailureCode::InvalidDescriptor);
            return false;
        }

        EditorCommandFailure commandFailure;
        if (!bridge.queryPresentation(contribution.command, presentation, &commandFailure, bridge.userData))
        {
            presentation = {};
            presentation.state = EditorCommandState::Disabled();
            RecordCommandFailure(contribution,
                                 commandFailure.code != EditorCommandFailureCode::None
                                     ? commandFailure.code
                                     : EditorCommandFailureCode::HandlerQueryFailure);
            return false;
        }
        return true;
    }

    bool EditorSurfaceRegistryCore::InvokeContributionCommand(const EditorUiContributionView& contribution,
                                                              const EditorUiCommandBridge& bridge) noexcept
    {
        if ((contribution.kind != EditorUiContributionKind::Command && contribution.kind != EditorUiContributionKind::Tool) ||
            !contribution.command.IsValid() || !bridge.IsValid())
        {
            RecordCommandFailure(contribution, EditorCommandFailureCode::InvalidDescriptor);
            return false;
        }

        EditorCommandFailure commandFailure;
        if (!bridge.invoke(contribution.command, &commandFailure, bridge.userData))
        {
            RecordCommandFailure(contribution,
                                 commandFailure.code != EditorCommandFailureCode::None
                                     ? commandFailure.code
                                     : EditorCommandFailureCode::HandlerInvocationFailure);
            return false;
        }
        return true;
    }

    void EditorSurfaceRegistryCore::BuildContextMenuItems(const EditorUiSurfaceId surfaceId,
                                                          const EditorUiCommandBridge* const bridge,
                                                          EditorUiContextMenuItems& items) noexcept
    {
        items.Clear();
        const SurfaceRecord* const surface = FindSurface(surfaceId);
        if (surface == nullptr || surface->desc.kind != EditorUiSurfaceKind::ContextMenu)
            return;

        struct Collector final
        {
            EditorUiContextMenuItems* items = nullptr;
        } collector{&items};
        VisitContributions(
            surfaceId,
            [](const EditorUiContributionView& contribution, void* const data) noexcept
            {
                auto& output = *static_cast<Collector*>(data)->items;
                if (output.Size() < MaximumEditorUiContributions)
                    output.PushBack({contribution, {}, false});
            },
            &collector);

        for (EditorUiContextMenuItemView& item : items)
        {
            if (item.contribution.kind != EditorUiContributionKind::Command)
                continue;
            item.presentation = {};
            item.presentation.state = EditorCommandState::Disabled();
            if (bridge == nullptr)
                continue;
            item.hasPresentation = QueryContributionPresentation(item.contribution, *bridge, item.presentation);
        }

        bool keep[MaximumEditorUiContributions]{};
        MarkNormalizedContextMenuChildren(items, {}, keep);
        const u32 sourceSize = items.Size();
        u32 outputSize = 0;
        for (u32 index = 0; index < sourceSize; ++index)
        {
            if (!keep[index])
                continue;
            if (outputSize != index)
                items[outputSize] = items[index];
            ++outputSize;
        }
        items.Resize(outputSize);
    }

    bool EditorSurfaceRegistryCore::ContextMenuParentHasVisibleContent(
        const EditorUiContextMenuItems& items,
        const EditorUiContributionId parent) const noexcept
    {
        for (const EditorUiContextMenuItemView& item : items)
        {
            if (item.contribution.parent != parent)
                continue;
            if (item.contribution.kind == EditorUiContributionKind::Command)
            {
                if (!item.hasPresentation || item.presentation.state.kind != EditorCommandStateKind::Unsupported)
                    return true;
                continue;
            }
            if (item.contribution.kind == EditorUiContributionKind::Submenu &&
                ContextMenuParentHasVisibleContent(items, item.contribution.id))
                return true;
        }
        return false;
    }

    void EditorSurfaceRegistryCore::MarkNormalizedContextMenuChildren(
        const EditorUiContextMenuItems& items, const EditorUiContributionId parent,
        bool (&keep)[MaximumEditorUiContributions]) const noexcept
    {
        u32 pendingSeparator = MaximumEditorUiContributions;
        bool emittedContent = false;
        for (u32 index = 0; index < items.Size(); ++index)
        {
            const EditorUiContextMenuItemView& item = items[index];
            if (item.contribution.parent != parent)
                continue;
            if (item.contribution.kind == EditorUiContributionKind::Separator)
            {
                if (emittedContent && pendingSeparator == MaximumEditorUiContributions)
                    pendingSeparator = index;
                continue;
            }

            bool visible = true;
            if (item.contribution.kind == EditorUiContributionKind::Command)
                visible = !item.hasPresentation || item.presentation.state.kind != EditorCommandStateKind::Unsupported;
            else if (item.contribution.kind == EditorUiContributionKind::Submenu)
                visible = ContextMenuParentHasVisibleContent(items, item.contribution.id);
            if (!visible)
                continue;

            if (pendingSeparator != MaximumEditorUiContributions)
            {
                keep[pendingSeparator] = true;
                pendingSeparator = MaximumEditorUiContributions;
            }
            keep[index] = true;
            emittedContent = true;
            if (item.contribution.kind == EditorUiContributionKind::Submenu)
                MarkNormalizedContextMenuChildren(items, item.contribution.id, keep);
        }
    }

    bool EditorSurfaceRegistryCore::QueueContextMenuRequest(const EditorUiStampedContextMenuRequest& request,
                                                            EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread, "editor context-menu request requires the main thread",
                        request.surfaceId);
        if (m_uiState == nullptr || *m_uiState != EditorUiState::BuildingFrame)
            return Fail(failure, EditorUiFailureCode::WrongPhase, "editor context-menu request requires an active UI frame",
                        request.surfaceId);
        if (!request.panel.IsValid() || !request.host.IsValid() || !request.owner.IsValid() || !request.surfaceId.IsValid() ||
            !request.surface.IsValid() || request.frame == 0 || !std::isfinite(request.anchor.x) || !std::isfinite(request.anchor.y))
            return Fail(failure, EditorUiFailureCode::InvalidDescriptor, "editor context-menu request stamp is invalid",
                        request.surfaceId);

        const SurfaceRecord* const surface = FindSurface(request.surface);
        if (surface == nullptr || !surface->reachable || surface->desc.kind != EditorUiSurfaceKind::ContextMenu)
            return Fail(failure, EditorUiFailureCode::InvalidHandle, "editor context-menu request surface token is invalid",
                        request.surfaceId);
        if (surface->desc.id != request.surfaceId)
            return Fail(failure, EditorUiFailureCode::InvalidHandle,
                        "editor context-menu request surface identity does not match its generation token", request.surfaceId);
        if (!(surface->owner == request.owner))
            return Fail(failure, EditorUiFailureCode::OwnerMismatch, "editor context-menu request owner does not match the surface",
                        surface->desc.id);

        for (const EditorUiStampedContextMenuRequest& pending : m_pendingRequests)
            if (pending.panel == request.panel && pending.frame == request.frame)
            {
                if (pending.surfaceId == request.surfaceId && pending.surface == request.surface && pending.host == request.host &&
                    pending.owner == request.owner && pending.anchor == request.anchor)
                    return true;
                return Fail(failure, EditorUiFailureCode::SurfaceRequestConflict,
                            "editor panel already has a different context-menu request in this frame", surface->desc.id);
            }

        if (m_pendingRequests.Size() >= MaximumPendingEditorContextMenuRequests)
            return Fail(failure, EditorUiFailureCode::CapacityExceeded, "editor context-menu request capacity is exhausted", surface->desc.id);
        m_pendingRequests.PushBack(request);
        return true;
    }

    void EditorSurfaceRegistryCore::CommitContextMenuRequests(const EditorPanelFocusSnapshot& focus) noexcept
    {
        if (m_pendingRequests.Size() == 0)
            return;
        for (const EditorUiStampedContextMenuRequest& pending : m_pendingRequests)
        {
            if (!focus.HasFocus() || pending.frame != focus.frame || pending.panel != focus.panel || pending.host != focus.host)
                continue;
            const SurfaceRecord* const surface = FindSurface(pending.surface);
            if (surface == nullptr || !surface->reachable || surface->desc.kind != EditorUiSurfaceKind::ContextMenu ||
                surface->desc.id != pending.surfaceId || !(surface->owner == pending.owner))
                continue;
            m_committedRequest = {pending.panel, pending.host, pending.owner, pending.surfaceId, pending.surface,
                                  pending.frame, pending.anchor};
            m_hasCommittedRequest = true;
            break;
        }
        m_pendingRequests.Clear();
    }

    void EditorSurfaceRegistryCore::DiscardPendingContextMenuRequests() noexcept
    {
        m_pendingRequests.Clear();
    }

    void EditorSurfaceRegistryCore::CancelContextMenuRequestsForPanel(const EditorPanelHandle panel) noexcept
    {
        for (u32 index = m_pendingRequests.Size(); index > 0; --index)
            if (m_pendingRequests[index - 1u].panel == panel)
                RemovePendingRequestAt(index - 1u);
        if (m_hasCommittedRequest && m_committedRequest.panel == panel)
            ClearCommittedContextMenuRequest();
    }

    void EditorSurfaceRegistryCore::CancelContextMenuRequestsForHost(const EditorHostWindowHandle host) noexcept
    {
        for (u32 index = m_pendingRequests.Size(); index > 0; --index)
            if (m_pendingRequests[index - 1u].host == host)
                RemovePendingRequestAt(index - 1u);
        if (m_hasCommittedRequest && m_committedRequest.host == host)
            ClearCommittedContextMenuRequest();
    }

    void EditorSurfaceRegistryCore::CancelContextMenuRequestsForSurface(const EditorUiSurfaceToken surface) noexcept
    {
        for (u32 index = m_pendingRequests.Size(); index > 0; --index)
            if (m_pendingRequests[index - 1u].surface == surface)
                RemovePendingRequestAt(index - 1u);
        if (m_hasCommittedRequest && m_committedRequest.surface == surface)
            ClearCommittedContextMenuRequest();
    }

    void EditorSurfaceRegistryCore::CancelContextMenuRequestsForOwner(const EditorExtensionOwner& owner) noexcept
    {
        for (u32 index = m_pendingRequests.Size(); index > 0; --index)
            if (m_pendingRequests[index - 1u].owner == owner)
                RemovePendingRequestAt(index - 1u);
        if (m_hasCommittedRequest && m_committedRequest.owner == owner)
            ClearCommittedContextMenuRequest();
    }

    bool EditorSurfaceRegistryCore::PeekCommittedContextMenuRequest(EditorUiCommittedContextMenuRequest& request) const noexcept
    {
        request = {};
        if (!m_hasCommittedRequest)
            return false;
        const SurfaceRecord* const surface = FindSurface(m_committedRequest.surface);
        if (surface == nullptr || !surface->reachable || surface->desc.kind != EditorUiSurfaceKind::ContextMenu ||
            surface->desc.id != m_committedRequest.surfaceId || !(surface->owner == m_committedRequest.owner))
            return false;
        request = m_committedRequest;
        return true;
    }

    void EditorSurfaceRegistryCore::ClearCommittedContextMenuRequest() noexcept
    {
        m_committedRequest = {};
        m_hasCommittedRequest = false;
    }

    EditorSurfaceRegistryStatus EditorSurfaceRegistryCore::GetStatus() const noexcept
    {
        EditorSurfaceRegistryStatus status;
        for (const SurfaceRecord& surface : m_surfaces)
            status.surfaceCount += surface.active && surface.reachable ? 1u : 0u;
        for (const ContributionRecord& contribution : m_contributions)
            status.contributionCount += contribution.active && contribution.reachable ? 1u : 0u;
        status.pendingRequestCount = m_pendingRequests.Size();
        status.fallbackPlacementCount = m_fallbackPlacementCount;
        status.lastFallbackContribution = m_lastFallbackContribution;
        status.hasCommittedRequest = m_hasCommittedRequest;
        status.commandFailureCount = m_commandFailureCount;
        status.repeatedCommandFailureCount = m_repeatedCommandFailureCount;
        status.lastCommandFailureContribution = m_lastCommandFailureContribution;
        status.lastCommandFailureCommand = m_lastCommandFailureCommand;
        status.lastCommandFailureCode = m_lastCommandFailureCode;
        return status;
    }

    bool EditorSurfaceRegistryCore::CanMutateTopology(EditorUiFailure* const failure, const EditorUiSurfaceId surface,
                                                      const EditorUiContributionId contribution) const noexcept
    {
        if (!concurrency::IsMainThread())
            return Fail(failure, EditorUiFailureCode::WrongThread, "editor surface topology mutation requires the main thread",
                        surface, contribution);
        if (m_uiState != nullptr && *m_uiState == EditorUiState::Ready)
            return true;
        return Fail(failure,
                    m_uiState != nullptr && *m_uiState == EditorUiState::BuildingFrame ? EditorUiFailureCode::WrongPhase
                                                                                     : EditorUiFailureCode::NotReady,
                    "editor surface topology mutation requires a ready UI service", surface, contribution);
    }

    u32 EditorSurfaceRegistryCore::ReserveSurfaceGeneration() noexcept
    {
        return m_generations.IsValid() ? ReserveGeneration(*m_generations.surfaceCounter) : 0u;
    }

    u32 EditorSurfaceRegistryCore::ReserveContributionGeneration() noexcept
    {
        return m_generations.IsValid() ? ReserveGeneration(*m_generations.contributionCounter) : 0u;
    }

    u32 EditorSurfaceRegistryCore::FindSurfaceSlot() const noexcept
    {
        for (u32 index = 0; index < MaximumEditorUiSurfaces; ++index)
            if (!m_surfaces[index].active)
                return index;
        return ~u32{0};
    }

    u32 EditorSurfaceRegistryCore::FindContributionSlot() const noexcept
    {
        for (u32 index = 0; index < MaximumEditorUiContributions; ++index)
            if (!m_contributions[index].active)
                return index;
        return ~u32{0};
    }

    EditorSurfaceRegistryCore::SurfaceRecord* EditorSurfaceRegistryCore::FindSurface(const EditorUiSurfaceToken token) noexcept
    {
        return const_cast<SurfaceRecord*>(static_cast<const EditorSurfaceRegistryCore*>(this)->FindSurface(token));
    }

    const EditorSurfaceRegistryCore::SurfaceRecord* EditorSurfaceRegistryCore::FindSurface(const EditorUiSurfaceToken token) const noexcept
    {
        if (!token.IsValid())
            return nullptr;
        const SurfaceRecord& surface = m_surfaces[token.index];
        return surface.active && surface.token == token ? &surface : nullptr;
    }

    EditorSurfaceRegistryCore::SurfaceRecord* EditorSurfaceRegistryCore::FindSurface(const EditorUiSurfaceId id) noexcept
    {
        return const_cast<SurfaceRecord*>(static_cast<const EditorSurfaceRegistryCore*>(this)->FindSurface(id));
    }

    const EditorSurfaceRegistryCore::SurfaceRecord* EditorSurfaceRegistryCore::FindSurface(const EditorUiSurfaceId id) const noexcept
    {
        if (!id.IsValid())
            return nullptr;
        for (const SurfaceRecord& surface : m_surfaces)
            if (surface.active && surface.reachable && surface.desc.id == id)
                return &surface;
        return nullptr;
    }

    EditorSurfaceRegistryCore::ContributionRecord* EditorSurfaceRegistryCore::FindContribution(const EditorUiContributionToken token) noexcept
    {
        return const_cast<ContributionRecord*>(static_cast<const EditorSurfaceRegistryCore*>(this)->FindContribution(token));
    }

    const EditorSurfaceRegistryCore::ContributionRecord* EditorSurfaceRegistryCore::FindContribution(const EditorUiContributionToken token) const noexcept
    {
        if (!token.IsValid())
            return nullptr;
        const ContributionRecord& contribution = m_contributions[token.index];
        return contribution.active && contribution.token == token ? &contribution : nullptr;
    }

    EditorSurfaceRegistryCore::ContributionRecord* EditorSurfaceRegistryCore::FindContribution(const EditorUiContributionId id) noexcept
    {
        return const_cast<ContributionRecord*>(static_cast<const EditorSurfaceRegistryCore*>(this)->FindContribution(id));
    }

    const EditorSurfaceRegistryCore::ContributionRecord* EditorSurfaceRegistryCore::FindContribution(const EditorUiContributionId id) const noexcept
    {
        if (!id.IsValid())
            return nullptr;
        for (const ContributionRecord& contribution : m_contributions)
            if (contribution.active && contribution.reachable && contribution.desc.id == id)
                return &contribution;
        return nullptr;
    }

    bool EditorSurfaceRegistryCore::IsPlacementOwnerAllowed(const EditorExtensionOwner& owner,
                                                            const ContributionRecord& anchor) const noexcept
    {
        return anchor.builtInAnchor || (anchor.owner.IsValid() && anchor.owner == owner);
    }

    const EditorSurfaceRegistryCore::ContributionRecord* EditorSurfaceRegistryCore::SelectNextInPlacementBucket(
        const EditorUiSurfaceId surface, const EditorUiContributionId parent, const EditorUiPlacementRelation placement,
        const EditorUiContributionId sibling, const bool (&emitted)[MaximumEditorUiContributions]) const noexcept
    {
        const ContributionRecord* selected = nullptr;
        for (u32 index = 0; index < MaximumEditorUiContributions; ++index)
        {
            const ContributionRecord& candidate = m_contributions[index];
            if (emitted[index] || !candidate.active || !candidate.reachable || candidate.desc.surface != surface ||
                candidate.desc.parent != parent || candidate.desc.placement != placement || candidate.desc.sibling != sibling)
                continue;
            if (selected == nullptr || candidate.desc.order < selected->desc.order ||
                (candidate.desc.order == selected->desc.order && candidate.desc.id.value < selected->desc.id.value))
                selected = &candidate;
        }
        return selected;
    }

    void EditorSurfaceRegistryCore::VisitPlacementBucket(const EditorUiSurfaceId surface, const EditorUiContributionId parent,
                                                         const EditorUiPlacementRelation placement,
                                                         const EditorUiContributionId sibling,
                                                         const EditorUiContributionVisitor visitor, void* const userData,
                                                         bool (&emitted)[MaximumEditorUiContributions]) const noexcept
    {
        for (;;)
        {
            const ContributionRecord* const selected =
                SelectNextInPlacementBucket(surface, parent, placement, sibling, emitted);
            if (selected == nullptr)
                return;
            VisitPlacedContribution(*selected, visitor, userData, emitted);
        }
    }

    void EditorSurfaceRegistryCore::VisitPlacedContribution(
        const ContributionRecord& contribution, const EditorUiContributionVisitor visitor, void* const userData,
        bool (&emitted)[MaximumEditorUiContributions]) const noexcept
    {
        const u32 index = contribution.token.index;
        if (emitted[index])
            return;

        VisitPlacementBucket(contribution.desc.surface, contribution.desc.parent, EditorUiPlacementRelation::Before,
                             contribution.desc.id, visitor, userData, emitted);
        emitted[index] = true;
        const EditorUiContributionView view{
            contribution.token, contribution.desc.id, contribution.desc.surface, contribution.desc.kind,
            contribution.desc.parent, contribution.desc.sibling, contribution.desc.placement, contribution.desc.order,
            contribution.desc.command, contribution.desc.icon, contribution.desc.label, contribution.owner, contribution.depth,
            contribution.builtInAnchor, contribution.fallbackPlacement};
        visitor(view, userData);
        if (contribution.desc.kind == EditorUiContributionKind::Submenu)
            VisitChildren(contribution.desc.surface, contribution.desc.id, visitor, userData, emitted);
        VisitPlacementBucket(contribution.desc.surface, contribution.desc.parent, EditorUiPlacementRelation::After,
                             contribution.desc.id, visitor, userData, emitted);
    }

    void EditorSurfaceRegistryCore::VisitChildren(const EditorUiSurfaceId surface, const EditorUiContributionId parent,
                                                  const EditorUiContributionVisitor visitor, void* const userData,
                                                  bool (&emitted)[MaximumEditorUiContributions]) const noexcept
    {
        VisitPlacementBucket(surface, parent, EditorUiPlacementRelation::Append, {}, visitor, userData, emitted);
    }

    void EditorSurfaceRegistryCore::RemovePendingRequestAt(const u32 index) noexcept
    {
        static_cast<void>(m_pendingRequests.RemoveAt(index));
    }

    void EditorSurfaceRegistryCore::RecordCommandFailure(const EditorUiContributionView& contribution,
                                                         const EditorCommandFailureCode code) noexcept
    {
        if (m_commandFailureCount != ~u32{0})
            ++m_commandFailureCount;
        if (m_lastCommandFailureContribution == contribution.id && m_lastCommandFailureCommand == contribution.command &&
            m_lastCommandFailureCode == code)
        {
            if (m_repeatedCommandFailureCount != ~u32{0})
                ++m_repeatedCommandFailureCount;
        }
        else
        {
            m_repeatedCommandFailureCount = 0;
        }
        m_lastCommandFailureContribution = contribution.id;
        m_lastCommandFailureCommand = contribution.command;
        m_lastCommandFailureCode = code;
    }
} // namespace vanguard::editor::detail
