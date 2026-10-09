# E3.4 - menu, toolbar and context-menu composition

## Status and authorization

E3.1, E3.2 and E3.3 are accepted. On 2026-09-22 the user explicitly authorized
**E3.4 definition work**. The definition is now **closed** after exact-CURRENT
Vanguard/RED preflight, status/hygiene reconciliation, correction of all independent
review findings, and fresh post-fix independent architecture, documentation/status and
adversarial reviews at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. This document is the durable
cross-agent definition for E3.4.

Definition work may inspect exact-CURRENT Vanguard and RED source, create the active
planning triplet and reconcile planning/status documentation. It did **not** authorize
E3.4 P0 or production implementation, Premake generation, compilation, executable
tests or native editor execution. The user subsequently explicitly authorized
**E3.4 P0 only**. P0 subsequently closed after exact-CURRENT source/planning preflight
and fresh post-fix independent architecture/test-shape review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user subsequently explicitly authorized
**E3.4 P1 only**; P1 bounded surface identity/placement/registry-core implementation is
now **closed** after source-level hygiene and fresh exact-CURRENT independent review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user subsequently explicitly authorized
**E3.4 P2 only**; P2 extension ownership and same-owner command-reference integration
is now **closed** after scoped source/static hygiene and fresh exact-CURRENT independent
review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P2 performed no Premake generation,
compilation, executable tests or native editor run; broad execution remains deferred to
P6. The user subsequently explicitly authorized **E3.4 P3 only**; P3 primary
MainMenu/MainToolbar composition, command presentation bridging and shell-surface focus
lease work is now **closed** after scoped source/static hygiene and fresh exact-CURRENT
independent review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P3 performed no Premake
generation, compilation, executable tests or native editor run; broad execution remains
deferred to P6. The user subsequently explicitly authorized **E3.4 P4 only**; P4
transactional context-menu composition is now **closed** after scoped source/static
hygiene and fresh exact-CURRENT independent review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
P4 performed no Premake generation, compilation, executable tests or native editor run;
broad execution remains deferred to P6. The user subsequently explicitly authorized
**E3.4 P5 only**. P5 production-shaped native fixture/telemetry/runner completion is now
**closed** after scoped source/static hygiene, correction of the first independent
review's **0 BLOCKER / 2 HIGH / 3 MEDIUM** oracle findings, and fresh exact-CURRENT
post-fix independent review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P5 performed no
Premake generation, compilation, executable tests or native editor execution. The user
subsequently explicitly authorized **E3.4 P6**. P6 is now **closed** after fresh
generation, serialized Debug + Development build/test execution, lifecycle/native
acceptance, final hygiene and fresh exact-CURRENT independent review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. **E3.4 is accepted.** E3.5+ remains
unauthorized/unstarted.

The active definition triplet is
`.planning/e3-4-menu-toolbar-context-composition/{task_plan,findings,progress}.md`.

## Objective

Turn the accepted E3.2 command domain and E3.3 shell/panel foundation into one
extension-owned, declarative command-surface system for:

- the primary editor main menu;
- one primary shell toolbar; and
- bounded panel-scoped context menus.

Extensions contribute stable data that references E3.2 command IDs. They do not
mutate ImGui menu trees, retain toolkit pointers, install arbitrary menu callbacks or
call domain behavior through a second path. E3.4 owns **composition and presentation
of command surfaces**; E3.2 remains the sole authority for command state, contextual
receiver selection, shortcut binding and invocation.

E3.4 is functional shell infrastructure, not the editor's visual-design pass. It does
not choose the final theme, palette, typography, spacing, icon language or pixel-level
menu/toolbar treatment. When actual theme/styling work is reached later, ask the user
for the visual references they requested before choosing aesthetics.

## Read-first authority

Before any E3.4 gate, re-read:

- root `AGENTS.md`;
- [development document map](../README.md);
- [editor foundation roadmap](editor-foundation-study-and-roadmap.md);
- [editor UI integration contract](../designs/editor-ui-integration.md);
- [E3 parent plan](editor-foundation-e3-plan.md);
- [accepted E3.1 plan](editor-foundation-e3-1-plan.md);
- [accepted E3.2 plan](editor-foundation-e3-2-plan.md);
- [accepted E3.3 plan](editor-foundation-e3-3-plan.md);
- `editor/README.md`;
- CURRENT `editor_ui.hpp/.cpp`, private panel/UI implementation and native test
  support;
- CURRENT E3.1 shell owner/registration ledger;
- CURRENT E3.2 command service and shortcut/context tests;
- CURRENT `editor/premake5.lua`.

## Exact-CURRENT starting facts

1. `EditorUiService` is the one editor-workspace UI owner. It owns the ImGui
   context/frame, primary and detached hosts, panel authoring and finalized UI render
   data. E3.4 must not add another UI service, frame pump, window owner or renderer.
2. E3.1 owns one statically compiled extension catalog, one
   `EditorExtensionOwner {extension, activationGeneration}` identity and one bounded
   reverse owned-registration ledger. E3.4 contributions extend that ownership path
   instead of creating a second plugin/contribution lifecycle.
3. E3.2 owns the one `EditorCommandService`. `Query(command)` resolves
   ControlTool -> Local -> Workspace, distinguishes Unsupported/Disabled/Enabled and
   carries the checked state. `Invoke(command)` is the only command behavior path.
4. E3.2 owns effective shortcut bindings. Menu shortcut hints must read that existing
   binding; E3.4 does not maintain a parallel shortcut table.
5. E3.3 publishes panel focus only after successful UI frame authoring and reconciles
   one shell-owned Local command context before committing focused-panel state and
   pending visibility. E3.4 must preserve that transaction boundary.
6. The command service depends on `EditorUiService`. E3.4 must not make
   `EditorUiService` depend on `EditorCommandService` and create a service-graph
   cycle.
7. Main-thread registry mutation is already the shell rule. Extension activation
   occurs outside an active UI frame; panel visibility has a narrow deferred
   BuildingFrame exception. E3.4 contribution topology follows the activation rule.
8. The current shell ledger is bounded to 1024 owned registrations across command,
   context, handler and panel domains. E3.4 surface registrations consume that same
   ledger budget.
9. Runtime and Headless profiles do not compose the editor UI/command/shell domain.
10. CURRENT production UI code contains no main-menu/toolbar/context-menu contribution
    registry. E3.4 therefore adds a new domain **inside the existing UI/shell
    ownership**, not a competing shell.

## RED definition evidence and intentional Vanguard differences

The local RED reference under
`D:/root/R6.Root/Mainline/dev/src/editor/editorCommon/` provides useful ownership
patterns:

- `include/menu.h` models menu data separately from widget implementation and states
  that the data holder has no logic; menu entries carry identity, caption, action
  identity, type and ordering.
- `src/menu.cpp` merges menu trees by stable item identity and sorts children by
  explicit order rather than letting callers mutate a live widget tree arbitrarily.
- `include/toolbar.h` builds toolbar controls from the same menu/action data rather
  than creating an unrelated action model for toolbars.
- `src/dockingMenuBuilder.cpp` contributes view-menu items that reference actions
  and keeps panel visibility behavior in the action/docking owners.
- `include/framework.h` centralizes action, shortcut, focus and active context-menu
  ownership under one editor framework owner.

E3.4 adopts the useful RED principles: declarative data, one command/action route,
shared menu/toolbar command semantics, stable identity and centralized composition.
Vanguard intentionally differs by using fixed-capacity/generation-checked registries,
explicit extension owners, typed E3.2 command IDs, Dear ImGui behind the private UI
owner and deterministic failure rather than fatal assertions or wx widget ownership.
Vanguard does not import RED's serializable menu files, RTTI discovery, raw widget
pointers, mutable global menu objects or callback-based action bypasses.

## Locked ownership model

### One UI-owned surface registry, no new service

E3.4 adds one toolkit-neutral surface registry/core owned by the existing
`ManagedEditorUiService`. It may live in a private implementation unit so focused
tests can compile the exact production state machine without ImGui or GPU owners, but
it is not a new EngineHost service/capability and not a second shell.

The private UI owner remains the only code that translates resolved surface data into
ImGui menu bars, toolbar widgets or popups. Extension-facing descriptors contain no
ImGui types, widget pointers, lambdas, draw callbacks or native-window handles.

### One shell-owned command bridge, no service dependency cycle

`EditorUiService` must not discover or depend on `EditorCommandService`. E3.4 uses
one private removable bridge registered by the existing `EditorShellService`, in the
same architectural spirit as E3.3's panel-focus sink.

The bridge exposes only the narrow operations needed during surface authoring:

- query one E3.2 command through the ordinary current context stack and return one
  presentation snapshot containing its state plus the effective shortcut binding of
  the **same resolved handler/context**;
- invoke one E3.2 command through the ordinary invocation path; and
- validate at registration time that a command belongs to the exact contribution
  owner.

It does not expose handler tables, receiver pointers or a second routing algorithm.
The shell removes the bridge before command/UI dependencies can disappear.

CURRENT `GetShortcutBinding(contextType, command)` is context-specific and therefore
cannot safely be paired with a separate public `Query(command)` plus mutable
`GetStatus().lastContext`: another query would make that status coupling ambiguous,
and performing two independent resolutions could display a shortcut from a different
receiver than the state being shown. P0 must therefore lock one narrow private
E3.2 presentation-query seam that executes the **existing Resolve once** and derives
both `EditorCommandState` and the selected handler's existing effective
`EditorShortcutBinding` before ending that route. This is an observation adapter over
the accepted resolver, not a second resolver or new public command behavior path.

The existing resolver may legitimately succeed with
`EditorCommandStateKind::Unsupported` and **no selected handler** (for example an
Override barrier with no handler, or no applicable Workspace handler). That is a
successful presentation snapshot with an **unbound/empty shortcut**. The presentation
query must not fall through to a lower tier, consult `lastContext`, perform a second
lookup or manufacture a binding when `ResolvedRoute::handler == nullptr`.

### Stable identities, tokens and bounds

Technical identities are distinct from display text:

- `EditorUiSurfaceId {u64 value}` - stable root identity;
- `EditorUiContributionId {u64 value}` - stable entry identity within the global surface
  registry;
- generation-checked surface and contribution tokens for exact physical cleanup.

The initial definition locks these production bounds:

- at most **64 surface roots**;
- at most **512 contributions** across all surfaces;
- at most **8 levels** of menu/submenu nesting;
- at most **128 bytes** for copied technical/display strings used by this domain;
- at most **16 pending context-menu open requests** between committed UI frames.

The lower 1024-entry E3.1 owned-registration ledger remains an independent global
bound and may reject an activation before a domain-local surface bound is reached.

Token generations are nonzero, nonwrapping and stale-safe across slot reuse and a fresh
UI-service instance. Generation exhaustion is an explicit failure before partial state
becomes reachable. Display labels, indices, ImGui IDs and pointers are never accepted
as persistent identity.

### Descriptor and presentation-text rules

A surface-root descriptor contains its stable surface ID and surface kind. An ordinary
contribution descriptor contains at least its stable contribution ID, target surface,
entry kind, parent/placement data and explicit order. A Command contribution also
contains its E3.2 command ID; Command and Submenu contributions contain their own copied
bounded user-facing label. Separator has neither command nor label.

The user-facing label is presentation metadata, not technical identity and not command
behavior. E3.4 never derives visible text from an E3.2 technical command name and never
uses the label as a lookup key. Localization infrastructure is not invented here; a
later localization owner may replace the copied-label representation without changing
surface/command identities. Toolbar Command entries remain text-capable in E3.4;
final iconography is deliberately deferred with the visual-design pass.

### Extension ownership and revoke-before-cleanup

Surface roots and entries registered by an extension are stamped with the exact
`EditorExtensionOwner` and entered into the existing E3.1 owned-registration ledger.
Instances of shell-owned built-in roots/anchors are not extension ledger entries.

Owner revocation makes all owned surface roots/entries and pending context-menu
requests logically unreachable **before** command callbacks or extension private state
can be reached again. Physical cleanup then consumes exact generation tokens in reverse
registration order. An owned context surface cannot be physically removed while owned
children remain live; normal reverse registration order registers a root before its
children and therefore removes children first.

Partial extension activation failure must leave no reachable surface contribution,
pending popup request or stale command path.

## Surface model

### Shell-owned primary surfaces and anchors

E3.4 supplies two process-stable shell roots:

- one **MainMenu** surface authored only on the primary editor host; and
- one **MainToolbar** surface authored only on the primary editor host.

The main menu exposes stable shell-owned structural anchors for the conventional
`File`, `Edit`, `View`, `Tools` and `Help` categories. These are workflow
organization anchors, not a theme/style decision. Visible typography, spacing, color,
icons and exact chrome remain outside E3.4.

The main toolbar exposes stable shell-owned start/end placement anchors. E3.4 does not
define multiple global toolbar rows, draggable toolbar customization or persisted
toolbar placement.

### Owner-defined panel context-menu surfaces

An extension may register a bounded `ContextMenu` surface root for one panel/tool
domain, but the **initial request/lifetime contract is panel-scoped**. The root is a
declaration only; it owns no native popup window and no receiver data. Its command items
route against the ordinary E3.2 context stack when the popup is actually authored.

In the initial contract, an owned ContextMenu may be requested only from a live panel
owned by the **same exact EditorExtensionOwner**. The UI owner can validate both records
without exposing either owner to extension code. Cross-extension popup reuse is
rejected until a later consumer defines an explicit extension dependency/lifetime
contract.

Context surfaces are static composition in E3.4. Arbitrary per-open dynamic item
generation, asset-derived unbounded lists and user-authored menu scripts are deferred
until a real consumer demonstrates a bounded requirement.

### Allowed contribution kinds

The initial surface entry kinds are deliberately small:

- **Command** - references one E3.2 command ID;
- **Submenu** - menu/context-menu structural container with copied display text;
- **Separator** - structural separator with no command.

MainToolbar accepts Command and Separator only. MainMenu and ContextMenu accept all
three. Arbitrary custom toolbar widgets, embedded text fields/combo boxes, extension
draw callbacks and executable menu callbacks are not part of E3.4.

Command entries may expose the E3.2 checked bit as the toolkit's selected/toggled
presentation. E3.4 does not add radio-group or multistate command semantics not already
represented by E3.2.

### Command ownership at registration

An extension-owned Command contribution must reference a live E3.2 command registered
for the **same exact EditorExtensionOwner**. E3.4 adds only the narrow private
registration-time validation seam needed to prove this fact; it does not expose command
registry internals.

This same-owner rule prevents a contribution from retaining a dependency on another
extension's command without an explicit lifecycle relationship. Cross-extension command
contributions are outside the initial contract. A future need must first define exact
dependency and teardown semantics.

The command normally registers before its surface contribution during the same
extension activation. A surface entry that references a missing, revoked or
cross-owner command is rejected transactionally.

## Placement and deterministic ordering

Each contribution carries:

- its stable contribution ID;
- target surface root;
- parent container/anchor;
- optional sibling placement anchor;
- before/after/append relation where applicable; and
- an explicit signed order used to sort contributions sharing the same placement
  bucket.

Initial E3.4 cross-owner placement is intentionally restricted:

- any extension may target shell-owned MainMenu/MainToolbar anchors;
- an extension may target its **own** submenu/context-menu anchors;
- an extension may not target another extension's private anchor in E3.4.

Parent containers must already exist when a contribution registers; forward parent
references are rejected. A sibling anchor may be declared either **Required** or
**FallbackAppend**:

- Required + missing anchor -> reject with no partial registration;
- FallbackAppend + missing anchor -> attach at the parent's deterministic append bucket
  and record a bounded placement diagnostic.

Within one resolved placement bucket, entries sort by explicit order and then stable
contribution ID. Registration order, slot index, pointer value, translated label and
extension activation order are never tie-breakers. Rebuilding the same descriptors
therefore produces the same tree.

Submenu nesting validates the eight-level depth bound and rejects self/ancestor cycles.
Separators are normalized at presentation time so a missing/hidden command does not
produce leading, trailing or duplicate visible separators.

## Command state, shortcut hints and invocation

There is exactly one behavior path:

```text
surface Command contribution
  -> private E3.2 presentation query
       -> existing Resolve(command) exactly once
       -> current Enabled/Disabled/Unsupported + checked state
       -> effective shortcut of that same selected handler/context
  -> present that snapshot
  -> user activation only when Enabled
  -> E3.2 Invoke(command) exactly once
```

No surface contribution stores a handler callback or receiver pointer.

Presentation mapping is deterministic:

- **Enabled** -> visible and activatable; checked state is presented when applicable;
- **Disabled** -> visible but not activatable;
- **Unsupported** -> omitted from ContextMenu, but retained disabled in the persistent
  MainMenu/MainToolbar so shell structure does not jump as focus changes.

Menu command entries receive E3.2's effective shortcut binding in the same atomic
presentation snapshot as command state and present a textual shortcut hint when bound.
E3.4 never infers the context from command-service status, performs a second route to
discover the shortcut, invokes a shortcut through the menu path or owns override
persistence.

State is queried during authoring; it is not cached across frames. Query callbacks
remain subject to E3.2's synchronous/bounded/side-effect-free rule. One click/activation
may produce at most one `Invoke` call. Failed invocation does not fall through to a
second handler and does not automatically retry.

## Frame ordering and context-menu focus semantics

### Shell chrome must not steal the active panel receiver

E3.3's accepted panel focus bridge keys the Local command receiver to the panel that
owns toolkit keyboard focus. E3.4 introduces menus, toolbars and popups that may
temporarily take ImGui navigation/focus even though the user is still issuing a command
**for the previously active panel**. Clearing the Local token merely because shell
chrome became focused would make a menu change command meaning while it is open.

E3.4 therefore adds one narrow private **surface-interaction focus lease** between the
existing UI owner and shell focus bridge:

- it can preserve only the exact already-committed E3.3 panel/receiver; it can never
  install a new panel receiver or invent a command context;
- that panel must still be open, reachable and authored in the current UI frame; the
  lease does not weaken E3.3's rule that an unauthored panel cannot remain current;
- it is active only while MainMenu/MainToolbar/ContextMenu interaction is the reason
  panel keyboard focus is transiently absent;
- a newly focused panel still goes through the ordinary E3.3 reconciliation path and
  supersedes the old receiver;
- panel close, destroy, logical owner revoke, explicit focus invalidation, host loss or
  failed reachability validation overrides the lease and clears the receiver
  synchronously;
- frame abort cannot begin or extend a committed lease;
- when shell-surface interaction ends, ordinary E3.3 focus reconciliation resumes and
  clears the Local token if no valid panel owns focus.

The lease carries no extension callback and does not copy/retain a raw receiver pointer
outside the existing E3.3 panel-focus ownership. It only prevents one transient
shell-chrome focus transition from being misinterpreted as domain receiver loss.
P0 must lock the smallest way to represent this in the existing private focus update;
there is no second focus service or command stack.

The lease deliberately preserves only E3.3's panel Local receiver. E3.4 does not extend
the lifetime of a ControlTool context; a focused control/tool remains responsible for
its own accepted E3.2 push/pop lifetime. A future control-specific context-menu contract
may be added only when a real consumer defines how that ControlTool receiver remains
valid across deferred popup authoring. E3.4 V1 does not claim that behavior.

### Persistent surfaces

MainMenu and MainToolbar are authored once on the primary host during the existing UI
frame. They use the last successfully committed E3.2 context state at that point. A
menu/toolbar interaction may hold that exact receiver through the surface-interaction
lease above, but E3.4 does not move E3.3 focus commit earlier or create a speculative
command route. Same-frame focus changes become visible to persistent surfaces on the
next UI frame, while E3.2 shortcut routing retains its accepted post-focus-commit
ordering.

### Deferred context-menu open request

The initial E3.4 request seam is panel-scoped: `EditorPanelContext` gains one narrow
toolkit-neutral operation equivalent to
`RequestContextMenu(EditorUiSurfaceId, ...)`. The extension supplies the registered
surface identity while the existing UI owner stamps the exact requesting panel, current
host, **current surface generation token/owner**, and request-time popup anchor position.
Extension code does not supply or retain a host handle, surface token, receiver pointer,
ImGui pointer or executable callback.

Request admission validates that the panel and ContextMenu are both reachable and have
the same exact owner before any pending request is recorded.

The popup anchor is captured at request time in finite toolkit-neutral UI logical
coordinates associated with that host. It is transient placement data, never stable
identity or persisted state. The following-frame authoring uses that captured anchor,
not the mouse position at popup-authoring time; host invalidation cancels the request.
P0 locks the smallest neutral coordinate representation already compatible with current
UI/platform conventions rather than creating a general geometry framework.

Because E3.3 commits a new panel Local context only after successful frame authoring,
E3.4 intentionally treats a context-menu open as a **transactional next-frame
request**:

1. the right-click/current panel tool updates its ordinary state and records a bounded
   open request through `EditorPanelContext`, with UI privately capturing host and
   anchor position;
2. successful `EndFrame` and E3.3 focus reconciliation must commit the **same exact
   requesting panel and host** as the resulting focused-panel snapshot; only then is
   the request committed;
3. the following UI frame opens/authors the popup on the requesting host and queries
   commands through the now-committed E3.2 context; while that popup owns transient
   toolkit navigation/focus, the surface-interaction lease preserves that exact panel
   Local receiver.

An aborted frame discards the uncommitted request. A focus-sink/reconciliation failure,
no committed panel focus, or a committed panel/host different from the request stamp
also discards that request; it must never be carried forward to query against B or
Workspace after being requested from A. Closing/destroying the requesting panel,
unregistering the context surface or owner revocation cancels a committed request
before authoring. A later surface registered with the same stable ID cannot inherit the
old request because the internally stamped generation token must still match.

Popup arbitration is explicit: one panel may have at most **one distinct pending
ContextMenu request per UI frame**. Repeating the exact same surface/host/anchor request
coalesces into that record. A second request from the same panel/frame that differs by
surface or anchor fails with an explicit request-conflict failure and leaves the first
record unchanged. Requests from other panels may occupy other bounded queue entries,
but only the request whose exact panel/host becomes the committed E3.3 focus can commit;
the others are discarded at frame commit. Queue-capacity overflow also fails explicitly.
No implementation may choose a popup by slot index, unordered traversal, later cursor
state or toolkit popup replacement behavior.

This one-frame transactional handoff is preferred over adding a second
`QueryForReceiver` command route or exposing raw receiver pointers to the menu system.

## Host and toolkit boundary

- MainMenu and MainToolbar are authored exactly once on the primary editor host.
  Detached ImGui platform hosts do not receive duplicate global bars.
- ContextMenu is authored on the host associated with its committed requesting panel.
- Menu/toolbar/popup toolkit focus is shell chrome and must not by itself replace the
  active panel command receiver; the bounded private focus lease above owns that
  distinction.
- All final ImGui `BeginMenu`/`MenuItem`/popup/toolbar construction stays in the
  existing private UI implementation.
- Surfaces never create a native window, presentation output, render viewport, swap
  chain or renderer graph.
- Finalized surface draw data follows the same retained multi-host E0 composition path
  as ordinary panel UI.
- E3.4 adds no new input pump. Toolkit hit-testing/clicks remain part of the existing
  UI frame; keyboard shortcut arbitration remains E3.2.

## Failure and bounded diagnostics model

Stable surface failures must cover at least:

- invalid surface/contribution identity;
- invalid descriptor or entry kind for the target surface;
- duplicate surface or contribution ID;
- stale/invalid surface or contribution token;
- capacity or generation exhaustion;
- missing/invalid parent;
- required placement anchor missing;
- illegal cross-owner/private anchor reference;
- hierarchy depth/cycle violation;
- missing, revoked or cross-owner command reference;
- owner revoked;
- wrong thread or wrong mutation/UI phase;
- context-menu request overflow or stale requesting panel/host;
- conflicting second context-menu request from the same panel/frame;
- cross-owner panel/context-menu request;
- invalid/non-finite context-menu popup anchor;
- command query failure;
- command invocation failure;
- cleanup/lifecycle failure.

Registry/placement failures are transactional and leave no partially reachable entry.
Per-frame command query/invocation failures do **not** corrupt the surface tree or abort
the entire native UI frame merely because one extension action failed. The affected
item becomes non-activatable for that authoring pass and the surface domain records a
bounded diagnostic/status entry.

E3.4 stores only bounded current/last-failure information and a bounded count of
suppressed/repeated diagnostics. It does not implement E3.5's console/log/progress UI
or retain an unbounded history.

## Threading, reentrancy and lifecycle

- Surface root/contribution registration, unregistration and owner cleanup are
  MainThread owned and occur outside `EditorUiService::BuildingFrame`.
- Direct wrong-thread mutation fails before state change.
- Surface authoring occurs only inside the existing UI frame owner.
- Registry topology mutation from command query/invoke or panel authoring is rejected.
- Context-menu open request is the narrow BuildingFrame-safe operation; it records
  bounded pending state and never mutates the committed contribution tree.
- Command Query/Invoke reentrancy remains governed entirely by E3.2.
- Logical surface revoke happens before command owner revoke/private-state destruction
  so a stale visible item cannot resolve an owner that is already dead.
- Pending popup requests are cleared on panel invalidation, owner revoke, UI quiesce and
  shell shutdown.
- Surface-interaction focus leases are cleared on panel/host invalidation, owner revoke,
  UI abort/quiesce/shutdown and shell teardown; they never outlive the exact committed
  panel receiver they preserve.
- The private command bridge is detached before the command service or UI service can
  quiesce underneath it.
- Shell quiesce must be testable as one ordered boundary: stop surface callback
  admission, clear pending/uncommitted popups and any committed focus lease, detach the
  private command bridge, then allow command/UI dependency quiesce and extension
  private-state teardown. No surface query/invoke/focus callback may run after bridge
  detachment or against dead shell state.
- Runtime/Headless profiles compose none of the surface domain.

## Focused validation strategy

### GPU/toolkit-free `editorSurfaceTests`

E3.4 should add one focused ConsoleApp test target that compiles the same production
surface registry/composition core used by `ManagedEditorUiService`. The target must
not initialize ImGui, D3D12, PresentationService, native windows or asset streaming.

At minimum it proves:

- root/contribution identity, duplicate rejection, capacities and stale tokens;
- nonwrapping generation exhaustion with no partial state;
- MainMenu/MainToolbar built-in roots/anchors are deterministic;
- command/submenu/separator kind validation;
- missing Required anchor rejection and FallbackAppend deterministic placement;
- stable order independent of registration slot/address;
- same-owner nested anchor acceptance and cross-owner private anchor rejection;
- hierarchy depth/cycle rejection;
- owner revoke makes entries unreachable before physical cleanup;
- pending context-menu request commit/discard/cancel/coalesce/overflow;
- one distinct request per panel/frame: exact duplicate coalesces, differing second
  request fails without replacing the first;
- same-owner panel/context-surface admission and cross-owner rejection;
- surface token generation change cancels a deferred popup rather than retargeting by
  stable ID;
- pending request commits only when the successfully reconciled focus snapshot matches
  the exact requesting panel/host; focus failure/mismatch discards it;
- deferred popup uses the request-time host/anchor rather than a following-frame cursor
  position;
- shell-surface focus lease preserves only an existing live panel receiver and cannot
  defeat close/destroy/revoke/host invalidation;
- hidden Unsupported context entries do not leave bad separators;
- failure paths leave no partial tree mutation.

### Retained shell/command regressions

`editorShellTests` remains the owner-ledger/rollback oracle. It gains only focused
surface cases needed to prove:

- owner-stamped root/entry registration;
- same-owner command validation;
- reverse cleanup and partial activation rollback;
- logical surface revoke before command/private-state teardown;
- quiesce clears pending popup/focus-lease state and detaches the surface-command bridge
  before command/UI dependency teardown, with no callback into dead shell state;
- owned-registration capacity interaction.

`editorCommandTests` remains the routing oracle. E3.4 adds only the smallest bridge
coverage needed to prove the private presentation query returns state and effective
binding from one exact accepted E3.2 resolution, and that surface Invoke reaches the
ordinary invocation path without creating a second receiver algorithm.
It must include the valid Unsupported/no-handler route and prove that presentation
returns an empty/unbound shortcut without falling through or resolving again.

### Native/editor integration evidence

Final E3.4 acceptance must separately prove the real UI integration:

- one primary main menu and one primary toolbar, with no duplicates on detached hosts;
- deterministic menu/submenu/toolbar ordering;
- Enabled/Disabled/Unsupported/check state presentation as defined above;
- effective shortcut hint changes track the E3.2 binding;
- menu and toolbar activation invoke the same command handler exactly once;
- opening/holding a main menu or interacting with the toolbar does not silently replace
  the active panel command receiver merely because shell chrome takes toolkit focus;
- a context menu requested from a panel opens on the intended host after the
  transactional focus commit, at the request-time anchor, and routes to the intended
  Local receiver;
- a request whose post-EndFrame committed focus is none/different, or whose focus
  reconciliation fails, produces no next-frame popup;
- the same context-menu path works from a detached panel host;
- close/destroy/revoke before the deferred popup frame cancels the request safely;
- extension rollback/removal leaves no visible/stale surface callbacks;
- ordinary panel docking/focus/shortcut/native-host behavior remains intact.

Automated lifecycle evidence and any real mouse/focus interaction remain separate
claims. Before launching automation that intentionally moves the user's mouse, changes
desktop focus or drags native windows, warn the user first.

## Expected implementation file/target ledger

P0 must refresh this against exact CURRENT before implementation. The intended narrow
set is:

### Existing production files likely extended

- `editor/framework/include/vanguard/editor/editor_ui.hpp` for the tiny neutral
  surface identity and panel-scoped context-menu request seam;
- `editor/framework/src/editor_ui.cpp` for one UI-owned surface core, primary-host
  authoring and private command-bridge/context-popup integration;
- `editor/private/vanguard/editor/editor_shell_service.hpp` for owner-stamped
  activation registration methods;
- `editor/extensions/editor_shell_service.cpp` for admission, owned-ledger,
  revoke/cleanup and private command bridge;
- `editor/private/vanguard/editor/editor_commands.hpp` and
  `editor/commands/editor_command_service.cpp` only for the narrow same-owner
  command-validation seam and one atomic private presentation-query seam over the
  existing resolver;
- `editor/tests/README.md`;
- `editor/premake5.lua`.

### Narrow new private production files permitted if P0 confirms this remains cleaner

- `editor/private/vanguard/editor/editor_surfaces.hpp`;
- `editor/surfaces/editor_surface_registry.cpp`.

These files implement the one UI-owned surface domain; their existence does not
authorize `EditorSurfaceServiceId`, another frame participant or a parallel command
router.

### Focused tests/support likely touched

- `editor/tests/editor_surface_tests.cpp`;
- `editor/tests/editor_shell_tests.cpp`;
- `editor/tests/editor_command_tests.cpp`;
- existing phase-neutral files under `editor/tests/support/` only where real native
  surface integration evidence requires them.

Test-only fixtures remain under `editor/tests/**`; no E3.4/P-number/test-mode naming
or scaffolding may leak into production paths or runtime contracts.

Any materially larger production file set requires a P0 finding explaining why the
accepted owners cannot satisfy the contract.

## Ordered implementation gates

### P0 - exact-CURRENT architecture and test-shape lock

Source/planning review only. Re-read CURRENT UI, shell ledger, command service,
panel-focus commit ordering, Premake/test support and RED menu/action evidence. Verify
the definition-locked bounds against exact CURRENT, then lock the minimal concrete
API/file spelling, command-bridge location, context-menu transaction point and
native-test shape without silently changing the contract. Obtain fresh independent
exact-CURRENT review.
No production C++, Premake generation, build, executable test or native editor run.

### P1 - bounded surface identity, placement and registry core

Implement the one toolkit-free production registry/core, built-in roots/anchors,
generation-checked tokens, copied descriptors, deterministic ordering, structural
validation, owner reachability and transactional pending context-menu request state.
Add the focused GPU/toolkit-free production-core test target. Broad build/test
execution remains deferred to P6 under the current editor workflow.

### P2 - E3.1 extension ownership and E3.2 command-reference validation

Extend `EditorExtensionActivationContext` and the one owned-registration ledger with
surface root/contribution registration. Add exact same-owner command validation, logical
surface revoke and reverse physical cleanup. Prove source-level rollback/teardown
ordering and retain no callback or receiver pointer in a contribution.

### P3 - primary main menu and toolbar composition

Wire one private removable shell-command bridge into the existing UI owner. Author
MainMenu/MainToolbar exactly once on the primary host, map E3.2 state/check/shortcut
binding to presentation and invoke only through E3.2. Add the narrow surface-interaction
focus lease so shell chrome cannot silently clear the active panel receiver. Keep ImGui
types private and do not alter native host/presentation ownership.

### P4 - transactional context-menu composition

Add bounded panel/tool context-menu open requests, commit/discard them with the accepted
E3.3 focus transaction and author the popup on the requesting host on the following UI
frame. Prove no speculative receiver route, stale panel callback or cross-owner command
dependency is introduced.

### P5 - production-shaped integration and acceptance fixture completion

Integrate a phase-neutral test extension/fixture under `editor/tests/support/` that
uses real extension, command, panel and surface paths to exercise persistent and
contextual surfaces. Finish source-level regression/hygiene review and the exact P6
execution matrix. Do not add production test modes or test-only shell hooks.

### P6 - comprehensive validation, documentation and acceptance

Run fresh Premake VS2022 generation. If exact-current unrelated worktree failures block
root generation, use the established isolated/editor-focused generation route only
when it preserves the intended target graph and record that limitation explicitly.
Build serialized `/m:1` Debug and Development focused targets, run
`editorSurfaceTests`, `editorShellTests`, `editorCommandTests`, retained panel/UI
regressions and applicable lifecycle/native integration evidence, then build/link the
real production editor.

Run scoped/full `git diff --check`, final-newline/untracked checks, target-membership
and production/test-isolation scans. Search explicitly for duplicate UI/surface
services, alternate command routing, ImGui leakage into domain descriptors, dynamic
plugin loading, E3.5 diagnostics/progress implementation, E3.6 persistence and
theme/styling work.

Obtain a fresh independent exact-CURRENT architecture/acceptance review and resolve
every BLOCKER/HIGH/MEDIUM before marking E3.4 accepted. Reconcile the parent E3 plan,
roadmap, development map and active planning triplet, then stop. E3.5+ remain
unauthorized until explicitly started.

## Minimum acceptance matrix

| Scenario | Required outcome |
|---|---|
| Duplicate/invalid surface or entry | Explicit rejection; no partial tree mutation |
| Slot/service reuse | Old surface/contribution tokens remain stale |
| Generation exhaustion | Explicit failure; no wraparound alias |
| Wrong-thread/topology mutation during UI frame | Rejected with no committed change |
| MainMenu/MainToolbar authoring | Exactly once on primary host; none duplicated on detached hosts |
| Same-owner command contribution | Registers and resolves through E3.2 |
| Missing/cross-owner command | Registration rejected transactionally |
| Required anchor missing | Registration rejected |
| Fallback anchor missing | Deterministic append placement + bounded diagnostic |
| Same-order contributions | Stable contribution ID determines deterministic order |
| Cross-owner private anchor | Rejected |
| Submenu depth/cycle invalid | Rejected without reachable partial nodes |
| Enabled command | Visible/activatable; one Invoke on activation |
| Disabled command | Visible, disabled, never invoked |
| Unsupported persistent command | Visible disabled; persistent structure stable |
| Unsupported context command | Omitted; separators normalize correctly |
| Checked command | Current E3.2 checked state is presented |
| Effective shortcut changed | Same resolved presentation snapshot changes the menu hint; no second route/store |
| Unsupported route with no selected handler | Successful Unsupported snapshot with empty/unbound shortcut; no fallback/second lookup |
| Main-menu/toolbar transient focus | Existing live panel Local receiver is preserved only for shell-surface interaction |
| Context request from newly focused panel | Opens next frame after successful focus commit and routes Local receiver |
| Cross-owner panel/context surface | Request rejected with no pending popup |
| Same panel repeats exact popup request in one frame | Coalesces to one pending record |
| Same panel requests a different popup/anchor in one frame | Second request fails explicitly; first pending record is unchanged |
| Deferred context-menu position | Opens at the request-time host/anchor, not the later cursor position |
| Context surface removed/recreated with same stable ID | Old deferred request is stale/cancelled by generation token |
| Requested panel/host != committed focus, or focus reconciliation fails | Pending request discarded; no popup/query against another receiver |
| Menu/popup open across frames | Exact live panel receiver remains stable until surface interaction ends or invalidation wins |
| UI frame abort | Uncommitted context request discarded |
| Panel close/destroy or owner revoke before popup | Request cancelled; no stale callback |
| Owner revoke | Surface unreachable before command/private-state teardown |
| Shell quiesce/shutdown | Popup/lease state clears and command bridge detaches before dependency/private-state teardown |
| Partial extension activation failure | No visible root/item/popup survives rollback |
| Runtime/Headless profile | No surface registry/composer callbacks active |

## Explicit non-goals and stop boundary

- No UI theme, palette, typography, spacing, final iconography or pixel-perfect visual
  design. **When that later styling stage begins, ask the user for their requested
  visual references before choosing aesthetics.**
- No E3.5 diagnostics console, progress or status presentation.
- No E3.6 layout/menu/toolbar persistence or user customization files.
- No E3.7 demonstration-extension closure beyond the narrow phase-neutral validation
  fixture needed to prove E3.4 itself.
- No E4 Asset Browser implementation or asset/build/cache work in UI callbacks.
- No document/undo/inspector/hierarchy/viewport manipulation behavior.
- No arbitrary custom toolbar widgets, extension draw callbacks, script-generated menus
  or unbounded dynamic context lists.
- No deferred ControlTool-specific context-menu receiver lifetime; E3.4 V1 popup
  requests preserve the owning panel's accepted Local receiver only.
- No alternate command registry, receiver stack, shortcut store or invocation callback
  in the surface domain.
- No second UI service, frame participant, input pump, window system, swap chain,
  presentation owner or renderer.
- No dynamic native plugin loading, hot unload ABI, scripting or marketplace.
- No direct ImGui types in extension/domain surface descriptors.
- No phase/task/test identifiers or proof scaffolding in production source names,
  symbols, persistent formats or runtime contracts.

## Definition exit gate

Definition closes only after:

1. exact-CURRENT Vanguard and RED ownership evidence is recorded;
2. the ownership/ordering/command/focus/lifecycle contract above is internally
   consistent with accepted E3.1-E3.3;
3. the active planning triplet exists;
4. parent/roadmap/development-map status is reconciled;
5. scoped whitespace/link/status hygiene passes; and
6. fresh independent exact-CURRENT definition review reports
   **0 BLOCKER / 0 HIGH / 0 MEDIUM** after any fixes.

Definition closure makes no implementation/build/test/native claim and does not
authorize P0.

### Definition closure evidence

- Documentation/status review initially found one MEDIUM: predecessor E3.1/E3.2
  planning-triplet closure text sounded current after later phases had advanced. Those
  checkpoints were relabeled explicitly historical and paired with the superseding
  current boundary; post-fix review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Architecture review initially found three MEDIUM contract gaps: the valid
  Unsupported/no-selected-handler shortcut case, exact deferred-popup commit coupling
  to reconciled panel/host focus, and explicit bridge/lease teardown proof. All three
  were locked above; post-fix architecture review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Adversarial definition review then found one MEDIUM in same-panel/frame deferred
  popup arbitration. The contract now permits one distinct request per panel/frame,
  coalesces exact duplicates and explicitly rejects a differing second request without
  replacing the first. Post-fix adversarial review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Scoped link, status, trailing-whitespace and final-newline checks are clean.
- No production C++, Premake generation, compilation, executable test or native editor
  execution was performed or claimed during definition work.

**Definition status: CLOSED. E3.4 P0 subsequently closed after exact-CURRENT
source/planning preflight and fresh post-fix independent architecture/test-shape review
at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently authorized P1 only; P1 is now
closed after source-level hygiene and fresh exact-CURRENT independent review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently authorized P2 only; P2 is now
closed after scoped source/static hygiene and fresh exact-CURRENT independent review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently authorized P3 only; P3 is now
closed after scoped source/static hygiene and fresh exact-CURRENT independent review at
0 BLOCKER / 0 HIGH / 0 MEDIUM. At that P3 closure checkpoint P4-P6 and E3.5+ remained
unauthorized/unstarted; P4 was explicitly authorized afterward and is now closed.**

## P0 exact-CURRENT architecture/test-shape lock

**Current status:** P0 CLOSED. This section records only source/planning decisions. P0
made no production C++, Premake generation, compilation, executable-test or native
editor claim and did not authorize P1.

### Exact-CURRENT source facts revalidated for P0

- `editor/framework/include/vanguard/editor/editor_ui.hpp` remains command-domain
  neutral: it does not include the private E3.2 command header. P1-P4 must preserve that
  layering instead of making the framework UI contract depend on command
  implementation types.
- `ManagedEditorUiService` still owns the one UI frame, panel authoring loop, primary
  dockspace, detached-host platform and the post-`EndFrame` `CommitPanelFrame`
  transaction. This remains the sole composition point for E3.4.
- `EditorPanelRegistryCore::ResolveFocusReceiver` already treats a BuildingFrame
  pending close as effectively closed before visibility commit. The deferred-popup
  transaction can therefore reuse `CommitPanelFrame`: resolve focus, dispatch the
  accepted E3.3 focus update, commit panel visibility, then commit/discard the exact
  E3.4 popup request without adding a second frame participant.
- `EditorCommandService::Resolve` remains the single ControlTool -> Local -> Workspace
  route and already has the valid Override/no-handler Unsupported result.
  `CommandRecord` already stores the exact `EditorExtensionOwner`, so same-owner
  surface-command validation needs only a private observation seam, not a new registry.
- `EditorShellService` already depends on UI and command services, owns the 1024-entry
  reverse registration ledger, installs the E3.3 focus bridge during initialize, closes
  registration admission before teardown and deactivates extensions before dependency
  shutdown. E3.4 extends this owner rather than the service graph.
- `EditorPanelRegistryCore` stores the exact owner in both type and instance records,
  while its private authoring view currently omits that owner. P4 therefore adds exactly
  `EditorExtensionOwner owner` to private `EditorPanelAuthoringView`; the registry
  fills it from the reachable `PanelRecord` already being visited. No public
  panel-owner lookup/API is added.
- `EditorPanelContext` is a small copyable value. A popup request must therefore
  validate the **currently authored panel** in `ManagedEditorUiService`, not trust a
  copied/stale context's panel handle alone.
- The private ImGui platform can resolve the current panel window to an
  `EditorHostWindowHandle` through `GetCurrentViewportHost`. No new public/native
  host handle needs to enter the panel request API.
- CURRENT editor/native lifecycle targets compile explicit
  `commands/**.cpp`, `extensions/**.cpp` and `panels/**.cpp` groups but have no
  `surfaces/**.cpp` group. P1 must add the surface production group consistently to
  the editor and both full-production lifecycle/native integration targets.

### P0-locked public/private API placement

The public framework header stays neutral and gains only the minimum UI-domain
identity/request vocabulary:

- `EditorUiSurfaceId {u64 value}`;
- `EditorUiContributionId {u64 value}`;
- invalid constants and the definition-locked 64-root / 512-contribution /
  8-level / 128-byte / 16-request bounds;
- the surface-related `EditorUiFailureCode` values required for deterministic
  request/registry failures; and
- `EditorPanelContext::RequestContextMenu(EditorUiSurfaceId,
  EditorUiFailure* = nullptr)`.

P1 appends, without renumbering existing UI failures, only the distinct surface codes
that cannot be represented precisely by an existing code:

- `MissingAnchor`;
- `OwnerMismatch`;
- `HierarchyViolation`;
- `SurfaceRequestConflict`; and
- `SurfaceCommandFailure`.

Existing `InvalidDescriptor`, `InvalidHandle`, `DuplicateIdentity`,
`CapacityExceeded`, `WrongPhase`, `WrongThread`, `GenerationExhausted` and
`OwnerRevoked` continue to cover invalid descriptors/non-finite anchors, stale tokens
or panel/host stamps, duplicate IDs, request/domain overflow, illegal mutation phase,
thread violations, generation exhaustion and revoked owners respectively.
`EditorUiFailure` appends neutral `surface` and `contribution` identity fields so
these failures remain attributable without exposing command handlers or owner pointers.

`RequestContextMenu` takes **no host, owner, surface token or popup position from the
extension**. Its `editor_ui.cpp` implementation is valid only while the exact
`EditorPanelContext::GetPanel()` is the panel currently being authored in
`BuildingFrame`. The UI owner privately stamps the current host, current frame,
request-time finite ImGui desktop/logical position and live surface
token/generation/owner.

Command-bearing surface declarations remain private in
`editor/private/vanguard/editor/editor_surfaces.hpp`, which may include
`editor_commands.hpp` without polluting `editor_ui.hpp`. P1 locks the concrete
private vocabulary to:

- `EditorUiSurfaceKind {MainMenu, MainToolbar, ContextMenu}`;
- `EditorUiContributionKind {Command, Submenu, Separator}`;
- generation-checked `EditorUiSurfaceToken` and `EditorUiContributionToken`;
- root/contribution descriptors with stable IDs, copied label, target/parent/sibling
  anchors, append/before/after relation, Required/FallbackAppend missing-anchor policy,
  explicit signed order and optional E3.2 command ID as appropriate; and
- one private `EditorSurfaceRegistryCore` plus process-static nonwrapping
  surface/contribution generation source.

The MainMenu/MainToolbar roots, File/Edit/View/Tools/Help menu anchors and toolbar
start/end anchors are core-owned built-ins. They consume the same 64/512 domain
capacities but never consume the E3.1 extension-owned ledger. Extensions may register
ContextMenu roots; they may target shell-owned anchors or their own private anchors,
never another extension's private anchor.

The extension-facing activation registrar does not expose a generic way to create a
second MainMenu/MainToolbar root. P2 adds exactly:

- `EditorExtensionActivationContext::RegisterContextMenuSurface(...)`; and
- `EditorExtensionActivationContext::RegisterSurfaceContribution(...)`.

Both return exact generation tokens for the one existing owned-registration ledger.
The shell validates the live owner, global ledger bound and same-owner command
reference before forwarding to private owner-stamped UI hooks. The private UI hooks are
declared with forward-declared `detail` surface types in `editor_ui.hpp`, so the
public framework header still does not include `editor_surfaces.hpp` or
`editor_commands.hpp`.

### P0-locked command bridge

No UI -> command service dependency is added. One removable private bridge definition
belongs in `editor_surfaces.hpp` and is registered into `EditorUiService` by the
existing shell during `OnInitialize`, next to the panel-focus bridge.

Bridge registration is MainThread + Ready-only, accepts exactly one live bridge and
returns a generation-checked private token. Duplicate/stale registration/removal fails
without replacing another bridge. The shell stores that exact token. If shell
`OnInitialize` successfully installs one of the panel-focus/surface-command bridges
and installing the other fails, it unregisters the first before returning failure; a
partially initialized shell may not leave any UI callback behind.

The bridge has exactly three logical operations:

1. `QueryPresentation(command)` - a new private E3.2 seam that executes the existing
   `Resolve` exactly once and returns `EditorCommandState` plus the effective
   `EditorShortcutBinding` of that same selected handler/context;
2. `Invoke(command)` - delegates to the ordinary public E3.2 `Invoke`; and
3. `ValidateCommandOwner(owner, command)` - confirms a reachable command's existing
   `CommandRecord::owner` exactly matches the contribution owner.

The valid Resolve result `Unsupported + handler == nullptr` returns a successful
presentation with `binding.bound == false` and an empty chord. It never falls through
or performs another resolution. UI rendering receives only the presentation result; it
never sees handler/receiver tables.

### P0-locked registry/request transaction

The production core lives in the definition-permitted files:

- `editor/private/vanguard/editor/editor_surfaces.hpp`;
- `editor/surfaces/editor_surface_registry.cpp`.

`ManagedEditorUiService` owns exactly one instance by composition. Extension
registration reaches it only through private `EditorUiService` owner-stamped methods
used by `EditorExtensionRegistrationAccess`; no public surface-registry accessor and
no `EditorSurfaceServiceId` are added.

During panel authoring the UI owner sets a short-lived
`currentAuthoringPanel/currentAuthoringOwner` stamp immediately around the ordinary
panel draw callback, then clears it before leaving that callback path. A copied or
fabricated `EditorPanelContext` is accepted only when its handle exactly equals that
active stamp while the UI is BuildingFrame. The UI then validates same-owner
panel/ContextMenu admission, captures current host/position and asks the surface core to
queue an already-stamped request. Exact duplicate requests coalesce; a differing second
surface/anchor request for the same panel/frame fails without replacing the first.

After successful `EndFrame`, `CommitPanelFrame` remains the only transaction point:

1. resolve the candidate panel against pending visibility;
2. dispatch the existing E3.3 focus update;
3. on failure discard panel visibility plus pending E3.4 request state;
4. on success commit panel visibility/focus and commit at most the request whose exact
   panel/host matches that focus; discard every other frame request.

Every existing `EndFrame` failure path must discard pending surface requests at the
same point it discards pending panel visibility. `AbortFrame` also discards them while
BuildingFrame. This is required because current `EndFrame` returns the UI state to
Ready before the outer frame participant calls `AbortFrame`, so the outer abort alone
cannot be relied on to clear a failed frame's request.

The internally stamped surface generation must still match. A closed/destroyed/revoked
panel, stale host, revoked/replaced surface or focus mismatch cancels the request.

The committed popup is authored **inside the requesting panel's existing private
top-level ImGui window wrapper on the following frame**. This naturally keeps it on the
correct detached/primary host and avoids adding a reverse host->ImGui-viewport API. The
presenter revalidates that the current host still equals the request stamp.

### P0-locked shell-surface focus lease

The lease is represented in the existing UI focus-commit path, not as a new service or
command context. Surface authoring records only whether shell chrome is actively
holding transient UI focus. `DrawPanelForAuthoring` separately records whether the
already-committed panel was still open/reachable and actually authored in the current
frame.

When no new candidate panel has focus, the lease may reuse only that exact previous
`m_panelFocus` after `ResolveFocusReceiver` revalidates it. A newly focused panel
still wins normally. Pending close, destroy, owner revoke, host loss, frame abort or
failed reachability defeats the lease. No ControlTool token is preserved.

### P0-locked ownership and teardown order

P2 adds `SurfaceRoot` and `SurfaceContribution` to the existing
`OwnedRegistrationKind`. Normal activation order is command/context/handler first,
then a ContextMenu root when needed, then contributions, so reverse physical cleanup
removes contributions before their root and before command records.

`RevokeOwner` changes its logical order to:

1. surface owner revoke/cancel pending popup/focus-lease reachability;
2. existing command owner revoke;
3. existing panel owner revoke/invalidation.

This makes a surface unreachable before its referenced command or panel private state
can die.

Shell quiesce/stop/shutdown first closes new registration/surface callback admission,
clears pending/committed popup + focus-lease state and unregisters the surface command
bridge, then detaches the existing panel-focus bridge, then performs reverse extension
deactivation/physical registration cleanup. `EditorUiService::OnQuiesce` must fail
closed if either private bridge is still attached.

### P0-locked file and target ledger

Production files expected to change across P1-P4:

- `editor/framework/include/vanguard/editor/editor_ui.hpp` - neutral IDs, bounds,
  request entry point, failure additions and private opaque surface hooks only;
- `editor/framework/src/editor_ui.cpp` - one composed surface core, current-authoring
  stamp, primary-surface/context-popup authoring, transaction/focus lease and bridge
  hooks;
- `editor/private/vanguard/editor/editor_surfaces.hpp` - new private descriptors,
  tokens, bridge and production core;
- `editor/surfaces/editor_surface_registry.cpp` - new toolkit/GPU-free production
  core;
- `editor/private/vanguard/editor/editor_panels.hpp` and
  `editor/panels/editor_panel_registry.cpp` - add only
  `EditorPanelAuthoringView::owner` and populate it from the visited reachable panel;
- `editor/private/vanguard/editor/editor_commands.hpp` and
  `editor/commands/editor_command_service.cpp` - private atomic presentation and
  same-owner command-validation seams over existing records/resolver;
- `editor/private/vanguard/editor/editor_shell_service.hpp` and
  `editor/extensions/editor_shell_service.cpp` - activation registration methods,
  bridge install/remove, ledger kinds and revoke/cleanup order;
- `editor/premake5.lua` - surface production group plus focused target membership; and
- `editor/tests/README.md`.

The two panel-core files are the only P0 addition beyond the definition's expected
production ledger. Exact CURRENT requires them solely because panel ownership is
private to `EditorPanelRegistryCore` and the popup request must not expose owner
identity publicly.

Focused test files:

- new `editor/tests/editor_surface_tests.cpp`;
- existing `editor/tests/editor_command_tests.cpp`;
- existing `editor/tests/editor_shell_tests.cpp`;
- existing `editor/tests/editor_panel_tests.cpp` only for the narrow authoring-owner
  view regression if needed; and
- existing phase-neutral `editor/tests/support/native_ui_integration_test_*` at
  P5/P6 for real menu/toolbar/context-popup integration.

The new `editorSurfaceTests` target compiles
`editor_surface_tests.cpp`, `editor_surface_registry.cpp`,
`editor_surfaces.hpp`, `editor_extension.hpp`, `editor_commands.hpp`,
`editor_ui.hpp` and `editor_service_ids.hpp`. It follows the existing
`editorPanelTests` style and links only non-GPU support needed by those production
headers/core (containers/concurrency/memory/diagnostics/system compatibility). It does
**not** compile `editor_ui.cpp`, ImGui, rendering, RHI, window or platform
implementations.

The focused surface target explicitly owns wrong-thread and Ready-vs-BuildingFrame
topology rejection, copied-label validation (nonempty and NUL-terminated before the
128-byte storage bound, with overlong input rejected rather than silently truncated),
all fixed capacities/generation exhaustion, hierarchy/placement ordering and pending
request arbitration. It also injects an internal stamped request with a non-finite
anchor and proves explicit rejection with no pending-state mutation, and uses a fake
private presentation bridge to prove a query failure makes only that authoring item
non-activatable, records bounded surface status/diagnostics and leaves the committed
tree intact.

`editorCommandTests` owns atomic Resolve-once presentation/same-owner command seams,
Unsupported/no-handler empty binding and ordinary Invoke rejection/failure mapping.
`editorShellTests` owns ledger/revoke/rollback/bridge teardown. Once P2 gives the real
shell surface registrations, that target also compiles
`surfaces/editor_surface_registry.cpp` + `editor_surfaces.hpp`, and its existing
`FixtureUiService` composes the production surface core behind the same private UI
hooks; it does not grow a test-only registration algorithm. Native support owns real
ImGui/host presentation and proves a real failed/rejected command activation does not
terminate the UI frame or poison later surface interaction.

The production `editor`, `editorApplicationLifecycleIntegrationTest` and
`editorNativeUiIntegrationTest` target source wildcards all gain
`surfaces/**.cpp`; the production editor also gains a Surfaces vpath. The native test
continues to use its existing test-only compiled extension rather than any production
test mode.

### P0-locked gate/file ownership

- **P1** owns the neutral IDs/bounds/failure attribution in `editor_ui.hpp`, the new
  private `editor_surfaces.hpp` + `editor_surface_registry.cpp`, one composed core in
  `ManagedEditorUiService`, process-stale-safe generations, structural placement/
  request state and `editorSurfaceTests`/Premake production-group wiring. It does not
  yet install the command bridge or expose extension registration.
- **P2** owns private same-owner command validation, activation-context
  `RegisterContextMenuSurface`/`RegisterSurfaceContribution`, the two new owned-
  ledger kinds, logical surface revoke and reverse cleanup. `editorShellTests` gains
  the same production core behind its existing fake UI service.
- **P3** owns the Resolve-once presentation query, removable shell->UI command bridge,
  primary-only MainMenu/MainToolbar authoring, shortcut/check/state projection,
  E3.2-only Invoke path and persistent shell-surface focus lease.
- **P4** owns `EditorPanelContext::RequestContextMenu`,
  `EditorPanelAuthoringView::owner`, current-authoring-panel validation, request-time
  host/anchor stamping, EndFrame/Abort discard, post-focus commit, next-frame popup
  authoring and panel-Local lease/cancellation behavior.
- **P5** owns only the phase-neutral production-shaped native fixture/telemetry/runner
  completion plus source-level regression/hygiene preparation for P6. It does not add a
  new production API.

### P0-locked native P5/P6 fixture shape

Keep the existing dedicated `editorNativeUiIntegrationTest`; do not add a second
native executable or a product runtime test mode.

P5/P6 extend only phase-neutral test support already owned by that target:

- `editor/tests/support/native_ui_integration_test_extension.hpp/.cpp` registers
  same-owner real MainMenu and MainToolbar command contributions plus one owned
  ContextMenu root/contribution. The existing owned command panel requests that context
  menu only through `EditorPanelContext::RequestContextMenu`. Test-owned command
  state drives Enabled/Disabled/Unsupported/checked presentation and includes one
  intentionally rejected Invoke path; it does not reach into surface internals.
- `editor/tests/support/native_ui_integration_test_harness.hpp/.cpp` adds only
  phase-neutral telemetry/state transitions required to observe primary-host menu/
  toolbar presentation, request/open/cancel context popup state and the existing
  primary <-> detached panel lifecycle.
- `editor/tests/run-native-ui-integration-test.ps1` remains the external interaction
  driver and acceptance oracle for real menu/toolbar clicks, shell-chrome focus,
  primary/detached context popup placement/routing/cancellation and continued frame
  progress after an intentionally rejected command activation.
- The existing native catalog/application/project fixture remain unchanged unless P5
  needs a test-owned caller to exercise the already-public
  `EditorCommandService::SetShortcutOverride` for the required live shortcut-hint
  change. If needed, only the existing test application/support is extended; no
  production hook or alternate shortcut store is permitted.

The native fixture must preserve the existing docking/redocking, cross-host focus/input,
mixed-DPI, minimized-atlas and close/reopen evidence rather than replacing it with
surface-only assertions. Any P5/P6 run that intentionally moves the real mouse, changes
desktop focus or drags windows requires the existing user warning before execution.

### P0 validation boundary

P0 validation is documentation/source review only. Fresh independent exact-CURRENT
architecture and test-shape review must return **0 BLOCKER / 0 HIGH / 0 MEDIUM** after
any corrections before P0 can close. No generation/build/test/native result may be
claimed for this gate.

### P0 closure evidence

- Fresh architecture preflight initially returned
  **0 BLOCKER / 0 HIGH / 1 MEDIUM**: exact-current panel ownership existed only in the
  private panel record, while same-owner context-popup admission needed a concrete
  provenance seam. P0 locked `EditorPanelAuthoringView::owner` plus the short-lived
  current-authoring panel/owner stamp and explicit stale/copied-context rejection. The
  same correction also locked pending-surface discard on every current `EndFrame`
  failure path. Fresh post-fix architecture re-review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Fresh test-shape preflight initially returned
  **0 BLOCKER / 0 HIGH / 2 MEDIUM**: the native P5/P6 fixture shape was not concrete,
  and non-finite-anchor/query/invoke failure evidence lacked explicit owners. P0 locked
  the existing native target/extension/harness/runner shape and assigned each failure
  case to the focused command/surface/native oracle above. Fresh post-fix test-shape
  re-review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- A separate exact-current documentation/status audit returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** with the definition closed, P0 source/planning-only,
  and P1-P6/E3.5+ still unauthorized.
- Scoped whitespace/link/status checks are clean. No production source, Premake file,
  build output, executable test or native-editor behavior was modified/executed by P0.

**Historical pre-P4 status checkpoint:** P0-P3 were CLOSED while P4-P6 and E3.5+
remained unauthorized/unstarted. P4 was explicitly authorized afterward and closed; P5
was subsequently authorized and closed; P6 was subsequently authorized and closed. E3.4
is now accepted; E3.5+ remains unauthorized/unstarted.

## P1 closure evidence

- Added the command-neutral surface/contribution identities, fixed
  64/512/8/128/16 bounds and neutral failure attribution in the existing editor UI
  contract while keeping command-bearing surface descriptors private.
- Added one GPU/toolkit-free surface registry core with process-static nonwrapping
  generations, core-owned MainMenu/MainToolbar roots and fixed structural anchors,
  copied labels, deterministic placement buckets, owner/hierarchy validation and
  transactional already-stamped pending context-menu request state.
- Exact-token physical lookup remains usable after future logical unreachability while
  stable-ID/tree traversal and pending-request queue/commit/peek remain reachable-only.
  Stable IDs stay reserved until physical cleanup.
- The eight-level limit counts submenu nesting; leaf command/separator content beneath
  the eighth submenu remains valid.
- ManagedEditorUiService composes exactly one core. P1 adds no new service, frame owner,
  command bridge, extension surface registrar, MainMenu/MainToolbar authoring,
  EditorPanelContext request entry point, focus lease or popup presenter.
- Production editor, application-lifecycle and native integration source groups include
  surfaces/**.cpp. The new editorSurfaceTests target compiles the exact production core
  without editor_ui.cpp, ImGui, rendering, RHI, window or platform implementation.
- Independent review findings were resolved across physical-cleanup reachability,
  submenu-depth semantics, neutral early-failure attribution, exact-host/fresh-core
  token oracles and WrongThread identity assertions. A final manual pass also restored
  reachable + stable-ID validation on request commit after physical token lookup was
  decoupled from reachability.
- Fresh final exact-CURRENT independent source review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** and confirmed the strict P1/P2-P4 boundary.
- Scoped whitespace/status/link/production-scope checks are clean. P1 performed no
  Premake generation, compilation, executable tests or native editor run; the live plan
  explicitly defers broad execution to P6.

**P1 status: CLOSED.**

## P2 implementation and closure record

- Added private same-owner command validation over the existing command records. A
  contribution may reference only a currently reachable command owned by the same
  exact extension owner; missing/logically revoked/cross-owner references fail before
  surface topology is mutated.
- Added private owner-stamped surface registration/unregistration forwarding to the one
  existing `EditorUiService` owner. No public surface registry, second service or
  command dependency was added to the UI layer.
- `EditorExtensionActivationContext` now exposes only
  `RegisterContextMenuSurface(...)` and `RegisterSurfaceContribution(...)`.
  Context-menu registration forces `ContextMenu`; extensions still cannot create a
  second MainMenu/MainToolbar root.
- Added `SurfaceRoot` and `SurfaceContribution` to the existing 1024-entry extension
  owned-registration ledger. Successful registrations record exact generation tokens;
  failed validation/registration records nothing.
- Owner teardown now logically revokes surface roots/contributions and cancels the
  owner's pending context-menu request state before existing command and panel revoke.
  Exact-token lookup remains available for the subsequent reverse physical cleanup.
- `editorShellTests` now composes the same production `EditorSurfaceRegistryCore`
  behind its fixture UI service and contains focused source coverage for owned
  ContextMenu/MainMenu/MainToolbar contributions, missing/cross-owner command
  rejection, cross-owner private root/anchor rejection, shared-ledger capacity,
  rollback, deactivate-time admission closure and contribution-before-root cleanup.
  `editorCommandTests` owns direct same-owner/missing/logically-revoked validation
  coverage. `editorShellTests` Premake membership includes the production surface
  source/header.
- Scoped `git diff --check`, production-hygiene and phase-boundary scans are clean.
  ImGui matches in `editor_ui.cpp` are pre-existing E3.3 panel/docking authoring; P2
  added no menu/toolbar/popup rendering, `QueryPresentation`, shell-command bridge,
  presentation-path Invoke, focus lease or `EditorPanelContext::RequestContextMenu`.
- Fresh independent exact-CURRENT read-only source/static review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** and explicitly made no generation/build/test/native
  execution claim.

**Historical P2 closure checkpoint:** P2 closed while P3-P6/E3.5+ were still
unauthorized. P3 was authorized only afterward and is now closed.

## P3 implementation and closure record

- Added a private atomic command-presentation snapshot that enters the existing command
  route once, resolves exactly once and returns both state and effective shortcut from
  that exact selected handler/context. A valid Unsupported/no-handler route succeeds
  with an empty binding instead of falling through to another handler.
- Added one process-generation-checked, MainThread + UI-Ready private surface-command
  bridge slot. Shell initialization installs the existing panel-focus bridge and then
  the surface bridge; a surface-bridge install failure rolls the focus bridge back.
- Shell bridge callbacks delegate presentation to the private one-Resolve seam, Invoke
  only through the ordinary E3.2 command service path and same-owner validation through
  the existing private owner validator. No second command resolver/domain exists.
- ManagedEditorUiService authors MainMenu/MainToolbar only through ImGui's main/primary
  viewport paths. Detached hosts do not receive duplicate persistent shell chrome.
- Stable contribution IDs own ImGui toolkit identity. Copied labels remain display
  metadata only and adjacent hash sequences are neutralized for ImGui display parsing,
  so equal labels or labels containing `##`/`###` cannot alias widget identity.
- Persistent command presentation maps Enabled to activatable, Disabled/Unsupported to
  visible-disabled, projects checked state and textual menu shortcut hints from the same
  presentation snapshot, and issues at most one ordinary Invoke per activation.
- Presentation-query or Invoke failure affects only the current item and records bounded
  surface status; it does not mutate the committed surface tree or fail the UI frame.
- The shell-surface focus lease reuses only the exact prior E3.3 panel when no new panel
  candidate wins, that exact panel was authored in the current frame on the same live
  host, and `ResolveFocusReceiver` still succeeds. Pending close, destroy/revoke,
  host unregister, frame abort or reachability failure defeats the lease.
- Host unregister synchronously clears committed/candidate focus for that host through
  the existing shell focus bridge before native host retirement.
- Quiesce/stop/shutdown detach the surface-command bridge first, then the panel-focus
  bridge, then reverse extension cleanup. Stop/shutdown fail closed if either bridge
  detach fails; UI quiesce also fails while either private bridge remains attached.
- `editorCommandTests` owns the atomic presentation/no-handler route proof;
  `editorSurfaceTests` owns bridge thread/phase/token handling plus item-local
  query/invoke diagnostics; `editorShellTests` owns bridge installation rollback
  and bridge-before-focus-before-UI-quiesce teardown ordering.
- P3 preflight initially returned **0 BLOCKER / 1 HIGH / 3 MEDIUM**. The HIGH raw-label
  ImGui identity issue and MEDIUM host-loss, teardown-order and focused-proof gaps were
  all corrected before closure.
- Scoped `git diff --check` and production/phase-boundary scans are clean. No P4
  `EditorPanelContext::RequestContextMenu`, current-authoring request admission or
  popup authoring was added.
- Fresh independent exact-CURRENT read-only source/static review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** and explicitly made no generation/build/test/native
  execution claim.

**Historical P3 closure checkpoint:** P3 closed while P4-P6 and E3.5+ were still
unauthorized/unstarted. P4 was explicitly authorized afterward and closed; P5 was
subsequently authorized and closed; P6 was subsequently authorized and closed. E3.4 is
now accepted; E3.5+ remains unauthorized/unstarted.

## P4 closure evidence

- Added the neutral `EditorPanelContext::RequestContextMenu(EditorUiSurfaceId, ...)`
  entry point without exposing surface records, commands or extension-owner identity in
  the public request contract. UI-created panel contexts carry a private authoring-frame
  stamp; fabricated contexts use stamp zero and prior-frame copies cannot pass the live
  current-authoring check.
- `EditorPanelAuthoringView` now privately carries the exact inherited extension owner.
  During the panel callback the one UI owner stamps exact panel handle, owner, current
  host and frame, rechecks effective panel reachability, resolves the current
  `ContextMenu` surface generation/token and requires the same owner before capturing the
  finite request-time ImGui pointer position.
- Reused the existing fixed 16-entry pending request transaction. Exact duplicates
  coalesce, differing same-panel/frame requests conflict, exact post-EndFrame panel/host
  focus commits at most one request, and losing requests drain without spuriously
  clearing an already committed popup. EndFrame failure, `AbortFrame`, focus-dispatch
  failure, panel close/destroy/revoke, host loss, surface retirement/revoke, owner revoke
  and UI quiesce/shutdown defeat speculative or committed popup state.
- Committed requests author only on a later UI frame, inside the exact requesting panel's
  existing top-level ImGui host. Host/panel/surface generation are revalidated before
  command queries. The popup uses request-time position, stable contribution-ID ImGui
  identity, copied display-only labels and the existing P3 command bridge/query/invoke
  path; there is no second resolver or surface service.
- Context-only presentation omits `Unsupported` commands and removes leading, trailing
  and duplicate separators after omission while preserving `Disabled` and query-failed
  commands as visible disabled items. Each command presentation is queried once per
  popup-authoring pass and invocation remains one ordinary E3.2 `Invoke`.
- Focused source oracles now cover private panel-owner propagation and effective-open
  pending-close rejection, request ownership/generation/finite-anchor/coalesce/conflict/
  capacity, exact focus/host commit, active-popup preservation, host/surface/owner
  cancellation and context visibility/separator normalization. These tests were authored
  but **not executed** in P4.
- Per the P0-locked validation split, those toolkit-free targets do not compile
  `editor_ui.cpp` or ImGui and therefore do not claim direct execution of the
  `EditorPanelContext::RequestContextMenu`/`ManagedEditorUiService` admission or popup
  authoring path. That production wiring was source-reviewed in P4; real request/open/
  placement/detached-host interaction remains explicitly assigned to the P5/P6 native
  integration fixture and acceptance run.
- Independent P4 review initially found **0 BLOCKER / 1 HIGH / 0 MEDIUM**: a committed
  popup could query/render after an in-frame close was already requested. UI close is now
  applied before panel callbacks, request admission rechecks `ResolveFocusReceiver`, and
  popup authoring rechecks effective reachability after the callback. Fresh post-fix
  exact-CURRENT read-only review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Scoped tracked `git diff --check`, touched-file trailing-whitespace/final-newline checks,
  production phase/test-scaffold scans, duplicate-service/route scans and P5-leakage
  scans are clean. Existing LF-to-CRLF warnings on tracked editor files are warnings only.
  No Premake generation, compilation, executable test or native editor execution was run
  or claimed for P4.

## Post-P4 pre-P5 implementation cleanup

- A user-authorized read-only sanity/bloat pass after P4 closure found one implementation-
  bloat MEDIUM and no correctness/lifecycle finding: context-menu presentation first
  materialized a full 512-entry private snapshot and then callback-copied the normalized
  result into a second 512-entry UI snapshot.
- The narrow cleanup replaces that pair with one caller-owned
  `EditorUiContextMenuItems` snapshot. `EditorSurfaceRegistryCore::BuildContextMenuItems`
  fills it, queries each command once, marks normalized visibility/separators into a small
  fixed keep map, then stably compacts the same snapshot in place. No cache or persistent
  normalization state was introduced.
- The obsolete context-item visitor, UI collection callback/list wrapper and recursive
  visitor-normalization plumbing were removed. Persistent-menu rendering, popup request
  transaction state, owner/token/host/frame stamps, cancellation paths, focus lease and
  P3 command routing were deliberately left unchanged.
- Exact-current read-only review of the cleanup returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Source/static hygiene is clean; the old callback/list
  symbols are absent. No Premake generation, compilation, executable test or native run
  was performed for this cleanup pass.
- This cleanup was explicitly **not P5 authorization or P5 work** at that checkpoint.
  The user subsequently authorized P5 only; the cleanup remains a historical pre-P5
  boundary rather than part of the P5 implementation.

## P5 production-shaped native fixture closure

- The existing `editorNativeUiIntegrationTest` remains the sole native fixture. Its
  phase-neutral test extension now registers, through one real
  `EditorExtensionActivationContext` owner, the existing command/Local context/handler/
  panel plus one MainMenu command contribution, one MainToolbar command contribution and
  one owned ContextMenu root with command/separator contributions. No production hook,
  product test mode, alternate surface service, command route or shortcut store was
  added.
- Test-owned command presentation state covers Enabled, Disabled, Unsupported, checked
  and Enabled-with-intentionally-rejected-Invoke. Successful and rejected Invoke counts
  are separate, so Disabled/Unsupported cannot masquerade as rejected activations. The
  owned panel requests its contextual surface only through
  `EditorPanelContext::RequestContextMenu(...)` from live authoring.
- The existing test service uses the already-public `EditorCommandService` to change the
  effective Local binding from F8 to F12. The runner records the resolved binding, proves
  the real menu shortcut column changes, and routes F12 through the same Local handler;
  no second shortcut path exists.
- Test-only ImGui/native telemetry now exposes the real primary persistent-surface
  viewport, one-per-frame toolbar composition sentinel, command/panel interaction
  coordinates, accepted contextual request sequence/frame/viewport/request-time anchor,
  real popup frame/viewport/rect/item location, later cursor location, cancellation and
  child-menu draw geometry. Context popup discovery runs for every accepted request, so
  Unsupported and close-before-next-frame cancellation cannot self-certify absence.
- The external runner remains additive to the retained E3.3 dock/redock, cross-host
  text/click/focus, mixed-DPI, minimized-atlas and close/reopen flow. Its P5 source now
  requires primary-only persistent composition, Enabled activation, Disabled rejection,
  persistent Unsupported visibility, contextual Unsupported omission/separator
  normalization, checked draw geometry, the live F8->F12 hint change, item-local rejected
  Invoke followed by later frame/surface progress, exact next-frame primary and detached
  popup opening, request-time anchor rather than later cursor, Local receiver invocation,
  and primary/detached close-before-next-frame cancellation.
- The first exact-current independent P5 fixture review returned
  **0 BLOCKER / 2 HIGH / 3 MEDIUM**. The two HIGHs were false-positive popup-absence
  oracles for Unsupported/cancellation. The three MEDIUMs were non-exact popup timing,
  one-sided primary-only evidence and screenshot-only checked-state evidence. All five
  were corrected; fresh post-fix exact-current review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Source-only hygiene is clean: the PowerShell runner parses successfully, touched P5
  files have no trailing whitespace and end with LF, the fixture has no direct surface-
  registry-core access, its only popup request seam is the public panel context API, and
  P5 test identifiers/telemetry do not leak into production editor directories. No
  Premake generation, compilation, executable test or native UI automation was run or
  claimed in P5.

### Exact P6 execution matrix prepared by P5

P6 was subsequently explicitly authorized and this matrix was executed against exact
current rather than treating the P5 source/static closure as runtime evidence:

1. Run fresh Premake VS2022 generation for x64. Record any exact-current unrelated
   generation blocker; use an established isolated/editor-focused generation route only
   if it preserves the intended project graph.
2. Build serialized `/m:1` in both Debug and Development at minimum:
   `editorSurfaceTests`, `editorCommandTests`, `editorPanelTests`, `editorShellTests`,
   `editorApplicationLifecycleIntegrationTest`, `editorNativeUiIntegrationTest`, and the
   production `editor` target.
3. Run the freshly built focused executable tests in both configurations, including the
   surface/command/panel/shell and retained application-lifecycle regressions.
4. Run the dedicated native UI lifecycle path without interactive desktop automation and
   require its retained primary/detached presentation, resize, close/reopen and clean-exit
   evidence.
5. Before the interactive native runner, explicitly warn the user that it intentionally
   moves the real mouse, changes desktop focus and drags native windows. Only after that
   warning, run the existing interactive fixture and require all retained E3.3 evidence
   plus the P5 surface assertions described above.
6. Rebuild/link the real production editor in the same exact-current matrix and retain
   the established native bootstrap/workspace smoke applicable to the editor foundation.
7. Run scoped/full `git diff --check`, final-newline/untracked/target-membership and
   production/test-isolation scans, then obtain a fresh exact-current independent final
   architecture/acceptance review. Resolve every BLOCKER/HIGH/MEDIUM before accepting
   E3.4 and reconciling parent status.

### P6 closure evidence

- Fresh `generate-vs2022.bat` completed successfully and the engine-service audit passed.
- Serialized outer `/m:1` builds passed in both Debug and Development for SDL3 plus
  `editorSurfaceTests`, `editorCommandTests`, `editorPanelTests`, `editorShellTests`,
  `editorApplicationLifecycleIntegrationTest`, `editorNativeUiIntegrationTest` and the
  production `editor` target. The final production binaries are under
  `bin/Editor/{Debug,Development}/VanguardEditor.exe`.
- Fresh Debug + Development execution of `editorSurfaceTests`, `editorCommandTests`,
  `editorPanelTests` and `editorShellTests` passed **8/8**.
- The first direct lifecycle launch against the source-tree `.vproject` exited `-8`
  because that checkout did not physically contain the project descriptor's declared
  empty `Assets`/derived/intermediate/config/plugin roots. No E3.4 production path had
  executed. A contained project copy with all declared roots materialized then passed
  both `--bootstrap-path` and `--workspace-path` in Debug and Development **4/4**. The
  reusable test README now documents that contained fixture requirement.
- Exact-current noninteractive native lifecycle execution passed in Debug and Development:
  `frames=120 peakHosts=2 finalHosts=1 primaryPresent=1 detachedPresent=1 resized=1
  closeReopen=1 lifecycle=PASS`, with exit code 0 in both configurations.
- After the required real-input warnings, the final exact-current Debug interactive native
  run exited 0. It retained cross-host text/click, dock/detach/redock, mixed-DPI,
  minimize/restore and minimized-atlas evidence and added the E3.4 acceptance path:
  primary-only persistent surfaces, F8 -> F12 effective shortcut presentation, Disabled /
  Unsupported / checked mapping, one intentionally rejected Invoke followed by continued
  frame/surface progress, a newly-focused primary-panel request, exact next-frame primary
  and detached popup authoring, request-time anchoring, Local invocation and deterministic
  primary/detached close-before-popup cancellation. Final command telemetry was
  `dockCycle=1 dpiTransition=1 invokes=10 attempts=11 rejected=1 toolbarOffPrimary=0`.
- Interactive execution exposed only test-oracle/desktop-stimulus defects. P6 corrected
  them solely in the dedicated test harness/runner: settled menu-width observation,
  pre-interaction toolbar-authoring sentinel semantics, stable focus/mode-control retries,
  frame-synchronized request clicks, primary proof ordering before redock hides its source
  tab, and current-frame (`Active`) popup observation. No production C++ changed in P6.
- Final scoped/full tracked `git diff --check` exits 0 apart from existing line-ending
  warnings. Explicit E3.4 whitespace/final-LF, target-membership, direct-surface-core,
  production-test-token, duplicate-surface-service and dynamic-plugin scans are clean.
  Broad unrelated dirty asset/rendering/runtime/doc state was preserved rather than reset.
- Fresh final exact-CURRENT architecture/acceptance review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** and found no missing P6 requirement.

**P6 status: CLOSED. E3.4 is accepted at final exact-CURRENT 0 BLOCKER / 0 HIGH /
0 MEDIUM. E3.5+ remains unauthorized/unstarted.**
