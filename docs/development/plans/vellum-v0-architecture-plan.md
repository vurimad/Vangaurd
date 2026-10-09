# V0 — Vellum architecture

## Status and authorization

The user explicitly started **V0** on 2026-10-01. V0 is **CLOSED**.

The already-completed Vellum Definition becomes **V0 Definition**. This authorization starts V0's
architecture work only. **V1 runtime implementation remains unauthorized.**

V0 performs source study, architecture definition and documentation. It does not add production
Vellum source, register a new engine service, alter Premake, cook Vellum assets, build tests or run
native applications unless the user separately expands scope.

## V0 objective

Leave V1 with a production-grade, source-grounded architecture contract detailed enough that its
first implementation can be small without being naive.

V0 must answer, before V1 code exists:

- where Vellum modules live and which direction dependencies flow;
- which existing Vanguard service owns composition/lifetime;
- exactly when UI input/update/render publication occurs in the frame;
- what the runtime tree/property/handle/mutation ABI looks like conceptually;
- how authored documents/templates/styles/bindings/animation compile and migrate;
- how semantic invalidation, layout and geometry truth propagate;
- how text, data, behavior and animation plug in without dictating node storage;
- how screen, texture, world and projected UI share one core;
- how UI display lists enter the existing renderer without a second presentation path;
- how asynchronous resource/spawn lifetime works;
- how RTL, scaling, safe-area policy, accessibility semantics and overlays are represented; and
- what cardinality/performance invariants later implementations must prove.

## V0 gate structure

| Gate | Purpose | State |
| --- | --- | --- |
| Definition | Ink/Slate capability study, anti-naive constraints, initial Vellum constitution | **CLOSED** |
| P0 | Exact-current Vanguard ownership/module/frame/resource/render seam lock | **CLOSED** |
| P1 | Runtime data model: handles, node/property storage, mutation and invalidation contracts | **CLOSED** |
| P2 | Authored/cooked document, templates, schema/versioning, dependency and async-spawn contracts | **CLOSED** |
| P3 | Layout/text/input/data/behavior/animation semantic contracts | **CLOSED** |
| P4 | Display-list, renderer, clipping/cache and GPU lifetime contracts | **CLOSED** |
| P5 | Surface/layer/world/projected/overlay/RTL/scaling/accessibility/authoring/diagnostic cross-cuts | **CLOSED** |
| P6 | Adversarial architecture review, V1 implementation contract and V0 closure | **CLOSED** |

All V0 gates are architecture-only. P1 here means **V0 P1**, not program phase V1.

## Definition — closed foundation

The closed Definition established:

- REDengine ink as the capability floor;
- Slate/UMG as a secondary mechanism reference;
- one compact cooked runtime tree;
- stable authored identity separate from fast runtime addressing;
- deterministic structural mutation ownership;
- semantic invalidation domains `TextShape`, `Measure`, `Arrange`, `Paint`, `HitTest`,
  `Navigation`;
- incremental measure/arrange and one authoritative arranged geometry;
- compiled styles, bindings and animation targets;
- first-class virtualization and text constraints;
- renderer-neutral retained display lists;
- screen/texture/world surface direction;
- cancelable/budgeted async spawning; and
- first-class diagnostics/cardinality rules.

Durable evidence remains in:

- `docs/development/plans/vellum-ui-system-plan.md`; and
- `docs/development/studies/vellum-ui-reference-study.md`.

## P0 — exact-current Vanguard ownership lock

### P0 finding: module boundaries

V0 locks the intended dependency direction before V1 source exists:

```text
vellum          game-facing retained UI runtime semantics
  ^
  |
vellumRendering renderer adapter/lowering for Vellum display lists
  ^
  |
engine          VellumService composition over existing engine services

later:
vellumTools     authoring/cook/compiler support; never a shipped runtime dependency
editor          Vellum authoring UI consumes tools/runtime preview APIs
```

The exact Premake project names are not yet production identifiers because no module exists, but
the dependency rule is locked:

- core Vellum does not depend on `engine`, runtime application code or editor code;
- renderer-specific GPU lowering is not embedded throughout the core node/layout model;
- engine composition depends on the Vellum runtime/renderer adapter, not vice versa;
- future tools/editor code may depend on Vellum authoring/compiler libraries; runtime never depends
  on editor tooling; and
- no duplicate window, input, streaming, rendering, job or presentation subsystem is created.

This intentionally resembles the useful separation between RED `inkCore` and `inkRendering`
without cloning RED's global/service architecture.

### P0 finding: engine composition owner

The future `engine::VellumService` is the intended engine-lifetime composition owner for interactive
game/editor/tool use. It will own the process-level Vellum runtime and renderer bridge, while
depending on existing services:

- `FramePipelineService`;
- `InputService`;
- `GameInputService`;
- `WindowService`;
- `ResourceStreamingService`; and
- `RenderingService` when rendering is enabled.

It is a composition/lifecycle boundary, not the owner of the underlying platform or renderer
systems. Exact-current profile coverage makes the service boundary intentionally **interactive**:
`InputService`/`GameInputService` include Runtime/Editor/Tool, while `WindowService` and
`RenderingService` include Runtime/Editor/Tool and exclude Headless. The future Vellum service is
therefore selected for **Runtime | Editor | Tool**, not Headless or Test. Headless document loading,
validation, cook/compiler support or other noninteractive work uses the lower-level `vellum` /
`vellumTools` libraries directly and does not instantiate a half-functional game UI service.

V0 locks **no new EngineHost module** for this service. `engine::VellumService` is registered under
the existing `EngineModuleId`, exactly like the other engine-lifetime composition services. The
future Premake/static-library boundaries `vellum` and `vellumRendering` are code/dependency
boundaries, not an excuse to create another service-lifecycle namespace. Future authoring/cook code
may live in `vellumTools` without owning the game runtime service.

The interactive service is one main-thread engine-scope owner with Required dependencies on
`FramePipelineService`, `InputService`, `GameInputService`, `WindowService`,
`ResourceStreamingService` and `RenderingService`. EngineHost starts providers before Vellum and
unwinds the dependency plan in reverse, so Vellum quiesces/releases UI submissions, resource
requests and renderer borrows before those providers shut down.

Quiesce is a real terminal boundary, not merely a flag: Vellum must stop accepting new document/
surface publication, cancel or release outstanding streaming interest, stop new texture-output demand
and renderer-packet publication, and begin terminal destruction/release of owned semantic/render state.
Drain then requires spawn/materialization/control work, exact-generation resource handles, output/cache
borrows and retained UI frame-packet references to reach terminal state. Only after Drain proves no
Vellum resource generation remains Loaded/Reloading/Evicting/otherwise owned does Shutdown unregister
Vellum resource decoder(s)/materializer callbacks while ResourceStreamingService is still alive. The service must
not rely on RenderingService's private previous-frame-completion participant for this lifetime;
P4 must define an explicit retained packet/receipt contract. Frame-pipeline scratch/frame-pool
storage is not valid backing memory for renderer-retained UI packets that survive asynchronous frame
execution.

### P0 finding: input ordering

Exact-current source establishes:

- `InputService` runs in `FramePhase::Input` as `InputFrameParticipantId` and publishes the current
  `FrameSnapshot` plus bounded raw events;
- `GameInputService` runs in the same phase after `InputFrameParticipantId` and updates the compiled
  `ActionMap` from that exact snapshot/event set;
- `game_input::ContextLayer::UserInterface` already exists;
- action listeners can `Continue`, `ConsumeAction` or `ConsumeControl`; and
- hold, repeat, tap, multi-tap, toggle and axis action semantics already exist.

Vellum therefore does not parse devices twice. Raw pointer/text events come from `InputService`;
semantic navigation/confirm/cancel/game UI actions come from `GameInputService`.

V0 locks one **main-thread Vellum semantic/update participant in `BeginFrame`**, dependent on
`GameInputFrameParticipantId`. Game-input listener callbacks may enqueue bounded semantic events
during the Input phase; the Vellum participant consumes those plus raw input afterward. This lets
UI interaction/state settle before simulation while avoiding re-entrant hierarchy mutation from a
game-input callback.

Vellum's default animation/UI time domain uses `FrameContext::realDeltaSeconds` so menus/HUD can
continue while simulation is paused. Explicit animation/binding domains may opt into simulation or
external time later.

### P0 finding: window/platform ownership

`WindowService` exposes the existing `WindowManager`, which already owns:

- native focus requests and focus/content-scale/window events;
- cursor shape/visibility;
- clipboard read/write;
- text-input/IME activation geometry through `TextInputRequest`;
- per-window safe-area state plus `SafeAreaChanged` notification;
- window/display snapshots; and
- native presentation attachment lifecycle.

Vellum consumes these APIs and never introduces a platform UI backend. One current gap is recorded:
the input event model exposes committed UTF-8 `TextInput`, while no generic pre-edit/composition
event surfaced in the exact-current input/window source. V3/V4 implementation must not fake full
IME composition support; V0 P3 will define the required seam and whether the platform/input layer
needs a narrow extension.

Raw `input::RawEvent` records already carry their originating `window::WindowHandle`, and mouse
motion preserves both local and desktop coordinates. Vellum therefore keeps input routing
window/surface-scoped from day one instead of baking the primary window into pointer state.

### P0 finding: resource ownership

`ResourceStreamingService` owns `ResourceStreamer`, package-set sources and decoder registration
lifetime. Configured sources exist before dependent services initialize and are released after
them. Vellum documents/fonts/vector/media references therefore use this existing source/dependency
authority.

Vellum may register typed decoders/materializers through the existing service in V1+, but does not
create a Vellum depot, package mount, loose-resource resolver or private streaming thread.

Authoring imports/build dependencies remain future `vellumTools`/asset-pipeline concerns and stay
separate from runtime streaming dependencies.

### P0 finding: renderer and viewport ownership

`RenderingService` is already the engine-lifetime renderer composition owner. It exposes descriptor
domains, command system, viewports, render scenes/cameras and residency owners. Its exact-current
frame participants provide stable extension ordering:

- renderer update: `FramePhase::RenderUpdate`, `RenderingUpdateFrameParticipantId`;
- renderer FrameTick: `FramePhase::Render`, `RenderingFrameTickParticipantId`;
- frame sources submitting an `EngineViewport` run after renderer FrameTick; and
- `PresentationService`/`ViewportManager` own native presentation and texture outputs.

Vellum never calls native Present and never attaches a second swapchain to a runtime window.

Exact-current rendering also contains intentionally empty retained graph stages named `DrawHUD`,
`FullscreenVideo` and `RenderFinal2D`, plus `RenderingMode::OverlayOnly` and texture viewport output.
These are the preferred **existing renderer seams to complete**, not evidence that Vellum should
create a separate UI render graph/executor.

The camera graph currently ends directly after `ComposeCameraOutput`; the legacy/final-2D stages
are presently present only in the blank graph and their `Execute` methods are empty. V0 therefore
records a real renderer integration gap: later rendering work must place the chosen Vellum UI stage
into ordinary camera/output composition before terminal `EndRender`/Present while preserving the
retained graph's submission ownership. It must not simply submit a second frame afterward.

`RenderFramePayload` already gives each submitted frame retained opaque payload lifetime across
asynchronous renderer execution. V0 P4 will decide whether Vellum uses a typed extension built on
that lifetime mechanism or a dedicated renderer-owned UI packet interface; V1/V2 must not abuse a
`void*` payload convention without a typed contract.

### P0 finding: runtime viewport composition

Exact-current `RuntimeViewportService` owns the runtime `PresentationService`, presentation output
and `EngineViewport`. It reconciles presentation in `FramePhase::Presentation` after renderer update
and submits the normal world frame in `FramePhase::Render` after renderer FrameTick.

Consequently Vellum screen UI for the ordinary game viewport is **content of the existing runtime
frame**, not a second runtime viewport/presentation owner. Texture/offscreen Vellum surfaces may use
renderer texture-output facilities under their future surface owner, but they are independent renderer
frames by definition and therefore require an explicit bounded frame-source seam. P5 locks that seam:
the interactive `engine::VellumService` composition owns the demand-driven Render-phase submission loop
for Vellum UI-only texture EngineViewports; RenderingService/ViewportManager still owns viewport/GPU/
output lifetime and those texture frames never Present.

### P0 locked dependency/order sketch

```text
PlatformEvents
  -> InputService [Input]
  -> GameInputService [Input]
  -> VellumService semantic/update [BeginFrame]
       raw pointer/text + queued semantic actions
       behavior/data/animation
       mutation commit
       invalidation/layout
       retained display-list publication
  -> simulation/world phases
  -> RenderingService update [RenderUpdate]
  -> Vellum renderer-side base preparation [RenderUpdate, after RenderingUpdate]
       retained lowering/base final-2D packet only; no current prepared-view assumption
  -> presentation reconciliation [Presentation]
  -> RenderingService FrameTick [Render]
  -> ordinary runtime/editor frame source [Render]
       begin exact EngineViewport frame
       renderer prepares one authoritative view family for that frame serial/extents/jitter/regions
       bounded projected-HUD late transforms resolve from that immutable view + world-anchor snapshots
       same prepared family + immutable final-2D contribution attach to the frame, then SubmitFrame
  -> Vellum texture-surface frame source [Render, only demanded texture surfaces]
       bounded OverlayOnly EngineViewport BeginFrame/SubmitFrame; never Present
  -> retained render graph UI stage
  -> terminal renderer/presentation ownership
```

The split between BeginFrame publication and RenderUpdate renderer-side preparation intentionally
keeps UI semantics out of the renderer while giving GPU/resource preparation a renderer-ordered
boundary. V0 P4 will lock the exact packet lifetime and whether unchanged display lists require any
RenderUpdate work at all.

### P0 non-owners

Vellum explicitly does **not** own:

- native windows, cursor backend, clipboard or IME backend;
- physical input polling or game-input mapping compilation;
- package mounting/source arbitration;
- texture/mesh/material residency systems already owned by rendering;
- general job scheduling;
- world simulation/session lifetime;
- swapchains, viewport presentation or Present;
- global game/frontend state; or
- editor panel/docking/application UI.

### P0 known gaps carried forward

P0 intentionally records rather than prematurely implements:

- ordinary camera graph has no functioning HUD/final-2D stage yet;
- current `RenderFrameFeatures` has no typed Vellum/UI packet contract;
- current input path exposes committed text but no generic IME pre-edit/composition event found;
- no Vellum resource types/decoders/compiler exist yet;
- ink exposes no generic safe-zone primitive, but exact-current Vanguard WindowManager already
  exposes platform `safeArea` plus `SafeAreaChanged`; V0 P3/P5 must define how that becomes a
  Vellum surface/layout environment, with game-authored overscan/title/action-safe overrides layered
  on top rather than creating a second platform source;
- accessibility settings exist in RED game/product layers but a generic ink screen-reader semantic
  API was not found; V0 P5 must define Vellum accessibility semantics rather than claiming parity
  through font-size/color settings alone; and
- projected HUD/world-to-screen UI is distinct from world-rendered surfaces in RED and needs its own
  generic projection/edge-clamping seam in V0 P5.

## P1 — runtime data model

**State: CLOSED.** P1 completed at source/document evidence only; no production code/build/test/native
execution was introduced.

### P1 handle and address model

Vellum separates **public identity**, **instance-local hot addressing** and **authored identity**.
They are not interchangeable:

- `VellumInstanceHandle` is `{u32 index, u32 generation}` into the process Vellum document-instance
  registry.
- `VellumSurfaceHandle` is independently `{u32 index, u32 generation}`; a surface may outlive or host
  different document instances over time.
- `LocalNodeHandle` is `{u32 slot, u32 generation}` and is valid only inside one live document
  instance.
- public `VellumNodeHandle`/node reference contains the owning `VellumInstanceHandle` plus
  `LocalNodeHandle`, so the same local slot/generation in two instances can never alias. It never
  contains a raw node pointer and is safe across physical-row relocation.
- internal code distinguishes a stable local **node slot** from a movable **physical row**. Public/
  queued identities use the slot+generation; an O(1) slot-to-row indirection (or equivalent
  relocation-safe mapping) lets dense physical rows compact/repack without invalidating handles.
  Hot passes may carry validated row indices/ranges and do not repeatedly carry the full public
  handle.
- stable authored node/template IDs remain document identities used by cook, hot reload, debugging
  and explicit lookup. They are not the runtime slot address and hierarchy edits do not require
  string-path repair in hot code.

The representation may receive convenience packing/helpers later, but the identity widths/rules are
locked: 32-bit indices, 32-bit generations, value handles, and no correctness dependency on object
addresses. Generation value zero is invalid. A node slot destroyed in one semantic
update is quarantined through the remainder of that update epoch; it cannot be reused by later
mutation/dirty/animation work from the same epoch. Reuse on a later epoch increments generation before
the slot becomes admissible again. Generation wrap must never create an ABA-valid handle: matching
current Vanguard slot-table practice, a slot whose generation space is exhausted is retired
permanently at `0xffffffff` and further admission reports capacity exhaustion rather than wrapping to
a previously valid generation.

### P1 compact runtime storage

One document instance owns a stable generational slot table plus compact physical node storage. The
storage is chunked/pool-backed and row relocation updates only the slot-to-row mapping; it is not one
allocation/object per node. Persistent Vellum storage uses Vanguard's existing memory/pool authority,
never a private allocator subsystem and never frame-pool memory whose lifetime ends before retained
state/packets do.

The generic hot node record contains only data needed by broad traversal/dispatch, conceptually:

- type/schema ID and type-payload index;
- lifecycle/visibility/interactivity flags;
- parent, first/last-child, previous/next-sibling, child-count and ordering metadata as stable local
  slot indices;
- dirty/enqueued domain bits;
- compact layout/transform/cache references or versions needed by generic passes; and
- property-block/schema reference where a field is not promoted into the generic record.

Concrete image/text/panel/control/etc. state lives in type-specific dense stores keyed by payload
index. Expensive or uncommon state (accessibility text, debug provenance, controller objects, large
effect data, authored-ID maps) stays in cold side stores. Generic traversal therefore does not pull
every widget's entire payload through cache.

Topology is an intrusive ordered forest over stable local slots. Child order is authoritative and
deterministic; reparenting rewires links in O(1) topology work and never moves every descendant merely
to preserve preorder contiguity. Physical row order is non-semantic. Implementations may build dense
iterative traversal spans/stamps for a pass, but those are caches over the authoritative topology,
not a second hierarchy. Production traversal does not rely on C++ recursion; materialization and
reparent also enforce a configured maximum hierarchy depth so malformed/deep chains cannot overflow
the stack or create unbounded route construction.

Cross-instance reparent is not a primitive operation: callers detach/destroy and instantiate/adopt
through an explicit document/template path so resource, binding, authored-ID and owner lifetime
remain coherent.

Visibility is an explicit state, not unrelated booleans. At minimum the architecture distinguishes
visible, hidden-but-layout-participating and collapsed/non-layout states; hit-test/interactivity is a
separate policy so layout visibility is not overloaded with input semantics.

### P1 typed property slots

Every node type has a static/runtime-registered **property schema**. Authoring names and reflection
paths are resolved during cook/materialization into typed slot ordinals. Hot writes target a resolved
slot consisting conceptually of `(node index, property slot, expected value type)`; they do not hash
or parse strings each frame.

Each property descriptor carries semantic metadata:

- value type/default/storage class;
- writable source classes (authored override, binding, animation, behavior/controller);
- invalidation domains caused by a changed value;
- optional equality/canonicalization rule; and
- whether the property is generic-hot, type-payload, or cold extension state.

Frequently accessed generic properties such as visibility, transforms and core layout participation
may be direct fields while still exposing typed slot metadata to bindings/animation. A property write
first establishes whether the canonical value actually changed; only a real change publishes its
declared invalidation. Nodes do not allocate a giant fixed table for every property known by their
type: cooked/default-equivalent storage is compact and schema/type-specific, with only needed runtime
slots/materialized side data. Arbitrary per-node string maps are not the default extension mechanism.
Missing or ambiguous invalidation metadata is a registration/cook error, never a runtime fallback to
"rebuild everything."

### P1 semantic invalidation and work discovery

The runtime never scans the complete node arena just to discover that nothing changed. P1 locks one
owner-thread work-discovery model:

- each semantic domain has a dirty worklist/queue and a per-node queued bit or epoch for de-dup;
- writes enqueue work only on clean-to-dirty transition;
- `TextShape` can feed `Measure`; `Measure` propagates toward affected ancestors and may stop when a
  recomputed desired size is unchanged; `Arrange` descends only through geometry made stale by the
  affected layout roots;
- `Paint`, `HitTest`, `Navigation` and later accessibility/display-list cache work are invalidated
  independently when the property metadata permits it; and
- queue consumption may create narrower follow-up work, but there is no mandatory all-node
  invalidation discovery pass.

Visibility uses ancestor gating: a stable hidden/collapsed subtree is skipped from its highest
effective gate without rewriting/scanning all descendant effective flags each frame. Revealing or
changing a collapsed subtree may legitimately schedule O(K) work once because geometry becomes
demanded again.

All invalidation ownership is on Vellum's authoritative semantic thread in P1. No atomic dirty bit
is paid per property/node merely to support hypothetical concurrent writers. Future parallel work may
consume immutable snapshots/ranges after ownership has settled them; it does not change the
single-owner mutation contract.

With at most one dirty entry per live node per domain, each node-domain frontier is provisioned from
the admitted node capacity and cannot require emergency heap growth. Dirty entries carry enough
instance/slot-generation evidence (or are protected by same-epoch slot quarantine) that a reclaimed
slot can never redirect queued work to a replacement node.

Relevant caches carry generation/version evidence sufficient to reject stale reuse after topology,
layout, property or resource replacement. The exact counters can be packed/tiered later; P1 does not
require a 64-bit version field on every node for every domain.

### P1 deterministic mutation and destruction

Hierarchy mutation is command buffered. Event handlers, game-input callbacks and controllers on the
semantic owner may request mutation, but only the Vellum semantic owner applies it at explicit commit
boundaries. Background threads never race arbitrary structural commands into this queue: resource/
data completions publish generation/versioned notifications to bounded ingress channels, and
BeginFrame consumes/coalesces them in a deterministic source/channel order before translating any
structural consequence into mutation commands. Commands are bounded records using handles/indices
and typed payloads; they do not carry arbitrary lambdas that hide ownership or heap lifetime.

Within one commit, commands are applied in stable enqueue order. Each command validates the target's
generation against the state produced by preceding commands. A stale/dead target returns an explicit
`StaleHandle`-class result/diagnostic, never a silent no-op or use-after-free. Destruction dominates
the destroyed subtree: later commands targeting those generations cannot resurrect it accidentally.

Structural commit is non-reentrant. Callbacks emitted while a commit is applying may enqueue work for
a later explicit checkpoint, but may not recursively repair topology or drain the mutation queue.
This mirrors Vanguard's existing main-thread lifecycle-mutation guards and prevents nested callbacks
from observing half-rewired parent/child state.

Subtree destruction performs, in deterministic owner order:

1. mark the subtree terminal for new semantic work;
2. revoke pointer capture/focus/navigation/modal references owned by it;
3. detach it from authoritative topology and invalidate the old parent/affected geometry routes;
4. remove controller ticks, animation targets, binding/data subscriptions and other active-set
   membership;
5. release/cancel node-owned resource/spawn interest through the appropriate owner seam;
6. retire any display-list/render references through the later P4 lifetime contract; and
7. increment slot generations and return slots/payload records to free lists only after the commit
   can no longer observe the old occupants; actual reuse is quarantined until a later semantic update
   epoch so stale work queued earlier in the frame cannot alias a new node.

Reparent is one atomic mutation, not an observable detach followed later by attach. It validates
same-instance ownership, root restrictions, destination liveness and cycle freedom before any link is
rewired. Failure leaves topology/invalidation unchanged. Success invalidates only old/new ancestry and
affected geometry/input/navigation domains; it does not mark the entire document dirty by default.

Detach is distinct from destroy. A detached node remains document-owned under an explicit detached
root set, counts against capacity, and is excluded from surface layout/paint/hit/navigation. Its
ordinary active controller/animation/binding memberships are suspended unless that record explicitly
declares detached execution. Reattach is a structural mutation; destroy is terminal.

### P1 active sets instead of tree ticking

Controllers/behaviors are separate from visual node records. Only controllers that explicitly need
time-based work register in an active controller set; passive controls cost no per-frame callback.
Animation instances likewise live in an active animation set. Notification-driven bindings enqueue
only affected resolved bindings; they are not polled by walking nodes.

Active-set entries retain generation-checked targets and deterministic registration/authoring order
where observable ordering matters. Dense sets may use per-record back-indices for O(1) swap-removal
when ordering is explicitly non-semantic; ordered sets use a stable sequence/tombstone strategy or
explicit ordering key rather than falling back to tree scans. Timer/delayed/polling exceptions live
in due-work structures so the runtime does not scan every alive controller merely to ask whether it
needs an update. Removal is bounded and does not require scanning every node. A node may own multiple
side-store records without becoming a heavyweight polymorphic C++ object.

Virtualized data cardinality is deliberately not node cardinality: a list with 100,000 model records
and 30 visible rows should have roughly visible+overscan row nodes, not 100,000 dormant visual nodes.
P3 will define the data/view contract that preserves stable item identity while recycling those
visual instances.

### P1 capacity, admission and failure policy

Every cardinality-bearing runtime structure exposes configured capacity/hard-limit and diagnostics.
The architecture names, at minimum: surfaces; live document instances; live nodes per instance and
total; runtime property/payload records; behaviors; animation instances; queued structural mutations;
queued/coalesced data notifications; detached roots/nodes; and maximum hierarchy depth. Exact numeric
tuning remains implementation/profile configuration, not an excuse to leave cardinality unbounded.

Vellum may reserve/grow storage in chunks at document/spawn admission boundaries, but a successfully
admitted document's steady BeginFrame/layout/paint/input worklists do not silently grow the heap.
Candidate materialization preflights required node/property/payload/behavior capacity before
publication. Allocation/capacity failure is an explicit result, never undefined behavior.

Cooked documents/templates provide declared counts/hints so materialization can reserve coherently.
Operations that cannot be admitted return explicit failure such as capacity/queue exhaustion before
publishing partial structural state, or remain in a bounded async pending state where P2 permits it.
Mutation-queue overflow is reported to the caller/diagnostics; it does not silently drop a destroy or
reparent request. Event/journal-style queues similarly expose loss/overflow counters and a recovery
policy; overwriting unread structural commands is never an allowed recovery.

Capacity pressure must not prevent the operations needed to free capacity. The mutation/control
design therefore reserves a non-failing terminal lane (or equivalent owner-direct terminal path) for
destroy/cancel/release/service-quiesce operations. Ordinary create/reparent/style work may be rejected
when its lane is full; teardown cannot deadlock behind the work that exhausted it. Data-notification
overflow is also not silent loss: notifications may coalesce by stable source, then mark that source/
instance for authoritative reconciliation at the next semantic update.

Minimum P1 diagnostics include current/peak node and payload counts, free/retired slot counts,
mutation queue current/peak/rejections, per-domain dirty queue current/peak, active controller/
animation/binding counts, generation-stale command/handle counts, and structural work performed per
commit, plus maximum hierarchy depth/sibling count observed. These are aggregate/cardinality counters,
not one profiler object or atomic per node. Expensive topology/property consistency scans are explicit
validation/debug operations, never hidden inside normal `GetStats` or frame discovery.

### P1 adversarial acceptance cases

The data model must remain structurally credible for all of these without changing representation:

- 100,000 data records with about 30 instantiated/visible virtualized rows;
- a large static HUD/menu tree where an unchanged frame performs no whole-tree tick/layout/paint
  discovery;
- thousands of paint-only animation writes that do not trigger measure/arrange;
- a smaller set of layout-affecting animations that invalidates only affected layout ancestry/
  descendants;
- large subtree insert/remove/reparent without relocating unrelated nodes;
- stale external node handles after slot reuse;
- document/resource generation replacement while older render packets are still retained; and
- modal/layer replacement that revokes focus/capture/routes without scanning unrelated documents.

Additional P1 stale/ordering proofs are architectural requirements:

- destroy a node and allocate another in the same update: old handles, dirty entries, animation
  targets and callbacks cannot hit the replacement because reuse is quarantined;
- destroy an instance and create another with identical local slot values: the old node reference is
  stale because the instance generation changed;
- reparenting an ancestor under its descendant fails atomically with no partial invalidation;
- `reparent -> destroy old parent` and `destroy old parent -> reparent` in one batch have deterministic
  different outcomes based solely on admitted order, with stale-target errors visible;
- repeated equal property writes enqueue no dirty work, and repeated changed writes to one already-
  dirty node/domain produce one frontier entry;
- a dirty node destroyed before frontier consumption cannot redirect the entry after slot reuse;
- hidden and detached nodes remain capacity-accounted even though they are absent from ordinary
  surface traversal; and
- renderer-retained packets never borrow mutable node rows, so document replacement can make old
  semantic handles stale immediately while GPU-side retirement proceeds independently.

P1 also locks the expected asymptotic shape. With `N` live nodes, affected subtree size `K` and depth
`h`: handle resolve is O(1); a static warm frame with no events is independent of `N`; paint-only
writes are O(1) enqueue/coalesce; measure invalidation is at most O(h) propagation before affected
frontier work; topology reparent is O(1) plus old/new ancestor invalidation and only later O(K) when
the moved subtree genuinely needs geometry/input/paint repair; destroy is inherently O(K plus owned
memberships); and dirty entries per domain are bounded by `N`.

## P2 — document/resource architecture

**State: CLOSED.** Closed at source/document evidence only; no production code/build/test/native
execution was introduced.

P2 is the authoring/cook/runtime-resource boundary. It must preserve rich reusable authoring without
shipping an editor object graph or forcing runtime name/reflection lookups.

### P2 authored identity and source/cooked split

Authoring uses opaque stable IDs generated independently of hierarchy names/paths. At minimum there
are stable identities for authored nodes and exported templates/components. Rename, reparent and
display-label changes do not alter identity. Names remain human-facing metadata, not primary links.

Stable-ID semantics are operational: IDs are unique within their declared source/resource scope;
rename, reparent, source-file move and undo/redo preserve the same conceptual object's ID; duplicate/
copy creates fresh IDs; override-inserted authored nodes receive IDs; and collisions are cook errors.
Repeated/nested template instances derive a separate composite **origin identity** from export ID +
instance lineage + prototype stable node ID, so reinstance/debugging can distinguish many live copies
of one prototype without changing that prototype's authored identity.

The authoring representation belongs to future `vellumTools` and may use Vanguard's reflected schema
infrastructure where appropriate. The shipped representation is a separately versioned cooked
artifact designed for direct bounded decode/materialization. V0 does not lock a source file extension
or editor serialization UI; it does lock that source/editor data is not the mandatory runtime tree.

The cooker resolves author-facing paths/names/references into compact numeric identities and emits a
deterministic target-platform artifact. Runtime data never contains native pointers, vtables, raw C++
structure dumps or editor service references. Semantically unordered tables are canonically sorted;
authored child order remains explicit where it affects layout/paint/navigation.

### P2 cooked resource family

The minimum runtime resource family is:

- **Vellum document resource** — one or more instantiable roots plus local templates, compiled style/
  binding/animation tables and cold authored-ID lookup data needed by runtime APIs/hot replacement;
- **Vellum template resource** — one independently streamable exported template/component (or a small
  compiler-chosen cohort that intentionally shares one dependency lifetime) with its compiled
  prototype/tables; and
- **Vellum library/catalog resource** — optional lightweight export directory/shared metadata mapping
  stable exported symbols to typed template/shared-resource references. Loading a catalog does not
  imply loading every exported template.

This split is conceptual in V0; exact type names/IDs and file extensions are V1/V9 implementation
details. Both are ordinary typed Vanguard resources loaded through `ResourceStreamingService` /
`ResourceStreamer`. Fonts, textures, vector data, video and other media remain their owning resource
types rather than being copied into a private Vellum depot.

The cooked payload is sectioned and bounds checked. Conceptual sections include header/version/
feature flags, root/template directory, compact prototype node/topology records, typed property data,
string/localization-key pools, style/theme tables, resolved binding programs, animation target/track
tables and cold authored-ID/export lookup tables. Resource dependencies use Vanguard package/loose
metadata rather than hidden path strings embedded in node constructors.

Each instantiable root/template entry records stable export ID, final prototype/table ranges, exact or
conservative upper-bound materialized node/payload counts, required runtime feature/schema IDs and
external-link fingerprints. **Runtime blocking-dependency granularity equals resource granularity**:
if two exports must be independently streamable/admitted, cook emits them as distinct template
resource identities rather than pretending one package Resource record has per-export dependency
slices. Local templates whose lifetime intentionally follows one document may remain embedded.

### P2 templates, components and overrides

Local and external templates use the same authored model. A template has stable exported identity and
an immutable cooked prototype graph. Instantiation copies/materializes that prototype into P1 runtime
slots; the prototype is not a second live mutable widget tree.

Author-time overrides target stable authored node/property identity. Cook validates the target and
lowers the override to `(prototype node ordinal, typed property slot)` plus any structural operation.
Static template inheritance/import chains are canonicalized by the cooker so runtime spawn does not
walk arbitrary string-named inheritance chains. Cycles, missing targets, incompatible property types
and ambiguous overrides are cook errors, not runtime best-effort behavior.

External libraries stay explicit typed dependencies when runtime reuse/sharing is required. A cooker
may inline/bake an external asset only when it deliberately converts that edge into a build-only
dependency. Build dependencies and runtime streaming dependencies are never conflated.

External authored libraries therefore cook to stable template/shared resource identities plus, when
useful, a lightweight catalog. A direct required reference from document A to template B is an ordinary
Required dependency and package planning includes B and B's own closure. A dynamically chosen template
may remain a typed Soft/catalog reference and is requested explicitly at spawn time; packaging tools
must deliberately include such dynamic resources as package roots/content because exact-current
PackagePlanner does not auto-include Soft dependencies. Vellum never relies on a Soft edge magically
appearing in the package.

Compiled **cross-resource** references never store a naked ordinal into another independently cooked
resource. A retained external template/style/theme/animation link contains its dependency-table slot,
stable exported symbol ID and a cooked **link-compatibility fingerprint** for the external ABI it was
compiled against. That fingerprint is separate from ordinary content identity/CRC and hashes only the
public contract a dependent can address: exported symbols, explicitly exposed child/node symbols,
externally writable property IDs/types, required node/control/schema contracts and other link-visible
layout. Private/default visual-content changes therefore need not invalidate dependents merely because
their bytes changed. When the Required dependency generation is loaded, Vellum resolves that exported
ID to the dependency's current local ordinal table and validates compatibility before the parent
resource can materialize. A mismatched/repacked replacement generation cannot silently reinterpret the
old parent's ordinal.

External templates expose an explicit override/link surface. Cross-resource overrides, navigation,
bindings and animation references may target only exported/public child symbols and public property
slots. Private internal stable IDs remain useful to that resource's own authoring/reinstance but are
not external ABI. Cook rejects cross-resource references to private targets, and the link-compatibility
fingerprint includes this public surface so template internals can evolve without freezing every child.

Every materialized instance pins one coherent generation set for the Required resources that supplied
its prototype/style/binding/animation links. If an independently replaced dependency changes an ABI
fingerprint, existing instances remain on their pinned old generation set and a dependent reinstance/
relink is required before adopting the new one. The same rule applies to imported shared style/theme/
animation tables, not only widget templates.

Cook ordering is deterministic and dependency aware: resolve/canonicalize template inheritance and
imports first; apply/validate structural overrides second; assign final local prototype ordinals third;
compile property/style/binding/animation targets against those final ordinals last. Structural edits
therefore cannot invalidate ordinals that were already emitted.

Legal patch/import precedence is semantic and cannot be changed by deterministic table sorting. Base
template values apply first, nested/imported template layers follow their declared composition order,
and the consuming instance's explicit overrides apply last. Style/theme imports likewise preserve
declared cascade order where order is meaningful. Circular imports and ambiguous duplicate exported
selectors/tokens/themes are cook errors unless the source construct explicitly declares an ordered
override layer.

Every extensible authored/compiled payload that can contain a resource reference must participate in
cook dependency visitation through a typed schema/compiler hook. Opaque controller/event/custom-node
blobs are not allowed to hide runtime paths from the package dependency graph. The cooker finalizes
dependency tables only after all node properties, behaviors, animation events and extension payloads
have enumerated their references.

### P2 compiled styles, bindings and animation references

Styles/themes compile authored selectors/tokens/states to typed IDs and sparse property-slot patches.
Runtime state changes resolve already-compiled tables and invalidate exactly the properties affected;
they do not rescan arbitrary descendant strings to discover bindings.

Bindings compile target node/property references to local ordinals/typed slots and compile source
field access to the P3 data-schema/notification representation. Runtime binding execution contains no
default reflection-string traversal.

Animation tracks likewise compile target node/property slots, value/interpolator type and property
invalidation class. P3 defines write-conflict/time semantics; P2 guarantees materialization does not
resolve animation targets by hierarchy path every play.

### P2 resource dependency policy

Vanguard already distinguishes `Required`, `Optional` and `Soft` resource dependencies. Vellum uses
those semantics explicitly:

- structural resources required to construct the requested document/template are Required;
- fallback-capable content whose absence is acceptable may be Optional. **Optional still participates
  in pipeline dependency fan-in and blocks construction until terminal;** its failure is tolerated
  rather than failing the parent; and
- lazy/visibility-dependent media that must not block structural spawn is Soft and requested on demand
  by the owning runtime feature.

Cook emits every **direct** Required/Optional/Soft edge into normal artifact/package metadata. Package
planning computes the selected transitive inclusion closure; Soft resources are not assumed to be
pulled in automatically and must be deliberately available for later demand.

Structural Required cycles are cook/load errors. Required dominates Optional for duplicate references;
dependency-table entries are canonicalized/deduplicated independently of authored occurrence order,
and compiled references use that canonical identity rather than assuming ResourcePipeline's internal
discovery order. Selective export loading is achieved by separate template resource identities, not
by a fictitious per-export slice in Vanguard's resource-wide dependency metadata.

Exact-current ResourceStreamer omits Soft edges from normal blocking load context/OpenSource
dependency copies. A cooked Vellum resource therefore retains compact typed Soft references/indices in
its payload for the owning runtime feature to request later. This is identity metadata only—actual
resolution/loading still goes through ResourceStreamer, so no second dependency database/depot exists.

Existing `PipelineRequest` coalescing lets several Vellum callers share underlying resource work while
retaining independent caller interest.

### P2 decode, limits and versioning

Cooked Vellum resources have their own format version independent of authoring-source schema version.
The decoder validates magic/version, section/count/offset arithmetic, topology consistency, node/
property schema IDs, dependency indices and configured count/byte/nesting limits before publication.
Malformed or unsupported content fails atomically; no half-decoded resource becomes current.

Dependency discovery/construct/decode can execute off the semantic thread, and final ResourceObject
destruction may occur on the thread that releases the last strong handle. Vellum decoder/resource
callbacks are therefore thread-safe immutable-resource work only: they cannot publish nodes, mutate
focus/controllers/data, invoke game callbacks or touch semantic-owner containers. Decoder descriptor,
schema (when used) and callback state remain alive through explicit unregister.

Source migrations are a tools concern: authoring data is upgraded explicitly and cook always emits
the current runtime format. Runtime compatibility is bounded and explicit. A reader may support a
small declared backward window through version-specific readers/up-conversion, but unknown major/
incompatible versions return `UnsupportedVersion`; Vellum does not carry an unbounded legacy
reflection migration engine in shipping runtime.

Cooked node/property schema compatibility is also explicit. The artifact records the runtime schema/
feature version needed to interpret its slot tables so an engine that does not know a node/property
contract rejects the resource instead of reinterpreting bytes accidentally.

### P2 asynchronous spawn/materialization

Resource loading and visual-tree publication are separate lifetimes. Loading an immutable Vellum
document/library resource through `ResourceStreamer` does not automatically instantiate every root.
`VellumSpawnRequest` is a move-only caller interest over a **unique bounded instance-materialization
operation** with states
conceptually equivalent to:

```text
Queued -> Loading -> Materializing -> ReadyToCommit -> Succeeded
   \          \            \              \
    +----------+------------+---------------+-> Failed / Cancelled
```

The exact enum names are deferred, but terminal semantics are not. Cancellation withdraws only that
caller's interest in shared resource work, matching `PipelineRequest`; it never globally cancels a
resource still needed elsewhere. A race with terminal commit reports whether cancellation won or the
instance already succeeded.

Spawn state is monotonic and has one terminal result. Failure/cancel reason is typed enough to
distinguish resource/decode/version/dependency/link mismatch, admission/capacity, owner expiry,
explicit cancellation, materialization and semantic-commit failure. Every request-owned reservation,
pipeline interest and strong staging handle is released **exactly once** on its terminal path.
Cancellation/owner liveness is checked at queue admission, after resource terminal transition, between
budgeted materialization slices and immediately before commit. `ReadyToCommit -> Succeeded` performs a
final generation check of the owner, requested parent/surface and source dependency snapshot; once a
cancel/owner-expiry terminal result wins, no later callback/publication can occur.

Two spawn requests for the same export normally create two independent instances and are not
coalesced at Vellum's materialization layer. Each owns its own pipeline-request interest; only the
underlying ResourcePipeline load may coalesce.

Each spawn request carries generation-checked owner context, priority and declared/admitted work.
Owner expiry cancels the request and suppresses late callbacks/publication. Priority maps to Vanguard
resource load priority and Vellum's own bounded materialization queue; Vellum does not create a
private worker pool or scheduler.

Admission charges real reservations, not advisory estimates. Each request reserves its required P1
node/payload/behavior capacity (or an equivalent isolated staging capacity that guarantees final
commit), plus staged-byte/work accounting, before expensive materialization. Limits exist globally and
per owner/surface so one producer cannot consume every staging slot. Within a priority class ordering
is stable FIFO/sequence; bounded aging/fair-share promotion prevents an endless high-priority stream
from starving admitted lower-priority work. Priority affects scheduling, never correctness or owner
generation rules.

Large materialization may be spread across semantic updates under deterministic work/node/byte
budgets. It may fill reserved **unpublished** P1 slots/staging records incrementally, but no partial
subtree participates in layout, hit testing, bindings or rendering. `ReadyToCommit` attaches the
complete root atomically through P1's structural commit. Cancellation/failure tears staging down
exactly once.

Success transfers the new `VellumInstanceHandle`/subtree result to the caller/owner; the spawn request
does not secretly remain the lifetime owner of a committed instance. Cancel after the commit
linearization reports Completed/Succeeded rather than destroying the instance. Completion/failure/
cancel notifications are delivered to the BeginFrame semantic owner through the same bounded ingress
discipline as P1; worker/resource callbacks never invoke game/UI handlers while the tree is mutable.

After the pipeline reaches Loaded, spawn calls `Acquire()` and retains the exact strong
`ResourceHandle` generation **before** releasing/resetting pipeline-request interest or materializing.
The committed instance owns the coherent strong document/library generation set that supplied its
prototype/styles/bindings/animations. Generation numbers by themselves are not pins.

Off-thread cancellation/owner expiry publishes a bounded terminal-control notification; it never tears
down staging/tree state directly. Resource-interest cancellation uses the pipeline's terminal-aware
cancel result, while Vellum separately linearizes Cancelled vs Succeeded at semantic commit. BeginFrame
never blocks in `Wait()` for a load.

Admission preflights the fully expanded cooked prototype counts plus required behavior/property side
records before incremental staging starts. If dependencies load successfully but Vellum capacity
cannot admit the spawn, the operation fails without publishing nodes and releases only this request's
resource interests; other coalesced callers remain unaffected.

Streamer and Vellum budgets stay separate. ResourceStreamer owns I/O/decode staging and can fail
admission immediately with resource OutOfMemory/LimitExceeded; Vellum owns node/property/materialization
staging and per-update work budgets. Spawn maps those failures explicitly rather than pretending a
streamer rejection is a Vellum pending-budget state.

### P2 replacement, hot reload and reinstance

`ResourceStreamer::Replace`/resource-registry generation cutover is the resource authority. Existing
Vellum instances retain their exact old `ResourceHandle` generation and remain valid while a new
candidate loads. Vellum never mutates an immutable resource object in place.

`Replace` is used only for an existing current loaded generation. Failed/cancelled replacement leaves
the old generation authoritative. Reinstance starts only after replacement succeeds and Vellum has
acquired the new exact strong handle; old live instances keep their old strong generation handles
until semantic/render/resource retirement releases them.

Reinstance is explicit and transactional: build a replacement instance/subtree from the new
generation off-tree, use stable authored IDs to match only state declared transferable by the relevant
control/behavior contract, then swap the root at a semantic commit. Failure leaves the old instance
authoritative. Focus/capture/modal ownership transfers only when the replacement supplies a compatible
mapped target; otherwise it is cleanly revoked. Old instance/resource/render references retire under
their normal generation/receipt lifetimes after cutover.

Replacement compatibility is checked over the complete Required linked-resource generation set, not
only the top-level document. A new document generation that still expects an old external library ABI
cannot bind accidentally to an incompatible newer library. Existing live instances/candidates may
continue using an **already-retained exact old strong handle**. New spawn/reinstance work can use only
the current generation if its link-compatibility fingerprint validates; otherwise it fails/requires a
recooked or republished dependent generation. Vellum never assumes ResourceRegistry can resurrect or
request an arbitrary historical generation after its last strong handle has retired.

Upstream dependency replacement never silently changes a live dependent instance. If library/style/
animation resource B is replaced while document A is unchanged, A's existing instances remain pinned
to B's old compatible generation set until A is explicitly relinked/reinstanced or destroyed. A new
candidate adopts B-new only after fingerprint/link validation. Replacement candidates are themselves
generation fenced: if document/dependency generation N+2 supersedes a still-staging N+1 reinstance,
N+1 is cancelled/stale and cannot commit over the newer desired generation.

State transfer during reinstance matches composite template-origin identity plus compatible runtime
node/control schema, not authored node ID alone. Repeated/nested instances therefore cannot steal one
another's state. Transfer/rollback covers subscriptions, binding source identity, active animations/
transitions, behavior/controller state explicitly marked transferable, resource interests and
focus/capture/modal state. Removed/incompatible targets revoke rather than receiving coerced state.

Editor/project hot reload observes committed loose publication through the existing project asset /
ResourceStreamer path. Vellum runtime does not watch files, rescan source directories or bypass the
asset pipeline.

Authoring source is always authoritative and cook is one-way: runtime/cooked data is never decompiled
back into source as an editing mechanism. Save/undo/migration preserve stable IDs and override records;
copy/duplicate intentionally regenerates identities. When an upstream template edit or migration
orphans an override, tooling preserves an explicit conflict/orphan record for repair and cook fails
deterministically until policy resolves it—no silent override loss. Preview compiles/links the same
representation that runtime materializes.

Copy/duplicate is reference aware. Tooling first allocates fresh authored IDs for the copied closure,
builds an old->new ID map, then rewrites each internal binding/animation/navigation/controller/
override/template reference whose target lies inside that closure. References intentionally targeting
outside the copied closure/resource remain external and unchanged. A copied self-contained component
therefore stays self-contained without accidentally retaining links back to the original.

### P2 admission and diagnostics

The runtime exposes configured bounds for active spawn operations, staged nodes/bytes and per-update
materialization work. Cooked artifacts declare counts/reservation hints so obviously impossible
spawns fail admission before partial publication. Required minimum diagnostics are queued/active/
materializing spawn counts, staged nodes/bytes, priority promotions, cancellations, owner expirations,
load/decode/materialization failures, successful commits, replacement/reinstance attempts and old-
generation instances retained across replacement, plus dependency-generation/fingerprint mismatch
failures and count of coherent old dependency sets retained by live instances.

Quiesce stops new spawn/reinstance admission and emits terminal cancellation/destruction for staged and
live instance ownership. Drain proves spawn/materialization/control queues terminal and all Vellum
strong document/library handles that must be released are gone. Shutdown unregisters Vellum decoder(s)
while ResourceStreamingService is still alive. `activeLoads == 0` alone is not enough: a Loaded/
Reloading/Evicting Vellum resource generation can still prevent loader/decoder unregister.

For the proposed compact custom sectioned cooked format, the default decoder shape is a normal
`streaming::DecoderDescriptor`. `SchemaDecoderDescriptor` is used only if the final runtime resource is
intentionally represented as a VOBJ/reflected schema object; V0 does not force custom sections through
the schema decoder merely because authoring may use Vanguard schemas.

P2 adversarial acceptance includes: two callers coalescing one document load while one cancels;
owner destruction during Loading/Materializing/ReadyToCommit; cancellation racing the commit point;
capacity rejection after dependencies were loaded; an external library generation being replaced and
repacked while an old parent document remains live; cyclic external-template dependencies; structural
override changing child order before binding/animation target compilation; failed decode/version/
fingerprint validation publishing nothing; and transactional reinstance while an older renderer packet
still retains the previous semantic/resource generations. It also includes: one template instantiated
many times/nested while state transfer remains occurrence-correct; a 100-export authored library whose
independently streamable exports cook to separate template resources so spawning one does not block on
the other 99 exports' Required dependencies; custom extension payload resource refs appearing in
metadata; cancel/owner-expire at every expensive phase releasing reservations exactly once; a
high-priority flood that does not starve admitted low-priority work indefinitely; and dependency
generation N+2 superseding a staging N+1 reinstance without allowing N+1 to commit.

## P3 — semantic update contracts

**State: CLOSED.** Closed at source/document evidence only; no production code/build/test/native
execution was introduced.

P3 locks the semantic meaning of a Vellum update. It does not choose renderer primitives or GPU
storage; those belong to P4.

### P3 layout: constraints, measure and arrange

Every layout policy participates in the same two-stage protocol:

1. **Measure** receives bounded min/max constraints in Vellum logical units and returns a desired size.
2. **Arrange** receives the final parent-assigned rectangle and publishes the node's arranged geometry
   plus child rectangles/transforms/clips.

Constraints are finite-or-explicitly-unbounded values with normalized `min <= max`; NaN, negative
extent and invalid transforms are rejected/canonicalized at the property boundary rather than flowing
through layout. Min/max/preferred size, margin, padding, alignment, aspect policy and container-specific
layout data are typed properties. A parent controls child constraints; a child cannot silently escape
its contract by writing absolute framebuffer coordinates.

Measure caches are keyed by the effective constraints plus the versions of the data that actually
affects desired size. A dirty child can propagate measure toward ancestors, but propagation stops once
an ancestor's desired size is unchanged under the same constraints. Arrange begins at the narrowest
stale layout root whose final rectangle may have changed and descends only through affected geometry.

Stack, canvas/anchors, grid, flex-like flow, scroll/viewport and other future containers are **layout
policies over the same node/storage ABI**, not separate runtime trees or mandatory heavyweight C++
widget subclasses. Absolute/anchored placement is supported as a policy, not as a bypass around
measure/arrange.

Arranged geometry is the one semantic geometry truth. Paint, spatial hit indexing, focus navigation,
tooltip anchoring, accessibility bounds and projected/surface interaction consume the same committed
geometry snapshot. None independently recomputes a slightly different layout. Geometry contains the
local-to-surface transform, arranged rect, effective clip and ordering evidence needed by those
consumers; expensive inverse transforms are cached only where input actually requires them.

Scroll offsets and paint-only transforms are classified explicitly. A transform that changes hit
geometry invalidates hit/navigation geometry even if it does not require re-measure; a purely visual
effect may remain paint-only. Property invalidation metadata, not the caller, owns that distinction.

### P3 surface environment and scale

Layout is expressed in Vellum **logical units**. A screen surface receives a versioned environment
snapshot derived from existing owners:

- window-local logical extent and `WindowSnapshot.nativeState.safeArea` from WindowManager;
- framebuffer/pixel extent and content scale from the window/presentation snapshots;
- game/user UI scale and reference-resolution policy from Vellum/game settings inputs;
- locale, UI flow direction and text direction inputs;
- active input-device family; and
- later P5 accessibility/presentation preferences such as text scale or reduced motion.

Platform content scale maps logical coordinates to physical pixels; user UI scale/reference-resolution
policy changes the root Vellum layout transform/available logical space. These are distinct knobs so a
high-DPI framebuffer does not accidentally double-scale authored UI. Resource-quality/atlas resolution
selection is also separate from logical layout scale.

The environment exposes both **full bounds** and **platform safe bounds**. P5 adds game-authored
overscan/title/action-safe policy and popup clamping choices on top. A `SafeAreaChanged`, logical-size,
content-scale, locale, root-flow or UI-scale revision invalidates only the semantic domains its changed
fields affect; it is not permission for a permanent whole-tree per-frame prepass.

### P3 text and localization contract

Text is a foundational measured primitive, not an image drawn after layout. The Vellum text boundary
accepts UTF-8/source text or a localized text key plus typography/style inputs and produces immutable
shaped/layout results containing glyph identities, advances/offsets, line boxes and source-to-glyph
mapping needed by selection/editing/accessibility.

Shaping/layout cache keys include all semantically relevant inputs: resolved text/localization
generation, font family/fallback generation, size/style/features, locale/script/language, base text
direction, wrap/line-break policy and the width constraint when wrapping applies. Color/opacity and
other paint-only text properties do not poison the shaping key. Font/locale/text/wrap changes dirty
TextShape/Measure; paint color changes dirty Paint only.

The text service supports complex shaping, fallback, BiDi ordering, Unicode line breaking, rich runs,
inline images/input glyphs and source/glyph mapping as architectural requirements. Rich text is parsed/
compiled into typed run descriptors; arbitrary markup parsing is not repeated every frame.

Rich runs may also carry a compact **interactive-span identity** for hyperlinks/clickable text without
requiring one runtime node per span. Shaping publishes the span's source range and one or more visual hit
rects/cluster ranges in the immutable text result; P3 hit testing resolves pointer/focus/navigation onto
that sub-run semantic target and routes typed hover/activate events through its owning text node/controller.
The span exposes its own semantic role/name/action to the accessibility snapshot while structural
mutation and lifetime remain fenced by the owning node/document generation. BiDi or line wrapping may
split one logical span into several visual regions, but activation still targets the same stable span ID.

Editable text operates on Unicode **grapheme-cluster caret stops**, not UTF-8 byte increments or raw
code-point increments. The text model retains logical source positions plus shaped visual-caret stops
and affinity. Pointer hit testing maps shaped visual positions back to the nearest legal logical caret
boundary; Left/Right visual movement follows the shaped BiDi caret graph, while logical selection
anchors remain stable source positions. Backspace/Delete remove one grapheme cluster by default;
word/line navigation uses Unicode word/line boundaries and explicit logical-vs-visual Home/End policy.
Selection, composition and accessibility all consume the same source<->cluster<->glyph mapping so a
ligature, combining mark or RTL run cannot split into invalid user-visible caret positions.

Font/fallback readiness is asynchronous and generation explicit. A shaping operation uses one pinned
font-set snapshot/generation for its entire result. If a requested optional font is pending, Vellum
uses the first ready declared fallback (or deterministic missing-glyph fallback) and records that the
shape depends on the pending request; it never blocks BeginFrame waiting for font I/O. Ready/failed/
replaced font-generation notifications invalidate only dependent TextShape/Measure entries. Existing
shaped results retain their exact font generations until replaced/retired, so an asynchronous font
cutover cannot mix metrics from two generations inside one layout result.

Text direction/BiDi and **UI flow direction** are separate. A mixed Arabic/English paragraph may be
BiDi inside either an LTR or RTL UI hierarchy. Root/subtree UI mirroring is defined below/P5 and does
not reverse glyph shaping by pretending text is layout order.

Editable text uses WindowManager's existing `SetTextInput` for activation and IME caret/line geometry,
clipboard APIs for copy/paste, and raw committed `TextInput` for finalized UTF-8. Exact-current input
does **not** expose composition/pre-edit. Before Vellum can claim production IME editing, Vanguard's
platform/input seam must add window-scoped composition events with at least start/update/end or
equivalent state, UTF-8 pre-edit text, composition selection/range/caret information, commit and cancel
semantics. Vellum consumes that seam; it does not poll SDL/Win32 directly or add a second platform
backend.

WindowManager exposes one platform text-input/IME activation state per window. Per-user Vellum
navigation focus does **not** imply several simultaneous platform composition owners in one window.
The surface/window has one generation-checked `TextInputOwner` selected by the deterministic focus/
event rules; granting it revokes the previous composition owner cleanly before `SetTextInput` is
updated. Other UI users may retain navigation focus but cannot concurrently own OS composition on that
same window.

### P3 input routing and event reply

Vellum consumes the exact InputService frame snapshot/raw-event batch plus bounded semantic action
events queued by GameInput during `FramePhase::Input`. BeginFrame is the first place either source may
cause Vellum semantic mutation.

Raw pointing events retain `WindowHandle` and surface identity all the way through routing. The current
backend provides mouse events; Vellum's semantic pointer record may use a generic pointer ID/kind so a
future platform touch/pen extension can enter the same route, but V0 does **not** claim touch/pen input
exists in exact-current Vanguard.

Pointer targeting queries a spatial hit index derived from committed arranged geometry and clips. It
does not linearly walk the full tree for every move. A routed event records an immutable route of
generation-checked node references for that dispatch and runs:

```text
Preview/Tunnel (root -> target)
Target
Bubble (target -> root)
```

Handlers return/merge a typed `VellumEventReply`. The reply can mark semantic handling/consumption and
request bounded side effects such as set/clear focus, capture/release pointer, navigation, text-input
activation/caret update, popup/modal dismissal/open request, cursor change, or start/cancel a generic
drag gesture. Applying the reply is deferred to deterministic semantic checkpoints; event handlers do
not recursively rewrite hierarchy while the route is live.

The generic drag gesture itself has a bounded deterministic lifecycle: Start records pointer/owner/
origin and optional capture policy; admitted pointer movement emits Update with current/delta positions;
release emits End; owner/capture/surface invalidation or explicit cancellation emits Cancel. Generation
fencing prevents a destroyed/recycled owner from receiving a late update/end. This protocol does not
invent a generic drop-target manager: product controls may compose inventory/drop/targeting semantics
from routed drag + hit/focus events when needed.

`Handled` and propagation are separate semantics. `Handled` controls default/action consumption;
`StopPropagation` prevents later nodes/phases on the current immutable route from receiving that event.
Handlers are never allowed to mutate the route in place. Non-conflicting reply effects accumulate; if
multiple handlers request the same exclusive effect (focus target, capture owner, cursor, navigation
destination), the deepest valid node on the route wins, with Target preferred over Bubble and Bubble
over Preview at equal depth. Contradictory requests from one handler are invalid and diagnosed. This
rule is data-driven/deterministic and does not depend on callback race order.

Raw/semantic events are processed in their original admitted order. At the boundary after each event,
interaction-state effects that do not require new geometry—focus, pointer capture/release, cursor,
text-input ownership and action consumption—are committed before the next event is routed, so a mouse
down capture can affect a later move/up in the same batch. Hierarchy/popup structural work remains a P1
mutation request and cannot make newly created geometry targetable until the later structural/layout
settlement checkpoint. Later events therefore never hit-test against half-arranged nodes.

Pointer capture is scoped by user/device/pointer and target generation. Capture bypasses ordinary hit
targeting for the captured pointer but still validates window/surface/modal scope. Destroy, detach,
surface loss, focus loss or owner-generation replacement revokes invalid capture deterministically.

Hover state is derived from a physical hit-test path independently of ordinary routed-event capture,
unless an explicit capture mode requests hover lock. When the physical target changes, Vellum computes
the longest common prefix of the old/new generation-checked routes and emits deterministic transitions:
direct `PointerLeave` for old-only nodes, routed `PointerOut` from the old target with related target,
direct `PointerEnter` for new-only nodes, then routed `PointerOver` to the new target. Ordinary
`PointerMove` is routed afterward to the capture target or physical target as appropriate. Destroyed/
hidden/detached nodes are pruned before transition dispatch, preventing synthetic hover callbacks to
stale generations.

Raw keyboard text/editing keys and semantic navigation are not conflated. Game actions such as
Navigate/Confirm/Cancel come through `GameInputService`'s `UserInterface` context and consumption
semantics; committed text and pointer position remain raw platform input. Vellum does not compile a
second physical-device mapping table.

### P3 focus, navigation, modal scopes and virtual cursor

Focus/navigation state is per **UI user/local-player scope**, not one process-global focused widget.
Each scope tracks keyboard/navigation focus, optional pointer capture, active navigation root/modal
scope and last navigation intent. A surface/layer can be assigned to one or several UI users by the
game-facing composition layer.

Directional navigation first uses explicit authored links when valid, then a deterministic spatial
search over focusable arranged geometry within the active navigation scope. Navigation respects
visibility, enabled state, modal boundaries, flow direction and clipping policy. Focus order for
sequential navigation is stable/authored and later exposed to accessibility; physical slot/row order
is never an implicit focus order.

Focus transitions are transactional: `FocusLeaving`/`FocusEntering`-class notifications observe valid
old/new generations, the final focus owner changes once, and callbacks cannot recursively create an
unbounded focus loop. Node destruction/reinstance either transfers focus through an explicit stable-ID
mapping or clears it according to the enclosing scope's fallback policy.

A gamepad-driven virtual cursor, when enabled, is Vellum interaction state driven by semantic GameInput
axes/actions. It feeds the same hit/capture/hover route as a pointer; it is not another input backend.

### P3 behavior/controller boundary

Visual node types describe presentation/layout primitives. Reusable **presentation behaviors** attach
to nodes/instances and implement UI mechanics such as button state, selection, scroll inertia,
tooltip trigger or transition orchestration. Game-facing **controller adapters** bind those behaviors
to game actions/data/commands. Neither requires a subclass per authored control.

Behaviors/controllers receive a narrow typed context: their owning instance/node, semantic mutation/
property APIs, typed data-source/view handles, action dispatch and explicitly granted game adapter
interfaces. They do not fetch arbitrary engine/global services through a service locator. Platform,
streaming, renderer and audio work remains behind the P0/P4/P5 owner seams.

Lifecycle is explicit: attach/materialize -> activate/mount -> optional due-work updates -> suspend on
detach/hidden policy -> deactivate -> destroy. Only records in P1 active/due sets receive time updates.

### P3 typed data, bindings and virtualization

Game data enters Vellum through typed **data source/view** interfaces. A data schema exposes stable
field IDs, value types and monotonically advancing source/version evidence. Changes publish bounded
notifications containing source identity/version plus affected field IDs or a reconcile marker. The
BeginFrame owner resolves notifications to the compiled binding dependency table produced by P2.

The promised coherent source version has an explicit read contract. A semantic-owner source may
guarantee it cannot mutate during Vellum's binding stage; a cross-thread source instead exposes an
immutable snapshot/read token whose getters remain stable for the token's recorded version until
release. Binding evaluation never combines arbitrary live getters from different versions. If a source
cannot pin a snapshot, it must support end-of-read version validation; a detected version change
discards that binding batch and schedules authoritative reconcile for a later update rather than
spinning/retrying unboundedly in BeginFrame.

A compiled binding stores resolved source-field access, optional typed conversion/program and target
`VellumNodeHandle`/property slot. It is notification driven by default; it never performs a full-tree
or all-binding poll each frame. Explicit polling adapters are allowed only as opt-in due-work records
and appear in diagnostics.

Default bindings are one-way source->Vellum-property. Interactive controls write back by emitting a
typed game/data command through an explicit adapter, not by secretly mutating an arbitrary source from
the property resolver. If a future authored Vellum-property->property dependency is allowed, the cook
must compile a dependency DAG and reject cycles; runtime discovery of recursive binding loops is not a
normal execution path.

Binding evaluation is transactional per semantic update. It evaluates from a coherent source version,
stages typed writes, then applies them through the ordinary property resolver so equality and
invalidation rules remain centralized. A notification overflow/source-version gap requests an
authoritative source reconcile rather than guessing missed intermediate state.

Large collections expose a view contract with stable **ItemIdentity = {ItemKey, incarnation}**,
count/range/version access and an optional template/classifier key. `ItemKey` is unique among live
logical items in the source/view identity domain and remains stable across reorder/sort/filter/group
operations that preserve the same item. Removal terminates that incarnation; reusing the same key for
a logically new item increments/replaces the incarnation so stale selection/focus/animation cannot
attach to an unrelated object. Virtualizing containers compute visible+overscan ranges from the same
arranged scroll geometry, request only that range, and materialize/recycle row templates under explicit
per-update budgets. Focus/selection/animation continuity for a logical item is keyed by `ItemIdentity`, not
by the recycled row node handle. Recycled rows must clear transient presentation state, subscriptions,
capture/focus and item-owned animations before binding a different item.

Virtualization is one of the small set of **post-arrange observers** because its desired materialized
range can depend on the geometry produced by the current arrange (resize, scroll offset, variable row
height). Only registered observers whose geometry version changed run; Vellum never scans all
controllers after arrange. An observer may request bounded row materialize/recycle mutations and a
narrow follow-up measure/arrange frontier. Semantic settlement permits only a configured small number
of such follow-up passes per BeginFrame; if a huge jump cannot be fully populated inside the node/work
budget, the container publishes the best complete visible prefix/range it admitted and continues next
frame rather than recursively rebuilding until exhaustion.

Variable-height virtualization maintains an item-keyed measured-size/position estimate cache separate
from row node lifetime. Recycled rows update estimates after arrange; scroll anchoring preserves an
explicit anchor ItemKey + logical offset when earlier estimates are corrected, preventing visible
content from jumping solely because offscreen item sizes converged. Cache invalidation follows data/
template/width/scale generations rather than discarding every estimate each frame.

Sorting/filtering/grouping can be view operations owned by game/data adapters; Vellum does not require
copying a 100k-item model into visual nodes. A data-source count is therefore never an implicit node
allocation request.

### P3 property source resolution

All systems write through one typed property resolver. Vellum distinguishes the persisted/base
contributors from transient animation so a finished animation reveals the current underlying value
instead of restoring a stale snapshot. The default precedence, lowest to highest, is:

```text
schema default
  < authored/template value
  < style/theme/state value
  < compiled data binding
  < explicit local/behavior/controller override
  < active animation layer(s)
```

This is a semantic channel order, not six separately allocated values on every node. Cook/schema
metadata lets the implementation allocate contributor state only for properties that use it. A
behavior may deliberately clear its local override to reveal the binding/style value underneath.

Multiple active animations targeting one property are not ordered by race arrival. Cook/runtime
records carry explicit animation layer/priority and deterministic authored/start sequence. Non-
blendable conflicts select the highest deterministic writer; blendable property types may use an
explicit blend operator/weight contract. Undeclared blending is an error, not implicit arithmetic.

### P3 animation update contract

Animation instances use explicit time domains: Real/UI time (default, `realDeltaSeconds`), simulation
time where available, and externally supplied/manual clocks for synchronized media/cinematics/tests.
Pause/scale policy is part of the clock, not baked into every track.

Playback state is explicit: playing/paused/stopped/completed, direction, rate, authored/effective time
range, loop policy/count and current iteration. Pause preserves the current animation contributor;
Resume continues from the same timeline position. Stop removes the contributor unless an authored
hold/fill policy explicitly keeps the terminal sample. Seek samples the requested position immediately
and declares whether crossed events are suppressed or dispatched under the normal bounded event policy.
Reverse playback and ping-pong/loop traversal define crossed-event order from the actual timeline
direction; large deltas may cross several loop boundaries but remain subject to the same event budget.

P2 provides pre-resolved target slots and static write sets. Active animations are evaluated first
into bounded temporary result records, then applied deterministically through the property resolver.
Evaluation may later parallelize only when its immutable inputs/write-set partition make that safe;
property application and semantic invalidation remain deterministic. There is never one scheduled job
per track/node.

Animation markers/callbacks are queued during evaluation and dispatched only after property apply and
the current mutation checkpoint, preventing callbacks from mutating a write set halfway through its
evaluation. Stopping/completing an animation removes only its animation contributor so the current
underlying binding/style/local value becomes effective.

Authored animation events are compiled typed operations, not arbitrary function pointers. The event
set can cover property/state/visibility changes, start/stop another animation, controller/behavior
commands, media intent, UI-audio intent and explicit game-adapter callbacks through their owning seams.
Events crossed in one update are ordered by animation layer/priority, animation start sequence, track
and event time/index. Looping/large-delta playback has a configured event-dispatch budget; events are
not silently dropped—remaining crossings carry a continuation/diagnostic policy or the animation is
declared non-catch-up according to authored playback policy. Event effects enter the same bounded
property/mutation/action queues as equivalent non-animation work.

Paint-only animated properties enqueue Paint only. Layout-affecting animations are supported but
their declared invalidation cost is visible in diagnostics; they do not earn a hidden whole-tree
layout shortcut. Text-shaping-affecting animation is allowed only for properties whose schema declares
that cost.

### P3 semantic-update order

Within the BeginFrame Vellum participant, the default deterministic semantic order is:

```text
ingest raw input + queued GameInput actions + resource/data completions
  -> apply terminal/cancel/owner-expiry work
  -> dispatch input/focus/navigation against previous committed geometry
  -> process data notifications/binding evaluations
  -> update due behaviors/controllers/timers
  -> evaluate/apply active animation
  -> dispatch bounded typed animation events
  -> commit requested structural mutations
  -> resolve property effective values/invalidation produced by the above
  -> text shaping + measure + arrange dirty frontiers
  -> run dirty post-arrange observers (virtualization etc.)
  -> bounded follow-up mutation/property/layout settlement if those observers requested it
  -> rebuild affected hit/navigation/accessibility geometry indexes
  -> publish the immutable semantic/display-list inputs consumed by P4
```

The exact implementation may fuse adjacent stages where dependencies permit, but externally visible
ordering must remain equivalent. Input targets the last committed geometry for the frame in which the
platform event arrived; structural/layout consequences settle before simulation and before renderer
publication. No stage recursively restarts the entire update because a callback changed one value;
new work is admitted to the defined current-or-next checkpoint with bounded iteration. Post-arrange
settlement is the explicit exception needed for geometry-dependent virtualization/layout behaviors; it
runs only registered dirty observers and narrow resulting frontiers, never an unconditional second
full-tree pass.

### P3 adversarial acceptance cases

P3 must remain credible for:

- a static 20k-node menu with no input/data/animation doing no full-tree semantic/layout discovery;
- one deeply nested text label changing string and propagating measure only until desired size
  stabilizes;
- thousands of opacity animations without Measure/Arrange work;
- one layout animation in a large tree touching only its layout frontier;
- two local players navigating different focus scopes on one surface without stealing focus;
- pointer capture surviving ordinary pointer motion but being revoked by target destruction/modal
  replacement/window focus loss;
- a 100k-item data view with about 30 visible rows and bounded row recycling;
- data-notification overflow recovering by authoritative reconcile rather than stale UI;
- mixed RTL/LTR text inside an RTL UI without conflating text BiDi and child-order mirroring;
- high-DPI/content-scale changes without double-applying user/reference UI scale; and
- editable text with Latin committed input working now while IME composition remains explicitly
  unsupported until the narrow composition event seam exists.

## P4 — rendering contract

**State: CLOSED.** Closed at source/document evidence only; no production code/build/test/native
execution was introduced.

P4 separates Vellum semantic paint, Vellum-to-renderer lowering and Vanguard rendering. The renderer
never traverses the live Vellum node tree, and core Vellum never emits RHI commands.

### P4 renderer-neutral display list

After P3 geometry settles, dirty paint roots rebuild immutable display-list segments. The core
primitive vocabulary is typed and backend-neutral. The architecture reserves at least:

```text
SolidRect / RoundedRect / GradientRect
Image / NineSlice
GlyphRun
VectorPath / Shape
VideoImage
CustomGeometry (bounded typed extension, never an arbitrary RHI callback)
ClipPush / ClipPop
EffectPush / EffectPop
```

Each draw primitive carries resolved logical geometry/transform, premultiplied color/tint semantics,
z/layer order, typed resource reference(s) and only the material/effect parameters needed to describe
the visual result. There is no device pointer, descriptor allocated by Vellum core or command-list
callback in this representation.

Display lists are segmented at stable semantic boundaries so one dirty subtree does not require
copying/re-recording the whole document. A segment is immutable once published and carries owner/
paint/resource generation evidence. Unchanged segments are retained/referenced in final paint order;
dirty segments atomically replace their preceding generation. Segment reuse is a cache over the P3
hierarchy, not a second mutable proxy tree.

### P4 clipping, effects and ordering

Clip/effect commands form validated balanced scopes. Axis-aligned rectangular clips lower to scissor
where possible; rotated/non-rectangular/nested complex clips lower to stencil/mask work selected by
the renderer. Vellum describes clip semantics, not the backend mechanism.

Each clip scope has an explicit semantic operator and coordinate space. The baseline operators are
`Intersect` and `ReplaceWithinIsolatedScope`; ordinary nested widget clipping uses Intersect. Clip
geometry is expressed in the same committed local/surface transform space used by P3 hit testing, then
lowered to renderer space once. Empty intersection culls the enclosed paint span deterministically.
Clip nesting is bounded by the P4 capacity profile; overflow is a typed admission/fallback failure and
may not silently drop a clip. Hit testing consumes the same effective clip semantics, so a primitive
cannot be visually clipped but still interact outside that clip by accidental implementation drift.

Effect scopes declare a source span plus typed parameters for supported effects such as blur, glow,
color correction, mask/wipe or glitch. Intermediate render targets are renderer-owned. An effect
cannot embed arbitrary RHI work into the display list.

Final ordering is stable from surface layer order + semantic sibling/paint order + explicit local
z-order. Every primitive/span receives an authoritative paint-sequence position. Adjacent compatible
spans may merge freely. The default lowering path preserves this sequence exactly; a non-adjacent
reorder is legal only for an explicit commutative class whose lowering can prove no visible change
(for example opaque/disjoint draws with no destination read under identical clip/effect context).
Resource/material equality alone is never such a proof. Stacking correctness wins over batch count.

Reusable display segments have locally balanced clip/effect scopes: a push cannot begin in one segment
and be popped by an unrelated segment. A segment may still execute under ancestor/inherited clip/effect
state; its immutable header carries an effective-context fingerprint. Lowered-segment cache keys include
that inherited context (plus output/device/resource state), so a child segment cannot be reused under a
different stencil/mask/effect environment merely because its own primitive bytes were unchanged.

Effect types have explicit compositing semantics, not a generic "apply shader" promise. Each registered
effect declares whether it is source-local, source-isolating or destination/backdrop-reading; capture-
bounds expansion; source/input clip behavior; final-output clip behavior; premultiplied-alpha/blend
contract; intermediate-format requirements; nesting compatibility; and deterministic failure/fallback
policy. Source-isolating blur/glow/mask/wipe-style effects capture only their declared source bounds,
process the intermediate, then composite once in paint order. Destination-reading effects explicitly
depend on the already-composited destination and therefore create an ordering/pass barrier.

If an intermediate/cache allocation is denied, the effect follows its registered fallback contract
(for example a cheaper variant, unfiltered source where semantically allowed, or omission/failure of the
affected scope). Correctness-critical masking may not silently become unmasked content. The fallback and
budget reason are diagnostic; stale intermediate contents are never presented as a recovery path.

### P4 typed frame contribution seam

`RenderFramePayload::void*` is lifetime precedent, **not** the Vellum ABI. Vellum instead uses a new
typed renderer-owned final-2D contribution contract in the future rendering API, conceptually a
`rendering::Final2DFrameContribution` retained packet.

```text
vellum::DisplayList / immutable segments
          |
          v
vellumRendering adapter   (knows Vellum + rendering)
          |
          v
rendering::Final2DFrameContribution
          |
          v
existing EngineViewport / FrameRenderer / render graph / Present
```

The rendering module does not include or call Vellum. `vellumRendering` lowers Vellum primitives and
resource identities during normal RenderUpdate preparation. Engine composition associates the typed
contribution with the ordinary presentation EngineViewport frame before submission. For ordinary
screen/presentation UI, the existing runtime/editor frame source remains the only
BeginFrame/ConfigureViews/SubmitFrame owner. P5's explicitly separate UI-only texture/offscreen surfaces
use their one bounded non-presenting frame-source owner because no pre-existing presentation frame exists
for those outputs. Rendering/presentation remains the only Present owner in every case.

Lowering is retained/incremental too. `vellumRendering` caches immutable lowered renderer-segment
generations keyed by the Vellum display-segment generation plus only the renderer/output/resource
state that materially changes lowering. A static segment is not decoded and re-batched primitive by
primitive every frame. Packet assembly references already-lowered spans and performs only bounded
adjacent-span/order reconciliation; resource/output generation changes invalidate only dependent
lowered segments.

The ordered **base surface packet** itself is generation cached. If no display-segment ordering/content,
output encoding/extent or renderer/device epoch changed, RenderUpdate may retain the already assembled
base packet directly rather than walking every segment merely to reconstruct the same span list. If one
segment changes, assembly repairs the affected ordered span/batch neighborhood and publishes a new
immutable base-packet generation. Projection/world late-presentation transforms are a separate bounded
immutable table layered over that base packet at the Render-phase frame-source boundary once the exact
view generation exists; they never force static semantic segments to be re-lowered. This does not
eliminate renderer CPU/GPU work inherently needed to execute the resulting draw batches, but it prevents
static widget/segment cardinality from becoming repeated semantic/lowering discovery overhead.

The submitted contribution contains immutable retained references only: lowered draw/batch spans,
clip/effect state, renderer resource/residency tokens, surface/output metadata, the optional bounded late-
transform table and diagnostics IDs. It never
contains pointers into mutable node rows, frame-pipeline scratch memory or callbacks into live Vellum.
Once submitted, the contribution and every immutable segment/resource it references survive until
renderer frame retirement releases them, even if the semantic document is replaced immediately.

Between RenderUpdate preparation and the existing Render-phase frame source, engine composition owns
one generation-checked **prepared final-2D base slot per EngineViewport/surface binding**. Publishing a
newer base generation atomically supersedes an older **unsubmitted** generation and releases its adapter/
resource pins; it never mutates an already submitted packet. The Render-phase owner snapshots/retains the
newest compatible base generation when constructing the ordinary frame. After the renderer-owned exact
view family for that same frame serial/extents/jitter/output regions is available, a narrow typed
composition adapter may derive the optional bounded late-transform table and freeze the submitted
contribution. It either attaches that immutable contribution or records an explicit skip/reject result.
The frame source sees only renderer/composition types, not Vellum headers, and never calls back into live
Vellum to discover content.

Quiesce stops new prepared publication first, retires every never-submitted prepared generation, then
waits only for already submitted generations through the receipt contract below. Thus a packet cannot
leak forever merely because it was prepared after the last frame that would have submitted it.

CPU retained-frame lifetime is not enough to prove GPU retirement. The typed seam therefore includes
an opaque renderer-owned **final-2D contribution receipt/ticket** per accepted packet generation. Its
observable lifecycle distinguishes at least Pending/AcceptedForGpu and terminal Retired, Skipped,
Failed or DeviceLost-equivalent states. Rejected/skipped packets release CPU pins without claiming GPU
use. Once commands referencing contribution resources are submitted, the renderer transfers those GPU
resource/residency pins into its normal fence-owned retirement records and marks the contribution
Retired only after all relevant queue completion evidence is safe. Device-loss terminal handling is
defined by renderer ownership and guarantees no later GPU access before releasing pins.

Vellum Drain polls/consumes these typed terminal receipts plus resource/output receipts; it never treats
`RetainedRenderFrameRef` destruction or RenderFramePayload release as GPU completion and never adds its
own queue-wide wait/fence owner.

### P4 final-2D render-graph integration

Exact-current blank rendering already orders
`DrawComposition -> CompositionPostProcess -> DrawHUD -> FullscreenVideo -> RenderFinal2D`, but those
final-2D bodies are empty. Exact-current camera rendering performs `ComposeCameraOutput` and then
advances to diagnostics/`EndRender`; it does not yet execute that final-2D chain.

Exact-current `ComposeCameraOutput` is not merely camera-region composition: for a DisplaySrgb output it
already selects `ResolveCameraOutputDisplaySrgb` and writes encoded presentation pixels. Therefore V0
does **not** place ordinary UI after that transfer. The production renderer must split/refactor the
semantic stages when final-2D content exists:

```text
camera/view rendering
  -> compose selected camera regions into a scene-linear final-composition target
  -> Vellum/HUD/video/final-2D contribution in that linear composition space
  -> final output transfer/encoding to FrameOutput (DisplaySrgb or SceneLinear contract)
  -> EndRender / terminal submission / Present
```

For zero final-2D work the renderer may retain the current direct `ComposeCameraOutput` fast path. For a
SceneLinear texture output it may also alias/target an equivalent linear output when resource/state
contracts permit. What is locked is semantic ordering: UI blends **after camera-region composition but
before output transfer/encoding**, never after DisplaySrgb encoding. Blank/`OverlayOnly` graphs reuse the
same final-composition/final-transfer machinery for UI-only/offscreen cases. Vellum never uses
`RenderFrameInfo::SetGraph` as the normal seam, runs a second graph/frame, or Presents separately.

The legacy `DrawHUD`/`FullscreenVideo`/`RenderFinal2D` node names may be retained or refactored when the
renderer implements this seam; V0 locks ownership and ordering, not those names.

### P4 batching and GPU resource ownership

Vellum core never owns renderer descriptors, command allocators, PSOs, GPU fences, device resources or
bindless tables. `vellumRendering` and rendering-owned APIs retain/resolve the GPU-side forms of fonts/
glyphs, textures, vectors, video frames, geometry and effect materials.

Lowering groups compatible primitives into batches/instances. The batch key contains only state that
actually breaks a draw: primitive/pipeline/material variant, resource binding/sampler, clip/effect,
blend/depth where relevant, output encoding and backend-required state. One node is never one draw call
or one render job. Descriptor/bindless/atlas strategy remains renderer-owned and capability-driven;
Vellum cannot assume descriptor-per-widget or mandatory atlasing.

An immutable Vellum display segment owns generation-safe semantic resource references for everything
needed to reproduce its visuals until the segment retires; a lowered segment owns the corresponding
renderer residency/resource tokens. Soft/lazy content can move from fallback/pending to ready by
invalidating the resource-dependent segment generation. No retained packet borrows a raw ResourceObject
pointer whose lifetime is independent of the packet.

Text shaping remains P3 semantic work. Glyph rasterization/cache/atlas placement is renderer-side:
`vellumRendering` requests glyph images for shaped glyph IDs while rendering owns GPU atlas pages,
descriptors and retirement. Missing glyph GPU residency can use fallback/pending rendering without
making core layout query the GPU. Vector data likewise remains vector until the chosen rendering
tessellation/cache boundary; Vellum is not quad-only by design.

All renderer-side Vellum caches are bounded, not only raster caches. This includes lowered-segment
cache entries/bytes, glyph atlas pages/bytes, vector tessellation/mesh bytes, effect-intermediate pools
and any UI-specific instance/upload caches. Each has explicit admission, high-water, eviction and
in-flight pin accounting; an entry referenced by a submitted packet cannot be evicted until renderer
retirement releases that pin. Cache exhaustion follows a typed fallback/failure path rather than
unbounded growth.

Glyph cache identity includes exact font resource generation plus glyph ID, raster size/scale,
variation/features needed by the rasterizer, outline/stroke and color-glyph mode. Atlas UV/page
references used by retained packets are either immutable for the pin lifetime or reached through a
generation-safe renderer indirection; atlas compaction cannot silently move live glyphs out from under
a submitted packet. Vector tessellation/cache keys likewise include source resource generation, fill/
stroke semantics, tolerance/quality and effective output scale.

`CustomGeometry` is versioned/registered, not an escape hatch. Its record contains a stable custom
primitive type/schema ID, declared bounds, bounded immutable payload bytes and dependency references.
`vellumRendering` registers a deterministic lowerer for that type. Cook/decode validates payload size/
version/dependencies; unsupported types produce a typed fallback/failure and diagnostics. The lowerer
may emit ordinary renderer-owned geometry/batches but cannot receive a raw command list or arbitrary
live callback.

### P4 raster caches and offscreen outputs

Subtree raster caching is optional. The first cache is immutable display-segment reuse, which makes a
static subtree cheap without allocating a render target. A cache hint/heuristic may request a
renderer-owned texture for an expensive stable subtree.

Raster-cache entries have hard target/byte budgets, generation-safe owner keys, output scale/color
keys, quality/last-use metadata and dirty regions. Layout/paint/resource changes conservatively dirty
the affected region; complex effects/transforms may expand it or force full redraw. Dirty rectangles
are optimization only—uncertain coverage falls back to full cache redraw rather than stale pixels.

Cache **policy** belongs to `vellumRendering` because it understands semantic segment stability and UI
cost; cache **resources** belong to rendering. The adapter chooses whether a subtree is eligible and
builds the semantic cache key, then requests renderer-owned targets/resources. The key includes the
segment generation, inherited clip/effect context, output color encoding, effective pixel scale/
transform/subpixel policy and every resource generation that affects pixels. A cache hit from a
different scale/clip/color/resource context is invalid even if the authored node ID is the same.

Offscreen Vellum surfaces reuse `RenderViewportOutputKind::Texture` and existing output-revision/
`RenderOutputReceipt` lifetime. Exact-current ViewportManager requires a pre-existing TextureRef, so V2
must expose/compose a **rendering-owned texture-target allocator/pool** that `vellumRendering` requests
from; ViewportManager then retains that target for the texture viewport and owns output revision/
receipt. Core Vellum/engine composition never calls RHI allocation directly and does not invent
unmanaged dynamic textures. `RenderingMode::OverlayOnly` is the normal UI-only mode when a scene is
unnecessary.

### P4 color/output contract

Authored UI colors have explicit source-space semantics and convert to renderer-approved linear
blending representation before composition. Images/video retain declared encoding. Final presentation
transfer/HDR mapping stays with rendering/presentation. Texture outputs declare/consume the existing
`RenderOutputColorEncoding` and output contract rather than performing private state transitions or
signaling unrelated fences. This contract is why final-2D occurs before the renderer's output-transfer
stage for DisplaySrgb presentation rather than blending linear UI directly into an already encoded
UNorm target.

### P4 retirement and drain

Semantic instance generation, immutable display-segment/packet generation and GPU/output generation
are independent. Replacing the semantic document may leave older packets/resources retained safely
until normal renderer retirement.

On Vellum quiesce: stop new surface/frame publication; revoke new cache/output demand; release/cancel
unused residency/output requests; release all prepared-but-never-submitted packet generations; allow already-submitted packets to complete through the normal
RenderCommandSystem/FrameRenderer path; then report Drain only after renderer-retained Vellum packets,
their typed final-2D receipts, cache outputs/residency borrows and streaming interests are terminal.
Vellum does not add Present or a queue-wide GPU wait to retire one document.

P4 capacity is explicit: each surface/profile has configured limits for display commands/bytes,
primitive/custom-geometry vertices, clip/effect nesting depth, lowered spans/batches/instance bytes,
effect/intermediate pixels/bytes, raster-cache bytes/targets and retained in-flight packet generations.
Cook/materialization can preflight static maxima where possible; dynamic overflow fails/degrades through
typed admission/fallback rules and diagnostics rather than hidden allocation or silently truncating a
clip/effect stack.

### P4 diagnostics and acceptance

Per frame/surface diagnostics expose paint-dirty roots, segments rebuilt/reused, renderer segments
lowered/reused, surface packets assembled/reused, primitive counts by
kind, clip depth/complex clips, effect/intermediate counts, raster-cache hits/misses/dirty area,
lowered batches/instances/draws, resource/glyph misses, UI renderer-cache bytes/pages, retained packet
generations and retirement backlog. CPU timing separates semantic paint from renderer lowering; GPU
timing brackets final-2D work through existing renderer timing facilities.

Acceptance requires: a large static HUD reuses segments without full-node re-emission or full
primitive re-lowering; one changed icon/opacity rebuilds/lowers only relevant segments; thousands of compatible primitives do not imply
thousands of draws/jobs; complex clip/effect paths do not infect unrelated primitives; a document can
be replaced while an older frame safely retains its packet; zero UI produces zero final-2D GPU work;
and screen, texture and later world-surface outputs share the same display-list/lowering core.

## P5 — cross-cutting capability contracts

**State: CLOSED.** Closed at source/document evidence only; no production code/build/test/native
execution was introduced.

P5 locks the seams that a basic HUD can easily postpone until they become architectural rewrites.

### P5 surface layer stack

Each Vellum surface owns an ordered set of lightweight layer records, not hard-coded singleton classes.
A layer has stable identity plus ordering band/priority, root instance(s), visibility/update policy,
input participation, modal scope and optional transition state. Products can express HUD, menu,
loading, notifications, fullscreen/media and system overlay roles using these records without making
those names part of the core runtime type hierarchy.

Layer ordering is total and deterministic: semantic band first, explicit layer priority second, then a
stable registration/activation sequence. Equal priorities never fall back to container/hash iteration
order. Each UI user resolves its topmost active modal layer from this same order; lower modal candidates
remain outside that user's hit/focus/navigation route until the higher modal exits.

A layer may declare an optional `game_input::ContextId` contribution, but exact-current
GameInputService exposes one global ActionMap/`UserInterface` context stack and `ActionEvent` has no UI-
user identity. Therefore only **genuinely global UI state** may push/pop that existing context stack.
Controls never do so ad hoc. Per-user/local-player modal layers instead gate/reroute the already-produced
semantic action stream inside Vellum according to composition-supplied device/user assignment; opening
player A's modal cannot mutate the global mapper and change player B's bindings. Shared keyboard/mouse
ownership is likewise a game/composition policy input, not guessed by Vellum.

If product scope needs independent per-player action maps/context stacks, V4 must add/consume a future
GameInput user-scoped action/context seam and tag semantic events with that authoritative scope. Vellum's
own modal/focus/layer state is already per-user and does not collapse to the current global mapper.

Modal ownership is explicit. An active modal layer can constrain hit testing, focus/navigation and
semantic GameInput to its allowed subtree/scope while lower layers remain rendered according to
policy. Opening/closing a modal uses P1 structural/focus transactions; it does not rely on global
booleans sprinkled through controls.

Passive/invisible layers do not pay tree-tick cost. A layer may stay resident but dormant, be detached,
or be destroyed depending on product policy; those states remain distinct for capacity/resource
accounting.

### P5 local visual-state orchestration

Vellum supports compiled **local visual state machines** at document/component/layer scope for UI
presentation flow such as Normal/Focused/Disabled, menu open/close phases, loading transitions or
control substates. A state machine owns typed state IDs, explicit transition edges/priority, entry/exit
operations and references to P3 animation/behavior/property operations compiled by P2.

State changes are semantic-owner operations and deterministic: one accepted transition publishes its
property/state contributors, starts/stops declared animations and queues typed events at the normal P3
checkpoints. Guards depend only on explicit Vellum state/data inputs through compiled typed predicates,
not arbitrary runtime reflection/script polling. Transition interruption/reentry policy is declared per
edge/state and cannot be decided by callback race order.

This is **local UI orchestration only**. Vellum does not own the game's frontend/gameplay state machine,
save/load flow or screen-routing product logic; game-facing controller adapters decide which document/
layer/state-machine commands to issue. The design therefore preserves ink-level visual-state capability
without importing a monolithic global product UI state owner.

### P5 transient overlay/tooltip placement

Tooltips, combo menus, context menus and transient popups use one generic placement solver. A request
contains:

- anchor kind: arranged node geometry, explicit surface rect/point or current pointer;
- ordered candidate placements expressed in logical `Start/End/Above/Below/Center` terms;
- anchor/popup alignment, gap/margins and optional arrow/attachment point;
- allowed bounds: full, platform-safe, game-safe or explicit work rect;
- flip/shift/clamp policy and minimum visible-area policy;
- whether the popup escapes the anchor's clipping subtree into an overlay root;
- focus/input/modal ownership and dismissal policy; and
- owner generation so anchor destruction automatically invalidates the request.

The solver evaluates candidates against committed P3 geometry, mirrors logical Start/End candidates
under RTL, then applies bounded flip/shift/clamp according to policy. It never changes source hierarchy
merely to escape clipping: visual overlay hosting and logical owner/event ancestry are separate, with
an explicit route bridge so dismissal/focus semantics remain deterministic.

Popup sizing/placement has a bounded feedback contract. The overlay root first measures the popup under
an explicit maximum/work-area constraint, solves the ordered placement candidates, and may perform at
most **one** deterministic remeasure + re-solve when the chosen side materially changes the available
width/height (for example a tooltip that must wrap in the remaining right-side space). If content still
exceeds the admitted bounds, authored policy chooses Clip, Scroll/Viewport, ScaleWithinAllowedPolicy or
Fail/DoNotOpen. The runtime never loops measure <-> placement until convergence and never silently lets
an oversized popup escape its declared safe/work bounds.

Dismissal is typed: outside pointer press, focus loss, Cancel semantic action, owner hidden/destroyed,
explicit command and/or timeout. A popup cannot globally steal all users' input unless its modal scope
says so.

### P5 three spatial UI categories

Vellum deliberately models three different spatial problems:

1. **Screen UI** — logical surface maps directly to an EngineViewport presentation/texture output.
2. **World-rendered surface UI** — Vellum renders to a texture/live output mapped onto world geometry;
   pointer/ray interaction maps world hit coordinates back into the surface.
3. **Projected HUD** — a world/entity anchor is projected into an ordinary screen surface and the UI
   itself remains screen-space. This does not allocate a world UI render target.

They reuse the same core document/layout/display-list system but have different demand, projection and
input ownership. Treating (2) or (3) as merely a transform on (1) is rejected.

### P5 projected world-to-screen HUD

A projected marker record contains a generation-safe game/world anchor provider, target screen
surface/user, projection camera/view identity, logical offset/anchor, priority and visibility policy.
Projection is sourced from the **same prepared renderer view generation** used by the ordinary
EngineViewport frame: frame serial, prepared `RenderView` world-to-clip data, selected root-camera/
output-region rectangle and actual jitter/projection state for that frame. Exact-current Vanguard does
not create that prepared family until the Render-phase frame path, so V0 explicitly does **not** query it
from RenderUpdate. The future frame-composition seam prepares one authoritative immutable view family for
the just-begun frame, exposes a read-only projection snapshot from it to the bounded late-transform
adapter, then transfers that **same** prepared family into the submitted `RenderFrameInfo` (the frame
packet already has a prepared-family attachment path) instead of preparing a different camera result
later. Vellum never reconstructs projection from a mutable camera object or owns scene transforms/camera
simulation. Split-screen and multi-region markers therefore project against the exact region that will
render their world view.

Each demanded update produces screen position plus depth/front-facing/on-screen status. Policy may
hide behind-camera targets, clamp offscreen targets to a safe/full edge, choose an edge arrow, expose
distance/depth data to bindings, and sleep/decimate low-priority stable markers. Optional world
occlusion is an externally supplied signal/query result, not a hidden synchronous scene/RHI readback
inside layout.

Projection work is budgeted/prioritized and can use hysteresis so markers near visibility/edge/range
thresholds do not thrash. P3 layout determines the marker/attached-overlay **intrinsic local extents**;
current projection chooses only the late surface placement of that immutable group.

Because the main Vellum semantic pass runs at BeginFrame before simulation, current-frame world/camera
projection is a **late presentation channel**, not a second semantic tree mutation pass. BeginFrame lays
out each marker in marker-local coordinates and publishes immutable marker presentation data. RenderUpdate
may lower/cache that immutable local group, but does not resolve current projection. In the ordinary
Render-phase frame-source path, after `BeginFrame` establishes the exact frame serial/extents and after
the renderer prepares the authoritative view family, the typed composition adapter samples only immutable/
generation-safe world-anchor snapshots plus that read-only prepared view and computes the bounded
projection/clamp/visibility table. It cannot change hierarchy, binding values, intrinsic measure or run
arbitrary controllers. The same prepared view family is attached to the submitted frame, and the accepted
projection record is retained back to the next BeginFrame for hit/navigation/accessibility state when
markers are interactive. This keeps world motion current for rendering without recursively reopening
layout after simulation or inventing an impossible earlier prepared-view query.

This creates an explicit two-stage geometry contract rather than two competing truths:

- **intrinsic committed geometry** is the P3 marker-local arranged rect/clip used by semantic layout;
- **late presentation geometry** is `{prepared view/frame/output-region generation + surface transform +
  effective late visibility/clamp state}` composed over that intrinsic geometry for pixels; and
- the next BeginFrame's hit/navigation/accessibility bounds compose the **last accepted** late
  presentation record over the same intrinsic geometry. If the renderer view/anchor generation is
  missing, stale or no longer matches the surface binding, policy deterministically hides the marker
  (or uses an explicitly authored stale fallback); it never silently reuses a transform under the wrong
  camera/region.

Safe-edge clamp, edge-arrow placement and any tooltip/overlay visually attached to the projected marker
use a bounded **late placement group** over immutable local extents. RenderUpdate may translate/rotate/
selecting/lowering the pre-laid-out marker+attachment arrangement remains ordinary preparation; the
Render-phase late adapter may translate/rotate that group and choose a predeclared edge orientation from
the exact prepared view, but it cannot remeasure text, spawn hierarchy or run the generic popup solver
against current projection.
Projection-dependent changes that truly require different intrinsic layout become next-BeginFrame work.

### P5 world-rendered UI demand and quality

A world surface binds a `VellumSurfaceHandle` to externally owned world placement/geometry plus a
renderer texture output. Its manager tracks demand state from range, frustum, approximate screen
coverage, interaction/focus, product priority and explicit always-live overrides.

The world-surface demand manager is Vellum surface/composition state owned by the interactive
`engine::VellumService`; it is not a second world-streaming or renderer owner. World components hold
generation-checked registrations/anchors into that manager, RenderingService owns texture/viewports,
and world streaming owns component/entity lifetime. Registration loss/owner expiry removes demand
without requiring the world system to know Vellum node internals.

Demand is not binary. Conceptual states include Dormant, Candidate, Live and Cached/Throttled with
hysteresis. Vellum composition selects a bounded **surface render-quality class** plus refresh cadence
and cached/throttled policy from demand, priority and budgets. `vellumRendering` realizes that class into
the concrete renderer target descriptor—pixel extent, raster quality/effect tier and cache policy—through
the P4 renderer-owned allocation seam. The authored/logical surface extent remains an independent
semantic environment value; render-quality degradation does not silently re-layout the document. Hard budgets bound simultaneously live
surfaces, total pixels/bytes, updates per frame and expensive redraw/effect work. Admission uses
priority + screen benefit, never unbounded "every world screen redraws every frame."

Interaction/critical policy can temporarily promote a surface's **priority**, but hard live-surface,
pixel/byte and work limits remain inviolable. Profiles may reserve explicit quota for interactive/
critical surfaces; if that reserve is exhausted, deterministic priority/benefit/age ordering selects
degradation/failure/cached-stale fallback rather than overcommit. An `always-live` request means highest
eligible demand priority inside the configured hard budgets, never permission to bypass them.

Dropping demand never destroys
semantic document state unless policy requests it; it may suspend presentation and retain/collapse
resources according to explicit memory policy. World streaming owns whether the entity/component
exists; Vellum only owns its UI surface/document state.

World surfaces also track an explicit **presented generation** so cached/throttled pixels cannot drift
away from interaction truth. Each accepted texture publication records the Vellum semantic/layout/
display-packet generation that produced it together with the renderer `RenderViewportHandle`,
`outputRevision` and `producerSerial`/receipt generation. The world material samples the last accepted
Ready publication by default. A future same-queue optimization may consume the current producer only
when the renderer expresses an explicit producer->consumer dependency for that exact output generation;
sampling a mutable "current texture" without generation/readiness evidence is forbidden.

If semantic/layout/hit state advances while a Cached/Throttled surface still shows an older presented
generation, interaction follows one declared policy: either it is disabled and the hit promotes/redraws
the surface until a matching publication becomes ready, or the surface retains a bounded immutable
interaction snapshot keyed to that exact presented generation and routes only against that snapshot.
It never routes current P3 geometry/state over stale pixels. Destroy/reinstance/output replacement
invalidates both the presented generation and any retained interaction snapshot atomically.

Ray/pointer interaction uses an external world-hit provider and then enters the normal P3 hit route.
The hit contract is generation fenced, not a bare UV: it supplies the target world-surface registration/
geometry revision plus local UV, hit distance and front/back-facing classification (or equivalent
signed-normal evidence). Each world surface declares maximum interaction distance and whether back-face
interaction is allowed. Vellum validates registration/revision, distance and facing before converting
the hit into a P3 pointer event, then additionally validates the surface's presented-generation
interaction policy above; stale/policy-rejected or pixel/semantic-mismatched hits are discarded/promoted
as declared. Vellum never performs its own physics raycast or world scene query.

The world-space placement used to draw the textured surface is likewise late renderer/world data; it
does not force Vellum to rewrite semantic layout when an entity moves. Only changes to semantic
environment inputs such as logical extent/UI scale, or an explicitly authored conditional-content
`QualityClass`, feed back into layout/content selection. Renderer `SurfaceRenderQuality` is a separate
presentation concern and never aliases that semantic `QualityClass` merely because both are called
"quality" in product settings.

### P5 offscreen/fullscreen/media surfaces

Offscreen UI is an ordinary Vellum surface targeting the P4 texture-output path. Preview tools,
world-surface textures, capture/streaming and composited game effects therefore share one semantic/
render core. `vellumRendering` owns translation from composition-selected `SurfaceRenderQuality` into
the concrete UI target-size/raster/effect/cache descriptor and requests the rendering-owned
texture-target allocator/pool described in P4. ViewportManager then retains the supplied texture and
owns viewport output revision/receipt; it is not the allocation owner. Core Vellum never creates RHI
textures.

Unlike ordinary screen UI, a texture/world surface has no pre-existing runtime/editor presentation
frame that can carry its contribution. The interactive `engine::VellumService` composition therefore
owns one **bounded demand-driven texture-surface frame-source participant** in `FramePhase::Render`
after `RenderingFrameTickParticipantId`. For each admitted/demanded UI-only texture EngineViewport it
uses the normal viewport `BeginFrame`/`SubmitFrame` path with `RenderingMode::OverlayOnly`, attaches the
prepared typed final-2D contribution, and records the normal output/contribution receipts. It does not
use a custom graph, call Present, own command submission, or create a thread/loop per surface. Work is
bounded by P5's per-frame surface-update budget; undemanded/throttled surfaces submit no frame.

Preview/tool composition may provide an equivalent explicit texture frame source instead of selecting
the interactive game service, but there is still exactly one frame-source owner per texture surface and
all paths use ViewportManager/RenderingService. This distinction resolves the apparent rule conflict:
ordinary presentation UI is content of an existing frame; explicit texture outputs necessarily submit
their own non-presenting renderer frames.

Exact-current texture RenderViewport has no in-place output-texture replacement/resize API. A world/
offscreen quality change therefore uses a generation-safe transition: the rendering-side target owner
allocates a candidate texture at the new pixel extent/format; a future rendering composition seam creates
or replaces the texture-output binding as a new output generation; surface publication atomically cuts
to that generation only when ready; and the old viewport/texture target retires only after its output
and final-2D receipts are terminal. Logical Vellum surface extent remains unchanged unless a separate
semantic environment revision explicitly changes it.

Fullscreen video/media can be represented as a layer/media primitive/final-composition contribution;
exact-current Vanguard does not expose a general media/video service owner in the audited engine
composition, so Vellum defines an explicit **media provider adapter seam** rather than inventing one.
The game/future media subsystem owns decoder threads, decode-session/demux buffers, audio/video sync and
produced-frame/session lifetime. Media asset identity, source/package resolution and exact resource-
generation pins remain under P2 `ResourceStreamer`/`ResourceHandle` authority; the provider consumes
those pinned/reference inputs and does not become a second depot/registry. Vellum owns layout/state/
transitions and emits typed media intents.

The provider contract exposes generation-safe media/session handles plus asynchronous status at least for
Pending/Ready/Playing/Paused/Ended/Failed/Cancelled, typed ready/error/end notifications, owner-expiry
and cancellation. Vellum intents cover prepare/prefetch, play, pause, seek, loop/range and stop/release.
Clock ownership is explicit: provider/media clock is authoritative for decoded frame timestamps while
Vellum animation may react to that clock/markers through P3's external clock seam; Vellum never advances
decode by assuming UI `realDeltaSeconds` is media truth.

Ready video frames are immutable generation/timestamp/color-encoding identities consumed by P4
`VideoImage`; packet/resource pinning follows the provider + renderer lifetime contract. Active decode,
prefetch and retained frame demand are bounded/profiled, and a hidden/destroyed/expired UI owner releases
or suspends demand according to authored policy. Provider absence/failure has a typed fallback/error
presentation path; it never causes Vellum to spawn a private decoder thread.

### P5 RTL and UI flow direction

Every surface/subtree resolves `FlowDirection = Inherit | LeftToRight | RightToLeft`. Authoring uses
logical **Start/End** for margins, alignment, anchor sides, flow ordering and navigation where
directional semantics are desired. Containers opt into flow-aware child ordering; absolute physical
Left/Right remains available when art direction explicitly requires it.

Flow direction affects layout order/alignment, default navigation interpretation, scrollbar/
decoration sides, popup candidate order and other UI-level presentation. Icons/images expose an
explicit `MirrorInRTL`/directional-art policy; photographs/logos and other non-directional media are not
mirrored automatically. A subtree can force LTR/RTL for maps, timelines or art.

Changing resolved flow invalidates affected layout/navigation/paint metadata once. It is explicitly
separate from Unicode BiDi inside text shaping.

### P5 safe-area semantics

WindowManager is the sole platform safe-area source. Screen surface environment converts
`WindowSnapshot.nativeState.safeArea` into Vellum logical **platformSafeBounds** beside `fullBounds`.
Vellum adds optional game-authored content insets/policies for overscan/title-safe/action-safe style
needs without pretending those are platform truth.

For a surface bound to a sub-view/split-screen region, WindowManager's safe area remains full-window
truth, but P3 BeginFrame layout cannot depend on a later Render-phase prepared view. Surface composition
therefore owns a versioned **semantic output-region binding** available at/before BeginFrame (for example
the composition-authored region or the last accepted region generation). BeginFrame intersects that
window-local safe rectangle with this binding and remaps the result into surface-local logical
`platformSafeBounds` before root layout or popup solving. An empty intersection is an explicit
no-platform-safe-area condition for that region, not permission to reuse the full-window safe rect.

Later Render-phase prepared-view/output-region data carries/validates the same binding generation for
projected-edge clamping and final presentation. If the prepared renderer region no longer matches the
semantic binding, Vellum does not retroactively relayout the current frame: policy skips/hides or uses an
explicit late stale fallback and publishes a new surface-environment/region revision for the next
BeginFrame. Thus two local-player regions cannot both apply a safe rectangle that belongs to the whole
window, and layout never time-travels to consume future RenderUpdate state.

Layout exposes typed policies such as IgnoreSafeArea, PadInsideSafeArea and ConstrainToSafeArea at
appropriate roots/containers. Popups/projected markers can separately choose full/platform/game-safe
bounds for clamp. Background art can intentionally bleed to full bounds while interactive/content
panels stay safe. Safe-area changes are environment revisions and invalidate only consumers that opted
into the changed bounds.

### P5 device-aware prompts and conditional content

Input prompts bind to semantic `game_input::ActionId`, not hard-coded controller button strings. A
prompt resolver observes the UI user's active/assigned device family plus an authoritative binding
description and resolves localized text and/or a glyph resource variant. Exact-current InputService only
exposes a process/frame-level `lastActiveDeviceType/lastActiveDevice`; that is a useful single-user
fallback, not sufficient authority for split-screen/per-user prompt selection. Game-facing composition
therefore supplies per-UI-user device assignment when needed.

Exact-current `game_input::ActionMap` can execute actions and rebind known `BindingId`s, but it does not
offer a stable public query that enumerates the **current bindings for an ActionId** for presentation.
Before Vellum prompt parity is claimed, GameInputService needs a narrow read-only binding-description
seam (action/context -> current controls/modifiers/display-relevant metadata) plus a monotonically
changing mapping/rebind revision or notification. Vellum consumes that authority; it never reaches into
`ActionMap::Impl`, reparses the mapping resource, or maintains a duplicate binding database. Rebinding,
mapping replacement or active-device change invalidates only dependent prompt runs/nodes.

With exact-current one ActionMap this yields **shared current mapping + per-UI-user device family**.
V0 does not claim independent per-user remap/binding sets. If V4/V5 product requirements need those,
GameInput must expose a user-scoped mapping/binding provider/revision (or distinct authoritative
per-user ActionMaps) and Vellum prompt resolution keys that user scope. Vellum will not synthesize
per-user mappings by copying the global map.

Cooked conditional content supports bounded discriminants such as locale/language, target platform,
device family/input scheme, accessibility/game presentation policy and quality tier. The cooker
validates variant coverage/fallback and lowers it to compact tables; runtime evaluates known keys, not
arbitrary scripts or reflection expressions. Conditional images/video/text remain ordinary resource
dependencies with Required/Optional/Soft classification.

Variant selection has a deterministic total order. Each authored variant declares its matched
discriminant mask plus explicit priority; cook ranks first by configured specificity order/number of
matched discriminants, then explicit priority, and rejects equal-rank ambiguous matches. A single
declared fallback/default handles no-match. Author/source order is never the hidden tie-break. Runtime
environment revisions track which discriminants changed and invalidate only selectors whose compiled
dependency mask intersects those changes.

### P5 accessibility semantic contract

Accessibility is a native Vellum semantic layer, not inferred later from pixels. Accessible nodes can
provide typed role, name/label, description/help text, state flags, value/range/text value, supported
actions, heading/group information, focusability/order and live-region/change policy. Values may come
from localized text/bindings without requiring a duplicate visual tree.

The runtime builds an incremental **accessibility semantic snapshot/tree** from only participating
nodes, preserving logical parent/owned relationships and P3 arranged bounds. Accessibility dirty work
is sparse like other domains. Presentation-only wrapper nodes may be skipped/reparented semantically so
assistive traversal reflects meaning rather than implementation hierarchy.

P5 makes that incremental promise concrete by extending P1 property invalidation with a dedicated
`Semantics`/Accessibility frontier. Property metadata for role/name/description/state/value/actions/
semantic ownership/order marks Semantics without forcing Paint/Layout. Geometry, flow, focusability and
focus changes enqueue the affected semantic records because their accessible bounds/order/state changed.
First-dirty dedup/capacity rules match other P1 frontiers; semantic-only updates never trigger a whole
visual-tree scan or visual repaint unless another property domain also requires it.

Accessibility publication is generation/versioned and bounded. The immutable snapshot carries one
semantic revision; change/live-region notifications are coalesced by accessible node/property and
published through a bounded bridge queue. Overflow never drops the authoritative semantic state: it
marks the bridge for snapshot reconcile, mirroring P1/P3 notification-recovery policy. Rapid live-region
updates can be rate-limited/coalesced by declared politeness/priority without delaying ordinary visual
property settlement.

Keyboard/navigation focus and accessibility focus are related but distinct. A future platform
assistive-technology bridge consumes immutable semantic snapshots and sends typed actions/focus
requests back through Vellum's semantic owner. Vellum core does not directly bind OS screen-reader APIs
in widget code, and V0 does not falsely claim such a bridge already exists in Vanguard.

Bridge actions are generation fenced like every other external semantic ingress. Each request carries
the semantic snapshot revision, target surface/UI-user scope and stable semantic-node identity, which
BeginFrame resolves against the current Vellum node generation. Requests from obsolete snapshots or
reinstanced/recycled targets are rejected as stale rather than retargeted by slot reuse. The assistive
action/focus ingress queue is bounded with current/peak/rejected/stale diagnostics and reserved terminal/
focus-clear progress; its deterministic ordering relative to ordinary pointer/GameInput events is part
of the future bridge contract.

User accessibility presentation preferences—text/UI scale, reduced motion/flashing, high contrast,
color-vision presentation policy, subtitle/caption preferences—are **inputs** supplied by game/settings
ownership. Vellum applies those declared inputs to style/animation/content policy; it does not own the
game's settings store.

Those inputs arrive as one versioned accessibility/presentation profile snapshot so one revision has a
deterministic semantic result. Policy metadata maps fields to invalidation/actions: text/UI scale ->
Measure/Arrange (+ TextShape when font metrics change); contrast/color policy -> compiled style/effective
property + Paint; subtitle/caption policy -> bounded conditional content selection; reduced motion/
flashing -> animation/effect substitution policy. An active animation/effect declares how it responds
to a reduced-motion revision (`Continue`, `FinishImmediately`, `SnapToEnd`, or authored reduced-motion
substitute); the policy is applied at the normal animation checkpoint, never by nondeterministic callback
order. A profile change therefore cannot leave half the UI under the old accessibility policy.

### P5 UI audio seam

Vellum emits typed semantic audio events such as FocusMoved, Activate, InvalidAction, Open, Close,
ValueChanged and authored custom event IDs with surface/user/control/state context. Exact-current
Vanguard does not expose a general AudioService/SoundService owner in the audited engine composition,
so this is an **optional future/game-supplied audio adapter/provider seam**. That provider maps intents
to actual sounds and owns playback, mixing, streaming, listeners and device state. Vellum has no audio
device/mixer/streaming thread. If no provider is composed, audio intents are safely ignored/count as
unhandled presentation intent without changing UI semantics. Repeated navigation events may be
coalesced/rate-limited by adapter policy without modifying focus semantics.

For world-rendered UI, the event context may additionally include the external world anchor/entity or
resolved world position supplied by the world-surface registration so the audio owner can choose a
positional presentation. Vellum does not perform spatialization or listener selection itself.

### P5 authoring round-trip invariants

Future Vellum authoring lives in Vanguard Editor tooling but edits source Vellum assets, never the
compact runtime slot arena directly. Stable authored IDs are preserved across rename/reparent and are
the anchor for selection, overrides, animation/binding targets, hot-reload state transfer and source
diagnostics.

Cook emits a source map/provenance table in tool/development outputs mapping cooked prototype/node/
property records back to asset + stable authored ID + source field/range where available. Shipping may
strip nonessential labels/file-location strings, but exported/template identities and any IDs required
for runtime reinstance remain. Runtime slot indices/generations are never persisted back into source.

Preview instantiates the same cooked/materialized architecture with a synthetic versioned surface
environment (resolution, safe area, locale, flow, device, accessibility settings) and the same
`vellumRendering` path. Authoring preview must not secretly use a different immediate-mode renderer.

Unsaved editor changes use an **editor-only candidate compile/link path**: the authoring graph is
compiled into the exact same current cooked schema/sections and decoded/materialized by the same Vellum
runtime contracts, but the candidate is owned by the preview session and is **not** published as the
project's current resource identity/generation. Concretely, preview tooling uses a preview-session-scoped
synthetic resource identity/source namespace on the **same ResourceStreamer authority** (with an editor-
owned ephemeral backing artifact where exact-current loose streaming requires one), then performs the
normal P2 request/Acquire/materialization path and pins the exact strong `ResourceHandle` generation.
Candidate dependencies still resolve through ordinary streamer metadata and Required/Optional/Soft
rules. Recompiling publishes/replaces only that preview identity transactionally; old preview instances
retain their exact old handles until retired. Discarding the preview drains those handles/requests and
unregisters the ephemeral preview source after it is no longer busy. Saved/reimported content continues through normal project asset ->
committed loose publication -> ResourceStreamer replacement. This prevents both direct editing of live
runtime rows, a second preview-only object representation, and a second resource authority.

Live edit/reimport flows through the existing project asset pipeline -> committed loose publication ->
ResourceStreamer replacement -> P2 transactional reinstance. Failed cook/reinstance leaves the last
known-good live generation intact and reports source-linked errors.

### P5 diagnostics, profiling and functional observability

Diagnostics must explain **why** work happened, not only how much. Development instrumentation can
attach compact causality records to sampled/selected work:

```text
source event/data/resource/animation/mutation
  -> property/effective-value change
  -> invalidation domain + propagation root
  -> layout/geometry/display segment rebuilt
  -> renderer batch/cache/output work
```

The chain is asynchronously joinable, not merely descriptive. Development tracing propagates a compact
correlation token/tuple through source identity + source/change generation -> semantic update/
invalidation epoch -> affected node/property -> display-segment generation -> lowered-segment/surface-
packet generation -> final-2D contribution receipt/output generation. Resource/spawn/reinstance events
retain exact resource/instance generations. Late projected/world decisions additionally record marker/
world-surface identity plus prepared renderer frame/view/output-region generation, so a retained GPU/
output hitch can still be joined to the semantic/resource cause after the source tree is replaced.

Normal always-on stats remain aggregate and cheap. Tool/debug snapshots may expose hierarchy + stable
authored provenance, layout/clip bounds, dirty reason, hit-test bins, event route, focus/capture per
user, layer/modal/overlay stack, virtualized visible/recycled ranges, active animation/binding/source
state, resource/spawn generation, display-segment reuse, lowered batches/draws, raster-cache residency,
world-surface demand/quality decisions, projected-marker state and accessibility semantics.

Functional tests need stable query/action seams rather than framebuffer coordinates only. A locator is
instance/surface scoped and composes `surface/instance + template-origin lineage + authored/test node
ID`, optionally qualified by P3 virtual `ItemKey + incarnation` and semantic role. Repeated templates,
multiple instances and recycled rows are therefore distinguishable. An ambiguous locator rejects or
returns an explicitly requested deterministic set; it never selects the first physical row/hash result.
Test hooks use the same routed action/property/mutation pathways as production and cannot directly
mutate private row storage.

Tests also use a typed **settle/version seam**, never sleeps or "wait N frames" as correctness.
Observable milestones include semantic commit/update generation, layout/geometry publication generation,
display-list publication generation, prepared final-2D packet generation and—when pixels/output matter—
terminal contribution/output receipt. A harness can wait for a known generation to reach the required
milestone without changing production ordering. Projected/world/media tests inject deterministic read-
only provider snapshots/clocks through the same production provider interfaces rather than patching
private state or relying on wall-clock timing.

Cardinality/cost telemetry includes rejected/degraded work and budget reasons. A profiler must be able
to answer whether a hitch came from spawn/materialization, binding storm, layout frontier, text shaping,
display rebuild, renderer lowering, resource miss, world-surface promotion or GPU final-2D work.

P5 structures follow P1's hard-limit doctrine. Configured capacities/high-water/rejection/degradation
stats exist for at least layers per surface, transient overlays per surface/user, projected markers +
late-transform records, registered world surfaces + retained presented-interaction snapshots,
accessibility semantic records + change/live-region
queue, queued UI-audio intents, and active media sessions/prefetch/frame demand. Structural/open requests fail explicitly when full; accessibility
reconciles from the latest authoritative snapshot; disposable audio intents use a declared coalesce/
drop policy with loss statistics. Close/dismiss/unregister/cancel paths retain reserved terminal
progress under pressure so saturation cannot prevent freeing state.

### P5 breadth acceptance

The architecture must support without new ownership domains: nested modal menus and anchored tooltips;
HUD markers for hundreds/thousands of world anchors under a projection budget; dozens/hundreds of
world UI surfaces where only the beneficial subset is live; platform-safe/full-bleed compositions;
LTR/RTL variants without duplicating documents; per-user input prompts; incremental accessibility
semantics; and editor preview/hot reload using the same cooked/runtime/render path as the game.

V0 does not need to define a generic runtime drag-and-drop subsystem merely because the Editor has
one: the RED runtime study found inventory drop behavior composed over pointer events rather than a
generic ink drag/drop primitive. Vellum P3 may still define a generic drag gesture/event protocol if
it materially improves game controls, but it is not falsely claimed as ink parity.

## P6 — architecture closure

**State: CLOSED.** Closed after whole-V0 exact-current adversarial review and final documentation/static
hygiene; no production code/build/test/cook/native execution was introduced.

P6 performs the adversarial closure review over all V0 contracts and turns the architecture into an
exact implementation entry boundary. It remains documentation/source work only.

### P6 ownership closure matrix

Vellum composes existing Vanguard owners instead of replacing them:

| Concern | Authoritative owner after V0 | Vellum role |
| --- | --- | --- |
| native window/focus/cursor/clipboard/IME activation/safe area | WindowManager / WindowService | consume snapshots/events and issue existing typed requests |
| raw device state/events | InputService | consume current snapshot/raw events |
| physical-control -> semantic game actions | GameInputService | consume UI semantic actions; request future binding-description + user-scoped context/event seams only where later product scope needs them |
| frame lifecycle/order | FramePipelineService | one BeginFrame semantic participant and RenderUpdate adapter participation when interactive service exists |
| resource registry/source/package/loading/replacement | ResourcePipeline + ResourceStreamer / ResourceStreamingService | register Vellum decoder, hold exact generation handles, issue ordinary requests/replacement |
| compact UI semantic runtime | `vellum` runtime under future `engine::VellumService` composition | own documents/nodes/properties/mutation/layout/data/animation/UI state |
| Vellum -> renderer lowering/cache policy | `vellumRendering` adapter | translate immutable Vellum display segments to typed renderer final-2D contributions |
| GPU/device/descriptors/render graph/fences/renderer resources | RenderingService / rendering module | own GPU resources, final-2D execution, receipts and retirement |
| ordinary game/editor frame + presentation/swapchain/Present | existing EngineViewport frame source + presentation owner | attach typed contribution to the already existing frame; never Present |
| Vellum UI-only texture/world frame submission | `engine::VellumService` texture-surface frame source, or equivalent explicit preview/tool owner | bounded Render-phase BeginFrame/SubmitFrame for demanded OverlayOnly texture EngineViewports; RenderingService still owns execution/output and there is no Present |
| world entity/transform/streaming/raycast/camera truth | existing world/render owners | supply generation-safe anchor/hit data plus exact Render-phase prepared RenderView/frame/output-region projection snapshots consumed before submitting that same prepared family |
| audio playback/mixing/spatialization | optional future/game-supplied audio provider | consume semantic Vellum audio intent; no exact-current generic Vanguard audio service is claimed |
| media decode/demux/clock/audio-video sync/frame production | optional future/game-supplied media provider | consume typed media intents and publish immutable generation/timestamp/color-encoded frames for `VideoImage` |
| authoring source/cook/preview tooling | future `vellumTools` hosted by Vanguard Editor/project asset pipeline | edit source assets, cook final runtime format, preview through the same runtime/render path |

There is no remaining justified second application loop, input mapper, platform backend, resource depot,
package resolver, renderer/device, swapchain/presentation owner, world streamer or audio mixer in Vellum.

### P6 module dependency closure

The final direction is acyclic:

```text
vellum
  -> low-level Vanguard containers/memory/resources/streaming/schema primitives as needed
  -/-> engine, runtime, editor, rendering

vellumRendering
  -> vellum
  -> rendering

engine::VellumService   [EngineModuleId, Runtime|Editor|Tool]
  -> vellum
  -> vellumRendering
  -> FramePipeline/Input/GameInput/Window/ResourceStreaming/Rendering services

runtime/editor frame owners
  -> engine service/composition seams
  -/-> Vellum internals

vellumTools
  -> authoring/cook APIs + project asset pipeline + shared format/schema declarations
  -/-> shipped runtime dependency
```

The rendering module never depends on Vellum. Core `vellum` remains usable for headless validation and
cook/document work without instantiating the interactive EngineHost service. No new EngineHost module ID
is needed for the current architecture; the future interactive service remains under `EngineModuleId`.

Core Vellum's document/spawn layer may depend directly on the low-level `resources`/`streaming` modules,
but never on `engine::ResourceStreamingService`. Its runtime/spawn composition accepts an explicit
non-owning `streaming::ResourceStreamer&` (or an equivalently exact low-level typed request seam backed by
that object) supplied by the caller. The interactive engine service supplies its existing streamer;
headless V1 proofs may instantiate ResourcePipeline/ResourceStreamer explicitly. Vellum registers/uses
its resource decoder through that injected authority and never constructs a private streamer/depot.

### P6 hot-path closure

The architecture has no required production fast path that depends on:

- one heap/shared-pointer object per visual node;
- per-node locks/atomics;
- runtime hierarchy/property string lookup or reflection in steady update;
- full-tree tick/layout/paint/invalidation discovery on unchanged frames;
- scanning all alive behaviors to discover due work;
- per-node/per-track jobs;
- one GPU submission/draw per widget;
- rasterizing every subtree merely to hide CPU traversal; or
- retained renderer packets borrowing mutable node/frame-scratch memory.

The unavoidable cardinality costs are explicit instead: subtree destroy/first reveal/first materialize
are O(affected nodes); layout follows dirty ancestry/frontiers; active work follows active records; and
renderer submission follows final batch/instance cardinality rather than semantic-node count.

### P6 parity reachability review

No major capability from the RED ink parity ledger is designed out. Implementation remains staged:

- **V1 Runtime kernel** receives the P1/P2 core tree/handle/property/mutation/invalidation/basic layout/
  cooked-resource contracts.
- **V2 Rendering foundation** receives P4 display-list/lowering/final-2D/clipping/image-shape/cache base.
- **V3 Text** receives P3 shaping/fallback/BiDi/localization/rich/editing semantic contracts and P4 glyph
  renderer ownership.
- **V4 Interaction** receives routed pointer/focus/navigation/per-user/virtual-cursor/editable-input
  behavior, including the required Vanguard composition-event extension before full IME parity.
- **V5 Styling / behavior / animation** receives compiled styles/themes/state machines, behavior/controller
  composition, timelines/events and UI-audio intent.
- **V6 Data / large UI** receives compiled notification bindings, views, scrolling/virtualization and
  stable item-incarnation semantics.
- **V7 Advanced presentation** receives vectors, richer effects/masks/video/media and mature raster-cache/
  dirty-region policy on the P4 contract.
- **V8 Surfaces / world UI** receives texture/world/projected surfaces, world-hit/projection adapters,
  demand/quality budgets and split/local-user spatial composition.
- **V9 Authoring / production maturity** receives source editing, preview/profiling/accessibility tooling,
  hot-reload workflows and final whole-program parity/measurement.

The explicit RED study gaps remain honest: V0 does not invent a claim that ink has a generic runtime
drag/drop primitive, generic safe-zone primitive or toolkit-level screen-reader API. Vellum's own
drag-gesture, safe-area and accessibility contracts stand on their documented Vanguard/Slate-informed
architecture rather than false parity claims.

### P6 known future seam work — not V0 blockers

V0 closes with a small set of deliberately explicit implementation prerequisites:

1. **IME composition** — exact-current input exposes committed TextInput but no pre-edit/composition
   events. V4 must add the narrow window-scoped composition seam through Vanguard input/window before
   claiming production IME editing.
2. **GameInput presentation and per-user scope** — exact-current ActionMap lacks a public action ->
   current bindings/revision query and exposes one global UI context stack/ActionEvent stream rather
   than user-scoped context/event identity. Device-glyph work first adds the read-only binding seam;
   V4 adds user-scoped action/context authority only if independent local-player mappings/modal contexts
   are required. Vellum never copies the ActionMap to fake either capability.
3. **Renderer final-2D + texture-output seams** — V2 must introduce the typed renderer-owned final-2D
   contribution/receipt and split/refactor camera output composition so final-2D blends in scene-linear
   composition before DisplaySrgb/output transfer. It must also expose/compose a rendering-owned texture-
   target allocator/pool and generation-safe texture-output replacement/cutover because exact-current
   ViewportManager only retains caller-supplied TextureRefs and cannot resize/replace them in place.
4. **Assistive platform bridge** — Vellum accessibility semantics are defined, but no OS screen-reader
   bridge is claimed today. V9 can add the bridge over immutable semantic snapshots without changing
   node/layout architecture.
5. **Touch/pen** — Vellum pointer routing is future-compatible with generic pointer IDs, but exact-current
   Vanguard input only proves mouse/gamepad/keyboard. V4 may extend platform input if product scope
   requires touch/pen.
6. **Prepared-view/world adapters** — exact-current runtime chooses `RenderFrameViewSetup` in its Render-
   phase frame source and `FrameRenderer` prepares the actual view family only after submission today.
   V8 must expose a renderer-owned prepare/attach composition seam so the ordinary frame source can begin
   the frame, prepare one authoritative immutable view family for that exact serial/extents/jitter/output
   regions, resolve bounded projected-HUD late transforms from it plus generation-safe world-anchor
   snapshots, attach that same family/contribution, and submit. No RenderUpdate query for a not-yet-created
   prepared view and no mutable-camera reconstruction are allowed.
7. **Audio/media providers** — no generic exact-current Vanguard audio/media service was found. V5/V7
   must compose optional future/game providers for playback/mixing and decode/demux/clock/frame
   production through the typed P5 seams rather than inventing those owners inside Vellum.

None of these requires a V0 ownership reversal or blocks the V1 runtime kernel.

### Exact V1 entry contract

V1 is **not authorized by V0 closure**. If the user starts V1, its Definition/P-gates begin from this
contract rather than from a prototype sketch.

The first production slice must prove the P1/P2 runtime architecture without pulling rendering or
interaction forward. Its smallest acceptable vertical path is:

```text
final-format cooked Vellum document bytes/resource
  -> bounded decoder/validation
  -> exact immutable document resource
  -> one unique spawn/materialization operation
  -> compact generational document arena
  -> root + one production panel policy + one production leaf payload
  -> typed property writes
  -> deterministic mutation commit
  -> semantic invalidation
  -> measure/arrange to committed geometry
  -> diagnostics/query
  -> destroy/cancel/release with zero stale ownership
```

The first slice therefore implements final-direction **core `vellum`** contracts only: value handles,
stable slot-to-row indirection, intrusive ordered topology, typed property schemas, sparse dirty
frontiers, structural command buffer/terminal lane, active-set infrastructure, basic measure/arrange,
immutable cooked resource decode and bounded spawn/materialization. It may expose the lower-level runtime
update API used later by `engine::VellumService`, but it does not need to instantiate that interactive
service merely to prove the kernel.

The cooked fixture must use the real sectioned/versioned runtime format defined by V1, not temporary
JSON/reflection paths or a format planned for replacement. A test fixture may be generated by a
production-format writer/helper or checked as deterministic cooked bytes; V1 does not need the eventual
V9 visual editor to validate the runtime format.

The first proof intentionally does **not** implement `vellumRendering`, renderer final-2D integration,
text shaping/glyph rendering, routed input/focus, styles/themes/animation playback, data virtualization,
world/projected surfaces, accessibility OS bridge or editor authoring UI. It must nevertheless preserve
the ABI/extensions those later phases require.

Before V1 closes, focused tests/diagnostics must cover at least:

- stale node/instance handles and generation exhaustion without ABA resurrection;
- same-update destroy + attempted reuse/dirty/queued-or-active stale work quarantine through generation fencing;
- atomic reparent/cycle rejection and deterministic command-order conflicts;
- capacity pressure where create/reparent fails but destroy/cancel/release still succeeds;
- a large admitted static tree (target order of 100k nodes unless exact V1 Definition establishes a
  more representative/harder bound) whose warm update performs zero clean-node discovery work;
- allocation/cardinality diagnostics proving that admitting that tree grows compact arena/chunk storage
  rather than requiring one independent heap/shared-object allocation per node, and that a warm unchanged
  update performs zero semantic heap allocations, per-node jobs or discovery work;
- collapsing a large admitted subtree of K descendants removes its descendant semantic/layout/paint/hit
  work from warm updates; revealing it pays at most the explicitly bounded one-time O(K) repair/reentry
  cost, after which unchanged warm updates return to zero descendant discovery;
- equal property writes producing zero dirty work and render-only-class invalidation producing no
  Measure/Arrange work even before rendering exists;
- one layout-affecting property touching only the required ancestry/arrange frontier;
- pathological hierarchy depth rejected/handled without C++ recursion failure;
- cooked decode/version/count/topology corruption failing atomically;
- two spawn requests sharing an underlying resource load while materializing independent instances,
  with cancel/owner-expiry races and exact strong-handle lifetime; and
- complete runtime/resource teardown returning all gauges/active queues to zero.

V1's Definition must re-read exact-current Vanguard before source edits and may refine numeric capacities
or file/API names from implementation evidence, but it may not weaken the architectural invariants just
to make the first rectangle/tree easier.

### P6 closure evidence boundary

V0 closure evidence is source/document architecture evidence only: independent adversarial review,
cross-document status/link consistency, scoped `git diff --check` and stale-status/ownership searches.
No Premake generation, compilation, tests, package cook or native execution is required or appropriate
for V0 because no production Vellum source exists yet.

V0 closure does **not** authorize V1 automatically.

Final P6 reconciliation closed the remaining cross-gate issues rather than deferring them into V1:
decoder unregister now follows Drain/Shutdown resource-terminal ordering; ordinary presentation and
UI-only texture frame-source ownership are distinct; world render quality is separate from semantic
`QualityClass`; rich-text hyperlinks use compact interactive spans; cached world-surface interaction is
generation-coupled to the pixels actually presented; split-view safe areas use a BeginFrame-visible
region generation; preview/media keep ResourceStreamer authority; projected HUD resolves only after the
exact prepared view family exists and submits that same family; and V1's first proof directly measures
compact allocation shape plus collapsed-subtree gating. After the final status/evidence-ledger corrections,
an independent exact-current whole-V0 closure review reported **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
