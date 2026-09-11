# Rendering component performance preflight

2026-09-09. Static inspection before 9D ownership integration. No compilation,
project generation, test authoring, execution or performance measurements.

## Scaling problems addressed

- Proxy admission previously removed a prefix of a dynamic array and reindexed
  every remaining pending slot each frame. Processing B entries from backlog N
  cost O(N), despite the batch limit. Existing admission slots now carry FIFO
  links; enqueue/cancellation/dequeue are O(1), frame extraction O(B). The pending
  index array was removed. Existing fixed slot storage and batch scratch remain.
  Extracted slots become Processing before callbacks, so cancellation cannot
  unlink a slot that has already left the queue.
- Camera dirty cancellation previously searched the dirty list per component,
  giving O(C squared) worst-case mass cancellation. Each existing camera node now
  has a backlink. The successful atomic head inserter establishes the old head's
  backlink; removal/draining occur only after all transform producers join.
  Cancellation is O(1); changed-camera publication remains O(C). No lock added.
- Retirement admission searched all R pending retirements for duplicates on every
  removal, giving O(R squared) mass removal. BeginProxyRetirement now hides and
  claims the exact live proxy generation through its existing RenderScene slot.
  One boolean replaces the search; no ownership map or duplicate directory.
  The marker resets with slot reuse. The existing world delay queue remains owner
  of when destruction is attempted.
- Removing successful retirements could shift failed entries repeatedly. Reverse
  processing now swap-removes successful entries in O(1); already-visited moved
  entries preserve their countdown. Shutdown simply pops the tail.
- Removed the redundant mesh-specific failure forwarding method; all component
  failures use the existing general reporting method and failure storage.

## Existing costs retained deliberately

Mesh preparation visits at most the configured pending-work budget per frame
(default 128). Ready components leave that list. RequestDrawable shares an
existing closure and validates its bounded phase context; topology/primitive
construction belongs to residency and runs when creating a closure, not once per
entity every frame. Resource loading uses existing requests and handles.

Camera updates use RenderCommandSystem's existing previous-frame boundary. The
first call can join the CPU tail; subsequent calls see an invalid tail and return
immediately. This is not a separate wait for every camera. Each camera's phase
set is built at attachment, not rebuilt in the dirty publication loop.

GPU scene and tracked-proxy lookup are direct/paged and generation checked.
GPU publication visits dirty indices and existing disjoint write ranges. Serial
publisher methods take an existing scene lock. The 9D path batches acceptance
checks for changed proxies under that lock instead of polling all proxies or
adding another lock.

Retirement countdown still visits pending retirements once per frame, O(R),
bounded by its configured capacity. With long delays or a large failing backlog
this can still be costly; it is not an all-live-entity scan. A due-time queue can
replace the delay policy if required, within this same owner. No second timer or
retirement manager was introduced. Global directional collection visits the
existing global/unindexed candidates per view, as required for eligible lights.

## 9D constraints established by this inspection

MeshDrawableBinding supports O(1) main-thread Retain/Reset. The 9D implementation
keeps accepted/candidate bindings in RenderScene and exposes an owner-thread retain
for recording ownership. It does not embed the binding in an arbitrary worker-
released frame payload. GPU use after proxy removal relies on the existing
withdrawal and fence-retirement chain.

Reuse RenderScene proxy storage, the publisher's existing binding revision and
acceptance machinery, retained frames, candidate batches and GPU lifetime/fences.
Track pending replacement/clear work by changed proxy, not by polling every mesh.
Mesh bind/clear now return the existing publisher revision receipt. Acceptance
resolution uses an intrusive changed-payload queue and one shared-lock batch per
affected scene, capped at 256 checks with round-robin fairness. No entity or live
proxy scan was introduced. This is source implementation; execution proof remains.

Deferred verification: FIFO admission across bounded batches; head/middle/tail
cancellation; reentrant admission callbacks; exact generation reuse; concurrent
camera producers followed by arbitrary cancellation; duplicate retirement;
mixed successful/failed retirement and preserved delays; large streaming/removal
bursts. These require the user's final executable verification batch.
