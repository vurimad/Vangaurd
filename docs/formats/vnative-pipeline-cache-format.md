# Vanguard native pipeline cache (`vnative pipeline cache`)

The native pipeline cache is an operational, machine-specific file containing one opaque blob exported by the active renderer backend. It is not a resource, is never placed in VPAK, and is never required for correctness. A miss or rejected record only causes native pipelines to be recreated.

## RED lineage and Vanguard hardening

RED initializes a D3D12 pipeline library from a disk blob, shares stores across worker-facing clones, periodically serializes it, and waits for save work during renderer shutdown. Vanguard retains the device-lifetime import/export model while adding properties absent from the original raw blob:

- explicit backend, schema, adapter, driver, and engine-build identity;
- SHA-256 identity and payload validation;
- a configured size ceiling;
- validation before publication;
- atomic write-through replacement;
- abandoned-temporary cleanup;
- corruption, incompatibility, and backend-rejection recovery;
- operational telemetry.

## Version 1 record

The file uses magic `VNPC`, version `1.0`, little-endian scalar encoding, and a fixed 208-byte header.

| Field | Size |
|---|---:|
| Magic, version, header size, flags | 16 bytes |
| File size and payload size | 16 bytes |
| Identity SHA-256 | 32 bytes |
| Payload SHA-256 | 32 bytes |
| Backend identity and version | 12 bytes |
| Cache schema | 4 bytes |
| PCI vendor and device IDs | 8 bytes |
| Stable adapter identity | 8 bytes |
| Driver version | 8 bytes |
| Backend compatibility digest | 32 bytes |
| Engine build digest | 32 bytes |
| Reserved | 8 bytes |
| Opaque backend payload | Remaining bytes |

All reserved bits and bytes must be zero. The reader verifies exact file sizing, supported version, complete identity, both SHA-256 digests, and the configured payload limit before exposing bytes to the backend.

## Identity and invalidation

The record is compatible only when every identity component matches:

- renderer backend and backend version;
- cache schema version;
- GPU vendor and device;
- selected adapter identity;
- driver version;
- backend-defined compatibility identity;
- Vanguard engine-build identity.

This is intentionally conservative. Rebuilding native pipelines is cheaper and safer than asking a driver to consume ambiguous cache data.

## Publication and recovery

The backend exports bytes into Vanguard-owned memory. Vanguard writes a complete temporary record, reopens and validates it, compares its payload fingerprint with the source, then atomically replaces the active record using write-through filesystem semantics.

Startup removes abandoned temporary files. A malformed record, incompatible identity, or backend import rejection removes the active record and increments recovery telemetry. Vanguard then continues with an empty backend cache.

## NVRHI placement

The future renderer adapter owns the concrete import/export calls:

1. Query NVRHI/backend, adapter, and driver identity.
2. Initialize `NativeCacheStore`.
3. Call `Restore` before or during backend pipeline-library initialization.
4. Create runtime pipelines through `pipelineCache`.
5. At controlled save points, call `CaptureAndPublish`.
6. Flush the runtime cache before destroying the NVRHI device.

No NVRHI structure appears in this file. The opaque payload may change whenever the backend compatibility identity or cache schema changes.
