# Vellum program roadmap

## Status

The user explicitly started **V1** on 2026-10-10. V0 and V1 are **CLOSED**.

The earlier Vellum Definition is retained as **V0 Definition** and is already closed. The coarse
post-Definition P0-P5 outline in the original system definition is superseded by this program-level
V0-V9 decomposition. V1 Runtime Kernel is closed. V2, V2.5 and V3-V9 remain unauthorized until
explicitly started by the user.

## Program shape

Vellum is too broad to be treated as one engine phase with a few giant implementation gates.
The program is divided by architectural responsibility so each major subsystem can have its own
Definition/P-gates, focused validation and RED/Slate comparison without hiding several engines'
worth of work behind one checkbox.

| Phase | Scope | Current state |
| --- | --- | --- |
| **V0 — Architecture** | System constitution, ownership, runtime/data/resource/render/input/text/surface contracts, parity ledger and implementation boundaries | **CLOSED** |
| **V1 — Runtime kernel** | Compact runtime tree, handles, mutation, typed properties, semantic invalidation, basic measure/arrange and cooked document instantiation | **CLOSED** |
| **V2 — Rendering foundation** | Display lists, GPU lowering, batching/instances, clipping, image/shape primitives, atlas/bindless integration, renderer lifecycle and cache foundations | UNAUTHORIZED |
| **V2.5 — Text Backend** | Font ingestion/face services, Unicode run shaping and fallback backend, glyph raster/vector representation, glyph cache/atlas population, and renderer-facing `GlyphRun` lowering | UNAUTHORIZED |
| **V3 — Text** | Text nodes and paragraphs, line breaking/wrapping/alignment/overflow, localization, rich runs/inline content and semantic shaped-layout caching | UNAUTHORIZED |
| **V4 — Interaction** | Hit testing, routed events/replies, pointer capture, focus, navigation, per-user input, drag semantics, text input/IME integration | UNAUTHORIZED |
| **V5 — Styling / behavior / animation** | Styles/themes/states, presentation behaviors, game-facing adapters, transitions, timelines/events/audio hooks | UNAUTHORIZED |
| **V6 — Data / large UI** | Compiled binding, data sources/views, list/grid virtualization, recycling, scrolling, stable selection/focus and large-data workloads | UNAUTHORIZED |
| **V7 — Advanced presentation** | SVG/vector, advanced shapes, masks/effects, video/media, raster caches, dirty rectangles, fullscreen composition | UNAUTHORIZED |
| **V8 — Surfaces / world UI** | Screen/texture/world surfaces, offscreen rendering, projected HUD, world interaction, demand/quality budgets, local-user/split-screen | UNAUTHORIZED |
| **V9 — Authoring / production maturity** | Vanguard Editor authoring, round-trip assets, hot reload, diagnostics/profiling, accessibility tooling, final ink parity and bounded production measurements | UNAUTHORIZED |

## Cross-program rules

Every later phase inherits the V0 architecture and the REDengine ink capability floor. A later
phase may refine an interface when real implementation evidence requires it, but must not silently
replace the ownership model to make a local demo easier.

The following are permanent program rules:

- Vellum runtime is game-facing and independent of Vanguard Editor Dear ImGui.
- REDengine ink is the authoritative implementation reference for Vellum. From 2026-10-10 onward,
  Vellum is to follow ink **1:1 by default** in subsystem boundaries, runtime semantics, data flow,
  behavior, authored capability and ownership contracts. Do not redesign an ink concept merely because
  another design appears cleaner or more modern.
- Slate/UMG and other systems are secondary references only for gaps that ink does not cover; they must
  not displace an existing ink contract.
- Deviations from ink require either an unavoidable Vanguard integration constraint (for example,
  existing renderer/resource/window ownership) or explicit user authorization. In those cases preserve
  ink-observable behavior and structure as closely as possible and record the deviation and rationale.
- one compact cooked runtime representation, not mandatory authoring + runtime mirror trees;
- semantic invalidation and incremental work are baseline behavior, not later optimizations;
- no per-node jobs, per-node GPU submissions or unavoidable per-frame whole-tree discovery walks;
- `ResourceStreamer`, Window Manager, renderer/viewports and FramePipeline retain their existing
  authorities;
- Vellum never owns native Present or another application loop;
- text, virtualization, world/offscreen surfaces and per-user interaction remain architectural
  constraints even before their implementation phases;
- authored stable identities compile to fast runtime addressing;
- asynchronous UI resource/spawn work is bounded and cancelable; and
- diagnostics/cardinality evidence are designed with each subsystem, not appended in V9.

### V2.5 implementation note — copy RED ink's text backend first

When V2.5 is explicitly authorized, its baseline is **not** a fresh font-rendering design. Port the
proven REDengine ink text stack and subsystem split as directly as Vanguard's ownership model allows:

- ICU for Unicode/BiDi/break/culture services;
- HarfBuzz for shaping into glyph IDs, clusters, advances and offsets;
- FreeType for font faces, metrics, auto-hinted grayscale rasterization and stroked outlines;
- RED-style shaped-text caching before renderer lowering;
- a dynamic renderer-owned `R8_UNORM` glyph atlas/cache; and
- quad/instance-based `GlyphRun` rendering from cached atlas entries.

Primary RED references are `common/textSystem`, `common/inkRendering`,
`common/renderData/src/font.cpp`, and `common/renderer/src/fontCache.*`. Preserve this architecture
1:1 before considering MSDF/SDF/direct-outline alternatives. Any modernization that would materially
change ink's architecture, semantics or runtime behavior requires explicit user authorization; only
mechanical adaptation to Vanguard's existing renderer/resource ownership is implicit.

## Phase dependency direction

```text
V0 Architecture
   |
   v
V1 Runtime kernel -----> V2 Rendering foundation
   |                         |
   |                         v
   +-----------------> V2.5 Text Backend
   |                         |
   +--------------------> V3 Text
   |
   +-----> V4 Interaction
   |
   +-----> V5 Style/Behavior/Animation
   |
   +-----> V6 Data/Large UI
   |
   +-----> V7 Advanced Presentation
   |
   +-----> V8 Surfaces/World UI
                             |
                             v
                  V9 Authoring/Production maturity
```

This is not a strict one-phase-at-a-time dependency graph. Some later architecture can be studied
in parallel, but production implementation must not bypass the prerequisites it depends on.

## V0 entry point

The closed detailed contract and exact V1 entry boundary are
[`vellum-v0-architecture-plan.md`](vellum-v0-architecture-plan.md).
