# Game Input

`gameInput` translates immutable physical input frames into application actions. It does not know which platform or library produced the physical controls.

The compiled mapping contains bounded, allocation-free runtime tables for Player, User Interface, and Debug context stacks. Bindings support keyboard, mouse, and gamepad controls; modifiers; scalar and two-dimensional composition; hysteretic analog-to-button thresholds; radial dead zones; piecewise response curves; sensitivity; and remappable defaults with conflict reporting.

Button actions support press, release, tap, multi-tap, hold progress/completion, repeat, and toggle state. Action events are accumulated in deterministic context/action priority order, then dispatched to stable-priority listeners. A listener may consume only an action event or consume its physical control for subsequent events.

The engine `GameInputService` runs in `FramePhase::Input` after `InputFrameParticipantId`. Applications register mapping descriptors during composition/session entry; the service compiles the map before its first frame and evaluates it from `InputService::Snapshot()` and `InputService::Events()`. SDL therefore remains entirely below the physical input boundary.

## Cooked mapping resource

`vinput` is the runtime mapping resource (`VINP`). It stores contexts, actions, bindings, modifier controls, response curves, timing policy, overridable defaults, and initial context-stack order. Records are canonicalized by stable ID, making the cooked output byte deterministic even when importer/editor source order changes. The document uses Vanguard's versioned section container and a CRC64 over the mapping body; bounded readers validate every count, enum, reference, duplicate, control, curve, and timing contract before publication.

`MappingFile::Install` is transactional: it constructs and compiles a complete candidate `ActionMap`, then replaces the live map only on success. The resource streamer decoder is owned by `GameInputService`, so loose resources and VPAK-contained mappings follow the same loading route. Editor source remains outside this runtime format: `gameInputTools` parses the versioned source mapping grammar and registers its deterministic `vinput` compiler with the ordinary asset/DDC pipeline.
