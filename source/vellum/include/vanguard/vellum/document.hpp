#pragma once

#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/vellum/types.hpp>

namespace vanguard::vellum::detail
{
    struct DocumentResourceAccess;
}

namespace vanguard::vellum
{
    inline constexpr u32 DocumentMagic = serialization::MakeFourCC('V', 'L', 'U', 'M');
    inline constexpr resources::ResourceTypeId DocumentResourceType = DocumentMagic;
    inline constexpr serialization::Version DocumentFileVersion{1, 0};
    inline constexpr serialization::VersionRange SupportedDocumentVersions{1, 0, 0};
    inline constexpr u32 InvalidDocumentNodeIndex = 0xffffffffu;
    using AuthoredNodeId = u64;
    inline constexpr AuthoredNodeId InvalidAuthoredNodeId = 0;

    struct DocumentNode
    {
        AuthoredNodeId authoredId = InvalidAuthoredNodeId;
        u32 parentIndex = InvalidDocumentNodeIndex;
        u32 firstProperty = 0;
        u32 propertyCount = 0;
        u32 depth = 0;
        NodeKind kind = NodeKind::Invalid;
    };

    struct DocumentProperty
    {
        PropertyId id = PropertyId::Invalid;
        PropertyType type = PropertyType::Invalid;
        u32 canonicalBits = 0;
    };

    struct AuthoredNodeLookup
    {
        AuthoredNodeId authoredId = InvalidAuthoredNodeId;
        u32 nodeIndex = InvalidDocumentNodeIndex;
        u32 reserved = 0;
    };

    class DocumentResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override { return DocumentResourceType; }
        [[nodiscard]] u32 GetRootIndex() const noexcept { return m_rootIndex; }
        [[nodiscard]] u32 GetNodeCount() const noexcept { return m_nodeCount; }
        [[nodiscard]] u32 GetPropertyCount() const noexcept { return m_propertyCount; }
        [[nodiscard]] containers::ArraySpan<const DocumentNode> GetNodes() const noexcept { return {m_nodes, m_nodeCount}; }
        [[nodiscard]] containers::ArraySpan<const DocumentProperty> GetProperties() const noexcept { return {m_properties, m_propertyCount}; }
        [[nodiscard]] containers::ArraySpan<const AuthoredNodeLookup> GetAuthoredNodeLookup() const noexcept
        {
            return {m_authoredNodeLookup, m_nodeCount};
        }
        [[nodiscard]] u32 FindNodeByAuthoredId(AuthoredNodeId authoredId) const noexcept;

    private:
        usize m_allocationSize = 0;
        DocumentNode* m_nodes = nullptr;
        DocumentProperty* m_properties = nullptr;
        AuthoredNodeLookup* m_authoredNodeLookup = nullptr;
        u32 m_rootIndex = InvalidDocumentNodeIndex;
        u32 m_nodeCount = 0;
        u32 m_propertyCount = 0;

        friend struct detail::DocumentResourceAccess;
    };
} // namespace vanguard::vellum
