# Generated test data

Use `vanguard::tests::ScratchDirectory("suite_name")` for disposable filesystem
fixtures. It creates `<platform temp>/VanguardTests/<configuration>/<suite>/`
through the existing filesystem API and fails explicitly if creation fails.
The launch working directory must never determine where a test writes files.
Existing fixture cleanup remains in the test; an interrupted run leaves data
inside the scratch directory instead of the engine checkout. Do not run two
instances of the same suite/configuration concurrently against that directory.

Authored assets in `tests/data_assets` and engine shader sources remain inputs.
Do not move them into scratch storage or delete them during fixture cleanup.
The low-level diagnostics smoke test has no filesystem dependency and uses the
platform temp location directly, with a process-specific log filename.

Editor caches use the project's configured DerivedData location; intermediate
generated work uses Intermediate, and logs/user state use Saved. The editor log
opens after project resolution at `<project Saved>/Logs/VanguardEditor.log`.
Standalone runtime machine caches use the platform user-cache path. Packaged
game DATA files belong beside the packaged executable. Explicitly cooked images
intended for interactive editor/runtime validation belong in a caller-selected
build/image directory; they are distinct from disposable test fixtures.
