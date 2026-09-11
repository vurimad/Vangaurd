# Projects

`projects` owns the shared authoring/tool `.vproject` document and workspace contracts. It contains no editor UI, CLI command handling, asset cooking, or runtime startup behavior. The editor and `nanovanguard` consume this single parser, canonical writer, validator, and workspace resolver.

`ResolveWorkspace` validates the descriptor and document filename and resolves its seven non-overlapping roots beneath the project directory. It performs no I/O, does not require a global filesystem manager, and leaves its output unchanged on failure. Editor startup uses the result to configure the filesystem; the editor workspace service retains it; the CLI uses it before checking that the directories exist. Document-only validation remains distinct from workspace/layout validation.

These are canonical lexical paths, not physical filesystem authorization: symlinks, junctions, and filesystem aliases require a physical containment policy before future asset mutation operations follow them. The resolver does not create directories or mount content.

## Source ownership for E1

- The project's `Assets` root contains writable authored sources and sidecars. Generated content belongs in the configured derived/intermediate/build roots, never underneath `Assets`.
- Engine and plugin content are explicit read-only inputs to the future source catalog, not implicitly writable project assets. `Plugins` resolves the project plugin directory; it does not mount every child as an asset source or grant source-write permission.
- `source/assets` owns source inventory, identity mapping, dependency/build integration and queries. Format-specific processing stays in the existing tool modules. Editor services and standalone tools consume those APIs; runtime loading remains in `resources`/streaming.
- Physical source paths do not become durable asset IDs. Metadata, output identity mapping and source-root registration belong to the following E1 slices, not to the workspace resolver.
