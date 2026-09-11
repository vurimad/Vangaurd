# Packet execution lifecycle correction

## Direction and scope

Follow RED's frame-counter, kickoff, node dependency, continuation, and terminal-cleanup ordering. Do not replace that ordering with per-packet runtime locks or a second scheduler. Vanguard retains separate resource declaration/execution, NVRHI command-list state seeding, retained generation metadata, and truthful GPU submission/fence retirement. CPU recording completion is not GPU completion.

All three phases are complete at the source-only gate. No compilation, tests, generation, or benchmark execution was performed. Runtime correctness and performance remain unverified.

## Source evidence and existing mapping

| Ordering obligation | RED source | Existing Vanguard equivalent |
| --- | --- | --- |
| New frame follows previous CPU rendering chain | `renderCommandHandler.cpp`, `CRenderCommandHandler::RenderScene`: `builder.DispatchWait(m_flushCounter)` and replacement with the extracted tail | `render_command_system.cpp`, frame dispatcher: `builder.AddDependency(cpuTail)` and extracted replacement counter |
| Preparation completes before recording starts | `renderRenderFrame.cpp`: draw-buffer/kickoff counter and resolve completion deferral | `frame_renderer.cpp`, `ExecuteBuiltGraph`: kickoff and setup deferrals; resource resolution and packet preparation precede release |
| Node work includes child continuations | Node builders/RunContext continuation relationship | `render_node_job.cpp`: one task per occurrence, continuation `jobs::Builder(jobContext)`; command-list group child batches and owning epilogue stay ordered |
| Terminal cleanup follows all nodes | `renderRenderFrame.cpp`: wait on node builder then `RunRenderNodeJobs/Cleanup` | `render_node_job.cpp`: terminal builder depends on the unique CPU leaf (or kickoff for an empty graph), then calls `FinishBuiltGraph`; the branch is joined into the frame builder |
| Phase mutation must not overlap consumption | `renderFlowInternalData.cpp`: inclusive `UpdateFlagGuard` for consumption/resource lookup, exclusive guard for `SetPhase` | Explicit allocator coordinator/recording contract; packet/generation lifecycle locks removed in Phase 2 |
| Detect invalid overlap rather than serialize supported work | `redThreadsThread.h/.inl`: `UpdateFlag` asserts on conflict and is disabled without `RED_ASSERTS_ENABLED` | No new runtime synchronization added in Phase 1; consider reuse of this existing diagnostic primitive in Phase 2 only if needed |

Authoritative RED renderer files are under `D:/root/R6.Root/Mainline/dev/src/common/renderer/src`; the imported concurrency implementation is under `source/imported/common/redSystem`. Vanguard's `jobs::Builder` continuation and dependency adapters delegate to imported `job::Builder(RunContext)` and `DispatchWait`, not an independent scheduler.

## Required ownership contract

1. Before recording kickoff, the coordinator publishes the packet/resource tables. One scheduled occurrence owns each packet. A command-list group can hand that ownership through ordered child jobs; it cannot run simultaneous cursor users.
2. A copied packet view retains metadata, not permission to execute the same packet twice. Repeated sequential claims remain rejectable; concurrent claims are unsupported caller misuse.
3. Finish, device-loss cleanup, abandonment, and destruction run only after every possible recording job and continuation has joined. A queued-but-not-started job counts as outstanding work. Cursors must be finalized, canceled by their owner, or destroyed before teardown.
4. The join is provided by the existing frame/job chain. `TerminalJoinToken` records the caller's already established evidence; neither token factory waits for jobs, and an arbitrary ready counter is not evidence for the full generation.
5. Standalone allocator clients have the same obligation without requiring a render graph: finish synchronous use or join their own job branch before teardown. Retained inactive views may outlive a quiescent allocator; later cursor opening fails because the generation is terminal.
6. Native GPU references/fences remain necessary after the CPU join. This contract does not authorize freeing in-flight GPU memory or removing RHI submission locks.

## Phase 1 changes

- Documented the coordinator, packet-owner, and join-token obligations at the public API boundary.
- `AbandonAllocatorSession` validates all packets before invalidation and fatals if a cursor is still executing. It no longer turns an actively executing packet into Failed and proceeds with teardown. The existing locks remain in this phase.
- This scan is a diagnostic, not a substitute for the caller's join: it cannot prove that an unclaimed packet has no queued job. No shared active-job counter was invented to replace the existing scheduler dependency.
- The `RunRenderNodeJobs` setup-failure fallback now checks its kickoff wait before invoking terminal cleanup. Failure to establish that boundary is fatal, not permission to release borrowed state.
- Normal terminal cleanup and private `AbandonPublishedExecution` remain in `FinishBuiltGraph`; no extra cleanup task or public abandonment API was added.

## Path audit

- **Normal execution:** the complete acyclic CPU graph has one terminal leaf; completion depends on its counter, which transitively covers all occurrences and their attached continuations. The frame builder also joins the node branch before its tail completes.
- **Empty graph:** terminal completion depends on kickoff, so resolution/setup cannot race cleanup.
- **Setup failure before node dispatch:** `RunRenderNodeJobs` validates/preallocates before dispatch. Its recoverable false return reaches a checked kickoff wait, then cleanup. Once dispatch begins, dispatch/counter/terminal scheduling failures are fatal rather than returning with detached work.
- **Preparation failure:** failures block occurrences before releasing kickoff. Completion still follows the scheduled branch. Setup deferral prevents cleanup racing the dispatching function.
- **Device loss:** submission outcome is retained; it does not bypass the CPU node join. Terminal cleanup builds the appropriate receipt only after recording completion.
- **Service quiescence and shutdown:** `RenderingService::OnQuiesce` flushes previous CPU processing; `RenderCommandSystem::Shutdown` also flushes before device/allocator destruction. `RenderUpdate` flushes before device-loss owner cleanup. Initialization rollback occurs before accepting frame work.
- **Direct destruction:** the caller must already be quiescent. The allocator now rejects visibly executing cursors instead of pretending it can safely cancel workers. Retained inactive packet-view destruction coverage remains valid; no current test requires destruction racing recording.

## Phase 2 changes

- Removed `ExecutionGenerationRef::Impl::terminalLock` and `PacketExecutionRuntime::lock`, together with all 13 acquisition sites. No replacement shared counter, CAS claim, or blocking guard was introduced. Duplicate concurrent claims remain forbidden by the single-owner contract, not made safe by a plain state check.
- `OpenCursor` validates the packet and bound command list, seeds command-list-local state, and initializes its cursor without allocator lifecycle locking. Seeding failure still invalidates the packet and requires discarding the command list.
- Cursor destruction, move assignment, finalization, and owner-driven cancellation retain their state/liveness transitions without packet locks. Ordered continuation handoffs remain the scheduler's responsibility.
- `Finish` inspects packets and explicit-state predecessors after the full recording join, retaining validation-before-mutation and every submission/fence check. Quiescent abandonment retains the Phase 1 executing-cursor fatal check before invalidation.
- The terminal atomic and generation/liveness reference counts remain unchanged. Retained inactive views still observe terminal state; GPU-resource retention, pool retirement, writer/publication locks, viewport locks, and submission locks are unchanged.
- Source search found no remaining `terminalLock` or `runtime.lock` references in Rendering. Whitespace checks passed. This establishes removal of the known critical section, not a measured speedup or a proof of all concurrent behavior. No new debug overlap guard was added in this slice; existing local validation does not claim to detect illegal concurrent teardown.

## Phase 3 closure audit

The frame orchestration was retraced through the actual imported scheduler, not inferred from the names of the Vanguard wrappers. `redJobs2/src/jobBuilder.cpp` stores the parent continuation counter in `Builder(RunContext)`; `FinalSync_NoGuard` links the builder's final dependency into that parent, and destruction performs this linkage when the counter was not explicitly extracted. `redJobs2/include/jobBuilder.h` defaults ordinary dispatch to `Fence::Full`. Vanguard's continuation constructor and dependency/dispatch adapters use these operations directly.

Consequently, the source-level chain is:

`previous cpuTail -> frame continuation -> declaration fence -> Resolve -> packet preparation/kickoff -> CPU-parent node dependencies (including child continuations and owning epilogues) -> terminal cleanup -> completed frame tail`

Node-job setup remains a separate early branch joined into the frame continuation. Its terminal task is attached through `finishBuilder(setupContext)` and depends on the unique CPU leaf, so completion includes all nodes in the validated complete acyclic graph. The empty graph explicitly depends on kickoff. This placement is not textually identical to RED's cleanup job on the main frame builder, but preserves its required edges without adding a second cleanup task. The next frame cannot overtake the setup branch or terminal cleanup because the frame continuation joins that branch through the imported builder machinery.

Additional checks:

- Command-list group child batches use ordinary fenced dispatch; a builder-using child terminates its batch. Child continuation builders attach their work, including their epilogues, before a later batch or the owning group's epilogue can finish. They do not create concurrent cursor owners.
- Declaration uses task-local `nodeContext`, whereas early job setup initializes each occurrence's separate execution context. Packet preparation fills bindings before kickoff; recording reads them only after kickoff. No new dependency is needed between these independent setup operations.
- Recoverable setup failures occur before the node dispatch loop and take the checked kickoff wait. After dispatch starts, dispatch/counter/terminal scheduling failures are fatal. No recoverable path returns while abandoning detached recording work.
- Normal cleanup, device-loss completion, service quiescence, command shutdown and render-update device-loss cleanup retain their CPU joins. Standalone clients must provide their own equivalent join; neither the terminal atomic nor the packet-state check detects arbitrary concurrent misuse.
- Existing tests cover wrong queue/order, incomplete finalization, terminal generation mismatch, stale packet replay and inactive views surviving allocator destruction. Added unrun checks for sequential duplicate claims and active cursor move construction/assignment. These are not concurrency proofs; no test deliberately races operations forbidden by the ownership contract.
- Cursor ownership documentation now explicitly covers moves, cancellation and destruction. All 13 removed lifecycle lock sites remain absent. Writer/publication, pool, viewport and RHI synchronization remain outside this change.

No additional broken production ordering edge was identified in this bounded static review. No new scheduler, runtime counter, lock or cleanup job was added. The closure is source-only: compilation, the allocator/rendering/job integration suite, concurrent stress of independent packets and ordered child continuations, and performance measurement remain deferred until authorized.
