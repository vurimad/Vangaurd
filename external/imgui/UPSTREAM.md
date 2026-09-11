# Dear ImGui upstream

- Repository: https://github.com/ocornut/imgui
- Branch: `docking`
- Revision: `ca49eff3980443a97c470e09fe55b1740cfb9584`
- Description: `v1.92.8-docking-158-gca49eff39`
- Source version: `1.92.9 WIP` (`IMGUI_VERSION_NUM` 19287)
- Revision date: 2026-07-22
- Repository copy: `external/imgui/upstream`
- Imported content: complete upstream worktree excluding `.git`
- Local packaging patch: the Android example's `.gitignore` explicitly re-includes its `gradle` directory before `gradle/libs.versions.toml`, allowing the upstream-tracked catalog to remain visible when this checkout is vendored inside Vanguard.
- Local editor interaction patch: `imgui.cpp` keeps a moving root panel/dock tree in its current host while the pointer remains inside that host, instead of creating a native viewport merely because the panel rectangle protrudes. Leaving the host resumes normal native detachment. Dropping without docking resumes normal floating-window placement. Explicit viewport requests, `NoAutoMerge`, already detached windows, popups and tooltips retain their existing behavior. Recheck the late viewport-creation block in `Begin()` on upgrades.
- License: MIT; see `upstream/LICENSE.txt`
- Owning Vanguard product: editor
- Public engine API exposure: none; Dear ImGui types remain confined to the external target and editor-private UI implementation
- Upgrade responsibility: Vanguard editor-framework owners

Vanguard compiles only Dear ImGui's platform-independent core in the `imgui` static-library project. The upstream platform and renderer backends are retained as integration evidence but are not compiled by that project. Later editor adapters route platform requests, input, RHI recording, swap-chain ownership and presentation through Vanguard's existing owners rather than allowing an upstream backend to bypass them.
