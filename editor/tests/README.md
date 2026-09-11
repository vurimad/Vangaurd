# Native UI integration proof

## Interactive E0F pass

Drag activation correction: `Platform_ShowWindow` now forwards ImGui's `NoFocusOnAppearing` through `WindowStateRequest::activateWhenShown`. SDL temporarily suppresses show-time activation for that operation and restores its previous effective setting afterward. This avoids stealing focus from the window holding an active drag. Editor process startup also enables SDL focus-click-through so an activation click can operate a tab/control. Automatic SDL mouse capture and real application focus-loss handling are unchanged. Check a single uninterrupted drag from docked to detached, and a first-click drag on an unfocused editor host; native focus/hint checks do not replace that physical-input observation.

Transparent docking preview: `ConfigDockingTransparentPayload` is enabled through an editor-private `Platform_SetWindowAlpha` callback and `WindowManager::SetOpacity`. Native SDL opacity is restored to one by ImGui when transparency is no longer needed. Dock hints are drawn only on the target viewport, avoiding the second copy riding on the dragged window's older rendered contents. Check that hints remain anchored and the dragged window returns to opaque after docking or dropping outside. The separate Present-stall investigation remains open.

Placement feedback correction: window-manager move/resize acknowledgements that already match ImGui's integer native placement no longer set `PlatformRequestMove`/`PlatformRequestResize`. Differing OS placements still do. An isolated ImGui reproduction confirmed that the old echoed move flag suppresses the next native position update during an ongoing drag, while treating it as an acknowledgement permits the update. Live overlap/hint stability is not yet verified; the earlier desktop-coordinate change alone did not resolve the user's report.

Drag-input correction: platform input translation now preserves desktop mouse coordinates alongside unchanged local coordinates/deltas. The editor no longer adds a later window position to buffered motion. Hover reporting follows native mouse-enter/leave events, not the recipient of captured motion; reporting is disabled during button capture or a docking payload so ImGui infers the target underneath. This adds no locks or presentation changes. Debug platform translation regression verifies a window move between translation and input drain; live hint stability and any effect on Present stalls still require user observation.

Current controlled comparison: editor host swap chains disable tearing. Secondary hosts retain Immediate/interval-zero presentation and the primary retains FIFO. Runtime defaults, job waits and resource-fence protection are unchanged. This follows three stall captures showing worker threads in `NtDxgkSubmitPresentToHwQueue`; those captures did not establish a sent-message deadlock or a driver defect. Interactive results are pending; do not treat the no-tearing policy as a confirmed stall fix. Use the ordinary interactive command for the first comparison so debugger pauses do not affect the observation.

### Automatic stall capture

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File editor/tests/capture-ui-stall.ps1
```

This launches the Debug interactive proof with an opt-in auto-reset heartbeat event. Only this diagnostic launch signals the event once per completed UI frame; ordinary launches add no per-frame event signaling. An external watcher arms after the first completed frame. After 750 ms without a heartbeat it attaches the locally installed x64 Windows debugger, writes all thread stacks and a small minidump, then detaches. It captures at most three separate stalls and waits for frame recovery between captures. This detects frame-loop stalls even when Windows sent-message processing remains responsive.

Artifacts and the editor log go into a unique timestamped directory under `build/e0f/hang-capture`. Dumps include thread-stack memory and can contain sensitive process data; keep them local. Capturing briefly pauses the process, so capture-time latency is not an uninstrumented performance measurement. Close the editor normally when done. If the debugger fails to detach, the script reports its PID; do not kill an attached debugger because that can terminate the editor.

The script syntax and debugger attach/stack/dump/detach path were smoke-tested on a bounded helper process. Debug editor compilation passes. This does not yet establish that an actual editor stall has been captured. Acquisition timing now separates swap-chain lock acquisition, native back-buffer-index query and GPU-fence waiting.

Render-wait correction: user logs correlated 1.2–2.0-second native `Present` calls with the main-thread render-chain join, including interval-zero presentation. Inspection found the imported job waiter already services synchronous Windows messages, but uses a one-second queued-reminder throttle. The `redJobsCompat` build now selects `VG_INTERACTIVE_DXGI_WAIT`: the existing idle-wait pump services sent messages at most once per millisecond, without allocating reminder jobs or consuming posted input (`PM_NOREMOVE | PM_QS_SENDMESSAGE | PM_NOYIELD`). Counter completion, job assistance, graph presentation and the join remain unchanged. This policy applies to the shared Windows job waiter, not only the editor.

A Windows Jobs regression requires three successive worker-to-main synchronous messages to complete within their 500-ms timeouts while `WaitOnProcessFrame` is active, and verifies a posted message remains queued. Debug Jobs and native UI lifecycle tests pass. Actual drag smoothness still awaits user confirmation. Earlier statements that the imported waiter had no native-message pump were incorrect; it had an unsuitable latency policy for this interaction.

Temporary Debug stall diagnostics now log operations lasting at least 100 ms as `UI stall:`. Timed paths include the whole running tick, render-chain join, presentation reconciliation/resize, host creation/destruction, host submission and ImGui platform-window updates. These scopes allocate nothing, compile to empty scopes outside Debug, and only log slow operations. The automated baseline did not reproduce the user's interactive freeze; use the interactive run to collect evidence before removing synchronization or changing lifetime behavior. Remove the temporary probe after this investigation.

Smoothness follow-up: user testing reported the latency-timeout crash stopped, but docking remained slow. Interactive mode no longer performs the automated fixture's extra end-of-frame join or reads its joined render counters. Secondary UI hosts use Immediate presentation rather than each adding a FIFO refresh wait; the primary retains FIFO, and back-buffer GPU-fence protection remains unchanged. Debug compilation and automated lifecycle regression pass. This is a source-backed reduction of waits, not a measured claim that all docking stalls are fixed; resize still contains the RHI's GPU-idle boundary and requires separate profiling before changes.

Interactive failure found: native dragging eventually returned `DXGI frame-latency pacing timed out` (backend code 258). Editor host outputs now disable the optional per-swap-chain latency pacing wait, which otherwise blocks the main-thread UI loop for up to five seconds per host. The RHI still waits for each back buffer's GPU completion fence; runtime presentation defaults are unchanged. Debug rebuild and automated lifecycle regression cover the change; user drag/redock confirmation remains required. Other drag performance and docking UX are not declared solved by this change.

Launch the built Debug editor with an empty proof project and `--inspect-editor-ui` (the automated runner creates the project below):

```powershell
& .\bin\Editor\Debug\VanguardEditor.exe .\build\e0f\interaction-regression\project\ui-proof.vproject --inspect-editor-ui
```

This mode stays open, permits native auto-merge/redocking, and disables scripted resize/closure. Each panel has independent text input and click counters. Closing a panel now propagates ImGui's open flag to the panel registry; the previous fixture omitted that connection. Close the main OS window to exit. This is an opt-in fixture, not the normal editor shell.

Remaining manual checks:

- Drag a docked panel toward another docking target while keeping the pointer inside its host: protruding panel edges must not create an OS window. Move the pointer outside to detach normally. Release over a docking hint to dock; release without docking retains normal floating-window behavior. This editor-only ImGui interaction patch does not claim to fix native Present stalls.
- Drag the docked tab outside, then redock it. Confirm its text and click count survive.
- Alternate focus between hosts; type, paste, click and scroll. Confirm only the intended panel responds.
- Resize and minimize/restore hosts; verify the image, clipping and input coordinates remain correct.
- Move between different-DPI monitors when available; verify text scale, clipping, dragging and hit testing.
- Close a detached panel, then close the editor while rendering.
- Font-atlas updates while all hosts are minimized still need a dedicated exercised case; ordinary restore alone does not prove this.

The interactive fixture builds in Debug and the automated 120-frame regression still passes. No actual drag/redock or mixed-DPI success is claimed: this agent session has no desktop interaction tool. E0F remains open pending those observations.

## Automated lifecycle pass

After generating with Premake and building the Debug editor, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File editor/tests/run-ui-proof.ps1 -Executable bin/Editor/Debug/VanguardEditor.exe -Artifacts build/e0f/debug
```

The script copies the empty generic project fixture into the artifact directory if absent. An optional `-Project` selects an existing project. It launches `--validate-editor-ui`, captures only windows owned by that process, prints its log and returns its exit code. A timeout leaves the process available for diagnosis.

Two ordinary registered panels exercise the existing UI service and graph renderer. The proof checks presentation on both native hosts, a detached output-size change, detached closure and a 120-frame clean exit. It displays the font atlas, a four-color registered image and clipped child rows. The fixture uses a one-time startup texture upload; production atlas updates continue through the allocator-declared upload graph. The proof-only render-tail join permits safe observation of the existing viewport counters; it adds no production synchronization.

Screenshots are evidence to inspect, not pixel assertions. Interactive drag/dock/redock, input focus, mixed-DPI monitor moves and atlas updates while minimized require additional verification. The detached fixture uses NoAutoMerge for deterministic native-host creation; its automatic close is not a redocking test.

E0F corrections found by native execution: valid shared sampler-domain capacity, reuse of rendering-owned descriptor domains with explicit slot retirement, editor build/API corrections, and removal of a second display-origin/DPI transform on captured framebuffer-local clip rectangles. No panel-specific rendering path was introduced.

Debug and Shipping native lifecycle passed on D3D12 / RTX 4080 Laptop GPU. Shipping completed 120 frames with both hosts presenting, detached resize/closure and exit code 0. Its run ended before the screenshot delay, so visual inspection evidence remains the Debug captures.

When building `editor.vcxproj` directly, build `build/projects/vs2022/SDL3.vcxproj` with the same Configuration and Platform first. Premake's SDL3 `dependson` ordering is represented at solution level; a direct editor project build does not traverse that dependency. Building the Shipping SDL3 target resolved the missing import library without source changes.
