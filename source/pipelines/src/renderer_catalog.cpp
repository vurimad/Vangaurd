#include <vanguard/pipelines/renderer_catalog.hpp>
#include <vanguard/memory/memory.hpp>
#include <cstring>
#include <new>

namespace vanguard::pipelines
{
    namespace
    {
        constexpr u32 HeaderBytes = 24;
        constexpr u32 EntryBytes = RendererCatalogNameBytes + 28;

        bool Validate(containers::ArraySpan<const RendererCatalogEntry> entries) noexcept
        {
            if (entries.Empty() || entries.Size() > MaximumRendererCatalogEntries)
                return false;
            containers::HashMap<u64, u32> names(memory::pools::Resources::GetInstance());
            for (const auto& entry : entries)
            {
                if (entry.name[0] == '\0' || std::memchr(entry.name, 0, sizeof(entry.name)) == nullptr ||
                    !entry.shader.IsValid() || entry.shader.ExpectedType() != shaders::ShaderResourceType ||
                    !entry.pipeline.IsValid() || entry.pipeline.ExpectedType() != PipelineResourceType ||
                    static_cast<u32>(entry.constants) > static_cast<u32>(RendererConstants::SinglePushConstant))
                    return false;
                const auto inserted = names.Insert(shaders::HashInterfaceName(entry.name), 0);
                if (!inserted.IsSuccessful())
                    return false;
            }
            return true;
        }
    }

    RendererCatalogResource::RendererCatalogResource() noexcept : m_entries(memory::pools::Resources::GetInstance()) {}
    resources::ResourceTypeId RendererCatalogResource::GetType() const noexcept { return RendererCatalogResourceType; }

    Result WriteRendererCatalog(filesystem::IFile& file, containers::ArraySpan<const RendererCatalogEntry> entries) noexcept
    {
        if (!Validate(entries))
            return Result::InvalidArgument;
        containers::DynamicArray<u8> bytes(memory::pools::Resources::GetInstance());
        filesystem::MemoryFileWriter payloadFile(bytes);
        serialization::BinaryWriter payload(payloadFile);
        for (const auto& entry : entries)
        {
            char name[RendererCatalogNameBytes]{};
            std::memcpy(name, entry.name, std::strlen(entry.name));
            const bool written = payload.WriteBytes(name, sizeof(name)) && payload.WriteU64(entry.shader.GetPath().Id()) &&
                payload.WriteU32(entry.shader.ExpectedType()) && payload.WriteU64(entry.pipeline.GetPath().Id()) &&
                payload.WriteU32(entry.pipeline.ExpectedType()) && payload.WriteU32(static_cast<u32>(entry.constants));
            if (!written)
                return Result::IoFailure;
        }
        serialization::BinaryWriter writer(file);
        const bool written = writer.WriteU32(RendererCatalogResourceType) && writer.WriteU16(1) && writer.WriteU16(0) &&
            writer.WriteU32(entries.Size()) && writer.WriteU32(EntryBytes) && writer.WriteU64(serialization::Crc64(bytes.Data(), bytes.Size())) &&
            writer.WriteBytes(bytes.Data(), bytes.Size());
        return written ? Result::Success : Result::IoFailure;
    }

    Result RendererCatalogResource::Open(filesystem::IFile& file) noexcept
    {
        m_entries.Clear();
        if (file.GetSize() < HeaderBytes || file.GetSize() > HeaderBytes + MaximumRendererCatalogEntries * EntryBytes)
            return Result::InvalidLayout;
        serialization::BinaryReader reader(file);
        u32 magic = 0, count = 0, entrySize = 0;
        u16 major = 0, minor = 0;
        u64 crc = 0;
        const bool headerRead = reader.ReadU32(magic) && reader.ReadU16(major) && reader.ReadU16(minor) &&
            reader.ReadU32(count) && reader.ReadU32(entrySize) && reader.ReadU64(crc);
        if (!headerRead || magic != RendererCatalogResourceType || count == 0 || count > MaximumRendererCatalogEntries ||
            entrySize != EntryBytes || file.GetSize() != HeaderBytes + count * EntryBytes)
            return Result::InvalidLayout;
        if (major != 1 || minor != 0)
            return Result::UnsupportedVersion;
        containers::DynamicArray<u8> bytes(memory::pools::Resources::GetInstance());
        bytes.Resize(count * EntryBytes);
        if (bytes.Size() != count * EntryBytes)
            return Result::LimitExceeded;
        const bool payloadRead = reader.ReadBytes(bytes.Data(), bytes.Size());
        if (!payloadRead)
            return Result::IoFailure;
        if (serialization::Crc64(bytes.Data(), bytes.Size()) != crc)
            return Result::IntegrityFailure;
        filesystem::MemoryFileReader payloadFile(bytes, 0);
        serialization::BinaryReader payload(payloadFile);
        containers::DynamicArray<RendererCatalogEntry> entries(memory::pools::Resources::GetInstance());
        entries.Resize(count);
        if (entries.Size() != count)
            return Result::LimitExceeded;
        for (auto& entry : entries)
        {
            u64 shader = 0, pipeline = 0;
            u32 shaderType = 0, pipelineType = 0, constants = 0;
            const bool read = payload.ReadBytes(entry.name, sizeof(entry.name)) && payload.ReadU64(shader) && payload.ReadU32(shaderType) &&
                payload.ReadU64(pipeline) && payload.ReadU32(pipelineType) && payload.ReadU32(constants);
            if (!read)
                return Result::InvalidLayout;
            entry.shader = resources::ResourceReference(resources::ResourcePath::FromId(shader), shaderType);
            entry.pipeline = resources::ResourceReference(resources::ResourcePath::FromId(pipeline), pipelineType);
            entry.constants = static_cast<RendererConstants>(constants);
        }
        if (!Validate(entries))
            return Result::InvalidLayout;
        m_entries = std::move(entries);
        return Result::Success;
    }

    resources::ResourceObject* DecodeRendererCatalog(resources::ResourceReference reference, const void* data, usize size,
        const resources::LoadContext&, resources::Failure& failure, void*) noexcept
    {
        if (reference.ExpectedType() != RendererCatalogResourceType || data == nullptr || size > HeaderBytes + MaximumRendererCatalogEntries * EntryBytes)
        {
            failure = resources::Failure::DeserializationFailure;
            return nullptr;
        }
        const auto block = memory::Allocate(memory::PoolId::Resources, sizeof(RendererCatalogResource), alignof(RendererCatalogResource));
        if (!block)
        {
            failure = resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* resource = ::new (block.address) RendererCatalogResource();
        filesystem::MemoryFileReader file(static_cast<const u8*>(data), static_cast<u32>(size), 0);
        const Result opened = resource->Open(file);
        if (opened == Result::Success)
        {
            failure = resources::Failure::None;
            return resource;
        }
        DestroyRendererCatalog(resource, nullptr);
        failure = resources::Failure::DeserializationFailure;
        return nullptr;
    }

    void DestroyRendererCatalog(resources::ResourceObject* object, void*) noexcept
    {
        if (object == nullptr)
            return;
        static_cast<RendererCatalogResource*>(object)->~RendererCatalogResource();
        memory::MemoryBlock block{object, sizeof(RendererCatalogResource), memory::PoolId::Resources};
        memory::Free(block);
    }
}
