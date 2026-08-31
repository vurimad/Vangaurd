#pragma once

#include <vanguard/shaders/shaders.hpp>

namespace vanguard::pipelines
{
    inline constexpr u32 PipelineMagic = serialization::MakeFourCC('V', 'P', 'L', 'N');
    inline constexpr resources::ResourceTypeId PipelineResourceType = serialization::MakeFourCC('V', 'P', 'L', 'N');
    inline constexpr u32 MaximumColorAttachments = 8;

    inline constexpr u32 MaximumVertexSemanticNameLength = 32;

    enum class Format : u16
    {
        Unknown,
        R8UNorm,
        R8SNorm,
        R8UInt,
        R8G8UNorm,
        R8G8SNorm,
        R8G8UInt,
        R8G8B8A8UNorm,
        R8G8B8A8UNormSrgb,
        R8G8B8A8SNorm,
        R8G8B8A8UInt,
        B8G8R8A8UNorm,
        B8G8R8A8UNormSrgb,
        R16UNorm,
        R16SNorm,
        R16UInt,
        R16Float,
        R16G16UNorm,
        R16G16SNorm,
        R16G16UInt,
        R16G16Float,
        R16G16B16A16UNorm,
        R16G16B16A16SNorm,
        R16G16B16A16UInt,
        R16G16B16A16Float,
        R32UInt,
        R32Float,
        R32G32UInt,
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        R10G10B10A2UNorm,
        R11G11B10Float,
        D16UNorm,
        D24UNormS8UInt,
        D32Float,
        D32FloatS8UInt,
        BC1UNorm,
        BC1UNormSrgb,
        BC2UNorm,
        BC2UNormSrgb,
        BC3UNorm,
        BC3UNormSrgb,
        BC4UNorm,
        BC4SNorm,
        BC5UNorm,
        BC5SNorm,
        BC6HUFloat,
        BC6HSFloat,
        BC7UNorm,
        BC7UNormSrgb,
        Count
    };

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidMagic,
        UnsupportedVersion,
        InvalidLayout,
        IntegrityFailure,
        LimitExceeded,
        DuplicateShader,
        DuplicateVertexStream,
        DuplicateVertexAttribute,
        DuplicateRayTracingGroup,
        ShaderMismatch,
        AttachmentMismatch,
        IncompatiblePipeline,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    enum class PipelineKind : u8
    {
        Graphics,
        Compute,
        RayTracing
    };

    enum class AttachmentPolicy : u8
    {
        Deferred,
        Exact
    };

    enum class DynamicState : u64
    {
        None = 0,
        Viewport = 1ull << 0u,
        Scissor = 1ull << 1u,
        BlendConstants = 1ull << 2u,
        StencilReference = 1ull << 3u,
        DepthBias = 1ull << 4u,
        DepthBounds = 1ull << 5u,
        PrimitiveTopology = 1ull << 6u,
        FragmentShadingRate = 1ull << 7u
    };

    [[nodiscard]] constexpr DynamicState operator|(const DynamicState left, const DynamicState right) noexcept
    {
        return static_cast<DynamicState>(static_cast<u64>(left) | static_cast<u64>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const DynamicState value, const DynamicState flag) noexcept
    {
        return (static_cast<u64>(value) & static_cast<u64>(flag)) != 0;
    }

    enum class PrimitiveTopology : u8
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        PatchList
    };

    enum class FillMode : u8
    {
        Solid,
        Wireframe
    };

    enum class CullMode : u8
    {
        None,
        Front,
        Back
    };

    enum class FrontFace : u8
    {
        CounterClockwise,
        Clockwise
    };

    enum class CompareOperation : u8
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    enum class StencilOperation : u8
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap
    };

    enum class BlendOperation : u8
    {
        Add,
        Subtract,
        ReverseSubtract,
        Minimum,
        Maximum
    };

    enum class BlendFactor : u8
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        DestinationColor,
        OneMinusDestinationColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationAlpha,
        OneMinusDestinationAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha,
        SourceAlphaSaturate,
        SourceOneColor,
        OneMinusSourceOneColor,
        SourceOneAlpha,
        OneMinusSourceOneAlpha
    };

    enum class LogicOperation : u8
    {
        Clear,
        And,
        AndReverse,
        Copy,
        AndInverted,
        NoOperation,
        Xor,
        Or,
        Nor,
        Equivalent,
        Invert,
        OrReverse,
        CopyInverted,
        OrInverted,
        Nand,
        Set
    };

    enum class InputRate : u8
    {
        PerVertex,
        PerInstance
    };

    enum class DepthStencilClass : u8
    {
        None,
        Depth,
        Stencil,
        DepthStencil
    };

    enum class RayTracingGroupKind : u8
    {
        General,
        TrianglesHitGroup,
        ProceduralHitGroup
    };

    struct ShaderReference
    {
        resources::ResourceId resource = resources::InvalidResourceId;
        crypto::Digest256 permutation;
        crypto::Digest256 bindingLayout;
        crypto::Digest256 pipelineInterface;
    };

    struct AttachmentFormat
    {
        Format format = Format::Unknown;
        shaders::NumericClass numericClass = shaders::NumericClass::FloatingPoint;
    };

    struct AttachmentSignature
    {
        AttachmentFormat colors[MaximumColorAttachments]{};
        u32 colorCount = 0;
        Format depthStencilFormat = Format::Unknown;
        DepthStencilClass depthStencilClass = DepthStencilClass::None;
        u8 sampleCount = 1;
    };

    struct VertexStream
    {
        u32 binding = 0;
        u32 stride = 0;
        InputRate inputRate = InputRate::PerVertex;
        u32 instanceStepRate = 1;
    };

    struct VertexAttribute
    {
        u64 semantic = 0;
        u32 semanticIndex = 0;
        u32 location = 0;
        u32 streamBinding = 0;
        u32 byteOffset = 0;
        shaders::NumericClass numericClass = shaders::NumericClass::FloatingPoint;
        u8 componentCount = 0;
        u8 componentBits = 0;
        Format format = Format::Unknown;
        char semanticName[MaximumVertexSemanticNameLength]{};
    };

    struct RasterizerState
    {
        FillMode fill = FillMode::Solid;
        CullMode cull = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        bool depthClipEnable = true;
        bool conservativeRasterization = false;
        bool rasterizerDiscard = false;
        i32 depthBias = 0;
        f32 depthBiasClamp = 0.0f;
        f32 slopeScaledDepthBias = 0.0f;
    };

    struct MultisampleState
    {
        u8 sampleCount = 1;
        u32 sampleMask = 0xffffffffu;
        bool alphaToCoverage = false;
        bool sampleShading = false;
        f32 minimumSampleShading = 0.0f;
    };

    struct StencilFaceState
    {
        StencilOperation fail = StencilOperation::Keep;
        StencilOperation depthFail = StencilOperation::Keep;
        StencilOperation pass = StencilOperation::Keep;
        CompareOperation compare = CompareOperation::Always;
    };

    struct DepthStencilState
    {
        bool depthTest = false;
        bool depthWrite = false;
        CompareOperation depthCompare = CompareOperation::LessEqual;
        bool depthBoundsTest = false;
        f32 minimumDepthBounds = 0.0f;
        f32 maximumDepthBounds = 1.0f;
        bool stencilTest = false;
        u8 stencilReadMask = 0xff;
        u8 stencilWriteMask = 0xff;
        StencilFaceState front;
        StencilFaceState back;
    };

    struct BlendAttachmentState
    {
        bool blendEnable = false;
        BlendFactor sourceColor = BlendFactor::One;
        BlendFactor destinationColor = BlendFactor::Zero;
        BlendOperation colorOperation = BlendOperation::Add;
        BlendFactor sourceAlpha = BlendFactor::One;
        BlendFactor destinationAlpha = BlendFactor::Zero;
        BlendOperation alphaOperation = BlendOperation::Add;
        u8 writeMask = 0x0f;
    };

    struct BlendState
    {
        bool independentBlend = false;
        bool logicOperationEnable = false;
        LogicOperation logicOperation = LogicOperation::NoOperation;
        BlendAttachmentState attachments[MaximumColorAttachments]{};
        u32 attachmentCount = 0;
    };

    struct GraphicsState
    {
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        u8 patchControlPoints = 0;
        bool primitiveRestart = false;
        RasterizerState rasterizer;
        MultisampleState multisample;
        DepthStencilState depthStencil;
        BlendState blend;
        AttachmentPolicy attachmentPolicy = AttachmentPolicy::Deferred;
        AttachmentSignature exactAttachments;
    };

    struct RayTracingState
    {
        u32 maximumRecursionDepth = 1;
        u32 maximumPayloadBytes = 0;
        u32 maximumAttributeBytes = 8;
    };

    struct RayTracingGroup
    {
        u64 name = 0;
        RayTracingGroupKind kind = RayTracingGroupKind::General;
        u32 shaderLibrary = 0;
        u64 generalEntry = 0;
        u64 closestHitEntry = 0;
        u64 anyHitEntry = 0;
        u64 intersectionEntry = 0;
    };

    struct BuildDescription
    {
        PipelineKind kind = PipelineKind::Graphics;
        u64 name = 0;
        DynamicState dynamicStates = DynamicState::Viewport | DynamicState::Scissor | DynamicState::BlendConstants | DynamicState::StencilReference;
        containers::ArraySpan<const ShaderReference> shaders;
        GraphicsState graphics;
        containers::ArraySpan<const VertexStream> vertexStreams;
        containers::ArraySpan<const VertexAttribute> vertexAttributes;
        RayTracingState rayTracing;
        containers::ArraySpan<const RayTracingGroup> rayTracingGroups;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 16ull * 1024ull * 1024ull;
        u32 maximumShaders = 64;
        u32 maximumVertexStreams = 64;
        u32 maximumVertexAttributes = 256;
        u32 maximumRayTracingGroups = 4096;
    };

    class PipelineFile final
    {
    public:
        PipelineFile() noexcept;
        ~PipelineFile() = default;

        PipelineFile(const PipelineFile&) = delete;
        PipelineFile& operator=(const PipelineFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] PipelineKind GetKind() const noexcept;
        [[nodiscard]] u64 GetName() const noexcept;
        [[nodiscard]] DynamicState GetDynamicStates() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetTemplateFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ShaderReference> GetShaders() const noexcept;
        [[nodiscard]] const GraphicsState& GetGraphics() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexStream> GetVertexStreams() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexAttribute> GetVertexAttributes() const noexcept;
        [[nodiscard]] const RayTracingState& GetRayTracing() const noexcept;
        [[nodiscard]] containers::ArraySpan<const RayTracingGroup> GetRayTracingGroups() const noexcept;

    private:
        PipelineKind m_kind = PipelineKind::Graphics;
        u64 m_name = 0;
        DynamicState m_dynamicStates = DynamicState::None;
        crypto::Digest256 m_templateFingerprint;
        containers::DynamicArray<ShaderReference> m_shaders;
        GraphicsState m_graphics;
        containers::DynamicArray<VertexStream> m_vertexStreams;
        containers::DynamicArray<VertexAttribute> m_vertexAttributes;
        RayTracingState m_rayTracing;
        containers::DynamicArray<RayTracingGroup> m_rayTracingGroups;
        bool m_open = false;
    };

    [[nodiscard]] Result WritePipeline(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result CalculateTemplateFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculateConcretePipelineKey(const PipelineFile& pipeline, const AttachmentSignature* attachments, crypto::Digest256& key) noexcept;
    [[nodiscard]] Result ValidateShaderCompatibility(const PipelineFile& pipeline, const shaders::ShaderFile& shader,
                                                     const AttachmentSignature* attachments = nullptr) noexcept;
} // namespace vanguard::pipelines
