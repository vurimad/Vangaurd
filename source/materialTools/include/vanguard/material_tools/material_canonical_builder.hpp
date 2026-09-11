#pragma once

#include <vanguard/material_tools/material_asset_compilers.hpp>
#include <vanguard/material_tools/material_frontend.hpp>

namespace vanguard::material_tools
{
    enum class MaterialCanonicalResult : u8
    {
        Success,
        InvalidArgument,
        DomainMismatch,
        FrontendFailure,
        SlangFailure,
        MissingTechnique,
        DuplicateTechnique,
        InvalidDynamicValue,
        InvalidResourceValue,
        UnsupportedTargetCapability,
        LimitExceeded,
        InputEncodingFailure
    };

    struct MaterialCanonicalDiagnostic
    {
        MaterialCanonicalResult code = MaterialCanonicalResult::Success;
        MaterialFrontendResult frontend = MaterialFrontendResult::Success;
        MaterialIrResult ir = MaterialIrResult::Success;
        MaterialSlangResult slang = MaterialSlangResult::Success;
        MaterialInputResult input = MaterialInputResult::Success;
        resources::ResourceReference resource;
        u64 node = 0;
        u32 pin = 0;
    };

    /// One domain-required technique after authored pipeline-template decoding.
    /// source must equal the frozen domain requirement's pipelineTemplate.
    struct MaterialCanonicalTechnique
    {
        u64 name = 0;
        resources::ResourceReference source;
        resources::ResourceReference output;
        pipelines::BuildDescription recipe;
        // Optional pass entry selection over the same generated source, IR
        // permutation, defines and compile settings. All three are required
        // together; otherwise this technique uses the primary shader.
        resources::ResourceReference programSource;
        resources::ResourceReference shader;
        containers::ArraySpan<const shader_tools::EntryPoint> entryPoints;
    };

    /// One authored logical resource assignment. semantic addresses the
    /// DynamicParameter declaration in the finalized material IR.
    struct MaterialCanonicalResourceValue
    {
        u64 semantic = 0;
        u32 arrayIndex = 0;
        resources::ResourceReference resource;
        resources::DependencyKind dependency = resources::DependencyKind::Required;
    };

    struct MaterialCanonicalDescription
    {
        MaterialSourceGraph graph;
        MaterialFrontendConfig frontend;
        MaterialSlangDomain slang;

        const char* sourceName = nullptr;
        const char* moduleName = nullptr;
        u64 program = 0;
        u64 material = 0;
        containers::ArraySpan<const shader_tools::EntryPoint> entryPoints;
        containers::ArraySpan<const shader_tools::Define> defines;
        shader_tools::CompileSettings compileSettings;
        MaterialOfflineContract offline;

        resources::ResourceReference programSource;
        resources::ResourceReference shader;
        resources::ResourceReference materialSource;
        resources::ResourceReference materialOutput;
        containers::ArraySpan<const MaterialCanonicalTechnique> techniques;
        containers::ArraySpan<const MaterialCanonicalResourceValue> resources;

        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        containers::ArraySpan<const u8> buildSettings;
    };

    struct MaterialCanonicalLimits
    {
        u32 maximumTechniques = 256;
        u32 maximumResourceValues = 16384;
        // Total serialized source/input bytes across primary and pass programs.
        u32 maximumProgramInputBytes = 64u * 1024u * 1024u;
        u32 maximumPipelineInputBytes = 16u * 1024u * 1024u;
        u32 maximumMaterialInputBytes = 64u * 1024u * 1024u;
        u32 maximumBuildSettingsBytes = 1024u * 1024u;
        MaterialSlangLimits slang;
    };

    /// Owns canonical input bytes and BuildRequest views over those bytes.
    /// Rebuilding the object invalidates previously copied request spans.
    class MaterialCanonicalBuildSet final
    {
    public:
        MaterialCanonicalBuildSet() noexcept;

        MaterialCanonicalBuildSet(const MaterialCanonicalBuildSet&) = delete;
        MaterialCanonicalBuildSet& operator=(const MaterialCanonicalBuildSet&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const assets::BuildRequest& Program() const noexcept;
        // Includes Program() first, followed by explicit technique variants.
        // Submit this span when any technique selects its own shader entries.
        [[nodiscard]] containers::ArraySpan<const assets::BuildRequest> Programs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const assets::BuildRequest> Pipelines() const noexcept;
        [[nodiscard]] const assets::BuildRequest& Material() const noexcept;

    private:
        struct PipelineRange
        {
            u32 offset = 0;
            u32 size = 0;
        };

        struct ProgramRange
        {
            u32 offset = 0;
            u32 size = 0;
            resources::ResourceReference source;
            resources::ResourceReference output;
        };

        containers::DynamicArray<u8> m_programBytes;
        containers::DynamicArray<u8> m_pipelineBytes;
        containers::DynamicArray<u8> m_materialBytes;
        containers::DynamicArray<u8> m_buildSettings;
        containers::DynamicArray<PipelineRange> m_pipelineRanges;
        containers::DynamicArray<assets::BuildRequest> m_pipelineRequests;
        containers::DynamicArray<ProgramRange> m_programRanges;
        containers::DynamicArray<assets::BuildRequest> m_programRequests;
        assets::BuildRequest m_programRequest;
        assets::BuildRequest m_materialRequest;
        bool m_valid = false;

        friend MaterialCanonicalResult BuildMaterialCanonicalInputs(const MaterialCanonicalDescription&, MaterialCanonicalBuildSet&,
                                                                    MaterialCanonicalDiagnostic*, const MaterialCanonicalLimits&) noexcept;
    };

    [[nodiscard]] MaterialCanonicalResult BuildMaterialCanonicalInputs(const MaterialCanonicalDescription& description,
                                                                        MaterialCanonicalBuildSet& build,
                                                                        MaterialCanonicalDiagnostic* diagnostic = nullptr,
                                                                        const MaterialCanonicalLimits& limits = {}) noexcept;
    [[nodiscard]] const char* ToString(MaterialCanonicalResult result) noexcept;
} // namespace vanguard::material_tools
