#pragma once

#include <vanguard/meshes/meshes.hpp>

namespace vanguard::mesh_tools
{
    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        UnknownCookingProfile,
        LimitExceeded,
        MissingPositionStream,
        InvalidVertexStream,
        InvalidIndex,
        OptimizationFailure,
        MeshWriteFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    struct SourceVertexStream
    {
        meshes::VertexSemantic semantic = meshes::VertexSemantic::Position;
        u8 semanticIndex = 0;
        meshes::VertexFormat format = meshes::VertexFormat::R32G32B32Float;
        const void* data = nullptr;
        u32 vertexCount = 0;
        u32 stride = 0;
    };

    struct SourceSubmesh
    {
        u64 stableId = 0;
        u64 name = 0;
        u64 materialName = 0;
        resources::ResourceReference material;
        meshes::SubmeshFlags flags = meshes::SubmeshFlags::CastsShadow;
        containers::ArraySpan<const SourceVertexStream> vertexStreams;
        containers::ArraySpan<const u32> indices;
    };

    struct SourceMesh
    {
        u64 name = 0;
        crypto::Digest256 sourceFingerprint;
        resources::ResourceReference skeleton;
        containers::ArraySpan<const SourceSubmesh> submeshes;
    };

    using MeshCookingProfileId = u64;
    inline constexpr u8 AnySemanticIndex = 0xffu;

    namespace profiles
    {
        inline constexpr MeshCookingProfileId PreserveSource = 0x7072657365727665ull;
        inline constexpr MeshCookingProfileId RuntimeStatic = 0x7374617469630001ull;
        inline constexpr MeshCookingProfileId RuntimeSkinned4 = 0x736b696e6e656434ull;
    }

    enum class MeshCookingProfileFlags : u8
    {
        None = 0,
        QuantizePositions = 1u << 0u
    };

    [[nodiscard]] constexpr MeshCookingProfileFlags operator|(
        const MeshCookingProfileFlags left,
        const MeshCookingProfileFlags right) noexcept
    {
        return static_cast<MeshCookingProfileFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(
        const MeshCookingProfileFlags value,
        const MeshCookingProfileFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    enum class VertexPackingRuleFlags : u8
    {
        None = 0,
        Required = 1u << 0u,
        MatchAnySemanticIndex = 1u << 1u
    };

    [[nodiscard]] constexpr VertexPackingRuleFlags operator|(
        const VertexPackingRuleFlags left,
        const VertexPackingRuleFlags right) noexcept
    {
        return static_cast<VertexPackingRuleFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(
        const VertexPackingRuleFlags value,
        const VertexPackingRuleFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    enum class UnmatchedVertexStreamPolicy : u8
    {
        Reject,
        PreserveInDedicatedBinding
    };

    using VertexPackFunction = bool (*)(
        u8* destination,
        const SourceVertexStream& sourceStream,
        const u8* sourceElement,
        const meshes::PositionQuantization& positionQuantization) noexcept;

    /// One declarative source-to-runtime vertex element rule. Rules with the same bindingGroup
    /// are emitted into one interleaved binding in canonical semantic/index order. A null pack
    /// function is valid only when sourceFormat and storedFormat are identical and performs a
    /// byte-exact copy. Descriptor and rule-array storage is needed only during registration
    /// because the registry copies every rule. Callback code must remain loaded while cooking.
    struct VertexPackingRule
    {
        meshes::VertexSemantic semantic = meshes::VertexSemantic::Position;
        u8 semanticIndex = 0;
        meshes::VertexFormat sourceFormat = meshes::VertexFormat::R32G32B32Float;
        meshes::VertexFormat storedFormat = meshes::VertexFormat::R32G32B32Float;
        u8 bindingGroup = 0;
        VertexPackingRuleFlags flags = VertexPackingRuleFlags::None;
        VertexPackFunction pack = nullptr;
    };

    struct MeshCookingProfile
    {
        MeshCookingProfileId id = 0;
        u32 version = 1;
        MeshCookingProfileId baseProfile = 0;
        meshes::MeshKind meshKind = meshes::MeshKind::Static;
        MeshCookingProfileFlags flags = MeshCookingProfileFlags::None;
        UnmatchedVertexStreamPolicy unmatchedStreams = UnmatchedVertexStreamPolicy::Reject;
        containers::ArraySpan<const VertexPackingRule> rules;
    };

    enum class ProfileRegistrationResult : u8
    {
        Success,
        InvalidArgument,
        DuplicateIdentifier,
        CapacityExceeded,
        RegistrySealed
    };

    // LOD0 is always the fully cooked source mesh. Every generated LOD is simplified directly
    // from LOD0, never from the preceding LOD. triangleRatio is relative to LOD0 and
    // maximumNormalizedError is meshoptimizer's normalized quadric-error limit for that direct
    // simplification. It is not an exact Hausdorff-distance guarantee and must not decrease for
    // successively coarser LODs.
    struct LodLevelSettings
    {
        f32 triangleRatio = 0.5f;
        f32 maximumNormalizedError = 0.01f;
        f32 minimumScreenCoverage = 0.5f;
    };

    struct LodAttributeWeights
    {
        f32 normal = 0.5f;
        f32 tangent = 0.25f;
        f32 texCoord = 1.0f;
        f32 color = 0.25f;
        f32 jointWeights = 0.25f;
        f32 morphPosition = 1.0f;
    };

    /// The RuntimeStatic built-in profile controls the final cooker-only conversion from
    /// full-precision mesh-processing data into the representation persisted in vmesh.
    ///
    /// RuntimeStatic is Vanguard's production runtime packing profile. Deduplication,
    /// simplification, cache optimization, overdraw optimization, bounds, and cooker
    /// statistics are completed before this conversion:
    ///
    /// | Attribute      | Accepted source        | Stored format          | Stored meaning and runtime interpretation |
    /// |----------------|------------------------|------------------------|-------------------------------------------|
    /// | Position       | Float3                 | R16G16B16A16SNorm      | XYZ is object-space position encoded with the vmesh-wide PositionQuantization scale and bias. Decode as `snorm.xyz * scale + bias`. W is positive one. |
    /// | Normal         | Float3                 | R10G10B10A2UNorm       | XYZ maps `[-1, +1]` to `[0, 1]`. Decode as `unorm.xyz * 2 - 1`, then normalize. A is positive one and has no normal-specific meaning. |
    /// | Tangent        | Float3 or Float4       | R10G10B10A2UNorm       | XYZ uses the normal encoding. Decode and normalize it identically. A stores tangent-frame handedness: zero represents `-1`, one represents `+1`. |
    /// | TexCoord       | Float2                 | R16G16Float            | UV coordinates stored as IEEE half values. GPU vertex fetch expands them directly to floats. |
    /// | Color          | Float4                 | R8G8B8A8UNorm          | RGBA components quantized to `[0, 1]`. |
    /// | JointIndices   | Integer vertex format  | Source integer format  | Integer indices selecting the influencing skeleton joints. |
    /// | JointWeights   | Float4                 | R8G8B8A8UNorm          | Bone influences quantized to `[0, 1]`. The importer must provide a normalized set; packing does not silently renormalize it. |
    /// | MorphPosition  | Any supported format   | Source format          | Preserved byte-exact in a dedicated binding. |
    /// | Custom         | Any supported format   | Source format          | Preserved byte-exact in a dedicated binding. |
    ///
    /// Packed Position occupies its own binding for position-only/depth passes. Normal,
    /// Tangent, TexCoord, and Color streams are interleaved into the shading binding.
    /// JointIndices and JointWeights share the skinning binding. Small compact LODs use
    /// UInt16 indices when every vertex is addressable; larger LODs use UInt32.
    ///
    /// The PreserveSource profile retains every source vertex format in dedicated bindings and
    /// is intended for diagnostics and tooling. RuntimeSkinned4 additionally requires one set of
    /// four joint indices and weights. Applications may register specialized profiles during
    /// startup for cloth, vegetation, vehicles, or project-specific deformation streams.

    struct CookSettings
    {
        bool optimizeVertexCache = true;
        bool optimizeOverdraw = true;
        bool optimizeVertexFetch = true;
        bool lockLodBorders = true;
        bool regularizeLodTriangles = false;
        f32 overdrawThreshold = 1.05f;
        MeshCookingProfileId meshCookingProfile = profiles::RuntimeStatic;
        LodAttributeWeights lodAttributeWeights;
        containers::ArraySpan<const LodLevelSettings> lodLevels;
        u32 maximumSubmeshes = 65536;
        u32 maximumVertexStreamsPerSubmesh = 32;
        u32 maximumVerticesPerSubmesh = 16777216;
        u32 maximumIndicesPerSubmesh = 50331648;
        u32 maximumLodLevels = 8;
    };

    struct SubmeshCookStatistics
    {
        u64 stableId = 0;
        u32 sourceVertexCount = 0;
        u32 cookedVertexCount = 0;
        u32 indexCount = 0;
        f32 sourceVertexCacheMissRatio = 0.0f;
        f32 cookedVertexCacheMissRatio = 0.0f;
        f32 sourceVertexFetchOverfetch = 0.0f;
        f32 cookedVertexFetchOverfetch = 0.0f;
        f32 sourceOverdraw = 0.0f;
        f32 cookedOverdraw = 0.0f;
    };

    struct LodCookStatistics
    {
        u64 stableId = 0;
        u16 lod = 0;
        u32 sourceIndexCount = 0;
        u32 cookedIndexCount = 0;
        u32 attributeComponentCount = 0;
        // Direct LOD0-to-this-LOD normalized quadric error reported by meshoptimizer.
        f32 normalizedError = 0.0f;
        bool reachedTriangleTarget = true;
    };

    struct CookReport
    {
        CookReport() noexcept
            : submeshes(memory::pools::Assets::GetInstance()),
              lods(memory::pools::Assets::GetInstance())
        {
        }

        containers::DynamicArray<SubmeshCookStatistics> submeshes;
        containers::DynamicArray<LodCookStatistics> lods;
        MeshCookingProfileId meshCookingProfile = 0;
        u32 meshCookingProfileVersion = 0;
    };

    // Installs meshoptimizer's process-global temporary allocator callbacks.
    // The application composition root must call this once after memory and containers initialization.
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    /// Registers a build-time mesh layout policy. Registration is startup-only and becomes
    /// permanently sealed when the first mesh cook begins, allowing subsequent lock-free reads.
    [[nodiscard]] ProfileRegistrationResult RegisterMeshCookingProfile(const MeshCookingProfile& profile) noexcept;
    [[nodiscard]] const MeshCookingProfile* FindMeshCookingProfile(MeshCookingProfileId id) noexcept;

    // Source data is importer-neutral and caller-owned. The output is a complete, deterministic vmesh document.
    [[nodiscard]] Result CookMesh(
        const SourceMesh& source,
        filesystem::IFile& output,
        const CookSettings& settings = {},
        CookReport* report = nullptr) noexcept;
} // namespace vanguard::mesh_tools
