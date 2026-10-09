# Asset Browser Specification (Revised)

**Conventions**

- **P0** = required for first usable release. **P1** = expected for a mature tool. **P2** = advanced / optional.
- Keys are written for Windows/Linux. On macOS, Ctrl → Cmd, Alt → Option, Delete → Cmd+Backspace, Show in Explorer → Reveal in Finder.
- Numeric targets are **proposed budgets** to be validated on reference hardware. They replace words like "instant" and "quickly".
- "Must" = requirement. "Should" = strong default. "May" = optional.

---

## 1. Purpose and Principles

The Asset Browser is the primary interface for navigating, organizing, discovering, inspecting, creating, importing, and manipulating project content. It should feel like a mature core editor tool, not a generic file browser.

| Principle | Meaning |
| --- | --- |
| Preserve orientation | The user can always answer: where am I, what am I looking at, why am I seeing these assets, how do I get back? Persistent tree, breadcrumbs, visible filter/scope indicators, history. |
| Familiar first, clever second | Folders behave like folders, trees like trees. Double-click opens, F2 renames, Ctrl+C copies. Innovation happens around the engine's unique needs, not in basic interactions. |
| Prefer reversible actions | Move, rename, delete, and reorganize participate in undo wherever feasible. |
| Quiet chrome | Thumbnails and hierarchy dominate. Few borders, cards, shadows, floating panels. |
| Assist, never fight | No surprise navigation, no automatic reorganizing, no popups, no hiding content. |
| Scale by design | Works for 10, 10,000, and 500,000+ assets. |

---

## 2. Mental Model and Layout (P0)

- **Left panel = Navigation**: the persistent folder hierarchy. It shows *where things exist*.
- **Right panel = Contents**: folders and assets in the current location. It is the working area.
- Selecting a folder in the tree never rebuilds or replaces the tree.

```text
┌───────────────────────────────────────────────────────────────┐
│ Toolbar: nav / breadcrumbs / search / filter / sort / view     │
├───────────────────┬───────────────────────────────────────────┤
│ Favorites         │ Breadcrumbs                               │
│ Folder Tree       ├───────────────────────────────────────────┤
│ Collections       │ Asset / Folder Grid                       │
├───────────────────┴───────────────────────────────────────────┤
│ Status: counts / selection / background tasks                  │
└───────────────────────────────────────────────────────────────┘
```

- Left panel: resizable (with min/max width) and collapsible. Sizes persist across sessions.
- Toolbar priority, left to right: navigation, breadcrumb, search, filters, sort, view, overflow (⋯). Rare actions go in the overflow menu so horizontal room stays with the assets.

---

## 3. Folder Tree

### 3.1 Click behavior (P0): resolved

| Action in **tree** | Result |
| --- | --- |
| Click disclosure arrow ▶ | Toggle expand/collapse only. Selection unchanged. |
| Single-click folder name | Select folder and show its contents on the right. Expansion unchanged. |
| Double-click folder name | Toggle expansion (in addition to the selection from the first click). |
| Right/Left arrow | Expand / collapse (Left on a collapsed folder moves to parent). |
| Up/Down arrow | Move selection between visible rows. |

| Action in **content grid** | Result |
| --- | --- |
| Single-click folder | Select it (no navigation). |
| Double-click folder | Enter it (selects it in the tree and auto-reveals). |

The two panels differ on purpose: the tree exists for navigation, so one click navigates; the grid is a working area, so one click selects. Single-clicking an asset never changes directory.

### 3.2 State persistence (P0)

Remember and restore: expanded folders, selected folder, tree scroll position, panel width. State should survive editor restarts when "Remember Navigation State" is on (default on).

### 3.3 Auto-reveal (P0)

Navigating via search, breadcrumbs, asset references, "Show in Asset Browser", history, recent items, or the dependency viewer must reveal the target folder in the tree.

- Expand only the ancestors required.
- Never collapse unrelated folders.
- Scroll the target row into view.

### 3.4 Project roots (P1)

Support multiple logical roots, e.g. *Project* (Assets, Scenes, Plugins, Generated), *Engine* (Engine Content), *Packages* (Physics, Networking…). Engine and Package roots are hidden by default and toggled in browser settings. Roots from read-only sources show a lock indicator (see Appendix A.4).

### 3.5 Favorites (P0 for folders, P1 for assets)

- A Favorites section sits above the tree.
- Pin via drag-and-drop or context menu. Favorites are references; they never duplicate or move content.
- Individual assets may also be pinned (P1).
- If the target is deleted or unavailable, the entry shows a "missing" state and offers *Remove*.

---

## 4. Content Panel

### 4.1 View modes (P0)

| Mode | Purpose |
| --- | --- |
| Grid (default) | Large thumbnails, names beneath. |
| Compact Grid | Smaller icons for large folders. |
| Details / List | Columns: Name, Type, Size, Modified, Source, Status. Columns are resizable, reorderable, and show/hide-able. |

View mode persists, and is remembered per folder where practical (see 4.5).

### 4.2 Thumbnail size (P0)

Continuous slider plus Ctrl+Mouse Wheel over the content panel. Grid alignment must not depend on name length.

### 4.3 Visual rules (P0)

- Folders use a recognizable folder silhouette and never resemble asset cards.
- Long names truncate with an ellipsis; the full name appears on hover.
- States must be visually distinct: **hover** (subtle background), **selected** (stronger background/outline), **focused** (selection plus keyboard focus ring), **cut** (50% opacity).
- Selection must remain obvious when the browser loses focus (use a dimmed, but still clearly visible, selected style).
- Never rely on checkmarks or color alone.

### 4.4 Breadcrumbs and history (P0)

- Every breadcrumb segment is clickable. Segments may show a dropdown of sibling folders (P1).
- Controls: Back, Forward, Up.
- Shortcuts: Alt+Left, Alt+Right, Alt+Up (macOS: Cmd+\[ , Cmd+\] , Cmd+Up). Where the OS reserves a binding, the editor's keymap system must allow remapping.
- History stores location *and* selection where practical.

### 4.5 Navigation memory (P1)

When returning to a folder, restore scroll position, last selected asset, thumbnail size, and view mode.

### 4.6 Sorting (P0)

Sort by Name, Type, Date Modified, Date Created, File Size, Recently Used (P1); ascending/descending. Default: **Folders first, Name A–Z**. Folders-first is a setting. Name sort uses natural ordering (`Rock2` before `Rock10`) and locale-aware collation.

### 4.7 Empty states (P0)

- Empty folder: "This folder is empty. Drop files here to import, or \[Create Asset\]."
- No search results: show the active query, scope, and filters, a "Did you mean…" suggestion when available, and \[Clear Filters\].

---

## 5. Search and Filtering

### 5.1 Behavior (P0)

- Search runs as the user types (debounced), with no Enter required.
- Matches: asset name, partial name, type, tags, path, imported source name.
- Search is backed by a metadata index and never scans the disk per query (see §14).

### 5.2 Scope (P0)

Options: **Current Folder**, **Current Folder + Children** (default), **Entire Project**. The active scope is always visible next to the search field. Changing scope re-runs the search.

### 5.3 Filter chips (P0)

- Filters become visible, removable chips: `dragon [Type: Texture ×] [Path: Characters ×]`.
- Typed syntax (`type:material dragon`, `-type:texture`, `tag:boss`) converts to chips on entry.
- **Combination rules:** different chip categories combine with AND; multiple values within one category combine with OR (`Type: Texture, Material` = either); negated chips exclude.
- The type-filter list shows only asset types present in the current scope.

### 5.4 Ranking (P1)

Order: (1) exact name, (2) name prefix, (3) name contains, (4) tag, (5) path, (6) other metadata. Ties break by recency of use, then shorter path, then alphabetical. Typo correction ("Did you mean") is a suggestion only, never silently applied.

### 5.5 Context preservation (P0)

Entering and clearing a search or filter returns the user to the previous folder with approximately the same scroll position and selection. Search and filters never change the tree selection.

### 5.6 Quick navigation (P1)

Ctrl+P (or the command palette) opens a global, keyboard-only jump-to-asset/folder search. Enter navigates and reveals.

---

## 6. Selection, Keyboard, and Clipboard

### 6.1 Selection (P0)

Click = select; Ctrl+Click = toggle; Shift+Click = range; Ctrl+A = select all; Esc = clear selection. Box (marquee) selection from empty space in grid modes (P1).

### 6.2 Keyboard (P0)

| Key | Action |
| --- | --- |
| Arrow keys | Move selection (grid: spatial movement across wrapped rows; stops at edges) |
| Typing letters | Type-to-select: jumps to the first matching name (resets after \~1 s idle) |
| Enter | Open (same as double-click) |
| F2 | Rename |
| Delete | Delete (safe-delete flow) |
| Ctrl+C / X / V | Copy / Cut / Paste |
| Ctrl+D | Duplicate |
| Ctrl+F | Focus search field |
| Ctrl+Shift+N | Open Create Asset command search |
| Space | Quick Preview (see 10.4) |
| Backspace | Navigate up **only when no text field has focus**. Backspace never deletes. |
| Esc | Cancel the active operation, in this order: drag → rename → preview → search focus → selection |

All bindings are remappable. Single-key shortcuts (Space, typing) are active only when the browser has focus and no text field is editing.

### 6.3 Open behavior (P0)

| Input | Result |
| --- | --- |
| Single click | Select and update the Inspector |
| Double-click / Enter | Open the asset's dedicated editor (where one exists) |
| Middle-click | Open in a separate editor tab (P2) |

The rule is identical for all asset types unless a type has a documented reason to differ.

### 6.4 Clipboard (P0)

Cut items render at 50% opacity until pasted or cancelled. Paste into another folder completes the move. Copy creates duplicates under engine asset rules. Pasting follows the collision rules in Appendix A.1.

---

## 7. Creating and Renaming

### 7.1 Create (P0)

- Right-clicking empty space opens **Create**, with frequently used types first and a search field at the top, so the menu stays short as the engine grows.
- Ctrl+Shift+N opens a command-style search: type "material", press Enter, and the asset is created in the current folder.
- The new asset enters inline-rename mode immediately.

### 7.2 Rename (P0)

- F2, or slow double-click on the label. Rename is inline.
- The extension is not selected for editing and is not editable when the engine hides it. Native assets may not show extensions.
- Validation (invalid characters, reserved names, collisions) is shown inline before commit. Esc cancels.

### 7.3 Duplicate naming (P1)

One global, configurable convention (default `Name_01`, `Name_02`…). The naming must be predictable and consistent across duplicate, paste, and import collisions.

### 7.4 Batch rename (P1)

Operations: Prefix, Suffix, Find/Replace, Numbering. **Always shows a full before→after preview** before commit, flags collisions, and is a single undo step.

---

## 8. Drag and Drop (P0)

### 8.1 Sources and targets

Assets may be dragged into folders (tree or grid), scenes, viewports, inspectors, object properties, asset reference fields, collections, and Favorites. Folders accept assets.

### 8.2 Modifiers (consistent across the whole editor)

| Input | Result |
| --- | --- |
| Drag | Move (within the project) |
| Ctrl + Drag | Copy |
| Alt + Drag | Create reference **only when the destination type supports it**; otherwise the modifier has no effect and the drop preview shows what will actually occur |

### 8.3 Feedback and safety

- Show a drop preview naming the action and target, e.g. *"Move 4 assets to Assets/Characters/Enemies."*
- The cursor communicates outcome: create / assign / copy / blocked (🚫 with reason).
- Hovering over a collapsed tree folder for \~600 ms expands it (collapsing again afterward only those auto-expanded folders if the drop is cancelled).
- Esc cancels a drag at any time.
- Invalid targets (itself, a descendant, read-only, incompatible type) are rejected with a visible reason, not silently ignored.

### 8.4 External files (P0)

Dragging OS files onto the browser imports them. The target folder is clearly highlighted before drop. Progress appears in the status bar, not in blocking dialogs.

---

## 9. Importing and Background Tasks (P0)

- Imports run in the background; browsing continues whenever technically possible.
- The status area shows task name, count, and progress ("Importing 34 assets… 78%"), with a click-through to details and cancel.
- Failures stay visible as assets with an error badge and actionable detail (see 12.3).
- Import collisions follow Appendix A.1.

---

## 10. Thumbnails, Badges, Hover, Preview

### 10.1 Thumbnails (P0)

Meaningful per type: texture (actual image), material (rendered sphere), mesh (rendered model), scene (preview where practical), audio (waveform), script (language icon), animation (pose/icon), unknown (type icon). Generation is asynchronous. Placeholders show the type icon immediately; failed generation falls back to the type icon plus an unobtrusive error badge with *Retry* in the context menu. Very expensive thumbnails (huge meshes/scenes) may be generated on demand or at lower priority.

### 10.2 Prioritization (P0)

Visible items → highest. Near-viewport → medium. Off-screen → lowest. Priority is re-evaluated on scroll; stale work is cancelled.

### 10.3 Badges (P1)

Unsaved, Error, Reimporting, Read-only, External source, source-control state. Badges are small and secondary. A thumbnail shows at most two badges at once; overflow collapses to a single "more" indicator. Every badge needs a text equivalent (tooltip/details column), never color alone.

### 10.4 Hover info and Quick Preview

- **Hover (P1):** after a short delay (\~500 ms), show a type-aware tooltip (e.g. texture: dimensions/format/mips; sound: duration/channels/sample rate; mesh: triangle count). Tooltips are small and are suppressed during scroll and drag.
- **Quick Preview (P1):** Space toggles a preview for the selected asset (image, rotatable 3D mesh, audio playback, material sphere, animation playback, scene info). Space or Esc closes it. Preview never changes selection. Space is active only when the browser has focus and no text field is editing, so it cannot conflict with viewport shortcuts.

---

## 11. Context Menus and Asset Operations

### 11.1 Menus (P0)

- **Right-click an asset** → actions for the selection. **Right-click empty space** → actions for the current folder (Create, Paste, Import, Show in Explorer, New Folder, Refresh, Properties).
- Menus are context-sensitive and concise; irrelevant actions are hidden, rare actions live in submenus.
- Common asset actions: Open, Rename, Duplicate, Move, Copy, Delete; Show in Explorer; Copy Path / Relative Path / Full File Path / Asset ID; Find References; Find Dependencies; Reimport; Properties.
- Show in Explorer reveals the physical file itself, not merely its folder.

### 11.2 References vs dependencies (P0)

- **Find References:** what uses this asset.
- **Find Dependencies:** what this asset uses. Results are navigable lists that reveal items in the browser.

### 11.3 Safe delete (P0)

- **Unreferenced:** delete immediately with a non-modal toast and **Undo**.
- **Referenced:** dialog summarizing usage ("Used by 3 Prefabs, 1 Scene") with **\[View References\] \[Cancel\] \[Delete Anyway\]**.
- **Folders and mixed multi-selections:** aggregate the references of everything inside into one summary, never one dialog per asset.

### 11.4 Undo (P0)

Rename, move, duplicate, delete, create folder, and create asset participate in global editor undo. Multi-asset operations are one undo step. Operations that cannot be undone (e.g. permanent external changes) must say so before executing.

### 11.5 Type-aware and multi-asset actions (P1)

Examples: texture → *Create Material From Texture*; mesh → *Create Prefab*; several compatible textures → one *Create Material*. Multi-selection actions are summarized, not repeated per asset (e.g. "Reimport 42 textures", "Tag 40 assets").

### 11.6 Missing assets (P1)

Broken references offer **\[Locate\] \[Search Project\] \[Clear Reference\]**; search considers similar names and prior asset IDs.

### 11.7 Move detection (P1)

References are held by internal asset ID, not path, so in-editor moves never break them. When an external move is detected, propose **\[Reconnect\]** with old and new paths. Never reconnect automatically when confidence is low.

---

## 12. Organization Layer

### 12.1 Recent (P1)

- **Recent Assets:** opened/edited assets only, not merely displayed ones.
- **Recent Locations:** recently visited folders. Both are compact, accessible from the navigation area or a quick menu.

### 12.2 Collections (P1)

Collections group assets from any folder without moving them (they hold references). Smart Collections (P2) are query-driven (e.g. "Texture, References: 0") and update automatically; they are optional and unobtrusive.

### 12.3 Errors and validation (P1)

- Broken assets remain visible with an error badge. Selecting one shows the reason plus **\[Retry\] \[Show Source\] \[Open Import Log\]**.
- Validation warnings (e.g. texture exceeds recommended size) are inspectable in the Inspector or details view and are never modal.

### 12.4 Tags and color markers (P1/P2)

- Tags describe meaning; folders describe location. Tags complement folders.
- Color markers are a small, subtle indicator on folders or collections, optional, and always paired with a text label or tooltip.

### 12.5 Source control (P2)

Shown only when enabled. Status: Modified, Added, Locked, Conflict, Not tracked. Actions: Checkout, Revert, View Diff, History.

### 12.6 Where user data lives (decision required: proposed defaults)

| Data | Proposed scope |
| --- | --- |
| Tree state, panel sizes, view mode, sort, thumbnail size, recent items, Favorites | **Per user**, stored locally, not source-controlled |
| Tags, shared collections, color markers on shared content | **Per project**, stored in a diff-friendly, merge-friendly metadata file under version control |
| Personal collections | Per user |

Conflicts between concurrent edits to shared metadata must be resolvable without data loss.

---

## 13. Advanced Workflows (P2)

- **Split view:** "Open in Split View" on a folder shows two content panes side by side. Not the default. Drag-and-drop works between panes. Each pane has its own location and selection.
- **Focus View:** temporarily isolates a folder or search result ("Focus: Characters/Dragon") without altering the tree. Esc or *Exit Focus* restores the exact prior state. Interaction rule: Focus sets the effective scope; the scope indicator displays it; search within Focus searches within it.
- **Multiple browser instances:** independent navigation, search, selection, and view mode per instance; locations persist with the workspace. Browser tabs represent *locations*; asset-editor tabs represent *documents*, and the two must never be visually conflated.
- **Command palette integration (P1):** every browser action is exposed as an editor command.

---

## 14. Performance and Asynchrony

All operations below are **proposed budgets** on reference hardware with a warm index.

| Scenario | Target |
| --- | --- |
| Open a folder of 20,000 assets: first visible items | ≤ 100 ms |
| Scrolling | 60 fps, no hitches from thumbnail/metadata work |
| Search: first results at 500,000 indexed assets | ≤ 50 ms after the last keystroke |
| Cached thumbnails for visible items after scroll stops | ≤ 300 ms |
| Any single UI-thread task | ≤ 16 ms |
| Reveal-in-browser from anywhere | Item visible and selected ≤ 250 ms |

Requirements (P0):

- **Virtualize** the grid, list, and tree; create UI elements only for visible and near-visible entries.
- **Load incrementally**: show visible entries immediately, then populate metadata and thumbnails progressively.
- Run these asynchronously: scanning, metadata loading, thumbnails, importing, asset and search indexing, dependency scanning.
- Search reads a dedicated, incrementally updated metadata index.
- Filesystem changes are detected automatically. A manual Refresh exists for recovery only.
- Background indexing shows subtle status ("Indexing assets… 1,423 / 8,912") and never blocks browsing.
- Status bar example: "248 assets | 4 selected | 82.6 MB".

---

## 15. Appendix A: Behavior Rules for Edge Cases (P0)

**A.1 Name collisions** (move, paste, rename, import)

- Rename: reject with an inline message. Case-only renames on case-insensitive filesystems must succeed.
- Move/paste/import: show one non-blocking summary prompt for the whole operation with **Keep Both (auto-rename)**, **Replace**, **Skip**, with an "apply to all" option. Replace is never the default and is undoable.

**A.2 Invalid moves** A folder cannot be moved or copied into itself or a descendant. The drop target is rejected with a visible reason.

**A.3 Deleting or moving referenced content** In-editor moves and renames never break references (ID-based). Deleting follows 11.3. Open editors, scene instances, and Inspector targets for a deleted asset must update to a clear "missing" state rather than crash or silently vanish.

**A.4 Read-only, locked, and cross-root operations**

- Read-only or locked items: Rename, Move, and Delete are disabled with an explanatory tooltip; Copy/Duplicate into writable locations remains allowed.
- Dropping project content into read-only roots (Engine, Packages) is blocked with a reason.
- Moving between roots is a copy-then-confirm operation if it crosses a boundary with different ownership rules.

**A.5 External changes during editing** If an item changes on disk while it is being renamed or dragged, finish or cancel the user's operation first, then reconcile and notify non-modally.

**A.6 Selection after operations** After Delete, select the nearest remaining neighbour. After Move, the destination is not entered unless the user navigated there. After Create, the new asset is selected and in rename mode.

---

## 16. Appendix B: Accessibility, Localization, Platform (P0 unless noted)

- Full keyboard operation of tree, grid, list, menus, and dialogs, with a defined focus order: toolbar → tree → breadcrumbs → content → status.
- Screen-reader semantics: tree (`tree/treeitem`, expanded state), grid/list (`grid`/`row`/`cell`), and live-region announcements for operations, search result counts, and import progress.
- Color is never the only carrier of meaning (badges, markers, selection). Respect high-contrast modes.
- Respect reduced-motion preferences (e.g. replace the reveal "highlight pulse" with a static highlight).
- Minimum hit targets for touch and pen on tablets or hybrid devices (P1).
- Localization: layouts handle long names and RTL; sorting uses locale-aware collation.
- HiDPI and mixed-DPI displays: thumbnails render crisply at any scale.

---

## 17. Appendix C: Validation of "Intuitive" (P0)

**Usability scenarios** (new users, no help, timed, with observed error counts):

1. Find "Dragon" material and open it.
2. Move 8 materials into a new subfolder via drag, then undo.
3. Rename a texture without touching its extension.
4. Delete a referenced material and recover from the warning.
5. Reveal an asset from an Inspector reference and return to where you were.
6. Import 30 files and keep browsing during the import.
7. Filter to Textures with 4096+ resolution within a deep folder.
8. Recover a broken (missing) reference.

**Technical validation:** automated tests on reference projects of 10 / 10,000 / 500,000 assets with files per folder up to 20,000; tests for each Appendix A rule; screen-reader and keyboard-only passes. **Telemetry (opt-in):** use frequency of features, search success rate, and undo-after-move rate, to guide P1/P2 priorities.

---

## 18. Open Decisions

1. Alt+Drag "reference": confirm the semantics of a reference in the engine (alias asset? link in a collection?).
2. Final scope of shared vs per-user metadata (12.6) and its file format.
3. Whether quick preview is hold-to-show or toggle (this spec says toggle).
4. Default key bindings per platform for navigation shortcuts that collide with OS or window-manager keys.
5. Reference hardware and dataset definitions for the §14 budgets.

---

## 19. Closing Principle

Innovation belongs in what is unique to the engine: dependency awareness, safe refactoring, type-aware workflows, and intelligent recovery. It does not belong in basic navigation. The Asset Browser should make users faster and more confident, and should never make them wonder what just happened.