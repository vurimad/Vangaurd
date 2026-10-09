# Editor integration tests

## Test-support boundary

Native UI integration scaffolding is test code and is not linked into the
production `VanguardEditor` target. Its C++ implementation lives only under
`editor/tests/support/`, uses explicit `*test*`/`*harness*` names, and is compiled
only by the dedicated `editorNativeUiIntegrationTest` Premake target. That target
builds the normal editor sources plus the test support so the fixture still
exercises the real composition. Normal `VanguardEditor` uses only the production
compiled-extension catalog and contains no native UI test command-line handling.
The dedicated test application wrapper interprets those modes, delegates to the
real `EditorApplication` composition, then adds a test-owned service/catalog from
`editor/tests/support/`. Production services and features never depend on the
test descriptors or harness.

Application lifecycle smoke coverage follows the same boundary. The dedicated
`editorApplicationLifecycleIntegrationTest` target produces
`VanguardEditorApplicationLifecycleIntegrationTest` from the normal production
editor sources plus the explicitly listed test application/main under
`editor/tests/support/`. Production `VanguardEditor` contains no lifecycle-test
CLI switches or early-exit branch. The project descriptor declares ordinary project
directories, so materialize them in a contained fixture root before running the dedicated
executable. From the repository root:

```powershell
$fixture = '.\build\editor-application-lifecycle-smoke'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item .\editor\tests\editor-application-lifecycle-integration-test.vproject `
  (Join-Path $fixture 'editor-application-lifecycle-integration-test.vproject') -Force
@('Assets','DerivedData','Intermediate','Saved','Builds','Config','Plugins') | ForEach-Object {
  New-Item -ItemType Directory -Path (Join-Path $fixture $_) -Force | Out-Null
}

& .\editor\bin\EditorTests\Debug\VanguardEditorApplicationLifecycleIntegrationTest.exe `
  (Join-Path $fixture 'editor-application-lifecycle-integration-test.vproject') --bootstrap-path

& .\editor\bin\EditorTests\Debug\VanguardEditorApplicationLifecycleIntegrationTest.exe `
  (Join-Path $fixture 'editor-application-lifecycle-integration-test.vproject') --workspace-path
```

`--bootstrap-path` reselects the real editor running state after ordinary product
composition; `--workspace-path` retains the product startup-session state. A
test-owned EndFrame participant requests exit after the first successful real editor
frame. This intentionally replaces the historical product smoke flags, which exited
before ordinary frame execution, with stronger test-owned one-frame coverage.

Development phase/task labels are historical documentation, not executable
architecture. Do not add phase numbers such as `P5` or `E3.3` to test-support
filenames, C++ identifiers, plugin technical names, environment variables, or
runtime log contracts. Name reusable test support by the behavior it exercises.

## Activity service core

`editorActivityTests` is the focused GPU/toolkit-free oracle for the production
diagnostics/activity service core. It compiles `framework/src/editor_activity_service.cpp`
directly with the public/private Activity contracts and a narrow in-test Frame Pipeline
capability fixture. It does not compile or link `editor_ui.cpp`, ImGui, rendering, RHI,
window, `assetStreaming`, the aggregate `engine` project, shell extensions or production
panels.

The focused target owns bounded diagnostics-ingress/retention/coalescing/gap/eviction
behavior, service-instance cursor staleness, wrong-thread rejection, bridge-only callback
lifetime, diagnostics detach-failure restart reuse, and the P1 source-neutral cancellation
boundary. Its real service fixture verifies the production Activity participant descriptor
is BeginFrame-after-Input. Full compiled `Input -> Activity -> UI` schedule proof remains
the existing application-lifecycle integration target because that is the target that owns
the real UI service and Frame Pipeline composition. Typed source registration, shell-owned
source lifetime and producer cancellation routing are later Activity work and are not
implemented by this P1-focused target.

## User-state persistence core

`editorUserStateTests` is the focused non-native oracle for the bounded project-local
user-state document/store plus panel/layout/shortcut/preference persistence core. It compiles
`src/editor_user_state_service.cpp` plus the panel registry directly and exercises canonical
VUSR encoding, whole-envelope validation, project/future/integrity/bound rejection,
collision-safe sibling-stage publication, bounded PNLS/UIST staging, exact panel restore
decisions, synthetic saved/current-display placement normalization, SHCT/PREF materialization,
exact-key live/orphan merge and overflow/collision rejection, whole-envelope semantic-preflight
ordering, and dirty/baseline/final-persistence policy. Live command shortcut transactions are
proved by `editorCommandTests`; Shell provider registration/validation/apply/revision/revoke
and Activity preference policy are proved by `editorShellTests`. The focused UserState target
also owns P4 recovery source oracles for rejected/future/foreign/unreadable canonical
no-clobber/write-inhibit behavior, abandoned-stage non-promotion, stage/replace/cleanup failures,
retry/final-quiesce policy, durable exact-key orphan round trips, captured-placeholder restart
identity, and removed-display/DPI/work-area placement recovery. It does not instantiate the
ImGui platform or invoke extension-provider callbacks directly; full native save/restart/docking
behavior remains owned by later validation gates.

The production service participates in the real dependency graph and loads/validates the
canonical image before the first editor frame. Publication remains closed until authoritative
owner capture can supply a complete coherent document; the focused store helper still proves
the complete stage/write/reopen/validate/replace transaction without adding a product test
mode. Generic physical exclusive-write and complete-read behavior remains owned by
`filesystemTests`.

## Surface registry core

`editorSurfaceTests` is the focused GPU/toolkit-free oracle for the production
surface registry core. It compiles `surfaces/editor_surface_registry.cpp` directly
with the private declarative surface vocabulary and exercises bounded identities,
generation-stale tokens, built-in roots/anchors, copied labels, deterministic
structural placement, hierarchy/owner restrictions, topology phase/thread checks
and already-stamped pending context-menu transaction state. It also exercises the
production private command-bridge slot plus item-local presentation/invocation failure
containment and bounded failure status without compiling the toolkit UI owner. The
transaction coverage additionally owns cross-owner request rejection, exact-host
cancellation, preservation of an already committed popup when no replacement request
commits, and ContextMenu-only Unsupported omission with normalized separators from one
presentation query per command.

The target does not compile `editor_ui.cpp`, ImGui, rendering, RHI, window or
platform implementations. It therefore makes no real menu/toolbar authoring, panel
authoring-admission or native popup claim. `editorPanelTests` owns the narrow private
authoring-view regression proving an extension-owned panel carries its exact owner into
the UI authoring seam without exposing owner lookup publicly, plus the effective-open
receiver check used to reject/cancel popup work immediately after an in-frame pending
close. `editorCommandTests` owns
the atomic single-resolution command-presentation snapshot and no-handler/empty-binding
route.

`editorShellTests` also compiles the same production surface core behind its
fixture UI service. Its focused ownership cases exercise extension-stamped context
roots and menu/toolbar contributions, same-owner command references, cross-owner
private-root/anchor rejection, shared owned-registration capacity, logical owner
revoke and exact reverse physical cleanup. The same fixture owns shell command-bridge
installation rollback and bridge-before-panel-focus-before-UI-quiesce teardown
ordering. It does not provide a second surface registration algorithm or claim native
ImGui rendering behavior.

## Interactive E0F pass

Drag activation correction: `Platform_ShowWindow` now forwards ImGui's `NoFocusOnAppearing` through `WindowStateRequest::activateWhenShown`. SDL temporarily suppresses show-time activation for that operation and restores its previous effective setting afterward. This avoids stealing focus from the window holding an active drag. Editor process startup also enables SDL focus-click-through so an activation click can operate a tab/control. Automatic SDL mouse capture and real application focus-loss handling are unchanged. Check a single uninterrupted drag from docked to detached, and a first-click drag on an unfocused editor host; native focus/hint checks do not replace that physical-input observation.

Transparent docking preview: `ConfigDockingTransparentPayload` is enabled through an editor-private `Platform_SetWindowAlpha` callback and `WindowManager::SetOpacity`. Native SDL opacity is restored to one by ImGui when transparency is no longer needed. Dock hints are drawn only on the target viewport, avoiding the second copy riding on the dragged window's older rendered contents. Check that hints remain anchored and the dragged window returns to opaque after docking or dropping outside. The separate Present-stall investigation remains open.

Placement feedback correction: window-manager move/resize acknowledgements that already match ImGui's integer native placement no longer set `PlatformRequestMove`/`PlatformRequestResize`. Differing OS placements still do. An isolated ImGui reproduction confirmed that the old echoed move flag suppresses the next native position update during an ongoing drag, while treating it as an acknowledgement permits the update. Live overlap/hint stability is not yet verified; the earlier desktop-coordinate change alone did not resolve the user's report.

Drag-input correction: platform input translation now preserves desktop mouse coordinates alongside unchanged local coordinates/deltas. The editor no longer adds a later window position to buffered motion. Hover reporting follows native mouse-enter/leave events, not the recipient of captured motion; reporting is disabled during button capture or a docking payload so ImGui infers the target underneath. This adds no locks or presentation changes. Debug platform translation regression verifies a window move between translation and input drain; live hint stability and any effect on Present stalls still require user observation.

Current controlled comparison: editor host swap chains disable tearing. Secondary hosts retain Immediate/interval-zero presentation and the primary retains FIFO. Runtime defaults, job waits and resource-fence protection are unchanged. This follows three stall captures showing worker threads in `NtDxgkSubmitPresentToHwQueue`; those captures did not establish a sent-message deadlock or a driver defect. Interactive results are pending; do not treat the no-tearing policy as a confirmed stall fix. Use the ordinary interactive command for the first comparison so debugger pauses do not affect the observation.

### Automatic stall capture

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File editor/tests/capture-native-ui-integration-test-stall.ps1
```

This launches the Debug interactive native UI test with an opt-in auto-reset heartbeat event. Only this diagnostic launch signals the event once per completed UI frame; ordinary launches add no per-frame event signaling. An external watcher arms after the first completed frame. After 750 ms without a heartbeat it attaches the locally installed x64 Windows debugger, writes all thread stacks and a small minidump, then detaches. It captures at most three separate stalls and waits for frame recovery between captures. This detects frame-loop stalls even when Windows sent-message processing remains responsive.

Artifacts and the editor log go into a unique timestamped directory under `build/editor-native-ui-test/stall-capture`. Dumps include thread-stack memory and can contain sensitive process data; keep them local. Capturing briefly pauses the process, so capture-time latency is not an uninstrumented performance measurement. Close the editor normally when done. If the debugger fails to detach, the script reports its PID; do not kill an attached debugger because that can terminate the editor.

The script syntax and debugger attach/stack/dump/detach path were smoke-tested on a bounded helper process. Debug editor compilation passes. This does not yet establish that an actual editor stall has been captured. Acquisition timing now separates swap-chain lock acquisition, native back-buffer-index query and GPU-fence waiting.

Render-wait correction: user logs correlated 1.2–2.0-second native `Present` calls with the main-thread render-chain join, including interval-zero presentation. Inspection found the imported job waiter already services synchronous Windows messages, but uses a one-second queued-reminder throttle. The `redJobsCompat` build now selects `VG_INTERACTIVE_DXGI_WAIT`: the existing idle-wait pump services sent messages at most once per millisecond, without allocating reminder jobs or consuming posted input (`PM_NOREMOVE | PM_QS_SENDMESSAGE | PM_NOYIELD`). Counter completion, job assistance, graph presentation and the join remain unchanged. This policy applies to the shared Windows job waiter, not only the editor.

A Windows Jobs regression requires three successive worker-to-main synchronous messages to complete within their 500-ms timeouts while `WaitOnProcessFrame` is active, and verifies a posted message remains queued. Debug Jobs and native UI lifecycle tests pass. Actual drag smoothness still awaits user confirmation. Earlier statements that the imported waiter had no native-message pump were incorrect; it had an unsuitable latency policy for this interaction.

Historical stall investigation used temporary Debug `UI stall:` scopes in the product tree. Those scopes were removed during the post-closure test-support cleanup; retained stall capture now lives entirely in the dedicated native UI test harness/driver and uses its test-only heartbeat plus debugger capture. Do not reintroduce temporary instrumentation into production editor source for this workflow.

Smoothness follow-up: user testing reported the latency-timeout crash stopped, but docking remained slow. Interactive mode no longer performs the automated fixture's extra end-of-frame join or reads its joined render counters. Secondary UI hosts use Immediate presentation rather than each adding a FIFO refresh wait; the primary retains FIFO, and back-buffer GPU-fence protection remains unchanged. Debug compilation and automated lifecycle regression pass. This is a source-backed reduction of waits, not a measured claim that all docking stalls are fixed; resize still contains the RHI's GPU-idle boundary and requires separate profiling before changes.

Interactive failure found: native dragging eventually returned `DXGI frame-latency pacing timed out` (backend code 258). Editor host outputs now disable the optional per-swap-chain latency pacing wait, which otherwise blocks the main-thread UI loop for up to five seconds per host. The RHI still waits for each back buffer's GPU completion fence; runtime presentation defaults are unchanged. Debug rebuild and automated lifecycle regression cover the change; user drag/redock confirmation remains required. Other drag performance and docking UX are not declared solved by this change.

Launch the built Debug editor with the native UI integration-test project and `--inspect-editor-ui` (the automated runner creates the project below):

```powershell
& .\editor\bin\EditorTests\Debug\VanguardEditorNativeUiIntegrationTest.exe .\build\e0f\interaction-regression\project\native-ui-integration-test.vproject --inspect-editor-ui
```

This mode stays open, permits native auto-merge/redocking, and disables scripted resize/closure. Each panel has independent text input and click counters. Closing a panel now propagates ImGui's open flag to the panel registry; the previous fixture omitted that connection. Close the main OS window to exit. This is an opt-in fixture, not the normal editor shell.

The automated native interaction path uses the same test harness with real Windows input:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File editor/tests/run-native-ui-integration-test.ps1 `
  -Executable editor/bin/EditorTests/Debug/VanguardEditorNativeUiIntegrationTest.exe `
  -Artifacts build/e3-3-p5/interactive-current `
  -InteractiveNativeUiTest
```

`native-ui-integration-test.vproject` selects the compiled `editor-native-ui-integration-test` test extension. In automation mode that extension registers one command, one owned Local context, one handler and one panel type through the ordinary editor shell registrar; the same extension owner also registers a real MainMenu contribution, a real MainToolbar contribution and one owned ContextMenu root/contribution set. The panel requests that context menu only through `EditorPanelContext::RequestContextMenu`. Test-owned presentation modes expose Enabled, Disabled, Unsupported, checked and intentionally rejected-Invoke states, while the existing test service can exercise the public `EditorCommandService::SetShortcutOverride` path from default F8 to custom F12. The external Windows driver retains its real process-owned detach/redock, cross-host focus/input, resize/move, minimize/restore, DPI, minimized-atlas and close/reopen evidence and now also has source-level acceptance steps for real menu/toolbar activation, primary-only persistent composition, live shortcut-hint change, primary/detached exact-next-frame contextual popup placement/routing, request-time anchoring, Unsupported omission/separator normalization, cancellation and rejected-Invoke frame continuity. Screenshots remain visual evidence; machine oracles use the fixture's native/ImGui telemetry rather than pixel comparison.

E3.5 P5 extends this same phase-neutral fixture in source only. The native custom extension
registers one synthetic cancellable Activity source through the normal shell activation
context; its query only copies producer state, while the existing test service advances the
producer independently. The noninteractive path semantically proves Background Activity
close/reopen/latest-state and queued-cancel ordering. The real-input driver uses visible-title
black-box telemetry for the production Background Activity/Console tabs to prepare hidden,
detach/redock and one real-mouse cancel interaction, with Activity service publication and
owner-callback counters as the oracle. A unique worker-thread diagnostic is observed through
`EditorActivityService::CaptureDiagnostics`. P5 did not execute this matrix; fresh native
execution remains a P6 responsibility.

E3.5 P6 corrected one test-support type qualification in the native application wrapper
(`u32` -> `vanguard::u32`) and then completed fresh Debug + Development native execution.
Both noninteractive lifecycle runs pass. The warned Debug real-input path also passes the
hidden/detached/redocked Activity proof, exact-next-frame context-menu path and one-shot
physical cancellation oracle. On the mixed-DPI acceptance machine, P6 additionally hardened
the external driver so each newly detached HWND is placed fully inside the selected monitor's
working area and verified there before later mouse/focus actions; semantic success remains
telemetry-driven rather than timing/screenshot-driven.

E3.6 P5 prepared the persistence/restart oracle in source. The final P6 gate later compiled the
exact-current production/test surface and executed the focused proof required for acceptance.
`run-user-state-persistence-test.ps1` is the canonical noninteractive restart runner: it owns a
scratch project under `-Artifacts`, runs seed -> restore -> selected-extension absent -> extension
return, then corrupt/truncated/future/project-mismatch write-inhibit cases plus an isolated
unwritable-final-save case. It rejects editor frame/application-state failures and preserves the
project ID/Saved tree across the missing-owner cycle. `editorUserStateTests` owns the deterministic
saved-topology/DPI/work-area repair proof; the noninteractive restart runner does not manufacture a
detached native window solely to duplicate that oracle. `-InteractivePersistenceLayoutTest`
remains the warned real-input dock/detach/topology path and was not needed for E3.6 P6 closure.

The E3.4 P5 work only prepared this fixture and external oracle in source. E3.4 P6 later
ran the exact-current acceptance matrix: Debug + Development native lifecycle both exit 0
with `frames=120 peakHosts=2 finalHosts=1 primaryPresent=1 detachedPresent=1 resized=1
closeReopen=1`, and after the required real-input warning the Debug interactive runner exits
0 with retained E3.3 host/input/DPI/minimized-atlas evidence plus the E3.4 surface matrix.
Its final command summary is `dockCycle=1 dpiTransition=1 invokes=10 attempts=11 rejected=1
toolbarOffPrimary=0`. P6 also hardened only test-side telemetry/input synchronization found
during live execution; it added no product hook or alternate command/surface/shortcut path.

Historical E3.3 P5 acceptance is closed. After the unrelated rendering workstream repaired its compile blocker, exact-current rendering and compatibility libraries were rebuilt and the Debug editor linked coherently. The final visible run exited 0 and proved a real command-panel dock -> detach -> redock cycle with native host count 2 -> 3 -> 2, the same command state/identity through the cycle, four F8 query/invoke cycles through the normal shell Local-context path, real text/click input across hosts, detached resize to 700x500, minimize/restore, available 1.5/1.0 DPI transitions, and an ImGui atlas `WantUpdates -> OK` update while all hosts were minimized. A separate exact-current hidden lifecycle run exited 0 with `frames=120 peakHosts=2 finalHosts=1 primaryPresent=1 detachedPresent=1 resized=1 closeReopen=1 lifecycle=PASS`. Those phase labels describe historical acceptance only; the current reusable test implementation is phase-neutral.

The accepted E3.3 P6 gate reran both native paths against exact current using the
dedicated executable under `editor/bin/EditorTests/Debug`. The hidden lifecycle again exited
0 with `frames=120 peakHosts=2 finalHosts=1 primaryPresent=1 detachedPresent=1 resized=1
closeReopen=1 lifecycle=PASS`. After an explicit real-input warning, the interactive runner
also exited 0 with `dockCycle=1`, `hostsPeak=3`, four command query/invoke cycles, cross-host
text/click input, an available 144-DPI -> 96-DPI transition and minimized-atlas
`triggered=1 queuedWhileMinimized=1 acceptedWhileMinimized=1`. These are automated telemetry
oracles; screenshots remain inspection artifacts rather than pixel assertions.

Optional visual/manual follow-up outside the P5 closure gate:

- Drag a docked panel toward another docking target while keeping the pointer inside its host: protruding panel edges must not create an OS window. Move the pointer outside to detach normally. Release over a docking hint to dock; release without docking retains normal floating-window behavior. This editor-only ImGui interaction patch does not claim to fix native Present stalls.
- Drag the docked tab outside, then redock it. Confirm its text and click count survive.
- Alternate focus between hosts; type, paste, click and scroll. Confirm only the intended panel responds.
- Resize and minimize/restore hosts; verify the image, clipping and input coordinates remain correct.
- Move between different-DPI monitors when available; verify text scale, clipping, dragging and hit testing.
- Close a detached panel, then close the editor while rendering.
- Inspect visual smoothness/appearance during these operations when desired; the P5 acceptance oracles are telemetry/native-state based rather than pixel assertions.

The interactive fixture has passing exact-current native automation for the checks above. Historical partial diagnostic observations remain historical and are not relabeled as acceptance.

## Automated lifecycle pass

After generating with Premake and building the Debug editor, run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File editor/tests/run-native-ui-integration-test.ps1 -Executable editor/bin/EditorTests/Debug/VanguardEditorNativeUiIntegrationTest.exe -Artifacts build/e0f/debug
```

The script copies `native-ui-integration-test.vproject` into the artifact directory if absent. An optional `-Project` selects an existing test project. It launches `--validate-editor-ui`, captures only windows owned by that process, prints its log and returns its exit code. A timeout leaves the process available for diagnosis.

Two ordinary registered panels exercise the existing UI service and graph renderer. The test checks presentation on both native hosts, a detached output-size change, detached closure and a 120-frame clean exit. It displays the font atlas, a four-color registered image and clipped child rows. The fixture uses a one-time startup texture upload; production atlas updates continue through the allocator-declared upload graph. The test-only render-tail join permits safe observation of the existing viewport counters; it adds no production synchronization.

Screenshots are evidence to inspect, not pixel assertions. The automated lifecycle fixture uses ordinary `Floating` first-use placement to create the detached host; its automatic close/reopen is not a redocking test. The separate visible interaction mode owns the native drag/focus/DPI/minimize evidence described above.

E0F corrections found by native execution: valid shared sampler-domain capacity, reuse of rendering-owned descriptor domains with explicit slot retirement, editor build/API corrections, and removal of a second display-origin/DPI transform on captured framebuffer-local clip rectangles. No panel-specific rendering path was introduced.

Debug and Shipping native lifecycle passed on D3D12 / RTX 4080 Laptop GPU. Shipping completed 120 frames with both hosts presenting, detached resize/closure and exit code 0. Its run ended before the screenshot delay, so visual inspection evidence remains the Debug captures.

When building `editor.vcxproj` directly, build `build/projects/vs2022/SDL3.vcxproj` with the same Configuration and Platform first. Premake's SDL3 `dependson` ordering is represented at solution level; a direct editor project build does not traverse that dependency. Building the Shipping SDL3 target resolved the missing import library without source changes.
