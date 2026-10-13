#pragma once

#include <vanguard/system/types.hpp>

#include <limits>

namespace vanguard::vellum
{
    inline constexpr u32 InvalidIndex = 0xffffffffu;
    inline constexpr u16 InvalidPropertySlot = 0xffffu;
    inline constexpr u64 InvalidAuthoredId = 0;

    using AuthoredId = u64;

    struct VellumInstanceHandle
    {
        u32 index = InvalidIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const VellumInstanceHandle&, const VellumInstanceHandle&) noexcept = default;
    };

    struct VellumSurfaceHandle
    {
        u32 index = InvalidIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const VellumSurfaceHandle&, const VellumSurfaceHandle&) noexcept = default;
    };

    struct LocalNodeHandle
    {
        u32 slot = InvalidIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return slot != InvalidIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const LocalNodeHandle&, const LocalNodeHandle&) noexcept = default;
    };

    struct VellumNodeHandle
    {
        VellumInstanceHandle instance;
        LocalNodeHandle local;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return instance.IsValid() && local.IsValid();
        }

        [[nodiscard]] friend constexpr bool operator==(const VellumNodeHandle&, const VellumNodeHandle&) noexcept = default;
    };

    struct SpawnHandle
    {
        u32 index = InvalidIndex;
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidIndex && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const SpawnHandle&, const SpawnHandle&) noexcept = default;
    };

    enum class NodeKind : u8
    {
        Invalid,
        StackPanel,
        Spacer
    };

    enum class Visibility : u8
    {
        Visible,
        Hidden,
        Collapsed
    };

    enum class Orientation : u8
    {
        Horizontal,
        Vertical
    };

    enum class PropertyType : u8
    {
        Invalid,
        Float,
        Visibility,
        Orientation
    };

    enum class PropertyId : u16
    {
        Invalid = 0,
        Width = 1,
        Height = 2,
        Visibility = 3,
        Opacity = 4,
        PanelOrientation = 5,
        PanelSpacing = 6
    };

    enum class InvalidationDomain : u16
    {
        None = 0,
        TextShape = 1u << 0u,
        Measure = 1u << 1u,
        Arrange = 1u << 2u,
        Paint = 1u << 3u,
        HitTest = 1u << 4u,
        Navigation = 1u << 5u
    };

    [[nodiscard]] constexpr InvalidationDomain operator|(const InvalidationDomain left, const InvalidationDomain right) noexcept
    {
        return static_cast<InvalidationDomain>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr InvalidationDomain operator&(const InvalidationDomain left, const InvalidationDomain right) noexcept
    {
        return static_cast<InvalidationDomain>(static_cast<u16>(left) & static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasInvalidation(const InvalidationDomain value, const InvalidationDomain domain) noexcept
    {
        return (value & domain) != InvalidationDomain::None;
    }

    struct PropertyHandle
    {
        VellumNodeHandle node;
        u16 slot = InvalidPropertySlot;
        PropertyType expectedType = PropertyType::Invalid;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return node.IsValid() && slot != InvalidPropertySlot && expectedType != PropertyType::Invalid;
        }

        [[nodiscard]] friend constexpr bool operator==(const PropertyHandle&, const PropertyHandle&) noexcept = default;
    };

    struct Point
    {
        f32 x = 0.0f;
        f32 y = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(const Point&, const Point&) noexcept = default;
    };

    struct Size
    {
        f32 width = 0.0f;
        f32 height = 0.0f;

        [[nodiscard]] friend constexpr bool operator==(const Size&, const Size&) noexcept = default;
    };

    struct Rect
    {
        Point position;
        Size size;

        [[nodiscard]] friend constexpr bool operator==(const Rect&, const Rect&) noexcept = default;
    };

    struct LayoutConstraints
    {
        Size minimum;
        Size maximum{std::numeric_limits<f32>::infinity(), std::numeric_limits<f32>::infinity()};

        [[nodiscard]] friend constexpr bool operator==(const LayoutConstraints&, const LayoutConstraints&) noexcept = default;
    };

    struct Geometry
    {
        Rect rect;
        u64 generation = 0;
    };

    enum class SpawnState : u8
    {
        Invalid,
        Queued,
        Loading,
        Materializing,
        ReadyToCommit,
        Succeeded,
        Failed,
        Cancelled
    };
} // namespace vanguard::vellum
