#pragma once

#include <vanguard/meshes/mesh_page_source.hpp>
#include <vanguard/streaming/streaming.hpp>

namespace vanguard::meshes
{
    /// CPU-side resource publication for one immutable cooked mesh generation.
    /// Geometry placement and page residency deliberately live elsewhere.
    class MeshResourceObject final : public resources::ResourceObject
    {
    public:
        MeshResourceObject() noexcept;
        ~MeshResourceObject() override;

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;

        [[nodiscard]] Result Open(MeshPageSource&& source, containers::ArraySpan<const resources::ResourceHandle> dependencies,
                                  const ReadLimits& limits = {}) noexcept;
        [[nodiscard]] Result OpenPrepared(MeshPageSource&& source, MeshFile&& metadata,
                                         containers::ArraySpan<const resources::ResourceHandle> dependencies) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const MeshFile& GetMetadata() const noexcept;
        [[nodiscard]] const MeshPageSource& GetPageSource() const noexcept;
        [[nodiscard]] containers::ArraySpan<const resources::ResourceHandle> GetDependencies() const noexcept;

        /// Returns the complete, sorted and deduplicated page set for one LOD.
        /// For the fallback (last) LOD, every returned page is additionally
        /// required to carry RequiredForLowestLod.
        [[nodiscard]] Result BuildLodUploadSet(u16 lod, containers::DynamicArray<u32>& pages) const noexcept;

    private:
        [[nodiscard]] bool ValidateDependencies(containers::ArraySpan<const resources::ResourceHandle> dependencies) const noexcept;

        MeshFile m_metadata;
        MeshPageSource m_source;
        containers::DynamicArray<resources::ResourceHandle> m_dependencies;
        bool m_open = false;
    };

    /// Registers VMSH with ResourcePipeline without routing it through the
    /// whole-resource decoder. One operation resolves the normal streaming
    /// source, reads only the document prefix/META range, loads dependencies,
    /// and publishes a metadata-only MeshResourceObject.
    class MeshResourceLoader final
    {
    public:
        struct Impl;

        MeshResourceLoader() noexcept = default;
        ~MeshResourceLoader();

        MeshResourceLoader(const MeshResourceLoader&) = delete;
        MeshResourceLoader& operator=(const MeshResourceLoader&) = delete;

        [[nodiscard]] bool Initialize(streaming::ResourceStreamer& streamer, resources::ResourcePipeline& pipeline,
                                      const ReadLimits& limits = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::meshes
