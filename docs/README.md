# Documentation map

Start with [repository layout](REPOSITORY_LAYOUT.md) for physical ownership.

- `architecture/`: shared engine ownership and implemented subsystem contracts.
- `formats/`: versioned on-disk/wire format specifications.
- `engineering/`: coding, build and dependency standards.
- `migration/`: compatibility and migration guidance.
- [development/](development/README.md): roadmaps, implementation plans, designs,
  research and progress, organized by document purpose.

Module-local documentation remains with its owner under `source/<module>/docs/`.
Root `AGENTS.md` remains the agent entry point. `.planning/<task>/` holds scoped
execution plans, findings and validation ledgers; it is not a specification tree.
Historical checkpoints are context, not current completion authority.
