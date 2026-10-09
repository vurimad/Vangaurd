# E3.5 - diagnostics, progress and status presentation

## Status and authorization

E3.1 through E3.4 are accepted. On 2026-09-23 the user explicitly authorized
**E3.5 definition work**. The definition is now **closed** after exact-CURRENT
Vanguard/RED study, correction of all architecture/adversarial/documentation findings,
scoped hygiene and fresh final **0 BLOCKER / 0 HIGH / 0 MEDIUM** reviews. This document
is the durable cross-agent definition for that slice.

After definition closure the user explicitly authorized **E3.5 P0 only** on 2026-09-24.
P0 is now **closed** after exact-CURRENT source/planning architecture/test-shape locking,
correction of every P0 review finding, fresh final **0 BLOCKER / 0 HIGH / 0 MEDIUM**
architecture/lifecycle/test-shape/docs review and scoped static/document hygiene. P0 did
not perform production C++, Premake edits/generation, compilation, executable tests or
native editor execution. The user subsequently explicitly authorized **E3.5 P1 only** on
2026-09-24. P1 is now **closed** after implementation of the locked diagnostics-sink and
bounded Activity-core source/test shape, correction of every exact-current review finding,
fresh final **0 BLOCKER / 0 HIGH / 0 MEDIUM** source/static review and scoped hygiene.
No Premake generation, compilation, executable test or native editor execution occurred
in P1. The user subsequently explicitly authorized **E3.5 P2 only** on 2026-09-24.
P2 is now **closed** after implementation of the locked typed Activity
source/snapshot/cancellation/shell-ownership contract, correction of every exact-current
review finding, fresh final **0 BLOCKER / 0 HIGH / 0 MEDIUM** source/static review and
scoped hygiene. No P2 Premake generation, compilation, executable test or native editor
execution was performed. The user subsequently explicitly authorized **E3.5 P3 only** on
2026-09-24. P3 is now **closed** after implementation of the locked Console production
presentation, correction of the exact-current owner-scoped panel-instance/reopen seam,
fresh final **0 BLOCKER / 0 HIGH / 0 MEDIUM** source/static review and scoped hygiene.
No P3 Premake generation, compilation, executable test or native editor execution was
performed. The user subsequently explicitly authorized **E3.5 P4 only** on 2026-09-24.
P4 is now **closed** after implementation of the locked Background Activity presentation
and display-only project-asset adapter, scoped source/static hygiene and fresh final
**0 BLOCKER / 0 HIGH / 0 MEDIUM** exact-current review. No P4 Premake generation,
compilation, executable test or native editor execution was performed. A later independent
read-only P2 proof audit identified **0 BLOCKER / 0 HIGH / 1 MEDIUM** test-oracle debt for
the two-tick conservative total-row reservation/stale-fallback case under the 256-row cap;
P2 remains phase-closed, no production-semantics defect was established, and P5 did not
reopen that phase. The user subsequently explicitly authorized **E3.5 P5 only** on
2026-09-25. P5 is now **closed** after lifecycle/fixture hardening, scoped source/static
hygiene and fresh exact-current native-fixture plus architecture/lifecycle reviews at
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. No P5 Premake generation, compilation, executable test
or native editor execution was performed. The user subsequently explicitly authorized
**E3.5 P6** on 2026-09-26. P6 is now **closed** after fresh Premake generation, serialized
Debug + Development full builds and focused execution, both lifecycle paths, Debug +
Development noninteractive native lifecycle, the warned Debug real-input native acceptance
path, final hygiene and fresh independent exact-current 0 BLOCKER / 0 HIGH / 0 MEDIUM
reviews. P6 corrected the exact 512-entry diagnostics-ingress instantiation plus narrow
test-support/runner defects exposed by execution, including mixed-DPI detached-window
placement. **E3.5 is accepted. E3.6+ remain unauthorized/unstarted.**

The active definition triplet is
`.planning/e3-5-diagnostics-progress-status/{task_plan,findings,progress}.md`.

## Objective

Add one bounded editor-facing observation/presentation layer for:

- recent process diagnostics produced by the existing `diagnostics` owner;
- typed progress for existing asynchronous owners;
- typed subsystem/status snapshots; and
- cancellation **requests** routed back to the exact owner that already owns the work.

E3.5 must make background work understandable without turning the editor UI into a
second logger, task scheduler, build coordinator or cancellation authority. Producers
continue to run whether the corresponding panel is visible, hidden, detached or closed.
Panels consume copied editor-owned snapshots only; they do not poll arbitrary backend
state during draw and do not execute backend work from ImGui callbacks.

The first production presentation is functional shell infrastructure, not a visual
theme pass. E3.5 does not choose final colors, typography, iconography, spacing or
animation. E3.6 owns persistence and E3.7 owns the separate demonstration extension.

## Read-first authority

Before any E3.5 gate, re-read:

- root `AGENTS.md` and `docs/development/test-support-isolation.md`;
- [development document map](../README.md);
- [editor foundation roadmap](editor-foundation-study-and-roadmap.md);
- [editor UI integration contract](../designs/editor-ui-integration.md);
- [E3 parent plan](editor-foundation-e3-plan.md);
- accepted [E3.1](editor-foundation-e3-1-plan.md),
  [E3.2](editor-foundation-e3-2-plan.md),
  [E3.3](editor-foundation-e3-3-plan.md) and
  [E3.4](editor-foundation-e3-4-plan.md) plans;
- `editor/README.md` and `editor/tests/README.md`;
- `source/diagnostics/README.md` plus the public diagnostics API/backend adapter;
- CURRENT `ProjectAssetPipeline` status/cancellation surfaces;
- CURRENT shell owned-registration ledger, panel registry, command route and surface
  composition;
- CURRENT `editor/premake5.lua`.

## Exact-CURRENT starting facts

1. `diagnostics` is already the one process log producer. Its imported RED logger owns
   asynchronous dispatch, lock-free queues, filtering, file output and sink fan-out.
   Editor code must not initialize a second logger or read the log file back as a live
   console transport.
2. The supported Vanguard diagnostics callback receives borrowed strings. In the
   normal asynchronous mode it runs on the logger worker thread and must not retain
   those pointers.
3. CURRENT Vanguard exposes only one `SetSinkCallback` slot. E3.5 is the first real
   production consumer that needs a long-lived callback without stealing the legacy
   test/future-consumer slot, so the diagnostics contract needs a small bounded removable
   sink-registration capability rather than editor code reaching into RED headers.
4. `EditorUiService` remains the only UI/frame/native-host owner. E3.5 does not add an
   event pump, renderer, panel registry or window owner.
5. E3.1 owns exact `EditorExtensionOwner {extension, activationGeneration}` lifetime and
   one bounded reverse registration ledger. Extension-provided activity/status adapters
   reuse that ownership rather than inventing a second plugin subscription lifetime.
6. E3.2 remains the only command route. Any production commands used to show E3.5 panels
   or request a local UI action use the ordinary E3.2 query/invoke path.
7. E3.3 already owns singleton/multi-instance panels, visibility, docking, detachment and
   focus. E3.5 presentation uses those panels; it does not invent a console-window owner.
8. E3.4 already owns declarative View-menu/context/toolbar surfaces. E3.5 may contribute
   ordinary commands/surfaces through that accepted route; it does not mutate ImGui menu
   structures directly.
9. `ProjectAssetPipeline` already exposes copyable automatic-recook, observation, VDDC
   maintenance and loose-maintenance status plus explicit cancellation requests. E3.5
   may adapt those APIs but must not call `Tick*`, build, reconcile, rescan, publish or
   otherwise progress E2 work itself.
10. E3.2/E3.3/E3.4 already expose bounded read-only status for later diagnostics. E3.5
    may present those snapshots, but must not make them mutually dependent on the new
    service merely to display themselves.
11. The Frame Pipeline already provides one main-thread schedule. E3.5 can drain copied
    ingress, sample registered adapters and then process queued cancellation requests once
    per frame without creating a second scheduler.
12. Runtime/Headless products do not compose editor shell services and do not gain an
    E3.5 panel or editor activity service.

## RED definition evidence and intentional Vanguard differences

The local RED reference provides useful behavior/ownership principles:

- `editor/editorCommon/include/progressDialog.h` separates stage/task/message data from
  the widget and gives progress records explicit typed identity.
- `editor/editorWorld/src/contentCompilationFeedback.cpp` routes Cancel through
  `ICompilationFeedbackProxy::SendCompilerCancelRequest`; the dialog transitions to a
  pending "Cancelling..." presentation and disables repeated activation rather than
  taking compiler ownership.
- `common/interopServices/include/interopNotificationClb.h` and its implementation use
  explicit registration/unregistration lifetime; the notifier contract documents its
  main-thread callback behavior.
- `editor/editorCommon/src/actionPopup.cpp` asserts main-thread presentation, and
  `editorRunnable.h` provides main-thread deferral rather than executing UI work on an
  arbitrary producer thread.

RED also shows patterns Vanguard should **not** copy literally:

- backend progress dialogs that poll `InteropRequest::WasServiced()` on a UI-owned timer;
- message logs that insert every event and refresh the complete list without an explicit
  retention bound;
- status-change callbacks that immediately issue another backend synchronous query; and
- global singleton popup arrays whose lifetime is governed only by visual fade timeout.

Vanguard keeps RED's typed task/message identity, owner-routed cancellation, explicit
subscription lifetime and main-thread presentation. Vanguard intentionally adds fixed
bounds, generation-checked tokens, copied cross-thread data, one frame-owned sampling
point and deterministic overflow/failure counters.

## Locked ownership model

### One `EditorActivityService`

E3.5 introduces one editor-workspace service/capability named `EditorActivityService`,
as the generic editor-facing owner for diagnostics ingestion and
typed activity snapshots.

It is **not** a task executor. It owns only:

- the active generation/consumer side of one bounded process-lifetime diagnostics
  ingress bridge;
- one bounded retained diagnostics view;
- one bounded registry of typed activity/status source adapters;
- one latest published snapshot per registered source;
- one bounded queue of cancellation requests; and
- bounded service status/failure counters.

It does not own asset jobs, build requests, rendering work, file transactions or domain
state. The real producer remains authoritative for whether work is running, completed,
failed or cancellable.

The service runs only for Editor/Tool profiles, uses the existing main-thread Frame
Pipeline, and is discoverable through one typed editor capability. Runtime/Headless do
not compose it.

### Frame ordering

The activity service has one bounded main-thread participant in `BeginFrame`.
Its descriptor depends after the existing `InputFrameParticipantId`. E3.5 also changes
the existing `EditorUiFrameParticipantId` dependency list so the UI participant depends
after `EditorActivityFrameParticipantId`. This is a real Frame Pipeline edge; ordering
must not rely on participant numeric IDs, registration order or service initialization
order.

On each tick, in this order, it:

1. drains at most the configured diagnostics-ingress budget into private bounded scratch;
2. samples and validates each live activity source exactly once into private bounded
   staging scratch without changing the currently published activity generation;
3. processes at most the configured pending cancellation-request budget against those
   freshly sampled exact source/item identities, including the authoritative
   producer-operation recheck, and records request results only in staging state;
4. commits the privately drained diagnostics and atomically publishes the complete new
   activity generation, including fresh/stale source rows plus queued/request-result
   presentation metadata; and
5. updates its read-only service status/generation as part of that same publication.

The editor UI frame participant runs after this activity participant, so all panels in
one UI frame observe one immutable activity generation. A cancellation click authored in
that UI frame is queued only; it reaches the backend at the next activity tick. That
one-frame delay is intentional: panel draw callbacks do not execute backend cancellation
code. `RequestCancel` mutates only the bounded internal request/dedupe queue; it never
modifies the currently published activity rows or generation. Captures before and after a
successful `RequestCancel` in the same UI frame are therefore identical. Queued/request-
result presentation appears only in the next atomic activity publication.

If the UI is hidden, a panel is closed, or all editor hosts are minimized, the activity
participant still runs. Presentation lifetime therefore cannot stop or own producer work.

### Diagnostics owner remains below E3.5

E3.5 requires the Vanguard diagnostics API to add a bounded removable callback-sink
registration alongside the existing legacy `SetSinkCallback` behavior. The diagnostics
module remains the fan-out owner and remains the only layer that touches imported RED
logger types.

The intended production contract is:

- fixed small sink capacity (initial definition: **8 registered callback sinks** in
  addition to the existing legacy callback slot);
- process-wide nonzero, nonwrapping generation-checked sink tokens that are not reset by
  diagnostics shutdown/reinitialize;
- thread-safe register/unregister;
- unregister provides a no-callback-after-return lifetime guarantee for that token; and
- existing `SetSinkCallback` semantics/tests remain valid and coexist with registered
  sinks instead of being silently replaced.

Diagnostics sink registry synchronization is never held while invoking a user callback.
Registration/unregistration from inside a currently executing diagnostics callback is a
reentrant invalid operation and must fail without mutation; in particular, a sink cannot
wait for its own in-flight callback to finish. The exact result spelling and detach
mechanism are locked in the P0 section below, and the no-callback-after-successful-
unregister guarantee is fixed.

Every callback remains a borrowed-view callback. It must not allocate, block on the UI,
call ImGui, retain strings, recursively log, or invoke backend/editor services.

The E3.5 diagnostics callback copies only bounded message metadata/text into its ingress
record and returns. If the ingress is full it increments an atomic dropped counter and
returns immediately. It never waits for a panel or the main thread.

The callback `userData` is **not an `EditorActivityService*`**. It points only to the tiny
private process-lifetime `EditorActivityDiagnosticsBridge`
whose bounded queue/counters outlive every EngineHost service instance and remain valid
until diagnostics process shutdown. Each admitted record is stamped with the currently
published activity-service generation. Quiesce clears that active generation before
service storage can die; a callback already in flight may finish copying into the bridge,
but it cannot dereference destroyed service/panel/extension state, and a later service
instance discards records stamped for an older generation. This bridge is not a second
logger or service and owns no presentation/history beyond its fixed ingress queue.

When the bridge's active service generation is zero, the callback returns immediately
without queue admission; it may update only a bridge-local bounded inactive-discard
counter. A callback that observed the previous nonzero generation immediately before
quiesce may still finish one stamped copy, which the next consumer discards as belonging
to the old generation. Thus an inactive bridge cannot accumulate an unbounded/stale
workspace backlog between service instances.

The bridge also owns the exact diagnostics sink-registration token/state across activity-
service instances. A successful service detach may unregister and clear that token, but
an unregister failure leaves the exact token/state in the bridge rather than forgetting
it. A later service attachment uses the P0-locked exact token-liveness query: reuse the
retained token while it is live, clear/replace it only after exact stale proof, otherwise
fail/quarantine attachment. It must **never** blindly register a second callback while
the prior registration is live or unresolved. Diagnostics shutdown first fences the one
aggregate RED sink, then may clear registered callback slots; the process-wide generation
source is never reset, so a bridge token retained across diagnostics reinitialize is
provably stale rather than aliasing a new registration.

### Typed activity sources, not a universal message bus

Async/domain systems are adapted by registered `EditorActivitySource` descriptors. A
source is a main-thread observation adapter, not the producer itself.

Each source has:

- one stable technical `EditorActivitySourceId`;
- one generation-checked registration token;
- a copied bounded display name;
- one query callback that writes a complete snapshot into a caller-owned bounded writer;
- optional cancellation-request callback; and
- opaque caller-owned state that must outlive the registration.

The query callback is invoked only by the activity participant on the editor main
thread. It may call existing **read-only/copyable status APIs**. It must not tick jobs,
wait for background completion, perform filesystem scans, start builds, recurse into the
UI, or allocate an unbounded result.

The cancellation callback is also invoked only by the activity participant and represents
one request to the existing owner. It may call the owner's explicit `Cancel*`/request API;
it must not implement cancellation itself.

There is deliberately no generic arbitrary event bus, no backend-to-panel observer list
and no callback fired directly into panel code.

### Consumer capture, not per-panel delivery queues

Presentation consumers use one bounded copy-out `Capture`/visitor surface over the
service's retained/current model. They do **not** register callbacks and the service does
not retain one event backlog per panel.

A consumer-owned read cursor contains the process-wide nonzero service-instance
generation plus central-model revisions/sequences. Capture rejects a cursor from another
service generation rather than aliasing restarted storage. It returns an immutable
bounded result containing the range/revision actually copied and an
explicit `gap` indication when the requested diagnostic sequence is older than the
retained ring. The consumer resumes from the retained range reported by the service;
E3.5 never allocates hidden catch-up storage to satisfy a slow or hidden panel.

Activity rows are current-state snapshots rather than a history stream. Capture returns
one complete published activity generation (or explicit insufficient-capacity failure)
so a panel never observes half of a source publication. A cursor/revision is observation
state only and is never accepted as producer or cancellation identity.

In this definition, **removable producer/source registrations are the subscriptions whose
lifetime must be torn down**. Consumer capture needs no registration token and therefore
cannot leave a stale callback behind when a panel is hidden, closed or destroyed.

Production extensions that present E3.5 data do not discover the activity service through
a global singleton or a second service locator. Because `EditorShellService` has a required
dependency on `EditorActivityService`, `EditorExtensionActivationContext` exposes one
borrowed **consumer** reference/accessor to that exact service instance for the duration of
the activation generation. A built-in Console/Background Activity extension may store
that borrowed reference only in extension private/factory state whose lifetime is bounded
by the same shell activation generation; panel draw/factory code then uses it only for
bounded `Capture`/`RequestCancel` calls. The borrowed consumer reference is never a
registration token, never retained beyond owner revoke and never made globally reachable.

### Extension ownership

Extension-provided activity sources reuse E3.1 ownership:

- `EditorExtensionActivationContext` gains the narrow activity-source registration seam
  plus the borrowed activity-service consumer reference described above;
- the existing shell-owned 1024-entry registration ledger owns the returned exact token;
- logical owner revoke makes the source unreachable and discards its queued cancellation
  requests before extension private state can be destroyed; and
- reverse physical cleanup consumes the exact generation token.

The shell service has a required `EditorActivityService` dependency so it can provide that registrar.
The activity service does not depend on the shell, command service or UI service, avoiding
a dependency cycle.

## Data model

### Diagnostics record

The retained editor diagnostic record is a copied presentation record, not a logger
message owner. It contains at least:

- monotonic `firstSequence` / `lastSequence` range;
- first/last frame metadata when available;
- thread ID, thread-context ID and network-peer ID;
- diagnostics level and category;
- copied message text bounded by the existing diagnostics maximum message size;
- a repeat count for strict adjacent coalescing; and
- flags for truncation/synthetic overflow presentation where applicable.

The console does not read the log file and does not retain `MessageView` pointers.

Every E3.5 diagnostics callback attempt reserves one monotonic process-wide nonzero,
nonwrapping ingress sequence before queue admission; service restart does not reset it.
A full ingress queue therefore consumes a sequence and
increments the dropped counter even though no retained record is created; a later
capture can distinguish that loss from ordinary eviction. Strict coalescing extends the
retained record's `lastSequence` rather than erasing sequence evidence.

Strict adjacent coalescing is permitted only when level, category, copied text and the
thread/context/peer identity are equal **and** the incoming admitted sequence is exactly
`retained.lastSequence + 1`. Coalescing updates the last-seen metadata and repeat count;
it never spans a consumed-but-dropped sequence and never merges unrelated warnings merely
because their display labels match. Identical admitted records on opposite sides of an
ingress drop therefore remain separate retained records, preserving the capture gap.

### Activity key and row

One published activity item is addressed by:

`{source id, source registration generation, item id, item generation}`.

The source/item generations prevent a delayed cancel click from retargeting a restarted
operation or a newly registered source reusing the same stable IDs.

Source-registration generations are process-wide, nonzero and nonwrapping across service
instance reuse. Every item has a nonzero row-incarnation generation. For a **cancellable**
item that generation must be the **authoritative producer operation instance**, not merely
the panel row or the last sampled state; the source query and cancellation callback share
that identity contract. Immediately before dispatching a cancel request, the source
adapter must re-check that the authoritative owner still represents the same operation
generation. A display-only status item whose owner exposes no operation identity may use a
source-owned row-incarnation generation solely for observation/reuse safety, but that value
is never accepted as a producer precondition and the row remains non-cancellable. A row or
snapshot generation may never be promoted into cancellation identity.

Each copied activity row contains at least:

- stable item ID and nonzero item generation;
- kind: `Status` or `Progress`;
- state: a small typed set covering idle/pending/running/cancelling/succeeded/failed/
  cancelled/blocked presentation;
- severity/attention level independent from progress state;
- copied bounded label and optional detail text;
- optional determinate progress as integer `completed/total` values; unknown total is
  explicitly indeterminate rather than encoded as an arbitrary percentage;
- whether a cancellation request is currently supported; and
- whether E3.5 has already queued/accepted a cancel request for this exact key.

The UI may derive a fraction for drawing only after validating `total != 0` and
`completed <= total`. Floating-point percentages are not persistent identity/state.

### Transactional source publication

Each source query writes into empty bounded scratch. A snapshot is published only when
the entire source query validates:

- unique item IDs within that source snapshot;
- valid nonzero generations;
- bounded copied text;
- row and total-capacity limits;
- finite/valid presentation values; and
- no callback failure.

On failure, the service retains the source's previous successful rows for diagnostic
continuity, increments a bounded failure counter and records the latest failure, but the
source snapshot is explicitly marked **stale** with last-success/current-failure metadata.
Stale rows are presentation evidence only: their cancellation capability is suppressed
and `RequestCancel` rejects them until a later successful source query publishes a fresh
snapshot. A partial new snapshot never becomes visible. The next successful publication
clears stale/failure presentation for that source.

Unchanged source rows coalesce as latest-value state; E3.5 does not append one history
entry every frame. A source that stops reporting an item removes it on the next
successful snapshot. Durable operation history belongs to the domain owner or ordinary
diagnostics, not an unbounded shell journal.

## Initial bounds

The definition proposed the following initial production bounds; P0 revalidated them
against exact CURRENT without change. A later phase may change them only with an explicit
documented finding:

- diagnostics registered callback sinks: **8** plus the legacy callback slot;
- diagnostics ingress records: **512**;
- diagnostics retained console records: **2048**;
- diagnostics copied message bytes: the existing `diagnostics::MaxMessageLength`;
- diagnostics drained per activity tick: **256**;
- registered activity sources: **64**;
- rows emitted by one source snapshot: **32**;
- total published activity rows: **256**;
- pending cancellation requests: **32**;
- copied activity/source label: **128 bytes**;
- copied activity detail: **512 bytes**.

The existing 1024-entry E3.1 extension-owned registration ledger remains an independent
global bound and may reject activation before the activity registry reaches 64 sources.

Capacity overflow is explicit and observable. It never silently reallocates without
limit and never blocks the logger worker waiting for the editor.

## P0 exact-CURRENT API, identity and lifecycle lock

The 2026-09-24 P0 preflight revalidated the bounds above against CURRENT capacities and
locks the following implementation spelling/shape for later authorized gates. These are
documentation contracts only in P0; none of the declarations below exist in production
source yet.

### Diagnostics removable-sink API

`diagnostics` keeps exactly one process-static `VanguardSink` registered with RED. P1 must
not register eight imported `red::LoggerSink` objects. That existing aggregate sink gains
one fixed internal fan-out of **8** removable Vanguard callbacks in addition to the
legacy `SetSinkCallback` slot.

The public additions are locked as:

```cpp
inline constexpr u32 MaximumSinkRegistrations = 8;

struct SinkRegistrationToken
{
    u32 index = ~u32{0};
    u64 generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return index < MaximumSinkRegistrations && generation != 0;
    }
};

enum class SinkRegistrationResult : u8
{
    Success,
    InvalidArgument,
    CapacityExceeded,
    GenerationExhausted,
    StaleToken,
    ReentrantOperation,
    InternalFailure
};

[[nodiscard]] SinkRegistrationResult RegisterSinkCallback(
    SinkCallback callback, void* userData, SinkRegistrationToken& token) noexcept;
[[nodiscard]] SinkRegistrationResult UnregisterSinkCallback(SinkRegistrationToken token) noexcept;
[[nodiscard]] bool IsSinkCallbackRegistered(SinkRegistrationToken token) noexcept;
```

`SetSinkCallback` keeps its current source/API contract and remains outside the eight-slot
capacity. Registration tokens use a process-wide nonzero `u64` generation allocated from
one nonwrapping source; shutdown/reinitialize never resets that source. Slot reuse always
gets a new generation. Null callback, full capacity and generation exhaustion fail without
mutating existing registrations or the output token.

The registry mechanics live behind a production-private
`diagnostics::detail::SinkRegistryCore` in
`source/diagnostics/private/vanguard/diagnostics/sink_registry.hpp`. Like the accepted
panel/surface registry cores, it receives a neutral
`SinkRegistrationGenerationSource {std::atomic<u64>* counter}`; production supplies the
one process-static counter while `diagnosticsSmoke` may instantiate the exact core with a
local counter near `~u64{0}` to prove exhaustion deterministically. This is private core
dependency injection, not a public diagnostics test hook.

The compatibility backend remains C++17 and does not gain a dependency on the Vanguard
concurrency module merely for this registry. The intended mechanism is a tiny backend-
private fixed registry guarded by a short `std::atomic_flag` mutation/snapshot lock plus
per-slot atomic in-flight counters and explicit `Free -> Live -> Retiring -> Free` state.
Registration may claim **Free only** and skips `Retiring`; a slot generation/counter can
therefore never be reused while callbacks from the prior generation are draining. Dispatch
claims one exact `Live` slot/increments its in-flight count while synchronized, snapshots
the callback/user-data pair, releases the registry lock, invokes user code, then decrements
in-flight. The legacy callback/user-data pair is likewise snapshotted coherently, but
retains its legacy non-token API semantics.

Unregister validates the exact slot/generation and all failure gates **before** mutation.
The first caller that observes matching `Live` changes that slot to `Retiring` while
synchronized and becomes the retirement owner. A concurrent unregister of the **same**
token that observes matching `Retiring` joins the already-established retirement fence:
it must not return `StaleToken`, mutate the slot or start a second retirement. Both paths
release the registry lock and wait outside it until that generation's already-claimed
in-flight calls reach zero. The retirement owner then reacquires the lock, clears callback/
user-data/generation bookkeeping and only then marks the slot `Free`; a joining caller may
return `Success` once the same old-generation in-flight count is zero because `Retiring`
already prevents any new claim. From `Retiring` onward the operation cannot report a
failure. `StaleToken` is returned only when the requested generation is neither the live
nor retiring generation for that slot; because `Retiring` retains its generation until the
old in-flight count reaches zero, `StaleToken` is therefore also proof that no callback for
that token remains in flight. Successful return means no callback can occur afterward for
that token, and a concurrent register can never alias the old counter/generation during
the fence.
A thread-local diagnostics-callback depth covers legacy and registered callbacks.
`RegisterSinkCallback`/`UnregisterSinkCallback` from inside any diagnostics callback return
`ReentrantOperation` without mutation, preventing self-wait. No registry synchronization is
held while user code runs. `IsSinkCallbackRegistered` is the exact bridge restart proof:
it returns true only for the same live index+generation.

`RegisterSinkCallback` is valid before diagnostics initialization, matching the legacy
pre-initialize callback setup pattern. Diagnostics shutdown unregisters/fences the one RED
aggregate sink before clearing the eight registered slots; it does not reset the global
generation source and it does not redefine the legacy slot contract. For Activity detach,
`Success` clears the bridge token; `StaleToken` is exact proof that the token is already
unreachable and also clears it; any other failure retains the token/state for later
liveness proof/quarantine rather than guessing.

`InternalFailure` exists for a real internal registry/invariant failure and is returned
only **before** the `Live -> Retiring` transition, so it leaves the exact token live and
unchanged. Once retirement starts it is a completion fence, not a fallible half-detach.
The Activity implementation does not need a public fault
switch to test that branch: its private core receives an
`EditorActivityDiagnosticsRegistrationAccess` function table containing the exact
register/unregister/liveness operations. Production access forwards only to the three
public diagnostics functions above; `editorActivityTests` supplies a local fake that can
return `InternalFailure` while reporting the retained token still live. This proves
restart reuse/quarantine and duplicate-registration prevention without exposing any
test-only behavior to production consumers.

### Activity public/private split and exact identities

The generic public consumer contract is
`editor/framework/include/vanguard/editor/editor_activity.hpp`. It contains no ImGui,
RED or asset-streaming types and owns the following exact public names:

- `EditorActivitySourceId` and `EditorActivityItemId` (`u64`, zero invalid);
- `EditorActivityKey {source, sourceGeneration, item, itemGeneration}`, with both
  generations nonzero `u64` values;
- `EditorActivityKind {Status, Progress}`;
- `EditorActivityState {Idle, Pending, Running, Cancelling, Succeeded, Failed,
  Cancelled, Blocked}`;
- `EditorActivityAttention {None, Info, Warning, Error}`;
- `EditorActivityCancelAdmission {Queued, AlreadyQueued, InvalidState, WrongThread,
  ReentrantMutation, Stale, Unsupported, CapacityExceeded}` for the UI->service enqueue
  result;
- `EditorActivityCancelResult {None, Accepted, AlreadyRequested, Busy, Stale,
  Unsupported, Failed}` for the producer callback result published on a later generation;
- `EditorActivityFailureCode`, `EditorActivityFailure`, `EditorActivityCursor`, copied
  diagnostic/source/item records, diagnostic/activity capture metadata and
  `EditorActivityServiceStatus`; and
- `EditorActivityService`, exposing bounded `CaptureDiagnostics`, all-or-nothing
  `CaptureActivities`, `RequestCancel`, `ClearDiagnostics` and `GetStatus` operations.

`EditorActivityFailureCode` must spell the distinct cases `None`, `NotReady`,
`InvalidArgument`, `InvalidDescriptor`, `WrongThread`, `DuplicateSource`, `DuplicateItem`,
`CapacityExceeded`, `GenerationExhausted`, `StaleToken`, `StaleCursor`, `StaleActivity`,
`OwnerMismatch`, `ReentrantMutation`, `SourceQueryFailed`, `CancellationUnsupported`,
`CancellationRejected`, `CancellationBusy`, `CancellationFailed` and
`DiagnosticsSinkFailure`. Failure attribution carries source/item identity when relevant.

The public consumer signatures are locked as the following shape (ordinary
`containers::ArraySpan` caller storage; no heap-owned return object or consumer
subscription):

```cpp
struct EditorActivityCursor
{
    u64 serviceGeneration = 0;
    u64 diagnosticSequence = 0;
    u64 activityGeneration = 0;
};

class EditorActivityService : public application::Service
{
public:
    [[nodiscard]] virtual bool CaptureDiagnostics(
        const EditorActivityCursor& cursor,
        containers::ArraySpan<EditorActivityDiagnosticRecord> records,
        EditorActivityDiagnosticCapture& capture,
        EditorActivityFailure* failure = nullptr) const noexcept = 0;
    [[nodiscard]] virtual bool CaptureActivities(
        const EditorActivityCursor& cursor,
        containers::ArraySpan<EditorActivitySourceSnapshot> sources,
        containers::ArraySpan<EditorActivityItem> items,
        EditorActivityCapture& capture,
        EditorActivityFailure* failure = nullptr) const noexcept = 0;
    [[nodiscard]] virtual EditorActivityCancelAdmission RequestCancel(
        EditorActivityKey key, EditorActivityFailure* failure = nullptr) noexcept = 0;
    [[nodiscard]] virtual bool ClearDiagnostics(
        EditorActivityFailure* failure = nullptr) noexcept = 0;
    [[nodiscard]] virtual EditorActivityServiceStatus GetStatus() const noexcept = 0;
};
```

`EditorActivityDiagnosticCapture` reports the current service generation, requested/
retained/copied diagnostic sequence range, copied count and `gap`; `EditorActivityCapture`
reports the current service generation/activity generation and exact source/item counts.
`EditorActivitySourceSnapshot` carries source identity/generation, copied name, item range,
fresh/stale state, last-success generation and current attributed source failure.
`EditorActivityItem` carries the exact key, kind/state/attention, copied bounded label and
detail, optional integer `completed/total`, cancellable flag, and the last published
queued/request-result presentation for that key.

A default/zero consumer cursor starts at the current service instance. Any nonzero cursor
from another service-instance generation fails `StaleCursor`. Diagnostics capture reports
requested/retained/copied sequence ranges plus an explicit gap and advances only to what
was actually copied. Activity capture copies one entire published activity generation;
insufficient caller source/item capacity returns `CapacityExceeded` without partial output
or cursor mutation.

Producer registration remains private in
`editor/private/vanguard/editor/editor_activity_service.hpp`. It owns
`EditorActivitySourceToken {index, generation}`, `EditorActivitySourceDescriptor`,
`EditorActivitySourceWriter`, the query/cancel callback types, the fixed bounds and the
register/unregister/revoke access used by the shell. Source-token and service-instance
generations are process-wide nonzero/nonwrapping `u64` values. The query callback writes a
complete snapshot through the bounded writer; the cancel callback receives the exact
`EditorActivityKey` and must re-read the authoritative producer operation generation
immediately before forwarding one owner request.

The private producer shape is locked as:

```cpp
struct EditorActivitySourceToken
{
    u32 index = ~u32{0};
    u64 generation = 0;
};

using EditorActivitySourceQuery = bool (*)(
    EditorActivitySourceWriter& writer, void* userData,
    EditorActivityFailure* failure) noexcept;
using EditorActivitySourceCancel = EditorActivityCancelResult (*)(
    EditorActivityKey key, void* userData) noexcept;

struct EditorActivitySourceDescriptor
{
    EditorActivitySourceId id = 0;
    const char* displayName = nullptr;
    EditorActivitySourceQuery query = nullptr;
    EditorActivitySourceCancel requestCancel = nullptr;
    void* userData = nullptr;
};
```

`EditorActivitySourceWriter::Add(...)` accepts one bounded item descriptor and returns
false with an attributed failure on duplicate item ID, invalid/zero item generation,
invalid progress, overlong copied text or per-source/total row overflow.
The writer has no direct publication pointer and no method that can tick or cancel work.

The service owns one main-thread source-callback depth guard covering both query and cancel
callbacks. While that depth is nonzero, **every mutating Activity entry point** rejects
without mutation: public `RequestCancel` returns `ReentrantMutation`, `ClearDiagnostics`
fails with `EditorActivityFailureCode::ReentrantMutation`, and private source register,
unregister and owner-revoke operations fail/reject with the same attributed failure. This
prevents a query/cancel callback from queueing work into the transaction currently being
sampled/dispatched or invalidating its registry traversal. Read-only `CaptureDiagnostics`,
`CaptureActivities` and `GetStatus` are permitted during a source callback but observe only
the last completely **published** generation/status; they never expose private staging or
trigger publication. Source callbacks still may not recurse into UI/panels.

The one implementation unit is
`editor/framework/src/editor_activity_service.cpp`. Its process-static private
`EditorActivityDiagnosticsBridge` owns only a
`containers::MPSCLockFreeQueue<EditorActivityDiagnosticIngressRecord, 512>` plus fixed
counters,
active service generation and exact diagnostics sink token/state. It never stores a
service, panel or extension pointer. Service `OnInitialize` reserves the service generation,
attaches/reuses the inactive bridge sink, then registers the inactive frame participant as
its last fallible step; `OnStart` publishes the new service generation and enables the
participant. `OnQuiesce` closes source/cancel admission,
clears the bridge active generation, revokes all sources/discards cancels, makes the frame
callback a no-op, then attempts sink detach. `OnStop`/`OnShutdown` repeat only idempotent
safety barriers. A detach failure retains the exact bridge token/state.

The exact initialize order avoids an unrollbackable fallible step after frame registration:
reserve the process-wide service generation first, resolve the required frame capability,
attach/reuse the diagnostics bridge sink while its active generation is still zero, then
register the inactive Activity frame participant **last**. If frame registration fails,
best-effort sink detach follows while the bridge remains inactive; a detach failure still
targets only process-lifetime bridge storage. Once participant registration succeeds,
`OnInitialize` performs no later fallible operation. `OnStart` is the only transition that
publishes the service generation into the bridge and flips frame execution active.

The template argument is intentionally 512: exact imported RED
`MultipleProducersSingleConsumerLockFreeQueue<T, QueueSize>` exposes `QueueSize` usable
entries. Status/tests therefore assert the locked **512 usable diagnostics-ingress
records**, matching the implementation template value. P6 executable validation corrected
the earlier sentinel-slot assumption after it allowed a 513th record and broke overflow/
drop sequence-gap evidence.

### Service and frame topology

P0 reserves the following currently-unused IDs; the repository-wide literal scan found no
collision:

- `EditorActivityServiceId = 0x6564616374737601ull`;
- `EditorActivityCapabilityId = 0x6564616374636101ull`; and
- `EditorActivityFrameParticipantId = 0x6564616374667201ull`.

The service/capability IDs live in `editor_service_ids.hpp`; the frame participant ID lives
with the public activity contract in `editor_activity.hpp`, matching existing ownership
patterns. The later service descriptor is Editor/Tool, EditorWorkspace, MainThread and has
one required EngineHost dependency on `FramePipelineServiceId` only. Generic activity core
does not depend on workspace, UI, commands, shell or asset streaming.

To preserve the GPU/toolkit-free focused target, `EditorActivityService::OnInitialize`
resolves the frame capability directly through
`ServiceContext::FindCapability(FramePipelineCapabilityId)` and casts to the interface; it
does not call the out-of-line `FindFramePipelineService` helper, which would force the
focused target to link the aggregate `engine` library solely for lookup glue.

Production composition registers Activity before UI for readability. More importantly,
`EditorUiService` gains a required `EditorActivityServiceId` dependency and its BeginFrame
descriptor changes from `after={Input}` to `after={EditorActivityFrameParticipantId}`;
the Activity descriptor itself has `after={InputFrameParticipantId}`. `EditorCommandService`
keeps its existing UI dependency/shortcut edge. `EditorShellService` gains a required
Activity dependency so its activation context can borrow the exact activity service.
The resulting structural order is `Input -> Activity -> UI -> CommandShortcut`; no proof
may rely on registration order or numeric IDs. CURRENT Frame Pipeline permits forward
dependency IDs during registration and validates them at schedule compilation, so this
topology creates no registration-order cycle.

### Shell ownership seam

`EditorExtensionActivationContext` gains `GetActivity()` returning the borrowed exact
`EditorActivityService&` plus `RegisterActivitySource(...)`. The borrowed service may be
kept only in activation-generation-bounded extension/factory state. The source token is
not the borrowed reference: it is entered as one new `ActivitySource` kind in the existing
1024-entry `OwnedRegistrationRecord` ledger.

Shell `RevokeOwner` must call Activity logical revoke alongside command/panel/surface
revoke before `RemoveOwnedRegistrations` and before extension `deactivate`. That logical
revoke marks all matching sources unreachable and discards matching queued cancels even if
later physical token cleanup fails. Reverse cleanup then consumes the exact source token.
Partial activation rollback follows the same ledger path; no second activity ownership
table is introduced in shell.

## Cancellation request contract

Panel code calls only `EditorActivityService::RequestCancel(exactActivityKey)`. That operation:

1. must run on the editor main thread;
2. rejects with `ReentrantMutation` while a source query/cancel callback is executing;
3. validates that the exact published source/item generations are still current, the
   source snapshot is fresh and the item is currently cancellable;
4. enqueues one bounded request without invoking the source callback;
5. coalesces an exact duplicate already queued for that key; and
6. returns explicit stale/unsupported/capacity failure rather than guessing.

Successful admission does not mutate the published activity snapshot/generation or its
queued/result fields. Those fields are presentation from the next activity transaction,
not an immediate side effect of a panel call.

At the next activity tick the service validates the key again before invoking the live
source's cancellation callback once. The source maps the real owner's result into a
small typed request result such as Accepted, AlreadyRequested, Busy, Stale/Unsupported
or Failed. **Accepted means only that the request reached the authoritative owner.**
E3.5 records that result; it never loops on Busy and never declares the operation
Cancelling, Cancelled, Failed or otherwise terminal merely from the request result.
Producer snapshots alone establish the producer's subsequent state.

Owner/source revoke discards queued requests for that source. Shutdown never calls a
callback after its source token has been removed.

## Diagnostics retention and overflow

The console is a bounded recent view, not the authoritative log archive.

- When the retained ring is full, admitting a new non-coalesced record evicts the oldest
  record and increments an eviction counter.
- When the cross-thread ingress is full, the producer callback drops the new console copy
  and increments a dropped-ingress counter. The original logger/file/debugger sinks are
  unaffected.
- The service exposes drop/eviction/coalescing/text-truncation counters directly. It does
  not recursively log "logs were dropped" through the same callback path.
- `Clear` clears only the editor retained view and related view counters/generation; it
  does not flush, truncate or reconfigure the underlying process logger/file sink.

Transient console filters operate over retained records only. Filter persistence is
E3.6 work.

## Production presentation

E3.5 adds ordinary E3.3 singleton panels through statically compiled production
extension ownership:

### Console

- reads only the retained diagnostics model;
- offers bounded level/category/text filtering over those retained entries;
- shows repeat/overflow state without generating recursive diagnostics;
- may clear only the retained editor view; and
- never subscribes to the logger itself.

### Background activity

- reads one immutable activity generation per UI frame;
- displays current status/progress rows;
- enqueues cancellation requests through `EditorActivityService` only;
- treats accepted cancel as pending until the source reports a new state; and
- performs no `Tick*`, build, wait, filesystem or backend query from draw callbacks.

The production UI extension registers ordinary E3.2 workspace commands and E3.4 View
menu contributions to reopen these panels. It uses E3.3 singleton panels and the existing
UI host/docking path. No new status bar or toolbar surface kind is introduced by E3.5.

Panel visibility/filter/open state is not persisted in this slice; E3.6 owns that.

## Project-asset adapter

E2 is the first concrete typed consumer, but its dependency remains outside the generic
activity core.

A small editor-side built-in adapter/extension may reference `ProjectWorkspaceService`
and `ProjectAssetPipeline` public APIs to publish rows for:

- automatic recook / explicit reimport state;
- VDDC maintenance;
- loose-output maintenance; and
- observation/recovery status when it has meaningful operator-facing state.

The adapter may read copyable status and bounded diagnostic spans already owned by the
pipeline. Every borrowed span/string is copied into the activity query's bounded scratch
before the adapter returns; neither the service nor any panel retains an E2 pointer/span.
It must not call `TickAutomaticRecook`, `TickVddcMaintenance`,
`TickLooseMaintenance`, `TickObservation`, `Rescan`, build or publication APIs.

CURRENT exact-source review found that `CancelAutomaticRecook`, `CancelVddcMaintenance`
and `CancelLooseMaintenance` accept no operation-generation/precondition argument.
`activeAcceptedGeneration` is accepted-source evidence, `protectionEpoch` is a protection-
snapshot epoch, and loose maintenance exposes no operation generation. None can therefore
prove that a delayed editor click still addresses the operation that produced its row.

P0 consequently locks the first project-asset adapter rows as **display-only for
cancellation** under CURRENT E2 APIs. P4 must not expose a Cancel affordance for those
rows merely because an existing `Cancel*` function exists. If P4 requires cancellability,
the allowed correction is a small backend-owned neutral operation-generation/precondition
surface in `assetStreaming`, with its own backend proof; only then may the editor adapter
revalidate the exact generation and forward one request. Editor types or shell ownership
must not cross into E2. Observation disablement is not task cancellation.

The generic activity service contains no `assetStreaming` implementation headers and no
E2 state machine.

## Subscription and lifecycle rules

1. Activity quiesce first closes source/cancel admissions, publishes the service as
   non-active in the process-lifetime diagnostics bridge, marks every source unreachable
   and discards all queued cancellation requests. Those logical barriers happen before
   any operation that can report a cleanup failure.
2. Diagnostics sink unregister is then attempted and a successful unregister remains a
   no-callback-after-return fence. **Safety does not depend on EngineHost stopping after a
   lifecycle failure:** if unregister itself reports failure, any surviving callback still
   targets only the inactive process-lifetime bridge, never destroyed service storage.
3. CURRENT Frame Pipeline has no participant-unregister API and E3.5 does not add one.
   Quiesce marks the activity participant inactive so `ExecuteFrame` returns Success
   without touching producer/presentation state. EngineHost does not execute more frames
   after teardown begins; the descriptor may remain in Frame Pipeline storage until that
   service is destroyed without requiring a new unregister path.
4. The shell depends on the activity service, so shell owner revoke/panel teardown is
   attempted before activity-service teardown. Activity quiesce nevertheless revokes all
   remaining source reachability itself as a safety backstop if a dependent cleanup
   reported failure; it never assumes that a failure prevented EngineHost from proceeding.
5. Source query/cancel callbacks are never executed while the registry is being mutated
   or from a panel draw callback. Once quiesce starts, no source query/cancel callback is
   invoked again.
6. Lifecycle failures remain reported for diagnosis, but no use-after-free prevention
   relies on fail-closed lifecycle propagation. Every callback-facing pointer is either
   logically unreachable before return or refers only to process-lifetime bridge storage.
7. Hiding, closing or detaching Console/Background Activity changes only presentation.
   It does not unregister producer adapters, stop the diagnostics sink or cancel work.

## Failure and read-only status model

E3.5 uses one bounded activity failure model with source/item attribution where useful.
The exact P0 enum spelling is recorded above and distinguishes:

- invalid state/argument/descriptor;
- wrong thread;
- duplicate source/item identity;
- source/row/cancel capacity exceeded;
- generation exhausted;
- stale token/activity key;
- owner mismatch;
- source query failure;
- cancellation unsupported/rejected/busy/failure; and
- diagnostics sink registration/lifecycle failure.

`EditorActivityServiceStatus` exposes bounded current evidence rather than an event log:

- service state and current snapshot generation;
- live source/activity counts;
- diagnostics ingress/retained counts;
- dropped, evicted, coalesced and truncated diagnostic counts;
- pending cancellation count;
- source-query/cancellation failure counts; and
- latest attributed failure.

This status may itself be presented by E3.5 but does not recursively register an
activity source for itself.

## Threading and reentrancy

- Diagnostics ingestion is the only E3.5 operation accepted off the editor main thread.
- The diagnostics callback performs bounded copying/queue admission only.
- Service/source registration, source publication, cancel request admission, clear and
  panel model visitation are main-thread operations.
- Activity source query/cancel callbacks execute only from the activity frame participant.
- A source callback must not recursively mutate Activity state or call back into a panel.
  The callback-depth barrier rejects `RequestCancel`, `ClearDiagnostics` and producer
  register/unregister/revoke until the callback returns; read-only capture/status remains
  permitted against the prior published generation only.
- Visitors receive borrowed read-only rows valid only for the call; consumers do not
  retain internal pointers across activity generations.
- No panel-owned mutex may ever be acquired by a logger/background producer.

## Test and proof strategy

### Diagnostics foundation

Extend `diagnosticsSmoke` to prove:

- registered sink tokens coexist with the legacy callback slot;
- multiple registered sinks receive one message exactly once;
- unregister is stale-token safe and guarantees no later callback;
- concurrent unregister/register cannot reuse a `Retiring` slot or share the prior
  generation's in-flight counter; the slot becomes reusable only after the fence completes;
- concurrent same-token double-unregister joins the same retirement fence; neither caller
  may report success/stale while an old-generation callback can still be in flight;
- sink capacity and generation exhaustion fail explicitly without disturbing existing
  sinks; and
- asynchronous flush/unregister behavior does not lose lifetime safety.
- reentrant self-unregister/register from a sink callback fails without deadlock or
  partial registry mutation.

### Focused activity core

Add one GPU/toolkit-free `editorActivityTests` target compiling the exact production
activity core/service pieces needed to prove:

- cross-thread bounded diagnostics ingress and drop accounting;
- retained-ring eviction and strict adjacent coalescing;
- deterministic drain budget;
- source registration/duplicate/capacity/stale-token/generation behavior;
- transactional source snapshot publication and rollback on invalid/overflow/failure;
- query failure/overflow retaining bounded prior rows only as stale evidence with cancel
  suppressed, followed by a successful publication clearing stale state;
- latest-value coalescing rather than per-frame history growth;
- wrong-thread rejection;
- source query/cancel callback reentrancy rejects `RequestCancel`, `ClearDiagnostics` and
  producer register/unregister/revoke with no mutation, while read-only capture/status sees
  only the prior published generation;
- bounded cancel queue, duplicate coalescing and exact-key stale rejection;
- accepted/busy/failed cancellation request result handling;
- `Capture` before/after `RequestCancel` in one UI frame remains identical with the same
  generation; only the next activity publication exposes queued/request-result state;
- slow-reader diagnostic capture with explicit sequence-gap reporting and no
  per-consumer backlog;
- identical diagnostics separated by an ingress drop do not coalesce across the missing
  sequence; capture still reports the gap;
- owner revoke invalidating source/cancel callbacks before private state death; and
- service quiesce with queued diagnostics/cancel requests.
- EngineHost continuation after injected quiesce/unregister failure cannot make a logger,
  frame or source callback dereference destroyed activity/extension state.
- service restart after an injected diagnostics-unregister failure retains/reuses or
  explicitly quarantines the exact bridge sink token and never registers a duplicate.
- the Activity participant descriptor requires Input and becomes a safe no-op at quiesce;
  the real compiled `Input -> Activity -> UI` schedule is proved by the existing
  application-lifecycle integration target rather than a fake focused scheduler.

### Shell/extension integration

`editorShellTests` owns only generic extension registration-ledger, activation rollback,
borrowed-consumer and revoke ordering for activity sources, using a fixture Activity
service/source. It intentionally does not compile ImGui/rendering/RHI/window/assetStreaming
or the production presentation/project-asset extension. Production built-in descriptor and
catalog membership are source/lifecycle/native integration responsibilities.

`editorCommandTests`, `editorPanelTests` and `editorSurfaceTests` remain the authoritative
oracles for their existing domains; E3.5 adds only targeted regression cases when its
production extension uses those APIs.

### Native/lifecycle integration

Reuse the existing `editorApplicationLifecycleIntegrationTest` and
`editorNativeUiIntegrationTest`; do not add a second native executable or product test
mode.

The lifecycle target already compiles the production static catalog. The native target
intentionally removes `extensions/editor_static_extensions.cpp` and supplies
`tests/support/native_ui_integration_test_catalog.cpp`; P5 must therefore make that test
catalog compose the authoritative production E3.5 descriptor provider(s) plus the native
test extension. It must not redeclare/copy independent production IDs/names/callbacks.

`editorApplicationLifecycleIntegrationTest` is the real Frame Pipeline oracle for the
compiled BeginFrame edge: Activity after Input, UI after Activity, independent of
registration/numeric ID order. It also owns the EngineHost continuation proof with the
real service dependency graph; the focused Activity target owns deterministic injected
failure mechanics and callback-lifetime assertions around that service contract.

The production-shaped native fixture should prove, at minimum:

- worker-thread diagnostics become visible through the copied Console model;
- an activity source keeps advancing while its panel is hidden, detached and closed;
- reopening observes the newest snapshot rather than replaying panel-owned polling;
- one real cancel interaction queues exactly one owner request and later reflects the
  source-reported cancelling/terminal state;
- closing a panel with cancellation pending neither cancels implicitly nor loses the
  source subscription; and
- retained E3.3/E3.4 panel/menu/input/native-host behavior remains intact.

Screenshots may be supporting evidence only; semantic acceptance uses service/test
telemetry, not pixel comparison.

## Expected production file scope

P0 refreshed this set against exact CURRENT. The locked narrow set is:

### Existing modules likely extended

- `source/diagnostics/include/vanguard/diagnostics/diagnostics.hpp` and its backend
  implementation for removable bounded callback-sink registration;
- `source/diagnostics/private/vanguard/diagnostics/sink_registry.hpp` plus
  `source/diagnostics/premake5.lua` for the exact private registry core/generation-source
  test access used by `diagnosticsSmoke`;
- `editor/framework/include/vanguard/editor/editor_service_ids.hpp` for one activity
  service/capability identity;
- `editor/framework/src/editor_ui.cpp` only to add the explicit
  `EditorUiFrameParticipantId -> EditorActivityFrameParticipantId` schedule dependency;
- `editor/private/vanguard/editor/editor_shell_service.hpp` and
  `editor/extensions/editor_shell_service.cpp` for owner-stamped activity-source
  registration and exact cleanup;
- `editor/src/editor_application.cpp` for one service registration in the existing
  composition;
- `editor/extensions/editor_static_extensions.cpp` and
  `editor/private/vanguard/editor/editor_static_extensions.hpp` for the authoritative
  production E3.5 built-in descriptor catalog/provider seam;
- `editor/premake5.lua` and `editor/tests/README.md`.

### Narrow new production files confirmed by P0

- `editor/framework/include/vanguard/editor/editor_activity.hpp`;
- `editor/private/vanguard/editor/editor_activity_service.hpp`;
- `editor/framework/src/editor_activity_service.cpp`;
- `editor/extensions/editor_activity_presentation.cpp` for the statically compiled
  Console/Background Activity panel, commands and View-menu contributions; and
- `editor/extensions/editor_project_asset_activity.cpp` for the editor-side project-asset
  source adapter. Production descriptor getters are authoritative and are reused by the
  native test catalog rather than separately re-declaring production descriptors.

No production file under `source/assets`/`source/assetStreaming` should depend on editor
headers for E3.5.

### Tests/support likely touched

- `source/diagnostics/tests/diagnostics_smoke.cpp`;
- new `editor/tests/editor_activity_tests.cpp`;
- `editor/tests/editor_shell_tests.cpp`;
- `editor/tests/support/native_ui_integration_test_catalog.cpp` plus existing phase-neutral
  native/lifecycle support only where real activity integration evidence needs it.

The new `editorActivityTests` project is added only in P1. It is explicit-file and
GPU/toolkit-free: no wildcard `tests/support/**`, no `editor_ui`/extensions/panels/surfaces,
ImGui, rendering, RHI, window or assetStreaming and no link to the aggregate `engine`
project. It compiles the exact activity service/core plus a narrow test Frame Pipeline
capability fixture and links only the application/diagnostics/concurrency/containers/
memory/system compatibility set required by those exact files. Real Frame Pipeline/UI
schedule compilation is additionally proved by the existing application-lifecycle target,
which already owns the full production composition.

Any materially larger production surface requires a P0 finding explaining why the
accepted owners cannot satisfy the contract.

### Locked P1-P5 file/target split

- **P1:** diagnostics changes stay in the existing public/backend/src/compat files plus
  the new private `sink_registry.hpp`, `source/diagnostics/premake5.lua` and
  `diagnostics_smoke.cpp`. Editor core changes are `editor_activity.hpp`,
  `editor_activity_service.hpp`, `editor_activity_service.cpp`, `editor_service_ids.hpp`,
  `editor_ui.cpp`, `editor_application.cpp`, `editor/premake5.lua` and the new
  `editor_activity_tests.cpp`. No extension/panel/backend adapter is P1 work.
- **P2:** add Activity source ownership only in `editor_shell_service.hpp`,
  `editor_shell_service.cpp` and `editor_shell_tests.cpp`; the generic core/source
  registry changes remain in the activity files. No production presentation or E2 adapter.
- **P3:** add `editor_activity_presentation.cpp` and production static-catalog/provider
  membership for Console. Targeted command/panel/surface regressions are added only where
  this real extension uses those already-accepted APIs. Exact-current implementation also
  requires one narrow private shell seam: `EditorExtensionActivationContext::GetOwnedPanels()`
  returns an activation-generation-bounded owner-scoped panel facade. It can create an
  instance only for a panel type already owned by that exact extension during activation,
  and later change visibility only for an exact panel handle owned by the same extension.
  This does not expose `EditorPanelRegistry`/the full UI service, add a locator/owner,
  permit registry enumeration/destroy/type mutation or permit topology mutation from the
  View-menu callback.
- **P4:** extend the same production presentation extension with Background Activity and
  add `editor_project_asset_activity.cpp` plus its production catalog membership. No new
  focused executable is introduced merely for the E2 adapter: CURRENT E2 domain behavior
  remains its backend's tests, adapter forbidden-call/copy/ownership constraints are
  source-level oracles, and real product mapping/activation is exercised by the existing
  lifecycle/native integration path. `editorActivityTests` remains assetStreaming-free.
- **P5:** production code changes are limited to lifecycle defects found by review plus the
  narrow catalog-ownership refactor required to keep one authoritative production provider
  aggregate shared by the normal and native catalogs. Test support reuses
  `editorApplicationLifecycleIntegrationTest` and `editorNativeUiIntegrationTest`;
  synthetic log/activity/cancel stimulus remains under `editor/tests/support/`, while the
  actual Console/Background Activity panels and descriptor providers remain production.

No second diagnostics test executable, project-asset activity executable, lifecycle
executable or native executable is part of the locked P0 shape.

## Ordered implementation gates

### P0 - exact-CURRENT architecture and test-shape lock

**Status: CLOSED on 2026-09-24. P0 only was authorized.**

Source/planning review only. Re-read diagnostics sink internals, concurrency queue
primitives, Frame Pipeline/UI ordering, shell owned-ledger rules, panel/surface APIs,
ProjectAssetPipeline status/cancel surfaces and RED progress/cancellation evidence.
Lock exact APIs/files/bounds, callback unregister semantics, frame ordering and native
fixture shape. Obtain fresh independent exact-CURRENT review.

No production C++, Premake generation, build, executable test or native editor run.

### P1 - diagnostics sink registration and bounded activity service core

**Status: CLOSED on 2026-09-24. P1 only was authorized.**

Add removable diagnostics sink registration while preserving the legacy callback API.
Implement one activity service with bounded cross-thread ingress, retained diagnostic
ring, service status/failure model and BeginFrame drain participant. Add focused
diagnostics/activity tests. Broad execution remains deferred to P6.

P1 closure is source/static only: no Premake generation, compilation, executable test or
native editor execution was performed. Fresh final exact-current review is
**0 BLOCKER / 0 HIGH / 0 MEDIUM** after correcting sink retirement/shutdown lifetime,
wrong-thread/read-only failure attribution and the locked async/reentrancy/cross-thread
focused-oracle gaps.

### P2 - typed source registry, snapshot publication and cancellation routing

**Status: CLOSED on 2026-09-24. P2 only was authorized.**

Implement generation-safe activity sources, transactional copied snapshots, exact-key
cancellation queue and source callback routing. Integrate source ownership with the E3.1
shell ledger and prove logical revoke before extension private-state teardown. No backend
adapter or panel owns progress.

P2 closure is source/static only: no Premake generation, compilation, executable test or
native editor execution was performed. Fresh final exact-current review is
**0 BLOCKER / 0 HIGH / 0 MEDIUM** after correcting callback-visible transaction isolation,
total-row writer budgeting/oracles, shell Activity target/fixture ownership and durable
authorization/status reconciliation.

### P3 - Console production presentation

**Status: CLOSED on 2026-09-24. P3 only was authorized.**

Add the production built-in Console singleton through the normal extension/panel/command/
surface routes. Present retained diagnostics, filters, repeat/overflow evidence and local
clear without reading/reconfiguring the underlying logger. Preserve toolkit privacy and
do not add persistence.

P3 closure is source/static only: no Premake generation, compilation, executable test or
native editor execution was performed. Fresh final exact-current review is
**0 BLOCKER / 0 HIGH / 0 MEDIUM** after narrowing the exact-current panel instance/reopen
seam to owner-scoped EditorOwnedPanelAccess, adding adversarial cross-owner/revoke
oracles, and verifying the production provider/catalog, Console retained-diagnostics
presentation, workspace reopen command and View-menu contribution.

### P4 - Background Activity presentation and first typed backend adapter

**Status: CLOSED on 2026-09-24. P4 only was authorized.**

Add the production Background Activity singleton and one editor-side project-asset
adapter. Show automatic-recook/reimport and maintenance status from existing E2 query
surfaces. Under exact CURRENT E2 APIs these project-asset rows remain **display-only** and
Background Activity exposes no Cancel affordance for them. The optional cancellation branch
was not taken in P4: no neutral backend operation-generation/precondition surface was added,
and the editor adapter forwards no project-asset cancel request. The adapter must never call
E2 `Tick*`/`Rescan`/build/publication paths.

P4 closure is source/static only: the production presentation provider now owns one
Background Activity singleton with an ordinary Workspace reopen command and existing View
anchor contribution; draw copies one complete bounded Activity generation and routes generic
Cancel only through EditorActivityService for rows that declare themselves cancellable.
The separate built-in project-asset adapter registers one shell-owned Activity source and
publishes four fixed copied status rows from only IsInitialized plus the automatic-recook,
VDDC-maintenance, loose-maintenance and observation getters. Its source has no cancellation
callback and every project-asset row is non-cancellable. Production catalog composition
preserves the Console provider and adds the authoritative project-asset provider. Fresh
final exact-current review is **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No Premake generation,
compilation, executable test or native editor execution occurred in P4.

### P5 - lifecycle hardening and production-shaped integration fixture

**Status: CLOSED on 2026-09-25.** P5 only was authorized and completed source/static;
at P5 closure, P6 and E3.6+ remained unauthorized.

Extend the existing phase-neutral native/lifecycle fixture only as needed to exercise
real Console/Activity presentation, hidden/detached/closed continuity and queued cancel
routing. Finish source-level failure/lifecycle/hygiene review and prepare the exact P6
matrix. P5 may make the narrow product catalog-ownership refactor needed for that fixture:
one authoritative `GetEditorProductionExtensions()` aggregate is shared by the normal
compiled catalog and the native test catalog, while the native catalog appends only its
test provider. This is a production composition point, not a test seam: do not copy product
IDs/callbacks into test support or add product test modes. Do not add another native
executable.

P5 closure uses one authoritative production extension aggregate shared by product and the
native custom catalog, fail-closed required-provider composition, one shell-owned synthetic
Activity source in test support, producer progression outside panel/query callbacks, worker
diagnostic observation through `EditorActivityService::CaptureDiagnostics`, semantic
close/reopen/cancel lifecycle coverage, and source-prepared real-input hidden/detached/
redocked/cancel proof in the existing native runner. Fresh independent exact-current
source/static reviews of the fixture/runner and architecture/lifecycle/cleanliness both
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. Scoped `git diff --check`, provider/product-test
isolation scans and PowerShell parser validation are clean. No P5 generation/build/test/
native execution is claimed; the prepared matrix remains for P6 only.

### P6 - comprehensive validation, documentation and acceptance

**Status: CLOSED on 2026-09-27.** P6 was explicitly authorized and the complete locked
acceptance matrix is now green. Fresh Premake generation, serialized Debug + Development
full builds, all focused executables, retained lifecycle bootstrap/workspace paths, both
noninteractive native configurations and the required Debug real-input native path all pass.
P6 corrected the exact 512-entry diagnostics-ingress instantiation plus narrow test-support/
runner defects exposed by execution; the final mixed-DPI runner correction keeps detached
native hosts fully inside the selected monitor before later physical input, without weakening
the telemetry oracle. Fresh final independent exact-current reviews are 0 BLOCKER / 0 HIGH /
0 MEDIUM. **E3.5 is accepted. E3.6+ remain unauthorized/unstarted.**

Run fresh Premake VS2022 generation. Build serialized `/m:1` Debug + Development at
minimum:

- `diagnosticsSmoke`;
- `editorActivityTests`;
- `editorCommandTests`;
- `editorPanelTests`;
- `editorSurfaceTests`;
- `editorShellTests`;
- `editorApplicationLifecycleIntegrationTest`;
- `editorNativeUiIntegrationTest`; and
- production `editor`.

Run the freshly built focused tests in both configurations, retained application
lifecycle coverage, Debug + Development noninteractive native lifecycle and the minimum
real-input native path required by the final P5 fixture. Warn immediately before any
automation that moves the real mouse, changes desktop focus or drags native windows.

Run scoped/full `git diff --check`, final-newline checks, target membership, production/
test isolation, duplicate-service/route scans, and explicit searches for:

- a second logger or diagnostics archive;
- panel-owned backend polling/ticking;
- blocking logger callbacks;
- unbounded diagnostics/activity retention;
- alternate cancellation ownership;
- E3.6 persistence/layout implementation;
- E3.7 demonstration-extension implementation; and
- theme/styling work.

Obtain fresh independent exact-CURRENT architecture/acceptance review and resolve every
BLOCKER/HIGH/MEDIUM before marking E3.5 accepted. Reconcile parent E3 plan, roadmap,
development map and active planning triplet, then stop. E3.6+ remain unauthorized until
explicitly started.

## Minimum acceptance matrix

| Scenario | Required outcome |
|---|---|
| Diagnostics callback on logger worker | Bounded copy/queue only; no retained borrowed pointer or UI/backend call |
| Diagnostics ingress full | New editor copy dropped + counted; underlying logger/file/debugger sinks continue |
| Retained diagnostics full | Oldest editor entry evicted + counted; bounded memory |
| Strict repeated log | Adjacent identical record coalesces with repeat count; unrelated records do not |
| Identical logs separated by ingress drop | Records remain separate across the missing sequence and capture reports the gap |
| Slow/hidden diagnostics reader | Bounded capture reports a sequence gap and resumes from retained data; no subscriber backlog grows |
| Multiple diagnostics consumers | Registered sink token coexists with legacy callback and other sinks |
| Sink unregister | No callback after unregister returns; stale token rejected |
| Sink unregister racing register | Retiring slot is not reusable until prior-generation callbacks drain and storage becomes Free |
| Concurrent same-token unregister | Both callers join the same fence; neither returns while an old-generation callback may still execute |
| Sink self-unregister from callback | Explicit reentrant rejection; no self-wait/deadlock or registry mutation |
| Source snapshot succeeds | Complete validated snapshot replaces that source atomically |
| Source snapshot fails/overflows | Previous rows retained but explicitly stale/non-cancellable; partial new rows never visible |
| Source recovers after stale snapshot | Fresh publication atomically replaces rows and clears stale/failure state |
| Same activity row each frame | Latest value coalesces; no per-frame history growth |
| Cancel clicked | Request queued only; no backend callback during panel draw |
| Capture before/after same-frame cancel admission | Identical rows and generation; pending/result state appears only on next activity publication |
| Cancel key stale/reused | Rejected; cannot retarget new source/item generation |
| Producer restarted between click and dispatch | Fresh owner-generation revalidation rejects the old request; no new operation is cancelled |
| Cancel accepted | One backend request; UI remains pending until later source status confirms |
| Cancel Busy/failed | Explicit bounded result; no automatic retry loop |
| Source callback re-enters Activity mutation | RequestCancel/clear/register/unregister/revoke reject with ReentrantMutation and no state change; read-only capture/status sees prior publication only |
| Extension owner revoked | Source unreachable + queued cancels discarded before private-state teardown |
| Console/Activity hidden | Ingestion/sampling/background work continues |
| BeginFrame schedule | Activity runs after Input and before Editor UI through explicit dependencies, never numeric/registration ordering |
| Panel detached | Same retained model; no duplicate polling or subscription |
| Panel closed/reopened | Latest snapshot visible after reopen; no implicit cancellation |
| Project-asset adapter on CURRENT E2 | Reads/copies existing status APIs only; rows stay display-only for cancellation; never calls pipeline `Tick*`, `Rescan`, build or publication paths |
| Runtime/headless | No editor activity service/panels |
| Shutdown with queued ingress/cancel | Logical admissions/source/cancel barriers happen before fallible cleanup; no stale callback |
| Cleanup failure then EngineHost continuation | Surviving logger callback targets only inactive process-lifetime bridge; activity frame is no-op; no destroyed service/extension dereference |
| Service restart after sink-unregister failure | Exact bridge token state is retained/reused or attachment fails; no duplicate diagnostics sink/callback |

## Explicit non-goals

- No second logger, log-file tailer or persistent editor log database.
- No universal message/event bus.
- No scheduler/job system, build coordinator or backend polling loop owned by a panel.
- No automatic retry/cancellation policy beyond routing one explicit user request.
- No asset classification, cooking, recook ticking, publication, cache maintenance or
  source observation logic moved into editor shell code.
- No direct backend callbacks on logger threads.
- No unbounded message/task history.
- No E3.6 layout/filter/user-preference persistence.
- No E3.7 demonstration extension.
- No new renderer/presentation path, native window owner or input pump.
- No visual theme/style pass.
- No dynamic plugin loader.

## Definition exit gate

The E3.5 definition may close only when:

1. exact-CURRENT Vanguard diagnostics/editor/backend ownership and relevant RED patterns
   have been re-read;
2. diagnostics sink lifetime, cross-thread copying and overflow behavior are explicit;
3. generic activity/source/cancellation ownership is separated from E2/backend work;
4. frame ordering proves one immutable UI snapshot without panel-owned polling;
5. extension/source revoke and service shutdown are stale-safe even when EngineHost
   records a cleanup failure and continues teardown;
6. focused/native test responsibilities and P0-P6 implementation gates are explicit;
7. parent/roadmap/development-map status is reconciled;
8. scoped whitespace/link/status hygiene passes; and
9. fresh independent definition reviews return 0 BLOCKER / 0 HIGH / 0 MEDIUM after any
   corrections.

Definition closure is not E3.5 implementation authorization.

## Definition closure record

- Exact-CURRENT Vanguard diagnostics, Frame Pipeline, EngineHost, shell/panel/command/
  surface ownership and ProjectAssetPipeline status/cancellation contracts were re-read.
- Relevant RED progress, cancellation, notification-lifetime and main-thread presentation
  patterns were studied; polling/unbounded/message-bus patterns were explicitly rejected.
- The first independent architecture preflight returned **0 BLOCKER / 4 HIGH /
  4 MEDIUM**. All eight findings were incorporated into this contract.
- The first documentation/status review returned **0 BLOCKER / 1 HIGH / 1 MEDIUM**;
  both findings were corrected and its post-fix recheck returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Adversarial follow-up found and corrected explicit Frame Pipeline ordering,
  EngineHost-continues-teardown lifetime, stale-snapshot cancellation, diagnostics-gap,
  same-frame cancellation-publication and bridge-token restart edge cases. Final narrow
  post-fix adversarial rechecks returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- A fresh final exact-CURRENT architecture review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** with no remaining definition gap.
- Scoped `git diff --check`, local-link, trailing-whitespace and final-newline checks pass.
- No E3.5 production implementation, Premake generation, compilation, executable test or
  native editor execution was performed or claimed in this definition gate.

## P0 closure record

- Exact-CURRENT diagnostics/RED queue, Frame Pipeline, EngineHost unwind, shell owner
  ledger, service composition, E2 status/cancel identity and real target/catalog shapes
  were refreshed read-only before locking implementation/test contracts.
- The combined initial P0 preflights/prime audit exposed **0 BLOCKER / 3 HIGH / 5 MEDIUM**
  planning gaps; all were corrected in documentation/planning only.
- The post-lock architecture/lifecycle review then exposed **0 BLOCKER / 2 HIGH /
  2 MEDIUM**, followed by one **0 BLOCKER / 0 HIGH / 1 MEDIUM** RED queue-capacity
  correction. All were resolved, including sink retirement/double-unregister fencing,
  callback reentrancy, CURRENT E2 display-only cancellation and the exact 512-usable-entry
  RED MPSC queue shape.
- The independent post-fix test-shape/scope/docs review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- The fresh final combined architecture/lifecycle/test-shape/docs closure review returned
  **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
- Scoped `git diff --check`, local-link, trailing-whitespace/final-newline, target-name,
  reserved-ID and no-production-symbol checks are clean.
- No E3.5 production implementation, Premake edit/generation, compilation, executable
  test or native editor execution occurred in P0.

### P1 closure record

- Added removable bounded diagnostics sink registration alongside the legacy callback
  while retaining one aggregate RED diagnostics sink owner.
- Added the process-lifetime diagnostics bridge, bounded 512-usable cross-thread ingress,
  2048-record retained ring, exact-repeat coalescing/gap/drop/eviction/truncation evidence,
  service status/failure model and BeginFrame Activity participant.
- Wired production service/frame ordering as Input -> Activity -> UI.
- Expanded diagnosticsSmoke source coverage and added the GPU/toolkit-free
  editorActivityTests target/source shape, including locked async detach,
  callback-reentrancy, cross-thread ingress, drain, restart and lifetime oracles.
- Exact-current review findings were corrected through the final independent
  **0 BLOCKER / 0 HIGH / 0 MEDIUM** recheck.
- Scoped source/static/document hygiene is clean. No Premake generation, compilation,
  executable test or native editor execution occurred in P1; broad execution remains
  deferred to P6.

### P6 closure record

- Fresh `premake5.exe vs2022` generation passed.
- All nine required targets build clean in Debug + Development with normal project
  references and serialized outer MSBuild `/m:1`.
- `diagnosticsSmoke`, `editorActivityTests`, `editorCommandTests`, `editorPanelTests`,
  `editorSurfaceTests` and `editorShellTests` execute cleanly in both configurations.
- `editorApplicationLifecycleIntegrationTest` passes both `--bootstrap-path` and
  `--workspace-path` in Debug + Development.
- The native lifecycle runner passes noninteractively in Debug + Development. The required
  warned real-input Debug run also passes after the test driver was hardened for this
  machine's mixed-DPI monitor boundary by placing each newly detached native host fully
  inside the chosen monitor before subsequent input.
- Final real-input command telemetry is `dockCycle=1 dpiTransition=1 invokes=10 attempts=11
  rejected=1 queries=4269 mode=0 state=4e554954 create=1 destroy=1 toolbarMax=2
  toolbarOffPrimary=0`.
- Final Activity telemetry is `sourceGeneration=1 operation=1 samples=5220 producerTicks=5220
  advances=0 uiInteractions=1 queued=1 callbacks=1 accepted=1 admission=0 state=6`, proving
  exact one-shot public cancellation through Running+queued -> Cancelling -> stable Cancelled.
- The historical P2 conservative 256-row reservation/stale-fallback oracle executes and
  passes in both configurations; the exact 512-entry RED MPSC ingress contract is also
  exercised after the P6 production correction.
- Final full/scoped `git diff --check`, PowerShell parser, product/test isolation,
  provider/catalog ownership and no-backend-polling/no-E3.6+ scans are clean apart from
  existing repository line-ending warnings.
- Fresh final independent exact-current architecture/acceptance and validation/test-shape
  reviews returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

**E3.5 definition and P0-P6 are closed. E3.5 is accepted. E3.6+ remain
unauthorized/unstarted.**
