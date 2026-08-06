# VanguardDiagnostics

The `diagnostics` project exposes the proven RED logger through a Vanguard-owned
contract. The imported implementation supplies its asynchronous worker,
lock-free queues, message formatting, category filtering, sink fan-out,
debugger output, and file sink.

## Boundaries

- `include/vanguard/diagnostics` is the supported public API.
- `src` is normal Vanguard implementation and contains no RED names.
- `compat` is the only diagnostics area allowed to include RED headers or name
  `red::` symbols.
- The complete logger source image is retained in the shared imported
  `redSystem` compatibility snapshot already required by memory.

New engine modules must not include RED logging headers. Missing logging
capabilities are materialized through the Vanguard contract when a real
dependency requires them.

## Current contract

- synchronous and asynchronous modes;
- all imported severity levels and categories;
- runtime enable, level, and category filtering;
- thread context, frame number, and network-peer metadata;
- synchronous and asynchronous flushing;
- file output using the imported file sink;
- a callback sink suitable for tests and future editor ingestion.

The callback receives borrowed message data. In asynchronous mode it executes
on the logger worker thread and must not retain the supplied string pointers.

## Verified baseline

`diagnosticsSmoke` passes in Debug, Development, Profile, and Shipping.
It verifies:

- synchronous logging and severity/category metadata;
- level, global-enable, and category filtering;
- formatted messages through the Vanguard macros;
- synchronous flush behavior;
- persisted output through the imported file sink;
- 4,096 messages through the imported asynchronous worker and lock-free queue;
- complete queue consumption through the synchronous flush fence.

Vanguard-owned diagnostics code compiles with warnings as errors. The
RED-facing adapter is isolated in `diagnosticsCompat` under imported
warning policy.
