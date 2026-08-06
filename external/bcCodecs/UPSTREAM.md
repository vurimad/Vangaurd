# BC codec upstream

- Source: https://github.com/richgel999/bc7enc_rdo
- Commit: `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0`
- License: MIT or public domain (`LICENSE`)
- Imported from the user-provided checkout at `D:\vendors\bc7enc_rdo`
- Imported content: `rgbcx` BC1/BC3/BC4/BC5 encoder, the portable C++ BC7 encoder, and its BC7 conformance decoder
- Local source patches: none
- Owning Vanguard module: `textureTools`
- Public API exposure: none

`SUPPORT_BC7E` is deliberately not defined. It controls only the optional ISPC BC7E backend; it is not required by the portable BC1/BC3/BC4/BC5 or BC7 encoders compiled here. RDO and ISPC are separate future cooker backends and are not silently enabled by this snapshot.
