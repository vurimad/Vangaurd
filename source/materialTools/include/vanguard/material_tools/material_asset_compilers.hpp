#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>
#include <vanguard/textures/textures.hpp>

namespace vanguard::material_tools
{
    inline constexpr resources::ResourceTypeId MaterialProgramInputResourceType = serialization::MakeFourCC('M', 'P', 'G', 'I');
    inline constexpr resources::ResourceTypeId MaterialPipelineInputResourceType = serialization::MakeFourCC('M', 'P', 'L', 'I');
    inline constexpr resources::ResourceTypeId MaterialValueInputResourceType = serialization::MakeFourCC('M', 'V', 'L', 'I');
    inline constexpr resources::ResourceTypeId MaterialCompilerToolResourceType = serialization::MakeFourCC('M', 'T', 'O', 'L');
    inline constexpr u32 MaterialArtifactCompilerVersion = 4;

    /// Headless cook policy. It is deliberately independent from the active RHI
    /// adapter: cooks may run on a different machine from the target runtime.
    struct MaterialOfflineContract
    {
        shaders::MaterialShaderCapabilityMask targetCapabilities[static_cast<u32>(assets::TargetPlatform::Count)]{
            shaders::KnownMaterialShaderCapabilityMask,
            shaders::KnownMaterialShaderCapabilityMask,
            shaders::KnownMaterialShaderCapabilityMask};
        resources::ResourceTypeId resourceTypes[4]{textures::TextureResourceType, resources::InvalidResourceTypeId,
                                                    resources::InvalidResourceTypeId, resources::InvalidResourceTypeId};

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] shaders::MaterialShaderCapabilityMask Capabilities(assets::TargetPlatform target) const noexcept;
        [[nodiscard]] resources::ResourceTypeId ResourceType(materials::ResourceParameterKind kind) const noexcept;
    };

    [[nodiscard]] bool CalculateMaterialOfflineContractFingerprint(const MaterialOfflineContract& contract,
                                                                    crypto::Digest256& fingerprint) noexcept;

    enum class MaterialInputResult : u8
    {
        Success,
        InvalidArgument,
        LimitExceeded,
        InvalidEncoding,
        UnsupportedVersion
    };

    struct MaterialProgramInput
    {
        const char* sourceName = nullptr;
        const char* moduleName = nullptr;
        u64 program = 0;
        crypto::Digest256 permutation;
        shaders::MaterialDomainContract expectedDomain;
        crypto::Digest256 expectedDomainFingerprint;
        shaders::MaterialShaderCapabilityMask requiredCapabilities = 0;
        containers::ArraySpan<const u8> source;
        containers::ArraySpan<const shader_tools::EntryPoint> entryPoints;
        containers::ArraySpan<const shader_tools::Define> defines;
        containers::ArraySpan<const MaterialGeneratedSourceRange> sourceRanges;
        shader_tools::CompileSettings settings;
    };

    struct MaterialPipelineInput
    {
        resources::ResourceReference shader;
        pipelines::BuildDescription recipe;
    };

    /// One canonical, tightly packed material value. MVLI keeps authored value
    /// semantics independent of backend constant-buffer padding; the material
    /// compiler repacks these bytes against the reflected VSHADER layout.
    struct MaterialLogicalConstantValue
    {
        u64 name = 0;
        shaders::ScalarType scalarType = shaders::ScalarType::F32;
        u8 rows = 1;
        u8 columns = 1;
        bool rowMajor = false;
        u32 arrayCount = 1;
        containers::ArraySpan<const u8> data;
    };

    struct MaterialValueInput
    {
        u64 name = 0;
        resources::ResourceReference shader;
        containers::ArraySpan<const materials::TechniqueBuildRecord> techniques;
        containers::ArraySpan<const MaterialLogicalConstantValue> constants;
        containers::ArraySpan<const materials::ResourceValueBuildRecord> resources;
    };

    [[nodiscard]] MaterialInputResult EncodeMaterialProgramInput(const MaterialProgramInput& input,
                                                                 containers::DynamicArray<u8>& bytes) noexcept;
    [[nodiscard]] MaterialInputResult EncodeMaterialPipelineInput(const MaterialPipelineInput& input,
                                                                  containers::DynamicArray<u8>& bytes) noexcept;
    [[nodiscard]] MaterialInputResult EncodeMaterialValueInput(const MaterialValueInput& input,
                                                               containers::DynamicArray<u8>& bytes) noexcept;

    struct MaterialArtifactCompilerConfig
    {
        MaterialOfflineContract offline;
        u32 maximumEntryPoints = 32;
        u32 maximumDefines = 1024;
        u32 maximumSourceRanges = 65536;
        u32 maximumTechniques = 256;
        u32 maximumConstants = 16384;
        u32 maximumResources = 16384;
        u64 maximumProgramInputBytes = 64ull * 1024ull * 1024ull;
        u64 maximumProgramArtifactBytes = 544ull * 1024ull * 1024ull;
        u64 maximumPipelineArtifactBytes = 16ull * 1024ull * 1024ull;
        u64 maximumMaterialArtifactBytes = 64ull * 1024ull * 1024ull;
        u64 estimatedShaderTransientBytes = 1024ull * 1024ull * 1024ull;
    };

    class MaterialArtifactCompilers final
    {
    public:
        struct Impl;

        MaterialArtifactCompilers() noexcept = default;
        ~MaterialArtifactCompilers();

        MaterialArtifactCompilers(const MaterialArtifactCompilers&) = delete;
        MaterialArtifactCompilers& operator=(const MaterialArtifactCompilers&) = delete;

        [[nodiscard]] bool Initialize(const MaterialArtifactCompilerConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] assets::Result Register(assets::BuildSystem& buildSystem) noexcept;
        [[nodiscard]] assets::Result Unregister() noexcept;
        [[nodiscard]] assets::CompilerDescriptor ProgramDescriptor() noexcept;
        [[nodiscard]] assets::CompilerDescriptor PipelineDescriptor() noexcept;
        [[nodiscard]] assets::CompilerDescriptor MaterialDescriptor() noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    [[nodiscard]] const char* ToString(MaterialInputResult result) noexcept;
} // namespace vanguard::material_tools
