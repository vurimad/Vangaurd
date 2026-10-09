# Vellum game UI system

## Status and authorization

The user explicitly authorized **Definition work only** on 2026-10-01. Definition is **CLOSED**
after source-level study of REDengine ink, Unreal Slate/UMG and exact-current Vanguard integration
seams, reconciliation of the delegated capability/anti-naive findings, and final docs/static hygiene.
The final prime Definition review is **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

At the moment this original Definition closed, its coarse post-Definition P0-P5 gates remained
**UNAUTHORIZED** pending explicit user authorization. This is historical authorization evidence: the
later `vellum-program-roadmap.md` superseded that coarse gate shape with V0-V9, and
`vellum-v0-architecture-plan.md` is the authoritative current status for V0 architecture work.

This Definition was source-study and documentation work only. It did not authorize or perform
production Vellum code, Premake changes, builds, tests, package cooking or native execution.

Vellum is Vanguard Engine's **game-facing** UI system. It is separate from the Vanguard Editor's
Dear ImGui UI. The editor may later host Vellum authoring, preview and diagnostics, but game runtime
code must never depend on editor UI code.

## Mandate

Vellum is designed against the end-state capability of a shipped AAA game UI system, not against
the easiest first rectangle or menu.

The user's direction is explicit: REDengine ink is a master-class reference and Vellum should
copy as much of its practical capability as useful, then improve the implementation where Vanguard
can provide cleaner ownership, stronger performance contracts or better tooling. Unreal Slate/UMG
is a second source-level reference, especially for semantic invalidation, focus/navigation, event
replies, virtualization, text shaping and renderer separation.

The first implementation must therefore be **small in breadth but final-direction in architecture**.
The following naive shortcuts are rejected even for the first proof:

- one heap/shared-pointer object per node as the required runtime representation;
- full-tree tick, layout or paint every frame;
- rebuilding UI from scratch every frame;
- per-frame string/reflection property binding;
- one draw submission per visual node/control;
- screen-space-only assumptions;
- single-player/single-focus assumptions;
- temporary layout semantics that cannot support authored or virtualized content;
- animation that writes properties without declaring its invalidation cost;
- editor-only resource loading in game runtime;
- direct RHI drawing that bypasses Vanguard renderer ownership; or
- a throwaway resource/document format intended to be replaced after the first demo.

## Reference source trees

Primary RED source:

`D:/root/R6.Root/Mainline/dev/src/common/inkCore`

Supporting RED source:

- `common/inkRendering`;
- `common/resourceInk`;
- `editor/editorInk`;
- `tests/unitTestsInkFramework`; and
- shipped game UI scripts where native ownership alone does not show intended use.

Secondary Unreal source:

- `D:/UnrealEngine/Engine/Source/Runtime/SlateCore`;
- `D:/UnrealEngine/Engine/Source/Runtime/Slate`;
- `D:/UnrealEngine/Engine/Source/Runtime/SlateRHIRenderer`;
- `D:/UnrealEngine/Engine/Source/Runtime/UMG`; and
- `D:/UnrealEngine/Engine/Plugins/Runtime/ModelViewViewModel`.

Vanguard integration is defined against exact-current `input`, `gameInput`, `window`, `streaming`,
`resources`, `rendering`, `engine`, `runtime` and frame-pipeline ownership. Vellum composes these
systems; it does not duplicate them.

The companion source study is
[`vellum-ui-reference-study.md`](../studies/vellum-ui-reference-study.md).

## Capability floor inherited from ink

Vellum is not required to reproduce ink's exact classes or product-specific layers. It **is**
required to preserve an intentional production path for the capability classes below. A capability
may be deferred to a later implementation gate; it may not be designed out by an early shortcut.

| Capability | RED evidence | Vellum definition |
| --- | --- | --- |
| Retained hierarchy and reusable templates | `Widget`, `CompoundWidget`, `WidgetLibraryResource`, `WidgetLibraryReference` | One retained runtime instance tree produced from compiled reusable documents/templates. |
| General layout | canvas, horizontal/vertical panels, flex, grid, uniform grid, scroll area | Extensible measure/arrange protocol supporting absolute, stack, flex, grid and scrolling. |
| Precise dirtiness | layout vs visual invalidation, cached desired sizes | Semantic invalidation is foundational, not an after-the-fact optimization. |
| Styles/themes/states | `StyleResource`, `StyleTheme`, `PropertyManager` | Compiled style/theme/state dependencies with narrow runtime invalidation. |
| Behavior/controllers | `WidgetLogicController`, controller processor and control controllers | Visual nodes and behaviors are separate; controls are composition + behavior. |
| Animation | sequences, resources, processor, interpolation, callbacks, state transitions | Compiled timelines, state transitions, target resolution and demand-driven ticking. |
| Data sources/views | source/listener/view families | Notification-driven typed data and projected views. |
| Virtualized lists/grids | virtual list/grid/uniform controllers + template classifiers | Virtualization/recycling is core runtime infrastructure. |
| Pointer/focus/input | hit-test grid, routed pointer/focus events, custom hit testers | Spatial hit testing, routed events, pointer capture, per-user focus and custom surface mapping. |
| Keyboard/gamepad navigation | discrete/container navigation, virtual cursor, input contexts | Directional navigation, focus scopes and optional virtual-cursor mode. |
| Text | shaped text/layout/cache, rich text, text input, inline image/input runs | Unicode/BiDi/fallback/localization/rich text/editing/IME are first-class architecture. |
| Input prompts/glyphs | input-key icon manager, input display controller, inline input text runs | Semantic action prompts can resolve to active-device glyph/text presentation without hard-coded platform strings. |
| Platform/language variants | language/platform-specific image/text/video controllers | Authored conditional variants for locale, platform/device class and game policy without duplicating whole documents. |
| Resolution/resource scaling | root/native resolutions, atlas resolution overrides, layer/resource scalers | Surface scale, authored reference resolution and resource-quality choice are explicit and independent of layout correctness. |
| Image/vector/shapes | image, gradients, shapes, line patterns, SVG vector widget | Renderer/display-list model must support more than textured quads. |
| Video/media | video widget/manager/controllers | Async media-backed UI with playback/sync hooks. |
| UI audio hooks | UI sound system, audio handlers, animation sound events, world-widget positional handlers | Controls/animations can emit semantic UI audio events without making Vellum an audio mixer; world UI can provide positional context. |
| Effects/masks | wipes, blur, mask, glitch, point cloud, fill, glow, correction, multisampling | Scoped effect composition without forcing every subtree through a render target. |
| Cached/partial rendering | `CacheWidget`, invalid rectangles, dynamic textures | Retained display-list caching first; optional raster cache/dirty rectangles where profitable. |
| Layered UI | HUD/menu/loading/notification/fullscreen/photo/debug/video layers | General layer stack with ordering, modal/input ownership, active/passive update and visibility. |
| World-space UI | `WorldLayer`, component wrappers, custom world hit tester | Same Vellum runtime on world surfaces; no second world-UI toolkit. |
| Offscreen/render-to-texture | `OffscreenLayer` + dynamic render target | Texture surfaces are first-class Vellum outputs. |
| Fullscreen composition | composition resource/presets/transitions | Explicit renderer extension seam for authored fullscreen UI composition. |
| UI flow/state orchestration | viewport UI state machine + visual-state events | Vellum supports authored/local visual-state machines and transitions, while game/product flow remains caller-owned rather than a hard-coded global RED-style state machine. |
| Async resource use | async widget/font/atlas/video dependencies | Existing Vanguard resource/cook/streamer ownership. |
| Authoring/runtime split | widget pack/unpack, editor-only backend data, runtime style maps | Rich authoring metadata lowers to compact cooked runtime data. |
| Profiling/debug/testing | metrics, block profiler, runtime optimizer, spawn debugger, functional testing | Diagnostics and cardinality instrumentation exist from the first production slice. |

This table is a **parity ledger**. Intentional exclusions must be recorded with rationale and a
Vanguard replacement; capabilities are not silently dropped.

## Core runtime model

### One compact runtime tree

Authoring and runtime representations are separate:

```text
authoring document / templates / styles / animation / bindings
                           |
                           | cook
                           v
                compiled Vellum document
                           |
                           | instantiate
                           v
              compact Vellum runtime instance
                           |
             +-------------+-------------+
             |             |             |
          layout       behavior/data   animation
             +-------------+-------------+
                           |
                           v
                  retained display list
                           |
                           v
                    Vanguard renderer
```

The runtime tree uses generation-checked handles rather than caller-owned raw pointers. Exact type
names/bit widths are P0/P1 details, but the contract is:

- stable identity for the lifetime of an instance;
- stale handles are detectable;
- parent/child topology is compact and traversal-friendly;
- generic hot node data is not buried in a deep virtual C++ hierarchy;
- type-specific payloads are separate from generic tree/layout state;
- hot traversal does not require one heap allocation or shared-pointer operation per node;
- authoring names/paths/undo metadata are not needed for steady-state traversal; and
- template/document provenance remains available in diagnostic builds.

Authoring nodes additionally carry stable authored IDs that survive hierarchy edits/reparenting.
Cook resolves those identities into compact runtime node/property slots. Animation, binding and
controller references therefore remain repairable in the editor without imposing runtime string
paths or forever-stable child-index identity.

Internally **node** is the low-level hierarchy unit. **Control/widget** is a higher-level authored
or reusable concept composed from nodes plus behaviors. A button is not required to be a giant C++
subclass merely because it behaves like a button.

### Deterministic mutation ownership

ink's child/controller/world-widget code uses partial locking and exposes the complexity of
concurrent structural mutation. Vellum instead defines one authoritative mutation domain during UI
update. Other threads may publish bounded data notifications or mutation commands, but may not
directly mutate a live hierarchy concurrently.

Layout, hit testing, navigation and rendering therefore observe one coherent tree snapshot per
Vellum update without fine-grained node locks.

## Semantic invalidation

Every runtime-writable property declares what work it invalidates. Initial domains:

```text
TextShape
Measure
Arrange
Paint
HitTest
Navigation
```

The set is intentionally compact compared with Slate's mature invalidation state machine while
retaining Slate's strongest idea: a property change says **why** cached work became stale.

Examples:

| Change | Required work |
| --- | --- |
| opacity/tint | `Paint` |
| render translation/rotation | `Paint | HitTest` |
| width/height/min/max/margin/padding | `Measure | Arrange` plus geometry-dependent paint/hit-test |
| text content | `TextShape | Measure | Arrange | Paint` |
| font/locale/direction | `TextShape | Measure | Arrange | Paint` |
| focusability/navigation rule | `Navigation` |
| visibility | enum-dependent combination of layout, paint, hit-test and navigation |

Measure dirtiness propagates upward only as necessary. Recompute child-before-parent and stop
propagation when cached desired size is unchanged. Arrange flows top-down only through affected
geometry. Paint, hit-test and navigation caches are independently dirty.

A 60 Hz opacity animation must not re-run layout. A render-only transform must not reshape text.
Updating one ammo counter must not imply a whole-HUD traversal.

Dirty-reason diagnostics are mandatory so correctness issues are visible rather than hidden behind
emergency full-tree rebuilds.

## Layout and geometry

Vellum uses a two-stage retained layout protocol:

1. **Measure** computes desired size from constraints, content and children.
2. **Arrange** assigns final child geometry from the parent's allotted geometry.

Definition locks:

- logical UI coordinates separate from framebuffer pixels;
- explicit surface/DPI/UI scale;
- min/max/preferred constraints;
- margin and padding;
- horizontal/vertical alignment;
- anchors/absolute positioning;
- fit-to-content and stretch/fill semantics;
- render transforms separated from layout where possible;
- clipping as arranged geometry;
- an explicit visibility enum rather than independent booleans; and
- one authoritative arranged geometry shared by paint, hit testing and navigation.

Initial general containers must cover canvas/absolute, horizontal/vertical stack, flex, grid,
uniform grid and scroll composition. Higher-level controls use these; they do not add another
layout system.

## Documents, templates and cooked resources

The primary runtime asset is a compiled **Vellum document**. Source syntax is not locked by
Definition; the cooked contract is.

A document may contain/reference:

- node templates and topology;
- reusable named template/library items;
- external template/document references;
- typed default properties;
- style/theme/state references;
- behavior/controller descriptors;
- compiled binding programs;
- animation timelines/events;
- text/font/localization references;
- image/vector/media references;
- effect/mask descriptors; and
- stable debug/provenance IDs in diagnostic builds.

Human-readable property paths, reflection variants and editor object graphs are compiler inputs,
not required runtime storage. Cook validates them and lowers them into compact typed IDs/offsets or
pre-resolved programs wherever possible.

Vellum assets use Vanguard's existing resource compiler/package/`ResourceStreamer` path. Vellum
does not add another depot, package format, loose-resource registry or GPU-residency authority.

## Styles, themes and visual states

Vellum preserves ink's authored style/theme/state power while making dependency tracking narrower.
It supports:

- semantic style properties;
- theme selection/overrides;
- control/node states such as default, hover, pressed, selected, focused, disabled and game-defined;
- style-scope inheritance/composition; and
- runtime state changes that update only dependent properties.

Style compilation records concrete runtime properties depending on each theme/state value. State
changes therefore invalidate only those properties and their semantic domains rather than rebinding
an entire subtree by default.

## Behaviors/controllers

Visual structure and behavior are separate. A behavior may:

- resolve stable node references in its owning document instance;
- subscribe to routed Vellum events;
- read/write typed runtime properties;
- start/stop animations;
- subscribe to compiled data/data views;
- publish semantic control events; and
- own bounded control-specific state.

Buttons, toggles, sliders, selectors, scroll views, list/grid presenters and text inputs are built
from generic nodes plus reusable behaviors. Games may provide custom behaviors without deriving
every visual element from an engine widget base class.

Controllers/behaviors are scheduled from active sets. Vellum does not walk every behavior every
frame merely to discover that nothing needs time-based work.

Where needed, the controller boundary has two levels:

- **presentation behavior** — operates only on Vellum state/events/data interfaces and stays
  reusable across games; and
- **game-facing controller/adapter** — receives explicitly supplied game/view-model services or
  data and translates them into the presentation contract.

This preserves ink's useful separation between widget-local logic and game-facing controllers
without turning Vellum into a service locator for arbitrary game systems.

## Data and bindings

The binding model combines the best direction from Unreal's compiled MVVM and ink's sources/views:

- source paths/converters validate and compile at authoring/cook time;
- runtime uses typed IDs/offsets or precompiled accessors;
- notification-driven one-way bindings are normal;
- one-time, event and explicitly justified two-way bindings are supported;
- converter cost is explicit/profileable;
- cyclic graphs are rejected or diagnosed; and
- polling bindings are opt-in exceptions.

Collection data uses source/view separation with stable identity, change notifications and
filter/sort projections.

## Virtualization and recycling

Virtualization is core Vellum infrastructure, not a late specialized widget. It supports:

- list/grid presentation;
- uniform and variable item extents;
- item template selection/classification;
- viewport-driven visible ranges + overscan;
- instance recycling/pooling;
- stable selection/focus across recycled visuals;
- incremental insert/remove/move/change notifications; and
- scroll-to-item/navigation without materializing every prior record.

An inventory of tens of thousands of records must be representable with only visible/overscan
presentation subtrees live.

## Input, focus and navigation

Vellum consumes Vanguard's existing input/window/game-input owners. It does not create another
platform event pump, native-window system, clipboard owner, IME backend or gamepad mapping layer.

The interaction model includes:

- spatial hit-test acceleration;
- custom hit-test/coordinate adapters for world/unusual surfaces;
- hover/enter/leave;
- press/release/double-click/wheel/axis;
- pointer capture/release;
- generic drag-gesture lifecycle (start/update/end/cancel), with product-specific drop/targeting composed
  over ordinary routed pointer/semantic events rather than assumed as a mandatory toolkit subsystem;
- keyboard focus;
- text-input/IME sessions;
- per-user/player focus;
- focus scopes and modal boundaries;
- directional navigation with explicit overrides/boundary policy;
- semantic game-input actions; and
- optional gamepad virtual cursor.

Events route through:

```text
Preview/Tunnel -> Target -> Bubble
```

Handlers return an explicit reply object that can mark handled/stop propagation, request/release
pointer capture, request/clear focus, request text input and start/cancel drag gestures; admitted pointer
movement/end then produces deterministic drag update/end/cancel events through the same route. A game
may layer inventory/drop-target semantics on that gesture protocol without making generic drag-and-drop
a core parity requirement.

Exact frame ordering and game-input consumption are P0 locks after exact-current source tracing.

## Animation and transitions

Authored animation is a core data type with timelines, multiple property tracks, easing,
delay/loop/reverse/playback-rate options, events/callbacks, state transitions, independent time
domains where required, text-specific presentation tracks and media/external synchronization hooks.

Targets resolve to compact property slots when an instance is created. Steady-state evaluation does
not perform string path lookup.

Evaluation and property application are separate. Coarse evaluation may run in parallel when work
size justifies scheduling cost; apply order and conflicting writes remain deterministic. The
compiler/runtime should precompute target write sets rather than repeatedly discovering conflicts.

Render-only tracks stay render-only. Layout-affecting animation is supported but its cost is
explicit and diagnosable.

## Text, localization and editable text

Text is infrastructure, not a leaf feature postponed until layout is frozen. Vellum must have a
production path for:

- font-family resources and fallback chains;
- Unicode shaping;
- BiDi/directionality;
- ligatures/kerning;
- localization and live locale changes;
- line breaking, wrapping, alignment and overflow;
- shaped glyph/layout caches;
- rich spans;
- inline images and input glyphs;
- hyperlinks/clickable spans;
- selection/caret;
- clipboard; and
- IME/composition through Vanguard Window Manager.

Shaping/layout results should be immutable/hash-keyed where practical. Locale/font/text changes
invalidate shaping explicitly; unrelated paint changes do not.

### Input prompts and conditional presentation

Vellum must be able to author semantic input prompts that resolve through Vanguard game-input/device
state into the appropriate glyph or text form. Prompt presentation is therefore data-driven by
action/device identity, not hard-coded controller-brand or keyboard strings in layout documents.

The same conditional-presentation mechanism should cover locale/platform/device-class variants for
text, images and media where games require them. This is a bounded authored condition system, not a
general runtime scripting language hidden inside the style layer.

### Resolution and resource-quality scaling

Layout operates in logical surface units and remains correct regardless of which texture/atlas
resolution is resident. Vellum documents may carry an authored reference resolution and resource
quality hints, while the resource/renderer side chooses suitable atlas/image variants for actual
output scale and platform policy.

This deliberately separates **layout scale** from **asset resolution selection**. A higher-DPI
surface must not require a second copy of the UI hierarchy or alter authored gameplay semantics.

## Rendering and display lists

Vellum is retained at the UI layer but renderer-neutral at the semantic boundary. The tree emits
typed display-list primitives such as:

```text
Rect / RoundedRect
Image / NineSlice
GlyphRun
VectorPath / Shape
VideoImage
CustomMesh (bounded extension)
ClipPush / ClipPop
EffectPush / EffectPop
```

These lower into Vanguard renderer work. Vellum never owns `Present`, creates a second render
scheduler or submits one command list per control.

Rendering targets:

- instance-oriented rect/image/glyph drawing;
- bindless/descriptor-indexed resources where supported;
- atlases where they improve batching/residency;
- stable batch keys from material/shader, resource, clip/effect and layer state;
- axis-aligned clipping through scissor;
- stencil/mask paths for transformed/complex clipping;
- explicit color-space/composition semantics;
- renderer-owned GPU resource lifetime; and
- bounded custom-rendering extension points inside graph/resource ownership.

Display lists are segmentable by subtree. Unchanged segments are retained. Expensive static
subtrees may additionally opt into raster caching/render-to-texture, but raster caching is not the
default answer to CPU inefficiency. When raster caching is used, bounded dirty rectangles remain
part of the contract.

## Effects, masks and fullscreen composition

Scoped effects must be representable without forcing effect knowledge into every node. The system
must be able to express the broad families demonstrated by ink: wipes, blur, masks,
glitch/distortion, fill/correction, glow and quality/multisampling controls.

Effect scopes lower into renderer-owned material variants/passes according to actual cost. A simple
tint must not become an offscreen render merely because complex effects exist.

Fullscreen UI composition/preset transitions have an explicit extension seam for authored global
HUD/menu effects without bypassing Vellum or the renderer.

## UI audio and local visual-state orchestration

Vellum may emit semantic UI audio events from controls, behaviors and animation events. Vellum does
not mix/play platform audio directly. The later V0 exact-current audit found no generic Vanguard audio
service in the audited composition, so playback/mixing/spatialization remains behind an optional
future/game-supplied provider seam rather than an invented Vellum owner. World surfaces may additionally
provide a positional/context handle for that provider when a game wants spatialized UI feedback.

Vellum also supports local authored visual-state machines/transitions for controls, screens and
layer compositions. This does **not** make Vellum the game's master frontend/game-state machine.
Game/application flow selects layers/documents and publishes state; Vellum owns visual transition
semantics inside the UI it presents.

## Surfaces, layers and world UI

The same runtime core targets multiple surfaces:

1. **Screen surface** - ordinary HUD/menu output.
2. **Texture surface** - offscreen render target for in-world screens/compositing/consumers.
3. **World surface** - document instance associated with a world transform/component plus an
   interaction mapper/custom hit tester.

Surface adapters own coordinate conversion/output binding; they do not fork layout, controls,
styles, data or animation into separate toolkits.

World surfaces also need lifecycle/demand policy: visibility/frustum/range demand, hysteresis,
screen-coverage priority, live-vs-cached representation and render-target quality/budget may vary
with projected importance. Those policies belong to the world-surface manager boundary rather than
generic nodes or layout.

Above surfaces, Vellum provides a general layer stack controlling order, visibility, interaction,
modal ownership, active/passive update and target surface. HUD, menu, loading, notifications and
game-specific overlays are layer compositions rather than hard-coded engine singleton types.

Inactive layers may advance intentionally retained work (for example passive animation/media time)
without executing a normal full update.

## Resource streaming and lifetime

Vellum references ordinary Vanguard resources for textures, fonts, vector data, video/media and
other content. Existing `ResourceStreamer` ownership remains authoritative for runtime source
selection/dependencies.

The Vellum service owns UI-level leases/instances and renderer-facing bindings only. It does not
duplicate package mounting, loose-source choice, texture residency or GPU retirement.

Resource arrival/replacement dirties only dependents. Missing/streaming resources use defined
fallback presentation rather than blocking the entire frame or corrupting layout ownership.

Document/template/media spawning across asynchronous dependencies has explicit request identity and
state: queued/loading/ready/instantiating/complete/failed/cancelled (or equivalent). Requests support
cancellation, owner expiry, priority and bounded admission. Construction and paint never synchronously
wait for assets.

## Authoring and editor integration

The eventual Vanguard Editor Vellum workspace should retain the productive ideas visible in ink's
editor/runtime split and UMG Designer without inheriting their runtime overhead. Expected tooling:

- hierarchy + visual canvas;
- reusable template/library editing;
- layout/constraint inspection;
- style/theme/state editing;
- animation timeline;
- binding/behavior inspection;
- localization/text preview;
- multiple resolution/aspect/safe-zone previews;
- gamepad focus/navigation visualization;
- screen/texture/world-surface preview; and
- invalidation/layout/batch/performance overlays.

Authoring uses stable IDs and rich metadata for undo/redo, copy/paste and external references. Cook
lowers this to compact runtime data. This future tooling does **not** couple runtime Vellum to
Editor Dear ImGui.

## Observability and performance contracts

Vellum is measurable from its first production slice. Diagnostics expose at minimum:

- live node/control/behavior counts;
- structural mutations;
- dirty-node counts per invalidation domain;
- measure/arrange counts + CPU time;
- text shape/layout cache hits/misses;
- regenerated vs reused display-list segments;
- primitive/vertex/index/instance/batch counts;
- clip/effect/raster-cache counts;
- hit-test entries/query cost;
- routed input path/focus changes;
- active animation/controller counts;
- virtualized visible/recycled/spawned counts;
- resource waits/fallbacks; and
- UI GPU time where existing renderer timing can measure it without a second timing owner.

Tracy instrumentation may be used through Vanguard's profiling integration, but Vellum must know
its own cardinalities and dirty-work causes.

Performance rules:

- static UI approaches near-zero semantic/layout CPU work after warm-up;
- no mandatory full-tree walk merely to discover nothing changed;
- no per-node job scheduling/handoff architecture;
- coarse parallel work only when cardinality amortizes scheduling cost;
- no unbounded historical temporary allocation;
- hot paths use bounded pooled/arena/frame memory;
- no steady-state string lookup for resolved node/property/template references;
- virtualization bounds presentation by visible/overscan demand; and
- caches are correctness-preserving accelerators, not excuses for an intrinsically expensive
  default traversal.

These rules apply even when the first proof contains only a handful of nodes. The first proof is
not the target workload.

## Vanguard ownership and integration

Vellum composes existing Vanguard owners:

- `InputService` - raw pointer/keyboard/window/text input and snapshots;
- `GameInputService` - semantic UI/game actions and contexts;
- Window Manager / `WindowService` - native focus, cursor, clipboard, DPI and IME/text input;
- `ResourceStreamingService` / `ResourceStreamer` - cooked Vellum and referenced content;
- `RenderingService` / viewport/render graph - UI GPU work and output composition;
- `FramePipelineService` - deterministic update/render ordering; and
- existing presentation/output owners - final presentation and retirement.

Vellum does not own native `Present` or establish a private application loop.

Exact service registration and frame dependency ordering are P0 deliverables; Definition does not
guess where exact-current source still needs tracing.

## First implementation principle

The first implementation is intentionally small in **surface area**, not naive in architecture. After
the V0-V9 program decomposition, the **first V1 kernel proof** is deliberately headless and stops at the
real cooked-document -> compact-runtime -> invalidation -> measure/arrange boundary defined by the V0
architecture plan. The first **visible/integrated** proof, once the required rendering phase is
authorized, extends that same final-direction runtime path as follows:

```text
cooked Vellum document
  -> ResourceStreamer
  -> compact runtime instance
  -> semantic invalidation
  -> measure + arrange
  -> Rect + Image (+ Text once shaping foundation is ready)
  -> retained display-list segment
  -> Vanguard renderer UI phase
  -> ordinary viewport presentation
```

It uses the same document/runtime/layout/invalidation/display-list/resource ownership intended for
the mature system. It may leave advanced capabilities unimplemented; it may not use a temporary
immediate-mode UI, direct-RHI rectangle path or disposable serialization format.

## Program roadmap after Definition

The initial coarse P0-P5 implementation sketch has been superseded. The user explicitly rejected
treating the complete Vellum effort as one phase with a few huge subphases and started **V0** on
2026-10-01.

The authoritative program decomposition is now
[`vellum-program-roadmap.md`](vellum-program-roadmap.md):

- V0 Architecture;
- V1 Runtime kernel;
- V2 Rendering foundation;
- V3 Text;
- V4 Interaction;
- V5 Styling / behavior / animation;
- V6 Data / large UI;
- V7 Advanced presentation;
- V8 Surfaces / world UI; and
- V9 Authoring / production maturity.

The closed detailed V0 architecture record is
[`vellum-v0-architecture-plan.md`](vellum-v0-architecture-plan.md). V0 was architecture-only and
contains its own Definition + P0-P6 closure evidence. V1-V9 remain separately gated, unauthorized
production phases until explicitly started.

## Definition exit conditions

Definition is complete when:

- the ink capability floor is broad enough that early implementation cannot quietly design out
  major shipped-game UI categories;
- adopted/rejected Slate ideas are recorded;
- runtime tree/document/invalidation/layout/behavior/data/input/animation/text/render/surface
  ownership is coherent;
- exact-current Vanguard systems Vellum composes are identified;
- naive-first-implementation traps are explicit;
- the first vertical slice is final-direction architecture; and
- implementation remains unauthorized until the user explicitly starts the next gate.
