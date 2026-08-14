# Image codec upstreams

These libraries are private dependencies of `textureTools`; their types never cross into Vanguard APIs and runtime texture loading does not link them.

- libpng: https://github.com/pnggroup/libpng, copied under `external/imageCodecs/upstream/libpng`.
- zlib: https://github.com/madler/zlib, copied under `external/imageCodecs/upstream/zlib`.
- libjpeg-turbo: https://github.com/libjpeg-turbo/libjpeg-turbo, copied under `external/imageCodecs/upstream/libjpeg-turbo`.

The checked-in configuration headers select the portable decoder implementation. SIMD assembly is deliberately not claimed until a Premake-owned NASM tool step is available and tested on every supported host.
