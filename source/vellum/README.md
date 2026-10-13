# Vellum runtime kernel

`vellum` is Vanguard's game-facing, headless UI runtime kernel. It owns compact retained document/node
state, typed semantic mutation, layout and cooked document materialization. Rendering integration lives
in a later `vellumRendering` phase and is intentionally not part of this module.

The runtime composes existing Vanguard authorities rather than replacing them. In particular,
`ResourceStreamer` remains resource/source/generation authority and is injected non-owningly by the
composition root.

The durable architecture and active implementation plan live under
`docs/development/plans/vellum-v0-architecture-plan.md` and
`docs/development/plans/vellum-v1-runtime-kernel-plan.md`.
