# Vanguard Crypto

`crypto` owns allocation-free cryptographic primitives required by durable
engine identities. The initial contract is incremental SHA-256 and a stable
256-bit digest value.

## RED-derived implementation

The SHA-256 transform and incremental structure were adapted from:

- `redCrypto/include/sha256.h`;
- `redCrypto/src/sha256prv.h`;
- `redCrypto/src/sha256prv.cpp`;
- `redCrypto/src/sha256.cpp`.

Vanguard corrected RED's finalization path to encode the complete 64-bit
message length. The public API contains no RED types or names and performs no
allocation.

This module does not yet provide encryption, signatures, key storage, random
generation, password hashing, or transport security.
