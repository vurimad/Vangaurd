# nanovanguard

`nanovanguard sources inspect <project> [--format jsonl]` performs a read-only scan through the shared source database and reports source/metadata issues. It does not cook, assign identities, mount runtime resources or compose importers; records remain NotClassified until tool composition supplies descriptors. E1C.3 adds this command at source level; build/link and executable verification are deferred.

`nanovanguard` is Vanguard's headless project, build, asset, cooking, and packaging orchestration facade. Commands coordinate shared Vanguard tool APIs; they do not reimplement importers, the DDC, the build graph, or VPAK assembly.

The CLI uses strict nested commands, rejects unknown and duplicate options, provides deterministic generated help, uses stable process exit codes, accepts UTF-8 paths on Windows, and supports stable JSON Lines output for editor and CI integration.

Initial commands:

```text
nanovanguard help
nanovanguard version
nanovanguard version --format jsonl
nanovanguard project
nanovanguard project create "Project Cardinal"
nanovanguard project create "Project Cardinal" --destination "D:\\Projects\\Project Cardinal" --target windows-x64
nanovanguard project inspect "D:\\Projects\\Project Cardinal"
nanovanguard project validate "D:\\Projects\\Project Cardinal"
nanovanguard project validate "D:\\Projects\\Project Cardinal" --level document --format jsonl
```

`project create` builds the complete canonical directory tree in a uniquely named sibling staging directory, writes and re-reads the `.vproject` document, validates the staged layout, and publishes it with a no-replace atomic directory rename. Existing destinations are never modified. Failed creation removes only directories and files owned by that staging transaction.

`project inspect` accepts either a `.vproject` file or a directory containing exactly one such file. `project validate` defaults to cumulative `layout` validation; `--level document` checks only encoding, grammar, schema, and descriptor invariants. Human output is intended for terminals, while JSON Lines output is the stable editor and CI protocol.
