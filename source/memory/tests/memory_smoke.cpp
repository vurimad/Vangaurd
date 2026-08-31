#include <vanguard/memory/memory.hpp>

#include <cstdint>
#include <cstring>
#include <limits>

namespace
{
    bool IsAligned(const void* address, const vanguard::usize alignment)
    {
        return (reinterpret_cast<std::uintptr_t>(address) & (alignment - 1)) == 0;
    }
} // namespace

int main()
{
    using namespace vanguard;

    if (!memory::Initialize() || !memory::IsInitialized())
    {
        return 1;
    }

    memory::MemoryBlock small = memory::Allocate(128, 16);
    memory::MemoryBlock cacheLine = memory::Allocate(4096, 64);

    if (!small || !cacheLine)
    {
        return 2;
    }

    if (!IsAligned(small.address, 16) || !IsAligned(cacheLine.address, 64))
    {
        return 3;
    }

    std::memset(small.address, 0x5A, 128);
    std::memset(cacheLine.address, 0xA5, 4096);

    memory::Free(small);
    memory::Free(cacheLine);

    if (small || cacheLine)
    {
        return 4;
    }

    if (memory::Allocate(0) || memory::Allocate(128, 3) || memory::Allocate(static_cast<usize>(std::numeric_limits<std::uint32_t>::max()) + 1))
    {
        return 5;
    }

    return 0;
}
