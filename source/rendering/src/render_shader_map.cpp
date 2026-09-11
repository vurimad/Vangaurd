#include <vanguard/rendering/render_shader_map.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace vanguard::rendering
{
    struct RenderShaderMap::Impl
    {
        struct Entry
        {
            containers::String name{memory::pools::Rendering::GetInstance()};
            RenderShader shader;
            Entry* next = nullptr;
        };
        containers::HashMap<u64, Entry*> entries{memory::pools::Rendering::GetInstance()};

        static u64 Hash(const containers::StringView name) noexcept
        {
            u64 hash = 14695981039346656037ull;
            for (u32 index = 0; index < name.Size(); ++index)
                hash = (hash ^ static_cast<u8>(name[index])) * 1099511628211ull;
            return hash;
        }

        ~Impl()
        {
            for (auto item : entries)
            {
                Entry* entry = item.Value();
                while (entry != nullptr)
                {
                    Entry* next = entry->next;
                    entry->~Entry();
                    memory::MemoryBlock block{entry, sizeof(Entry), memory::PoolId::Rendering};
                    memory::Free(block);
                    entry = next;
                }
            }
        }
    };

    RenderShaderMap::~RenderShaderMap() { Clear(); }

    void RenderShaderMap::Clear() noexcept
    {
        if (m_impl == nullptr)
            return;
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        m_impl = nullptr;
    }

    const RenderShader* RenderShaderMap::FindShader(const containers::StringView name) const noexcept
    {
        if (m_impl == nullptr || name.Size() == 0)
            return nullptr;
        Impl::Entry* entry = nullptr;
        static_cast<void>(m_impl->entries.Find(Impl::Hash(name), entry));
        for (; entry != nullptr; entry = entry->next)
            if (containers::StringView(entry->name) == name)
                return &entry->shader;
        return nullptr;
    }

    const RenderShader* RenderShaderMap::GetShader(const containers::StringView name) const noexcept
    {
        const RenderShader* shader = FindShader(name);
        if (shader == nullptr)
            VG_FATAL("required renderer shader is not present in the initialized catalog");
        return shader;
    }

    RenderShaderResult RenderShaderMap::Init(const containers::ArraySpan<const NamedRenderShader> shaders, rhi::Failure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (m_impl != nullptr)
            return RenderShaderResult::InvalidState;
        RenderShaderMap building;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return RenderShaderResult::OutOfMemory;
        building.m_impl = new (block.address) Impl();
        building.m_impl->entries.Reserve(shaders.Size());
        for (const NamedRenderShader& source : shaders)
        {
            if (source.name.Size() == 0 || source.file == nullptr || !source.file->IsOpen() || building.FindShader(source.name) != nullptr)
                return RenderShaderResult::InvalidArgument;
            memory::MemoryBlock entryBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl::Entry), alignof(Impl::Entry));
            if (!entryBlock)
                return RenderShaderResult::OutOfMemory;
            auto* entry = new (entryBlock.address) Impl::Entry();
            entry->name.Set(source.name);
            const u64 hash = Impl::Hash(source.name);
            Impl::Entry* previous = nullptr;
            static_cast<void>(building.m_impl->entries.Find(hash, previous));
            entry->next = previous;
            const bool indexed = previous != nullptr ? building.m_impl->entries.Set(hash, entry).IsSuccessful() : building.m_impl->entries.Insert(hash, entry).IsSuccessful();
            if (!indexed)
            {
                entry->~Entry();
                memory::Free(entryBlock);
                return RenderShaderResult::OutOfMemory;
            }
            const RenderShaderResult result = entry->shader.Load(*source.file, failure);
            if (result != RenderShaderResult::Success)
                return result;
        }
        m_impl = building.m_impl;
        building.m_impl = nullptr;
        return RenderShaderResult::Success;
    }

}
