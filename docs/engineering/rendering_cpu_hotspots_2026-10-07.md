# Rendering CPU Hotspot Baseline — 2026-10-07

Source profile: `cpu_sample_geometry_20261007_185011` (exact-current runtime at capture time).

This note freezes the three structural CPU costs currently under investigation so later changes can be compared against the same baseline. These are sampled/inclusive costs and may overlap; do not sum them into a frame-time total.

The command-list lifecycle redesign selected to address buckets 1 and 2 is documented in `docs/engineering/command_list_recording_lease_redesign_2026-10-07.md`.

## 1. Command-list open / acquisition path

- Observed cost: **~0.423 ms aggregate CPU/frame**.
- Profiled function: `nvrhi::d3d12::CommandList::open()` (`external/nvrhi/upstream/src/d3d12/d3d12-commandlist.cpp:237`).
- Vanguard entry path: `RenderNodeImpl::Process()` -> `RenderNodeCommandListGroup::CreateCommandList()` -> `rhi::CreateCommandList()` -> `CommonBackend::CreateCommandList()` -> `AcquireNativeCommandList()` -> `native->open()`.
- Investigation target is **Vanguard command-list acquisition/reuse/lifetime policy**, not NVRHI implementation code.

## 2. Old command-list reclamation / destruction

- Observed cost: **~0.498 ms aggregate CPU/frame**.
- Profile path: `ResourceLifetimeManager::Impl::ReclaimReady()` -> Vanguard command-list lifetime callback -> `nvrhi::RefCounter::Release` -> `nvrhi::d3d12::CommandList` destruction.
- This includes real destruction of surplus NVRHI command-list objects and therefore their owned D3D12 command-list/allocator instances.
- Investigation target is **Vanguard command-list retirement/recycling policy**.

## 3. Per-node resource-use setup

- `RenderNodeImplContext::BeginNewNode()`: **~0.253 ms aggregate CPU/frame**.
- `ExecutionPacketCursor::BeginBufferUse()`: **~0.157 ms aggregate CPU/frame**, largely a child of `BeginNewNode()` and therefore **not additive** with the 0.253 ms figure.
- Independent Tracy view: `RenderGraph/Resources/BeginNodeUses` **~0.237 ms/frame**, `EndNodeUses` **~0.041 ms/frame**.
- Investigation target is **resource-use setup/execution mechanics and transient context/storage churn**, while preserving current RenderFlow semantics.

## Comparison rule

After each optimization pass, re-profile the same workload and compare these three buckets individually. Treat WPR inclusive/thread-time and Tracy zone totals as attribution evidence, not values to blindly add together.

## Performance target and guardrails

The representative quad workload has an explicit CPU target of **2000 FPS**, i.e. **<= 0.500 ms total CPU frame time** on the frozen benchmark host/workload. This is a direction-setting performance target, not permission to optimize for an empty scene.

Every optimization toward that target must preserve the renderer's full correctness and production scaling properties:

- do not weaken synchronization, lifetime, resource-state, residency, validation, generation, failure, device-loss, or shutdown semantics to satisfy the benchmark;
- do not add quad-specific, empty-scene, low-resource-count, or benchmark-only fast paths;
- do not choose an algorithm because it is cheap when almost no work exists if it becomes materially worse as real scene/node/resource/draw counts grow;
- prefer structural reductions in work, better reuse, denser/precomputed execution data, and better asymptotic behavior that remain valid under representative production load;
- evaluate changes on both the frozen small workload and their expected/scaled cost model for populated scenes. A small-scene win that merely moves cost into worse large-scene scaling is a regression, not progress toward the target.

The target is therefore **<= 0.500 ms/frame without cutting correctness corners and without sacrificing real-work scalability**.
