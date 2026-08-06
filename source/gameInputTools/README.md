# Game Input Tools

`gameInputTools` converts editor-owned text mappings into deterministic `vinput` resources and registers that conversion with the Vanguard asset/DDC build system.

Editor source files use the `.inputmap` extension. Cooked `.vinput` files live in DerivedData/DDC and runtime packages; source files remain in the project's resource tree.

The source grammar is deliberately small, strict, diff-friendly, and independent of runtime serialization:

```text
vinput 1
context gameplay player 0 1
action jump button 0 0.30 0.20 2 0.20 0.25 0.40 0.10 0.00 1.00 1.00 0 0
curve move 0.00 0.00
curve move 1.00 1.00
binding jump.keyboard gameplay jump key 44 scalar 1.0 0.5 0.4 1
binding move.forward gameplay move key 26 scalar 1.0 0.5 0.4 1 key 225
```

Context fields are `name layer priority initiallyActive`. Action fields are `name type priority hold tap multiTapCount multiTapDown multiTapGap repeatDelay repeatInterval deadzoneInner deadzoneOuter sensitivity toggle consumeControl`. Binding fields are `name context action controlType controlCode component scale pressThreshold releaseThreshold overridable`, followed by zero to four `modifierType modifierCode` pairs. Control codes use Vanguard's stable physical input enum values, so the editor can present localized names without baking platform library constants into source data.
