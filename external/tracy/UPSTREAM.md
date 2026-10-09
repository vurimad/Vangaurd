# Tracy upstream

- Repository: https://github.com/wolfpld/tracy
- Revision: `b1fef9f334085ff2e0a0eda26b7fc166da95889e`
- Description: `v0.14.1-170-gb1fef9f3`
- Repository copy: `external/tracy/upstream`
- Imported content: complete upstream worktree excluding `.git`
- License: BSD 3-Clause; see `upstream/LICENSE`
- Local source patches: none
- Owning Vanguard subsystem: profiling
- Public API exposure: Tracy types remain behind Vanguard profiling macros; engine modules must not include Tracy headers directly
- Upgrade responsibility: Vanguard core/runtime owners

Vanguard enables the Tracy client only for the `Profile` configuration. Debug, Development and Shipping builds compile Vanguard profiling call sites to no-ops so profiling instrumentation cannot become an accidental benchmark or shipping dependency.
