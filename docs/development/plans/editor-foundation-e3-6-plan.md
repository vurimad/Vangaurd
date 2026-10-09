# E3.6 - layout and user-preference persistence

## Status and authorization

E3.1 through E3.5 are accepted. On 2026-09-27 the user explicitly authorized
**E3.6 definition work only**. The definition is now **CLOSED** after all review findings
were corrected and fresh post-fix independent architecture/lifecycle and adversarial/test/docs
reviews each returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. That authorization covered
exact-CURRENT architecture study, documentation/planning, bounded contract design,
static/document hygiene and independent definition review only.

On 2026-09-27 the user subsequently authorized **E3.6 P0 only**. P0 is now **CLOSED** after
the exact-CURRENT architecture/format/test-shape contract was frozen, the first adversarial
findings were corrected, fresh independent architecture/lifecycle/ownership and post-fix
adversarial format/test/docs reviews returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, and final
static/document hygiene passed. The user subsequently authorized **E3.6 P1 only** on
2026-09-27. P1 is now **CLOSED** after the bounded document/store + service lifecycle core,
focused source oracles, final scoped static hygiene and fresh exact-current independent
architecture/lifecycle/source-static and adversarial codec/store/test reviews each returned
**0 BLOCKER / 0 HIGH / 0 MEDIUM**. No Premake generation, compilation, executable test or
native editor run occurred in P1. The user subsequently authorized **E3.6 P2 only** on
2026-09-27. P2 is now **CLOSED** after the panel inventory/private UI persistence mechanisms,
bounded validated PNLS/UIST staging, focused source oracles, final scoped hygiene and fresh
exact-current independent architecture/lifecycle/source-static and adversarial/source-oracle
reviews each returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No Premake generation, compilation,
executable test or native editor run occurred in P2. At P2 closure, production panel/UI
application remained deliberately dormant until P3 could satisfy the frozen whole-envelope
provider preflight.
The user subsequently authorized **E3.6 P3 only** on 2026-09-27. P3 is now **CLOSED** after
transactional shortcut persistence, Shell-owned typed preferences including the Activity Console
provider, the whole-envelope semantic preflight/fixed owner-apply transaction, bounded exact-key
orphan merge and dirty/final-persistence integration were completed with focused source oracles,
scoped static hygiene and fresh exact-current independent architecture/lifecycle/source-static
and adversarial/source-oracle reviews each at **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No Premake
generation, compilation, executable test or native editor run occurred in P3.
The user subsequently authorized **E3.6 P4 only** on 2026-09-28. P4 is now **CLOSED** after
lifecycle/recovery hardening, focused source oracles, final scoped hygiene and fresh exact-current
independent architecture/lifecycle/source-static and adversarial/source-oracle reviews each
returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**. No Premake generation, compilation, executable test
or native editor run occurred in P4. The user subsequently authorized **E3.6 P5 only** on
2026-09-28. P5 is now **CLOSED** after production-shaped persistence fixture/native-oracle
preparation, scoped source/static hygiene and fresh exact-current independent architecture/
lifecycle/source-static and adversarial/acceptance-oracle reviews each returned **0 BLOCKER /
0 HIGH / 0 MEDIUM**. No P5 Premake generation, compilation, executable test or native editor
automation was run. The user subsequently authorized **E3.6 P6 only** on 2026-09-28 and corrected
the final-gate validation rule. P6 is now **CLOSED** after focused compile/link plus the required
UserState and noninteractive persistence proof, and **E3.6 is accepted**. E3.7 remains
unauthorized/unstarted.

The definition triplet is
`.planning/e3-6-layout-user-preferences/{task_plan,findings,progress}.md`.

## Objective

Make the accepted E3 shell remember a user's project-local editor state without turning
that state into authored project content or creating a second owner for panels, docking,
commands, windows or filesystem transactions.

E3.6 persists and restores:

- the bounded set of current editor panel instances and their open/closed state;
- the private Dear ImGui docking/floating layout required to reconstruct panel placement;
- detached-host placement hints sufficient to recover safely across monitor/DPI changes;
- explicit E3.2 shortcut overrides; and
- bounded owner-scoped shell preferences explicitly deferred by accepted earlier slices,
  beginning with E3.5 Console level/category/text filters.

E3.6 does **not** persist authored documents, asset state, project policy, extension
selection, theme/styling, recent files, content selection, viewport camera state or
runtime/rendering configuration.

## Read-first authority

Before any E3.6 gate, re-read:

- root `AGENTS.md`, `editor/README.md` and `editor/tests/README.md`;
- [development document map](../README.md);
- [editor foundation roadmap](editor-foundation-study-and-roadmap.md);
- [editor UI integration contract](../designs/editor-ui-integration.md);
- [E3 parent plan](editor-foundation-e3-plan.md);
- accepted [E3.1](editor-foundation-e3-1-plan.md),
  [E3.2](editor-foundation-e3-2-plan.md),
  [E3.3](editor-foundation-e3-3-plan.md),
  [E3.4](editor-foundation-e3-4-plan.md) and
  [E3.5](editor-foundation-e3-5-plan.md) contracts;
- exact-current `EditorUiService`, private ImGui platform adapter, panel registry,
  command service, shell service and editor composition;
- exact-current Project Workspace, Window Manager display-topology and Filesystem
  publication contracts; and
- the repository-vendored RED filesystem behavior used by Vanguard.

The external RED tree named by repository guidance was outside the definition file-read
boundary, so definition evidence used exact vendored RED filesystem source plus exact-current
Vanguard code. During P0 one scoped terminal read of that tree became available and confirmed
supporting lifecycle precedent: one editor-settings owner initializes after broader editor
composition, explicit changes trigger save, and exit performs a final save before settings
shutdown. Vanguard does not import RED's file format/config store/layout machinery from that
precedent.

## Exact-CURRENT starting facts

1. `ProjectWorkspaceService::GetSavedRoot()` resolves the validated project's
   `Saved/` directory. The roadmap defines `Saved/` as project-local, non-authoritative
   user/recovery/log state. `Config/` and `.vproject` remain authored policy.
2. `EditorUiService` is the one UI/native-host owner. Its private ImGui adapter owns the
   one ImGui context and currently sets `io.IniFilename = nullptr`, so Dear ImGui performs
   no uncontrolled cwd-relative `imgui.ini` disk IO.
3. Dear ImGui already exposes manual `LoadIniSettingsFromMemory`,
   `SaveIniSettingsToMemory` and `WantSaveIniSettings`. Its private settings encode
   docking/window topology, positions, sizes, viewport IDs and dock IDs. Those bytes are
   toolkit state, not Vanguard project authority.
4. E3.3 panel windows use a stable internal ImGui identity derived from
   `EditorPanelTypeId + EditorPanelInstanceId` behind `###`, independent of display
   title changes.
5. `EditorPanelRegistry` is bounded to the accepted panel capacities, can enumerate
   current instances, create exact instances, change open state and create the accepted
   missing-type placeholder. Placeholders preserve stable unresolved type/instance IDs
   and invoke no missing extension callback.
6. E3.2 is the sole shortcut owner. It already validates transactional
   `Default / Unbound / Custom` runtime overrides, physical-key chords, same-context
   conflicts, main-thread affinity and stale registrations. E3.2 intentionally owns no
   disk persistence.
7. Runtime `EditorHostWindowHandle`, `window::WindowHandle` and
   `window::DisplayHandle` are generational process-lifetime handles and are **not**
   persistent identities.
8. Window Manager exposes at most `MaximumDisplays` current `DisplaySnapshot` records
   with current bounds/work area, desktop pixel extent, content scale and a runtime
   fingerprint. That fingerprint is useful for **within-process topology** continuity,
   but the SDL backend hashes `SDL_DisplayID` into it, so it is not a durable
   cross-process/restart monitor identity and must not be persisted as one.
9. The ImGui platform adapter currently forwards requested detached viewport position and
   size directly into explicit Window Manager placement. Persisted placement therefore
   needs a restore-time normalization/clamp before a detached native host becomes visible.
10. The current primary dock builder is a first-use fallback. Only a persisted layout restore
    that the private UI bridge validates as containing a usable primary Vanguard dockspace/root
    suppresses rebuilding/splitting the default layout over the restored dock tree.
11. `filesystem::ReplaceFile` publishes a complete same-directory sibling staging file
    without deleting the old target first and leaves the staged file available on failure.
    This is the preferred publication primitive.
12. RED `FOF_SafeWrite` is **not** the E3.6 publication contract: its preserved
    `BufferedTempFile` implementation opens the destination writer first and later copies
    temporary contents into it. E3.6 requires the stronger sibling-stage/validate/replace
    behavior already used by Vanguard persistence code.
13. EngineHost starts services in dependency order and quiesces/drains/stops/shuts down in
    reverse dependency order. A user-state service depending on Frame Pipeline, Filesystem,
    Workspace, UI, Commands and Shell can restore after those owners are live and can capture
    final state before any of them tears down.
14. E3.5 is accepted. E3.6 must not reopen diagnostics/activity ownership or add another
    logger/archive merely to report preference failures. E3.5 intentionally left Console
    level/category/text filters transient for E3.6; those values currently live in
    extension-private `ActivityPresentationState` and disappear at extension deactivation.

## Non-negotiable ownership model

### One workspace-scoped user-state owner

E3.6 introduces one main-thread `EditorUserStateService` in Editor/Tool profiles. It is
the sole owner of editor preference-file parsing, migration, restore decisions, dirty
coalescing and complete-file publication.

It depends explicitly on:

- the existing Frame Pipeline for one bounded dirty/autosave participant;
- the existing Filesystem service for physical user-state IO/publication;
- `ProjectWorkspaceService`;
- `EditorUiService`;
- `EditorCommandService`; and
- `EditorShellService`.

The dependency edges, not registration order or numeric IDs, establish lifecycle order.
The service owns **persistence orchestration only**. It does not own:

- the ImGui context, dock nodes, native windows or renderer;
- panel type/instance lifetime rules;
- command registration or shortcut conflict rules;
- extension activation/deactivation;
- project configuration or authored transactions; or
- a worker/event loop of its own.

### Existing owners remain authoritative

- UI/private ImGui code remains the only code allowed to translate a bounded layout payload
  to/from Dear ImGui.
- `EditorPanelRegistry` remains the only panel instance/topology owner.
- `EditorCommandService` remains the only shortcut validator/conflict arbiter.
- Window Manager remains the only monitor/window-placement owner.
- Filesystem remains the only physical-file owner.
- Shell remains the only extension activation/registration owner.

E3.6 may request bounded operations from those owners through narrow APIs; it must not
peek into their storage or duplicate their registries.

### Owner-scoped shell-preference providers

E3.6 does not introduce a mutable global “preference bag” and does not retain pointers to
extension-private state. Extensions that own deliberately persistent shell preferences
register one bounded **typed preference provider** through the existing Shell activation/
owned-registration path.

The Shell ledger owns provider lifetime exactly like its other extension-owned callbacks:

- provider identity is the stable pair `{EditorExtensionId, providerId}`; `providerId`
  is a stable `u64` chosen by that extension and need not be globally unique across
  unrelated extensions;
- fields have stable `u64` field IDs and a small frozen type set (boolean, signed integer,
  unsigned bit-mask and bounded UTF-8 string in version 1);
- provider callbacks only copy/validate those typed values to/from extension-private state;
- no arbitrary byte blob, pointer, toolkit object or backend object is accepted;
- the provider can mark its exact owner-scoped preference generation dirty after a real
  user mutation;
- Shell revoke makes the provider unreachable before extension private state is destroyed.

`EditorUserStateService` depends on Shell, so it applies provider values only after
activation and captures them only before Shell quiesce. It never invokes a provider after
revoke. Missing provider/field IDs remain bounded orphan preferences keyed by the exact
durable `{EditorExtensionId, providerId, fieldId}` triple; they reserve no live callback.

The first concrete provider is the accepted E3.5 Activity presentation owner:

- six diagnostic-level toggles are persisted as a validated mask;
- category is `-1` or a current diagnostics category value; and
- text is copied as bounded UTF-8 with the existing 128-byte storage limit.

## Persistent state authority and path

E3.6 owns **one versioned complete editor-user-state document** beneath the current
project's `Saved/Editor/` root. P0 freezes the literal filename, magic and extension in the
exact contract below before implementation.

The document is:

- non-authoritative, local user state;
- scoped to the exact project identity even though it already lives beneath that project;
- never written to `.vproject`, `Config/`, `Assets/`, `DerivedData/` or
  `Intermediate/`;
- never treated as an E1/E2 asset/source transaction;
- never required by Runtime/Headless/Shipping products; and
- safe to delete: deletion returns the editor to defaults without changing authored data.

The first implementation has no account/OS-user identity layer. “User state” means local
state for this project workspace under `Saved/`, matching the repository's existing
project-layout contract. Introducing a roaming account/profile store is outside E3.6.

## Versioned document contract

P0 freezes a concrete binary framing using existing Vanguard serialization primitives.
The definition requires at least:

- fixed magic/domain;
- major + minor schema version;
- project identity;
- total declared byte size;
- counts/lengths for every section;
- reserved fields that must be zero until defined; and
- an integrity digest over the serialized payload.

The initial document has four bounded logical sections:

1. **Panel inventory**
   - stable `EditorPanelTypeId`;
   - stable `EditorPanelInstanceId`;
   - open/closed state;
   - bounded fallback display title used only if a missing type becomes a placeholder.
2. **Private UI layout**
   - bounded opaque ImGui settings bytes captured by the private UI owner;
   - a bounded **saved display-topology description** containing no runtime handles:
     saved work-area/bounds geometry, desktop-pixel extent, content scale and primary hint.
     It exists only so private UI restore can map yesterday's floating coordinates onto
     today's current displays.
3. **Shortcut overrides**
   - stable `EditorCommandContextTypeId + EditorCommandId`;
   - only explicit `Unbound` or `Custom` preferences;
   - validated physical key + logical modifier bits for Custom.
4. **Typed shell preferences**
   - stable owning `EditorExtensionId` + provider ID + field ID + frozen value type;
   - bounded scalar/string payload only;
   - no arbitrary extension-private blob.

`Default` is represented by absence of a persisted shortcut record. Runtime array slots,
generation handles, pointers, localized labels, ImGui window display names and native
window/display handles are never persisted as Vanguard authority.

The private ImGui bytes may naturally contain toolkit dock/viewport IDs. They remain
opaque toolkit data inside the versioned section and may not be used as public Vanguard
host identity.

## Initial hard bounds

P0 may tighten but must not silently enlarge these definition bounds:

- complete user-state file: **1 MiB** maximum;
- private ImGui settings bytes: **512 KiB** maximum;
- panel inventory: at most `MaximumEditorPanelInstances` records;
- saved display-topology records: at most `window::MaximumDisplays`;
- persisted shortcut records: **2048** maximum, allowing the current bounded live handler
  set plus bounded orphaned preferences from temporarily missing extensions;
- typed shell-preference records: **512** maximum across at most
  `MaximumEditorExtensions` providers;
- one typed shell-preference string: **256 bytes** maximum, with a provider free to impose
  a smaller accepted field bound such as the Console's existing 128-byte text buffer;
- fallback panel title: at most the existing panel-title/name bound; and
- every count/byte multiplication must be overflow-checked before allocation/read.

No unbounded strings, vectors, history, recovery copies or per-frame append log are part
of E3.6.

## P0 exact-current frozen architecture and format contract

**Status: CLOSED.** The literals and algorithms in this section are the P0 implementation
contract. Closure does not authorize P1 production edits.

### Service, capability and frame identities

P0 reserves the following non-colliding editor identities after an exact-current scan of
the existing editor service/frame ID set:

- `EditorUserStateServiceId = 0x6564757374737601ull`;
- `EditorUserStateCapabilityId = 0x6564757374636101ull`;
- `EditorUserStateFrameParticipantId = 0x6564757374667201ull`.

The service descriptor is frozen as:

- technical service name: `"editorUserState"`;
- profiles: `Editor | Tool`;
- scope: `EditorWorkspace`;
- affinity: `MainThread`;
- required dependencies, all explicit: `engine::FramePipelineServiceId`,
  `engine::FilesystemServiceId`, `ProjectWorkspaceServiceId`, `EditorUiServiceId`,
  `EditorCommandServiceId`, `EditorShellServiceId`;
- provides: `EditorUserStateCapabilityId`.

The future editor composition registers it immediately after `RegisterEditorShellService`.
EngineHost dependency sorting remains authoritative: the dependency set, not source-call
proximity, proves that UserState starts after every owner and quiesces before every owner.

The frame participant is named `"editorUserState.persist"`, runs on the main thread in
`FramePhase::EndFrame`, and has explicit `after` edges to
`EditorUiRenderParticipantId` and `EditorCommandShortcutFrameParticipantId`. The latter is
an earlier-phase edge but is retained deliberately as executable documentation of the
complete UI/shortcut observation boundary. No E3.6 participant runs before the editor UI
frame or supplies a second frame loop.

### Canonical path and sibling stage identity

The canonical path is exactly:

`<ProjectWorkspaceService::GetSavedRoot()>/Editor/user-state.vstate`

The literal extension is `.vstate`. `Saved/Editor` is created through the existing
Filesystem manager only when publication needs it; an absent `Saved/Editor`/canonical
file at load is the normal `Missing` case. Failure to create/open the directory or stage
during publication is `StageOpenFailed` and does not alter an existing canonical target.

One service activation obtains a 128-bit session nonce from `crypto::SecureRandom`; tests
inject that nonce rather than using randomness. Every publication consumes a non-wrapping
`u64` save serial starting at one. The sibling stage basename is exactly:

`user-state.vstate.stage-<32 lowercase hex nonce>-<16 lowercase hex saveSerial>`

The nonce is generated only on a fresh service activation; the serial is never reused in
that activation. A nonce-generation failure disables publication for that activation and is
reported as the first stage-open/publication failure rather than falling back to a weak
timestamp.

P0 additionally freezes one narrow **Filesystem-owned atomic create-new write seam** because
exact-current `Manager::CreateFileWriter` / `RawFileWriter::Create` reaches
`eOpenFlag_WriteNew` and Win32 `CREATE_ALWAYS`, which truncates an existing file. A
`FileExist` precheck would be TOCTOU and is forbidden. The new Vanguard Filesystem API is:

`ExclusiveFileWriteResult WriteExclusiveFile(const AbsolutePath& path, const void* bytes,
usize byteCount) noexcept`

with exact result values `Success`, `AlreadyExists`, `OpenFailed`, `WriteFailed`,
`FlushFailed`, `UnsupportedPlatform`. Its contract is: atomically create the path only if it
does not exist; never open/truncate/replace an existing path; write exactly the supplied
complete bounded image; flush durable OS-visible file data; close before returning `Success`;
and, after a successful exclusive create, leave any partial owned file in place on write/
flush failure so the caller can remove only that exact owned stage. `AlreadyExists` proves
that the caller never acquired ownership of that path.

UserState maps `AlreadyExists`/`OpenFailed`/`UnsupportedPlatform` to `StageOpenFailed`,
`WriteFailed` to `StageWriteFailed`, and `FlushFailed` to `StageFlushFailed`, preserving the
more specific Filesystem result in the stable failure context. If the exact nonce+serial
candidate unexpectedly already exists, that publication attempt therefore fails without
touching it; it is never deleted as "ours". Two concurrent editors can race on the same
candidate name safely because only one atomic create-new can acquire it. Stages are never
scanned for recovery. Stage ownership is exact by write result: `Success`, `WriteFailed` and
`FlushFailed` mean this call atomically created the path and the caller may remove that exact
owned file on later failure; `AlreadyExists`, `OpenFailed` and `UnsupportedPlatform` mean no
stage ownership and cleanup must not touch that candidate.

The closed definition also requires a canonical read/open failure to be distinguished from a
**positively confirmed missing path**, which exact-current `CreateFileReader`'s nullable return
cannot express. P0 therefore freezes the companion Filesystem primitive:

`CompleteFileReadResult ReadCompleteFile(const AbsolutePath& path, void* destination,
usize capacity, usize& byteCount) noexcept`

with exact results `Success`, `Missing`, `OpenFailed`, `ReadFailed`, `LimitExceeded`,
`UnsupportedPlatform`. `Missing` is returned only when the underlying platform reports an
unambiguous file/path-not-found condition; permission/sharing/device/other open failures are
`OpenFailed`, never guessed to be missing. `Success` copies one complete file into the caller
buffer and sets the exact byte count; a file larger than capacity is `LimitExceeded` without a
partial success. UserState maps canonical `Missing` to its normal writable Missing state,
canonical open/read/unsupported failure to `IoFailure` + write inhibition, canonical
`LimitExceeded` to `RejectedBounds` + write inhibition, and uses the same read primitive for
staged reopen where any non-Success result is `StageValidationFailed`.

### Outer document framing

The file reuses the existing Vanguard `serialization::DocumentHeader` and
`SectionDescriptor` wire format; E3.6 does not introduce a second generic binary container.
All scalar fields are written through `serialization::BinaryWriter`, therefore use the
serialization module's little-endian encoding.

The V1 envelope is frozen as:

- magic: `serialization::MakeFourCC('V', 'U', 'S', 'R')` (`VUSR`);
- document schema: major `1`, minor `0`;
- `serialization::ReadLimits` for this document are exactly `maximumFileSize = 1,048,576`,
  `maximumSections = 5`, `maximumAlignmentLog2 = 3`; section-specific count/byte bounds are
  checked before any destination allocation or owner lookup;
- `DocumentHeader::EncodingVersion == 1`, little-endian marker `1`, and header size exactly
  `40` bytes;
- document flags exactly `HasEditorData | Deterministic` (`0x0003`); any other document
  flag bit is unsupported V1;
- physical section count exactly `5`;
- each section uses `SectionFlags::EditorOnly` (`0x00000002`), `Codec::None` (`0`), and
  `alignmentLog2 == 3` (8-byte alignment); any other section flags/codec/alignment are
  unsupported V1;
- sections appear in the exact order `META`, `PNLS`, `UIST`, `SHCT`, `PREF` and section
  table entries use the same order;
- every section version is exactly `1.0`;
- section bodies are placed after the 40-byte header, each body offset aligned to eight;
  alignment padding is zero-filled and validated as zero;
- the section table follows the final aligned section body and consists of exactly five
  48-byte existing `SectionDescriptor` records;
- a 32-byte SHA-256 trailer immediately follows the section table and is the final bytes of
  the file; `header.fileSize` includes this trailer;
- the SHA-256 is computed over every byte from file offset zero through the end of the
  section table, excluding only the 32-byte digest trailer. This covers header/header CRC,
  zero padding, every section body, and every section descriptor without a circular digest
  field;
- every section's existing `storedCrc64` is also validated over its exact stored bytes;
- every section-specific parser must consume exactly `logicalSize == storedSize`; any
  unconsumed payload suffix is `RejectedCorrupt`. Consequently `META` is exactly 32 bytes,
  `SHCT` is exactly `8 + recordCount * 32` bytes, `UIST` is exactly
  `16 + displayCount * 48 + imguiByteCount` bytes, and the variable `PNLS`/`PREF` readers
  must end exactly at their section boundary after validated zero record padding;
- `header.sectionTableOffset + 5 * SectionDescriptor::WireSize + 32 ==
  header.fileSize` must hold exactly; unclaimed trailing/interstitial bytes are invalid;
- total `header.fileSize <= 1 MiB`.

The five physical section IDs are the literal FourCCs `META`, `PNLS`, `UIST`, `SHCT` and
`PREF`. `META` is envelope metadata; the other four correspond exactly to the four logical
state sections already defined above. Unknown/missing/duplicate/reordered sections,
nonzero existing serialization reserved fields, unexpected known-standard flags, or unknown
flag bits are unsupported V1 and map to `RejectedFuture` with write inhibition. Malformed
layout/count/value/CRC/SHA evidence maps to `RejectedCorrupt` unless it is specifically a
hard-bound violation (`RejectedBounds`). A bad final SHA-256 is `RejectedCorrupt`.

`1.0` is the first durable production schema, so the initial production migration table is
empty. The migration dispatcher accepts only exact `1.0` until a later schema explicitly
adds an older-version decoder and migration. Focused tests exercise the dispatcher through
an injected codec-local known-old migration fixture; that fixture is not an accepted product
wire version and is not a product test mode. Higher major or minor, and any otherwise
unrecognized production schema, are unsupported and write-inhibit.

### `META` envelope metadata

`META` is a fixed 32-byte body:

1. `u64 domain = 0x6564757374617465ull` (`edustate` domain separation);
2. four `u32` values containing `projects::ProjectId::parts[0..3]` in array order;
3. `u32 reserved0 = 0`;
4. `u32 reserved1 = 0`.

The project identity is the semantic four-part `ProjectId`, not its path/name/text spelling
and not its host-memory byte layout. Equality is exact `ProjectId` equality. This mirrors the
existing `.vproject` parser's stable 128-bit identity while leaving the user-state wire fully
little-endian and explicit.

### `PNLS` panel inventory wire

The section begins with `u32 recordCount` and `u32 reserved = 0`. `recordCount` is at most
`MaximumEditorPanelInstances` (512). Records are sorted by ascending
`EditorPanelInstanceId`, then `EditorPanelTypeId`, and duplicate instance IDs are corrupt
because panel instance identity is globally resolved by the current registry.

Each record is:

- `u64 typeId`;
- `u64 instanceId`;
- `u16 fallbackTitleByteCount`;
- `u8 open`, exactly `0` or `1`;
- `u8 reserved8 = 0`;
- `u32 reserved32 = 0`;
- exactly `fallbackTitleByteCount` title bytes, followed by zero padding to the next
  8-byte record boundary.

Both stable IDs must be nonzero. `fallbackTitleByteCount` is `1..127`, matching the current
`MaximumEditorPanelNameBytes == 128` C-string contract; the stored bytes contain no NUL and
are reconstructed with a terminating NUL before calling the existing registry. The title is
fallback presentation only and never replaces a current registered title/type identity.

### `UIST` private UI/topology wire and dock-root rule

The section header is four `u32` values: `displayCount`, `imguiByteCount`, `reserved0 = 0`,
`reserved1 = 0`. `displayCount` is `1..window::MaximumDisplays` (32) in a captured file and
`imguiByteCount <= 512 KiB`. The body then contains exactly `displayCount` fixed 48-byte
saved-display records followed immediately by exactly `imguiByteCount` opaque ImGui settings
bytes. No runtime display/window handle, topology revision or display fingerprint is stored.

Each saved-display record is:

- bounds: `i32 x`, `i32 y`, `u32 width`, `u32 height`;
- work area: `i32 x`, `i32 y`, `u32 width`, `u32 height`;
- desktop pixel extent: `u32 width`, `u32 height`;
- content scale encoded as unsigned Q16.16 `u32` (`round(scale * 65536)`), with exact wire
  range `1..0x00100000` corresponding to `(0, 16]`, the exact-current Window Manager scale
  domain;
- `u8 primary`, exactly `0` or `1`;
- three reserved zero bytes.

Bounds/work/pixel extents must be nonzero, capture accepts only exact-current Window Manager
scale `(0,16]` values whose Q16.16 rounding remains in `1..0x00100000`, and exactly one saved
display is primary. A current scale that would quantize to zero fails capture rather than
inventing a persistent value. The decoder rejects raw Q16.16 zero or `>0x00100000` as an
invalid value. Records are
serialized in canonical order: primary first, then lexicographically by bounds
`x,y,width,height`, work area `x,y,width,height`, desktop pixel `width,height`, then Q16.16
scale. Enumeration order and `DisplayFingerprint` are never an oracle.

P0 reserves the private ImGui dock root literal
`EditorPrimaryDockspaceId = 0x5647444Bu`. The E3.6 implementation changes the existing
`DockSpaceOverViewport(0, ...)` call to pass this explicit ID; it remains private to the UI
implementation. After `ImGui::LoadIniSettingsFromMemory` and before the first `NewFrame`,
the private bridge reports a usable restored root only when:

- `ImGui::DockBuilderGetNode(EditorPrimaryDockspaceId)` exists;
- that node is both a dockspace and a root node; and
- `ImGui::DockBuilderGetCentralNode(EditorPrimaryDockspaceId)` exists and belongs to that
  same root.

Dear ImGui's docking settings handler rebuilds runtime nodes during its settings `ApplyAll`,
so this validation is available immediately after the memory load. Only success sets the
existing UI owner's `m_defaultDockLayoutInitialized` equivalent. On failure the private
bridge calls `ImGui::ClearIniSettings()`, clears the manual dirty indication created by the
attempt, and leaves the accepted E3.3 default dock builder unsatisfied. Unknown ImGui window
entries remain inert private data and cannot author panels/extensions/commands.

The exact owner-facing seam is a new private
`detail::EditorUiPersistencePrivateService : EditorUiService`, implemented by the existing
managed UI service and reached only by trusted editor code through the existing UI capability.
It exposes no ImGui type. Its P0-locked operations are:

- `LoadUserStateLayout(bytes, savedDisplays, repairedPlacementCount)` — one pre-frame load,
  placement repair and dock-root validation returning `AppliedUsable`, `DiscardedUnusable`,
  `EnvironmentUnavailable` or `InvalidArgument`;
- `CaptureUserStateLayout(destination, byteCount, savedDisplays, displayCount)` — one bounded
  no-frame capture of the complete manual ImGui settings bytes plus current topology;
- `ConsumeUserStateLayoutDirty()` — returns and clears only the observed manual ImGui dirty
  signal (`io.WantSaveIniSettings`);
- `IsUserStateLayoutSettling()` — reports whether Dear ImGui's private
  `SettingsDirtyTimer > 0`, without changing that timer; and
- `GetPanelUserStateRevision()` — the current panel registry persistence revision without
  exposing registry slots/handles as durable state.

`LoadUserStateLayout` is legal only after UI/platform initialization and before any first
`NewFrame`; capture/dirty/settling observation are legal only while UI is live with no ImGui
authoring frame active. Wrong thread/phase returns a bounded failure and never touches runtime
state. `io.IniFilename` remains null. Exact-current Dear ImGui raises
`WantSaveIniSettings` when its private settings timer reaches zero in manual mode; E3.6 does
not reinterpret that as proof that no newer change is pending and therefore keeps the
separate settling query above.

The private bridge also owns pre-first-frame detached placement repair. It walks loaded
top-level `ImGuiWindowSettings` with a nonzero/non-default `ViewportId`; saved absolute
rectangle is `ViewportPos + Pos` with `Size`, widened to signed 32-bit arithmetic. It never
persists or matches the viewport ID as Vanguard display authority.

### Frozen display matching and placement normalization

For one detached saved rectangle, select its saved display by greatest rectangle/work-area
intersection area. Ties prefer a work area containing the rectangle center, then the saved
primary, then the canonical saved-display ordering above. If all intersection areas are zero,
use the saved primary.

For that saved display, compare every current `DisplaySnapshot` using this lexicographic
similarity tuple, all differences computed in widened/saturating unsigned arithmetic:

1. primary mismatch (`0` before `1`);
2. absolute saved/current work-area width difference;
3. absolute work-area height difference;
4. absolute desktop-pixel width difference;
5. absolute desktop-pixel height difference;
6. absolute Q16.16 content-scale difference;
7. absolute bounds width difference;
8. absolute bounds height difference.

The **current topology is usable for persistence restore only when it is nonempty, every
snapshot satisfies the existing Window Manager validity rules, and exactly one snapshot is
primary**. Exact-current Window Manager rejects multiple primaries but permits zero, so E3.6
adds this stricter restore-only requirement instead of assuming a primary exists. Zero-primary,
multiple-primary synthetic input, or an otherwise invalid current topology yields private
`EnvironmentUnavailable`/default-layout fallback for that startup; it does not make the
persisted document corrupt.

If exactly one current display has the best tuple, it is the target. If multiple displays
tie, choose the unique tied display with the greatest positive current-work-area overlap with
the saved work area; if that is still tied/zero, use the **current primary display**. No
enumeration index, handle or runtime fingerprint breaks ties.

Reconstruct in current logical work-area units. With saved work origin/extent `S`, saved
window origin/extent `R`, and current target work origin/extent `C`, compute in `f64`:

`relativePos = (R.origin - S.origin) / S.extent`

`relativeExtent = R.extent / S.extent`

and map with `std::llround(C.origin + relativePos * C.extent)` and
`std::llround(relativeExtent * C.extent)`. Saved content scale is used only in the matching
tuple; there is **no saved-DPI multiplier** during reconstruction. Current Window Manager /
ImGui DPI remains authoritative.

The minimum normal restored detached extent is exactly `320 x 200` logical units. Each
dimension is clamped to `[min(minimum, currentWorkDimension), currentWorkDimension]` and the
origin is then clamped so the **entire final rectangle** lies inside the current work area;
this is intentionally stronger than a partial minimum-visible-margin rule. A degenerate
saved rectangle falls back to the current E3.3 floating default `440 x 340`, centered in the
current primary work area and subjected to the same fit clamp. Invalid current topology
prevents private detached-layout restoration for that startup and falls back to default UI;
it does not make a previously valid file corrupt or invent saved runtime identities.

Vendored Dear ImGui stores `ImGuiWindowSettings::Pos`, `Size` and `ViewportPos` as signed-16
`ImVec2ih`; `Pos` is **relative** to the viewport and runtime reconstruction is
`absoluteWindowPos = ViewportPos + Pos`. P0 therefore freezes the exact post-repair writeback
for every restored detached top-level window:

- let repaired absolute origin be `(x,y)` and repaired extent be `(w,h)` after all matching,
  rounding and work-area clamps above;
- require `x` and `y` each be in `[-32768, 32767]` and `w` and `h` each be in
  `[1, 32767]` **before** any cast/narrowing;
- write `settings.ViewportPos = ImVec2ih((short)x, (short)y)`;
- write `settings.Pos = ImVec2ih(0, 0)` so stale saved relative coordinates cannot be added a
  second time;
- write `settings.Size = ImVec2ih((short)w, (short)h)`; and
- leave the raw ImGui `ViewportId` unchanged only as private toolkit state. It is never used
  by Vanguard to choose or identify a current display.

If **any** detached restored rectangle cannot satisfy those signed-16 requirements, the bridge
performs no narrowing and treats the attempted private layout as
`EnvironmentUnavailable`: clear the loaded private ImGui settings and let the accepted E3.3
default docking/floating placement run for the startup. Panel inventory/open state remains
authoritative and is not rejected. This is an environment/toolkit representability fallback,
not document corruption.

After successful repair the rewritten settings are complete before any platform viewport can
be created/shown. The existing platform path still creates detached native windows initially
hidden; visibility occurs only after normalized placement has reached Window Manager.

### `SHCT` shortcut wire and private command transaction

The section starts with `u32 recordCount`, `u32 reserved = 0`; count is at most 2048.
Records are fixed 32 bytes and sorted by ascending context type ID then command ID:

- `u64 contextTypeId`;
- `u64 commandId`;
- `u16 keyCode`;
- `u8 modifiers`;
- `u8 mode`;
- `u32 reserved32 = 0`;
- `u64 reserved64 = 0`.

Wire mode `1` means `Unbound`; wire mode `2` means `Custom`. `0`/`Default` is forbidden
because default is absence. For `Unbound`, key/modifiers are zero. For `Custom`, `keyCode`
is the exact current platform-stable USB-HID `input::Key` `u16` value and must satisfy the
same known-key ranges and modifier-key exclusion as `ValidShortcutChord`; modifier bits are
exactly Control=`0x01`, Shift=`0x02`, Alt=`0x04`, Gui=`0x08`, with no other bits. Zero IDs,
duplicate `{context,command}` keys, invalid key gaps or invalid mode/modifier combinations are
corrupt input.

The private command seam is frozen around stable records, not handler tokens. It supplies:

- bounded capture of all current non-Default live overrides plus a monotonic shortcut-
  preference revision;
- one `ApplyShortcutPreferenceBatch(contextType, records)` operation for each restored
  context; and
- per-record `Applied`, `MissingCommand`, `Invalid` plus a context-level `Conflict` result.

The private names are frozen as `EditorShortcutPreferenceRecord`,
`EditorShortcutPreferenceItemResult`, `EditorShortcutPreferenceBatchResult`,
`CaptureShortcutPreferences(...)`, `ApplyShortcutPreferenceBatch(...)`, and
`GetShortcutPreferenceRevision()`. Capture is caller-buffered and all-or-nothing: insufficient
capacity returns the exact required count and copies no partial prefix. Apply accepts one
context ID plus a span of records for that context and a same-length result span; a mismatched
context key/duplicate input key is `Invalid` with no mutation.

Apply resolves stable IDs first, retains missing context/command records as UserState orphans,
then validates the **complete live candidate map for that context** against unaffected live
handlers and against every proposed candidate before mutating any handler. A conflict among
live candidates applies zero records for that context; valid swaps therefore succeed and
record order cannot select a winner. Orphans are outside conflict ownership. Successful
ordinary `SetShortcutOverride` increments the private revision only when the stored override
actually changes. Restore samples the post-apply revision as its baseline so loading does not
immediately dirty the file.

### `PREF` typed shell-preference wire and owner seam

The section starts with `u32 recordCount`, `u32 reserved = 0`; count is at most 512 and
records are sorted by `{extensionId, providerId, fieldId}`. Duplicate triples are corrupt.
Each variable record has this fixed 40-byte header:

- `u64 extensionId`, `u64 providerId`, `u64 fieldId`;
- `u8 valueType`;
- three reserved zero bytes;
- `u32 payloadByteCount`;
- `u64 reserved64 = 0`;

The payload follows immediately and the next record begins at an 8-byte boundary with zero
padding. Wire value types are exactly: `1 = Bool`, `2 = SignedInteger`,
`3 = UnsignedBitMask`, `4 = Utf8String`. Bool payload is one byte `0`/`1`; integer and mask
payloads are exactly eight-byte `i64`/`u64`; UTF-8 is `0..256` raw bytes, contains no NUL,
and must be well-formed UTF-8. Provider descriptors may tighten a string bound.

All three stable identity fields are required to be nonzero. A zero extension/provider/field
ID is `RejectedCorrupt`, never an orphan. Registration likewise rejects `providerId == 0`;
field IDs were already frozen as nonzero/unique within the provider.

P0 freezes `MaximumEditorPreferenceProviders = 256`,
`MaximumEditorPreferenceFieldsPerProvider = 32`, and at most **512 live registered fields**
across all providers, with a generational provider token `{u32 index,u32 generation}`.
Provider identity is `{owner.extensionId, providerId}` and a
provider registration consumes one slot in the existing
`MaximumEditorExtensionOwnedRegistrations == 1024` Shell ledger. A provider descriptor owns
`1..32` stable field descriptors (`fieldId`, frozen type, optional tighter string bound), with
nonzero unique field IDs, and four callbacks: `queryRevision`, `capture`, `validate`, `apply`. Shell copies
the descriptors and callbacks into its bounded registry at registration; it never borrows the
descriptor array. The provider record stores the owner identity and callback functions but
**no extension-private state pointer**. Shell resolves the currently active extension's
`privateState` only for the duration of a callback invocation.

Registration requires a nonzero `providerId`, uniqueness of `{owner.extensionId,providerId}`
among live providers, `1..32` nonzero unique field IDs, recognized field types/bounds and all
four callbacks nonnull. Any duplicate/invalid descriptor or capacity failure mutates neither
the provider table nor the existing owned-registration ledger. The first later
`queryRevision` and every refresh must return nonzero; zero is a provider callback/invariant
failure, records stable failure context and write-inhibits automatic publication rather than
being accepted as a durable revision.

Restore is two-pass: Shell resolves providers/fields and invokes every live provider's
side-effect-free `validate` callback before any `apply`; missing exact providers/fields become
UserState orphans. This provider-validation pass is part of UserState's **global pre-apply
preflight** and runs before panel, ImGui, command or provider runtime mutation. Any live
provider validation failure therefore rejects the file as an invalid value, write-inhibits the
session and leaves every runtime owner unchanged. Only after the full live set validates does
the later restore application phase invoke `apply`. Owner revoke first
makes the provider token/callback record unreachable and removes its owned ledger entry, then
the existing `DeactivateOne` may call the extension's deactivate callback and destroy its
private state. No UserState record retains a callback/private pointer across that boundary.

Every provider owns one nonzero monotonic non-wrapping `u64` value revision returned by the
read-only `queryRevision(privateState)` callback and increments it only when one of that
provider's durable field values actually changes. UserState's EndFrame participant calls the
Shell-private `RefreshTypedPreferenceRevision()` once per persistence tick. Shell visits live
providers in slot order, resolves private state only for each callback, compares the returned
owner revision and exact provider token/owner identity with its stored baseline, and increments
one global non-wrapping `u64` typed-preference revision **once** when any provider set/value
changed during that refresh. It then updates the provider baselines. Provider or global
revision exhaustion is a stable failure and write-inhibits automatic publication rather than
wrapping. Restore samples all provider revisions only after the successfully applied state and
installs them as the new baseline, so restore itself does not dirty the file. Full typed-value
capture therefore happens only for coherent save/status operations, not every frame. This
keeps owner callbacks bounded/read-only during dirty observation and avoids a second
extension-side preference store.

The private names are frozen as `EditorPreferenceProviderId = u64`,
`EditorPreferenceFieldId = u64`, `EditorPreferenceProviderToken`,
`EditorPreferenceValueType`, `EditorPreferenceFieldDescriptor`,
`EditorPreferenceProviderDescriptor`, and `EditorPreferenceFieldValue`.
`EditorExtensionActivationContext::RegisterPreferenceProvider(...)` is the sole owner-stamped
registration entry point and returns the generational token. The Shell-private persistence
operations are `CaptureTypedPreferences(...)`, `ValidateTypedPreferences(...)`,
`ApplyTypedPreferences(...)`, `RefreshTypedPreferenceRevision()`, and
`GetTypedPreferenceRevision()`. Capture is caller-buffered
and all-or-nothing. Validate is side-effect-free. Apply is legal only for the exact set that
just validated in the same restore transaction and callbacks must not reject or retain the
transient private-state pointer. `UnregisterPreferenceProvider(...)` is owner-checked; Shell
owner revoke remains authoritative even if explicit unregister was omitted.

`EditorPreferenceFieldValue` is a pointer-free bounded value object: type tag plus a union of
`bool`, `i64`, `u64`, or `{u16 byteCount; char bytes[257];}`. Generic string `byteCount` is
`0..256`; `bytes[byteCount]` is always a local terminating NUL and is not part of the wire
payload. The callback forms are field-local and `noexcept`:
`queryRevision(privateState) -> u64`, `capture(privateState, fieldId, value) -> bool`,
`validate(privateState, fieldId, value) -> bool`, and
`apply(privateState, fieldId, value) -> void`. `apply` is intentionally infallible for a value
accepted by the immediately preceding validation transaction; a provider that cannot honor
that contract is not registerable in V1.

The first provider is E3.5 Activity presentation:

- owner `ActivityPresentationExtensionId = 0x6564333561637401ull`;
- provider ID `0x6564333570726601ull`;
- levels field ID `0x656433356c766c01ull`, type `UnsignedBitMask`, only low six bits valid;
- category field ID `0x6564333563617401ull`, type `SignedInteger`, valid values `-1` or a
  current diagnostics category value representable by the existing `i32` field;
- text field ID `0x6564333574787401ull`, type `Utf8String`, maximum **127 payload bytes** so
  the existing 128-byte C buffer always retains its NUL terminator.

### Dirty generations, debounce and retry cadence

The panel registry gains one private monotonic user-state revision incremented only by an
effective panel create/destroy or committed open-state change. The command service exposes the
shortcut revision above. Shell exposes the typed-preference revision above. The UI bridge
consumes `io.WantSaveIniSettings` into one private layout-dirty observation while keeping
`io.IniFilename = nullptr` permanently, and separately reports whether ImGui's
`SettingsDirtyTimer` is still positive.

UserState stores the last observed value of those three revisions plus the UI dirty signal.
If one or more sources changed since the preceding persistence tick, the service increments
its **single** non-wrapping `u64 dirtyGeneration` once for that tick and records
`lastDirtyRealTimeSeconds`. Source baselines are sampled only after startup restore has fully
completed, so startup reconstruction is not mistaken for a user edit. Generation exhaustion
write-inhibits further automatic publication rather than wrapping.

The quiet debounce is exactly **2.0 seconds** of `FrameContext.realTimeSeconds`. Autosave is
eligible when `dirtyGeneration != publishedGeneration` and at least 2.0 seconds have elapsed
since the last newly observed dirty generation **and** the private UI reports no layout still
settling. This second gate is required because exact-current Dear ImGui's manual
`WantSaveIniSettings` notification is itself timer-delayed and its timer can be restarted by a
newer layout mutation. After an autosave failure, the next automatic
attempt is additionally held until at least 2.0 seconds after that failure; a newer dirty
generation restarts the quiet deadline. This prevents per-frame retry. Focused tests call the
same persistence-tick helper with deterministic `f64 nowSeconds`; they never sleep.

`OnQuiesce` closes autosave admission and, when the session is writable, performs exactly one
final coherent capture/stage/validate/replace attempt regardless of whether dirty and
published generations are equal. It ignores the debounce/backoff deadline. A write-inhibited
session performs no final write. Save failure is soft lifecycle evidence and teardown
continues.

### Exact P0 future file/test matrix

No file in this matrix is created or edited as production code during P0. The frozen future
implementation surface is:

**New production files**

- `editor/private/vanguard/editor/editor_user_state_service.hpp` — service/status/store
  contract and bounded test seams;
- `editor/private/vanguard/editor/editor_ui_persistence.hpp` — private UI persistence
  interface and saved-display/layout result types, with no ImGui/native type leakage;
- `editor/src/editor_user_state_service.cpp` — codec, migration dispatch, placement resolver,
  orchestration, publication and service registration.

**Narrow existing production edits**

- `source/filesystem/include/vanguard/filesystem/filesystem.hpp` and
  `source/filesystem/src/filesystem.cpp` — the generic bounded atomic create-new complete-file
  write and typed complete-read primitives above; no user-state policy enters Filesystem;
- `editor/framework/include/vanguard/editor/editor_service_ids.hpp` — the three IDs above
  (service/capability here; frame participant declared with the private service contract);
- `editor/src/editor_application.cpp` — one UserState registration after Shell;
- `editor/private/vanguard/editor/editor_ui_imgui.hpp`,
  `editor/private/vanguard/editor/editor_ui_imgui_platform.hpp`,
  `editor/framework/src/editor_ui.cpp`, and
  `editor/framework/src/editor_ui_imgui_platform.cpp` — private layout capture/load/dirty,
  explicit dock-root and placement-normalization bridge only;
- `editor/private/vanguard/editor/editor_panels.hpp` and
  `editor/panels/editor_panel_registry.cpp` — private persistence revision/capture helper only;
- `editor/private/vanguard/editor/editor_commands.hpp` and
  `editor/commands/editor_command_service.cpp` — stable capture/revision/per-context batch seam;
- `editor/private/vanguard/editor/editor_extension.hpp`,
  `editor/private/vanguard/editor/editor_shell_service.hpp`, and
  `editor/extensions/editor_shell_service.cpp` — owned typed-provider registration,
  two-pass validation/apply/capture/revision/revoke;
- `editor/extensions/editor_activity_presentation.cpp` — first typed Console provider only;
- `editor/premake5.lua` and `editor/tests/README.md` — target/documentation wiring in an
  authorized implementation/validation gate, never in P0.

**Tests**

- existing `filesystemTests`, source `source/filesystem/tests/filesystem_tests.cpp`, owns the
  atomic-create-new primitive oracle: first create writes exact bytes, second create at the
  same path returns `AlreadyExists` and leaves the first bytes identical. It does **not**
  pretend exact-current Filesystem already has an injectable OS write/flush backend. The same
  target proves typed complete-read Success/Missing/LimitExceeded on controlled files;
- new focused target `editorUserStateTests`, source
  `editor/tests/editor_user_state_tests.cpp`; it uses fixture owner interfaces and the real
  UserState codec/store/placement code but no native desktop or GPU;
- existing `editorPanelTests` owns the panel revision/capture seam;
- existing `editorCommandTests` owns batch swap/order/conflict/orphan-independent command
  semantics;
- existing `editorShellTests` owns provider registration, validation/apply, revision and
  revoke-before-private-state-destruction semantics;
- `editorApplicationLifecycleIntegrationTest` owns service dependency/start/restore/quiesce
  ordering and scoped `Saved/Editor` IO failure behavior;
- `editorNativeUiIntegrationTest` owns eventual restart/docking/detached-host topology/DPI
  acceptance. Its external runner remains the only interactive native automation surface.

The focused `editorUserStateTests` oracle matrix is exactly the definition matrix above plus
wire-specific assertions for the literal path/magic/IDs/FourCC order, header/section flags,
zero padding/reserved fields, per-section CRC64, final SHA-256 trailer, Q16.16 display scale,
shortcut wire codes, typed-value payload sizes, secure stage-name formatting and mapping of
every `ExclusiveFileWriteResult` to the exact UserState save status. Exact-bound
and bound+1 cases exist for file bytes, ImGui bytes, panel/display/shortcut/preference counts,
panel title, generic typed string and Console text. **Shell registry capacities are not faked
here:** provider-count, per-provider-field, total-live-field and owned-ledger exact-bound/+1
oracles belong to `editorShellTests`, which exercises the real Shell registry/ledger.
The deterministic dirty-tick oracle also
proves that a positive private ImGui settling signal prevents autosave even after the 2-second
UserState deadline, and that the later consumed dirty notification restarts that deadline.
Synthetic placement tests compare repaired rectangles/display facts, never runtime handles or
enumeration order. Placement tests additionally cover a loaded nonzero relative `Pos`, exact
signed-16 origin/size boundaries, and one-unit-outside each signed-16 boundary; the success
cases must rewrite to `ViewportPos = repaired absolute origin`, `Pos = {0,0}` and exact size,
while any unrepresentable case must take the no-narrowing/default-layout fallback.

The store code is parameterized internally by one non-durable
`detail::EditorUserStateFileOps` function table containing only the physical operations it
needs: `ensureDirectory`, `readComplete`, `writeExclusive`, `replace`, and
`removeOwnedStage`, plus one opaque `userData` passed only during each synchronous call.
`readComplete` has a frozen private result domain `Success`, `Missing`, `OpenFailed`,
`ReadFailed`, `LimitExceeded`, `UnsupportedPlatform`, exactly mirroring
`filesystem::CompleteFileReadResult`; it copies into a caller-owned buffer capped at 1 MiB and reports
the exact byte count on `Success`. Production service registration always binds the real
Filesystem functions and static production context; no function table or test context is
durable/persisted, and there is no runtime test mode or public switch.
`editorUserStateTests` constructs the store helper directly with deterministic fake ops so it
can force directory/open/read/bounds, `AlreadyExists`/write/flush, staged-reopen validation,
replace and owned-remove outcomes and prove exact load/save status mapping, cleanup ownership,
dirty retention and canonical no-clobber behavior without requiring an injectable OS backend
or filesystem mtimes.

Wire tests include Q16.16 scale `1`, exact maximum `0x00100000`, maximum+1 rejection and
capture failure for a positive scale that would quantize to zero; PREF zero extension,
provider and field IDs; and synthetic current topologies with zero and multiple primary
records, both of which must take the environment-unavailable/default-layout path.

## Load, validation and migration

The entire file is parsed into bounded scratch before mutating UI, panels or commands.
Restore is all-or-default at the document-envelope level:

- missing file -> normal defaults, not an error;
- any canonical-target load/open/read IO failure other than a positively confirmed missing
  file -> `IoFailure`, continue with defaults, preserve the canonical target and **inhibit
  automatic writes for that session** because unreadable existing state must not be silently
  replaced;
- current known schema + valid digest/project identity -> eligible for restore;
- known older schema -> explicit in-memory migration, then normal restore;
- truncated/corrupt/oversized/invalid-count/invalid-enum/duplicate stable-key document ->
  reject the document, continue with defaults **and inhibit automatic writes for that
  session** so rejection evidence is not silently destroyed on ordinary shutdown;
- mismatched project identity -> reject, continue with defaults **and inhibit automatic
  writes for that session**;
- unknown/newer schema -> reject restore **and inhibit writes for the session** so an older
  editor cannot destroy a newer preference file.

An existing rejected file, and any canonical target that cannot be read/opened well enough to
prove it is missing, is preserved untouched for the session. Only positively confirmed missing
state and a successfully parsed current/known-migratable document remain writable. A future
explicit Reset Preferences workflow may clear rejected/unreadable state, but silent clean-
shutdown writeback is not that workflow.

Version 1 has no implicit skippable/optional-section compatibility. Once P0 freezes the
literal current major/minor values, only that exact version plus explicitly enumerated older
versions with a defined in-memory migration are accepted. Any higher major **or minor**,
unknown section kind, unknown required flag or nonzero reserved field is
`RejectedFuture`/unsupported and write-inhibits the session. Forward compatibility must
arrive through a later explicit schema contract, never parser guesswork.

## Panel inventory restore

Panel inventory is authoritative for **which Vanguard panel instances are restored**;
the opaque ImGui payload is not.

Before the first UI frame, after Shell activation:

1. enumerate the current exact panel instances;
2. for a persisted record whose exact instance is already live, apply only its open state;
3. for a missing instance whose type is currently registered, request ordinary
   `EditorPanelRegistry::CreatePanel` with the exact stable instance ID and let the
   registry enforce singleton/multi policy and factory ownership;
4. for a truly missing type, create the accepted bounded E3.3 placeholder with the stable
   unresolved type/instance IDs and fallback title;
5. if a registered type rejects the persisted instance for policy/identity reasons, do
   **not** disguise that failure as a placeholder; diagnose and skip that record; and
6. never replace a placeholder hot in the same session when an extension appears later;
   the accepted static-extension model restores it normally on the next startup.

Current registered type/display metadata wins over stale persisted display text. Persisted
title is diagnostic fallback only for unresolved placeholders.

## Private UI layout bridge

E3.6 adds only a narrow **private** layout persistence bridge to the existing UI owner.
No ImGui type or raw settings text enters public extension/domain APIs.

The bridge must support:

- load one already-validated bounded layout payload while the ImGui context/platform is
  initialized, no UI frame is active and before the first `NewFrame`;
- capture one complete bounded layout payload while the context is live and no frame is
  being authored;
- query/consume the existing manual ImGui dirty signal without letting ImGui write disk;
- mark the default dock builder as satisfied by a valid restored dock layout so it cannot
  overwrite the restored tree; and
- pass detached-viewport restore metadata into the existing private platform adapter for
  safe placement.

The UI bridge never opens the preference file. The user-state service owns bytes/file IO;
the UI owner only validates/translates its private layout section.

The opaque payload can describe only toolkit layout for UI windows that Vanguard later
authors. An unknown/stale ImGui window-settings entry is inert: it cannot create a
`EditorPanelRegistry` instance, activate an extension, register a command or override the
validated panel inventory. After loading, the private bridge must confirm that the restored
settings contain a usable primary Vanguard dockspace/root before reporting
`RestoredLayout`; otherwise it clears/discards the attempted private layout and lets the
accepted E3.3 default dock builder run. Default-layout suppression therefore follows a
**usable restored dock root**, not merely a syntactically accepted settings blob.

The initial schema is deliberately **envelope-atomic**: an invalid section rejects the
whole document before runtime mutation. Version 1 does not perform accidental section-local
salvage. A future schema may add independently authenticated optional sections, but only
through an explicit versioned migration contract.

## Detached monitor/DPI recovery

Current Window Manager display handles/fingerprints are runtime topology identities, not
cross-restart monitor IDs. E3.6 persists neither as durable authority. Instead the private
layout section captures one bounded description of the **saved topology** (geometry,
desktop-pixel extent, content scale and primary hint) and maps floating rectangles to the
current topology on restore.

For each detached viewport accepted for restore:

1. determine which saved work area contained or most overlapped the persisted floating
   rectangle;
2. choose a current target by a deterministic similarity rule over stable facts available
   in both snapshots (primary hint, work-area/desktop extent and content scale), never by
   generational handle or SDL-ID-derived fingerprint;
3. if no unique safe match exists, choose the current primary display;
4. reconstruct placement by normalized position/extent within the **current** target work
   area, not yesterday's absolute desktop coordinate;
5. normalize invalid/non-finite/degenerate values and clamp the final logical rectangle so
   it is usable within the current work area;
6. treat saved content scale only as matching/migration evidence; current Window Manager/ImGui DPI
   state is authoritative for the restored host; and
7. verify the final requested placement is representable before creating/showing the native
   host.

The exact clamp/minimum-visible-area rule is a P0 lock. It must handle:

- unplugged/reordered displays;
- changed work-area origin/size;
- mixed-DPI transitions;
- a topology refresh that changes the SDL-ID-derived runtime fingerprint;
- extreme/off-screen persisted coordinates; and
- a saved window larger than the current work area.

This is production restore safety, not a test-driver workaround.

## Shortcut persistence contract

E3.6 must not replay shortcut records one at a time in file order. That would make valid
swaps/order-dependent layouts fail because one live default could temporarily occupy a
chord needed by another persisted override.

P0 therefore locks one narrow private command-persistence seam:

- capture the current explicit live overrides as stable context+command records;
- apply a bounded restored override set **transactionally per command context** using the
  existing E3.2 validation/conflict rules; and
- report applied, missing/orphaned, invalid and conflicted records without exposing command
  registry internals.

Missing command/context IDs are retained as bounded orphan preferences, not applied to an
unrelated replacement. Orphans reserve no live chord and do not participate in live conflict
ownership until the exact stable IDs return on a later startup, when they may apply. A true
same-context conflict rejects the entire restored batch for that context symmetrically with no
partial context mutation; file order cannot choose a winner. Invalid live records remain
unapplied and diagnosed. Saving merges current explicit live overrides with still-valid bounded
orphans by stable key. Duplicate keys are a corrupt document, not “last record wins.” If current live overrides plus
retained orphans would exceed the frozen persistent bound, publication fails rather than
silently dropping preference intent.

Typed shell preferences use the same exact-key/capacity rule: saving merges current
provider values with still-valid missing-provider/field orphans, rejects duplicate
`{extension, provider, field}` keys, and fails publication rather than dropping records
when the frozen 512-record bound would be exceeded.

## Dirty state and publication cadence

Persistence must be coalesced; no disk write occurs per UI frame, mouse move or docking
event.

The service tracks one monotonic in-memory dirty generation sourced from:

- accepted UI layout dirtiness;
- panel inventory/open-state mutation;
- successful shortcut preference mutation; and
- Shell's owner-scoped typed preference dirty generation.

One bounded main-thread Frame Pipeline participant observes those revision/dirty signals
after the ordinary UI/command work needed to make them complete for that frame. P0 locks
the exact phase/edges and debounce interval. The implementation must support:

- bounded deferred autosave after a quiet interval;
- an explicit final capture/publication attempt during E3.6 `OnQuiesce`; and
- no automatic or final write at all while the current session is write-inhibited by rejected
  or unreadable canonical preference state.

Failed autosave keeps the state dirty and records bounded failure evidence. It must not
busy-retry each frame. Test acceptance never sleeps for the debounce interval: focused
tests drive a deterministic persistence tick/clock seam, while restart/native acceptance
uses the mandatory quiesce save.

## Complete-file publication

Every save:

1. captures one coherent main-thread snapshot from the accepted owners;
2. serializes a complete bounded current-schema document to a **same-directory sibling**
   staging file;
3. flushes and closes it;
4. reopens and validates the complete staged bytes with the same parser/integrity rules;
5. publishes only with `filesystem::ReplaceFile(staged, target)`; and
6. deletes only the service's own still-existing staging file on an ordinary failed attempt.

The old target is never deleted before replacement. A failed stage/flush/validation/replace
must leave the previous target usable.

The canonical target is the **only** committed authority. An abandoned sibling stage is
never promoted or used for recovery when the canonical file is absent/corrupt. P0 locks the
collision-resistant per-activation sibling staging name plus atomic exclusive-create ownership
defined above; a process removes only a
stage it created/owns. Cross-process preference merging is not an E3.6 goal; concurrent
editors may be last-complete-writer-wins at the canonical target, but their in-progress
stages may never share ownership or truncate/overwrite one another; an exact candidate-name
collision is a failed exclusive-create attempt.

## Lifecycle contract

### Start / restore

`EditorUserStateService::OnStart` runs after Frame Pipeline, Filesystem, Workspace, UI,
Commands and Shell are started by explicit dependencies. At this point:

- the project/Saved root is known;
- the one ImGui context/platform exists;
- extension panel types/initial instances are registered;
- command handlers/default shortcuts exist; and
- no application frame has begun.

Restore first parses the **entire** envelope into bounded scratch and completes every
document-level semantic preflight that can reject the file, including live typed-provider
`validate` callbacks, before mutating any runtime owner. Only a preflight-clean document then
enters the fixed application order: panel inventory, private layout, per-context shortcut
state, typed shell preferences. Provider `apply` is infallible for its just-validated values.
Document rejection still degrades to editor defaults/status and write-inhibits rather than
failing editor startup; ordinary restore outcomes that are intentionally non-authoritative
(panel policy skip, per-context shortcut conflict/orphan, unusable private layout fallback,
missing typed provider/field orphan) follow their locked local semantics without converting a
valid document into an editor lifecycle failure.

### Quiesce / final save

Because EngineHost tears down in reverse dependency order, E3.6 quiesces before Shell,
Commands and UI. It first closes persistence admission/debounce, captures one final coherent
state while all owners are still live, and attempts complete-file publication.

Preference IO failure is **soft lifecycle evidence**, not a reason to strand EngineHost
shutdown with active shell/UI callbacks. Wrong-thread/internal invariant failures may still
fail the service lifecycle.

After E3.6 quiesce, Shell may deactivate extensions, Commands may clear owned handlers and
UI may destroy the ImGui platform/context. E3.6 must retain no borrowed extension/panel/
command/ImGui/native-window pointer across that boundary.

## Failure/status model

The service exposes bounded status sufficient for tests and E3.5 diagnostics presentation:

- service state;
- stable load result (`NotAttempted`, `Missing`, `Restored`, `Migrated`,
  `RejectedCorrupt`, `RejectedBounds`, `RejectedProjectMismatch`,
  `RejectedFuture`, `IoFailure`);
- stable save result (`NotAttempted`, `Published`, `WriteInhibited`,
  `StageOpenFailed`, `StageWriteFailed`, `StageFlushFailed`,
  `StageValidationFailed`, `ReplaceFailed`, `CleanupFailed`);
- loaded schema version;
- panel restored/created/placeholder/skipped counts;
- shortcut applied/orphaned/conflicted/invalid counts;
- typed preference applied/orphaned/invalid counts;
- repaired detached-placement count;
- whether writes are inhibited;
- dirty/published generations;
- autosave/final-save failure counts; and
- the latest typed failure code with stable numeric/context identity, not borrowed file text.

Status is observation only. E3.5 may display it later without becoming a persistence owner.

## Initial failure policy

| Condition | Required behavior |
|---|---|
| No state file | Use defaults; writable current session |
| Canonical load/open/read IO failure | Defaults; `IoFailure`; **write inhibit**; preserve canonical target |
| Truncated/corrupt/oversized current state | No partial restore; defaults; **write inhibit**; preserve rejected target |
| Future/unknown schema | No restore; **write inhibit**; preserve file untouched |
| Project identity mismatch | No restore; defaults; write inhibit; do not apply/replace foreign state |
| Missing panel type | Exact bounded placeholder; no extension callback |
| Invalid known panel instance/policy | Skip + diagnose; never masquerade as missing type |
| Missing command/context | Keep bounded orphan by stable key; do not retarget |
| Shortcut conflict | Per-context transaction does not partially apply that conflicting set |
| Missing typed-preference provider | Keep bounded typed orphan; invoke no missing owner |
| Invalid Console filter preference | Global provider preflight rejects document as `RejectedCorrupt`; zero owner mutation; write inhibit |
| Removed display | Move affected detached host to current primary work area |
| Changed DPI/work area | Reconstruct/clamp using current display facts |
| Bad detached placement | Sanitize/fallback before native host becomes visible |
| Autosave IO failure | Keep dirty + bounded evidence; no per-frame retry |
| Final-save IO failure | Preserve old complete file; continue teardown |
| Staged validation failure | Never replace target |
| Canonical missing + stale stage | Defaults; stale stage is never promoted |

## Project-policy boundary

E3.6 adds no new authored project-policy fields merely to justify user preferences.
Current/future project policy may constrain restoration, but:

- user state can only be intersected with such policy;
- user state never mutates `.vproject` or `Config/`;
- extension selection remains the authored/static E3.1 shell contract;
- a preference file cannot activate an extension not selected by project policy; and
- a placeholder is presentation of unresolved saved state, not implicit plugin activation.

## Expected production scope

P0 has refreshed and locked the exact file set in **Exact P0 future file/test matrix** above.
This section is only a summary of that frozen matrix; it must not narrow or contradict it.

### New production files

- `editor/private/vanguard/editor/editor_user_state_service.hpp`;
- `editor/private/vanguard/editor/editor_ui_persistence.hpp`; and
- `editor/src/editor_user_state_service.cpp`.

### Existing files extended narrowly in later authorized implementation gates

- `source/filesystem/include/vanguard/filesystem/filesystem.hpp` and
  `source/filesystem/src/filesystem.cpp` for the generic typed `ReadCompleteFile` and atomic
  `WriteExclusiveFile` primitives only; no editor/user-state policy enters Filesystem;
- `editor/framework/include/vanguard/editor/editor_service_ids.hpp` for one service/
  capability identity;
- `editor/src/editor_application.cpp` for one service registration;
- `editor/framework/src/editor_ui.cpp`, private UI persistence header and private ImGui
  platform files only for the
  bounded private layout capture/load/dirty/placement bridge;
- `editor/private/vanguard/editor/editor_panels.hpp` and
  `editor/panels/editor_panel_registry.cpp` only for the private persistence revision/capture
  helper;
- `editor/private/vanguard/editor/editor_commands.hpp` and
  `editor/commands/editor_command_service.cpp` only for the bounded private shortcut
  snapshot/batch-restore seam;
- `editor/private/vanguard/editor/editor_extension.hpp`,
- `editor/private/vanguard/editor/editor_shell_service.hpp` and
  `editor/extensions/editor_shell_service.cpp` for one owner-stamped typed preference
  provider/dirty-generation seam in the existing shell ledger;
- `editor/extensions/editor_activity_presentation.cpp` only to register/copy/validate its
  previously deferred Console filter preferences through that shell-owned seam;
- `editor/premake5.lua` and `editor/tests/README.md` only in an implementation gate.

No change under assets/assetStreaming/rendering/world is part of E3.6.

## Test strategy

### Pure/focused persistence tests

P0 locks a new focused `editorUserStateTests` shape that does not require a native desktop
or GPU. It must cover:

- format round-trip/canonical ordering, each exact bound and its `+1` rejection;
- all size/count/enum/overflow/digest/project-identity rejection, with parse failure leaving
  every destination owner unchanged;
- current/known-old/future-version behavior, including zero runtime application and a
  byte-identical canonical target after quiesce for future/rejected state;
- canonical load/open/read IO failure preserving the target and write-inhibiting the session;
- complete stage open/write/flush/reopen-validation/replace failure coverage, stale-stage
  non-promotion and read-only-target behavior, always preserving the prior canonical target;
- panel stable identity/open state/placeholder decisions against a fixture registry;
- missing-panel placeholder retention and normal exact-ID resolution on a later simulated
  restart when the panel type returns;
- transactional shortcut restore including reversed-record-order invariance, valid swaps,
  unbound-first cases, true same-context conflicts, legal reuse of the same chord in distinct
  contexts, missing IDs, orphan retention and later exact-ID reappearance;
- typed preference provider apply/capture validation, owner revoke, orphan retention and
  Console level/category/text bounds;
- deterministic merge/dedup of live overrides + orphans;
- a pure placement resolver over synthetic saved/current display topologies covering removed/
  reordered displays, work-area changes, mixed DPI, changed runtime fingerprint, extreme
  off-screen coordinates and saved windows larger than the current work area;
- project A -> A acceptance and A -> B project-identity rejection without applying A's state;
- wrong-thread/lifecycle guards; and
- soft IO-failure status.

The E3.6-specific unwritable-target oracle must not make the whole project `Saved/` root
unwritable before normal editor composition, because accepted diagnostics/log startup
already requires `Saved/Logs`. Focused tests use an isolated store path/failure seam;
lifecycle/native proof makes only the E3.6 `Saved/Editor/` target/subdirectory unwritable
after ordinary workspace startup.

### Existing focused regression targets

- `filesystemTests`: authoritative for `ReplaceFile` plus the new generic
  `ReadCompleteFile` Success/Missing/LimitExceeded and atomic-create-new
  `WriteExclusiveFile` first-create/AlreadyExists/no-truncate behavior. OS write/flush failure
  injection is **not** claimed by this target; UserState's private deterministic file-ops seam
  owns exact higher-level status mapping for those outcomes;
- `editorPanelTests`: private panel persistence revision/capture behavior actually added for
  restore/capture;
- `editorCommandTests`: private batch capture/apply behavior and no regression of existing
  E3.2 interactive override semantics;
- `editorShellTests`: real provider registration/descriptor-copy/validation/apply/revision/
  revoke behavior and exact provider/per-provider-field/total-live-field/owned-ledger capacity
  +1 rejection.

### Lifecycle/native restart proof

Reuse `editorApplicationLifecycleIntegrationTest` and
`editorNativeUiIntegrationTest`; do not add a product test mode or a second native editor.

P5/P6 eventually prove a two-run/restart workflow:

1. create a non-default dock/detached layout, panel open state and shortcut override;
2. persist and cleanly exit;
3. restart the same project and prove exact panel IDs/open state, docking/detached placement
   and shortcut behavior restore;
4. repeat after removing/changing a display topology and prove all native hosts remain
   usable/on-screen;
5. repeat with one selected fixture extension omitted and prove a placeholder rather than
   callback/identity aliasing;
6. exercise corrupt/truncated/future-version files and prove fallback/write-inhibit rules;
7. prove unwritable/failed preference publication cannot prevent clean editor shutdown.

The lifecycle fixture must also record semantic ordering from `Activate/Start` through
`Restore` to the first UI frame, and from final `Capture/Save` through Shell deactivation to
UI shutdown. Focused and lifecycle tests must not use sleeps, filesystem mtimes, real monitor
enumeration order, screenshot pixels or unordered-container iteration as pass/fail oracles; deterministic injected
clock/tick/topology/state seams and stable telemetry are the machine evidence.

Any automation that moves the real mouse, changes focus, clicks controls or drags native
windows requires the user's warning immediately before execution. Screenshots are supporting
evidence only; semantic acceptance uses state/telemetry.

## Ordered implementation gates

### P0 - exact-CURRENT architecture, format and test-shape lock

**Status: CLOSED; final exact-current independent reviews 0 BLOCKER / 0 HIGH / 0 MEDIUM.**

Source/planning only. Re-read all persistence owners and freeze:

- service/capability IDs and exact dependency graph;
- literal Saved path/filename, magic/version/section framing and hard bounds;
- private UI layout bridge and monitor/DPI normalization algorithm;
- private command batch capture/restore contract;
- panel restore decision table;
- dirty generation/debounce/final-save rules;
- exact file/target matrix and native restart oracle.

No production implementation, Premake generation, build, executable or native run.

### P1 - bounded document/store + service lifecycle core

**Status: CLOSED; explicitly authorized 2026-09-27 and closed after fresh exact-current
independent 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static reviews and scoped hygiene.**

Implement the service, exact bounded parser/serializer, load status, migration/write-inhibit
policy, sibling-stage validation + `ReplaceFile`, lifecycle dependency wiring and focused
format/store tests. No ImGui layout application, panel restore or shortcut persistence yet.

### P2 - panel inventory + private UI layout restore

**Status: CLOSED; explicitly authorized 2026-09-27 and closed after fresh exact-current
independent 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static reviews and scoped hygiene.**

Implement exact panel inventory capture/restore/placeholders plus private ImGui
capture/load/dirty/default-dock suppression, saved-topology normalization and safe detached
placement reconstruction.
Keep raw toolkit state private.

### P3 - shortcut preference persistence

**Status: CLOSED; explicitly authorized 2026-09-27 and closed after fresh exact-current
independent 0 BLOCKER / 0 HIGH / 0 MEDIUM architecture/lifecycle/source-static and
adversarial/source-oracle reviews plus scoped static hygiene. No generation/build/executable/
native validation was run in P3.**

Add private command snapshot/per-context transactional restore, bounded shortcut-orphan
retention, owner-scoped typed shell-preference providers (including E3.5 Console filters),
stable-key merge and dirty-generation integration. No new command or extension-state owner.

### P4 - lifecycle/recovery hardening

**Status: CLOSED; explicitly authorized 2026-09-28 and closed after fresh exact-current
independent 0 BLOCKER / 0 HIGH / 0 MEDIUM architecture/lifecycle/source-static and
adversarial/source-oracle reviews plus scoped static hygiene. No generation/build/executable/
native validation was run in P4.**

Harden corrupted/future/read-only state, project mismatch, missing extensions, display
removal/fingerprint replacement, mixed-DPI/work-area changes, save failure and repeated
restart. Close any real source/static lifecycle defects without expanding scope.

### P5 - production-shaped persistence fixture and native oracle preparation

**Status: CLOSED; explicitly authorized 2026-09-28 and closed after scoped source/static
hygiene plus fresh exact-current independent architecture/lifecycle/source-static and
adversarial/acceptance-oracle reviews each returned 0 BLOCKER / 0 HIGH / 0 MEDIUM. No
Premake generation, compilation, executable test or native automation was run in P5.**

Prepare the smallest test-only stimulus/telemetry needed for real save/restart/restore,
missing-extension and changed-topology proof using existing lifecycle/native executables.
No broad execution yet under the user's batching policy unless separately authorized.

### P6 - comprehensive validation, documentation and acceptance

**Status: CLOSED; explicitly authorized 2026-09-28. E3.6 is accepted.**

The user's final-gate correction supersedes the earlier source-only P6 interpretation: ordinary
and intermediate work stays light, but the final authorized gate must leave the relevant source
compiling and run the smallest focused proof tests necessary for closure. P6 therefore performed
fresh Premake VS2022 generation and serialized `/m:1` Debug x64 compile/link of
`editorUserStateTests`, `editorApplicationLifecycleIntegrationTest`,
`editorNativeUiIntegrationTest` and the production `editor` target. Compilation exposed several
real exact-current E3.6/editor integration defects (type-definition ordering, focused-target include
closure, stale path API spelling, UI-persistence helper linkage, const/fatal-warning and ImGui
platform mismatches); those were corrected before acceptance.

Focused execution then passed `editorUserStateTests` and the dedicated noninteractive
`run-user-state-persistence-test.ps1` sequence. That sequence passed seed, restart restore,
missing-extension placeholder/orphan, extension return, corrupt, truncated, future-version,
project-mismatch preservation and isolated unwritable-final-save cases, exiting 0. Deterministic
saved-topology/DPI/work-area repair remains covered by `editorUserStateTests`; no physical
mouse/focus automation was necessary for final closure. The broader historical Debug+Development
matrix and warned real-input layout runner were intentionally not expanded beyond this focused
proof. E3.7 remains unauthorized/unstarted.

## Minimum acceptance matrix

| Scenario | Required outcome |
|---|---|
| First launch / no file | Accepted default dock/open/shortcut state; no error |
| Save + restart | Exact stable panels/open state/layout/shortcuts restore |
| Closed singleton | Same stable instance remains closed after restart |
| Multi-instance panels | Exact persisted instance IDs recreated within policy/capacity |
| Missing extension/type | Placeholder preserves IDs/title; no missing callback |
| Extension returns next restart | Exact type/instance may restore normally; no same-session hot replacement requirement |
| Shortcut swap | Batch restore succeeds without file-order artifact |
| Shortcut record order | Reversing persisted record order produces the same per-context result |
| Shortcut duplicate/conflict | Context not partially mutated; conflict diagnosed |
| Same chord in different contexts | Accepted when existing E3.2 context rules allow it |
| Missing shortcut owner | Stable orphan retained; cannot retarget another command |
| Missing shortcut owner returns | Exact orphan may apply on later restart; no unrelated identity capture |
| Console filters | Level/category/text values restore through owner callback with exact bounds |
| Provider revoked | No capture/apply callback after shell revoke/private-state destruction |
| Canonical load/open/read IO failure | Defaults; canonical target preserved; automatic/final writes inhibited |
| Corrupt/truncated/oversized | No partial restore; defaults; write inhibited; rejected target preserved |
| Future schema | No restore and no overwrite by older editor |
| Project mismatch | Foreign user state rejected and preserved; no automatic overwrite |
| Crash/failure before replace | Previous complete target remains |
| Replace failure | Previous complete target remains; dirty/failure evidence retained |
| Removed/ambiguous secondary display | Detached content restored usable on deterministic current fallback |
| Same display, changed DPI/work area | Placement reconstructed/clamped from current facts |
| Invalid/offscreen extent | Sanitized before visible native host creation |
| ImGui dirty signal | Coalesced save; ImGui never writes its own file |
| Quiesce | Final snapshot occurs before Shell/Command/UI teardown |
| Unwritable `Saved/Editor/` target | Editor can still shut down; preference failure is observable |
| Runtime/headless | No E3.6 service or preference-file access |

## Explicit non-goals

- No authored project/layout asset format.
- No project-policy editor or `.vproject` mutation.
- No dynamic plugin loading/hot replacement.
- No roaming account/cloud preference synchronization.
- No recent-files/session/document recovery system.
- No document dirty/save/undo state.
- No asset-browser/hierarchy/inspector/viewport-camera persistence.
- No renderer/runtime settings store.
- No theme/styling/font preference pass.
- No arbitrary extension-private binary blobs in the central preference file.
- No second UI, panel registry, command registry, window owner, service container or event loop.
- No E3.7 demonstration extension.

## Definition exit gate

The E3.6 definition may close only when:

1. exact-current Workspace/UI/panel/command/shell/window/filesystem contracts are re-read;
2. one persistence owner and its lifecycle dependency/order are explicit;
3. file authority/path/version/bounds/publication/future-version behavior are explicit;
4. panel/placeholder and shortcut-orphan identity semantics are stable-ID based;
5. private ImGui payload ownership/default-dock suppression is explicit;
6. monitor/DPI/offscreen restore cannot create unusable detached hosts;
7. project policy and user state cannot overwrite one another;
8. P0-P6 gates, focused/restart/native acceptance shape and warning boundary are explicit;
9. parent/roadmap/development-map status is reconciled;
10. scoped whitespace/link/status hygiene passes; and
11. fresh independent definition reviews return 0 BLOCKER / 0 HIGH / 0 MEDIUM after all
    corrections.

Definition closure does not authorize P0 or production implementation.
