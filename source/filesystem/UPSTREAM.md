# Imported filesystem provenance

- RED source: `D:/root/R6.Root/Mainline/dev/src/common/redFileSystem`
- Vanguard image: `source/imported/common/redFileSystem`
- Imported inventory: 58 files, 190,204 bytes
- Sorted `relative-path<TAB>file-SHA-256` manifest SHA-256:
  `ef3ac2eb4153d6396908c1534f9f78acdda9790e078f64ebae681d39459c124a`

The image differs from the source only in:

- `src/fileSyncFileManager.cpp`
- `src/fileSyncService.cpp`

Those two files contain include-path relocation only, so the original
implementation can find Vanguard's quarantined FileSync header.

`redCompression` is carried as the complete RED compression implementation
required by chunked filesystem streams. Third-party codec sources compile in a
separate no-PCH compatibility project because RED's module PCH intentionally
redefines the CRT assertion macro.

## Binary dependencies

FileSync and UDT came from the previous Vanguard integration because the
matching internal binary package is absent from the RED source tree available
locally. Oodle came from that integration's RED dependency image.

| File | Bytes | SHA-256 |
|---|---:|---|
| `FileSync.dll` | 73,216 | `1af2d138726a7b1b86b51aed1e473b5895cea8dadc8e21609dbbae492e6d9b5f` |
| `FileSync.lib` | 11,570 | `4bb90ceea07bedba46b16b6e653df5944ade4610c0fbf928e3e03b36f81693fd` |
| `udt.dll` (release) | 240,128 | `e5dbb562efb0adcabebeab9a547939b1f5a265f9aead76bb2abdb0d81c0deccc` |
| `udt.lib` (release) | 28,992 | `faf4c098990cd755b5530424ad53683cc28399ed6dbb1163e92c12ca840dcea1` |
| `oo2ext_7_win64_debug.dll` | 3,122,176 | `679d0fe560f267798f38d84b4ecd6605b6d81a67c3bfe6f3c29b000c4ab0f9ec` |
| `oo2ext_win64_debug.lib` | 54,526 | `d3e71324b3ddccd73f2e118cb34949e15c70b59a5eecc3cf4c960901b022d20d` |
| `oo2ext_7_win64.dll` | 1,223,680 | `23bb4eb079a00c2d583472e11e4d36188ba1b365bf115f8a33cb53d18a319908` |
| `oo2ext_win64.lib` | 53,272 | `6aa7d80269a9e6a8114680ac0528a5b3aca56aac22d38ce1ec64c8412db7848f` |

The FileSync header, UDT license/debug binaries, Oodle headers, and dictionary
are retained with their original packages. Premake links only the binaries
selected by the RED configuration contract.

