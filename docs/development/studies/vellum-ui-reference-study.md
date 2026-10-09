# Vellum UI reference study — REDengine ink and Unreal Slate/UMG

## Status

Source study for Vellum Definition, 2026-10-01. No Vanguard production implementation,
project generation, build, test, package cook or native execution occurred.

The purpose is to identify the capabilities and architectural constraints that must be respected
**before** Vellum's first runtime implementation makes later high-end UI expensive or impossible.

## Finding

REDengine ink is the stronger reference for **what a shipped game UI platform must be able to do**.
It is much more than a collection of controls: authored widget libraries, controllers, styles,
animation, data views, virtualized collections, rich text, media, effects, world UI, offscreen UI,
layered application UI, resource streaming, profiling/debugging and editor authoring all exist as a
single family of systems.

Slate/UMG is the stronger reference for several **runtime mechanisms**: reactive typed attributes,
semantic invalidation, event replies, mature focus/navigation, renderer-neutral draw elements,
clipping policy, list virtualization and notification-driven compiled MVVM binding.

Vellum therefore uses ink as the capability floor, borrows selected Slate mechanisms, and avoids
both systems' accumulated runtime complexity where Vanguard can compile more aggressively and own
the runtime in a data-oriented form.

## REDengine ink

### Retained hierarchy and incremental layout

`common/inkCore/include/widget.h` and `src/widget.cpp` define a retained `ink::Widget` with cached
desired size and arranged geometry. `FlagForLayoutInvalidation()` propagates child dirtiness upward;
`Internal_InvalidateDesiredSize()` recomputes dirty descendants child-first; `CacheDesiredSize()`
lets propagation stop when externally relevant desired size is unchanged. Arrangement then flows
top-down through `ArrangeChildrenDownRecurse()`.

The important lesson is not the exact class hierarchy. It is that measure and arrange are cached,
incremental and share one geometry truth with draw/hit testing.

Layout/container families visible in the native source include `CanvasWidget`,
`HorizontalPanelWidget`, `VerticalPanelWidget`, `FlexWidget`, `GridWidget`, `UniformGridWidget`,
`ScrollAreaWidget`, `VirtualCompoundWidget` and related controller families.

### Visibility semantics

`Widget::SetVisible`, `ShouldAffectLayout`, `ShouldBeRendered` and the
`m_affectsLayoutWhenHidden` state show that "not painted" and "collapsed from layout" are distinct
concepts. Vellum should encode this as an explicit visibility state rather than independent booleans
whose combinations can become ambiguous.

### Paint invalidation and subtree caching

`Widget::FlagForVisualInvalidation()` is separate from layout invalidation. `CacheWidget` renders a
subtree into a dynamic texture and tracks invalidated rectangles so cache redraw can be bounded.

Vellum should preserve separate paint dirtiness, but make reusable display-list/geometry segments
the normal cheap path. Raster subtree caching remains optional for expensive static/effect-heavy
content instead of being required to make ordinary UI fast.

### Hit testing and routed input

`common/inkCore/include/hittestGrid.h` stores arranged geometry, clipping and hierarchy information
in a spatial grid. `InkSystem` routes pointer events from the target back through ancestors;
`SetFocusedWidget` centralizes focus. `ICustomHitTester` exists for nonstandard coordinate spaces,
including world UI.

Vellum should preserve spatial hit testing and ancestor routing, then add an explicit preview/tunnel
phase plus a reply object carrying capture/focus/drag requests.

### Controller/behavior separation

`WidgetLogicController`, `IWidgetController`, `ControllerProcessor` and concrete controllers for
buttons, lists, grids, sliders, toggles, selectors, navigation and other controls demonstrate that
visual hierarchy and behavior are separate concepts. `ControllerProcessor` also defers add/remove
and has active/passive update paths.

Vellum should build high-level controls as generic visual nodes + reusable behavior rather than
requiring every control to become a deep heavyweight C++ widget subclass.

### Styles, themes and visual state

`styleResource.h/.cpp`, `styleTheme.h`, `styleProperty.h` and `propertyManager.h/.cpp` provide
authored style/theme/state property binding. `StyleResource::BuildRuntimePropertyMap` builds runtime
lookups and post-load can discard authoring-side serialized structures.

The useful pattern is the authoring/runtime split. The liability is broad state/style refresh and
heavy name/reflection machinery. Vellum should compile style dependencies directly to concrete
runtime property slots so only affected values and invalidation domains become dirty.

### Widget libraries and authoring/runtime packing

`widgetResource.h`, `widgetLibraryDefinitionPack.h`, `widgetLibraryReference.h`,
`widgetLibraryUnpackedView.h`, `spawnUtils.h` and `spawningProcessor.h` implement reusable local and
external widget-library items plus asynchronous spawn.

The editor accessor on `WidgetLibraryResource` can unpack a packed resource into editable state and
pack it again. External libraries, animation-library references, resource version migrations and
import/dependency discovery are first-class.

This is a critical anti-naive lesson: reusable authored composition must exist in the resource
model before the first controls are hard-coded around runtime-only construction APIs.

`backend/backendInk/src/inkCooking.cpp` further confirms that UI cooking performs dependency
collection/cook work rather than treating widget files as opaque runtime blobs. Vellum should have
schema/version migration, deterministic compiled spawn data and transitive dependency enumeration
from its first cooked resource contract.

### Animation

`animationSequence.h`, `animationDefinition.h`, `animationInterpolators.h`,
`animationInstance.h`, `animationProcessor.h`, `animationProxy.h` and related controllers show a
substantial authored animation system. `anim::Sequence::TargetInfo` has compact runtime target paths
and editor identity support. The processor separates evaluation from apply and has active/passive
ticking.

Vellum should preserve compiled target resolution, state transitions, callbacks/events and
active/passive work. It should improve target-conflict handling by precomputing write sets rather
than repeatedly scanning/grouping at runtime, and it must know whether an animated property costs
paint, hit-test or layout work.

ink animation target data also distinguishes runtime hierarchy paths from editor identity used to
repair target bindings after hierarchy edits. Vellum should make that distinction explicit with
stable authored node IDs compiled to fast runtime slots.

### Data and virtualization

`dataSource*` headers provide source/listener/view abstractions. `virtualCompoundController.h`,
`virtualListController.h`, `virtualGridController.h`, uniform variants and
`virtualItemTemplateClassifier.h` implement data-driven virtualization and multiple template
classification.

`VirtualListController` caches template/item geometry and spawns/despawns demanded ranges instead
of keeping one complete presentation subtree per record. This establishes list/grid virtualization
as an ink capability, not merely a Slate feature.

### Text, rich content and input

`textWidget.h`, `textLayoutNew.h` and `inkRendering/textFontCache.*` implement shaped text/layout
caching. Surrounding native types include `RichTextBoxWidget`, `TextInputWidget`, hyperlink
callbacks, inline image runs, inline input-key runs, localization utilities and language/platform
specific content controllers.

Shaped glyph sequences cache widths, source-to-glyph information, font faces and render data,
including ligatures/BiDi-related mapping. Text therefore affects the core layout and invalidation
model; it cannot be bolted on after geometry semantics are frozen.

### Input glyphs and device-aware prompts

`inputKeyIconManager.h`, `inputIconTextureData.h`, `inputDisplayController.h` and
`inputTextRun.h` map input/device identities to presentable icon data and allow input prompts inside
text. Vellum should preserve this capability at the semantic-action level so keyboard/controller
switching does not require hard-coded authored strings.

### Locale/platform-specific presentation

`LanguageSpecificImageController`, `LanguageSpecificVideoController`,
`PlatformSpecificImageController`, `PlatformSpecificTextController` and
`PlatformSpecificVideoController` demonstrate conditional content variants beyond text
localization. Vellum should support bounded authored variants without requiring duplicate complete
screen hierarchies.

### Reference resolution and resource variants

`WidgetLibraryResource` carries root resolution; `CompoundWidget`, `Layer`, `ImageWidget`,
`WidgetTextureAtlas`, `InkResourceManager` and related helpers support native-resolution scaling and
atlas resolution variants/overrides. The useful lesson is to separate logical layout scale from
resource-quality/resolution choice.

### Vector graphics, images and shape primitives

Native widgets include image, border, gradient, shape, quad, line-pattern and vector graphics.
`VectorGraphicWidget` loads an SVG/vector resource and derives desired size from it.

The Vellum renderer must therefore have a path for vector/shape semantics even if Rect/Image are
the first implemented draw primitives.

### Video/media

`VideoWidget` and `VideoManager` support asynchronous media, play/pause/seek, looping, prefetching,
audio synchronization, animation/external synchronization and callbacks. Language/platform-specific
video controllers also exist.

Vellum's resource/surface model should not assume every image source is a static texture.

### UI audio

`InkSystem` exposes UI sound integration; `audioUserData.h`, controller utilities and animation
default events provide control/event-driven sound hooks. World widget wrappers can update audio
handler position. Vellum should expose semantic audio events/context while leaving actual audio
playback/mixing to Vanguard's audio owner.

### Effects and fullscreen composition

`common/inkRendering/include/effect.h` enumerates scanline/linear/radial wipes, light sweep,
box blur, mask, glitch, point cloud, color fill, inner glow, color correction and multisampling.
`FullscreenCompositionResource` stores authored composition states/presets and transitions.

Vellum should represent scoped effects and global UI composition semantically, then allow the
renderer to choose a material variant, stencil/mask or offscreen pass according to actual cost.

### Layered application UI

ink has explicit layer types and resources for HUD, menu, loading, system notifications,
fullscreen composition, debug, editor, photo mode, video and other product-specific concerns.
`Layer` owns controllers/spawning/animation/event work and supports active/passive ticking.

Vellum should generalize this into a layer stack controlling order, modal/input ownership,
visibility, active/passive update and output surface instead of cloning RED's product-specific
layer subclasses.

ink also contains a viewport-level `StateMachine` plus visual-state events and transition animation
controllers. The capability lesson is useful, but Vellum should not hard-code RED's frontend/game
states. It should support authored local visual-state machines/transitions while leaving game flow
to the caller.

### World UI

`worldLayer.h`, `worldInstance.h` and component-wrapper interfaces support registered world-widget
components, streaming/spawning, live widgets vs texture-backed representations, custom hit testing,
world transforms and dynamic texture lifetime.

The source additionally tracks frustum/range demand, screen coverage and prioritized processing.
Mature world UI therefore needs budget/quality/streaming policy and hysteresis, not merely a
transform from 2D coordinates into 3D.

This directly argues against a screen-only Vellum core. World UI should be a surface/interaction
adapter over the same document/layout/style/behavior/animation runtime.

### Offscreen/render-to-texture UI

`offscreenLayer.h` can spawn a widget resource into a supplied `DynamicTexture`, with active state,
game controller and asynchronous spawn handling. This is a clean precedent for Vellum texture
surfaces used by in-world screens, compositing and external consumers.

### Async resources and lifetime

ink widget/media/font/atlas systems use async resource references and explicit resource managers/
accessors. Resource expiry/keep-alive behavior appears in several UI resource consumers.

`spawnUtils.h` and `spawningProcessor.cpp` additionally expose explicit asynchronous spawn status,
cancellation, priority, dependency tokens and bounded active work. Vellum should not synchronously
load document/template/media dependencies during construction or paint.

Vellum should use Vanguard's existing `ResourceStreamer` and residency owners, but it needs UI-level
leases/dependency tracking so resource arrival/replacement invalidates only the dependents.

### Debugging, testing and metrics

The source includes metrics gatherers/tokens, block profiling, dynamic metrics, runtime optimizer,
spawn debugger, functional testing interfaces and editor-side performance tooling. This reinforces
that observability is part of the UI platform rather than a final polishing task.

### Additional breadth confirmed in native source

The broader native inventory further confirms that Vellum's capability floor must remain wider than
the first implementation surface:

- layout includes fixed/stretch sizing, horizontal/vertical alignment, many anchor modes, transforms
  including scale/rotation/shear and dynamic compound hierarchy mutation;
- control behavior includes hold progress, toggles, sliders, steppers, selectors, combo boxes,
  radio groups, scrolling, list/grid controls and both discrete/container navigation;
- grid presentation supports more than simple one-cell rows, including multi-slot placement;
- animation supports markers/ranges, seeking, looping, nested/code/text/video/sound/VO events,
  audio synchronization and broad easing families (linear through polynomial, sinusoidal,
  exponential, elastic, circular and back variants);
- text supports tracking, line height, overflow, auto-scroll and input-glyph/image runs in addition
  to shaping/localization;
- pointer/gamepad interaction includes hold/repeat/double-click/axis/relative events and virtual
  cursor interpolation, snap/magnetism behavior;
- brushes/atlases support tiling/mirroring/material-backed drawing and masks/dynamic textures;
- world UI dynamically selects live/cached/texture representations and render-target quality from
  projected importance;
- loading, notifications, HUD entries and other product layers demonstrate queued/priority/context-
  driven presentation rather than only a static root HUD; and
- several high-cardinality product systems (for example advertisements/street signs) explicitly
  bound processing and pooled/replacement work rather than letting UI cardinality explode per frame.

Not every RED product feature needs a namesake Vellum class. The architectural lesson is that
Vellum must preserve extensible behaviors/events/layers/surfaces and bounded work so these patterns
remain natural to implement without replacing the core.

## Unreal Slate/UMG findings

### Widget/layout protocol

`SlateCore/Public/Widgets/SWidget.h`, `SCompoundWidget.h`, `SPanel.h` and `SLeafWidget.h` encode the
same broad measure/arrange/paint contract: prepass caches desired size, panels arrange children and
`Paint`/`OnPaint` emits draw work.

The protocol is useful; the macro/template-heavy declarative construction layer is not a Vellum
runtime requirement.

### Reactive typed attributes and semantic invalidation

`SlateCore/Public/Types/SlateAttribute.h` and
`Private/Types/SlateAttributeMetaData.cpp` associate reactive attributes with
`EInvalidateWidgetReason`. Changed values invalidate only the semantic work their property requires.

This is Slate's strongest architectural contribution to Vellum. Vellum adopts the idea but keeps a
smaller, explicitly testable invalidation-domain set.

### Invalidation fast path — useful result, cautionary implementation

`SlateInvalidationRoot.*` maintains proxy lists/heaps and chooses cached fast paths vs slow rebuilds.
It is sophisticated and effective, but accumulated child-order, visibility, volatility, layer-ID
repair and fallback cases make correctness complex.

Vellum should make its compact runtime tree directly carry enough dirty/cache state to avoid
rebuilding a second proxy universe around the tree.

### Draw elements and batching

`DrawElements.*` builds `FSlateWindowElementList`. `ElementBatcher.*` lowers elements to render
batches. `SlateRHIRenderer`/`SlateRHIRenderingPolicy` eventually submit indexed drawing through the
RHI.

The separation to retain is:

```text
retained UI semantics -> renderer-neutral display list -> batch/lower -> renderer backend
```

Vellum should avoid copying Slate's very broad monolithic batcher/state-permutation surface.

### Clipping

`SlateCore/Public/Layout/Clipping.h` and its implementation use cheap scissor clipping for
axis-aligned cases and stencil for transformed/complex cases. Vellum should follow the same cost
policy.

### Routed events and replies

`SlateApplication.cpp` routes pointer/key events through direct/tunnel/bubble patterns.
`SlateCore/Public/Input/Reply.h` carries handled state plus capture/focus/drag/navigation requests.

Vellum should adopt the explicit event-reply model rather than letting handlers mutate hidden
global input state.

### Focus and navigation

Slate has mature widget paths, per-user focus, directional navigation, hit-test-grid-assisted
destination calculation and explicit boundary/navigation replies. Vellum should plan per-user focus
and navigation from its first interaction gate, rather than retrofit split-screen/local multiplayer.

### Text shaping/cache

Slate's text shaper and shaped-text cache provide a fast kerning-only path where applicable and
HarfBuzz shaping for complex text. The major lesson is immutable/cacheable shaped runs separated
from higher-level widgets.

### Virtualized lists and pooling

`SListView` generates only demanded rows and recycles/releases presentation widgets. UMG adds
`FUserWidgetPool` for dynamic entries. The desired behavior is core Vellum virtualization; the dual
UMG+Slate object lifetime machinery is not.

### UMG authoring/runtime bridge

`UWidget::TakeWidget`, `UWidgetTree`, `UUserWidget` and `WidgetBlueprintGeneratedClass` build and
synchronize a UObject authoring/runtime layer over Slate widgets. This provides strong designer
integration but means two related runtime object worlds must remain coherent.

Vellum should instead compile rich authoring documents into **one** compact runtime representation.

### Notification-driven compiled MVVM

The runtime ModelViewViewModel plugin subscribes to field notifications and uses compiled binding
libraries to execute affected bindings. This is much closer to the intended Vellum model than
legacy reflection/property polling.

### Retainer/invalidation panels

`SInvalidationPanel` and `SRetainerWidget` provide cached subtree behavior and phased/invalidation-
driven render-to-texture. These are useful tools but too costly/complex to be the routine way every
static UI becomes fast. Vellum should first retain semantic/display-list work and reserve raster
retention for cases that benefit from it.

## What Vellum copies

From ink:

- game-oriented breadth and world/offscreen/layer integration;
- retained measure/arrange hierarchy;
- reusable widget/document libraries/templates;
- separate controllers/behaviors;
- styles/themes/visual states;
- animation resources + active/passive processing;
- data views and virtualized collection presentation;
- shaped/rich/editable/localized text;
- semantic input prompts + device glyphs and locale/platform conditional variants;
- authored reference-resolution/resource-quality scaling;
- media/vector/effects support;
- semantic UI audio hooks and local visual-state orchestration;
- spatial hit testing/custom hit testing;
- authored/runtime resource separation; and
- profiling/debug/test hooks.

From Slate/UMG:

- property-level semantic invalidation;
- preview/target/bubble routing + explicit replies;
- per-user focus/navigation rigor;
- neutral display-list -> batching -> renderer separation;
- scissor-first clipping policy;
- immutable/cacheable shaping results;
- collection virtualization patterns; and
- notification-driven compiled binding.

## What Vellum deliberately improves

- one compact cooked runtime tree rather than UMG + Slate dual trees;
- generation handles/arena-oriented hot storage rather than mandatory heap/shared-pointer nodes;
- a small semantic dirty-domain model instead of Slate's accumulated fast-path state machine;
- compiled style/binding/animation dependencies rather than steady-state name/reflection lookup;
- precomputed animation write sets rather than repeated collision discovery;
- deterministic structural mutation ownership instead of fine-grained live hierarchy locking;
- display-list segment retention as the default static-UI optimization, with raster cache optional;
- surface adapters for screen/texture/world UI over one runtime core; and
- built-in cardinality/dirty-work diagnostics from the first implementation.

## Anti-naive constraints before implementation

The source study makes the following constraints non-negotiable before P1:

1. **Resource composition first.** Reusable local/external templates and compiled dependencies must
   exist in the document model before controls become hard-coded runtime constructors.
2. **Semantic invalidation first.** Full-tree layout/paint cannot be the baseline architecture to be
   optimized later.
3. **Text affects layout architecture.** Font/shaping/localization dependencies must be considered
   before container APIs are frozen.
4. **World/offscreen are core surface variants.** Screen-space coordinates cannot be baked into
   node/control semantics.
5. **Virtualization affects identity/focus/data.** Large collections cannot be implemented by
   creating every control and later hiding most of them.
6. **Behavior is separate from visuals.** Otherwise reusable authored hierarchies and game-specific
   logic become coupled to engine node subclasses.
7. **Animation knows write cost.** Otherwise animation becomes a permanent source of accidental
   layout churn.
8. **One geometry truth.** Layout, paint, hit testing and navigation must consume the same arranged
   results; parallel independent geometry calculations will drift.
9. **One renderer/resource authority.** Vellum does not create a private graphics loop, texture
   manager, package/depot or presentation path.
10. **Authoring data compiles away.** Stable IDs/names are valuable for editing/debugging, but they
    should not force reflection/string traversal into hot runtime paths.
11. **Mutation ownership is explicit.** Concurrent direct tree edits are rejected in favor of one
    deterministic UI mutation/update domain.
12. **Diagnostics are architecture.** Dirty reasons, traversal cardinality, cache reuse, batches,
    active animation and virtualization counts must be observable before performance tuning.
13. **Stable authored identity differs from runtime addressing.** Hierarchy edits must not silently
    break animations/bindings/controllers; cook resolves stable editor IDs to compact runtime slots.
14. **Async UI spawning is lifecycle work.** Requests need cancellation, owner expiry, priority,
    budgets and explicit failure/completion states rather than blocking construction.
15. **World UI needs demand policy.** Range/frustum hysteresis, screen-coverage priority and
    live-vs-cached/quality decisions belong to the world-surface owner.

## Consequence for the first implementation

The first visible proof may be visually tiny, but it must exercise the production-shaped chain:

```text
cooked Vellum document
 -> ResourceStreamer
 -> compact retained instance
 -> semantic invalidation
 -> measure/arrange
 -> retained display list
 -> Vanguard renderer
 -> ordinary presentation
```

It is explicitly **not** acceptable to satisfy the first proof by adding a special direct-RHI quad,
an immediate-mode prototype or an asset format that bypasses the intended final ownership.
