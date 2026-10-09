#include <vanguard/editor/editor_surfaces.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    namespace editor = vanguard::editor;
    namespace detail = vanguard::editor::detail;
    namespace memory = vanguard::memory;
    using vanguard::i32;
    using vanguard::u32;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[editorSurfaceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    [[nodiscard]] editor::EditorExtensionOwner Owner(const u64 id, const u64 generation = 1) noexcept
    {
        return {{id}, generation};
    }

    struct LocalGenerationSource final
    {
        std::atomic<u32> surfaces;
        std::atomic<u32> contributions;

        explicit LocalGenerationSource(const u32 surfaceSeed = 0, const u32 contributionSeed = 0) noexcept
            : surfaces(surfaceSeed), contributions(contributionSeed)
        {
        }

        [[nodiscard]] detail::EditorSurfaceGenerationSource Get() noexcept
        {
            return {&surfaces, &contributions};
        }
    };

    [[nodiscard]] detail::EditorUiSurfaceDesc ContextSurface(const u64 id) noexcept
    {
        return {{id}, detail::EditorUiSurfaceKind::ContextMenu};
    }

    [[nodiscard]] detail::EditorUiContributionDesc Separator(const u64 id, const editor::EditorUiSurfaceId surface,
                                                             const editor::EditorUiContributionId parent = {},
                                                             const i32 order = 0) noexcept
    {
        detail::EditorUiContributionDesc desc;
        desc.id = {id};
        desc.surface = surface;
        desc.kind = detail::EditorUiContributionKind::Separator;
        desc.parent = parent;
        desc.order = order;
        return desc;
    }

    [[nodiscard]] detail::EditorUiContributionDesc Submenu(const u64 id, const editor::EditorUiSurfaceId surface,
                                                           const char* const label,
                                                           const editor::EditorUiContributionId parent = {},
                                                           const i32 order = 0) noexcept
    {
        detail::EditorUiContributionDesc desc;
        desc.id = {id};
        desc.surface = surface;
        desc.kind = detail::EditorUiContributionKind::Submenu;
        desc.parent = parent;
        desc.order = order;
        desc.label = label;
        return desc;
    }

    [[nodiscard]] detail::EditorUiContributionDesc Command(const u64 id, const editor::EditorUiSurfaceId surface,
                                                           const char* const label,
                                                           const editor::EditorUiContributionId parent = {},
                                                           const i32 order = 0) noexcept
    {
        detail::EditorUiContributionDesc desc;
        desc.id = {id};
        desc.surface = surface;
        desc.kind = detail::EditorUiContributionKind::Command;
        desc.parent = parent;
        desc.order = order;
        desc.command = {0x100000ull + id};
        desc.label = label;
        return desc;
    }

    struct SurfaceObservation final
    {
        u32 count = 0;
        editor::EditorUiSurfaceId ids[editor::MaximumEditorUiSurfaces]{};
        detail::EditorUiSurfaceKind kinds[editor::MaximumEditorUiSurfaces]{};
        bool builtIn[editor::MaximumEditorUiSurfaces]{};
    };

    void ObserveSurface(const detail::EditorUiSurfaceView& surface, void* const userData) noexcept
    {
        auto& observation = *static_cast<SurfaceObservation*>(userData);
        if (observation.count >= editor::MaximumEditorUiSurfaces)
            return;
        const u32 index = observation.count++;
        observation.ids[index] = surface.id;
        observation.kinds[index] = surface.kind;
        observation.builtIn[index] = surface.builtIn;
    }

    struct ContributionObservation final
    {
        u32 count = 0;
        editor::EditorUiContributionId ids[editor::MaximumEditorUiContributions]{};
        const char* labels[editor::MaximumEditorUiContributions]{};
        u32 depths[editor::MaximumEditorUiContributions]{};
        bool builtIn[editor::MaximumEditorUiContributions]{};
        bool fallback[editor::MaximumEditorUiContributions]{};
    };

    void ObserveContribution(const detail::EditorUiContributionView& contribution, void* const userData) noexcept
    {
        auto& observation = *static_cast<ContributionObservation*>(userData);
        if (observation.count >= editor::MaximumEditorUiContributions)
            return;
        const u32 index = observation.count++;
        observation.ids[index] = contribution.id;
        observation.labels[index] = contribution.label;
        observation.depths[index] = contribution.depth;
        observation.builtIn[index] = contribution.builtInAnchor;
        observation.fallback[index] = contribution.fallbackPlacement;
    }

    void TestBuiltInIdentityAndAnchors() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Offline;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;

        Check(registry.InitializeBuiltIns(&failure), "built-in surface roots initialize before UI Ready");
        const detail::EditorSurfaceRegistryStatus status = registry.GetStatus();
        Check(status.surfaceCount == 2 && status.contributionCount == 7,
              "built-in roots and structural anchors consume the fixed surface-domain capacities");

        detail::EditorUiSurfaceToken menuToken;
        detail::EditorUiSurfaceToken toolbarToken;
        editor::EditorExtensionOwner owner;
        detail::EditorUiSurfaceKind kind = detail::EditorUiSurfaceKind::ContextMenu;
        Check(registry.ResolveSurface(detail::EditorMainMenuSurfaceId, menuToken, owner, kind) && menuToken.IsValid() &&
                  !owner.IsValid() && kind == detail::EditorUiSurfaceKind::MainMenu,
              "main menu is a core-owned built-in surface");
        Check(registry.ResolveSurface(detail::EditorMainToolbarSurfaceId, toolbarToken, owner, kind) && toolbarToken.IsValid() &&
                  !owner.IsValid() && kind == detail::EditorUiSurfaceKind::MainToolbar,
              "main toolbar is a core-owned built-in surface");

        SurfaceObservation surfaces;
        registry.VisitSurfaces(ObserveSurface, &surfaces);
        Check(surfaces.count == 2 && surfaces.builtIn[0] && surfaces.builtIn[1],
              "surface visitation exposes exactly the two reachable built-in roots");

        ContributionObservation menu;
        registry.VisitContributions(detail::EditorMainMenuSurfaceId, ObserveContribution, &menu);
        Check(menu.count == 5 && menu.ids[0] == detail::EditorMainMenuFileAnchorId &&
                  menu.ids[1] == detail::EditorMainMenuEditAnchorId && menu.ids[2] == detail::EditorMainMenuViewAnchorId &&
                  menu.ids[3] == detail::EditorMainMenuToolsAnchorId && menu.ids[4] == detail::EditorMainMenuHelpAnchorId,
              "main-menu built-in anchors have deterministic File/Edit/View/Tools/Help order");
        Check(menu.labels[0] != nullptr && std::strcmp(menu.labels[0], "File") == 0 &&
                  menu.labels[4] != nullptr && std::strcmp(menu.labels[4], "Help") == 0,
              "built-in menu anchor labels are copied presentation metadata");

        ContributionObservation toolbar;
        registry.VisitContributions(detail::EditorMainToolbarSurfaceId, ObserveContribution, &toolbar);
        Check(toolbar.count == 2 && toolbar.ids[0] == detail::EditorMainToolbarStartAnchorId &&
                  toolbar.ids[1] == detail::EditorMainToolbarEndAnchorId && toolbar.labels[0] == nullptr &&
                  toolbar.labels[1] == nullptr,
              "toolbar start/end structural anchors are deterministic and label-free");
    }

    void TestIdentityStalenessAndCapacities() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        const editor::EditorExtensionOwner owner = Owner(100);
        Check(registry.InitializeBuiltIns(&failure), "capacity fixture initializes built-ins");

        detail::EditorUiSurfaceToken first;
        Check(registry.RegisterSurface(owner, ContextSurface(1000), first, &failure) && first.IsValid(),
              "context surface receives an exact generation token");
        detail::EditorUiSurfaceToken duplicate;
        Check(!registry.RegisterSurface(owner, ContextSurface(1000), duplicate, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity && !duplicate.IsValid(),
              "duplicate surface identity rejects without a partial token");
        Check(registry.UnregisterSurface(owner, first, &failure), "exact surface token unregisters");
        detail::EditorUiSurfaceToken second;
        Check(registry.RegisterSurface(owner, ContextSurface(1000), second, &failure) && second.index == first.index &&
                  second.generation != first.generation,
              "reused surface slot receives a fresh generation");
        Check(!registry.UnregisterSurface(owner, first, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "stale surface token cannot remove a reused slot");

        detail::EditorUiContributionToken firstContribution;
        Check(registry.RegisterContribution(owner, Separator(2000, {1000}), firstContribution, &failure),
              "contribution registers under the owned context surface");
        Check(registry.UnregisterContribution(owner, firstContribution, &failure), "exact contribution token unregisters");
        detail::EditorUiContributionToken secondContribution;
        Check(registry.RegisterContribution(owner, Separator(2000, {1000}), secondContribution, &failure) &&
                  secondContribution.index == firstContribution.index &&
                  secondContribution.generation != firstContribution.generation,
              "reused contribution slot receives a fresh generation");
        detail::EditorUiContributionToken duplicateContribution;
        Check(!registry.RegisterContribution(owner, Separator(2000, {1000}), duplicateContribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::DuplicateIdentity &&
                  !duplicateContribution.IsValid(),
              "duplicate contribution identity rejects without a partial token");
        Check(!registry.UnregisterContribution(owner, firstContribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "stale contribution token cannot remove a reused slot");

        editor::EditorUiState surfaceCapacityState = editor::EditorUiState::Ready;
        LocalGenerationSource surfaceCapacityGenerations;
        detail::EditorSurfaceRegistryCore surfaceCapacity(surfaceCapacityState, surfaceCapacityGenerations.Get());
        Check(surfaceCapacity.InitializeBuiltIns(&failure), "surface-capacity fixture initializes built-ins");
        detail::EditorUiSurfaceToken surfaces[editor::MaximumEditorUiSurfaces - 2u]{};
        for (u32 index = 0; index < editor::MaximumEditorUiSurfaces - 2u; ++index)
            Check(surfaceCapacity.RegisterSurface(owner, ContextSurface(3000u + index), surfaces[index], &failure),
                  "surface capacity admits every non-built-in slot");
        detail::EditorUiSurfaceToken excessSurface;
        Check(!surfaceCapacity.RegisterSurface(owner, ContextSurface(9000), excessSurface, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded && !excessSurface.IsValid(),
              "surface capacity rejects one registration beyond the fixed root bound");

        editor::EditorUiState contributionCapacityState = editor::EditorUiState::Ready;
        LocalGenerationSource contributionCapacityGenerations;
        detail::EditorSurfaceRegistryCore contributionCapacity(contributionCapacityState, contributionCapacityGenerations.Get());
        Check(contributionCapacity.InitializeBuiltIns(&failure), "contribution-capacity fixture initializes built-ins");
        detail::EditorUiSurfaceToken capacityRoot;
        Check(contributionCapacity.RegisterSurface(owner, ContextSurface(10000), capacityRoot, &failure),
              "contribution-capacity fixture registers its context root");
        detail::EditorUiContributionToken contributions[editor::MaximumEditorUiContributions - 7u]{};
        for (u32 index = 0; index < editor::MaximumEditorUiContributions - 7u; ++index)
            Check(contributionCapacity.RegisterContribution(owner, Separator(11000u + index, {10000}), contributions[index], &failure),
                  "contribution capacity admits every slot not occupied by built-in anchors");
        detail::EditorUiContributionToken excessContribution;
        Check(!contributionCapacity.RegisterContribution(owner, Separator(20000, {10000}), excessContribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded && !excessContribution.IsValid(),
              "contribution capacity rejects one registration beyond the fixed contribution bound");
    }

    void TestGenerationExhaustionAndFreshCoreStaleness() noexcept
    {
        editor::EditorUiFailure failure;

        editor::EditorUiState surfaceFailureState = editor::EditorUiState::Offline;
        LocalGenerationSource surfaceFailureGenerations(~u32{0} - 1u, 0);
        detail::EditorSurfaceRegistryCore surfaceFailure(surfaceFailureState, surfaceFailureGenerations.Get());
        Check(!surfaceFailure.InitializeBuiltIns(&failure) &&
                  failure.code == editor::EditorUiFailureCode::GenerationExhausted &&
                  surfaceFailure.GetStatus().surfaceCount == 0 && surfaceFailure.GetStatus().contributionCount == 0,
              "built-in surface generation exhaustion leaves no partially reachable root or anchor");

        editor::EditorUiState contributionFailureState = editor::EditorUiState::Offline;
        LocalGenerationSource contributionFailureGenerations(0, ~u32{0} - 6u);
        detail::EditorSurfaceRegistryCore contributionFailure(contributionFailureState, contributionFailureGenerations.Get());
        Check(!contributionFailure.InitializeBuiltIns(&failure) &&
                  failure.code == editor::EditorUiFailureCode::GenerationExhausted &&
                  contributionFailure.GetStatus().surfaceCount == 0 &&
                  contributionFailure.GetStatus().contributionCount == 0,
              "built-in contribution generation exhaustion leaves no partially reachable state");

        editor::EditorUiState registrationFailureState = editor::EditorUiState::Ready;
        LocalGenerationSource registrationFailureGenerations(~u32{0} - 2u, ~u32{0} - 7u);
        detail::EditorSurfaceRegistryCore registrationFailure(registrationFailureState, registrationFailureGenerations.Get());
        Check(registrationFailure.InitializeBuiltIns(&failure), "near-exhausted generation space can initialize exact built-ins");
        detail::EditorUiSurfaceToken exhaustedSurface;
        Check(!registrationFailure.RegisterSurface(Owner(200), ContextSurface(21000), exhaustedSurface, &failure) &&
                  failure.code == editor::EditorUiFailureCode::GenerationExhausted && !exhaustedSurface.IsValid() &&
                  registrationFailure.GetStatus().surfaceCount == 2,
              "owned surface generation exhaustion rejects before topology mutation");
        detail::EditorUiContributionToken exhaustedContribution;
        Check(!registrationFailure.RegisterContribution(Owner(200), Separator(22000, detail::EditorMainMenuSurfaceId),
                                                        exhaustedContribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::GenerationExhausted &&
                  !exhaustedContribution.IsValid() && registrationFailure.GetStatus().contributionCount == 7,
              "contribution generation exhaustion rejects before topology mutation");

        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource sharedGenerations;
        detail::EditorUiSurfaceToken oldToken;
        detail::EditorUiContributionToken oldContribution;
        {
            detail::EditorSurfaceRegistryCore first(state, sharedGenerations.Get());
            Check(first.InitializeBuiltIns(&failure) &&
                      first.RegisterSurface(Owner(201), ContextSurface(23000), oldToken, &failure) &&
                      first.RegisterContribution(Owner(201), Separator(23001, {23000}), oldContribution, &failure),
                  "first core registers surface and contribution tokens using the shared process-style generation source");
        }
        detail::EditorSurfaceRegistryCore second(state, sharedGenerations.Get());
        Check(second.InitializeBuiltIns(&failure), "fresh core reinitializes built-ins from the same generation source");
        detail::EditorUiSurfaceToken newToken;
        Check(second.RegisterSurface(Owner(201), ContextSurface(23000), newToken, &failure) &&
                  newToken.index == oldToken.index && newToken.generation != oldToken.generation,
              "fresh core cannot alias an old token even when the same slot and stable ID are reused");
        detail::EditorUiContributionToken newContribution;
        Check(second.RegisterContribution(Owner(201), Separator(23001, {23000}), newContribution, &failure) &&
                  newContribution.index == oldContribution.index &&
                  newContribution.generation != oldContribution.generation,
              "fresh core cannot alias an old contribution token after the same slot and stable ID are reused");
        Check(!second.UnregisterSurface(Owner(201), oldToken, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "token from an earlier core instance remains stale");
        Check(!second.UnregisterContribution(Owner(201), oldContribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidHandle,
              "contribution token from an earlier core instance remains stale");
    }

    void TestDescriptorLabelsKindsAndOwnership() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        const editor::EditorExtensionOwner owner = Owner(300);
        const editor::EditorExtensionOwner otherOwner = Owner(301);
        Check(registry.InitializeBuiltIns(&failure), "descriptor fixture initializes built-ins");

        detail::EditorUiSurfaceToken context;
        Check(registry.RegisterSurface(owner, ContextSurface(30000), context, &failure),
              "descriptor fixture registers an owned context root");

        char copiedLabel[] = "Original label";
        detail::EditorUiContributionToken copied;
        Check(registry.RegisterContribution(owner, Command(30100, {30000}, copiedLabel), copied, &failure),
              "command contribution accepts command identity and bounded label");
        copiedLabel[0] = 'X';
        ContributionObservation copiedObservation;
        registry.VisitContributions({30000}, ObserveContribution, &copiedObservation);
        Check(copiedObservation.count == 1 && copiedObservation.labels[0] != nullptr &&
                  std::strcmp(copiedObservation.labels[0], "Original label") == 0,
              "contribution label is copied rather than borrowed from caller storage");

        char validMaximum[editor::MaximumEditorUiSurfaceStringBytes]{};
        for (u32 index = 0; index + 1u < editor::MaximumEditorUiSurfaceStringBytes; ++index)
            validMaximum[index] = 'a';
        detail::EditorUiContributionToken maxLabel;
        Check(registry.RegisterContribution(owner, Submenu(30101, {30000}, validMaximum), maxLabel, &failure),
              "label whose terminator fits inside the fixed byte bound is accepted");

        char overlong[editor::MaximumEditorUiSurfaceStringBytes + 1u]{};
        for (u32 index = 0; index < editor::MaximumEditorUiSurfaceStringBytes; ++index)
            overlong[index] = 'b';
        detail::EditorUiContributionToken rejected;
        Check(!registry.RegisterContribution(owner, Submenu(30102, {30000}, overlong), rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor && !rejected.IsValid(),
              "overlong label is rejected rather than silently truncated");
        Check(!registry.RegisterContribution(owner, Command(30103, {30000}, ""), rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
              "empty command label is rejected");

        auto invalidSeparator = Separator(30104, {30000});
        invalidSeparator.label = "bad";
        Check(!registry.RegisterContribution(owner, invalidSeparator, rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
              "separator cannot carry presentation text");
        auto invalidSubmenu = Submenu(30105, {30000}, "bad");
        invalidSubmenu.command = {77};
        Check(!registry.RegisterContribution(owner, invalidSubmenu, rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
              "submenu cannot carry command identity");
        Check(!registry.RegisterContribution(owner, Submenu(30106, detail::EditorMainToolbarSurfaceId, "bad"), rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor,
              "main toolbar rejects submenu contributions");

        detail::EditorUiContributionToken privateMenu;
        Check(registry.RegisterContribution(owner,
                                            Submenu(30200, detail::EditorMainMenuSurfaceId, "Owned",
                                                    detail::EditorMainMenuFileAnchorId),
                                            privateMenu, &failure),
              "owner may nest a private submenu below a shell-owned anchor");
        detail::EditorUiContributionToken nested;
        Check(registry.RegisterContribution(owner,
                                            Command(30201, detail::EditorMainMenuSurfaceId, "Nested", {30200}),
                                            nested, &failure),
              "same owner may target its private submenu anchor");
        Check(!registry.RegisterContribution(otherOwner,
                                             Command(30202, detail::EditorMainMenuSurfaceId, "Cross owner", {30200}),
                                             rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::OwnerMismatch,
              "another extension cannot target a private parent anchor");

        auto beforeShellAnchor = Command(30203, detail::EditorMainMenuSurfaceId, "Before File");
        beforeShellAnchor.placement = detail::EditorUiPlacementRelation::Before;
        beforeShellAnchor.sibling = detail::EditorMainMenuFileAnchorId;
        Check(registry.RegisterContribution(otherOwner, beforeShellAnchor, rejected, &failure),
              "any extension may target a shell-owned sibling anchor");

        Check(!registry.RegisterContribution(otherOwner, Separator(30204, {30000}), rejected, &failure) &&
                  failure.code == editor::EditorUiFailureCode::OwnerMismatch,
              "owned context-menu root rejects contributions from another extension");
    }

    void TestHierarchyPlacementAndDeterministicOrdering() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        const editor::EditorExtensionOwner owner = Owner(400);
        detail::EditorUiSurfaceToken context;
        Check(registry.InitializeBuiltIns(&failure) &&
                  registry.RegisterSurface(owner, ContextSurface(40000), context, &failure),
              "placement fixture initializes its context root");

        detail::EditorUiContributionToken token;
        Check(registry.RegisterContribution(owner, Separator(40030, {40000}, {}, 0), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(40020, {40000}, {}, 0), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(40040, {40000}, {}, -1), token, &failure),
              "append placement admits entries independent of registration order");
        ContributionObservation ordered;
        registry.VisitContributions({40000}, ObserveContribution, &ordered);
        Check(ordered.count == 3 && ordered.ids[0].value == 40040 && ordered.ids[1].value == 40020 &&
                  ordered.ids[2].value == 40030,
              "append bucket sorts by explicit order then stable contribution identity");

        detail::EditorUiSurfaceToken relationSurface;
        Check(registry.RegisterSurface(owner, ContextSurface(40100), relationSurface, &failure),
              "relation fixture registers a second isolated context root");
        Check(registry.RegisterContribution(owner, Separator(40110, {40100}), token, &failure),
              "relation fixture registers its stable anchor");
        Check(registry.RegisterContribution(owner, Separator(40113, {40100}, {}, 50), token, &failure),
              "relation fixture registers an unrelated append-bucket sibling");
        auto before = Separator(40111, {40100}, {}, 1000);
        before.placement = detail::EditorUiPlacementRelation::Before;
        before.sibling = {40110};
        auto after = Separator(40112, {40100}, {}, -1000);
        after.placement = detail::EditorUiPlacementRelation::After;
        after.sibling = {40110};
        Check(registry.RegisterContribution(owner, before, token, &failure) &&
                  registry.RegisterContribution(owner, after, token, &failure),
              "before/after placement resolves against an existing sibling anchor");
        ContributionObservation relationOrder;
        registry.VisitContributions({40100}, ObserveContribution, &relationOrder);
        Check(relationOrder.count == 4 && relationOrder.ids[0].value == 40111 &&
                  relationOrder.ids[1].value == 40110 && relationOrder.ids[2].value == 40112 &&
                  relationOrder.ids[3].value == 40113,
              "before/after buckets remain attached to their anchor while append siblings use their own order bucket");

        auto requiredMissing = Separator(40200, {40100});
        requiredMissing.placement = detail::EditorUiPlacementRelation::Before;
        requiredMissing.sibling = {999999};
        requiredMissing.missingAnchor = detail::EditorUiMissingAnchorPolicy::Required;
        const u32 beforeRequiredCount = registry.GetStatus().contributionCount;
        Check(!registry.RegisterContribution(owner, requiredMissing, token, &failure) &&
                  failure.code == editor::EditorUiFailureCode::MissingAnchor &&
                  registry.GetStatus().contributionCount == beforeRequiredCount,
              "required missing sibling rejects transactionally");

        auto fallbackMissing = requiredMissing;
        fallbackMissing.id = {40201};
        fallbackMissing.missingAnchor = detail::EditorUiMissingAnchorPolicy::FallbackAppend;
        Check(registry.RegisterContribution(owner, fallbackMissing, token, &failure) &&
                  registry.GetStatus().fallbackPlacementCount == 1 &&
                  registry.GetStatus().lastFallbackContribution == fallbackMissing.id,
              "fallback missing sibling resolves to append and records bounded placement status");
        ContributionObservation fallbackObservation;
        registry.VisitContributions({40100}, ObserveContribution, &fallbackObservation);
        bool foundFallback = false;
        for (u32 index = 0; index < fallbackObservation.count; ++index)
            if (fallbackObservation.ids[index] == fallbackMissing.id)
                foundFallback = fallbackObservation.fallback[index];
        Check(foundFallback, "resolved fallback placement is observable without changing stable contribution identity");

        auto selfParent = Submenu(40300, {40100}, "self", {40300});
        Check(!registry.RegisterContribution(owner, selfParent, token, &failure) &&
                  failure.code == editor::EditorUiFailureCode::HierarchyViolation,
              "self-parent placement is rejected explicitly");
        auto selfSibling = Separator(40301, {40100});
        selfSibling.placement = detail::EditorUiPlacementRelation::After;
        selfSibling.sibling = selfSibling.id;
        Check(!registry.RegisterContribution(owner, selfSibling, token, &failure) &&
                  failure.code == editor::EditorUiFailureCode::HierarchyViolation,
              "self-sibling placement is rejected explicitly");
        Check(!registry.RegisterContribution(owner, Command(40302, {40100}, "forward", {49999}), token, &failure) &&
                  failure.code == editor::EditorUiFailureCode::MissingAnchor,
              "forward parent reference is rejected rather than creating partial hierarchy");

        editor::EditorUiContributionId parent;
        for (u32 depth = 1; depth <= editor::MaximumEditorUiSurfaceDepth; ++depth)
        {
            const u64 id = 40400u + depth;
            Check(registry.RegisterContribution(owner, Submenu(id, {40100}, "level", parent), token, &failure),
                  "hierarchy admits every level through the fixed maximum depth");
            parent = {id};
        }
        Check(registry.RegisterContribution(owner, Command(40450, {40100}, "leaf at max depth", parent), token, &failure),
              "leaf contribution is allowed beneath the eighth submenu because it does not add submenu nesting");
        ContributionObservation depthObservation;
        registry.VisitContributions({40100}, ObserveContribution, &depthObservation);
        bool leafHasMaximumSubmenuDepth = false;
        for (u32 index = 0; index < depthObservation.count; ++index)
            if (depthObservation.ids[index].value == 40450)
                leafHasMaximumSubmenuDepth = depthObservation.depths[index] == editor::MaximumEditorUiSurfaceDepth;
        Check(leafHasMaximumSubmenuDepth,
              "leaf view reports the number of containing submenu levels without exceeding the submenu bound");
        Check(!registry.RegisterContribution(owner, Submenu(40500, {40100}, "too deep", parent), token, &failure) &&
                  failure.code == editor::EditorUiFailureCode::HierarchyViolation,
              "hierarchy rejects a submenu beyond the fixed maximum depth");
    }

    void TestPendingRequestTransactionState() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        const editor::EditorExtensionOwner owner = Owner(500);
        detail::EditorUiSurfaceToken surface;
        Check(registry.InitializeBuiltIns(&failure) &&
                  registry.RegisterSurface(owner, ContextSurface(50000), surface, &failure),
              "request fixture initializes an owned context surface");

        const editor::EditorPanelHandle panelA{1, 1};
        const editor::EditorPanelHandle panelB{2, 1};
        const editor::EditorHostWindowHandle host{1, 1};
        detail::EditorUiStampedContextMenuRequest request{
            panelA, host, owner, {50000}, surface, 42, {10.0f, 20.0f}};

        Check(!registry.QueueContextMenuRequest(request, &failure) &&
                  failure.code == editor::EditorUiFailureCode::WrongPhase && failure.surface.value == 50000,
              "stamped request is rejected outside BuildingFrame");
        state = editor::EditorUiState::BuildingFrame;
        Check(registry.QueueContextMenuRequest(request, &failure) &&
                  registry.QueueContextMenuRequest(request, &failure) &&
                  registry.GetStatus().pendingRequestCount == 1,
              "exact duplicate request from one panel/frame coalesces");

        auto conflict = request;
        conflict.anchor.x += 1.0f;
        Check(!registry.QueueContextMenuRequest(conflict, &failure) &&
                  failure.code == editor::EditorUiFailureCode::SurfaceRequestConflict &&
                  registry.GetStatus().pendingRequestCount == 1,
              "differing second request from the same panel/frame fails without replacing the first");

        auto nonFinite = request;
        nonFinite.panel = panelB;
        nonFinite.anchor.x = std::numeric_limits<float>::infinity();
        Check(!registry.QueueContextMenuRequest(nonFinite, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidDescriptor &&
                  failure.surface.value == 50000 && registry.GetStatus().pendingRequestCount == 1,
              "non-finite stamped anchor rejects without pending-state mutation");

        auto crossOwner = request;
        crossOwner.panel = panelB;
        crossOwner.owner = Owner(501);
        Check(!registry.QueueContextMenuRequest(crossOwner, &failure) &&
                  failure.code == editor::EditorUiFailureCode::OwnerMismatch &&
                  registry.GetStatus().pendingRequestCount == 1,
              "context-menu request rejects a panel owner stamp that differs from the live context surface owner");

        auto requestB = request;
        requestB.panel = panelB;
        requestB.anchor = {30.0f, 40.0f};
        Check(registry.QueueContextMenuRequest(requestB, &failure) &&
                  registry.GetStatus().pendingRequestCount == 2,
              "different panels may occupy independent bounded pending-request entries");

        editor::EditorPanelFocusSnapshot focus;
        focus.frame = 42;
        focus.panel = panelB;
        focus.host = host;
        registry.CommitContextMenuRequests(focus);
        detail::EditorUiCommittedContextMenuRequest committed;
        Check(registry.PeekCommittedContextMenuRequest(committed) && committed.panel == panelB &&
                  committed.host == host && committed.surfaceId.value == 50000 && committed.surface == surface &&
                  committed.anchor == requestB.anchor &&
                  registry.GetStatus().pendingRequestCount == 0,
              "only the exact focus-matched panel/host request commits and all frame requests drain");
        registry.CommitContextMenuRequests(focus);
        Check(registry.PeekCommittedContextMenuRequest(committed) && committed.panel == panelB,
              "an already committed popup survives a later focus commit when no new pending request exists");
        Check(registry.QueueContextMenuRequest(request, &failure),
              "active-popup replacement fixture queues a losing request from another panel");
        registry.CommitContextMenuRequests(focus);
        Check(registry.PeekCommittedContextMenuRequest(committed) && committed.panel == panelB,
              "a losing pending request does not replace or clear the already committed popup");
        registry.CancelContextMenuRequestsForHost(host);
        Check(!registry.PeekCommittedContextMenuRequest(committed),
              "host cancellation clears an already committed popup stamped for that exact host");

        Check(registry.QueueContextMenuRequest(request, &failure), "host-mismatch fixture queues a request");
        focus.panel = panelA;
        focus.host = {2, 1};
        registry.CommitContextMenuRequests(focus);
        Check(!registry.PeekCommittedContextMenuRequest(committed) && registry.GetStatus().pendingRequestCount == 0,
              "host mismatch discards the pending request even when the focused panel matches");

        Check(registry.QueueContextMenuRequest(request, &failure), "panel-mismatch fixture queues a request");
        focus.panel = panelB;
        focus.host = host;
        registry.CommitContextMenuRequests(focus);
        Check(!registry.PeekCommittedContextMenuRequest(committed) && registry.GetStatus().pendingRequestCount == 0,
              "focus mismatch discards the pending request without a committed popup");

        Check(registry.QueueContextMenuRequest(request, &failure), "explicit-discard fixture queues a request");
        registry.DiscardPendingContextMenuRequests();
        Check(registry.GetStatus().pendingRequestCount == 0,
              "explicit pending-request discard clears the bounded frame transaction");

        Check(registry.QueueContextMenuRequest(request, &failure), "panel-cancel fixture queues a request");
        Check(registry.QueueContextMenuRequest(requestB, &failure), "panel-cancel fixture queues another panel request");
        registry.CancelContextMenuRequestsForPanel(panelA);
        Check(registry.GetStatus().pendingRequestCount == 1,
              "panel cancellation removes only requests stamped for the exact panel");
        registry.CancelContextMenuRequestsForOwner(owner);
        Check(registry.GetStatus().pendingRequestCount == 0,
              "owner cancellation removes the remaining requests stamped for that owner");

        Check(registry.QueueContextMenuRequest(request, &failure), "host-cancel fixture queues a request");
        registry.CancelContextMenuRequestsForHost(host);
        Check(registry.GetStatus().pendingRequestCount == 0,
              "host cancellation removes pending requests stamped for that exact host");

        for (u32 index = 0; index < editor::MaximumPendingEditorContextMenuRequests; ++index)
        {
            detail::EditorUiStampedContextMenuRequest bounded{
                {index + 10u, 1}, host, owner, {50000}, surface, 43, {static_cast<float>(index), 1.0f}};
            Check(registry.QueueContextMenuRequest(bounded, &failure),
                  "pending-request queue admits every fixed-capacity entry");
        }
        detail::EditorUiStampedContextMenuRequest overflow{
            {100, 1}, host, owner, {50000}, surface, 43, {100.0f, 1.0f}};
        Check(!registry.QueueContextMenuRequest(overflow, &failure) &&
                  failure.code == editor::EditorUiFailureCode::CapacityExceeded &&
                  registry.GetStatus().pendingRequestCount == editor::MaximumPendingEditorContextMenuRequests,
              "pending-request queue rejects one entry beyond the fixed bound");
        registry.CancelContextMenuRequestsForSurface(surface);
        Check(registry.GetStatus().pendingRequestCount == 0,
              "surface cancellation removes every pending request stamped with its exact token");

        detail::EditorUiStampedContextMenuRequest staleRequest{
            panelA, host, owner, {50000}, surface, 44, {5.0f, 6.0f}};
        Check(registry.QueueContextMenuRequest(staleRequest, &failure), "stale-token fixture queues the live surface token");
        state = editor::EditorUiState::Ready;
        Check(registry.UnregisterSurface(owner, surface, &failure),
              "surface removal cancels stamped pending requests before physical token retirement");
        detail::EditorUiSurfaceToken replacement;
        Check(registry.RegisterSurface(owner, ContextSurface(50000), replacement, &failure) &&
                  replacement.index == surface.index && replacement.generation != surface.generation,
              "replacement surface with the same stable ID receives a different generation");
        state = editor::EditorUiState::BuildingFrame;
        Check(!registry.QueueContextMenuRequest(staleRequest, &failure) &&
                  failure.code == editor::EditorUiFailureCode::InvalidHandle && failure.surface.value == 50000,
              "old stamped request cannot retarget a replacement surface by stable ID");
        focus.frame = 44;
        focus.panel = panelA;
        registry.CommitContextMenuRequests(focus);
        Check(!registry.PeekCommittedContextMenuRequest(committed),
              "replacement surface cannot inherit a pending request from the retired generation");
    }

    class WrongThreadMutationWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadMutationWorker(detail::EditorSurfaceRegistryCore& registry,
                                  const editor::EditorExtensionOwner owner,
                                  const detail::EditorUiSurfaceToken surface) noexcept
            : Thread("surfaceWrongThread"), m_registry(registry), m_owner(owner), m_surface(surface)
        {
        }

        void ThreadFunction() noexcept override
        {
            editor::EditorUiFailure failure;
            detail::EditorUiSurfaceToken surface;
            m_registerSurface = m_registry.RegisterSurface(m_owner, ContextSurface(60001), surface, &failure);
            m_registerSurfaceFailure = failure.code;
            m_registerSurfaceIdentity = failure.surface;
            detail::EditorUiContributionToken contribution;
            m_registerContribution =
                m_registry.RegisterContribution(m_owner, Separator(60002, {60000}), contribution, &failure);
            m_registerContributionFailure = failure.code;
            m_registerContributionSurface = failure.surface;
            m_registerContributionIdentity = failure.contribution;
            detail::EditorUiStampedContextMenuRequest request{
                {1, 1}, {1, 1}, m_owner, {60000}, m_surface, 60, {1.0f, 1.0f}};
            m_request = m_registry.QueueContextMenuRequest(request, &failure);
            m_requestFailure = failure.code;
            m_requestSurface = failure.surface;
        }

        [[nodiscard]] bool AllRejected() const noexcept
        {
            return !m_registerSurface && !m_registerContribution && !m_request &&
                   m_registerSurfaceFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_registerSurfaceIdentity.value == 60001 &&
                   m_registerContributionFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_registerContributionSurface.value == 60000 &&
                   m_registerContributionIdentity.value == 60002 &&
                   m_requestFailure == editor::EditorUiFailureCode::WrongThread &&
                   m_requestSurface.value == 60000;
        }

    private:
        detail::EditorSurfaceRegistryCore& m_registry;
        editor::EditorExtensionOwner m_owner;
        detail::EditorUiSurfaceToken m_surface;
        editor::EditorUiFailureCode m_registerSurfaceFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiSurfaceId m_registerSurfaceIdentity;
        editor::EditorUiFailureCode m_registerContributionFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiSurfaceId m_registerContributionSurface;
        editor::EditorUiContributionId m_registerContributionIdentity;
        editor::EditorUiFailureCode m_requestFailure = editor::EditorUiFailureCode::None;
        editor::EditorUiSurfaceId m_requestSurface;
        bool m_registerSurface = false;
        bool m_registerContribution = false;
        bool m_request = false;
    };

    void TestPhaseAndThreadAffinity() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        editor::EditorUiFailure failure;
        const editor::EditorExtensionOwner owner = Owner(600);
        detail::EditorUiSurfaceToken surface;
        Check(registry.InitializeBuiltIns(&failure) &&
                  registry.RegisterSurface(owner, ContextSurface(60000), surface, &failure),
              "phase/thread fixture initializes its context root");

        state = editor::EditorUiState::BuildingFrame;
        detail::EditorUiContributionToken contribution;
        detail::EditorUiSurfaceToken rejectedSurface;
        Check(!registry.RegisterSurface(owner, ContextSurface(60004), rejectedSurface, &failure) &&
                  failure.code == editor::EditorUiFailureCode::WrongPhase && failure.surface.value == 60004 &&
                  !rejectedSurface.IsValid(),
              "surface-root mutation during BuildingFrame rejects before committed-tree mutation");
        Check(!registry.RegisterContribution(owner, Separator(60003, {60000}), contribution, &failure) &&
                  failure.code == editor::EditorUiFailureCode::WrongPhase && failure.surface.value == 60000 &&
                  failure.contribution.value == 60003 &&
                  registry.GetStatus().contributionCount == 7,
              "topology mutation during BuildingFrame rejects before committed-tree mutation");
        state = editor::EditorUiState::Ready;

        WrongThreadMutationWorker worker(registry, owner, surface);
        worker.InitThread();
        worker.JoinThread();
        Check(worker.AllRejected(), "surface topology and stamped-request mutation reject worker-thread callers");
        Check(registry.GetStatus().surfaceCount == 3 && registry.GetStatus().contributionCount == 7 &&
                  registry.GetStatus().pendingRequestCount == 0,
              "wrong-thread attempts leave registry topology and request state unchanged");
    }

    struct BridgeFixture final
    {
        editor::EditorCommandPresentation presentation;
        editor::EditorCommandFailureCode queryFailure = editor::EditorCommandFailureCode::None;
        editor::EditorCommandFailureCode invokeFailure = editor::EditorCommandFailureCode::None;
        bool queryResult = true;
        bool invokeResult = true;
        editor::EditorCommandId unsupportedCommand;
        editor::EditorCommandId unsupportedCommandSecond;
        u32 queryCalls = 0;
        u32 invokeCalls = 0;
        u32 validateCalls = 0;
    };

    bool QueryBridge(const editor::EditorCommandId command, editor::EditorCommandPresentation& presentation,
                     editor::EditorCommandFailure* const failure, void* const userData) noexcept
    {
        auto& fixture = *static_cast<BridgeFixture*>(userData);
        ++fixture.queryCalls;
        presentation = fixture.presentation;
        if (command == fixture.unsupportedCommand || command == fixture.unsupportedCommandSecond)
        {
            presentation = {};
            presentation.state = editor::EditorCommandState::Unsupported();
            return true;
        }
        if (fixture.queryResult)
            return true;
        if (failure != nullptr)
            *failure = {fixture.queryFailure, command, {}, {}, {}, "fixture presentation query failure"};
        return false;
    }

    bool InvokeBridge(const editor::EditorCommandId command, editor::EditorCommandFailure* const failure,
                      void* const userData) noexcept
    {
        auto& fixture = *static_cast<BridgeFixture*>(userData);
        ++fixture.invokeCalls;
        if (fixture.invokeResult)
            return true;
        if (failure != nullptr)
            *failure = {fixture.invokeFailure, command, {}, {}, {}, "fixture presentation invoke failure"};
        return false;
    }

    bool ValidateBridge(const editor::EditorExtensionOwner&, const editor::EditorCommandId,
                        editor::EditorCommandFailure*, void* const userData) noexcept
    {
        ++static_cast<BridgeFixture*>(userData)->validateCalls;
        return true;
    }

    class WrongThreadBridgeWorker final : public vanguard::concurrency::Thread
    {
    public:
        WrongThreadBridgeWorker(detail::EditorUiCommandBridgeSlot& slot, const detail::EditorUiCommandBridge& bridge,
                                const detail::EditorUiCommandBridgeToken token, const bool remove) noexcept
            : Thread("surfaceBridgeWrongThread"), m_slot(slot), m_bridge(bridge), m_token(token), m_remove(remove)
        {
        }

        void ThreadFunction() noexcept override
        {
            editor::EditorUiFailure failure;
            if (m_remove)
                m_result = m_slot.Unregister(m_token, &failure);
            else
            {
                detail::EditorUiCommandBridgeToken token;
                m_result = m_slot.Register(m_bridge, token, &failure);
            }
            m_failure = failure.code;
        }

        [[nodiscard]] bool Result() const noexcept { return m_result; }
        [[nodiscard]] editor::EditorUiFailureCode Failure() const noexcept { return m_failure; }

    private:
        detail::EditorUiCommandBridgeSlot& m_slot;
        detail::EditorUiCommandBridge m_bridge;
        detail::EditorUiCommandBridgeToken m_token;
        bool m_remove = false;
        bool m_result = false;
        editor::EditorUiFailureCode m_failure = editor::EditorUiFailureCode::None;
    };

    struct ContributionCapture final
    {
        detail::EditorUiContributionView view;
        bool found = false;
    };

    void CaptureCommandContribution(const detail::EditorUiContributionView& contribution, void* const userData) noexcept
    {
        auto& capture = *static_cast<ContributionCapture*>(userData);
        if (!capture.found && contribution.kind == detail::EditorUiContributionKind::Command)
        {
            capture.view = contribution;
            capture.found = true;
        }
    }

    void TestContextMenuPresentationNormalization() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        const editor::EditorExtensionOwner owner = Owner(750);
        editor::EditorUiFailure failure;
        detail::EditorUiSurfaceToken surface;
        detail::EditorUiContributionToken token;
        Check(registry.InitializeBuiltIns(&failure) &&
                  registry.RegisterSurface(owner, ContextSurface(75000), surface, &failure),
              "context-presentation fixture initializes an owned context surface");
        Check(registry.RegisterContribution(owner, Separator(75001, {75000}, {}, 0), token, &failure) &&
                  registry.RegisterContribution(owner, Command(75002, {75000}, "unsupported", {}, 1), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(75003, {75000}, {}, 2), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(75004, {75000}, {}, 3), token, &failure) &&
                  registry.RegisterContribution(owner, Command(75005, {75000}, "enabled", {}, 4), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(75006, {75000}, {}, 5), token, &failure) &&
                  registry.RegisterContribution(owner, Command(75007, {75000}, "unsupported tail", {}, 6), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(75008, {75000}, {}, 7), token, &failure) &&
                  registry.RegisterContribution(owner, Command(75009, {75000}, "enabled after gap", {}, 8), token, &failure) &&
                  registry.RegisterContribution(owner, Separator(75010, {75000}, {}, 9), token, &failure),
              "context-presentation fixture registers leading/duplicate/trailing separators around commands");

        BridgeFixture fixture;
        fixture.presentation.state = editor::EditorCommandState::Enabled();
        fixture.unsupportedCommand = {0x100000ull + 75002u};
        fixture.unsupportedCommandSecond = {0x100000ull + 75007u};
        const detail::EditorUiCommandBridge bridge{QueryBridge, InvokeBridge, ValidateBridge, &fixture};
        detail::EditorUiContextMenuItems items;
        registry.BuildContextMenuItems({75000}, &bridge, items);
        Check(items.Size() == 3u && items[0].contribution.id.value == 75005u &&
                  items[1].contribution.id.value == 75006u && items[2].contribution.id.value == 75009u &&
                  items[0].contribution.kind == detail::EditorUiContributionKind::Command &&
                  items[1].contribution.kind == detail::EditorUiContributionKind::Separator &&
                  items[2].contribution.kind == detail::EditorUiContributionKind::Command,
              "context presentation omits Unsupported commands and normalizes leading/duplicate/trailing separators");
        Check(fixture.queryCalls == 4u,
              "context presentation queries each command exactly once while deriving visibility");
    }

    void TestCommandBridgeAndFailureContainment() noexcept
    {
        editor::EditorUiState state = editor::EditorUiState::Ready;
        LocalGenerationSource generations;
        detail::EditorSurfaceRegistryCore registry(state, generations.Get());
        const editor::EditorExtensionOwner owner = Owner(700);
        editor::EditorUiFailure failure;
        Check(registry.InitializeBuiltIns(&failure), "command-bridge fixture initializes built-in surfaces");

        detail::EditorUiSurfaceToken surface;
        detail::EditorUiContributionToken contribution;
        Check(registry.RegisterSurface(owner, ContextSurface(70000), surface, &failure) &&
                  registry.RegisterContribution(owner, Command(70001, {70000}, "Bridge Command"), contribution, &failure),
              "command-bridge fixture registers one command contribution");
        ContributionCapture capture;
        registry.VisitContributions({70000}, CaptureCommandContribution, &capture);
        Check(capture.found && capture.view.command.IsValid(),
              "command-bridge fixture resolves the committed command view without exposing record internals");

        BridgeFixture fixture;
        fixture.presentation.state = editor::EditorCommandState::Enabled(true);
        fixture.presentation.binding.bound = true;
        fixture.presentation.binding.chord = {vanguard::input::Key::K, editor::EditorShortcutModifiers::Control};
        const detail::EditorUiCommandBridge bridge{QueryBridge, InvokeBridge, ValidateBridge, &fixture};

        editor::EditorCommandPresentation presentation;
        Check(registry.QueryContributionPresentation(capture.view, bridge, presentation) &&
                  fixture.queryCalls == 1u && presentation.state.kind == editor::EditorCommandStateKind::Enabled &&
                  presentation.state.checked && presentation.binding.bound &&
                  presentation.binding.chord == fixture.presentation.binding.chord,
              "surface core forwards one presentation query and preserves the atomic command snapshot");

        const detail::EditorSurfaceRegistryStatus beforeFailure = registry.GetStatus();
        fixture.queryResult = false;
        fixture.queryFailure = editor::EditorCommandFailureCode::HandlerQueryFailure;
        presentation = {};
        Check(!registry.QueryContributionPresentation(capture.view, bridge, presentation) &&
                  presentation.state.kind == editor::EditorCommandStateKind::Disabled &&
                  registry.GetStatus().surfaceCount == beforeFailure.surfaceCount &&
                  registry.GetStatus().contributionCount == beforeFailure.contributionCount &&
                  registry.GetStatus().commandFailureCount == beforeFailure.commandFailureCount + 1u &&
                  registry.GetStatus().lastCommandFailureContribution == capture.view.id &&
                  registry.GetStatus().lastCommandFailureCommand == capture.view.command &&
                  registry.GetStatus().lastCommandFailureCode == editor::EditorCommandFailureCode::HandlerQueryFailure,
              "presentation query failure disables only the item and records bounded status without tree mutation");
        Check(!registry.QueryContributionPresentation(capture.view, bridge, presentation) &&
                  registry.GetStatus().repeatedCommandFailureCount == 1u,
              "repeated identical presentation failures increment the bounded repeat counter");

        fixture.invokeResult = false;
        fixture.invokeFailure = editor::EditorCommandFailureCode::HandlerInvocationFailure;
        const u32 invokes = fixture.invokeCalls;
        Check(!registry.InvokeContributionCommand(capture.view, bridge) && fixture.invokeCalls == invokes + 1u &&
                  registry.GetStatus().lastCommandFailureCode == editor::EditorCommandFailureCode::HandlerInvocationFailure,
              "surface invocation failure is recorded once without retry or topology mutation");

        detail::EditorUiCommandBridgeSlot slot(state);
        WrongThreadBridgeWorker wrongThreadRegister(slot, bridge, {}, false);
        wrongThreadRegister.InitThread();
        wrongThreadRegister.JoinThread();
        Check(!wrongThreadRegister.Result() && wrongThreadRegister.Failure() == editor::EditorUiFailureCode::WrongThread &&
                  !slot.IsAttached(),
              "bridge registration rejects worker-thread callers without partial attachment");
        detail::EditorUiCommandBridgeToken bridgeToken;
        Check(slot.Register(bridge, bridgeToken, &failure) && bridgeToken.IsValid() && slot.Get() != nullptr,
              "production bridge slot admits one exact live bridge while UI is Ready");
        WrongThreadBridgeWorker wrongThreadRemoval(slot, bridge, bridgeToken, true);
        wrongThreadRemoval.InitThread();
        wrongThreadRemoval.JoinThread();
        Check(!wrongThreadRemoval.Result() && wrongThreadRemoval.Failure() == editor::EditorUiFailureCode::WrongThread &&
                  slot.IsAttached(),
              "bridge removal rejects worker-thread callers without detaching callbacks");
        detail::EditorUiCommandBridgeToken duplicate;
        Check(!slot.Register(bridge, duplicate, &failure) && failure.code == editor::EditorUiFailureCode::InUse &&
                  !duplicate.IsValid() && slot.Get() != nullptr,
              "duplicate bridge registration fails without replacing the live bridge");
        const detail::EditorUiCommandBridgeToken stale{bridgeToken.generation + 1u};
        Check(!slot.Unregister(stale, &failure) && failure.code == editor::EditorUiFailureCode::InvalidHandle &&
                  slot.IsAttached(),
              "stale bridge token cannot detach the live bridge");
        state = editor::EditorUiState::BuildingFrame;
        Check(!slot.Unregister(bridgeToken, &failure) && failure.code == editor::EditorUiFailureCode::WrongPhase &&
                  slot.IsAttached(),
              "bridge removal during UI authoring fails closed without detaching callbacks");
        state = editor::EditorUiState::Ready;
        Check(slot.Unregister(bridgeToken, &failure) && !slot.IsAttached(),
              "exact bridge token detaches while UI is Ready");
    }
} // namespace

int main()
{
    vanguard::concurrency::InitializeMainThread();
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    TestBuiltInIdentityAndAnchors();
    TestIdentityStalenessAndCapacities();
    TestGenerationExhaustionAndFreshCoreStaleness();
    TestDescriptorLabelsKindsAndOwnership();
    TestHierarchyPlacementAndDeterministicOrdering();
    TestPendingRequestTransactionState();
    TestPhaseAndThreadAffinity();
    TestContextMenuPresentationNormalization();
    TestCommandBridgeAndFailureContainment();

    if (g_failures == 0)
        std::printf("Vanguard editor bounded surface registry tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
