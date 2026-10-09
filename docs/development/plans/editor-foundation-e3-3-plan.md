# E3.3 - panel instances, docking and focus context

## Status and authorization

E3.1 is accepted. E3.2 definition plus P0-P6 are closed and E3.2 is accepted after
final independent exact-CURRENT review at 0 BLOCKER / 0 HIGH / 0 MEDIUM.

On 2026-09-19 the user explicitly authorized **E3.3 definition work only**. This
document is the durable cross-agent definition for E3.3. Definition work may inspect
CURRENT source, reconcile planning/status documentation and create the active planning
triplet. It does **not** authorize E3.3 P0 or any production implementation, Premake
generation, compilation, executable tests or native editor execution.

The definition is now **closed** after exact-CURRENT Vanguard/RED ownership review,
incorporation of the panel-owner/presentation/LIFO/thread/generation/testability findings,
definition hygiene and a fresh independent exact-CURRENT review at **0 BLOCKER /
0 HIGH / 0 MEDIUM**. No implementation/build/test/native-execution claim is made by
definition closure.

The active definition triplet is
`.planning/e3-3-panel-docking-focus/{task_plan,findings,progress}.md`.

The user subsequently said `start P0`; **E3.3 P0 is now closed** after exact-CURRENT
Vanguard/RED source review, planning/file/test-shape lock, hygiene and fresh independent
review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P0 made no production C++ change and ran no
Premake generation, compilation, executable tests or native editor.

The user subsequently explicitly said `start P1`. **E3.3 P1 - bounded panel type/instance
policy core is now closed** after fresh Premake generation, Debug + Development focused /
retained executable validation, Debug + Development production-editor compile/link,
dependency/scope/hygiene checks and fresh independent exact-CURRENT review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. No native-editor run is claimed for P1.

The user then explicitly said `start P2`. **E3.3 P2 - E3.1 extension-owned panel
registration and rollback is closed** after exact-CURRENT implementation review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**.

The user subsequently changed the E3.3 validation policy: do not spend large amounts of
time repeating broad Premake/build/test matrices for every intermediate P. Comprehensive
generation, Debug/Development, native and acceptance validation is batched into **P6**.
Intermediate Ps close on coherent implementation plus focused source/review evidence, with
deferred execution obligations recorded for P6. The user's instruction to update CURRENT
and "move on" authorized **P3**. **P3 is now closed** after source-level coherence review
and clean post-fix independent exact-CURRENT review. Its first independent closure review
found 0 BLOCKER / 0 HIGH / 1 MEDIUM because the production-shaped proof closed the detached
panel but did not reopen the same handle. The proof now performs close -> one closed frame ->
same-handle reopen and accepts retention only with the original factory state, one create
and zero destroys before a final close. Fresh post-fix independent reviews returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. The existing private UI owner centralizes ordinary panel
windows, stable ImGui identity, one default dockspace, first-use placement and close
propagation; the E0 proof is contents-only; quiesce clears cached dock-node identity before
the ImGui context can be recreated. Broad execution remains explicitly deferred to P6.
The user subsequently said `START NEXT P THEN`, authorizing **P4**. **P4 is now closed**
after coherent exact-CURRENT implementation/source review and fresh independent
**0 BLOCKER / 0 HIGH / 0 MEDIUM** review. It publishes toolkit-neutral focused-panel state,
uses one private shell-owned removable focus sink, validates same-owner live Local-context
associations, reconciles focus before pending visibility/shortcut routing, preserves strict
ControlTool-above-Local LIFO failure, and clears focused receiver borrows before
close/destroy/revoke and shell teardown. Broad execution remains deferred to P6.
The user subsequently explicitly said `START P5`; **P5 is now closed** after exact-current
visible native acceptance, exact-current close/reopen lifecycle evidence, scoped hygiene and
fresh independent **0 BLOCKER / 0 HIGH / 0 MEDIUM** closure review.
The user subsequently explicitly authorized **P6**. Its exact-current comprehensive
validation and documentation reconciliation are complete; the first final review's two
documentation-only MEDIUMs were corrected and fresh post-fix independent review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P6 is closed and E3.3 is accepted.** The E3.4
definition and P0 source/planning review subsequently closed at fresh post-fix
0 BLOCKER / 0 HIGH / 0 MEDIUM while P1-P6 and E3.5+ remain
unauthorized/unstarted.

## Objective

Turn the existing editor UI panel foundation into one extension-owned, bounded shell
surface with stable panel type/instance identity, deterministic singleton and
multi-instance policy, explicit close/reopen semantics, a centrally owned dockspace and
floating/detached-window behavior, and a same-frame focus bridge into the accepted E3.2
command context stack.

E3.3 is **panel shell infrastructure**, not a collection of production tools. It must
make later Asset Browser, Console, Inspector, document views and viewport panels able to
plug into one owner without each inventing lifetime, docking or command-focus behavior.

## Read-first authority

Before any E3.3 gate, re-read:

- root `AGENTS.md`;
- [development document map](../README.md);
- [editor foundation roadmap](editor-foundation-study-and-roadmap.md);
- [editor UI integration contract](../designs/editor-ui-integration.md);
- [E3 parent plan](editor-foundation-e3-plan.md);
- [accepted E3.1 plan](editor-foundation-e3-1-plan.md);
- [accepted E3.2 plan](editor-foundation-e3-2-plan.md);
- `editor/README.md`;
- CURRENT `editor_ui.hpp/.cpp`, private ImGui platform adapter and UI proof;
- CURRENT E3.1 shell/extension registration owner and ledger;
- CURRENT E3.2 command/context service and focused command tests;
- CURRENT `editor/premake5.lua` and native UI proof instructions.

## Exact-CURRENT starting facts

1. `EditorUiService` is the one `EditorWorkspace` UI service. It owns the ImGui
   context/frame, native UI hosts, textures and the existing `EditorPanelRegistry`.
   E3.3 must not add another panel service or registry.
2. The current panel registry is already bounded to 256 panel types and 512 panel
   instances. It rejects duplicate type/instance IDs, refuses type removal while live
   instances exist, and gives instances generation-checked `EditorPanelHandle`s.
3. Current `EditorPanelTypeDesc` has factory/destroy/draw callbacks and copied display
   name storage, but no extension owner, type-generation token, singleton/multi policy,
   focus-context association or default docking policy.
4. The current instance-generation helper is per-record state and wraps back to
   generation 1 after `u32` exhaustion. E3.3 touches this lifetime domain, so P1 must
   use process-monotonic/nonzero token generations (or an equivalently proven scheme)
   that keep stale handles invalid across slot reuse **and a fresh UI-service instance**,
   and fail explicitly on generation exhaustion instead of wrapping.
5. Current panel registry mutation checks UI service phase but does not independently
   reject a wrong-thread direct caller. E3.3 must add explicit MainThread validation and
   wrong-thread regression coverage rather than relying only on service affinity.
6. Current `SetOpen(false)` hides an instance without destroying panel state. Explicit
   `DestroyPanel` releases the instance state. E3.3 preserves that useful distinction:
   **close means hidden; destroy means lifetime ends**.
7. `DrawOpenPanels` currently invokes each panel callback directly. The E0 proof panel
   itself calls `ImGui::Begin/End`, creates `DockSpaceOverViewport`, chooses dock IDs and
   propagates the close flag. That proof-only ownership is not a scalable production
   shell contract; E3.3 moves top-level panel window/dock/focus ownership into the
   existing private UI implementation while panel callbacks author contents.
8. Native detached hosts are already created through the private ImGui platform adapter,
   `WindowService`, PresentationService and existing renderer/viewport ownership. E3.3
   reuses that path and never creates a second swap chain, window manager or renderer.
9. E3.1 owns one bounded extension activation identity and one 1024-entry owned-
   registration ledger. Command/context/handler contributions already use that owner and
   are logically revoked before reverse physical cleanup/private-state teardown.
10. E3.2 owns the one command service and its neutral active-context stack. `ControlTool`
   wins over `Local`, which wins over Workspace; newest context wins within a tier;
   contexts are generation checked and must be popped in exact LIFO order.
11. The E3.2 shortcut participant already runs after `EditorUiFrameParticipantId`, so a
   panel focus bridge that commits during the UI participant is visible to same-frame
   shortcut routing without another input pump.
12. Runtime and Headless profiles do not compose the editor UI/command/shell domain.

## RED definition evidence and intentional Vanguard differences

The accepted E3.1 exact-current RED study used
`D:/root/R6.Root/Mainline/dev/src/editor/editorCommon/` and recorded:

- `include/framework.h` lines 31-52 centralizes views/action/shortcut ownership; lines
  55-63 centralize focused editor/widget state; lines 82-117 centralize view
  registration, input dispatch, actions and shortcuts.
- `src/controlWidget.cpp` lines 45-60 pushes shortcut contexts on focus gain and pops
  them in reverse order on focus loss. Context lifetime follows focus lifetime.
- `src/dockingManager.cpp` lines 16-38 binds/unbinds docking and activation events in
  one manager; lines 41-74 route pane registration through one path and report
  registration/visibility changes.
- `src/editorResourceEditorManager.cpp` lines 744-770 centrally enumerates registered
  factories and asks them to create an editor rather than hard-coding every tool.

E3.3 adopts the useful RED ownership principles: one centralized shell/panel owner,
factory-based panel creation, focus-scoped command receivers and reverse teardown.
Vanguard deliberately keeps Dear ImGui docking, `EngineHost`, generation-checked
bounded registries, explicit statically compiled extension owners and typed direct APIs.
It does **not** copy wxAUI ownership, global mutable events, RTTI-wide discovery or a
second framework singleton.

## Locked ownership model

### One panel registry, no second service

`EditorUiService::GetPanels()` remains authoritative for panel type and instance state.
E3.3 may extract its internal bookkeeping into a private implementation unit to make it
GPU-free testable, but that object is the implementation behind the existing registry,
not another registry or service.

The panel registry remains main-thread/editor-frame owned. Registry mutation that
changes type/instance topology occurs while the UI service is Ready, not from inside an
active panel callback. Visibility/focus requests that are explicitly safe while a frame
is being authored remain narrowly defined.

### Stable type identity and generation token

`EditorPanelTypeId` remains the stable technical type identity. Display name/title is
not identity. E3.3 adds a generation-checked `EditorPanelTypeToken` (or equivalent exact
token) so physical owner cleanup cannot unregister a later registration that reused the
same type ID.

The public unowned path remains available only for editor-owned built-ins/proofs. A
statically compiled extension registers a panel type through the accepted E3.1
`EditorExtensionActivationContext`; the raw owner-stamped registration/revoke seam stays
private between shell and panel registry plus focused tests.

Each extension-owned panel type stores its exact `EditorExtensionOwner`. Every instance
of that type inherits the type owner; instance creation does not invent another owner.
One owned panel type consumes one E3.1 owned-ledger record rather than one ledger record
per runtime instance.

### Type policy: singleton versus multi-instance

Add an explicit bounded type policy:

- **Singleton** - at most one live instance of the type. Reopening an existing hidden
  singleton reuses the same instance/state/handle rather than running the factory again.
- **Multiple** - several stable instance IDs may coexist up to the global 512-instance
  bound. Duplicate instance IDs remain rejected.

The singleton contract includes one stable default instance ID. A singleton open/create
operation is idempotent for that ID. Multi-instance creation requires an explicit stable
`EditorPanelInstanceId`; E3.3 does not introduce a random/pointer-derived identity.

Instance handle generation continues to change after actual destruction/recreation.
Stale handles never resolve to a later instance, including after a full editor UI
service teardown/recreation in the same process. Type-token and instance-handle
generation exhaustion fails before slot reuse; it must never wrap to a value that can
alias an earlier stale token.

### Close/reopen versus destroy

The top-level window close affordance maps to `SetOpen(false)`. Closing a panel:

- removes it from drawing and focus routing;
- preserves its instance ID, state object and generation-checked handle;
- does not rerun its factory on reopen;
- can be reopened through the registry without a new identity.

`DestroyPanel` remains an explicit lifetime operation. It first removes focus/context
reachability, then runs the type destroy callback while the owning extension private
state is still alive, clears the instance slot and invalidates the old handle.

Owner teardown destroys every remaining instance of an owned type before physically
unregistering that type. Extension private-state teardown occurs only afterward.

### Logical owner revocation and callback safety

E3.3 extends the E3.1 revoke-before-cleanup rule to panels:

```text
close new extension registration admission
  -> logically revoke EditorExtensionOwner in command + panel domains
       -> owned panel types/instances stop drawing and stop winning focus
       -> focused panel Local context is removed before receiver state can die
       -> no later UI frame can call create/draw/destroy through a revoked owner
  -> reverse owned-ledger cleanup
       -> destroy all instances of each owned panel type
       -> unregister exact generation-checked panel type token
       -> remove command/context/handler registrations in established ledger order
  -> extension deactivate/private-state release
```

Logical revocation is allocation-free, idempotent and fail-closed. Physical cleanup may
diagnose an invariant violation but never restores draw/focus callback reachability.

## Docking and top-level panel window contract

### Private toolkit owner creates the window shell

The private ImGui-backed UI implementation, not extension panel code, owns the top-level
dockable window for every ordinary panel instance. It is responsible for:

- one `ImGui::Begin/End` pair per open panel instance;
- close-button propagation to the registry;
- stable internal ImGui identity derived from stable type+instance IDs, never only the
  localized/display title;
- first-use/default dock placement;
- focus observation;
- mapping floating/detached ImGui viewports through the accepted Vanguard native-host
  adapter.

The panel draw callback becomes a **contents callback**. It may use editor-private ImGui
widgets, but it does not own another top-level `Begin/End`, create its own dockspace,
create native windows or bypass the panel registry. The E0 proof fixture is migrated to
that same production shape rather than remaining a special ownership model.

Core/public editor domain APIs remain free of ImGui types.

### One default dockspace

E3.3 creates one primary workspace dockspace through the existing private UI layer once
per UI frame. It is not a new service or render owner.

Panel types may declare a small toolkit-neutral **initial placement** hint (for example
center/left/right/bottom/floating). This applies only when an instance has no current
runtime placement. Interactive user docking wins after that. E3.3 writes no layout file
and does not persist dock-node IDs; E3.6 owns versioned layout/user preference storage.

### Detached/floating hosts

Dragging a panel out of the primary host uses ImGui multi-viewport and the existing
`EditorUiImGuiPlatform` callbacks. The resulting native window remains a Vanguard
`WindowService` window with existing presentation/render-viewport ownership. Redocking
destroys the no-longer-needed detached host through the existing platform adapter.

No panel type directly owns a native window, swap chain, presentation output, render
graph or event pump.

## Focus publication and E3.2 command bridge

### Toolkit-neutral focus snapshot

After ordinary panel authoring, `EditorUiService` publishes one bounded same-frame
toolkit-neutral panel focus snapshot containing at least:

- frame number;
- focused `EditorPanelHandle` when any;
- stable panel type and instance IDs;
- native/editor host identity when available.

The public snapshot contains no `ImGuiWindow`, dock node, toolkit pointer or extension
state pointer.

Focus means the top-level panel or one of its child widgets owns ImGui keyboard focus.
Detached hosts use exactly the same rule. A panel that is closed, destroyed, logically
revoked or not authored this frame cannot remain the current focus receiver.

### One shell-owned focus bridge, no second command router

E3.3 adds a narrow private focus bridge owned beneath the existing shell/registry
integration. It maps each extension-owned panel type to one already registered E3.2
`Local` context type plus its explicit Inherit/Override policy.

That association is optional, but when present it must reference a live Local context
type registered by the **same `EditorExtensionOwner`** as the panel type. Registration
order is context first, panel type second, so reverse E3.1 ledger cleanup removes the
panel type/instances before the referenced context type. A panel may not borrow another
extension's context type, even if the stable ID is otherwise valid.

The focus bridge is not another command service. It calls the existing
`EditorCommandService::PushContext/PopContext` API and owns at most one production
panel-Local token at a time in E3.3.

Do not add a UI -> shell `EngineHost` dependency or another frame participant. The
existing later-starting shell registers exactly one **private typed, removable focus
sink** with the existing UI/panel owner once UI and command services are running. The
sink is a narrow synchronous callback contract, not a general event bus. UI frame focus
publication, focused-panel close/destroy and explicit focus invalidation use that sink
to ask the shell bridge to reconcile the one Local token. Shell Quiesce clears the
panel Local token and unregisters the sink before extension teardown; UI never retains a
shell callback after the shell owner is inactive.

The bridge commits only after `EditorUiService::EndFrame` has succeeded and published
the same-frame UI/capture/focus snapshot, but before `EditorUiFrameParticipantId`
returns. Because E3.2 shortcut routing is ordered after that participant, a focus change
affects shortcuts in the same engine frame. If focus reconciliation fails, the UI frame
participant fails and shortcut dispatch does not execute that frame.

Focus transition is deterministic:

```text
same focused panel -> retain current Local token
focused panel A -> panel B -> pop A Local token, push B Local token
focused panel -> no panel -> pop Local token, Workspace remains
panel close/destroy/revoke -> pop its Local token before receiver state destruction
```

The receiver borrow supplied to E3.2 is the focused panel's live instance state (or an
explicit type-provided receiver pointer derived from it). Its lifetime is therefore
bounded by the panel instance and must be removed from the command stack first.

A panel type with no Local command context simply contributes no panel context; focus
still remains observable for later UI consumers.

### ControlTool boundary

E3.3 does not implement generic widget/control tool contexts. Later controls may push
`ControlTool` contexts above the focused panel Local context using the accepted E3.2
API. They must release those contexts before the underlying panel Local context is
replaced or destroyed so E3.2's strict LIFO invariant remains true.

The focus bridge never force-pops through a live ControlTool token. If an attempted
panel focus replacement encounters E3.2 `ContextOrderViolation`, the transition fails
closed before shortcut dispatch and retains no newly pushed panel receiver. E3.3 tests
this explicitly and preserves the ControlTool > Local > Workspace rule; it does not
invent a second focus stack.

## Missing panel types and E3.6 restore boundary

E3.3 defines one bounded in-memory placeholder seam for later layout restore. Given a
stable missing type ID, instance ID and display label, the registry can represent an
**unresolved panel placeholder** that:

- preserves the unresolved stable IDs for later round-trip/diagnostics;
- draws only editor-owned bounded diagnostic content;
- never aliases another registered type;
- never invokes an extension factory/draw/destroy callback;
- cannot become a command receiver for the missing type;
- can be closed/destroyed like an ordinary shell instance.

E3.3 does **not** read or write layout files. E3.6 parses/version-checks persisted layout
data and decides when to request a placeholder. Because runtime dynamic extension
loading is not supported, E3.3 does not promise hot replacement of a placeholder by a
newly loaded extension.

## Failure and status model

Extend stable panel/UI failure reporting as needed for at least:

- invalid type/instance identity or invalid policy;
- duplicate type or instance;
- singleton policy violation;
- capacity exhausted;
- stale type token or panel handle;
- owner revoked / cross-owner cleanup attempt;
- type still in use when an unowned caller requests removal;
- factory failure;
- invalid/missing Local command context association;
- cross-owner Local command context association;
- focus synchronization/LIFO failure;
- wrong thread or UI phase;
- platform/docking failure;
- cleanup/lifecycle failure.

Expose bounded read-only status sufficient for tests and later diagnostics: registered
type count, instance/open counts, focused type/instance/handle, current focus frame and
last stable failure IDs. Do not retain unbounded focus/docking history.

## Threading, frame ordering and lifecycle

- Panel registry topology and extension registration are MainThread owned.
- Every panel-registry mutation path that can be called directly checks the MainThread
  owner and returns a stable WrongThread failure before mutation.
- Extension-owned panel types register during shell activation while ordinary UI frames
  are not running.
- Top-level window authoring/focus observation stays inside the existing UI participant.
- The focus bridge commits before the UI participant completes; E3.2 shortcut routing
  remains after UI and consumes the same-frame result.
- Registry mutation from inside a panel contents callback is rejected except the narrow
  close/focus-request operations explicitly safe for deferred application.
- Panel destroy callbacks never run after the owning extension private state is released.
- Type/instance generation counters remain stale-safe across slot reuse and fresh UI
  service instances; they never wrap into a reusable stale identity and exhaustion is
  an explicit bounded failure.
- Quiesce closes focus/command callback admission, removes the active panel context,
  revokes owned panel draw reachability, then reverse-cleans registrations before UI and
  workspace service shutdown.
- No worker executes panel callbacks or mutates docking/focus state.
- Runtime/Headless profiles compose none of this domain.

## Focused validation strategy

### GPU-free `editorPanelTests`

E3.3 should add a focused ConsoleApp `editorPanelTests`. If necessary, extract the
panel bookkeeping/state machine from `editor_ui.cpp` into a private implementation unit
used by `ManagedEditorUiService`; the test target must exercise that **same production
implementation**, not a duplicate registry.

The GPU-free matrix proves at least:

- type/instance generation, stale-token rejection across slot reuse/fresh service, and capacities;
- type/instance generation exhaustion fails without wraparound aliasing;
- wrong-thread registration/create/destroy/open/focus mutation fails without state change;
- singleton create/open/reopen identity and factory-count behavior;
- multi-instance uniqueness and capacity;
- close preserves state/handle; destroy invalidates handle and runs destroy once;
- owner revoke removes draw/focus reachability before physical cleanup;
- owner cleanup destroys instances before unregistering the type;
- partial extension activation rollback leaves no live panel callbacks;
- missing-type placeholder preserves unresolved IDs and invokes no extension callbacks;
- focus state transition model, hidden/destroyed/revoked focus clearing;
- no allocation/partial state on rejected descriptor/policy/capacity paths.

This target must not initialize D3D12, PresentationService, a native window or
`assetStreaming`.

### Retained E3.1/E3.2 regressions

`editorShellTests` remains the owner/rollback oracle and gains only the focused cases
needed for panel-type owned-ledger behavior.

`editorCommandTests` remains the command-routing oracle and gains the smallest focused
panel Local-context bridge cases: same-frame focus switch, Local > Workspace,
ControlTool > Local preservation, close/revoke clearing and no stale receiver callback.

No test may replace the accepted E3.1/E3.2 matrices with a panel-specific mock of their
core ownership semantics.

### Native docking/focus proof

Production editor/native proof remains separate from the GPU-free behavioral oracles.
Before E3.3 may be accepted, exact CURRENT evidence must cover:

- default primary dockspace and at least one ordinary registered production-shaped panel;
- docked -> detached -> redocked interaction with the same stable instance/state;
- detached host creation/destruction through Vanguard ownership;
- alternating focus across primary/detached hosts with the intended panel receiving
  text/command focus;
- close/reopen preserving state;
- resize/minimize/restore;
- mixed-DPI monitor movement when the environment provides different-DPI displays;
- font-atlas/update behavior while hosts are minimized, or equally strong current
  evidence replacing that exact E0F open check;
- clean shutdown with submitted work and detached hosts.

Automated lifecycle proof and manual desktop observation remain separate claims. E3.3
must not relabel an unperformed interactive check as passed.

## Expected implementation file/target ledger

P0 must refresh this ledger against exact CURRENT before implementation. The intended
minimal set is:

### Existing production files likely extended

- `editor/framework/include/vanguard/editor/editor_ui.hpp`
- `editor/framework/src/editor_ui.cpp`
- `editor/private/vanguard/editor/editor_ui_imgui_platform.hpp` only for a narrow private
  current-viewport -> existing `EditorHostWindowHandle` query used by P3/P4 focus
  publication;
- `editor/framework/src/editor_ui_imgui_platform.cpp` only for that query; native-window,
  presentation and viewport ownership remain unchanged;
- `editor/private/vanguard/editor/editor_shell_service.hpp`
- `editor/extensions/editor_shell_service.cpp`
- `editor/private/vanguard/editor/editor_commands.hpp` only if a narrow read-only/private
  context-validation seam is required;
- `editor/commands/editor_command_service.cpp` only for that exact validation/order seam,
  not for a new focus stack;
- Historical implementation used `editor/framework/src/editor_ui_proof.cpp` and
  `editor/private/vanguard/editor/editor_ui_proof.hpp`. After closure cleanup that
  fixture is isolated and phase-neutral under
  `editor/tests/support/native_ui_integration_test_harness.{cpp,hpp}`. It retains
  the exact generation-checked panel-type token from `Start` through `Stop` while
  remaining test-only; production editor code must not depend on the harness;
- `editor/tests/README.md`
- `editor/premake5.lua`

### Narrow new private files permitted if CURRENT P0 confirms extraction is cleaner

- `editor/private/vanguard/editor/editor_panels.hpp`
- `editor/panels/editor_panel_registry.cpp`
- `editor/tests/editor_panel_tests.cpp`

The extracted panel implementation remains owned by `EditorUiService`; its existence is
not permission to add `EditorPanelServiceId`, another registry capability or another
frame participant.

### Retained tests likely touched narrowly

- `editor/tests/editor_shell_tests.cpp`
- `editor/tests/editor_command_tests.cpp`

Any larger file set requires a P0 finding explaining why the existing owners cannot
satisfy the contract.

### P0 exact-CURRENT ledger clarification

The authorized P0 source review confirms the three permitted private files are the
smallest clean extraction/test seam:

- `editor/private/vanguard/editor/editor_panels.hpp` - private production panel-core
  contract/authoring view only;
- `editor/panels/editor_panel_registry.cpp` - the one production panel type/instance
  state machine used by `ManagedEditorUiService` and `editorPanelTests`;
- `editor/tests/editor_panel_tests.cpp` - GPU/toolkit-free focused oracle.

`ManagedEditorUiService` changes from inheriting the registry implementation to owning
that private core by composition and still returns it from `GetPanels()`. The core owns
no host, renderer, ImGui or presentation state. A private open-panel authoring traversal
lets the existing UI frame owner invoke contents in P1 and later wrap those same records
with P3's top-level ImGui shell without duplicating the registry.

`SetOpen` stays the one visibility request admitted during `BuildingFrame`, but the
extracted core makes that admission deferred: it records bounded per-instance pending
visibility instead of changing committed traversal state inside a contents callback.
P1 commits those requests only after successful frame authoring and discards them on
abort. P4 extends that exact commit point so, after successful `EndFrame`, focus/Local
context reconciliation succeeds **before** pending visibility and the new focused-panel
snapshot become committed. A strict-LIFO failure therefore cannot leave a hidden panel
as the still-committed Local receiver; the frame fails with the prior visibility/focus
state intact. Ready-phase visibility changes remain synchronous.

CURRENT `EditorUiImGuiPlatform::Impl::ViewportRecord` already stores the authoritative
`ImGuiViewport* -> WindowHandle -> EditorHostWindowHandle` association, but that record is
private to `editor_ui_imgui_platform.cpp`. P3/P4 therefore add one narrow private
platform query that resolves the currently authored ImGui viewport to its existing
`EditorHostWindowHandle`; the query exposes no ImGui type in public/domain APIs and does
not create or own another host. This is why the two existing platform files are now in
the exact future ledger above.

P1's ordinary editor-owned registration returns a generation-checked
`EditorPanelTypeToken {index,generation}`, and physical unregistration consumes that
exact token. `EditorPanelTypeId` remains the stable descriptor/lookup identity; an
ID-only physical-unregister path is not retained. Later P2 owner-stamped registration
uses the same tokenized production core, so built-ins/proofs and extension cleanup share
one stale-safe removal rule.

The production core also separates physical slot/token validity from logical
reachability. Ordinary lookup/create/authoring resolves only reachable type/instance
records. Later P2 revoke can therefore make owned create/draw/focus routes unreachable
without erasing the exact physical type record, destroy callback or factory data needed
for synchronous instance destruction and exact-token removal before extension private
state is released.

P0 also confirms the conditional command-service files in the ledger **are required in
future P4**, but only for one private read-only validation seam proving an associated
context ID is live, Local and stamped with the exact same `EditorExtensionOwner` as the
panel type. The current shell ledger has the context token but not authoritative tier /
reachability data, so duplicating that command-owned state in the shell would be worse.
The panel->Local association itself stays in the shell-private owned registration/focus
seam; public `editor_ui.hpp` / `EditorPanelTypeDesc` does not gain a dependency on the
private command-domain types.

The future `editorPanelTests` target compiles the same production panel core directly.
It may carry include paths for types named by `editor_ui.hpp`, but it must not link or
initialize `engine`, rendering/RHI, ImGui, native window/presentation or `assetStreaming`
owners. Existing `editorCommandTests` / `editorShellTests` fakes are updated only as
needed to satisfy evolved `EditorPanelRegistry` virtual signatures.

Generation exhaustion is tested through the same private production allocator path, not
by looping billions of registrations: the extracted core accepts a private generation-
source dependency whose production instance uses process-static atomics, while focused
tests may supply isolated counters seeded immediately below `UINT32_MAX`. The increment,
zero-reservation and terminal-exhaustion logic remains single-source production code.
For both type registration and instance creation, all descriptor/duplicate/capacity
validation and slot selection occurs first, then the required nonzero generation is
reserved **before** any factory callback, copied record mutation or slot activation. A
terminal generation counter therefore fails with an invalid output token/handle, zero
factory calls and no partially registered record. A later factory failure may consume a
generation value, but it still leaves no live instance; generation values need not be
contiguous.

## Ordered implementation gates

### P0 - exact-CURRENT architecture and test-shape lock

P0 was explicitly authorized and completed as READ-ONLY/current-tree preflight:

- refresh RED/Vanguard evidence and E0F open checks;
- verify exact panel-registry extraction/test seam;
- verify shell owner ledger capacity/order with panel-type cleanup;
- verify how the private UI layer can centralize top-level Begin/End without exposing
  toolkit types;
- verify the same-frame focus bridge point before E3.2 shortcut dispatch and strict
  context LIFO;
- verify exact file/target/dependency set and forbidden-scope scans;
- obtain independent exact-CURRENT P0 review and resolve every B/H/M.

P0 remained planning/source review only. Its final independent exact-CURRENT review
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM** after the planning contract was tightened for
transactional close/focus, exact platform/proof file coverage and pre-factory generation
exhaustion ordering. P0 made no production C++ change and ran no Premake generation,
compilation, executable test or native editor. **P0 is closed; P1-P6 and E3.4+ remain
unauthorized and unstarted.**

### P1 - bounded panel type/instance policy core

Implement the generation-checked type token, singleton/multi policy, stable singleton
instance ID, close/reopen versus destroy semantics, owner-neutral core record state and
the in-memory missing-type placeholder seam. Add GPU-free `editorPanelTests` for core
identity/lifetime/capacity/wrong-thread behavior. Correct existing instance-generation
storage so stale handles remain invalid across slot reuse and fresh service instances,
and exhaustion fails instead of aliasing a stale handle. No extension owner integration,
ImGui dockspace or command focus bridge yet.

### P2 - E3.1 extension-owned panel registration and rollback

Extend the accepted E3.1 activation context/private owned-registration seam with panel
type registration. Stamp the extension owner in the existing registry, add PanelType to
the existing 1024-entry owned ledger, logically revoke panel callbacks before cleanup,
destroy instances before exact type-token removal and prove partial activation failure /
reverse teardown/private-state lifetime. Preserve all E3.1/E3.2 regressions.

**Current status:** closed at exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM implementation
review. Comprehensive validation is deferred to P6 under the user's batching policy.

### P3 - centralized top-level panel windows and default docking

Move ordinary panel `Begin/End`, close propagation, stable internal window identity,
one primary dockspace and first-use placement into the existing private UI layer. Migrate
the E0 proof to contents-only callbacks. Prove close/reopen state retention and ordinary
floating/detached host creation through the existing platform adapter. No persistence.

**Current status:** closed after coherent exact-CURRENT source review and clean post-fix
independent **0 BLOCKER / 0 HIGH / 0 MEDIUM** review. The first independent closure review
found one MEDIUM because the proof did not reopen a closed panel; the corrected proof closes
the detached panel, observes a closed frame, reopens the same handle, verifies the original
factory state with no recreate/destroy, and closes it again before the final host assertion.
The UI-owned shell provides one top-level `Begin/End` per open panel, stable type+instance
internal identity, one default dockspace, first-use placement and deferred close
propagation. Floating panels remain on the existing multi-viewport/platform-adapter host
path; the proof callback owns contents only. Cached dock-node identity is reset on
quiesce/shutdown so a later ImGui context cannot reuse stale node IDs. Broad
compile/test/native validation is deferred to P6 under the user's batching policy.

Current implementation has moved the top-level window/dockspace shell into the existing
private UI owner, added toolkit-neutral initial-placement hints, stable type+instance ImGui
identity, close propagation through deferred `SetOpen`, editor-owned placeholder content,
and migrated the E0 proof away from owning Begin/End or a dockspace. The same proof now
demonstrates close/reopen retained state on the same panel handle without factory rerun.
Comprehensive runtime validation remains deferred to P6.

### P4 - same-frame panel focus to E3.2 Local context bridge

Publish toolkit-neutral focus state and implement the one shell-owned panel Local
context bridge. Associate owned panel types with validated E3.2 Local context types,
commit focus after successful UI `EndFrame` and before shortcut routing, remove focus before
close/destroy/revoke, and prove same-frame Local/Workspace/ControlTool arbitration.
Require any associated Local context type to be live and owned by the same extension as
the panel type; reject cross-owner borrowing transactionally.
Prove a live ControlTool-above-Local order violation fails closed rather than being
force-popped. No generic document model or widget-level ControlTool framework.

**Current status:** closed after coherent exact-CURRENT source implementation and fresh
independent **0 BLOCKER / 0 HIGH / 0 MEDIUM** review. The UI publishes only toolkit-neutral
focus identity; one private shell sink reconciles at most one associated Local token after
successful `EndFrame` and before the existing shortcut participant. Same-owner/live/Local
validation is transactional. Pending close/focus and ready close/destroy remove the Local
receiver before visibility/state mutation; owner revoke uses the existing privileged
command-owner compaction. A ControlTool above Local fails the transition through exact LIFO
rather than being force-popped. Focused source-level oracles are present; comprehensive
compile/test/native execution remains deferred to P6.

### P5 - native docking/detachment and remaining E0F interactive evidence

Run/repair only the bounded E3.3/E0F integration needed to prove real dock -> detach ->
redock, focus across hosts, state-preserving close/reopen, resize/minimize/restore,
mixed-DPI behavior and minimized-host atlas/update handling. Keep fixes in existing
window/input/presentation ownership; do not introduce E3.4+ surfaces or a second render
path. Record automated and manual evidence separately.

P5 cannot close by citing the older automated two-host proof alone while applicable E0F
interactive checks remain open.

**Current status:** closed. Exact-current visible native acceptance proves the real
command-panel dock -> detach -> redock cycle, same state/identity, third-host creation and
destruction, cross-host Local-context F8 routing, real input, 700x500 detached resize,
minimize/restore, available mixed-DPI transitions and minimized-host atlas
`WantUpdates -> OK`. A separate exact-current hidden lifecycle run proves one closed frame,
same-handle/state reopen, one create and zero destroys before final cleanup. Both runs exit 0.
Fresh independent exact-CURRENT review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. At this
historical P5 closure checkpoint broad generation/build/test acceptance remained deferred to
P6, which was not yet authorized. The later explicit P6 authorization recorded above
supersedes that P6 boundary. At that historical checkpoint E3.4+ remained unauthorized.

### P6 - validation, documentation and acceptance

Run fresh Premake VS2022 generation, serialized `/m:1` Debug + Development focused
builds/execution, `editorPanelTests`, retained `editorShellTests` and
`editorCommandTests`, production Debug editor integration, the applicable native docking
proof, full/scoped `git diff --check`, explicit final-LF/untracked hygiene, header-layout
checks and forbidden dependency/scope scans.

Obtain a fresh independent exact-CURRENT closure review. Resolve every BLOCKER/HIGH/
MEDIUM before marking E3.3 accepted. Reconcile the active triplet, this plan, E3 parent,
roadmap, development document map and applicable cross-status docs, then stop.
At this historical E3.3 P6 closure gate, **E3.4+ remained unauthorized.** The later
E3.4 definition-only authorization and subsequent definition closure supersede that
boundary without authorizing E3.4 P0.

**Current P6 execution status:** the exact-current acceptance matrix has completed. Fresh
focused generation passes; `editorPanelTests`, `editorCommandTests` and `editorShellTests`
build and exit 0 in Debug and Development, including the four focus/ownership cases added
after the matrix preflight. A validation-only full dependency workspace also generates and
links the production editor plus the dedicated lifecycle/native integration executables in
both configurations after building the existing SDL3 prerequisite first. The dedicated
lifecycle executable exits 0 for both `--bootstrap-path` and `--workspace-path` in Debug and
Development. Exact-current Debug native execution exits 0 for both the 120-frame hidden
lifecycle proof and the warned real-input interaction proof; the latter records a real
dock -> detach -> redock cycle, four command query/invoke cycles, cross-host input, an
available 144-DPI -> 96-DPI transition and an atlas update accepted while all hosts are
minimized. Scoped diff/final-LF, production/test-isolation, generated-target membership and
forbidden-symbol/phase-label scans are clean. Root `premake5.exe vs2022` still stops only on
the unrelated nanovanguard source-lineage audit at
`tools/nanovanguard/integration/project_asset_context_tests.cpp:744`; no E3.3 change is made
to that workstream. The first final independent P6 review found no code/matrix/hygiene defect
and two documentation-only MEDIUMs (stale authorization text and reusable executable paths).
Those corrections were applied; fresh post-fix independent review returned **0 BLOCKER /
0 HIGH / 0 MEDIUM**. **P6 is closed and E3.3 is accepted.**

## Minimum acceptance matrix

| Scenario | Required outcome |
|---|---|
| Duplicate/invalid type | Explicit rejection; no partial record |
| Type/instance slot reuse or fresh service | Old tokens/handles stay stale |
| Generation exhaustion | Fails before reuse; no wraparound stale alias |
| Wrong-thread mutation | Explicit rejection; no panel/type/focus mutation |
| Singleton open twice | Same live instance/handle; factory runs once |
| Singleton close/reopen | Same state/identity/handle; no factory rerun |
| Multi-instance type | Distinct stable instance IDs; duplicate ID rejected |
| Destroy/recreate | Old handle stale; generation advances; destroy exactly once |
| Owner revoke | Draw/focus callbacks unreachable before physical cleanup |
| Owner teardown | All owned instances destroyed before exact type removal/deactivate |
| Partial extension activation failure | No live panel type/instance/callback survives |
| Missing restored type | Bounded placeholder preserves unresolved IDs; no alias/callback |
| Docked panel focus | Associated Local context wins over Workspace |
| ControlTool above focused panel | ControlTool still wins over Local |
| Focus switches A -> B | A context removed, B context active in same frame |
| Panel Local association references another owner | Registration rejected; no partial panel type |
| Focus switch with live ControlTool above Local | Fails closed before shortcut routing; no force-pop/new stale receiver |
| Focus leaves panels | Panel Local context removed; Workspace remains |
| Close/destroy focused panel | Context removed before state destruction |
| Detached panel focus | Same routing path as primary host; no second input system |
| Dock -> detach -> redock | Same stable panel instance/state survives |
| Runtime/Headless | No E3.3 panel/shell callbacks composed |
| E0F interactive boundary | Applicable drag/redock, DPI and minimized checks have real evidence |

## Explicit non-goals

- No E3.4 menu, toolbar or context-menu contribution system.
- No E3.5 console/log/progress/status models.
- No E3.6 layout or shortcut/preferences persistence, disk IO or restore parser.
- No E3.7 demonstration-all-surfaces acceptance extension.
- No asset browser, hierarchy, inspector, console, material editor or other production
  tool beyond the smallest proof fixture needed to validate the infrastructure.
- No document ownership, dirty/save/close prompts, undo/redo, selection or transaction
  model; those belong to later document/tool phases.
- No viewport camera/navigation/gizmo ownership.
- No new application loop, service container, panel service, panel registry, input pump,
  event bus, window system, presentation owner, swap chain or render scheduler.
- No dynamic DLL/plugin loading, directory scan, hot reload or scripting.
- No `assetStreaming` implementation coupling or asset work in panel callbacks.
- No ImGui type in command/domain contracts.
- No UI theme, styling, color, typography or visual-design decisions. When actual theme/
  styling work is reached later, ask the user for the references they requested before
  choosing aesthetics.
- No broad editor folder reorganization or unrelated formatting.

## Definition exit gate

The E3.3 definition closed only after exact-CURRENT review found no unresolved
BLOCKER/HIGH/MEDIUM contradiction in this contract, its parent boundaries and the
CURRENT panel/UI/shell/command ownership. Closing the definition did **not** authorize
P0; the implementing agent stopped until the user separately started that gate.

**Definition closure:** final independent exact-CURRENT review returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. At definition closure E3.3 P0-P6 and E3.4+ were
unauthorized/unstarted. The user's later `start P0` instruction authorized P0 only; P0
subsequently closed at independent exact-CURRENT **0 BLOCKER / 0 HIGH / 0 MEDIUM** with
no production/build/test/native claim. At that historical P0 checkpoint, P1-P6 and E3.4+
were still unauthorized and unstarted; later phase records above supersede that boundary.
