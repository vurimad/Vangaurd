# RED Math adaptation

## Source

The complete working image comes from:

```text
D:\root\R6.Root\Mainline\dev\src\common\redMath
```

The quarantined repository image is:

```text
source/imported/common/redMath
```

The previous `D:\RED Vanguard` integration was consulted for the known Windows
build exclusions and project dependencies.

`scripts/adapt-red-math.ps1` reproducibly copies that image to
`source/math/adapted` and changes the canonical `math` and `simd` namespaces to
`vanguard::math` and `vanguard::math::simd`. Do not hand-maintain a second
partial implementation.

## Adaptation policy

- Keep RED's inline and SIMD implementations intact.
- Preserve type size, alignment, coordinate conventions, normalization edge
  behavior, and floating-point behavior.
- Expose the image only through `vanguard/math/math.hpp`.
- Do not create friendlier implicit behavior where RED asserts or requires an
  explicit operation.
- Materialize missing dependencies when a consuming adaptation proves they are
  needed.
- Changes to the imported image must be documented here and kept minimal.
- A file's presence in the depot is not proof that RED supported it as a
  standalone public header. Broken historical headers remain quarantined until
  a real dependency requires materialization.

## Current build exclusions

`redScalar_simd.cpp` and `vectorFunctions_float.cpp` are excluded exactly as in
the prior known-good integration. Their active implementations are supplied by
the current SIMD headers/sources; compiling these legacy translation units
would duplicate obsolete paths.

## Local compatibility corrections

- `doubleVector3.h` used a depot-era relative include for
  `redSystem/compilerExtensions.h`. It now resolves the quarantined repository
  layout through `../../redSystem/include/compilerExtensions.h`; no math
  behavior changed.
