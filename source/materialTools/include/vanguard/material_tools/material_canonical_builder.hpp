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

    // Contract v3 carries glTF specular color and sampled AO as distinct
    // values; transparent technique selection has its own render phase.
    inline constexpr u32 ImportedSurfaceMaterialContractVersion = 3;
    inline constexpr u32 ImportedSurfaceMaterialMapperVersion = 4;

    inline constexpr u64 ImportedSurfaceUvInputSemantic = 1;
    inline constexpr u64 ImportedSurfaceWorldNormalInputSemantic = 2;
    inline constexpr u64 ImportedSurfaceViewRelativePositionInputSemantic = 3;
    inline constexpr u64 ImportedSurfaceBaseColorOutputSemantic = 100;
    inline constexpr u64 ImportedSurfaceNormalOutputSemantic = 101;
    inline constexpr u64 ImportedSurfaceRoughnessOutputSemantic = 102;
    inline constexpr u64 ImportedSurfaceMetallicOutputSemantic = 103;
    inline constexpr u64 ImportedSurfaceEmissiveOutputSemantic = 104;
    inline constexpr u64 ImportedSurfaceOpacityCutoffOutputSemantic = 105;
    inline constexpr u64 ImportedSurfaceSpecularColorOutputSemantic = 106;
    inline constexpr u64 ImportedSurfaceOcclusionOutputSemantic = 107;
    inline constexpr u64 ImportedSurfaceBaseColorParameterSemantic = 200;
    inline constexpr u64 ImportedSurfaceNormalParameterSemantic = 201;
    inline constexpr u64 ImportedSurfaceRoughnessParameterSemantic = 202;
    inline constexpr u64 ImportedSurfaceMetallicParameterSemantic = 203;
    inline constexpr u64 ImportedSurfaceEmissiveParameterSemantic = 204;
    inline constexpr u64 ImportedSurfaceOpacityCutoffParameterSemantic = 205;
    inline constexpr u64 ImportedSurfaceSpecularColorParameterSemantic = 206;
    inline constexpr u64 ImportedSurfaceOcclusionStrengthParameterSemantic = 207;
    inline constexpr u64 ImportedSurfaceBaseColorTextureSemantic = 300;
    inline constexpr u64 ImportedSurfaceSamplerSemantic = 301;
    inline constexpr u64 ImportedSurfaceNormalTextureSemantic = 302;
    inline constexpr u64 ImportedSurfaceMetallicTextureSemantic = 303;
    inline constexpr u64 ImportedSurfaceRoughnessTextureSemantic = 304;
    inline constexpr u64 ImportedSurfaceMetallicRoughnessTextureSemantic = 305;
    inline constexpr u64 ImportedSurfaceEmissiveTextureSemantic = 306;
    inline constexpr u64 ImportedSurfaceOpacityTextureSemantic = 307;
    inline constexpr u64 ImportedSurfaceSpecularTextureSemantic = 308;
    inline constexpr u64 ImportedSurfaceOcclusionTextureSemantic = 309;
    inline constexpr u64 ImportedSurfaceSpecularStrengthTextureSemantic = 310;

    inline constexpr MaterialNodeTypeId ImportedSurfaceFloatNodeType = 0xb727e34b904dbb68ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceFloat3NodeType = 0x8682946634155ba1ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceFloat4NodeType = 0x8682956634155d54ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceTexture2DNodeType = 0xa97b9eda861e004bull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceSamplerNodeType = 0x43416933d2dd0746ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceUvNodeType = 0xfd614952ed345ff1ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceSampleNodeType = 0x3a01c6c8ccb23cd0ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceMultiplyFloat4NodeType = 0xdcf6c8b1eb1ddcadull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceExtractFloatNodeType = 0xf745ae5c13fd9fb3ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceConstructFloat3NodeType = 0xdafbcf924d4f5defull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceConstructFloat4NodeType = 0xdafbcc924d4f58d6ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceMultiplyFloatNodeType = 0xdcf6c3b1eb1dd42eull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceMultiplyFloat3NodeType = 0xdcf6c1b1eb1dd0c8ull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceSubtractFloat3NodeType = 0xb904e5a2d192f38cull;
    inline constexpr MaterialNodeTypeId ImportedSurfaceSubtractFloatNodeType = 0xb904e5a2d192f38dull;

    // These durable technique identities intentionally match the renderer's
    // standard depth-prepass and opaque render-phase keys without introducing a
    // materialTools -> rendering dependency.
    inline constexpr u64 ImportedSurfaceDepthPrepassTechnique = 0xcb496401f14ff67dull;
    inline constexpr u64 ImportedSurfaceOpaqueTechnique = 0x7dde2ef544fe4892ull;
    inline constexpr u64 ImportedSurfaceTransparentTechnique = 0x9fffb6bbeaab350dull;

    enum class ImportedSurfaceCoverageMode : u8
    {
        Opaque,
        Masked,
        Blended
    };

    enum class ImportedSurfaceTextureSemantic : u8
    {
        BaseColor,
        Normal,
        Metallic,
        Roughness,
        MetallicRoughness,
        Specular,
        Emissive,
        Occlusion,
        Opacity,
        SpecularStrength
    };

    enum class ImportedSurfaceMappingWarning : u32
    {
        None = 0,
        SpecularUnsupported = 1u << 0u,
        TextureSemanticUnsupported = 1u << 1u,
        UvSetUnsupported = 1u << 2u,
        OpaqueOpacityIgnored = 1u << 3u
    };

    [[nodiscard]] constexpr ImportedSurfaceMappingWarning operator|(const ImportedSurfaceMappingWarning left,
                                                                     const ImportedSurfaceMappingWarning right) noexcept
    {
        return static_cast<ImportedSurfaceMappingWarning>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const ImportedSurfaceMappingWarning value,
                                         const ImportedSurfaceMappingWarning flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    struct ImportedSurfaceTextureBinding
    {
        ImportedSurfaceTextureSemantic semantic = ImportedSurfaceTextureSemantic::BaseColor;
        u32 uvSet = 0;
        resources::ResourceReference resource;
        resources::DependencyKind dependency = resources::DependencyKind::Required;
    };

    /// Format-neutral, versioned input to the standard imported-surface mapper.
    /// Source-format adapters translate their material record into this structure;
    /// no Assimp/model provenance is retained in the canonical material graph.
    struct ImportedSurfaceMaterialInput
    {
        f32 baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
        f32 emissive[3]{};
        f32 specular[3]{};
        f32 metallic = 0.0f;
        f32 roughness = 1.0f;
        f32 opacity = 1.0f;
        f32 opacityCutoff = 0.5f;
        f32 occlusionStrength = 1.0f;
        f32 specularFactor = 1.0f;
        f32 ior = 1.5f;
        bool twoSided = false;
        ImportedSurfaceCoverageMode coverage = ImportedSurfaceCoverageMode::Opaque;
        containers::ArraySpan<const ImportedSurfaceTextureBinding> textures;
    };

    struct ImportedSurfaceTechniqueDescriptor
    {
        u64 name = 0;
        resources::ResourceReference pipelineTemplate;
        const char* vertexEntryPoint = nullptr;
        const char* opaqueFragmentEntryPoint = nullptr;
        const char* maskedFragmentEntryPoint = nullptr;
    };

    enum class ImportedSurfaceMappingResult : u8
    {
        Success,
        InvalidArgument
    };

    /// Owns one deterministic canonical source graph. The fixed storage keeps all
    /// borrowed graph/data/operand spans stable until Reset or destruction.
    class ImportedSurfaceMaterialMapping final
    {
    public:
        ImportedSurfaceMaterialMapping() noexcept = default;
        ImportedSurfaceMaterialMapping(const ImportedSurfaceMaterialMapping&) = delete;
        ImportedSurfaceMaterialMapping& operator=(const ImportedSurfaceMaterialMapping&) = delete;
        ImportedSurfaceMaterialMapping(ImportedSurfaceMaterialMapping&&) = delete;
        ImportedSurfaceMaterialMapping& operator=(ImportedSurfaceMaterialMapping&&) = delete;

        void Reset() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] MaterialSourceGraph GetGraph() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialCanonicalResourceValue> GetResources() const noexcept;
        [[nodiscard]] ImportedSurfaceMappingWarning GetWarnings() const noexcept;
        [[nodiscard]] ImportedSurfaceCoverageMode GetCoverageMode() const noexcept;
        [[nodiscard]] bool IsTwoSided() const noexcept;

    private:
        resources::ResourceReference m_graphIdentity;
        MaterialSourceValue m_values[96]{};
        MaterialSourceOutput m_outputs[8]{};
        MaterialCanonicalResourceValue m_resources[10]{};
        MaterialSourceOperand m_operands[160]{};
        u8 m_extractData[32][4]{};
        f32 m_baseColor[4]{};
        f32 m_normal[3]{};
        f32 m_roughness = 1.0f;
        f32 m_metallic = 0.0f;
        f32 m_emissive[3]{};
        f32 m_specular[3]{};
        f32 m_occlusionStrength = 1.0f;
        f32 m_opacityCutoff = 0.5f;
        f32 m_normalScale[3]{2.0f, 2.0f, 2.0f};
        f32 m_normalBias[3]{1.0f, 1.0f, 1.0f};
        f32 m_one = 1.0f;
        u32 m_valueCount = 0;
        u32 m_resourceCount = 0;
        u32 m_operandCount = 0;
        u32 m_extractCount = 0;
        ImportedSurfaceMappingWarning m_warnings = ImportedSurfaceMappingWarning::None;
        ImportedSurfaceCoverageMode m_coverage = ImportedSurfaceCoverageMode::Opaque;
        bool m_twoSided = false;
        bool m_valid = false;

        friend ImportedSurfaceMappingResult BuildImportedSurfaceMaterialMapping(const ImportedSurfaceMaterialInput&,
                                                                                resources::ResourceReference,
                                                                                ImportedSurfaceMaterialMapping&) noexcept;
    };

    [[nodiscard]] MaterialDomainId GetImportedSurfaceDomainId() noexcept;
    [[nodiscard]] MaterialSlangDomain MakeImportedSurfaceSlangDomain(containers::ArraySpan<const u8> prefix,
                                                                     containers::ArraySpan<const u8> suffix) noexcept;
    [[nodiscard]] containers::ArraySpan<const ImportedSurfaceTechniqueDescriptor> GetImportedSurfaceTechniques() noexcept;
    /// Adds the imported-surface node/domain definitions to mutable registries.
    /// The caller may register other schemas before freezing both registries.
    [[nodiscard]] bool RegisterImportedSurfaceMaterialSchema(const shaders::MaterialDomainContract& reflectedDomain,
                                                             MaterialNodeRegistry& nodes,
                                                             MaterialDomainRegistry& domains) noexcept;
    [[nodiscard]] ImportedSurfaceMappingResult BuildImportedSurfaceMaterialMapping(const ImportedSurfaceMaterialInput& input,
                                                                                    resources::ResourceReference graphIdentity,
                                                                                    ImportedSurfaceMaterialMapping& output) noexcept;
    [[nodiscard]] const char* ToString(ImportedSurfaceMappingResult result) noexcept;

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

    struct ImportedSurfaceCanonicalIntermediates
    {
        resources::ResourceReference programSource;
        resources::ResourceReference shader;
        resources::ResourceReference depthProgramSource;
        resources::ResourceReference depthShader;
        resources::ResourceReference materialSource;
        resources::ResourceReference depthPipeline;
        resources::ResourceReference opaquePipeline;
        resources::ResourceReference transparentPipeline;

        [[nodiscard]] bool IsValid() const noexcept;
    };

    struct ImportedSurfaceMaterialContextDescription
    {
        // Renderer-owned trusted source bytes. The context copies them during
        // Initialize and can therefore outlive the caller's source storage.
        containers::ArraySpan<const u8> prefix;
        containers::ArraySpan<const u8> suffix;
        containers::ArraySpan<const u8> gpuSceneTypes;
        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        shader_tools::CompileSettings compileSettings;
        MaterialOfflineContract offline;
    };

    /// Reusable headless bootstrap for the production imported-surface domain.
    /// It owns copied trusted shader sources plus frozen frontend registries, and
    /// supplies the exact include provider required later by MaterialArtifactCompilers.
    class ImportedSurfaceMaterialContext final
    {
    public:
        struct Impl;

        ImportedSurfaceMaterialContext() noexcept = default;
        ~ImportedSurfaceMaterialContext();
        ImportedSurfaceMaterialContext(const ImportedSurfaceMaterialContext&) = delete;
        ImportedSurfaceMaterialContext& operator=(const ImportedSurfaceMaterialContext&) = delete;

        [[nodiscard]] bool Initialize(const ImportedSurfaceMaterialContextDescription& description) noexcept;
        void Reset() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] MaterialFrontendConfig GetFrontendConfig() const noexcept;
        [[nodiscard]] MaterialSlangDomain GetSlangDomain() const noexcept;
        [[nodiscard]] assets::TargetPlatform GetTarget() const noexcept;
        [[nodiscard]] const shader_tools::CompileSettings& GetCompileSettings() const noexcept;
        [[nodiscard]] const MaterialOfflineContract& GetOfflineContract() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetSourceProviderFingerprint() const noexcept;

        /// Installs this context's trusted include provider and matching offline
        /// contract into an artifact-compiler config. This context must outlive
        /// any MaterialArtifactCompilers initialized from the resulting config.
        [[nodiscard]] bool ConfigureArtifactCompiler(MaterialArtifactCompilerConfig& config) const noexcept;

    private:
        [[nodiscard]] static bool LoadShaderSource(const char* canonicalPath, shader_tools::LoadedSource& source,
                                                   void* userData) noexcept;
        Impl* m_impl = nullptr;
    };

    /// Deterministically derives private generated identities from the stable VMAT
    /// output identity plus the imported-surface contract/mapper versions.
    [[nodiscard]] bool DeriveImportedSurfaceCanonicalIntermediates(resources::ResourceReference materialOutput,
                                                                   ImportedSurfaceCanonicalIntermediates& output) noexcept;

    /// Completes imported-surface canonical setup around caller-owned renderer
    /// pipeline recipes. Recipes must be graphics recipes with no shader entries;
    /// the helper assigns the durable technique names, program variants, canonical
    /// input identities, compile target, frontend/domain state, and material values.
    [[nodiscard]] MaterialCanonicalResult BuildImportedSurfaceCanonicalInputs(
        const ImportedSurfaceMaterialContext& context, const ImportedSurfaceMaterialInput& material,
        resources::ResourceReference materialOutput, const pipelines::BuildDescription& depthPrepassRecipe,
        const pipelines::BuildDescription& opaqueRecipe, MaterialCanonicalBuildSet& build,
        ImportedSurfaceMappingWarning* warnings = nullptr, MaterialCanonicalDiagnostic* diagnostic = nullptr,
        containers::ArraySpan<const u8> buildSettings = {}, const MaterialCanonicalLimits& limits = {},
        const pipelines::BuildDescription* transparentRecipe = nullptr) noexcept;

    [[nodiscard]] const char* ToString(MaterialCanonicalResult result) noexcept;
} // namespace vanguard::material_tools
