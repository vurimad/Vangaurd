# Vanguard I/O

Vanguard I/O is the complete RED low-level I/O image behind a Vanguard-owned
public boundary. The imported implementation supplies synchronous native-file
access, asynchronous reads, priority queues, the worker thread, file-handle
caching, cancellation contexts, memory throttling and caching, statistics, and
profiling hooks.

Filesystem paths, mounts, directory traversal, archives, package formats, and
file watching are not owned here. They belong to the next `filesystem` module
and will consume this module.

## Initialization and lifetime

Memory, Diagnostics, and Containers must be initialized before I/O:

```cpp
vanguard::memory::Initialize();
vanguard::diagnostics::Initialize();
vanguard::containers::Initialize();
vanguard::io::Initialize();
```

Initialization and shutdown are composition-root operations and must be
serialized. Repeated initialization while running is idempotent. The preserved
RED global worker supports one initialization/shutdown lifetime; Vanguard
rejects an attempt to restart it after shutdown instead of entering invalid
state.

Shutdown stops and joins the RED I/O worker, cancels queued callbacks, waits
for allocator-backed I/O buffers to be released, and then destroys worker
state. Callers must release every async file handle and callback-owned I/O
buffer before shutdown.

## Synchronous use

```cpp
vanguard::io::NativeFileHandle file;
if (file.Open(path, vanguard::io::eOpenFlag_Read))
{
    vanguard::u32 bytesRead = 0;
    file.Read(destination, size, bytesRead);
    file.Close();
}
```

This is RED's unbuffered native-file contract. Higher-level engine code should
normally use the future filesystem layer.

## Asynchronous use

Open a physical file through `vanguard::io::System()`, fill an
`AsyncReadToken`, and submit it with the required priority. The token callback
runs on the RED I/O callback thread and receives success, cancellation, or
error exactly once for a submitted operation.

If `AsyncReadToken::m_buffer` is null, the preserved RED I/O allocator supplies
shareable memory and transfers it through the callback. If the field is not
null, the caller owns that destination and must keep it alive until callback
completion. `IOContext` preserves RED cancellation, loading-state, observer
distance, and priority-inversion metadata.

The queue has the original GAME, UI, AUDIO, and FULLSCREENVIDEO lanes and RED's
weighted scheduling behavior. Critical priority markers, thread-local bulk
submission, loading/saving modes, sorting, read merging where enabled, cache
reuse, handle reference counting, and final-build statistics remain intact.

## Memory and diagnostics

The implementation uses Vanguard's imported RED memory backend. RED's
`PoolAsyncIO`, `PoolEngine` allocations, reference-counted buffers, and the
dedicated bounded I/O allocator are preserved rather than replaced with CRT
allocation. RED log calls flow through the Vanguard Diagnostics adapter.

## Verification

`ioTests` covers dependency rejection, initialization and one-lifetime
shutdown, native create/write/read/seek/flush, invalid and valid async opens,
handle metadata and references, caller-buffer and allocator-backed async
reads, callbacks, byte telemetry, context state and cancellation, priority
submission, queue sorting, and bulk submission boundaries.

Debug, Development, Profile, and Shipping builds are required to pass.
