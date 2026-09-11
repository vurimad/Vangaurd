#pragma once

#include <vanguard/pipelines/pipelines.hpp>

namespace vanguard::pipelines
{
    inline constexpr resources::ResourceTypeId RendererCatalogResourceType = serialization::MakeFourCC('V', 'R', 'C', 'T');
    inline constexpr u32 MaximumRendererCatalogEntries = 256;
    inline constexpr u32 RendererCatalogNameBytes = 64;

    // This describes the renderer's binding convention. Sizes, registers and
    // stage visibility are taken from the referenced shader reflection.
    enum class RendererConstants : u32 { None, SinglePushConstant };

    struct RendererCatalogEntry
    {
        char name[RendererCatalogNameBytes]{};
        resources::ResourceReference shader;
        resources::ResourceReference pipeline;
        RendererConstants constants = RendererConstants::SinglePushConstant;
    };

    class RendererCatalogResource final : public resources::ResourceObject
    {
    public:
        RendererCatalogResource() noexcept;
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;
        [[nodiscard]] Result Open(filesystem::IFile& file) noexcept;
        [[nodiscard]] containers::ArraySpan<const RendererCatalogEntry> Entries() const noexcept { return m_entries; }
    private:
        containers::DynamicArray<RendererCatalogEntry> m_entries;
    };

    [[nodiscard]] Result WriteRendererCatalog(filesystem::IFile& file, containers::ArraySpan<const RendererCatalogEntry> entries) noexcept;
    [[nodiscard]] resources::ResourceObject* DecodeRendererCatalog(resources::ResourceReference reference, const void* data, usize size,
        const resources::LoadContext& context, resources::Failure& failure, void* userData) noexcept;
    void DestroyRendererCatalog(resources::ResourceObject* object, void* userData) noexcept;
}
