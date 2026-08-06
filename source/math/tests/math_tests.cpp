#include <cmath>
#include <cstdio>
#include <type_traits>

#include <vanguard/math/math.hpp>

namespace
{
    int failures = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAILED: %s\n", message);
            ++failures;
        }
    }

    bool Near(float left, float right, float epsilon = 1.0e-4f)
    {
        return std::fabs(left - right) <= epsilon;
    }
} // namespace

int main()
{
    using namespace vanguard::math;

    static_assert(sizeof(Vector2) == 8);
    static_assert(sizeof(Vector3) == 12);
    static_assert(sizeof(Vector4) == 16);
    static_assert(sizeof(Quaternion) == 16);
    static_assert(sizeof(Matrix) == 64);
    static_assert(sizeof(Transform) == 32);
    static_assert(alignof(Vector4) == 16);
    static_assert(alignof(Matrix) == 16);
    static_assert(std::is_trivially_destructible_v<Vector4>);

    {
        const Vector3 x{1.0f, 0.0f, 0.0f};
        const Vector3 y{0.0f, 1.0f, 0.0f};
        const Vector3 z = x.Cross(y);
        Check(z == Vector3::EZ(), "Vector3 cross product follows RED handedness");
        Check(Near(x.Dot(y), 0.0f), "Vector3 dot product");

        Vector3 vector{3.0f, 4.0f, 0.0f};
        const float originalLength = vector.Normalize();
        Check(Near(originalLength, 5.0f), "Vector3 Normalize returns original length");
        Check(Near(vector.Mag(), 1.0f), "Vector3 Normalize produces unit vector");

        Vector3 zero = Vector3::ZEROS();
        Check(Near(zero.Normalize(), 0.0f) && zero.IsZero(), "RED zero-vector normalization semantics are preserved");
    }

    {
        const Vector4 point{1.0f, 2.0f, 3.0f, 1.0f};
        const Vector4 direction{1.0f, 2.0f, 3.0f, 0.0f};
        Check(Near(point.SquareMag3(), 14.0f), "Vector4 three-lane magnitude");
        Check(Near(point.Dot4(direction), 14.0f), "Vector4 four-lane dot product");
        Check(Vector4::Cross(Vector4::EX(), Vector4::EY(), 0.0f) == Vector4::EZ(), "Vector4 SIMD cross product");
    }

    {
        const EulerAngles angles{0.0f, 0.0f, 90.0f};
        const Quaternion rotation = angles.ToQuat();
        Check(rotation.IsOk(), "Euler-to-quaternion conversion");

        const Matrix matrix = angles.ToMatrix();
        const Vector4 transformed = matrix.TransformVector(Vector4::EY());
        Check(Near(transformed.X, -1.0f) && Near(transformed.Y, 0.0f), "RED Euler Y-X-Z convention is preserved");

        const Matrix identity = matrix * matrix.FullInverted();
        Check(Matrix::Near(identity, Matrix::IDENTITY(), 1.0e-3f), "Full matrix inversion");
    }

    {
        const Transform transform{Vector3{10.0f, 20.0f, 30.0f}, Quaternion::IDENTITY()};
        const Vector3 transformed = transform.TransformPoint(Vector3{1.0f, 2.0f, 3.0f});
        Check(Vector3::Near3(transformed, Vector3{11.0f, 22.0f, 33.0f}), "Transform point semantics");
        Check(Vector3::Near3(transform.GetInverse().TransformPoint(transformed), Vector3{1.0f, 2.0f, 3.0f}), "Transform inverse");
    }

    {
        Box box{Vector3{-1.0f, -2.0f, -3.0f}, Vector3{1.0f, 2.0f, 3.0f}};
        Check(box.Contains(Vector3::ZEROS()), "Box containment");
        Check(!box.Contains(Vector3{2.0f, 0.0f, 0.0f}), "Box exclusion");

        const Half half{1.5f};
        Check(Near(half.ToFloat(), 1.5f, 1.0e-3f), "Half conversion");
    }

    {
        const simd::Vector4 left{1.0f, 2.0f, 3.0f, 4.0f};
        const simd::Vector4 right{2.0f, 3.0f, 4.0f, 5.0f};
        const simd::Vector4 sum = simd::Add(left, right);
        alignas(16) float lanes[4]{};
        sum.Store(lanes);
        Check(Near(lanes[0], 3.0f) && Near(lanes[1], 5.0f) && Near(lanes[2], 7.0f) && Near(lanes[3], 9.0f),
              "Explicit SIMD vector arithmetic");
    }

    if (failures == 0)
    {
        std::puts("mathTests: all RED Math conformance checks passed");
    }
    return failures == 0 ? 0 : 1;
}
