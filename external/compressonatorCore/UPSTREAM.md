# Compressonator core upstream

- Source: https://github.com/GPUOpen-Tools/compressonator
- Commit: `f4b53d79ec5abbb50924f58aebb7bf2793200b94`
- License: MIT (`license/corelicense.txt`)
- Repository copy: `external/compressonatorCore/upstream`
- Imported content: CPU block-codec source snapshot; the Premake target compiles only BC2 and BC6H translation units
- Local source patches: none
- Owning Vanguard module: `textureTools`
- Public API exposure: none

Vanguard currently uses this backend for BC2, signed BC4/BC5, and BC6H. Those paths are absent from the smaller `rgbcx`/portable BC7 snapshot. Runtime texture loading does not link this library.
