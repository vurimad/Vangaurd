# Test-support isolation

This policy applies repository-wide. Production source trees are product code
only. Test, proof, diagnostic, temporary, automation, agent-workflow, and
development-history artifacts must be visibly separated by both **placement**
and **name**.

## Production-tree invariant

Directories that represent a product/runtime implementation (`src`, `private`,
`framework`, `extensions`, `runtime`, platform implementation folders, and
similar module-owned source trees) must not contain code whose purpose is only
testing, proof gathering, temporary diagnosis, automation, or development
workflow bookkeeping.

The following are not product concepts and must not become executable product
contracts:

- development phase/task/checkpoint identifiers;
- ticket/milestone names used only to track implementation progress;
- agent, worker, chat, session, or review identities;
- proof-only plugin names, environment variables, command-line switches, logs,
  persistent IDs, or fixture behavior;
- automation-only panels, commands, callbacks, services, or descriptors.

Historical references belong in plans/progress documents, not in production
identifiers or runtime strings.

## Required placement and naming

Test-only implementation belongs under an unmistakable test tree such as:

- `<module>/tests/`
- `<module>/tests/support/`
- `<module>/tests/fixtures/`
- `<module>/tests/integration/`

The filename must communicate its role without opening the file. Prefer names
such as `*_integration_test.cpp`, `*_test_fixture.cpp`,
`*_integration_test_harness.cpp`, or `temporary_*_test.cpp` when the file is
intentionally short-lived.

Bad placement/name examples:

- `editor/framework/src/editor_ui_proof.cpp`
- `editor/extensions/editor_static_probe_extension.cpp` when it exists only for
  validation
- `source/rendering/src/temp_debug.cpp` for an automation-only stimulus

Good examples:

- `editor/tests/support/native_ui_integration_test_harness.cpp`
- `source/rendering/tests/support/viewport_integration_test_fixture.cpp`
- a dedicated `*Test` executable that explicitly lists its test-support files

Do not move a test fixture into a production directory merely because it needs
private access. Either keep it test-owned and compile the required production
sources into the test target, or introduce a narrow production API only when
that API is independently justified as product functionality.

## Build isolation

Normal product targets must not compile or link test support. In particular:

- never include `tests/**` or `tests/support/**` through a production wildcard;
- never put a test plugin/descriptor in a production compiled catalog;
- never make production source include a header from `tests/`;
- never add automation-only CLI handling or test harness state to a product
  application class;
- when a native/integration harness needs real product composition, use a
  dedicated test executable that compiles the real production sources plus an
  explicit test-owned entry point/service/catalog/fixture as needed.

Test code may depend on production code. Production code must never depend on
test code.

## Review checklist

Before accepting new test/proof scaffolding, verify all of the following:

1. Every test-only file lives in an explicit test path and has a test-explicit
   filename.
2. Production projects contain no test-support source/header and no test-only
   preprocessor switch added solely to activate automation.
3. Product source contains no automation-only CLI, plugin, environment-variable,
   runtime-log, panel/command, or persistent identity.
4. Product source contains no development phase/checkpoint/agent identifiers.
5. Dedicated test targets explicitly list test support instead of relying on a
   broad product wildcard.
6. The production executable behaves independently of, and can be built without,
   the test-support tree.
7. Long-lived tests use behavior-based names/messages rather than phase or
   checkpoint labels.

When a temporary proof becomes obsolete, delete it. When behavior graduates into
a real product requirement, move the behavior behind the proper production
owner/API and remove the test-only dependency rather than renaming the proof into
production code.
