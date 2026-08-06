# Windows platform adapter

This module implements Vanguard's Windows process boundary: UTF-16 process-command-line normalization, SDL3 event pumping, DPI-awareness setup, and process-control events translated into portable framework contracts. SDL is a hidden platform implementation dependency; its types do not cross the platform API. Its bounded backend translates physical keyboard, mouse, text, focus, and gamepad events, owns SDL gamepad handles, assigns generation-bearing gamepad IDs, performs device refresh, and forwards rumble. It does not interpret gameplay actions or own rendering devices.

Runtime and editor own their `wWinMain` entry slices and call the Windows framework adapter with their product application. The adapter constructs `WindowsPlatformHost` and enters portable `RunFramework`. Future platforms provide sibling adapters under `source/platform` and require no changes to the framework or product composition classes.
