# Vanguard Editor resources

This tree contains immutable application-owned resources for Vanguard Editor. It is staged beside editor-capable executables as `<target>/Editor/**`; it is not project content and is not packaged into game `DATA###.vpak` files.

Stable top-level namespaces are `Fonts`, `Icons`, `Images`, `Cursors`, `Themes`, and `Extensions/<technical-name>`. Production code resolves resources through `EditorResourceService` semantic identities or validated extension-local paths; callers must not depend on the repository path, current working directory, directory scanning, or ad-hoc absolute paths.

Fonts and other third-party resources keep their license text beside the resource family.

Vanguard-owned branding artwork lives under `Images/Branding/`. The canonical editor wordmark is `Images/Branding/VanguardEngine.svg`; it is stored as path-only SVG geometry with no embedded raster image or C2PA metadata.

The production editor toolbar icon set lives under `Images/Icons/`. Those SVG files are the canonical artwork; they are not converted to a font or checked in as authored raster derivatives. `EditorResourceKind::SvgIcon` is parsed and rasterized by the vendored NanoSVG integration into a high-resolution white-alpha runtime cache, then tinted by the editor UI. The authored SVG remains the source of truth while the cache stays sharp across supported editor UI and monitor-DPI scales.
