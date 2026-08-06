# Textures

The runtime `textures` module owns the renderer-agnostic `vtex` document. It stores canonical GPU-ready subresources,
their exact upload pitches, integrity digests, an explicit resident mip tail, and byte-exact storage segments suitable
for partial VPAK residency. Source images, importer state, mip generation, and compression belong to `textureTools`.

`vtex` never embeds editor source images, depot paths, renderer objects, or platform API handles.
