# Math

Vanguard Math owns the engine's scalar, vector, matrix, rotation, transform,
geometry, high-precision world-coordinate, interpolation, random, half-float,
and explicit SIMD contracts.

Include:

```cpp
#include <vanguard/math/math.hpp>
```

Use types through `vanguard::math`; runtime and editor code must not include
files from `source/imported` directly.

This phase preserves RED Math behavior and layout because the implementation is
already mature, inline, and SIMD-heavy. The complete source image is
reproducibly adapted so canonical type names and symbols belong to
`vanguard::math`; there is no per-operation compatibility call. See
`UPSTREAM.md` for provenance and `docs/architecture/math-capabilities.md` for
the deliberate boundary.
