# Render lock alignment correction

## Scope and validation

Follow the imported graph and local reference implementation's ownership and scheduling model. Keep Vanguard's separate declaration/execution entry points and native resource lifetime/state requirements. No compilation, tests, generation or benchmarks are authorized for these slices. Source inspection and whitespace checks are not runtime verification.

## Four phases

1. Parallel declaration ownership: preallocated group-owned storage, no shared publication bottleneck, group-owned queue requests.
2. Immutable placement access: remove global placement validation locking from recording after establishing publication and retention safety.
3. Parallel command-list closing: parallel close, join, ordered submission; preserve native state/residency and fence outcomes.
4. Remaining cleanup and orchestration audit: diagnostic graph guard, redundant receipt/pool locks only where ownership proves them unnecessary, terminal/failure/shutdown review.

## Phase 1: implemented, source-only

Reference: `renderFlowInternalData.cpp::HandlePreConsumeRequest` uses preallocated per-group storage with one declaration owner per group. Shared indexing uses atomics rather than a mutex around complete request processing.

- `PrepareResourcesParallel` prepares exact, dense group storage and creates writers before dispatch. Each worker owns one indexed writer; creation failures occur before dispatch. Explicit abandonment completes failed writers before their worker returns.
- Standalone allocator clients lazily prepare the configured bounded group storage. `ResourcePlanningWriter::Impl` retains its stable group slot address; group storage cannot grow during declaration.
- Writer creation indexes the prepared slot directly by flow group. It no longer takes the shared writer lock or scans previously created writers. Duplicate node ownership is checked once by the joined coordinator during sealing.
- `Close` and `Abandon` publish only to the owner's slot. Their shared lock, linear reservation search and shared batch append are removed. Shared open/failed counters are removed; the joined coordinator inspects slots instead.
- Queue begin/end/synchronization declarations write only to their pre-established group slot. CPU synchronization groups therefore use the same single-owner rule without requiring a resource writer, shared append or shared lock.
- `SealPlanning` runs only after the declaration join, validates all completion slots and aggregate limits without the old writer lock, and then moves tapes to the resolver's existing input array. A rejected seal does not consume any tape.
- Resolution consumes queue requests directly in dense group order. The former flat queue-request staging array and its sorting/index arrays are removed.
- Rejection accounting uses a pending atomic only when a writer has rejected operations; successful close adds no shared atomic operation. Joined resolution/cancellation folds the pending count into cumulative statistics.
- Direct allocator clients retain registration validation. Cancellation/teardown must not race declaration owners; a readiness token does not itself wait for them.

The remaining `writerLock` sites protect coordinator-side import/export registration, published-export transfer and statistics snapshots. None is entered by parallel node declaration, writer close/abandon or queue declaration. No runtime claims are made until validation is authorized.

## Phase 2: implemented, source-only

- `BindMemory` is now explicitly the one-time placement publication boundary: the caller exclusively owns an unbound resource until binding returns, and successful placement metadata is immutable for that resource's lifetime.
- The backend placement generation is allocated atomically. Independent bind operations no longer require one backend-wide placement spinlock merely to produce unique placement identities.
- `GetPlacement` reads the immutable published record directly.
- Alias activation validates the destination and complete predecessor coverage from immutable placement records without taking a backend-wide lock. Parallel command-list recording therefore does not serialize at alias barriers.
- The execution generation and placed pool already establish the required lifetime: resolve binds before publication, the pool retains assigned resources during recording, terminal processing follows the complete CPU join, and retirement retains native ownership through the reported GPU fences.
- Pool locks remain around acquire, rollback, retirement, eviction, device loss and statistics because those operations mutate the persistent cache. They are not entered by packet recording.

No runtime claims are made until validation is authorized. Command-list pool and actual queue submission/presentation synchronization are not blanket-removal targets.

## Phase 3: implemented, source-only

- The RHI command-list lifecycle is split into close and submit operations. Closing commits the command list's private barrier tracker and closes its native list without entering the backend submission lock.
- Each synchronization node dispatches one indexed parallel close batch over its authored command-list interval. Every iteration owns one frame command-list entry and publishes only a compact failure code to that entry.
- The parallel epilogue is the sole submission coordinator. It validates the compiled queue boundary, submits the already-closed lists under the backend submission lock, performs queue waits in the requested fork/join order, signals fences, publishes receipts and advances the ordered flush cursor.
- Native resource retention, residency preparation, submission callbacks and command-list consumption remain in the serialized submit transaction. They are not moved into recording workers.
- A close failure prevents the entire interval from reaching native submission. Terminal cleanup can discard a mixture of open and already-closed lists, so partial parallel-close failure does not leak list or resource ownership.
- The existing close-and-submit RHI entry point remains as a sequential compatibility operation for standalone callers. Render-graph execution uses the split lifecycle to obtain parallel close behavior.

No runtime claims are made until validation is authorized.

## Phase 4: implemented, source-only

- `RenderFrameCommandLists` no longer has a receipt/submission spinlock. Synchronization nodes form one authored continuation chain, each parallel close batch joins into its submission epilogue, and `FinishBuiltGraph` reads receipts only after the terminal node and all of its continuations complete.
- The graph exclusive-update flag remains. Graph-cache entries can outlive a frame and may be selected for rebuilding, so execution takes the same one-acquire/one-release lifetime guard used by the imported graph. Nodes do not acquire it individually.
- Retained-frame and preparation failure locks remain cold-path latches. Successful node execution no longer polls them; they protect first-failure publication from independently scheduled setup, resolve and terminal work.
- Allocator registration/publication locking remains outside parallel declaration and recording. It protects public import/export/statistics access, while each declaration worker owns its preallocated group slot.
- Dedicated and placed pool locks, plus the byte-ledger lock, remain coordinator-side persistent-cache locks. Resolve, rollback, retirement, trimming and device loss mutate cache ownership; packet recording does not enter these locks.
- The RHI command-list pool lock remains because native list reuse is shared. The corresponding backend also uses a shared command-list pool lock; Vanguard holds it only for bounded pool lookup/removal or return, never while creating a new native list or closing/recording one.
- The backend submission lock remains the queue serialization point. Native close work now happens before it in parallel; the lock covers only residency preparation, queue waits/execution, fence signaling and ownership finalization that must be globally ordered with other RHI submissions and presentation.

### Post-correction hot-path comparison

| Operation | Vanguard shared synchronization | Imported implementation shape | Result |
| --- | --- | --- | --- |
| Parallel resource declaration | None on success; one atomic only when publishing rejected-operation statistics | Preallocated group-owned request storage | Aligned |
| Packet opening and resource actions | No graph-wide, generation-wide, packet or placement lock | Per-node command-list work after frame kickoff | Aligned; Vanguard retains explicit packet validation for the NVRHI seam |
| Command-list acquisition | One bounded shared pool lock | One shared command-list-pool lock | Aligned |
| Native command-list close | None across independent lists | Parallel close jobs | Aligned |
| Queue submission and waits | One backend submission lock in the ordered epilogue | One ordered submit job | Equivalent serialization boundary |
| Graph definition lifetime | One exclusive flag per graph execution | One exclusive flag per graph execution | Aligned |
| Terminal receipts and cleanup | No rendering-layer receipt lock after terminal join | Ordered cleanup job | Aligned; receipts are a Vanguard allocator seam |

Raw lock counts are not directly comparable because Vanguard's NVRHI backend owns persistent resource lifetime, residency, descriptor and placed-resource caches that live outside the imported render-graph layer. The relevant result is that none of those locks is entered merely to declare a node, open a packet, execute ordinary node commands or close an independent command list. Remaining locks sit at cache mutation, failure publication or inherently serialized queue boundaries.

No runtime or contention claims are made until validation and profiling are authorized. All four correction phases are source-complete.
