#include <vanguard/memory/memory.hpp>

#include <tiffio.h>

namespace
{
    using vanguard::u8;
    using vanguard::u64;
    using vanguard::usize;

    struct TiffAllocationHeader final
    {
        u64 size = 0;
        u64 reserved = 0;
    };
    static_assert(sizeof(TiffAllocationHeader) == 16);

    [[nodiscard]] TiffAllocationHeader* Header(void* address) noexcept
    {
        return reinterpret_cast<TiffAllocationHeader*>(static_cast<u8*>(address) - sizeof(TiffAllocationHeader));
    }
}

extern "C"
{
    TIFFErrorHandler _TIFFwarningHandler = nullptr;
    TIFFErrorHandler _TIFFerrorHandler = nullptr;

    void* _TIFFmalloc(const tmsize_t requested)
    {
        if (requested <= 0 || static_cast<u64>(requested) > 0xffffffffull - sizeof(TiffAllocationHeader)) return nullptr;
        const usize size = static_cast<usize>(requested) + sizeof(TiffAllocationHeader);
        const vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Assets, size, 16);
        if (!block) return nullptr;
        auto* const header = static_cast<TiffAllocationHeader*>(block.address);
        header->size = size;
        return reinterpret_cast<u8*>(header) + sizeof(TiffAllocationHeader);
    }

    void* _TIFFcalloc(const tmsize_t count, const tmsize_t elementSize)
    {
        if (count <= 0 || elementSize <= 0 || static_cast<u64>(count) > 0xffffffffull / static_cast<u64>(elementSize))
            return nullptr;
        const tmsize_t size = count * elementSize;
        void* const address = _TIFFmalloc(size);
        if (address != nullptr) _TIFFmemset(address, 0, size);
        return address;
    }

    void _TIFFfree(void* address)
    {
        if (address == nullptr) return;
        TiffAllocationHeader* const header = Header(address);
        vanguard::memory::MemoryBlock block{header, static_cast<usize>(header->size),
                                            vanguard::memory::PoolId::Assets};
        vanguard::memory::Free(block);
    }

    void* _TIFFrealloc(void* address, const tmsize_t requested)
    {
        if (address == nullptr) return _TIFFmalloc(requested);
        if (requested <= 0)
        {
            _TIFFfree(address);
            return nullptr;
        }
        TiffAllocationHeader* const oldHeader = Header(address);
        const u64 oldSize = oldHeader->size - sizeof(TiffAllocationHeader);
        void* const replacement = _TIFFmalloc(requested);
        if (replacement == nullptr) return nullptr;
        const u64 copySize = oldSize < static_cast<u64>(requested) ? oldSize : static_cast<u64>(requested);
        _TIFFmemcpy(replacement, address, static_cast<tmsize_t>(copySize));
        _TIFFfree(address);
        return replacement;
    }

    void _TIFFmemset(void* destination, const int value, const tmsize_t count)
    {
        auto* const bytes = static_cast<u8*>(destination);
        for (tmsize_t index = 0; index < count; ++index) bytes[index] = static_cast<u8>(value);
    }

    void _TIFFmemcpy(void* destination, const void* source, const tmsize_t count)
    {
        auto* const output = static_cast<u8*>(destination);
        const auto* const input = static_cast<const u8*>(source);
        for (tmsize_t index = 0; index < count; ++index) output[index] = input[index];
    }

    int _TIFFmemcmp(const void* left, const void* right, const tmsize_t count)
    {
        const auto* const first = static_cast<const u8*>(left);
        const auto* const second = static_cast<const u8*>(right);
        for (tmsize_t index = 0; index < count; ++index)
            if (first[index] != second[index]) return first[index] < second[index] ? -1 : 1;
        return 0;
    }
}
