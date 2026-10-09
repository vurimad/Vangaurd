# Source asset metadata - VMETA 1

Status: E1F source-level identity and transactional operation integration. Not compiled or tested yet.

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

The settings line above ends with a space: an empty byte sequence follows its version. All numeric fields use fixed-width lowercase hexadecimal. ID is 16 bytes in written byte order, importer is the 64-bit `SourceImporterId`, and importer/settings versions and output resource types are 32-bit values. The type value is illustrative; concrete importers provide their existing resource type IDs.

Settings are the importer-owned canonical byte sequence, encoded as hex after the settings version and space. This reuses existing typed compiler-setting encoders rather than adding a general property/variant framework. Human-facing settings controls and future metadata extensions are separate work. Schema 1 does not yet include labels or cooking overrides; adding authored fields requires an explicit schema revision rather than silently dropping unknown data.

Output records repeat in stored order. Keys are 1-64 lowercase ASCII letters, digits, underscores or hyphens. They are persistent importer-owned names, never array positions or physical filenames. An empty output list is permitted before first import. The document is bounded to 1 MiB, settings to 256 KiB and outputs to 4096. Unknown schema versions are rejected; malformed, extra, missing or reordered records are not silently repaired.

## Identity and reimport rules

- AssetId is nonzero, persistent and randomly assigned when adopting a new source. `GenerateAssetId` is the shared UUID-v4-shaped secure-random primitive; the project command owner retries catalog collisions before publication. Parsing and encoding never generate or guess an identity.
- Moving a source with its sidecar preserves ID. Duplicating a source must assign a new ID. A copied sidecar with an already-present ID is a conflict, not an alias inferred from identical bytes.
- An output maps to `assets/<32 lowercase hex source ID>/<output key>`, then through the existing ResourcePath hash and typed ResourceReference. Changing settings, source location or importer version does not change this logical identity.
- Do not reuse a removed key for a different semantic output, and do not change an existing key's resource type silently. Importer matching/migration must report ambiguous subasset correspondence. Schema/setting migration is explicit; an unknown newer schema must never be overwritten by an older tool.
- The codec rejects duplicate keys and output hash collisions within one sidecar. The source catalog rejects source-ID conflicts and resource-path hash collisions across catalog assets. Conflicts with other mounted non-project identities must still be checked at the integration boundary; a 64-bit hash is not proof of identity.
- ImporterVersion records the authored source-importer revision; the registered importer's actual version is authoritative for interpreting the source and composing build requests. The selected low-level compiler version remains independently authoritative in the build fingerprint. Version mismatches are explicit and never trust metadata as the running tool version.

## Ownership and publication

Parse/encode are pure codecs; destinations remain unchanged on failure. They do not perform file I/O or runtime registration. Settings and outputs are retained by the caller's metadata value, with no global registry, locks or renderer dependency.

Only build-relevant metadata is passed into BuildRequest fingerprints. Future labels/browser state must not accidentally trigger recooking. The project source catalog maps the source-root-relative path, including the source extension, into `BuildRequest::source`; generated outputs retain their path-independent AssetId/output-key identity. This keeps relative include and auxiliary-source relationships meaningful without changing VPAK identities or confusing an authored source with its cooked output.

Publication uses the E1F durable filesystem transaction. Encoding valid bytes does not itself guarantee atomic sidecar replacement or safe source/sidecar moves.
