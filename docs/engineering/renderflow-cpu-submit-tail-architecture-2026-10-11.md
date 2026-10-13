# RenderFlow CPU submission-to-terminal tail: preserved architecture study

**Date:** 2026-10-11  
**Status:** read-only study completed; first implementation gate authorized 2026-10-11.  
**Scope:** CPU-side work between `RenderGraph/CloseAndSubmitCommandLists` and `RenderGraph/NodesComplete`, **excluding all presentation work**. Never change DXGI Present, swap-chain acquisition, display synchronization, presentation scheduling or generation dependencies as part of this program.

## Evidence and accounting

The 320-resource scheduler probe contains 320 logical resources (240 textures/80 buffers), 640 resource-use occurrences, approximately 32 command-list groups and 90 node calls including 70 synthetic empty CPU nodes. It stresses structural overhead; it is not a representative scene sample.

The first timestamp correlation found a p50 serial submit-to-terminal interval of approximately **831 us**. The later 320 diagnostic (`rf_cpu_submit_tail_320_cleanup_probe_20261011_215938`, 18-second trace, final 12-second steady window, n=4647) showed approximately **859 us median**, **889 us mean**. The latter run had p50 frame **2.481232 ms**, p95 **3.343057 ms**, p99 **3.869261 ms**, **387.25 FPS**. Differences in capture/cutoff and binary must not be suppressed.

Mean decomposition of the 889-us interval:

| Component | Mean CPU time | Other observed steady p50 |
|---|---:|---:|
| Retained-frame final cleanup | 212.4 us | ~197 us |
| NVRHI native command submission | 188.5 us | ~180 us |
| Residency preparation | 52.1 us | ~51 us |
| Submitted-resource cleanup | 44.5 us | ~43–44 us |
| Allocator Finish | 28.1 us | ~27 us |
| Receipt bookkeeping | 18.6 us | Not isolated |
| Other CPU work | 33.2 us | Not isolated |
| **Total non-Present measured CPU operations** | **~577 us** | Non-additive p50s |
| Between-job gap (not CPU operation attribution) | 56.5 us | ~54–57 us |
| DXGI native Present, explicitly excluded | 255.2 us | ~239 us |

The ~577 us represents a **measured cost budget, not recoverable savings**; some native D3D12/driver time is unavoidable. The serial interval crosses render worker jobs and need not be time on a single core. Never add independently sampled medians as an exact interval. No explicit wait for completed GPU rendering/display occurs in Present; the user ruled out revisiting presentation or investigating general main-thread waiting.

Identical initial Profile binary across normal/64/320 measured native NVRHI submit p50 ~69/~102/~176 us, residency p50 ~16 us at 64 and ~51 us at 320, final retained cleanup p50 ~41/~82/~204 us (latest separately isolated destructor p50 ~197 us). The 64-to-320 native difference ~78 us is an **upper bound for all resource-scaled causes combined**, not a tracker gain estimate. Initial binary hash prefix `DDAF24DD1B1E`; later cleanup diagnostic runtime SHA-256 `3725680732B3CF3992D073F85D286430CAA71F045F38A943629DEAD00E1E7397`. Do not mix binary cohorts in an A/B claim.

Existing captures:

- `build/profiling/desktop_runs/rf_cpu_submit_tail_320_profile_20261011_215329`
- `build/profiling/desktop_runs/rf_cpu_submit_tail_64_profile_20261011_215514`
- `build/profiling/desktop_runs/rf_cpu_submit_tail_normal_profile_20261011_215721`
- `build/profiling/desktop_runs/rf_cpu_submit_tail_320_cleanup_probe_20261011_215938`

Profiling zone `RFCPU/Frame/FinalRetainedCleanup` isolates last `RetainedRenderFrameRef::Reset` ownership teardown. Other relevant zones: `RFCPU/RHI/NativeExecuteLists`, `RFCPU/RHI/PrepareResidency`, `RFCPU/RHI/FinalizePayloads`, `RFCPU/Finish/AllocatorFinish`, `RFCPU/Submit/PublishReceipts`. Profiler-only edits from the preceding capture phase exist in the shared dirty checkout: preserve them and unrelated ongoing work.

## Gate A — immutable warm contract and per-frame mutable overlay (first priority)

**Evidence:** `RenderNodeGraph::InstantiateResourceContract` (`source/rendering/src/render_node_graph.cpp:774-853`) copies every cached use into a fresh `RenderNodeResourceUseBinding`, including `target.name.Set(containers::StringView(use.name))`, plus decisions, ranges, lookup buckets and lookup groups. Per-frame `RenderNodeResourceBindings` is prepared at `:657-689` and torn down in `render_node_impl_context.cpp:27-73`. Last frame destruction originates at `render_command_system.cpp:663-673`. The scheduler probe makes 640 copies of 26-byte names; RED `containers::String` has 20 inline bytes (32-byte object less 8-byte pointer/metadata and 4-byte length), so these copies entail heap-backed names. Precise fraction of ~197-us destructor due to Strings was **not** instrumented.

**Target:** borrow/pin immutable cached `ResourceContractNodeTemplate` data for stable use names, hashes, kinds, flow spaces, lookup topology, duplicate chains and static ranges, while keeping only dynamic state per frame: generated `{flowGroup,ordinal,sessionGeneration}` handles, lookup `runtimeNextUse`, resolved texture/buffer witnesses, mutable patch fields, execution context/cursor, failures, and packet references. Avoid per-use owned `String` creation on the warm path. Keep exact byte comparison on lookup hash collisions.

**Critical safety:** immutable borrowing requires explicit proof that graph cache eviction/contract publication cannot destroy `ResourceContractStorage` until **all** warm borrowers have detached. `CacheEntry::Reset` (`render_graph_cache.cpp:133-189`) synchronizes graph reset before contract destruction; `RenderNodeGraph::Reset` (`render_node_graph.cpp:1148-1160`) is exclusive. Warm setup in `frame_renderer.cpp:1727-1759` and eventual exclusive-update unlock at `:1409` or `:1448` are not automatically sufficient to prove a retained frame's destructor runs first. Treat these as lifetime seams, **not** authority to change presentation.

Cold `RTUseBegin` (`render_node_impl_context.cpp:737-770`) receives potentially transient names from workers and must still **own** their bytes. Warm failure in Instantiate/Patch/Bind must detach views, reset overlays and allow cold rebuild without dangling pointers. Preserve all generation checks, exact name equality, node ordering, packet liveness and independent workers. Retained scratch must be bounded with transient overflow and capacity decay, not a new semantic limit.

**Implementation sequence (one gated change at a time):**

1. Add representation/accessors that differentiate owning cold uses from borrowed cached warm uses; only immutable fields are borrowed. Preserve public lookup semantics.
2. Add frame-local mutable overlay and per-frame handle generation, eliminating warm names and static table copies.
3. Prove borrowed contract lifetime across workers, failure fallback, graph exclusivity and last frame release; detach before any invalidating publication or unlock.
4. Add bounded scratch reuse only after core representation is correct and independently validated. Optional cold-name arena later only if measured.

**Proof:** cold/warm equivalence and hash collisions; dynamic cold names; stable vs changed graph, contract cache eviction/replacement, concurrent node work, failure-to-cold fallback, stale/wrong use ID and generation, retained-frame teardown after worker exit, cancellation, device loss and memory cap/overflow. Instrument a 320-resource warm run proving 640 uses with **zero per-use heap-backed name copies**; same-binary normal/64/320 before/after CPU timings and frame p50/p95, no shifted cost at another stage. Do not promise savings before testing.

## Gate B — generation-stamped NVRHI tracker (separate second priority)

`external/nvrhi/upstream/src/common/state-tracking.h:87-137` owns per-recording `std::unordered_map<ResourceExtension*, std::unique_ptr<State>>` for texture and buffer state. `state-tracking.cpp:405-452` allocates states, `:336-360` walks keepInitialStates at Close, `:363-403` publishes permanent state and first-use initialization then clears maps on native submission. `d3d12-commandlist.cpp:237-272` opens, `:310-330` closes, `:332-374` executes; `d3d12-device.cpp:617-653` wraps D3D12 ExecuteCommandLists, private queue Signal, per-list executed and device removal checks. The measured ~180-us p50 includes native driver calls as well as tracker work; no exact inner split exists.

**Target:** retained, lazy and bounded generation-stamped flat texture/buffer tables, active indices and subresource vector retention. `open()` begins a fresh epoch (including extension-pointer ABA protection). `close()` must finish keepInitialStates and commitBarriers, then `SealRecording` may reclaim record-only scratch outside serial submission. `executed()` / `CommitSubmitted()` publishes only sparse deferred permanent states and first-use initialization witnesses; do not semantically publish at Close or before native Execute. No pointers to state objects escape tracker-private calls, but any table growth must not invalidate a borrowed pointer mid-call or during iteration.

Preserve pre-native failures (no publication), post-native Vanguard fence-signal failure (native submitted and quarantined), fork/join queue ordering, permanent states, per-subresource barriers and device loss. Retained capacity must be bounded because up to 4096 outer command contexts may exist; oversize uses transient fallback. Test pointer ABA, epochs/wrap, cold discard, resource init, partial failures, and queue/fence semantics.

## Gate C — submit-wide physical residency/lifetime batching (third priority)

`source/rhi/nvrhi/src/common_backend.cpp:3939-3955` prepares residency per list. Admission `:5022-5126` already sorts/uniques each list's canonical physical allocation references, but there is no submit-wide dedup. `d3d12_backend.cpp:614-641` additionally linear-searches native pageables in a batch; `:816-870` resolves and commits working sets. Finalize `common_backend.cpp:4012-4045` commits per-queue residency fences and per-resource `RecordUse/Release`; `resource_lifetime.cpp:863-947` includes generation-aware CAS and per-queue atomic fence updates.

Build submit-wide unique *physical allocations*, not logical resource handles, while keeping per-allocation per-queue fence evidence and per-command-list callbacks and lease lifetimes unchanged. Deduplicate only residency preparation, not semantic submission/retirement. Measured target has ~51-us residency and ~43–44-us submitted cleanup p50, not guaranteed removable totals. Do not bundle with Gate B.

## Explicit exclusions

- DXGI Present, swap-chain or presentation-related generation handoff, output acquisition, early-next-frame overlap, GPU/display waits or main-thread scheduling studies.
- Merging NVRHI private and Vanguard receipt fences as first patch; queue/fence semantics too broad.
- Repeating RF18 inline 16 native-list-pointer optimization (already shipped).
- Relying on one small diagnostic call (e.g., GetDeviceRemovedReason) for the architectural target.
- Changing correctness contracts, bounded capacities into hard application limits, or erasing dirty shared-checkout changes.

## Implementation and validation status

At time of initial preservation, **only prior profiling instrumentation** was present; A/B/C architectural changes were not yet built or benchmarked. User authorized starting implementation after reviewing this study. Use serialized VS2022 x64 MSBuild `/m:1`, keep shared dirty checkout and remote benchmark runner intact.

### 2026-10-11 implementation checkpoint: Gate A core representation

The first immutable warm-contract cut has since been implemented in:

- `source/rendering/include/vanguard/rendering/render_node_impl_context.hpp`
- `source/rendering/src/render_node_impl_context.cpp`
- `source/rendering/src/render_node_graph.cpp`
- `source/rendering/src/frame_renderer.cpp`

Warm `RenderNodeResources` now borrows the immutable cached node's use names, use metadata, decisions, lookup buckets/groups and static ranges through typed read accessors. The frame owns only lookup `runtimeNextUse`, compiled range executions, resolved resource witnesses and the synthesized current-generation use/decision IDs; no warm use-name `String::Set` copies remain. Cold declarations/capture still own strings. Graph instantiation validates the complete node-identity set before binding any borrowed views; warm-to-cold fallback detaches before compilation. `FinishBuiltGraph` detaches after joined node jobs and before either graph unlock, and early dispatch failure detaches before its unlock. Independent lifetime review found no surviving blockers. This is a **core representation cut**, not yet an additional persistent scratch-pool program.

Regression: `source/rendering/tests/geometry_allocator_tests.cpp` RF-EX1 direct binding test now exercises 64 duplicate uses with a >20-byte name and another distinct long name, alongside exact hash collisions, dynamic decision patches, stale generation witnesses, and warm/cold equivalence.

Builds/tests:

- Profile x64 `runtime.vcxproj`, `geometryAllocatorTests.vcxproj`, `renderingTests.vcxproj` — **PASS**, serialized MSBuild `/m:1`.
- Profile `geometryAllocatorTests.exe` — **PASS**, including RF-EX1 and real-D3D12 contract tests.
- Profile `renderingTests.exe` — **PASS**, including graph cache and resource allocator tests.
- Development x64 first attempt — blocked by another process locking the shared `nvToolsExt64_1.dll` during a dependency project's post-build copy; no RenderFlow compile defect was established by that aborted build. Do not kill another process or reset shared output to suppress this.

Desktop benchmark, exact same **new** Profile runtime SHA-256 `59BD7A813C33A5CC1DA421EE53873E2336412971A69B69D9A7D2B7ADF2CB649C`, 1280x720, same host/settings and final 12-second steady cutoff:

| Metric | Previous 320 cleanup-probe binary | New 320 warm-view binary |
|---|---:|---:|
| Frame p50 | 2.481232 ms | **2.188598 ms** |
| Frame p95 | 3.343057 ms | **2.991670 ms** |
| Frame p99 | 3.869261 ms | **3.392157 ms** |
| Reported FPS | 387.25 | **440.50** |
| Retained-frame cleanup p50 | 197.027 us | **97.281 us** |
| Retained-frame cleanup mean | 212.362 us | **103.654 us** |
| Native NVRHI submit p50 | 180.296 us | **163.418 us** |
| Residency preparation p50 | 51.422 us | 46.891 us |
| Submit-to-terminal paired interval p50 | 858.978 us | **718.454 us** |
| Submit-to-terminal paired interval mean | 889.078 us | **747.938 us** |

The cleanup p50 reduction is **99.746 us / 50.6%**; full interval p50 reduction is **140.524 us / 16.4%**. Median frame time improved **292.634 us / 11.8%**; reported FPS rose 13.7%. These are **before/after separate binaries** under matched capture settings, not a causal isolation experiment for each secondary stage; driver timing, runtime variance and other shared-checkout work may contribute. Presentation was measured for decomposition only and **was neither edited nor optimized**.

At 64 resources (same new runtime hash), previous same-settings trace frame p50 1.574815 ms vs new **1.439650 ms**; paired serial interval p50 531.871 us vs **488.796 us**. The new 64 cleanup p50 is 47.994 us, while the earlier 64 trace did not have the separately isolated cleanup-destructor zone. Do not compare nonexistent individual old-zone data as if measured. The 320 reduction is larger, consistent with resource-use-count scaling.

New run folders:

- `build/profiling/desktop_runs/rf_cpu_warm_immutable_320_profile_20261011_234208` — `ok=true`, `validRendererThroughEnd=true`, n=5286
- `build/profiling/desktop_runs/rf_cpu_warm_immutable_64_profile_20261011_234404` — `ok=true`, `validRendererThroughEnd=true`, n=8030
- `build/profiling/desktop_runs/rf_cpu_warm_immutable_normal_profile_20261011_234621` — `ok=true`, `validRendererThroughEnd=true`, n=10653, same new runtime hash.

Normal-scene steady-window Tracy A/B, using the original `rf_cpu_submit_tail_normal_profile_20261011_215721` capture and the new normal run (same capture settings, **different binaries**):

| Metric | Original normal Profile | New normal warm-view Profile |
|---|---:|---:|
| Application/Frame p50, Tracy timestamp cutoff | 1.126439 ms | **1.079115 ms** |
| Frame p95 | 1.634614 ms | **1.518718 ms** |
| Frame p99 | 1.920539 ms | **1.768049 ms** |
| Paired submission-to-terminal interval p50 | 375.353 us | **361.928 us** |
| Native NVRHI submission p50 | 69.046 us | 66.354 us |
| `RenderGraph/NodesComplete` outside `FinishBuiltGraph` p50 | 41.241 us | **23.015 us** |
| Directly instrumented final retained cleanup p50 | Not instrumented in original normal capture | 22.106 us |

Normal-frame p50 improved **47.324 us (~4.2%)** on precisely the same Tracy steady-window calculation (the older report's 1.147983-ms normal baseline was a different initial frame sample). Normal paired interval improved **13.425 us (~3.6%)**. The terminal-job residual decreased by ~18.2 us p50; old normal does **not** contain the separately added last-reference zone, so that is not a direct destructor-zone A/B. The normal-scene result has smaller absolute benefit than the 320-resource structural probe; do not generalize synthetic 320 FPS gains to all games.

**Core Gate A representation implementation and measurement completed.** Optional additional bounded scratch-pool reuse and stronger fault-injection eviction races are separate potential follow-ups, not implemented or measured here. B (NVRHI tracker) and C (residency batching) remain unimplemented. No presentation behavior was changed.

Remaining optional Gate A follow-ups: bounded scratch reuse only if independently worth its memory and complexity cost, and a dedicated cache-eviction race/late-retained-frame/fail-to-cold regression oracle beyond the existing graph-cache locking proof. The normal-scene cross-check was completed above. Gates B/C remain unimplemented separate architectures.

### 2026-10-12 next-cut preflight — measured opportunity, not yet implementation

User asked whether the **718.454 us p50** 320-resource submission-to-terminal interval can be reduced further. **Yes**, but the current metric includes **229.565 us p50 native DXGI Present**, which is explicitly **out of optimization scope**. Do not present the entire 718.454 us as removable CPU or estimate the non-Present median by subtracting two unrelated medians. The 12-second steady capture above is the canonical current baseline (Profile runtime SHA-256 `59BD7A813C33A5CC1DA421EE53873E2336412971A69B69D9A7D2B7ADF2CB649C`).

Current 320-resource stage p50: native NVRHI `RFCPU/RHI/NativeExecuteLists` **163.418 us**; `RFCPU/RHI/PrepareResidency` **46.891 us**; `RFCPU/RHI/FinalizePayloads` **43.460 us**; `RFCPU/Frame/FinalRetainedCleanup` **97.281 us**; allocator finish **25.389 us**. Cross-workload 64-resource native **95.195 us**, residency **15.131 us**, retained cleanup **47.994 us**. Thus the 320-vs-64 native delta **68.223 us** is only an upper bound for all resource-scaled native causes, and residency delta **31.760 us** is only an upper bound for all scaled residency causes. D3D12 driver time and fixed synchronization cannot be inferred from these deltas.

Exact-current source review supports two non-presentation structural cuts:

1. **Submit-wide canonical physical-allocation residency union (bounded, transactional):** `source/rhi/nvrhi/src/common_backend.cpp:3940-3954` repeatedly prepares a working set for each submitted list, although `AdmitCommandListResources:5064-5077,5093-5099` already maps placed resources to their owning heap and sorts/uniques physical residency references *within* each list. `d3d12_backend.cpp:614-640,816-852` repeats physical lookup, native pageable conversion, an O(batch²) pageable duplicate scan, residency-lock checks and potential MakeResident calls for every list. Union the already canonical `ResourceRef` allocations across the **whole submitted batch before any QueueWait or native Execute** and make resident once per unique physical allocation (bounded chunks/fallback). Preserve each list's callback/resource-retention identity, and preserve separately per-queue last-use fence evidence after submission. Never dedup by logical name, handle ordinal, or unqualified native pointer; heap alias identity is essential.
2. **Retained NVRHI recording state and sparse submit commit (needs instrumentation first):** `external/nvrhi/upstream/src/common/state-tracking.h:124-134` currently uses per-command-list unordered maps of individually allocated `unique_ptr` texture/buffer states; `state-tracking.cpp:363-403` does per-list permanent-state publication, keepInitialState initialization and destruction of all tracked entries in `commandListSubmitted()`. `d3d12-commandlist.cpp:310-330,332-374` closes, then commits these states at `executed()`; `d3d12-device.cpp:617-653` also calls D3D12 `ExecuteCommandLists`, queue `Signal`, and device removal query in this timing zone. Instrument separate native driver, queue signal, per-list `executed`, tracker commit/clear and lifetime enqueue costs **before promising a gain**. An epoch-stamped bounded/retained state table and compact per-list active witness list can eliminate per-resource allocation/deallocation in steady recording and work at serial submit while preserving pointer stability and init/permanent-state semantics. A cheap move of `clear()` from submit to close is **not** an architectural CPU reduction and must not count as one.

Submission-wide residency is a lower-risk first correctness gate; the tracker redesign targets a larger opaque timing category but contains driver time that may dominate. The sequencing of implementation must follow measured per-list counts/inner breakdown, not just stage sizes. Both cuts are **unimplemented** as of this preflight, and must be separately measured and tested.

**NVRHI tracker independent design cross-check:** Exact-current review found no architectural blocker to bounded epoch-stamped retained texture/buffer state tables with a sparse submission witness set. `d3d12-commandlist.cpp:237-272` begins recording; `:310-330` must still complete `keepInitialStates` and `commitBarriers` *before* sealing recording scratch; `:332-374` is the only permissible publication point after actual native execute. `state-tracking.cpp:363-403` currently performs deferred permanent-state commits and sets `stateInitialized` for first-touched, keep-initial textures while clearing all tracking maps. An optimized `commitSubmitted` must explicitly retain a sparse list of pending permanent-state and first-use-init witnesses; **cannot** simply omit the latter. Inactive stale pointer-hash table entries must be epoch-tested *before* any dereference to defeat stale objects and pointer-address ABA. Real native `ExecuteCommandLists` and `Queue::Signal` in `d3d12-device.cpp:617-653` are driver-side floor candidates.

**First falsifiable performance gate:** instrument subzones for driver `ExecuteCommandLists`, private queue `Signal`, per-list `executed`, `commandListSubmitted` map destruction, and lifetime-tracker push, while separately measuring canonical residency references per list vs per-submission unique allocations. Keep exactly one baseline executable and new-instrumentation cohort for normal/64/320; only after seeing nontrivial removable cost implement the appropriate single architecture. Success must reduce **total CPU/frame as well as paired interval**, not merely relocate `clear()` to Close. Target no regression in normal-scene p50/p95, same-generation and cross-generation reuse, D3D12 and Vulkan initialization behavior, native pre-submit and post-submit errors, cross-queue fork/join and resource/device loss.

**Residency independent design cross-check:** Source-backed, no blocker. Because each of at most 128 submitted lists already has a sorted-unique physical `residencyResources` array, an allocation-free 128-cursor k-way streaming merge can produce <=1024-reference unique prepare chunks without an aggregate allocation; maintain per-physical allocation queue-use mask. A submit-only D3D12 resolver can avoid redundant `GetResidencyAllocation` and O(batch²) pageable pointer dedup for this proven canonical input, leaving general-purpose `MakeResident/Evict` unchanged. After valid queue fences exist, stream the same merge per queue to commit residency last-use exactly once per physical allocation/queue. **Do not** dedup `payload.resources` lifetime by physical heap: placed aliases are separate logical resources and their refs still need correct last-use fences. A submission-private `RecordOwnedUse` may safely skip the generic `RecordUse` AddRef/Release pin *only* while a validated owned payload reference remains held through the update, with an exact generation check; generic callers retain the original safe path. Per-list callbacks, framebuffer release, lease retirement/quarantine and all `Release` operations remain intact.

Residency failure or queue wait failure before native execution still returns with no submitted work; making an earlier chunk resident is safe. After native execution, external fence failure must commit **only** queues having valid signaled fences, preserve receipt `workSubmitted`, callback valid/invalid fence evidence and quarantine unsignaled leases. Fork/join command order is unchanged. Tests must exercise duplicate dedicated allocations across lists, placed aliases sharing a heap, cross-queue shared heap (both fences), >1024 unique allocations, second-chunk failure, queue wait failure and post-submit signal failure. Current 320 p50 residency+finalization zones total ~90.4 us when naively added, but their separate medians are **not** a firm recoverable-cost ceiling; duplicate ratio is still unmeasured. The 64 and normal scene residency p50s (15.1us and 5.4us) warn against extrapolating synthetic 320 savings. **This second design remains unimplemented.**

### 2026-10-12 Gate C implementation, correction and measured status

This section supersedes the earlier preflight's **unimplemented** status for **Gate C only**. Gate B (NVRHI tracker redesign) remains unimplemented. No DXGI Present behavior was changed. No commits, resets or cleanup of the shared dirty checkout were performed.

Implemented in `source/rhi/nvrhi/src/common_backend.cpp`, `source/rhi/nvrhi/src/d3d12_backend.cpp`, `source/rhi/nvrhi/private/vanguard/rhi/backend/resource_lifetime.hpp` and `source/rhi/nvrhi/src/resource_lifetime.cpp`:

- A bounded, allocation-free min-heap merge over up to 128 lists' sorted canonical `residencyResources`, coalescing shared physical heap/self references into <=1024 batches. Legacy out-of-order lists are normalized only when their recording flag says they need it; admitted RenderFlow sets remain sorted. One physical allocation can carry graphics/compute/copy queue-use bits.
- One pre-native residency preparation per unique physical allocation. Submit-only D3D12 `PrepareWorkingSet` directly resolves canonical refs 1:1 rather than re-canonicalizing/deduplicating native pageables, fixing a possible resource/index mismatch; generic explicit MakeResident/Evict remains unchanged.
- Separate per-queue residency fence commit for unique physical references, preserving fork/join and partial-signal evidence. Per-list callbacks, logical alias lifetime references, resource releases, framebuffer release and command-list retirement remain separate and ordered.
- `ResourceLifetimeManager::RecordOwnedUse` uses the continuous reference held by the closed payload and validates live generation/count before last-use publication, eliminating an extra AddRef/Release CAS pair. Generic `RecordUse` retains its own pin.
- Initial implementation re-ran the min-heap union after external fence signaling. Profile proved that this **regressed finalization**. Corrected to retain the <=1024-allocation prepared union/masks in bounded stack storage for finalization, falling back to deterministic re-merge only on larger unions. This is a substantive, measured correction rather than a merely cosmetic move.

Added test coverage in `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp`: owned-use refcount stability, queue-specific monotonic fences, retirement requiring *every* queue, invalid arguments/stale generation after slot reuse; native 1/16/17/32-list batches all sharing one dedicated physical allocation produce exactly **one** automatic residency working-set check; graphics+compute sharing one allocation also prepares once and publishes both receipt queue fences.

**Build/test:** serialized Profile x64 builds `rhiNvrhiTests`, `geometryAllocatorTests`, `renderingTests`, `runtime` all PASS. Full real-D3D12 Profile `rhiNvrhiTests.exe`, `geometryAllocatorTests.exe`, `renderingTests.exe` PASS. After cached-union correction, `rhiNvrhiTests.exe` rebuilt/reran PASS including cross-queue fixture. Two independent workers: implementation source review and separate read-only correctness audit; auditor found **0 BLOCKER** but noted targeted >1024 unique-union, injected second-chunk/queue-wait/partial-signal failure and true placed-alias cross-queue retirement stress oracles still desirable. Current tests/structural proof must not be misrepresented as those fault injections having run.

Same desktop capture conditions: Profile x64, 1280x720, 18s capture, 2s warmup, last 12s, user foreground verified, NVIDIA GTX 1060 6GB / i7-4790K. Traces under `build/profiling/desktop_runs/`. Original post-A runtime SHA-256 `59BD7A813C33A5CC1DA421EE53873E2336412971A69B69D9A7D2B7ADF2CB649C`; first Gate C runtime `95E2CD3C45E6D73A18E470DCF19DF4FAD280E92A380CBCDD012FAC0A8683A316`; cached-union final runtime `C115E9FAEE0C4844C911AA7E03E41819A3079862EF31F9EE4F2167D95C0B1FED`. D3D12Core SHA `37FA14281A58CC834076971873006FEB8A8D25CDDC908D1A345BDA1B149FFC7D` across cohorts.

| 320-resource p50 | Post-Gate A | Gate C initial union | Gate C cached union |
|---|---:|---:|---:|
| Application frame | 2.188598 ms | 2.342947 ms | 2.244389 ms |
| Paired submit-to-terminal | 718.454 us | 771.798 us | **727.322 us** |
| Native NVRHI ExecuteLists | 163.418 us | 183.061 us | 176.508 us |
| RHI PrepareResidency | 46.891 us | 47.208 us | 47.760 us |
| RHI FinalizePayloads | 43.460 us | 56.055 us | **24.751 us** |
| Final retained cleanup | 97.281 us | 104.430 us | 103.270 us |

The cached union cuts **FinalizePayloads p50 by 18.709 us (43.0%)** against post-A and by 31.304us vs the initial regression. However the 320 **paired serial p50 is 8.868us slower than post-A**, and overall frame p50 is 55.791us slower, with concurrently elevated native NVRHI and Present timings. Do **not** claim the 320 end-to-end target was met or that every other stage difference is caused by the change.

| Normal-scene p50 | Post-Gate A | Gate C initial union | Gate C cached union |
|---|---:|---:|---:|
| Application frame | 1.079115 ms | 1.076680 ms | **1.068158 ms** |
| Paired submit-to-terminal | 361.928 us | 351.573 us | **349.108 us** |
| RHI FinalizePayloads | not restated here | 7.336 us | **4.852 us** |

Thus on normal scene, current Gate C improves paired p50 by **12.820us / 3.5%** and frame p50 by **10.957us / ~1.0%** versus post-A. New normal p95 1.515047ms vs post-A 1.518718ms. The initial 64-resource Gate C capture (before cached-union correction) showed paired p50 **488.578us vs post-A 488.796us** and frame p50 1.462187ms vs post-A 1.439650ms. A separate **cached-union 64** capture was *not* run; do not infer its result.

Raw folders: `rf_cpu_submit_residency_320_profile_20261012_002718`, `rf_cpu_submit_residency_64_profile_20261012_002959`, `rf_cpu_submit_residency_normal_profile_20261012_003146`, `rf_cpu_submit_residency_cached_320_profile_20261012_003503`, `rf_cpu_submit_residency_cached_normal_profile_20261012_003937`. All reported `ok=true`, `validRendererThroughEnd=true`, full native Tracy traces. These are matched-setting different-binary single captures, not confidence intervals or matched same-binary toggles.

**Verdict:** Gate C is implemented, correctness-tested and preserved, with a significant real finalization cost reduction and a positive normal-scene CPU tail, but **not** a clean 320 synthetic end-to-end win. The next architectural gate should first instrument NVRHI native D3D12 `ExecuteCommandLists` versus `executed()` tracker map destruction/keep-initial publication to assess whether Gate B can reduce the remaining ~160-180us native zone; avoid speculative backend driver assumptions and continue excluding Present.

### 2026-10-12 Gate B measured pre-implementation attribution

The user explicitly authorized Gate B: "okay study and start". Instrumented the existing NVRHI common state-tracker submit and D3D12 driver/per-list submission, leaving presentation unchanged. All three captures use **one Profile runtime** SHA-256 `319129CE9AF62D656997774B7DDD80C097ED9B4BDE1893B3B0C34BCC29CB0C17`, unchanged D3D12Core SHA-256 `37FA14281A58CC834076971873006FEB8A8D25CDDC908D1A345BDA1B149FFC7D`; matched desktop 1280x720, 18s capture/2s warmup/final 12s analysis, `ok=true`, valid renderer through end. This build also contains a concurrent RF18 16-element inline native command-list pointer-array cut, so compare subsequent Gate B results against **these freshly instrumented captures**, not just historic Gate C binaries.

| Instrumented Profile p50 | 320 resources | 64 resources | Normal scene |
|---|---:|---:|---:|
| Application frame | 2213.046 us | 1420.322 us | 1059.569 us |
| Paired submit-to-terminal | 707.690 us | 480.698 us | 357.467 us |
| RHI NativeExecuteLists | 171.870 us | 99.100 us | 68.304 us |
| D3D12 ExecuteCommandLists driver call | 92.232 us | 58.515 us | 50.989 us |
| Per-submit NVRHI ExecutedLists | 64.939 us | 30.465 us | 9.484 us |
| Private queue signal | 6.937 us | 5.548 us | 5.050 us |
| Tracker map destruction **per executed list** | 3.099 us | 0.026 us | 0.177 us |
| Tracker map destruction **per-list mean** | 3.275 us | 1.131 us | 1.012 us |
| TrackerSubmit **per-list mean** | 3.857 us | 1.403 us | 1.185 us |

The 320 and 64 probes each execute **16 lists/frame**; normal averages **6 lists/frame**. Thus the 320 tracker map destruction mean alone costs approximately **52.4 us/frame** (16 * 3.275us), while 64 costs ~18.1us/frame and normal ~6.1us/frame. These products of means are workload attribution, **not additive median estimates or guaranteed savings**. The driver ExecuteLists is independently ~92us p50 at 320 and is not removable by a CPU tracker rewrite. `commandListSubmitted()` also scans first-use texture initialization witnesses and destroys resource-state maps. A retained epoch-stamped table with sparse publication is justified, but must be verified against total CPU and the paired interval after implementation.

Capture directories: `rf_gateb_tracker_attribution_320_20261012_005454`, `rf_gateb_tracker_attribution_64_20261012_005803`, `rf_gateb_tracker_attribution_normal_20261012_005955`, under `build/profiling/desktop_runs`. Profiler source zones: `RFCPU/NVRHI/DriverExecuteCommandLists`, `PrivateQueueSignal`, `ExecutedLists`, `ExecutedList`, `ExecutedTracker`, `TrackerSubmit`, `PermanentPublication`, `InitialStatePublication`, `TrackerMapDestruction`, `LifetimePush`, `DeviceRemovedCheck`. Retain names for the optimized same-instrumentation comparison.

Independent lifecycle review also identified a correctness hazard predating Gate B: upstream NVRHI ProgrammingGuide explicitly permits `close()->open()` without execute, but ordinary tracker maps/deferred raw-pointer permanent transitions were only cleared in `commandListSubmitted()`. Reopening an abandoned recording can carry stale state and even dereference released resources when the next recording submits. Gate B must introduce an explicit `beginRecording()` on both D3D12 and Vulkan open; abandon speculative state **without publishing it**, and keep close-to-native-execute deferred publication unchanged. Vulkan's existing post-device-loss behavior is a separate caveat, not a reason to publish on close.

### 2026-10-12 Gate B implementation, regressions and measured closure

**Implemented:** `external/nvrhi/upstream/src/common/state-tracking.h/.cpp` now has per-kind lazy fixed 512-slot retained pointer-hash tables with embedded texture/buffer recording states, epoch-qualified keys, sparse active slot indices and transient map overflow when 512 current-epoch entries are exceeded. Slot capacity is never resized after allocation, so returned `State*` values stay stable; inactive old pointer keys are rejected by epoch **before equality or dereference**. The prior recording's state is fully reset on first touch, including UAV flags and subresource tracking; texture subresource vectors are retained only up to 256 entries, with sparse cleanup for oversized allocations. Deferred permanent texture/buffer state vectors preserve original ordered conflict semantics; only first-touch uninitialized keepInitial textures enter a sparse publication witness. `commandListSubmitted()` commits these witnesses after native execute and retires the epoch without deleting all normal tracked states. Excess resource counts use safe map overflow with no semantic cap.

`external/nvrhi/upstream/src/d3d12/d3d12-commandlist.cpp` and `external/nvrhi/upstream/src/vulkan/vulkan-commandlist.cpp` invoke `beginRecording()` at command-list open, safely abandoning an unsubmitted earlier recording. This also fixes the independent preexisting close/open raw-pointer lifetime hazard; neither close nor pre-native submission failure publishes state. `external/nvrhi/upstream/src/d3d12/d3d12-device.cpp` has only non-presentation tracing additions on top of concurrent RF18 inline command-list pointer array work; the native driver call, private queue signal, executed ordering and device-removed query are semantically unchanged. No DXGI Present, swap chain or presentation scheduling edits.

`source/rhi/nvrhi/tests/d3d12_backend_tests.cpp` adds direct common tracker tests for abandoned deferred permanent state, abandoned first-use texture initialization, successful submitted publication, same-address resource pointer ABA, fresh per-recording state, and 4096 unique buffer states exercising overflow. `source/rhi/nvrhi/premake5.lua` adds the private NVRHI include path needed for these tests. Current `premake5.exe vs2022` regeneration is independently blocked by 18 **unrelated** preexisting engine-source audit violations across other concurrently dirty source; existing generated VS project therefore required build-local `$env:CL = '/I"D:\ENGINE\external\nvrhi\upstream\include"'` for the one `rhiNvrhiTests` build. Do not alter those unrelated violations or claim regeneration passed.

**Validation:** VS2022 Profile x64 serialized outer `/m:1` builds **PASS** for `rhiNvrhiTests`, `rhiTests`, `geometryAllocatorTests`, `renderingTests`, `runtime`. Full executables **PASS**: `rhiNvrhiTests` on NVIDIA GeForce RTX 4080 Laptop GPU (including new common tracker test), `rhiTests`, real-D3D12 `geometryAllocatorTests` (RF-EX1, state/barrier parity, dynamic import, placed ownership, queue fork/join, retained import/export), `renderingTests`. `git diff --check` clean except LF/CRLF warnings. Vulkan backend hook was source reviewed, but **no separate Vulkan GPU execution test** was run.

Matched desktop optimized Profile executable SHA-256 **`14F4BB3F95AE4FBE21DB65335A289F4C84A60EB9A371247FE0BB992F5891A01E`**, instrumented preimplementation SHA **`319129CE9AF62D656997774B7DDD80C097ED9B4BDE1893B3B0C34BCC29CB0C17`**; same D3D12Core SHA across all six runs. Same desktop user/GPU/driver, 1280x720, 18s capture, 2s warmup, final 12s window; all report `ok=true`, foreground verified, valid renderer through end. **Different binaries** but same inner profiler-zone instrumentation.

| p50 (us except frame ms) | 320 baseline | 320 retained | 64 baseline | 64 retained | Normal baseline | Normal retained |
|---|---:|---:|---:|---:|---:|---:|
| Frame (ms) | 2.213046 | **2.082712** | 1.420322 | 1.437700 | 1.059569 | **1.043442** |
| Paired submit-to-terminal (us) | 707.690 | **632.499** | 480.698 | **468.441** | 357.467 | **339.550** |
| NVRHI NativeExecuteLists | 171.870 | **112.472** | 99.100 | **89.551** | 68.304 | **62.561** |
| Native driver ExecuteCommandLists | 92.232 | 90.561 | 58.515 | 68.396 | 50.989 | 50.947 |
| NVRHI ExecutedLists | 64.939 | **11.829** | 30.465 | **10.849** | 9.484 | **3.887** |
| Tracker map cleanup per list p50 | 3.099 | **0.041** | 0.026 | 0.038 | 0.177 | **0.043** |
| Tracker map cleanup per list mean | 3.275 | **0.057** | 1.131 | **0.053** | 1.012 | **0.075** |

320 paired improves **75.191us / 10.6%**, NVRHI native p50 **59.398us / 34.6%**, frame p50 **130.334us / 5.9%**. The driver call remains ~91-92us p50, isolating the gain to NVRHI tracker/bookkeeping rather than driver or Present. Normal paired improves **17.917us / 5.0%**, frame p50 **16.127us / 1.5%**; 64 paired improves **12.257us / 2.5%**, but its frame p50 **worsens 17.378us (~1.2%)** coincident with a higher native driver call median (58.515 -> 68.396us). Do not claim a universal full-frame win; other stage/driver variance is not isolated by a same-binary feature toggle. 320 achieved <718.454us post-Gate-A and <727.322us final Gate C historic serial medians, but those historical cross-binary comparisons are not controlled attribution.

Optimized runs: `rf_gateb_tracker_retained_320_20261012_010722`, `rf_gateb_tracker_retained_64_20261012_010943`, `rf_gateb_tracker_retained_normal_20261012_011132`. Baseline runs are listed above. Each has a native `.tracy` capture, runner result and CSV exports under `build/profiling/desktop_runs/`.

**Closure:** Gate B source implementation, focused regression, serialized real-D3D12 Profile tests and matched-instrumentation desktop measurements are complete. Follow-up robustness coverage still desirable: real native close/reopen with abandoned resource lifetime, Vulkan GPU path including vkQueueSubmit terminal device loss, injected pre-native failure/retry and per-queue partial signal failure, and same-binary feature-toggle interleaving to distinguish run variability. None is claimed to have been dynamically executed. Preserve Gate A warm immutable contracts and Gate C canonical residency union; no presentation modifications.
