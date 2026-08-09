#pragma once

#include <vanguard/pipelines/pipelines.hpp>

namespace vanguard::materials
{
    inline constexpr u32 MaterialMagic = serialization::MakeFourCC('V', 'M', 'A', 'T');
    inline constexpr resources::ResourceTypeId MaterialResourceType = serialization::MakeFourCC('V', 'M', 'A', 'T');

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
        DuplicateTechnique,
        DuplicateParameter,
        DuplicateResource,
        UnknownShaderInterface,
        TypeMismatch,
        IoFailure
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    struct TechniqueBuildRecord
    {
        u64 name = 0;
        resources::ResourceReference pipeline;
        const pipelines::PipelineFile* pipelineReflection = nullptr;
    };

    struct ConstantValueBuildRecord
    {
        u64 name = 0;
        const void* data = nullptr;
        u32 byteSize = 0;
    };

    struct ResourceValueBuildRecord
    {
        u64 name = 0;
        u32 arrayIndex = 0;
        resources::ResourceReference resource;
        resources::DependencyKind dependency = resources::DependencyKind::Required;
    };

    enum class ResourceParameterKind : u8
    {
        Texture,
        Buffer,
        Sampler,
        AccelerationStructure
    };

    struct ResourceParameterBuildRecord
    {
        u64 name = 0;
        u32 arrayCount = 1;
        ResourceParameterKind kind = ResourceParameterKind::Texture;
    };

    /// Describes a fully flattened runtime material. Selected names identify the
    /// material-owned part of the logical shader interface. Byte layout and value
    /// types are derived from reflection; descriptor placement is renderer policy.
    struct BuildDescription
    {
        u64 name = 0;
        resources::ResourceReference shader;
        const shaders::ShaderFile* shaderReflection = nullptr;
        containers::ArraySpan<const u64> materialConstantBuffers;
        containers::ArraySpan<const ResourceParameterBuildRecord> resourceParameters;
        containers::ArraySpan<const TechniqueBuildRecord> techniques;
        containers::ArraySpan<const ConstantValueBuildRecord> constants;
        containers::ArraySpan<const ResourceValueBuildRecord> resources;
    };

    struct TechniqueRecord
    {
        u64 name = 0;
        resources::ResourceReference pipeline;
    };

    struct ConstantBufferRecord
    {
        u64 name = 0;
        u32 byteSize = 0;
        u32 dataOffset = 0;
        u32 firstParameter = 0;
        u32 parameterCount = 0;
    };

    struct ParameterRecord
    {
        u64 name = 0;
        u32 buffer = 0;
        u32 byteOffset = 0;
        u32 byteSize = 0;
        u32 arrayStride = 0;
        u32 matrixStride = 0;
        shaders::ScalarType scalarType = shaders::ScalarType::F32;
        u8 rows = 1;
        u8 columns = 1;
        bool rowMajor = false;
    };

    struct ResourceParameterRecord
    {
        u64 name = 0;
        u32 arrayIndex = 0;
        ResourceParameterKind kind = ResourceParameterKind::Texture;
        resources::ResourceReference resource;
        resources::DependencyKind dependency = resources::DependencyKind::Optional;
    };

    struct ResourceDependency
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    struct ReadLimits
    {
        u64 maximumFileSize = 64ull * 1024ull * 1024ull;
        u32 maximumTechniques = 256;
        u32 maximumConstantBuffers = 256;
        u32 maximumParameters = 16384;
        u32 maximumResourceParameters = 16384;
        u32 maximumParameterBytes = 16u * 1024u * 1024u;
        u32 maximumDependencies = 32768;
    };

    class MaterialFile final
    {
    public:
        MaterialFile() noexcept;
        ~MaterialFile() = default;

        MaterialFile(const MaterialFile&) = delete;
        MaterialFile& operator=(const MaterialFile&) = delete;

        [[nodiscard]] Result Open(filesystem::IFile& reader, const ReadLimits& limits = {}) noexcept;
        void Close() noexcept;

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] u64 Name() const noexcept;
        [[nodiscard]] const resources::ResourceReference& Shader() const noexcept;
        [[nodiscard]] const crypto::Digest256& ContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const TechniqueRecord> Techniques() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantBufferRecord> ConstantBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ParameterRecord> Parameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ResourceParameterRecord> ResourceParameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ResourceDependency> Dependencies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> ParameterData() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> ConstantBufferData(const ConstantBufferRecord& buffer) const noexcept;

    private:
        u64 m_name = 0;
        resources::ResourceReference m_shader;
        crypto::Digest256 m_contentFingerprint;
        containers::DynamicArray<TechniqueRecord> m_techniques;
        containers::DynamicArray<ConstantBufferRecord> m_constantBuffers;
        containers::DynamicArray<ParameterRecord> m_parameters;
        containers::DynamicArray<ResourceParameterRecord> m_resourceParameters;
        containers::DynamicArray<ResourceDependency> m_dependencies;
        containers::DynamicArray<u8> m_parameterData;
        bool m_open = false;
    };

    [[nodiscard]] Result WriteMaterial(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result CalculateContentFingerprint(const BuildDescription& description,
                                                     crypto::Digest256& fingerprint) noexcept;
} // namespace vanguard::materials
