# RenderFlow RF1 — descriptor-domain reclaim hygiene closure

Status: **CLOSED — 0 BLOCKER / 0 HIGH / 0 MEDIUM**

Date: 2026-10-04

Program: [`render-flow-rf-descriptor-ownership-plan.md`](render-flow-rf-descriptor-ownership-plan.md)

## 1. Gate purpose

RF1 is the RHI hygiene prerequisite for persistent physical-owner descriptors. It does
not implement RF2's canonical descriptor ownership model. Its job is narrower: remove
repeated retirement-list scans from descriptor hot paths while preserving exact public
descriptor behavior and fence safety.

The intended steady-state hot-path shape after RF1 is:

```text
Allocate: free-list pop
          -> one retirement-list reclaim only if free capacity is exhausted

Retire:   validate -> RecordUse -> known-slot fence check
          -> complete this slot immediately if safe
          -> otherwise enqueue this slot only

Maintenance / pressure: walk retiring list and reclaim completed slots
```

RF1 deliberately leaves the per-generation whole-resource Allocate/Write/Create*View
publication model in place. Removing that publication is RF2-RF5 work.

## 2. Implementation

Primary source:

- `source/rhi/nvrhi/src/common_backend.cpp`

`CompleteDescriptorRetirement(...)` centralizes one-slot completion and preserves the
existing completion contract:

- resource-domain native table entry is cleared to `None(index)`;
- the descriptor-held strong resource reference is released;
- resource, retirement and retiring-list link state are cleared;
- `pendingRetirement` decrements and `completedRetirements` increments;
- generation `0xffffffff` transitions to `Exhausted` and never re-enters the free list;
- otherwise generation advances and the slot is pushed onto the free list.

`AllocateDescriptor(...)` now performs O(1) free-list allocation while free capacity
exists. Only `freeHead == InvalidReferenceIndex` requests one
`ReclaimDescriptorSlots(...)` pass. Allocation failure is counted only if no slot is
available after that pressure pass.

`RetireDescriptor(...)` keeps validation and `RecordUse(...)` before the completion
test. It transitions the known live slot to `Retiring`; if that slot's fences are
already complete, including `{}`, it synchronously completes exactly that slot. If not,
it links that slot at `retiringHead` and returns without scanning unrelated retirements.

`RetireResources()` remains the maintenance owner of the full retirement-list reclaim
pass. Descriptor operations remain serialized by the descriptor-domain lock and the
existing maintenance lock order `m_descriptorDomainListLock -> domain->lock` is
unchanged.

## 3. Focused correctness coverage

Primary test:

- `source/rhi/nvrhi/tests/d3d12_backend_tests.cpp::TestDescriptorDomainReclaim`

The focused D3D12 proof now covers:

1. capacity-1 empty-fence retirement completes synchronously;
2. same index is immediately reusable with a new generation;
3. stale-generation `WriteDescriptor` and `RetireDescriptor` reject;
4. future-fence retirement remains pending and cannot be reused early;
5. free-list exhaustion returns `CapacityExceeded` while the fence is incomplete;
6. after fence completion, allocation pressure reclaims the pending slot without a
   `RetireResources()` maintenance call;
7. a capacity-2 staggered test builds a two-entry pending retiring list on independent
   queues, completes only the non-head entry, proves pressure reclaim unlinks that
   middle/tail entry while the head remains pending, then completes and reclaims the
   head;
8. 64 concurrent Allocate+Write operations return unique live indices;
9. those 64 descriptors retire concurrently with empty fences and return to a coherent
   fully-free state; and
10. 64 concurrent reallocations remain unique and each reused physical index advances
    generation before final cleanup.

The existing descriptor-domain block in `TestNativeResourcesAndSubmission` was also
strengthened with full final stats and an immediate recycle check.

Final exact-current adversarial rereads:

- backend reclaim implementation: **0 BLOCKER / 0 HIGH / 0 MEDIUM**;
- focused descriptor-domain test matrix: **0 BLOCKER / 0 HIGH / 0 MEDIUM**.

## 4. Build and runtime validation

Development builds completed successfully for:

- `rhiNvrhiTests.vcxproj`;
- `rhiTests.vcxproj`;
- `renderingTests.vcxproj`; and
- `geometryAllocatorTests.vcxproj`.

Runtime test results:

- `rhiTests.exe`: passed;
- `renderingTests.exe`: passed;
- `geometryAllocatorTests.exe`: passed;
- `rhiNvrhiTests.exe`: the RF1 focused descriptor-domain stage passes, including the
  final staggered/concurrency coverage, then the process later exits at
  `GPU Scene runtime integration test failed:` with no appended failure text.

The later GPU Scene failure is not claimed to be pre-existing and is not claimed to be
caused by RF1. Test ordering proves only that the RF1 focused stage completed
successfully before that later failure.

`git diff --check` on the RF1 source/test files reports no whitespace errors; only the
checkout's LF-to-CRLF warning is emitted.

## 5. 320-resource Profile evidence

The retained 320-resource descriptor-heavy workload remains the RF measurement floor.

An older baseline trace (`quad_runtime_scheduler_probe_resources320_final.tracy`) and
the first RF1 trace were captured at different dirty-checkout states, so their
difference is observational only and not a causal A/B. The first RF1 capture measured
median `FinalizeDescriptors` at 553.323 us and median `Resolve` at 1234.927 us. A second
RF1 run measured 595.908 us and 1502.991 us respectively, confirming material
run-to-run/environment variance.

To isolate RF1 itself, a same-checkout paired A/B temporarily changed only the two RF1
hot-path decisions (unconditional allocation reclaim and enqueue+reclaim retirement),
captured the established workload, restored the exact RF1 source, rebuilt, and captured
again. The RF1 source was restored byte-for-byte to its pre-A/B SHA-256 before the RF1
rebuild.

| Zone | pre-RF1 hot path p50 | RF1 p50 | delta |
| --- | ---: | ---: | ---: |
| `RenderFlow/Resolve/FinalizeDescriptors` | 690.505 us | 664.607 us | -25.898 us (-3.75%) |
| `RenderFlow/Resolve` | 1676.515 us | 1627.278 us | -49.237 us (-2.94%) |
| `Application/Frame` | 8089.846 us | 7467.538 us | -622.308 us (-7.69%) |

The frame delta is not promoted to an RF1 performance claim because whole-frame noise
and run-to-run variance are large. Even `Resolve` mean moved in the opposite direction
in the paired traces because of outliers. The defensible RF1 conclusion is narrower:
the repeated retirement-list work is eliminated by code construction, and the paired
median descriptor/Resolve measurements move in the expected direction, but the dominant
per-generation native descriptor publication floor remains.

That remaining floor is expected: the 320-resource workload still performs hundreds of
whole-resource descriptor allocations/writes/native view creations per generation.
RF2 is the first gate that moves canonical descriptors onto persistent dedicated
physical owners so warm generations can borrow stable handles instead.

## 6. Exit and authorization boundary

RF1 exit conditions are satisfied:

- no unconditional retirement-list sweep on normal Allocate or Retire;
- immediate zero-fence recycle preserved;
- fenced retirement/maintenance/pressure safety preserved;
- focused deterministic regression coverage is present;
- matched 320-resource Profile evidence is recorded; and
- final source/test adversarial review is 0/0/0.

RF1 is closed. **RF2 is not authorized by RF1 closure and must not begin without an
explicit user instruction.**
