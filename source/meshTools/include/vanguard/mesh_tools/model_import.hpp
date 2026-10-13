#pragma once

#include <vanguard/assets/asset_operations.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/mesh_tools/mesh_import.hpp>

namespace vanguard::mesh_tools
{
    constexpr u32 InvalidModelImportIndex = ~u32{0};

    enum class ModelImportResult : u8
    {
        Success,
        InvalidArgument,
        UnsupportedSource,
        SourceNotFound,
        SourceTooLarge,
        ImportFailure,
        NoGeometry,
        LimitExceeded,
        InvalidGeometry,
        MissingDependency,
        UnsupportedExternalBuffer,
        ConflictingGeometryBinding,
        AmbiguousBoneBinding,
        Cancelled
    };

    [[nodiscard]] const char* ToString(ModelImportResult result) noexcept;

    enum class ModelImportSourceKind : u8
    {
        Generic,
        Gltf2,
        Fbx
    };

    enum class ModelImportDependencyKind : u8
    {
        MainSource,
        StructuralCompanion,
        ExternalTexture,
        OtherDecoderAuxiliary
    };

    enum class ModelImportDiagnosticSeverity : u8
    {
        Info,
        Warning,
        Error
    };

    enum class ModelImportDiagnosticCode : u8
    {
        None,
        UnsupportedPrimitive,
        MissingExternalTexture,
        ExternalBufferForbidden,
        ConflictingGeometryBinding,
        AmbiguousBoneBinding,
        NonFiniteTransform,
        NonAffineTransform,
        SingularTransform,
        NegativeDeterminant,
        NonUniformScale,
        ShearTransform
    };

    enum ModelImportNodeFlags : u32
    {
        ModelImportNode_None = 0,
        ModelImportNode_DecoderSynthetic = 1u << 0,
        ModelImportNode_NonFiniteTransform = 1u << 1,
        ModelImportNode_NonAffineTransform = 1u << 2,
        ModelImportNode_SingularTransform = 1u << 3,
        ModelImportNode_NegativeDeterminant = 1u << 4,
        ModelImportNode_NonUniformScale = 1u << 5,
        ModelImportNode_ShearTransform = 1u << 6
    };

    struct ModelImportLimits
    {
        u64 maximumSourceBytes = 512ull * 1024ull * 1024ull;
        u64 maximumOwnedPayloadBytes = 2048ull * 1024ull * 1024ull;
        u32 maximumNodes = 1u << 20;
        u32 maximumNodeDepth = 1024;
        u32 maximumGeometryDefinitions = 65536;
        u32 maximumPrimitives = 65536;
        u32 maximumGeometryInstances = 1u << 20;
        u32 maximumMaterials = 65536;
        u32 maximumTextureReferences = 1u << 20;
        u32 maximumEmbeddedTextures = 65536;
        u64 maximumEmbeddedTextureBytes = 1024ull * 1024ull * 1024ull;
        u32 maximumDependencies = 4096;
        u32 maximumDiagnostics = 65536;
        u32 maximumVerticesPerPrimitive = 16777216;
        u32 maximumIndicesPerPrimitive = 50331648;
        u64 maximumVertices = 268435456ull;
        u64 maximumIndices = 805306368ull;
        u32 maximumBoneBindings = 1u << 20;
        u64 maximumInfluences = 1ull << 24;
    };

    using ModelImportProgressFunction = bool (*)(f32 progress, void* userData) noexcept;

    struct ModelImportOptions
    {
        bool generateNormals = true;
        bool generateTangents = true;
        bool flipUVs = false;
        f32 smoothingAngleDegrees = 80.0f;
        ModelImportProgressFunction progress = nullptr;
        void* progressUserData = nullptr;
    };

    struct ModelImportMatrix
    {
        f32 values[16]{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct ModelImportBoneWeight
    {
        u32 vertexIndex = 0;
        f32 weight = 0.0f;
    };

    struct ModelImportBoneBinding
    {
        ModelImportBoneBinding() noexcept : weights(memory::pools::Assets::GetInstance()) {}

        containers::String name;
        u32 nodeIndex = InvalidModelImportIndex;
        u32 armatureNodeIndex = InvalidModelImportIndex;
        ModelImportMatrix inverseBind;
        containers::DynamicArray<ModelImportBoneWeight> weights;
    };

    struct ModelImportPrimitive
    {
        ModelImportPrimitive() noexcept
            : vertexStreams(memory::pools::Assets::GetInstance()), indices(memory::pools::Assets::GetInstance()),
              boneBindings(memory::pools::Assets::GetInstance())
        {
        }

        u32 sourceMeshIndex = InvalidModelImportIndex;
        u32 sourcePartitionOrdinal = 0;
        u32 sourceMaterialIndex = InvalidModelImportIndex;
        containers::String displayName;
        meshes::SubmeshFlags flags = meshes::SubmeshFlags::CastsShadow;
        u32 vertexCount = 0;
        containers::DynamicArray<ImportedVertexStream> vertexStreams;
        containers::DynamicArray<u32> indices;
        containers::DynamicArray<ModelImportBoneBinding> boneBindings;
    };

    struct ModelImportGeometryDefinition
    {
        ModelImportGeometryDefinition() noexcept : primitiveIndices(memory::pools::Assets::GetInstance()) {}

        ModelImportSourceKind sourceKind = ModelImportSourceKind::Generic;
        u64 sourceGeometryKey = 0;
        u64 acceptedBindingKey = 0;
        u32 acceptedBindingFlags = 0;
        containers::String displayName;
        containers::DynamicArray<u32> primitiveIndices;
    };

    struct ModelImportGeometryInstance
    {
        u32 nodeIndex = InvalidModelImportIndex;
        u32 geometryDefinitionIndex = InvalidModelImportIndex;
        u32 occurrenceOrdinal = 0;
    };

    struct ModelImportNode
    {
        ModelImportNode() noexcept : geometryInstanceIndices(memory::pools::Assets::GetInstance()) {}

        u32 parentIndex = InvalidModelImportIndex;
        u32 siblingOrdinal = 0;
        u32 flags = ModelImportNode_None;
        containers::String sourceName;
        containers::String displayName;
        ModelImportMatrix localTransform;
        containers::DynamicArray<u32> geometryInstanceIndices;
    };

    struct ModelImportTextureReference
    {
        enum class SpecularTextureRole : u8
        {
            None,
            FactorAlpha,
            ColorRgb
        };

        ImportedTextureSemantic semantic = ImportedTextureSemantic::Unknown;
        ImportedTextureStorage storage = ImportedTextureStorage::ExternalSource;
        // Runtime inspection detail only. Persisted texture/output evidence continues to use
        // semantic + sourceBindingOrdinal, so old Specular correspondence remains stable.
        SpecularTextureRole specularTextureRole = SpecularTextureRole::None;
        u32 sourceBindingOrdinal = 0;
        u32 uvSet = 0;
        u32 embeddedTextureIndex = InvalidModelImportIndex;
        containers::String authoredPath;
        containers::String resolvedPath;
        bool resolved = false;
        bool missing = false;
    };

    enum class ModelImportAlphaMode : u8
    {
        Opaque,
        Mask,
        Blend
    };

    struct ModelImportMaterial
    {
        ModelImportMaterial() noexcept : textureReferenceIndices(memory::pools::Assets::GetInstance()) {}

        u32 sourceMaterialIndex = InvalidModelImportIndex;
        containers::String name;
        f32 baseColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
        f32 emissiveColor[4]{};
        f32 specularColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
        f32 specularFactor = 1.0f;
        f32 indexOfRefraction = 1.5f;
        f32 metallic = 0.0f;
        f32 roughness = 1.0f;
        f32 opacity = 1.0f;
        f32 occlusionStrength = 1.0f;
        ModelImportAlphaMode alphaMode = ModelImportAlphaMode::Opaque;
        f32 alphaCutoff = 0.5f;
        bool twoSided = false;
        containers::DynamicArray<u32> textureReferenceIndices;
    };

    struct ModelImportEmbeddedTexture
    {
        ModelImportEmbeddedTexture() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        u32 sourceTextureIndex = InvalidModelImportIndex;
        containers::String name;
        containers::String formatHint;
        u32 width = 0;
        u32 height = 0;
        bool compressed = true;
        containers::DynamicArray<u8> bytes;
    };

    struct ModelImportDependency
    {
        ModelImportDependencyKind kind = ModelImportDependencyKind::OtherDecoderAuxiliary;
        containers::String path;
        bool exists = false;
    };

    struct ModelImportDiagnostic
    {
        ModelImportDiagnosticSeverity severity = ModelImportDiagnosticSeverity::Info;
        ModelImportDiagnosticCode code = ModelImportDiagnosticCode::None;
        u32 nodeIndex = InvalidModelImportIndex;
        u32 geometryDefinitionIndex = InvalidModelImportIndex;
        u32 primitiveIndex = InvalidModelImportIndex;
        containers::String message;
    };

    struct ModelImportManifest
    {
        ModelImportManifest() noexcept
            : nodes(memory::pools::Assets::GetInstance()), geometryDefinitions(memory::pools::Assets::GetInstance()),
              geometryInstances(memory::pools::Assets::GetInstance()), primitives(memory::pools::Assets::GetInstance()),
              materials(memory::pools::Assets::GetInstance()), textureReferences(memory::pools::Assets::GetInstance()),
              embeddedTextures(memory::pools::Assets::GetInstance()), dependencies(memory::pools::Assets::GetInstance()),
              diagnostics(memory::pools::Assets::GetInstance())
        {
        }

        void Clear() noexcept;

        crypto::Digest256 sourceFingerprint;
        containers::String sourceName;
        containers::String formatHint;
        containers::String decoderName;
        containers::DynamicArray<ModelImportNode> nodes;
        containers::DynamicArray<ModelImportGeometryDefinition> geometryDefinitions;
        containers::DynamicArray<ModelImportGeometryInstance> geometryInstances;
        containers::DynamicArray<ModelImportPrimitive> primitives;
        containers::DynamicArray<ModelImportMaterial> materials;
        containers::DynamicArray<ModelImportTextureReference> textureReferences;
        containers::DynamicArray<ModelImportEmbeddedTexture> embeddedTextures;
        containers::DynamicArray<ModelImportDependency> dependencies;
        containers::DynamicArray<ModelImportDiagnostic> diagnostics;
    };

    struct ModelImportReport
    {
        containers::String diagnostic;
        u32 nodeCount = 0;
        u32 geometryDefinitionCount = 0;
        u32 geometryInstanceCount = 0;
        u32 primitiveCount = 0;
        u32 materialCount = 0;
        u32 textureReferenceCount = 0;
        u32 embeddedTextureCount = 0;
        u32 dependencyCount = 0;
        u32 boneBindingCount = 0;
        u64 vertexCount = 0;
        u64 indexCount = 0;
        u64 influenceCount = 0;
    };

    using ResolveModelImportMaterialFunction =
        bool (*)(u32 sourceMaterialIndex, resources::ResourceReference& material, void* userData) noexcept;

    struct ModelGeometryExtractionSettings
    {
        f32 uniformScale = 1.0f;
        SourceUpAxis sourceUpAxis = SourceUpAxis::Z;
        bool flipWinding = true;
        resources::ResourceReference defaultMaterial;
        resources::ResourceReference skeleton;
        ResolveModelImportMaterialFunction resolveMaterial = nullptr;
        void* resolveMaterialUserData = nullptr;
    };

    [[nodiscard]] ModelImportResult InspectModelFile(const filesystem::AbsolutePath& source, const ModelImportOptions& options,
                                                     const ModelImportLimits& limits, ModelImportManifest& output,
                                                     ModelImportReport* report = nullptr) noexcept;

    [[nodiscard]] ModelImportResult InspectModelMemory(const void* sourceData, usize sourceSize, containers::StringView formatHint,
                                                       containers::StringView sourceName, const ModelImportOptions& options,
                                                       const ModelImportLimits& limits, ModelImportManifest& output,
                                                       ModelImportReport* report = nullptr) noexcept;

    [[nodiscard]] ModelImportResult ExtractModelGeometry(const ModelImportManifest& manifest, u32 geometryDefinitionIndex,
                                                         const ModelGeometryExtractionSettings& settings, ImportedMesh& output,
                                                         ModelImportReport* report = nullptr) noexcept;

    [[nodiscard]] bool ConvertModelImportLocalTransform(const ModelImportMatrix& source,
                                                        const ModelGeometryExtractionSettings& settings,
                                                        ModelImportMatrix& output) noexcept;

    inline constexpr u32 ModelImportSettingsVersion = 1;
    inline constexpr u32 ModelImportMaterialMapperVersion = 2;

    enum class ModelImportSettingsResult : u8
    {
        Success,
        InvalidArgument,
        InvalidData,
        LimitExceeded,
        UnsupportedVersion
    };

    enum class ModelImportElementKind : u8
    {
        Geometry,
        Material,
        EmbeddedTexture,
        Composition,
        ExternalTextureDependency
    };

    enum class ModelImportMaterialPolicy : u8
    {
        ImportModelOwned,
        UseDefaultMaterial
    };

    enum class ModelImportEmbeddedTexturePolicy : u8
    {
        ImportModelOwned,
        Ignore
    };

    enum class ModelImportTextureResolutionKind : u8
    {
        Unresolved,
        ReuseManagedResource,
        AdoptProjectSource,
        IgnoreMissing
    };

    struct ModelImportSourceEvidence
    {
        ModelImportSourceKind sourceKind = ModelImportSourceKind::Generic;
        u64 nativeKey = 0;
        crypto::Digest256 structuralKey;
        crypto::Digest256 relationshipKey;
        containers::String sourceName;
        bool nativeKeyStable = false;
    };

    struct ModelImportOutputCorrespondence
    {
        ModelImportElementKind kind = ModelImportElementKind::Geometry;
        u64 persistentId = 0;
        assets::AssetOutput output;
        ModelImportSourceEvidence source;
    };

    struct ModelImportRetiredOutput
    {
        ModelImportElementKind kind = ModelImportElementKind::Geometry;
        u64 persistentId = 0;
        assets::AssetOutput output;
    };

    struct ModelImportMaterialSlotCorrespondence
    {
        u64 stableSlotId = 0;
        u64 geometryPersistentId = 0;
        u32 sourcePartitionOrdinal = 0;
        u64 materialPersistentId = 0;
        crypto::Digest256 structuralKey;
        crypto::Digest256 partitionKey;
    };

    struct ModelImportRetiredMaterialSlot
    {
        u64 stableSlotId = 0;
    };

    /// Stable identity for one authored source node inside the generated composition.
    /// sourceNodeIndex is only a locator for the exact sourceFingerprint captured by the
    /// settings; stableNodeId survives ordinary source reorder/rename when evidence remains
    /// globally unambiguous. parentStableNodeId is zero for a source root.
    struct ModelImportCompositionNodeCorrespondence
    {
        u64 stableNodeId = 0;
        u64 parentStableNodeId = 0;
        u32 sourceNodeIndex = InvalidModelImportIndex;
        ModelImportSourceEvidence source;
    };

    /// Stable component identity for one mesh instance under an authored source node.
    /// sourceInstanceIndex and sameGeometryOrdinal are locators for the exact source snapshot;
    /// stableComponentId is the durable ComponentDirectory identity. Keeping this record for
    /// single-mesh nodes lets that mesh retain identity if the authored node later gains peers;
    /// the single-mesh runtime representation still remains a direct StaticMeshComponent.
    struct ModelImportCompositionMeshCorrespondence
    {
        u64 stableComponentId = 0;
        u64 stableNodeId = 0;
        u64 geometryPersistentId = 0;
        u32 sourceInstanceIndex = InvalidModelImportIndex;
        u32 sameGeometryOrdinal = 0;
    };

    struct ModelImportTextureResolution
    {
        containers::String authoredPath;
        ModelImportTextureResolutionKind resolution = ModelImportTextureResolutionKind::Unresolved;
        resources::ResourceReference managedResource;
        resources::ResourcePath projectSourceIdentity;
    };

    struct ModelImportOwnedCookSettings
    {
        ModelImportOwnedCookSettings() noexcept : lodLevels(memory::pools::Assets::GetInstance()) {}

        bool optimizeVertexCache = true;
        bool optimizeOverdraw = true;
        bool optimizeVertexFetch = true;
        bool lockLodBorders = true;
        bool regularizeLodTriangles = false;
        f32 overdrawThreshold = 1.05f;
        MeshCookingProfileId meshCookingProfile = profiles::RuntimeStatic;
        LodAttributeWeights lodAttributeWeights;
        containers::DynamicArray<LodLevelSettings> lodLevels;
        u32 maximumSubmeshes = 65536;
        u32 maximumVertexStreamsPerSubmesh = 32;
        u32 maximumVerticesPerSubmesh = 16777216;
        u32 maximumIndicesPerSubmesh = 50331648;
        u32 maximumLodLevels = 8;
    };

    struct ModelImportSettings
    {
        ModelImportSettings() noexcept
            : outputs(memory::pools::Assets::GetInstance()), retiredOutputs(memory::pools::Assets::GetInstance()),
              materialSlots(memory::pools::Assets::GetInstance()), retiredMaterialSlots(memory::pools::Assets::GetInstance()),
              compositionNodes(memory::pools::Assets::GetInstance()), compositionMeshes(memory::pools::Assets::GetInstance()),
              textureResolutions(memory::pools::Assets::GetInstance())
        {
        }

        u64 nextOutputId = 1;
        u64 nextMaterialSlotId = 1;
        u64 nextCompositionNodeId = 1;
        // Exact authored-source snapshot that these persisted output/slot
        // correspondences were planned against. Candidate settings may leave
        // this empty; PlanModelImport stamps the accepted source fingerprint.
        crypto::Digest256 sourceFingerprint;
        MeshImportSettings mesh;
        ModelImportOwnedCookSettings cook;
        ModelImportMaterialPolicy materialPolicy = ModelImportMaterialPolicy::ImportModelOwned;
        ModelImportEmbeddedTexturePolicy embeddedTexturePolicy = ModelImportEmbeddedTexturePolicy::ImportModelOwned;
        u32 materialMapperVersion = ModelImportMaterialMapperVersion;
        bool includeComposition = false;
        containers::DynamicArray<ModelImportOutputCorrespondence> outputs;
        containers::DynamicArray<ModelImportRetiredOutput> retiredOutputs;
        containers::DynamicArray<ModelImportMaterialSlotCorrespondence> materialSlots;
        containers::DynamicArray<ModelImportRetiredMaterialSlot> retiredMaterialSlots;
        containers::DynamicArray<ModelImportCompositionNodeCorrespondence> compositionNodes;
        containers::DynamicArray<ModelImportCompositionMeshCorrespondence> compositionMeshes;
        containers::DynamicArray<ModelImportTextureResolution> textureResolutions;
    };

    [[nodiscard]] ModelImportSettingsResult ValidateModelImportSettings(const ModelImportSettings& settings) noexcept;
    [[nodiscard]] ModelImportSettingsResult EncodeModelImportSettings(const ModelImportSettings& settings,
                                                                      containers::DynamicArray<u8>& output) noexcept;
    [[nodiscard]] ModelImportSettingsResult DecodeModelImportSettings(u32 settingsVersion,
                                                                      containers::ArraySpan<const u8> bytes,
                                                                      ModelImportSettings& output) noexcept;

    enum class ModelImportReconcileState : u8
    {
        Preserved,
        Added,
        Removed,
        Ambiguous
    };

    enum class ModelImportPlanBlockerKind : u8
    {
        ExternalReference,
        ReferenceStateUnavailable,
        StableSlotReferenceUnavailable,
        MissingDependency
    };

    enum class ModelImportPlanResult : u8
    {
        Success,
        InvalidArgument,
        SettingsInvalid,
        MetadataMismatch,
        IdentityConflict,
        LimitExceeded
    };

    struct ModelImportOutputTypes
    {
        resources::ResourceTypeId mesh = resources::InvalidResourceTypeId;
        resources::ResourceTypeId material = resources::InvalidResourceTypeId;
        resources::ResourceTypeId texture = resources::InvalidResourceTypeId;
        resources::ResourceTypeId composition = resources::InvalidResourceTypeId;
        resources::ResourceTypeId skeleton = resources::InvalidResourceTypeId;
    };

    struct ModelImportPlanningLimits
    {
        u32 maximumOutputs = assets::MaximumAssetOutputs;
        u32 maximumReconciliationRecords = assets::MaximumAssetOutputs * 2u;
        u32 maximumAmbiguousCandidates = assets::MaximumAssetOutputs * 4u;
        u32 maximumBlockers = 65536;
        u32 maximumMaterialSlots = 65536;
        u32 maximumTextureResolutions = 65536;
        u32 maximumDependencyWitnesses = 65536;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumOutputs != 0u && maximumOutputs <= assets::MaximumAssetOutputs &&
                   maximumReconciliationRecords >= maximumOutputs && maximumAmbiguousCandidates != 0u &&
                   maximumBlockers != 0u && maximumMaterialSlots != 0u && maximumTextureResolutions != 0u &&
                   maximumDependencyWitnesses != 0u;
        }
    };

    struct ModelImportDependencyWitness
    {
        resources::ResourcePath sourceIdentity;
        filesystem::AbsolutePath physicalPath;
        assets::AssetId asset;
        resources::ResourceReference selectedResource;
        crypto::Digest256 sourceContentFingerprint;
        crypto::Digest256 metadataFingerprint;
        bool sourceExists = false;
        bool metadataExists = false;
        bool hasSourceContentFingerprint = false;
        bool hasMetadataFingerprint = false;
    };

    struct ModelImportPlanningContext
    {
        const assets::SourceDatabase* database = nullptr;
        const assets::AssetMetadata* persistedMetadata = nullptr;
        assets::AssetId parentAsset;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 importerVersion = 0;
        u64 acceptedStateGeneration = 0;
        filesystem::AbsolutePath sourceOrigin;
        filesystem::AbsolutePath sourceRoot;
        filesystem::AbsolutePath sourceDestination;
        resources::ResourcePath sourceIdentity;
        crypto::Digest256 persistedMetadataDigest;
        bool hasPersistedMetadataDigest = false;
        ModelImportOutputTypes outputTypes;
        ModelImportPlanningLimits limits;
        containers::ArraySpan<const ModelImportDependencyWitness> dependencyWitnesses;
    };

    struct ModelImportPlanWitnesses
    {
        crypto::Digest256 sourceFingerprint;
        crypto::Digest256 candidateSettingsFingerprint;
        crypto::Digest256 persistedMetadataFingerprint;
        assets::AssetId parentAsset;
        assets::SourceImporterId importer = assets::InvalidSourceImporterId;
        u32 importerVersion = 0;
        u32 settingsVersion = ModelImportSettingsVersion;
        u64 sourceDatabaseGeneration = 0;
        u64 acceptedStateGeneration = 0;
        filesystem::AbsolutePath sourceOrigin;
        filesystem::AbsolutePath sourceRoot;
        filesystem::AbsolutePath sourceDestination;
        resources::ResourcePath sourceIdentity;
        bool hasPersistedMetadataFingerprint = false;
        bool copySourceToDestination = false;
    };

    struct ModelImportPlannedOutput
    {
        ModelImportElementKind kind = ModelImportElementKind::Geometry;
        u64 persistentId = 0;
        assets::AssetOutput output;
        u32 sourceIndex = InvalidModelImportIndex;
        containers::String displayName;
    };

    struct ModelImportReconciliationRecord
    {
        ModelImportReconcileState state = ModelImportReconcileState::Added;
        ModelImportElementKind kind = ModelImportElementKind::Geometry;
        u64 persistentId = 0;
        assets::AssetOutput previousOutput;
        assets::AssetOutput proposedOutput;
        ModelImportSourceEvidence previousSource;
        ModelImportSourceEvidence currentSource;
        u32 sourceIndex = InvalidModelImportIndex;
        bool blocking = false;
    };

    struct ModelImportAmbiguousCandidate
    {
        u32 reconciliationIndex = InvalidModelImportIndex;
        u32 sourceIndex = InvalidModelImportIndex;
        ModelImportSourceEvidence source;
        containers::String displayName;
    };

    enum class ModelImportSlotReconcileState : u8
    {
        Preserved,
        Added,
        Removed
    };

    struct ModelImportMaterialSlotReconciliation
    {
        ModelImportSlotReconcileState state = ModelImportSlotReconcileState::Added;
        u64 stableSlotId = 0;
        u64 geometryPersistentId = 0;
        u32 sourcePartitionOrdinal = 0;
        u64 previousMaterialPersistentId = 0;
        u64 currentMaterialPersistentId = 0;
        crypto::Digest256 structuralKey;
        crypto::Digest256 partitionKey;
        bool blocking = false;
    };

    struct ModelImportPlannedTextureResolution
    {
        containers::String authoredPath;
        containers::String resolvedPath;
        ModelImportTextureResolutionKind resolution = ModelImportTextureResolutionKind::Unresolved;
        resources::ResourceReference managedResource;
        resources::ResourcePath projectSourceIdentity;
        bool missing = false;
        bool blocking = false;
    };

    struct ModelImportPlanBlocker
    {
        ModelImportPlanBlockerKind kind = ModelImportPlanBlockerKind::ExternalReference;
        ModelImportElementKind elementKind = ModelImportElementKind::Geometry;
        u64 persistentId = 0;
        u64 stableSlotId = 0;
        resources::ResourceReference target;
        assets::AssetId referencer;
        resources::ResourcePath referencerSourceIdentity;
        filesystem::AbsolutePath referencerPath;
        resources::DependencyKind referenceKind = resources::DependencyKind::Required;
        assets::AssetReferenceLocation referenceLocation = assets::AssetReferenceLocation::Primary;
        bool referenceInPlace = false;
        assets::SourceReferenceScanState referenceState = assets::SourceReferenceScanState::NotScanned;
    };

    struct ModelImportPlanReport
    {
        containers::String diagnostic;
        u32 preserved = 0;
        u32 added = 0;
        u32 removed = 0;
        u32 ambiguous = 0;
        u32 blockers = 0;
    };

    class ModelImportPlan final
    {
    public:
        ModelImportPlan() noexcept;
        ModelImportPlan(const ModelImportPlan&) = delete;
        ModelImportPlan& operator=(const ModelImportPlan&) = delete;
        ModelImportPlan(ModelImportPlan&&) noexcept = default;
        ModelImportPlan& operator=(ModelImportPlan&&) noexcept = default;

        [[nodiscard]] const assets::SourceImportDescription& GetImportDescription() const noexcept;
        [[nodiscard]] const ModelImportPlanWitnesses& GetWitnesses() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportPlannedOutput> GetOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportReconciliationRecord> GetReconciliation() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportAmbiguousCandidate> GetAmbiguousCandidates() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportMaterialSlotReconciliation> GetMaterialSlotReconciliation() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportPlannedTextureResolution> GetTextureResolutions() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportDependencyWitness> GetDependencyWitnesses() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ModelImportPlanBlocker> GetBlockers() const noexcept;
        [[nodiscard]] bool HasBlockingConflicts() const noexcept;

    private:
        assets::SourceImportDescription m_import;
        ModelImportPlanWitnesses m_witnesses;
        containers::DynamicArray<ModelImportPlannedOutput> m_outputs;
        containers::DynamicArray<ModelImportReconciliationRecord> m_reconciliation;
        containers::DynamicArray<ModelImportAmbiguousCandidate> m_ambiguousCandidates;
        containers::DynamicArray<ModelImportMaterialSlotReconciliation> m_materialSlotReconciliation;
        containers::DynamicArray<ModelImportPlannedTextureResolution> m_textureResolutions;
        containers::DynamicArray<ModelImportDependencyWitness> m_dependencyWitnesses;
        containers::DynamicArray<ModelImportPlanBlocker> m_blockers;

        friend struct ModelImportPlanBuilderAccess;
        friend ModelImportPlanResult PlanModelImport(const ModelImportManifest&, const ModelImportSettings&,
                                                     const ModelImportPlanningContext&, ModelImportPlan&,
                                                     ModelImportPlanReport*) noexcept;
    };

    [[nodiscard]] ModelImportPlanResult PlanModelImport(const ModelImportManifest& manifest,
                                                        const ModelImportSettings& candidateSettings,
                                                        const ModelImportPlanningContext& context,
                                                        ModelImportPlan& output,
                                                        ModelImportPlanReport* report = nullptr) noexcept;

    // Shared format-to-authoring bridge. The returned request borrows the plan's
    // immutable SourceImportDescription and caller-owned dependency storage for
    // the immediate ProjectAssetPipeline::CommitPlannedImport call.
    [[nodiscard]] bool BuildModelPlannedImportRequest(
        const ModelImportPlan& plan, containers::ArraySpan<const u8> sourceSnapshot,
        containers::DynamicArray<assets::PlannedAssetDependencyWitness>& dependencyStorage,
        assets::PlannedAssetImportRequest& output) noexcept;

    // P3 compound-source build routing. The model importer owns source decoding,
    // persistent-output correspondence and geometry cooking; material/texture
    // canonicalization is supplied by the owning tool modules so meshTools does
    // not duplicate their wire formats or create a dependency cycle.
    inline constexpr resources::ResourceTypeId ModelGeometryBuildInputResourceType = serialization::MakeFourCC('V', 'M', 'G', 'I');
    inline constexpr resources::ResourceTypeId ModelBuildToolResourceType = serialization::MakeFourCC('V', 'M', 'T', 'L');
    inline constexpr u32 ModelAssetCompilerVersion = 1;
    inline constexpr u32 ModelSourceImporterVersion = 2;

    struct ModelOutputResolutionCache;

    struct ModelOutputBuildRequestContext
    {
        const ModelImportManifest* manifest = nullptr;
        const ModelImportSettings* settings = nullptr;
        const ModelImportOutputCorrespondence* correspondence = nullptr;
        const assets::SourceRecord* source = nullptr;
        assets::SourceDataView data;
        resources::ResourceReference requestedOutput;
        u32 sourceIndex = InvalidModelImportIndex;
        const ModelOutputResolutionCache* resolutionCache = nullptr;
    };

    // Resolve one exact embedded-texture record from the inspected source
    // snapshot to its persisted model-owned child output. Cross-tool material
    // adapters use this instead of duplicating P1 correspondence/evidence rules.
    [[nodiscard]] bool ResolveModelEmbeddedTextureOutput(const ModelOutputBuildRequestContext& context,
                                                         u32 embeddedTextureIndex,
                                                         resources::ResourceReference& output) noexcept;
    [[nodiscard]] bool ResolveModelGeometryOutput(const ModelOutputBuildRequestContext& context,
                                                  u32 geometryDefinitionIndex,
                                                  resources::ResourceReference& output,
                                                  u64& persistentId) noexcept;
    [[nodiscard]] bool ResolveModelCompositionNode(const ModelOutputBuildRequestContext& context,
                                                   u32 sourceNodeIndex,
                                                   u64& stableNodeId,
                                                   u64& parentStableNodeId) noexcept;

    struct ModelImportRigidTransform
    {
        f32 translation[3]{};
        f32 rotation[4]{0.0f, 0.0f, 0.0f, 1.0f};
    };

    [[nodiscard]] bool ConvertModelImportRigidLocalTransform(const ModelImportNode& node,
                                                             const ModelGeometryExtractionSettings& settings,
                                                             ModelImportRigidTransform& output) noexcept;

    // Implementations must deep-own the returned request in `request`. The
    // router copies/views it only for the current CreateSourceBuildRequest call.
    using ComposeModelOutputBuildRequestFunction =
        bool (*)(const ModelOutputBuildRequestContext& context, assets::OwnedBuildRequest& request, void* userData) noexcept;

    struct ModelAssetCompilerConfig
    {
        ModelImportLimits inspectionLimits;
        ModelImportOutputTypes outputTypes;
        ComposeModelOutputBuildRequestFunction composeMaterial = nullptr;
        void* composeMaterialUserData = nullptr;
        ComposeModelOutputBuildRequestFunction composeEmbeddedTexture = nullptr;
        void* composeEmbeddedTextureUserData = nullptr;
        ComposeModelOutputBuildRequestFunction composeComposition = nullptr;
        void* composeCompositionUserData = nullptr;
        u32 maximumCachedInspections = 8;
        u64 maximumMeshArtifactBytes = 2ull * 1024ull * 1024ull * 1024ull;
        u64 estimatedMeshCompilerTransientBytes = 4ull * 1024ull * 1024ull * 1024ull;
    };

    struct ModelAssetCompilerStats
    {
        u64 inspections = 0;
        u64 inspectionCacheHits = 0;
        u32 cachedInspections = 0;
    };

    class ModelAssetCompiler final
    {
    public:
        struct Impl;

        ModelAssetCompiler() noexcept = default;
        ~ModelAssetCompiler();

        ModelAssetCompiler(const ModelAssetCompiler&) = delete;
        ModelAssetCompiler& operator=(const ModelAssetCompiler&) = delete;

        [[nodiscard]] bool Initialize(const ModelAssetCompilerConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] assets::CompilerDescriptor GetMeshDescriptor() noexcept;
        [[nodiscard]] assets::SourceImporterDescriptor GetImporterDescriptor() noexcept;
        [[nodiscard]] assets::Result Register(assets::BuildSystem& buildSystem) noexcept;
        [[nodiscard]] assets::Result Unregister() noexcept;
        [[nodiscard]] ModelAssetCompilerStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::mesh_tools
