# Vanguard project definition (`.vproject`)

Status: schema 1.0 normative draft.

`vproject` is the authored, source-controlled definition of an editable Vanguard project. It is consumed by editor and tool products only. A shipped runtime image never contains or reads it.

## Ownership and invariants

- The project file is the sole authority for project identity and project-relative root selection.
- Project identity is a random, persistent 128-bit identifier. Renaming or moving a project never changes it.
- Every stored project path is relative to the directory containing the `.vproject` file.
- Absolute paths, parent traversal, environment expansion, and implicit engine-repository paths are forbidden.
- Unknown required fields, duplicate scalar fields, malformed UTF-8, and unsupported schema major versions are errors.
- Readers preserve no transient build state. Fingerprints, indexes, compiler records, and cooked artifacts belong in `DerivedData`.
- Writers emit fields in canonical order with UTF-8 encoding and LF line endings, enabling stable diffs.

## Text grammar

Schema 1 uses a deliberately small declarative grammar rather than embedding a general scripting language:

```text
vproject 1.0

project.id = "7f71a8a7-9be8-4ceb-897d-c837bd3e0fd6"
project.name = "Project Cardinal"
project.technicalName = "project-cardinal"

engine.minimum = "0.1.0"
engine.maximum = "0.1.x"

paths.assets = "Assets"
paths.derivedData = "DerivedData"
paths.intermediate = "Intermediate"
paths.saved = "Saved"
paths.builds = "Builds"
paths.config = "Config"
paths.plugins = "Plugins"

target = "windows-x64"

policy.cooking = "default"
policy.packaging = "default"

startup.editorWorld = ""
startup.runtimeWorld = ""
startup.input = ""
```

Whitespace outside quoted values is insignificant. `#` begins a comment outside a quoted value. Strings use JSON-compatible escapes for quote, reverse solidus, newline, carriage return, tab, and `\uXXXX`. Repeated `target` and `plugin` fields are ordered sets; every other schema 1 field is scalar and may occur exactly once.

## Required fields

| Field | Contract |
|---|---|
| `project.id` | Non-zero RFC 4122 textual UUID generated once at project creation. |
| `project.name` | User-facing UTF-8 display name, 1–128 Unicode scalar values. |
| `project.technicalName` | Stable lowercase ASCII identifier using letters, digits, and single hyphens. |
| `engine.minimum` | Oldest compatible Vanguard semantic version. |
| `engine.maximum` | Newest compatible version or compatible `x` range. |
| `paths.*` | Distinct, normalized project-relative directory paths. |
| `target` | At least one canonical target triplet. |
| `policy.cooking` | Named project cooking policy. |
| `policy.packaging` | Named project package-placement policy. |

Startup resource fields are required but may initially be empty. Once assigned, they contain canonical logical resource identities—the same identities emitted into DerivedData and VPAK indexes—not operating-system paths, source filenames, or VPAK offsets. The field supplies the resource kind, so the identity does not depend on a filename extension.

## Canonical project layout

Creation of a default project publishes this complete structure as one transaction:

```text
<Project>/
|-- <TechnicalName>.vproject
|-- Assets/
|   |-- Worlds/
|   |-- Meshes/
|   |-- Textures/
|   |-- Materials/
|   |-- Shaders/
|   |-- Audio/
|   `-- Prefabs/
|-- Config/
|   |-- Project/
|   |-- Cooking/
|   `-- Packaging/
|-- Plugins/
|-- DerivedData/
|-- Intermediate/
|-- Saved/
`-- Builds/
```

The destination must not already exist. Creation occurs in a uniquely named sibling staging directory, validates the completed document and layout, flushes written files, and publishes the staging directory with a no-replace atomic rename. Failure removes only that owned staging directory; it never modifies an existing destination.

## Validation levels

`nanovanguard project validate` supports cumulative levels:

1. `document`: grammar, encoding, schema, identity, values, and duplicate detection.
2. `layout`: required roots exist with the correct type and remain beneath the project root.
3. `content`: metadata/index consistency and referenced startup assets exist.
4. `build`: registered importers, compilers, plugins, targets, and policy compatibility.

The default is `layout`. Machine output reports stable diagnostic codes, field names, source locations, and suggested remediation without parsing human prose.

## Migration

Major schema changes require an explicit migration command. Migration reads and validates the old document, creates a complete new candidate beside it, reports semantic changes, and atomically replaces the file only after validation. `nanovanguard` never silently upgrades a project during editor launch, inspection, cooking, or packaging.
