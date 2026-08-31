#pragma once

#include <vanguard/meshes/meshes.hpp>
#include <vanguard/streaming/resource_source.hpp>

namespace vanguard::meshes
{
    enum class MeshPageSourceKind : u8
    {
        LooseFile,
        Package
    };

    struct MeshPageReadStats
    {
        u64 storedBytesRead = 0;
        u32 decodedSegments = 0;
    };

    class MeshPageReadRequest final
    {
    public:
        struct Impl;

        MeshPageReadRequest() noexcept = default;
        ~MeshPageReadRequest();

        MeshPageReadRequest(const MeshPageReadRequest&) = delete;
        MeshPageReadRequest& operator=(const MeshPageReadRequest&) = delete;
        MeshPageReadRequest(MeshPageReadRequest&& other) noexcept;
        MeshPageReadRequest& operator=(MeshPageReadRequest&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds) const noexcept;
        [[nodiscard]] bool Cancel() noexcept;
        [[nodiscard]] Result GetResult() const noexcept;
        [[nodiscard]] MeshPageReadStats GetStats() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetBytes() const noexcept;
        void Reset() noexcept;

    private:
        explicit MeshPageReadRequest(Impl* impl) noexcept;

        Impl* m_impl = nullptr;

        friend class MeshPageSource;
    };

    /// Immutable, reopenable physical source for one vmesh generation.
    ///
    /// Generic loose/VPAK handle pinning and logical range decoding are provided
    /// by streaming::ResourceSource. This adapter owns only the vmesh-facing
    /// metadata/page validation policy; every read remains independently seekable.
    class MeshPageSource final
    {
    public:
        struct Impl;

        MeshPageSource() noexcept = default;
        ~MeshPageSource();

        MeshPageSource(const MeshPageSource&) = delete;
        MeshPageSource& operator=(const MeshPageSource&) = delete;
        MeshPageSource(MeshPageSource&& other) noexcept;
        MeshPageSource& operator=(MeshPageSource&& other) noexcept;

        [[nodiscard]] Result OpenLoose(const filesystem::AbsolutePath& physicalPath) noexcept;
        [[nodiscard]] Result OpenPackage(const filesystem::AbsolutePath& physicalPath, resources::ResourceId resource) noexcept;
        [[nodiscard]] Result Open(streaming::ResourceSource&& source) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] MeshPageSourceKind GetKind() const noexcept;
        [[nodiscard]] const filesystem::AbsolutePath& GetPhysicalPath() const noexcept;
        [[nodiscard]] u64 GetLogicalSize() const noexcept;

        [[nodiscard]] Result ReadMetadata(MeshFile& mesh, MeshPageReadStats* stats = nullptr, const ReadLimits& limits = {}) const noexcept;
        [[nodiscard]] Result ReadPage(const MeshFile& mesh, u32 page, containers::DynamicArray<u8>& bytes, MeshPageReadStats* stats = nullptr) const noexcept;
        [[nodiscard]] Result ReadPageAsync(const MeshFile& mesh, u32 page, MeshPageReadRequest& request, io::AsyncPriority priority = io::eAsyncPriority_Streaming) const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::meshes
