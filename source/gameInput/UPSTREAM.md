# Design provenance

The pipeline mirrors RED game input: physical changes are mapped independently of action contexts, active Player/User Interface/Debug context stacks select mappings, complex action state advances once per frame, and deferred action events are ordered by priority before listener consumption. Hold, repeat, tap, multi-tap, toggle, response shaping, modifier chords, remapping, and control consumption follow the same architectural responsibilities.

Vanguard keeps the design but owns the contracts: bounded descriptor tables, stable engine IDs, immutable physical snapshots, and no RED XML, CName, RTTI, or game-specific configuration resources. Persisted mapping schemas and editor-facing mapping assets remain a later layer above this runtime core.
