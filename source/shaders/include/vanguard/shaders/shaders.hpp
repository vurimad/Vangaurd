#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::shaders
{
    inline constexpr u32 ShaderMagic = serialization::MakeFourCC('V', 'S', 'H', 'D');
    inline constexpr resources::ResourceTypeId ShaderResourceType = serialization::MakeFourCC('V', 'S', 'H', 'D');

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
        AccelerationStructure
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
        [[nodiscard]] ProgramKind Kind() const noexcept;
        [[nodiscard]] u64 Program() const noexcept;
        [[nodiscard]] const crypto::Digest256& Permutation() const noexcept;
        [[nodiscard]] const crypto::Digest256& CompilerFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& LayoutFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& BindingLayoutFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& PipelineInterfaceFingerprint() const noexcept;
        [[nodiscard]] const PipelineInterface& Interface() const noexcept;
        [[nodiscard]] containers::ArraySpan<const StageRecord> Stages() const noexcept;
        [[nodiscard]] containers::ArraySpan<const DescriptorBinding> Bindings() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantBuffer> ConstantBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantMember> ConstantMembers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const VertexInput> VertexInputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const FragmentOutput> FragmentOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SpecializationConstant> SpecializationConstants() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> Bytecode(const StageRecord& stage) const noexcept;

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
        containers::DynamicArray<u8> m_bytecode;
        bool m_open = false;
    };

    [[nodiscard]] Result WriteShader(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result CalculateLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculateBindingLayoutFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result CalculatePipelineInterfaceFingerprint(const BuildDescription& description,
                                                               crypto::Digest256& fingerprint) noexcept;
    [[nodiscard]] Result ValidatePipeline(const ShaderFile& shader, const PipelineCompatibility& pipeline) noexcept;
} // namespace vanguard::shaders
