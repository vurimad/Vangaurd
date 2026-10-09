# E3.2 - commands, contexts and shortcut routing

## Status and authorization

The user explicitly authorized **E3.2 definition work only** on 2026-09-17 after
E3.1 P0-P6 closed and E3.1 was accepted at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The
definition subsequently closed at independent exact-CURRENT 0 BLOCKER / 0 HIGH /
0 MEDIUM. On 2026-09-18 the user explicitly authorized **E3.2 P0 only**. P0 is now
closed after exact-CURRENT Vanguard/RED preflight, capture/lifecycle/test/file-ledger
corrections, final documentation hygiene and fresh independent exact-CURRENT review at
0 BLOCKER / 0 HIGH / 0 MEDIUM.

P0 made no production C++ change and ran no Premake generation, compilation,
executable test or native editor execution. After P0 closed, the user explicitly
authorized **P1 only** with `start next`. P1 is now closed after bounded command-core
implementation, fresh Debug + Development focused execution, Debug production-editor
compile/link integration, retained E3.1 shell regression, final hygiene/scope checks and
fresh independent exact-CURRENT review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The user
subsequently authorized **P2 only** with `okay start next P`. P2 is now closed after
bounded context-type/active-context implementation, fresh Debug + Development focused
execution, a forced exact-current Debug production-editor rebuild/link, retained E3.1
shell regression, final hygiene/scope checks and fresh independent exact-CURRENT review
at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. After P2 closed, the user explicitly authorized
**P3 only** with `start next P`. P3 is now closed after extension-owned
command/context/handler integration, focused/native validation, final hygiene and fresh
independent exact-CURRENT review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **P4-P6 remain
unstarted and unauthorized.** The user subsequently explicitly authorized **P4 only**
with `start next P`. P4 is now closed after context-local shortcut/default/override
implementation, fresh Debug + Development focused validation, production Debug editor
integration, final hygiene/scope checks and fresh independent exact-CURRENT review at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. A subsequent adversarial audit found one MEDIUM
sparse-key validation defect; the narrow fix was applied, fresh post-fix validation
passed, and a fresh post-fix adversarial review returned **0 BLOCKER / 0 HIGH /
0 MEDIUM**. At P4 closure, **P5-P6 remained unstarted and unauthorized.**
The user subsequently explicitly authorized **P5 only** with `start P5`.
P5 is now closed after implementation, fresh Debug + Development focused validation,
forced Debug production-editor rebuild/link, native composition smokes, final hygiene and
two independent exact-CURRENT read-only closure reviews at **0 BLOCKER / 0 HIGH /
0 MEDIUM**. The user subsequently explicitly authorized **P6 only** with `start P6`.
P6 is now closed after fresh Premake/service audit, Debug + Development command/shell/
application/input execution, forced Debug production-editor rebuild/link, rebuilt-editor
native bootstrap/workspace proof, final hygiene/scope scans and fresh independent
exact-CURRENT review at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. **E3.2 is accepted. E3.3 and
later implementation remained unauthorized at E3.2 acceptance. E3.3 subsequently
closed P0-P6 and is accepted. E3.4 definition and P0 source/planning review
subsequently closed at fresh post-fix 0 BLOCKER / 0 HIGH / 0 MEDIUM while P1-P6 and
E3.5+ remain unauthorized/unstarted.**

The active planning triplet is
`.planning/e3-2-commands-context-shortcuts/{task_plan,findings,progress}.md`. On
separate later authorization, refresh exact-CURRENT evidence and start only the
authorized numbered gate without weakening the P0-P6 contract below.

## Objective

Add one bounded editor command domain beneath the existing `EngineHost` so direct
UI requests and keyboard shortcuts resolve through the same command state and
receiver path. Commands retain stable identities; focused contexts select the
receiver; extension ownership controls every callback-bearing registration; and
the existing ordered `InputService` plus toolkit-neutral UI capture state decide
whether a key event is available to editor shortcuts.

E3.2 is routing infrastructure. It does not implement menus, toolbars, panel
docking/focus ownership, shortcut persistence, document transactions, viewport
navigation or domain-specific editor behavior.

## Mandatory read-first set

Before implementation, read in order:

1. root `AGENTS.md`;
2. [E3 parent plan](editor-foundation-e3-plan.md);
3. [accepted E3.1 lifecycle contract](editor-foundation-e3-1-plan.md);
4. [editor roadmap](editor-foundation-study-and-roadmap.md), especially E0/E3;
5. [editor UI integration contract](../designs/editor-ui-integration.md), especially
   input/focus ownership;
6. `editor/private/vanguard/editor/editor_shell_service.hpp` and
   `editor/extensions/editor_shell_service.cpp`;
7. `editor/framework/include/vanguard/editor/editor_ui.hpp` and
   `editor/framework/src/editor_ui.cpp`;
8. `editor/framework/src/editor_ui_imgui_platform.cpp`;
9. `source/engine/include/vanguard/engine/{input_service,frame_pipeline_service}.hpp`
   plus their implementations/tests;
10. `source/input/include/vanguard/input/input.hpp`;
11. `editor/src/editor_application.cpp`, `editor/premake5.lua` and current focused
    editor tests.

P0 must also refresh the relevant current RED sources rather than relying only on
the definition-time evidence recorded below.

## Definition-time CURRENT Vanguard evidence

### Existing owners

- `EditorApplication` still composes one `EngineHost`, one application state machine,
  one Input service, one Frame Pipeline, one `EditorUiService` and the accepted
  E3.1 `EditorShellService`. E3.2 must extend this graph rather than add an input
  pump, service container or application loop.
- `InputService` is the authoritative ordered per-frame input source. It publishes
  at most 2048 `RawEvent` values per frame plus a snapshot. Key events preserve the
  physical `input::Key`, pressed/released state and repeat flag; text input and
  focus changes are separate events.
- Input collection is a MainThread frame participant in `FramePhase::Input`.
- `EditorUiService` is an Editor/Tool, MainThread, `EditorWorkspace` service. Its
  UI frame participant runs after `InputFrameParticipantId`, forwards the same
  ordered input stream into Dear ImGui, authors panels once, then finalizes the UI
  frame. It already owns IME/text-input requests through the Vanguard window layer.
- The UI integration contract requires text/IME capture, command shortcuts and
  viewport navigation not to consume the same event, but CURRENT exposes no
  toolkit-neutral UI keyboard/text-capture snapshot yet.
- E3.1 owns extension lifetime using `EditorExtensionOwner {extension,
  activationGeneration}` plus a bounded 1024-record typed registration ledger.
  Logical owner revocation is allocation-free/non-failing and precedes physical
  cleanup; extension `deactivate` runs last while all external callback reachability
  is already gone.
- E3.1 shell activation runs in `OnStart`. The shell's only **required** service
  dependencies are Project Workspace and Editor UI, and that invariant remains.
  E3.2 may add a non-required ordering relationship for the command service; it may
  not turn commands into a new required E3.1 dependency.
- CURRENT `EngineHost` treats both `Required` and `StartAfter` service dependencies as
  plan-order edges when the provider is selected. `Start` then fully constructs,
  initializes and starts each planned service before advancing to the next, and normal
  unwind visits the plan in reverse for each global lifecycle stage. Only a missing
  `Required` provider is itself a compile failure. E3.2 therefore combines
  `StartAfter` ordering with an explicit production shell-config presence policy
  instead of weakening E3.1's accepted Required-dependency contract.

### Current frame timing available to E3.2

The accepted path is:

```text
native/backend input
  -> InputService ordered RawEvent publication
  -> EditorUiService latches ImGui's pre-NewFrame application-routing capture outputs
  -> EditorUiService forwards input / runs NewFrame / panel authoring / EndFrame
  -> toolkit-neutral frame-tagged UI capture snapshot publication
  -> E3.2 shortcut-routing participant
  -> later editor input consumers
```

The command participant therefore runs after `EditorUiFrameParticipantId`, not in
the platform backend. The ImGui-backed UI latches the application-routing capture
outputs that apply to the current input batch **immediately before `ForwardInput()` /
`ImGui::NewFrame()`**, matching ImGui's own guidance to use
`WantCaptureKeyboard` before `NewFrame()` when deciding whether the underlying
application receives that batch. It then conservatively combines that state with public
post-authoring ownership evidence at successful `EndFrame`, publishes one
toolkit-neutral snapshot for the same `FrameContext::frame`, and never exposes
`ImGuiIO` to the command domain. Later viewport/tool consumers that need keyboard
events must order after the E3.2 shortcut participant **and** honor its frame-tagged
event-claim/capture surface before processing the same events.

## RED definition-time evidence and intentional differences

Definition-time reference checkout:
`D:/root/R6.Root/Mainline/dev/src`.

- `editor/editorCommon/include/framework.h` centralizes action/shortcut managers and
  focused widget/editor state rather than giving each editor tool an input loop.
- `editor/editorCommon/src/controlWidget.cpp` pushes a control's shortcut contexts on
  focus gain and pops them in reverse order on focus loss. Context lifetime follows
  focus lifetime.
- `common/generalEditorBackend/include/shortcutManager.h` defines an active context
  stack with `Override` and `Inherit` modes.
- `common/generalEditorBackend/src/shortcutManager.cpp` searches active contexts from
  newest/topmost to oldest. `Inherit` continues when no binding exists; `Override`
  stops fallback. User bindings are distinct from defaults.
- `shortcutInputHandler` tracks modifiers/repeat state before asking the centralized
  shortcut manager to dispatch.

Vanguard adopts the useful ownership rules: centralized command routing, focus-owned
context lifetime, newest-first nested contexts, explicit inheritance/override and
separate default/user binding state. Vanguard deliberately does **not** copy RED's
global mutable framework manager, wx ownership, interop path, RTTI discovery,
unbounded callback/function storage or arbitrary multi-key shortcut machinery.
Vanguard uses its existing `EngineHost`, bounded registries, E3.1 owner generation,
physical `input::Key` events and explicit typed callbacks.

## Locked E3.2 ownership model

### One `EditorCommandService`, no second shell or input system

E3.2 adds one editor-owned command service in the `editor/commands` domain. Its
descriptor is:

- profiles: `Editor | Tool`;
- scope: `EditorWorkspace`;
- affinity: `MainThread`;
- required dependencies: `FramePipelineServiceId`, `InputServiceId`,
  `EditorUiServiceId`;
- provides one editor command capability;
- owns no worker, scheduler, event pump, renderer, project-asset backend or domain
  implementation.

Production composition registers the command service before the shell registration.
The E3.1 shell gains a **`StartAfter`** ordering relationship to the command service,
not a new `Required` dependency. This preserves the accepted E3.1 required-dependency
set while guaranteeing that, when E3.2 is present, command registration is available
before extension `OnStart` activation and remains alive through shell Quiesce cleanup.
`EngineHost` treats `StartAfter` as a real ordering edge whenever both services are
selected, so reverse global lifecycle passes quiesce/stop the shell before the command
service. Focused E3.1 hosts that intentionally omit E3.2 remain valid. In production
Editor/Tool composition the command service is registered unconditionally and the
retained shell config explicitly requires the E3.2 command domain. Shell Initialize
therefore fails before extension activation if production composition unexpectedly
lacks the command service. Focused pre-E3.2/E3.1 fixtures may explicitly leave that
policy disabled. This gives production the same fail-closed presence guarantee without
changing E3.1's accepted host-level **Required** dependency set. Shell command-
registration operations also reject a missing/non-running service. A non-command E3.1
extension does not gain a new mandatory host dependency merely because E3.2 exists.

The command service registers one MainThread frame participant for shortcut routing.
It consumes the existing immutable `InputService` event span; it never drains the
platform backend itself. The participant executes after `EditorUiFrameParticipantId`
and publishes a bounded current-frame shortcut claim/status surface for later editor
input consumers.

Lifecycle responsibilities are explicit in the **final P5-complete service**:

- `OnInitialize` resolves its required services. Once P5 is implemented it also
  registers the shortcut frame participant while the Frame Pipeline is still building;
  P1-P4 do not register or execute shortcut input routing early.
- `OnStart` opens command query/invocation/context/shortcut routing admission.
- shell `OnQuiesce` runs first because of the `StartAfter` edge and revokes/removes all
  extension-owned command-domain registrations while extension private state is live.
- command-service `OnQuiesce` then closes routing/context/override admission and clears
  transient active-context and per-frame claim state. If any extension callback-bearing
  record unexpectedly survives, it is logically neutralized before a cleanup failure
  can be reported; no stale callback may remain reachable.
- `OnDrain` requires zero live extension-owned command/context/handler callbacks and no
  active transient context; Stop/Shutdown are idempotent fallback/final clear paths.

### Source/layout ownership

The command contract remains editor-private rather than becoming an engine API.
Use the established application layout:

```text
editor/private/vanguard/editor/editor_commands.hpp   command-domain private API
editor/commands/*.cpp                                command-domain implementation
editor/tests/editor_command_tests.cpp                focused GPU-free behavior
```

`editor/premake5.lua` explicitly includes the command implementation/filter and the
focused test target. Do not place Vanguard-owned headers beside `.cpp` files and do
not expose ImGui types through the command contract.

## Stable identities, bounds and descriptors

E3.2 uses stable numeric identities, not display strings or pointers:

- `EditorCommandId`: non-zero `u64`; zero invalid;
- `EditorCommandContextTypeId`: non-zero `u64`; zero invalid;
- command/context/handler tokens: slot + non-zero generation as required for stale
  handle rejection;
- persisted future references use stable IDs, never array positions, pointers,
  localized labels or ImGui names.

Initial hard bounds:

- maximum command descriptors: **512**;
- maximum command context types: **256**;
- maximum command handlers: **1024**;
- maximum simultaneously active non-workspace contexts: **64**;
- maximum command/context display/debug name: **128 bytes** including the bounded
  terminating scan rule used elsewhere in editor registries;
- E3.1 aggregate extension-owned registration ledger remains **1024** records and may
  be the tighter limit when one activation owns commands, context types and handlers.

Every append checks capacity first. Capacity failure cannot make a partial command,
handler, binding or context visible.

### Command descriptor

One descriptor contains only immutable command metadata:

- stable command ID;
- bounded display/debug name;
- flags required by routing, initially including whether held-key repeat is allowed.

The command descriptor does **not** contain domain behavior, an ImGui callback,
workspace state or a universal service locator. Behavior is supplied by contextual
handlers below. Registering duplicate command IDs is an explicit failure.

### Context type descriptor

One context type has:

- stable context-type ID;
- bounded debug/display name;
- one receiver tier:
  - `ControlTool` - highest priority focused control or active tool;
  - `Local` - focused panel, view or document;
  - `Workspace` - base/fallback tier.

The command service owns one internal always-present workspace base context. Extension
context types may use `ControlTool` or `Local`; `Workspace` is reserved for the one
service-owned base context and is rejected for extension-defined context types.
Additional workspace handlers target that built-in workspace context type without
registering a second workspace stack.

### Handler descriptor

A handler binds one `{contextTypeId, commandId}` pair to:

- a synchronous state-query callback;
- an invocation callback;
- extension-lifetime handler data;
- an optional default shortcut chord for that context;
- the owning `EditorExtensionOwner` for extension-contributed handlers.

The owner is not caller-authored public input. Extension code can register through the
E3.1 activation context only; the shell stamps its already validated live
`EditorExtensionOwner` into the private command-domain registration call and records
the returned typed token in the E3.1 ledger. Ordinary command users cannot fabricate
or submit an owner generation.

The `{contextTypeId, commandId}` pair is unique. Ambiguous duplicate handlers are
rejected rather than resolved by registration order.

Handler callbacks receive transient active-context receiver data separately from
extension-lifetime handler data. The command service stores both as **non-owning
borrowed pointers only for their bounded live lifetimes**: handler data may be retained
only while that exact handler registration remains live, and receiver data only while
that exact active-context token remains live. Owner/context revocation makes those
pointers unreachable before their backing state can die; unregister/pop/revocation
clears them, and no callback may retain either pointer past the call. No pointer
survives handler removal, context pop, owner revocation or service teardown. This
permits one registered handler to service multiple panel/document/tool instances of
the same context type without per-instance command registration.

## Context activation and receiver precedence

Context **types/handlers** are registry entries. Active context **instances** are
bounded runtime routing state and may be pushed/popped while UI focus changes; they
are not new EngineHost services or extension activations.

Pushing a context records:

- live context-type identity;
- one transient receiver-data pointer;
- `Inherit` or `Override` mode;
- slot generation for stale-token rejection;
- monotonic activation order within the bounded stack.

Popping requires the exact live token. Focus-owned nested contexts are released in
reverse order. Owner/context-type revocation invalidates all matching active contexts
before handler or receiver callbacks can be reached again and clears their borrowed
receiver-data pointers before the receiver backing may be destroyed.

Receiver search order is deterministic:

1. active `ControlTool` contexts, newest first;
2. active `Local` contexts, newest first;
3. the workspace base context.

`Inherit` permits search to continue when the current context has no handler for the
command or its handler reports `Unsupported`. `Override` forms an explicit barrier:
if that context does not support the command, lower contexts are not consulted. This
is the bounded Vanguard equivalent of RED's override/inherit behavior and supports
modal tools without process-global input switches.

## Command state and one invocation path

Command state must distinguish at least:

- `Unsupported` - this receiver does not own the command; inheritance may continue;
- `Disabled` - this receiver owns the command but cannot execute it now;
- `Enabled` - executable now, with an optional checked/toggled bit.

This distinction is mandatory. A focused receiver that reports `Disabled` wins
receiver selection and **does not fall through** to a workspace handler. Otherwise a
disabled local Delete/Copy/Save could accidentally execute a different workspace
operation.

State query callbacks are synchronous, bounded and side-effect-free. They may inspect
their receiver/handler data but may not perform filesystem, asset-build, renderer or
other long-running work.

There is one public routing path for both state and invocation:

```text
command ID + current context stack
  -> deterministic receiver resolution
  -> state query
  -> selected handler
  -> invocation callback when enabled
```

Direct calls, future buttons/menus/toolbars and shortcut dispatch all use this path.
E3.2 tests may represent a future UI button by calling the same public invocation API;
they do not implement an E3.4 menu/toolbar contribution.

If the selected enabled handler's invocation callback fails, command invocation
reports a stable handler failure and does not fall through to another receiver or
invoke a second callback. Domain behavior remains with the receiver/domain system;
the command layer routes the request only.

## Shortcut chord and binding contract

Initial shortcuts are intentionally simple and deterministic:

```text
EditorShortcutChord = one non-modifier physical input::Key
                      + Ctrl/Shift/Alt/Gui(Super) modifier mask
```

- `Key::Unknown` and modifier-only primaries are invalid.
- Chords use physical `input::Key`, not localized text characters.
- left/right Control, Shift, Alt and Gui keys collapse into the corresponding logical
  modifier bit for conflict/dispatch purposes; ordered raw events still preserve exact
  press/release transitions used to maintain that mask.
- One initial shortcut does not encode arbitrary two-nonmodifier chords, key
  sequences or timing gestures. Add those only if a real consumer demonstrates need.
- Key-down triggers; key-up never invokes a command.
- repeated key-down invokes only when the command descriptor explicitly allows
  repeat.
- focus loss/reset clears command-service modifier tracking. Ordered raw key events,
  not only the end-of-frame keyboard snapshot, define modifier state for dispatch.

Shortcut bindings are **context-local**. The same chord may legally map to different
commands in disjoint context types. Within one context type, two live effective
bindings may not claim the same chord. Registration or override that would create a
same-context conflict fails with stable IDs and leaves the previous effective state
unchanged.

Each handler may declare one optional default chord. User override state is keyed by
`{contextTypeId, commandId}` and has three runtime modes:

- `Default` - use the registered default;
- `Unbound` - intentionally no shortcut;
- `Custom` - use one validated replacement chord.

Applying or clearing overrides is transactional and main-thread owned. **E3.2 does
not persist overrides.** E3.6 owns the versioned Saved/user preference file and later
reapplies validated overrides through this E3.2 API.

When a handler is physically removed, its runtime effective/default/custom binding
state disappears with that handler. Durable orphaned preference retention, migration
and later reapplication belong to E3.6 rather than a hidden E3.2 persistence store.

### Shortcut dispatch

For one key-down event, the shortcut router walks the same context precedence as
commands. For each active context it checks that context's effective binding for the
chord:

- no binding + `Inherit` -> continue;
- no binding + `Override` -> stop without lower-context dispatch;
- binding -> query that bound command's handler for this context;
- once a binding is found it **shadows every lower-context binding**, matching RED's
  found-binding behavior; dynamic handler state never causes a different lower command
  on the same chord to fire;
- `Unsupported` -> claim/stop without invocation;
- `Disabled` -> claim/stop without invocation;
- `Enabled` -> claim once and invoke once.

The workspace binding table is considered last. One physical key event can trigger at
most one command invocation.

## UI capture and input arbitration

Dear ImGui remains private to the UI implementation. E3.2 adds the smallest
toolkit-neutral capture snapshot to `EditorUiService`, conceptually:

```text
EditorUiInputCapture
  frame
  textInputCaptured
  keyboardCaptured
```

The exact names may change during implementation, but the semantics may not. The
ImGui-backed platform computes the snapshot without copying private ImGui algorithms:

1. immediately before `ForwardInput()` / `ImGui::NewFrame()`, latch the public
   `io.WantCaptureKeyboard` and `io.WantTextInput` values that ImGui documents for
   application-side dispatch of the current input batch;
2. when the existing `Platform_SetImeDataFn` callback runs during `EndFrame`, latch
   the current `ImGuiPlatformImeData::WantTextInput` request;
3. after `ImGui::Render()` has completed panel authoring, conservatively sample public
   post-authoring keyboard ownership using `ImGui::IsAnyItemActive()`,
   `ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)`, and
   `io.NavActive && io.ConfigNavCaptureKeyboard`;
4. on successful UI `EndFrame`, publish:
   `textInputCaptured = preWantTextInput || postImeWantTextInput` and
   `keyboardCaptured = preWantCaptureKeyboard || textInputCaptured ||
   postAnyItemActive || postAnyPopup || postNavigationCapture`, tagged with the exact
   UI frame.

The pre/post OR is deliberate. Pre-frame capture prevents a widget that releases focus
because of this input batch from leaking the same key underneath the UI; post-frame
evidence prevents a widget or popup activated by this batch from doing the same. The
policy may conservatively suppress one ordinary editor shortcut during a transition,
which is preferable to double-routing one physical input event. Abort/quiesce clears
the unpublished/transient capture state. No command-domain code includes
`imgui_internal.h`, calls `UpdateHoveredWindowAndCaptureFlags`, or duplicates ImGui's
private ownership algorithm.

Ordinary E3.2 shortcuts do not dispatch a key event while the published frame capture
reports text/IME capture. If the UI owns the keyboard batch more generally, the event
is likewise not dispatched as an editor shortcut in the initial contract. E3.2 adds no special
"global shortcut while typing" escape hatch; such a policy requires an explicit later
consumer and safety review.

The command service records which input event indices it claims in the current
`InputService::GetEvents()` span. The claim set is fixed-capacity, frame-tagged and
cleared every frame. It is not a mutable copy of input and not a universal event bus.
Later viewport/tool keyboard consumers must both order after shortcut routing **and**
honor this current-frame claim/capture surface before processing each relevant event,
so one event cannot become both an editor command and navigation action. Ordering by
itself is explicitly insufficient because `InputService` exposes an immutable span
rather than destructive event consumption.

UI capture and command claims do not mutate `InputService` and do not prevent game or
runtime systems from retaining their existing independent input ownership outside the
editor routing domain.

## E3.1 extension ownership integration

E3.2 extends the accepted E3.1 typed owned-registration mechanism; it does not create
a second extension owner.

The activation context gains narrow typed operations for command-domain registration,
for example command descriptor, context type and contextual handler registration.
Every successful extension-owned command registration consumes one E3.1 owned-ledger
record and returns a generation-checked typed token.

The raw owner-stamped register/revoke functions remain private between the shell and
command service (plus focused test seams). The general command API exposes command
query/invoke, active-context tokens and runtime shortcut overrides, not arbitrary
`EditorExtensionOwner` construction or registry mutation. E3.2 does not bind these
generic context tokens to `EditorPanelRegistry`; E3.3 will translate real panel/view
focus lifetime into the neutral context push/pop API.

For each extension owner, teardown remains:

```text
close new shell registration admission
  -> logical EditorExtensionOwner revocation
       -> command service marks that owner's command/context/handler callbacks unreachable
       -> active contexts owned by revoked context types stop resolving
  -> remove exact command-domain registrations in reverse owned-ledger order
  -> invoke final extension deactivate
```

Command-domain logical revocation is allocation-free, non-failing and idempotent for
a valid owner. After shell owner revocation returns, query/invoke/shortcut dispatch can
never reach that owner's handler data, command callbacks or context type through any
ordinary path. Revocation also ends the borrow lifetime of that owner's handler data
and any active receiver-data pointers reachable only through its context types before
physical removal or extension private-state teardown. Physical unregister may diagnose
an in-use/dependency-order defect but may not restore callback reachability.

A command descriptor cannot be physically removed while live handlers from other
owners still reference it, and a context type cannot be physically removed while live
handlers reference it. Correct E3.1 extension dependency activation/reverse teardown
is expected to remove dependent owners first. Any violation fails closed and is a
cleanup diagnostic, never a reason to invoke stale callbacks.

Failed extension activation must roll back partial command/context/handler
registrations synchronously before `OnStart` returns, exactly like the accepted P3-P5
probe proof. A fresh host generation rejects every stale command/context token.

## Failure/status model

Use stable enums and IDs, not unbounded retained strings, for at least:

- invalid descriptor/identity/chord;
- duplicate command/context/handler identity;
- missing command or context type;
- capacity exhausted;
- shortcut conflict;
- invalid/stale token;
- wrong thread/phase;
- owner revoked;
- command unsupported/disabled;
- handler invocation failure;
- cleanup/lifecycle failure.

Read-only bounded service status should expose enough information for tests and later
diagnostics UI: registered command/context/handler counts, active-context count,
current frame, last command/chord route result and stable offending IDs. Status is not
a second logger and does not retain unbounded history.

## Threading, reentrancy and mutation rules

- Registry mutation, override mutation, active-context mutation, query/invocation and
  shortcut dispatch are MainThread owned.
- Extension command/context/handler registration occurs during shell activation,
  before ordinary UI frames. Extension-owned physical removal occurs during shell
  rollback/Quiesce before extension private state dies.
- Command/context registry mutation is not permitted from inside a command query or
  invocation callback. A callback may submit bounded domain requests but may not
  recursively mutate command registration or active-context topology.
- Query/invocation may not recursively invoke another command through the same service
  unless a later phase explicitly defines a bounded reentrancy policy. Initial E3.2
  fails closed on recursive dispatch.
- Active context push/pop is permitted at the UI focus boundary on the MainThread;
  context-stack mutation never allocates beyond its reserved/fixed capacity and does
  not register a new command handler.
- No E3.2 callback runs on a worker. Long work remains with its existing domain owner.

## Focused validation seam

Add a GPU-free `editorCommandTests` Premake ConsoleApp rather than turning native UI
execution into the core command oracle.

The focused matrix should use production command-domain code, the real `EngineHost`,
Frame Pipeline and Input service with a deterministic fake `IInputBackend`, plus the
smallest UI-service fixture needed to publish toolkit-neutral capture state. It must
not initialize ImGui, D3D12, rendering, RHI, `assetStreaming`, game world or a native
window.

Keep `editorShellTests` as the accepted E3.1 lifecycle regression. E3.2 may extend its
fixture only where necessary to prove command-domain owner revocation/cleanup, but it
must not replace or weaken the E3.1 matrix.

The production editor remains the integration target for service composition and
native bootstrap/workspace proof. A native startup result is not a shortcut/focus
behavior proof unless actual deterministic input was exercised.

## Required implementation gates

### P0 - exact-CURRENT architecture preflight

After separate explicit authorization:

- create/activate the E3.2 planning triplet and copy P0-P6 gates;
- refresh all mandatory Vanguard and RED evidence;
- confirm the exact UI-capture snapshot point and Frame Pipeline ordering seam;
- confirm command service descriptor and shell `StartAfter` ordering preserve E3.1
  required dependencies and prove command-before-shell startup plus shell-before-command
  reverse teardown from exact `EngineHost` scheduling behavior;
- confirm command/context/handler bounds and callback lifetime against CURRENT
  containers/memory conventions;
- confirm focused GPU-free Premake linkage is feasible without a second UI/input
  implementation;
- record exact expected files/targets and scoped forbidden-dependency checks;
- obtain a fresh independent CURRENT-tree definition review and resolve every
  BLOCKER/HIGH/MEDIUM before P1.

P0 is read-only/planning unless a documentation correction is required. It does not
implement the command system.

### P1 - command identity, descriptor and direct routing core

Implement the bounded command service/core types, stable IDs, descriptor validation,
registry, status/failure model, workspace base context and direct state-query/invoke
path. Prove invalid/duplicate/capacity cases and Unsupported/Disabled/Enabled semantics.
No shortcut input routing or extension registrar yet.

### P2 - context types, active stack and receiver precedence

Implement bounded context-type/handler registration plus generation-checked active
context tokens. Prove ControlTool > Local > Workspace precedence, newest-first ordering
within a tier, exact LIFO focus lifetime, `Inherit` fallback, `Override` barriers,
disabled-wins-without-fallback, invalid/reserved context tiers, context/handler/active-
stack capacity failure without partial state and stale token rejection. No panel/docking
implementation.

### P3 - E3.1 extension-owned command registration and rollback

Extend the E3.1 typed registrar/owned ledger with command descriptor, context type and
handler registration tokens. Add optional command-service lookup plus shell
`StartAfter` ordering plus the production shell-config presence requirement. Prove
missing-command-service production startup fails before extension activation, partial
activation failure, reverse cleanup, logical revocation before physical removal,
private-state lifetime, cross-owner dependency cleanup and fresh-generation stale token
rejection. Preserve every E3.1 P1-P5 test.

**Closed.** The accepted E3.1 owner and exact 1024-entry owned ledger now cover
extension-owned command descriptors, context types and handlers through a private
owner-stamped seam. Logical revoke removes callback/context reachability and receiver
borrows before reverse physical cleanup/private-state teardown. Production composes the
command service before shell, shell adds only `StartAfter(EditorCommandServiceId)`, and
an explicit production presence policy fails before activation if the command domain is
missing/non-running. Partial failure, cross-owner cleanup, shared-ledger capacity and
fresh-generation stale-token proofs pass. Fresh independent exact-CURRENT closure review
is **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

### P4 - context-local shortcuts and runtime user overrides

Implement validated physical-key chords, per-context default bindings, repeat policy,
context-local conflict detection and transactional Default/Unbound/Custom runtime
overrides. Prove identical chords in disjoint contexts are legal, same-context
conflicts fail without mutation, focus loss clears modifier state and no disk
persistence is introduced.

**Closed.** Each existing handler may carry one validated physical-key default chord and
one bounded runtime Default/Unbound/Custom override. Effective bindings conflict only
within the same context type; disjoint contexts may reuse a chord. Left/right physical
Control/Shift/Alt/Gui state collapses to the logical chord mask and clears on
FocusLost/command-service Quiesce/Reset. Handler removal drops override state and P3
owner revocation makes owned shortcut state unreachable before physical cleanup.
AllowRepeat remains command-owned metadata; P4 does not dispatch raw key events or
register a shortcut frame participant. The validator also rejects undefined numeric
values in sparse `input::Key` holes rather than relying only on the keyboard-state
storage bound. The post-fix exact-CURRENT adversarial closure review is
**0 BLOCKER / 0 HIGH / 0 MEDIUM**.

### P5 - ordered input/UI-capture integration and routing matrix

**Closed.** The toolkit-neutral current-frame UI capture snapshot is published from the
existing UI service and one Editor|Tool/MainThread command shortcut participant runs
after `EditorUiFrameParticipantId`. It traverses the immutable ordered raw-event span,
applies FocusLost/modifier transitions, resolves exact P2 context precedence against P4
effective bindings, and publishes fixed frame-tagged claims sized to
`MaximumInputEventsPerFrame == 2048` without copying or mutating events.

Public ImGui outputs are latched before forwarding/NewFrame, IME text intent is latched
through `Platform_SetImeDataFn`, and public post-authoring item/popup/navigation state is
sampled after Render. Publication occurs only after successful UI EndFrame. Abort clears
unpublished/transient platform capture; Quiesce/Shutdown clear retained service capture.
Any earlier successful snapshot remains frame-tagged and cannot affect a later frame.

Focused GPU-free tests prove modifier ordering, repeat policy, focus reset,
Inherit/Override barriers, Disabled/Unsupported/Enabled shadowing, context-local chord
reuse, Default/Custom/Unbound dispatch, same-frame text/keyboard capture, stale-frame
capture rejection, one-event/one-command, downstream claim/capture honoring and owner
revocation. Direct/UI-equivalent/shortcut invocation share the same exact-handler
query/invoke callbacks. Fresh focused/production/native validation and final hygiene
passed; two independent exact-CURRENT closure reviews returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**.

At minimum cover modifier ordering within one frame, repeated/non-repeated keys,
focus loss/reset, Override/Inherit shortcut behavior, disabled commands, context-local
binding reuse, overrides, owner revocation during shutdown and Runtime/Headless
profile exclusion.

### P6 - validation, documentation and acceptance

Run fresh Premake VS2022 generation; serialized outer `/m:1` Debug and Development
focused builds; `editorCommandTests`, retained `editorShellTests` and applicable
application/input regressions; production Debug editor integration; and the narrow
non-interactive native bootstrap/workspace proof if CURRENT P0 still deems it
applicable.

Run full/scoped `git diff --check`, explicit untracked-file whitespace/final-newline
checks, header-layout checks and searches for:

- duplicate command registries/services/input pumps;
- ImGui/toolkit leakage into command API;
- dynamic plugin loading or `paths.plugins` scanning;
- E2/`assetStreaming` implementation coupling;
- E3.3 panel/docking implementation;
- E3.4 menu/toolbar/context-menu implementation;
- E3.6 persistence/layout/shortcut-file implementation.

Record literal commands and outcomes. Obtain a fresh independent exact-CURRENT review
of ownership, bounds, receiver semantics, shortcut arbitration, rollback/teardown,
tests and documentation. Resolve every BLOCKER/HIGH/MEDIUM before marking E3.2
accepted. Update the E3 parent, roadmap, development document map and planning triplet,
then stop. **At the E3.2 acceptance boundary, E3.3 implementation remained
unauthorized; its definition was authorized and closed only afterward.**

P6 acceptance completed on exact CURRENT with **0 BLOCKER / 0 HIGH / 0 MEDIUM**. The
required rebuilt-editor `--validate-bootstrap` and `--validate-workspace` proofs both
exited 0 against the fresh P6 project fixture at that historical acceptance point.
Those product-editor smoke switches were subsequently retired during test-support
isolation cleanup; equivalent current coverage belongs to the dedicated
`VanguardEditorApplicationLifecycleIntegrationTest` executable with test-owned
`--bootstrap-path` / `--workspace-path` modes. An auxiliary current nanovanguard project
validation attempt exits 70 before dispatch because the existing `sources inspect`
child is registered beneath a missing `sources` parent. The durable E2.6 plan already
records that exact CLI closure gap and assigns its repair to E2.6 P3, which remains
separately unauthorized; E3.2 did not cross that phase boundary. The independent P6
review explicitly classified that external CLI defect as non-blocking for E3.2.

## Minimum acceptance matrix

| Scenario | Required outcome |
|---|---|
| Valid command + workspace handler | Stable state/query and single invocation |
| Focused ControlTool handler | Wins over Local and Workspace |
| Local handler disabled | Disabled wins; workspace is not invoked |
| Unsupported Inherit context | Falls through deterministically |
| Unsupported Override context | Stops lower receiver/binding lookup |
| Same chord, different context types | Valid; active context selects meaning |
| Same chord conflict in one context | Rejected without changing old binding |
| Custom/unbound/default override | Transactional effective binding update |
| Text/IME or UI keyboard capture | Ordinary shortcut not dispatched |
| Repeat event | Invoked only for a repeat-enabled command |
| Focus loss/reset | Modifier state cannot remain stuck |
| Extension activation failure | Partial command state removed synchronously |
| Owner revocation | No query/invoke/shortcut callback can resolve owner |
| Fresh host generation | Stale command/context tokens remain invalid |
| Runtime/Headless profile | Command service/shell command callbacks absent |

## Explicit non-goals and stop boundary

- No production panel instance/focus/docking ownership; E3.3 owns that integration.
- No menu, toolbar or context-menu contribution registry; E3.4 owns those surfaces.
- No diagnostics console/progress UI; E3.5.
- No shortcut/layout/preferences file format, disk IO or restore; E3.6 persists user
  overrides through the E3.2 runtime API later.
- No demonstration all-surfaces extension; E3.7.
- No UI theme, styling or visual design decisions. When actual theme/styling work is
  reached, ask the user for the references they requested before choosing aesthetics.
- No document transactions, undo/redo semantics, scene hierarchy or inspector commands.
- No viewport navigation implementation, camera control or pointer-capture redesign.
- No dynamic DLL/plugin ABI, directory scan, hot reload/unload or scripting.
- No universal event/message bus and no second input pump.
- No asset build/cache/resource-streaming behavior in command callbacks.
- No ImGui types in command-domain contracts.
- No broad editor folder reorganization or unrelated formatting.

The implementing agent stops after each explicitly authorized gate. P0-P6 are closed.
**E3.2 is accepted** after final exact-CURRENT review at 0 BLOCKER / 0 HIGH / 0 MEDIUM.
E3.3 subsequently closed its definition and P0-P6 and is accepted. E3.4 definition and
P0 source/planning review subsequently closed at fresh post-fix 0 BLOCKER / 0 HIGH /
0 MEDIUM while P1-P6 and E3.5+ remain unauthorized and unstarted.
