# Math capability boundary

Vanguard exposes the complete adapted RED Math image through one deliberate
public include and canonical `vanguard::math` types.

| Family | Capability |
|---|---|
| Canonical | `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix`, `Transform`, `EulerAngles` |
| Geometry | Plane, box, sphere, segment, quad, capsule, oriented box, tetrahedron, cylinder, cut cone |
| Generic | Color, integer/float rectangles, 2D/3D points |
| Precision | Double matrix, fixed point, world position, world transform |
| Storage | Half scalars/vectors and float-16 compression |
| Algorithms | Numerical utilities, interpolation, random, Perlin noise |
| SIMD | Scalar, vector, matrix, Q/S/T transform, box, quad, comparisons, structure-of-arrays helpers |

## Conventions retained from RED

- `Vector3` is a compact, unaligned 12-byte storage type.
- `Vector4`, `Quaternion`, `Matrix`, and `Transform` are 16-byte aligned.
- Euler rotations are counter-clockwise, expressed in degrees, in Y-X-Z order.
- `Transform` stores position plus normalized orientation; it does not contain
  scale.
- Vector operations that distinguish two, three, and four lanes keep their
  explicit RED names.
- Zero-vector normalization, inversion, comparison tolerances, and assertion
  behavior are not silently changed.

## Boundary

Runtime and editor modules include `vanguard/math/math.hpp` and name facilities
through `vanguard::math`. Direct includes from `source/imported/common/redMath`
are compatibility-layer implementation details.

This is intentionally a zero-overhead source and namespace adaptation of the
complete working image. It avoids an incomplete hand-written wrapper and
preserves inlining and SIMD code generation. The quarantined image continues
to serve legacy compatibility projects; Vanguard runtime symbols and RTTI use
`vanguard::math`.

Depot-only headers that RED itself did not place in its public umbrella are not
promoted merely because they exist in the image. `doubleVector3.h`,
`simdMatrix.h`, and `simdSoAHelper.h` currently depend on removed historical
types and remain quarantined until a real consuming subsystem requires them.
