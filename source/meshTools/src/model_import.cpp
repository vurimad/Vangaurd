#include <vanguard/mesh_tools/model_import.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/serialization/serialization.hpp>

#include "../../../external/assimp/vanguard/assimp_vanguard_io.hpp"

#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/vanguard_model_provenance.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vanguard::mesh_tools
{
    struct ModelImportPlanBuilderAccess final
    {
        [[nodiscard]] static containers::DynamicArray<ModelImportPlannedOutput>& Outputs(ModelImportPlan& plan) noexcept
        {
            return plan.m_outputs;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportReconciliationRecord>& Reconciliation(ModelImportPlan& plan) noexcept
        {
            return plan.m_reconciliation;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportAmbiguousCandidate>& AmbiguousCandidates(ModelImportPlan& plan) noexcept
        {
            return plan.m_ambiguousCandidates;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportMaterialSlotReconciliation>& MaterialSlots(ModelImportPlan& plan) noexcept
        {
            return plan.m_materialSlotReconciliation;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportPlannedTextureResolution>& TextureResolutions(ModelImportPlan& plan) noexcept
        {
            return plan.m_textureResolutions;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportDependencyWitness>& DependencyWitnesses(ModelImportPlan& plan) noexcept
        {
            return plan.m_dependencyWitnesses;
        }
        [[nodiscard]] static containers::DynamicArray<ModelImportPlanBlocker>& Blockers(ModelImportPlan& plan) noexcept
        {
            return plan.m_blockers;
        }
        [[nodiscard]] static assets::SourceImportDescription& Import(ModelImportPlan& plan) noexcept
        {
            return plan.m_import;
        }
        [[nodiscard]] static ModelImportPlanWitnesses& Witnesses(ModelImportPlan& plan) noexcept
        {
            return plan.m_witnesses;
        }
    };

    struct ModelOutputResolutionCache final
    {
        struct OutputLookup final
        {
            resources::ResourcePath path;
            resources::ResourceTypeId type = resources::InvalidResourceTypeId;
            u32 outputIndex = InvalidModelImportIndex;
        };

        ModelOutputResolutionCache() noexcept
            : sourceIndexByOutput(memory::pools::Tools::GetInstance()),
              geometryOutputIndexBySource(memory::pools::Tools::GetInstance()),
              materialOutputIndexBySource(memory::pools::Tools::GetInstance()),
              embeddedTextureOutputIndexBySource(memory::pools::Tools::GetInstance()),
              outputLookup(memory::pools::Tools::GetInstance())
        {
        }

        containers::DynamicArray<u32> sourceIndexByOutput;
        containers::DynamicArray<u32> geometryOutputIndexBySource;
        containers::DynamicArray<u32> materialOutputIndexBySource;
        containers::DynamicArray<u32> embeddedTextureOutputIndexBySource;
        containers::DynamicArray<OutputLookup> outputLookup;
    };
} // namespace vanguard::mesh_tools

namespace
{
    namespace assets = vanguard::assets;
    namespace containers = vanguard::containers;
    namespace crypto = vanguard::crypto;
    namespace filesystem = vanguard::filesystem;
    namespace memory = vanguard::memory;
    namespace meshes = vanguard::meshes;
    namespace resources = vanguard::resources;
    namespace serialization = vanguard::serialization;
    namespace tools = vanguard::mesh_tools;
    using vanguard::f32;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::u8;
    using vanguard::usize;

    constexpr u32 MaximumModelSettingsStringBytes = 4096;

    struct PrimitiveProvenance
    {
        tools::ModelImportSourceKind sourceKind = tools::ModelImportSourceKind::Generic;
        u64 sourceGeometryKey = 0;
        u32 sourcePartitionOrdinal = 0;
        u32 aiMeshIndex = 0;
    };

    struct GeometryUseProvenance
    {
        tools::ModelImportSourceKind sourceKind = tools::ModelImportSourceKind::Generic;
        u64 sourceGeometryKey = 0;
        u64 bindingKey = 0;
        u32 flags = 0;
    };

    struct InspectionContext
    {
        InspectionContext(const tools::ModelImportOptions& optionsValue, const tools::ModelImportLimits& limitsValue,
                          tools::ModelImportManifest& outputValue, tools::ModelImportReport* reportValue) noexcept
            : options(optionsValue), limits(limitsValue), output(outputValue), report(reportValue),
              primitiveProvenance(memory::pools::Assets::GetInstance()),
              geometryUses(memory::pools::Assets::GetInstance()),
              nodePointers(memory::pools::Assets::GetInstance())
        {
        }

        const tools::ModelImportOptions& options;
        const tools::ModelImportLimits& limits;
        tools::ModelImportManifest& output;
        tools::ModelImportReport* report = nullptr;
        filesystem::AbsolutePath sourceDirectory;
        containers::String formatHint;
        bool resolveDependencies = false;
        bool cancelled = false;
        bool structuralDependencyRejected = false;
        tools::ModelImportResult callbackFailure = tools::ModelImportResult::Success;
        containers::DynamicArray<PrimitiveProvenance> primitiveProvenance;
        containers::DynamicArray<GeometryUseProvenance> geometryUses;
        containers::DynamicArray<const aiNode*> nodePointers;
        u64 totalVertices = 0;
        u64 totalIndices = 0;
        u64 totalEmbeddedBytes = 0;
        u64 totalInfluences = 0;
        u64 ownedPayloadBytes = 0;
        u32 totalBoneBindings = 0;
    };

    tools::ModelImportSourceKind ConvertSourceKind(const aiVanguardModelSourceKind kind) noexcept
    {
        switch (kind)
        {
        case aiVanguardModelSourceKind::Gltf2:
            return tools::ModelImportSourceKind::Gltf2;
        case aiVanguardModelSourceKind::Fbx:
            return tools::ModelImportSourceKind::Fbx;
        default:
            return tools::ModelImportSourceKind::Generic;
        }
    }

    void SetDiagnostic(tools::ModelImportReport* const report, const char* const text)
    {
        if (report != nullptr)
        {
            report->diagnostic = text;
        }
    }

    void SetDiagnostic(tools::ModelImportReport* const report, const containers::String& text)
    {
        if (report != nullptr)
        {
            report->diagnostic = text;
        }
    }

    void AddDiagnostic(InspectionContext& context, const tools::ModelImportDiagnosticSeverity severity,
                       const tools::ModelImportDiagnosticCode code, const char* const message,
                       const u32 nodeIndex = tools::InvalidModelImportIndex,
                       const u32 geometryDefinitionIndex = tools::InvalidModelImportIndex,
                       const u32 primitiveIndex = tools::InvalidModelImportIndex)
    {
        if (context.output.diagnostics.Size() >= context.limits.maximumDiagnostics)
        {
            context.callbackFailure = tools::ModelImportResult::LimitExceeded;
            return;
        }
        tools::ModelImportDiagnostic& diagnostic = context.output.diagnostics.EmplaceBack();
        diagnostic.severity = severity;
        diagnostic.code = code;
        diagnostic.nodeIndex = nodeIndex;
        diagnostic.geometryDefinitionIndex = geometryDefinitionIndex;
        diagnostic.primitiveIndex = primitiveIndex;
        diagnostic.message = message;
    }

    bool AddUniqueDependency(InspectionContext& context, const tools::ModelImportDependencyKind kind, const char* const path,
                             const bool exists)
    {
        for (tools::ModelImportDependency& dependency : context.output.dependencies)
        {
            if (dependency.kind == kind && dependency.path == path)
            {
                dependency.exists = dependency.exists || exists;
                return true;
            }
        }
        if (context.output.dependencies.Size() >= context.limits.maximumDependencies)
        {
            context.callbackFailure = tools::ModelImportResult::LimitExceeded;
            return false;
        }
        tools::ModelImportDependency& dependency = context.output.dependencies.EmplaceBack();
        dependency.kind = kind;
        dependency.path = path;
        dependency.exists = exists;
        return true;
    }

    bool RecordPrimitiveProvenance(void* const userData, const aiVanguardPrimitiveProvenance& value) noexcept
    {
        InspectionContext& context = *static_cast<InspectionContext*>(userData);
        if (context.primitiveProvenance.Size() >= context.limits.maximumPrimitives)
        {
            context.callbackFailure = tools::ModelImportResult::LimitExceeded;
            return false;
        }
        PrimitiveProvenance& record = context.primitiveProvenance.EmplaceBack();
        record.sourceKind = ConvertSourceKind(value.sourceKind);
        record.sourceGeometryKey = value.sourceGeometryKey;
        record.sourcePartitionOrdinal = value.sourcePartitionOrdinal;
        record.aiMeshIndex = value.aiMeshIndex;
        return true;
    }

    bool RecordGeometryUseProvenance(void* const userData, const aiVanguardGeometryUseProvenance& value) noexcept
    {
        InspectionContext& context = *static_cast<InspectionContext*>(userData);
        const tools::ModelImportSourceKind sourceKind = ConvertSourceKind(value.sourceKind);
        for (const GeometryUseProvenance& accepted : context.geometryUses)
        {
            if (accepted.sourceKind == sourceKind && accepted.sourceGeometryKey == value.sourceGeometryKey)
            {
                if (accepted.bindingKey != value.bindingKey || accepted.flags != value.flags)
                {
                    context.callbackFailure = tools::ModelImportResult::ConflictingGeometryBinding;
                    return false;
                }
                return true;
            }
        }
        if (context.geometryUses.Size() >= context.limits.maximumGeometryInstances)
        {
            context.callbackFailure = tools::ModelImportResult::LimitExceeded;
            return false;
        }
        GeometryUseProvenance& record = context.geometryUses.EmplaceBack();
        record.sourceKind = sourceKind;
        record.sourceGeometryKey = value.sourceGeometryKey;
        record.bindingKey = value.bindingKey;
        record.flags = value.flags;
        return true;
    }

    bool RecordStructuralDependency(void* const userData, const aiVanguardModelSourceKind sourceKind, const char* const path) noexcept
    {
        InspectionContext& context = *static_cast<InspectionContext*>(userData);
        if (sourceKind == aiVanguardModelSourceKind::Gltf2)
        {
            context.structuralDependencyRejected = true;
            context.callbackFailure = tools::ModelImportResult::UnsupportedExternalBuffer;
            return false;
        }
        return AddUniqueDependency(context, tools::ModelImportDependencyKind::StructuralCompanion, path, true);
    }

    void RecordOpenedFile(const char* const path, void* const userData) noexcept
    {
        InspectionContext& context = *static_cast<InspectionContext*>(userData);
        static_cast<void>(AddUniqueDependency(context, tools::ModelImportDependencyKind::OtherDecoderAuxiliary, path, true));
    }

    bool ForwardProgress(const float percentage, void* const userData) noexcept
    {
        InspectionContext& context = *static_cast<InspectionContext*>(userData);
        if (context.options.progress == nullptr)
        {
            return true;
        }
        const f32 clamped = percentage < 0.0f ? 0.0f : (percentage > 1.0f ? 1.0f : percentage);
        if (!context.options.progress(clamped, context.options.progressUserData))
        {
            context.cancelled = true;
            return false;
        }
        return true;
    }

    bool ValidateOptions(const tools::ModelImportOptions& options, const tools::ModelImportLimits& limits) noexcept
    {
        return std::isfinite(options.smoothingAngleDegrees) && options.smoothingAngleDegrees > 0.0f &&
               options.smoothingAngleDegrees <= 175.0f && limits.maximumSourceBytes != 0 && limits.maximumNodes != 0 &&
               limits.maximumNodeDepth != 0 && limits.maximumGeometryDefinitions != 0 && limits.maximumPrimitives != 0 &&
               limits.maximumGeometryInstances != 0 && limits.maximumMaterials != 0 && limits.maximumTextureReferences != 0 &&
               limits.maximumEmbeddedTextures != 0 && limits.maximumEmbeddedTextureBytes != 0 && limits.maximumDependencies != 0 &&
               limits.maximumOwnedPayloadBytes != 0 && limits.maximumDiagnostics != 0 &&
               limits.maximumVerticesPerPrimitive != 0 && limits.maximumIndicesPerPrimitive >= 3 && limits.maximumVertices != 0 &&
               limits.maximumIndices != 0 && limits.maximumBoneBindings != 0 && limits.maximumInfluences != 0;
    }

    unsigned int ModelImportFlags(const tools::ModelImportOptions& options) noexcept
    {
        unsigned int flags = static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                       aiProcess_ValidateDataStructure | aiProcess_FindInvalidData |
                                                       aiProcess_FindDegenerates | aiProcess_GenBoundingBoxes);
        if (options.generateNormals)
        {
            flags |= aiProcess_GenSmoothNormals;
        }
        if (options.generateTangents)
        {
            flags |= aiProcess_CalcTangentSpace;
        }
        if (options.flipUVs)
        {
            flags |= aiProcess_FlipUVs;
        }
        return flags;
    }

    void ConfigureImporter(Assimp::Importer& importer, InspectionContext& context, aiVanguardModelProvenanceSink& provenance)
    {
        importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, context.options.smoothingAngleDegrees);
        importer.SetPropertyPointer(AI_CONFIG_VANGUARD_MODEL_PROVENANCE, &provenance);
        if (context.options.progress != nullptr)
        {
            vanguard_assimp::InstallProgressHandler(importer, ForwardProgress, &context);
        }
    }

    tools::ModelImportMatrix CopyMatrix(const aiMatrix4x4& value) noexcept
    {
        tools::ModelImportMatrix result;
        const f32 values[16] = {
            value.a1, value.a2, value.a3, value.a4,
            value.b1, value.b2, value.b3, value.b4,
            value.c1, value.c2, value.c3, value.c4,
            value.d1, value.d2, value.d3, value.d4};
        std::memcpy(result.values, values, sizeof(values));
        return result;
    }

    bool IsFiniteMatrix(const aiMatrix4x4& matrix) noexcept
    {
        const ai_real values[16] = {
            matrix.a1, matrix.a2, matrix.a3, matrix.a4,
            matrix.b1, matrix.b2, matrix.b3, matrix.b4,
            matrix.c1, matrix.c2, matrix.c3, matrix.c4,
            matrix.d1, matrix.d2, matrix.d3, matrix.d4};
        for (const ai_real value : values)
        {
            if (!std::isfinite(static_cast<f32>(value)))
            {
                return false;
            }
        }
        return true;
    }

    u32 ClassifyTransform(const aiMatrix4x4& matrix) noexcept
    {
        if (!IsFiniteMatrix(matrix))
        {
            return tools::ModelImportNode_NonFiniteTransform;
        }

        u32 flags = tools::ModelImportNode_None;
        constexpr f32 Epsilon = 1.0e-5f;
        if (std::fabs(matrix.d1) > Epsilon || std::fabs(matrix.d2) > Epsilon ||
            std::fabs(matrix.d3) > Epsilon || std::fabs(matrix.d4 - 1.0f) > Epsilon)
        {
            flags |= tools::ModelImportNode_NonAffineTransform;
        }

        const f32 determinant =
            matrix.a1 * (matrix.b2 * matrix.c3 - matrix.b3 * matrix.c2) -
            matrix.a2 * (matrix.b1 * matrix.c3 - matrix.b3 * matrix.c1) +
            matrix.a3 * (matrix.b1 * matrix.c2 - matrix.b2 * matrix.c1);
        if (std::fabs(determinant) <= 1.0e-8f)
        {
            flags |= tools::ModelImportNode_SingularTransform;
        }
        if (determinant < 0.0f)
        {
            flags |= tools::ModelImportNode_NegativeDeterminant;
        }

        const f32 sx = std::sqrt(matrix.a1 * matrix.a1 + matrix.b1 * matrix.b1 + matrix.c1 * matrix.c1);
        const f32 sy = std::sqrt(matrix.a2 * matrix.a2 + matrix.b2 * matrix.b2 + matrix.c2 * matrix.c2);
        const f32 sz = std::sqrt(matrix.a3 * matrix.a3 + matrix.b3 * matrix.b3 + matrix.c3 * matrix.c3);
        const f32 maxScale = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
        const f32 minScale = sx < sy ? (sx < sz ? sx : sz) : (sy < sz ? sy : sz);
        if (maxScale > Epsilon && maxScale - minScale > Epsilon * (1.0f + maxScale))
        {
            flags |= tools::ModelImportNode_NonUniformScale;
        }
        if (sx > Epsilon && sy > Epsilon && sz > Epsilon)
        {
            const f32 xy = (matrix.a1 * matrix.a2 + matrix.b1 * matrix.b2 + matrix.c1 * matrix.c2) / (sx * sy);
            const f32 xz = (matrix.a1 * matrix.a3 + matrix.b1 * matrix.b3 + matrix.c1 * matrix.c3) / (sx * sz);
            const f32 yz = (matrix.a2 * matrix.a3 + matrix.b2 * matrix.b3 + matrix.c2 * matrix.c3) / (sy * sz);
            if (std::fabs(xy) > Epsilon || std::fabs(xz) > Epsilon || std::fabs(yz) > Epsilon)
            {
                flags |= tools::ModelImportNode_ShearTransform;
            }
        }
        return flags;
    }

    u32 FindNodeIndex(const InspectionContext& context, const aiNode* const node) noexcept
    {
        if (node == nullptr)
        {
            return tools::InvalidModelImportIndex;
        }
        for (u32 index = 0; index < context.nodePointers.Size(); ++index)
        {
            if (context.nodePointers[index] == node)
            {
                return index;
            }
        }
        return tools::InvalidModelImportIndex;
    }

    containers::String SafeName(const aiString& value, const char* const prefix, const u32 index)
    {
        return value.length != 0 ? containers::String(value.C_Str()) : containers::String::Printf("%s_%u", prefix, index);
    }

    tools::ImportedVertexStream& AddFloatStream(tools::ModelImportPrimitive& primitive, const meshes::VertexSemantic semantic,
                                                const u8 semanticIndex, const meshes::VertexFormat format, const u32 stride)
    {
        tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
        stream.semantic = semantic;
        stream.semanticIndex = semanticIndex;
        stream.format = format;
        stream.stride = stride * sizeof(f32);
        stream.floatValues.Reserve(primitive.vertexCount * stride);
        return stream;
    }

    void Append3(tools::ImportedVertexStream& stream, const aiVector3D& value)
    {
        stream.floatValues.PushBack(value.x);
        stream.floatValues.PushBack(value.y);
        stream.floatValues.PushBack(value.z);
    }

    void Append4(tools::ImportedVertexStream& stream, const f32 x, const f32 y, const f32 z, const f32 w)
    {
        stream.floatValues.PushBack(x);
        stream.floatValues.PushBack(y);
        stream.floatValues.PushBack(z);
        stream.floatValues.PushBack(w);
    }

    tools::ImportedTextureSemantic TextureSemantic(const aiMaterial& material, const aiTextureType type) noexcept
    {
        switch (type)
        {
        case aiTextureType_BASE_COLOR:
        case aiTextureType_DIFFUSE:
            return tools::ImportedTextureSemantic::BaseColor;
        case aiTextureType_NORMALS:
        case aiTextureType_HEIGHT:
            return tools::ImportedTextureSemantic::Normal;
        case aiTextureType_METALNESS:
            return tools::ImportedTextureSemantic::Metalness;
        case aiTextureType_DIFFUSE_ROUGHNESS:
            // Assimp uses DIFFUSE_ROUGHNESS for two distinct contracts. glTF publishes its
            // packed B=metallic/G=roughness texture under the dedicated glTF key as well as
            // METALNESS and DIFFUSE_ROUGHNESS, while FBX uses DIFFUSE_ROUGHNESS for a
            // roughness-only map. The dedicated property is the semantic witness; format or
            // path guessing would silently corrupt ordinary FBX metal/roughness pairs.
            return material.GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) != 0
                       ? tools::ImportedTextureSemantic::MetallicRoughness
                       : tools::ImportedTextureSemantic::Roughness;
        case aiTextureType_SPECULAR:
            return tools::ImportedTextureSemantic::Specular;
        case aiTextureType_EMISSIVE:
            return tools::ImportedTextureSemantic::Emissive;
        case aiTextureType_LIGHTMAP:
            return tools::ImportedTextureSemantic::Occlusion;
        case aiTextureType_OPACITY:
            return tools::ImportedTextureSemantic::Opacity;
        default:
            return tools::ImportedTextureSemantic::Unknown;
        }
    }
} // namespace

namespace
{
    [[nodiscard]] bool SameSourceEvidence(const tools::ModelImportSourceEvidence&,
                                          const tools::ModelImportSourceEvidence&) noexcept;
    [[nodiscard]] bool BuildSlotStructuralKey(const tools::ModelImportPrimitive&, crypto::Digest256&) noexcept;
    [[nodiscard]] bool BuildSlotPartitionKey(const tools::ModelImportPrimitive&, crypto::Digest256&) noexcept;

    constexpr u32 ModelGeometryRouteMagic = vanguard::serialization::MakeFourCC('V', 'M', 'G', 'R');
    constexpr u16 ModelGeometryRouteVersion = 1;
    constexpr char ModelImporterName[] = "vanguard.model.importer";
    constexpr char ModelMeshCompilerName[] = "vanguard.model.mesh";
    constexpr char ModelMeshToolPath[] = "tools/model_mesh_compiler";

    struct ModelGeometryRoute
    {
        u64 persistentId = 0;
        containers::String formatHint;
        containers::String sourceName;
    };

    [[nodiscard]] bool EncodeModelGeometryRoute(const u64 persistentId, const containers::StringView formatHint,
                                                const containers::StringView sourceName,
                                                containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (persistentId == 0u || formatHint.Empty() || sourceName.Empty() ||
            formatHint.Length() > MaximumModelSettingsStringBytes || sourceName.Length() > MaximumModelSettingsStringBytes)
            return false;
        const u32 total = 4u + 2u + 2u + 8u + 4u + formatHint.Length() + 4u + sourceName.Length();
        bytes.Resize(total);
        if (bytes.Size() != total)
            return false;
        u32 offset = 0u;
        const auto write = [&bytes, &offset](const void* const data, const u32 size) noexcept
        {
            if (data == nullptr || offset > bytes.Size() || size > bytes.Size() - offset)
                return false;
            std::memcpy(bytes.TypedData() + offset, data, size);
            offset += size;
            return true;
        };
        const auto writeU16 = [&write](const u16 value) noexcept
        {
            const u8 encoded[2]{static_cast<u8>(value), static_cast<u8>(value >> 8u)};
            return write(encoded, 2u);
        };
        const auto writeU32 = [&write](const u32 value) noexcept
        {
            const u8 encoded[4]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
            return write(encoded, 4u);
        };
        const auto writeU64 = [&writeU32](const u64 value) noexcept
        {
            return writeU32(static_cast<u32>(value)) && writeU32(static_cast<u32>(value >> 32u));
        };
        return writeU32(ModelGeometryRouteMagic) && writeU16(ModelGeometryRouteVersion) && writeU16(0u) && writeU64(persistentId) &&
               writeU32(formatHint.Length()) && write(formatHint.Data(), formatHint.Length()) && writeU32(sourceName.Length()) &&
               write(sourceName.Data(), sourceName.Length()) && offset == bytes.Size();
    }

    [[nodiscard]] bool DecodeModelGeometryRoute(const containers::ArraySpan<const u8> bytes, ModelGeometryRoute& route) noexcept
    {
        ModelGeometryRoute staged;
        u32 offset = 0u;
        const auto read = [&bytes, &offset](void* const data, const u32 size) noexcept
        {
            if (data == nullptr || offset > bytes.Size() || size > bytes.Size() - offset)
                return false;
            std::memcpy(data, bytes.Data() + offset, size);
            offset += size;
            return true;
        };
        const auto readU16 = [&read](u16& value) noexcept
        {
            u8 encoded[2]{};
            if (!read(encoded, 2u))
                return false;
            value = static_cast<u16>(encoded[0]) | static_cast<u16>(static_cast<u16>(encoded[1]) << 8u);
            return true;
        };
        const auto readU32 = [&read](u32& value) noexcept
        {
            u8 encoded[4]{};
            if (!read(encoded, 4u))
                return false;
            value = static_cast<u32>(encoded[0]) | (static_cast<u32>(encoded[1]) << 8u) |
                    (static_cast<u32>(encoded[2]) << 16u) | (static_cast<u32>(encoded[3]) << 24u);
            return true;
        };
        const auto readU64 = [&readU32](u64& value) noexcept
        {
            u32 low = 0u;
            u32 high = 0u;
            if (!readU32(low) || !readU32(high))
                return false;
            value = static_cast<u64>(low) | (static_cast<u64>(high) << 32u);
            return true;
        };
        u32 magic = 0u;
        u16 version = 0u;
        u16 reserved = 0u;
        u32 formatLength = 0u;
        u32 sourceNameLength = 0u;
        if (!readU32(magic) || !readU16(version) || !readU16(reserved) || magic != ModelGeometryRouteMagic ||
            version != ModelGeometryRouteVersion || reserved != 0u || !readU64(staged.persistentId) || staged.persistentId == 0u ||
            !readU32(formatLength) || formatLength == 0u || formatLength > MaximumModelSettingsStringBytes ||
            offset > bytes.Size() || formatLength > bytes.Size() - offset)
            return false;
        staged.formatHint = containers::String(reinterpret_cast<const char*>(bytes.Data() + offset), formatLength);
        offset += formatLength;
        if (!readU32(sourceNameLength) || sourceNameLength == 0u || sourceNameLength > MaximumModelSettingsStringBytes ||
            offset > bytes.Size() || sourceNameLength > bytes.Size() - offset)
            return false;
        staged.sourceName = containers::String(reinterpret_cast<const char*>(bytes.Data() + offset), sourceNameLength);
        offset += sourceNameLength;
        if (offset != bytes.Size())
            return false;
        route = static_cast<ModelGeometryRoute&&>(staged);
        return true;
    }

    [[nodiscard]] bool DecodeModelMetadata(const containers::ArraySpan<const u8> metadataBytes,
                                           assets::AssetMetadata& metadata) noexcept
    {
        if (metadataBytes.Empty() || metadataBytes.Data() == nullptr)
            return false;
        const containers::StringView text(reinterpret_cast<const char*>(metadataBytes.Data()), metadataBytes.Size());
        return assets::ParseMetadata(text, metadata) == assets::MetadataResult::Success;
    }

    [[nodiscard]] tools::ModelImportOptions InspectionOptions(const tools::ModelImportSettings& settings) noexcept
    {
        tools::ModelImportOptions options;
        options.generateNormals = settings.mesh.generateNormals;
        options.generateTangents = settings.mesh.generateTangents;
        options.flipUVs = settings.mesh.flipUVs;
        options.smoothingAngleDegrees = settings.mesh.smoothingAngleDegrees;
        return options;
    }

    [[nodiscard]] tools::CookSettings CookSettingsView(const tools::ModelImportOwnedCookSettings& owned) noexcept
    {
        tools::CookSettings cook;
        cook.optimizeVertexCache = owned.optimizeVertexCache;
        cook.optimizeOverdraw = owned.optimizeOverdraw;
        cook.optimizeVertexFetch = owned.optimizeVertexFetch;
        cook.lockLodBorders = owned.lockLodBorders;
        cook.regularizeLodTriangles = owned.regularizeLodTriangles;
        cook.overdrawThreshold = owned.overdrawThreshold;
        cook.meshCookingProfile = owned.meshCookingProfile;
        cook.lodAttributeWeights = owned.lodAttributeWeights;
        cook.lodLevels = {owned.lodLevels.TypedData(), owned.lodLevels.Size()};
        cook.maximumSubmeshes = owned.maximumSubmeshes;
        cook.maximumVertexStreamsPerSubmesh = owned.maximumVertexStreamsPerSubmesh;
        cook.maximumVerticesPerSubmesh = owned.maximumVerticesPerSubmesh;
        cook.maximumIndicesPerSubmesh = owned.maximumIndicesPerSubmesh;
        cook.maximumLodLevels = owned.maximumLodLevels;
        return cook;
    }

    [[nodiscard]] const tools::ModelImportOutputCorrespondence* FindOutputCorrespondence(
        const tools::ModelImportSettings& settings, const tools::ModelOutputResolutionCache& resolutionCache,
        const resources::ResourceReference output, u32& outputIndex) noexcept
    {
        outputIndex = tools::InvalidModelImportIndex;
        const auto& lookup = resolutionCache.outputLookup;
        const tools::ModelOutputResolutionCache::OutputLookup expected{output.GetPath(), output.ExpectedType(), tools::InvalidModelImportIndex};
        const tools::ModelOutputResolutionCache::OutputLookup* const begin = lookup.TypedData();
        const tools::ModelOutputResolutionCache::OutputLookup* const end = begin + lookup.Size();
        const tools::ModelOutputResolutionCache::OutputLookup* const found = std::lower_bound(
            begin, end, expected,
            [](const tools::ModelOutputResolutionCache::OutputLookup& left,
               const tools::ModelOutputResolutionCache::OutputLookup& right) noexcept
            {
                if (left.path != right.path)
                    return left.path < right.path;
                return left.type < right.type;
            });
        if (found == end || found->path != expected.path || found->type != expected.type || found->outputIndex >= settings.outputs.Size())
            return nullptr;
        outputIndex = found->outputIndex;
        const tools::ModelImportOutputCorrespondence& correspondence = settings.outputs[outputIndex];
        if (correspondence.output.type != output.ExpectedType())
            return nullptr;
        return &correspondence;
    }

    struct ModelMaterialResolverContext
    {
        const tools::ModelImportManifest* manifest = nullptr;
        const tools::ModelImportSettings* settings = nullptr;
        const assets::AssetMetadata* metadata = nullptr;
        const tools::ModelOutputResolutionCache* resolutionCache = nullptr;
    };

    [[nodiscard]] bool ResolveModelMaterial(const u32 sourceMaterialIndex, resources::ResourceReference& material,
                                            void* const userData) noexcept
    {
        if (userData == nullptr)
            return false;
        const ModelMaterialResolverContext& context = *static_cast<const ModelMaterialResolverContext*>(userData);
        if (context.manifest == nullptr || context.settings == nullptr || context.metadata == nullptr || context.resolutionCache == nullptr ||
            sourceMaterialIndex >= context.manifest->materials.Size())
            return false;
        if (context.settings->materialPolicy == tools::ModelImportMaterialPolicy::UseDefaultMaterial)
        {
            material = context.settings->mesh.defaultMaterial;
            return material.IsValid() && material.IsTyped();
        }

        if (sourceMaterialIndex >= context.resolutionCache->materialOutputIndexBySource.Size())
            return false;
        const u32 outputIndex = context.resolutionCache->materialOutputIndexBySource[sourceMaterialIndex];
        if (outputIndex == tools::InvalidModelImportIndex || outputIndex >= context.settings->outputs.Size())
            return false;
        const tools::ModelImportOutputCorrespondence& found = context.settings->outputs[outputIndex];
        if (found.kind != tools::ModelImportElementKind::Material)
            return false;
        material = assets::GetAssetOutputReference(context.metadata->id, found.output);
        return material.IsValid() && material.IsTyped();
    }

    [[nodiscard]] bool ApplyStableMaterialSlots(const tools::ModelImportManifest& manifest,
                                                const tools::ModelImportSettings& settings,
                                                const tools::ModelImportOutputCorrespondence& geometry,
                                                const u32 geometryIndex, tools::ImportedMesh& mesh) noexcept
    {
        if (geometryIndex >= manifest.geometryDefinitions.Size())
            return false;
        const tools::ModelImportGeometryDefinition& definition = manifest.geometryDefinitions[geometryIndex];
        if (mesh.submeshes.Size() != definition.primitiveIndices.Size())
            return false;
        containers::DynamicArray<u8> claimed(memory::pools::Assets::GetInstance());
        claimed.Resize(settings.materialSlots.Size());
        if (claimed.Size() != settings.materialSlots.Size())
            return false;
        for (u8& value : claimed)
            value = 0u;
        for (u32 submeshIndex = 0; submeshIndex < definition.primitiveIndices.Size(); ++submeshIndex)
        {
            const u32 primitiveIndex = definition.primitiveIndices[submeshIndex];
            if (primitiveIndex >= manifest.primitives.Size())
                return false;
            const tools::ModelImportPrimitive& primitive = manifest.primitives[primitiveIndex];
            crypto::Digest256 structuralKey;
            crypto::Digest256 partitionKey;
            if (!BuildSlotStructuralKey(primitive, structuralKey) || !BuildSlotPartitionKey(primitive, partitionKey))
                return false;
            u32 foundIndex = tools::InvalidModelImportIndex;
            for (u32 slotIndex = 0; slotIndex < settings.materialSlots.Size(); ++slotIndex)
            {
                const tools::ModelImportMaterialSlotCorrespondence& slot = settings.materialSlots[slotIndex];
                if (slot.geometryPersistentId != geometry.persistentId || slot.structuralKey != structuralKey || slot.partitionKey != partitionKey)
                    continue;
                if (foundIndex != tools::InvalidModelImportIndex)
                    return false;
                foundIndex = slotIndex;
            }
            if (foundIndex == tools::InvalidModelImportIndex || claimed[foundIndex] != 0u ||
                settings.materialSlots[foundIndex].stableSlotId == 0u)
                return false;
            claimed[foundIndex] = 1u;
            mesh.submeshes[submeshIndex].stableId = settings.materialSlots[foundIndex].stableSlotId;
        }
        return true;
    }

    [[nodiscard]] const tools::ModelImportTextureResolution* FindTextureResolution(const tools::ModelImportSettings& settings,
                                                                                   const containers::String& authoredPath) noexcept;

    [[nodiscard]] bool MaterialVertexRequirements(const tools::ModelImportManifest& manifest,
                                                  const tools::ModelImportSettings& settings,
                                                  const u32 sourceMaterialIndex,
                                                  bool& requiresUv0,
                                                  bool& requiresTangent0) noexcept
    {
        requiresUv0 = false;
        requiresTangent0 = false;
        if (sourceMaterialIndex >= manifest.materials.Size())
            return false;
        const tools::ModelImportMaterial& material = manifest.materials[sourceMaterialIndex];
        for (const u32 textureIndex : material.textureReferenceIndices)
        {
            if (textureIndex >= manifest.textureReferences.Size())
                return false;
            const tools::ModelImportTextureReference& texture = manifest.textureReferences[textureIndex];
            switch (texture.semantic)
            {
            case tools::ImportedTextureSemantic::BaseColor:
            case tools::ImportedTextureSemantic::Metalness:
            case tools::ImportedTextureSemantic::Roughness:
            case tools::ImportedTextureSemantic::MetallicRoughness:
            case tools::ImportedTextureSemantic::Emissive:
            case tools::ImportedTextureSemantic::Opacity:
                break;
            case tools::ImportedTextureSemantic::Normal:
                break;
            case tools::ImportedTextureSemantic::Specular:
            case tools::ImportedTextureSemantic::Occlusion:
            case tools::ImportedTextureSemantic::Unknown:
                // The material child owns representability for unsupported semantics. They do not
                // establish a vertex-input prerequisite for an independently routed mesh child.
                continue;
            }

            bool consumed = false;
            if (texture.storage == tools::ImportedTextureStorage::Embedded)
            {
                if (settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned)
                {
                    if (texture.embeddedTextureIndex == tools::InvalidModelImportIndex || texture.embeddedTextureIndex >= manifest.embeddedTextures.Size())
                        return false;
                    consumed = true;
                }
            }
            else if (texture.storage == tools::ImportedTextureStorage::ExternalSource && !texture.authoredPath.Empty())
            {
                const tools::ModelImportTextureResolution* const resolution = FindTextureResolution(settings, texture.authoredPath);
                if (resolution != nullptr)
                {
                    switch (resolution->resolution)
                    {
                    case tools::ModelImportTextureResolutionKind::ReuseManagedResource:
                    case tools::ModelImportTextureResolutionKind::AdoptProjectSource:
                        consumed = true;
                        break;
                    case tools::ModelImportTextureResolutionKind::IgnoreMissing:
                    case tools::ModelImportTextureResolutionKind::Unresolved:
                        break;
                    }
                }
            }

            if (consumed)
            {
                if (texture.uvSet != 0u)
                    return false;
                requiresUv0 = true;
                requiresTangent0 = requiresTangent0 || texture.semantic == tools::ImportedTextureSemantic::Normal;
            }
        }
        return true;
    }

    [[nodiscard]] bool ExtractModelGeometryForBuild(const tools::ModelImportManifest& manifest,
                                                    const tools::ModelImportSettings& settings,
                                                    const assets::AssetMetadata& metadata,
                                                    const tools::ModelOutputResolutionCache& resolutionCache,
                                                    const tools::ModelImportOutputCorrespondence& geometry,
                                                    const u32 geometryIndex, tools::ImportedMesh& mesh) noexcept
    {
        ModelMaterialResolverContext resolver{&manifest, &settings, &metadata, &resolutionCache};
        tools::ModelGeometryExtractionSettings extraction;
        extraction.uniformScale = settings.mesh.uniformScale;
        extraction.sourceUpAxis = settings.mesh.sourceUpAxis;
        extraction.flipWinding = settings.mesh.flipWinding;
        extraction.defaultMaterial = settings.mesh.defaultMaterial;
        extraction.skeleton = settings.mesh.skeleton;
        extraction.resolveMaterial = ResolveModelMaterial;
        extraction.resolveMaterialUserData = &resolver;
        if (tools::ExtractModelGeometry(manifest, geometryIndex, extraction, mesh) != tools::ModelImportResult::Success ||
            !ApplyStableMaterialSlots(manifest, settings, geometry, geometryIndex, mesh))
            return false;

        if (settings.materialPolicy != tools::ModelImportMaterialPolicy::ImportModelOwned)
            return true;
        if (settings.cook.meshCookingProfile != tools::profiles::RuntimeStatic)
            return false;

        // Imported-surface materials use one production vertex ABI. Keep the
        // inspection manifest untouched, but canonicalize the build-only mesh
        // before RuntimeStatic packing so VMAT pipeline layout is deterministic.
        const tools::ModelImportGeometryDefinition& definition = manifest.geometryDefinitions[geometryIndex];
        for (u32 submeshIndex = 0; submeshIndex < mesh.submeshes.Size(); ++submeshIndex)
        {
            tools::ImportedSubmesh& submesh = mesh.submeshes[submeshIndex];
            if (submeshIndex >= definition.primitiveIndices.Size() || definition.primitiveIndices[submeshIndex] >= manifest.primitives.Size())
                return false;
            const tools::ModelImportPrimitive& primitive = manifest.primitives[definition.primitiveIndices[submeshIndex]];
            bool requiresUv0 = false;
            bool requiresTangent0 = false;
            if (!MaterialVertexRequirements(manifest, settings, primitive.sourceMaterialIndex, requiresUv0, requiresTangent0))
                return false;
            tools::ImportedVertexStream* position = nullptr;
            tools::ImportedVertexStream* normal = nullptr;
            tools::ImportedVertexStream* tangent = nullptr;
            tools::ImportedVertexStream* uv = nullptr;
            for (tools::ImportedVertexStream& stream : submesh.vertexStreams)
            {
                if (stream.semanticIndex != 0u)
                    continue;
                tools::ImportedVertexStream** destination = nullptr;
                meshes::VertexFormat requiredFormat = meshes::VertexFormat::R32G32B32Float;
                switch (stream.semantic)
                {
                case meshes::VertexSemantic::Position:
                    destination = &position;
                    break;
                case meshes::VertexSemantic::Normal:
                    destination = &normal;
                    break;
                case meshes::VertexSemantic::Tangent:
                    destination = &tangent;
                    requiredFormat = meshes::VertexFormat::R32G32B32A32Float;
                    break;
                case meshes::VertexSemantic::TexCoord:
                    destination = &uv;
                    requiredFormat = meshes::VertexFormat::R32G32Float;
                    break;
                default:
                    break;
                }
                if (destination == nullptr)
                    continue;
                if (*destination != nullptr || stream.format != requiredFormat)
                    return false;
                *destination = &stream;
            }
            if (position == nullptr || normal == nullptr ||
                position->floatValues.Size() != submesh.vertexCount * 3u ||
                normal->floatValues.Size() != submesh.vertexCount * 3u)
                return false;

            tools::ImportedVertexStream generatedTangent;
            if (tangent == nullptr)
            {
                if (requiresTangent0)
                    return false;
                generatedTangent.semantic = meshes::VertexSemantic::Tangent;
                generatedTangent.semanticIndex = 0u;
                generatedTangent.format = meshes::VertexFormat::R32G32B32A32Float;
                generatedTangent.stride = sizeof(f32) * 4u;
                generatedTangent.floatValues.Reserve(submesh.vertexCount * 4u);
                for (u32 vertex = 0; vertex < submesh.vertexCount; ++vertex)
                {
                    const u32 base = vertex * 3u;
                    const f32 nx = normal->floatValues[base];
                    const f32 ny = normal->floatValues[base + 1u];
                    const f32 nz = normal->floatValues[base + 2u];
                    f32 tx = std::fabs(nz) < 0.999f ? -ny : 1.0f;
                    f32 ty = std::fabs(nz) < 0.999f ? nx : 0.0f;
                    f32 tz = 0.0f;
                    const f32 length = std::sqrt(tx * tx + ty * ty + tz * tz);
                    if (length > 1.0e-20f)
                    {
                        tx /= length;
                        ty /= length;
                        tz /= length;
                    }
                    else
                    {
                        tx = 1.0f;
                        ty = 0.0f;
                        tz = 0.0f;
                    }
                    generatedTangent.floatValues.PushBack(tx);
                    generatedTangent.floatValues.PushBack(ty);
                    generatedTangent.floatValues.PushBack(tz);
                    generatedTangent.floatValues.PushBack(1.0f);
                }
                tangent = &generatedTangent;
            }
            else if (tangent->floatValues.Size() != submesh.vertexCount * 4u)
                return false;

            tools::ImportedVertexStream generatedUv;
            if (uv == nullptr)
            {
                if (requiresUv0)
                    return false;
                generatedUv.semantic = meshes::VertexSemantic::TexCoord;
                generatedUv.semanticIndex = 0u;
                generatedUv.format = meshes::VertexFormat::R32G32Float;
                generatedUv.stride = sizeof(f32) * 2u;
                generatedUv.floatValues.Resize(submesh.vertexCount * 2u);
                for (f32& value : generatedUv.floatValues)
                    value = 0.0f;
                uv = &generatedUv;
            }
            else if (uv->floatValues.Size() != submesh.vertexCount * 2u)
                return false;

            containers::DynamicArray<tools::ImportedVertexStream> canonical(memory::pools::Assets::GetInstance());
            canonical.Reserve(4u);
            canonical.PushBack(static_cast<tools::ImportedVertexStream&&>(*position));
            canonical.PushBack(static_cast<tools::ImportedVertexStream&&>(*normal));
            canonical.PushBack(static_cast<tools::ImportedVertexStream&&>(*tangent));
            canonical.PushBack(static_cast<tools::ImportedVertexStream&&>(*uv));
            if (canonical.Size() != 4u)
                return false;
            submesh.vertexStreams = static_cast<containers::DynamicArray<tools::ImportedVertexStream>&&>(canonical);
            submesh.sourceStreams.Clear();
        }
        return true;
    }

    [[nodiscard]] bool AddUniqueGeneratedDependency(assets::DependencyCollector& dependencies,
                                                    const resources::ResourceReference identity) noexcept
    {
        if (!identity.IsValid() || !identity.IsTyped())
            return false;
        for (const assets::BuildDependency& existing : dependencies.GetDependencies())
            if (existing.role == assets::DependencyRole::Generated && existing.identity == identity)
                return true;
        return dependencies.Add({identity, {}, assets::DependencyRole::Generated, assets::DependencyRequirement::Required}) ==
               assets::Result::Success;
    }
} // namespace

namespace vanguard::mesh_tools
{
    struct ModelAssetCompiler::Impl final
    {
        struct CacheEntry final
        {
            crypto::Digest256 sourceFingerprint;
            crypto::Digest256 settingsFingerprint;
            assets::AssetId assetId;
            resources::ResourcePath sourceIdentity;
            containers::String formatHint;
            containers::String sourceName;
            u32 importerVersion = 0;
            u32 settingsVersion = 0;
            u32 pins = 0;
            u64 lastUse = 0;
            ModelImportSettings settings;
            ModelImportManifest manifest;
            ModelOutputResolutionCache resolutionCache;
        };

        struct InFlightEntry final
        {
            crypto::Digest256 sourceFingerprint;
            crypto::Digest256 settingsFingerprint;
            assets::AssetId assetId;
            resources::ResourcePath sourceIdentity;
            containers::String formatHint;
            containers::String sourceName;
            u32 importerVersion = 0;
            u32 settingsVersion = 0;
        };

        Impl() noexcept : cache(memory::pools::Tools::GetInstance()), inFlight(memory::pools::Tools::GetInstance()) {}

        ModelAssetCompilerConfig config;
        assets::BuildSystem* buildSystem = nullptr;
        assets::CompilerId meshCompilerId = assets::InvalidCompilerId;
        assets::SourceImporterId importerId = assets::InvalidSourceImporterId;
        containers::String sourceExtensions;
        containers::DynamicArray<CacheEntry*> cache;
        containers::DynamicArray<InFlightEntry*> inFlight;
        mutable vanguard::concurrency::Mutex cacheMutex;
        vanguard::concurrency::ConditionVariable cacheCondition;
        u64 useClock = 0;
        u64 inspections = 0;
        u64 cacheHits = 0;
    };
} // namespace vanguard::mesh_tools

namespace
{
    [[nodiscard]] bool BuildModelOutputResolutionCache(tools::ModelAssetCompiler::Impl::CacheEntry& entry) noexcept;

    class ModelInspectionLease final
    {
    public:
        ModelInspectionLease() noexcept = default;
        ModelInspectionLease(tools::ModelAssetCompiler::Impl* implementation,
                             tools::ModelAssetCompiler::Impl::CacheEntry* entry) noexcept
            : m_implementation(implementation), m_entry(entry)
        {
        }
        ~ModelInspectionLease() { Reset(); }
        ModelInspectionLease(const ModelInspectionLease&) = delete;
        ModelInspectionLease& operator=(const ModelInspectionLease&) = delete;
        ModelInspectionLease(ModelInspectionLease&& other) noexcept
            : m_implementation(other.m_implementation), m_entry(other.m_entry)
        {
            other.m_implementation = nullptr;
            other.m_entry = nullptr;
        }
        ModelInspectionLease& operator=(ModelInspectionLease&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_implementation = other.m_implementation;
                m_entry = other.m_entry;
                other.m_implementation = nullptr;
                other.m_entry = nullptr;
            }
            return *this;
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_entry != nullptr; }
        [[nodiscard]] tools::ModelAssetCompiler::Impl::CacheEntry* operator->() const noexcept { return m_entry; }

    private:
        void Reset() noexcept
        {
            if (m_implementation != nullptr && m_entry != nullptr)
            {
                vanguard::concurrency::ScopedLock lock(m_implementation->cacheMutex);
                if (m_entry->pins != 0u)
                    --m_entry->pins;
            }
            m_implementation = nullptr;
            m_entry = nullptr;
        }

        tools::ModelAssetCompiler::Impl* m_implementation = nullptr;
        tools::ModelAssetCompiler::Impl::CacheEntry* m_entry = nullptr;
    };

    [[nodiscard]] ModelInspectionLease AcquireModelInspection(tools::ModelAssetCompiler::Impl& implementation,
                                                              const assets::AssetMetadata& metadata,
                                                              const containers::ArraySpan<const u8> sourceBytes,
                                                              const containers::StringView formatHint,
                                                              const containers::StringView sourceName,
                                                              const resources::ResourcePath sourceIdentity) noexcept
    {
        if (!metadata.id.IsValid() || metadata.importer != implementation.importerId ||
            metadata.importerVersion != tools::ModelSourceImporterVersion || metadata.settingsVersion != tools::ModelImportSettingsVersion ||
            sourceBytes.Empty() || sourceBytes.Data() == nullptr || formatHint.Empty() || sourceName.Empty() || !sourceIdentity.IsValid())
            return {};
        const crypto::Digest256 sourceFingerprint = crypto::Sha256(sourceBytes.Data(), sourceBytes.SizeInBytes());
        const crypto::Digest256 settingsFingerprint = crypto::Sha256(metadata.settings.TypedData(), metadata.settings.Size());
        if (sourceFingerprint.IsEmpty() || settingsFingerprint.IsEmpty())
            return {};

        const auto matches = [&](const tools::ModelAssetCompiler::Impl::CacheEntry* const entry) noexcept
        {
            return entry != nullptr && entry->sourceFingerprint == sourceFingerprint && entry->settingsFingerprint == settingsFingerprint &&
                   entry->assetId == metadata.id && entry->sourceIdentity == sourceIdentity && entry->importerVersion == metadata.importerVersion &&
                   entry->settingsVersion == metadata.settingsVersion && entry->formatHint == formatHint && entry->sourceName == sourceName;
        };

        tools::ModelAssetCompiler::Impl::InFlightEntry flight;
        flight.sourceFingerprint = sourceFingerprint;
        flight.settingsFingerprint = settingsFingerprint;
        flight.assetId = metadata.id;
        flight.sourceIdentity = sourceIdentity;
        flight.formatHint = containers::String(formatHint.Data(), formatHint.Length());
        flight.sourceName = containers::String(sourceName.Data(), sourceName.Length());
        flight.importerVersion = metadata.importerVersion;
        flight.settingsVersion = metadata.settingsVersion;
        const auto matchesFlight = [&](const tools::ModelAssetCompiler::Impl::InFlightEntry* const candidate) noexcept
        {
            return candidate != nullptr && candidate->sourceFingerprint == flight.sourceFingerprint &&
                   candidate->settingsFingerprint == flight.settingsFingerprint && candidate->assetId == flight.assetId &&
                   candidate->sourceIdentity == flight.sourceIdentity &&
                   candidate->importerVersion == flight.importerVersion && candidate->settingsVersion == flight.settingsVersion &&
                   candidate->formatHint == flight.formatHint && candidate->sourceName == flight.sourceName;
        };

        {
            vanguard::concurrency::ScopedLock lock(implementation.cacheMutex);
            for (;;)
            {
                for (tools::ModelAssetCompiler::Impl::CacheEntry* const entry : implementation.cache)
                {
                    if (matches(entry))
                    {
                        ++implementation.useClock;
                        ++entry->pins;
                        entry->lastUse = implementation.useClock;
                        ++implementation.cacheHits;
                        return {&implementation, entry};
                    }
                }

                bool matchingInspectionInFlight = false;
                for (const tools::ModelAssetCompiler::Impl::InFlightEntry* const candidate : implementation.inFlight)
                    matchingInspectionInFlight = matchingInspectionInFlight || matchesFlight(candidate);
                if (matchingInspectionInFlight)
                {
                    implementation.cacheCondition.Wait(implementation.cacheMutex);
                    continue;
                }

                if (implementation.cache.Size() >= implementation.config.maximumCachedInspections)
                {
                    bool hasReusableSlot = false;
                    for (const tools::ModelAssetCompiler::Impl::CacheEntry* const candidate : implementation.cache)
                        hasReusableSlot = hasReusableSlot || (candidate != nullptr && candidate->pins == 0u);
                    if (!hasReusableSlot)
                        return {};
                }
                if (implementation.inFlight.Size() >= implementation.config.maximumCachedInspections)
                    return {};
                const u32 previous = implementation.inFlight.Size();
                implementation.inFlight.PushBack(&flight);
                if (implementation.inFlight.Size() != previous + 1u)
                    return {};
                break;
            }
        }

        const auto cancelFlight = [&]() noexcept
        {
            vanguard::concurrency::ScopedLock lock(implementation.cacheMutex);
            for (u32 index = 0; index < implementation.inFlight.Size(); ++index)
            {
                if (implementation.inFlight[index] == &flight)
                {
                    static_cast<void>(implementation.inFlight.RemoveAt(index));
                    break;
                }
            }
            implementation.cacheCondition.WakeAll();
        };

        auto* const entry = VANGUARD_NEW(tools::ModelAssetCompiler::Impl::CacheEntry, memory::pools::Tools);
        if (entry == nullptr)
        {
            cancelFlight();
            return {};
        }
        entry->sourceFingerprint = sourceFingerprint;
        entry->settingsFingerprint = settingsFingerprint;
        entry->assetId = metadata.id;
        entry->sourceIdentity = sourceIdentity;
        entry->formatHint = containers::String(formatHint.Data(), formatHint.Length());
        entry->sourceName = containers::String(sourceName.Data(), sourceName.Length());
        entry->importerVersion = metadata.importerVersion;
        entry->settingsVersion = metadata.settingsVersion;
        entry->pins = 1u;
        if (tools::DecodeModelImportSettings(metadata.settingsVersion, {metadata.settings.TypedData(), metadata.settings.Size()}, entry->settings) !=
                tools::ModelImportSettingsResult::Success ||
            entry->settings.sourceFingerprint.IsEmpty() || entry->settings.sourceFingerprint != sourceFingerprint ||
            tools::InspectModelMemory(sourceBytes.Data(), sourceBytes.SizeInBytes(), formatHint, sourceName,
                                      InspectionOptions(entry->settings), implementation.config.inspectionLimits, entry->manifest) !=
                tools::ModelImportResult::Success ||
            entry->manifest.sourceFingerprint != sourceFingerprint || !BuildModelOutputResolutionCache(*entry))
        {
            VANGUARD_DELETE(entry);
            cancelFlight();
            return {};
        }

        {
            vanguard::concurrency::ScopedLock lock(implementation.cacheMutex);
            ++implementation.inspections;
            ++implementation.useClock;
            for (tools::ModelAssetCompiler::Impl::CacheEntry* const published : implementation.cache)
            {
                if (matches(published))
                {
                    ++published->pins;
                    published->lastUse = implementation.useClock;
                    ++implementation.cacheHits;
                    for (u32 index = 0; index < implementation.inFlight.Size(); ++index)
                        if (implementation.inFlight[index] == &flight)
                        {
                            static_cast<void>(implementation.inFlight.RemoveAt(index));
                            break;
                        }
                    implementation.cacheCondition.WakeAll();
                    VANGUARD_DELETE(entry);
                    return {&implementation, published};
                }
            }

            u32 slot = implementation.cache.Size();
            if (implementation.cache.Size() >= implementation.config.maximumCachedInspections)
            {
                u64 oldest = ~u64{0};
                for (u32 index = 0; index < implementation.cache.Size(); ++index)
                {
                    const auto* const candidate = implementation.cache[index];
                    if (candidate != nullptr && candidate->pins == 0u && candidate->lastUse < oldest)
                    {
                        oldest = candidate->lastUse;
                        slot = index;
                    }
                }
                if (slot >= implementation.cache.Size())
                {
                    for (u32 index = 0; index < implementation.inFlight.Size(); ++index)
                        if (implementation.inFlight[index] == &flight)
                        {
                            static_cast<void>(implementation.inFlight.RemoveAt(index));
                            break;
                        }
                    implementation.cacheCondition.WakeAll();
                    VANGUARD_DELETE(entry);
                    return {};
                }
            }

            entry->lastUse = implementation.useClock;
            if (slot < implementation.cache.Size())
            {
                VANGUARD_DELETE(implementation.cache[slot]);
                implementation.cache[slot] = entry;
            }
            else
            {
                implementation.cache.PushBack(entry);
                if (implementation.cache.Size() != slot + 1u)
                {
                    for (u32 index = 0; index < implementation.inFlight.Size(); ++index)
                        if (implementation.inFlight[index] == &flight)
                        {
                            static_cast<void>(implementation.inFlight.RemoveAt(index));
                            break;
                        }
                    implementation.cacheCondition.WakeAll();
                    VANGUARD_DELETE(entry);
                    return {};
                }
            }
            for (u32 index = 0; index < implementation.inFlight.Size(); ++index)
                if (implementation.inFlight[index] == &flight)
                {
                    static_cast<void>(implementation.inFlight.RemoveAt(index));
                    break;
                }
            implementation.cacheCondition.WakeAll();
        }
        return {&implementation, entry};
    }

    [[nodiscard]] bool AcquireModelInspectionFromRequest(tools::ModelAssetCompiler::Impl& implementation,
                                                         const assets::BuildRequest& request,
                                                         const ModelGeometryRoute& route,
                                                         assets::AssetMetadata& metadata,
                                                         ModelInspectionLease& lease) noexcept
    {
        if (!DecodeModelMetadata(request.source.metadata, metadata) || metadata.importer != implementation.importerId ||
            metadata.importerVersion != tools::ModelSourceImporterVersion)
            return false;
        lease = AcquireModelInspection(implementation, metadata, request.source.content, route.formatHint, route.sourceName,
                                       request.source.identity.GetPath());
        return static_cast<bool>(lease);
    }

    [[nodiscard]] bool ModelRecognizeSource(const containers::StringView sourcePath, void* const userData) noexcept
    {
        if (userData == nullptr)
            return false;
        const containers::StringView extension = filesystem::paths::GetExtension(sourcePath);
        if (extension.Empty())
            return false;
        const containers::StringView supported = static_cast<tools::ModelAssetCompiler::Impl*>(userData)->sourceExtensions;
        u32 start = 0;
        for (u32 end = 0; end <= supported.Length(); ++end)
        {
            if (end != supported.Length() && supported[end] != ';')
                continue;
            const containers::StringView token = supported.Slice(start, end);
            if (token.StartsWith("*.") && token.Length() == extension.Length() + 2u && token.SubView(2).StartsWithIgnoreCase(extension))
                return true;
            start = end + 1u;
        }
        return false;
    }

    [[nodiscard]] bool ModelValidateSettings(const u32 settingsVersion, const containers::ArraySpan<const u8> settings,
                                             void*) noexcept
    {
        tools::ModelImportSettings decoded;
        return tools::DecodeModelImportSettings(settingsVersion, settings, decoded) == tools::ModelImportSettingsResult::Success;
    }

    [[nodiscard]] bool ModelScanSourceReferences(const assets::SourceRecord& source, const assets::SourceDataView&,
                                                 assets::AssetReferenceCollector& references, void*) noexcept
    {
        tools::ModelImportSettings settings;
        if (source.metadataResult != assets::MetadataResult::Success ||
            tools::DecodeModelImportSettings(source.metadata.settingsVersion,
                                             {source.metadata.settings.TypedData(), source.metadata.settings.Size()}, settings) !=
                tools::ModelImportSettingsResult::Success)
            return false;
        const auto add = [&references](const resources::ResourceReference reference) noexcept
        {
            return !reference.IsValid() ||
                   (reference.IsTyped() && references.Add({reference, resources::DependencyKind::Required}) == assets::Result::Success);
        };
        if (!add(settings.mesh.defaultMaterial) || !add(settings.mesh.skeleton))
            return false;
        for (const tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
            if (!add(resolution.managedResource))
                return false;
        return true;
    }

    [[nodiscard]] bool ModelCreateBuildRequest(const assets::SourceRecord& source, const assets::SourceDataView& data,
                                               const resources::ResourceReference output, assets::BuildRequest& request,
                                               void* const userData) noexcept
    {
        request = {};
        if (userData == nullptr || source.metadataResult != assets::MetadataResult::Success || !data.IsValid())
            return false;
        auto& implementation = *static_cast<tools::ModelAssetCompiler::Impl*>(userData);
        const containers::StringView formatHint = filesystem::paths::GetExtension(source.path.AsStringView());
        const containers::StringView sourceName = filesystem::paths::GetFileName(source.path);
        ModelInspectionLease inspection =
            AcquireModelInspection(implementation, source.metadata, data.content, formatHint, sourceName, source.sourceIdentity);
        if (!inspection)
            return false;
        u32 outputIndex = tools::InvalidModelImportIndex;
        const tools::ModelImportOutputCorrespondence* const correspondence =
            FindOutputCorrespondence(inspection->settings, inspection->resolutionCache, output, outputIndex);
        if (correspondence == nullptr || correspondence->output.type != output.ExpectedType())
            return false;
        u32 sourceIndex = tools::InvalidModelImportIndex;
        if (correspondence->kind != tools::ModelImportElementKind::Composition)
        {
            if (outputIndex >= inspection->resolutionCache.sourceIndexByOutput.Size())
                return false;
            sourceIndex = inspection->resolutionCache.sourceIndexByOutput[outputIndex];
            if (sourceIndex == tools::InvalidModelImportIndex)
                return false;
        }

        if (correspondence->kind == tools::ModelImportElementKind::Geometry)
        {
            static thread_local containers::DynamicArray<u8> routeBytes(memory::pools::Assets::GetInstance());
            if (!EncodeModelGeometryRoute(correspondence->persistentId, formatHint, sourceName, routeBytes))
                return false;
            request = {{resources::ResourceReference(source.sourceIdentity, tools::ModelGeometryBuildInputResourceType), data.content, data.metadata},
                       output, data.target, {routeBytes.TypedData(), routeBytes.Size()}};
            return request.IsValid() && output.ExpectedType() == meshes::MeshResourceType;
        }

        tools::ComposeModelOutputBuildRequestFunction compose = nullptr;
        void* composeUserData = nullptr;
        if (correspondence->kind == tools::ModelImportElementKind::Material)
        {
            compose = implementation.config.composeMaterial;
            composeUserData = implementation.config.composeMaterialUserData;
        }
        else if (correspondence->kind == tools::ModelImportElementKind::EmbeddedTexture)
        {
            compose = implementation.config.composeEmbeddedTexture;
            composeUserData = implementation.config.composeEmbeddedTextureUserData;
        }
        else if (correspondence->kind == tools::ModelImportElementKind::Composition)
        {
            compose = implementation.config.composeComposition;
            composeUserData = implementation.config.composeCompositionUserData;
        }
        else
            return false;
        if (compose == nullptr)
            return false;

        static thread_local assets::OwnedBuildRequest composed;
        composed.Reset();
        const tools::ModelOutputBuildRequestContext context{&inspection->manifest, &inspection->settings, correspondence, &source,
                                                            data, output, sourceIndex, &inspection->resolutionCache};
        if (!compose(context, composed, composeUserData))
            return false;
        const assets::BuildRequest borrowed = composed.View();
        if (!borrowed.IsValid() || borrowed.output != output || borrowed.target != data.target)
            return false;
        request = borrowed;
        return true;
    }

    [[nodiscard]] bool ResolveModelGeometryBuild(const assets::BuildRequest& request,
                                                 tools::ModelAssetCompiler::Impl& implementation,
                                                 assets::AssetMetadata& metadata, ModelInspectionLease& inspection,
                                                 const tools::ModelImportOutputCorrespondence*& correspondence,
                                                 u32& geometryIndex, tools::ImportedMesh& mesh) noexcept
    {
        correspondence = nullptr;
        geometryIndex = tools::InvalidModelImportIndex;
        ModelGeometryRoute route;
        if (!DecodeModelGeometryRoute(request.settings, route) || request.source.identity.ExpectedType() != tools::ModelGeometryBuildInputResourceType ||
            request.output.ExpectedType() != meshes::MeshResourceType ||
            !AcquireModelInspectionFromRequest(implementation, request, route, metadata, inspection))
            return false;
        u32 outputIndex = tools::InvalidModelImportIndex;
        correspondence = FindOutputCorrespondence(inspection->settings, inspection->resolutionCache, request.output, outputIndex);
        if (correspondence == nullptr || correspondence->kind != tools::ModelImportElementKind::Geometry ||
            correspondence->persistentId != route.persistentId)
            return false;
        if (outputIndex >= inspection->resolutionCache.sourceIndexByOutput.Size())
            return false;
        geometryIndex = inspection->resolutionCache.sourceIndexByOutput[outputIndex];
        if (geometryIndex == tools::InvalidModelImportIndex)
            return false;
        return ExtractModelGeometryForBuild(inspection->manifest, inspection->settings, metadata, inspection->resolutionCache,
                                            *correspondence, geometryIndex, mesh);
    }

    [[nodiscard]] bool ModelMeshDiscover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies,
                                         void* const userData) noexcept
    {
        if (userData == nullptr)
            return false;
        auto& implementation = *static_cast<tools::ModelAssetCompiler::Impl*>(userData);
        assets::AssetMetadata metadata;
        ModelInspectionLease inspection;
        const tools::ModelImportOutputCorrespondence* correspondence = nullptr;
        u32 geometryIndex = tools::InvalidModelImportIndex;
        tools::ImportedMesh mesh;
        if (!ResolveModelGeometryBuild(request, implementation, metadata, inspection, correspondence, geometryIndex, mesh))
            return false;
        const resources::ResourceReference tool(resources::ResourcePath::FromString(ModelMeshToolPath), tools::ModelBuildToolResourceType);
        crypto::Digest256 profileFingerprint;
        if (!tools::GetMeshCookingProfileFingerprint(inspection->settings.cook.meshCookingProfile, profileFingerprint))
            return false;
        crypto::Sha256Builder toolFingerprintBuilder;
        if (!toolFingerprintBuilder.Update(ModelMeshCompilerName, sizeof(ModelMeshCompilerName) - 1u) ||
            !toolFingerprintBuilder.Update(profileFingerprint.bytes, sizeof(profileFingerprint.bytes)))
            return false;
        crypto::Digest256 toolFingerprint;
        if (!toolFingerprintBuilder.Finalize(toolFingerprint) || toolFingerprint.IsEmpty())
            return false;
        if (dependencies.Add({tool, toolFingerprint, assets::DependencyRole::Tool, assets::DependencyRequirement::Required}) != assets::Result::Success)
            return false;
        if (mesh.skeleton.IsValid() && !AddUniqueGeneratedDependency(dependencies, mesh.skeleton))
            return false;
        for (const tools::ImportedSubmesh& submesh : mesh.submeshes)
            if (!AddUniqueGeneratedDependency(dependencies, submesh.material))
                return false;
        return true;
    }

    [[nodiscard]] bool ModelMeshEstimate(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                         assets::BuildResourceEstimate& estimate, void* const userData) noexcept
    {
        if (userData == nullptr)
            return false;
        const auto& implementation = *static_cast<const tools::ModelAssetCompiler::Impl*>(userData);
        estimate.compilerTransientBytes = implementation.config.estimatedMeshCompilerTransientBytes;
        estimate.artifactBytes = implementation.config.maximumMeshArtifactBytes;
        return estimate.IsValid() && estimate.compilerTransientBytes != 0u;
    }

    [[nodiscard]] bool ModelMeshCompile(const assets::CompileContext& context, assets::ArtifactWriter& artifacts,
                                        void* const userData) noexcept
    {
        if (userData == nullptr || context.IsCancellationRequested())
            return false;
        auto& implementation = *static_cast<tools::ModelAssetCompiler::Impl*>(userData);
        assets::AssetMetadata metadata;
        ModelInspectionLease inspection;
        const tools::ModelImportOutputCorrespondence* correspondence = nullptr;
        u32 geometryIndex = tools::InvalidModelImportIndex;
        tools::ImportedMesh mesh;
        if (!ResolveModelGeometryBuild(context.request, implementation, metadata, inspection, correspondence, geometryIndex, mesh))
            return false;
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        filesystem::MemoryFileWriter writer(bytes);
        if (tools::CookMesh(mesh.BuildSourceView(), writer, CookSettingsView(inspection->settings.cook)) != tools::Result::Success ||
            context.IsCancellationRequested() || bytes.Size() > implementation.config.maximumMeshArtifactBytes)
            return false;
        filesystem::MemoryFileReader reader(bytes, 0);
        meshes::MeshFile cooked;
        containers::DynamicArray<meshes::StorageSegment> segments(memory::pools::Assets::GetInstance());
        if (cooked.Open(reader) != meshes::Result::Success || meshes::BuildStorageSegments(cooked, bytes.Size(), segments) != meshes::Result::Success)
            return false;
        for (u32 segmentIndex = 0; segmentIndex < segments.Size(); ++segmentIndex)
        {
            const meshes::StorageSegment& segment = segments[segmentIndex];
            assets::ArtifactFlags flags = assets::ArtifactFlags::Streamable;
            if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::Metadata))
                flags = assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident;
            else if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::RequiredForLowestLod))
                flags = flags | assets::ArtifactFlags::MemoryResident;
            if (segment.offset > bytes.Size() || segment.byteSize > bytes.Size() - segment.offset ||
                artifacts.Add(context.request.output, segmentIndex, flags, segment.alignmentLog2, bytes.TypedData() + segment.offset,
                              static_cast<usize>(segment.byteSize)) != assets::Result::Success)
                return false;
        }
        return !context.IsCancellationRequested();
    }
} // namespace

namespace vanguard::mesh_tools
{
    bool ResolveModelEmbeddedTextureOutput(const ModelOutputBuildRequestContext& context, const u32 embeddedTextureIndex,
                                           resources::ResourceReference& output) noexcept
    {
        output = {};
        if (context.manifest == nullptr || context.settings == nullptr || context.source == nullptr || context.resolutionCache == nullptr ||
            embeddedTextureIndex >= context.manifest->embeddedTextures.Size() || !context.source->metadata.id.IsValid())
            return false;

        if (embeddedTextureIndex >= context.resolutionCache->embeddedTextureOutputIndexBySource.Size())
            return false;
        const u32 outputIndex = context.resolutionCache->embeddedTextureOutputIndexBySource[embeddedTextureIndex];
        if (outputIndex == InvalidModelImportIndex || outputIndex >= context.settings->outputs.Size())
            return false;
        const ModelImportOutputCorrespondence& match = context.settings->outputs[outputIndex];
        if (match.kind != ModelImportElementKind::EmbeddedTexture)
            return false;

        const resources::ResourceReference reference = assets::GetAssetOutputReference(context.source->metadata.id, match.output);
        if (!reference.IsValid() || !reference.IsTyped())
            return false;
        output = reference;
        return true;
    }

    bool ResolveModelGeometryOutput(const ModelOutputBuildRequestContext& context, const u32 geometryDefinitionIndex,
                                    resources::ResourceReference& output, u64& persistentId) noexcept
    {
        output = {};
        persistentId = 0u;
        if (context.manifest == nullptr || context.settings == nullptr || context.source == nullptr || context.resolutionCache == nullptr ||
            geometryDefinitionIndex >= context.manifest->geometryDefinitions.Size() || !context.source->metadata.id.IsValid())
            return false;

        if (geometryDefinitionIndex >= context.resolutionCache->geometryOutputIndexBySource.Size())
            return false;
        const u32 outputIndex = context.resolutionCache->geometryOutputIndexBySource[geometryDefinitionIndex];
        if (outputIndex == InvalidModelImportIndex || outputIndex >= context.settings->outputs.Size())
            return false;
        const ModelImportOutputCorrespondence& match = context.settings->outputs[outputIndex];
        if (match.kind != ModelImportElementKind::Geometry || match.persistentId == 0u)
            return false;
        const resources::ResourceReference reference = assets::GetAssetOutputReference(context.source->metadata.id, match.output);
        if (!reference.IsValid() || !reference.IsTyped())
            return false;
        output = reference;
        persistentId = match.persistentId;
        return true;
    }

    bool ResolveModelCompositionNode(const ModelOutputBuildRequestContext& context, const u32 sourceNodeIndex,
                                     u64& stableNodeId, u64& parentStableNodeId) noexcept
    {
        stableNodeId = 0u;
        parentStableNodeId = 0u;
        if (context.manifest == nullptr || context.settings == nullptr || sourceNodeIndex >= context.manifest->nodes.Size())
            return false;
        const ModelImportCompositionNodeCorrespondence* match = nullptr;
        for (const ModelImportCompositionNodeCorrespondence& candidate : context.settings->compositionNodes)
        {
            if (candidate.sourceNodeIndex != sourceNodeIndex)
                continue;
            if (match != nullptr)
                return false;
            match = &candidate;
        }
        if (match == nullptr || match->stableNodeId == 0u)
            return false;
        stableNodeId = match->stableNodeId;
        parentStableNodeId = match->parentStableNodeId;
        return true;
    }

    ModelAssetCompiler::~ModelAssetCompiler()
    {
        static_cast<void>(Shutdown());
    }

    bool ModelAssetCompiler::Initialize(const ModelAssetCompilerConfig& config) noexcept
    {
        if (m_impl != nullptr || !mesh_tools::IsInitialized() || config.outputTypes.mesh != meshes::MeshResourceType ||
            config.maximumCachedInspections == 0u || config.maximumMeshArtifactBytes == 0u ||
            config.estimatedMeshCompilerTransientBytes == 0u)
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return false;
        implementation->config = config;
        implementation->meshCompilerId = assets::HashCompilerName(ModelMeshCompilerName);
        implementation->importerId = assets::HashCompilerName(ModelImporterName);
        implementation->sourceExtensions = "*.gltf;*.glb";
        if (implementation->sourceExtensions.Empty())
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        implementation->cache.Reserve(config.maximumCachedInspections);
        implementation->inFlight.Reserve(config.maximumCachedInspections);
        if (implementation->cache.Capacity() < config.maximumCachedInspections ||
            implementation->inFlight.Capacity() < config.maximumCachedInspections)
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        m_impl = implementation;
        return true;
    }

    bool ModelAssetCompiler::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (m_impl->buildSystem != nullptr && Unregister() != assets::Result::Success)
            return false;
        {
            vanguard::concurrency::ScopedLock lock(m_impl->cacheMutex);
            if (!m_impl->inFlight.Empty())
                return false;
            for (Impl::CacheEntry* const entry : m_impl->cache)
            {
                if (entry != nullptr && entry->pins != 0u)
                    return false;
            }
            for (Impl::CacheEntry* const entry : m_impl->cache)
                if (entry != nullptr)
                    VANGUARD_DELETE(entry);
            m_impl->cache.Clear();
        }
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ModelAssetCompiler::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    assets::CompilerDescriptor ModelAssetCompiler::GetMeshDescriptor() noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->meshCompilerId, ModelMeshCompilerName, ModelAssetCompilerVersion, ModelGeometryBuildInputResourceType,
                meshes::MeshResourceType, ModelMeshDiscover, ModelMeshCompile, m_impl, ModelMeshEstimate};
    }

    assets::SourceImporterDescriptor ModelAssetCompiler::GetImporterDescriptor() noexcept
    {
        if (m_impl == nullptr)
            return {};
        assets::SourceImporterDescriptor descriptor;
        descriptor.id = m_impl->importerId;
        descriptor.name = ModelImporterName;
        descriptor.version = ModelSourceImporterVersion;
        descriptor.primaryOutputType = meshes::MeshResourceType;
        descriptor.recognizeSource = ModelRecognizeSource;
        descriptor.createBuildRequest = ModelCreateBuildRequest;
        descriptor.scanSourceReferences = ModelScanSourceReferences;
        descriptor.userData = m_impl;
        descriptor.validateSettings = ModelValidateSettings;
        return descriptor;
    }

    assets::Result ModelAssetCompiler::Register(assets::BuildSystem& buildSystem) noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem != nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = buildSystem.RegisterCompiler(GetMeshDescriptor());
        if (result == assets::Result::Success)
            m_impl->buildSystem = &buildSystem;
        return result;
    }

    assets::Result ModelAssetCompiler::Unregister() noexcept
    {
        if (m_impl == nullptr || m_impl->buildSystem == nullptr)
            return assets::Result::InvalidState;
        const assets::Result result = m_impl->buildSystem->UnregisterCompiler(m_impl->meshCompilerId);
        if (result == assets::Result::Success)
            m_impl->buildSystem = nullptr;
        return result;
    }

    ModelAssetCompilerStats ModelAssetCompiler::GetStats() const noexcept
    {
        ModelAssetCompilerStats stats;
        if (m_impl == nullptr)
            return stats;
        vanguard::concurrency::ScopedLock lock(m_impl->cacheMutex);
        stats.inspections = m_impl->inspections;
        stats.inspectionCacheHits = m_impl->cacheHits;
        stats.cachedInspections = m_impl->cache.Size();
        return stats;
    }
} // namespace vanguard::mesh_tools

namespace
{
    [[nodiscard]] bool SameOutputIdentity(const assets::AssetOutput& left, const assets::AssetOutput& right) noexcept;

    struct ModelPlanCurrentElement
    {
        tools::ModelImportElementKind kind = tools::ModelImportElementKind::Geometry;
        u32 sourceIndex = tools::InvalidModelImportIndex;
        resources::ResourceTypeId outputType = resources::InvalidResourceTypeId;
        tools::ModelImportSourceEvidence source;
        containers::String displayName;
    };

    [[nodiscard]] bool HashBytes(crypto::Sha256Builder& hash, const void* const data, const usize size) noexcept
    {
        return size == 0u || hash.Update(data, size);
    }

    [[nodiscard]] bool HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        return HashBytes(hash, &value, sizeof(value));
    }

    [[nodiscard]] bool HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[4]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                          static_cast<u8>(value >> 24u)};
        return HashBytes(hash, bytes, sizeof(bytes));
    }

    [[nodiscard]] bool HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        return HashU32(hash, static_cast<u32>(value)) && HashU32(hash, static_cast<u32>(value >> 32u));
    }

    [[nodiscard]] bool HashF32(crypto::Sha256Builder& hash, const f32 value) noexcept
    {
        u32 bits = 0u;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return HashU32(hash, bits);
    }

    [[nodiscard]] bool HashString(crypto::Sha256Builder& hash, const containers::String& value) noexcept
    {
        return HashU32(hash, value.Length()) && (value.Empty() || HashBytes(hash, value.AsChar(), value.Length()));
    }

    [[nodiscard]] bool FinalizeEvidenceHash(crypto::Sha256Builder& hash, crypto::Digest256& digest) noexcept
    {
        return hash.Finalize(digest) && !digest.IsEmpty();
    }

    [[nodiscard]] bool HashPrimitiveSchema(crypto::Sha256Builder& hash,
                                           const tools::ModelImportPrimitive& primitive) noexcept
    {
        if (!HashU32(hash, static_cast<u32>(primitive.flags)) || !HashU32(hash, primitive.vertexStreams.Size()) ||
            !HashU32(hash, primitive.boneBindings.Size()))
            return false;
        for (const tools::ImportedVertexStream& stream : primitive.vertexStreams)
            if (!HashU8(hash, static_cast<u8>(stream.semantic)) || !HashU8(hash, stream.semanticIndex) ||
                !HashU8(hash, static_cast<u8>(stream.format)) || !HashU32(hash, stream.stride))
                return false;
        return true;
    }

    [[nodiscard]] bool HashPrimitiveSlotContent(crypto::Sha256Builder& hash,
                                                const tools::ModelImportPrimitive& primitive) noexcept
    {
        if (!HashPrimitiveSchema(hash, primitive) || !HashU32(hash, primitive.vertexCount) ||
            !HashU32(hash, primitive.indices.Size()))
            return false;
        for (const tools::ImportedVertexStream& stream : primitive.vertexStreams)
        {
            if (!HashU32(hash, stream.floatValues.Size()) || !HashU32(hash, stream.integerValues.Size()))
                return false;
            for (const f32 value : stream.floatValues)
                if (!HashF32(hash, value))
                    return false;
            for (const u8 value : stream.integerValues)
                if (!HashU8(hash, value))
                    return false;
        }
        for (const u32 index : primitive.indices)
            if (!HashU32(hash, index))
                return false;
        for (const tools::ModelImportBoneBinding& binding : primitive.boneBindings)
        {
            if (!HashU32(hash, binding.weights.Size()))
                return false;
            for (const f32 value : binding.inverseBind.values)
                if (!HashF32(hash, value))
                    return false;
            for (const tools::ModelImportBoneWeight& weight : binding.weights)
                if (!HashU32(hash, weight.vertexIndex) || !HashF32(hash, weight.weight))
                    return false;
        }
        return true;
    }

    constexpr char GeometryPlacementEvidenceDomain[] = "vanguard.model.geometry.placement.v1";
    constexpr char GeometryRelationshipPlacementEvidenceDomain[] = "vanguard.model.geometry.placement-relationship.v1";
    constexpr char CompositionNodeStructuralEvidenceDomain[] = "vanguard.model.composition-node.structural.v2";
    constexpr char CompositionNodeRelationshipEvidenceDomain[] = "vanguard.model.composition-node.relationship.v2";
    // Source cardinality/depth limits remain permissive for inspection. Planning additionally
    // bounds cumulative ancestry hashing so a valid deep fan-out cannot monopolize a build thread.
    constexpr u64 MaximumModelEvidenceAncestryVisits = 16ull * 1024ull * 1024ull;
    constexpr u64 MaximumModelEvidenceAncestryStringBytes = 256ull * 1024ull * 1024ull;

    struct ModelEvidenceDigestPair final
    {
        crypto::Digest256 structural;
        crypto::Digest256 relationship;
    };

    struct ModelPlanEvidenceIndex final
    {
        ModelPlanEvidenceIndex() noexcept
            : geometryInstanceOffsets(memory::pools::Assets::GetInstance()),
              geometryInstanceIndices(memory::pools::Assets::GetInstance()),
              geometryNodeSlots(memory::pools::Assets::GetInstance()),
              geometryPlacementKeys(memory::pools::Assets::GetInstance()),
              geometryRelationshipPlacementKeys(memory::pools::Assets::GetInstance()),
              compositionStructuralKeys(memory::pools::Assets::GetInstance()),
              nodeRelationshipDepths(memory::pools::Assets::GetInstance()),
              geometryEvidence(memory::pools::Assets::GetInstance()),
              materialEvidence(memory::pools::Assets::GetInstance()),
              embeddedTextureEvidence(memory::pools::Assets::GetInstance())
        {
        }

        containers::DynamicArray<u32> geometryInstanceOffsets;
        containers::DynamicArray<u32> geometryInstanceIndices;
        containers::DynamicArray<u32> geometryNodeSlots;
        containers::DynamicArray<crypto::Digest256> geometryPlacementKeys;
        containers::DynamicArray<crypto::Digest256> geometryRelationshipPlacementKeys;
        containers::DynamicArray<crypto::Digest256> compositionStructuralKeys;
        containers::DynamicArray<u32> nodeRelationshipDepths;
        containers::DynamicArray<ModelEvidenceDigestPair> geometryEvidence;
        containers::DynamicArray<ModelEvidenceDigestPair> materialEvidence;
        containers::DynamicArray<ModelEvidenceDigestPair> embeddedTextureEvidence;
    };

    [[nodiscard]] bool BuildModelPlanEvidenceIndex(const tools::ModelImportManifest& manifest,
                                                   const bool includeCompositionEvidence,
                                                   ModelPlanEvidenceIndex& output) noexcept
    {
        const u32 geometryCount = manifest.geometryDefinitions.Size();
        const u32 nodeCount = manifest.nodes.Size();
        if (geometryCount == ~u32{0})
            return false;

        output.geometryInstanceOffsets.Resize(geometryCount + 1u);
        output.geometryInstanceIndices.Resize(manifest.geometryInstances.Size());
        output.geometryNodeSlots.Resize(nodeCount);
        output.compositionStructuralKeys.Resize(includeCompositionEvidence ? nodeCount : 0u);
        output.nodeRelationshipDepths.Resize(includeCompositionEvidence ? nodeCount : 0u);
        if (output.geometryInstanceOffsets.Size() != geometryCount + 1u ||
            output.geometryInstanceIndices.Size() != manifest.geometryInstances.Size() ||
            output.geometryNodeSlots.Size() != nodeCount ||
            output.compositionStructuralKeys.Size() != (includeCompositionEvidence ? nodeCount : 0u) ||
            output.nodeRelationshipDepths.Size() != (includeCompositionEvidence ? nodeCount : 0u))
            return false;

        for (u32& offset : output.geometryInstanceOffsets)
            offset = 0u;
        for (u32& slot : output.geometryNodeSlots)
            slot = tools::InvalidModelImportIndex;

        u32 geometryNodeCount = 0u;
        for (const tools::ModelImportGeometryInstance& instance : manifest.geometryInstances)
        {
            if (instance.geometryDefinitionIndex >= geometryCount || instance.nodeIndex >= nodeCount ||
                output.geometryInstanceOffsets[instance.geometryDefinitionIndex + 1u] == ~u32{0})
                return false;
            ++output.geometryInstanceOffsets[instance.geometryDefinitionIndex + 1u];
            if (output.geometryNodeSlots[instance.nodeIndex] == tools::InvalidModelImportIndex)
            {
                if (geometryNodeCount == tools::InvalidModelImportIndex)
                    return false;
                output.geometryNodeSlots[instance.nodeIndex] = geometryNodeCount++;
            }
        }

        output.geometryPlacementKeys.Resize(geometryNodeCount);
        output.geometryRelationshipPlacementKeys.Resize(geometryNodeCount);
        if (output.geometryPlacementKeys.Size() != geometryNodeCount ||
            output.geometryRelationshipPlacementKeys.Size() != geometryNodeCount)
            return false;

        u64 runningOffset = 0u;
        for (u32 geometryIndex = 0u; geometryIndex < geometryCount; ++geometryIndex)
        {
            runningOffset += output.geometryInstanceOffsets[geometryIndex + 1u];
            if (runningOffset > ~u32{0})
                return false;
            output.geometryInstanceOffsets[geometryIndex + 1u] = static_cast<u32>(runningOffset);
        }
        if (runningOffset != manifest.geometryInstances.Size())
            return false;

        containers::DynamicArray<u32> writeOffsets(memory::pools::Assets::GetInstance());
        writeOffsets.Resize(geometryCount);
        if (writeOffsets.Size() != geometryCount)
            return false;
        for (u32 geometryIndex = 0u; geometryIndex < geometryCount; ++geometryIndex)
            writeOffsets[geometryIndex] = output.geometryInstanceOffsets[geometryIndex];
        for (u32 instanceIndex = 0u; instanceIndex < manifest.geometryInstances.Size(); ++instanceIndex)
        {
            const tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[instanceIndex];
            const u32 destination = writeOffsets[instance.geometryDefinitionIndex]++;
            if (destination >= output.geometryInstanceIndices.Size())
                return false;
            output.geometryInstanceIndices[destination] = instanceIndex;
        }

        u64 ancestryVisits = 0u;
        u64 ancestryStringBytes = 0u;
        for (u32 nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex)
        {
            const u32 geometrySlot = output.geometryNodeSlots[nodeIndex];
            const bool geometryEvidenceRequired = geometrySlot != tools::InvalidModelImportIndex;
            if (!geometryEvidenceRequired && !includeCompositionEvidence)
                continue;

            crypto::Sha256Builder geometryPlacementHash;
            crypto::Sha256Builder compositionStructuralHash;
            if ((geometryEvidenceRequired &&
                 !HashBytes(geometryPlacementHash, GeometryPlacementEvidenceDomain, sizeof(GeometryPlacementEvidenceDomain) - 1u)) ||
                (includeCompositionEvidence &&
                 !HashBytes(compositionStructuralHash, CompositionNodeStructuralEvidenceDomain,
                            sizeof(CompositionNodeStructuralEvidenceDomain) - 1u)))
                return false;

            u32 current = nodeIndex;
            u32 depth = 0u;
            while (current != tools::InvalidModelImportIndex)
            {
                if (current >= nodeCount || depth >= nodeCount)
                    return false;
                const tools::ModelImportNode& node = manifest.nodes[current];
                if (ancestryVisits >= MaximumModelEvidenceAncestryVisits ||
                    node.sourceName.Length() > MaximumModelEvidenceAncestryStringBytes - ancestryStringBytes)
                    return false;
                ++ancestryVisits;
                ancestryStringBytes += node.sourceName.Length();
                // Preserve the existing leaf-to-root evidence byte stream exactly while
                // computing both consumers in one ancestry walk per node.
                if ((geometryEvidenceRequired && !HashString(geometryPlacementHash, node.sourceName)) ||
                    (includeCompositionEvidence && !HashString(compositionStructuralHash, node.sourceName)))
                    return false;
                current = node.parentIndex;
                ++depth;
            }
            if (includeCompositionEvidence)
                output.nodeRelationshipDepths[nodeIndex] = depth;

            if (geometryEvidenceRequired)
            {
                crypto::Sha256Builder relationshipPlacementHash;
                if (!HashU32(geometryPlacementHash, depth) ||
                    !FinalizeEvidenceHash(geometryPlacementHash, output.geometryPlacementKeys[geometrySlot]) ||
                    !HashBytes(relationshipPlacementHash, GeometryRelationshipPlacementEvidenceDomain,
                               sizeof(GeometryRelationshipPlacementEvidenceDomain) - 1u) ||
                    !HashU32(relationshipPlacementHash, depth) ||
                    !FinalizeEvidenceHash(relationshipPlacementHash, output.geometryRelationshipPlacementKeys[geometrySlot]))
                    return false;
            }
            if (includeCompositionEvidence &&
                (!HashU32(compositionStructuralHash, depth) ||
                 !FinalizeEvidenceHash(compositionStructuralHash, output.compositionStructuralKeys[nodeIndex])))
                return false;
        }
        return true;
    }

    void CopyBoundedEvidenceName(const containers::String& source, containers::String& destination) noexcept
    {
        destination.Clear();
        if (source.Length() <= MaximumModelSettingsStringBytes)
            destination = source;
    }

    [[nodiscard]] bool BuildGeometryEvidence(const tools::ModelImportManifest& manifest, const ModelPlanEvidenceIndex& index,
                                             const u32 geometryIndex,
                                             tools::ModelImportSourceEvidence& evidence) noexcept
    {
        if (geometryIndex >= manifest.geometryDefinitions.Size())
            return false;
        const tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions[geometryIndex];
        evidence = {};
        evidence.sourceKind = geometry.sourceKind;
        evidence.nativeKey = geometry.sourceGeometryKey;
        evidence.nativeKeyStable = geometry.sourceKind == tools::ModelImportSourceKind::Fbx && geometry.sourceGeometryKey != 0u;
        CopyBoundedEvidenceName(geometry.displayName, evidence.sourceName);
        if (index.geometryEvidence.Size() == manifest.geometryDefinitions.Size())
        {
            evidence.structuralKey = index.geometryEvidence[geometryIndex].structural;
            evidence.relationshipKey = index.geometryEvidence[geometryIndex].relationship;
            return true;
        }

        crypto::Sha256Builder hash;
        crypto::Sha256Builder relationshipHash;
        constexpr char Domain[] = "vanguard.model.geometry.evidence.v1";
        constexpr char RelationshipDomain[] = "vanguard.model.geometry.relationship.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) || !HashU8(hash, static_cast<u8>(geometry.sourceKind)) ||
            !HashU32(hash, geometry.primitiveIndices.Size()) ||
            !HashBytes(relationshipHash, RelationshipDomain, sizeof(RelationshipDomain) - 1u) ||
            !HashU8(relationshipHash, static_cast<u8>(geometry.sourceKind)) ||
            !HashU32(relationshipHash, geometry.primitiveIndices.Size()))
            return false;
        if (geometryIndex + 1u >= index.geometryInstanceOffsets.Size())
            return false;
        const u32 instanceBegin = index.geometryInstanceOffsets[geometryIndex];
        const u32 instanceEnd = index.geometryInstanceOffsets[geometryIndex + 1u];
        if (instanceBegin > instanceEnd || instanceEnd > index.geometryInstanceIndices.Size())
            return false;
        const u32 useCount = instanceEnd - instanceBegin;
        containers::DynamicArray<crypto::Digest256> primitiveKeys(memory::pools::Assets::GetInstance());
        primitiveKeys.Reserve(geometry.primitiveIndices.Size());
        for (const u32 primitiveIndex : geometry.primitiveIndices)
        {
            if (primitiveIndex >= manifest.primitives.Size())
                return false;
            const tools::ModelImportPrimitive& primitive = manifest.primitives[primitiveIndex];
            crypto::Sha256Builder primitiveHash;
            crypto::Digest256 primitiveKey;
            constexpr char PrimitiveDomain[] = "vanguard.model.geometry.primitive.v1";
            if (!HashBytes(primitiveHash, PrimitiveDomain, sizeof(PrimitiveDomain) - 1u) ||
                !HashPrimitiveSchema(primitiveHash, primitive))
                return false;
            // An unused definition has no placement relationship to prove correspondence,
            // so keep coarse topology counts as additional evidence rather than relying on
            // its transient source table position or display name.
            if (useCount == 0u &&
                (!HashU32(primitiveHash, primitive.vertexCount) || !HashU32(primitiveHash, primitive.indices.Size())))
                return false;
            if (!FinalizeEvidenceHash(primitiveHash, primitiveKey))
                return false;
            const u32 previous = primitiveKeys.Size();
            primitiveKeys.PushBack(primitiveKey);
            if (primitiveKeys.Size() != previous + 1u)
                return false;
        }
        if (primitiveKeys.Size() > 1u)
            std::sort(primitiveKeys.TypedData(), primitiveKeys.TypedData() + primitiveKeys.Size(), [](const crypto::Digest256& left,
                                                                                                     const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
        for (const crypto::Digest256& primitiveKey : primitiveKeys)
            if (!HashBytes(hash, primitiveKey.bytes, crypto::Digest256::ByteCount) ||
                !HashBytes(relationshipHash, primitiveKey.bytes, crypto::Digest256::ByteCount))
                return false;
        if (!HashU32(hash, useCount) || !HashU32(relationshipHash, useCount))
            return false;
        containers::DynamicArray<crypto::Digest256> placementKeys(memory::pools::Assets::GetInstance());
        containers::DynamicArray<crypto::Digest256> relationshipPlacementKeys(memory::pools::Assets::GetInstance());
        placementKeys.Resize(useCount);
        relationshipPlacementKeys.Resize(useCount);
        if (placementKeys.Size() != useCount || relationshipPlacementKeys.Size() != useCount)
            return false;
        for (u32 offset = 0u; offset < useCount; ++offset)
        {
            const u32 instanceIndex = index.geometryInstanceIndices[instanceBegin + offset];
            if (instanceIndex >= manifest.geometryInstances.Size())
                return false;
            const tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[instanceIndex];
            if (instance.geometryDefinitionIndex != geometryIndex || instance.nodeIndex >= index.geometryNodeSlots.Size())
                return false;
            const u32 slot = index.geometryNodeSlots[instance.nodeIndex];
            if (slot == tools::InvalidModelImportIndex || slot >= index.geometryPlacementKeys.Size() ||
                slot >= index.geometryRelationshipPlacementKeys.Size())
                return false;
            placementKeys[offset] = index.geometryPlacementKeys[slot];
            relationshipPlacementKeys[offset] = index.geometryRelationshipPlacementKeys[slot];
        }
        if (placementKeys.Size() > 1u)
        {
            std::sort(placementKeys.TypedData(), placementKeys.TypedData() + placementKeys.Size(), [](const crypto::Digest256& left,
                                                                                                     const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
            std::sort(relationshipPlacementKeys.TypedData(), relationshipPlacementKeys.TypedData() + relationshipPlacementKeys.Size(),
                      [](const crypto::Digest256& left, const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
        }
        for (const crypto::Digest256& placementKey : placementKeys)
            if (!HashBytes(hash, placementKey.bytes, crypto::Digest256::ByteCount))
                return false;
        for (const crypto::Digest256& placementKey : relationshipPlacementKeys)
            if (!HashBytes(relationshipHash, placementKey.bytes, crypto::Digest256::ByteCount))
                return false;
        return FinalizeEvidenceHash(hash, evidence.structuralKey) &&
               FinalizeEvidenceHash(relationshipHash, evidence.relationshipKey);
    }

    [[nodiscard]] bool BuildMaterialEvidence(const tools::ModelImportManifest& manifest, const ModelPlanEvidenceIndex& index,
                                             const u32 materialIndex,
                                             tools::ModelImportSourceEvidence& evidence) noexcept
    {
        if (materialIndex >= manifest.materials.Size())
            return false;
        const tools::ModelImportMaterial& material = manifest.materials[materialIndex];
        evidence = {};
        evidence.sourceKind = tools::ModelImportSourceKind::Generic;
        CopyBoundedEvidenceName(material.name, evidence.sourceName);
        if (index.materialEvidence.Size() == manifest.materials.Size())
        {
            evidence.structuralKey = index.materialEvidence[materialIndex].structural;
            evidence.relationshipKey = index.materialEvidence[materialIndex].relationship;
            return true;
        }

        crypto::Sha256Builder hash;
        crypto::Sha256Builder relationshipHash;
        constexpr char Domain[] = "vanguard.model.material.evidence.v1";
        constexpr char RelationshipDomain[] = "vanguard.model.material.relationship.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) ||
            !HashBytes(relationshipHash, RelationshipDomain, sizeof(RelationshipDomain) - 1u))
            return false;
        containers::DynamicArray<crypto::Digest256> useKeys(memory::pools::Assets::GetInstance());
        containers::DynamicArray<crypto::Digest256> relationshipUseKeys(memory::pools::Assets::GetInstance());
        for (u32 geometryIndex = 0; geometryIndex < manifest.geometryDefinitions.Size(); ++geometryIndex)
        {
            const tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions[geometryIndex];
            for (const u32 primitiveIndex : geometry.primitiveIndices)
            {
                if (primitiveIndex >= manifest.primitives.Size())
                    return false;
                const tools::ModelImportPrimitive& primitive = manifest.primitives[primitiveIndex];
                if (primitive.sourceMaterialIndex != materialIndex)
                    continue;
                tools::ModelImportSourceEvidence geometryEvidence;
                crypto::Sha256Builder useHash;
                crypto::Sha256Builder relationshipUseHash;
                crypto::Digest256 useKey;
                crypto::Digest256 relationshipUseKey;
                constexpr char UseDomain[] = "vanguard.model.material.use.v1";
                constexpr char RelationshipUseDomain[] = "vanguard.model.material.use-relationship.v1";
                if (!BuildGeometryEvidence(manifest, index, geometryIndex, geometryEvidence) ||
                    !HashBytes(useHash, UseDomain, sizeof(UseDomain) - 1u) ||
                    !HashBytes(useHash, geometryEvidence.structuralKey.bytes, crypto::Digest256::ByteCount) ||
                    !HashPrimitiveSchema(useHash, primitive) || !FinalizeEvidenceHash(useHash, useKey) ||
                    !HashBytes(relationshipUseHash, RelationshipUseDomain, sizeof(RelationshipUseDomain) - 1u) ||
                    !HashBytes(relationshipUseHash, geometryEvidence.relationshipKey.bytes, crypto::Digest256::ByteCount) ||
                    !HashPrimitiveSchema(relationshipUseHash, primitive) ||
                    !FinalizeEvidenceHash(relationshipUseHash, relationshipUseKey))
                    return false;
            const u32 previous = useKeys.Size();
            useKeys.PushBack(useKey);
            relationshipUseKeys.PushBack(relationshipUseKey);
            if (useKeys.Size() != previous + 1u || relationshipUseKeys.Size() != previous + 1u)
                return false;
            }
        }
        if (useKeys.Size() > 1u)
        {
            std::sort(useKeys.TypedData(), useKeys.TypedData() + useKeys.Size(), [](const crypto::Digest256& left,
                                                                                   const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
            std::sort(relationshipUseKeys.TypedData(), relationshipUseKeys.TypedData() + relationshipUseKeys.Size(),
                      [](const crypto::Digest256& left, const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
        }
        if (!HashU32(hash, useKeys.Size()) || !HashU32(relationshipHash, relationshipUseKeys.Size()))
            return false;
        for (const crypto::Digest256& useKey : useKeys)
            if (!HashBytes(hash, useKey.bytes, crypto::Digest256::ByteCount))
                return false;
        for (const crypto::Digest256& useKey : relationshipUseKeys)
            if (!HashBytes(relationshipHash, useKey.bytes, crypto::Digest256::ByteCount))
                return false;
        return FinalizeEvidenceHash(hash, evidence.structuralKey) &&
               FinalizeEvidenceHash(relationshipHash, evidence.relationshipKey);
    }

    [[nodiscard]] bool BuildEmbeddedTextureEvidence(const tools::ModelImportManifest& manifest, const ModelPlanEvidenceIndex& index,
                                                    const u32 textureIndex,
                                                    tools::ModelImportSourceEvidence& evidence) noexcept
    {
        if (textureIndex >= manifest.embeddedTextures.Size())
            return false;
        const tools::ModelImportEmbeddedTexture& texture = manifest.embeddedTextures[textureIndex];
        evidence = {};
        evidence.sourceKind = tools::ModelImportSourceKind::Generic;
        CopyBoundedEvidenceName(texture.name, evidence.sourceName);
        if (index.embeddedTextureEvidence.Size() == manifest.embeddedTextures.Size())
        {
            evidence.structuralKey = index.embeddedTextureEvidence[textureIndex].structural;
            evidence.relationshipKey = index.embeddedTextureEvidence[textureIndex].relationship;
            return true;
        }

        crypto::Sha256Builder hash;
        crypto::Sha256Builder relationshipHash;
        constexpr char Domain[] = "vanguard.model.embedded-texture.evidence.v1";
        constexpr char RelationshipDomain[] = "vanguard.model.embedded-texture.relationship.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) ||
            !HashBytes(relationshipHash, RelationshipDomain, sizeof(RelationshipDomain) - 1u))
            return false;
        containers::DynamicArray<crypto::Digest256> useKeys(memory::pools::Assets::GetInstance());
        containers::DynamicArray<crypto::Digest256> relationshipUseKeys(memory::pools::Assets::GetInstance());
        for (u32 materialIndex = 0; materialIndex < manifest.materials.Size(); ++materialIndex)
        {
            const tools::ModelImportMaterial& material = manifest.materials[materialIndex];
            for (const u32 referenceIndex : material.textureReferenceIndices)
            {
                if (referenceIndex >= manifest.textureReferences.Size())
                    return false;
                const tools::ModelImportTextureReference& reference = manifest.textureReferences[referenceIndex];
                if (reference.storage != tools::ImportedTextureStorage::Embedded || reference.embeddedTextureIndex != textureIndex)
                    continue;
                tools::ModelImportSourceEvidence materialEvidence;
                crypto::Sha256Builder useHash;
                crypto::Sha256Builder relationshipUseHash;
                crypto::Digest256 useKey;
                crypto::Digest256 relationshipUseKey;
                constexpr char UseDomain[] = "vanguard.model.embedded-texture.use.v1";
                constexpr char RelationshipUseDomain[] = "vanguard.model.embedded-texture.use-relationship.v1";
                if (!BuildMaterialEvidence(manifest, index, materialIndex, materialEvidence) ||
                    !HashBytes(useHash, UseDomain, sizeof(UseDomain) - 1u) ||
                    !HashBytes(useHash, materialEvidence.structuralKey.bytes, crypto::Digest256::ByteCount) ||
                    !HashU8(useHash, static_cast<u8>(reference.semantic)) || !HashU32(useHash, reference.uvSet) ||
                    !FinalizeEvidenceHash(useHash, useKey) ||
                    !HashBytes(relationshipUseHash, RelationshipUseDomain, sizeof(RelationshipUseDomain) - 1u) ||
                    !HashBytes(relationshipUseHash, materialEvidence.relationshipKey.bytes, crypto::Digest256::ByteCount) ||
                    !HashU8(relationshipUseHash, static_cast<u8>(reference.semantic)) ||
                    !HashU32(relationshipUseHash, reference.uvSet) ||
                    !FinalizeEvidenceHash(relationshipUseHash, relationshipUseKey))
                    return false;
                const u32 previous = useKeys.Size();
                useKeys.PushBack(useKey);
                relationshipUseKeys.PushBack(relationshipUseKey);
                if (useKeys.Size() != previous + 1u || relationshipUseKeys.Size() != previous + 1u)
                    return false;
            }
        }
        if (useKeys.Size() > 1u)
        {
            std::sort(useKeys.TypedData(), useKeys.TypedData() + useKeys.Size(), [](const crypto::Digest256& left,
                                                                                   const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
            std::sort(relationshipUseKeys.TypedData(), relationshipUseKeys.TypedData() + relationshipUseKeys.Size(),
                      [](const crypto::Digest256& left, const crypto::Digest256& right) noexcept
            {
                return std::memcmp(left.bytes, right.bytes, crypto::Digest256::ByteCount) < 0;
            });
        }
        if (!HashU32(hash, useKeys.Size()) || !HashU32(relationshipHash, relationshipUseKeys.Size()))
            return false;
        for (const crypto::Digest256& useKey : useKeys)
            if (!HashBytes(hash, useKey.bytes, crypto::Digest256::ByteCount))
                return false;
        for (const crypto::Digest256& useKey : relationshipUseKeys)
            if (!HashBytes(relationshipHash, useKey.bytes, crypto::Digest256::ByteCount))
                return false;
        return FinalizeEvidenceHash(hash, evidence.structuralKey) &&
               FinalizeEvidenceHash(relationshipHash, evidence.relationshipKey);
    }

    struct ModelOwnedEvidenceDigest final
    {
        u32 owner = tools::InvalidModelImportIndex;
        crypto::Digest256 digest;
    };

    [[nodiscard]] bool AppendOwnedEvidenceDigest(containers::DynamicArray<ModelOwnedEvidenceDigest>& values,
                                                 const u32 owner, const crypto::Digest256& digest) noexcept
    {
        const u32 previous = values.Size();
        ModelOwnedEvidenceDigest& value = values.EmplaceBack();
        if (values.Size() != previous + 1u)
            return false;
        value.owner = owner;
        value.digest = digest;
        return true;
    }

    [[nodiscard]] bool BuildGroupedEvidenceDigests(containers::DynamicArray<ModelOwnedEvidenceDigest>& uses,
                                                   const u32 ownerCount, const char* const domain,
                                                   const u32 domainBytes,
                                                   containers::DynamicArray<crypto::Digest256>& output) noexcept
    {
        output.Resize(ownerCount);
        if (output.Size() != ownerCount)
            return false;

        containers::DynamicArray<u32> offsets(memory::pools::Assets::GetInstance());
        offsets.Resize(ownerCount + 1u);
        if (offsets.Size() != ownerCount + 1u)
            return false;
        offsets[0] = 0u;
        {
            containers::DynamicArray<u32> counts(memory::pools::Assets::GetInstance());
            counts.Resize(ownerCount);
            if (counts.Size() != ownerCount)
                return false;
            for (u32& count : counts)
                count = 0u;
            for (const ModelOwnedEvidenceDigest& use : uses)
            {
                if (use.owner >= ownerCount || counts[use.owner] == ~u32{0})
                    return false;
                ++counts[use.owner];
            }
            u64 total = 0u;
            for (u32 owner = 0u; owner < ownerCount; ++owner)
            {
                total += counts[owner];
                if (total > uses.Size())
                    return false;
                offsets[owner + 1u] = static_cast<u32>(total);
            }
            if (total != uses.Size())
                return false;
        }

        // Counting-bucket the tagged digests in place, then only sort within each
        // owner's exact range. This preserves the persisted per-owner digest stream
        // without paying a global owner+digest comparison sort.
        containers::DynamicArray<u32> next(memory::pools::Assets::GetInstance());
        next.Resize(ownerCount);
        if (next.Size() != ownerCount)
            return false;
        for (u32 owner = 0u; owner < ownerCount; ++owner)
            next[owner] = offsets[owner];
        for (u32 owner = 0u; owner < ownerCount; ++owner)
        {
            u32 cursor = offsets[owner];
            const u32 end = offsets[owner + 1u];
            while (cursor < end)
            {
                if (uses[cursor].owner == owner)
                {
                    ++cursor;
                    continue;
                }
                const u32 destinationOwner = uses[cursor].owner;
                if (destinationOwner >= ownerCount)
                    return false;
                u32& destination = next[destinationOwner];
                const u32 destinationEnd = offsets[destinationOwner + 1u];
                while (destination < destinationEnd && uses[destination].owner == destinationOwner)
                    ++destination;
                if (destination >= destinationEnd || destination == cursor)
                    return false;
                std::swap(uses[cursor], uses[destination]);
                ++destination;
            }
        }

        for (u32 owner = 0u; owner < ownerCount; ++owner)
        {
            const u32 begin = offsets[owner];
            const u32 end = offsets[owner + 1u];
            if (end - begin > 1u)
            {
                std::sort(uses.TypedData() + begin, uses.TypedData() + end,
                          [](const ModelOwnedEvidenceDigest& left, const ModelOwnedEvidenceDigest& right) noexcept
                {
                    return std::memcmp(left.digest.bytes, right.digest.bytes, crypto::Digest256::ByteCount) < 0;
                });
            }
            crypto::Sha256Builder hash;
            if (!HashBytes(hash, domain, domainBytes) || !HashU32(hash, end - begin))
                return false;
            for (u32 index = begin; index < end; ++index)
                if (!HashBytes(hash, uses[index].digest.bytes, crypto::Digest256::ByteCount))
                    return false;
            if (!FinalizeEvidenceHash(hash, output[owner]))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool PopulateModelPlanEvidenceCache(const tools::ModelImportManifest& manifest,
                                                      const bool includeMaterialEvidence,
                                                      const bool includeEmbeddedTextureEvidence,
                                                      ModelPlanEvidenceIndex& index) noexcept
    {
        const u32 geometryCount = manifest.geometryDefinitions.Size();
        if (index.geometryEvidence.Size() != geometryCount)
        {
            containers::DynamicArray<ModelEvidenceDigestPair> geometryEvidence(memory::pools::Assets::GetInstance());
            geometryEvidence.Resize(geometryCount);
            if (geometryEvidence.Size() != geometryCount)
                return false;
            for (u32 geometryIndex = 0u; geometryIndex < geometryCount; ++geometryIndex)
            {
                tools::ModelImportSourceEvidence evidence;
                if (!BuildGeometryEvidence(manifest, index, geometryIndex, evidence))
                    return false;
                geometryEvidence[geometryIndex].structural = evidence.structuralKey;
                geometryEvidence[geometryIndex].relationship = evidence.relationshipKey;
            }
            index.geometryEvidence = static_cast<containers::DynamicArray<ModelEvidenceDigestPair>&&>(geometryEvidence);
        }

        const bool needMaterialEvidence = includeMaterialEvidence || includeEmbeddedTextureEvidence;
        if (needMaterialEvidence && index.materialEvidence.Size() != manifest.materials.Size())
        {
            constexpr char UseDomain[] = "vanguard.model.material.use.v1";
            constexpr char RelationshipUseDomain[] = "vanguard.model.material.use-relationship.v1";
            u32 materialUseCount = 0u;
            for (const tools::ModelImportGeometryDefinition& geometry : manifest.geometryDefinitions)
            {
                for (const u32 primitiveIndex : geometry.primitiveIndices)
                {
                    if (primitiveIndex >= manifest.primitives.Size())
                        return false;
                    if (manifest.primitives[primitiveIndex].sourceMaterialIndex >= manifest.materials.Size())
                        continue;
                    if (materialUseCount == ~u32{0})
                        return false;
                    ++materialUseCount;
                }
            }
            const auto collectUses = [&](const bool relationship,
                                         containers::DynamicArray<ModelOwnedEvidenceDigest>& uses) noexcept
            {
                uses.Reserve(materialUseCount);
                for (u32 geometryIndex = 0u; geometryIndex < geometryCount; ++geometryIndex)
                {
                    const tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions[geometryIndex];
                    if (geometryIndex >= index.geometryEvidence.Size())
                        return false;
                    const ModelEvidenceDigestPair& geometryEvidence = index.geometryEvidence[geometryIndex];
                    for (const u32 primitiveIndex : geometry.primitiveIndices)
                    {
                        if (primitiveIndex >= manifest.primitives.Size())
                            return false;
                        const tools::ModelImportPrimitive& primitive = manifest.primitives[primitiveIndex];
                        if (primitive.sourceMaterialIndex >= manifest.materials.Size())
                            continue;

                        crypto::Sha256Builder useHash;
                        crypto::Digest256 useKey;
                        if (relationship)
                        {
                            if (!HashBytes(useHash, RelationshipUseDomain, sizeof(RelationshipUseDomain) - 1u) ||
                                !HashBytes(useHash, geometryEvidence.relationship.bytes, crypto::Digest256::ByteCount))
                                return false;
                        }
                        else if (!HashBytes(useHash, UseDomain, sizeof(UseDomain) - 1u) ||
                                 !HashBytes(useHash, geometryEvidence.structural.bytes, crypto::Digest256::ByteCount))
                            return false;
                        if (!HashPrimitiveSchema(useHash, primitive) || !FinalizeEvidenceHash(useHash, useKey) ||
                            !AppendOwnedEvidenceDigest(uses, primitive.sourceMaterialIndex, useKey))
                            return false;
                    }
                }
                return true;
            };

            containers::DynamicArray<crypto::Digest256> structural(memory::pools::Assets::GetInstance());
            containers::DynamicArray<crypto::Digest256> relationship(memory::pools::Assets::GetInstance());
            constexpr char Domain[] = "vanguard.model.material.evidence.v1";
            constexpr char RelationshipDomain[] = "vanguard.model.material.relationship.v1";
            {
                containers::DynamicArray<ModelOwnedEvidenceDigest> uses(memory::pools::Assets::GetInstance());
                if (!collectUses(false, uses) ||
                    !BuildGroupedEvidenceDigests(uses, manifest.materials.Size(), Domain, sizeof(Domain) - 1u, structural))
                    return false;
            }
            {
                containers::DynamicArray<ModelOwnedEvidenceDigest> uses(memory::pools::Assets::GetInstance());
                if (!collectUses(true, uses) ||
                    !BuildGroupedEvidenceDigests(uses, manifest.materials.Size(), RelationshipDomain,
                                                 sizeof(RelationshipDomain) - 1u, relationship))
                    return false;
            }
            index.materialEvidence.Resize(manifest.materials.Size());
            if (index.materialEvidence.Size() != manifest.materials.Size())
                return false;
            for (u32 materialIndex = 0u; materialIndex < manifest.materials.Size(); ++materialIndex)
            {
                index.materialEvidence[materialIndex].structural = structural[materialIndex];
                index.materialEvidence[materialIndex].relationship = relationship[materialIndex];
            }
        }

        if (includeEmbeddedTextureEvidence && index.embeddedTextureEvidence.Size() != manifest.embeddedTextures.Size())
        {
            constexpr char UseDomain[] = "vanguard.model.embedded-texture.use.v1";
            constexpr char RelationshipUseDomain[] = "vanguard.model.embedded-texture.use-relationship.v1";
            u32 embeddedUseCount = 0u;
            for (const tools::ModelImportMaterial& material : manifest.materials)
            {
                for (const u32 referenceIndex : material.textureReferenceIndices)
                {
                    if (referenceIndex >= manifest.textureReferences.Size())
                        return false;
                    const tools::ModelImportTextureReference& reference = manifest.textureReferences[referenceIndex];
                    if (reference.storage != tools::ImportedTextureStorage::Embedded ||
                        reference.embeddedTextureIndex >= manifest.embeddedTextures.Size())
                        continue;
                    if (embeddedUseCount == ~u32{0})
                        return false;
                    ++embeddedUseCount;
                }
            }
            const auto collectUses = [&](const bool relationship,
                                         containers::DynamicArray<ModelOwnedEvidenceDigest>& uses) noexcept
            {
                uses.Reserve(embeddedUseCount);
                for (u32 materialIndex = 0u; materialIndex < manifest.materials.Size(); ++materialIndex)
                {
                    if (materialIndex >= index.materialEvidence.Size())
                        return false;
                    const ModelEvidenceDigestPair& materialEvidence = index.materialEvidence[materialIndex];
                    const tools::ModelImportMaterial& material = manifest.materials[materialIndex];
                    for (const u32 referenceIndex : material.textureReferenceIndices)
                    {
                        if (referenceIndex >= manifest.textureReferences.Size())
                            return false;
                        const tools::ModelImportTextureReference& reference = manifest.textureReferences[referenceIndex];
                        if (reference.storage != tools::ImportedTextureStorage::Embedded ||
                            reference.embeddedTextureIndex >= manifest.embeddedTextures.Size())
                            continue;

                        crypto::Sha256Builder useHash;
                        crypto::Digest256 useKey;
                        if (relationship)
                        {
                            if (!HashBytes(useHash, RelationshipUseDomain, sizeof(RelationshipUseDomain) - 1u) ||
                                !HashBytes(useHash, materialEvidence.relationship.bytes, crypto::Digest256::ByteCount))
                                return false;
                        }
                        else if (!HashBytes(useHash, UseDomain, sizeof(UseDomain) - 1u) ||
                                 !HashBytes(useHash, materialEvidence.structural.bytes, crypto::Digest256::ByteCount))
                            return false;
                        if (!HashU8(useHash, static_cast<u8>(reference.semantic)) || !HashU32(useHash, reference.uvSet) ||
                            !FinalizeEvidenceHash(useHash, useKey) ||
                            !AppendOwnedEvidenceDigest(uses, reference.embeddedTextureIndex, useKey))
                            return false;
                    }
                }
                return true;
            };

            containers::DynamicArray<crypto::Digest256> structural(memory::pools::Assets::GetInstance());
            containers::DynamicArray<crypto::Digest256> relationship(memory::pools::Assets::GetInstance());
            constexpr char Domain[] = "vanguard.model.embedded-texture.evidence.v1";
            constexpr char RelationshipDomain[] = "vanguard.model.embedded-texture.relationship.v1";
            {
                containers::DynamicArray<ModelOwnedEvidenceDigest> uses(memory::pools::Assets::GetInstance());
                if (!collectUses(false, uses) ||
                    !BuildGroupedEvidenceDigests(uses, manifest.embeddedTextures.Size(), Domain, sizeof(Domain) - 1u, structural))
                    return false;
            }
            {
                containers::DynamicArray<ModelOwnedEvidenceDigest> uses(memory::pools::Assets::GetInstance());
                if (!collectUses(true, uses) ||
                    !BuildGroupedEvidenceDigests(uses, manifest.embeddedTextures.Size(), RelationshipDomain,
                                                 sizeof(RelationshipDomain) - 1u, relationship))
                    return false;
            }
            index.embeddedTextureEvidence.Resize(manifest.embeddedTextures.Size());
            if (index.embeddedTextureEvidence.Size() != manifest.embeddedTextures.Size())
                return false;
            for (u32 textureIndex = 0u; textureIndex < manifest.embeddedTextures.Size(); ++textureIndex)
            {
                index.embeddedTextureEvidence[textureIndex].structural = structural[textureIndex];
                index.embeddedTextureEvidence[textureIndex].relationship = relationship[textureIndex];
            }
        }
        return true;
    }

    struct ModelCurrentEvidenceRecord final
    {
        tools::ModelImportElementKind kind = tools::ModelImportElementKind::Geometry;
        u32 sourceIndex = tools::InvalidModelImportIndex;
        tools::ModelImportSourceEvidence evidence;
    };

    [[nodiscard]] int CompareSourceEvidence(const tools::ModelImportSourceEvidence& left,
                                            const tools::ModelImportSourceEvidence& right) noexcept
    {
        if (left.sourceKind != right.sourceKind)
            return static_cast<u8>(left.sourceKind) < static_cast<u8>(right.sourceKind) ? -1 : 1;
        if (left.nativeKey != right.nativeKey)
            return left.nativeKey < right.nativeKey ? -1 : 1;
        int comparison = std::memcmp(left.structuralKey.bytes, right.structuralKey.bytes, crypto::Digest256::ByteCount);
        if (comparison != 0)
            return comparison;
        comparison = std::memcmp(left.relationshipKey.bytes, right.relationshipKey.bytes, crypto::Digest256::ByteCount);
        if (comparison != 0)
            return comparison;
        const u32 commonNameBytes = std::min(left.sourceName.Length(), right.sourceName.Length());
        if (commonNameBytes != 0u)
        {
            comparison = std::memcmp(left.sourceName.Data(), right.sourceName.Data(), commonNameBytes);
            if (comparison != 0)
                return comparison;
        }
        if (left.sourceName.Length() != right.sourceName.Length())
            return left.sourceName.Length() < right.sourceName.Length() ? -1 : 1;
        if (left.nativeKeyStable != right.nativeKeyStable)
            return left.nativeKeyStable ? 1 : -1;
        return 0;
    }

    [[nodiscard]] int CompareCurrentEvidenceRecord(const ModelCurrentEvidenceRecord& left,
                                                   const tools::ModelImportElementKind kind,
                                                   const tools::ModelImportSourceEvidence& evidence) noexcept
    {
        if (left.kind != kind)
            return static_cast<u8>(left.kind) < static_cast<u8>(kind) ? -1 : 1;
        return CompareSourceEvidence(left.evidence, evidence);
    }

    [[nodiscard]] bool AppendCurrentEvidenceRecord(containers::DynamicArray<ModelCurrentEvidenceRecord>& records,
                                                   const tools::ModelImportElementKind kind, const u32 sourceIndex,
                                                   tools::ModelImportSourceEvidence&& evidence) noexcept
    {
        const u32 previous = records.Size();
        ModelCurrentEvidenceRecord& record = records.EmplaceBack();
        if (records.Size() != previous + 1u)
            return false;
        record.kind = kind;
        record.sourceIndex = sourceIndex;
        record.evidence = static_cast<tools::ModelImportSourceEvidence&&>(evidence);
        return true;
    }

    [[nodiscard]] bool BuildCurrentEvidenceRecords(const tools::ModelImportManifest& manifest,
                                                   const ModelPlanEvidenceIndex& evidenceIndex,
                                                   const bool includeMaterials, const bool includeEmbeddedTextures,
                                                   containers::DynamicArray<ModelCurrentEvidenceRecord>& records) noexcept
    {
        records.Clear();
        const u64 required = static_cast<u64>(manifest.geometryDefinitions.Size()) +
                             (includeMaterials ? manifest.materials.Size() : 0u) +
                             (includeEmbeddedTextures ? manifest.embeddedTextures.Size() : 0u);
        if (required > assets::MaximumAssetOutputs)
            return false;
        records.Reserve(static_cast<u32>(required));
        for (u32 index = 0u; index < manifest.geometryDefinitions.Size(); ++index)
        {
            tools::ModelImportSourceEvidence evidence;
            if (!BuildGeometryEvidence(manifest, evidenceIndex, index, evidence) ||
                !AppendCurrentEvidenceRecord(records, tools::ModelImportElementKind::Geometry, index,
                                             static_cast<tools::ModelImportSourceEvidence&&>(evidence)))
                return false;
        }
        if (includeMaterials)
        {
            for (u32 index = 0u; index < manifest.materials.Size(); ++index)
            {
                tools::ModelImportSourceEvidence evidence;
                if (!BuildMaterialEvidence(manifest, evidenceIndex, index, evidence) ||
                    !AppendCurrentEvidenceRecord(records, tools::ModelImportElementKind::Material, index,
                                                 static_cast<tools::ModelImportSourceEvidence&&>(evidence)))
                    return false;
            }
        }
        if (includeEmbeddedTextures)
        {
            for (u32 index = 0u; index < manifest.embeddedTextures.Size(); ++index)
            {
                tools::ModelImportSourceEvidence evidence;
                if (!BuildEmbeddedTextureEvidence(manifest, evidenceIndex, index, evidence) ||
                    !AppendCurrentEvidenceRecord(records, tools::ModelImportElementKind::EmbeddedTexture, index,
                                                 static_cast<tools::ModelImportSourceEvidence&&>(evidence)))
                    return false;
            }
        }
        if (records.Size() != required)
            return false;
        if (records.Size() > 1u)
        {
            std::sort(records.TypedData(), records.TypedData() + records.Size(),
                      [](const ModelCurrentEvidenceRecord& left, const ModelCurrentEvidenceRecord& right) noexcept
            {
                if (left.kind != right.kind)
                    return static_cast<u8>(left.kind) < static_cast<u8>(right.kind);
                return CompareSourceEvidence(left.evidence, right.evidence) < 0;
            });
        }
        return true;
    }

    [[nodiscard]] bool FindCurrentOutputSourceIndex(const containers::DynamicArray<ModelCurrentEvidenceRecord>& records,
                                                    const tools::ModelImportOutputCorrespondence& correspondence,
                                                    u32& sourceIndex) noexcept
    {
        sourceIndex = tools::InvalidModelImportIndex;
        const ModelCurrentEvidenceRecord* const begin = records.TypedData();
        const ModelCurrentEvidenceRecord* const end = begin + records.Size();
        const ModelCurrentEvidenceRecord* const found = std::lower_bound(
            begin, end, correspondence,
            [](const ModelCurrentEvidenceRecord& record, const tools::ModelImportOutputCorrespondence& expected) noexcept
            {
                return CompareCurrentEvidenceRecord(record, expected.kind, expected.source) < 0;
            });
        if (found == end || CompareCurrentEvidenceRecord(*found, correspondence.kind, correspondence.source) != 0)
            return false;
        const ModelCurrentEvidenceRecord* const next = found + 1;
        if (next != end && CompareCurrentEvidenceRecord(*next, correspondence.kind, correspondence.source) == 0)
            return false;
        sourceIndex = found->sourceIndex;
        return sourceIndex != tools::InvalidModelImportIndex;
    }

    [[nodiscard]] bool BuildModelOutputResolutionCache(tools::ModelAssetCompiler::Impl::CacheEntry& entry) noexcept
    {
        const tools::ModelImportManifest& manifest = entry.manifest;
        const tools::ModelImportSettings& settings = entry.settings;
        tools::ModelOutputResolutionCache& cache = entry.resolutionCache;

        u64 expectedOutputs = manifest.geometryDefinitions.Size();
        if (settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned)
            expectedOutputs += manifest.materials.Size();
        if (settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned)
            expectedOutputs += manifest.embeddedTextures.Size();
        if (settings.includeComposition)
            ++expectedOutputs;
        if (expectedOutputs == 0u || expectedOutputs > assets::MaximumAssetOutputs || expectedOutputs != settings.outputs.Size())
            return false;

        cache.sourceIndexByOutput.Resize(settings.outputs.Size());
        cache.geometryOutputIndexBySource.Resize(manifest.geometryDefinitions.Size());
        cache.materialOutputIndexBySource.Resize(
            settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned ? manifest.materials.Size() : 0u);
        cache.embeddedTextureOutputIndexBySource.Resize(
            settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned ? manifest.embeddedTextures.Size() : 0u);
        cache.outputLookup.Resize(settings.outputs.Size());
        if (cache.sourceIndexByOutput.Size() != settings.outputs.Size() ||
            cache.geometryOutputIndexBySource.Size() != manifest.geometryDefinitions.Size() ||
            cache.materialOutputIndexBySource.Size() !=
                (settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned ? manifest.materials.Size() : 0u) ||
            cache.embeddedTextureOutputIndexBySource.Size() !=
                (settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned ? manifest.embeddedTextures.Size() : 0u) ||
            cache.outputLookup.Size() != settings.outputs.Size())
            return false;
        for (u32& index : cache.sourceIndexByOutput)
            index = tools::InvalidModelImportIndex;
        for (u32& index : cache.geometryOutputIndexBySource)
            index = tools::InvalidModelImportIndex;
        for (u32& index : cache.materialOutputIndexBySource)
            index = tools::InvalidModelImportIndex;
        for (u32& index : cache.embeddedTextureOutputIndexBySource)
            index = tools::InvalidModelImportIndex;
        for (u32 outputIndex = 0u; outputIndex < settings.outputs.Size(); ++outputIndex)
        {
            const resources::ResourceReference reference = assets::GetAssetOutputReference(entry.assetId, settings.outputs[outputIndex].output);
            if (!reference.IsValid() || !reference.IsTyped())
                return false;
            cache.outputLookup[outputIndex] = {reference.GetPath(), reference.ExpectedType(), outputIndex};
        }
        if (cache.outputLookup.Size() > 1u)
        {
            std::sort(cache.outputLookup.TypedData(), cache.outputLookup.TypedData() + cache.outputLookup.Size(),
                      [](const tools::ModelOutputResolutionCache::OutputLookup& left,
                         const tools::ModelOutputResolutionCache::OutputLookup& right) noexcept
            {
                if (left.path != right.path)
                    return left.path < right.path;
                return left.type < right.type;
            });
            for (u32 index = 1u; index < cache.outputLookup.Size(); ++index)
                if (cache.outputLookup[index - 1u].path == cache.outputLookup[index].path &&
                    cache.outputLookup[index - 1u].type == cache.outputLookup[index].type)
                    return false;
        }

        ModelPlanEvidenceIndex evidenceIndex;
        if (!BuildModelPlanEvidenceIndex(manifest, false, evidenceIndex) ||
            !PopulateModelPlanEvidenceCache(
                manifest, settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned,
                settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned, evidenceIndex))
            return false;
        containers::DynamicArray<ModelCurrentEvidenceRecord> currentEvidence(memory::pools::Assets::GetInstance());
        if (!BuildCurrentEvidenceRecords(
                manifest, evidenceIndex, settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned,
                settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned, currentEvidence))
            return false;

        u32 compositionOutputs = 0u;
        constexpr u32 AmbiguousOutputIndex = tools::InvalidModelImportIndex - 1u;
        for (u32 outputIndex = 0u; outputIndex < settings.outputs.Size(); ++outputIndex)
        {
            const tools::ModelImportOutputCorrespondence& correspondence = settings.outputs[outputIndex];
            if (correspondence.kind == tools::ModelImportElementKind::Composition)
            {
                ++compositionOutputs;
                continue;
            }

            u32 sourceIndex = tools::InvalidModelImportIndex;
            if (!FindCurrentOutputSourceIndex(currentEvidence, correspondence, sourceIndex))
                continue;
            cache.sourceIndexByOutput[outputIndex] = sourceIndex;

            containers::DynamicArray<u32>* reverse = nullptr;
            switch (correspondence.kind)
            {
            case tools::ModelImportElementKind::Geometry: reverse = &cache.geometryOutputIndexBySource; break;
            case tools::ModelImportElementKind::Material: reverse = &cache.materialOutputIndexBySource; break;
            case tools::ModelImportElementKind::EmbeddedTexture: reverse = &cache.embeddedTextureOutputIndexBySource; break;
            default: return false;
            }
            if (reverse == nullptr || sourceIndex >= reverse->Size())
                return false;
            u32& reverseOutput = (*reverse)[sourceIndex];
            if (reverseOutput == tools::InvalidModelImportIndex)
                reverseOutput = outputIndex;
            else
                reverseOutput = AmbiguousOutputIndex;
        }

        if (compositionOutputs != (settings.includeComposition ? 1u : 0u))
            return false;
        const auto clearAmbiguous = [](containers::DynamicArray<u32>& reverse) noexcept
        {
            for (u32& outputIndex : reverse)
                if (outputIndex == AmbiguousOutputIndex)
                    outputIndex = tools::InvalidModelImportIndex;
        };
        clearAmbiguous(cache.geometryOutputIndexBySource);
        clearAmbiguous(cache.materialOutputIndexBySource);
        clearAmbiguous(cache.embeddedTextureOutputIndexBySource);
        return true;
    }

    [[nodiscard]] bool BuildCompositionEvidence(tools::ModelImportSourceEvidence& evidence) noexcept
    {
        evidence = {};
        evidence.sourceName = "composition";
        crypto::Sha256Builder hash;
        constexpr char Domain[] = "vanguard.model.composition.evidence.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) || !FinalizeEvidenceHash(hash, evidence.structuralKey))
            return false;
        evidence.relationshipKey = evidence.structuralKey;
        return true;
    }

    [[nodiscard]] bool AppendCurrentElement(containers::DynamicArray<ModelPlanCurrentElement>& elements,
                                            const tools::ModelImportElementKind kind, const u32 sourceIndex,
                                            const resources::ResourceTypeId outputType,
                                            const tools::ModelImportSourceEvidence& source,
                                            const containers::String& displayName,
                                            const u32 maximumOutputs) noexcept
    {
        if (elements.Size() >= maximumOutputs)
            return false;
        const u32 previous = elements.Size();
        ModelPlanCurrentElement& element = elements.EmplaceBack();
        if (elements.Size() != previous + 1u)
            return false;
        element.kind = kind;
        element.sourceIndex = sourceIndex;
        element.outputType = outputType;
        element.source = source;
        element.displayName = displayName;
        return true;
    }

    [[nodiscard]] bool BuildCurrentElements(const tools::ModelImportManifest& manifest,
                                            const ModelPlanEvidenceIndex& evidenceIndex,
                                            const tools::ModelImportSettings& settings,
                                            const tools::ModelImportPlanningContext& context,
                                            containers::DynamicArray<ModelPlanCurrentElement>& elements) noexcept
    {
        if (context.outputTypes.mesh == resources::InvalidResourceTypeId)
            return false;
        for (u32 index = 0; index < manifest.geometryDefinitions.Size(); ++index)
        {
            tools::ModelImportSourceEvidence evidence;
            if (!BuildGeometryEvidence(manifest, evidenceIndex, index, evidence) ||
                !AppendCurrentElement(elements, tools::ModelImportElementKind::Geometry, index, context.outputTypes.mesh,
                                      evidence, manifest.geometryDefinitions[index].displayName, context.limits.maximumOutputs))
                return false;
        }
        if (settings.materialPolicy == tools::ModelImportMaterialPolicy::ImportModelOwned)
        {
            if (!manifest.materials.Empty() && context.outputTypes.material == resources::InvalidResourceTypeId)
                return false;
            for (u32 index = 0; index < manifest.materials.Size(); ++index)
            {
                tools::ModelImportSourceEvidence evidence;
                if (!BuildMaterialEvidence(manifest, evidenceIndex, index, evidence) ||
                    !AppendCurrentElement(elements, tools::ModelImportElementKind::Material, index, context.outputTypes.material,
                                          evidence, manifest.materials[index].name, context.limits.maximumOutputs))
                    return false;
            }
        }
        if (settings.embeddedTexturePolicy == tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned)
        {
            if (!manifest.embeddedTextures.Empty() && context.outputTypes.texture == resources::InvalidResourceTypeId)
                return false;
            for (u32 index = 0; index < manifest.embeddedTextures.Size(); ++index)
            {
                tools::ModelImportSourceEvidence evidence;
                if (!BuildEmbeddedTextureEvidence(manifest, evidenceIndex, index, evidence) ||
                    !AppendCurrentElement(elements, tools::ModelImportElementKind::EmbeddedTexture, index, context.outputTypes.texture,
                                          evidence, manifest.embeddedTextures[index].name, context.limits.maximumOutputs))
                    return false;
            }
        }
        if (settings.includeComposition)
        {
            tools::ModelImportSourceEvidence evidence;
            containers::String name("composition");
            if (context.outputTypes.composition == resources::InvalidResourceTypeId || !BuildCompositionEvidence(evidence) ||
                !AppendCurrentElement(elements, tools::ModelImportElementKind::Composition, 0u, context.outputTypes.composition,
                                      evidence, name, context.limits.maximumOutputs))
                return false;
        }
        return !elements.Empty();
    }

    [[nodiscard]] bool SameEvidenceName(const tools::ModelImportSourceEvidence& left,
                                        const tools::ModelImportSourceEvidence& right) noexcept
    {
        return !left.sourceName.Empty() && left.sourceName == right.sourceName;
    }

    [[nodiscard]] bool AddUniqueMatch(containers::DynamicArray<u32>& matches, const u32 index) noexcept
    {
        for (const u32 existing : matches)
            if (existing == index)
                return true;
        const u32 previous = matches.Size();
        matches.PushBack(index);
        return matches.Size() == previous + 1u;
    }

    enum class ModelEvidenceMatchTier : u8
    {
        Native,
        Structural,
        Relationship
    };

    [[nodiscard]] bool FindTierMatches(const tools::ModelImportOutputCorrespondence& old,
                                       const containers::DynamicArray<ModelPlanCurrentElement>& current,
                                       const containers::DynamicArray<u8>& unavailable,
                                       const ModelEvidenceMatchTier tier,
                                       containers::DynamicArray<u32>& matches) noexcept
    {
        matches.Clear();
        if (unavailable.Size() != current.Size())
            return false;

        if (tier == ModelEvidenceMatchTier::Native)
        {
            if (!old.source.nativeKeyStable || old.source.nativeKey == 0u)
                return true;
            for (u32 index = 0; index < current.Size(); ++index)
            {
                if (unavailable[index] != 0u)
                    continue;
                const ModelPlanCurrentElement& candidate = current[index];
                if (candidate.kind == old.kind && candidate.outputType == old.output.type && candidate.source.nativeKeyStable &&
                    candidate.source.sourceKind == old.source.sourceKind && candidate.source.nativeKey == old.source.nativeKey &&
                    !AddUniqueMatch(matches, index))
                    return false;
            }
            return true;
        }

        if (tier == ModelEvidenceMatchTier::Structural)
        {
            if (old.source.structuralKey.IsEmpty())
                return true;
            for (u32 index = 0; index < current.Size(); ++index)
            {
                if (unavailable[index] != 0u)
                    continue;
                const ModelPlanCurrentElement& candidate = current[index];
                if (candidate.kind == old.kind && candidate.outputType == old.output.type &&
                    candidate.source.structuralKey == old.source.structuralKey && !AddUniqueMatch(matches, index))
                    return false;
            }
            if (matches.Size() > 1u && !old.source.sourceName.Empty())
            {
                containers::DynamicArray<u32> named(memory::pools::Assets::GetInstance());
                for (const u32 index : matches)
                    if (SameEvidenceName(old.source, current[index].source) && !AddUniqueMatch(named, index))
                        return false;
                if (!named.Empty())
                    matches = static_cast<containers::DynamicArray<u32>&&>(named);
            }
            return true;
        }

        if (old.source.relationshipKey.IsEmpty())
            return true;
        for (u32 index = 0; index < current.Size(); ++index)
        {
            if (unavailable[index] != 0u)
                continue;
            const ModelPlanCurrentElement& candidate = current[index];
            if (candidate.kind == old.kind && candidate.outputType == old.output.type &&
                candidate.source.relationshipKey == old.source.relationshipKey && !AddUniqueMatch(matches, index))
                return false;
        }
        return true;
    }

    [[nodiscard]] const char* OutputPrefix(const tools::ModelImportElementKind kind) noexcept
    {
        switch (kind)
        {
        case tools::ModelImportElementKind::Geometry: return "mesh";
        case tools::ModelImportElementKind::Material: return "mat";
        case tools::ModelImportElementKind::EmbeddedTexture: return "tex";
        case tools::ModelImportElementKind::Composition: return "composition";
        default: return "output";
        }
    }

    [[nodiscard]] bool MakeOutputKey(const tools::ModelImportElementKind kind, const u64 id, containers::String& key) noexcept
    {
        if (id == 0u || kind > tools::ModelImportElementKind::Composition)
            return false;
        key = containers::String::Printf("%s-%016llx", OutputPrefix(kind), static_cast<unsigned long long>(id));
        return assets::IsValidAssetOutputKey(key);
    }

    [[nodiscard]] bool AppendRetiredOutput(tools::ModelImportSettings& settings,
                                           const tools::ModelImportOutputCorrespondence& old) noexcept
    {
        for (const tools::ModelImportRetiredOutput& existing : settings.retiredOutputs)
            if (existing.persistentId == old.persistentId || existing.output.key == old.output.key)
                return existing.persistentId == old.persistentId && existing.output.key == old.output.key && existing.output.type == old.output.type;
        if (settings.retiredOutputs.Size() >= assets::MaximumAssetOutputs)
            return false;
        const u32 previous = settings.retiredOutputs.Size();
        tools::ModelImportRetiredOutput& retired = settings.retiredOutputs.EmplaceBack();
        if (settings.retiredOutputs.Size() != previous + 1u)
            return false;
        retired.kind = old.kind;
        retired.persistentId = old.persistentId;
        retired.output.key = old.output.key;
        retired.output.type = old.output.type;
        return true;
    }

    [[nodiscard]] bool AppendRetiredSlot(tools::ModelImportSettings& settings, const u64 stableSlotId) noexcept
    {
        for (const tools::ModelImportRetiredMaterialSlot& existing : settings.retiredMaterialSlots)
            if (existing.stableSlotId == stableSlotId)
                return true;
        const u32 previous = settings.retiredMaterialSlots.Size();
        settings.retiredMaterialSlots.PushBack({stableSlotId});
        return settings.retiredMaterialSlots.Size() == previous + 1u;
    }

    [[nodiscard]] bool AppendCorrespondence(tools::ModelImportSettings& settings,
                                            const tools::ModelImportElementKind kind, const u64 persistentId,
                                            const assets::AssetOutput& output,
                                            const tools::ModelImportSourceEvidence& source) noexcept
    {
        if (settings.outputs.Size() >= assets::MaximumAssetOutputs)
            return false;
        const u32 previous = settings.outputs.Size();
        tools::ModelImportOutputCorrespondence& value = settings.outputs.EmplaceBack();
        if (settings.outputs.Size() != previous + 1u)
            return false;
        value.kind = kind;
        value.persistentId = persistentId;
        value.output.key = output.key;
        value.output.type = output.type;
        value.source = source;
        return true;
    }

    [[nodiscard]] bool AppendPlannedOutput(tools::ModelImportPlan& plan, const ModelPlanCurrentElement& current,
                                           const u64 persistentId, const assets::AssetOutput& output,
                                           const u32 maximumOutputs) noexcept
    {
        containers::DynamicArray<tools::ModelImportPlannedOutput>& outputs = tools::ModelImportPlanBuilderAccess::Outputs(plan);
        if (outputs.Size() >= maximumOutputs)
            return false;
        const u32 previous = outputs.Size();
        tools::ModelImportPlannedOutput& value = outputs.EmplaceBack();
        if (outputs.Size() != previous + 1u)
            return false;
        value.kind = current.kind;
        value.persistentId = persistentId;
        value.output.key = output.key;
        value.output.type = output.type;
        value.sourceIndex = current.sourceIndex;
        value.displayName = current.displayName;
        return true;
    }

    [[nodiscard]] bool AppendReconciliation(tools::ModelImportPlan& plan,
                                            const tools::ModelImportReconcileState state,
                                            const tools::ModelImportOutputCorrespondence* const old,
                                            const ModelPlanCurrentElement* const current,
                                            const assets::AssetOutput* const proposed,
                                            const u64 persistentId,
                                            const u32 maximumRecords,
                                            u32& outputIndex) noexcept
    {
        containers::DynamicArray<tools::ModelImportReconciliationRecord>& reconciliation =
            tools::ModelImportPlanBuilderAccess::Reconciliation(plan);
        if (reconciliation.Size() >= maximumRecords)
            return false;
        outputIndex = reconciliation.Size();
        tools::ModelImportReconciliationRecord& record = reconciliation.EmplaceBack();
        if (reconciliation.Size() != outputIndex + 1u)
            return false;
        record.state = state;
        record.kind = old != nullptr ? old->kind : current->kind;
        record.persistentId = persistentId;
        if (old != nullptr)
        {
            record.previousOutput.key = old->output.key;
            record.previousOutput.type = old->output.type;
            record.previousSource = old->source;
        }
        if (current != nullptr)
        {
            record.currentSource = current->source;
            record.sourceIndex = current->sourceIndex;
        }
        if (proposed != nullptr)
        {
            record.proposedOutput.key = proposed->key;
            record.proposedOutput.type = proposed->type;
        }
        return true;
    }

    [[nodiscard]] bool AppendBlocker(tools::ModelImportPlan& plan, const tools::ModelImportPlanBlocker& blocker,
                                     const u32 maximumBlockers) noexcept
    {
        containers::DynamicArray<tools::ModelImportPlanBlocker>& blockers = tools::ModelImportPlanBuilderAccess::Blockers(plan);
        if (blockers.Size() >= maximumBlockers)
            return false;
        const u32 previous = blockers.Size();
        blockers.PushBack(blocker);
        return blockers.Size() == previous + 1u;
    }

    [[nodiscard]] bool SettingsOutputsMatchMetadata(const tools::ModelImportSettings& settings,
                                                    const assets::AssetMetadata& metadata) noexcept
    {
        if (settings.outputs.Size() != metadata.outputs.Size())
            return false;
        for (const tools::ModelImportOutputCorrespondence& correspondence : settings.outputs)
        {
            bool found = false;
            for (const assets::AssetOutput& output : metadata.outputs)
                found = found || SameOutputIdentity(correspondence.output, output);
            if (!found)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool CloneSettings(const tools::ModelImportSettings& source, tools::ModelImportSettings& destination,
                                     containers::DynamicArray<u8>& canonicalBytes) noexcept
    {
        canonicalBytes.Clear();
        if (tools::EncodeModelImportSettings(source, canonicalBytes) != tools::ModelImportSettingsResult::Success)
            return false;
        return tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion,
                                                {canonicalBytes.TypedData(), canonicalBytes.Size()}, destination) ==
               tools::ModelImportSettingsResult::Success;
    }

    [[nodiscard]] bool SameSourceEvidence(const tools::ModelImportSourceEvidence& left,
                                          const tools::ModelImportSourceEvidence& right) noexcept
    {
        return left.sourceKind == right.sourceKind && left.nativeKey == right.nativeKey &&
               left.structuralKey == right.structuralKey && left.relationshipKey == right.relationshipKey &&
               left.sourceName == right.sourceName &&
               left.nativeKeyStable == right.nativeKeyStable;
    }

    // Call only for settings decoded from the canonical wire form.
    [[nodiscard]] bool SamePersistentIdentityState(const tools::ModelImportSettings& left,
                                                   const tools::ModelImportSettings& right) noexcept
    {
        if (left.nextOutputId != right.nextOutputId || left.nextMaterialSlotId != right.nextMaterialSlotId ||
            left.nextCompositionNodeId != right.nextCompositionNodeId ||
            left.outputs.Size() != right.outputs.Size() || left.retiredOutputs.Size() != right.retiredOutputs.Size() ||
            left.materialSlots.Size() != right.materialSlots.Size() ||
            left.retiredMaterialSlots.Size() != right.retiredMaterialSlots.Size() ||
            left.compositionNodes.Size() != right.compositionNodes.Size() ||
            left.compositionMeshes.Size() != right.compositionMeshes.Size())
            return false;
        for (u32 index = 0; index < left.outputs.Size(); ++index)
        {
            const tools::ModelImportOutputCorrespondence& a = left.outputs[index];
            const tools::ModelImportOutputCorrespondence& b = right.outputs[index];
            if (a.kind != b.kind || a.persistentId != b.persistentId || !SameOutputIdentity(a.output, b.output) ||
                !SameSourceEvidence(a.source, b.source))
                return false;
        }
        for (u32 index = 0; index < left.retiredOutputs.Size(); ++index)
        {
            const tools::ModelImportRetiredOutput& a = left.retiredOutputs[index];
            const tools::ModelImportRetiredOutput& b = right.retiredOutputs[index];
            if (a.kind != b.kind || a.persistentId != b.persistentId || !SameOutputIdentity(a.output, b.output))
                return false;
        }
        for (u32 index = 0; index < left.materialSlots.Size(); ++index)
        {
            const tools::ModelImportMaterialSlotCorrespondence& a = left.materialSlots[index];
            const tools::ModelImportMaterialSlotCorrespondence& b = right.materialSlots[index];
            if (a.stableSlotId != b.stableSlotId || a.geometryPersistentId != b.geometryPersistentId ||
                a.sourcePartitionOrdinal != b.sourcePartitionOrdinal || a.materialPersistentId != b.materialPersistentId ||
                a.structuralKey != b.structuralKey || a.partitionKey != b.partitionKey)
                return false;
        }
        for (u32 index = 0; index < left.retiredMaterialSlots.Size(); ++index)
            if (left.retiredMaterialSlots[index].stableSlotId != right.retiredMaterialSlots[index].stableSlotId)
                return false;
        for (u32 index = 0; index < left.compositionNodes.Size(); ++index)
        {
            const tools::ModelImportCompositionNodeCorrespondence& a = left.compositionNodes[index];
            const tools::ModelImportCompositionNodeCorrespondence& b = right.compositionNodes[index];
            if (a.stableNodeId != b.stableNodeId || a.parentStableNodeId != b.parentStableNodeId ||
                a.sourceNodeIndex != b.sourceNodeIndex || !SameSourceEvidence(a.source, b.source))
                return false;
        }
        for (u32 index = 0; index < left.compositionMeshes.Size(); ++index)
        {
            const tools::ModelImportCompositionMeshCorrespondence& a = left.compositionMeshes[index];
            const tools::ModelImportCompositionMeshCorrespondence& b = right.compositionMeshes[index];
            if (a.stableComponentId != b.stableComponentId || a.stableNodeId != b.stableNodeId ||
                a.geometryPersistentId != b.geometryPersistentId || a.sourceInstanceIndex != b.sourceInstanceIndex ||
                a.sameGeometryOrdinal != b.sameGeometryOrdinal)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool SameMetadataSnapshot(const assets::AssetMetadata& left,
                                            const assets::AssetMetadata& right) noexcept
    {
        if (!(left.id == right.id) || left.importer != right.importer || left.importerVersion != right.importerVersion ||
            left.settingsVersion != right.settingsVersion || left.settings.Size() != right.settings.Size() ||
            left.outputs.Size() != right.outputs.Size())
            return false;
        if (!left.settings.Empty() && std::memcmp(left.settings.TypedData(), right.settings.TypedData(), left.settings.Size()) != 0)
            return false;
        for (u32 index = 0; index < left.outputs.Size(); ++index)
            if (!SameOutputIdentity(left.outputs[index], right.outputs[index]))
                return false;
        return true;
    }

    [[nodiscard]] bool MetadataDigestMatchesSnapshot(const assets::AssetMetadata& metadata,
                                                     const crypto::Digest256& expected) noexcept
    {
        if (expected.IsEmpty())
            return false;
        containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        return crypto::Sha256(encoded.AsChar(), encoded.Length()) == expected;
    }

    [[nodiscard]] const ModelPlanCurrentElement* FindCurrentElement(const containers::DynamicArray<ModelPlanCurrentElement>& current,
                                                                   const tools::ModelImportElementKind kind,
                                                                   const u32 sourceIndex) noexcept
    {
        for (const ModelPlanCurrentElement& element : current)
            if (element.kind == kind && element.sourceIndex == sourceIndex)
                return &element;
        return nullptr;
    }

    [[nodiscard]] u64 CurrentPersistentId(const containers::DynamicArray<ModelPlanCurrentElement>& current,
                                          const containers::DynamicArray<u64>& ids,
                                          const tools::ModelImportElementKind kind,
                                          const u32 sourceIndex) noexcept
    {
        for (u32 index = 0; index < current.Size(); ++index)
            if (current[index].kind == kind && current[index].sourceIndex == sourceIndex)
                return index < ids.Size() ? ids[index] : 0u;
        return 0u;
    }

    [[nodiscard]] bool BuildCurrentGeometryPersistentIds(const tools::ModelImportManifest& manifest,
                                                         const containers::DynamicArray<ModelPlanCurrentElement>& current,
                                                         const containers::DynamicArray<u64>& currentPersistentIds,
                                                         containers::DynamicArray<u64>& geometryPersistentIds) noexcept
    {
        geometryPersistentIds.Resize(manifest.geometryDefinitions.Size());
        if (geometryPersistentIds.Size() != manifest.geometryDefinitions.Size() || currentPersistentIds.Size() != current.Size())
            return false;
        for (u32 index = 0; index < geometryPersistentIds.Size(); ++index)
            geometryPersistentIds[index] = 0u;
        for (u32 index = 0; index < current.Size(); ++index)
        {
            if (current[index].kind != tools::ModelImportElementKind::Geometry)
                continue;
            if (current[index].sourceIndex >= geometryPersistentIds.Size() || currentPersistentIds[index] == 0u ||
                geometryPersistentIds[current[index].sourceIndex] != 0u)
                return false;
            geometryPersistentIds[current[index].sourceIndex] = currentPersistentIds[index];
        }
        for (const tools::ModelImportGeometryInstance& instance : manifest.geometryInstances)
            if (instance.geometryDefinitionIndex >= geometryPersistentIds.Size() || geometryPersistentIds[instance.geometryDefinitionIndex] == 0u)
                return false;
        return true;
    }

    [[nodiscard]] bool BuildCompositionNodeEvidence(const tools::ModelImportManifest& manifest, const ModelPlanEvidenceIndex& index,
                                                    const u32 nodeIndex,
                                                    const containers::DynamicArray<u64>& geometryPersistentIds,
                                                    const u32 childCount,
                                                    tools::ModelImportSourceEvidence& evidence) noexcept
    {
        if (nodeIndex >= manifest.nodes.Size() || nodeIndex >= index.compositionStructuralKeys.Size() ||
            nodeIndex >= index.nodeRelationshipDepths.Size())
            return false;
        const tools::ModelImportNode& node = manifest.nodes[nodeIndex];
        evidence = {};
        evidence.sourceKind = tools::ModelImportSourceKind::Generic;
        CopyBoundedEvidenceName(node.sourceName, evidence.sourceName);

        crypto::Sha256Builder relationship;
        evidence.structuralKey = index.compositionStructuralKeys[nodeIndex];
        if (evidence.structuralKey.IsEmpty() ||
            !HashBytes(relationship, CompositionNodeRelationshipEvidenceDomain,
                       sizeof(CompositionNodeRelationshipEvidenceDomain) - 1u) ||
            !HashU32(relationship, index.nodeRelationshipDepths[nodeIndex]))
            return false;

        containers::DynamicArray<u64> geometries(memory::pools::Assets::GetInstance());
        geometries.Reserve(node.geometryInstanceIndices.Size());
        for (const u32 instanceIndex : node.geometryInstanceIndices)
        {
            if (instanceIndex >= manifest.geometryInstances.Size())
                return false;
            const tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[instanceIndex];
            if (instance.geometryDefinitionIndex >= geometryPersistentIds.Size())
                return false;
            const u64 persistentId = geometryPersistentIds[instance.geometryDefinitionIndex];
            if (persistentId == 0u)
                return false;
            geometries.PushBack(persistentId);
        }
        if (geometries.Size() != node.geometryInstanceIndices.Size())
            return false;
        if (geometries.Size() > 1u)
            std::sort(geometries.TypedData(), geometries.TypedData() + geometries.Size());

        // Structural identity is authored node ancestry only. Mesh payload and child-count
        // edits must not churn an otherwise stable component. The relationship key is the
        // weaker rename fallback, so it deliberately includes shape/payload evidence and is
        // only reused under bidirectional 1:1 matching.
        if (!HashU32(relationship, geometries.Size()) || !HashU32(relationship, childCount))
            return false;
        for (const u64 geometry : geometries)
            if (!HashU64(relationship, geometry))
                return false;
        return FinalizeEvidenceHash(relationship, evidence.relationshipKey);
    }

    [[nodiscard]] bool BuildNodeDepths(const tools::ModelImportManifest& manifest, containers::DynamicArray<u32>& depths) noexcept
    {
        depths.Resize(manifest.nodes.Size());
        if (depths.Size() != manifest.nodes.Size())
            return false;
        for (u32& depth : depths)
            depth = tools::InvalidModelImportIndex;
        containers::DynamicArray<u32> path(memory::pools::Assets::GetInstance());
        path.Reserve(manifest.nodes.Size());
        for (u32 start = 0; start < manifest.nodes.Size(); ++start)
        {
            if (depths[start] != tools::InvalidModelImportIndex)
                continue;
            path.Clear();
            u32 cursor = start;
            while (cursor != tools::InvalidModelImportIndex && depths[cursor] == tools::InvalidModelImportIndex)
            {
                if (cursor >= manifest.nodes.Size() || path.Size() >= manifest.nodes.Size())
                    return false;
                path.PushBack(cursor);
                cursor = manifest.nodes[cursor].parentIndex;
            }
            u32 depth = cursor == tools::InvalidModelImportIndex ? 0u : depths[cursor] + 1u;
            for (u32 offset = path.Size(); offset > 0; --offset)
                depths[path[offset - 1u]] = depth++;
        }
        return true;
    }

    struct CompositionStableNodeIndex final
    {
        u64 stableNodeId = 0u;
        u32 index = tools::InvalidModelImportIndex;
    };

    [[nodiscard]] bool CompositionStableNodeIndexLess(const CompositionStableNodeIndex& left,
                                                      const CompositionStableNodeIndex& right) noexcept
    {
        return left.stableNodeId < right.stableNodeId;
    }

    [[nodiscard]] u32 FindCompositionStableNodeIndex(const containers::DynamicArray<CompositionStableNodeIndex>& indices,
                                                     const u64 stableNodeId) noexcept
    {
        u32 begin = 0u;
        u32 end = indices.Size();
        while (begin < end)
        {
            const u32 middle = begin + (end - begin) / 2u;
            if (indices[middle].stableNodeId < stableNodeId)
                begin = middle + 1u;
            else
                end = middle;
        }
        return begin < indices.Size() && indices[begin].stableNodeId == stableNodeId ? indices[begin].index : tools::InvalidModelImportIndex;
    }

    [[nodiscard]] bool BuildPersistedCompositionNodeDepths(const tools::ModelImportSettings& settings,
                                                           containers::DynamicArray<u32>& depths) noexcept
    {
        depths.Resize(settings.compositionNodes.Size());
        containers::DynamicArray<u8> states(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> path(memory::pools::Assets::GetInstance());
        containers::DynamicArray<CompositionStableNodeIndex> indices(memory::pools::Assets::GetInstance());
        states.Resize(settings.compositionNodes.Size());
        path.Reserve(settings.compositionNodes.Size());
        indices.Resize(settings.compositionNodes.Size());
        if (depths.Size() != settings.compositionNodes.Size() || states.Size() != settings.compositionNodes.Size() ||
            path.Capacity() < settings.compositionNodes.Size() || indices.Size() != settings.compositionNodes.Size())
            return false;
        for (u32 index = 0; index < settings.compositionNodes.Size(); ++index)
        {
            depths[index] = tools::InvalidModelImportIndex;
            states[index] = 0u;
            indices[index] = {settings.compositionNodes[index].stableNodeId, index};
        }
        if (indices.Size() > 1u)
            std::sort(indices.TypedData(), indices.TypedData() + indices.Size(), CompositionStableNodeIndexLess);
        for (u32 index = 1; index < indices.Size(); ++index)
            if (indices[index - 1u].stableNodeId == indices[index].stableNodeId)
                return false;

        for (u32 start = 0; start < settings.compositionNodes.Size(); ++start)
        {
            if (states[start] == 2u)
                continue;
            path.Clear();
            u32 cursor = start;
            while (cursor != tools::InvalidModelImportIndex && states[cursor] == 0u)
            {
                states[cursor] = 1u;
                path.PushBack(cursor);
                const u64 parent = settings.compositionNodes[cursor].parentStableNodeId;
                cursor = parent == 0u ? tools::InvalidModelImportIndex : FindCompositionStableNodeIndex(indices, parent);
                if (parent != 0u && cursor == tools::InvalidModelImportIndex)
                    return false;
            }
            if (cursor != tools::InvalidModelImportIndex && states[cursor] == 1u)
                return false;
            u32 depth = cursor == tools::InvalidModelImportIndex ? 0u : depths[cursor] + 1u;
            for (u32 offset = path.Size(); offset > 0u; --offset)
            {
                const u32 index = path[offset - 1u];
                depths[index] = depth++;
                states[index] = 2u;
            }
        }
        return true;
    }

    [[nodiscard]] bool BuildCompositionDepthOrder(const containers::DynamicArray<u32>& depths,
                                                  containers::DynamicArray<u32>& order) noexcept
    {
        order.Resize(depths.Size());
        if (order.Size() != depths.Size())
            return false;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        if (order.Size() > 1u)
            std::sort(order.TypedData(), order.TypedData() + order.Size(), [&depths](const u32 left, const u32 right) noexcept
            {
                if (depths[left] != depths[right])
                    return depths[left] < depths[right];
                return left < right;
            });
        return true;
    }

    struct CompositionNodeMatchKey final
    {
        u64 parentStableNodeId = 0u;
        crypto::Digest256 key;
        u32 index = 0u;
        bool old = false;
    };

    [[nodiscard]] bool CompositionNodeMatchKeyLess(const CompositionNodeMatchKey& left, const CompositionNodeMatchKey& right) noexcept
    {
        if (left.parentStableNodeId != right.parentStableNodeId)
            return left.parentStableNodeId < right.parentStableNodeId;
        const int digest = std::memcmp(left.key.bytes, right.key.bytes, crypto::Digest256::ByteCount);
        if (digest != 0)
            return digest < 0;
        if (left.old != right.old)
            return left.old;
        return left.index < right.index;
    }

    [[nodiscard]] bool SameCompositionNodeMatchGroup(const CompositionNodeMatchKey& left,
                                                     const CompositionNodeMatchKey& right) noexcept
    {
        return left.parentStableNodeId == right.parentStableNodeId && left.key == right.key;
    }

    [[nodiscard]] bool MatchCompositionNodeEvidenceAtDepth(
        const tools::ModelImportManifest& manifest, const containers::DynamicArray<u32>& oldOrder, const u32 oldBegin, const u32 oldEnd,
        const containers::DynamicArray<u32>& currentOrder, const u32 currentBegin, const u32 currentEnd,
        const containers::DynamicArray<tools::ModelImportSourceEvidence>& currentEvidence,
        const tools::ModelImportSettings& oldSettings, containers::DynamicArray<u64>& currentStableIds,
        containers::DynamicArray<u8>& oldUsed, const bool structural) noexcept
    {
        containers::DynamicArray<CompositionNodeMatchKey> keys(memory::pools::Assets::GetInstance());
        keys.Reserve((oldEnd - oldBegin) + (currentEnd - currentBegin));
        for (u32 offset = oldBegin; offset < oldEnd; ++offset)
        {
            const u32 oldIndex = oldOrder[offset];
            if (oldUsed[oldIndex] != 0u)
                continue;
            const tools::ModelImportCompositionNodeCorrespondence& old = oldSettings.compositionNodes[oldIndex];
            const crypto::Digest256& key = structural ? old.source.structuralKey : old.source.relationshipKey;
            if (key.IsEmpty())
                continue;
            keys.PushBack({old.parentStableNodeId, key, oldIndex, true});
        }
        for (u32 offset = currentBegin; offset < currentEnd; ++offset)
        {
            const u32 nodeIndex = currentOrder[offset];
            if (currentStableIds[nodeIndex] != 0u)
                continue;
            const tools::ModelImportNode& node = manifest.nodes[nodeIndex];
            const u64 parentStableNodeId = node.parentIndex == tools::InvalidModelImportIndex
                                               ? 0u
                                               : (node.parentIndex < currentStableIds.Size() ? currentStableIds[node.parentIndex] : 0u);
            if (node.parentIndex != tools::InvalidModelImportIndex && parentStableNodeId == 0u)
                return false;
            const crypto::Digest256& key = structural ? currentEvidence[nodeIndex].structuralKey : currentEvidence[nodeIndex].relationshipKey;
            if (key.IsEmpty())
                continue;
            keys.PushBack({parentStableNodeId, key, nodeIndex, false});
        }
        if (keys.Size() > 1u)
            std::sort(keys.TypedData(), keys.TypedData() + keys.Size(), CompositionNodeMatchKeyLess);
        for (u32 begin = 0; begin < keys.Size();)
        {
            u32 end = begin + 1u;
            while (end < keys.Size() && SameCompositionNodeMatchGroup(keys[begin], keys[end]))
                ++end;
            u32 oldCount = 0u;
            u32 currentCount = 0u;
            u32 oldIndex = tools::InvalidModelImportIndex;
            u32 currentIndex = tools::InvalidModelImportIndex;
            for (u32 index = begin; index < end; ++index)
            {
                if (keys[index].old)
                {
                    ++oldCount;
                    oldIndex = keys[index].index;
                }
                else
                {
                    ++currentCount;
                    currentIndex = keys[index].index;
                }
            }
            if (oldCount == 1u && currentCount == 1u && oldIndex < oldSettings.compositionNodes.Size() &&
                currentIndex < currentStableIds.Size() && oldUsed[oldIndex] == 0u && currentStableIds[currentIndex] == 0u)
            {
                currentStableIds[currentIndex] = oldSettings.compositionNodes[oldIndex].stableNodeId;
                oldUsed[oldIndex] = 1u;
            }
            begin = end;
        }
        return true;
    }

    [[nodiscard]] bool ReconcileCompositionNodes(const tools::ModelImportManifest& manifest,
                                                 const ModelPlanEvidenceIndex& evidenceIndex,
                                                 const containers::DynamicArray<u64>& geometryPersistentIds,
                                                 const tools::ModelImportSettings& oldSettings,
                                                 tools::ModelImportSettings& nextSettings) noexcept
    {
        containers::DynamicArray<u32> depths(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> oldDepths(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> depthOrder(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> oldDepthOrder(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> childCounts(memory::pools::Assets::GetInstance());
        containers::DynamicArray<tools::ModelImportSourceEvidence> evidence(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u64> stableIds(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> oldUsed(memory::pools::Assets::GetInstance());
        if (!BuildNodeDepths(manifest, depths) || !BuildPersistedCompositionNodeDepths(oldSettings, oldDepths) ||
            !BuildCompositionDepthOrder(depths, depthOrder) || !BuildCompositionDepthOrder(oldDepths, oldDepthOrder))
            return false;
        childCounts.Resize(manifest.nodes.Size());
        evidence.Resize(manifest.nodes.Size());
        stableIds.Resize(manifest.nodes.Size());
        oldUsed.Resize(oldSettings.compositionNodes.Size());
        if (childCounts.Size() != manifest.nodes.Size() || evidence.Size() != manifest.nodes.Size() ||
            stableIds.Size() != manifest.nodes.Size() || oldUsed.Size() != oldSettings.compositionNodes.Size())
            return false;
        for (u32 index = 0; index < childCounts.Size(); ++index)
        {
            childCounts[index] = 0u;
            stableIds[index] = 0u;
        }
        for (u32 index = 0; index < oldUsed.Size(); ++index)
            oldUsed[index] = 0u;
        u32 maximumDepth = 0u;
        for (u32 nodeIndex = 0; nodeIndex < manifest.nodes.Size(); ++nodeIndex)
        {
            const tools::ModelImportNode& node = manifest.nodes[nodeIndex];
            if (node.parentIndex != tools::InvalidModelImportIndex)
            {
                if (node.parentIndex >= manifest.nodes.Size() || childCounts[node.parentIndex] == ~u32{0})
                    return false;
                ++childCounts[node.parentIndex];
            }
            maximumDepth = std::max(maximumDepth, depths[nodeIndex]);
        }
        for (u32 nodeIndex = 0; nodeIndex < manifest.nodes.Size(); ++nodeIndex)
            if (!BuildCompositionNodeEvidence(manifest, evidenceIndex, nodeIndex, geometryPersistentIds, childCounts[nodeIndex], evidence[nodeIndex]))
                return false;

        nextSettings.compositionNodes.Clear();
        nextSettings.compositionNodes.Reserve(manifest.nodes.Size());
        u32 currentCursor = 0u;
        u32 oldCursor = 0u;
        for (u32 depth = 0u; depth <= maximumDepth; ++depth)
        {
            const u32 currentBegin = currentCursor;
            while (currentCursor < depthOrder.Size() && depths[depthOrder[currentCursor]] == depth)
                ++currentCursor;
            while (oldCursor < oldDepthOrder.Size() && oldDepths[oldDepthOrder[oldCursor]] < depth)
                ++oldCursor;
            const u32 oldBegin = oldCursor;
            while (oldCursor < oldDepthOrder.Size() && oldDepths[oldDepthOrder[oldCursor]] == depth)
                ++oldCursor;
            if (!MatchCompositionNodeEvidenceAtDepth(manifest, oldDepthOrder, oldBegin, oldCursor, depthOrder, currentBegin, currentCursor,
                                                     evidence, oldSettings, stableIds, oldUsed, true) ||
                !MatchCompositionNodeEvidenceAtDepth(manifest, oldDepthOrder, oldBegin, oldCursor, depthOrder, currentBegin, currentCursor,
                                                     evidence, oldSettings, stableIds, oldUsed, false))
                return false;
            for (u32 offset = currentBegin; offset < currentCursor; ++offset)
            {
                const u32 nodeIndex = depthOrder[offset];
                if (stableIds[nodeIndex] == 0u)
                {
                    if (nextSettings.nextCompositionNodeId == ~u64{0})
                        return false;
                    stableIds[nodeIndex] = nextSettings.nextCompositionNodeId++;
                }
                const tools::ModelImportNode& node = manifest.nodes[nodeIndex];
                const u64 parentStableNodeId = node.parentIndex == tools::InvalidModelImportIndex ? 0u : stableIds[node.parentIndex];
                if (node.parentIndex != tools::InvalidModelImportIndex && parentStableNodeId == 0u)
                    return false;
                tools::ModelImportCompositionNodeCorrespondence& correspondence = nextSettings.compositionNodes.EmplaceBack();
                correspondence.stableNodeId = stableIds[nodeIndex];
                correspondence.parentStableNodeId = parentStableNodeId;
                correspondence.sourceNodeIndex = nodeIndex;
                correspondence.source = evidence[nodeIndex];
            }
        }
        return nextSettings.compositionNodes.Size() == manifest.nodes.Size();
    }

    struct CurrentCompositionMesh final
    {
        u64 stableNodeId = 0u;
        u64 geometryPersistentId = 0u;
        u64 stableComponentId = 0u;
        u32 sourceInstanceIndex = tools::InvalidModelImportIndex;
        u32 sameGeometryOrdinal = 0u;
    };

    struct CompositionMeshOrder final
    {
        u64 stableNodeId = 0u;
        u64 geometryPersistentId = 0u;
        u32 sourceInstanceIndex = tools::InvalidModelImportIndex;
        u32 currentIndex = tools::InvalidModelImportIndex;
    };

    [[nodiscard]] bool CompositionMeshOrderLess(const CompositionMeshOrder& left, const CompositionMeshOrder& right) noexcept
    {
        if (left.stableNodeId != right.stableNodeId)
            return left.stableNodeId < right.stableNodeId;
        if (left.geometryPersistentId != right.geometryPersistentId)
            return left.geometryPersistentId < right.geometryPersistentId;
        return left.sourceInstanceIndex < right.sourceInstanceIndex;
    }

    struct CompositionMeshMatchKey final
    {
        u64 stableNodeId = 0u;
        u64 geometryPersistentId = 0u;
        u32 sameGeometryOrdinal = 0u;
        u32 index = tools::InvalidModelImportIndex;
        bool old = false;
    };

    [[nodiscard]] bool CompositionMeshMatchKeyLess(const CompositionMeshMatchKey& left, const CompositionMeshMatchKey& right) noexcept
    {
        if (left.stableNodeId != right.stableNodeId)
            return left.stableNodeId < right.stableNodeId;
        if (left.geometryPersistentId != right.geometryPersistentId)
            return left.geometryPersistentId < right.geometryPersistentId;
        if (left.sameGeometryOrdinal != right.sameGeometryOrdinal)
            return left.sameGeometryOrdinal < right.sameGeometryOrdinal;
        if (left.old != right.old)
            return left.old;
        return left.index < right.index;
    }

    [[nodiscard]] bool SameCompositionMeshMatchKey(const CompositionMeshMatchKey& left,
                                                   const CompositionMeshMatchKey& right) noexcept
    {
        return left.stableNodeId == right.stableNodeId && left.geometryPersistentId == right.geometryPersistentId &&
               left.sameGeometryOrdinal == right.sameGeometryOrdinal;
    }

    struct CompositionMeshNodeMatch final
    {
        u64 stableNodeId = 0u;
        u32 index = tools::InvalidModelImportIndex;
        bool old = false;
    };

    [[nodiscard]] bool CompositionMeshNodeMatchLess(const CompositionMeshNodeMatch& left,
                                                    const CompositionMeshNodeMatch& right) noexcept
    {
        if (left.stableNodeId != right.stableNodeId)
            return left.stableNodeId < right.stableNodeId;
        if (left.old != right.old)
            return left.old;
        return left.index < right.index;
    }

    [[nodiscard]] bool ReconcileCompositionMeshes(const tools::ModelImportManifest& manifest,
                                                  const containers::DynamicArray<u64>& geometryPersistentIds,
                                                  const tools::ModelImportSettings& oldSettings,
                                                  tools::ModelImportSettings& nextSettings) noexcept
    {
        containers::DynamicArray<u64> stableNodeIds(memory::pools::Assets::GetInstance());
        stableNodeIds.Resize(manifest.nodes.Size());
        if (stableNodeIds.Size() != manifest.nodes.Size())
            return false;
        for (u32 index = 0; index < stableNodeIds.Size(); ++index)
            stableNodeIds[index] = 0u;
        for (const tools::ModelImportCompositionNodeCorrespondence& node : nextSettings.compositionNodes)
        {
            if (node.sourceNodeIndex >= stableNodeIds.Size() || node.stableNodeId == 0u || stableNodeIds[node.sourceNodeIndex] != 0u)
                return false;
            stableNodeIds[node.sourceNodeIndex] = node.stableNodeId;
        }

        containers::DynamicArray<CurrentCompositionMesh> current(memory::pools::Assets::GetInstance());
        containers::DynamicArray<CompositionMeshOrder> order(memory::pools::Assets::GetInstance());
        current.Reserve(manifest.geometryInstances.Size());
        order.Reserve(manifest.geometryInstances.Size());
        for (u32 nodeIndex = 0; nodeIndex < manifest.nodes.Size(); ++nodeIndex)
        {
            const tools::ModelImportNode& node = manifest.nodes[nodeIndex];
            if (node.geometryInstanceIndices.Empty() || stableNodeIds[nodeIndex] == 0u)
                continue;
            for (const u32 instanceIndex : node.geometryInstanceIndices)
            {
                if (instanceIndex >= manifest.geometryInstances.Size())
                    return false;
                const tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[instanceIndex];
                if (instance.nodeIndex != nodeIndex || instance.geometryDefinitionIndex >= geometryPersistentIds.Size() ||
                    geometryPersistentIds[instance.geometryDefinitionIndex] == 0u)
                    return false;
                const u32 currentIndex = current.Size();
                current.PushBack({stableNodeIds[nodeIndex], geometryPersistentIds[instance.geometryDefinitionIndex], 0u, instanceIndex, 0u});
                order.PushBack({stableNodeIds[nodeIndex], geometryPersistentIds[instance.geometryDefinitionIndex], instanceIndex, currentIndex});
            }
        }
        if (current.Size() != order.Size())
            return false;
        if (order.Size() > 1u)
            std::sort(order.TypedData(), order.TypedData() + order.Size(), CompositionMeshOrderLess);
        u32 ordinal = 0u;
        for (u32 index = 0; index < order.Size(); ++index)
        {
            if (index == 0u || order[index - 1u].stableNodeId != order[index].stableNodeId ||
                order[index - 1u].geometryPersistentId != order[index].geometryPersistentId)
                ordinal = 0u;
            if (order[index].currentIndex >= current.Size())
                return false;
            current[order[index].currentIndex].sameGeometryOrdinal = ordinal++;
        }

        containers::DynamicArray<u8> oldUsed(memory::pools::Assets::GetInstance());
        oldUsed.Resize(oldSettings.compositionMeshes.Size());
        if (oldUsed.Size() != oldSettings.compositionMeshes.Size())
            return false;
        for (u32 index = 0; index < oldUsed.Size(); ++index)
            oldUsed[index] = 0u;

        containers::DynamicArray<CompositionMeshMatchKey> keys(memory::pools::Assets::GetInstance());
        keys.Reserve(oldSettings.compositionMeshes.Size() + current.Size());
        for (u32 oldIndex = 0; oldIndex < oldSettings.compositionMeshes.Size(); ++oldIndex)
        {
            const tools::ModelImportCompositionMeshCorrespondence& old = oldSettings.compositionMeshes[oldIndex];
            keys.PushBack({old.stableNodeId, old.geometryPersistentId, old.sameGeometryOrdinal, oldIndex, true});
        }
        for (u32 currentIndex = 0; currentIndex < current.Size(); ++currentIndex)
            keys.PushBack({current[currentIndex].stableNodeId, current[currentIndex].geometryPersistentId,
                           current[currentIndex].sameGeometryOrdinal, currentIndex, false});
        if (keys.Size() > 1u)
            std::sort(keys.TypedData(), keys.TypedData() + keys.Size(), CompositionMeshMatchKeyLess);
        for (u32 begin = 0; begin < keys.Size();)
        {
            u32 end = begin + 1u;
            while (end < keys.Size() && SameCompositionMeshMatchKey(keys[begin], keys[end]))
                ++end;
            u32 oldCount = 0u;
            u32 currentCount = 0u;
            u32 oldIndex = tools::InvalidModelImportIndex;
            u32 currentIndex = tools::InvalidModelImportIndex;
            for (u32 index = begin; index < end; ++index)
            {
                if (keys[index].old)
                {
                    ++oldCount;
                    oldIndex = keys[index].index;
                }
                else
                {
                    ++currentCount;
                    currentIndex = keys[index].index;
                }
            }
            if (oldCount == 1u && currentCount == 1u && oldIndex < oldSettings.compositionMeshes.Size() && currentIndex < current.Size())
            {
                current[currentIndex].stableComponentId = oldSettings.compositionMeshes[oldIndex].stableComponentId;
                oldUsed[oldIndex] = 1u;
            }
            begin = end;
        }

        // If one component on a stable node was genuinely replaced, preserve that slot.
        // Multiple unmatched old/current children are ambiguous and deliberately receive new
        // identities rather than guessing by source order.
        containers::DynamicArray<CompositionMeshNodeMatch> nodeMatches(memory::pools::Assets::GetInstance());
        nodeMatches.Reserve(oldSettings.compositionMeshes.Size() + current.Size());
        for (u32 oldIndex = 0; oldIndex < oldSettings.compositionMeshes.Size(); ++oldIndex)
            if (oldUsed[oldIndex] == 0u)
                nodeMatches.PushBack({oldSettings.compositionMeshes[oldIndex].stableNodeId, oldIndex, true});
        for (u32 currentIndex = 0; currentIndex < current.Size(); ++currentIndex)
            if (current[currentIndex].stableComponentId == 0u)
                nodeMatches.PushBack({current[currentIndex].stableNodeId, currentIndex, false});
        if (nodeMatches.Size() > 1u)
            std::sort(nodeMatches.TypedData(), nodeMatches.TypedData() + nodeMatches.Size(), CompositionMeshNodeMatchLess);
        for (u32 begin = 0; begin < nodeMatches.Size();)
        {
            u32 end = begin + 1u;
            while (end < nodeMatches.Size() && nodeMatches[end].stableNodeId == nodeMatches[begin].stableNodeId)
                ++end;
            u32 oldCount = 0u;
            u32 currentCount = 0u;
            u32 oldIndex = tools::InvalidModelImportIndex;
            u32 currentIndex = tools::InvalidModelImportIndex;
            for (u32 index = begin; index < end; ++index)
            {
                if (nodeMatches[index].old)
                {
                    ++oldCount;
                    oldIndex = nodeMatches[index].index;
                }
                else
                {
                    ++currentCount;
                    currentIndex = nodeMatches[index].index;
                }
            }
            if (oldCount == 1u && currentCount == 1u && oldIndex < oldSettings.compositionMeshes.Size() && currentIndex < current.Size())
            {
                current[currentIndex].stableComponentId = oldSettings.compositionMeshes[oldIndex].stableComponentId;
                oldUsed[oldIndex] = 1u;
            }
            begin = end;
        }

        nextSettings.compositionMeshes.Clear();
        nextSettings.compositionMeshes.Reserve(current.Size());
        for (CurrentCompositionMesh& mesh : current)
        {
            if (mesh.stableComponentId == 0u)
            {
                if (nextSettings.nextCompositionNodeId == ~u64{0})
                    return false;
                mesh.stableComponentId = nextSettings.nextCompositionNodeId++;
            }
            tools::ModelImportCompositionMeshCorrespondence& correspondence = nextSettings.compositionMeshes.EmplaceBack();
            correspondence.stableComponentId = mesh.stableComponentId;
            correspondence.stableNodeId = mesh.stableNodeId;
            correspondence.geometryPersistentId = mesh.geometryPersistentId;
            correspondence.sourceInstanceIndex = mesh.sourceInstanceIndex;
            correspondence.sameGeometryOrdinal = mesh.sameGeometryOrdinal;
        }
        return nextSettings.compositionMeshes.Size() == current.Size();
    }

    [[nodiscard]] bool BuildSlotStructuralKey(const tools::ModelImportPrimitive& primitive,
                                              crypto::Digest256& output) noexcept
    {
        crypto::Sha256Builder hash;
        constexpr char Domain[] = "vanguard.model.material-slot.evidence.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) || !HashPrimitiveSchema(hash, primitive))
            return false;
        return FinalizeEvidenceHash(hash, output);
    }

    [[nodiscard]] bool BuildSlotPartitionKey(const tools::ModelImportPrimitive& primitive,
                                             crypto::Digest256& output) noexcept
    {
        crypto::Sha256Builder hash;
        constexpr char Domain[] = "vanguard.model.material-slot.partition.v1";
        if (!HashBytes(hash, Domain, sizeof(Domain) - 1u) || !HashPrimitiveSlotContent(hash, primitive))
            return false;
        return FinalizeEvidenceHash(hash, output);
    }

    [[nodiscard]] bool AbsolutePathUnderAnyRoot(const assets::SourceDatabase& database,
                                                const filesystem::AbsolutePath& path) noexcept
    {
        if (!path.IsFilePath())
            return false;
        for (const assets::SourceRoot& root : database.GetRoots())
        {
            const containers::StringView rootPath = root.directory.AsStringView();
            const containers::StringView targetPath = path.AsStringView();
            if (targetPath.Length() > rootPath.Length() && targetPath.StartsWithIgnoreCase(rootPath))
                return true;
        }
        return false;
    }

    [[nodiscard]] bool AbsolutePathUnderRoot(const filesystem::AbsolutePath& root,
                                             const filesystem::AbsolutePath& path) noexcept
    {
        if (!root.IsDirectoryPath() || !path.IsFilePath())
            return false;
        const containers::StringView rootPath = root.AsStringView();
        const containers::StringView targetPath = path.AsStringView();
        if (rootPath.Empty() || targetPath.Length() <= rootPath.Length() || !targetPath.StartsWithIgnoreCase(rootPath))
            return false;
        const char rootTail = rootPath[rootPath.Length() - 1u];
        return rootTail == '/' || rootTail == '\\' || targetPath[rootPath.Length()] == '/' || targetPath[rootPath.Length()] == '\\';
    }

    [[nodiscard]] bool PlanningRootIsWritable(const tools::ModelImportPlanningContext& context) noexcept
    {
        if (context.database == nullptr)
            return true;
        for (const assets::SourceRoot& root : context.database->GetRoots())
            if (root.directory == context.sourceRoot)
                return !root.readOnly;
        return false;
    }

    [[nodiscard]] bool SourceIdentityMatchesDestination(const tools::ModelImportPlanningContext& context) noexcept
    {
        if (!AbsolutePathUnderRoot(context.sourceRoot, context.sourceDestination))
            return false;
        const containers::StringView rootPath = context.sourceRoot.AsStringView();
        const containers::StringView destinationPath = context.sourceDestination.AsStringView();
        const containers::StringView relativePath = destinationPath.Slice(rootPath.Length(), destinationPath.Length());
        const resources::ResourcePath expected = resources::ResourcePath::FromString(relativePath);
        return expected.IsValid() && expected == context.sourceIdentity;
    }

    [[nodiscard]] bool SourceRecordRootIsWritable(const assets::SourceDatabase& database,
                                                  const assets::SourceRecord& source) noexcept
    {
        const containers::ArraySpan<const assets::SourceRoot> roots = database.GetRoots();
        return source.root < roots.Count() && !roots[source.root].readOnly;
    }

    [[nodiscard]] const tools::ModelImportTextureResolution* FindTextureResolution(const tools::ModelImportSettings& settings,
                                                                                   const containers::String& authoredPath) noexcept
    {
        for (const tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
            if (resolution.authoredPath == authoredPath)
                return &resolution;
        return nullptr;
    }

    [[nodiscard]] const tools::ModelImportDependencyWitness* FindDependencyWitness(
        const tools::ModelImportPlanningContext& context, const resources::ResourcePath sourceIdentity) noexcept
    {
        if (!sourceIdentity.IsValid())
            return nullptr;
        for (const tools::ModelImportDependencyWitness& witness : context.dependencyWitnesses)
            if (witness.sourceIdentity == sourceIdentity)
                return &witness;
        return nullptr;
    }

    [[nodiscard]] bool AppendDependencyWitness(tools::ModelImportPlan& plan,
                                               const tools::ModelImportDependencyWitness& witness,
                                               const resources::ResourceReference selectedResource,
                                               const u32 maximum) noexcept
    {
        containers::DynamicArray<tools::ModelImportDependencyWitness>& witnesses =
            tools::ModelImportPlanBuilderAccess::DependencyWitnesses(plan);
        for (const tools::ModelImportDependencyWitness& existing : witnesses)
            if (existing.sourceIdentity == witness.sourceIdentity && existing.selectedResource == selectedResource)
                return existing.physicalPath == witness.physicalPath && existing.sourceContentFingerprint == witness.sourceContentFingerprint &&
                       existing.metadataFingerprint == witness.metadataFingerprint && existing.asset == witness.asset &&
                       existing.sourceExists == witness.sourceExists && existing.metadataExists == witness.metadataExists &&
                       existing.hasSourceContentFingerprint == witness.hasSourceContentFingerprint &&
                       existing.hasMetadataFingerprint == witness.hasMetadataFingerprint;
        if (witness.sourceIdentity.IsValid() == false || !witness.physicalPath.IsFilePath() || witnesses.Size() >= maximum)
            return false;
        const u32 previous = witnesses.Size();
        tools::ModelImportDependencyWitness& owned = witnesses.EmplaceBack();
        if (witnesses.Size() != previous + 1u)
            return false;
        owned = witness;
        owned.selectedResource = selectedResource;
        return true;
    }

    [[nodiscard]] bool ValidateManagedDependencyWitness(const tools::ModelImportPlanningContext& context,
                                                        const resources::ResourceReference reference,
                                                        const tools::ModelImportDependencyWitness*& witness) noexcept
    {
        witness = nullptr;
        if (context.database == nullptr || !reference.IsValid() || !reference.IsTyped())
            return false;
        const assets::SourceRecord* const source = context.database->LookupOutput(reference);
        witness = source != nullptr ? FindDependencyWitness(context, source->sourceIdentity) : nullptr;
        return source != nullptr && witness != nullptr && witness->physicalPath == source->path && witness->sourceExists &&
               witness->metadataExists && witness->hasSourceContentFingerprint && witness->hasMetadataFingerprint &&
               !witness->sourceContentFingerprint.IsEmpty() && !witness->metadataFingerprint.IsEmpty() &&
               witness->asset == source->metadata.id && MetadataDigestMatchesSnapshot(source->metadata, witness->metadataFingerprint);
    }

    [[nodiscard]] bool AppendTextureResolution(tools::ModelImportSettings& settings,
                                               const tools::ModelImportTextureResolution& resolution,
                                               const u32 maximum) noexcept
    {
        if (settings.textureResolutions.Size() >= maximum)
            return false;
        const u32 previous = settings.textureResolutions.Size();
        tools::ModelImportTextureResolution& value = settings.textureResolutions.EmplaceBack();
        if (settings.textureResolutions.Size() != previous + 1u)
            return false;
        value.authoredPath = resolution.authoredPath;
        value.resolution = resolution.resolution;
        value.managedResource = resolution.managedResource;
        value.projectSourceIdentity = resolution.projectSourceIdentity;
        return true;
    }

    [[nodiscard]] bool AddMissingDependencyBlocker(tools::ModelImportPlan& plan,
                                                   const u32 maximumBlockers) noexcept
    {
        tools::ModelImportPlanBlocker blocker;
        blocker.kind = tools::ModelImportPlanBlockerKind::MissingDependency;
        blocker.elementKind = tools::ModelImportElementKind::ExternalTextureDependency;
        return AppendBlocker(plan, blocker, maximumBlockers);
    }
} // namespace

namespace
{
    constexpr u32 ModelSettingsMagic = vanguard::serialization::MakeFourCC('V', 'M', 'I', 'S');
    constexpr u16 LegacyModelSettingsWireVersion = 1;
    constexpr u16 SourceFingerprintModelSettingsWireVersion = 2;
    constexpr u16 CompositionNodeModelSettingsWireVersion = 3;
    constexpr u16 ModelSettingsWireVersion = 4;

    class ModelSettingsWriter final
    {
    public:
        explicit ModelSettingsWriter(containers::DynamicArray<u8>& output) noexcept : m_output(output) {}

        [[nodiscard]] bool U8(const u8 value) noexcept { return Bytes(&value, sizeof(value)); }
        [[nodiscard]] bool U16(const u16 value) noexcept
        {
            const u8 bytes[2]{static_cast<u8>(value), static_cast<u8>(value >> 8u)};
            return Bytes(bytes, sizeof(bytes));
        }
        [[nodiscard]] bool U32(const u32 value) noexcept
        {
            const u8 bytes[4]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                              static_cast<u8>(value >> 24u)};
            return Bytes(bytes, sizeof(bytes));
        }
        [[nodiscard]] bool U64(const u64 value) noexcept
        {
            return U32(static_cast<u32>(value)) && U32(static_cast<u32>(value >> 32u));
        }
        [[nodiscard]] bool F32(const f32 value) noexcept
        {
            u32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return U32(bits);
        }
        [[nodiscard]] bool Bool(const bool value) noexcept { return U8(value ? 1u : 0u); }
        [[nodiscard]] bool Digest(const crypto::Digest256& value) noexcept
        {
            return Bytes(value.bytes, crypto::Digest256::ByteCount);
        }
        [[nodiscard]] bool String(const containers::String& value) noexcept
        {
            return value.Length() <= MaximumModelSettingsStringBytes && U32(value.Length()) &&
                   (value.Empty() || Bytes(value.AsChar(), value.Length()));
        }
        [[nodiscard]] bool Reference(const resources::ResourceReference value) noexcept
        {
            return U64(value.GetPath().Id()) && U32(value.ExpectedType());
        }
        [[nodiscard]] bool Path(const resources::ResourcePath value) noexcept { return U64(value.Id()); }

    private:
        [[nodiscard]] bool Bytes(const void* const data, const u32 size) noexcept
        {
            if (!m_valid || data == nullptr || size > assets::MaximumAssetSettingsBytes ||
                m_output.Size() > assets::MaximumAssetSettingsBytes - size)
            {
                m_valid = false;
                return false;
            }
            const u32 offset = m_output.Size();
            m_output.Resize(offset + size);
            if (m_output.Size() != offset + size)
            {
                m_valid = false;
                return false;
            }
            if (size != 0u)
                std::memcpy(m_output.TypedData() + offset, data, size);
            return true;
        }

        containers::DynamicArray<u8>& m_output;
        bool m_valid = true;
    };

    class ModelSettingsReader final
    {
    public:
        explicit ModelSettingsReader(const containers::ArraySpan<const u8> bytes) noexcept : m_bytes(bytes) {}

        [[nodiscard]] bool U8(u8& value) noexcept { return Bytes(&value, sizeof(value)); }
        [[nodiscard]] bool U16(u16& value) noexcept
        {
            u8 bytes[2]{};
            if (!Bytes(bytes, sizeof(bytes)))
                return false;
            value = static_cast<u16>(bytes[0]) | static_cast<u16>(static_cast<u16>(bytes[1]) << 8u);
            return true;
        }
        [[nodiscard]] bool U32(u32& value) noexcept
        {
            u8 bytes[4]{};
            if (!Bytes(bytes, sizeof(bytes)))
                return false;
            value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) |
                    (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
            return true;
        }
        [[nodiscard]] bool U64(u64& value) noexcept
        {
            u32 low = 0;
            u32 high = 0;
            if (!U32(low) || !U32(high))
                return false;
            value = static_cast<u64>(low) | (static_cast<u64>(high) << 32u);
            return true;
        }
        [[nodiscard]] bool F32(f32& value) noexcept
        {
            u32 bits = 0;
            if (!U32(bits))
                return false;
            std::memcpy(&value, &bits, sizeof(value));
            return true;
        }
        [[nodiscard]] bool Bool(bool& value) noexcept
        {
            u8 encoded = 0;
            if (!U8(encoded) || encoded > 1u)
                return false;
            value = encoded != 0u;
            return true;
        }
        [[nodiscard]] bool Digest(crypto::Digest256& value) noexcept
        {
            return Bytes(value.bytes, crypto::Digest256::ByteCount);
        }
        [[nodiscard]] bool String(containers::String& value) noexcept
        {
            u32 size = 0;
            if (!U32(size) || size > MaximumModelSettingsStringBytes || size > Remaining())
                return false;
            containers::String staged;
            if (size != 0u)
            {
                if (!staged.Resize(size))
                    return false;
                if (!Bytes(staged.Data(), size))
                    return false;
                for (u32 index = 0; index < size; ++index)
                    if (staged[index] == '\0')
                        return false;
            }
            value = static_cast<containers::String&&>(staged);
            return true;
        }
        [[nodiscard]] bool Reference(resources::ResourceReference& value) noexcept
        {
            u64 path = 0;
            resources::ResourceTypeId type = resources::InvalidResourceTypeId;
            if (!U64(path) || !U32(type))
                return false;
            value = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
            return true;
        }
        [[nodiscard]] bool Path(resources::ResourcePath& value) noexcept
        {
            u64 id = 0;
            if (!U64(id))
                return false;
            value = resources::ResourcePath::FromId(id);
            return true;
        }
        [[nodiscard]] u32 Remaining() const noexcept { return m_bytes.Size() - m_offset; }

    private:
        [[nodiscard]] bool Bytes(void* const destination, const u32 size) noexcept
        {
            if (destination == nullptr || size > Remaining())
                return false;
            if (size != 0u)
                std::memcpy(destination, m_bytes.Data() + m_offset, size);
            m_offset += size;
            return true;
        }

        containers::ArraySpan<const u8> m_bytes;
        u32 m_offset = 0;
    };

    [[nodiscard]] bool ValidOptionalReference(const resources::ResourceReference value) noexcept
    {
        return value.IsValid() ? value.IsTyped()
                               : !value.GetPath().IsValid() && value.ExpectedType() == resources::InvalidResourceTypeId;
    }

    [[nodiscard]] bool HasEmbeddedNull(const containers::String& value) noexcept
    {
        for (u32 index = 0; index < value.Length(); ++index)
            if (value[index] == '\0')
                return true;
        return false;
    }

    [[nodiscard]] bool ValidMeshImportSettings(const tools::MeshImportSettings& settings) noexcept
    {
        return std::isfinite(settings.uniformScale) && settings.uniformScale > 0.0f &&
               std::isfinite(settings.smoothingAngleDegrees) && settings.smoothingAngleDegrees > 0.0f &&
               settings.smoothingAngleDegrees <= 175.0f && settings.sourceUpAxis <= tools::SourceUpAxis::Z &&
               settings.maximumSubmeshes != 0u && settings.maximumVerticesPerSubmesh != 0u &&
               settings.maximumIndicesPerSubmesh >= 3u && settings.maximumJoints != 0u && settings.maximumJoints <= 255u &&
               ValidOptionalReference(settings.defaultMaterial) && ValidOptionalReference(settings.skeleton);
    }

    [[nodiscard]] bool ValidCookSettings(const tools::ModelImportOwnedCookSettings& settings) noexcept
    {
        if (!std::isfinite(settings.overdrawThreshold) || settings.overdrawThreshold < 1.0f ||
            settings.meshCookingProfile == 0u ||
            settings.maximumSubmeshes == 0u ||
            settings.maximumVertexStreamsPerSubmesh == 0u || settings.maximumVerticesPerSubmesh == 0u ||
            settings.maximumIndicesPerSubmesh < 3u || settings.maximumLodLevels == 0u ||
            settings.lodLevels.Size() > settings.maximumLodLevels ||
            !std::isfinite(settings.lodAttributeWeights.normal) || settings.lodAttributeWeights.normal < 0.0f ||
            !std::isfinite(settings.lodAttributeWeights.tangent) || settings.lodAttributeWeights.tangent < 0.0f ||
            !std::isfinite(settings.lodAttributeWeights.texCoord) || settings.lodAttributeWeights.texCoord < 0.0f ||
            !std::isfinite(settings.lodAttributeWeights.color) || settings.lodAttributeWeights.color < 0.0f ||
            !std::isfinite(settings.lodAttributeWeights.jointWeights) || settings.lodAttributeWeights.jointWeights < 0.0f ||
            !std::isfinite(settings.lodAttributeWeights.morphPosition) || settings.lodAttributeWeights.morphPosition < 0.0f)
            return false;

        f32 previousRatio = 1.0f;
        f32 previousCoverage = 1.0f;
        f32 previousError = 0.0f;
        for (const tools::LodLevelSettings& lod : settings.lodLevels)
        {
            if (!std::isfinite(lod.triangleRatio) || !std::isfinite(lod.maximumNormalizedError) ||
                !std::isfinite(lod.minimumScreenCoverage) || lod.triangleRatio <= 0.0f || lod.triangleRatio >= previousRatio ||
                lod.maximumNormalizedError <= 0.0f || lod.maximumNormalizedError < previousError ||
                lod.minimumScreenCoverage <= 0.0f || lod.minimumScreenCoverage >= previousCoverage)
                return false;
            previousRatio = lod.triangleRatio;
            previousCoverage = lod.minimumScreenCoverage;
            previousError = lod.maximumNormalizedError;
        }
        return true;
    }

    [[nodiscard]] int CompareStrings(const containers::String& left, const containers::String& right) noexcept
    {
        return std::strcmp(left.AsChar(), right.AsChar());
    }

    [[nodiscard]] bool CorrespondenceLess(const tools::ModelImportOutputCorrespondence& left,
                                          const tools::ModelImportOutputCorrespondence& right) noexcept
    {
        if (left.kind != right.kind)
            return left.kind < right.kind;
        if (left.persistentId != right.persistentId)
            return left.persistentId < right.persistentId;
        return CompareStrings(left.output.key, right.output.key) < 0;
    }

    [[nodiscard]] bool RetiredOutputLess(const tools::ModelImportRetiredOutput& left,
                                         const tools::ModelImportRetiredOutput& right) noexcept
    {
        if (left.kind != right.kind)
            return left.kind < right.kind;
        if (left.persistentId != right.persistentId)
            return left.persistentId < right.persistentId;
        return CompareStrings(left.output.key, right.output.key) < 0;
    }

    [[nodiscard]] bool SlotLess(const tools::ModelImportMaterialSlotCorrespondence& left,
                                const tools::ModelImportMaterialSlotCorrespondence& right) noexcept
    {
        return left.stableSlotId < right.stableSlotId;
    }

    [[nodiscard]] bool RetiredSlotLess(const tools::ModelImportRetiredMaterialSlot& left,
                                       const tools::ModelImportRetiredMaterialSlot& right) noexcept
    {
        return left.stableSlotId < right.stableSlotId;
    }

    [[nodiscard]] bool CompositionNodeLess(const tools::ModelImportCompositionNodeCorrespondence& left,
                                           const tools::ModelImportCompositionNodeCorrespondence& right) noexcept
    {
        return left.stableNodeId < right.stableNodeId;
    }

    [[nodiscard]] bool CompositionMeshLess(const tools::ModelImportCompositionMeshCorrespondence& left,
                                           const tools::ModelImportCompositionMeshCorrespondence& right) noexcept
    {
        return left.stableComponentId < right.stableComponentId;
    }

    [[nodiscard]] bool TextureResolutionLess(const tools::ModelImportTextureResolution& left,
                                             const tools::ModelImportTextureResolution& right) noexcept
    {
        return CompareStrings(left.authoredPath, right.authoredPath) < 0;
    }

    void CanonicalizeSettings(tools::ModelImportSettings& settings) noexcept
    {
        if (settings.outputs.Size() > 1u)
            std::sort(settings.outputs.TypedData(), settings.outputs.TypedData() + settings.outputs.Size(), CorrespondenceLess);
        if (settings.retiredOutputs.Size() > 1u)
            std::sort(settings.retiredOutputs.TypedData(), settings.retiredOutputs.TypedData() + settings.retiredOutputs.Size(), RetiredOutputLess);
        if (settings.materialSlots.Size() > 1u)
            std::sort(settings.materialSlots.TypedData(), settings.materialSlots.TypedData() + settings.materialSlots.Size(), SlotLess);
        if (settings.retiredMaterialSlots.Size() > 1u)
            std::sort(settings.retiredMaterialSlots.TypedData(), settings.retiredMaterialSlots.TypedData() + settings.retiredMaterialSlots.Size(), RetiredSlotLess);
        if (settings.compositionNodes.Size() > 1u)
            std::sort(settings.compositionNodes.TypedData(), settings.compositionNodes.TypedData() + settings.compositionNodes.Size(), CompositionNodeLess);
        if (settings.compositionMeshes.Size() > 1u)
            std::sort(settings.compositionMeshes.TypedData(), settings.compositionMeshes.TypedData() + settings.compositionMeshes.Size(), CompositionMeshLess);
        if (settings.textureResolutions.Size() > 1u)
            std::sort(settings.textureResolutions.TypedData(), settings.textureResolutions.TypedData() + settings.textureResolutions.Size(), TextureResolutionLess);
    }

    [[nodiscard]] bool SameOutputIdentity(const assets::AssetOutput& left, const assets::AssetOutput& right) noexcept
    {
        return left.type == right.type && left.key == right.key;
    }

    [[nodiscard]] bool ValidTextureResolution(const tools::ModelImportTextureResolution& value) noexcept
    {
        if (value.authoredPath.Empty() || value.authoredPath.Length() > MaximumModelSettingsStringBytes || HasEmbeddedNull(value.authoredPath) ||
            value.resolution > tools::ModelImportTextureResolutionKind::IgnoreMissing)
            return false;
        switch (value.resolution)
        {
        case tools::ModelImportTextureResolutionKind::Unresolved:
            return ValidOptionalReference(value.managedResource) && !value.managedResource.IsValid() && !value.projectSourceIdentity.IsValid();
        case tools::ModelImportTextureResolutionKind::ReuseManagedResource:
            return value.managedResource.IsValid() && value.managedResource.IsTyped() && !value.projectSourceIdentity.IsValid();
        case tools::ModelImportTextureResolutionKind::AdoptProjectSource:
            return ValidOptionalReference(value.managedResource) && !value.managedResource.IsValid() && value.projectSourceIdentity.IsValid();
        case tools::ModelImportTextureResolutionKind::IgnoreMissing:
            return ValidOptionalReference(value.managedResource) && !value.managedResource.IsValid() && !value.projectSourceIdentity.IsValid();
        }
        return false;
    }

    [[nodiscard]] bool EncodeEvidence(ModelSettingsWriter& writer, const tools::ModelImportSourceEvidence& source) noexcept
    {
        return writer.U8(static_cast<u8>(source.sourceKind)) && writer.Bool(source.nativeKeyStable) && writer.U64(source.nativeKey) &&
               writer.Digest(source.structuralKey) && writer.Digest(source.relationshipKey) && writer.String(source.sourceName);
    }

    [[nodiscard]] bool DecodeEvidence(ModelSettingsReader& reader, tools::ModelImportSourceEvidence& source) noexcept
    {
        u8 kind = 0;
        if (!reader.U8(kind) || kind > static_cast<u8>(tools::ModelImportSourceKind::Fbx) || !reader.Bool(source.nativeKeyStable) ||
            !reader.U64(source.nativeKey) || !reader.Digest(source.structuralKey) || !reader.Digest(source.relationshipKey) ||
            !reader.String(source.sourceName))
            return false;
        source.sourceKind = static_cast<tools::ModelImportSourceKind>(kind);
        return true;
    }

    [[nodiscard]] bool EncodeAssetOutput(ModelSettingsWriter& writer, const assets::AssetOutput& output) noexcept
    {
        return writer.String(output.key) && writer.U32(output.type);
    }

    [[nodiscard]] bool DecodeAssetOutput(ModelSettingsReader& reader, assets::AssetOutput& output) noexcept
    {
        return reader.String(output.key) && reader.U32(output.type);
    }
} // namespace

namespace vanguard::mesh_tools
{
    ModelImportSettingsResult ValidateModelImportSettings(const ModelImportSettings& settings) noexcept
    {
        if (settings.nextOutputId == 0u || settings.nextMaterialSlotId == 0u || settings.nextCompositionNodeId == 0u ||
            !ValidMeshImportSettings(settings.mesh) ||
            !ValidCookSettings(settings.cook) || settings.materialPolicy > ModelImportMaterialPolicy::UseDefaultMaterial ||
            settings.embeddedTexturePolicy > ModelImportEmbeddedTexturePolicy::Ignore || settings.materialMapperVersion == 0u ||
            settings.outputs.Size() > assets::MaximumAssetOutputs || settings.retiredOutputs.Size() > assets::MaximumAssetOutputs ||
            settings.materialSlots.Size() > 65536u || settings.retiredMaterialSlots.Size() > 65536u ||
            settings.compositionNodes.Size() > (1u << 20u) ||
            settings.compositionMeshes.Size() > (1u << 20u) ||
            settings.textureResolutions.Size() > 65536u)
            return ModelImportSettingsResult::InvalidData;

        u64 largestOutputId = 0u;
        for (u32 index = 0; index < settings.outputs.Size(); ++index)
        {
            const ModelImportOutputCorrespondence& value = settings.outputs[index];
            if (value.kind > ModelImportElementKind::Composition || value.persistentId == 0u ||
                !assets::IsValidAssetOutputKey(value.output.key) || value.output.type == resources::InvalidResourceTypeId ||
                value.source.sourceKind > ModelImportSourceKind::Fbx || value.source.sourceName.Length() > MaximumModelSettingsStringBytes ||
                HasEmbeddedNull(value.source.sourceName) || (value.source.nativeKeyStable && value.source.nativeKey == 0u))
                return ModelImportSettingsResult::InvalidData;
            largestOutputId = std::max(largestOutputId, value.persistentId);
            for (u32 other = index + 1u; other < settings.outputs.Size(); ++other)
                if (settings.outputs[other].persistentId == value.persistentId || settings.outputs[other].output.key == value.output.key)
                    return ModelImportSettingsResult::InvalidData;
            for (const ModelImportRetiredOutput& retired : settings.retiredOutputs)
                if (retired.persistentId == value.persistentId || retired.output.key == value.output.key)
                    return ModelImportSettingsResult::InvalidData;
        }
        for (u32 index = 0; index < settings.retiredOutputs.Size(); ++index)
        {
            const ModelImportRetiredOutput& value = settings.retiredOutputs[index];
            if (value.kind > ModelImportElementKind::Composition || value.persistentId == 0u ||
                !assets::IsValidAssetOutputKey(value.output.key) || value.output.type == resources::InvalidResourceTypeId)
                return ModelImportSettingsResult::InvalidData;
            largestOutputId = std::max(largestOutputId, value.persistentId);
            for (u32 other = index + 1u; other < settings.retiredOutputs.Size(); ++other)
                if (settings.retiredOutputs[other].persistentId == value.persistentId || settings.retiredOutputs[other].output.key == value.output.key)
                    return ModelImportSettingsResult::InvalidData;
        }
        if (settings.nextOutputId <= largestOutputId)
            return ModelImportSettingsResult::InvalidData;

        u64 largestSlotId = 0u;
        for (u32 index = 0; index < settings.materialSlots.Size(); ++index)
        {
            const ModelImportMaterialSlotCorrespondence& value = settings.materialSlots[index];
            if (value.stableSlotId == 0u || value.geometryPersistentId == 0u || value.structuralKey.IsEmpty() ||
                value.partitionKey.IsEmpty())
                return ModelImportSettingsResult::InvalidData;
            largestSlotId = std::max(largestSlotId, value.stableSlotId);
            for (u32 other = index + 1u; other < settings.materialSlots.Size(); ++other)
                if (settings.materialSlots[other].stableSlotId == value.stableSlotId)
                    return ModelImportSettingsResult::InvalidData;
            for (const ModelImportRetiredMaterialSlot& retired : settings.retiredMaterialSlots)
                if (retired.stableSlotId == value.stableSlotId)
                    return ModelImportSettingsResult::InvalidData;
        }
        for (u32 index = 0; index < settings.retiredMaterialSlots.Size(); ++index)
        {
            const u64 id = settings.retiredMaterialSlots[index].stableSlotId;
            if (id == 0u)
                return ModelImportSettingsResult::InvalidData;
            largestSlotId = std::max(largestSlotId, id);
            for (u32 other = index + 1u; other < settings.retiredMaterialSlots.Size(); ++other)
                if (settings.retiredMaterialSlots[other].stableSlotId == id)
                    return ModelImportSettingsResult::InvalidData;
        }
        if (settings.nextMaterialSlotId <= largestSlotId)
            return ModelImportSettingsResult::InvalidData;

        containers::DynamicArray<u32> stableNodeOrder(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> sourceNodeOrder(memory::pools::Assets::GetInstance());
        stableNodeOrder.Resize(settings.compositionNodes.Size());
        sourceNodeOrder.Resize(settings.compositionNodes.Size());
        if (stableNodeOrder.Size() != settings.compositionNodes.Size() || sourceNodeOrder.Size() != settings.compositionNodes.Size())
            return ModelImportSettingsResult::LimitExceeded;
        u64 largestCompositionId = 0u;
        for (u32 index = 0; index < settings.compositionNodes.Size(); ++index)
        {
            const ModelImportCompositionNodeCorrespondence& value = settings.compositionNodes[index];
            if (value.stableNodeId == 0u || value.sourceNodeIndex == InvalidModelImportIndex ||
                value.source.sourceKind > ModelImportSourceKind::Fbx || value.source.sourceName.Length() > MaximumModelSettingsStringBytes ||
                HasEmbeddedNull(value.source.sourceName) || (value.source.nativeKeyStable && value.source.nativeKey == 0u) ||
                value.parentStableNodeId == value.stableNodeId)
                return ModelImportSettingsResult::InvalidData;
            largestCompositionId = std::max(largestCompositionId, value.stableNodeId);
            stableNodeOrder[index] = index;
            sourceNodeOrder[index] = index;
        }
        std::sort(stableNodeOrder.TypedData(), stableNodeOrder.TypedData() + stableNodeOrder.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return settings.compositionNodes[left].stableNodeId < settings.compositionNodes[right].stableNodeId;
        });
        std::sort(sourceNodeOrder.TypedData(), sourceNodeOrder.TypedData() + sourceNodeOrder.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return settings.compositionNodes[left].sourceNodeIndex < settings.compositionNodes[right].sourceNodeIndex;
        });
        for (u32 index = 1; index < stableNodeOrder.Size(); ++index)
            if (settings.compositionNodes[stableNodeOrder[index - 1u]].stableNodeId == settings.compositionNodes[stableNodeOrder[index]].stableNodeId)
                return ModelImportSettingsResult::InvalidData;
        for (u32 index = 1; index < sourceNodeOrder.Size(); ++index)
            if (settings.compositionNodes[sourceNodeOrder[index - 1u]].sourceNodeIndex == settings.compositionNodes[sourceNodeOrder[index]].sourceNodeIndex)
                return ModelImportSettingsResult::InvalidData;

        const auto findStableNode = [&settings, &stableNodeOrder](const u64 stableId) noexcept -> u32
        {
            u32 begin = 0u;
            u32 end = stableNodeOrder.Size();
            while (begin < end)
            {
                const u32 middle = begin + (end - begin) / 2u;
                const u64 candidate = settings.compositionNodes[stableNodeOrder[middle]].stableNodeId;
                if (candidate < stableId)
                    begin = middle + 1u;
                else
                    end = middle;
            }
            return begin < stableNodeOrder.Size() && settings.compositionNodes[stableNodeOrder[begin]].stableNodeId == stableId
                       ? stableNodeOrder[begin]
                       : InvalidModelImportIndex;
        };

        containers::DynamicArray<u8> hierarchyState(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> hierarchyPath(memory::pools::Assets::GetInstance());
        hierarchyState.Resize(settings.compositionNodes.Size());
        hierarchyPath.Reserve(settings.compositionNodes.Size());
        if (hierarchyState.Size() != settings.compositionNodes.Size() || hierarchyPath.Capacity() < settings.compositionNodes.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < hierarchyState.Size(); ++index)
            hierarchyState[index] = 0u;
        for (u32 start = 0; start < settings.compositionNodes.Size(); ++start)
        {
            if (hierarchyState[start] == 2u)
                continue;
            hierarchyPath.Clear();
            u32 cursor = start;
            while (cursor != InvalidModelImportIndex && hierarchyState[cursor] == 0u)
            {
                hierarchyState[cursor] = 1u;
                hierarchyPath.PushBack(cursor);
                const u64 parent = settings.compositionNodes[cursor].parentStableNodeId;
                cursor = parent == 0u ? InvalidModelImportIndex : findStableNode(parent);
                if (parent != 0u && cursor == InvalidModelImportIndex)
                    return ModelImportSettingsResult::InvalidData;
            }
            if (cursor != InvalidModelImportIndex && hierarchyState[cursor] == 1u)
                return ModelImportSettingsResult::InvalidData;
            for (const u32 pathIndex : hierarchyPath)
                hierarchyState[pathIndex] = 2u;
        }

        containers::DynamicArray<u32> stableMeshOrder(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> sourceMeshOrder(memory::pools::Assets::GetInstance());
        stableMeshOrder.Resize(settings.compositionMeshes.Size());
        sourceMeshOrder.Resize(settings.compositionMeshes.Size());
        if (stableMeshOrder.Size() != settings.compositionMeshes.Size() || sourceMeshOrder.Size() != settings.compositionMeshes.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < settings.compositionMeshes.Size(); ++index)
        {
            const ModelImportCompositionMeshCorrespondence& value = settings.compositionMeshes[index];
            if (value.stableComponentId == 0u || value.stableNodeId == 0u || value.geometryPersistentId == 0u ||
                value.sourceInstanceIndex == InvalidModelImportIndex || findStableNode(value.stableNodeId) == InvalidModelImportIndex)
                return ModelImportSettingsResult::InvalidData;
            largestCompositionId = std::max(largestCompositionId, value.stableComponentId);
            stableMeshOrder[index] = index;
            sourceMeshOrder[index] = index;
        }
        std::sort(stableMeshOrder.TypedData(), stableMeshOrder.TypedData() + stableMeshOrder.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return settings.compositionMeshes[left].stableComponentId < settings.compositionMeshes[right].stableComponentId;
        });
        std::sort(sourceMeshOrder.TypedData(), sourceMeshOrder.TypedData() + sourceMeshOrder.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return settings.compositionMeshes[left].sourceInstanceIndex < settings.compositionMeshes[right].sourceInstanceIndex;
        });
        for (u32 index = 1; index < stableMeshOrder.Size(); ++index)
            if (settings.compositionMeshes[stableMeshOrder[index - 1u]].stableComponentId ==
                settings.compositionMeshes[stableMeshOrder[index]].stableComponentId)
                return ModelImportSettingsResult::InvalidData;
        for (u32 index = 1; index < sourceMeshOrder.Size(); ++index)
            if (settings.compositionMeshes[sourceMeshOrder[index - 1u]].sourceInstanceIndex ==
                settings.compositionMeshes[sourceMeshOrder[index]].sourceInstanceIndex)
                return ModelImportSettingsResult::InvalidData;
        for (const u32 meshIndex : stableMeshOrder)
            if (findStableNode(settings.compositionMeshes[meshIndex].stableComponentId) != InvalidModelImportIndex)
                return ModelImportSettingsResult::InvalidData;
        if (settings.nextCompositionNodeId <= largestCompositionId)
            return ModelImportSettingsResult::InvalidData;

        for (u32 index = 0; index < settings.textureResolutions.Size(); ++index)
        {
            if (!ValidTextureResolution(settings.textureResolutions[index]))
                return ModelImportSettingsResult::InvalidData;
            for (u32 other = index + 1u; other < settings.textureResolutions.Size(); ++other)
                if (settings.textureResolutions[other].authoredPath == settings.textureResolutions[index].authoredPath)
                    return ModelImportSettingsResult::InvalidData;
        }
        return ModelImportSettingsResult::Success;
    }

    ModelImportSettingsResult EncodeModelImportSettings(const ModelImportSettings& settings,
                                                        containers::DynamicArray<u8>& output) noexcept
    {
        if (ValidateModelImportSettings(settings) != ModelImportSettingsResult::Success)
            return ModelImportSettingsResult::InvalidData;

        containers::DynamicArray<u8> encoded(memory::pools::Assets::GetInstance());
        ModelSettingsWriter writer(encoded);
        if (!writer.U32(ModelSettingsMagic) || !writer.U16(ModelSettingsWireVersion) || !writer.U16(0u) ||
            !writer.U64(settings.nextOutputId) || !writer.U64(settings.nextMaterialSlotId) || !writer.U64(settings.nextCompositionNodeId) ||
            !writer.Digest(settings.sourceFingerprint) ||
            !writer.F32(settings.mesh.uniformScale) || !writer.U8(static_cast<u8>(settings.mesh.sourceUpAxis)) ||
            !writer.Bool(settings.mesh.generateNormals) || !writer.Bool(settings.mesh.generateTangents) || !writer.Bool(settings.mesh.flipUVs) ||
            !writer.Bool(settings.mesh.flipWinding) || !writer.Bool(settings.mesh.strictDependencies) ||
            !writer.F32(settings.mesh.smoothingAngleDegrees) || !writer.U32(settings.mesh.maximumSubmeshes) ||
            !writer.U32(settings.mesh.maximumVerticesPerSubmesh) || !writer.U32(settings.mesh.maximumIndicesPerSubmesh) ||
            !writer.U32(settings.mesh.maximumJoints) || !writer.Reference(settings.mesh.defaultMaterial) || !writer.Reference(settings.mesh.skeleton) ||
            !writer.Bool(settings.cook.optimizeVertexCache) || !writer.Bool(settings.cook.optimizeOverdraw) ||
            !writer.Bool(settings.cook.optimizeVertexFetch) || !writer.Bool(settings.cook.lockLodBorders) ||
            !writer.Bool(settings.cook.regularizeLodTriangles) || !writer.F32(settings.cook.overdrawThreshold) ||
            !writer.U64(settings.cook.meshCookingProfile) || !writer.F32(settings.cook.lodAttributeWeights.normal) ||
            !writer.F32(settings.cook.lodAttributeWeights.tangent) || !writer.F32(settings.cook.lodAttributeWeights.texCoord) ||
            !writer.F32(settings.cook.lodAttributeWeights.color) || !writer.F32(settings.cook.lodAttributeWeights.jointWeights) ||
            !writer.F32(settings.cook.lodAttributeWeights.morphPosition) || !writer.U32(settings.cook.maximumSubmeshes) ||
            !writer.U32(settings.cook.maximumVertexStreamsPerSubmesh) || !writer.U32(settings.cook.maximumVerticesPerSubmesh) ||
            !writer.U32(settings.cook.maximumIndicesPerSubmesh) || !writer.U32(settings.cook.maximumLodLevels) ||
            !writer.U32(settings.cook.lodLevels.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const LodLevelSettings& lod : settings.cook.lodLevels)
            if (!writer.F32(lod.triangleRatio) || !writer.F32(lod.maximumNormalizedError) || !writer.F32(lod.minimumScreenCoverage))
                return ModelImportSettingsResult::LimitExceeded;
        if (!writer.U8(static_cast<u8>(settings.materialPolicy)) || !writer.U8(static_cast<u8>(settings.embeddedTexturePolicy)) ||
            !writer.U16(0u) || !writer.U32(settings.materialMapperVersion) || !writer.Bool(settings.includeComposition) ||
            !writer.U8(0u) || !writer.U16(0u))
            return ModelImportSettingsResult::LimitExceeded;

        containers::DynamicArray<u32> order(memory::pools::Assets::GetInstance());
        order.Resize(settings.outputs.Size());
        if (order.Size() != settings.outputs.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return CorrespondenceLess(settings.outputs[left], settings.outputs[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportOutputCorrespondence& value = settings.outputs[index];
            if (!writer.U8(static_cast<u8>(value.kind)) || !writer.U64(value.persistentId) || !EncodeAssetOutput(writer, value.output) ||
                !EncodeEvidence(writer, value.source))
                return ModelImportSettingsResult::LimitExceeded;
        }

        order.Resize(settings.retiredOutputs.Size());
        if (order.Size() != settings.retiredOutputs.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return RetiredOutputLess(settings.retiredOutputs[left], settings.retiredOutputs[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportRetiredOutput& value = settings.retiredOutputs[index];
            if (!writer.U8(static_cast<u8>(value.kind)) || !writer.U64(value.persistentId) || !EncodeAssetOutput(writer, value.output))
                return ModelImportSettingsResult::LimitExceeded;
        }

        order.Resize(settings.materialSlots.Size());
        if (order.Size() != settings.materialSlots.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return SlotLess(settings.materialSlots[left], settings.materialSlots[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportMaterialSlotCorrespondence& value = settings.materialSlots[index];
            if (!writer.U64(value.stableSlotId) || !writer.U64(value.geometryPersistentId) || !writer.U32(value.sourcePartitionOrdinal) ||
                !writer.U64(value.materialPersistentId) || !writer.Digest(value.structuralKey) || !writer.Digest(value.partitionKey))
                return ModelImportSettingsResult::LimitExceeded;
        }

        order.Resize(settings.retiredMaterialSlots.Size());
        if (order.Size() != settings.retiredMaterialSlots.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return RetiredSlotLess(settings.retiredMaterialSlots[left], settings.retiredMaterialSlots[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
            if (!writer.U64(settings.retiredMaterialSlots[index].stableSlotId))
                return ModelImportSettingsResult::LimitExceeded;

        order.Resize(settings.compositionNodes.Size());
        if (order.Size() != settings.compositionNodes.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return CompositionNodeLess(settings.compositionNodes[left], settings.compositionNodes[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportCompositionNodeCorrespondence& value = settings.compositionNodes[index];
            if (!writer.U64(value.stableNodeId) || !writer.U64(value.parentStableNodeId) || !writer.U32(value.sourceNodeIndex) ||
                !EncodeEvidence(writer, value.source))
                return ModelImportSettingsResult::LimitExceeded;
        }

        order.Resize(settings.compositionMeshes.Size());
        if (order.Size() != settings.compositionMeshes.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return CompositionMeshLess(settings.compositionMeshes[left], settings.compositionMeshes[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportCompositionMeshCorrespondence& value = settings.compositionMeshes[index];
            if (!writer.U64(value.stableComponentId) || !writer.U64(value.stableNodeId) || !writer.U64(value.geometryPersistentId) ||
                !writer.U32(value.sourceInstanceIndex) || !writer.U32(value.sameGeometryOrdinal))
                return ModelImportSettingsResult::LimitExceeded;
        }

        order.Resize(settings.textureResolutions.Size());
        if (order.Size() != settings.textureResolutions.Size())
            return ModelImportSettingsResult::LimitExceeded;
        for (u32 index = 0; index < order.Size(); ++index)
            order[index] = index;
        std::sort(order.TypedData(), order.TypedData() + order.Size(), [&settings](const u32 left, const u32 right) noexcept
        {
            return TextureResolutionLess(settings.textureResolutions[left], settings.textureResolutions[right]);
        });
        if (!writer.U32(order.Size()))
            return ModelImportSettingsResult::LimitExceeded;
        for (const u32 index : order)
        {
            const ModelImportTextureResolution& value = settings.textureResolutions[index];
            if (!writer.String(value.authoredPath) || !writer.U8(static_cast<u8>(value.resolution)) || !writer.U8(0u) || !writer.U16(0u) ||
                !writer.Reference(value.managedResource) || !writer.Path(value.projectSourceIdentity))
                return ModelImportSettingsResult::LimitExceeded;
        }

        if (encoded.Size() > assets::MaximumAssetSettingsBytes)
            return ModelImportSettingsResult::LimitExceeded;
        output = static_cast<containers::DynamicArray<u8>&&>(encoded);
        return ModelImportSettingsResult::Success;
    }

    ModelImportSettingsResult DecodeModelImportSettings(const u32 settingsVersion, const containers::ArraySpan<const u8> bytes,
                                                        ModelImportSettings& output) noexcept
    {
        if (settingsVersion != ModelImportSettingsVersion)
            return ModelImportSettingsResult::UnsupportedVersion;
        if ((bytes.Empty() && bytes.Data() != nullptr) || (!bytes.Empty() && bytes.Data() == nullptr) ||
            bytes.Size() > assets::MaximumAssetSettingsBytes)
            return ModelImportSettingsResult::InvalidArgument;

        ModelSettingsReader reader(bytes);
        ModelImportSettings staged;
        u32 magic = 0;
        u16 wireVersion = 0;
        u16 reserved16 = 0;
        u8 sourceAxis = 0;
        if (!reader.U32(magic) || !reader.U16(wireVersion) || !reader.U16(reserved16) || magic != ModelSettingsMagic ||
            (wireVersion != LegacyModelSettingsWireVersion && wireVersion != SourceFingerprintModelSettingsWireVersion &&
             wireVersion != CompositionNodeModelSettingsWireVersion && wireVersion != ModelSettingsWireVersion) || reserved16 != 0u ||
            !reader.U64(staged.nextOutputId) || !reader.U64(staged.nextMaterialSlotId) ||
            (wireVersion >= CompositionNodeModelSettingsWireVersion && !reader.U64(staged.nextCompositionNodeId)) ||
            (wireVersion >= SourceFingerprintModelSettingsWireVersion && !reader.Digest(staged.sourceFingerprint)) ||
            !reader.F32(staged.mesh.uniformScale) || !reader.U8(sourceAxis) ||
            sourceAxis > static_cast<u8>(SourceUpAxis::Z) || !reader.Bool(staged.mesh.generateNormals) ||
            !reader.Bool(staged.mesh.generateTangents) || !reader.Bool(staged.mesh.flipUVs) || !reader.Bool(staged.mesh.flipWinding) ||
            !reader.Bool(staged.mesh.strictDependencies) || !reader.F32(staged.mesh.smoothingAngleDegrees) ||
            !reader.U32(staged.mesh.maximumSubmeshes) || !reader.U32(staged.mesh.maximumVerticesPerSubmesh) ||
            !reader.U32(staged.mesh.maximumIndicesPerSubmesh) || !reader.U32(staged.mesh.maximumJoints) ||
            !reader.Reference(staged.mesh.defaultMaterial) || !reader.Reference(staged.mesh.skeleton))
            return ModelImportSettingsResult::InvalidData;
        staged.mesh.sourceUpAxis = static_cast<SourceUpAxis>(sourceAxis);

        if (!reader.Bool(staged.cook.optimizeVertexCache) || !reader.Bool(staged.cook.optimizeOverdraw) ||
            !reader.Bool(staged.cook.optimizeVertexFetch) || !reader.Bool(staged.cook.lockLodBorders) ||
            !reader.Bool(staged.cook.regularizeLodTriangles) || !reader.F32(staged.cook.overdrawThreshold) ||
            !reader.U64(staged.cook.meshCookingProfile) || !reader.F32(staged.cook.lodAttributeWeights.normal) ||
            !reader.F32(staged.cook.lodAttributeWeights.tangent) || !reader.F32(staged.cook.lodAttributeWeights.texCoord) ||
            !reader.F32(staged.cook.lodAttributeWeights.color) || !reader.F32(staged.cook.lodAttributeWeights.jointWeights) ||
            !reader.F32(staged.cook.lodAttributeWeights.morphPosition) || !reader.U32(staged.cook.maximumSubmeshes) ||
            !reader.U32(staged.cook.maximumVertexStreamsPerSubmesh) || !reader.U32(staged.cook.maximumVerticesPerSubmesh) ||
            !reader.U32(staged.cook.maximumIndicesPerSubmesh) || !reader.U32(staged.cook.maximumLodLevels))
            return ModelImportSettingsResult::InvalidData;
        u32 lodCount = 0;
        if (!reader.U32(lodCount) || lodCount > staged.cook.maximumLodLevels || lodCount > 65536u)
            return ModelImportSettingsResult::InvalidData;
        staged.cook.lodLevels.Resize(lodCount);
        if (staged.cook.lodLevels.Size() != lodCount)
            return ModelImportSettingsResult::LimitExceeded;
        for (LodLevelSettings& lod : staged.cook.lodLevels)
            if (!reader.F32(lod.triangleRatio) || !reader.F32(lod.maximumNormalizedError) || !reader.F32(lod.minimumScreenCoverage))
                return ModelImportSettingsResult::InvalidData;

        u8 materialPolicy = 0;
        u8 embeddedPolicy = 0;
        u8 reserved8 = 0;
        if (!reader.U8(materialPolicy) || !reader.U8(embeddedPolicy) || !reader.U16(reserved16) || reserved16 != 0u ||
            materialPolicy > static_cast<u8>(ModelImportMaterialPolicy::UseDefaultMaterial) ||
            embeddedPolicy > static_cast<u8>(ModelImportEmbeddedTexturePolicy::Ignore) ||
            !reader.U32(staged.materialMapperVersion) || !reader.Bool(staged.includeComposition) || !reader.U8(reserved8) ||
            !reader.U16(reserved16) || reserved8 != 0u || reserved16 != 0u)
            return ModelImportSettingsResult::InvalidData;
        staged.materialPolicy = static_cast<ModelImportMaterialPolicy>(materialPolicy);
        staged.embeddedTexturePolicy = static_cast<ModelImportEmbeddedTexturePolicy>(embeddedPolicy);

        u32 count = 0;
        if (!reader.U32(count) || count > assets::MaximumAssetOutputs)
            return ModelImportSettingsResult::InvalidData;
        staged.outputs.Resize(count);
        if (staged.outputs.Size() != count)
            return ModelImportSettingsResult::LimitExceeded;
        for (ModelImportOutputCorrespondence& value : staged.outputs)
        {
            u8 kind = 0;
            if (!reader.U8(kind) || kind > static_cast<u8>(ModelImportElementKind::Composition) || !reader.U64(value.persistentId) ||
                !DecodeAssetOutput(reader, value.output) || !DecodeEvidence(reader, value.source))
                return ModelImportSettingsResult::InvalidData;
            value.kind = static_cast<ModelImportElementKind>(kind);
        }

        if (!reader.U32(count) || count > assets::MaximumAssetOutputs)
            return ModelImportSettingsResult::InvalidData;
        staged.retiredOutputs.Resize(count);
        if (staged.retiredOutputs.Size() != count)
            return ModelImportSettingsResult::LimitExceeded;
        for (ModelImportRetiredOutput& value : staged.retiredOutputs)
        {
            u8 kind = 0;
            if (!reader.U8(kind) || kind > static_cast<u8>(ModelImportElementKind::Composition) || !reader.U64(value.persistentId) ||
                !DecodeAssetOutput(reader, value.output))
                return ModelImportSettingsResult::InvalidData;
            value.kind = static_cast<ModelImportElementKind>(kind);
        }

        if (!reader.U32(count) || count > 65536u)
            return ModelImportSettingsResult::InvalidData;
        staged.materialSlots.Resize(count);
        if (staged.materialSlots.Size() != count)
            return ModelImportSettingsResult::LimitExceeded;
        for (ModelImportMaterialSlotCorrespondence& value : staged.materialSlots)
            if (!reader.U64(value.stableSlotId) || !reader.U64(value.geometryPersistentId) || !reader.U32(value.sourcePartitionOrdinal) ||
                !reader.U64(value.materialPersistentId) || !reader.Digest(value.structuralKey) || !reader.Digest(value.partitionKey))
                return ModelImportSettingsResult::InvalidData;

        if (!reader.U32(count) || count > 65536u)
            return ModelImportSettingsResult::InvalidData;
        staged.retiredMaterialSlots.Resize(count);
        if (staged.retiredMaterialSlots.Size() != count)
            return ModelImportSettingsResult::LimitExceeded;
        for (ModelImportRetiredMaterialSlot& value : staged.retiredMaterialSlots)
            if (!reader.U64(value.stableSlotId))
                return ModelImportSettingsResult::InvalidData;

        if (wireVersion >= CompositionNodeModelSettingsWireVersion)
        {
            if (!reader.U32(count) || count > (1u << 20u))
                return ModelImportSettingsResult::InvalidData;
            staged.compositionNodes.Resize(count);
            if (staged.compositionNodes.Size() != count)
                return ModelImportSettingsResult::LimitExceeded;
            for (ModelImportCompositionNodeCorrespondence& value : staged.compositionNodes)
                if (!reader.U64(value.stableNodeId) || !reader.U64(value.parentStableNodeId) || !reader.U32(value.sourceNodeIndex) ||
                    !DecodeEvidence(reader, value.source))
                    return ModelImportSettingsResult::InvalidData;
        }

        if (wireVersion >= ModelSettingsWireVersion)
        {
            if (!reader.U32(count) || count > (1u << 20u))
                return ModelImportSettingsResult::InvalidData;
            staged.compositionMeshes.Resize(count);
            if (staged.compositionMeshes.Size() != count)
                return ModelImportSettingsResult::LimitExceeded;
            for (ModelImportCompositionMeshCorrespondence& value : staged.compositionMeshes)
                if (!reader.U64(value.stableComponentId) || !reader.U64(value.stableNodeId) || !reader.U64(value.geometryPersistentId) ||
                    !reader.U32(value.sourceInstanceIndex) || !reader.U32(value.sameGeometryOrdinal))
                    return ModelImportSettingsResult::InvalidData;
        }

        if (!reader.U32(count) || count > 65536u)
            return ModelImportSettingsResult::InvalidData;
        staged.textureResolutions.Resize(count);
        if (staged.textureResolutions.Size() != count)
            return ModelImportSettingsResult::LimitExceeded;
        for (ModelImportTextureResolution& value : staged.textureResolutions)
        {
            u8 resolution = 0;
            if (!reader.String(value.authoredPath) || !reader.U8(resolution) ||
                resolution > static_cast<u8>(ModelImportTextureResolutionKind::IgnoreMissing) || !reader.U8(reserved8) ||
                !reader.U16(reserved16) || reserved8 != 0u || reserved16 != 0u || !reader.Reference(value.managedResource) ||
                !reader.Path(value.projectSourceIdentity))
                return ModelImportSettingsResult::InvalidData;
            value.resolution = static_cast<ModelImportTextureResolutionKind>(resolution);
        }

        if (reader.Remaining() != 0u)
            return ModelImportSettingsResult::InvalidData;
        CanonicalizeSettings(staged);
        const ModelImportSettingsResult valid = ValidateModelImportSettings(staged);
        if (valid != ModelImportSettingsResult::Success)
            return valid;
        output = static_cast<ModelImportSettings&&>(staged);
        return ModelImportSettingsResult::Success;
    }

    ModelImportPlan::ModelImportPlan() noexcept
        : m_outputs(memory::pools::Assets::GetInstance()), m_reconciliation(memory::pools::Assets::GetInstance()),
          m_ambiguousCandidates(memory::pools::Assets::GetInstance()),
          m_materialSlotReconciliation(memory::pools::Assets::GetInstance()),
          m_textureResolutions(memory::pools::Assets::GetInstance()),
          m_dependencyWitnesses(memory::pools::Assets::GetInstance()), m_blockers(memory::pools::Assets::GetInstance())
    {
    }

    const assets::SourceImportDescription& ModelImportPlan::GetImportDescription() const noexcept { return m_import; }
    const ModelImportPlanWitnesses& ModelImportPlan::GetWitnesses() const noexcept { return m_witnesses; }
    containers::ArraySpan<const ModelImportPlannedOutput> ModelImportPlan::GetOutputs() const noexcept
    {
        return {m_outputs.TypedData(), m_outputs.Size()};
    }
    containers::ArraySpan<const ModelImportReconciliationRecord> ModelImportPlan::GetReconciliation() const noexcept
    {
        return {m_reconciliation.TypedData(), m_reconciliation.Size()};
    }
    containers::ArraySpan<const ModelImportAmbiguousCandidate> ModelImportPlan::GetAmbiguousCandidates() const noexcept
    {
        return {m_ambiguousCandidates.TypedData(), m_ambiguousCandidates.Size()};
    }
    containers::ArraySpan<const ModelImportMaterialSlotReconciliation> ModelImportPlan::GetMaterialSlotReconciliation() const noexcept
    {
        return {m_materialSlotReconciliation.TypedData(), m_materialSlotReconciliation.Size()};
    }
    containers::ArraySpan<const ModelImportPlannedTextureResolution> ModelImportPlan::GetTextureResolutions() const noexcept
    {
        return {m_textureResolutions.TypedData(), m_textureResolutions.Size()};
    }
    containers::ArraySpan<const ModelImportDependencyWitness> ModelImportPlan::GetDependencyWitnesses() const noexcept
    {
        return {m_dependencyWitnesses.TypedData(), m_dependencyWitnesses.Size()};
    }
    containers::ArraySpan<const ModelImportPlanBlocker> ModelImportPlan::GetBlockers() const noexcept
    {
        return {m_blockers.TypedData(), m_blockers.Size()};
    }
    bool ModelImportPlan::HasBlockingConflicts() const noexcept { return !m_blockers.Empty(); }

    ModelImportPlanResult PlanModelImport(const ModelImportManifest& manifest, const ModelImportSettings& candidateSettings,
                                          const ModelImportPlanningContext& context, ModelImportPlan& output,
                                          ModelImportPlanReport* const report) noexcept
    {
        if (report != nullptr)
            *report = {};

        if (!context.limits.IsValid() || !context.parentAsset.IsValid() || context.importer == assets::InvalidSourceImporterId ||
            context.importerVersion == 0u || !context.sourceOrigin.IsFilePath() || !context.sourceDestination.IsFilePath() ||
            !context.sourceRoot.IsDirectoryPath() ||
            !context.sourceIdentity.IsValid() || context.acceptedStateGeneration == 0u || manifest.geometryDefinitions.Empty() ||
            manifest.sourceFingerprint.IsEmpty() || !SourceIdentityMatchesDestination(context) || !PlanningRootIsWritable(context))
        {
            if (report != nullptr)
                report->diagnostic = "Invalid model import planning inputs.";
            return ModelImportPlanResult::InvalidArgument;
        }
        if (ValidateModelImportSettings(candidateSettings) != ModelImportSettingsResult::Success)
        {
            if (report != nullptr)
                report->diagnostic = "Candidate model import settings are invalid.";
            return ModelImportPlanResult::SettingsInvalid;
        }
        if (FindMeshCookingProfile(candidateSettings.cook.meshCookingProfile) == nullptr)
        {
            if (report != nullptr)
                report->diagnostic = "Candidate model import settings select an unavailable mesh cooking profile.";
            return ModelImportPlanResult::SettingsInvalid;
        }

        containers::DynamicArray<u8> candidateSettingsBytes(memory::pools::Assets::GetInstance());
        if (EncodeModelImportSettings(candidateSettings, candidateSettingsBytes) != ModelImportSettingsResult::Success)
            return ModelImportPlanResult::LimitExceeded;

        ModelImportSettings oldSettings;
        const bool reimport = context.persistedMetadata != nullptr;
        if (reimport)
        {
            if (!context.hasPersistedMetadataDigest || context.persistedMetadataDigest.IsEmpty() ||
                !MetadataDigestMatchesSnapshot(*context.persistedMetadata, context.persistedMetadataDigest) ||
                !(context.sourceOrigin == context.sourceDestination))
            {
                if (report != nullptr)
                    report->diagnostic = "Reimport planning requires an exact persisted metadata snapshot digest witness.";
                return ModelImportPlanResult::InvalidArgument;
            }
            const assets::AssetMetadata& metadata = *context.persistedMetadata;
            if (!(metadata.id == context.parentAsset) || metadata.importer != context.importer ||
                metadata.importerVersion != context.importerVersion || metadata.settingsVersion != ModelImportSettingsVersion ||
                DecodeModelImportSettings(metadata.settingsVersion, {metadata.settings.TypedData(), metadata.settings.Size()}, oldSettings) !=
                    ModelImportSettingsResult::Success ||
                !SettingsOutputsMatchMetadata(oldSettings, metadata))
            {
                if (report != nullptr)
                    report->diagnostic = "Persisted model metadata does not match the planning identity/settings schema.";
                return ModelImportPlanResult::MetadataMismatch;
            }
            if (context.database != nullptr)
            {
                constexpr u32 IdentityIssues = assets::AssetIdentityConflict | assets::OutputIdentityConflict |
                                               assets::SourceIdentityConflict | assets::SourceMissing;
                const assets::SourceRecord* const live = context.database->LookupAsset(context.parentAsset);
                const assets::SourceRecord* const byPath = context.database->LookupResource(context.sourceDestination);
                if (live == nullptr || byPath != live || live->metadataResult != assets::MetadataResult::Success ||
                    !(live->metadata.id == context.parentAsset) || !(live->path == context.sourceDestination) ||
                    !(live->sourceIdentity == context.sourceIdentity) || (live->issues & IdentityIssues) != 0u ||
                    !SameMetadataSnapshot(*context.persistedMetadata, live->metadata))
                {
                    if (report != nullptr)
                        report->diagnostic = "The live project catalog no longer matches the persisted model source identity.";
                    return ModelImportPlanResult::MetadataMismatch;
                }
            }
        }
        else
        {
            bool firstImportIdentityConflict = context.hasPersistedMetadataDigest;
            if (context.database != nullptr)
            {
                firstImportIdentityConflict = firstImportIdentityConflict || context.database->ContainsAssetIdentity(context.parentAsset);
                const assets::SourceRecord* const byPath = context.database->LookupResource(context.sourceDestination);
                const assets::SourceRecord* const byIdentity = context.database->LookupSource(context.sourceIdentity);
                if (byPath != nullptr)
                {
                    constexpr u32 ConflictingDestinationIssues = assets::SourceMissing | assets::MetadataUnreadable |
                                                                 assets::MetadataInvalid | assets::AssetIdentityConflict |
                                                                 assets::OutputIdentityConflict | assets::SourceIdentityConflict;
                    const bool existingUnmanagedSource = context.sourceOrigin == context.sourceDestination &&
                                                         byPath->sourceIdentity == context.sourceIdentity &&
                                                         (byPath->issues & assets::MetadataMissing) != 0u &&
                                                         (byPath->issues & ConflictingDestinationIssues) == 0u;
                    firstImportIdentityConflict = firstImportIdentityConflict || !existingUnmanagedSource;
                }
                if (byIdentity != nullptr && byIdentity != byPath)
                    firstImportIdentityConflict = true;
                if (byIdentity == nullptr)
                {
                    for (const assets::SourceRecord& source : context.database->GetResources())
                    {
                        if (source.sourceIdentity == context.sourceIdentity &&
                            (source.path != context.sourceDestination || (source.issues & assets::SourceIdentityConflict) != 0u))
                        {
                            firstImportIdentityConflict = true;
                            break;
                        }
                    }
                }
            }
            if (firstImportIdentityConflict)
            {
                if (report != nullptr)
                    report->diagnostic = "The proposed first-import identity or source destination conflicts with the live project catalog.";
                return ModelImportPlanResult::IdentityConflict;
            }
            if (DecodeModelImportSettings(ModelImportSettingsVersion,
                                          {candidateSettingsBytes.TypedData(), candidateSettingsBytes.Size()}, oldSettings) !=
                ModelImportSettingsResult::Success)
            {
                return ModelImportPlanResult::SettingsInvalid;
            }
        }

        ModelImportSettings nextSettings;
        containers::DynamicArray<u8> clonedCandidate(memory::pools::Assets::GetInstance());
        if (!CloneSettings(candidateSettings, nextSettings, clonedCandidate))
            return ModelImportPlanResult::SettingsInvalid;
        if (reimport && !SamePersistentIdentityState(nextSettings, oldSettings))
        {
            if (report != nullptr)
                report->diagnostic = "Candidate settings cannot directly rewrite persisted model identity correspondence.";
            return ModelImportPlanResult::SettingsInvalid;
        }

        nextSettings.nextOutputId = std::max(nextSettings.nextOutputId, oldSettings.nextOutputId);
        nextSettings.nextMaterialSlotId = std::max(nextSettings.nextMaterialSlotId, oldSettings.nextMaterialSlotId);
        nextSettings.nextCompositionNodeId = std::max(nextSettings.nextCompositionNodeId, oldSettings.nextCompositionNodeId);
        for (const ModelImportRetiredOutput& retired : oldSettings.retiredOutputs)
        {
            bool found = false;
            for (const ModelImportRetiredOutput& existing : nextSettings.retiredOutputs)
                found = found || (existing.persistentId == retired.persistentId && existing.output.key == retired.output.key &&
                                  existing.output.type == retired.output.type);
            if (!found)
            {
                const u32 previous = nextSettings.retiredOutputs.Size();
                ModelImportRetiredOutput& copy = nextSettings.retiredOutputs.EmplaceBack();
                if (nextSettings.retiredOutputs.Size() != previous + 1u)
                    return ModelImportPlanResult::LimitExceeded;
                copy.kind = retired.kind;
                copy.persistentId = retired.persistentId;
                copy.output.key = retired.output.key;
                copy.output.type = retired.output.type;
            }
        }
        for (const ModelImportRetiredMaterialSlot& retired : oldSettings.retiredMaterialSlots)
            if (!AppendRetiredSlot(nextSettings, retired.stableSlotId))
                return ModelImportPlanResult::LimitExceeded;

        nextSettings.outputs.Clear();
        nextSettings.materialSlots.Clear();
        nextSettings.textureResolutions.Clear();

        // Dormant composition identity still needs current-source evidence so a later
        // re-enable preserves the authored/component IDs without retaining stale locators.
        const bool preserveDisabledCompositionIdentity =
            reimport && (!oldSettings.compositionNodes.Empty() || !oldSettings.compositionMeshes.Empty());
        const bool reconcileCompositionIdentity = candidateSettings.includeComposition || preserveDisabledCompositionIdentity;
        u64 currentElementCount = manifest.geometryDefinitions.Size();
        if (candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned)
            currentElementCount += manifest.materials.Size();
        if (candidateSettings.embeddedTexturePolicy == ModelImportEmbeddedTexturePolicy::ImportModelOwned)
            currentElementCount += manifest.embeddedTextures.Size();
        if (candidateSettings.includeComposition)
            ++currentElementCount;
        if (currentElementCount == 0u || currentElementCount > context.limits.maximumOutputs)
        {
            if (report != nullptr)
                report->diagnostic = "Current model outputs exceed planning limits.";
            return ModelImportPlanResult::LimitExceeded;
        }
        if (context.outputTypes.mesh == resources::InvalidResourceTypeId ||
            (candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned && !manifest.materials.Empty() &&
             context.outputTypes.material == resources::InvalidResourceTypeId) ||
            (candidateSettings.embeddedTexturePolicy == ModelImportEmbeddedTexturePolicy::ImportModelOwned &&
             !manifest.embeddedTextures.Empty() && context.outputTypes.texture == resources::InvalidResourceTypeId) ||
            (candidateSettings.includeComposition && context.outputTypes.composition == resources::InvalidResourceTypeId))
        {
            if (report != nullptr)
                report->diagnostic = "Current model outputs use unavailable output types.";
            return ModelImportPlanResult::LimitExceeded;
        }
        ModelPlanEvidenceIndex evidenceIndex;
        if (!BuildModelPlanEvidenceIndex(manifest, reconcileCompositionIdentity, evidenceIndex))
        {
            if (report != nullptr)
                report->diagnostic = "Current model evidence exceeds planning limits or has invalid geometry-instance topology.";
            return ModelImportPlanResult::LimitExceeded;
        }
        const bool needMaterialEvidence = candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned;
        const bool needEmbeddedTextureEvidence =
            candidateSettings.embeddedTexturePolicy == ModelImportEmbeddedTexturePolicy::ImportModelOwned &&
            !manifest.embeddedTextures.Empty();
        if (!PopulateModelPlanEvidenceCache(manifest, needMaterialEvidence, needEmbeddedTextureEvidence, evidenceIndex))
        {
            if (report != nullptr)
                report->diagnostic = "Current model evidence exceeds planning limits or has invalid source relationships.";
            return ModelImportPlanResult::LimitExceeded;
        }
        containers::DynamicArray<ModelPlanCurrentElement> current(memory::pools::Assets::GetInstance());
        if (!BuildCurrentElements(manifest, evidenceIndex, candidateSettings, context, current) || current.Size() > context.limits.maximumOutputs)
        {
            if (report != nullptr)
                report->diagnostic = "Current model outputs exceed planning limits or use unavailable output types.";
            return ModelImportPlanResult::LimitExceeded;
        }

        ModelImportPlan staged;
        struct ManagedSettingReference
        {
            resources::ResourceReference reference;
            resources::ResourceTypeId expectedType = resources::InvalidResourceTypeId;
        };
        const ManagedSettingReference managedSettingsReferences[]{{candidateSettings.mesh.defaultMaterial, context.outputTypes.material},
                                                                   {candidateSettings.mesh.skeleton, context.outputTypes.skeleton}};
        for (const ManagedSettingReference& selection : managedSettingsReferences)
        {
            const resources::ResourceReference reference = selection.reference;
            if (!reference.IsValid())
                continue;
            const ModelImportDependencyWitness* witness = nullptr;
            if (selection.expectedType == resources::InvalidResourceTypeId || reference.ExpectedType() != selection.expectedType ||
                !ValidateManagedDependencyWitness(context, reference, witness))
            {
                if (report != nullptr)
                    report->diagnostic = "A managed model setting reference no longer matches current project state.";
                return ModelImportPlanResult::SettingsInvalid;
            }
            if (!AppendDependencyWitness(staged, *witness, reference, context.limits.maximumDependencyWitnesses))
                return ModelImportPlanResult::LimitExceeded;
        }
        containers::DynamicArray<u8> claimed(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u64> currentPersistentIds(memory::pools::Assets::GetInstance());
        claimed.Resize(current.Size());
        currentPersistentIds.Resize(current.Size());
        if (claimed.Size() != current.Size() || currentPersistentIds.Size() != current.Size())
            return ModelImportPlanResult::LimitExceeded;
        for (u32 index = 0; index < current.Size(); ++index)
        {
            claimed[index] = 0u;
            currentPersistentIds[index] = 0u;
        }

        const auto addRetirementBlockers = [&](const ModelImportOutputCorrespondence& old, const u32 reconciliationIndex) noexcept -> bool
        {
            if (!reimport)
                return true;
            const u32 blockerStart = ModelImportPlanBuilderAccess::Blockers(staged).Size();
            const resources::ResourceReference target = assets::GetAssetOutputReference(context.parentAsset, old.output);
            if (context.database == nullptr)
            {
                ModelImportPlanBlocker blocker;
                blocker.kind = ModelImportPlanBlockerKind::ReferenceStateUnavailable;
                blocker.elementKind = old.kind;
                blocker.persistentId = old.persistentId;
                blocker.target = target;
                if (!AppendBlocker(staged, blocker, context.limits.maximumBlockers))
                    return false;
            }
            else
            {
                for (const assets::AssetReference& reference : context.database->GetBackwardReferences(target))
                {
                    if (reference.from == context.parentAsset)
                        continue;
                    ModelImportPlanBlocker blocker;
                    blocker.kind = ModelImportPlanBlockerKind::ExternalReference;
                    blocker.elementKind = old.kind;
                    blocker.persistentId = old.persistentId;
                    blocker.target = target;
                    blocker.referencer = reference.from;
                    blocker.referenceKind = reference.kind;
                    blocker.referenceLocation = reference.location;
                    blocker.referenceInPlace = reference.inPlace;
                    if (const assets::SourceRecord* const source = context.database->LookupAsset(reference.from))
                    {
                        blocker.referenceState = source->referenceScanState;
                        blocker.referencerSourceIdentity = source->sourceIdentity;
                        blocker.referencerPath = source->path;
                    }
                    if (!AppendBlocker(staged, blocker, context.limits.maximumBlockers))
                        return false;
                }
                for (const assets::SourceRecord& source : context.database->GetResources())
                {
                    if (source.referenceScanState == assets::SourceReferenceScanState::Scanned)
                        continue;
                    constexpr u32 PotentialManagedIssues = assets::MetadataMissing | assets::MetadataUnreadable | assets::MetadataInvalid |
                                                           assets::AssetIdentityConflict | assets::OutputIdentityConflict |
                                                           assets::SourceIdentityConflict;
                    const bool knownManaged = source.metadataResult == assets::MetadataResult::Success && source.metadata.id.IsValid();
                    if ((!knownManaged && (source.issues & PotentialManagedIssues) == 0u) ||
                        (knownManaged && source.metadata.id == context.parentAsset))
                        continue;
                    ModelImportPlanBlocker blocker;
                    blocker.kind = ModelImportPlanBlockerKind::ReferenceStateUnavailable;
                    blocker.elementKind = old.kind;
                    blocker.persistentId = old.persistentId;
                    blocker.target = target;
                    blocker.referencer = knownManaged ? source.metadata.id : assets::AssetId{};
                    blocker.referencerSourceIdentity = source.sourceIdentity;
                    blocker.referencerPath = source.path;
                    blocker.referenceState = source.referenceScanState;
                    if (!AppendBlocker(staged, blocker, context.limits.maximumBlockers))
                        return false;
                }
            }
            if (ModelImportPlanBuilderAccess::Blockers(staged).Size() != blockerStart &&
                reconciliationIndex < ModelImportPlanBuilderAccess::Reconciliation(staged).Size())
                ModelImportPlanBuilderAccess::Reconciliation(staged)[reconciliationIndex].blocking = true;
            return true;
        };

        containers::DynamicArray<u32> matches(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> allMatches(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> oldMatchBegins(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> oldMatchCounts(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> selectedCurrent(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> oldMatchStates(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> currentUnavailable(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> tierCurrentClaims(memory::pools::Assets::GetInstance());
        oldMatchBegins.Resize(oldSettings.outputs.Size());
        oldMatchCounts.Resize(oldSettings.outputs.Size());
        selectedCurrent.Resize(oldSettings.outputs.Size());
        oldMatchStates.Resize(oldSettings.outputs.Size());
        currentUnavailable.Resize(current.Size());
        tierCurrentClaims.Resize(current.Size());
        if (oldMatchBegins.Size() != oldSettings.outputs.Size() || oldMatchCounts.Size() != oldSettings.outputs.Size() ||
            selectedCurrent.Size() != oldSettings.outputs.Size() || oldMatchStates.Size() != oldSettings.outputs.Size() ||
            currentUnavailable.Size() != current.Size() || tierCurrentClaims.Size() != current.Size())
            return ModelImportPlanResult::LimitExceeded;
        for (u32 oldIndex = 0; oldIndex < oldSettings.outputs.Size(); ++oldIndex)
        {
            oldMatchBegins[oldIndex] = 0u;
            oldMatchCounts[oldIndex] = 0u;
            selectedCurrent[oldIndex] = InvalidModelImportIndex;
            oldMatchStates[oldIndex] = 0u;
        }
        for (u32 currentIndex = 0; currentIndex < current.Size(); ++currentIndex)
            currentUnavailable[currentIndex] = 0u;

        constexpr ModelEvidenceMatchTier MatchTiers[]{ModelEvidenceMatchTier::Native,
                                                      ModelEvidenceMatchTier::Structural,
                                                      ModelEvidenceMatchTier::Relationship};
        for (const ModelEvidenceMatchTier tier : MatchTiers)
        {
            for (u32 currentIndex = 0; currentIndex < current.Size(); ++currentIndex)
                tierCurrentClaims[currentIndex] = 0u;

            for (u32 oldIndex = 0; oldIndex < oldSettings.outputs.Size(); ++oldIndex)
            {
                if (oldMatchStates[oldIndex] != 0u)
                    continue;
                if (!FindTierMatches(oldSettings.outputs[oldIndex], current, currentUnavailable, tier, matches))
                    return ModelImportPlanResult::LimitExceeded;
                if (matches.Empty())
                    continue;
                if (matches.Size() > context.limits.maximumAmbiguousCandidates - allMatches.Size())
                    return ModelImportPlanResult::LimitExceeded;
                oldMatchBegins[oldIndex] = allMatches.Size();
                oldMatchCounts[oldIndex] = matches.Size();
                for (const u32 match : matches)
                {
                    if (match >= current.Size() || tierCurrentClaims[match] == ~u32{0})
                        return ModelImportPlanResult::LimitExceeded;
                    const u32 previous = allMatches.Size();
                    allMatches.PushBack(match);
                    if (allMatches.Size() != previous + 1u)
                        return ModelImportPlanResult::LimitExceeded;
                    ++tierCurrentClaims[match];
                }
            }

            // Resolve this evidence tier as one simultaneous graph. Stronger
            // matches are reserved before weaker tiers. Any non-1:1 component
            // is quarantined so weaker evidence cannot steal one of its current
            // candidates and silently change the ambiguity decision.
            for (u32 oldIndex = 0; oldIndex < oldSettings.outputs.Size(); ++oldIndex)
            {
                if (oldMatchStates[oldIndex] != 0u || oldMatchCounts[oldIndex] == 0u)
                    continue;
                const u32 begin = oldMatchBegins[oldIndex];
                const u32 count = oldMatchCounts[oldIndex];
                const u32 candidate = allMatches[begin];
                if (count == 1u && tierCurrentClaims[candidate] == 1u)
                {
                    oldMatchStates[oldIndex] = 1u;
                    selectedCurrent[oldIndex] = candidate;
                    currentUnavailable[candidate] = 1u;
                    continue;
                }
                oldMatchStates[oldIndex] = 2u;
                for (u32 offset = 0; offset < count; ++offset)
                    currentUnavailable[allMatches[begin + offset]] = 1u;
            }
        }

        for (u32 oldIndex = 0; oldIndex < oldSettings.outputs.Size(); ++oldIndex)
        {
            const ModelImportOutputCorrespondence& old = oldSettings.outputs[oldIndex];
            const u32 matchBegin = oldMatchBegins[oldIndex];
            const u32 matchCount = oldMatchCounts[oldIndex];

            u32 reconciliationIndex = InvalidModelImportIndex;
            if (oldMatchStates[oldIndex] == 0u)
            {
                if (!AppendReconciliation(staged, ModelImportReconcileState::Removed, &old, nullptr, nullptr, old.persistentId,
                                          context.limits.maximumReconciliationRecords, reconciliationIndex) ||
                    !AppendRetiredOutput(nextSettings, old) || !addRetirementBlockers(old, reconciliationIndex))
                    return ModelImportPlanResult::LimitExceeded;
                continue;
            }

            if (oldMatchStates[oldIndex] == 1u)
            {
                const u32 candidateIndex = selectedCurrent[oldIndex];
                if (candidateIndex >= current.Size())
                    return ModelImportPlanResult::LimitExceeded;
                const ModelPlanCurrentElement& candidate = current[candidateIndex];
                if (!AppendReconciliation(staged, ModelImportReconcileState::Preserved, &old, &candidate, &old.output,
                                          old.persistentId, context.limits.maximumReconciliationRecords, reconciliationIndex) ||
                    !AppendCorrespondence(nextSettings, candidate.kind, old.persistentId, old.output, candidate.source) ||
                    !AppendPlannedOutput(staged, candidate, old.persistentId, old.output, context.limits.maximumOutputs))
                    return ModelImportPlanResult::LimitExceeded;
                claimed[candidateIndex] = 1u;
                currentPersistentIds[candidateIndex] = old.persistentId;
                continue;
            }

            if (matchCount == 0u)
                return ModelImportPlanResult::LimitExceeded;
            if (!AppendReconciliation(staged, ModelImportReconcileState::Ambiguous, &old, nullptr, nullptr, old.persistentId,
                                      context.limits.maximumReconciliationRecords, reconciliationIndex) ||
                !AppendRetiredOutput(nextSettings, old) || !addRetirementBlockers(old, reconciliationIndex))
                return ModelImportPlanResult::LimitExceeded;
            for (u32 matchOffset = 0; matchOffset < matchCount; ++matchOffset)
            {
                const u32 match = allMatches[matchBegin + matchOffset];
                if (match >= current.Size() || ModelImportPlanBuilderAccess::AmbiguousCandidates(staged).Size() >=
                                                context.limits.maximumAmbiguousCandidates)
                    return ModelImportPlanResult::LimitExceeded;
                ModelImportAmbiguousCandidate& ambiguous = ModelImportPlanBuilderAccess::AmbiguousCandidates(staged).EmplaceBack();
                ambiguous.reconciliationIndex = reconciliationIndex;
                ambiguous.sourceIndex = current[match].sourceIndex;
                ambiguous.source = current[match].source;
                ambiguous.displayName = current[match].displayName;
            }
        }

        const auto isPersistedParentPath = [&](const resources::ResourcePath path) noexcept
        {
            if (!reimport)
                return false;
            for (const assets::AssetOutput& old : context.persistedMetadata->outputs)
                if (assets::GetAssetOutputResourcePath(context.parentAsset, old.key) == path)
                    return true;
            return false;
        };

        for (u32 currentIndex = 0; currentIndex < current.Size(); ++currentIndex)
        {
            if (claimed[currentIndex] != 0u)
                continue;
            if (nextSettings.nextOutputId == ~u64{0})
                return ModelImportPlanResult::LimitExceeded;
            const u64 persistentId = nextSettings.nextOutputId++;
            assets::AssetOutput proposed;
            proposed.type = current[currentIndex].outputType;
            if (!MakeOutputKey(current[currentIndex].kind, persistentId, proposed.key))
                return ModelImportPlanResult::LimitExceeded;
            const resources::ResourcePath path = assets::GetAssetOutputResourcePath(context.parentAsset, proposed.key);
            if (!path.IsValid() || (context.database != nullptr && context.database->ContainsOutputPath(path) && !isPersistedParentPath(path)))
            {
                if (report != nullptr)
                    report->diagnostic = "A proposed model output identity collides with an existing project output.";
                return ModelImportPlanResult::IdentityConflict;
            }
            u32 reconciliationIndex = InvalidModelImportIndex;
            if (!AppendReconciliation(staged, ModelImportReconcileState::Added, nullptr, &current[currentIndex], &proposed,
                                      persistentId, context.limits.maximumReconciliationRecords, reconciliationIndex) ||
                !AppendCorrespondence(nextSettings, current[currentIndex].kind, persistentId, proposed, current[currentIndex].source) ||
                !AppendPlannedOutput(staged, current[currentIndex], persistentId, proposed, context.limits.maximumOutputs))
                return ModelImportPlanResult::LimitExceeded;
            claimed[currentIndex] = 1u;
            currentPersistentIds[currentIndex] = persistentId;
        }

        // Once composition identity exists, keep reconciling it while composition is
        // temporarily disabled. This preserves durable authored/component IDs for a later
        // re-enable without leaving sourceNodeIndex/sourceInstanceIndex evidence stale
        // relative to the newly accepted sourceFingerprint.
        if (reconcileCompositionIdentity)
        {
            containers::DynamicArray<u64> geometryPersistentIds(memory::pools::Assets::GetInstance());
            if (!BuildCurrentGeometryPersistentIds(manifest, current, currentPersistentIds, geometryPersistentIds) ||
                !ReconcileCompositionNodes(manifest, evidenceIndex, geometryPersistentIds, oldSettings, nextSettings) ||
                !ReconcileCompositionMeshes(manifest, geometryPersistentIds, oldSettings, nextSettings))
                return ModelImportPlanResult::LimitExceeded;
        }
        else
        {
            nextSettings.compositionNodes.Clear();
            nextSettings.compositionMeshes.Clear();
        }

        if (context.database != nullptr)
        {
            for (const ModelImportPlannedOutput& planned : ModelImportPlanBuilderAccess::Outputs(staged))
            {
                const resources::ResourcePath path = assets::GetAssetOutputResourcePath(context.parentAsset, planned.output.key);
                if (!path.IsValid() || (context.database->ContainsOutputPath(path) && !isPersistedParentPath(path)))
                {
                    if (report != nullptr)
                        report->diagnostic = "A planned model output identity collides with an existing project output.";
                    return ModelImportPlanResult::IdentityConflict;
                }
            }
        }

        struct CurrentMaterialSlot final
        {
            u64 geometryPersistentId = 0u;
            u32 sourcePartitionOrdinal = 0u;
            u64 materialPersistentId = 0u;
            crypto::Digest256 structuralKey;
            crypto::Digest256 partitionKey;
        };
        containers::DynamicArray<CurrentMaterialSlot> currentSlots(memory::pools::Assets::GetInstance());

        for (u32 geometryIndex = 0; geometryIndex < manifest.geometryDefinitions.Size(); ++geometryIndex)
        {
            const u64 geometryPersistentId = CurrentPersistentId(current, currentPersistentIds, ModelImportElementKind::Geometry, geometryIndex);
            if (geometryPersistentId == 0u)
                return ModelImportPlanResult::InvalidArgument;
            const ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions[geometryIndex];
            for (const u32 primitiveIndex : geometry.primitiveIndices)
            {
                if (primitiveIndex >= manifest.primitives.Size())
                    return ModelImportPlanResult::InvalidArgument;
                const ModelImportPrimitive& primitive = manifest.primitives[primitiveIndex];
                crypto::Digest256 slotKey;
                crypto::Digest256 partitionKey;
                if (!BuildSlotStructuralKey(primitive, slotKey) || !BuildSlotPartitionKey(primitive, partitionKey))
                    return ModelImportPlanResult::LimitExceeded;
                const u64 materialPersistentId = candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned
                                                     ? CurrentPersistentId(current, currentPersistentIds,
                                                                           ModelImportElementKind::Material,
                                                                           primitive.sourceMaterialIndex)
                                                     : 0u;
                if (candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned && materialPersistentId == 0u)
                    return ModelImportPlanResult::InvalidArgument;

                if (currentSlots.Size() >= context.limits.maximumMaterialSlots)
                    return ModelImportPlanResult::LimitExceeded;
                CurrentMaterialSlot& slot = currentSlots.EmplaceBack();
                slot.geometryPersistentId = geometryPersistentId;
                slot.sourcePartitionOrdinal = primitive.sourcePartitionOrdinal;
                slot.materialPersistentId = materialPersistentId;
                slot.structuralKey = slotKey;
                slot.partitionKey = partitionKey;
            }
        }

        containers::DynamicArray<u8> oldSlotUsed(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u32> selectedOldSlots(memory::pools::Assets::GetInstance());
        oldSlotUsed.Resize(oldSettings.materialSlots.Size());
        selectedOldSlots.Resize(currentSlots.Size());
        if (oldSlotUsed.Size() != oldSettings.materialSlots.Size() || selectedOldSlots.Size() != currentSlots.Size())
            return ModelImportPlanResult::LimitExceeded;
        for (u32 index = 0; index < oldSlotUsed.Size(); ++index)
            oldSlotUsed[index] = 0u;
        for (u32 index = 0; index < selectedOldSlots.Size(); ++index)
            selectedOldSlots[index] = InvalidModelImportIndex;

        // Strong pass: preserve only a globally 1:1 exact partition correspondence.
        for (u32 currentIndex = 0; currentIndex < currentSlots.Size(); ++currentIndex)
        {
            const CurrentMaterialSlot& currentSlot = currentSlots[currentIndex];
            u32 selectedOld = InvalidModelImportIndex;
            u32 oldCandidateCount = 0u;
            for (u32 oldIndex = 0; oldIndex < oldSettings.materialSlots.Size(); ++oldIndex)
            {
                const ModelImportMaterialSlotCorrespondence& old = oldSettings.materialSlots[oldIndex];
                if (oldSlotUsed[oldIndex] == 0u && old.geometryPersistentId == currentSlot.geometryPersistentId &&
                    old.structuralKey == currentSlot.structuralKey && old.partitionKey == currentSlot.partitionKey)
                {
                    selectedOld = oldIndex;
                    ++oldCandidateCount;
                }
            }
            if (oldCandidateCount != 1u)
                continue;
            const ModelImportMaterialSlotCorrespondence& old = oldSettings.materialSlots[selectedOld];
            u32 currentCandidateCount = 0u;
            for (u32 otherCurrent = 0; otherCurrent < currentSlots.Size(); ++otherCurrent)
            {
                const CurrentMaterialSlot& candidate = currentSlots[otherCurrent];
                if (selectedOldSlots[otherCurrent] == InvalidModelImportIndex &&
                    candidate.geometryPersistentId == old.geometryPersistentId && candidate.structuralKey == old.structuralKey &&
                    candidate.partitionKey == old.partitionKey)
                    ++currentCandidateCount;
            }
            if (currentCandidateCount == 1u)
            {
                selectedOldSlots[currentIndex] = selectedOld;
                oldSlotUsed[selectedOld] = 1u;
            }
        }

        // Weak pass: once exact matches are removed, schema evidence may preserve
        // identity only for a remaining 1:1 pair. This permits ordinary content
        // edits without resolving any 1:N/N:1/N:M correspondence heuristically.
        for (u32 currentIndex = 0; currentIndex < currentSlots.Size(); ++currentIndex)
        {
            if (selectedOldSlots[currentIndex] != InvalidModelImportIndex)
                continue;
            const CurrentMaterialSlot& currentSlot = currentSlots[currentIndex];
            u32 selectedOld = InvalidModelImportIndex;
            u32 oldCandidateCount = 0u;
            for (u32 oldIndex = 0; oldIndex < oldSettings.materialSlots.Size(); ++oldIndex)
            {
                const ModelImportMaterialSlotCorrespondence& old = oldSettings.materialSlots[oldIndex];
                if (oldSlotUsed[oldIndex] == 0u && old.geometryPersistentId == currentSlot.geometryPersistentId &&
                    old.structuralKey == currentSlot.structuralKey)
                {
                    selectedOld = oldIndex;
                    ++oldCandidateCount;
                }
            }
            if (oldCandidateCount != 1u)
                continue;
            const ModelImportMaterialSlotCorrespondence& old = oldSettings.materialSlots[selectedOld];
            u32 currentCandidateCount = 0u;
            for (u32 otherCurrent = 0; otherCurrent < currentSlots.Size(); ++otherCurrent)
            {
                const CurrentMaterialSlot& candidate = currentSlots[otherCurrent];
                if (selectedOldSlots[otherCurrent] == InvalidModelImportIndex &&
                    candidate.geometryPersistentId == old.geometryPersistentId && candidate.structuralKey == old.structuralKey)
                    ++currentCandidateCount;
            }
            if (currentCandidateCount == 1u)
            {
                selectedOldSlots[currentIndex] = selectedOld;
                oldSlotUsed[selectedOld] = 1u;
            }
        }

        for (u32 currentIndex = 0; currentIndex < currentSlots.Size(); ++currentIndex)
        {
            const CurrentMaterialSlot& currentSlot = currentSlots[currentIndex];
            const u32 selectedOld = selectedOldSlots[currentIndex];
            u64 stableSlotId = 0u;
            ModelImportSlotReconcileState state = ModelImportSlotReconcileState::Added;
            u64 previousMaterial = 0u;
            if (selectedOld != InvalidModelImportIndex)
            {
                stableSlotId = oldSettings.materialSlots[selectedOld].stableSlotId;
                previousMaterial = oldSettings.materialSlots[selectedOld].materialPersistentId;
                state = ModelImportSlotReconcileState::Preserved;
            }
            else
            {
                if (nextSettings.nextMaterialSlotId == ~u64{0})
                    return ModelImportPlanResult::LimitExceeded;
                stableSlotId = nextSettings.nextMaterialSlotId++;
            }

            if (nextSettings.materialSlots.Size() >= context.limits.maximumMaterialSlots ||
                ModelImportPlanBuilderAccess::MaterialSlots(staged).Size() >= context.limits.maximumMaterialSlots)
                return ModelImportPlanResult::LimitExceeded;
            ModelImportMaterialSlotCorrespondence& slot = nextSettings.materialSlots.EmplaceBack();
            slot.stableSlotId = stableSlotId;
            slot.geometryPersistentId = currentSlot.geometryPersistentId;
            slot.sourcePartitionOrdinal = currentSlot.sourcePartitionOrdinal;
            slot.materialPersistentId = currentSlot.materialPersistentId;
            slot.structuralKey = currentSlot.structuralKey;
            slot.partitionKey = currentSlot.partitionKey;

            ModelImportMaterialSlotReconciliation& row = ModelImportPlanBuilderAccess::MaterialSlots(staged).EmplaceBack();
            row.state = state;
            row.stableSlotId = stableSlotId;
            row.geometryPersistentId = currentSlot.geometryPersistentId;
            row.sourcePartitionOrdinal = currentSlot.sourcePartitionOrdinal;
            row.previousMaterialPersistentId = previousMaterial;
            row.currentMaterialPersistentId = currentSlot.materialPersistentId;
            row.structuralKey = currentSlot.structuralKey;
            row.partitionKey = currentSlot.partitionKey;
        }

        for (u32 oldIndex = 0; oldIndex < oldSettings.materialSlots.Size(); ++oldIndex)
        {
            if (oldSlotUsed[oldIndex] != 0u)
                continue;
            const ModelImportMaterialSlotCorrespondence& old = oldSettings.materialSlots[oldIndex];
            if (!AppendRetiredSlot(nextSettings, old.stableSlotId) ||
                ModelImportPlanBuilderAccess::MaterialSlots(staged).Size() >= context.limits.maximumMaterialSlots)
                return ModelImportPlanResult::LimitExceeded;
            ModelImportMaterialSlotReconciliation& row = ModelImportPlanBuilderAccess::MaterialSlots(staged).EmplaceBack();
            row.state = ModelImportSlotReconcileState::Removed;
            row.stableSlotId = old.stableSlotId;
            row.geometryPersistentId = old.geometryPersistentId;
            row.sourcePartitionOrdinal = old.sourcePartitionOrdinal;
            row.previousMaterialPersistentId = old.materialPersistentId;
            row.structuralKey = old.structuralKey;
            row.partitionKey = old.partitionKey;
            if (reimport)
            {
                // A stable slot is scoped to its owning geometry. If that
                // entire geometry is being retired, its material slots retire
                // with it, and the geometry-output retirement proof above
                // already fences any external referencers. Only deleting a
                // slot from a still-live geometry requires separate proof that
                // no caller depends on the old slot identity.
                bool oldGeometryExists = false;
                bool geometrySurvives = false;
                for (const ModelImportOutputCorrespondence& candidateOutput : oldSettings.outputs)
                    oldGeometryExists |= candidateOutput.kind == ModelImportElementKind::Geometry &&
                                         candidateOutput.persistentId == old.geometryPersistentId;
                for (const ModelImportOutputCorrespondence& candidateOutput : nextSettings.outputs)
                    geometrySurvives |= candidateOutput.kind == ModelImportElementKind::Geometry &&
                                        candidateOutput.persistentId == old.geometryPersistentId;
                // Without a live reference database there is no authority to
                // prove the owning output's retirement safe: the original P1
                // conservative slot blocker still applies in that case.
                if (context.database == nullptr || !oldGeometryExists || geometrySurvives)
                {
                    row.blocking = true;
                    ModelImportPlanBlocker blocker;
                    blocker.kind = ModelImportPlanBlockerKind::StableSlotReferenceUnavailable;
                    blocker.elementKind = ModelImportElementKind::Geometry;
                    blocker.stableSlotId = old.stableSlotId;
                    if (!AppendBlocker(staged, blocker, context.limits.maximumBlockers))
                        return ModelImportPlanResult::LimitExceeded;
                }
            }
        }

        containers::DynamicArray<containers::String> plannedTexturePaths(memory::pools::Assets::GetInstance());
        if (candidateSettings.materialPolicy == ModelImportMaterialPolicy::ImportModelOwned)
        {
            for (const ModelImportTextureReference& texture : manifest.textureReferences)
            {
                if (texture.storage != ImportedTextureStorage::ExternalSource || texture.authoredPath.Empty())
                    continue;
                bool duplicate = false;
                for (const containers::String& path : plannedTexturePaths)
                    duplicate = duplicate || path == texture.authoredPath;
                if (duplicate)
                    continue;
                if (plannedTexturePaths.Size() >= context.limits.maximumTextureResolutions)
                    return ModelImportPlanResult::LimitExceeded;
                plannedTexturePaths.PushBack(texture.authoredPath);

                ModelImportTextureResolution decision;
                decision.authoredPath = texture.authoredPath;
                if (const ModelImportTextureResolution* const selected = FindTextureResolution(candidateSettings, texture.authoredPath))
                {
                    decision.resolution = selected->resolution;
                    decision.managedResource = selected->managedResource;
                    decision.projectSourceIdentity = selected->projectSourceIdentity;
                }

                if (decision.resolution == ModelImportTextureResolutionKind::Unresolved && context.database != nullptr &&
                    !texture.resolvedPath.Empty())
                {
                    const filesystem::AbsolutePath resolved = filesystem::AbsolutePath::ParseFilePath(texture.resolvedPath);
                    const assets::SourceRecord* const source = resolved.IsFilePath() ? context.database->LookupResource(resolved) : nullptr;
                    if (source != nullptr && source->metadataResult == assets::MetadataResult::Success &&
                        source->importState == assets::SourceImportState::MetadataSelected &&
                        context.outputTypes.texture != resources::InvalidResourceTypeId)
                    {
                        resources::ResourceReference unique;
                        bool ambiguous = false;
                        if (assets::ResolveUniqueAssetOutputOfType(source->metadata, context.outputTypes.texture, unique, ambiguous) && !ambiguous)
                        {
                            const ModelImportDependencyWitness* const witness = FindDependencyWitness(context, source->sourceIdentity);
                            if (witness != nullptr && witness->physicalPath == source->path && witness->sourceExists &&
                                witness->metadataExists && witness->hasSourceContentFingerprint && witness->hasMetadataFingerprint &&
                                !witness->sourceContentFingerprint.IsEmpty() && !witness->metadataFingerprint.IsEmpty() &&
                                witness->asset == source->metadata.id &&
                                MetadataDigestMatchesSnapshot(source->metadata, witness->metadataFingerprint))
                            {
                                decision.resolution = ModelImportTextureResolutionKind::ReuseManagedResource;
                                decision.managedResource = unique;
                            }
                        }
                    }
                }

                bool decisionValid = true;
                const ModelImportDependencyWitness* dependencyWitness = nullptr;
                switch (decision.resolution)
                {
                case ModelImportTextureResolutionKind::ReuseManagedResource:
                    decisionValid = context.database != nullptr && decision.managedResource.IsValid() && decision.managedResource.IsTyped() &&
                                    (context.outputTypes.texture == resources::InvalidResourceTypeId ||
                                     decision.managedResource.ExpectedType() == context.outputTypes.texture);
                    if (decisionValid)
                    {
                        const assets::SourceRecord* const source = context.database->LookupOutput(decision.managedResource);
                        dependencyWitness = source != nullptr ? FindDependencyWitness(context, source->sourceIdentity) : nullptr;
                        decisionValid = source != nullptr && dependencyWitness != nullptr && dependencyWitness->physicalPath == source->path &&
                                        dependencyWitness->sourceExists && dependencyWitness->metadataExists &&
                                        dependencyWitness->hasSourceContentFingerprint && dependencyWitness->hasMetadataFingerprint &&
                                        !dependencyWitness->sourceContentFingerprint.IsEmpty() &&
                                        !dependencyWitness->metadataFingerprint.IsEmpty() &&
                                        dependencyWitness->asset == source->metadata.id &&
                                        MetadataDigestMatchesSnapshot(source->metadata, dependencyWitness->metadataFingerprint);
                    }
                    break;
                case ModelImportTextureResolutionKind::AdoptProjectSource:
                    decisionValid = context.database != nullptr && decision.projectSourceIdentity.IsValid();
                    if (decisionValid)
                    {
                        const assets::SourceRecord* const source = context.database->LookupSource(decision.projectSourceIdentity);
                        dependencyWitness = source != nullptr ? FindDependencyWitness(context, source->sourceIdentity) : nullptr;
                        decisionValid = source != nullptr && context.outputTypes.texture != resources::InvalidResourceTypeId &&
                                        source->primaryOutputType == context.outputTypes.texture &&
                                        SourceRecordRootIsWritable(*context.database, *source) &&
                                        source->metadataResult != assets::MetadataResult::Success &&
                                        (source->issues & assets::MetadataMissing) != 0u &&
                                        source->importState == assets::SourceImportState::Candidate && dependencyWitness != nullptr &&
                                        dependencyWitness->physicalPath == source->path && dependencyWitness->sourceExists &&
                                        dependencyWitness->hasSourceContentFingerprint &&
                                        !dependencyWitness->sourceContentFingerprint.IsEmpty() && !dependencyWitness->metadataExists &&
                                        !dependencyWitness->hasMetadataFingerprint && dependencyWitness->metadataFingerprint.IsEmpty();
                    }
                    break;
                case ModelImportTextureResolutionKind::IgnoreMissing:
                    decisionValid = texture.missing;
                    break;
                case ModelImportTextureResolutionKind::Unresolved:
                    decisionValid = true;
                    break;
                }
                if (!decisionValid)
                {
                    if (report != nullptr)
                        report->diagnostic = "A model texture resolution decision no longer matches current project state.";
                    return ModelImportPlanResult::SettingsInvalid;
                }
                if (dependencyWitness != nullptr &&
                    !AppendDependencyWitness(staged, *dependencyWitness, decision.managedResource,
                                             context.limits.maximumDependencyWitnesses))
                    return ModelImportPlanResult::LimitExceeded;

                bool blocking = decision.resolution == ModelImportTextureResolutionKind::Unresolved;
                if (blocking && !texture.resolvedPath.Empty() && context.database != nullptr)
                {
                    const filesystem::AbsolutePath resolved = filesystem::AbsolutePath::ParseFilePath(texture.resolvedPath);
                    if (resolved.IsFilePath() && !AbsolutePathUnderAnyRoot(*context.database, resolved))
                        blocking = true;
                }
                if (blocking && !AddMissingDependencyBlocker(staged, context.limits.maximumBlockers))
                    return ModelImportPlanResult::LimitExceeded;

                if (!AppendTextureResolution(nextSettings, decision, context.limits.maximumTextureResolutions) ||
                    ModelImportPlanBuilderAccess::TextureResolutions(staged).Size() >= context.limits.maximumTextureResolutions)
                    return ModelImportPlanResult::LimitExceeded;
                ModelImportPlannedTextureResolution& planned = ModelImportPlanBuilderAccess::TextureResolutions(staged).EmplaceBack();
                planned.authoredPath = texture.authoredPath;
                planned.resolvedPath = texture.resolvedPath;
                planned.resolution = decision.resolution;
                planned.managedResource = decision.managedResource;
                planned.projectSourceIdentity = decision.projectSourceIdentity;
                planned.missing = texture.missing;
                planned.blocking = blocking;
            }
        }

        nextSettings.sourceFingerprint = manifest.sourceFingerprint;
        CanonicalizeSettings(nextSettings);
        if (ValidateModelImportSettings(nextSettings) != ModelImportSettingsResult::Success)
        {
            if (report != nullptr)
                report->diagnostic = "Reconciled model import settings are invalid.";
            return ModelImportPlanResult::SettingsInvalid;
        }

        assets::SourceImportDescription& import = ModelImportPlanBuilderAccess::Import(staged);
        import.settingsVersion = ModelImportSettingsVersion;
        if (EncodeModelImportSettings(nextSettings, import.settings) != ModelImportSettingsResult::Success)
            return ModelImportPlanResult::LimitExceeded;
        import.outputs.Reserve(ModelImportPlanBuilderAccess::Outputs(staged).Size());
        for (const ModelImportPlannedOutput& planned : ModelImportPlanBuilderAccess::Outputs(staged))
        {
            assets::AssetOutput& destination = import.outputs.EmplaceBack();
            destination.key = planned.output.key;
            destination.type = planned.output.type;
        }
        if (import.outputs.Size() != ModelImportPlanBuilderAccess::Outputs(staged).Size())
            return ModelImportPlanResult::LimitExceeded;
        if (import.outputs.Size() > 1u)
            std::sort(import.outputs.TypedData(), import.outputs.TypedData() + import.outputs.Size(), [](const assets::AssetOutput& left,
                                                                                                      const assets::AssetOutput& right) noexcept
            {
                const int key = std::strcmp(left.key.AsChar(), right.key.AsChar());
                return key != 0 ? key < 0 : left.type < right.type;
            });

        ModelImportPlanWitnesses& witnesses = ModelImportPlanBuilderAccess::Witnesses(staged);
        witnesses.sourceFingerprint = manifest.sourceFingerprint;
        witnesses.candidateSettingsFingerprint = crypto::Sha256(candidateSettingsBytes.TypedData(), candidateSettingsBytes.Size());
        witnesses.persistedMetadataFingerprint = context.persistedMetadataDigest;
        witnesses.parentAsset = context.parentAsset;
        witnesses.importer = context.importer;
        witnesses.importerVersion = context.importerVersion;
        witnesses.settingsVersion = ModelImportSettingsVersion;
        witnesses.sourceDatabaseGeneration = context.database != nullptr ? context.database->GetGeneration() : 0u;
        witnesses.acceptedStateGeneration = context.acceptedStateGeneration;
        witnesses.sourceOrigin = context.sourceOrigin;
        witnesses.sourceRoot = context.sourceRoot;
        witnesses.sourceDestination = context.sourceDestination;
        witnesses.sourceIdentity = context.sourceIdentity;
        witnesses.hasPersistedMetadataFingerprint = context.hasPersistedMetadataDigest;
        witnesses.copySourceToDestination = !(context.sourceOrigin == context.sourceDestination);

        if (report != nullptr)
        {
            for (const ModelImportReconciliationRecord& record : ModelImportPlanBuilderAccess::Reconciliation(staged))
            {
                switch (record.state)
                {
                case ModelImportReconcileState::Preserved: ++report->preserved; break;
                case ModelImportReconcileState::Added: ++report->added; break;
                case ModelImportReconcileState::Removed: ++report->removed; break;
                case ModelImportReconcileState::Ambiguous: ++report->ambiguous; break;
                }
            }
            report->blockers = ModelImportPlanBuilderAccess::Blockers(staged).Size();
        }

        output = static_cast<ModelImportPlan&&>(staged);
        return ModelImportPlanResult::Success;
    }

    [[nodiscard]] static assets::AssetOperationResult FinalizeModelPlannedSettings(
        const u32 settingsVersion, const containers::ArraySpan<const u8> reviewedSettings,
        const containers::ArraySpan<const assets::PlannedAssetAdoptionReceipt> adoptions,
        containers::DynamicArray<u8>& finalizedSettings, void*) noexcept
    {
        if (settingsVersion != ModelImportSettingsVersion)
            return assets::AssetOperationResult::SettingsInvalid;

        ModelImportSettings settings;
        if (DecodeModelImportSettings(settingsVersion, reviewedSettings, settings) != ModelImportSettingsResult::Success)
            return assets::AssetOperationResult::SettingsInvalid;

        for (const assets::PlannedAssetAdoptionReceipt& receipt : adoptions)
        {
            if (!receipt.sourceIdentity.IsValid() || !receipt.asset.IsValid() ||
                !receipt.selectedResource.IsValid() || !receipt.selectedResource.IsTyped())
                return assets::AssetOperationResult::InvalidArgument;
            bool referenced = false;
            for (const ModelImportTextureResolution& resolution : settings.textureResolutions)
                referenced = referenced ||
                             (resolution.resolution == ModelImportTextureResolutionKind::AdoptProjectSource &&
                              resolution.projectSourceIdentity == receipt.sourceIdentity);
            if (!referenced)
                return assets::AssetOperationResult::InvalidArgument;
        }

        for (ModelImportTextureResolution& resolution : settings.textureResolutions)
        {
            if (resolution.resolution != ModelImportTextureResolutionKind::AdoptProjectSource)
                continue;
            const assets::PlannedAssetAdoptionReceipt* match = nullptr;
            for (const assets::PlannedAssetAdoptionReceipt& receipt : adoptions)
            {
                if (receipt.sourceIdentity != resolution.projectSourceIdentity)
                    continue;
                if (match != nullptr)
                    return assets::AssetOperationResult::InvalidArgument;
                match = &receipt;
            }
            if (match == nullptr)
                return assets::AssetOperationResult::InvalidArgument;
            resolution.resolution = ModelImportTextureResolutionKind::ReuseManagedResource;
            resolution.managedResource = match->selectedResource;
            resolution.projectSourceIdentity = {};
        }

        const ModelImportSettingsResult encoded = EncodeModelImportSettings(settings, finalizedSettings);
        if (encoded == ModelImportSettingsResult::Success)
            return assets::AssetOperationResult::Success;
        return encoded == ModelImportSettingsResult::LimitExceeded ? assets::AssetOperationResult::LimitExceeded
                                                                   : assets::AssetOperationResult::SettingsInvalid;
    }

    bool BuildModelPlannedImportRequest(
        const ModelImportPlan& plan, const containers::ArraySpan<const u8> sourceSnapshot,
        containers::DynamicArray<assets::PlannedAssetDependencyWitness>& dependencyStorage,
        assets::PlannedAssetImportRequest& output) noexcept
    {
        output = {};
        dependencyStorage.Clear();
        if (plan.HasBlockingConflicts() || sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr)
            return false;
        const containers::ArraySpan<const ModelImportDependencyWitness> witnesses = plan.GetDependencyWitnesses();
        dependencyStorage.Reserve(witnesses.Count());
        if (dependencyStorage.Capacity() < witnesses.Count())
            return false;
        const containers::ArraySpan<const ModelImportPlannedTextureResolution> resolutions = plan.GetTextureResolutions();
        for (const ModelImportDependencyWitness& model : witnesses)
        {
            bool adoptCandidate = false;
            for (const ModelImportPlannedTextureResolution& resolution : resolutions)
                adoptCandidate = adoptCandidate ||
                                 (resolution.resolution == ModelImportTextureResolutionKind::AdoptProjectSource &&
                                  resolution.projectSourceIdentity == model.sourceIdentity);
            const u32 previous = dependencyStorage.Size();
            assets::PlannedAssetDependencyWitness& dependency = dependencyStorage.EmplaceBack();
            if (dependencyStorage.Size() != previous + 1u)
            {
                dependencyStorage.Clear();
                return false;
            }
            dependency.sourceIdentity = model.sourceIdentity;
            dependency.physicalPath = model.physicalPath;
            dependency.asset = model.asset;
            dependency.selectedResource = model.selectedResource;
            dependency.sourceContentFingerprint = model.sourceContentFingerprint;
            dependency.metadataFingerprint = model.metadataFingerprint;
            dependency.sourceExists = model.sourceExists;
            dependency.metadataExists = model.metadataExists;
            dependency.hasSourceContentFingerprint = model.hasSourceContentFingerprint;
            dependency.hasMetadataFingerprint = model.hasMetadataFingerprint;
            dependency.adoptCandidate = adoptCandidate;
        }

        const ModelImportPlanWitnesses& planWitnesses = plan.GetWitnesses();
        output.importDescription = &plan.GetImportDescription();
        output.parentAsset = planWitnesses.parentAsset;
        output.importer = planWitnesses.importer;
        output.importerVersion = planWitnesses.importerVersion;
        output.sourceDatabaseGeneration = planWitnesses.sourceDatabaseGeneration;
        output.acceptedStateGeneration = planWitnesses.acceptedStateGeneration;
        output.sourceOrigin = planWitnesses.sourceOrigin;
        output.sourceRoot = planWitnesses.sourceRoot;
        output.sourceDestination = planWitnesses.sourceDestination;
        output.sourceIdentity = planWitnesses.sourceIdentity;
        output.sourceFingerprint = planWitnesses.sourceFingerprint;
        output.persistedMetadataFingerprint = planWitnesses.persistedMetadataFingerprint;
        output.hasPersistedMetadataFingerprint = planWitnesses.hasPersistedMetadataFingerprint;
        output.copySourceToDestination = planWitnesses.copySourceToDestination;
        output.sourceBytes = sourceSnapshot;
        output.dependencyWitnesses = {dependencyStorage.TypedData(), dependencyStorage.Size()};
        output.finalizeSettings = FinalizeModelPlannedSettings;
        return output.parentAsset.IsValid() && output.importer != assets::InvalidSourceImporterId &&
               output.importerVersion != 0u && output.sourceDatabaseGeneration != 0u &&
               output.acceptedStateGeneration != 0u && output.sourceFingerprint == crypto::Sha256(sourceSnapshot.Data(), sourceSnapshot.Count());
    }
} // namespace vanguard::mesh_tools

namespace
{
    tools::ModelImportResult ReadSourceSnapshot(const filesystem::AbsolutePath& source, const tools::ModelImportLimits& limits,
                                                containers::DynamicArray<u8>& bytes);
    tools::ModelImportResult InspectModelMemoryInternal(const void* sourceData, usize sourceSize,
                                                        containers::StringView formatHint,
                                                        containers::StringView sourceName,
                                                        const filesystem::AbsolutePath& sourceDirectory,
                                                        const char* mainSourcePath,
                                                        const tools::ModelImportOptions& options,
                                                        const tools::ModelImportLimits& limits,
                                                        tools::ModelImportManifest& output,
                                                        tools::ModelImportReport* report);

    void MultiplyMatrix4(const f32* const first, const f32* const second, f32* const result) noexcept
    {
        f32 product[16]{};
        for (u32 row = 0; row < 4; ++row)
        {
            for (u32 column = 0; column < 4; ++column)
            {
                f32 value = 0.0f;
                for (u32 inner = 0; inner < 4; ++inner)
                {
                    value += first[row * 4 + inner] * second[inner * 4 + column];
                }
                product[row * 4 + column] = value;
            }
        }
        std::memcpy(result, product, sizeof(product));
    }

    aiVector3D ConvertExtractedPosition(const aiVector3D& value, const tools::ModelGeometryExtractionSettings& settings) noexcept
    {
        const f32 scale = settings.uniformScale;
        return settings.sourceUpAxis == tools::SourceUpAxis::Y ? aiVector3D(value.x * scale, -value.z * scale, value.y * scale)
                                                               : aiVector3D(value.x * scale, value.y * scale, value.z * scale);
    }

    aiVector3D ConvertExtractedDirection(const aiVector3D& value, const tools::SourceUpAxis axis) noexcept
    {
        aiVector3D converted = axis == tools::SourceUpAxis::Y ? aiVector3D(value.x, -value.z, value.y) : value;
        const f32 lengthSquared = converted.SquareLength();
        if (lengthSquared > 1.0e-20f)
        {
            converted /= std::sqrt(lengthSquared);
        }
        return converted;
    }

    u64 StableStructuralId(const u64 geometryKey, const u32 partitionOrdinal, const u32 primitiveIndex) noexcept
    {
        u64 hash = serialization::Crc64(reinterpret_cast<const char*>(&geometryKey), sizeof(geometryKey), 0);
        hash = serialization::Crc64(reinterpret_cast<const char*>(&partitionOrdinal), sizeof(partitionOrdinal), hash);
        hash = serialization::Crc64(reinterpret_cast<const char*>(&primitiveIndex), sizeof(primitiveIndex), hash);
        return hash != 0 ? hash : 1;
    }

    u64 StableStringHash(const containers::String& value) noexcept
    {
        u64 hash = serialization::Crc64(value.AsChar(), value.Length(), 0);
        return hash != 0 ? hash : 1;
    }

    bool EqualMatrix(const tools::ModelImportMatrix& first, const f32 (&second)[16]) noexcept
    {
        for (u32 index = 0; index < 16; ++index)
        {
            if (first.values[index] != second[index])
            {
                return false;
            }
        }
        return true;
    }

    struct ExtractedJointIdentity
    {
        u32 nodeIndex = tools::InvalidModelImportIndex;
        u32 outputIndex = tools::InvalidModelImportIndex;
    };

    u32 ResolveExtractedJoint(const tools::ModelImportBoneBinding& binding, tools::ImportedMesh& output,
                              containers::DynamicArray<ExtractedJointIdentity>& identities)
    {
        if (binding.nodeIndex != tools::InvalidModelImportIndex)
        {
            for (const ExtractedJointIdentity& identity : identities)
            {
                if (identity.nodeIndex == binding.nodeIndex && identity.outputIndex < output.joints.Size() &&
                    EqualMatrix(binding.inverseBind, output.joints[identity.outputIndex].inverseBind))
                {
                    return identity.outputIndex;
                }
            }
        }
        else
        {
            for (u32 index = 0; index < output.joints.Size(); ++index)
            {
                if (output.joints[index].name == binding.name && EqualMatrix(binding.inverseBind, output.joints[index].inverseBind))
                {
                    return index;
                }
            }
        }

        if (output.joints.Size() >= 255)
        {
            return tools::InvalidModelImportIndex;
        }
        tools::ImportedJoint& joint = output.joints.EmplaceBack();
        joint.name = binding.name;
        std::memcpy(joint.inverseBind, binding.inverseBind.values, sizeof(joint.inverseBind));
        const u32 outputIndex = output.joints.Size() - 1u;
        if (binding.nodeIndex != tools::InvalidModelImportIndex)
        {
            identities.PushBack({binding.nodeIndex, outputIndex});
        }
        return outputIndex;
    }

    tools::ModelImportResult AddExtractedSkinning(const tools::ModelImportPrimitive& source, tools::ImportedSubmesh& destination,
                                                  tools::ImportedMesh& output,
                                                  containers::DynamicArray<ExtractedJointIdentity>& identities)
    {
        if (source.boneBindings.Empty())
        {
            return tools::ModelImportResult::Success;
        }

        tools::ImportedVertexStream& jointIndices = destination.vertexStreams.EmplaceBack();
        jointIndices.semantic = meshes::VertexSemantic::JointIndices;
        jointIndices.format = meshes::VertexFormat::R8G8B8A8UInt;
        jointIndices.stride = 4;
        jointIndices.integerValues.Resize(destination.vertexCount * 4);

        tools::ImportedVertexStream& jointWeights = destination.vertexStreams.EmplaceBack();
        jointWeights.semantic = meshes::VertexSemantic::JointWeights;
        jointWeights.format = meshes::VertexFormat::R32G32B32A32Float;
        jointWeights.stride = 4 * sizeof(f32);
        jointWeights.floatValues.Resize(destination.vertexCount * 4);

        for (const tools::ModelImportBoneBinding& binding : source.boneBindings)
        {
            const u32 jointIndex = ResolveExtractedJoint(binding, output, identities);
            if (jointIndex == tools::InvalidModelImportIndex)
            {
                return tools::ModelImportResult::LimitExceeded;
            }
            for (const tools::ModelImportBoneWeight& influence : binding.weights)
            {
                if (influence.vertexIndex >= destination.vertexCount || !std::isfinite(influence.weight) || influence.weight < 0.0f)
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
                const u32 base = influence.vertexIndex * 4;
                for (u32 slot = 0; slot < 4; ++slot)
                {
                    if (influence.weight > jointWeights.floatValues[base + slot])
                    {
                        for (u32 move = 3; move > slot; --move)
                        {
                            jointWeights.floatValues[base + move] = jointWeights.floatValues[base + move - 1];
                            jointIndices.integerValues[base + move] = jointIndices.integerValues[base + move - 1];
                        }
                        jointWeights.floatValues[base + slot] = influence.weight;
                        jointIndices.integerValues[base + slot] = static_cast<u8>(jointIndex);
                        break;
                    }
                }
            }
        }

        for (u32 vertex = 0; vertex < destination.vertexCount; ++vertex)
        {
            const u32 base = vertex * 4;
            const f32 total = jointWeights.floatValues[base] + jointWeights.floatValues[base + 1] +
                              jointWeights.floatValues[base + 2] + jointWeights.floatValues[base + 3];
            if (!(total > 0.0f) || !std::isfinite(total))
            {
                return tools::ModelImportResult::InvalidGeometry;
            }
            for (u32 slot = 0; slot < 4; ++slot)
            {
                jointWeights.floatValues[base + slot] /= total;
            }
        }
        return tools::ModelImportResult::Success;
    }

    tools::ModelImportResult CopyExtractedVertexStreams(const tools::ModelImportPrimitive& source,
                                                        const tools::ModelGeometryExtractionSettings& settings,
                                                        tools::ImportedSubmesh& destination)
    {
        destination.vertexStreams.Reserve(source.vertexStreams.Size() + (source.boneBindings.Empty() ? 0u : 2u));
        for (const tools::ImportedVertexStream& sourceStream : source.vertexStreams)
        {
            tools::ImportedVertexStream& stream = destination.vertexStreams.EmplaceBack();
            stream.semantic = sourceStream.semantic;
            stream.semanticIndex = sourceStream.semanticIndex;
            stream.format = sourceStream.format;
            stream.stride = sourceStream.stride;

            if (!sourceStream.floatValues.Empty())
            {
                if (sourceStream.semantic == meshes::VertexSemantic::Position &&
                    sourceStream.format == meshes::VertexFormat::R32G32B32Float)
                {
                    if (sourceStream.floatValues.Size() != destination.vertexCount * 3)
                    {
                        return tools::ModelImportResult::InvalidGeometry;
                    }
                    stream.floatValues.Reserve(sourceStream.floatValues.Size());
                    for (u32 vertex = 0; vertex < destination.vertexCount; ++vertex)
                    {
                        const u32 base = vertex * 3;
                        const aiVector3D converted = ConvertExtractedPosition(
                            aiVector3D(sourceStream.floatValues[base], sourceStream.floatValues[base + 1],
                                       sourceStream.floatValues[base + 2]),
                            settings);
                        stream.floatValues.PushBack(converted.x);
                        stream.floatValues.PushBack(converted.y);
                        stream.floatValues.PushBack(converted.z);
                    }
                }
                else if (sourceStream.semantic == meshes::VertexSemantic::Normal &&
                         sourceStream.format == meshes::VertexFormat::R32G32B32Float)
                {
                    if (sourceStream.floatValues.Size() != destination.vertexCount * 3)
                    {
                        return tools::ModelImportResult::InvalidGeometry;
                    }
                    stream.floatValues.Reserve(sourceStream.floatValues.Size());
                    for (u32 vertex = 0; vertex < destination.vertexCount; ++vertex)
                    {
                        const u32 base = vertex * 3;
                        const aiVector3D converted = ConvertExtractedDirection(
                            aiVector3D(sourceStream.floatValues[base], sourceStream.floatValues[base + 1],
                                       sourceStream.floatValues[base + 2]),
                            settings.sourceUpAxis);
                        stream.floatValues.PushBack(converted.x);
                        stream.floatValues.PushBack(converted.y);
                        stream.floatValues.PushBack(converted.z);
                    }
                }
                else if (sourceStream.semantic == meshes::VertexSemantic::Tangent &&
                         sourceStream.format == meshes::VertexFormat::R32G32B32A32Float)
                {
                    if (sourceStream.floatValues.Size() != destination.vertexCount * 4)
                    {
                        return tools::ModelImportResult::InvalidGeometry;
                    }
                    stream.floatValues.Reserve(sourceStream.floatValues.Size());
                    for (u32 vertex = 0; vertex < destination.vertexCount; ++vertex)
                    {
                        const u32 base = vertex * 4;
                        const aiVector3D converted = ConvertExtractedDirection(
                            aiVector3D(sourceStream.floatValues[base], sourceStream.floatValues[base + 1],
                                       sourceStream.floatValues[base + 2]),
                            settings.sourceUpAxis);
                        stream.floatValues.PushBack(converted.x);
                        stream.floatValues.PushBack(converted.y);
                        stream.floatValues.PushBack(converted.z);
                        stream.floatValues.PushBack(sourceStream.floatValues[base + 3]);
                    }
                }
                else
                {
                    stream.floatValues.Reserve(sourceStream.floatValues.Size());
                    for (const f32 value : sourceStream.floatValues)
                    {
                        stream.floatValues.PushBack(value);
                    }
                }
            }
            if (!sourceStream.integerValues.Empty())
            {
                stream.integerValues.Reserve(sourceStream.integerValues.Size());
                for (const u8 value : sourceStream.integerValues)
                {
                    stream.integerValues.PushBack(value);
                }
            }
        }
        return tools::ModelImportResult::Success;
    }

    void CopyExtractedMaterials(const tools::ModelImportManifest& manifest, tools::ImportedMesh& output)
    {
        output.materials.Reserve(manifest.materials.Size());
        for (const tools::ModelImportMaterial& source : manifest.materials)
        {
            tools::ImportedMaterial& material = output.materials.EmplaceBack();
            material.name = source.name;
            std::memcpy(material.baseColor, source.baseColor, sizeof(material.baseColor));
            std::memcpy(material.emissiveColor, source.emissiveColor, sizeof(material.emissiveColor));
            std::memcpy(material.specularColor, source.specularColor, sizeof(material.specularColor));
            material.metallic = source.metallic;
            material.roughness = source.roughness;
            material.opacity = source.opacity;
            material.twoSided = source.twoSided;
            material.textures.Reserve(source.textureReferenceIndices.Size());
            for (const u32 textureIndex : source.textureReferenceIndices)
            {
                if (textureIndex >= manifest.textureReferences.Size())
                {
                    continue;
                }
                const tools::ModelImportTextureReference& canonical = manifest.textureReferences[textureIndex];
                tools::ImportedTextureReference& texture = material.textures.EmplaceBack();
                texture.semantic = canonical.semantic;
                texture.storage = canonical.storage;
                texture.uvSet = canonical.uvSet;
                texture.embeddedTexture =
                    canonical.embeddedTextureIndex == tools::InvalidModelImportIndex ? 0u : canonical.embeddedTextureIndex;
                texture.sourcePath = !canonical.resolvedPath.Empty() ? canonical.resolvedPath : canonical.authoredPath;
            }
        }

        output.embeddedTextures.Reserve(manifest.embeddedTextures.Size());
        for (const tools::ModelImportEmbeddedTexture& source : manifest.embeddedTextures)
        {
            tools::ImportedEmbeddedTexture& texture = output.embeddedTextures.EmplaceBack();
            texture.name = source.name;
            texture.formatHint = source.formatHint;
            texture.width = source.width;
            texture.height = source.height;
            texture.compressed = source.compressed;
            texture.bytes.Reserve(source.bytes.Size());
            for (const u8 byte : source.bytes)
            {
                texture.bytes.PushBack(byte);
            }
        }
    }
} // namespace

namespace vanguard::mesh_tools
{
    bool ConvertModelImportLocalTransform(const ModelImportMatrix& source, const ModelGeometryExtractionSettings& settings,
                                          ModelImportMatrix& output) noexcept
    {
        if (!std::isfinite(settings.uniformScale) || settings.uniformScale <= 0.0f ||
            settings.sourceUpAxis > SourceUpAxis::Z)
        {
            return false;
        }
        for (const f32 value : source.values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }

        const f32 scale = settings.uniformScale;
        const f32 inverseScale = 1.0f / scale;
        f32 basis[16]{};
        f32 inverseBasis[16]{};
        if (settings.sourceUpAxis == SourceUpAxis::Y)
        {
            const f32 sourceToTarget[16] = {
                scale, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, -scale, 0.0f,
                0.0f, scale, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
            const f32 targetToSource[16] = {
                inverseScale, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, inverseScale, 0.0f,
                0.0f, -inverseScale, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
            std::memcpy(basis, sourceToTarget, sizeof(basis));
            std::memcpy(inverseBasis, targetToSource, sizeof(inverseBasis));
        }
        else
        {
            const f32 sourceToTarget[16] = {
                scale, 0.0f, 0.0f, 0.0f,
                0.0f, scale, 0.0f, 0.0f,
                0.0f, 0.0f, scale, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
            const f32 targetToSource[16] = {
                inverseScale, 0.0f, 0.0f, 0.0f,
                0.0f, inverseScale, 0.0f, 0.0f,
                0.0f, 0.0f, inverseScale, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f};
            std::memcpy(basis, sourceToTarget, sizeof(basis));
            std::memcpy(inverseBasis, targetToSource, sizeof(inverseBasis));
        }

        f32 intermediate[16]{};
        MultiplyMatrix4(basis, source.values, intermediate);
        MultiplyMatrix4(intermediate, inverseBasis, output.values);
        return true;
    }

    bool ConvertModelImportRigidLocalTransform(const ModelImportNode& node, const ModelGeometryExtractionSettings& settings,
                                               ModelImportRigidTransform& output) noexcept
    {
        output = {};
        constexpr u32 Unsupported = ModelImportNode_NonFiniteTransform | ModelImportNode_NonAffineTransform |
                                    ModelImportNode_SingularTransform | ModelImportNode_NegativeDeterminant |
                                    ModelImportNode_NonUniformScale | ModelImportNode_ShearTransform;
        if ((node.flags & Unsupported) != 0u)
            return false;
        ModelImportMatrix converted;
        if (!ConvertModelImportLocalTransform(node.localTransform, settings, converted))
            return false;

        constexpr f32 Epsilon = 1.0e-4f;
        const f32* const m = converted.values;
        if (std::fabs(m[12]) > Epsilon || std::fabs(m[13]) > Epsilon || std::fabs(m[14]) > Epsilon ||
            std::fabs(m[15] - 1.0f) > Epsilon)
            return false;
        const f32 sx = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
        const f32 sy = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        const f32 sz = std::sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || std::fabs(sx - 1.0f) > Epsilon ||
            std::fabs(sy - 1.0f) > Epsilon || std::fabs(sz - 1.0f) > Epsilon)
            return false;
        const f32 xy = m[0] * m[1] + m[4] * m[5] + m[8] * m[9];
        const f32 xz = m[0] * m[2] + m[4] * m[6] + m[8] * m[10];
        const f32 yz = m[1] * m[2] + m[5] * m[6] + m[9] * m[10];
        if (std::fabs(xy) > Epsilon || std::fabs(xz) > Epsilon || std::fabs(yz) > Epsilon)
            return false;
        const f32 determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                                m[1] * (m[4] * m[10] - m[6] * m[8]) +
                                m[2] * (m[4] * m[9] - m[5] * m[8]);
        if (!std::isfinite(determinant) || determinant <= 0.0f || std::fabs(determinant - 1.0f) > 4.0f * Epsilon)
            return false;

        output.translation[0] = m[3];
        output.translation[1] = m[7];
        output.translation[2] = m[11];
        f32 qx = 0.0f;
        f32 qy = 0.0f;
        f32 qz = 0.0f;
        f32 qw = 1.0f;
        const f32 trace = m[0] + m[5] + m[10];
        if (trace > 0.0f)
        {
            const f32 scale = std::sqrt(trace + 1.0f) * 2.0f;
            qw = 0.25f * scale;
            qx = (m[6] - m[9]) / scale;
            qy = (m[8] - m[2]) / scale;
            qz = (m[1] - m[4]) / scale;
        }
        else if (m[0] > m[5] && m[0] > m[10])
        {
            const f32 scale = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
            qw = (m[6] - m[9]) / scale;
            qx = 0.25f * scale;
            qy = (m[4] + m[1]) / scale;
            qz = (m[8] + m[2]) / scale;
        }
        else if (m[5] > m[10])
        {
            const f32 scale = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
            qw = (m[8] - m[2]) / scale;
            qx = (m[4] + m[1]) / scale;
            qy = 0.25f * scale;
            qz = (m[9] + m[6]) / scale;
        }
        else
        {
            const f32 scale = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
            qw = (m[1] - m[4]) / scale;
            qx = (m[8] + m[2]) / scale;
            qy = (m[9] + m[6]) / scale;
            qz = 0.25f * scale;
        }
        const f32 lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
            return false;
        const f32 inverseLength = 1.0f / std::sqrt(lengthSquared);
        qx *= inverseLength;
        qy *= inverseLength;
        qz *= inverseLength;
        qw *= inverseLength;
        if (qw < 0.0f)
        {
            qx = -qx;
            qy = -qy;
            qz = -qz;
            qw = -qw;
        }
        output.rotation[0] = qx;
        output.rotation[1] = qy;
        output.rotation[2] = qz;
        output.rotation[3] = qw;
        return std::isfinite(output.translation[0]) && std::isfinite(output.translation[1]) && std::isfinite(output.translation[2]);
    }

    ModelImportResult InspectModelFile(const filesystem::AbsolutePath& source, const ModelImportOptions& options,
                                       const ModelImportLimits& limits, ModelImportManifest& output,
                                       ModelImportReport* const report) noexcept
    {
        output.Clear();
        if (report != nullptr)
        {
            *report = {};
        }
        if (!ValidateOptions(options, limits) || source.AsChar() == nullptr || source.AsChar()[0] == '\0')
        {
            return ModelImportResult::InvalidArgument;
        }
        if (!filesystem::SystemIO::FileExist(source.AsChar()))
        {
            SetDiagnostic(report, "Model source file does not exist.");
            return ModelImportResult::SourceNotFound;
        }

        containers::DynamicArray<u8> snapshot(memory::pools::Assets::GetInstance());
        const ModelImportResult snapshotResult = ReadSourceSnapshot(source, limits, snapshot);
        if (snapshotResult != ModelImportResult::Success)
        {
            SetDiagnostic(report, snapshotResult == ModelImportResult::SourceTooLarge
                                      ? "Model source exceeds the configured inspection source-byte limit."
                                      : "Failed to read an exact model source snapshot.");
            return snapshotResult;
        }

        const containers::StringView extension = filesystem::paths::GetExtension(source);
        containers::String format(extension.Data(), static_cast<u32>(extension.Length()));
        format.ToLower();
        if (format.Empty())
        {
            SetDiagnostic(report, "Model source has no file extension for decoder selection.");
            return ModelImportResult::UnsupportedSource;
        }
        const containers::StringView sourceName = filesystem::paths::GetFileName(source);
        const filesystem::AbsolutePath sourceDirectory = filesystem::paths::ParentAbsolutePath(source);
        return InspectModelMemoryInternal(snapshot.TypedData(), snapshot.Size(), containers::StringView(format), sourceName,
                                          sourceDirectory, source.AsChar(), options, limits, output, report);
    }

    ModelImportResult InspectModelMemory(const void* const sourceData, const usize sourceSize,
                                         const containers::StringView formatHint, const containers::StringView sourceName,
                                         const ModelImportOptions& options, const ModelImportLimits& limits,
                                         ModelImportManifest& output, ModelImportReport* const report) noexcept
    {
        return InspectModelMemoryInternal(sourceData, sourceSize, formatHint, sourceName, {}, nullptr, options, limits, output, report);
    }

    ModelImportResult ExtractModelGeometry(const ModelImportManifest& manifest, const u32 geometryDefinitionIndex,
                                           const ModelGeometryExtractionSettings& settings, ImportedMesh& output,
                                           ModelImportReport* const report) noexcept
    {
        output.Clear();
        if (report != nullptr)
        {
            *report = {};
        }
        if (geometryDefinitionIndex >= manifest.geometryDefinitions.Size() || !std::isfinite(settings.uniformScale) ||
            settings.uniformScale <= 0.0f || settings.sourceUpAxis > SourceUpAxis::Z)
        {
            return ModelImportResult::InvalidArgument;
        }

        const ModelImportGeometryDefinition& definition = manifest.geometryDefinitions[geometryDefinitionIndex];
        if (definition.primitiveIndices.Empty())
        {
            return ModelImportResult::NoGeometry;
        }

        output.sourceFingerprint = manifest.sourceFingerprint;
        output.name = StableStringHash(definition.displayName);
        output.skeleton = settings.skeleton;
        CopyExtractedMaterials(manifest, output);
        for (const ModelImportDependency& dependency : manifest.dependencies)
        {
            if (!dependency.path.Empty())
            {
                bool duplicate = false;
                for (const containers::String& existing : output.dependencies)
                {
                    if (existing == dependency.path)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                {
                    output.dependencies.PushBack(dependency.path);
                }
            }
        }

        containers::DynamicArray<ExtractedJointIdentity> jointIdentities(memory::pools::Assets::GetInstance());
        output.submeshes.Reserve(definition.primitiveIndices.Size());
        for (u32 definitionPrimitiveIndex = 0; definitionPrimitiveIndex < definition.primitiveIndices.Size(); ++definitionPrimitiveIndex)
        {
            const u32 primitiveIndex = definition.primitiveIndices[definitionPrimitiveIndex];
            if (primitiveIndex >= manifest.primitives.Size())
            {
                output.Clear();
                return ModelImportResult::InvalidGeometry;
            }
            const ModelImportPrimitive& source = manifest.primitives[primitiveIndex];
            if (source.sourceMaterialIndex >= manifest.materials.Size())
            {
                output.Clear();
                return ModelImportResult::InvalidGeometry;
            }

            ImportedSubmesh& destination = output.submeshes.EmplaceBack();
            destination.vertexCount = source.vertexCount;
            destination.displayName = source.displayName;
            destination.stableId = StableStructuralId(definition.sourceGeometryKey, source.sourcePartitionOrdinal,
                                                       definitionPrimitiveIndex);
            destination.name = StableStringHash(source.displayName);
            destination.materialName = StableStringHash(manifest.materials[source.sourceMaterialIndex].name);
            destination.flags = source.flags;
            destination.material = settings.defaultMaterial;
            if (settings.resolveMaterial != nullptr)
            {
                resources::ResourceReference resolved;
                if (!settings.resolveMaterial(source.sourceMaterialIndex, resolved, settings.resolveMaterialUserData))
                {
                    output.Clear();
                    SetDiagnostic(report, "Failed to resolve an extracted model primitive material.");
                    return ModelImportResult::MissingDependency;
                }
                destination.material = resolved;
            }

            const ModelImportResult streamResult = CopyExtractedVertexStreams(source, settings, destination);
            if (streamResult != ModelImportResult::Success)
            {
                output.Clear();
                return streamResult;
            }
            const ModelImportResult skinResult = AddExtractedSkinning(source, destination, output, jointIdentities);
            if (skinResult != ModelImportResult::Success)
            {
                output.Clear();
                return skinResult;
            }

            destination.indices.Reserve(source.indices.Size());
            if ((source.indices.Size() % 3u) != 0)
            {
                output.Clear();
                return ModelImportResult::InvalidGeometry;
            }
            for (u32 index = 0; index < source.indices.Size(); index += 3)
            {
                if (settings.flipWinding)
                {
                    destination.indices.PushBack(source.indices[index + 2]);
                    destination.indices.PushBack(source.indices[index + 1]);
                    destination.indices.PushBack(source.indices[index]);
                }
                else
                {
                    destination.indices.PushBack(source.indices[index]);
                    destination.indices.PushBack(source.indices[index + 1]);
                    destination.indices.PushBack(source.indices[index + 2]);
                }
            }
        }

        if (report != nullptr)
        {
            report->geometryDefinitionCount = 1;
            report->primitiveCount = output.submeshes.Size();
            report->materialCount = output.materials.Size();
            report->embeddedTextureCount = output.embeddedTextures.Size();
            report->boneBindingCount = output.joints.Size();
            for (const ImportedSubmesh& submesh : output.submeshes)
            {
                report->vertexCount += submesh.vertexCount;
                report->indexCount += submesh.indices.Size();
            }
        }
        return ModelImportResult::Success;
    }
} // namespace vanguard::mesh_tools

namespace vanguard::mesh_tools
{
    const char* ToString(const ModelImportResult result) noexcept
    {
        switch (result)
        {
        case ModelImportResult::Success:
            return "Success";
        case ModelImportResult::InvalidArgument:
            return "InvalidArgument";
        case ModelImportResult::UnsupportedSource:
            return "UnsupportedSource";
        case ModelImportResult::SourceNotFound:
            return "SourceNotFound";
        case ModelImportResult::SourceTooLarge:
            return "SourceTooLarge";
        case ModelImportResult::ImportFailure:
            return "ImportFailure";
        case ModelImportResult::NoGeometry:
            return "NoGeometry";
        case ModelImportResult::LimitExceeded:
            return "LimitExceeded";
        case ModelImportResult::InvalidGeometry:
            return "InvalidGeometry";
        case ModelImportResult::MissingDependency:
            return "MissingDependency";
        case ModelImportResult::UnsupportedExternalBuffer:
            return "UnsupportedExternalBuffer";
        case ModelImportResult::ConflictingGeometryBinding:
            return "ConflictingGeometryBinding";
        case ModelImportResult::AmbiguousBoneBinding:
            return "AmbiguousBoneBinding";
        case ModelImportResult::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    void ModelImportManifest::Clear() noexcept
    {
        sourceFingerprint = {};
        sourceName.Clear();
        formatHint.Clear();
        decoderName.Clear();
        nodes.Clear();
        geometryDefinitions.Clear();
        geometryInstances.Clear();
        primitives.Clear();
        materials.Clear();
        textureReferences.Clear();
        embeddedTextures.Clear();
        dependencies.Clear();
        diagnostics.Clear();
    }
} // namespace vanguard::mesh_tools

namespace
{
    bool CopyEmbeddedTextures(const aiScene& scene, InspectionContext& context)
    {
        if (scene.mNumTextures > context.limits.maximumEmbeddedTextures)
        {
            return false;
        }
        context.output.embeddedTextures.Reserve(scene.mNumTextures);
        for (u32 index = 0; index < scene.mNumTextures; ++index)
        {
            const aiTexture& source = *scene.mTextures[index];
            const u64 byteCount = source.mHeight == 0
                                      ? static_cast<u64>(source.mWidth)
                                      : static_cast<u64>(source.mWidth) * source.mHeight * sizeof(aiTexel);
            if (byteCount > ~u32{0} || byteCount > context.limits.maximumEmbeddedTextureBytes ||
                context.totalEmbeddedBytes > context.limits.maximumEmbeddedTextureBytes - byteCount)
            {
                return false;
            }
            if (byteCount > context.limits.maximumOwnedPayloadBytes ||
                context.ownedPayloadBytes > context.limits.maximumOwnedPayloadBytes - byteCount)
            {
                return false;
            }
            tools::ModelImportEmbeddedTexture& texture = context.output.embeddedTextures.EmplaceBack();
            texture.sourceTextureIndex = index;
            texture.name = SafeName(source.mFilename, "embedded", index);
            texture.formatHint = source.achFormatHint;
            texture.width = source.mWidth;
            texture.height = source.mHeight;
            texture.compressed = source.mHeight == 0;
            texture.bytes.Resize(static_cast<u32>(byteCount));
            if (byteCount != 0)
            {
                std::memcpy(texture.bytes.TypedData(), source.pcData, static_cast<usize>(byteCount));
            }
            context.totalEmbeddedBytes += byteCount;
            context.ownedPayloadBytes += byteCount;
        }
        return true;
    }

    bool AddMaterialTextures(const aiScene& scene, const aiMaterial& source, const aiTextureType type, InspectionContext& context,
                             tools::ModelImportMaterial& destination)
    {
        const u32 count = source.GetTextureCount(type);
        int specularColorTextureSlot = -1;
        bool gltfSpecular = false;
        if (type == aiTextureType_SPECULAR && count != 0u)
        {
            const containers::StringView format(context.formatHint.AsChar(), context.formatHint.Length());
            const bool gltfSource = (format.Length() == 3u && format.StartsWithIgnoreCase("glb")) ||
                                    (format.Length() == 4u && format.StartsWithIgnoreCase("gltf"));
            f32 specularFactor = 1.0f;
            gltfSpecular = gltfSource && source.Get(AI_MATKEY_SPECULAR_FACTOR, specularFactor) == AI_SUCCESS;
            const aiReturn colorSlotResult = source.Get(AI_MATKEY_GLTF_SPECULAR_COLOR_TEXTURE_SLOT, specularColorTextureSlot);
            if (colorSlotResult == AI_SUCCESS)
            {
                if (!gltfSpecular || count > 2u || specularColorTextureSlot < 0 || specularColorTextureSlot > 1 ||
                    static_cast<u32>(specularColorTextureSlot) >= count)
                    return false;
            }
            else if (gltfSpecular && count != 1u)
            {
                // KHR_materials_specular can publish at most one factor map and one color map.
                // Without Assimp's color-slot witness, more than one dense SPECULAR binding is ambiguous.
                return false;
            }
        }
        for (u32 index = 0; index < count; ++index)
        {
            if (context.output.textureReferences.Size() >= context.limits.maximumTextureReferences)
            {
                return false;
            }
            aiString path;
            u32 uvSet = 0;
            if (source.GetTexture(type, index, &path, nullptr, &uvSet) != AI_SUCCESS)
            {
                return false;
            }

            const u32 referenceIndex = context.output.textureReferences.Size();
            tools::ModelImportTextureReference& texture = context.output.textureReferences.EmplaceBack();
            texture.semantic = TextureSemantic(source, type);
            if (type == aiTextureType_SPECULAR && gltfSpecular)
            {
                texture.specularTextureRole = specularColorTextureSlot >= 0 && index == static_cast<u32>(specularColorTextureSlot)
                                                   ? tools::ModelImportTextureReference::SpecularTextureRole::ColorRgb
                                                   : tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha;
            }
            texture.sourceBindingOrdinal = index;
            texture.uvSet = uvSet;
            texture.authoredPath = path.C_Str();
            destination.textureReferenceIndices.PushBack(referenceIndex);

            const aiTexture* const embedded = scene.GetEmbeddedTexture(path.C_Str());
            if (embedded != nullptr)
            {
                texture.storage = tools::ImportedTextureStorage::Embedded;
                for (u32 embeddedIndex = 0; embeddedIndex < scene.mNumTextures; ++embeddedIndex)
                {
                    if (scene.mTextures[embeddedIndex] == embedded)
                    {
                        texture.embeddedTextureIndex = embeddedIndex;
                        texture.resolved = true;
                        break;
                    }
                }
                if (texture.embeddedTextureIndex == tools::InvalidModelImportIndex)
                {
                    return false;
                }
                continue;
            }

            texture.storage = tools::ImportedTextureStorage::ExternalSource;
            if (!context.resolveDependencies)
            {
                continue;
            }

            containers::String normalized(path.C_Str());
            for (u32 byte = 0; byte < normalized.Length(); ++byte)
            {
                if (normalized[byte] == '\\')
                {
                    normalized[byte] = '/';
                }
            }
            const containers::StringView normalizedView(normalized);
            const filesystem::AbsolutePath resolved = filesystem::paths::IsAbsolutePath(normalizedView)
                                                         ? filesystem::AbsolutePath::ParseFilePath(normalizedView)
                                                         : context.sourceDirectory.AddFilePath(normalizedView);
            const bool exists = filesystem::SystemIO::FileExist(resolved.AsChar());
            texture.resolvedPath = resolved.AsString();
            texture.resolved = exists;
            texture.missing = !exists;
            if (!AddUniqueDependency(context, tools::ModelImportDependencyKind::ExternalTexture, resolved.AsChar(), exists))
            {
                return false;
            }
            if (!exists)
            {
                AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Warning,
                              tools::ModelImportDiagnosticCode::MissingExternalTexture,
                              "Model references an external texture that is currently missing.");
            }
        }
        return true;
    }

    bool ExtractMaterials(const aiScene& scene, InspectionContext& context)
    {
        if (scene.mNumMaterials > context.limits.maximumMaterials)
        {
            return false;
        }
        const u32 materialCount = scene.mNumMaterials > 0 ? scene.mNumMaterials : 1u;
        context.output.materials.Reserve(materialCount);
        const containers::StringView format(context.formatHint.AsChar(), context.formatHint.Length());
        const bool gltfSource = (format.Length() == 3u && format.StartsWithIgnoreCase("glb")) ||
                                (format.Length() == 4u && format.StartsWithIgnoreCase("gltf"));
        static constexpr aiTextureType TextureTypes[] = {
            aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE, aiTextureType_NORMALS, aiTextureType_HEIGHT,
            aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SPECULAR, aiTextureType_EMISSIVE,
            aiTextureType_LIGHTMAP, aiTextureType_OPACITY};

        for (u32 materialIndex = 0; materialIndex < scene.mNumMaterials; ++materialIndex)
        {
            const aiMaterial& source = *scene.mMaterials[materialIndex];
            const bool hasPackedMetallicRoughness = source.GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) != 0;
            tools::ModelImportMaterial& material = context.output.materials.EmplaceBack();
            material.sourceMaterialIndex = materialIndex;
            aiString name;
            material.name = source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length != 0
                                ? containers::String(name.C_Str())
                                : containers::String::Printf("material_%u", materialIndex);

            aiColor4D color{1.0f, 1.0f, 1.0f, 1.0f};
            if (aiGetMaterialColor(&source, AI_MATKEY_BASE_COLOR, &color) != AI_SUCCESS)
            {
                aiGetMaterialColor(&source, AI_MATKEY_COLOR_DIFFUSE, &color);
            }
            material.baseColor[0] = color.r;
            material.baseColor[1] = color.g;
            material.baseColor[2] = color.b;
            material.baseColor[3] = color.a;
            if (aiGetMaterialColor(&source, AI_MATKEY_COLOR_EMISSIVE, &color) == AI_SUCCESS)
            {
                material.emissiveColor[0] = color.r;
                material.emissiveColor[1] = color.g;
                material.emissiveColor[2] = color.b;
                material.emissiveColor[3] = color.a;
            }
            if (aiGetMaterialColor(&source, AI_MATKEY_COLOR_SPECULAR, &color) == AI_SUCCESS)
            {
                material.specularColor[0] = color.r;
                material.specularColor[1] = color.g;
                material.specularColor[2] = color.b;
                material.specularColor[3] = color.a;
            }
            if (gltfSource)
            {
                f32 specularFactor = material.specularFactor;
                if (source.Get(AI_MATKEY_SPECULAR_FACTOR, specularFactor) == AI_SUCCESS)
                {
                    if (!std::isfinite(specularFactor) || specularFactor < 0.0f || specularFactor > 1.0f)
                        return false;
                    material.specularFactor = specularFactor;
                }
                f32 indexOfRefraction = material.indexOfRefraction;
                if (source.Get(AI_MATKEY_REFRACTI, indexOfRefraction) == AI_SUCCESS)
                {
                    if (!std::isfinite(indexOfRefraction) || indexOfRefraction < 1.0f)
                        return false;
                    material.indexOfRefraction = indexOfRefraction;
                }
            }
            aiGetMaterialFloat(&source, AI_MATKEY_METALLIC_FACTOR, &material.metallic);
            aiGetMaterialFloat(&source, AI_MATKEY_ROUGHNESS_FACTOR, &material.roughness);
            aiGetMaterialFloat(&source, AI_MATKEY_OPACITY, &material.opacity);
            if (!std::isfinite(material.opacity) || material.opacity < 0.0f || material.opacity > 1.0f)
                return false;
            aiString alphaMode;
            const bool hasExplicitAlphaMode = source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS;
            if (hasExplicitAlphaMode)
            {
                if (std::strcmp(alphaMode.C_Str(), "OPAQUE") == 0)
                    material.alphaMode = tools::ModelImportAlphaMode::Opaque;
                else if (std::strcmp(alphaMode.C_Str(), "MASK") == 0)
                    material.alphaMode = tools::ModelImportAlphaMode::Mask;
                else if (std::strcmp(alphaMode.C_Str(), "BLEND") == 0)
                    material.alphaMode = tools::ModelImportAlphaMode::Blend;
                else
                    return false;
            }
            f32 alphaCutoff = material.alphaCutoff;
            if (source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) == AI_SUCCESS)
            {
                if (!std::isfinite(alphaCutoff) || alphaCutoff < 0.0f || alphaCutoff > 1.0f)
                    return false;
                material.alphaCutoff = alphaCutoff;
            }
            int twoSided = 0;
            unsigned int valueCount = 1;
            if (aiGetMaterialIntegerArray(&source, AI_MATKEY_TWOSIDED, &twoSided, &valueCount) == AI_SUCCESS)
            {
                material.twoSided = twoSided != 0;
            }
            if (gltfSource && source.GetTextureCount(aiTextureType_LIGHTMAP) != 0u)
            {
                f32 occlusionStrength = material.occlusionStrength;
                if (source.Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), occlusionStrength) == AI_SUCCESS)
                {
                    if (!std::isfinite(occlusionStrength) || occlusionStrength < 0.0f || occlusionStrength > 1.0f)
                        return false;
                    material.occlusionStrength = occlusionStrength;
                }
            }

            for (const aiTextureType type : TextureTypes)
            {
                if ((type == aiTextureType_DIFFUSE && source.GetTextureCount(aiTextureType_BASE_COLOR) != 0) ||
                    (type == aiTextureType_HEIGHT && source.GetTextureCount(aiTextureType_NORMALS) != 0) ||
                    (type == aiTextureType_METALNESS && hasPackedMetallicRoughness))
                {
                    continue;
                }
                if (!AddMaterialTextures(scene, source, type, context, material))
                {
                    return false;
                }
            }
        }

        if (context.output.materials.Empty())
        {
            tools::ModelImportMaterial& material = context.output.materials.EmplaceBack();
            material.sourceMaterialIndex = 0;
            material.name = "default";
        }
        return true;
    }

    void AddTransformDiagnostics(InspectionContext& context, const u32 nodeIndex, const u32 flags)
    {
        if ((flags & tools::ModelImportNode_NonFiniteTransform) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Error, tools::ModelImportDiagnosticCode::NonFiniteTransform,
                          "Source node contains a non-finite local transform.", nodeIndex);
            return;
        }
        if ((flags & tools::ModelImportNode_NonAffineTransform) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Warning, tools::ModelImportDiagnosticCode::NonAffineTransform,
                          "Source node local transform is not affine.", nodeIndex);
        }
        if ((flags & tools::ModelImportNode_SingularTransform) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Warning, tools::ModelImportDiagnosticCode::SingularTransform,
                          "Source node local transform is singular.", nodeIndex);
        }
        if ((flags & tools::ModelImportNode_NegativeDeterminant) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Info, tools::ModelImportDiagnosticCode::NegativeDeterminant,
                          "Source node local transform contains a reflection.", nodeIndex);
        }
        if ((flags & tools::ModelImportNode_NonUniformScale) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Info, tools::ModelImportDiagnosticCode::NonUniformScale,
                          "Source node local transform contains non-uniform scale.", nodeIndex);
        }
        if ((flags & tools::ModelImportNode_ShearTransform) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Warning, tools::ModelImportDiagnosticCode::ShearTransform,
                          "Source node local transform contains shear or a non-orthogonal basis.", nodeIndex);
        }
    }

    tools::ModelImportResult CopyNodeTree(const aiNode& source, const u32 parentIndex, const u32 siblingOrdinal, const u32 depth,
                                          InspectionContext& context)
    {
        if (depth > context.limits.maximumNodeDepth || context.output.nodes.Size() >= context.limits.maximumNodes)
        {
            return tools::ModelImportResult::LimitExceeded;
        }

        const u32 nodeIndex = context.output.nodes.Size();
        tools::ModelImportNode& node = context.output.nodes.EmplaceBack();
        node.parentIndex = parentIndex;
        node.siblingOrdinal = siblingOrdinal;
        node.sourceName = source.mName.C_Str();
        node.displayName = SafeName(source.mName, "node", siblingOrdinal);
        node.localTransform = CopyMatrix(source.mTransformation);
        node.flags = ClassifyTransform(source.mTransformation);
        if (std::strstr(source.mName.C_Str(), "_$AssimpFbx$") != nullptr)
        {
            node.flags |= tools::ModelImportNode_DecoderSynthetic;
        }
        context.nodePointers.PushBack(&source);
        AddTransformDiagnostics(context, nodeIndex, node.flags);

        for (u32 childIndex = 0; childIndex < source.mNumChildren; ++childIndex)
        {
            const tools::ModelImportResult result = CopyNodeTree(*source.mChildren[childIndex], nodeIndex, childIndex, depth + 1, context);
            if (result != tools::ModelImportResult::Success)
            {
                return result;
            }
        }
        return tools::ModelImportResult::Success;
    }

    tools::ModelImportResult CopyPrimitive(const aiMesh& source, const u32 sourceMeshIndex, InspectionContext& context)
    {
        if (context.output.primitives.Size() >= context.limits.maximumPrimitives || source.mNumVertices == 0 || source.mNumFaces == 0)
        {
            return source.mNumVertices == 0 || source.mNumFaces == 0 ? tools::ModelImportResult::InvalidGeometry
                                                                     : tools::ModelImportResult::LimitExceeded;
        }
        if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 ||
            (source.mPrimitiveTypes & ~static_cast<unsigned int>(aiPrimitiveType_TRIANGLE)) != 0)
        {
            AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Error,
                          tools::ModelImportDiagnosticCode::UnsupportedPrimitive,
                          "Canonical model import currently accepts triangle primitives only.");
            return tools::ModelImportResult::InvalidGeometry;
        }

        const u64 indexCount = static_cast<u64>(source.mNumFaces) * 3ull;
        if (source.mNumVertices > context.limits.maximumVerticesPerPrimitive ||
            indexCount > context.limits.maximumIndicesPerPrimitive ||
            static_cast<u64>(source.mNumVertices) > context.limits.maximumVertices ||
            indexCount > context.limits.maximumIndices ||
            context.totalVertices > context.limits.maximumVertices - source.mNumVertices ||
            context.totalIndices > context.limits.maximumIndices - indexCount)
        {
            return tools::ModelImportResult::LimitExceeded;
        }
        if (source.mMaterialIndex >= context.output.materials.Size())
        {
            return tools::ModelImportResult::InvalidGeometry;
        }

        const u32 primitiveIndex = context.output.primitives.Size();
        tools::ModelImportPrimitive& primitive = context.output.primitives.EmplaceBack();
        primitive.sourceMeshIndex = sourceMeshIndex;
        primitive.sourceMaterialIndex = source.mMaterialIndex;
        primitive.displayName = SafeName(source.mName, "primitive", sourceMeshIndex);
        primitive.vertexCount = source.mNumVertices;
        if (context.output.materials[source.mMaterialIndex].twoSided)
        {
            primitive.flags = primitive.flags | meshes::SubmeshFlags::TwoSided;
        }

        u32 streamCapacity = 3;
        for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
        {
            streamCapacity += source.HasTextureCoords(channel) ? 1u : 0u;
        }
        for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
        {
            streamCapacity += source.HasVertexColors(channel) ? 1u : 0u;
        }
        primitive.vertexStreams.Reserve(streamCapacity);

        tools::ImportedVertexStream& positions =
            AddFloatStream(primitive, meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, 3);
        tools::ImportedVertexStream* normals = source.HasNormals()
                                                   ? &AddFloatStream(primitive, meshes::VertexSemantic::Normal, 0,
                                                                     meshes::VertexFormat::R32G32B32Float, 3)
                                                   : nullptr;
        tools::ImportedVertexStream* tangents = source.HasTangentsAndBitangents() && normals != nullptr
                                                    ? &AddFloatStream(primitive, meshes::VertexSemantic::Tangent, 0,
                                                                      meshes::VertexFormat::R32G32B32A32Float, 4)
                                                    : nullptr;

        tools::ImportedVertexStream* texCoords[AI_MAX_NUMBER_OF_TEXTURECOORDS]{};
        for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
        {
            if (source.HasTextureCoords(channel))
            {
                texCoords[channel] = &AddFloatStream(primitive, meshes::VertexSemantic::TexCoord, static_cast<u8>(channel),
                                                     meshes::VertexFormat::R32G32Float, 2);
            }
        }
        tools::ImportedVertexStream* colors[AI_MAX_NUMBER_OF_COLOR_SETS]{};
        for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
        {
            if (source.HasVertexColors(channel))
            {
                colors[channel] = &AddFloatStream(primitive, meshes::VertexSemantic::Color, static_cast<u8>(channel),
                                                  meshes::VertexFormat::R32G32B32A32Float, 4);
            }
        }

        for (u32 vertexIndex = 0; vertexIndex < source.mNumVertices; ++vertexIndex)
        {
            Append3(positions, source.mVertices[vertexIndex]);
            aiVector3D normal;
            if (normals != nullptr)
            {
                normal = source.mNormals[vertexIndex];
                const f32 lengthSquared = normal.SquareLength();
                if (lengthSquared > 1.0e-20f)
                {
                    normal /= std::sqrt(lengthSquared);
                }
                Append3(*normals, normal);
            }
            if (tangents != nullptr)
            {
                aiVector3D tangent = source.mTangents[vertexIndex];
                tangent -= normal * (normal * tangent);
                if (tangent.SquareLength() > 1.0e-20f)
                {
                    tangent.Normalize();
                }
                const aiVector3D bitangent = source.mBitangents[vertexIndex];
                const f32 handedness = (normal ^ tangent) * bitangent < 0.0f ? -1.0f : 1.0f;
                Append4(*tangents, tangent.x, tangent.y, tangent.z, handedness);
            }
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
            {
                if (texCoords[channel] != nullptr)
                {
                    texCoords[channel]->floatValues.PushBack(source.mTextureCoords[channel][vertexIndex].x);
                    texCoords[channel]->floatValues.PushBack(source.mTextureCoords[channel][vertexIndex].y);
                }
            }
            for (u32 channel = 0; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
            {
                if (colors[channel] != nullptr)
                {
                    const aiColor4D& color = source.mColors[channel][vertexIndex];
                    Append4(*colors[channel], color.r, color.g, color.b, color.a);
                }
            }
        }

        primitive.indices.Reserve(static_cast<u32>(indexCount));
        for (u32 faceIndex = 0; faceIndex < source.mNumFaces; ++faceIndex)
        {
            const aiFace& face = source.mFaces[faceIndex];
            if (face.mNumIndices != 3 || face.mIndices[0] >= source.mNumVertices || face.mIndices[1] >= source.mNumVertices ||
                face.mIndices[2] >= source.mNumVertices)
            {
                return tools::ModelImportResult::InvalidGeometry;
            }
            primitive.indices.PushBack(face.mIndices[0]);
            primitive.indices.PushBack(face.mIndices[1]);
            primitive.indices.PushBack(face.mIndices[2]);
        }

        if (source.mNumBones > context.limits.maximumBoneBindings ||
            context.totalBoneBindings > context.limits.maximumBoneBindings - source.mNumBones)
        {
            return tools::ModelImportResult::LimitExceeded;
        }
        primitive.boneBindings.Reserve(source.mNumBones);
        for (u32 boneIndex = 0; boneIndex < source.mNumBones; ++boneIndex)
        {
            const aiBone* const sourceBone = source.mBones[boneIndex];
            if (sourceBone == nullptr)
            {
                return tools::ModelImportResult::InvalidGeometry;
            }
            if (sourceBone->mNumWeights > context.limits.maximumInfluences ||
                context.totalInfluences > context.limits.maximumInfluences - sourceBone->mNumWeights)
            {
                return tools::ModelImportResult::LimitExceeded;
            }
            const u32 exactBoneNodeIndex = FindNodeIndex(context, sourceBone->mNode);
            if (sourceBone->mNode == nullptr || exactBoneNodeIndex == tools::InvalidModelImportIndex)
            {
                SetDiagnostic(context.report, "Bone-to-node identity is unresolved after decoder processing.");
                AddDiagnostic(context, tools::ModelImportDiagnosticSeverity::Error,
                              tools::ModelImportDiagnosticCode::AmbiguousBoneBinding,
                              "Bone-to-node identity is unresolved after decoder processing.",
                              tools::InvalidModelImportIndex, tools::InvalidModelImportIndex, primitiveIndex);
                return tools::ModelImportResult::AmbiguousBoneBinding;
            }
            tools::ModelImportBoneBinding& binding = primitive.boneBindings.EmplaceBack();
            binding.name = sourceBone->mName.C_Str();
            binding.nodeIndex = exactBoneNodeIndex;
            binding.armatureNodeIndex = FindNodeIndex(context, sourceBone->mArmature);
            binding.inverseBind = CopyMatrix(sourceBone->mOffsetMatrix);
            binding.weights.Reserve(sourceBone->mNumWeights);
            for (u32 weightIndex = 0; weightIndex < sourceBone->mNumWeights; ++weightIndex)
            {
                const aiVertexWeight& weight = sourceBone->mWeights[weightIndex];
                if (weight.mVertexId >= source.mNumVertices || !std::isfinite(weight.mWeight))
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
                binding.weights.PushBack({weight.mVertexId, weight.mWeight});
            }
            context.totalInfluences += sourceBone->mNumWeights;
        }
        context.totalBoneBindings += source.mNumBones;
        context.totalVertices += source.mNumVertices;
        context.totalIndices += indexCount;
        u64 primitivePayloadBytes = static_cast<u64>(primitive.indices.Size()) * sizeof(u32);
        for (const tools::ImportedVertexStream& stream : primitive.vertexStreams)
        {
            primitivePayloadBytes += static_cast<u64>(stream.floatValues.Size()) * sizeof(f32);
            primitivePayloadBytes += static_cast<u64>(stream.integerValues.Size()) * sizeof(u8);
        }
        for (const tools::ModelImportBoneBinding& binding : primitive.boneBindings)
        {
            primitivePayloadBytes += static_cast<u64>(binding.weights.Size()) * sizeof(tools::ModelImportBoneWeight);
        }
        if (primitivePayloadBytes > context.limits.maximumOwnedPayloadBytes ||
            context.ownedPayloadBytes > context.limits.maximumOwnedPayloadBytes - primitivePayloadBytes)
        {
            return tools::ModelImportResult::LimitExceeded;
        }
        context.ownedPayloadBytes += primitivePayloadBytes;
        static_cast<void>(primitiveIndex);
        return tools::ModelImportResult::Success;
    }

    const GeometryUseProvenance* FindGeometryUse(const InspectionContext& context, const tools::ModelImportSourceKind sourceKind,
                                                 const u64 sourceGeometryKey) noexcept
    {
        for (const GeometryUseProvenance& use : context.geometryUses)
        {
            if (use.sourceKind == sourceKind && use.sourceGeometryKey == sourceGeometryKey)
            {
                return &use;
            }
        }
        return nullptr;
    }

    tools::ModelImportResult BuildGeometryDefinitions(const aiScene& scene, InspectionContext& context,
                                                      containers::DynamicArray<u32>& meshToDefinition)
    {
        meshToDefinition.Resize(scene.mNumMeshes);
        for (u32 index = 0; index < meshToDefinition.Size(); ++index)
        {
            meshToDefinition[index] = tools::InvalidModelImportIndex;
        }

        for (u32 meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex)
        {
            const tools::ModelImportResult result = CopyPrimitive(*scene.mMeshes[meshIndex], meshIndex, context);
            if (result != tools::ModelImportResult::Success)
            {
                return result;
            }
        }

        if (context.primitiveProvenance.Empty())
        {
            if (scene.mNumMeshes > context.limits.maximumGeometryDefinitions)
            {
                return tools::ModelImportResult::LimitExceeded;
            }
            for (u32 meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex)
            {
                tools::ModelImportGeometryDefinition& definition = context.output.geometryDefinitions.EmplaceBack();
                definition.sourceKind = tools::ModelImportSourceKind::Generic;
                definition.sourceGeometryKey = static_cast<u64>(meshIndex) + 1u;
                definition.displayName = context.output.primitives[meshIndex].displayName;
                definition.primitiveIndices.PushBack(meshIndex);
                meshToDefinition[meshIndex] = context.output.geometryDefinitions.Size() - 1u;
            }
            return tools::ModelImportResult::Success;
        }

        for (const PrimitiveProvenance& provenance : context.primitiveProvenance)
        {
            if (provenance.aiMeshIndex >= scene.mNumMeshes || meshToDefinition[provenance.aiMeshIndex] != tools::InvalidModelImportIndex)
            {
                return tools::ModelImportResult::InvalidGeometry;
            }

            u32 definitionIndex = tools::InvalidModelImportIndex;
            for (u32 index = 0; index < context.output.geometryDefinitions.Size(); ++index)
            {
                const tools::ModelImportGeometryDefinition& existing = context.output.geometryDefinitions[index];
                if (existing.sourceKind == provenance.sourceKind && existing.sourceGeometryKey == provenance.sourceGeometryKey)
                {
                    definitionIndex = index;
                    break;
                }
            }
            if (definitionIndex == tools::InvalidModelImportIndex)
            {
                if (context.output.geometryDefinitions.Size() >= context.limits.maximumGeometryDefinitions)
                {
                    return tools::ModelImportResult::LimitExceeded;
                }
                definitionIndex = context.output.geometryDefinitions.Size();
                tools::ModelImportGeometryDefinition& definition = context.output.geometryDefinitions.EmplaceBack();
                definition.sourceKind = provenance.sourceKind;
                definition.sourceGeometryKey = provenance.sourceGeometryKey;
                definition.displayName = context.output.primitives[provenance.aiMeshIndex].displayName;
                if (const GeometryUseProvenance* const use = FindGeometryUse(context, provenance.sourceKind, provenance.sourceGeometryKey))
                {
                    definition.acceptedBindingKey = use->bindingKey;
                    definition.acceptedBindingFlags = use->flags;
                }
            }

            tools::ModelImportPrimitive& primitive = context.output.primitives[provenance.aiMeshIndex];
            primitive.sourcePartitionOrdinal = provenance.sourcePartitionOrdinal;
            context.output.geometryDefinitions[definitionIndex].primitiveIndices.PushBack(provenance.aiMeshIndex);
            meshToDefinition[provenance.aiMeshIndex] = definitionIndex;
        }

        for (tools::ModelImportGeometryDefinition& definition : context.output.geometryDefinitions)
        {
            for (u32 index = 1; index < definition.primitiveIndices.Size(); ++index)
            {
                const u32 moving = definition.primitiveIndices[index];
                const u32 ordinal = context.output.primitives[moving].sourcePartitionOrdinal;
                u32 destination = index;
                while (destination > 0 &&
                       context.output.primitives[definition.primitiveIndices[destination - 1]].sourcePartitionOrdinal > ordinal)
                {
                    definition.primitiveIndices[destination] = definition.primitiveIndices[destination - 1];
                    --destination;
                }
                definition.primitiveIndices[destination] = moving;
            }
            for (u32 ordinal = 0; ordinal < definition.primitiveIndices.Size(); ++ordinal)
            {
                if (context.output.primitives[definition.primitiveIndices[ordinal]].sourcePartitionOrdinal != ordinal)
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
            }
        }

        for (u32 meshIndex = 0; meshIndex < meshToDefinition.Size(); ++meshIndex)
        {
            if (meshToDefinition[meshIndex] == tools::InvalidModelImportIndex)
            {
                return tools::ModelImportResult::InvalidGeometry;
            }
        }
        return tools::ModelImportResult::Success;
    }

    bool GroupMatchesDefinition(const aiNode& node, const u32 firstMesh, const u32 meshCount,
                                const containers::DynamicArray<u32>& meshToDefinition,
                                const tools::ModelImportGeometryDefinition& definition) noexcept
    {
        if (meshCount != definition.primitiveIndices.Size())
        {
            return false;
        }
        for (u32 offset = 0; offset < meshCount; ++offset)
        {
            const u32 meshIndex = node.mMeshes[firstMesh + offset];
            if (meshIndex >= meshToDefinition.Size() || meshToDefinition[meshIndex] == tools::InvalidModelImportIndex)
            {
                return false;
            }
            if (meshIndex != definition.primitiveIndices[offset])
            {
                return false;
            }
        }
        return true;
    }

    tools::ModelImportResult BuildGeometryInstances(const aiScene& scene, const containers::DynamicArray<u32>& meshToDefinition,
                                                    InspectionContext& context)
    {
        static_cast<void>(scene);
        if (context.primitiveProvenance.Empty())
        {
            for (u32 nodeIndex = 0; nodeIndex < context.nodePointers.Size(); ++nodeIndex)
            {
                const aiNode& source = *context.nodePointers[nodeIndex];
                for (u32 meshOccurrence = 0; meshOccurrence < source.mNumMeshes; ++meshOccurrence)
                {
                    const u32 meshIndex = source.mMeshes[meshOccurrence];
                    if (meshIndex >= meshToDefinition.Size() ||
                        meshToDefinition[meshIndex] == tools::InvalidModelImportIndex)
                    {
                        return tools::ModelImportResult::InvalidGeometry;
                    }
                    if (context.output.geometryInstances.Size() >= context.limits.maximumGeometryInstances)
                    {
                        return tools::ModelImportResult::LimitExceeded;
                    }
                    const u32 instanceIndex = context.output.geometryInstances.Size();
                    tools::ModelImportGeometryInstance& instance = context.output.geometryInstances.EmplaceBack();
                    instance.nodeIndex = nodeIndex;
                    instance.geometryDefinitionIndex = meshToDefinition[meshIndex];
                    instance.occurrenceOrdinal = meshOccurrence;
                    context.output.nodes[nodeIndex].geometryInstanceIndices.PushBack(instanceIndex);
                }
            }
            return tools::ModelImportResult::Success;
        }

        for (u32 nodeIndex = 0; nodeIndex < context.nodePointers.Size(); ++nodeIndex)
        {
            const aiNode& source = *context.nodePointers[nodeIndex];
            u32 meshOffset = 0;
            u32 occurrenceOrdinal = 0;
            while (meshOffset < source.mNumMeshes)
            {
                const u32 sceneMeshIndex = source.mMeshes[meshOffset];
                if (sceneMeshIndex >= meshToDefinition.Size())
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
                const u32 definitionIndex = meshToDefinition[sceneMeshIndex];
                if (definitionIndex >= context.output.geometryDefinitions.Size())
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
                const tools::ModelImportGeometryDefinition& definition = context.output.geometryDefinitions[definitionIndex];
                const u32 groupCount = definition.primitiveIndices.Size();
                if (groupCount == 0 || meshOffset > source.mNumMeshes - groupCount ||
                    !GroupMatchesDefinition(source, meshOffset, groupCount, meshToDefinition, definition))
                {
                    return tools::ModelImportResult::InvalidGeometry;
                }
                if (context.output.geometryInstances.Size() >= context.limits.maximumGeometryInstances)
                {
                    return tools::ModelImportResult::LimitExceeded;
                }
                const u32 instanceIndex = context.output.geometryInstances.Size();
                tools::ModelImportGeometryInstance& instance = context.output.geometryInstances.EmplaceBack();
                instance.nodeIndex = nodeIndex;
                instance.geometryDefinitionIndex = definitionIndex;
                instance.occurrenceOrdinal = occurrenceOrdinal++;
                context.output.nodes[nodeIndex].geometryInstanceIndices.PushBack(instanceIndex);
                meshOffset += groupCount;
            }
        }
        return tools::ModelImportResult::Success;
    }

    void FinishReport(const InspectionContext& context)
    {
        if (context.report == nullptr)
        {
            return;
        }
        context.report->nodeCount = context.output.nodes.Size();
        context.report->geometryDefinitionCount = context.output.geometryDefinitions.Size();
        context.report->geometryInstanceCount = context.output.geometryInstances.Size();
        context.report->primitiveCount = context.output.primitives.Size();
        context.report->materialCount = context.output.materials.Size();
        context.report->textureReferenceCount = context.output.textureReferences.Size();
        context.report->embeddedTextureCount = context.output.embeddedTextures.Size();
        context.report->dependencyCount = context.output.dependencies.Size();
        context.report->boneBindingCount = context.totalBoneBindings;
        context.report->vertexCount = context.totalVertices;
        context.report->indexCount = context.totalIndices;
        context.report->influenceCount = context.totalInfluences;
    }

    tools::ModelImportResult ConvertScene(const aiScene& scene, InspectionContext& context)
    {
        if (scene.mRootNode == nullptr || scene.mNumMeshes == 0)
        {
            return tools::ModelImportResult::NoGeometry;
        }
        if (!CopyEmbeddedTextures(scene, context) || !ExtractMaterials(scene, context))
        {
            return context.callbackFailure != tools::ModelImportResult::Success ? context.callbackFailure
                                                                               : tools::ModelImportResult::LimitExceeded;
        }

        const tools::ModelImportResult nodeResult = CopyNodeTree(*scene.mRootNode, tools::InvalidModelImportIndex, 0, 0, context);
        if (nodeResult != tools::ModelImportResult::Success)
        {
            return nodeResult;
        }

        containers::DynamicArray<u32> meshToDefinition(memory::pools::Assets::GetInstance());
        const tools::ModelImportResult geometryResult = BuildGeometryDefinitions(scene, context, meshToDefinition);
        if (geometryResult != tools::ModelImportResult::Success)
        {
            return geometryResult;
        }
        const tools::ModelImportResult instanceResult = BuildGeometryInstances(scene, meshToDefinition, context);
        if (instanceResult != tools::ModelImportResult::Success)
        {
            return instanceResult;
        }
        if (context.callbackFailure != tools::ModelImportResult::Success)
        {
            return context.callbackFailure;
        }

        if (!context.primitiveProvenance.Empty())
        {
            switch (context.primitiveProvenance[0].sourceKind)
            {
            case tools::ModelImportSourceKind::Gltf2:
                context.output.decoderName = "Assimp glTF2";
                break;
            case tools::ModelImportSourceKind::Fbx:
                context.output.decoderName = "Assimp FBX";
                break;
            default:
                context.output.decoderName = "Assimp";
                break;
            }
        }
        else
        {
            context.output.decoderName = "Assimp";
        }
        FinishReport(context);
        return tools::ModelImportResult::Success;
    }

    tools::ModelImportResult ReadSourceSnapshot(const filesystem::AbsolutePath& source, const tools::ModelImportLimits& limits,
                                                containers::DynamicArray<u8>& bytes)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, source.AsChar(), "rb") != 0 || file == nullptr)
        {
            return tools::ModelImportResult::SourceNotFound;
        }
        if (_fseeki64(file, 0, SEEK_END) != 0)
        {
            std::fclose(file);
            return tools::ModelImportResult::ImportFailure;
        }
        const __int64 size = _ftelli64(file);
        if (size <= 0 || static_cast<u64>(size) > limits.maximumSourceBytes || static_cast<u64>(size) > ~u32{0})
        {
            std::fclose(file);
            return size > 0 ? tools::ModelImportResult::SourceTooLarge : tools::ModelImportResult::ImportFailure;
        }
        if (_fseeki64(file, 0, SEEK_SET) != 0)
        {
            std::fclose(file);
            return tools::ModelImportResult::ImportFailure;
        }
        bytes.Resize(static_cast<u32>(size));
        const bool read = std::fread(bytes.TypedData(), 1, bytes.Size(), file) == bytes.Size();
        const bool closed = std::fclose(file) == 0;
        return read && closed ? tools::ModelImportResult::Success : tools::ModelImportResult::ImportFailure;
    }

    tools::ModelImportResult InspectModelMemoryInternal(const void* const sourceData, const usize sourceSize,
                                                        const containers::StringView formatHint,
                                                        const containers::StringView sourceName,
                                                        const filesystem::AbsolutePath& sourceDirectory,
                                                        const char* const mainSourcePath,
                                                        const tools::ModelImportOptions& options,
                                                        const tools::ModelImportLimits& limits,
                                                        tools::ModelImportManifest& output,
                                                        tools::ModelImportReport* const report)
    {
        output.Clear();
        if (report != nullptr)
        {
            *report = {};
        }
        if (!ValidateOptions(options, limits) || sourceData == nullptr || sourceSize == 0 || formatHint.Empty() || sourceName.Empty())
        {
            return tools::ModelImportResult::InvalidArgument;
        }
        if (sourceSize > limits.maximumSourceBytes || sourceSize > static_cast<usize>(~u32{0}))
        {
            return tools::ModelImportResult::SourceTooLarge;
        }

        InspectionContext context(options, limits, output, report);
        context.sourceDirectory = sourceDirectory;
        context.resolveDependencies = mainSourcePath != nullptr;
        context.formatHint = containers::String(formatHint.Data(), static_cast<u32>(formatHint.Length()));
        output.sourceName = containers::String(sourceName.Data(), static_cast<u32>(sourceName.Length()));
        output.formatHint = context.formatHint;
        output.sourceFingerprint = crypto::Sha256(sourceData, sourceSize);
        if (mainSourcePath != nullptr &&
            !AddUniqueDependency(context, tools::ModelImportDependencyKind::MainSource, mainSourcePath, true))
        {
            output.Clear();
            return tools::ModelImportResult::LimitExceeded;
        }

        aiVanguardModelProvenanceSink provenance;
        provenance.userData = &context;
        provenance.recordPrimitive = RecordPrimitiveProvenance;
        provenance.recordGeometryUse = RecordGeometryUseProvenance;
        provenance.recordStructuralDependency = RecordStructuralDependency;

        Assimp::Importer importer;
        if (context.resolveDependencies)
        {
            importer.SetIOHandler(vanguard_assimp::CreateTrackingIOSystem(context.sourceDirectory.AsChar(), RecordOpenedFile, &context));
        }
        ConfigureImporter(importer, context, provenance);
        const aiScene* const scene =
            importer.ReadFileFromMemory(sourceData, sourceSize, ModelImportFlags(options), context.formatHint.AsChar());
        if (scene == nullptr)
        {
            tools::ModelImportResult result = tools::ModelImportResult::ImportFailure;
            if (context.cancelled)
            {
                result = tools::ModelImportResult::Cancelled;
            }
            else if (context.callbackFailure != tools::ModelImportResult::Success)
            {
                result = context.callbackFailure;
            }
            if (result == tools::ModelImportResult::UnsupportedExternalBuffer)
            {
                SetDiagnostic(report, "glTF v1 import requires buffers to be embedded in the model source.");
            }
            else if (result == tools::ModelImportResult::ConflictingGeometryBinding)
            {
                SetDiagnostic(report, "Source geometry is reused with conflicting per-instance material or skin bindings.");
            }
            else
            {
                SetDiagnostic(report, containers::String::Printf("Assimp failed to inspect '%.*s': %s",
                                                                  static_cast<int>(sourceName.Length()), sourceName.Data(),
                                                                  importer.GetErrorString()));
            }
            output.Clear();
            return result;
        }
        if (context.callbackFailure != tools::ModelImportResult::Success)
        {
            const tools::ModelImportResult result = context.callbackFailure;
            output.Clear();
            return result;
        }

        const tools::ModelImportResult result = ConvertScene(*scene, context);
        if (result != tools::ModelImportResult::Success)
        {
            if (report != nullptr && report->diagnostic.Empty())
            {
                report->diagnostic = tools::ToString(result);
            }
            output.Clear();
            return result;
        }
        if (options.progress != nullptr && !options.progress(1.0f, options.progressUserData))
        {
            output.Clear();
            if (report != nullptr)
            {
                *report = {};
                report->diagnostic = "Model inspection cancelled.";
            }
            return tools::ModelImportResult::Cancelled;
        }
        return tools::ModelImportResult::Success;
    }
} // namespace
