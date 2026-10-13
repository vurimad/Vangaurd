# Development documentation

Documents are grouped by purpose, not by agent or creation date:

| Folder | Contents |
|---|---|
| `plans/` | Roadmaps, ordered implementation gates and cross-agent contracts |
| `designs/` | Proposed/accepted designs, integration contracts and corrections |
| `studies/` | Research, source audits and study plans preceding implementation |
| `progress/` | Durable completion status and validation boundaries |
| `checkpoints/` | Historical conversation backups and resume context |

A folder does not imply acceptance. Read each document's status and validation
ledger. Former plans and checkpoints are historical evidence, not permission to
implement the next phase. Keep active task triplets in `.planning/<task>/`.

Development phase/task identifiers belong in these plans, progress records and
checkpoints only. They must not leak into production source filenames, product
identifiers, plugin technical names, environment variables or runtime log
contracts. Integration scaffolding must live under an unmistakable test/support
path, use behavior-based `test`/`fixture`/`harness` names, be explicitly opted in,
and never become a dependency of normal production behavior. See the repository
`AGENTS.md` test/proof isolation rules and each module's test documentation for
the concrete boundary.

## Vellum game UI - agent entry points

1. [Vellum program roadmap](plans/vellum-program-roadmap.md): program-level V0-V9 decomposition.
   **V0 is CLOSED** and **V1 Runtime Kernel is ACTIVE** as of 2026-10-10; V2-V9 remain unauthorized.
2. [V0 architecture plan](plans/vellum-v0-architecture-plan.md): closed architecture-only phase and
   exact V1 implementation-entry boundary. V0 Definition plus P0-P6 are closed.
3. [V1 runtime-kernel plan](plans/vellum-v1-runtime-kernel-plan.md): active production runtime-kernel
   phase. Its Definition starts from the closed V0 entry contract; V2 is not authorized.
4. [Vellum system plan](plans/vellum-ui-system-plan.md): closed architectural constitution that
   locks REDengine ink as the game-UI capability floor, selected Slate/UMG mechanisms, compact
   cooked runtime direction, semantic invalidation and the broad layout/data/input/text/render/
   surface constraints inherited by every later phase.
5. [Vellum reference study](studies/vellum-ui-reference-study.md): source-level comparison
   against the local REDengine ink and Unreal Slate/UMG implementations, including the
   anti-naive constraints that must hold before the first runtime implementation.
6. Active V1 task triplet: `.planning/vellum-v1-runtime-kernel/{task_plan,findings,progress}.md`.
7. Closed V0 task triplet: `.planning/vellum-v0-architecture/{task_plan,findings,progress}.md`.
   The former `.planning/vellum-ui-system/` triplet records the closed pre-roadmap Definition.

Vellum is the game-facing UI system for Vanguard titles. It is intentionally separate from the
Vanguard Editor's Dear ImGui UI; future editor authoring tools may consume Vellum assets without
making the Vellum runtime depend on editor UI code.

## Editor shell foundation - agent entry points

1. [Editor roadmap](plans/editor-foundation-study-and-roadmap.md): E0-E7 intent,
   dependencies and phase exits.
2. [Editor UI integration](designs/editor-ui-integration.md): accepted E0
   ownership, native-host, rendering, input and remaining interactive boundaries.
3. [E3 plan](plans/editor-foundation-e3-plan.md): ordered E3.1-E3.7 shell
   contracts and cross-phase invariants.
4. [E3.1 plan](plans/editor-foundation-e3-1-plan.md): cross-agent shell ownership
   and static extension-lifecycle contract. P0-P6 are closed and E3.1 is accepted
   after final native/focused validation and independent 0 BLOCKER / 0 HIGH /
   0 MEDIUM review.
5. [E3.2 plan](plans/editor-foundation-e3-2-plan.md): bounded command, contextual
   receiver and shortcut-routing execution contract. Definition plus P0-P6 are closed;
   **E3.2 is accepted** after fresh Debug + Development focused/regression execution,
   production/native validation, final hygiene and independent exact-CURRENT
   0 BLOCKER / 0 HIGH / 0 MEDIUM review.
6. [E3.3 plan](plans/editor-foundation-e3-3-plan.md): panel type/instance ownership,
   docking and focus-context integration contract. The **definition, P0, P1 and P2 are
   closed**; P2 closed at independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM
   implementation review. The user changed validation policy so broad Premake/build/test/
   native acceptance validation is batched into P6 instead of repeated per intermediate P.
   **P3 centralized top-level panel windows/default docking is also closed** after the
   first independent review's sole close/reopen proof MEDIUM was corrected and fresh
   post-fix independent exact-CURRENT review returned 0 BLOCKER / 0 HIGH / 0 MEDIUM.
   **P4 same-frame panel focus/Local-context integration is also closed** after coherent
   source implementation and fresh independent exact-CURRENT 0 BLOCKER / 0 HIGH /
   0 MEDIUM review. **P5 native docking/detachment and remaining E0F interactive evidence
   is also closed** after exact-current visible and lifecycle native passes plus fresh
   independent 0 BLOCKER / 0 HIGH / 0 MEDIUM review. **P6 is closed and E3.3 is accepted**
   after exact-current Debug/Development, lifecycle, native and hygiene validation plus fresh
   post-fix independent 0 BLOCKER / 0 HIGH / 0 MEDIUM review. The E3.4 definition
   subsequently closed after correction of all independent findings and fresh post-fix
   architecture, documentation/status and adversarial 0 BLOCKER / 0 HIGH / 0 MEDIUM
   reviews. The user subsequently authorized **E3.4 P0 only**; P0 is now closed after
   exact-current source/planning preflight and fresh post-fix independent architecture/
   test-shape review at 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user subsequently authorized
   **E3.4 P1 only**; P1 bounded surface identity/placement/registry-core implementation
   is now closed after source-level hygiene and fresh exact-CURRENT independent
   0 BLOCKER / 0 HIGH / 0 MEDIUM review. The user subsequently authorized
   **E3.4 P2 only**; P2 extension ownership and same-owner command-reference integration
   is now closed after scoped source/static hygiene and fresh exact-CURRENT independent
   0 BLOCKER / 0 HIGH / 0 MEDIUM review. The user subsequently authorized
   **E3.4 P3 only**; P3 primary persistent-surface composition is now closed after
   scoped source/static hygiene and fresh exact-CURRENT independent 0 BLOCKER / 0 HIGH /
   0 MEDIUM review. The user subsequently authorized **E3.4 P4 only**; transactional
   context-menu composition is now closed after scoped source/static hygiene and fresh
   exact-CURRENT independent 0 BLOCKER / 0 HIGH / 0 MEDIUM review. Broad execution
   remains deferred to P6. The user subsequently authorized **E3.4 P5 only**; the
   production-shaped native fixture/telemetry/runner source is now closed after correction
   of the initial 0 BLOCKER / 2 HIGH / 3 MEDIUM oracle review and fresh post-fix
   0 BLOCKER / 0 HIGH / 0 MEDIUM review. No generation/build/test/native execution occurred
   in P5. The user subsequently authorized **E3.4 P6**; fresh generation, serialized Debug
   + Development build/test execution, lifecycle/native acceptance, final hygiene and fresh
   independent 0 BLOCKER / 0 HIGH / 0 MEDIUM review are complete. **P6 is closed and E3.4
   is accepted.** The user subsequently authorized **E3.5 definition work only**; that
   definition is now closed at fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM. The user then
   authorized **E3.5 P0 only**; P0 is now closed after exact-current source/planning locks,
   corrected findings, final 0 BLOCKER / 0 HIGH / 0 MEDIUM review and static/document
   hygiene. P1 is now closed after exact-current source/static implementation, final
   0 BLOCKER / 0 HIGH / 0 MEDIUM review and scoped hygiene; no generation/build/executable/
   native execution occurred in P1. The user subsequently authorized **E3.5 P2 only**;
   P2 is now closed after typed Activity source/snapshot/cancellation/shell-ownership
   implementation, fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and scoped
   hygiene, with no P2 generation/build/executable/native execution. The user subsequently
   authorized **E3.5 P3 only**; P3 Console presentation is now closed after fresh final
   0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and scoped hygiene, with no P3
   generation/build/executable/native execution. The user subsequently authorized
   **E3.5 P4 only**; P4 Background Activity + the first display-only project-asset
   adapter is now closed after fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static
   review and scoped hygiene, with no P4 generation/build/executable/native execution.
   The user subsequently authorized **E3.5 P5 only**; P5 lifecycle hardening and the
   production-shaped integration fixture are now closed after fresh independent exact-current
   fixture/runner and architecture/lifecycle 0 BLOCKER / 0 HIGH / 0 MEDIUM reviews plus
   scoped source/static hygiene, with no P5 generation/build/executable/native execution.
   The user subsequently authorized **E3.5 P6**. P6 is now closed after fresh generation,
   serialized Debug + Development full builds and focused execution, both lifecycle paths,
   both noninteractive native configurations, warned Debug real-input native acceptance,
   final hygiene and fresh independent exact-current 0/0/0 reviews. The exact 512-entry
   diagnostics-ingress correction and historical P2 conservative-reservation oracle are
   covered by that acceptance. **E3.5 is accepted.** The user subsequently authorized
   **E3.6 definition work only**; that definition is now closed after fresh post-fix
   independent 0 BLOCKER / 0 HIGH / 0 MEDIUM reviews. The user subsequently authorized
   **E3.6 P0 only**; P0 is now closed after final exact-current independent 0/0/0 source/
   planning reviews and static/document hygiene. E3.6 P1 is also closed after final exact-current
   independent 0/0/0 source/static reviews and scoped hygiene with no generation/build/
   executable/native execution. E3.6 P2 is also closed after final exact-current independent
   0/0/0 source/static reviews and scoped hygiene with no generation/build/executable/native
   execution. E3.6 P3 is also closed after final exact-current independent 0/0/0 architecture/
   lifecycle/source-static and adversarial/source-oracle reviews plus scoped hygiene, with no
   generation/build/executable/native execution. E3.6 P4 is also closed after final exact-current
   independent 0/0/0 architecture/lifecycle/source-static and adversarial/source-oracle reviews
   plus scoped hygiene, with no generation/build/executable/native execution. P5 is closed after
   clean source/static fixture review, and P6 is closed after focused compile/link and passing
   UserState + noninteractive persistence restart proof; **E3.6 is accepted**. E3.7 P0-P2 are
   closed; **E3.7 and Editor Foundation E3 are accepted.** The user later separately authorized
   E4 Definition, P0 and P1; all three are now **CLOSED**, while E4 P2-P6 remain unauthorized.
7. [E3.4 plan](plans/editor-foundation-e3-4-plan.md): declarative menu, toolbar and
   context-menu composition over the accepted E3.2 command route and E3.3 focus/panel
   lifecycle. **Definition, P0 source/planning review, P1 bounded surface
   identity/placement/registry-core implementation, P2 extension ownership/same-owner
   command-reference integration and P3 primary persistent-surface composition are
   closed; P4 transactional context-menu composition is also closed at 0 BLOCKER /
   0 HIGH / 0 MEDIUM source/static review; P5 production-shaped native fixture/runner
   preparation is also closed at post-fix 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static
   review; P6 comprehensive acceptance is also closed at final exact-CURRENT
   0 BLOCKER / 0 HIGH / 0 MEDIUM. **E3.4 is accepted.** E3.5 definition is now closed at
   fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM; E3.5 P0 is also closed after final 0/0/0
   source/planning review and static/document hygiene, and P1 is closed after final 0/0/0
   source/static review and scoped hygiene with broad execution deferred. E3.5 P2 is also
   closed after final 0/0/0 source/static review and scoped hygiene; E3.5 P3 is also closed
   after final 0/0/0 source/static review and scoped hygiene. E3.5 P4 is also closed after
   final 0/0/0 source/static review and scoped hygiene; E3.5 P5 is also closed after final
   independent 0/0/0 source/static reviews and scoped hygiene with no generation/build/
   executable/native execution. E3.5 P6 is now closed after the complete Debug + Development
   build/test/lifecycle/native acceptance matrix, final hygiene and fresh independent 0/0/0
   reviews. E3.5 is accepted. E3.6 definition is closed at fresh final 0/0/0; E3.6 P0 is
   also closed after final exact-current 0/0/0 source/planning reviews and static/document
   hygiene. The user subsequently authorized **E3.6 P1 only**; P1 is CLOSED after final
   exact-current independent 0/0/0 source/static reviews and scoped hygiene with no generation/
   build/executable/native execution. The user subsequently authorized **E3.6 P2 only**; P2 is
   CLOSED after final exact-current independent 0/0/0 source/static reviews and scoped hygiene
   with no generation/build/executable/native execution. The user subsequently authorized
   **E3.6 P3 only**; P3 is CLOSED after final exact-current independent 0/0/0 architecture/
   lifecycle/source-static and adversarial/source-oracle reviews plus scoped hygiene, with no
   generation/build/executable/native execution. E3.6 P4 is also CLOSED after final exact-current
   independent 0/0/0 architecture/lifecycle/source-static and adversarial/source-oracle reviews
   plus scoped hygiene, with no generation/build/executable/native execution. E3.6 P5 and P6 are
   now closed; final P6 leaves the production editor compiled and passes the focused persistence
   proof. **E3.6 is accepted**; E3.7 P0-P2 are now closed after the compact architecture
   lock, production demonstration-extension implementation and focused final compile/proof;
   **E3.7 and Editor Foundation E3 are accepted.** E4 Definition, P0 and P1 are **CLOSED**; P2-P6 remain unauthorized.

   The visual theme/styling pass is explicitly outside E3.4. The user supplied CryEngine, Maya and
   3ds Max references and authorized **P0, P1 and P2** of the separate pre-E4
   [Editor visual foundation](plans/editor-visual-foundation-plan.md); CryEngine is the primary
   reference. Exact-current source includes the later narrow-window hit-test correction, but no separate
   user message explicitly accepting/closing visual P2 is recorded before E4 Definition authorization.
8. [E3.5 plan](plans/editor-foundation-e3-5-plan.md): bounded diagnostics ingestion,
   typed progress/status adapters, cancellation-request routing and lifecycle-safe
   Console/Background Activity presentation over existing backend owners. **Definition
   is closed at fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM; P0 is also closed after final
   0 BLOCKER / 0 HIGH / 0 MEDIUM source/planning review and static/document hygiene;
   P1 is also closed after final 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and
   scoped hygiene with no generation/build/executable/native execution; P2 is also closed
   after final 0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and scoped hygiene with
   no P2 generation/build/executable/native execution. P3 is also closed after final
   0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and scoped hygiene with no P3
   generation/build/executable/native execution; P4 is also closed after final
   0 BLOCKER / 0 HIGH / 0 MEDIUM source/static review and scoped hygiene with no P4
   generation/build/executable/native execution; P5 is also closed after final independent
   0 BLOCKER / 0 HIGH / 0 MEDIUM source/static fixture/runner and architecture/lifecycle
   reviews with no P5 generation/build/executable/native execution. P6 remained outside P5;
   it is now closed after fresh generation, complete serialized Debug + Development build/
   test/lifecycle/native acceptance, the exact ingress-capacity correction, the passing
   historical P2 conservative-reservation oracle, final hygiene and fresh independent 0/0/0
   reviews. **E3.5 is accepted.** E3.6 definition is closed at fresh final 0/0/0; E3.6 P0
   is also closed after final exact-current 0/0/0 source/planning reviews and static/document
   hygiene. P1 is also CLOSED after final exact-current independent 0/0/0 source/static reviews
   and scoped hygiene with no generation/build/executable/native execution; P2 is also CLOSED
   after final exact-current independent 0/0/0 source/static reviews and scoped hygiene with no
   generation/build/executable/native execution; P3 is also CLOSED after final exact-current
   independent 0/0/0 architecture/lifecycle/source-static and adversarial/source-oracle reviews
   plus scoped hygiene with no generation/build/executable/native execution; P4 is also CLOSED
   after final exact-current independent 0/0/0 architecture/lifecycle/source-static and
   adversarial/source-oracle reviews plus scoped hygiene with no generation/build/executable/
   native execution. E3.6 P5 and P6 are now closed; P6 completed fresh generation, focused Debug
   compile/link and the required UserState + restart/rejection/final-save proof. **E3.6 is
   accepted**; E3.7 P0-P2 are now closed; **E3.7 and Editor Foundation E3 are accepted.**
   E4 Definition, P0 and P1 are **CLOSED**; P2-P6 remain unauthorized/unstarted.
9. [E3.6 plan](plans/editor-foundation-e3-6-plan.md): versioned project-local editor
   layout/user-preference persistence over the accepted shell, panel, command, window and
   filesystem owners. **Definition is closed at fresh final 0 BLOCKER / 0 HIGH / 0 MEDIUM.**
   **P0 is now closed after final exact-current independent 0 BLOCKER / 0 HIGH / 0 MEDIUM
   source/planning reviews and static/document hygiene.**
   The closed definition locks
   one dependency-ordered user-state owner, bounded/canonical Saved state, private manual
   ImGui persistence, defensive topology/DPI recovery, transactional shortcut restore,
   Shell-owned typed preferences and complete sibling-stage replacement. **P1 is now CLOSED**
   after final exact-current independent 0/0/0 source/static reviews and scoped hygiene with no
   generation/build/executable/native execution. **P2 is now CLOSED** after final exact-current
   independent 0/0/0 architecture/lifecycle/source-static and adversarial/source-oracle reviews
   plus scoped hygiene, with no generation/build/executable/native execution. **P3 is now CLOSED**
   after final exact-current independent 0/0/0 architecture/lifecycle/source-static and
   adversarial/source-oracle reviews plus scoped hygiene, with no generation/build/executable/
   native execution. **P4 is now CLOSED** after final exact-current independent 0/0/0
   architecture/lifecycle/source-static and adversarial/source-oracle reviews plus scoped hygiene,
   with no generation/build/executable/native execution. **P5 and P6 are now CLOSED and E3.6 is
   accepted**; final P6 completed focused compile/link plus the passing UserState and nine-phase
   noninteractive persistence proof. E3.7 P0-P2 are now closed after the compact architecture
   lock, production demonstration-extension implementation and focused final compile/proof;
   **E3.7 and Editor Foundation E3 are accepted.** E4 Definition, P0 and P1 are **CLOSED**; P2-P6 remain unauthorized.
10. [E3.7 plan](plans/editor-foundation-e3-7-plan.md): one project-selectable production
    demonstration extension using the public E3 shell surfaces, followed by a focused final
    compile/proof gate. **P0-P2 are closed; E3.7 and Editor Foundation E3 are accepted.**
11. [Editor visual foundation](plans/editor-visual-foundation-plan.md): pre-E4 visual-system work
    over the accepted ImGui backend. P0 is closed; P1 is accepted enough to move on; P2 was explicitly
    authorized and the exact-current narrow-window hit-test correction is present. No separate user message
    explicitly accepting/closing P2 is recorded before E4 Definition authorization, so E4 authorization is
    not treated as retroactive visual acceptance.
12. [Editor Foundation E4 plan](plans/editor-foundation-e4-plan.md): Asset/Project Browser plus first
    import, settings mutation and ordinary runtime texture-preview delivery contract. **Definition, P0 and P1 are
    CLOSED; P2-P6 remain unauthorized/unstarted.** P1 implements the shared Candidate-adoption/settings backend,
    bounded allocation-free generated-identity admission and durable conditional VMETA transaction path; no
    generation/build/executable/native execution occurred in that gate.

## Project asset foundation - agent entry points

1. [Editor roadmap](plans/editor-foundation-study-and-roadmap.md): phase intent.
2. [Foundation progress](progress/project-asset-foundation-progress.md): accepted
   work and current boundaries.
3. [E2 plan](plans/project-asset-e2-plan.md): ordered E2 contracts.
4. [E2.3 plan](plans/project-asset-e2-3-plan.md): implemented and independently
   accepted build-coordination contract and evidence.
5. [E2.4 plan](plans/project-asset-e2-4-plan.md): implemented, validated and
   independently accepted; E2.4 is closed.
6. [E2.5 plan](plans/project-asset-e2-5-plan.md): cross-agent VDDC lifecycle and
   safe loose-generation reclamation contract. P0-P5 are closed after the final
   exact-CURRENT **0 BLOCKER / 0 HIGH / 0 MEDIUM** P5 review. P6 is authorized;
   fresh generation, Debug/Development compile/link, focused execution and
   hygiene/scope validation are complete. The final docs-inclusive independent
   exact-CURRENT P6 review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**; P6 and
   E2.5 are closed.
7. [E2.6 plan](plans/project-asset-e2-6-plan.md): headless surface, lifecycle and
   complete-E2 validation contract. A stricter definition audit found one MEDIUM
   in Generated-dependency freshness semantics after an earlier 0/0/0 review;
   the contract was corrected and final strict re-review returned **0 BLOCKER /
   0 HIGH / 0 MEDIUM**. The definition is closed. The user subsequently
   authorized P0 only; its read-only exact-CURRENT architecture/task-plan lock
   corrected a 0/3/2 independent preflight and final strict review returned
   **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P0 and P1 subsequently closed at final
   independent **0 BLOCKER / 0 HIGH / 0 MEDIUM**. P2 was separately authorized,
   implemented and closed after fresh Debug/Development focused validation,
   scope/dependency audits and final independent **0 BLOCKER / 0 HIGH / 0
   MEDIUM** review. P3 was then separately authorized and closed after the
   bounded nanovanguard asset command/protocol implementation, exact-current
   Debug/Development build plus 20/20 runtime invocations per configuration,
   production CLI smoke, dependency/scope/hygiene audit and final independent
   **0 BLOCKER / 0 HIGH / 0 MEDIUM** review. P4 was then separately authorized;
   its maintenance/mutation/lifecycle implementation, focused Debug/Development
   execution, production smoke and final independent **0 BLOCKER / 0 HIGH / 0
   MEDIUM** reviews are complete. The user then changed the remaining intermediate
   validation policy so broad Premake/build/test/native acceptance is batched into
   P6 while independent exact-CURRENT checks remain required. **P4 is closed**;
   the unrelated dirty world-session Premake audit was deferred to P6 and the
   fresh P6 generation now passes it. The user subsequently authorized P5 only;
   its deterministic test-only
   matrix implementation is source-complete and final independent exact-CURRENT
   review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**, so P5 is closed under the
   updated intermediate-gate policy. No P5 build/runtime pass is claimed; broad
   execution was deferred to P6. The user subsequently authorized P6; fresh
   generation, serialized Debug/Development compile/link, owner/test execution,
   42/42 headless matrices per configuration, rebuilt production CLI scratch
   execution and final dependency/scope/hygiene validation are complete. The P6
   matrix found and corrected one test-only create-vs-overwrite fixture defect
   without changing production freshness behavior. Final independent
   exact-CURRENT P6 closure review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
   **P6, E2.6 and complete E2 are closed.** Physical Ctrl+C/Ctrl+Break remains
   unexecuted/unclaimed.
8. [VPAS](../formats/vpas-format.md), [VMETA](../formats/vmeta-format.md) and
   [VTXN](../formats/vtxn-format.md): persistence and authored transaction rules.

## Rendering and residency entry points

- [World rendering 10.4.3 plan](plans/world-rendering-10-4-3-plan.md):
  renderer/world draw-contract and fixture compatibility; 10.4.3 is complete.
- [World rendering 10.4.4 plan](plans/world-rendering-10-4-4-plan.md):
  camera activation and streaming-observer binding; P0 and P1 are closed, with
  P1 accepted at independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM. P2 is
  also closed at final independent exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM.
  P3 is closed after implementation, scoped source/hygiene review and an independent
  exact-CURRENT read-only 0 BLOCKER / 0 HIGH / 0 MEDIUM review; its executable
  validation is batched into P6. P4 (explicit streaming-observer ownership) is closed
  at a fresh independent exact-CURRENT read-only 0 BLOCKER / 0 HIGH / 0 MEDIUM
  verdict after its peer findings were corrected. P5 (deterministic lifecycle and
  integration proof) is also closed at an independent exact-CURRENT read-only
  0 BLOCKER / 0 HIGH / 0 MEDIUM verdict. Its proof matrix was unexecuted at P5
  closure and now passes in both Debug and Development under the authorized P6
  batch. **P6 and 10.4.4 are closed** at a final independent exact-CURRENT
  0 BLOCKER / 0 HIGH / 0 MEDIUM verdict.
- [World rendering 10.4.5 plan](plans/world-rendering-10-4-5-plan.md): production
  runtime viewport frames and editor scene-texture output. The definition gate is
  closed at a fresh independent exact-CURRENT **0 BLOCKER / 0 HIGH / 0 MEDIUM**
  re-review; **P0 is also closed at a fresh independent exact-CURRENT 0 BLOCKER /
  0 HIGH / 0 MEDIUM re-review. P1 is closed at a fresh independent exact-CURRENT
  0 BLOCKER / 0 HIGH / 0 MEDIUM review. P2, P3, P4 and P5 are also closed at fresh independent
  exact-CURRENT 0 BLOCKER / 0 HIGH / 0 MEDIUM reviews. P6 is closed after fresh Premake,
  serialized Debug/Development focused execution, bootstrap/runtime/editor native smokes,
  scoped ownership/hygiene checks and a final independent exact-CURRENT **0 BLOCKER /
  0 HIGH / 0 MEDIUM** review. **10.4.5 is closed; 10.4.6 was not started as part of
  that phase.** P3 adds the bounded editor scene-output generation/late exact-receipt
  handoff; P4 adds the minimal SDR resolve plus exact native extent/surface transition
  behavior; P5 authors the deterministic runtime/editor lifecycle, readiness, generation,
  one-FrameTick/multi-output and retained-Present ownership matrix. P6 executed that matrix
  in both configurations, added the real-RHI action-boundary regression for the discovered
  RenderFlow pending-barrier defect, and used a Tests/Editor-only camera package to prove a
  clean editor exit without absorbing the known 10.4.6 terminal `LightComponent` drain gap.
  It preserves
  graph-tail presentation plus the existing previous-render dispatch flush, and
  defines the missing texture producer state/queue/readiness and generation-safe
  editor handoff without adding a second graph, swapchain or texture registry.
- [World rendering 10.4.6 plan](plans/world-rendering-10-4-6-plan.md): session
  replacement and terminal draining. **Definition and P0-P7 are closed; 10.4.6 is
  closed.** Its final state uses the root `AGENTS.md` proportional-validation rule:
  relevant source was left compiling, focused session/replacement/drain and P6
  ownership/cardinality proofs were green, scoped hygiene was clean and the final
  independent exact-current source review returned **0 BLOCKER / 0 HIGH / 0 MEDIUM**.
  Historical broader editor/native acceptance rows are deferred/not run rather than
  closure requirements.
  It closes the G6 boundary exposed by the
  normal 10.4.5 cooked editor path: a stopping session must keep narrow renderer
  publication/residency/retirement progress available until renderer-backed
  components and their RenderScene can release, while preserving explicit Pending
  versus terminal failure, latching terminal faults until accepted ownership is
  safely terminalized, admitting replacement only from `Idle`, and never running
  `GameWorld::Tick`/the gameplay frame as stop progress. The
  plan also used a dedicated P6 production-overhaul gate for integration bloat,
  high-cardinality algorithmic/synchronization hazards, RED-like structural
  cleanliness in Vanguard's ownership model, and strict test/proof/phase isolation.
- [World rendering 10.4.7 plan](plans/world-rendering-10-4-7-plan.md): real cooked and
  project-derived execution proof. **Definition and P0-P3 are closed; 10.4.7 is closed.**
  Focused Development/D3D12 execution proved the normal cooked runtime path to visible
  pixels, production project observation/cook/loose publication plus already-resident
  VTEX cutover through the existing streamer/residency owners, the fixed editor live
  camera + production Scene panel, clean 10.4.6 teardown, and bounded real CPU/GPU/
  geometry diagnostics. The final independent exact-current review is 0 BLOCKER /
  0 HIGH / 0 MEDIUM. The slice deliberately did not become a broad regression matrix,
  full editor viewport-tool phase or new asset/rendering subsystem.
- [Render graph design](designs/render-graph-design.md) and
  [execution plan](plans/render-graph-execution-plan.md).
- [Resource flow allocator design](designs/resource-flow-allocator-design.md) and
  [execution plan](plans/resource-flow-allocator-execution-plan.md).
- [Mesh residency design](designs/mesh-residency-design.md) and
  [execution plan](plans/mesh-residency-execution-plan.md).
- [Material design](designs/material-system-design.md) and
  [study plan](studies/material-system-study-plan.md).
- [Editor UI integration](designs/editor-ui-integration.md).

The former flat development documents were moved into these folders without
dropping their history. Links and repository-relative references were updated.
Add new documents to the matching purpose folder and link relevant entry points;
do not accumulate another flat set beside this index.
