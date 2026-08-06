#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vanguard::texture_tools::tests
{
    struct EncodedFixture final
    {
        std::array<std::uint8_t, 65536> bytes{};
        std::size_t size = 0;
    };

    [[nodiscard]] bool MakeTiledTiff16(EncodedFixture& output) noexcept;
    [[nodiscard]] bool MakeScanlineTiffFloat(EncodedFixture& output) noexcept;
    [[nodiscard]] bool MakeScanlineOpenExrFloat(EncodedFixture& output) noexcept;
}
