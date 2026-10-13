# RF18 — Native Command-List Submission Scratch

Status (2026-10-11): **implemented; source correctness audit 0 BLOCKER / 0 HIGH / 0 MEDIUM, functional gates green in Debug and Development; exact-current post-RF18 desktop Profile baseline captured. Isolated RF18 patch-only A/B remains unmeasured.**

## Re-evaluation

The command-list recording-lease architecture was already completed and validated by the October 7 command-list Gate 1–4 program (`command_list_recording_lease_redesign_2026-10-07.md`). The outer NVRHI context recycler already preserves generations, exclusive CPU recording, fence-aware prior completions, resource and callback retirement, bounded upload roles, and device-loss quarantine. RF18 must not add a second recycler or merge RenderGraph command-list boundaries merely to claim another optimization.

Exact-current D3D12 submission instead exposed a smaller unconditional allocation: `nvrhi::d3d12::Device::executeCommandLists()` allocated `std::vector<ID3D12CommandList*>` for every queue submission solely to translate NVRHI command-list interfaces into native D3D12 pointers. Vanguard's CommonBackend already stages its own NVRHI pointer tables on bounded stack storage; it must still use NVRHI's execution function for the backend's internal queue fence, detached recording lifetime, and `CommandList::executed()` bookkeeping.

## Change and invariants

- `external/nvrhi/upstream/src/d3d12/d3d12-device.cpp`: use 16 inline native pointer entries for ordinary submissions; use dynamically sized vector storage for larger submissions. Preserve exact input order and calls to `ExecuteCommandLists`, `Queue::Signal`, and `CommandList::executed`. The inline size is not a submission cap; direct NVRHI callers can exceed it, and Vanguard's public per-submit cap remains 128.
- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp`: exercise real D3D12 submissions of 1, 16, 17, and 32 command lists, fence completion, and stale public recording-lease rejection. Existing native submission, cross-queue, RF17 compiled barrier, retained resource, and shutdown proofs remain in the focused suites.
- `source/rendering/src/render_node_graph_factory.cpp`: add Profile-only Tracy zones for the renderer's command-list create/open and debug-name calls. No new production runtime state, pool, or API.

The vendored-upstream patch is deliberately small and must be carried through any later NVRHI upstream update.

## Validation

- `rhiNvrhiTests` x64 Development and Debug: PASS on real D3D12.
- `rhiTests` x64 Development: PASS.
- `renderingTests` x64 Development and Debug: PASS.
- `geometryAllocatorTests` x64 Development and Debug: PASS, including mixed command-list groups, RF17 barriers, queue fork/join, presentation, and retained imports.
- `rendering` and `nvrhiD3D12` x64 Profile static-library builds: PASS, including the new command-list profiling zones and NVRHI submission path.
- Independent exact-current source audit of the RF18 patch and tests: 0 BLOCKER / 0 HIGH / 0 MEDIUM.
- Targeted `git diff --check`: no whitespace errors (existing LF/CRLF conversion warnings).

The repository-wide `premake5.exe vs2022` pre-generation engine-source audit currently rejects 17 violations in the shared dirty checkout, unrelated to this RF18 patch. Existing generated VS2022 projects were used for the focused builds without altering those files.

## Profile baseline — October 11

The frozen desktop benchmark became reachable. A freshly built VS2022 x64 Profile runtime (`Vanguard.exe` SHA-256 `487675DF161BBE11400C755CD49812178C33EF66D61963CBA2E7E5B40936EBE7`) was deployed and verified by the remote runner, which also verified local `D3D12Core.dll`, foreground ownership, the exact 1280x720 client pixel extent, GPU adapter/driver, and renderer activity through the end of each capture. Remote machine: Intel Core i7-4790K, GTX 1060 6GB, Windows 10 IoT Enterprise LTSC, Balanced power mode. Each capture used 2 seconds of warmup, 18 seconds of Tracy collection, and the final 12-second frame window.

| Exact-current capture (`build/profiling/desktop_runs/`) | FPS | Frame p50 | Frame p95 | Mean frame | Frames in final window |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rf18_exactcurrent_normal_20261011_064901` | 805.42 | 1.1806 ms | 1.6933 ms | 1.2414 ms | 9665 |
| `rf18_exactcurrent_normal_repeat_20261011_065356` | 815.75 | 1.1702 ms | 1.6446 ms | 1.2258 ms | 9789 |
| `rf18_exactcurrent_resources64_20261011_065229` | 227.58 | 4.3137 ms | 5.1371 ms | 4.3952 ms | 2731 |
| `rf18_exactcurrent_resources320_20261011_065102` | 179.00 | 5.4901 ms | 6.5490 ms | 5.5874 ms | 2148 |

The `resources64` and `resources320` captures enable the extra ten scheduler-probe command-list phases and 64/320 resource stress; their frame rates are **not** directly comparable with the ordinary camera-quad run. All four result JSON files reported `ok=true`, `validRendererThroughEnd=true`, and the same runtime hash. Full `.tracy`, CSV and runner/stdout/stderr files are retained next to each JSON.

Tracy per-occurrence CPU-zone mean durations (nested categories are **not** additive):

| Zone | Normal mean | Scheduler + 64 resources | Scheduler + 320 resources |
| --- | ---: | ---: | ---: |
| `RenderFlow/Resolve` | 132 us | 304 us | 834 us |
| `RenderFlow/Resolve/FinalizeStates` | 69 us | 162 us | 464 us |
| `RenderFlow/Resolve/CompilePacketBarriers` (nested in finalization) | 17.6 us | 42.3 us | 132 us |
| `RenderGraph/ScheduleNodes` | 25.5 us | 169 us | 175 us |
| `RenderGraph/CommandList/CreateOpen` | 17.8–18.6 us/list | 23.2 us/list | 28.3 us/list |

The normal graph records five native command-list groups per graph execution; the synthetic scheduler probe makes that fifteen. Five `CreateOpen` occurrences at ~18 us each give approximately 90 us of summed CPU-zone time per normal frame, **not** a promise that merging lists would save 90 us of wall time. Fusing boundaries may destroy concurrent CPU recording or queue ordering. In the 320-resource stress trace, barrier compilation and state finalization are now prominent contributors to Resolve; direct execution costs must also be measured before treating this as a net RF17 regression.

For context, the earlier `rf13_final_normal_quad_20261010_121315` frozen desktop capture reported 842 FPS, p50 1.128 ms, and mean `RenderFlow/Resolve` 95 us; the earlier `rf320_exec_seed_split_20261010_141909` reported 185.6 FPS, p50 5.283 ms, and mean Resolve 638 us. These were different binaries/optimization stages, so the differences are **not isolated RF18 before/after effects**, even though host/window/probe methodology is comparable. The RF18 inline-pointer patch's speedup remains unquantified as a separate experiment.

GPU execution times are not supplied by the Tracy CPU-zone export (`-g` returned no GPU zones for this capture). Do not claim a GPU bottleneck or precise CPU/GPU overlap from these CPU traces alone. The previous 0.423 ms command-list open and 0.498 ms retirement buckets predate the October 7 recording-lease overhaul; do not reuse them as current costs. NVRHI internal/external fence coupling and any RF19 command-list fusion still require their own correctness/performance case.
