# Design provenance

The architecture follows RED's separation between its low-level input system and game-input manager: device collection produces buffered physical changes and persistent state, while contexts and action mappings consume that state above it. Vanguard retains the architecture while using its own types, service lifecycle, frame scheduler, and SDL-backed platform adapters.
