# RED Logger Full-Image Integration

## Decision

Vanguard reuses the complete mature logger already present in the imported
`redSystem` working image. The implementation retains RED's asynchronous
worker, lock-free queues, fixed-size messages, filtering, sink fan-out,
debugger output, and file sink.

The logger source is compiled once. Copying the same files into a second
project would create duplicate global logger state and duplicate symbols.

## Boundaries

```text
Vanguard modules
      |
      v
diagnostics
      |
      v
diagnosticsCompat
      |
      v
imported redSystem logger image
```

- `source/diagnostics/include` is the public Vanguard contract.
- `source/diagnostics/src` is RED-free Vanguard implementation.
- `source/diagnostics/compat` is the only diagnostics code permitted to include
  RED headers or name `red::` symbols.
- `diagnosticsCompat` contains imported-facing code and uses legacy
  warning policy.
- `diagnostics` and its tests treat warnings as errors.

## Materialized contract

The first adaptation covers the complete logger controls needed by low-level
modules:

- asynchronous and synchronous operation;
- fatal through trace severity levels;
- the complete imported category set;
- global, level, and category filtering;
- thread-context, frame-number, and network-peer metadata;
- callback, debugger, and file output;
- formatted messages and explicit flush modes.

RED data-error objects and application-level TCP/TTY orchestration are not
public Vanguard contracts. They will be materialized only when an adapted
module demonstrates a dependency.

## Verification

The diagnostics smoke test passes in Debug, Development, Profile, and Shipping.
It validates synchronous behavior, filtering, formatting, file persistence,
callback metadata, and 4,096 asynchronously queued messages followed by a
synchronous flush fence.

Source scans confirm that public and normal Vanguard diagnostics code contains
no RED includes or `red::` names.
