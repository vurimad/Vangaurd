# NanoSVG in Vanguard

Vanguard vendors NanoSVG for editor-owned SVG icon rasterization.

- Upstream: `https://github.com/memononen/nanosvg`
- Pinned upstream commit: `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`
- Vendored files under `upstream/` are copied unchanged from that revision.
- License: zlib (`upstream/LICENSE.txt`).

The editor keeps its authored SVG files as canonical resources. NanoSVG parses and rasterizes a high-resolution white-alpha cache during editor UI initialization; normal ImGui texture tinting supplies hover, active, disabled, and theme colors at draw time.
