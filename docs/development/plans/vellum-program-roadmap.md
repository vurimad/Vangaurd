# Vellum program roadmap

## Status

The user explicitly started **V0** on 2026-10-01. V0 is **CLOSED**.

The earlier Vellum Definition is retained as **V0 Definition** and is already closed. The coarse
post-Definition P0-P5 outline in the original system definition is superseded by this program-level
V0-V9 decomposition. Closing V0 does **not** authorize V1-V9; they remain unauthorized until explicitly
started by the user.

## Program shape

Vellum is too broad to be treated as one engine phase with a few giant implementation gates.
The program is divided by architectural responsibility so each major subsystem can have its own
Definition/P-gates, focused validation and RED/Slate comparison without hiding several engines'
worth of work behind one checkbox.

| Phase | Scope | Current state |
| --- | --- | --- |
| **V0 — Architecture** | System constitution, ownership, runtime/data/resource/render/input/text/surface contracts, parity ledger and implementation boundaries | **CLOSED** |
| **V1 — Runtime kernel** | Compact runtime tree, handles, mutation, typed properties, semantic invalidation, basic measure/arrange and cooked document instantiation | UNAUTHORIZED |
| **V2 — Rendering foundation** | Display lists, GPU lowering, batching/instances, clipping, image/shape primitives, atlas/bindless integration, renderer lifecycle and cache foundations | UNAUTHORIZED |
| **V3 — Text** | Fonts, shaping, fallback, Unicode/BiDi, localization, glyph caches, line layout, rich-run foundation | UNAUTHORIZED |
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
- REDengine ink remains the primary capability/ownership reference; Slate/UMG remains a secondary
  mechanism reference.
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

## Phase dependency direction

```text
V0 Architecture
   |
   v
V1 Runtime kernel -----> V2 Rendering foundation
   |                         |
   +-----> V3 Text <---------+
   |                         |
   +-----> V4 Interaction    |
   |                         |
   +-----> V5 Style/Behavior/Animation
   |                         |
   +-----> V6 Data/Large UI -+
   |                         |
   +-----> V7 Advanced Presentation
   |                         |
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
