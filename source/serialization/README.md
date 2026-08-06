# Vanguard Serialization

Vanguard Serialization defines the canonical binary primitives and document
envelope used by future Vanguard resources, packages, caches, and editor data.
It does not load or generate any RED resource, depot, archive, or package
format.

The implementation follows proven RED architectural principles:

- explicit readers and writers instead of dumping C++ object memory;
- fixed wire headers with compile-time sizes;
- format-specific magic and bounded version acceptance;
- absolute offsets and caller-controlled read limits;
- sorted, non-overlapping section tables;
- per-section codec, alignment, sizes, and integrity;
- sticky errors and explicit results;
- caller-owned storage and checksum scratch memory.

The wire values, compatibility policy, flags, section descriptors, and magic
values are Vanguard-owned.

## Binary primitives

`BinaryReader` and `BinaryWriter` operate on Vanguard filesystem streams.
Integers and IEEE floats are encoded little-endian. Booleans are exactly zero
or one. Variable integers use canonical unsigned LEB128 and zig-zag signed
mapping; overlong and overflowing encodings are rejected.

Alignment padding is written as zero and validated as zero while reading.
Writers cannot seek past the current end, preventing accidental uninitialized
gaps. Patch writes cannot extend a stream.

No serialization lifecycle or global registration is required.

## Documents and formats

Every concrete format selects its own FourCC, extension, supported version
range, section IDs, and semantic schema. The generic document header does not
make unrelated resources interchangeable.

```cpp
constexpr auto WorldMagic =
    vanguard::serialization::MakeFourCC('V', 'W', 'L', 'D');

vanguard::serialization::DocumentHeader header;
header.magic = WorldMagic;
header.version = {1, 0};
```

Major versions are incompatible wire/schema changes. Minor versions are
compatible changes only when the reader explicitly includes them in its
`VersionRange`. There is no implicit “accept everything newer” behavior.

CRC32 protects the header and CRC64 protects stored section bytes. These detect
corruption and are not cryptographic authenticity checks.

The normative wire layout is documented in
`docs/formats/vanguard-binary-format.md`.

