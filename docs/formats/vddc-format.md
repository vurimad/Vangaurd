# VDDC persistent derived-data record

`VDDC` is Vanguard's local persistent derived-data record. It stores one exact
asset build result and is not a runtime resource or VPAK member format.

The architecture is adapted from RED `backendData`: exact prerequisite hashes
identify outputs, loose files use two-level hash fan-out, entries load lazily,
and invalid entries are removed so local generation can recover. The bytes
below are Vanguard-owned and do not encode RED dependency, resource, archive,
or reflection formats.

## Addressing and publication

For a 32-byte SHA-256 build fingerprint encoded as lowercase hexadecimal:

```text
<root>/<byte-0>/<byte-1>/<64-hex-build-fingerprint>.vddc
```

The two directory names are the first and second digest bytes. A publisher:

1. writes a GUID-named `.tmp` file in the final record directory;
2. flushes and closes it;
3. decodes it through the production reader;
4. validates its build and artifact-content fingerprints;
5. atomically moves it to the immutable content-addressed name.

If another publisher wins the race, the loser accepts the existing record only
after validating that it contains the same build and content fingerprints.
Startup removes abandoned `.tmp` files below the configured DDC root.

## Version 1.0 wire layout

All integers are little-endian. No native C++ structure is written directly.

| Field | Size |
|---|---:|
| Magic `VDDC` | 4 |
| Major version | 2 |
| Minor version | 2 |
| Header size (`96`) | 4 |
| Artifact count | 4 |
| Complete file size | 8 |
| Build fingerprint (SHA-256) | 32 |
| Artifact-set content fingerprint (SHA-256) | 32 |
| Total payload bytes | 8 |

The header is followed by `artifact count` descriptors, each 32 bytes:

| Field | Size |
|---|---:|
| Resource path identity | 8 |
| Resource type identity | 4 |
| Segment number | 4 |
| Artifact flags | 2 |
| Alignment log2 | 1 |
| Reserved, zero | 1 |
| Payload size | 8 |
| Reserved, zero | 4 |

Artifact payloads follow in descriptor order without implicit padding.
Alignment describes the artifact's required consumer placement; it does not
add padding to this cache record.

## Validation

Readers reject records with unknown magic or major versions, newer unsupported
minor versions, nonzero reserved fields, unknown flags, invalid identities,
duplicate resource/segment pairs, invalid alignment, missing primary output,
overflow, configured-limit violations, size disagreement, truncated data, a
path/header build-fingerprint mismatch, or an artifact-set SHA-256 mismatch.

A rejected record is a cache failure, not an asset-build failure. It is deleted
and the registered compiler rebuilds and republishes the exact key.
