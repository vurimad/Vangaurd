# Source asset metadata - VMETA 1

Status: E1B source-level codec and identity mapping. Not compiled or tested yet. Source catalog integration, ID assignment and safe file publication remain later E1 slices.

The shared `assets` module owns this contract, not the editor. The sidecar for `tower.fbx` is `tower.fbx.vmeta`. Source bytes and metadata are authored truth; build fingerprints, build results and DDC locations do not belong here.

## Text encoding

Schema 1 is bounded ASCII with these ordered records; the encoder emits LF, and the parser also accepts CRLF and an omitted final newline:

```text
vmeta 1
id 0123456789abcdef0123456789abcdef
importer 0000000000000001 00000001
settings 00000001 
output main 48534d56
```

The settings line above ends with a space: an empty byte sequence follows its version. All numeric fields use fixed-width lowercase hexadecimal. ID is 16 bytes in written byte order, importer is the existing 64-bit CompilerId, and importer/settings versions and output resource types are 32-bit values. The type value is illustrative; concrete importers provide their existing resource type IDs.

Settings are the importer-owned canonical byte sequence, encoded as hex after the settings version and space. This reuses existing typed compiler-setting encoders rather than adding a general property/variant framework. Human-facing settings controls and future metadata extensions are separate work. Schema 1 does not yet include labels or cooking overrides; adding authored fields requires an explicit schema revision rather than silently dropping unknown data.

Output records repeat in stored order. Keys are 1-64 lowercase ASCII letters, digits, underscores or hyphens. They are persistent importer-owned names, never array positions or physical filenames. An empty output list is permitted before first import. The document is bounded to 1 MiB, settings to 256 KiB and outputs to 4096. Unknown schema versions are rejected; malformed, extra, missing or reordered records are not silently repaired.

## Identity and reimport rules

- AssetId is nonzero, persistent and randomly assigned when adopting a new source. The codec never generates or guesses it. The catalog/adoption caller must use secure platform randomness and check collisions.
- Moving a source with its sidecar preserves ID. Duplicating a source must assign a new ID. A copied sidecar with an already-present ID is a conflict, not an alias inferred from identical bytes.
- An output maps to `assets/<32 lowercase hex source ID>/<output key>`, then through the existing ResourcePath hash and typed ResourceReference. Changing settings, source location or importer version does not change this logical identity.
- Do not reuse a removed key for a different semantic output, and do not change an existing key's resource type silently. Importer matching/migration must report ambiguous subasset correspondence. Schema/setting migration is explicit; an unknown newer schema must never be overwritten by an older tool.
- The codec rejects duplicate keys and output hash collisions within one sidecar. The future catalog must reject source-ID conflicts and resource-path hash collisions across assets and mounted roots, including conflicts with non-generated logical paths. A 64-bit hash is not proof of identity.
- ImporterVersion records the authored importer revision; the registered compiler's actual version remains authoritative for build invalidation. Future integration must resolve version mismatches explicitly rather than trusting metadata as the running tool version.

## Ownership and publication

Parse/encode are pure codecs; destinations remain unchanged on failure. They do not perform file I/O or runtime registration. Settings and outputs are retained by the caller's metadata value, with no global registry, locks or renderer dependency.

Only build-relevant metadata is to be passed into BuildRequest fingerprints. Future labels/browser state must not accidentally trigger recooking. Stable source identity mapping into BuildRequest::source and the project-backed resolver are E1C/E1E work; this codec does not invent an untyped source resource or change VPAK identities.

Publication must use the existing safe filesystem publication path in E1F. Encoding valid bytes does not itself guarantee atomic sidecar replacement or safe source/sidecar moves.
