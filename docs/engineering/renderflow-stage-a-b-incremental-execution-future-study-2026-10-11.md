# RenderFlow: Deferred Stage A/B Incremental Execution Optimization Study

**Status:** DEFERRED — research/backlog only. **Do not implement, build, benchmark, or change source as a consequence of this document without fresh user authorization.**

**Study date:** 2026-10-11. **Target:** Vanguard RenderFlow `ResolveFrame`, especially certified warm 320-resource graphs. **Workspace:** `/vanguard` (`D:\ENGINE`). The checkout is shared and dirty; preserve unrelated work. This is a planning record, not an implementation result.

**Related existing record:** `docs/engineering/renderflow-warm-resolve-symbolic-execution-program-2026-10-11.md`, especially §§20–24. That record documents the currently implemented *whole-program* symbolic state cache, profiling, correctness tests, and current measured residuals. The present document is about a *possible future* immutable-execution ownership split and selective invalidation, not a claim those are implemented.

## 1. Problem and decision

The current implementation retains a certified structural plan, dedicated physical program, and a `PersistentStateProgram` snapshot. Stable warm hits bypass `LowerPhysicalStates`, `BuildPacketResourceBindings`, and `CompilePacketBarriers`, but each fresh execution generation still receives copied/patched steps, actions, native binding tables, and compiled barriers. A valid decision-patch change presently causes the state-program check to miss and compile the whole state program, even if the actual barrier topology would remain unchanged. Other changed contracts/topologies may force colder paths.

The user's concern is **frame-time spikes from unnecessary whole-program work after small invalidations** (potentially ~0.5 ms extra) and, equally, avoiding optimization circles: a dependency tracking system must not add large recurring cost to prevent rare spikes. The objective is **not** to make every scenario incremental at any cost; simple false-invalidation removal, multi-variant reuse, and full fallback remain valid outcomes.

**Current decision:** Save the study for the future, **do not proceed with Stage A or B now.** No source edits, runtime work, or acceptance claims are authorized by this record.

## 2. Measured starting point — retain executable provenance

The final matched-binary 320-resource `rf_bulk_structural_template_320_20261011_105819` Profile capture reported **391.282 µs mean Resolve**, **2.467281 ms full-frame p50**, approximately **141.093 µs `BindCachedStateProgram`**, **181.935 µs physical assignment**, **46.171 µs structural instantiation**, and **54.215 µs setup**. Executable SHA-256 was `69423F5AA27D6C6E89F020A8171FCABE34C3C888FBF9562B5BDF0689E412F2A7`. These phase numbers can overlap and should not be added as independent costs.

The original burn-free 320-resource uncached baseline was approximately **780 µs mean Resolve**, **2.841659 ms frame p50**. Same final binary also reported 64-resource **155.038 µs Resolve / 1.572185 ms frame p50** and normal workload **77.230 µs / 1.147983 ms**. The aspirational stable-warm target **≤300 µs mean Resolve remains unmet**; an incremental compiler alone does not eliminate its warm-frame copy/bind costs.

**New implementation must be judged using its own same-host, same-executable captures**, hash, 1280×720 runtime configuration, warmup, final-window frame count, cache-hit/miss counters, Resolve zones, recording CPU, GPU time, and full-frame percentiles. No new capture was run for this study.

## 3. Exact-current architecture and source seams

| Seam | Current behavior relevant to a future change |
| --- | --- |
| `source/rendering/private/vanguard/rendering/render_flow_resource_internal.hpp:927–991` | `CompiledPacket` owns both immutable-looking step/action/tape/barrier arrays **and** generation-owned native bindings, descriptors, waits, runtime/liveness. Splitting it is a lifetime/API change. |
| `.../render_flow_resource_internal.hpp:1288–1378` | `PersistentStatePacket` / `PersistentStateProgram` captured inside `PersistentDedicatedPhysicalPlan`. The cache snapshot stores symbolic indices, not current native handles. |
| `.../render_flow_resource_internal.hpp:1381–1411` | Up to four persistent dedicated physical-program variants, with eviction/replacement. A shared program cannot rely solely on that cache's lifetime. |
| `.../render_flow_resource_internal.hpp:2654–2678` | `ExecutionGenerationRef::Impl` owns published packets and per-generation physical/import metadata. |
| `source/rendering/src/render_flow_resource_resolve.cpp:842–894` | Dynamic retained-import/export/decision patch validation for warm structural binding. |
| `.../render_flow_resource_resolve.cpp:1520–2174` | `LowerPhysicalStates` performs whole-frame resource/UAV history, explicit state handoffs, predecessor tracking, terminal repairs, action tape, and cross-packet actions. |
| `.../render_flow_resource_resolve.cpp:1675–1725` | A later cross-queue consumer can **append exit actions to an earlier producer packet**. Dependencies are not purely forward. |
| `.../render_flow_resource_resolve.cpp:1832–1929` | UAV ordering uses access history; whole-texture normalization/restoration can touch cells outside the triggering subresource. Equal final states do not imply equivalent barriers. |
| `.../render_flow_resource_resolve.cpp:2116–2171` | Explicit terminal repairs target the last relevant packet; export terminal ownership must remain correct. |
| `.../render_flow_resource_resolve.cpp:2176–2333` | Current packet-native resource tables must be sorted and unique; slot numbers depend on current `ResourceRef` ordering. |
| `.../render_flow_resource_resolve.cpp:2335–2809` | RF17 `CompilePacketBarriers` produces packet-local native transition/UAV program **after** correct entry-state lowering, plus semantic barrier batches. |
| `.../render_flow_resource_resolve.cpp:2849–3144` | Capture, validate, restore snapshot; warm restore bulk copies data and patches session/execution generations, physical IDs and native sorted binding slots. |
| `.../render_flow_resource_resolve.cpp:2954–3013` | Current validation rejects differing decision vectors even when the structure is unchanged — investigate whether this is an unnecessary state-program invalidation. |
| `.../render_flow_resource_resolve.cpp:5655–5826` | Existing all-or-nothing state-program hit/miss, full `LowerPhysicalStates` / RF17 compile fallback, then snapshot replacement. |
| `source/rendering/src/render_flow_resource_execution.cpp:158–369,469–585,1017–1082` | Execution emits compiled barriers, admits **current** sorted native resources, seeds entry states, processes action tape and waits; it cannot skip per-frame GPU barriers or native-resource validity checks. |
| `source/rendering/src/render_flow_resource_allocator.cpp:2734–2808` | `Finish`/abandonment makes generations terminal and releases runtime/native owners while retained packet views may still hold metadata. |
| `source/rendering/tests/geometry_allocator_tests.cpp:3006–3139` | Existing changed-decision test expects one state-program miss/rebuild, followed by a new stable hit. Altering this requires stronger barrier-equivalence proof and a revised expectation. |

Other seams to inspect before implementation: `render_graph_cache.cpp` for contract eviction, `render_node_graph.cpp:962–1014` for dynamic decision patch semantics, `render_flow_resource_pool.cpp` for **real** dedicated lease/fence/owner checks, RHI/NVRHI native admission and barrier lifetime. Line numbers are from the 2026-10-11 study checkout; rediscover at implementation time.

## 4. Fundamental scope restriction

**Initial eligible domain:** certified identical structural contract/publication and dedicated physical topology, compatible state descriptors, no placed aliases. Native handle replacements may rebind; changed device/domain epoch, incompatible descriptor, missing patch, queue topology change, or ambiguous physical map must reject optimization.

**Not part of first Stage B:** arbitrary graph node insertion/removal, recompilation across new contract publications, alias placement changes, cross-contract subgraph identity matching, GPU command barrier elimination, relaxing queue/fence/ownership checks, or deferred asynchronous compilation. Such cases retain ordinary full compilation. Future incremental *structural graph* compilation is a separate research program.

## 5. Phase P0 — research gates before either stage

| Task | Work and required artifact |
| --- | --- |
| **P0.1 Invalidation taxonomy** | Enumerate contract, state-program, dedicated-lane, decision, import/export, native-handle, descriptor-domain, queue, presentation and device invalidations. For each, identify whether it changes semantic output, only binding overlay, or the entire structural contract. |
| **P0.2 Real frequency and spike evidence** | Measure stable hits, changed-decision misses, localized state misses, structural misses and full fallbacks; compare p50/p95/p99/p99.9 and worst observed frame spike. Do not assume a half-millisecond spike always occurs. |
| **P0.3 Decision-only false-miss proof** | Confirm that `PatchResourceContractDecision` changes only capture-time bools under today's renderer contract. Differentially prove `LowerPhysicalStates`/RF17 program identical for both values and only execution overlay changes. If true, reuse the existing snapshot without Stage B. **Do not make this change until proved.** |
| **P0.4 Simpler alternatives** | Evaluate conservative multi-variant state snapshots, eliminating false misses, and coarse compilation islands before building fine-grained dependency tracking. Estimate memory and variant thrash for alternating states. |
| **P0.5 Authoritative oracle** | Specify reference full-compile output normalized by symbolic physical identity: every ordered action, entry/exit state, predecessor edge, clear/UAV/Present, compiled barrier and native batch. Define collision-safe equality, not hash-only acceptance. |

**P0 gate:** Prefer a smaller proven change if it eliminates the real-world invalidation spikes. An incremental compiler must earn its steady-frame bookkeeping and complexity.

## 6. Stage A — immutable execution program plus generation overlay

**Goal:** Reduce unchanged-frame copies/binding cost without moving more work into command recording, while creating a safe ownership foundation usable by future Stage B. Stage A by itself does **not** reduce invalidation recompute breadth.

| Task | Deliverable and correctness condition |
| --- | --- |
| **A1 Field inventory** | Classify every `CompiledPacket`, snapshot, and `ExecutionGenerationRef::Impl` field as immutable semantic image, generation-local state, runtime/liveness, or RHI owner. Audit each reader and mutation site. |
| **A2 Compare representations** | Measure existing bulk generation clone vs shared immutable program with local overlay, optional *hybrid* packet/local barrier materialization. Do not assume zero-copy wins if it adds per-barrier pointer/ordinal indirections. |
| **A3 Immutable symbolic image** | Store steps, actions, tape, predecessors, barrier templates/batches and stable symbolic physical/binding indices without `ResourceRef`, descriptor handle, native pointer, pool lease, real fence or session generation. Version/certify per contract and physical topology. |
| **A4 Generation overlay** | Own current session/execution generation, decisions, incoming waits, terminal/presentation acquisition, sorted native ResourceRefs, symbolic-to-current physical map, symbolic-to-sorted-slot map, entry-state current refs, descriptor borrows and packet runtime/liveness. |
| **A5 Lifetime ownership** | If sharing with published generations, independently refcount the immutable program: cache/physical variant owns one reference; each living execution generation owns another. Preserve held packet-view metadata after `Finish` and after cache eviction/destruction, without retaining native resources. |
| **A6 Current native admission** | Continue `AdmitCommandListResources` on the current sorted unique `ResourceRef` table with its native/residency checks. Remap each symbolic barrier slot exactly; maintain currently required `SeedCommandListStates`, queue waits and descriptor ownership. |
| **A7 Fail-closed publication** | Construct a complete candidate generation privately; verify maps, resource kinds, state witnesses, capacity and native binding uniqueness; atomically publish only complete metadata. Any failure uses ordinary compile/failure semantics. |
| **A8 Correctness/performance gate** | Run stale-handle and multi-generation tests; profile both Resolve and recording, normal/64/320/large graphs, full-frame p50/p95/p99 and retained-program memory. Keep the simpler copied model if overlay indirections regress total CPU. |

**Stage A proposed performance hypothesis (not measured):** Investigate ~75–90 µs net Resolve reduction from the present ~391 µs; bind/overlay zone might approach ~55–65 µs, toward ~300–310 µs Resolve. Treat these as targets with an opt-out, **not promised gains**. Stage B should not compensate for a Stage A warm-path regression.

## 7. Stage B — dependency-aware incremental compilation

**Correct compilation unit:** a packet's transfer of **state facts for the symbolic physical resources it touches**, plus any retroactive predecessor/terminal effects — not an independent packet compiled solely from its own local declarations.

Suggested input frontier per relevant symbolic physical: exact per-subresource state; `seen/UAV/write` access history; explicit-state producer provenance (`lastPacket`/queue); queue fork/join proof; alias/ownership facts; and initial/terminal import/export obligations. Suggested output includes modified frontier **and** ordered effects on predecessor exits, presentation and terminal packets. The footprint and resource-to-packet index are built *once on cold compile*, not walked/built on every stable frame.

| Task | Deliverable and proof |
| --- | --- |
| **B1 Stable symbolic identity** | Base dependency identity on certified physical lane, not only logical allocation, because multiple logical allocations can reuse the same lane. Preserve imported/exported symbolic entries and exact state descriptor witnesses. |
| **B2 Capture transfer reads/writes** | Instrument/full-compile capture of packet resource/subresource read and write footprints, UAV history dependencies, queue ownership facts and external terminal effects. Avoid recording irrelevant fields that increase cold-build cost or false dependencies. |
| **B3 Complete frontier certificate** | Define canonical comparison of exact states + UAV `seen/write` bits + last relevant producer/queue + needed alias/export/presentation obligations. Hash only as a filter followed by exact equality or equivalent collision-proof versioning. |
| **B4 Bidirectional effect graph** | Encode consumer -> earlier producer exit-action mutations on cross-queue handoffs; last-packet terminal repair; same-frame physical reuse; coupled texture UAV normalization/restoration. Represent inseparable regions as compilation units where safer. |
| **B5 Sparse persistent dependency index** | Build physical -> producer/consumer/terminal edges at full compile, versioned under structural contract and dedicated topology; no per-frame graph traversal or rehash on unchanged hits. |
| **B6 Dirty seeding** | Distinguish changed semantics/state contract, decision-only overlay changes, current native-handle remap, and topology invalidation. Only genuine changed compiler inputs seed packet transfer recompilation. |
| **B7 Closure/convergence** | Visit affected units in semantic/queue order; propagate along physical-state and backward-effect edges. Reuse descendants only when the complete input/output frontier and predecessor/terminal effects match the old certified program. |
| **B8 Selective RF17 lowering** | After transfer convergence, rebuild packet-local RF17 barriers only for packets whose lowered entry state/action/barrier inputs changed. Always preserve native batch fences, exact action order and required GPU barrier emission. |
| **B9 Transactional program replacement** | Create changed packet images and updated dependency facts privately, leaving existing shared programs valid for active generations. Publish an all-or-nothing new immutable version; no mutations visible to old generation views. |
| **B10 Work and memory cap** | Preflight dirty-set breadth/estimated work and bail to full compile before expensive exploration when closure is wide/uncertain; bound scratch/memory peaks. Mid-compile proof failures also fall back safely, but instrument double-work cost. |

**Stage B go/no-go:** Measurably smaller local invalidation work and p99 frame spike, unchanged steady-frame overhead preferably ≤5 µs at 320 resources (proposed budget, unmeasured), no material regression on normal/64, and proven parity against full compile. If closure routinely covers most packets, favor coarse islands/variants or ordinary full recompilation.

## 8. Non-obvious counterexamples requiring focused proof

1. **Backward queue handoff:** graphics producer exit transitions to `Common` because later compute consumer cannot accept the prior state. Altering consumer can remove/change actions in the **earlier** packet. Correct closure cannot be forward-only (`resolve.cpp:1675–1725`).
2. **Same final state, different UAV ordering:** RAW/WAW chain ends in `UnorderedAccess` both ways, yet one path requires a UAV barrier and the other does not. State-only hash is insufficient (`1832–1929`).
3. **Disjoint mip, whole-texture coupling:** a partial texture UAV barrier requires normalization of *other* subresources and restoration afterward; a naive touched-range dependency misses them.
4. **Explicit last-producer provenance:** matching per-cell state values without matching last packet/queue can change entry states, cross-queue proofs, or terminal repair (`1620–1746`, `2116–2171`).
5. **Two logical resources, one dedicated physical:** distinct logical names may map to the same reusable lane during a frame. Logical-only dependency graphs are unsound (dedicated physical assignment and `BuildPacketResourceBindings`).
6. **Import/export and Present:** changed incoming *fence value* is frame overlay; changed import state/queue is a semantic contract mismatch. Export terminal state or Present acquisition/previous state can change exit work; no stale acquisition or descriptor may be cached.
7. **Native binding permutation:** changing numeric RHI handle order between frames must preserve sorted unique command-list admission and remap all compiled barriers to the right symbolic physical.
8. **Placed aliases:** alias predecessor finalization and destination activation alter dependencies outside normal dedicated state transfers. Initial incremental compiler must take full fallback; do not overclaim coverage.
9. **Cache eviction and held views:** an old `CompiledExecutionPacketView` may outlive the cache/physical-plan variant. Its immutable data survives; stale execution/liveness cannot revive resources or descriptors.
10. **Partial failure:** out-of-memory, capacity exhaustion, malformed remap, queue proof mismatch or device loss must not publish a partially mixed old/new packet program; preserve the existing reference compiler and failure contracts.

## 9. Validation packages — acceptance criteria, not tests already run

| ID | Required future test work |
| --- | --- |
| **V1 Reference differential** | Compare full vs cached/incremental normalized packet entry states, predecessor edges, exit actions, step/action tape, clears, subresource transitions, UAV barriers, native batch ids and final terminal state. |
| **V2 Decision-only variation** | Toggle one patched bool with otherwise identical immutable graph/physical contract; prove no barrier change before accepting decision-only reuse. Test both bool values repeatedly and missing patch rejection. |
| **V3 State convergence** | A -> B -> C explicit-resource chain where B recomputes to the original outgoing frontier so C is reused; also divergent B forcing C recompilation. |
| **V4 Backward and queue coupling** | Graphics -> compute fork/join changes producer exit actions; prove exact queue admission, barrier order and cross-queue fence behavior. |
| **V5 UAV and subresources** | Partial mip/slice edits, normalization/restoration, overlapping vs disjoint slices, RAW/WAW and UAV same-state ordering. |
| **V6 Physical identity / terminal** | Whole-frame lane reuse, logical mapping swap, imported and exported backing, last-use terminal repair, Present prior-state/acquisition change, native ResourceRef-sort inversion. |
| **V7 Lifetime and failures** | Held packet views after `Finish`/contract eviction, all four physical variant eviction, stale use IDs/ranges/decision IDs, descriptor epoch, device loss/restart, pool lease/fence retirement, capacity/OOM fallback and canceled generations. |
| **V8 Random differential stress** | Reproducible seeds over legal ordered uses, queue boundaries, decisions, imports/exports and failure points; compare exact output and actual D3D12/NVRHI behavior. |
| **V9 Performance and memory** | Stable normal/64/320/large graphs; local invalidations every 1, 10, 60, 120 frames; early/middle/late packet and high fan-out; full fallback; report Resolve and full-frame p50/p95/p99/p99.9, maximum spike, GPU work, record CPU, program bytes and compiled/reused packet counts. |

Use existing `geometryAllocatorTests`, `renderingTests`, and relevant `rhiNvrhiTests`/`rhiTests` as starting points, with real-RHI proofs; test-only equality is insufficient if backend recording differs. VS2022 x64 outer MSBuild `/m:1` when future implementation is authorized. Keep the shared dirty checkout intact.

## 10. Decision gates and stop rules

1. **Gate P0 — research:** establish real invalidation frequency, compiler oracle and whether decision-only misses or simple state-variant caching explain most spikes. If yes, choose smaller solution and reassess B.
2. **Gate A — ownership and warm cost:** implement Stage A only after explicit approval; reject/roll back if full-frame CPU, recorder cost or typical 64/normal graphs regress despite smaller Resolve zone. A refcounted immutable program is an option, not a compulsory final representation.
3. **Gate B1 — conservative pilot:** only identical certified structural contract + dedicated topology. Prove complete bidirectional effect closure, small dirty-set work, convergence and transactional publication; uncertain cases go full compiler.
4. **Gate B2 — production:** differential oracle, real GPU tests, devices/descriptors/lifetime and failure-injection pass, stable-frame cost negligible, local spikes materially reduced, and no hidden memory or GPU regression.
5. **Deferred separately:** arbitrary cross-contract graph incremental compilation and placed alias incremental recompilation require independent study and authorization.

**Stop rule:** If Stage A does not deliver a net warm benefit, or Stage B's extra steady-frame CPU/memory exceeds its practical spike savings, do not continue increasing cache complexity. Keep the old safe compiler and retain the lessons for a future profile-driven attempt.

## 11. Resumption checklist

When the user elects to revisit this optimization: read this deferred document and the related warm-Resolve record; inspect *current* source and exact build status (line locations may have changed); obtain new matched-workload baseline; independently audit real invalidation witnesses; select one gated task at a time; request fresh source-implementation authorization as applicable. Do not infer that this document authorizes code changes or tests.

**Study status at preservation:** P0/A/B/V plans specified, independent read-only reviews completed, **zero Stage A/B implementation work**, zero study-time builds/tests/profiles. No proof yet that Stage B provides net positive production value.
