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
        // Pipeline material shaders share the layout-authority shader's domain,
        // material layout and graph permutation; pass entries/resources may differ.
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

    /// One sealed offline compatibility rule. Asset types absent from this table
    /// are not assignable to material resource parameters of that kind.
    struct ResourceTypeCompatibility
    {
        ResourceParameterKind kind = ResourceParameterKind::Texture;
        resources::ResourceTypeId assetType = resources::InvalidResourceTypeId;
    };

    /// Describes a fully flattened runtime material. The shader's sealed material
    /// contract identifies the complete logical interface. Byte layout and value
    /// types are derived from reflection; descriptor placement is renderer policy.
    struct BuildDescription
    {
        u64 name = 0;
        resources::ResourceReference shader;
        const shaders::ShaderFile* shaderReflection = nullptr;
        containers::ArraySpan<const TechniqueBuildRecord> techniques;
        containers::ArraySpan<const ConstantValueBuildRecord> constants;
        containers::ArraySpan<const ResourceValueBuildRecord> resources;
        containers::ArraySpan<const ResourceTypeCompatibility> resourceTypeCompatibility;
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
        u32 slot = 0;
        ResourceParameterKind kind = ResourceParameterKind::Texture;
        resources::ResourceTypeId expectedAssetType = resources::InvalidResourceTypeId;
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
        u32 maximumResourceParameters = 256;
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
        [[nodiscard]] u64 GetName() const noexcept;
        [[nodiscard]] const resources::ResourceReference& GetShader() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetMaterialDomainFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetMaterialLayoutFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const TechniqueRecord> GetTechniques() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ConstantBufferRecord> GetConstantBuffers() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ParameterRecord> GetParameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ResourceParameterRecord> GetResourceParameters() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ResourceDependency> GetDependencies() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetParameterData() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetConstantBufferData(const ConstantBufferRecord& buffer) const noexcept;

    private:
        u64 m_name = 0;
        resources::ResourceReference m_shader;
        crypto::Digest256 m_contentFingerprint;
        crypto::Digest256 m_materialDomainFingerprint;
        crypto::Digest256 m_materialLayoutFingerprint;
        containers::DynamicArray<TechniqueRecord> m_techniques;
        containers::DynamicArray<ConstantBufferRecord> m_constantBuffers;
        containers::DynamicArray<ParameterRecord> m_parameters;
        containers::DynamicArray<ResourceParameterRecord> m_resourceParameters;
        containers::DynamicArray<ResourceDependency> m_dependencies;
        containers::DynamicArray<u8> m_parameterData;
        bool m_open = false;
    };

    struct LoadedMaterialDependency
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
        resources::ResourceHandle handle;
        resources::Failure failure = resources::Failure::None;
    };

    /// Immutable CPU material artifact and the exact non-Soft dependency
    /// generations observed at construction. Optional failures remain visible;
    /// Soft references remain only in MaterialFile and do not become load edges.
    class MaterialResourceObject final : public resources::ResourceObject
    {
    public:
        MaterialResourceObject() noexcept;
        ~MaterialResourceObject() override = default;

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const MaterialFile& GetFile() const noexcept;
        [[nodiscard]] containers::ArraySpan<const LoadedMaterialDependency> GetLoadedDependencies() const noexcept;

    private:
        MaterialFile m_file;
        containers::DynamicArray<LoadedMaterialDependency> m_loadedDependencies;

        friend resources::ResourceObject* DecodeMaterialResource(resources::ResourceReference, const void*, usize, const resources::LoadContext&, resources::Failure&, void*) noexcept;
    };

    struct MaterialResourceDecoderConfig
    {
        ReadLimits limits;
    };

    /// ResourceStreamer-compatible callbacks. The optional user data points to a
    /// MaterialResourceDecoderConfig and must outlive decoder registration.
    [[nodiscard]] resources::ResourceObject* DecodeMaterialResource(resources::ResourceReference reference, const void* data, usize size, const resources::LoadContext& context, resources::Failure& failure,
                                                                    void* userData) noexcept;
    void DestroyMaterialResource(resources::ResourceObject* resource, void* userData) noexcept;

    [[nodiscard]] Result WriteMaterial(filesystem::IFile& writer, const BuildDescription& description) noexcept;
    [[nodiscard]] Result CalculateContentFingerprint(const BuildDescription& description, crypto::Digest256& fingerprint) noexcept;
} // namespace vanguard::materials
