#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::resources
{
    class LoadContext;
}

namespace vanguard::shaders
{
    inline constexpr u32 ShaderMagic = serialization::MakeFourCC('V', 'S', 'H', 'D');
    inline constexpr resources::ResourceTypeId ShaderResourceType = serialization::MakeFourCC('V', 'S', 'H', 'D');
    inline constexpr u32 MaximumEntryPointLength = 128;

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
        DuplicateStage,
        DuplicateBinding,
        OverlappingBinding,
        DuplicateInput,
        DuplicateOutput,
        InvalidBytecode,
        IncompatiblePipeline,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;
    /// Canonical, case-sensitive identity used by shader reflection and every
    /// cooker that addresses reflected interface members.
    [[nodiscard]] u64 HashInterfaceName(const char* name) noexcept;
    [[nodiscard]] u64 HashInterfaceChildName(u64 parent, const char* name) noexcept;

    enum class ProgramKind : u8
    {
        Graphics,
        Compute,
        Library
    };

    enum class ShaderStage : u8
    {
        Vertex,
        Hull,
        Domain,
        Geometry,
        Fragment,
        Compute,
        Task,
        Mesh,
        Library,
        Count
    };

    using StageMask = u32;

    [[nodiscard]] constexpr StageMask StageBit(const ShaderStage stage) noexcept
    {
        return stage < ShaderStage::Count ? 1u << static_cast<u32>(stage) : 0;
    }

    enum class NativeFormat : u8
    {
        Dxil,
        SpirV,
        MetalLibrary,
        ConsoleNative
    };

    enum class BindingKind : u8
    {
        ConstantBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        StructuredBuffer,
        ReadWriteStructuredBuffer,
        ByteAddressBuffer,
        ReadWriteByteAddressBuffer,
        AccelerationStructure,
        TypedBuffer,
        ReadWriteTypedBuffer
    };

    enum class BindingAccess : u8
    {
        Read,
        Write,
        ReadWrite
    };

    enum class BindingFlags : u16
    {
        None = 0,
        Bindless = 1u << 0u
    };

    [[nodiscard]] constexpr bool HasFlag(const BindingFlags value, const BindingFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    inline constexpr u32 UnboundedDescriptorCount = 0xffffffffu;

    enum class ScalarType : u8
    {
        Bool,
        I16,
        U16,
        F16,
        I32,
        U32,
        F32,
        I64,
        U64,
        F64
    };

    enum class NumericClass : u8
    {
        FloatingPoint,
        SignedInteger,
        UnsignedInteger
    };

    enum class PrimitiveClass : u8
    {
        Any,
        Point,
        Line,
        Triangle,
        Patch,
        Mesh
    };

    enum class InterfaceFlags : u32
    {
        None = 0,
        WritesDepth = 1u << 0u,
        UsesSampleIndex = 1u << 1u,
        UsesSampleInterpolation = 1u << 2u,
        UsesViewId = 1u << 3u,
        DualSourceBlending = 1u << 4u,
        RequiresEarlyDepthStencil = 1u << 5u
    };

    [[nodiscard]] constexpr InterfaceFlags operator|(const InterfaceFlags left, const InterfaceFlags right) noexcept
    {
        return static_cast<InterfaceFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const InterfaceFlags value, const InterfaceFlags flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    struct StageRecord
    {
        ShaderStage stage = ShaderStage::Count;
        NativeFormat format = NativeFormat::Dxil;
        u64 entryPoint = 0;
        char entryPointName[MaximumEntryPointLength]{};
        u64 bytecodeOffset = 0;
        u64 bytecodeSize = 0;
        crypto::Digest256 bytecodeDigest;
    };

    struct StageBuildRecord
    {
        ShaderStage stage = ShaderStage::Count;
        NativeFormat format = NativeFormat::Dxil;
        u64 entryPoint = 0;
        const void* bytecode = nullptr;
        usize bytecodeSize = 0;
        const char* entryPointName = nullptr;
    };

    struct DescriptorBinding
    {
        u64 name = 0;
        u32 space = 0;
        u32 binding = 0;
        u32 arrayCount = 1;
        BindingKind kind = BindingKind::SampledTexture;
        BindingAccess access = BindingAccess::Read;
        StageMask stages = 0;
        BindingFlags flags = BindingFlags::None;
    };

    struct ConstantBuffer
    {
        u64 name = 0;
        u32 space = 0;
        u32 binding = 0;
        u32 byteSize = 0;
        u32 firstMember = 0;
        u32 memberCount = 0;
    };

    struct ConstantMember
    {
        u64 name = 0;
        u32 byteOffset = 0;
        u32 byteSize = 0;
        u32 arrayStride = 0;
        u32 matrixStride = 0;
        ScalarType scalarType = ScalarType::F32;
        u8 rows = 1;
        u8 columns = 1;
        bool rowMajor = false;
    };

    enum class MaterialResourceKind : u8
    {
        Texture,
        Buffer,
        Sampler,
        AccelerationStructure
    };

    enum class MaterialResourceFlags : u8
    {
        None = 0,
        Required = 1u << 0u
    };

    [[nodiscard]] constexpr bool HasFlag(const MaterialResourceFlags value, const MaterialResourceFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    enum class MaterialResourceAccess : u8
    {
        None,
        Read,
        Write,
        ReadWrite
    };

    enum class MaterialTextureDimension : u8
    {
        None,
        D1,
        D2,
        D3,
        Cube
    };

    enum class MaterialBufferKind : u8
    {
        None,
        Typed,
        Structured,
        ByteAddress
    };

    enum class MaterialSamplerKind : u8
    {
        None,
        Filtering,
        Comparison
    };

    enum class MaterialResourceShapeFlags : u8
    {
        None = 0,
        Arrayed = 1u << 0u,
        Multisampled = 1u << 1u
    };

    [[nodiscard]] constexpr bool HasFlag(const MaterialResourceShapeFlags value, const MaterialResourceShapeFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    /// Canonical reconstructable portion of an opaque reflected resource type.
    /// The full type fingerprint remains the compatibility authority; this
    /// record exists so runtime code can create an exact view or typed fallback
    /// without depending on Slang reflection objects.
    struct MaterialResourceShape
    {
        MaterialResourceAccess access = MaterialResourceAccess::None;
        MaterialTextureDimension textureDimension = MaterialTextureDimension::None;
        MaterialBufferKind bufferKind = MaterialBufferKind::None;
        MaterialSamplerKind samplerKind = MaterialSamplerKind::None;
        ScalarType scalarType = ScalarType::F32;
        u8 componentCount = 0;
        MaterialResourceShapeFlags flags = MaterialResourceShapeFlags::None;
        u8 reserved = 0;
        u32 elementStride = 0;

        [[nodiscard]] friend constexpr bool operator==(const MaterialResourceShape&, const MaterialResourceShape&) noexcept = default;
    };

    [[nodiscard]] constexpr bool IsValidMaterialResourceShape(const MaterialResourceKind kind, const MaterialResourceShape& shape) noexcept
    {
        const bool validAccess = shape.access == MaterialResourceAccess::Read || shape.access == MaterialResourceAccess::Write || shape.access == MaterialResourceAccess::ReadWrite;
        const bool validScalar = shape.scalarType <= ScalarType::F64;
        const u8 flags = static_cast<u8>(shape.flags);
        constexpr u8 knownFlags = static_cast<u8>(MaterialResourceShapeFlags::Arrayed) | static_cast<u8>(MaterialResourceShapeFlags::Multisampled);
        if (!validScalar || shape.reserved != 0 || (flags & ~knownFlags) != 0)
            return false;
        if (kind == MaterialResourceKind::Texture)
            return validAccess && shape.textureDimension != MaterialTextureDimension::None && shape.textureDimension <= MaterialTextureDimension::Cube && shape.bufferKind == MaterialBufferKind::None &&
                   shape.samplerKind == MaterialSamplerKind::None && shape.componentCount >= 1 && shape.componentCount <= 4 && shape.elementStride == 0 &&
                   (!HasFlag(shape.flags, MaterialResourceShapeFlags::Arrayed) || shape.textureDimension != MaterialTextureDimension::D3) &&
                   (!HasFlag(shape.flags, MaterialResourceShapeFlags::Multisampled) || shape.textureDimension == MaterialTextureDimension::D2);
        if (kind == MaterialResourceKind::Buffer)
        {
            if (!validAccess || shape.textureDimension != MaterialTextureDimension::None || shape.samplerKind != MaterialSamplerKind::None || shape.flags != MaterialResourceShapeFlags::None ||
                shape.bufferKind == MaterialBufferKind::None || shape.bufferKind > MaterialBufferKind::ByteAddress)
                return false;
            if (shape.bufferKind == MaterialBufferKind::Typed)
                return shape.componentCount >= 1 && shape.componentCount <= 4 && shape.elementStride == 0;
            if (shape.bufferKind == MaterialBufferKind::Structured)
                return shape.componentCount == 0 && shape.elementStride != 0;
            return shape.componentCount == 0 && shape.elementStride == 0;
        }
        if (kind == MaterialResourceKind::Sampler)
            return shape.access == MaterialResourceAccess::Read && shape.textureDimension == MaterialTextureDimension::None && shape.bufferKind == MaterialBufferKind::None &&
                   shape.samplerKind != MaterialSamplerKind::None && shape.samplerKind <= MaterialSamplerKind::Comparison && shape.componentCount == 0 && shape.flags == MaterialResourceShapeFlags::None &&
                   shape.elementStride == 0;
        if (kind == MaterialResourceKind::AccelerationStructure)
            return shape.access == MaterialResourceAccess::Read && shape.textureDimension == MaterialTextureDimension::None && shape.bufferKind == MaterialBufferKind::None &&
                   shape.samplerKind == MaterialSamplerKind::None && shape.componentCount == 0 && shape.flags == MaterialResourceShapeFlags::None && shape.elementStride == 0;
        return false;
    }

    /// Offline shader features required by a material domain. This is a cooked
    /// shader ABI contract, not a query of the active runtime device.
    enum class MaterialShaderCapability : u32
    {
        Numeric16Bit = 1u << 0u,
        Integer64Bit = 1u << 1u,
        FloatingPoint64Bit = 1u << 2u,
        WritableResources = 1u << 3u,
        MultisampledTextures = 1u << 4u,
        ComparisonSampling = 1u << 5u,
        AccelerationStructure = 1u << 6u
    };

    using MaterialShaderCapabilityMask = u32;

    inline constexpr MaterialShaderCapabilityMask KnownMaterialShaderCapabilityMask =
        static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::Numeric16Bit) | static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::Integer64Bit) |
        static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::FloatingPoint64Bit) | static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::WritableResources) |
        static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::MultisampledTextures) | static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::ComparisonSampling) |
        static_cast<MaterialShaderCapabilityMask>(MaterialShaderCapability::AccelerationStructure);

    [[nodiscard]] constexpr MaterialShaderCapabilityMask MaterialShaderCapabilityBit(const MaterialShaderCapability capability) noexcept
    {
        return static_cast<MaterialShaderCapabilityMask>(capability);
    }

    [[nodiscard]] constexpr bool HasMaterialShaderCapability(const MaterialShaderCapabilityMask capabilities, const MaterialShaderCapability capability) noexcept
    {
        return (capabilities & MaterialShaderCapabilityBit(capability)) != 0;
    }

    [[nodiscard]] constexpr bool IsValidMaterialShaderCapabilityMask(const MaterialShaderCapabilityMask capabilities) noexcept
    {
        return (capabilities & ~KnownMaterialShaderCapabilityMask) == 0;
    }

    /// Shader-authored ABI for one extensible material domain. The input and output
    /// digests describe the complete canonical Slang type trees, not only type names.
    struct MaterialDomainContract
    {
        u64 name = 0;
        u32 schemaVersion = 0;
        StageMask legalStages = 0;
        MaterialShaderCapabilityMask requiredCapabilities = 0;
        crypto::Digest256 inputType;
        crypto::Digest256 outputType;
    };

    [[nodiscard]] inline bool MaterialDomainContractsEqual(const MaterialDomainContract& left, const MaterialDomainContract& right) noexcept
    {
        return left.name == right.name && left.schemaVersion == right.schemaVersion && left.legalStages == right.legalStages && left.requiredCapabilities == right.requiredCapabilities &&
               left.inputType == right.inputType && left.outputType == right.outputType;
    }

    /// One logical resource position in the renderer-global material resource array.
    /// Descriptor spaces and bindings are intentionally absent.
    struct MaterialResourceRole
    {
        u64 name = 0;
        u32 arrayIndex = 0;
        u32 slot = 0;
        MaterialResourceKind kind = MaterialResourceKind::Texture;
        MaterialResourceFlags flags = MaterialResourceFlags::None;
        u16 reserved = 0;
        crypto::Digest256 typeFingerprint;
        MaterialResourceShape shape;
    };

    struct MaterialContractBuildDescription
    {
        MaterialDomainContract domain;
        u32 accessorAbiVersion = 0;
        u32 parameterByteSize = 0;
        containers::ArraySpan<const ConstantMember> parameters;
        containers::ArraySpan<const MaterialResourceRole> resources;
    };

    struct MaterialContract
    {
        MaterialDomainContract domain;
        u32 accessorAbiVersion = 0;
        u32 parameterByteSize = 0;
        crypto::Digest256 domainFingerprint;
        crypto::Digest256 layoutFingerprint;
    };

    struct VertexInput
    {
        u64 semantic = 0;
        u32 semanticIndex = 0;
        u32 location = 0;
        NumericClass numericClass = NumericClass::FloatingPoint;
        u8 componentCount = 0;
        u8 componentBits = 0;
    };

    struct FragmentOutput
    {
        u64 semantic = 0;
        u32 location = 0;
        u32 blendSource = 0;
        NumericClass numericClass = NumericClass::FloatingPoint;
        u8 componentMask = 0;
    };

    struct SpecializationConstant
    {
        u64 name = 0;
        u32 id = 0;
        ScalarType scalarType = ScalarType::U32;
        u64 defaultValueBits = 0;
        StageMask stages = 0;
    };

    struct PipelineInterface
    {
        StageMask stages = 0;
        PrimitiveClass primitiveClass = PrimitiveClass::Any;
        InterfaceFlags flags = InterfaceFlags::None;
        u32 renderTargetCount = 0;
        u32 threadGroupSizeX = 0;
        u32 threadGroupSizeY = 0;
        u32 threadGroupSizeZ = 0;
    };

    struct BuildDescription
    {
        ProgramKind kind = ProgramKind::Graphics;
        u64 program = 0;
        crypto::Digest256 permutation;
        crypto::Digest256 compilerFingerprint;
        PipelineInterface pipelineInterface;
        containers::ArraySpan<const StageBuildRecord> stages;
        containers::ArraySpan<const DescriptorBinding> bindings;
        containers::ArraySpan<const ConstantBuffer> constantBuffers;
        containers::ArraySpan<const ConstantMember> constantMembers;
        containers::ArraySpan<const VertexInput> vertexInputs;
        containers::ArraySpan<const FragmentOutput> fragmentOutputs;
        containers::ArraySpan<const SpecializationConstant> specializationConstants;
        const MaterialContractBuildDescription* materialContract = nullptr;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 512ull * 1024ull * 1024ull;
        u64 maximumBytecodeBytes = 480ull * 1024ull * 1024ull;
        u32 maximumStages = 16;
        u32 maximumBindings = 16384;
        u32 maximumConstantBuffers = 4096;
        u32 maximumConstantMembers = 262144;
        u32 maximumVertexInputs = 256;
        u32 maximumFragmentOutputs = 32;
        u32 maximumSpecializationConstants = 4096;
        u32 maximumMaterialParameters = 16384;
        u32 maximumMaterialResources = 256;
    };

    enum class PipelineKind : u8
    {
        Graphics,
        Compute,
        RayTracing
    };

    struct PipelineCompatibility
    {
        PipelineKind kind = PipelineKind::Graphics;
        PrimitiveClass primitiveClass = PrimitiveClass::Triangle;
        u32 renderTargetCount = 0;
        NumericClass renderTargetClasses[8]{};
        u32 sampleCount = 1;
        bool depthStencilFormatPresent = false;
        bool dualSourceBlendEnabled = false;
        containers::ArraySpan<const VertexInput> vertexLayout;
        crypto::Digest256 bindingLayoutFingerprint;
        crypto::Digest256 pipelineInterfaceFingerprint;
    };

    class ShaderFile final
    {
    public:
        ShaderFile() noexcept;
        ~ShaderFile() = default;

        ShaderFile(const ShaderFile&) = delete;
        ShaderFile& operator=(const ShaderFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ProgramKind GetKind() const noexcept;
        [[nodiscard]] u64 GetProgram() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetPermutation() const noexcept;
        [[nodiscard]] const crypto::Digest256& CompilerFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetLayoutFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& BindingLayoutFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetPipelineInterfaceFingerprint() const noexcept;
        [[nodiscard]] const PipelineInterface& GetInterface() const noexcept;
        [[nodiscard]] containers::ArraySpan<const StageRecord> GetStages() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DescriptorBinding> Bindings() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantBuffer> GetConstantBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantMember> GetConstantMembers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexInput> GetVertexInputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const FragmentOutput> GetFragmentOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SpecializationConstant> GetSpecializationConstants() const noexcept;
        [[nodiscard]] bool HasMaterialContract() const noexcept;
        [[nodiscard]] const MaterialContract* GetMaterialContract() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantMember> GetMaterialParameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialResourceRole> GetMaterialResources() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetBytecode(const StageRecord& stage) const noexcept;

    private:
        ProgramKind m_kind = ProgramKind::Graphics;
        u64 m_program = 0;
        crypto::Digest256 m_permutation;
        crypto::Digest256 m_compilerFingerprint;
        crypto::Digest256 m_layoutFingerprint;
        crypto::Digest256 m_bindingLayoutFingerprint;
        crypto::Digest256 m_pipelineInterfaceFingerprint;
        PipelineInterface m_interface;
        containers::DynamicArray<StageRecord> m_stages;
        containers::DynamicArray<DescriptorBinding> m_bindings;
        containers::DynamicArray<ConstantBuffer> m_constantBuffers;
        containers::DynamicArray<ConstantMember> m_constantMembers;
        containers::DynamicArray<VertexInput> m_vertexInputs;
        containers::DynamicArray<FragmentOutput> m_fragmentOutputs;
        containers::DynamicArray<SpecializationConstant> m_specializationConstants;
        MaterialContract m_materialContract;
        containers::DynamicArray<ConstantMember> m_materialParameters;
        containers::DynamicArray<MaterialResourceRole> m_materialResources;
        containers::DynamicArray<u8> m_bytecode;
        bool m_open = false;
    };

    /// Immutable CPU-side resource produced by the ordinary loose/VPAK
    /// ResourceStreamer decoder path. Native shader creation is renderer-owned.
    class ShaderResourceObject final : public resources::ResourceObject
    {
    public:
        ShaderResourceObject() noexcept = default;
        ~ShaderResourceObject() override = default;

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const ShaderFile& GetFile() const noexcept;

    private:
        ShaderFile m_file;

        friend resources::ResourceObject* DecodeShaderResource(resources::ResourceReference, const void*, usize, const resources::LoadContext&, resources::Failure&, void*) noexcept;
    };

    struct ShaderResourceDecoderConfig
    {
        ReadLimits limits;
    };

    /// ResourceStreamer-compatible callbacks. The optional user data points to a
    /// ShaderResourceDecoderConfig and must outlive decoder registration.
    [[nodiscard]] resources::ResourceObject* DecodeShaderResource(resources::ResourceReference reference, const void* data, usize size, const resources::LoadContext& context, resources::Failure& failure,
                                                                  void* userData) noexcept;
    void DestroyShaderResource(resources::ResourceObject* resource, void* userData) noexcept;

    [[nodiscard]] Result WriteShader(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result CalculateMaterialDomainFingerprint(const MaterialDomainContract& domain, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculateLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculateBindingLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculatePipelineInterfaceFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result ValidatePipeline(const ShaderFile& shader, const PipelineCompatibility& pipeline) noexcept;
} // namespace vanguard::shaders
