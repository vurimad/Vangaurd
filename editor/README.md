# Editor client

`editor` is a top-level product client alongside `runtime` and `source`. Its existing subdirectories are editor feature modules; `src` owns `EditorApplication`, while `platform/<platform>` owns native launch points. The Windows slice is a windowed executable with a native `wWinMain` entry and enters the framework through the Windows adapter.

The editor selects Runtime, Editor, and Tool profiles. It compiles and executes the same managed Frame Pipeline as runtime, so editor-only participants can join the graph without creating a second loop. World Session is available for play/edit world transactions without transferring engine-service ownership into editor states. The Game World participant is a deliberate no-op while no editable or play world is active. `--validate-bootstrap` verifies the complete native entry and teardown path without requiring window or rendering services.
