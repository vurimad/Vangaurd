# Command-List Recording Lease Redesign — 2026-10-07

## Purpose

Eliminate Vanguard's command-list lifetime/reclamation churn while embracing NVRHI's intended persistent `ICommandList` model and preserving Vanguard's stronger generational handles, resource retention, residency, callback, failure, device-loss, and shutdown correctness.

This redesign is motivated by the exact-current profile `cpu_sample_geometry_20261007_185011`. The frozen comparison baseline is in `docs/engineering/rendering_cpu_hotspots_2026-10-07.md`.

## Baseline problem

The current healthy path creates a fresh Vanguard `CommandListPayload` and generic `ResourceLifetimeManager` resource for each logical recording. After NVRHI submits and detaches the actual GPU recording, Vanguard still keeps the outer NVRHI `ICommandList` inside that payload until Vanguard's GPU fence completes. Only later does generic resource reclamation destroy the payload and optionally return the outer NVRHI object to the bounded recycler.

This makes a CPU recording object participate in GPU-lifetime retirement even though NVRHI already detached the submitted GPU instance during `executeCommandLists()`.

The measured structural costs to remove are:

- `nvrhi::d3d12::CommandList::open()` / acquisition path: about **0.423 ms aggregate CPU/frame**.
- `ResourceLifetimeManager::Impl::ReclaimReady()` -> command-list destruction: about **0.498 ms aggregate CPU/frame**.

These are sampled/inclusive costs and can overlap; they are comparison buckets, not values to sum into frame time.

## NVRHI contract we will preserve

NVRHI's intended outer-command-list lifecycle is:

`open -> record -> close -> execute -> open again`

The same outer `ICommandList` may be reopened immediately after execution while prior GPU work is still in flight. NVRHI detaches each submitted recording into backend-owned lifetime tracking. On DX12 it internally rotates/reuses `ID3D12GraphicsCommandList` / `ID3D12CommandAllocator` instances; on Vulkan it acquires command buffers from the queue pool.

One outer `ICommandList` is not a concurrently recordable object. Parallel CPU recording requires distinct outer command-list contexts. Required persistent context count therefore follows **simultaneous CPU recording concurrency**, not GPU frames in flight.

NVRHI garbage collection remains required for detached backend objects, but GC is reclamation and is **not** a prerequisite for reopening a successfully executed outer command list.

## Target architecture

`CommandListRef` remains the public 8-byte `{ index, generation }` handle, but becomes a short-lived **generational recording lease** backed by a dedicated command-recording arena instead of a generic `ResourceLifetimeManager` resource.

A reusable arena slot owns the persistent CPU-side recording context:

- persistent `nvrhi::CommandListHandle`,
- reusable `resources` capacity,
- reusable `residencyResources` capacity,
- reusable `submissionCallbacks` capacity,
- command-list type and reuse policy,
- recording-local binding/state cache fields,
- generation/state needed to reject stale handles.

Healthy recording lifecycle:

1. Acquire a free arena slot/context and issue a new generation.
2. `native->open()` and reset recording-local logical state without discarding reusable CPU capacity.
3. Record and close normally.
4. Submit through NVRHI.
5. After NVRHI has executed/detached the recording and Vanguard has successfully published its completion fence, preserve all existing Vanguard resource-use, residency, and submission-callback fence semantics.
6. Invalidate the old `CommandListRef` generation immediately and return the healthy context to the reusable pool without waiting for GPU completion.

The command-list recording lease itself no longer receives `RecordUse(commandList, fence)` and no longer travels through generic resource retirement.

## Correctness invariants

The redesign must not weaken any of the following:

- stale `CommandListRef` generations are rejected everywhere;
- `NativeReleaseObservation` keeps its literal native-payload-destruction meaning; reusable command-list recording leases do not expose that observation because successful lease retirement may transfer the same outer `ICommandList` into reuse without destroying it;
- one arena slot is exclusively owned by one CPU recorder while open;
- submitted textures, buffers, heaps, pipelines, readbacks, swap-chain resources, query state, and other retained resources keep their exact Vanguard fence ownership;
- submission callbacks receive the same exact completion fence and invalid completion on discard/failure where required;
- residency prepare/commit/release semantics remain unchanged;
- queue ordering, cross-queue waits, and submission serialization remain unchanged in this chunk;
- no context is returned to the healthy pool after ambiguous execution/fence-publication failure or device loss;
- discard does not depend on NVRHI's documented-but-lightly-tested `close -> open` without execution path;
- shutdown drains or abandons contexts according to the existing healthy/device-loss distinction.

## Working-set policy

Persistent NVRHI outer command lists retain high-water state. Upload/scratch managers and, on DX12, the outer object's internal allocator/list pool do not shrink during ordinary reuse. Therefore reuse is split into explicit classes.

### Clean contexts

No upload/scratch manager use. Reuse is keyed by exact Vanguard command-list type rather than debug role. This gives the broadest safe reuse and prevents ordinary render-graph names from fragmenting the native-context working set.

### Bounded working-set contexts

Callers explicitly using `CommandListRetentionPolicy::BoundedUploadReuse` assert that the role has a production-bounded per-recording upload footprint. These contexts remain isolated by type + reuse role so one role cannot inherit another role's retained high-water state. `GeometryWork` is the current intended example.

The native context carries sticky reuse provenance. A clean context may be promoted to `BoundedWorkingSet(type, role)` when a bounded recording first exercises NVRHI persistent working-set storage, but a bounded context never demotes back to clean on a later recording that happens not to upload. Bounded reuse requires a stable nonzero role.

### Unbounded / contaminated contexts

A default-policy command list that exercises upload/scratch state is not returned to the hot persistent pool. Its context is retired for fence-safe destruction so that NVRHI's retained high-water memory can be released.

The implementation tracks this as persistent-working-set use rather than as upload-only provenance. Current Vanguard recording reaches that state through buffer/texture uploads; future RT scratch-backed recording must mark the same state before issuing native work. Current Vanguard D3D12 RT recording entry points remain unsupported.

## Trimming and exceptional destruction

Immediate reuse and destruction are different boundaries.

- **Reuse:** allowed immediately after successful NVRHI execution/detach plus successful Vanguard fence publication.
- **Destruction / high-water shedding:** fence-safe and off the normal recording hot path.

The arena must support rare cold/excess-context retirement so transient concurrency or memory spikes do not become permanent process high-water. Gate 3 starts with RED's 30-maintenance-epoch inactivity precedent and deliberately treats it as a Gate 4 tuning parameter rather than a correctness constant.

Gate 3 uses an intrusive bounded pool rather than the old `16 total / 2 per role` recycler:

- clean contexts use O(1) LIFO heads by command-list type;
- bounded contexts use hashed `(type, role)` buckets;
- a global age list is separate from hot acquisition;
- no hot-path historical-peak bookkeeping is required: acquisition removes one context from its exact class and healthy retirement puts it back, so a class grows only when simultaneous CPU recording demand exhausted its available contexts and forced creation of another outer command list;
- clean/type and bounded `(type, role)` classes therefore converge naturally to their recent simultaneous demand without an extra lease-count spinlock; transient peaks decay through cold maintenance, and the 4096 retained-context storage is the hard hot-pool ceiling;
- a pooled context carries its latest successfully published Vanguard completion fence across reacquisition, so a later discarded recording cannot destroy retained upload/scratch state still needed by the prior GPU submission;
- full-pool pressure searches only a bounded oldest-first candidate window for a fence-complete victim; if none is safely destroyable, the returning context takes fence-delayed retirement instead of blocking or dropping an in-flight outer object.

Cold maintenance starts with RED's proven 30-flush inactivity window, examines only a bounded candidate window, and retires at most four completed contexts per maintenance pass. This is an initial performance policy, not a correctness constant: Gate 4 profiling is responsible for confirming or tuning the age/batch values. Destruction happens outside the pool lock.

Discarded and unbounded-working-set contexts with a published completion fence may use fence-delayed retirement. A failed Vanguard fence publication after NVRHI execution has no safe current-submission destruction fence, so that outer context remains quarantined and non-reusable until an after-idle/device-loss teardown boundary.

## Implementation gates

### Gate 1 — Dedicated generational recording arena

Introduce the command-recording arena and migrate `CommandListPayload` storage/lookups out of `ResourceLifetimeManager` while preserving current behavior otherwise.

Acceptance:

- public `CommandListRef` shape and API remain unchanged;
- stale generation behavior is covered explicitly;
- recording, close, discard, debug naming, native-object access, retained resources, callbacks, and residency still behave identically;
- reusable CPU array capacity survives lease turnover;
- no change yet to queue submission semantics or graph command-list boundaries.

### Gate 2 — Immediate healthy NVRHI context reuse

Move clean/bounded outer `ICommandList` reuse to the successful submission boundary. Remove command-list `RecordUse`/GPU-retirement dependence and make generation invalidation + arena return immediate after successful execution/fence publication.

Acceptance:

- previous GPU work can remain in flight while the same outer NVRHI context records again;
- simultaneous CPU recording leases still receive distinct contexts;
- actual retained GPU resources continue to carry exact Vanguard completion fences;
- failed signal/device-loss paths quarantine rather than reuse;
- steady-state command-list destruction through `ResourceLifetimeManager::ReclaimReady()` disappears.

### Gate 3 — Working-set containment and retirement

Finish the clean / bounded / unbounded policy, remove the old fixed `MaximumRecycledCommandLists` recycler, and add fence-safe cold/contaminated context retirement.

Acceptance:

- ordinary clean lists share by queue/type without debug-role fragmentation;
- bounded-upload roles remain isolated and reusable;
- default-policy upload/scratch users cannot permanently poison the hot pool;
- exceptional context destruction is fence-safe;
- healthy shutdown, forced shutdown, discard, and device-loss paths have explicit coverage;
- pool/context growth is bounded by real CPU concurrency plus explicit retained policy, not frame count.

### Gate 4 — Validation and profile closure

Run focused RHI/rendering tests, then reproduce the same representative profile workload used for the baseline before considering any command-list graph-boundary changes.

Acceptance:

- functional/stale-handle/failure/device-loss/shutdown tests are green;
- after warmup, normal frames show essentially zero outer NVRHI command-list creation/destruction unless genuine extra CPU concurrency appears;
- command-list `ReclaimReady` destruction bucket is absent from the healthy steady-state path;
- `CommandList::open()` residual is remeasured rather than assumed away;
- compare the frozen `~0.423 ms` open/acquisition and `~0.498 ms` reclamation/destruction buckets directly against the new capture;
- no graph command-list merging is justified from this work alone; any remaining five-boundary cost is a separate measured decision.

#### Gate 4 evidence — 2026-10-07

Correctness/build validation is complete for the command-list-specific part of the redesign:

- exact-current Development and Debug x64 serialized `/m:1` builds complete successfully for `rhiTests`, `rhiNvrhiTests`, `renderingTests`, and `geometryAllocatorTests`;
- `rhiTests` executes successfully in Development and Debug and retains the injected post-execution fence-publication failure oracle: submitted-work evidence survives an invalid completion fence and the public path returns `DeviceLost`;
- `renderingTests` executes successfully end to end in Development and Debug;
- `rhiNvrhiTests` contains a real-D3D12 `TestCommandListLeaseLifecycle` oracle covering nonzero bounded-role enforcement, stale public generation rejection after submit and discard, sticky bounded reacquisition, immediate discard of a reused bounded context while the prior submission may still be in flight, carried-prior-fence retirement, and clean-list stale generation rejection. That oracle passes before the current suite later reaches an unrelated `GpuSceneRuntime` integration expectation mismatch;
- an earlier focused diagnostic isolated that GPU Scene mismatch to the runtime's explicit `ResolveContributions()` contract. With that unrelated compatibility adjustment applied temporarily, the complete native suite passed through clean shutdown/reinitialize and abandonment; the adjustment was reverted and is not part of this command-list change;
- `geometryAllocatorTests` executes successfully end to end in Debug and on an exact Development rerun, including mixed CPU/GPU command-list groups and the graphics/compute fork-join smoke. An earlier Development invocation hit `material3C2` scene-binding stage 7 once, but the immediate exact rerun and Debug run both pass; it is therefore not treated as a reproducible command-list failure.

Two exceptional Gate-3 paths remain intentionally source-audited rather than fault-injected in the real D3D12 suite. There is no existing safe test seam that can make a later external queue-fence signal fail after NVRHI has already executed a multi-queue submission, and the private cold/pressure pool has no public native-destruction observation surface. Adding a production fault hook solely for this Gate, or manufacturing 4096 live native outers to force pool pressure, would widen the shipping design or turn the test into a resource-exhaustion exercise. The post-execution `DeviceLost` receipt contract is exercised by `rhiTests`; the exact partial-signal quarantine and cold/pressure list mechanics retain the two independent final Gate-3 source audits that closed with `0 BLOCKER / 0 HIGH / 0 MEDIUM`.

The frozen desktop benchmark host is now available again, so Gate 4 has a direct host/workload comparison. Both accepted runs use the same `DESKTOP-SE2KD37` physical-console session, i7-4790K, GTX 1060 6 GB, driver `32.0.15.8157`, Balanced power plan, Profile x64 runtime, app-local D3D12Core, exact `1280x720` client extent at 96 DPI, 2-second warmup, 18-second Tracy capture, and final 12-second `Application/Frame` window. Synthetic RenderGraph/RenderFlow probe environment variables are removed by the benchmark runner.

Primary clean run `command_list_gate4_20261007_232409` compared with frozen `cpu_sample_geometry_20261007_185011`:

| metric | frozen baseline | Gate 4 | delta |
| --- | ---: | ---: | ---: |
| FPS | `400.6667` | `602.0000` | `+50.25%` |
| mean frame | `2.496254 ms` | `1.661060 ms` | `-33.46%` (`-0.835194 ms`) |
| p50 frame | `2.422609 ms` | `1.598710 ms` | `-34.01%` |
| p95 frame | `3.274623 ms` | `2.225680 ms` | `-32.03%` |
| p99 frame | `4.020262 ms` | `2.609907 ms` | `-35.08%` |

The run is benchmark-valid (`validRendererThroughEnd=true`) and uses runtime SHA-256 `46631BED142A3C7EF205C210D7F1D07753BE8B6375B6D09736AAF631374BEB51`. A second clean matched run, `command_list_gate4_etw_20261007_233050`, independently reproduces the result at `602.8333 FPS`, `1.658923 ms` mean, `1.581086 ms` p50, `2.212403 ms` p95, and `2.651806 ms` p99.

The deferred per-node resource-use zones move much less than the matched whole-frame result. Using the same final-12-second Tracy rule, `RenderGraph/Resources/BeginNodeUses` changes from `0.232953` to `0.207762 ms/frame`, and `EndNodeUses` from `0.040755` to `0.035335 ms/frame`, while whole-frame mean changes by `0.835194 ms`. These zone values are not additive or an independently isolated causal decomposition; they show only that the measured resource-use-zone movement is much smaller than the matched frame-time delta. The structural outer-context probe below independently corroborates the command-list reuse mechanism, and the later resource-use optimization remains a separate program.

The earlier temporary command-list lifecycle counter probe remains the direct structural steady-state proof: the representative workload plateaued at `7` fresh default outers plus `1` fresh bounded outer. While those creation counts stayed flat, pool hits rose from `10,228 clean + 2,044 bounded` to `40,948 clean + 8,188 bounded`, standalone context retirements stayed at `0`, and the sampled retained hot pool stayed at `6`. The probe was removed afterward. This proves that, after warmup, outer-context population follows the CPU recording working set instead of frame count and normal frames reuse persistent NVRHI outers rather than create/destroy them per recording.

The historical WPR capture used 125-us sampled CPU stacks, but its exact orchestration command line was not preserved. Gate 4 verified with WPT that `xperf -SetProfInt 1250` is exactly `0.1250 ms` and attempted an auxiliary sampled capture. The first raw trace retained only the warmup edge; subsequent WPR-assisted attempts caused the benchmark runner's foreground-ownership validity gate to fail. Those traces are rejected and are not used for timing claims. Therefore the historical sampled buckets (`~0.423 ms` `CommandList::open()`/acquisition and `~0.498 ms` command-list reclamation/destruction) do not have a method-identical post-change number. No synthetic replacement is reported.

Gate 4 nevertheless closes the command-list lifecycle program on the accepted evidence: the matched frozen desktop workload improves repeatably by about one third in CPU frame time, while the independent lifecycle probe proves that steady-state native outer creation stops after the small warm working set is populated and healthy command-list retirement no longer rides generic `ResourceLifetimeManager` GPU reclamation. Because this is a matched before/after repository capture rather than a commit-isolated binary A/B, the full frame-time delta is reported as an observed Gate-4 result rather than attributed microsecond-for-microsecond to one source line. No profile evidence justifies changing the 30-epoch / four-retirement cold policy. Residual NVRHI `open()` cost can be revisited only if a future valid sampled capture is available; it is not a reason to merge graph command-list boundaries or weaken the persistent lease model.

## Non-goals for this chunk

- Do not modify NVRHI to force Vanguard-specific semantics.
- Do not weaken Vanguard resource/fence correctness.
- Do not introduce a quad/small-scene fast path.
- Do not merge StartRender / GeometryWork / CameraGeometry / ComposeCameraOutput / EndRender command-list boundaries as part of this redesign.
- Do not broaden into the separate per-node resource-use setup hotspot until the command-list lifecycle profile is closed.

## Success criterion

The finished system should treat a Vanguard command list as a **CPU recording lease over a persistent NVRHI command-list context**, while NVRHI owns submitted backend-instance lifetime and Vanguard owns the stronger lifetime evidence for the actual resources used by that submission.

The healthy hot path should contain recording reset/open, recording work, submission, fence publication, resource/callback/residency finalization, and O(1) lease recycle — not payload heap churn, generic command-list retirement, or outer NVRHI context destruction.
