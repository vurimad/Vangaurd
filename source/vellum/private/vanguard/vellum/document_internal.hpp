#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/vellum/document.hpp>

namespace vanguard::vellum::detail
{
    inline constexpr u32 TreeSectionId = serialization::MakeFourCC('T', 'R', 'E', 'E');
    inline constexpr serialization::Version TreeSectionVersion{1, 0};
    inline constexpr u32 TreeBodyWireVersion = 1;
    inline constexpr u32 RuntimePropertySchemaVersion = 1;
    inline constexpr u32 SupportedRequiredFeatureBits = 0;
    inline constexpr u32 TreeBodyHeaderWireSize = 24;
    inline constexpr u32 TreeNodeWireSize = 24;
    inline constexpr u32 TreePropertyWireSize = 8;
    inline constexpr u64 CanonicalSectionTableOffset = serialization::DocumentHeader::WireSize;
    inline constexpr u64 CanonicalTreeOffset = serialization::DocumentHeader::WireSize + serialization::SectionDescriptor::WireSize;
    inline constexpr u8 CanonicalTreeAlignmentLog2 = 3;

    enum class WireNodeKind : u8
    {
        StackPanel = 1,
        Spacer = 2
    };

    enum class WirePropertyType : u8
    {
        Float = 1,
        Visibility = 2,
        Orientation = 3
    };

    enum class WireVisibility : u32
    {
        Visible = 1,
        Hidden = 2,
        Collapsed = 3
    };

    enum class WireOrientation : u32
    {
        Horizontal = 1,
        Vertical = 2
    };

    struct DocumentDecoderState
    {
        u32 maximumDocumentBytes = 0;
        u32 maximumNodes = 0;
        u32 maximumProperties = 0;
        u32 maximumHierarchyDepth = 0;
        concurrency::Atomic<u32> liveDocuments;
        concurrency::Atomic<u32> peakDocuments;
    };

    struct DocumentResourceAccess
    {
        [[nodiscard]] static DocumentResource* Allocate(u32 nodeCount, u32 propertyCount) noexcept;
        static void Destroy(DocumentResource* resource) noexcept;
    };

    [[nodiscard]] resources::ResourceObject* DecodeDocumentResource(resources::ResourceReference reference, const void* data, usize size,
                                                                    const resources::LoadContext& context, resources::Failure& failure,
                                                                    void* userData) noexcept;
    void DestroyDocumentResource(resources::ResourceObject* resource, void* userData) noexcept;
} // namespace vanguard::vellum::detail
