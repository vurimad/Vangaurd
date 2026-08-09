# Editor client

`editor` is a top-level product client alongside `runtime` and `source`. Its existing subdirectories are editor feature modules; `src` owns `EditorApplication`, while `platform/<platform>` owns native launch points. The Windows slice is a windowed executable with a native `wWinMain` entry and enters the framework through the Windows adapter.

The editor selects Runtime, Editor, and Tool profiles. It compiles and executes the same managed Frame Pipeline as runtime, so editor-only participants can join the graph without creating a second loop. World Session is available for play/edit world transactions without transferring engine-service ownership into editor states. The Game World participant is a deliberate no-op while no editable or play world is active. `--validate-bootstrap` verifies the complete native entry and teardown path without requiring window or rendering services.

`--validate-workspace` additionally traverses project startup, the empty/configured world decision, transition into the running frame pipeline, and symmetric shutdown before exiting after the first running frame.

Editor composition opens and validates the requested `.vproject` through the shared `projects` module before registering engine services. A managed Project Workspace service then owns the validated document and its resolved Assets, DerivedData, Intermediate, Saved, Builds, Config, and Plugins roots for the full editor-service lifetime. The project controls the project and derived-data filesystem roots, and its display name is reflected in the primary window title as `<project> - Vanguard Editor`. No project-specific knowledge is compiled into the editor.

`startup.editorWorld` selects an ordinary `.vworld`; there is no editor-only world file format. When configured, the startup state opens the development package set under `Builds/Windows/Development`, requests the typed world through World Session, and reaches the running editor only after resource loading and Flecs materialization have completed. An empty value is a valid empty workspace. Shutdown releases the same session transaction symmetrically before engine services stop.
