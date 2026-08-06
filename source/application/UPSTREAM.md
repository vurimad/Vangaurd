# Upstream provenance

The lifecycle shape was derived from RED's application, engine, game-application, and runtime-scene layers, principally `app::AppInstance`, `CBaseEngine`, `GameAppInstance`, `world::RuntimeScene`, and `world::RuntimeSystem` under `D:/root/R6.Root/Mainline/dev/src/common`.

Retained architectural ideas include a small process bootstrap beneath engine services, distinct construction/initialization/running/shutdown stages, application-mode filtering, centralized ownership, orderly reverse teardown, and lifecycle observability.

Vanguard replaces RED's numeric initialization order, implicit cross-system dependencies, global access, and exceptional hard-coded shutdown ordering with explicit service and capability edges, deterministic graph compilation, immutable plans, generational observations, structured failure data, and rollback. No RED file format or depot/runtime dependency is introduced by this module.

The portable runner and state machine also retain RED's process-bootstrap and enter/tick/exit concepts while replacing static platform pumping, global exit state, raw integer state maps, and hardcoded state ownership with injected contracts, stable IDs, deferred transitions, asynchronous operation status, terminal exit precedence, and bounded graceful shutdown.
