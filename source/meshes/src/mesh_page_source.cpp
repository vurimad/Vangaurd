#include <vanguard/meshes/mesh_page_source.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/streaming/resource_source.hpp>

#include <new>

namespace vanguard::meshes
{
    namespace
    {
        template <typename T, typename... Args> [[nodiscard]] T* AllocateMeshSourceObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteMeshSourceObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Streaming};
            memory::Free(block);
        }

        [[nodiscard]] Result ConvertSourceResult(const streaming::ResourceSourceResult result) noexcept
        {
            switch (result)
            {
            case streaming::ResourceSourceResult::Success:
                return Result::Success;
            case streaming::ResourceSourceResult::InvalidArgument:
            case streaming::ResourceSourceResult::TypeMismatch:
                return Result::InvalidArgument;
            case streaming::ResourceSourceResult::InvalidState:
                return Result::InvalidState;
            case streaming::ResourceSourceResult::IntegrityFailure:
                return Result::IntegrityFailure;
            case streaming::ResourceSourceResult::UnsupportedVersion:
                return Result::UnsupportedVersion;
            case streaming::ResourceSourceResult::LimitExceeded:
                return Result::LimitExceeded;
            case streaming::ResourceSourceResult::BufferTooSmall:
                return Result::BufferTooSmall;
            case streaming::ResourceSourceResult::Cancelled:
                return Result::Cancelled;
            case streaming::ResourceSourceResult::NotFound:
            case streaming::ResourceSourceResult::IoFailure:
                return Result::IoFailure;
            }
            return Result::IoFailure;
        }

        void CopyStats(const streaming::ResourceSourceReader& reader, MeshPageReadStats* const destination) noexcept
        {
            if (destination == nullptr)
                return;
            const streaming::ResourceReadStats source = reader.GetStats();
            destination->storedBytesRead = source.storedBytesRead;
            destination->decodedSegments = source.decodedSegments;
        }

        [[nodiscard]] Result RefineIoFailure(const Result result, const streaming::ResourceSourceReader& reader) noexcept
        {
            return result == Result::IoFailure ? ConvertSourceResult(reader.GetLastResult()) : result;
        }
    } // namespace

    struct MeshPageSource::Impl
    {
        streaming::ResourceSource source;
        streaming::ResourceRangeReadQueue reads;
    };

    struct MeshPageReadRequest::Impl
    {
        streaming::CoalescedResourceReadRequest sourceRequest;
    };

    MeshPageReadRequest::MeshPageReadRequest(Impl* const impl) noexcept : m_impl(impl) {}

    MeshPageReadRequest::~MeshPageReadRequest()
    {
        Reset();
    }

    MeshPageReadRequest::MeshPageReadRequest(MeshPageReadRequest&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    MeshPageReadRequest& MeshPageReadRequest::operator=(MeshPageReadRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    bool MeshPageReadRequest::IsValid() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MeshPageReadRequest::HasFinished() const noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.HasFinished();
    }

    void MeshPageReadRequest::Wait() const noexcept
    {
        if (m_impl != nullptr)
            m_impl->sourceRequest.Wait();
    }

    bool MeshPageReadRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.TryWait(timeoutMilliseconds);
    }

    bool MeshPageReadRequest::Cancel() noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.Cancel();
    }

    Result MeshPageReadRequest::GetResult() const noexcept
    {
        if (m_impl == nullptr || !m_impl->sourceRequest.HasFinished())
            return Result::InvalidState;
        return ConvertSourceResult(m_impl->sourceRequest.GetResult());
    }

    MeshPageReadStats MeshPageReadRequest::GetStats() const noexcept
    {
        if (m_impl == nullptr || !m_impl->sourceRequest.HasFinished())
            return {};
        const streaming::ResourceReadStats stats = m_impl->sourceRequest.GetStats();
        return {stats.storedBytesRead, stats.decodedSegments};
    }

    containers::ArraySpan<const u8> MeshPageReadRequest::GetBytes() const noexcept
    {
        return GetResult() == Result::Success ? m_impl->sourceRequest.GetBytes() : containers::ArraySpan<const u8>();
    }

    void MeshPageReadRequest::Reset() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteMeshSourceObject(impl);
    }

    MeshPageSource::~MeshPageSource()
    {
        Close();
    }

    MeshPageSource::MeshPageSource(MeshPageSource&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    MeshPageSource& MeshPageSource::operator=(MeshPageSource&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    Result MeshPageSource::OpenLoose(const filesystem::AbsolutePath& physicalPath) noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(Impl), alignof(Impl));
        if (!block)
            return Result::LimitExceeded;
        Impl* const impl = ::new (block.address) Impl();
        Result result = ConvertSourceResult(impl->source.OpenLoose(physicalPath));
        if (result == Result::Success)
            result = ConvertSourceResult(impl->reads.Open(impl->source));
        if (result != Result::Success)
        {
            impl->~Impl();
            memory::Free(block);
            return result;
        }
        m_impl = impl;
        return Result::Success;
    }

    Result MeshPageSource::OpenPackage(const filesystem::AbsolutePath& physicalPath, const resources::ResourceId resource) noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(Impl), alignof(Impl));
        if (!block)
            return Result::LimitExceeded;
        Impl* const impl = ::new (block.address) Impl();
        Result result = ConvertSourceResult(impl->source.OpenPackage(physicalPath, resource, MeshResourceType));
        if (result == Result::Success)
            result = ConvertSourceResult(impl->reads.Open(impl->source));
        if (result != Result::Success)
        {
            impl->~Impl();
            memory::Free(block);
            return result;
        }
        m_impl = impl;
        return Result::Success;
    }

    Result MeshPageSource::Open(streaming::ResourceSource&& source) noexcept
    {
        if (m_impl != nullptr || !source.IsOpen() || source.GetResourceType() != MeshResourceType)
            return m_impl != nullptr ? Result::InvalidState : Result::InvalidArgument;
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(Impl), alignof(Impl));
        if (!block)
            return Result::LimitExceeded;
        Impl* const impl = ::new (block.address) Impl();
        impl->source = static_cast<streaming::ResourceSource&&>(source);
        if (impl->reads.Open(impl->source) != streaming::ResourceSourceResult::Success)
        {
            impl->~Impl();
            memory::Free(block);
            return Result::LimitExceeded;
        }
        m_impl = impl;
        return Result::Success;
    }

    void MeshPageSource::Close() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Streaming};
        memory::Free(block);
    }

    bool MeshPageSource::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }

    MeshPageSourceKind MeshPageSource::GetKind() const noexcept
    {
        if (m_impl == nullptr || m_impl->source.GetKind() == streaming::ResourceSourceKind::LooseFile)
            return MeshPageSourceKind::LooseFile;
        return MeshPageSourceKind::Package;
    }

    const filesystem::AbsolutePath& MeshPageSource::GetPhysicalPath() const noexcept
    {
        static const filesystem::AbsolutePath empty;
        return m_impl != nullptr ? m_impl->source.GetPhysicalPath() : empty;
    }

    u64 MeshPageSource::GetLogicalSize() const noexcept
    {
        return m_impl != nullptr ? m_impl->source.GetLogicalSize() : 0;
    }

    Result MeshPageSource::ReadMetadata(MeshFile& mesh, MeshPageReadStats* const stats, const ReadLimits& limits) const noexcept
    {
        if (stats != nullptr)
            *stats = {};
        if (m_impl == nullptr || mesh.IsOpen())
            return Result::InvalidState;

        streaming::ResourceSourceReader reader;
        const Result opened = ConvertSourceResult(reader.Open(m_impl->source));
        if (opened != Result::Success)
            return opened;
        const Result result = RefineIoFailure(mesh.Open(reader, limits), reader);
        CopyStats(reader, stats);
        return result;
    }

    Result MeshPageSource::ReadPage(const MeshFile& mesh, const u32 page, containers::DynamicArray<u8>& bytes, MeshPageReadStats* const stats) const noexcept
    {
        bytes.Clear();
        if (stats != nullptr)
            *stats = {};
        if (m_impl == nullptr || !mesh.IsOpen())
            return Result::InvalidState;
        if (page >= mesh.GetPages().Size() || mesh.GetPages()[page].byteSize > ~u32{0})
            return Result::InvalidArgument;

        bytes.Resize(static_cast<u32>(mesh.GetPages()[page].byteSize));
        if (bytes.Size() != mesh.GetPages()[page].byteSize)
            return Result::LimitExceeded;

        streaming::ResourceSourceReader reader;
        Result result = ConvertSourceResult(reader.Open(m_impl->source));
        if (result == Result::Success)
            result = RefineIoFailure(mesh.ReadPage(reader, page, bytes.TypedData(), bytes.Size()), reader);
        CopyStats(reader, stats);
        if (result != Result::Success)
            bytes.Clear();
        return result;
    }

    Result MeshPageSource::ReadPageAsync(const MeshFile& mesh, const u32 page, MeshPageReadRequest& request, const io::AsyncPriority priority) const noexcept
    {
        if (m_impl == nullptr || !mesh.IsOpen() || request.IsValid())
            return Result::InvalidState;
        if (page >= mesh.GetPages().Size() || mesh.GetPages()[page].byteSize > ~u32{0} || mesh.GetGeometryOffset() > ~u64{0} - mesh.GetPages()[page].dataOffset)
            return Result::InvalidArgument;

        MeshPageReadRequest::Impl* const operation = AllocateMeshSourceObject<MeshPageReadRequest::Impl>();
        if (operation == nullptr)
            return Result::LimitExceeded;
        request = MeshPageReadRequest(operation);

        const u64 offset = mesh.GetGeometryOffset() + mesh.GetPages()[page].dataOffset;
        const Result result = ConvertSourceResult(
            m_impl->reads.ReadVerified(offset, mesh.GetPages()[page].byteSize, mesh.GetPages()[page].digest, operation->sourceRequest, priority));
        if (result != Result::Success)
            request.Reset();
        return result;
    }
} // namespace vanguard::meshes
