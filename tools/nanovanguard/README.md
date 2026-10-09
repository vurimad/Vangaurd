# nanovanguard

`nanovanguard sources inspect <project> [--format jsonl]` performs a read-only scan through the shared source database and reports source/metadata issues. It does not cook, assign identities, mount runtime resources or compose importers; records remain NotClassified until tool composition supplies descriptors. The normal `sources` parent is registered and this command is covered by built/executed CLI tests.

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
nanovanguard sources inspect "D:\\Projects\\Project Cardinal" --format jsonl
nanovanguard asset status "D:\\Projects\\Project Cardinal" [source] [--target windows-x64] [--output key] [--format jsonl]
nanovanguard asset rescan "D:\\Projects\\Project Cardinal" [--target windows-x64] [--format jsonl]
nanovanguard asset build "D:\\Projects\\Project Cardinal" <source> [--target windows-x64] [--output key] [--format jsonl]
nanovanguard asset reimport "D:\\Projects\\Project Cardinal" <source> [--target windows-x64] [--format jsonl]
nanovanguard asset watch "D:\\Projects\\Project Cardinal" [--target windows-x64] [--format jsonl]
nanovanguard asset maintain "D:\\Projects\\Project Cardinal" --domain vddc|loose|all [--target windows-x64] [--format jsonl]
nanovanguard asset move "D:\\Projects\\Project Cardinal" <source> <destination> [--target windows-x64] [--format jsonl]
nanovanguard asset duplicate "D:\\Projects\\Project Cardinal" <source> <destination> [--target windows-x64] [--format jsonl]
nanovanguard asset retarget "D:\\Projects\\Project Cardinal" <source> --output <key> --replacement-source <source> --replacement-output <key> [--target windows-x64] [--format jsonl]
nanovanguard asset delete "D:\\Projects\\Project Cardinal" <source> --dependents reject|replace [replacement mappings] [--target windows-x64] [--format jsonl]
```

`project create` builds the complete canonical directory tree in a uniquely named sibling staging directory, writes and re-reads the `.vproject` document, validates the staged layout, and publishes it with a no-replace atomic directory rename. Existing destinations are never modified. Failed creation removes only directories and files owned by that staging transaction.

`project inspect` accepts either a `.vproject` file or a directory containing exactly one such file. `project validate` defaults to cumulative `layout` validation; `--level document` checks only encoding, grammar, schema, and descriptor invariants. Human output is intended for terminals, while JSON Lines output is the stable editor and CI protocol.

The P3 base `asset` family is `status`, `rescan`, `build`, `reimport` and
foreground `watch`; P4 adds `maintain`, `move`, `duplicate`, `retarget` and
`delete` without adding a second project-asset backend. Source arguments are lexical
UTF-8 paths relative to the project's `Assets` directory and resolve only
through accepted project state; output selection uses exact accepted VMETA
output keys. If `--target` is omitted the project must declare exactly one
target; the current supported project-asset mapping is `windows-x64`.
Reimport always covers the asset's complete current accepted output set and does
not accept `--output`.

Human and JSONL output share one semantic event model. Parsed asset operations
emit zero or more status/progress/diagnostic records and exactly one terminal
`completed`, `failed` or `cancelled` record. A syntactically valid raw
`--format jsonl` / `--format=jsonl` preselection also renders parser usage
errors as JSONL; invalid or conflicting format options remain ordinary usage
errors. Handler-side semantic usage validation emits the normal `failed` terminal
while retaining exit 2; only structural parser failures emit `usage-error`.
Exit codes are 0 success, 2 usage, 10 invalid project/target selection,
20 parsed operation/locator/pipeline failure, 30 current-session cancellation
and 70 bootstrap/protocol/unsafe-lifecycle failure.

On Windows, foreground cancellation uses a process-local Ctrl+C/Ctrl+Break
handler that only sets a small interlocked flag. The owner thread observes that
flag and cancels/drains the existing pipeline owner; the handler never calls
pipeline APIs. Deterministic integration coverage injects the same owner-visible
cancellation flag. Physical console-event delivery is not claimed as a tested
path, and nanovanguard exposes no cross-process status/cancel protocol.

`asset maintain` uses the existing VDDC/loose maintenance owners; `all` runs VDDC
to a true successful terminal before starting loose. A later CLI process starts a
fresh request rather than replaying session-local Retry state. `move` preserves
AssetId, `duplicate` draws its identity inside ProjectAssetPipeline, and
`retarget`/`delete` accept only explicit accepted-state replacement mappings.
The CLI does not choose physical deletion victims, rewrite authored files itself,
or expose persistent `asset undo`/`asset redo`. Mutation/history and maintenance
retry state are session-local and are not replayed after process restart.
