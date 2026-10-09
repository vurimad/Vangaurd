# Editor visual foundation plan

## Status and authorization

The user explicitly authorized **P0** and then **P1** on 2026-09-28 after providing
CryEngine, Maya and 3ds Max editor references. CryEngine is the primary visual reference;
Maya and 3ds Max are secondary references for dense professional DCC-tool presentation.

This work is deliberately separate from Editor Foundation E3. E3 remains accepted and
is not reopened. The user later separately authorized E4 Definition on 2026-10-02; E4
P0-P6 remain unauthorized.

The user approved a compact three-gate shape:

- **P0** - exact-current UI architecture, visual-language and acceptance lock;
- **P1** - production visual-foundation implementation and a real-editor candidate for
  user visual review; and
- **P2** - user-requested corrections, focused final compile/hygiene and final visual
  acceptance.

**P0 is CLOSED. P1 is CLOSED / accepted enough to move on after iterative real-editor visual review.
The user explicitly authorized P2 on 2026-10-02. Exact-current source contains the later narrow-window
hit-test correction, but no separate user message explicitly accepting/closing P2 is recorded before E4
Definition authorization. P2 therefore remains at final manual visual acceptance/bookkeeping rather than
being retroactively closed by E4. E4 Definition is CLOSED; E4 P0-P6 remain unauthorized.**

## Purpose

Establish a coherent Vanguard editor visual system before E4 starts adding high-surface-area
content tools. The goal is not to imitate Qt internally or replace Dear ImGui. The goal is
to make Vanguard own the editor's visual language while retaining the accepted ImGui docking,
multi-viewport, input, persistence and retained-rendering architecture.

The user must be able to review the actual `VanguardEditor` rather than a mock-up or a
test-only gallery before this work is accepted.

## Visual target

The primary reference is the supplied CryEngine editor screenshot. The viewport debug icons
inside that reference are explicitly out of scope. Maya and 3ds Max reinforce the desired
desktop-DCC density and information hierarchy, but do not override the CryEngine-led direction.

Locked characteristics:

- dark charcoal application/workspace chrome with several restrained depth levels;
- compact desktop-tool density rather than web/mobile spacing;
- square geometry, with zero or near-zero corner radii;
- thin one-pixel separators and borders instead of card framing;
- small, legible typography with clear normal/secondary/disabled hierarchy;
- muted gray controls and backgrounds with accent color reserved for selection, focus and
  meaningful active state;
- blue/cyan selection/focus as the initial accent family;
- compact tabs integrated into panel chrome;
- compact menu and toolbar strips that do not compete with the viewport/work area;
- dense tree/list rows and compact two-column property presentation for later E4/E5 consumers;
- small checkboxes, combo boxes, numeric/text fields, disclosure affordances and scrollbars;
- icon-friendly toolbar presentation without requiring every command to have an icon;
- viewport/work content remains visually dominant;
- almost no decorative empty space.

The intended direction is **CryEngine's professional dense character rebuilt cleanly**, not a
pixel-for-pixel copy. Vanguard should improve readability where the old reference becomes too
gray or cramped.

Explicitly rejected as the default editor language:

- rounded card-heavy layouts;
- large mobile/web-style controls;
- glass/translucent panels;
- Material-style presentation;
- oversized headings or generous decorative padding;
- bright accent color on every control;
- stock Dear ImGui styling as the finished product look.

## Exact-current architecture findings

### Existing UI ownership stays intact

The accepted UI integration remains authoritative:

- one Dear ImGui context owned by the editor;
- docking and native multi-viewport remain enabled;
- Vanguard Window Manager owns native windows, focus, DPI, cursor, clipboard and IME;
- Presentation/Rendering own host presentation and GPU submission;
- `EditorUiService` owns panel/host/texture identity and UI frame authoring;
- retained UI rendering consumes ordinary ImGui draw vertices/indices through Vanguard RHI;
- layout persistence remains the existing private ImGui-settings/UserState path.

No Qt dependency, second UI toolkit, second window loop, second panel registry, second renderer,
alternate presentation path or editor-only resource loader is authorized.

### Theme application seam

Exact-current `EditorUiImGuiPlatform::Initialize` creates the ImGui context and presently applies
only the established resize-border hover padding before the first frame. P1 should place one
private Vanguard theme application seam at this initialization boundary, after context creation
and before normal frame authoring.

The theme owns semantic tokens rather than letting individual panels scatter raw color/spacing
constants. At minimum it owns:

- text, secondary text and disabled text;
- workspace/panel/header/popup/field background levels;
- separator/border levels;
- selection/focus/accent states;
- hover/active/disabled control states;
- warning/error/success status colors;
- frame, item and window padding;
- row, menu, toolbar and tab density;
- border thickness and corner radii;
- scrollbar size/grab sizing;
- default indentation and spacing.

The first implementation should start around a 13 px logical body size at 1x scale, thin
one-pixel borders, zero/near-zero rounding and compact 3-5 px logical paddings. Exact visual
numbers are reviewable implementation values, not persistent project-format contracts.

### Shared widget boundary

The existing UI integration contract explicitly rejects a one-for-one wrapper around every
ImGui call. Preserve that rule.

P1 may add a small editor-private/shared visual helper layer only for recurring presentation
patterns whose appearance Vanguard needs to own, for example:

- compact toolbar/icon button treatment;
- section headers;
- dense row selection/hover treatment;
- search/filter fields;
- compact status/activity rows and badges;
- property-grid/tree-row primitives needed by later tools.

Ordinary ImGui layout calls may remain in tool implementations. Domain operations continue to
use Vanguard command, panel, selection, asset and document contracts; no ImGui type enters engine
or authored project/runtime APIs.

### Custom drawing is compatible with the retained renderer

The current renderer copies normal ImGui draw-list vertices, indices and texture references into
retained Vanguard render data. Therefore custom editor widgets may use ordinary `ImDrawList`
primitives and still follow the existing renderer.

Arbitrary ImGui user draw callbacks are **not** part of the visual-foundation design: the retained
boundary intentionally accepts only the render-state reset callback. Custom appearance must emit
ordinary draw geometry rather than bypassing retained rendering.

### Fonts and icons

The current ImGui managed-texture path already accepts RGBA32 atlas create/update requests and
routes them through `EditorUiService`/Vanguard RHI. This is sufficient for custom fonts and glyph
atlases without changing the renderer architecture.

P1 must use deterministic editor-owned/license-cleared font/icon inputs. It must not depend on a
font installed on the user's operating system and must not introduce an editor-only asset loader.
The exact font face is intentionally left to the implementation/review pass; typography size,
density and hierarchy are the locked requirements.

The visual foundation does not need a large asset-type icon catalogue before E4 exists. It must
establish an icon-capable presentation seam without inventing fake toolbar functionality merely to
fill space.

The user's P1 font review widened this into a small editor-resource foundation before the first
custom font was installed. The resulting ownership is deliberately separate from project assets and
runtime resource streaming:

- repository resources live under `editor/resources/**`;
- editor-capable build targets stage that tree beside the executable as `<target>/Editor/**`;
- the root is derived from the actual process image path, never the current working directory;
- `EditorResourceService` resolves stable typed core resource identities and validates required
  resources at startup;
- known compiled extensions may resolve contained files under `Editor/Extensions/<technical-name>/`
  without directory scanning or dynamic plugin discovery;
- no editor resource is a project `Assets/` source, DerivedData artifact, or game `DATA###.vpak`
  resource.

This takes the useful part of Unreal's Slate/AppStyle approach—stable application content roots,
separate core/editor resources, extension-local content roots and explicit staging—without adopting
Unreal's broad Content hierarchy or adding a second general-purpose asset pipeline.

### Current surfaces available for real visual review

The exact-current product already provides real persistent shell surfaces and built-in panels:

- main menu;
- main toolbar strip;
- dockspace/tabs/panel windows;
- built-in Console panel;
- built-in Background Activity panel.

`editor-demo-tool` remains ProjectSelectable by the accepted E3.7 contract. Project Cardinal does
not currently select it, and this visual-foundation work must not modify Project Cardinal policy
merely to create a showcase.

The production editor does not yet expose a normal production hierarchy/property inspector or a
scene-image panel. The user subsequently asked for deliberately made-up hierarchy/property/project
surfaces so the visual system can be judged before E4/E5 exist. That supersedes the earlier blanket
"no fake showcase" wording only for an **isolated test-owned manual-review fixture** under
`editor/tests/**`; production `VanguardEditor` remains free of fake domain tools. E4/E5 will become
the real production consumers of these visual patterns after the visual foundation is accepted.

## P0 - architecture and style lock

**Status: CLOSED; explicitly authorized 2026-09-28.**

P0 is planning/source review only. It changes no production code and runs no Premake generation,
build, executable test or native editor session.

P0 exit conditions:

- exact-current ImGui/platform/renderer/panel seams are mapped;
- CryEngine-led visual direction and non-goals are recorded;
- theme/widget/font/icon ownership is locked without replacing accepted E0/E3 architecture;
- P1 produces an actual production-editor visual candidate, not a mock-up;
- the user's P1 review and P2 final visual acceptance are explicit gates;
- P1/P2 implementation and validation scope stays narrow.

## P1 - production visual foundation and user review candidate

**Status: CLOSED - accepted enough to advance after iterative user visual review.**

When explicitly authorized, P1 should implement the smallest coherent production visual layer that
makes the existing editor visibly follow the locked direction:

1. add the private centralized theme/tokens and apply them at ImGui context initialization;
2. restyle menu, toolbar, docking tabs, panel backgrounds/chrome, popups, scrollbars and common
   controls using the central tokens;
3. add only the reusable higher-level visual helpers needed to avoid duplicated presentation logic;
4. migrate the built-in Console and Background Activity presentation to the new visual patterns
   where stock presentation prevents the target density/hierarchy;
5. preserve all existing command, panel, docking, focus, persistence, multi-viewport, DPI and
   retained-rendering ownership;
6. do not add E4 asset-browser functionality or fake hierarchy/inspector panels to production code.
   A test-owned manual visual-preview mode is explicitly permitted by the user's later P1 review
   request and must remain isolated from normal product composition.

Because the user explicitly requires visual review, P1 may produce one Development production-editor
build solely to create the review candidate. This is not permission for a regression suite or native
automation. The user opens the ordinary editor with Project Cardinal and reviews the real result.

P1 review focus:

- overall charcoal hierarchy and contrast;
- menu/toolbar height and density;
- tab/panel chrome;
- padding and control height;
- text size/weight/readability;
- border/separator subtlety;
- selection/hover/focus accent behavior;
- Console/Activity row density and control treatment;
- whether the result feels like a professional CryEngine-led DCC editor rather than styled ImGui.

P1 visual acceptance is user-owned, not agent-owned. The user accepted the P1 result enough to advance
and later explicitly authorized P2 on 2026-10-02.

### P1 implementation evidence

The production candidate now includes:

- `editor_visual_style.hpp` as the single private semantic style owner;
- theme application at the existing ImGui context-initialization boundary;
- compact charcoal menu/panel/popup/field/scrollbar/tab/tree defaults with square geometry and
  restrained blue/cyan active/selection states;
- a flat toolbar-command presentation that remains on the existing command/surface route;
- compact section/status visual helpers used by the real Console and Background Activity panels;
- tightened Console filter layout and clearer Background Activity source/status hierarchy;
- executable-relative `EditorResourceService` ownership for immutable editor fonts/icons/images/
  cursors/theme data, with typed semantic core identities and validated extension-local paths;
- `editor/resources/**` -> `<target>/Editor/**` Premake staging for editor-capable executables;
- the approved Vanguard Engine wordmark as a semantic path-only SVG image under
  `Editor/Images/Branding/VanguardEngine.svg`, with export metadata removed and no embedded raster;
- an integrated primary editor header: the editor main window is visually borderless, a muted
  VANGUARD-only treatment precedes the menu with explicit separation, project/tool title text occupies
  the draggable center, and minimize/maximize/restore/close controls occupy the right edge;
- native drag/resize/window actions remain owned by `WindowManager` and the SDL backend, while the
  canonical SVG is consumed through a narrow supersampled sanitized-wordmark rasterizer and the
  existing managed ImGui texture path; this does not establish a general SVG/icon loader or add
  another renderer;
- the main toolbar side bar is omitted when no non-anchor contribution exists, avoiding a permanent
  empty strip while keeping the declarative toolbar surface available for real tools;
- Source Sans 3 Regular as the 13 px default UI font and Source Sans 3 Semibold for stronger shared
  section headings, both bundled with the SIL OFL license.
- DPI-aware editor scaling built on the existing WindowManager/SDL content-scale path: the visual style
  is always reconstructed from immutable 100%-authored values at `userScale * viewportDpiScale`, ImGui
  retains per-viewport font/DPI ownership, and custom chrome/layout measurements share the same scale
  helper;
- a real built-in `Preferences` extension and panel under `Edit > Preferences...`. It does not belong
  to `--visual-style-preview`. The panel discovers optional presentation metadata attached to the
  existing shell typed-preference providers and provides shared category/section/search/typed-control
  rendering, so new ordinary editor preferences do not need bespoke panels or a parallel storage path.
  Presentation metadata is borrowed immutable provider-lifetime data; shared category IDs must publish
  one canonical label/order and live section IDs are unique so future providers cannot create ambiguous
  category/section identity;
- `Appearance > Interface > UI scale` as the first presented setting, persisted by the existing E3.6
  typed user-state path and offered at 75/100/125/150/175/200%. User scale multiplies current viewport
  DPI rather than overriding platform scale. Changes apply live (at the next frame boundary when edited
  during authoring); the requested user scale is frozen into one applied value per authored frame so a
  viewport/DPI transition cannot expose a half-scaled frame. Startup restore applies before UI framing.
- a production icon-toolbar foundation using the user's complete 31-icon SVG set. Canonical artwork is
  stored under `editor/resources/Images/Icons/` and resolved as required `SvgIcon` editor resources. The
  first hand-written path/arc approximation was rejected after visual review because it changed small
  glyph silhouettes. Production now vendors NanoSVG (pinned upstream commit
  `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`, zlib license), parses the untouched 24x24 currentColor
  SVGs once, and rasterizes 128x128 white-alpha runtime textures that ImGui downsamples and tints through
  the existing editor texture path. No authored raster derivative replaces the canonical SVG;
- a real toolbar `Tool` contribution type carrying icon identity, tooltip/display label and an optional
  real command. The initial requested toolbar is intentionally presentation-only: every icon is present,
  but unimplemented items register no placeholder command and therefore perform no action. When command
  functionality is reached later it binds through the existing extension-owned command/surface route;
- the production main toolbar uses a dense CryEngine-led desktop treatment: 28 logical pixels high,
  24-pixel square tool targets, 16-pixel muted monochrome glyphs, near-square geometry, thin separators,
  no persistent button fill, and restrained hover/pressed state. Functional grouping is presentation
  metadata rather than fake feature ownership.

For visual review breadth, the dedicated native-UI test executable also has an explicit
`--visual-style-preview` manual mode. It injects four **test-owned fictional panels only**:

- Hierarchy / outliner;
- Perspective scene mock viewport;
- Properties / inspector;
- Project / asset-browser mockup.

The visual-preview mode can run against an isolated scratch `.vproject`, does not select a fake
production extension, does not modify Project Cardinal, does not enable input automation, and does
not change the existing `--inspect-editor-ui` / `--validate-editor-ui` semantics.

No new service, registry, renderer, toolkit, resource loader, fake showcase panel, Project Cardinal
policy edit or E4 feature was introduced.

The explicitly permitted single production review build was executed with serialized outer MSBuild:

- `editor.vcxproj`, `Development|x64`: **PASS, 0 warnings / 0 errors**;
- output: `bin/Editor/Development/VanguardEditor.exe`.
- `editorNativeUiIntegrationTest.vcxproj`, `Development|x64` after the visual-preview fixture change:
  **PASS, 0 warnings / 0 errors**;
- review fixture output: `editor/bin/EditorTests/Development/VanguardEditorNativeUiIntegrationTest.exe`.
- after the editor-resource/font addition, Premake `vs2022` regeneration: **PASS**;
- production `editor.vcxproj`, `Development|x64`: **PASS, 0 warnings / 0 errors**;
- `editorNativeUiIntegrationTest.vcxproj`, `Development|x64`: **PASS, 0 warnings / 0 errors**;
- short production and visual-preview startup smokes launched with working directory `C:\` both
  remained alive until deliberately stopped, proving editor resources are not resolved from CWD.

No executable test suite, input-driving native automation or broad regression matrix ran in P1.

## P2 - corrections and final visual acceptance

**Status: final correction candidate complete; explicit final user visual acceptance is not separately recorded.**

P2 applies the user's P1 feedback. Expected corrections include density, proportions, palette,
typography, borders, tab/chrome treatment, icon treatment and control states; scope must remain within
the visual foundation unless the user explicitly widens it.

The concrete P1 feedback already incorporated before P2 authorization remains the production baseline:
integrated VANGUARD-only chrome with added vertical breathing room, coherent user-scale/DPI composition,
the real production Preferences foundation, the dense production icon toolbar, and the NanoSVG icon
fidelity replacement. The later request to brighten informational secondary text beyond the current
`#AFAFAF` treatment was explicitly deferred by the user and is not an automatic P2 correction.

Exact-current P2 source review found no additional required production visual correction beyond that
retained baseline. The focused `Development|x64` production editor compile/link passed on 2026-10-02,
and the ordinary Project Cardinal editor was launched for the user's manual final visual review without
input automation.

A final adversarial review then found one narrow-window chrome edge case: the prior draggable hit-test
rectangle could remain active after resize when no current drag lane existed. P2 now republishes the
current hit-test state every frame and permits resize-border-only native hit testing with an empty drag
region, preventing stale drag geometry without sacrificing border resizing. The focused production editor
build passed again after this correction.

No separate user message explicitly accepting/closing the final production-editor visual result is recorded
before the separately authorized E4 Definition. The deferred secondary-text brightening remains intentionally
outside automatic P2 scope. E4 Definition authorization does not waive or retroactively satisfy visual P2.

Final validation is intentionally narrow:

- compile/link the relevant production editor configuration after the final corrections;
- run lightweight source/static hygiene;
- launch/review the ordinary editor with Project Cardinal as needed for the user's final visual
  acceptance;
- no screenshot-pixel oracle, broad E3 regression matrix or interactive mouse/focus automation by
  default.

If physical input-driving automation is ever requested, warn the user immediately before running it.

## Non-goals

- replacing Dear ImGui with Qt or another toolkit;
- reopening accepted E3 ownership/lifecycle work;
- starting E4 Asset Browser functionality;
- adding a custom native title bar/window manager in this slice;
- creating fake **production** panels solely as a style gallery; the user-authorized test-owned
  manual visual-preview fixture is allowed during P1 review;
- building a universal UI abstraction over every ImGui call;
- changing docking/persistence identities or UserState wire formats for appearance;
- adding a second font/icon/resource-loading system;
- changing Project Cardinal's `.vproject` merely for visual demonstration;
- broad build/test/native validation during P0/P1.
