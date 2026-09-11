# Geometry diagnostics

10.2.6 is source-complete, with builds, shader compilation and executable checks
deferred to the explicitly authorized 10.3 batch. No rendered or performance
proof is claimed by this checkpoint.

## Request and consume

Set `RenderFrameSetup::features.geometryDiagnostics` for frames to sample. The
default is false. Register the optional named compute pipeline
`GeometryDiagnostics` through the existing renderer pipeline catalog, using
`shaders/geometry_diagnostics.vsl`, entry `GeometryDiagnosticsMain`, the existing
bindless descriptor interface and a 32-byte push-constant range. Cooking and
native creation belong to the existing shader/pipeline startup path. A missing
pipeline skips the sample and increases the dropped-sample count; it does not
silently substitute another shader or prevent ordinary rendering.

Call `RenderingService::PollGeometryDiagnostics(report, &failure)` from one host
thread consumer. The service forwards to its existing `FrameRenderer` owner.

- `Empty`: no pending or completed sample to return.
- `Pending`: samples exist but are still recording or awaiting GPU completion.
- `Ready`: the returned report has valid GPU counters (`gpuAvailable`).
- `Failed`: the sample failed or lost completion evidence; GPU counters are not
  valid. An RHI mapping failure can additionally supply `failure` evidence.

Reports identify the frame serial, logical viewport, scene, and persistent view
handles with generations. Array positions are compact family ordinals. Samples
may arrive out of serial order: do not overwrite a newer displayed report with
an older one. Reports contain scalar diagnostic data, with no retained scene,
entity, transform, material, visible-instance or indirect-argument payload.

Stop polling before the joined renderer shutdown. `ClearPipelines` resets the
readback owner before the existing native retirement flush. Native buffer
destruction uses RHI's existing fence-safe resource retirement.

## Report meanings

Per view, CPU metadata supplies actual candidates written to GPU planning,
unresolved GPU identities rejected before that write, planned shell ranges,
recorded pipeline-bind calls, and visibility/work/instance/argument capacities.
Candidate totals accumulate in the existing joined batch validation loop.
Pipeline binds accumulate locally and publish once per phase, including early
failure exits; neighboring camera jobs do not update shared cache lines per
shell. These are bind calls, not a claim about driver state changes or nonempty
GPU draws.

GPU data preserves `GpuVisibilityCounters` and `GpuGeometryCounters` verbatim:
requested visible count, culled/rejected instances, visibility overflow;
requested/emitted/rejected expanded primitive-phase work, expansion overflow;
invalid-bin work, instance overflow, visible bins and rejected indirect bins.
Requested work retains the existing saturating-counter semantics. Visibility
consumers clamp the requested visible count to the view's visible capacity.

A final reduction adds emitted indirect arguments, shell-argument overflow and
nonempty shells. Emitted arguments sum `min(drawCount, argumentCapacity)`;
overflow sums the separately maintained shell overflow counters. A visible bin
is not necessarily a successfully emitted draw. The report deliberately keeps
these separate, and keeps CPU planned shells distinct from GPU nonempty shells.

Preparatory failures before sample reservation remain available through the
existing frame failure status. No new frame-failure registry is introduced.

## Cost and lifetime

Disabled diagnostics add no graph nodes, GPU reduction, readback allocation or
GPU-to-CPU traffic. Requested samples use one unique graph command list after
camera rendering: `ReduceGeometryDiagnostics`, then
`ReadbackGeometryDiagnostics`. The graph declares the existing counter and shell
plan reads, a transient summary UAV, its CopySource transition, and a retained
readback-buffer import in CopyDestination. Material resources are unaffected.
The diagnostics feature bit participates in the graph key (renderer revision 5).
Cached nodes do not cache frame counters, descriptors or readback slots.

Each view gets one 64-thread reduction group. Lanes traverse disjoint admitted
shell ordinals, then reduce through group-shared memory with unconditional
barriers. One lane writes that view's summary. There are no new GPU atomics,
cross-view counters or scans of instances/bins/entity capacity. The CPU scans
the existing dense shell plan once per requested sample to identify each view's
range. Diagnostics cost scales with admitted shells and views.

Each GPU summary is 64 bytes; the 32-view family limit makes one readback at most
2048 bytes. The ring contains three samples: 6144 bytes of logical readback
capacity plus fixed CPU metadata; native allocation granularity can be larger.
Readback buffers are allocated lazily and reused. If all
slots are occupied, the sample is dropped without waiting or overwriting an
unread result. `GetDroppedGeometryDiagnosticSamples` includes full-ring,
allocation-failure and missing-pipeline skips, and resets at joined teardown.
Requesting diagnostics while the ring is full can still record an empty optional
command list, but no reduction, copy or readback allocation occurs.

The ring uses a bounded slot CAS and release/acquire publication once per sample,
not synchronization per scene element. Terminal publication happens after graph
jobs and submissions join. It uses the receipt for the exact diagnostic copy
command scope, not a guessed global fence. Poll never calls a GPU wait: it maps
only after that submission fence completes. Failed submitted samples also wait
for completion before slot reuse. Unknown submission evidence/device loss
quarantines a slot until joined teardown instead of risking overwrite.

RED evidence: `renderGeometryBatcher.cpp` passes `GeometryBatcherStats` through
its recording context and draw/buffer-bind calls (for example lines 304-352),
while `renderRenderFrame.cpp:4193` gathers renderer statistics. Those sources are
under `D:/root/R6.Root/Mainline/dev/src/common/renderer/src/`. Vanguard likewise
keeps recording counts with the existing batcher path and exposes renderer-owned
reports; its GPU summary transport uses Vanguard's graph and RHI contracts.

## Deferred 10.3 validation

Compile/reflection-check the optional compute shader and its 64-byte output ABI.
Exercise zero shells, partial groups, many shells, multiple phases and views;
match each summary against independently known candidate/work/bin/argument data.
Exercise every overflow class, rejected geometry, CPU identity failures, and
pipeline-bind counts. Verify normal disabled frames have no diagnostic graph
work. Exercise missing pipelines, full-ring skips, out-of-order consumption,
toggle/cache reuse, mapping failure, discard, post-submission failure, missing
receipts/device loss, and teardown with samples in flight. Verify imports and
summary buffers retire correctly and measure overhead on target hardware.
