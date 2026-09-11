# Windows platform adapter

Editor-profile startup enables SDL mouse-focus click-through: an activation click is also delivered as input. Other product profiles do not change this SDL hint. Mouse auto-capture remains SDL-owned.

This module implements Vanguard's Windows process boundary: UTF-16 process-command-line normalization, SDL3 event pumping, DPI-awareness setup, and process-control events translated into portable framework contracts. SDL is a hidden platform implementation dependency; its types do not cross the platform API. Its bounded backend translates physical keyboard, mouse, text, focus, and gamepad events, owns SDL gamepad handles, assigns generation-bearing gamepad IDs, performs device refresh, and forwards rumble. It does not interpret gameplay actions or own rendering devices.

Runtime and editor own their `wWinMain` entry slices and call the Windows framework adapter with their product application. The adapter constructs `WindowsPlatformHost` and enters portable `RunFramework`. Future platforms provide sibling adapters under `source/platform` and require no changes to the framework or product composition classes.

Mouse motion preserves local coordinates and relative deltas for game input. When the SDL source window is available, translation also records desktop coordinates using its native position at that point, with `hasDesktopPosition` indicating validity. Deferred UI consumers must use those coordinates rather than add a later window position. This matches SDL ImGui's conversion boundary; it is not a claim of reconstructing historical OS coordinates for events already queued within SDL. The platform regression moves the window after translation and before draining input to verify the preserved values.
