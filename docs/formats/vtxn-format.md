# VTXN asset-file transaction journal

## Version 1

VTXN is the recovery record for one bounded project source-file transaction. It is not an asset database, undo history, build record or runtime resource catalog. Only one journal may be active for a configured transaction owner.

All integers are little-endian. The fixed header is 48 bytes:

- `u32 magic`: `VTXN` (`0x4e585456`);
- `u32 version`: `1`;
- `u32 state`: `1` Staging, `2` Prepared or `3` Committed;
- `u32 entryCount`;
- `u8 digest[32]`: SHA-256 of header bytes 0-15 followed by all entry bytes.

Each entry contains:

- `u8 kind`: `0` Write, `1` Move or `2` Delete;
- `u8 targetExisted`: zero or one;
- two zero reserved bytes;
- `u32 sourcePathBytes` followed by that many UTF-8 bytes; empty except for Move;
- `u32 targetPathBytes` followed by that many UTF-8 bytes.

Staging and backup names are derived rather than serialized: `.vtxn-stage` and `.vtxn-backup` are appended to each target path. The journal update itself uses the sibling `.stage` suffix and write-through replacement.

## Publication and recovery

The owner publishes Staging before writing payload staging files, durably flushes and byte-validates every staged write, then advances to Prepared. Prepared authorizes filesystem mutation and always recovers by reverse-order rollback. Committed is the durable commit point and always recovers forward after validating that every new target exists, every deleted target is absent and every moved source is absent; only then are transaction-owned staging and backup files removed.

Recovery authenticates and bounds the entire journal before touching disk. Every path and derived auxiliary path must remain within one of the currently configured writable roots, must not collide with another entry or the journal, and must pass physical ancestor/reparse validation. Move source and target paths must belong to the same registered root. An invalid journal is retained for diagnosis and requires explicit intervention.

Staging recovery refuses unexpected backups because no original target may have moved before Prepared. Prepared recovery restores target backups and reverses completed moves. Committed recovery never restores backups; it preserves committed targets and finishes cleanup.
