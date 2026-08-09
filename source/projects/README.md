# Projects

`projects` owns the editor/tool-only `.vproject` document contract. It contains no editor UI, CLI command handling, asset cooking, or runtime startup behavior. The editor and `nanovanguard` consume this single parser, canonical writer, and validator.
