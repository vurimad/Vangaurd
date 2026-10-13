#include "../../../tests/support/scratch_directory.hpp"
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/mesh_tools/mesh_asset_compiler.hpp>
#include <vanguard/mesh_tools/mesh_import.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/mesh_tools/mesh_tools.hpp>
#include <vanguard/packages/packages.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[meshToolsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    bool WriteTestFile(const char* const path, const char* const text)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") != 0 || file == nullptr)
        {
            return false;
        }
        const size_t length = std::strlen(text);
        const bool written = std::fwrite(text, 1, length, file) == length;
        return std::fclose(file) == 0 && written;
    }

    bool ReadTestFile(const char* const path, vanguard::containers::DynamicArray<vanguard::u8>& bytes)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path, "rb") != 0 || file == nullptr || _fseeki64(file, 0, SEEK_END) != 0)
            return false;
        const __int64 size = _ftelli64(file);
        if (size <= 0 || size > static_cast<__int64>(~vanguard::u32{0}) || _fseeki64(file, 0, SEEK_SET) != 0)
        {
            std::fclose(file);
            return false;
        }
        bytes.Resize(static_cast<vanguard::u32>(size));
        const bool read = static_cast<__int64>(bytes.Size()) == size && std::fread(bytes.TypedData(), 1, bytes.Size(), file) == bytes.Size();
        return std::fclose(file) == 0 && read;
    }

    struct MeshCompilerFixture
    {
        vanguard::filesystem::AbsolutePath sourceFile;
        vanguard::resources::ResourceReference source;
        const vanguard::u8* bytes = nullptr;
        vanguard::u32 byteCount = 0;
    };

    bool ResolveMeshSourceFile(const vanguard::resources::ResourceReference source, vanguard::filesystem::AbsolutePath& file,
                               void* const userData) noexcept
    {
        const MeshCompilerFixture& fixture = *static_cast<MeshCompilerFixture*>(userData);
        if (source != fixture.source)
            return false;
        file = fixture.sourceFile;
        return true;
    }

    bool ResolveMeshDependency(const char*, vanguard::mesh_tools::MeshDependencySource& dependency, void* const userData) noexcept
    {
        const MeshCompilerFixture& fixture = *static_cast<MeshCompilerFixture*>(userData);
        dependency.identity = fixture.source;
        dependency.content = vanguard::crypto::Sha256(fixture.bytes, fixture.byteCount);
        return true;
    }

    bool CancelModelImportProgress(const vanguard::f32, void*) noexcept
    {
        return false;
    }

    bool ComposeTestModelMaterial(const vanguard::mesh_tools::ModelOutputBuildRequestContext& context,
                                  vanguard::assets::OwnedBuildRequest& request, void*) noexcept
    {
        if (context.source == nullptr || !context.source->sourceIdentity.IsValid() || !context.requestedOutput.IsValid() ||
            !context.requestedOutput.IsTyped() || !context.data.IsValid())
            return false;
        const vanguard::resources::ResourceReference source(
            context.source->sourceIdentity, vanguard::serialization::MakeFourCC('T', 'M', 'A', 'T'));
        const vanguard::assets::BuildRequest borrowed{
            {source, context.data.content, context.data.metadata}, context.requestedOutput, context.data.target, {}};
        return request.CopyFrom(borrowed);
    }

    vanguard::assets::AssetId MakeAssetId(const vanguard::u8 seed) noexcept
    {
        vanguard::assets::AssetId id;
        for (vanguard::u32 index = 0; index < static_cast<vanguard::u32>(sizeof(id.bytes)); ++index)
            id.bytes[index] = static_cast<vanguard::u8>(seed + index + 1u);
        return id;
    }

    bool MetadataDigest(const vanguard::assets::AssetMetadata& metadata, vanguard::crypto::Digest256& digest) noexcept
    {
        vanguard::containers::String encoded;
        if (vanguard::assets::EncodeMetadata(metadata, encoded) != vanguard::assets::MetadataResult::Success)
            return false;
        digest = vanguard::crypto::Sha256(encoded.AsChar(), encoded.Length());
        return !digest.IsEmpty();
    }

    void AppendU32(vanguard::containers::DynamicArray<vanguard::u8>& bytes, const vanguard::u32 value)
    {
        bytes.PushBack(static_cast<vanguard::u8>(value & 0xffu));
        bytes.PushBack(static_cast<vanguard::u8>((value >> 8u) & 0xffu));
        bytes.PushBack(static_cast<vanguard::u8>((value >> 16u) & 0xffu));
        bytes.PushBack(static_cast<vanguard::u8>((value >> 24u) & 0xffu));
    }

    void AppendBytes(vanguard::containers::DynamicArray<vanguard::u8>& bytes, const void* const data, const vanguard::u32 size)
    {
        const auto* const source = static_cast<const vanguard::u8*>(data);
        bytes.Reserve(bytes.Size() + size);
        for (vanguard::u32 index = 0; index < size; ++index)
        {
            bytes.PushBack(source[index]);
        }
    }

    void BuildTriangleGlb(vanguard::containers::DynamicArray<vanguard::u8>& bytes,
                          const char* const specularProperties = nullptr,
                          const bool includeIorAndOcclusion = false)
    {
        constexpr char Json[] = R"json({"asset":{"version":"2.0"},"buffers":[{"byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},{"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})json";
        std::string json(Json);
        if (specularProperties != nullptr)
        {
            // A real embedded image is used for the color/strength bindings; the
            // regression is on their glTF semantics and Assimp's dense slot contract.
            constexpr char ImageBase64[] = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jZl8AAAAASUVORK5CYII=";
            const std::string extensionsUsed = includeIorAndOcclusion
                                                   ? "\"extensionsUsed\":[\"KHR_materials_specular\",\"KHR_materials_ior\"],"
                                                   : "\"extensionsUsed\":[\"KHR_materials_specular\"],";
            const std::string materialPrefix = includeIorAndOcclusion
                                                   ? "\"occlusionTexture\":{\"index\":0,\"strength\":0.35},"
                                                   : "";
            const std::string iorExtension = includeIorAndOcclusion
                                                 ? ",\"KHR_materials_ior\":{\"ior\":1.8}"
                                                 : "";
            const std::string additions = extensionsUsed + std::string("\"images\":[{\"uri\":\"data:image/png;base64,") +
                                          ImageBase64 + "\"}],\"textures\":[{\"source\":0}],\"materials\":[{" + materialPrefix +
                                          "\"extensions\":{\"KHR_materials_specular\":{" + specularProperties + "}" + iorExtension + "}}],";
            const size_t meshes = json.find("\"meshes\":");
            const size_t indices = json.find("\"indices\":1}");
            if (meshes == std::string::npos || indices == std::string::npos)
                return;
            json.replace(indices, std::strlen("\"indices\":1}"), "\"indices\":1,\"material\":0}");
            json.insert(meshes, additions);
        }
        constexpr vanguard::f32 Positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        constexpr unsigned short Indices[] = {0, 1, 2};
        const vanguard::u32 JsonLength = static_cast<vanguard::u32>(json.size());
        const vanguard::u32 JsonPadding = (4u - (JsonLength & 3u)) & 3u;
        constexpr vanguard::u32 BinaryLength = sizeof(Positions) + sizeof(Indices);
        constexpr vanguard::u32 BinaryPadding = (4u - (BinaryLength & 3u)) & 3u;
        const vanguard::u32 PaddedJsonLength = JsonLength + JsonPadding;
        constexpr vanguard::u32 PaddedBinaryLength = BinaryLength + BinaryPadding;
        const vanguard::u32 TotalLength = 12u + 8u + PaddedJsonLength + 8u + PaddedBinaryLength;

        bytes.Clear();
        bytes.Reserve(TotalLength);
        AppendU32(bytes, 0x46546c67u);
        AppendU32(bytes, 2u);
        AppendU32(bytes, TotalLength);
        AppendU32(bytes, PaddedJsonLength);
        AppendU32(bytes, 0x4e4f534au);
        AppendBytes(bytes, json.data(), JsonLength);
        for (vanguard::u32 index = 0; index < JsonPadding; ++index)
        {
            bytes.PushBack(static_cast<vanguard::u8>(' '));
        }
        AppendU32(bytes, PaddedBinaryLength);
        AppendU32(bytes, 0x004e4942u);
        AppendBytes(bytes, Positions, sizeof(Positions));
        AppendBytes(bytes, Indices, sizeof(Indices));
        for (vanguard::u32 index = 0; index < BinaryPadding; ++index)
        {
            bytes.PushBack(0);
        }
    }
} // namespace

int main()
{
    namespace assets = vanguard::assets;
    namespace containers = vanguard::containers;
    namespace filesystem = vanguard::filesystem;
    namespace memory = vanguard::memory;
    namespace meshes = vanguard::meshes;
    namespace resources = vanguard::resources;
    namespace serialization = vanguard::serialization;
    namespace tools = vanguard::mesh_tools;

    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(tools::Initialize(), "mesh tools initialization");

    constexpr tools::MeshCookingProfileId CustomDeformableProfileId = 0x6465666f726d0001ull;
    static const tools::VertexPackingRule CustomDeformableRules[] = {{meshes::VertexSemantic::Custom, tools::AnySemanticIndex,
                                                                      meshes::VertexFormat::R32G32B32A32Float, meshes::VertexFormat::R32G32B32A32Float, 3,
                                                                      tools::VertexPackingRuleFlags::MatchAnySemanticIndex, nullptr}};
    const tools::MeshCookingProfile customDeformableProfile{
        CustomDeformableProfileId,
        7,
        tools::profiles::RuntimeStatic,
        meshes::MeshKind::Static,
        tools::MeshCookingProfileFlags::QuantizePositions,
        tools::UnmatchedVertexStreamPolicy::Reject,
        {CustomDeformableRules, static_cast<vanguard::u32>(sizeof(CustomDeformableRules) / sizeof(CustomDeformableRules[0]))}};
    Check(tools::FindMeshCookingProfile(tools::profiles::RuntimeStatic) != nullptr &&
              tools::FindMeshCookingProfile(tools::profiles::RuntimeSkinned4) != nullptr &&
              tools::FindMeshCookingProfile(tools::profiles::PreserveSource) != nullptr,
          "register built-in mesh cooking profiles during module initialization");
    Check(tools::RegisterMeshCookingProfile(customDeformableProfile) == tools::ProfileRegistrationResult::Success &&
              tools::FindMeshCookingProfile(CustomDeformableProfileId) != nullptr && tools::FindMeshCookingProfile(CustomDeformableProfileId)->version == 7 &&
              tools::FindMeshCookingProfile(CustomDeformableProfileId)->rules.Size() >
                  static_cast<vanguard::u32>(sizeof(CustomDeformableRules) / sizeof(CustomDeformableRules[0])),
          "resolve, copy, and expose a profile derived from the production static policy");
    vanguard::crypto::Digest256 runtimeStaticProfileFingerprint;
    vanguard::crypto::Digest256 repeatedRuntimeStaticProfileFingerprint;
    vanguard::crypto::Digest256 customProfileFingerprint;
    Check(tools::GetMeshCookingProfileFingerprint(tools::profiles::RuntimeStatic, runtimeStaticProfileFingerprint) &&
              tools::GetMeshCookingProfileFingerprint(tools::profiles::RuntimeStatic, repeatedRuntimeStaticProfileFingerprint) &&
              tools::GetMeshCookingProfileFingerprint(CustomDeformableProfileId, customProfileFingerprint) &&
              !runtimeStaticProfileFingerprint.IsEmpty() && runtimeStaticProfileFingerprint == repeatedRuntimeStaticProfileFingerprint &&
              !(runtimeStaticProfileFingerprint == customProfileFingerprint),
          "fingerprint the resolved mesh cooking profile policy deterministically for build invalidation");
    Check(tools::RegisterMeshCookingProfile(customDeformableProfile) == tools::ProfileRegistrationResult::DuplicateIdentifier,
          "reject duplicate mesh cooking profile identifiers");
    tools::MeshCookingProfile missingBaseProfile = customDeformableProfile;
    missingBaseProfile.id = 0x6d697373696e6701ull;
    missingBaseProfile.baseProfile = 0x6d697373696e67ffull;
    Check(tools::RegisterMeshCookingProfile(missingBaseProfile) == tools::ProfileRegistrationResult::InvalidArgument,
          "reject a derived profile whose base policy is not registered");

    constexpr char importedObj[] =
        "o ImportedTriangle\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1\n";
    const resources::ResourceReference importedMaterial(
        resources::ResourcePath::FromString("materials/imported.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T'));
    tools::MeshImportSettings importSettings;
    importSettings.sourceUpAxis = tools::SourceUpAxis::Y;
    importSettings.uniformScale = 2.0f;
    importSettings.defaultMaterial = importedMaterial;
    tools::ImportedMesh importedMesh;
    tools::MeshImportReport importReport;
    Check(tools::ImportMeshMemory(importedObj, sizeof(importedObj) - 1, "obj", "memory/imported.obj", importSettings, importedMesh, &importReport) ==
                  tools::MeshImportResult::Success,
          "import OBJ memory into Vanguard-owned mesh data");
    Check(importReport.submeshCount == 1 && importReport.vertexCount == 3 && importReport.indexCount == 3 &&
              importedMesh.materials.Size() == 1 && importedMesh.submeshes.Size() == 1,
          "report imported submesh, material, vertex, and index counts");
    if (importedMesh.submeshes.Size() == 1)
    {
        const tools::ImportedSubmesh& importedSubmesh = importedMesh.submeshes[0];
        const tools::ImportedVertexStream* importedPositions = nullptr;
        for (const tools::ImportedVertexStream& stream : importedSubmesh.vertexStreams)
        {
            if (stream.semantic == meshes::VertexSemantic::Position)
            {
                importedPositions = &stream;
                break;
            }
        }
        Check(importedPositions != nullptr && importedPositions->floatValues.Size() == 9 &&
                  std::fabs(importedPositions->floatValues[7]) < 0.0001f && std::fabs(importedPositions->floatValues[8] - 2.0f) < 0.0001f,
              "bake Y-up to Z-up conversion and uniform scale into imported positions");
        Check(importedSubmesh.indices.Size() == 3 && importedSubmesh.indices[0] == 2 && importedSubmesh.indices[1] == 1 &&
                  importedSubmesh.indices[2] == 0,
              "apply configured winding conversion");
    }
    containers::DynamicArray<vanguard::u8> importedCookedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter importedCookedWriter(importedCookedBytes);
    tools::SourceMesh importedSource = importedMesh.BuildSourceView();
    Check(tools::CookMesh(importedSource, importedCookedWriter) == tools::Result::Success,
          "feed imported Assimp data directly into the existing mesh cooker");

    constexpr char canonicalModelGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AAABAAIAAwAEAAUA","byteLength":84}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":72,"target":34962},
            {"buffer":0,"byteOffset":72,"byteLength":6,"target":34963},
            {"buffer":0,"byteOffset":78,"byteLength":6,"target":34963}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
        "images":[{"uri":"albedo.png"}],
        "textures":[{"source":0}],
        "materials":[
            {"name":"Body","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}}},
            {"name":"Glass"}],
        "meshes":[{"name":"SharedMesh","primitives":[
            {"attributes":{"POSITION":0},"indices":1,"material":0},
            {"attributes":{"POSITION":0},"indices":2,"material":1}]}],
        "nodes":[
            {"name":"Pivot","translation":[0,2,0],"children":[1,2]},
            {"name":"Duplicate","mesh":0},
            {"name":"Duplicate","translation":[3,0,0],"mesh":0}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportOptions modelOptions;
    tools::ModelImportLimits modelLimits;
    tools::ModelImportManifest canonicalManifest;
    tools::ModelImportReport canonicalReport;
    Check(tools::InspectModelMemory(canonicalModelGltf, sizeof(canonicalModelGltf) - 1, "gltf", "memory/shared.gltf",
                                    modelOptions, modelLimits, canonicalManifest, &canonicalReport) ==
              tools::ModelImportResult::Success,
          "inspect self-contained glTF into canonical model manifest");
    Check(canonicalManifest.geometryDefinitions.Size() == 1 && canonicalManifest.primitives.Size() == 2 &&
              canonicalManifest.geometryInstances.Size() == 2 && canonicalManifest.nodes.Size() == 3,
          "preserve one reusable multi-primitive geometry with two placement instances");
    Check(canonicalManifest.materials.Size() >= 2 && canonicalManifest.primitives.Size() == 2 &&
              canonicalManifest.primitives[0].sourceMaterialIndex == 0 &&
              canonicalManifest.primitives[1].sourceMaterialIndex == 1,
          "preserve source material table identity per primitive");
    Check(canonicalManifest.textureReferences.Size() == 2 &&
              canonicalManifest.textureReferences[0].authoredPath == "albedo.png" &&
              canonicalManifest.textureReferences[0].storage == tools::ImportedTextureStorage::ExternalSource &&
              canonicalManifest.textureReferences[1].authoredPath == "albedo.png" &&
              canonicalManifest.textureReferences[1].semantic == tools::ImportedTextureSemantic::MetallicRoughness,
          "preserve external image URI and packed metallic-roughness source semantics without requiring project resolution");
    Check(canonicalManifest.nodes.Size() == 3 && canonicalManifest.nodes[0].sourceName == "Pivot" &&
              canonicalManifest.nodes[1].sourceName == "Duplicate" &&
              canonicalManifest.nodes[2].sourceName == "Duplicate" &&
              std::fabs(canonicalManifest.nodes[0].localTransform.values[7] - 2.0f) < 0.0001f &&
              std::fabs(canonicalManifest.nodes[2].localTransform.values[3] - 3.0f) < 0.0001f,
          "preserve duplicate display names and exact local hierarchy transforms without identity collision");
    Check(canonicalManifest.sourceFingerprint ==
              vanguard::crypto::Sha256(canonicalModelGltf, sizeof(canonicalModelGltf) - 1),
          "fingerprint the exact model bytes inspected");

    // P1: versioned settings and immutable, zero-mutation import planning.
    tools::ModelImportSettings codecSettings;
    codecSettings.materialPolicy = tools::ModelImportMaterialPolicy::UseDefaultMaterial;
    codecSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    codecSettings.nextOutputId = 3u;
    codecSettings.nextCompositionNodeId = 13u;
    {
        tools::ModelImportOutputCorrespondence& second = codecSettings.outputs.EmplaceBack();
        second.kind = tools::ModelImportElementKind::Geometry;
        second.persistentId = 2u;
        second.output.key = "mesh-0000000000000002";
        second.output.type = meshes::MeshResourceType;
        second.source.sourceName = "Second";
        tools::ModelImportOutputCorrespondence& first = codecSettings.outputs.EmplaceBack();
        first.kind = tools::ModelImportElementKind::Geometry;
        first.persistentId = 1u;
        first.output.key = "mesh-0000000000000001";
        first.output.type = meshes::MeshResourceType;
        first.source.sourceName = "First";
    }
    {
        tools::ModelImportCompositionNodeCorrespondence& child = codecSettings.compositionNodes.EmplaceBack();
        child.stableNodeId = 11u;
        child.parentStableNodeId = 10u;
        child.sourceNodeIndex = 7u;
        child.source.sourceName = "Child";
        tools::ModelImportCompositionNodeCorrespondence& root = codecSettings.compositionNodes.EmplaceBack();
        root.stableNodeId = 10u;
        root.sourceNodeIndex = 3u;
        root.source.sourceName = "Root";
        tools::ModelImportCompositionMeshCorrespondence& mesh = codecSettings.compositionMeshes.EmplaceBack();
        mesh.stableComponentId = 12u;
        mesh.stableNodeId = 11u;
        mesh.geometryPersistentId = 1u;
        mesh.sourceInstanceIndex = 9u;
        mesh.sameGeometryOrdinal = 2u;
    }
    containers::DynamicArray<vanguard::u8> codecBytesA(memory::pools::Assets::GetInstance());
    containers::DynamicArray<vanguard::u8> codecBytesB(memory::pools::Assets::GetInstance());
    Check(tools::ValidateModelImportSettings(codecSettings) == tools::ModelImportSettingsResult::Success &&
              tools::EncodeModelImportSettings(codecSettings, codecBytesA) == tools::ModelImportSettingsResult::Success,
          "validate and encode bounded versioned model import settings");
    std::swap(codecSettings.outputs[0], codecSettings.outputs[1]);
    Check(tools::EncodeModelImportSettings(codecSettings, codecBytesB) == tools::ModelImportSettingsResult::Success &&
              codecBytesA.Size() == codecBytesB.Size() &&
              (codecBytesA.Empty() || std::memcmp(codecBytesA.TypedData(), codecBytesB.TypedData(), codecBytesA.Size()) == 0),
          "canonicalize model import settings encoding independently of in-memory correspondence order");
    tools::ModelImportSettings decodedCodecSettings;
    Check(tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion,
                                           {codecBytesA.TypedData(), codecBytesA.Size()}, decodedCodecSettings) ==
              tools::ModelImportSettingsResult::Success &&
              decodedCodecSettings.outputs.Size() == 2 && decodedCodecSettings.outputs[0].persistentId == 1u &&
              decodedCodecSettings.outputs[1].persistentId == 2u && decodedCodecSettings.compositionNodes.Size() == 2u &&
              decodedCodecSettings.compositionNodes[0].stableNodeId == 10u && decodedCodecSettings.compositionNodes[1].stableNodeId == 11u &&
              decodedCodecSettings.compositionMeshes.Size() == 1u && decodedCodecSettings.compositionMeshes[0].stableComponentId == 12u &&
              decodedCodecSettings.compositionMeshes[0].stableNodeId == 11u &&
              decodedCodecSettings.compositionMeshes[0].geometryPersistentId == 1u &&
              decodedCodecSettings.compositionMeshes[0].sourceInstanceIndex == 9u &&
              decodedCodecSettings.compositionMeshes[0].sameGeometryOrdinal == 2u,
          "decode wire4 model settings into canonical output, node and placed-mesh correspondence order");
    containers::DynamicArray<vanguard::u8> trailingCodecBytes(codecBytesA);
    trailingCodecBytes.PushBack(0u);
    tools::ModelImportSettings unchangedDecodeTarget;
    unchangedDecodeTarget.nextOutputId = 77u;
    Check(tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion,
                                           {trailingCodecBytes.TypedData(), trailingCodecBytes.Size()}, unchangedDecodeTarget) ==
                  tools::ModelImportSettingsResult::InvalidData &&
              unchangedDecodeTarget.nextOutputId == 77u &&
              tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion + 1u,
                                               {codecBytesA.TypedData(), codecBytesA.Size()}, unchangedDecodeTarget) ==
                  tools::ModelImportSettingsResult::UnsupportedVersion,
          "reject trailing data and unsupported settings versions without publishing partial decoded state");

    tools::ModelImportSettings oversizedCodecSettings;
    oversizedCodecSettings.materialPolicy = tools::ModelImportMaterialPolicy::UseDefaultMaterial;
    oversizedCodecSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    containers::String oversizedEvidenceName;
    oversizedEvidenceName.Resize(4096u);
    for (vanguard::u32 index = 0; index < oversizedEvidenceName.Length(); ++index)
        oversizedEvidenceName[index] = 'x';
    for (vanguard::u32 index = 0; index < 64u; ++index)
    {
        tools::ModelImportOutputCorrespondence& value = oversizedCodecSettings.outputs.EmplaceBack();
        value.kind = tools::ModelImportElementKind::Geometry;
        value.persistentId = static_cast<vanguard::u64>(index) + 1u;
        value.output.key = containers::String::Printf("mesh-%016llx", static_cast<unsigned long long>(value.persistentId));
        value.output.type = meshes::MeshResourceType;
        value.source.sourceName = oversizedEvidenceName;
    }
    oversizedCodecSettings.nextOutputId = 65u;
    containers::DynamicArray<vanguard::u8> oversizedCodecBytes(memory::pools::Assets::GetInstance());
    Check(tools::ValidateModelImportSettings(oversizedCodecSettings) == tools::ModelImportSettingsResult::Success &&
              tools::EncodeModelImportSettings(oversizedCodecSettings, oversizedCodecBytes) == tools::ModelImportSettingsResult::LimitExceeded,
          "enforce the generic asset 256 KiB settings cap without weakening valid bounded correspondence records");

    tools::ModelImportSettings duplicateCodecSettings;
    duplicateCodecSettings.materialPolicy = tools::ModelImportMaterialPolicy::UseDefaultMaterial;
    duplicateCodecSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    duplicateCodecSettings.nextOutputId = 3u;
    for (vanguard::u32 index = 0; index < 2u; ++index)
    {
        tools::ModelImportOutputCorrespondence& value = duplicateCodecSettings.outputs.EmplaceBack();
        value.kind = tools::ModelImportElementKind::Geometry;
        value.persistentId = static_cast<vanguard::u64>(index) + 1u;
        value.output.key = "mesh-duplicate";
        value.output.type = meshes::MeshResourceType;
    }
    Check(tools::ValidateModelImportSettings(duplicateCodecSettings) == tools::ModelImportSettingsResult::InvalidData,
          "reject duplicate persisted output keys before they can enter model settings metadata");
    tools::ModelImportSettings unknownProfileSettings;
    unknownProfileSettings.cook.meshCookingProfile = 0x7f7f7f7f7f7f7f7full;
    containers::DynamicArray<vanguard::u8> unknownProfileBytes(memory::pools::Assets::GetInstance());
    tools::ModelImportSettings decodedUnknownProfileSettings;
    Check(tools::ValidateModelImportSettings(unknownProfileSettings) == tools::ModelImportSettingsResult::Success &&
              tools::EncodeModelImportSettings(unknownProfileSettings, unknownProfileBytes) == tools::ModelImportSettingsResult::Success &&
              tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion,
                                               {unknownProfileBytes.TypedData(), unknownProfileBytes.Size()},
                                               decodedUnknownProfileSettings) == tools::ModelImportSettingsResult::Success &&
              decodedUnknownProfileSettings.cook.meshCookingProfile == unknownProfileSettings.cook.meshCookingProfile,
          "keep persisted model settings codec validity independent of live cooking-profile registration");

    const filesystem::AbsolutePath p1PlanningRoot = vanguard::tests::ScratchDirectory("mesh_tools_p1_planning");
    constexpr assets::SourceImporterId P1ImporterId = 0x706c616e6d6f646cull;
    const resources::ResourceTypeId p1MaterialType = serialization::MakeFourCC('V', 'M', 'A', 'T');
    const resources::ResourceTypeId p1TextureType = serialization::MakeFourCC('V', 'T', 'E', 'X');
    const resources::ResourceTypeId p1CompositionType = serialization::MakeFourCC('V', 'P', 'F', 'B');
    const assets::AssetId p1ParentAsset = MakeAssetId(0x20u);
    tools::ModelImportPlanningContext p1Context;
    p1Context.parentAsset = p1ParentAsset;
    p1Context.importer = P1ImporterId;
    p1Context.importerVersion = 1u;
    p1Context.acceptedStateGeneration = 41u;
    p1Context.sourceOrigin = p1PlanningRoot.AddFilePath("shared.gltf");
    p1Context.sourceRoot = p1PlanningRoot;
    p1Context.sourceDestination = p1PlanningRoot.AddFilePath("shared.gltf");
    p1Context.sourceIdentity = resources::ResourcePath::FromString("shared.gltf");
    p1Context.outputTypes = {meshes::MeshResourceType, p1MaterialType, p1TextureType, p1CompositionType};
    tools::ModelImportPlan unknownProfilePlan;
    Check(tools::PlanModelImport(canonicalManifest, unknownProfileSettings, p1Context, unknownProfilePlan) ==
              tools::ModelImportPlanResult::SettingsInvalid,
          "reject an unavailable mesh cooking profile at semantic planning time without corrupting wire decode semantics");

    tools::ModelImportPlanningContext outsideDestinationContext = p1Context;
    outsideDestinationContext.sourceDestination = filesystem::paths::ParentAbsolutePath(p1PlanningRoot).AddFilePath("p1_outside.gltf");
    tools::ModelImportPlan outsideDestinationPlan;
    Check(tools::PlanModelImport(canonicalManifest, tools::ModelImportSettings{}, outsideDestinationContext, outsideDestinationPlan) ==
              tools::ModelImportPlanResult::InvalidArgument,
          "reject a planned model destination that is not contained by the selected writable source root");
    tools::ModelImportPlanningContext mismatchedIdentityContext = p1Context;
    mismatchedIdentityContext.sourceIdentity = resources::ResourcePath::FromString("models/shared.gltf");
    tools::ModelImportPlan mismatchedIdentityPlan;
    Check(tools::PlanModelImport(canonicalManifest, tools::ModelImportSettings{}, mismatchedIdentityContext, mismatchedIdentityPlan) ==
              tools::ModelImportPlanResult::InvalidArgument,
          "reject a source identity that does not match the destination path relative to the selected source root");

    tools::ModelImportSettings firstImportSettings;
    firstImportSettings.materialPolicy = tools::ModelImportMaterialPolicy::UseDefaultMaterial;
    firstImportSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    tools::ModelImportPlan firstImportPlan;
    tools::ModelImportPlanReport firstImportPlanReport;
    Check(tools::PlanModelImport(canonicalManifest, firstImportSettings, p1Context, firstImportPlan, &firstImportPlanReport) ==
                  tools::ModelImportPlanResult::Success &&
              firstImportPlanReport.added == 1u && firstImportPlanReport.preserved == 0u &&
              firstImportPlan.GetOutputs().Size() == 1u && firstImportPlan.GetImportDescription().outputs.Size() == 1u &&
              firstImportPlan.GetOutputs()[0].kind == tools::ModelImportElementKind::Geometry &&
              firstImportPlan.GetOutputs()[0].output.type == meshes::MeshResourceType && !firstImportPlan.HasBlockingConflicts() &&
              firstImportSettings.outputs.Empty(),
          "plan first model import as owned immutable output schema without mutating candidate settings or durable state");
    Check(firstImportPlan.GetWitnesses().sourceFingerprint == canonicalManifest.sourceFingerprint &&
              firstImportPlan.GetWitnesses().parentAsset == p1ParentAsset &&
              firstImportPlan.GetWitnesses().acceptedStateGeneration == 41u &&
              !firstImportPlan.GetWitnesses().candidateSettingsFingerprint.IsEmpty(),
          "seal exact source, settings, identity and accepted-state witnesses into the immutable plan");

    tools::ModelImportPlanningContext externalOriginContext = p1Context;
    externalOriginContext.sourceOrigin = filesystem::paths::ParentAbsolutePath(p1PlanningRoot).AddFilePath("picked_outside.gltf");
    tools::ModelImportPlan externalOriginPlan;
    Check(tools::PlanModelImport(canonicalManifest, firstImportSettings, externalOriginContext, externalOriginPlan) ==
                  tools::ModelImportPlanResult::Success &&
              externalOriginPlan.GetWitnesses().copySourceToDestination &&
              externalOriginPlan.GetWitnesses().sourceOrigin == externalOriginContext.sourceOrigin &&
              externalOriginPlan.GetWitnesses().sourceDestination == p1Context.sourceDestination,
          "preserve the exact outside-project source origin and explicit copy-to-destination decision in a first-import plan");

    tools::ModelImportSettings acceptedFirstImportSettings;
    Check(tools::DecodeModelImportSettings(firstImportPlan.GetImportDescription().settingsVersion,
                                           {firstImportPlan.GetImportDescription().settings.TypedData(),
                                            firstImportPlan.GetImportDescription().settings.Size()},
                                           acceptedFirstImportSettings) == tools::ModelImportSettingsResult::Success &&
              acceptedFirstImportSettings.outputs.Size() == 1u && acceptedFirstImportSettings.materialSlots.Size() == 2u,
          "persist compact output and stable material-slot correspondence in proposed model settings");
    tools::ModelImportPlan provisionalReplan;
    tools::ModelImportPlanReport provisionalReplanReport;
    Check(tools::PlanModelImport(canonicalManifest, acceptedFirstImportSettings, p1Context, provisionalReplan, &provisionalReplanReport) ==
                  tools::ModelImportPlanResult::Success &&
              provisionalReplanReport.preserved == 1u && provisionalReplanReport.added == 0u &&
              provisionalReplan.GetOutputs().Size() == 1u &&
              provisionalReplan.GetOutputs()[0].output.key == firstImportPlan.GetOutputs()[0].output.key &&
              !provisionalReplan.HasBlockingConflicts(),
          "preserve provisional first-import output identities across repeated zero-mutation planning before metadata is committed");
    assets::AssetMetadata acceptedFirstImportMetadata;
    acceptedFirstImportMetadata.id = p1ParentAsset;
    acceptedFirstImportMetadata.importer = P1ImporterId;
    acceptedFirstImportMetadata.importerVersion = 1u;
    acceptedFirstImportMetadata.settingsVersion = firstImportPlan.GetImportDescription().settingsVersion;
    acceptedFirstImportMetadata.settings = containers::DynamicArray<vanguard::u8>(firstImportPlan.GetImportDescription().settings);
    for (const assets::AssetOutput& outputState : firstImportPlan.GetImportDescription().outputs)
    {
        assets::AssetOutput& copied = acceptedFirstImportMetadata.outputs.EmplaceBack();
        copied.key = outputState.key;
        copied.type = outputState.type;
    }

    // P3 foundation: exact-output model routing and one shared inspection across
    // source routing, dependency discovery and compilation.
    tools::ModelAssetCompilerConfig modelCompilerConfig;
    modelCompilerConfig.outputTypes = p1Context.outputTypes;
    modelCompilerConfig.composeMaterial = ComposeTestModelMaterial;
    tools::ModelAssetCompiler modelCompiler;
    Check(modelCompiler.Initialize(modelCompilerConfig), "initialize compound model asset compiler/router");
    const assets::SourceImporterDescriptor modelImporter = modelCompiler.GetImporterDescriptor();
    Check(modelImporter.IsValid() && modelImporter.createBuildRequest != nullptr && modelImporter.scanSourceReferences != nullptr,
          "publish a custom exact-output model source importer rather than a direct primary-output fallback");

    tools::ModelImportSettings p3CandidateSettings;
    p3CandidateSettings.materialPolicy = tools::ModelImportMaterialPolicy::ImportModelOwned;
    p3CandidateSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    tools::ModelImportPlanningContext p3Context = p1Context;
    p3Context.importer = modelImporter.id;
    p3Context.importerVersion = modelImporter.version;
    tools::ModelImportPlan p3Plan;
    tools::ModelImportPlanReport p3PlanReport;
    const tools::ModelImportPlanResult p3PlanResult =
        tools::PlanModelImport(canonicalManifest, p3CandidateSettings, p3Context, p3Plan, &p3PlanReport);
    if (p3PlanResult != tools::ModelImportPlanResult::Success)
        std::fprintf(stderr, "[meshToolsTests] P3 plan result=%u diagnostic=%s\n", static_cast<unsigned>(p3PlanResult),
                     p3PlanReport.diagnostic.AsChar());
    Check(p3PlanResult == tools::ModelImportPlanResult::Success &&
              p3Plan.GetImportDescription().outputs.Size() == 1u + canonicalManifest.materials.Size(),
          "plan a persisted model source for P3 exact child build routing");
    tools::ModelImportSettings p3AcceptedSettings;
    Check(tools::DecodeModelImportSettings(p3Plan.GetImportDescription().settingsVersion,
                                           {p3Plan.GetImportDescription().settings.TypedData(), p3Plan.GetImportDescription().settings.Size()},
                                           p3AcceptedSettings) == tools::ModelImportSettingsResult::Success &&
              p3AcceptedSettings.materialSlots.Size() == 2u &&
              p3AcceptedSettings.sourceFingerprint == canonicalManifest.sourceFingerprint,
          "decode the accepted P3 model settings with durable child/slot correspondence and exact source-snapshot authority");
    assets::AssetMetadata p3Metadata;
    p3Metadata.id = p1ParentAsset;
    p3Metadata.importer = modelImporter.id;
    p3Metadata.importerVersion = modelImporter.version;
    p3Metadata.settingsVersion = p3Plan.GetImportDescription().settingsVersion;
    p3Metadata.settings = containers::DynamicArray<vanguard::u8>(p3Plan.GetImportDescription().settings);
    for (const assets::AssetOutput& outputState : p3Plan.GetImportDescription().outputs)
    {
        assets::AssetOutput& copied = p3Metadata.outputs.EmplaceBack();
        copied.key = outputState.key;
        copied.type = outputState.type;
    }
    containers::String p3MetadataText;
    Check(assets::EncodeMetadata(p3Metadata, p3MetadataText) == assets::MetadataResult::Success,
          "encode committed-style P3 model metadata for the build boundary");
    assets::SourceRecord p3Source;
    p3Source.path = p1PlanningRoot.AddFilePath("shared.gltf");
    p3Source.sourceIdentity = resources::ResourcePath::FromString("shared.gltf");
    p3Source.metadataResult = assets::MetadataResult::Success;
    p3Source.metadata = p3Metadata;
    p3Source.importer = modelImporter.id;
    p3Source.availableImporterVersion = modelImporter.version;
    p3Source.primaryOutputType = meshes::MeshResourceType;
    const assets::SourceDataView p3SourceData{
        {reinterpret_cast<const vanguard::u8*>(canonicalModelGltf), static_cast<vanguard::u32>(sizeof(canonicalModelGltf) - 1u)},
        {reinterpret_cast<const vanguard::u8*>(p3MetadataText.AsChar()), p3MetadataText.Length()}, assets::TargetPlatform::WindowsD3D12};
    resources::ResourceReference p3MeshOutput;
    struct P3MaterialOutput
    {
        vanguard::u64 persistentId = 0u;
        resources::ResourceReference resource;
    };
    containers::DynamicArray<P3MaterialOutput> p3MaterialOutputs(memory::pools::Assets::GetInstance());
    for (const tools::ModelImportOutputCorrespondence& outputState : p3AcceptedSettings.outputs)
    {
        const resources::ResourceReference child = assets::GetAssetOutputReference(p3Metadata.id, outputState.output);
        if (outputState.kind == tools::ModelImportElementKind::Geometry)
            p3MeshOutput = child;
        else if (outputState.kind == tools::ModelImportElementKind::Material)
            p3MaterialOutputs.PushBack({outputState.persistentId, child});
    }
    containers::DynamicArray<resources::ResourceReference> p3UsedMaterialOutputs(memory::pools::Assets::GetInstance());
    for (const tools::ModelImportMaterialSlotCorrespondence& slot : p3AcceptedSettings.materialSlots)
    {
        resources::ResourceReference selected;
        for (const P3MaterialOutput& material : p3MaterialOutputs)
            if (material.persistentId == slot.materialPersistentId)
                selected = material.resource;
        bool duplicate = false;
        for (const resources::ResourceReference existing : p3UsedMaterialOutputs)
            duplicate = duplicate || existing == selected;
        if (selected.IsValid() && !duplicate)
            p3UsedMaterialOutputs.PushBack(selected);
    }
    Check(p3MeshOutput.IsValid() && p3UsedMaterialOutputs.Size() == 2u,
          "retain one exact geometry child and resolve its two slot-used model-owned material children without declaration-order assumptions");
    const containers::String staleP3Source =
        containers::String::Printf("%s\n", canonicalModelGltf);
    const assets::SourceDataView staleP3SourceData{
        {reinterpret_cast<const vanguard::u8*>(staleP3Source.AsChar()), staleP3Source.Length()},
        {reinterpret_cast<const vanguard::u8*>(p3MetadataText.AsChar()), p3MetadataText.Length()}, assets::TargetPlatform::WindowsD3D12};
    assets::BuildRequest staleP3MeshRequest;
    Check(!modelImporter.createBuildRequest(p3Source, staleP3SourceData, p3MeshOutput, staleP3MeshRequest, modelImporter.userData),
          "reject structurally equivalent edited model bytes when persisted child correspondence belongs to another exact source snapshot");
    assets::BuildRequest p3MeshRequest;
    Check(modelImporter.createBuildRequest(p3Source, p3SourceData, p3MeshOutput, p3MeshRequest, modelImporter.userData) &&
              p3MeshRequest.output == p3MeshOutput && p3MeshRequest.source.identity.ExpectedType() == tools::ModelGeometryBuildInputResourceType,
          "route an exact persistent model geometry output to the model geometry canonical compiler input");
    assets::BuildSystem p3BuildSystem;
    Check(p3BuildSystem.Initialize() && modelCompiler.Register(p3BuildSystem) == assets::Result::Success,
          "register the model geometry compiler with the normal asset build system");
    assets::BuildPlan p3MeshBuildPlan;
    bool p3DependenciesReady = p3BuildSystem.Prepare(p3MeshRequest, p3MeshBuildPlan) == assets::Result::Success &&
                               p3MeshBuildPlan.HasResourceEstimate();
    for (vanguard::u32 index = 0; index < p3UsedMaterialOutputs.Size() && p3DependenciesReady; ++index)
    {
        const containers::String content = containers::String::Printf("p3 material %u", index);
        p3DependenciesReady = p3MeshBuildPlan.SetGeneratedDependencyContent(
                                  p3UsedMaterialOutputs[index], vanguard::crypto::Sha256(content.AsChar(), content.Length())) ==
                              assets::Result::Success;
    }
    Check(p3DependenciesReady,
          "discover exact per-submesh model material dependencies with graph admission resources");
    assets::BuildOutput p3MeshBuildOutput;
    Check(p3BuildSystem.Execute(p3MeshRequest, p3MeshBuildPlan, p3MeshBuildOutput) == assets::Result::Success &&
              p3MeshBuildOutput.artifacts.Size() > 1u,
          "build one persistent model geometry output through the ordinary VMESH cooker");
    if (!p3MeshBuildOutput.artifacts.Empty())
    {
        containers::DynamicArray<vanguard::u8> p3MeshBytes(memory::pools::Assets::GetInstance());
        for (const assets::Artifact& artifact : p3MeshBuildOutput.artifacts)
            for (const vanguard::u8 byte : artifact.bytes)
                p3MeshBytes.PushBack(byte);
        filesystem::MemoryFileReader p3MeshReader(p3MeshBytes, 0);
        meshes::MeshFile p3Mesh;
        bool stableSlots = p3Mesh.Open(p3MeshReader) == meshes::Result::Success && p3Mesh.GetSubmeshes().Size() == 2u;
        if (stableSlots)
        {
            for (const meshes::SubmeshRecord& submesh : p3Mesh.GetSubmeshes())
            {
                bool known = false;
                for (const tools::ModelImportMaterialSlotCorrespondence& slot : p3AcceptedSettings.materialSlots)
                    known = known || slot.stableSlotId == submesh.stableId;
                stableSlots = stableSlots && known;
            }
        }
        Check(stableSlots, "persist planned stable model material-slot IDs into the cooked VMESH child");
    }
    const tools::ModelAssetCompilerStats p3Stats = modelCompiler.GetStats();
    Check(p3Stats.inspections == 1u && p3Stats.inspectionCacheHits >= 2u && p3Stats.cachedInspections == 1u,
          "reuse one bounded shared model inspection across routing, discovery and compilation");
    assets::SourceRecord p3OtherSource;
    p3OtherSource.path = p1PlanningRoot.AddFilePath("shared_copy.gltf");
    p3OtherSource.sourceIdentity = resources::ResourcePath::FromString("shared_copy.gltf");
    p3OtherSource.metadataResult = assets::MetadataResult::Success;
    p3OtherSource.metadata = p3Metadata;
    p3OtherSource.importer = modelImporter.id;
    p3OtherSource.availableImporterVersion = modelImporter.version;
    p3OtherSource.primaryOutputType = meshes::MeshResourceType;
    assets::BuildRequest p3OtherMeshRequest;
    Check(modelImporter.createBuildRequest(p3OtherSource, p3SourceData, p3MeshOutput, p3OtherMeshRequest, modelImporter.userData),
          "route an identical byte/settings snapshot under a distinct logical model source without aliasing cached source identity");
    const tools::ModelAssetCompilerStats p3DistinctSourceStats = modelCompiler.GetStats();
    Check(p3DistinctSourceStats.inspections == 2u && p3DistinctSourceStats.cachedInspections == 2u,
          "keep identical model bytes/settings from distinct logical source identities in separate inspection-cache entries");

    tools::ModelImportSettings p3ChangedSettings;
    Check(tools::DecodeModelImportSettings(p3Metadata.settingsVersion,
                                           {p3Metadata.settings.TypedData(), p3Metadata.settings.Size()}, p3ChangedSettings) ==
              tools::ModelImportSettingsResult::Success,
          "decode persisted P3 settings for cache policy invalidation proof");
    p3ChangedSettings.mesh.uniformScale = 2.0f;
    containers::DynamicArray<vanguard::u8> p3ChangedSettingsBytes(memory::pools::Assets::GetInstance());
    Check(tools::EncodeModelImportSettings(p3ChangedSettings, p3ChangedSettingsBytes) == tools::ModelImportSettingsResult::Success,
          "encode a distinct valid P3 settings snapshot");
    assets::SourceRecord p3ChangedSettingsSource;
    p3ChangedSettingsSource.path = p3Source.path;
    p3ChangedSettingsSource.sourceIdentity = p3Source.sourceIdentity;
    p3ChangedSettingsSource.metadataResult = assets::MetadataResult::Success;
    p3ChangedSettingsSource.metadata = p3Metadata;
    p3ChangedSettingsSource.metadata.settings = static_cast<containers::DynamicArray<vanguard::u8>&&>(p3ChangedSettingsBytes);
    p3ChangedSettingsSource.importer = modelImporter.id;
    p3ChangedSettingsSource.availableImporterVersion = modelImporter.version;
    p3ChangedSettingsSource.primaryOutputType = meshes::MeshResourceType;
    assets::BuildRequest p3ChangedSettingsRequest;
    Check(modelImporter.createBuildRequest(p3ChangedSettingsSource, p3SourceData, p3MeshOutput, p3ChangedSettingsRequest,
                                           modelImporter.userData),
          "route the same exact model source under a distinct valid importer-policy snapshot");
    const tools::ModelAssetCompilerStats p3ChangedSettingsStats = modelCompiler.GetStats();
    Check(p3ChangedSettingsStats.inspections == 3u && p3ChangedSettingsStats.cachedInspections == 3u,
          "invalidate shared model inspection cache when persisted importer settings change");

    tools::ModelImportSettings p3UvRequiredSettings = p3AcceptedSettings;
    Check(p3UvRequiredSettings.textureResolutions.Size() == 1u,
          "retain one persisted external texture-resolution decision for the model vertex-prerequisite proof");
    if (p3UvRequiredSettings.textureResolutions.Size() == 1u)
    {
        p3UvRequiredSettings.textureResolutions[0].resolution = tools::ModelImportTextureResolutionKind::ReuseManagedResource;
        p3UvRequiredSettings.textureResolutions[0].managedResource = resources::ResourceReference(
            resources::ResourcePath::FromString("textures/p3_uv_required.vtex"), p1TextureType);
        p3UvRequiredSettings.textureResolutions[0].projectSourceIdentity = {};
    }
    containers::DynamicArray<vanguard::u8> p3UvRequiredSettingsBytes(memory::pools::Assets::GetInstance());
    Check(tools::EncodeModelImportSettings(p3UvRequiredSettings, p3UvRequiredSettingsBytes) == tools::ModelImportSettingsResult::Success,
          "encode a persisted model snapshot whose material actually consumes the authored UV0 texture binding");
    assets::SourceRecord p3UvRequiredSource = p3Source;
    p3UvRequiredSource.metadata.settings = static_cast<containers::DynamicArray<vanguard::u8>&&>(p3UvRequiredSettingsBytes);
    containers::String p3UvRequiredMetadataText;
    Check(assets::EncodeMetadata(p3UvRequiredSource.metadata, p3UvRequiredMetadataText) == assets::MetadataResult::Success,
          "encode metadata for the consumed-texture vertex-prerequisite proof");
    const assets::SourceDataView p3UvRequiredSourceData{
        p3SourceData.content,
        {reinterpret_cast<const vanguard::u8*>(p3UvRequiredMetadataText.AsChar()), p3UvRequiredMetadataText.Length()},
        p3SourceData.target};
    assets::BuildRequest p3UvRequiredRequest;
    assets::BuildPlan p3UvRequiredPlan;
    Check(modelImporter.createBuildRequest(p3UvRequiredSource, p3UvRequiredSourceData, p3MeshOutput, p3UvRequiredRequest, modelImporter.userData) &&
              p3BuildSystem.Prepare(p3UvRequiredRequest, p3UvRequiredPlan) != assets::Result::Success,
          "reject RuntimeStatic model geometry that lacks real UV0 when its assigned material consumes a texture instead of synthesizing zero UVs");

    constexpr char ambiguousUnusedMaterialGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AAABAAIAAwAEAAUA","byteLength":84}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":72,"target":34962},
            {"buffer":0,"byteOffset":72,"byteLength":6,"target":34963}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "materials":[{"name":"Ambiguous"},{"name":"Ambiguous"},{"name":"Unique"}],
        "meshes":[{"name":"UniqueMesh","primitives":[
            {"attributes":{"POSITION":0},"indices":1,"material":0},
            {"attributes":{"POSITION":0},"indices":1,"material":1},
            {"attributes":{"POSITION":0},"indices":1,"material":2}]}],
        "nodes":[{"name":"Root","mesh":0}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportManifest ambiguousUnusedManifest;
    Check(tools::InspectModelMemory(ambiguousUnusedMaterialGltf, sizeof(ambiguousUnusedMaterialGltf) - 1u, "gltf",
                                    "memory/ambiguous-unused.gltf", modelOptions, modelLimits, ambiguousUnusedManifest) ==
                  tools::ModelImportResult::Success &&
              ambiguousUnusedManifest.geometryDefinitions.Size() == 1u && ambiguousUnusedManifest.materials.Size() >= 3u,
          "inspect a model with a unique geometry output, two same-name materials used through identical primitive schemas, and one positive-control material");
    tools::ModelImportPlanningContext ambiguousUnusedContext = p3Context;
    ambiguousUnusedContext.parentAsset = MakeAssetId(0x28u);
    tools::ModelImportPlan ambiguousUnusedPlan;
    Check(tools::PlanModelImport(ambiguousUnusedManifest, p3CandidateSettings, ambiguousUnusedContext, ambiguousUnusedPlan) ==
              tools::ModelImportPlanResult::Success,
          "plan independent outputs even when two model-owned materials have identical exact source evidence");
    tools::ModelImportSettings ambiguousUnusedSettings;
    Check(tools::DecodeModelImportSettings(ambiguousUnusedPlan.GetImportDescription().settingsVersion,
                                           {ambiguousUnusedPlan.GetImportDescription().settings.TypedData(),
                                            ambiguousUnusedPlan.GetImportDescription().settings.Size()},
                                           ambiguousUnusedSettings) == tools::ModelImportSettingsResult::Success,
          "decode the duplicate-evidence model output correspondences");
    assets::AssetMetadata ambiguousUnusedMetadata;
    ambiguousUnusedMetadata.id = ambiguousUnusedContext.parentAsset;
    ambiguousUnusedMetadata.importer = modelImporter.id;
    ambiguousUnusedMetadata.importerVersion = modelImporter.version;
    ambiguousUnusedMetadata.settingsVersion = ambiguousUnusedPlan.GetImportDescription().settingsVersion;
    ambiguousUnusedMetadata.settings = containers::DynamicArray<vanguard::u8>(ambiguousUnusedPlan.GetImportDescription().settings);
    for (const assets::AssetOutput& outputState : ambiguousUnusedPlan.GetImportDescription().outputs)
    {
        assets::AssetOutput& copied = ambiguousUnusedMetadata.outputs.EmplaceBack();
        copied.key = outputState.key;
        copied.type = outputState.type;
    }
    containers::String ambiguousUnusedMetadataText;
    Check(assets::EncodeMetadata(ambiguousUnusedMetadata, ambiguousUnusedMetadataText) == assets::MetadataResult::Success,
          "encode committed metadata for the duplicate-evidence routing proof");
    assets::SourceRecord ambiguousUnusedSource;
    ambiguousUnusedSource.path = p1PlanningRoot.AddFilePath("ambiguous-unused.gltf");
    ambiguousUnusedSource.sourceIdentity = resources::ResourcePath::FromString("ambiguous-unused.gltf");
    ambiguousUnusedSource.metadataResult = assets::MetadataResult::Success;
    ambiguousUnusedSource.metadata = ambiguousUnusedMetadata;
    ambiguousUnusedSource.importer = modelImporter.id;
    ambiguousUnusedSource.availableImporterVersion = modelImporter.version;
    ambiguousUnusedSource.primaryOutputType = meshes::MeshResourceType;
    const assets::SourceDataView ambiguousUnusedSourceData{
        {reinterpret_cast<const vanguard::u8*>(ambiguousUnusedMaterialGltf),
         static_cast<vanguard::u32>(sizeof(ambiguousUnusedMaterialGltf) - 1u)},
        {reinterpret_cast<const vanguard::u8*>(ambiguousUnusedMetadataText.AsChar()), ambiguousUnusedMetadataText.Length()},
        assets::TargetPlatform::WindowsD3D12};
    resources::ResourceReference ambiguousUnusedGeometryOutput;
    resources::ResourceReference ambiguousUnusedUniqueMaterialOutput;
    containers::DynamicArray<resources::ResourceReference> ambiguousUnusedMaterialOutputs(memory::pools::Assets::GetInstance());
    for (const tools::ModelImportOutputCorrespondence& outputState : ambiguousUnusedSettings.outputs)
    {
        const resources::ResourceReference child = assets::GetAssetOutputReference(ambiguousUnusedMetadata.id, outputState.output);
        if (outputState.kind == tools::ModelImportElementKind::Geometry)
            ambiguousUnusedGeometryOutput = child;
        else if (outputState.kind == tools::ModelImportElementKind::Material && outputState.source.sourceName == "Ambiguous")
            ambiguousUnusedMaterialOutputs.PushBack(child);
        else if (outputState.kind == tools::ModelImportElementKind::Material && outputState.source.sourceName == "Unique")
            ambiguousUnusedUniqueMaterialOutput = child;
    }
    assets::BuildRequest ambiguousUnusedGeometryRequest;
    bool ambiguousChildrenRejected = ambiguousUnusedMaterialOutputs.Size() == 2u;
    for (const resources::ResourceReference child : ambiguousUnusedMaterialOutputs)
    {
        assets::BuildRequest ambiguousChildRequest;
        ambiguousChildrenRejected = ambiguousChildrenRejected &&
                                    !modelImporter.createBuildRequest(ambiguousUnusedSource, ambiguousUnusedSourceData, child,
                                                                      ambiguousChildRequest, modelImporter.userData);
    }
    assets::BuildRequest ambiguousUnusedUniqueMaterialRequest;
    Check(ambiguousUnusedGeometryOutput.IsValid() &&
              modelImporter.createBuildRequest(ambiguousUnusedSource, ambiguousUnusedSourceData, ambiguousUnusedGeometryOutput,
                                               ambiguousUnusedGeometryRequest, modelImporter.userData) &&
              ambiguousUnusedUniqueMaterialOutput.IsValid() &&
              modelImporter.createBuildRequest(ambiguousUnusedSource, ambiguousUnusedSourceData, ambiguousUnusedUniqueMaterialOutput,
                                               ambiguousUnusedUniqueMaterialRequest, modelImporter.userData) &&
              ambiguousChildrenRejected,
          "publish a partial resolution cache: route unique geometry/material siblings while identical-evidence material children remain ambiguous");
    Check(modelCompiler.Unregister() == assets::Result::Success && modelCompiler.Shutdown() && p3BuildSystem.Shutdown(),
          "shutdown P3 model build routing without retained cache pins");

    tools::ModelAssetCompilerConfig boundedModelCompilerConfig = modelCompilerConfig;
    boundedModelCompilerConfig.maximumCachedInspections = 1u;
    tools::ModelAssetCompiler boundedModelCompiler;
    Check(boundedModelCompiler.Initialize(boundedModelCompilerConfig), "initialize one-entry bounded model inspection cache");
    const assets::SourceImporterDescriptor boundedModelImporter = boundedModelCompiler.GetImporterDescriptor();
    assets::BuildRequest boundedFirstRequest;
    assets::BuildRequest boundedSecondRequest;
    assets::BuildRequest boundedFirstAgainRequest;
    Check(boundedModelImporter.createBuildRequest(p3Source, p3SourceData, p3MeshOutput, boundedFirstRequest,
                                                  boundedModelImporter.userData) &&
              boundedModelImporter.createBuildRequest(p3OtherSource, p3SourceData, p3MeshOutput, boundedSecondRequest,
                                                       boundedModelImporter.userData) &&
              boundedModelImporter.createBuildRequest(p3Source, p3SourceData, p3MeshOutput, boundedFirstAgainRequest,
                                                       boundedModelImporter.userData),
          "route alternating logical model sources through a one-entry inspection cache");
    const tools::ModelAssetCompilerStats boundedModelStats = boundedModelCompiler.GetStats();
    Check(boundedModelStats.inspections == 3u && boundedModelStats.cachedInspections == 1u,
          "evict the oldest unpinned model inspection at the configured cache bound");
    Check(boundedModelCompiler.Shutdown(), "shutdown bounded model inspection cache");

    tools::ModelAssetCompiler concurrentModelCompiler;
    Check(concurrentModelCompiler.Initialize(modelCompilerConfig), "initialize concurrent model inspection cache proof");
    const assets::SourceImporterDescriptor concurrentModelImporter = concurrentModelCompiler.GetImporterDescriptor();
    constexpr vanguard::u32 ConcurrentInspectionRequests = 8u;
    std::array<assets::BuildRequest, ConcurrentInspectionRequests> concurrentRequests;
    std::array<bool, ConcurrentInspectionRequests> concurrentResults{};
    std::array<std::thread, ConcurrentInspectionRequests> concurrentThreads;
    std::atomic<vanguard::u32> concurrentReady{0u};
    std::atomic<bool> concurrentStart{false};
    for (vanguard::u32 index = 0; index < ConcurrentInspectionRequests; ++index)
    {
        concurrentThreads[index] = std::thread([&, index]()
        {
            concurrentReady.fetch_add(1u, std::memory_order_release);
            while (!concurrentStart.load(std::memory_order_acquire))
                std::this_thread::yield();
            concurrentResults[index] = concurrentModelImporter.createBuildRequest(
                p3Source, p3SourceData, p3MeshOutput, concurrentRequests[index], concurrentModelImporter.userData);
        });
    }
    while (concurrentReady.load(std::memory_order_acquire) != ConcurrentInspectionRequests)
        std::this_thread::yield();
    concurrentStart.store(true, std::memory_order_release);
    bool concurrentRoutesSucceeded = true;
    for (std::thread& thread : concurrentThreads)
        thread.join();
    for (const bool result : concurrentResults)
        concurrentRoutesSucceeded = concurrentRoutesSucceeded && result;
    const tools::ModelAssetCompilerStats concurrentModelStats = concurrentModelCompiler.GetStats();
    Check(concurrentRoutesSucceeded && concurrentModelStats.inspections == 1u &&
              concurrentModelStats.inspectionCacheHits >= ConcurrentInspectionRequests - 1u && concurrentModelStats.cachedInspections == 1u,
          "coalesce concurrent first requests for sibling model outputs into one Assimp inspection");
    Check(concurrentModelCompiler.Shutdown(), "shutdown concurrent model inspection cache proof");

    const containers::String savedCanonicalGeometryName = canonicalManifest.geometryDefinitions[0].displayName;
    const vanguard::crypto::Digest256 savedCanonicalFingerprint = canonicalManifest.sourceFingerprint;
    const containers::String firstImportOwnedDisplayName = firstImportPlan.GetOutputs()[0].displayName;
    canonicalManifest.geometryDefinitions[0].displayName = "RenamedSharedMesh";
    canonicalManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-renamed-source", 17u);
    Check(firstImportPlan.GetOutputs()[0].displayName == firstImportOwnedDisplayName &&
              firstImportPlan.GetWitnesses().sourceFingerprint == savedCanonicalFingerprint,
          "keep planned display data and source witnesses owned after the inspected manifest is mutated");
    tools::ModelImportPlanningContext renameContext = p1Context;
    renameContext.persistedMetadata = &acceptedFirstImportMetadata;
    renameContext.hasPersistedMetadataDigest = MetadataDigest(acceptedFirstImportMetadata, renameContext.persistedMetadataDigest);
    renameContext.acceptedStateGeneration = 42u;
    tools::ModelImportPlanningContext mismatchedMetadataDigestContext = renameContext;
    mismatchedMetadataDigestContext.persistedMetadataDigest.bytes[0] ^= 0x5au;
    tools::ModelImportPlan mismatchedMetadataDigestPlan;
    Check(tools::PlanModelImport(canonicalManifest, acceptedFirstImportSettings, mismatchedMetadataDigestContext,
                                 mismatchedMetadataDigestPlan) == tools::ModelImportPlanResult::InvalidArgument,
          "reject reimport planning when the claimed persisted metadata digest does not match the validated metadata snapshot");
    tools::ModelImportPlan renamedPlan;
    tools::ModelImportPlanReport renamedPlanReport;
    Check(tools::PlanModelImport(canonicalManifest, acceptedFirstImportSettings, renameContext, renamedPlan, &renamedPlanReport) ==
                  tools::ModelImportPlanResult::Success &&
              renamedPlanReport.preserved == 1u && renamedPlanReport.added == 0u && renamedPlanReport.removed == 0u &&
              renamedPlan.GetOutputs().Size() == 1u &&
              renamedPlan.GetOutputs()[0].output.key == firstImportPlan.GetOutputs()[0].output.key &&
              renamedPlan.GetMaterialSlotReconciliation().Size() == 2u && !renamedPlan.HasBlockingConflicts(),
          "preserve output and stable material-slot identities across a display-name-only source rename using stronger structural evidence");
    tools::ModelImportSettings editedIdentitySettings;
    containers::DynamicArray<vanguard::u8> acceptedSettingsBytes(memory::pools::Assets::GetInstance());
    Check(tools::EncodeModelImportSettings(acceptedFirstImportSettings, acceptedSettingsBytes) == tools::ModelImportSettingsResult::Success &&
              tools::DecodeModelImportSettings(tools::ModelImportSettingsVersion,
                                               {acceptedSettingsBytes.TypedData(), acceptedSettingsBytes.Size()},
                                               editedIdentitySettings) == tools::ModelImportSettingsResult::Success,
          "clone persisted model settings for working-copy identity edit rejection");
    if (!editedIdentitySettings.outputs.Empty())
        editedIdentitySettings.outputs[0].source.sourceName = "manual-identity-edit";
    tools::ModelImportPlan editedIdentityPlan;
    Check(tools::PlanModelImport(canonicalManifest, editedIdentitySettings, renameContext, editedIdentityPlan) ==
              tools::ModelImportPlanResult::SettingsInvalid,
          "reject unsupported direct working-copy edits to persisted output correspondence instead of silently ignoring them");
    canonicalManifest.geometryDefinitions[0].displayName = savedCanonicalGeometryName;
    canonicalManifest.sourceFingerprint = savedCanonicalFingerprint;
    if (!canonicalManifest.primitives.Empty() && !canonicalManifest.primitives[0].vertexStreams.Empty() &&
        !canonicalManifest.primitives[0].vertexStreams[0].floatValues.Empty())
    {
        const vanguard::f32 savedPosition = canonicalManifest.primitives[0].vertexStreams[0].floatValues[0];
        canonicalManifest.primitives[0].vertexStreams[0].floatValues[0] = savedPosition + 0.25f;
        canonicalManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-vertex-edit", 14u);
        tools::ModelImportPlan vertexEditPlan;
        tools::ModelImportPlanReport vertexEditReport;
        Check(tools::PlanModelImport(canonicalManifest, acceptedFirstImportSettings, renameContext, vertexEditPlan, &vertexEditReport) ==
                      tools::ModelImportPlanResult::Success &&
                  vertexEditReport.preserved == 1u && vertexEditReport.added == 0u && vertexEditReport.removed == 0u &&
                  vertexEditPlan.GetOutputs()[0].output.key == firstImportPlan.GetOutputs()[0].output.key,
              "preserve an unambiguous placed glTF geometry identity across ordinary vertex-content edits");
        canonicalManifest.primitives[0].vertexStreams[0].floatValues[0] = savedPosition;
        canonicalManifest.sourceFingerprint = savedCanonicalFingerprint;
    }

    const auto buildSyntheticPlanningManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                                    const char* const firstName, const char* const secondName,
                                                    const bool includeSecond, const bool secondDifferent) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        const auto appendGeometry = [&](const char* const name, const vanguard::u32 vertexCount, const vanguard::u32 indexCount) noexcept
        {
            const vanguard::u32 primitiveIndex = manifest.primitives.Size();
            tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
            primitive.sourcePartitionOrdinal = 0u;
            primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
            primitive.vertexCount = vertexCount;
            primitive.indices.Resize(indexCount);
            tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
            stream.semantic = meshes::VertexSemantic::Position;
            stream.format = meshes::VertexFormat::R32G32B32Float;
            stream.stride = 12u;
            tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
            geometry.sourceKind = tools::ModelImportSourceKind::Generic;
            geometry.sourceGeometryKey = static_cast<vanguard::u64>(manifest.geometryDefinitions.Size());
            geometry.displayName = name;
            geometry.primitiveIndices.PushBack(primitiveIndex);
        };
        appendGeometry(firstName, 3u, 3u);
        if (includeSecond)
            appendGeometry(secondName, secondDifferent ? 4u : 3u, secondDifferent ? 6u : 3u);
    };

    const auto metadataFromPlan = [&](const assets::AssetId id, const tools::ModelImportPlan& plan, assets::AssetMetadata& metadata) noexcept
    {
        metadata = {};
        metadata.id = id;
        metadata.importer = P1ImporterId;
        metadata.importerVersion = 1u;
        metadata.settingsVersion = plan.GetImportDescription().settingsVersion;
        metadata.settings = containers::DynamicArray<vanguard::u8>(plan.GetImportDescription().settings);
        for (const assets::AssetOutput& sourceOutput : plan.GetImportDescription().outputs)
        {
            assets::AssetOutput& destination = metadata.outputs.EmplaceBack();
            destination.key = sourceOutput.key;
            destination.type = sourceOutput.type;
        }
    };

    tools::ModelImportManifest removalSourceA;
    tools::ModelImportManifest removalSourceB;
    buildSyntheticPlanningManifest(removalSourceA, "p1-remove-a", "Keep", "Retire", true, true);
    buildSyntheticPlanningManifest(removalSourceB, "p1-remove-b", "Keep", "unused", false, false);
    tools::ModelImportPlan removalBaselinePlan;
    Check(tools::PlanModelImport(removalSourceA, firstImportSettings, p1Context, removalBaselinePlan) ==
                  tools::ModelImportPlanResult::Success &&
              removalBaselinePlan.GetOutputs().Size() == 2u,
          "build deterministic two-output baseline for reimport removal reconciliation");
    tools::ModelImportSettings removalBaselineSettings;
    Check(tools::DecodeModelImportSettings(removalBaselinePlan.GetImportDescription().settingsVersion,
                                           {removalBaselinePlan.GetImportDescription().settings.TypedData(),
                                            removalBaselinePlan.GetImportDescription().settings.Size()},
                                           removalBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode accepted removal baseline settings for source refresh");
    assets::AssetMetadata removalBaselineMetadata;
    metadataFromPlan(p1ParentAsset, removalBaselinePlan, removalBaselineMetadata);
    tools::ModelImportPlanningContext removalContext = p1Context;
    removalContext.persistedMetadata = &removalBaselineMetadata;
    removalContext.hasPersistedMetadataDigest = MetadataDigest(removalBaselineMetadata, removalContext.persistedMetadataDigest);
    tools::ModelImportPlan removalPlan;
    tools::ModelImportPlanReport removalReport;
    Check(tools::PlanModelImport(removalSourceB, removalBaselineSettings, removalContext, removalPlan, &removalReport) ==
                  tools::ModelImportPlanResult::Success &&
              removalReport.preserved == 1u && removalReport.removed == 1u && removalReport.added == 0u &&
              removalPlan.HasBlockingConflicts(),
          "classify preserved and removed outputs without guessing and conservatively block retirement without reference authority");
    bool sawUnavailableReferenceBlocker = false;
    bool sawStableSlotBlocker = false;
    for (const tools::ModelImportPlanBlocker& blocker : removalPlan.GetBlockers())
    {
        sawUnavailableReferenceBlocker = sawUnavailableReferenceBlocker ||
                                         blocker.kind == tools::ModelImportPlanBlockerKind::ReferenceStateUnavailable;
        sawStableSlotBlocker = sawStableSlotBlocker ||
                               blocker.kind == tools::ModelImportPlanBlockerKind::StableSlotReferenceUnavailable;
    }
    Check(sawUnavailableReferenceBlocker, "surface unavailable reference authority as an explicit destructive-reconciliation blocker");
    Check(sawStableSlotBlocker, "conservatively block stable material-slot retirement when exact external slot-reference evidence is unavailable");

    tools::ModelImportSettings postRemovalSettings;
    Check(tools::DecodeModelImportSettings(removalPlan.GetImportDescription().settingsVersion,
                                           {removalPlan.GetImportDescription().settings.TypedData(), removalPlan.GetImportDescription().settings.Size()},
                                           postRemovalSettings) == tools::ModelImportSettingsResult::Success &&
              postRemovalSettings.retiredOutputs.Size() == 1u,
          "tombstone removed output identity in proposed settings");
    const containers::String retiredOutputKey = postRemovalSettings.retiredOutputs[0].output.key;
    assets::AssetMetadata postRemovalMetadata;
    metadataFromPlan(p1ParentAsset, removalPlan, postRemovalMetadata);
    tools::ModelImportManifest replacementSourceC;
    buildSyntheticPlanningManifest(replacementSourceC, "p1-remove-c", "Keep", "Replacement", true, true);
    tools::ModelImportPlanningContext replacementContext = p1Context;
    replacementContext.persistedMetadata = &postRemovalMetadata;
    replacementContext.hasPersistedMetadataDigest = MetadataDigest(postRemovalMetadata, replacementContext.persistedMetadataDigest);
    tools::ModelImportPlan replacementPlan;
    Check(tools::PlanModelImport(replacementSourceC, postRemovalSettings, replacementContext, replacementPlan) ==
                  tools::ModelImportPlanResult::Success &&
              replacementPlan.GetOutputs().Size() == 2u,
          "plan later output addition after an accepted retirement baseline");
    bool recycledRetiredKey = false;
    for (const tools::ModelImportPlannedOutput& planned : replacementPlan.GetOutputs())
        recycledRetiredKey = recycledRetiredKey || planned.output.key == retiredOutputKey;
    Check(!recycledRetiredKey, "never recycle a retired output key for unrelated newly discovered model content");

    tools::ModelImportManifest ambiguousSourceA;
    tools::ModelImportManifest ambiguousSourceB;
    buildSyntheticPlanningManifest(ambiguousSourceA, "p1-ambiguous-a", "Old", "unused", false, false);
    buildSyntheticPlanningManifest(ambiguousSourceB, "p1-ambiguous-b", "NewA", "NewB", true, false);
    tools::ModelImportPlan ambiguousBaselinePlan;
    Check(tools::PlanModelImport(ambiguousSourceA, firstImportSettings, p1Context, ambiguousBaselinePlan) ==
                  tools::ModelImportPlanResult::Success,
          "build deterministic ambiguity baseline");
    tools::ModelImportSettings ambiguousBaselineSettings;
    Check(tools::DecodeModelImportSettings(ambiguousBaselinePlan.GetImportDescription().settingsVersion,
                                           {ambiguousBaselinePlan.GetImportDescription().settings.TypedData(),
                                            ambiguousBaselinePlan.GetImportDescription().settings.Size()},
                                           ambiguousBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode ambiguity baseline settings");
    assets::AssetMetadata ambiguousBaselineMetadata;
    metadataFromPlan(p1ParentAsset, ambiguousBaselinePlan, ambiguousBaselineMetadata);
    tools::ModelImportPlanningContext ambiguousContext = p1Context;
    ambiguousContext.persistedMetadata = &ambiguousBaselineMetadata;
    ambiguousContext.hasPersistedMetadataDigest = MetadataDigest(ambiguousBaselineMetadata, ambiguousContext.persistedMetadataDigest);
    tools::ModelImportPlan ambiguousPlan;
    tools::ModelImportPlanReport ambiguousReport;
    Check(tools::PlanModelImport(ambiguousSourceB, ambiguousBaselineSettings, ambiguousContext, ambiguousPlan, &ambiguousReport) ==
                  tools::ModelImportPlanResult::Success &&
              ambiguousReport.ambiguous == 1u && ambiguousReport.added == 2u &&
              ambiguousPlan.GetAmbiguousCandidates().Size() == 2u && ambiguousPlan.HasBlockingConflicts(),
          "report non-unique structural correspondence explicitly and allocate fresh identities instead of guessing");

    tools::ModelImportManifest manyToOneSourceA;
    tools::ModelImportManifest manyToOneSourceB;
    buildSyntheticPlanningManifest(manyToOneSourceA, "p1-many-to-one-a", "OldA", "OldB", true, false);
    buildSyntheticPlanningManifest(manyToOneSourceB, "p1-many-to-one-b", "Merged", "unused", false, false);
    tools::ModelImportPlan manyToOneBaselinePlan;
    Check(tools::PlanModelImport(manyToOneSourceA, firstImportSettings, p1Context, manyToOneBaselinePlan) ==
                  tools::ModelImportPlanResult::Success &&
              manyToOneBaselinePlan.GetOutputs().Size() == 2u,
          "build two-old-identity baseline for many-to-one reconciliation proof");
    tools::ModelImportSettings manyToOneBaselineSettings;
    Check(tools::DecodeModelImportSettings(manyToOneBaselinePlan.GetImportDescription().settingsVersion,
                                           {manyToOneBaselinePlan.GetImportDescription().settings.TypedData(),
                                            manyToOneBaselinePlan.GetImportDescription().settings.Size()},
                                           manyToOneBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode many-to-one baseline settings");
    assets::AssetMetadata manyToOneBaselineMetadata;
    metadataFromPlan(p1ParentAsset, manyToOneBaselinePlan, manyToOneBaselineMetadata);
    tools::ModelImportPlanningContext manyToOneContext = p1Context;
    manyToOneContext.persistedMetadata = &manyToOneBaselineMetadata;
    manyToOneContext.hasPersistedMetadataDigest = MetadataDigest(manyToOneBaselineMetadata, manyToOneContext.persistedMetadataDigest);
    tools::ModelImportPlan manyToOnePlan;
    tools::ModelImportPlanReport manyToOneReport;
    Check(tools::PlanModelImport(manyToOneSourceB, manyToOneBaselineSettings, manyToOneContext, manyToOnePlan, &manyToOneReport) ==
                  tools::ModelImportPlanResult::Success &&
              manyToOneReport.preserved == 0u && manyToOneReport.ambiguous == 2u && manyToOneReport.added == 1u &&
              manyToOnePlan.GetAmbiguousCandidates().Size() == 2u,
          "treat every old identity in a many-to-one correspondence as ambiguous instead of preserving the first one arbitrarily");

    tools::ModelImportManifest sameNameSourceA;
    tools::ModelImportManifest sameNameSourceB;
    buildSyntheticPlanningManifest(sameNameSourceA, "p1-same-name-a", "Same", "unused", false, false);
    buildSyntheticPlanningManifest(sameNameSourceB, "p1-same-name-b", "Same", "unused", false, false);
    sameNameSourceB.primitives[0].vertexCount = 4u;
    sameNameSourceB.primitives[0].indices.Resize(6u);
    tools::ModelImportPlan sameNameBaselinePlan;
    Check(tools::PlanModelImport(sameNameSourceA, firstImportSettings, p1Context, sameNameBaselinePlan) ==
                  tools::ModelImportPlanResult::Success,
          "build same-name replacement baseline");
    tools::ModelImportSettings sameNameBaselineSettings;
    Check(tools::DecodeModelImportSettings(sameNameBaselinePlan.GetImportDescription().settingsVersion,
                                           {sameNameBaselinePlan.GetImportDescription().settings.TypedData(),
                                            sameNameBaselinePlan.GetImportDescription().settings.Size()},
                                           sameNameBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode same-name replacement baseline settings");
    assets::AssetMetadata sameNameBaselineMetadata;
    metadataFromPlan(p1ParentAsset, sameNameBaselinePlan, sameNameBaselineMetadata);
    tools::ModelImportPlanningContext sameNameContext = p1Context;
    sameNameContext.persistedMetadata = &sameNameBaselineMetadata;
    sameNameContext.hasPersistedMetadataDigest = MetadataDigest(sameNameBaselineMetadata, sameNameContext.persistedMetadataDigest);
    tools::ModelImportPlan sameNamePlan;
    tools::ModelImportPlanReport sameNameReport;
    Check(tools::PlanModelImport(sameNameSourceB, sameNameBaselineSettings, sameNameContext, sameNamePlan, &sameNameReport) ==
                  tools::ModelImportPlanResult::Success &&
              sameNameReport.preserved == 0u && sameNameReport.removed == 1u && sameNameReport.added == 1u,
          "never preserve a persistent output solely because unrelated changed content kept the same display/source name");

    const auto buildSiblingReorderManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                                 const bool reordered) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        for (vanguard::u32 geometryIndex = 0u; geometryIndex < 2u; ++geometryIndex)
        {
            const vanguard::u32 primitiveIndex = manifest.primitives.Size();
            tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
            primitive.sourcePartitionOrdinal = 0u;
            primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
            primitive.vertexCount = 3u;
            primitive.indices.Resize(3u);
            tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
            stream.semantic = meshes::VertexSemantic::Position;
            stream.format = meshes::VertexFormat::R32G32B32Float;
            stream.stride = 12u;
            tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
            geometry.sourceKind = tools::ModelImportSourceKind::Gltf2;
            geometry.sourceGeometryKey = static_cast<vanguard::u64>(geometryIndex) + 1u;
            geometry.displayName = geometryIndex == 0u ? "GeoA" : "GeoB";
            geometry.primitiveIndices.PushBack(primitiveIndex);
        }
        for (vanguard::u32 nodeOffset = 0u; nodeOffset < 2u; ++nodeOffset)
        {
            const vanguard::u32 semanticNode = reordered ? 1u - nodeOffset : nodeOffset;
            tools::ModelImportNode& node = manifest.nodes.EmplaceBack();
            node.parentIndex = tools::InvalidModelImportIndex;
            node.siblingOrdinal = nodeOffset;
            node.sourceName = semanticNode == 0u ? "NodeA" : "NodeB";
            node.displayName = node.sourceName;
            tools::ModelImportGeometryInstance& instance = manifest.geometryInstances.EmplaceBack();
            instance.nodeIndex = nodeOffset;
            instance.geometryDefinitionIndex = semanticNode;
            instance.occurrenceOrdinal = 0u;
            node.geometryInstanceIndices.PushBack(manifest.geometryInstances.Size() - 1u);
        }
    };
    tools::ModelImportManifest siblingOrderSourceA;
    tools::ModelImportManifest siblingOrderSourceB;
    buildSiblingReorderManifest(siblingOrderSourceA, "p1-sibling-order-a", false);
    buildSiblingReorderManifest(siblingOrderSourceB, "p1-sibling-order-b", true);
    tools::ModelImportPlan siblingOrderBaselinePlan;
    Check(tools::PlanModelImport(siblingOrderSourceA, firstImportSettings, p1Context, siblingOrderBaselinePlan) ==
                  tools::ModelImportPlanResult::Success &&
              siblingOrderBaselinePlan.GetOutputs().Size() == 2u,
          "build two-geometry placement baseline for sibling reorder reconciliation");
    tools::ModelImportSettings siblingOrderBaselineSettings;
    Check(tools::DecodeModelImportSettings(siblingOrderBaselinePlan.GetImportDescription().settingsVersion,
                                           {siblingOrderBaselinePlan.GetImportDescription().settings.TypedData(),
                                            siblingOrderBaselinePlan.GetImportDescription().settings.Size()},
                                           siblingOrderBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode sibling reorder baseline settings");
    assets::AssetMetadata siblingOrderBaselineMetadata;
    metadataFromPlan(p1ParentAsset, siblingOrderBaselinePlan, siblingOrderBaselineMetadata);
    tools::ModelImportPlanningContext siblingOrderContext = p1Context;
    siblingOrderContext.persistedMetadata = &siblingOrderBaselineMetadata;
    siblingOrderContext.hasPersistedMetadataDigest = MetadataDigest(siblingOrderBaselineMetadata,
                                                                    siblingOrderContext.persistedMetadataDigest);
    tools::ModelImportPlan siblingOrderPlan;
    tools::ModelImportPlanReport siblingOrderReport;
    Check(tools::PlanModelImport(siblingOrderSourceB, siblingOrderBaselineSettings, siblingOrderContext, siblingOrderPlan,
                                 &siblingOrderReport) == tools::ModelImportPlanResult::Success &&
              siblingOrderReport.preserved == 2u && siblingOrderReport.added == 0u && siblingOrderReport.removed == 0u,
          "preserve geometry identities by semantic placement evidence when sibling order changes");
    containers::String baselineGeoAKey;
    containers::String reorderedGeoAKey;
    for (const tools::ModelImportPlannedOutput& output : siblingOrderBaselinePlan.GetOutputs())
        if (output.displayName == "GeoA")
            baselineGeoAKey = output.output.key;
    for (const tools::ModelImportPlannedOutput& output : siblingOrderPlan.GetOutputs())
        if (output.displayName == "GeoA")
            reorderedGeoAKey = output.output.key;
    Check(!baselineGeoAKey.Empty() && reorderedGeoAKey == baselineGeoAKey,
          "never swap persistent geometry keys merely because sibling positional ordinals were reordered");

    tools::ModelImportManifest partialPeerRemovalSource;
    buildSiblingReorderManifest(partialPeerRemovalSource, "p1-peer-removal", false);
    partialPeerRemovalSource.primitives.Resize(1u);
    partialPeerRemovalSource.geometryDefinitions.Resize(1u);
    partialPeerRemovalSource.nodes.Resize(1u);
    partialPeerRemovalSource.geometryInstances.Resize(1u);
    tools::ModelImportPlan partialPeerRemovalPlan;
    tools::ModelImportPlanReport partialPeerRemovalReport;
    Check(tools::PlanModelImport(partialPeerRemovalSource, siblingOrderBaselineSettings, siblingOrderContext,
                                 partialPeerRemovalPlan, &partialPeerRemovalReport) == tools::ModelImportPlanResult::Success &&
              partialPeerRemovalReport.preserved == 1u && partialPeerRemovalReport.removed == 1u &&
              partialPeerRemovalReport.ambiguous == 0u && partialPeerRemovalReport.added == 0u &&
              partialPeerRemovalPlan.GetOutputs().Size() == 1u && partialPeerRemovalPlan.GetOutputs()[0].output.key == baselineGeoAKey,
          "reserve an unchanged strong peer before weak fallback so deleting its same-schema sibling does not churn the survivor");

    tools::ModelImportManifest onePeerRenameSource;
    buildSiblingReorderManifest(onePeerRenameSource, "p1-one-peer-rename", false);
    onePeerRenameSource.nodes[1].sourceName = "RenamedNodeB";
    onePeerRenameSource.nodes[1].displayName = onePeerRenameSource.nodes[1].sourceName;
    tools::ModelImportPlan onePeerRenamePlan;
    tools::ModelImportPlanReport onePeerRenameReport;
    Check(tools::PlanModelImport(onePeerRenameSource, siblingOrderBaselineSettings, siblingOrderContext, onePeerRenamePlan,
                                 &onePeerRenameReport) == tools::ModelImportPlanResult::Success &&
              onePeerRenameReport.preserved == 2u && onePeerRenameReport.removed == 0u && onePeerRenameReport.ambiguous == 0u &&
              onePeerRenameReport.added == 0u,
          "reserve unchanged strong peers before preserving a uniquely remaining renamed peer through weak relationship evidence");

    tools::ModelImportManifest renamedPlacementSource;
    buildSiblingReorderManifest(renamedPlacementSource, "p1-placement-rename", false);
    renamedPlacementSource.nodes[0].sourceName = "RenamedNodeA";
    renamedPlacementSource.nodes[0].displayName = renamedPlacementSource.nodes[0].sourceName;
    renamedPlacementSource.nodes[1].sourceName = "RenamedNodeB";
    renamedPlacementSource.nodes[1].displayName = renamedPlacementSource.nodes[1].sourceName;
    tools::ModelImportPlan renamedPlacementPlan;
    tools::ModelImportPlanReport renamedPlacementReport;
    Check(tools::PlanModelImport(renamedPlacementSource, siblingOrderBaselineSettings, siblingOrderContext, renamedPlacementPlan,
                                 &renamedPlacementReport) == tools::ModelImportPlanResult::Success &&
              renamedPlacementReport.preserved == 0u && renamedPlacementReport.ambiguous == 2u &&
              renamedPlacementReport.added == 2u,
          "keep same-schema peer geometries ambiguous when placement names change and only weak relationship evidence remains");

    const auto buildStableSlotManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
        geometry.sourceKind = tools::ModelImportSourceKind::Fbx;
        geometry.sourceGeometryKey = 0x12345678ull;
        geometry.displayName = "StableFbxGeometry";
        for (vanguard::u32 ordinal = 0u; ordinal < 2u; ++ordinal)
        {
            const vanguard::u32 primitiveIndex = manifest.primitives.Size();
            tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
            primitive.sourcePartitionOrdinal = ordinal;
            primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
            primitive.vertexCount = 3u;
            primitive.indices.Resize(3u);
            tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
            stream.semantic = meshes::VertexSemantic::Position;
            stream.format = meshes::VertexFormat::R32G32B32Float;
            stream.stride = 12u;
            stream.floatValues.Resize(9u);
            geometry.primitiveIndices.PushBack(primitiveIndex);
        }
    };
    tools::ModelImportManifest slotSourceA;
    tools::ModelImportManifest slotSourceB;
    buildStableSlotManifest(slotSourceA, "p1-slot-a");
    buildStableSlotManifest(slotSourceB, "p1-slot-b");
    tools::ModelImportPlan slotBaselinePlan;
    Check(tools::PlanModelImport(slotSourceA, firstImportSettings, p1Context, slotBaselinePlan) ==
                  tools::ModelImportPlanResult::Success &&
              slotBaselinePlan.GetMaterialSlotReconciliation().Size() == 2u,
          "build stable-native-geometry material-slot baseline");
    tools::ModelImportSettings slotBaselineSettings;
    Check(tools::DecodeModelImportSettings(slotBaselinePlan.GetImportDescription().settingsVersion,
                                           {slotBaselinePlan.GetImportDescription().settings.TypedData(),
                                            slotBaselinePlan.GetImportDescription().settings.Size()},
                                           slotBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode stable material-slot baseline settings");
    assets::AssetMetadata slotBaselineMetadata;
    metadataFromPlan(p1ParentAsset, slotBaselinePlan, slotBaselineMetadata);
    tools::ModelImportPlanningContext slotContext = p1Context;
    slotContext.persistedMetadata = &slotBaselineMetadata;
    slotContext.hasPersistedMetadataDigest = MetadataDigest(slotBaselineMetadata, slotContext.persistedMetadataDigest);
    tools::ModelImportPlan slotPlan;
    Check(tools::PlanModelImport(slotSourceB, slotBaselineSettings, slotContext, slotPlan) ==
                  tools::ModelImportPlanResult::Success,
          "plan changed partition under a stable native geometry identity");
    vanguard::u32 addedSlots = 0u;
    vanguard::u32 removedSlots = 0u;
    vanguard::u32 preservedSlots = 0u;
    bool sawChangedSlotBlocker = false;
    for (const tools::ModelImportMaterialSlotReconciliation& row : slotPlan.GetMaterialSlotReconciliation())
    {
        addedSlots += row.state == tools::ModelImportSlotReconcileState::Added ? 1u : 0u;
        removedSlots += row.state == tools::ModelImportSlotReconcileState::Removed ? 1u : 0u;
        preservedSlots += row.state == tools::ModelImportSlotReconcileState::Preserved ? 1u : 0u;
    }
    for (const tools::ModelImportPlanBlocker& blocker : slotPlan.GetBlockers())
        sawChangedSlotBlocker = sawChangedSlotBlocker || blocker.kind == tools::ModelImportPlanBlockerKind::StableSlotReferenceUnavailable;
    Check(addedSlots == 2u && removedSlots == 2u && preservedSlots == 0u && sawChangedSlotBlocker,
          "retire and conservatively block structurally indistinguishable material slots instead of preserving IDs by partition ordinal alone");

    const auto buildSingleSlotEditManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                                 const vanguard::f32 firstPosition) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
        primitive.sourcePartitionOrdinal = 0u;
        primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
        primitive.vertexCount = 3u;
        primitive.indices.Resize(3u);
        tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
        stream.semantic = meshes::VertexSemantic::Position;
        stream.format = meshes::VertexFormat::R32G32B32Float;
        stream.stride = 12u;
        stream.floatValues.Resize(9u);
        stream.floatValues[0] = firstPosition;
        tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
        geometry.sourceKind = tools::ModelImportSourceKind::Fbx;
        geometry.sourceGeometryKey = 0x12345679ull;
        geometry.displayName = "SingleStableSlotGeometry";
        geometry.primitiveIndices.PushBack(0u);
    };
    tools::ModelImportManifest singleSlotSourceA;
    tools::ModelImportManifest singleSlotSourceB;
    buildSingleSlotEditManifest(singleSlotSourceA, "p1-single-slot-a", 0.0f);
    buildSingleSlotEditManifest(singleSlotSourceB, "p1-single-slot-b", 1.0f);
    tools::ModelImportPlan singleSlotBaselinePlan;
    Check(tools::PlanModelImport(singleSlotSourceA, firstImportSettings, p1Context, singleSlotBaselinePlan) ==
                  tools::ModelImportPlanResult::Success,
          "build single stable material-slot baseline");
    tools::ModelImportSettings singleSlotBaselineSettings;
    Check(tools::DecodeModelImportSettings(singleSlotBaselinePlan.GetImportDescription().settingsVersion,
                                           {singleSlotBaselinePlan.GetImportDescription().settings.TypedData(),
                                            singleSlotBaselinePlan.GetImportDescription().settings.Size()},
                                           singleSlotBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode single stable material-slot baseline settings");
    assets::AssetMetadata singleSlotBaselineMetadata;
    metadataFromPlan(p1ParentAsset, singleSlotBaselinePlan, singleSlotBaselineMetadata);
    tools::ModelImportPlanningContext singleSlotContext = p1Context;
    singleSlotContext.persistedMetadata = &singleSlotBaselineMetadata;
    singleSlotContext.hasPersistedMetadataDigest = MetadataDigest(singleSlotBaselineMetadata, singleSlotContext.persistedMetadataDigest);
    tools::ModelImportPlan singleSlotPlan;
    Check(tools::PlanModelImport(singleSlotSourceB, singleSlotBaselineSettings, singleSlotContext, singleSlotPlan) ==
                  tools::ModelImportPlanResult::Success &&
              singleSlotPlan.GetMaterialSlotReconciliation().Size() == 1u &&
              singleSlotPlan.GetMaterialSlotReconciliation()[0].state == tools::ModelImportSlotReconcileState::Preserved &&
              singleSlotPlan.GetMaterialSlotReconciliation()[0].stableSlotId ==
                  singleSlotBaselinePlan.GetMaterialSlotReconciliation()[0].stableSlotId,
          "preserve a unique stable material-slot ID across ordinary in-place vertex-content edits");

    const auto buildSplitSlotManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                            const bool reverseOrder) noexcept
    {
        buildSingleSlotEditManifest(manifest, fingerprintText, 1.0f);
        tools::ModelImportPrimitive& retainedPrimitive = manifest.primitives.EmplaceBack();
        retainedPrimitive.sourcePartitionOrdinal = 1u;
        retainedPrimitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
        retainedPrimitive.vertexCount = 3u;
        retainedPrimitive.indices.Resize(3u);
        tools::ImportedVertexStream& retainedStream = retainedPrimitive.vertexStreams.EmplaceBack();
        retainedStream.semantic = meshes::VertexSemantic::Position;
        retainedStream.format = meshes::VertexFormat::R32G32B32Float;
        retainedStream.stride = 12u;
        retainedStream.floatValues.Resize(9u);
        retainedStream.floatValues[0] = 0.0f;
        manifest.geometryDefinitions[0].primitiveIndices.Clear();
        manifest.geometryDefinitions[0].primitiveIndices.PushBack(reverseOrder ? 1u : 0u);
        manifest.geometryDefinitions[0].primitiveIndices.PushBack(reverseOrder ? 0u : 1u);
    };
    tools::ModelImportManifest splitSlotSource;
    tools::ModelImportManifest splitSlotReorderedSource;
    buildSplitSlotManifest(splitSlotSource, "p1-slot-split-a", false);
    buildSplitSlotManifest(splitSlotReorderedSource, "p1-slot-split-b", true);
    const auto splitPreservesExactOldPartition = [&](const tools::ModelImportManifest& source) noexcept
    {
        tools::ModelImportPlan plan;
        if (tools::PlanModelImport(source, singleSlotBaselineSettings, singleSlotContext, plan) != tools::ModelImportPlanResult::Success)
            return false;
        const tools::ModelImportMaterialSlotReconciliation& baseline = singleSlotBaselinePlan.GetMaterialSlotReconciliation()[0];
        vanguard::u32 preserved = 0u;
        vanguard::u32 added = 0u;
        bool exactOldPartitionPreserved = false;
        for (const tools::ModelImportMaterialSlotReconciliation& row : plan.GetMaterialSlotReconciliation())
        {
            preserved += row.state == tools::ModelImportSlotReconcileState::Preserved ? 1u : 0u;
            added += row.state == tools::ModelImportSlotReconcileState::Added ? 1u : 0u;
            exactOldPartitionPreserved = exactOldPartitionPreserved ||
                                         (row.state == tools::ModelImportSlotReconcileState::Preserved &&
                                          row.stableSlotId == baseline.stableSlotId && row.partitionKey == baseline.partitionKey);
        }
        return preserved == 1u && added == 1u && exactOldPartitionPreserved;
    };
    Check(splitPreservesExactOldPartition(splitSlotSource) && splitPreservesExactOldPartition(splitSlotReorderedSource),
          "preserve the exact old material-slot partition across a one-to-two split independent of current primitive order");

    tools::ModelImportManifest materialEditManifest;
    materialEditManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-material-binding-a", 21u);
    tools::ModelImportPrimitive& materialEditPrimitive = materialEditManifest.primitives.EmplaceBack();
    materialEditPrimitive.sourcePartitionOrdinal = 0u;
    materialEditPrimitive.sourceMaterialIndex = 0u;
    materialEditPrimitive.vertexCount = 3u;
    materialEditPrimitive.indices.Resize(3u);
    tools::ImportedVertexStream& materialEditPosition = materialEditPrimitive.vertexStreams.EmplaceBack();
    materialEditPosition.semantic = meshes::VertexSemantic::Position;
    materialEditPosition.format = meshes::VertexFormat::R32G32B32Float;
    materialEditPosition.stride = 12u;
    tools::ModelImportGeometryDefinition& materialEditGeometry = materialEditManifest.geometryDefinitions.EmplaceBack();
    materialEditGeometry.sourceKind = tools::ModelImportSourceKind::Generic;
    materialEditGeometry.displayName = "MaterialEditGeometry";
    materialEditGeometry.primitiveIndices.PushBack(0u);
    tools::ModelImportNode& materialEditNode = materialEditManifest.nodes.EmplaceBack();
    materialEditNode.parentIndex = tools::InvalidModelImportIndex;
    materialEditNode.sourceName = "MaterialEditNode";
    materialEditNode.displayName = materialEditNode.sourceName;
    tools::ModelImportGeometryInstance& materialEditInstance = materialEditManifest.geometryInstances.EmplaceBack();
    materialEditInstance.nodeIndex = 0u;
    materialEditInstance.geometryDefinitionIndex = 0u;
    materialEditInstance.occurrenceOrdinal = 0u;
    materialEditNode.geometryInstanceIndices.PushBack(0u);
    tools::ModelImportMaterial& materialEditMaterial = materialEditManifest.materials.EmplaceBack();
    materialEditMaterial.name = "MaterialEdit";
    materialEditMaterial.textureReferenceIndices.PushBack(0u);
    tools::ModelImportTextureReference& materialEditTexture = materialEditManifest.textureReferences.EmplaceBack();
    materialEditTexture.semantic = tools::ImportedTextureSemantic::BaseColor;
    materialEditTexture.storage = tools::ImportedTextureStorage::ExternalSource;
    materialEditTexture.authoredPath = "before.png";
    materialEditTexture.missing = true;
    tools::ModelImportSettings materialEditCandidateSettings;
    materialEditCandidateSettings.materialPolicy = tools::ModelImportMaterialPolicy::ImportModelOwned;
    materialEditCandidateSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    tools::ModelImportPlan materialEditBaselinePlan;
    Check(tools::PlanModelImport(materialEditManifest, materialEditCandidateSettings, p1Context, materialEditBaselinePlan) ==
                  tools::ModelImportPlanResult::Success,
          "build model-owned material baseline with an external texture binding");
    tools::ModelImportSettings materialEditSettings;
    Check(tools::DecodeModelImportSettings(materialEditBaselinePlan.GetImportDescription().settingsVersion,
                                           {materialEditBaselinePlan.GetImportDescription().settings.TypedData(),
                                            materialEditBaselinePlan.GetImportDescription().settings.Size()},
                                           materialEditSettings) == tools::ModelImportSettingsResult::Success,
          "decode model-owned material binding baseline settings");
    assets::AssetMetadata materialEditMetadata;
    metadataFromPlan(p1ParentAsset, materialEditBaselinePlan, materialEditMetadata);
    tools::ModelImportPlanningContext materialEditContext = p1Context;
    materialEditContext.persistedMetadata = &materialEditMetadata;
    materialEditContext.hasPersistedMetadataDigest = MetadataDigest(materialEditMetadata, materialEditContext.persistedMetadataDigest);
    containers::String baselineMaterialKey;
    for (const tools::ModelImportPlannedOutput& output : materialEditBaselinePlan.GetOutputs())
        if (output.kind == tools::ModelImportElementKind::Material)
            baselineMaterialKey = output.output.key;
    materialEditManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-material-binding-b", 21u);
    materialEditManifest.textureReferences[0].authoredPath = "after.png";
    tools::ModelImportPlan materialEditPlan;
    containers::String editedMaterialKey;
    const tools::ModelImportPlanResult materialEditResult =
        tools::PlanModelImport(materialEditManifest, materialEditSettings, materialEditContext, materialEditPlan);
    if (materialEditResult == tools::ModelImportPlanResult::Success)
        for (const tools::ModelImportPlannedOutput& output : materialEditPlan.GetOutputs())
            if (output.kind == tools::ModelImportElementKind::Material)
                editedMaterialKey = output.output.key;
    Check(materialEditResult == tools::ModelImportPlanResult::Success && !baselineMaterialKey.Empty() &&
              editedMaterialKey == baselineMaterialKey,
          "preserve model-owned material output identity across ordinary texture-binding content edits");
    materialEditManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-material-node-rename", 23u);
    materialEditManifest.nodes[0].sourceName = "RenamedMaterialEditNode";
    materialEditManifest.nodes[0].displayName = materialEditManifest.nodes[0].sourceName;
    tools::ModelImportPlan materialNodeRenamePlan;
    containers::String renamedNodeMaterialKey;
    const tools::ModelImportPlanResult materialNodeRenameResult =
        tools::PlanModelImport(materialEditManifest, materialEditSettings, materialEditContext, materialNodeRenamePlan);
    if (materialNodeRenameResult == tools::ModelImportPlanResult::Success)
        for (const tools::ModelImportPlannedOutput& output : materialNodeRenamePlan.GetOutputs())
            if (output.kind == tools::ModelImportElementKind::Material)
                renamedNodeMaterialKey = output.output.key;
    Check(materialNodeRenameResult == tools::ModelImportPlanResult::Success && renamedNodeMaterialKey == baselineMaterialKey,
          "preserve uniquely related model-owned geometry/material identity across a placement source-name rename");

    const auto buildSharedGeometryPlacementManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                                          const bool reordered) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
        primitive.sourcePartitionOrdinal = 0u;
        primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
        primitive.vertexCount = 3u;
        primitive.indices.Resize(3u);
        tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
        stream.semantic = meshes::VertexSemantic::Position;
        stream.format = meshes::VertexFormat::R32G32B32Float;
        stream.stride = 12u;
        tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
        geometry.sourceKind = tools::ModelImportSourceKind::Gltf2;
        geometry.sourceGeometryKey = 1u;
        geometry.displayName = "SharedGeometry";
        geometry.primitiveIndices.PushBack(0u);
        for (vanguard::u32 offset = 0u; offset < 2u; ++offset)
        {
            const vanguard::u32 semanticNode = reordered ? 1u - offset : offset;
            tools::ModelImportNode& node = manifest.nodes.EmplaceBack();
            node.parentIndex = tools::InvalidModelImportIndex;
            node.siblingOrdinal = offset;
            node.sourceName = semanticNode == 0u ? "PlacementA" : "PlacementB";
            node.displayName = node.sourceName;
            tools::ModelImportGeometryInstance& instance = manifest.geometryInstances.EmplaceBack();
            instance.nodeIndex = offset;
            instance.geometryDefinitionIndex = 0u;
            instance.occurrenceOrdinal = 0u;
            node.geometryInstanceIndices.PushBack(manifest.geometryInstances.Size() - 1u);
        }
    };
    tools::ModelImportManifest sharedPlacementSourceA;
    tools::ModelImportManifest sharedPlacementSourceB;
    buildSharedGeometryPlacementManifest(sharedPlacementSourceA, "p1-shared-placement-a", false);
    buildSharedGeometryPlacementManifest(sharedPlacementSourceB, "p1-shared-placement-b", true);
    tools::ModelImportPlan sharedPlacementBaselinePlan;
    Check(tools::PlanModelImport(sharedPlacementSourceA, firstImportSettings, p1Context, sharedPlacementBaselinePlan) ==
                  tools::ModelImportPlanResult::Success,
          "build reusable geometry multi-placement baseline");
    tools::ModelImportSettings sharedPlacementBaselineSettings;
    Check(tools::DecodeModelImportSettings(sharedPlacementBaselinePlan.GetImportDescription().settingsVersion,
                                           {sharedPlacementBaselinePlan.GetImportDescription().settings.TypedData(),
                                            sharedPlacementBaselinePlan.GetImportDescription().settings.Size()},
                                           sharedPlacementBaselineSettings) == tools::ModelImportSettingsResult::Success,
          "decode reusable geometry multi-placement baseline settings");
    assets::AssetMetadata sharedPlacementBaselineMetadata;
    metadataFromPlan(p1ParentAsset, sharedPlacementBaselinePlan, sharedPlacementBaselineMetadata);
    tools::ModelImportPlanningContext sharedPlacementContext = p1Context;
    sharedPlacementContext.persistedMetadata = &sharedPlacementBaselineMetadata;
    sharedPlacementContext.hasPersistedMetadataDigest = MetadataDigest(sharedPlacementBaselineMetadata,
                                                                       sharedPlacementContext.persistedMetadataDigest);
    tools::ModelImportPlan sharedPlacementPlan;
    tools::ModelImportPlanReport sharedPlacementReport;
    Check(tools::PlanModelImport(sharedPlacementSourceB, sharedPlacementBaselineSettings, sharedPlacementContext, sharedPlacementPlan,
                                 &sharedPlacementReport) == tools::ModelImportPlanResult::Success &&
              sharedPlacementReport.preserved == 1u && sharedPlacementReport.added == 0u && sharedPlacementReport.removed == 0u &&
              sharedPlacementPlan.GetOutputs()[0].output.key == sharedPlacementBaselinePlan.GetOutputs()[0].output.key,
          "preserve one reusable geometry identity when its placement-instance enumeration order changes");
    tools::ModelImportManifest sharedPlacementRenameSource;
    buildSharedGeometryPlacementManifest(sharedPlacementRenameSource, "p1-shared-placement-rename", false);
    sharedPlacementRenameSource.nodes[0].sourceName = "RenamedPlacementA";
    sharedPlacementRenameSource.nodes[0].displayName = sharedPlacementRenameSource.nodes[0].sourceName;
    sharedPlacementRenameSource.nodes[1].sourceName = "RenamedPlacementB";
    sharedPlacementRenameSource.nodes[1].displayName = sharedPlacementRenameSource.nodes[1].sourceName;
    tools::ModelImportPlan sharedPlacementRenamePlan;
    tools::ModelImportPlanReport sharedPlacementRenameReport;
    Check(tools::PlanModelImport(sharedPlacementRenameSource, sharedPlacementBaselineSettings, sharedPlacementContext,
                                 sharedPlacementRenamePlan, &sharedPlacementRenameReport) == tools::ModelImportPlanResult::Success &&
              sharedPlacementRenameReport.preserved == 1u && sharedPlacementRenameReport.added == 0u &&
              sharedPlacementRenameReport.removed == 0u &&
              sharedPlacementRenamePlan.GetOutputs()[0].output.key == sharedPlacementBaselinePlan.GetOutputs()[0].output.key,
          "preserve a uniquely related non-FBX geometry identity across placement source-name renames without positional authority");

    // P4 composition identity is authored-node/component identity, not mutable mesh payload identity.
    const auto buildCompositionIdentityManifest = [&](tools::ModelImportManifest& manifest, const char* const fingerprintText,
                                                       const bool reverseChildren, const bool renameNodes,
                                                       const vanguard::u32 replacementMask) noexcept
    {
        manifest.Clear();
        manifest.sourceFingerprint = vanguard::crypto::Sha256(fingerprintText, std::strlen(fingerprintText));
        for (vanguard::u32 geometryIndex = 0u; geometryIndex < 2u; ++geometryIndex)
        {
            const vanguard::u32 primitiveIndex = manifest.primitives.Size();
            tools::ModelImportPrimitive& primitive = manifest.primitives.EmplaceBack();
            primitive.sourcePartitionOrdinal = 0u;
            primitive.sourceMaterialIndex = tools::InvalidModelImportIndex;
            primitive.vertexCount = geometryIndex == 0u ? 3u : 4u;
            primitive.indices.Resize(geometryIndex == 0u ? 3u : 6u);
            tools::ImportedVertexStream& stream = primitive.vertexStreams.EmplaceBack();
            stream.semantic = meshes::VertexSemantic::Position;
            stream.format = meshes::VertexFormat::R32G32B32Float;
            stream.stride = 12u;
            stream.floatValues.Resize(primitive.vertexCount * 3u);
            tools::ModelImportGeometryDefinition& geometry = manifest.geometryDefinitions.EmplaceBack();
            const bool replacement = (replacementMask & (1u << geometryIndex)) != 0u;
            geometry.sourceKind = replacement ? tools::ModelImportSourceKind::Generic : tools::ModelImportSourceKind::Fbx;
            geometry.sourceGeometryKey = replacement ? (0x9000u + geometryIndex) : (0x7000u + geometryIndex);
            geometry.displayName = replacement ? (geometryIndex == 0u ? "ReplacementA" : "ReplacementB")
                                               : (geometryIndex == 0u ? "GeometryA" : "GeometryB");
            geometry.primitiveIndices.PushBack(primitiveIndex);
        }

        tools::ModelImportNode& rootNode = manifest.nodes.EmplaceBack();
        rootNode.parentIndex = tools::InvalidModelImportIndex;
        rootNode.sourceName = renameNodes ? "RenamedRoot" : "Root";
        rootNode.displayName = rootNode.sourceName;
        for (vanguard::u32 childOffset = 0u; childOffset < 2u; ++childOffset)
        {
            const vanguard::u32 semanticChild = reverseChildren ? 1u - childOffset : childOffset;
            const vanguard::u32 nodeIndex = manifest.nodes.Size();
            tools::ModelImportNode& node = manifest.nodes.EmplaceBack();
            node.parentIndex = 0u;
            node.siblingOrdinal = childOffset;
            if (semanticChild == 0u)
                node.sourceName = renameNodes ? "RenamedSingle" : "Single";
            else
                node.sourceName = renameNodes ? "RenamedMulti" : "Multi";
            node.displayName = node.sourceName;
            const auto appendInstance = [&](const vanguard::u32 geometryDefinitionIndex) noexcept
            {
                tools::ModelImportGeometryInstance& instance = manifest.geometryInstances.EmplaceBack();
                instance.nodeIndex = nodeIndex;
                instance.geometryDefinitionIndex = geometryDefinitionIndex;
                instance.occurrenceOrdinal = 0u;
                node.geometryInstanceIndices.PushBack(manifest.geometryInstances.Size() - 1u);
            };
            appendInstance(0u);
            if (semanticChild == 1u)
                appendInstance(1u);
        }
    };

    tools::ModelImportSettings compositionIdentitySettings;
    compositionIdentitySettings.materialPolicy = tools::ModelImportMaterialPolicy::UseDefaultMaterial;
    compositionIdentitySettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
    compositionIdentitySettings.includeComposition = true;
    tools::ModelImportManifest compositionIdentityBaselineSource;
    buildCompositionIdentityManifest(compositionIdentityBaselineSource, "p4-composition-baseline", false, false, 0u);
    tools::ModelImportPlan compositionIdentityBaselinePlan;
    Check(tools::PlanModelImport(compositionIdentityBaselineSource, compositionIdentitySettings, p1Context,
                                 compositionIdentityBaselinePlan) == tools::ModelImportPlanResult::Success,
          "P4 plan a one-entity composition identity baseline");
    tools::ModelImportSettings compositionIdentityBaselineSettings;
    Check(tools::DecodeModelImportSettings(compositionIdentityBaselinePlan.GetImportDescription().settingsVersion,
                                           {compositionIdentityBaselinePlan.GetImportDescription().settings.TypedData(),
                                            compositionIdentityBaselinePlan.GetImportDescription().settings.Size()},
                                           compositionIdentityBaselineSettings) == tools::ModelImportSettingsResult::Success &&
              compositionIdentityBaselineSettings.compositionNodes.Size() == 3u &&
              compositionIdentityBaselineSettings.compositionMeshes.Size() == 3u,
          "P4 persist three authored node IDs plus one durable component ID for every mesh instance in wire4 settings");
    const auto compositionNodeId = [](const tools::ModelImportSettings& settings, const vanguard::u32 sourceNodeIndex) noexcept
    {
        vanguard::u64 result = 0u;
        for (const tools::ModelImportCompositionNodeCorrespondence& node : settings.compositionNodes)
        {
            if (node.sourceNodeIndex != sourceNodeIndex)
                continue;
            if (result != 0u)
                return vanguard::u64{0};
            result = node.stableNodeId;
        }
        return result;
    };
    const auto compositionMeshId = [](const tools::ModelImportSettings& settings, const vanguard::u32 sourceInstanceIndex) noexcept
    {
        vanguard::u64 result = 0u;
        for (const tools::ModelImportCompositionMeshCorrespondence& mesh : settings.compositionMeshes)
        {
            if (mesh.sourceInstanceIndex != sourceInstanceIndex)
                continue;
            if (result != 0u)
                return vanguard::u64{0};
            result = mesh.stableComponentId;
        }
        return result;
    };
    const vanguard::u64 baselineRootNodeId = compositionNodeId(compositionIdentityBaselineSettings, 0u);
    const vanguard::u64 baselineSingleNodeId = compositionNodeId(compositionIdentityBaselineSettings, 1u);
    const vanguard::u64 baselineMultiNodeId = compositionNodeId(compositionIdentityBaselineSettings, 2u);
    const vanguard::u64 baselineSingleMeshId = compositionMeshId(compositionIdentityBaselineSettings, 0u);
    const vanguard::u64 baselineMultiMesh0Id = compositionMeshId(compositionIdentityBaselineSettings, 1u);
    const vanguard::u64 baselineMultiMesh1Id = compositionMeshId(compositionIdentityBaselineSettings, 2u);
    Check(baselineRootNodeId != 0u && baselineSingleNodeId != 0u && baselineMultiNodeId != 0u &&
              baselineSingleMeshId != 0u && baselineMultiMesh0Id != 0u && baselineMultiMesh1Id != 0u &&
              baselineSingleNodeId != baselineSingleMeshId,
          "P4 keep authored node identity distinct from the direct single-mesh ComponentDirectory identity");
    assets::AssetMetadata compositionIdentityBaselineMetadata;
    metadataFromPlan(p1ParentAsset, compositionIdentityBaselinePlan, compositionIdentityBaselineMetadata);
    tools::ModelImportPlanningContext compositionIdentityContext = p1Context;
    compositionIdentityContext.persistedMetadata = &compositionIdentityBaselineMetadata;
    compositionIdentityContext.hasPersistedMetadataDigest =
        MetadataDigest(compositionIdentityBaselineMetadata, compositionIdentityContext.persistedMetadataDigest);

    tools::ModelImportManifest compositionDisabledSource;
    buildCompositionIdentityManifest(compositionDisabledSource, "p4-composition-disabled-reimport", true, true, 0u);
    tools::ModelImportSettings compositionDisabledCandidate = compositionIdentityBaselineSettings;
    compositionDisabledCandidate.includeComposition = false;
    tools::ModelImportPlan compositionDisabledPlan;
    tools::ModelImportSettings compositionDisabledSettings;
    const tools::ModelImportPlanResult compositionDisabledResult =
        tools::PlanModelImport(compositionDisabledSource, compositionDisabledCandidate, compositionIdentityContext, compositionDisabledPlan);
    Check(compositionDisabledResult == tools::ModelImportPlanResult::Success,
          "P4 disabled reimport accepts a source reorder/rename while retaining dormant composition identity");
    const bool decodedCompositionDisabled =
        compositionDisabledResult == tools::ModelImportPlanResult::Success &&
        tools::DecodeModelImportSettings(compositionDisabledPlan.GetImportDescription().settingsVersion,
                                         {compositionDisabledPlan.GetImportDescription().settings.TypedData(),
                                          compositionDisabledPlan.GetImportDescription().settings.Size()},
                                         compositionDisabledSettings) == tools::ModelImportSettingsResult::Success;
    Check(decodedCompositionDisabled && !compositionDisabledSettings.includeComposition &&
              compositionDisabledSettings.compositionNodes.Size() == 3u && compositionDisabledSettings.compositionMeshes.Size() == 3u,
          "P4 disabled reimport keeps the dormant wire4 node and mesh correspondence tables");
    Check(decodedCompositionDisabled && compositionNodeId(compositionDisabledSettings, 0u) == baselineRootNodeId &&
              compositionNodeId(compositionDisabledSettings, 1u) == baselineMultiNodeId &&
              compositionNodeId(compositionDisabledSettings, 2u) == baselineSingleNodeId,
          "P4 disabled reimport refreshes authored-node locators without rekeying stable node identity");
    Check(decodedCompositionDisabled && compositionMeshId(compositionDisabledSettings, 0u) == baselineMultiMesh0Id &&
              compositionMeshId(compositionDisabledSettings, 1u) == baselineMultiMesh1Id &&
              compositionMeshId(compositionDisabledSettings, 2u) == baselineSingleMeshId,
          "P4 disabled reimport refreshes mesh-instance locators without rekeying ComponentDirectory identity");
    assets::AssetMetadata compositionDisabledMetadata;
    metadataFromPlan(p1ParentAsset, compositionDisabledPlan, compositionDisabledMetadata);
    tools::ModelImportPlanningContext compositionReenableContext = p1Context;
    compositionReenableContext.persistedMetadata = &compositionDisabledMetadata;
    compositionReenableContext.hasPersistedMetadataDigest =
        MetadataDigest(compositionDisabledMetadata, compositionReenableContext.persistedMetadataDigest);
    tools::ModelImportSettings compositionReenableCandidate = compositionDisabledSettings;
    compositionReenableCandidate.includeComposition = true;
    tools::ModelImportPlan compositionReenablePlan;
    tools::ModelImportSettings compositionReenableSettings;
    Check(tools::PlanModelImport(compositionDisabledSource, compositionReenableCandidate, compositionReenableContext,
                                 compositionReenablePlan) == tools::ModelImportPlanResult::Success &&
              tools::DecodeModelImportSettings(compositionReenablePlan.GetImportDescription().settingsVersion,
                                               {compositionReenablePlan.GetImportDescription().settings.TypedData(),
                                                compositionReenablePlan.GetImportDescription().settings.Size()},
                                               compositionReenableSettings) == tools::ModelImportSettingsResult::Success &&
              compositionReenableSettings.includeComposition && compositionReenableSettings.compositionNodes.Size() == 3u &&
              compositionReenableSettings.compositionMeshes.Size() == 3u &&
              compositionNodeId(compositionReenableSettings, 0u) == baselineRootNodeId &&
              compositionNodeId(compositionReenableSettings, 1u) == baselineMultiNodeId &&
              compositionNodeId(compositionReenableSettings, 2u) == baselineSingleNodeId &&
              compositionMeshId(compositionReenableSettings, 0u) == baselineMultiMesh0Id &&
              compositionMeshId(compositionReenableSettings, 1u) == baselineMultiMesh1Id &&
              compositionMeshId(compositionReenableSettings, 2u) == baselineSingleMeshId,
          "P4 re-enable restores composition output without rekeying preserved authored node or mesh component identity");

    tools::ModelImportManifest compositionMultiplicityExpandedSource;
    buildCompositionIdentityManifest(compositionMultiplicityExpandedSource, "p4-composition-single-to-many", false, false, 0u);
    {
        tools::ModelImportNode& singleNode = compositionMultiplicityExpandedSource.nodes[1u];
        tools::ModelImportGeometryInstance& added = compositionMultiplicityExpandedSource.geometryInstances.EmplaceBack();
        added.nodeIndex = 1u;
        added.geometryDefinitionIndex = 1u;
        added.occurrenceOrdinal = 1u;
        singleNode.geometryInstanceIndices.PushBack(compositionMultiplicityExpandedSource.geometryInstances.Size() - 1u);
    }
    tools::ModelImportPlan compositionMultiplicityExpandedPlan;
    tools::ModelImportSettings compositionMultiplicityExpandedSettings;
    Check(tools::PlanModelImport(compositionMultiplicityExpandedSource, compositionIdentityBaselineSettings, compositionIdentityContext,
                                 compositionMultiplicityExpandedPlan) == tools::ModelImportPlanResult::Success &&
              tools::DecodeModelImportSettings(compositionMultiplicityExpandedPlan.GetImportDescription().settingsVersion,
                                               {compositionMultiplicityExpandedPlan.GetImportDescription().settings.TypedData(),
                                                compositionMultiplicityExpandedPlan.GetImportDescription().settings.Size()},
                                               compositionMultiplicityExpandedSettings) == tools::ModelImportSettingsResult::Success &&
              compositionMultiplicityExpandedSettings.compositionMeshes.Size() == 4u &&
              compositionNodeId(compositionMultiplicityExpandedSettings, 1u) == baselineSingleNodeId &&
              compositionMeshId(compositionMultiplicityExpandedSettings, 0u) == baselineSingleMeshId,
          "P4 preserve the direct mesh component ID when a stable authored node grows from one mesh to many");
    assets::AssetMetadata compositionMultiplicityExpandedMetadata;
    metadataFromPlan(p1ParentAsset, compositionMultiplicityExpandedPlan, compositionMultiplicityExpandedMetadata);
    tools::ModelImportPlanningContext compositionMultiplicityContractContext = p1Context;
    compositionMultiplicityContractContext.persistedMetadata = &compositionMultiplicityExpandedMetadata;
    compositionMultiplicityContractContext.hasPersistedMetadataDigest =
        MetadataDigest(compositionMultiplicityExpandedMetadata, compositionMultiplicityContractContext.persistedMetadataDigest);
    tools::ModelImportManifest compositionMultiplicityContractedSource;
    buildCompositionIdentityManifest(compositionMultiplicityContractedSource, "p4-composition-many-to-single", false, false, 0u);
    tools::ModelImportPlan compositionMultiplicityContractedPlan;
    tools::ModelImportSettings compositionMultiplicityContractedSettings;
    Check(tools::PlanModelImport(compositionMultiplicityContractedSource, compositionMultiplicityExpandedSettings,
                                 compositionMultiplicityContractContext, compositionMultiplicityContractedPlan) ==
                  tools::ModelImportPlanResult::Success &&
              tools::DecodeModelImportSettings(compositionMultiplicityContractedPlan.GetImportDescription().settingsVersion,
                                               {compositionMultiplicityContractedPlan.GetImportDescription().settings.TypedData(),
                                                compositionMultiplicityContractedPlan.GetImportDescription().settings.Size()},
                                               compositionMultiplicityContractedSettings) == tools::ModelImportSettingsResult::Success &&
              compositionMultiplicityContractedSettings.compositionMeshes.Size() == 3u &&
              compositionNodeId(compositionMultiplicityContractedSettings, 1u) == baselineSingleNodeId &&
              compositionMeshId(compositionMultiplicityContractedSettings, 0u) == baselineSingleMeshId,
          "P4 preserve the surviving direct mesh component ID when a stable authored node contracts from many meshes to one");

    tools::ModelImportManifest compositionIdentityReorderedSource;
    buildCompositionIdentityManifest(compositionIdentityReorderedSource, "p4-composition-reordered", true, true, 0u);
    tools::ModelImportPlan compositionIdentityReorderedPlan;
    tools::ModelImportSettings compositionIdentityReorderedSettings;
    Check(tools::PlanModelImport(compositionIdentityReorderedSource, compositionIdentityBaselineSettings, compositionIdentityContext,
                                 compositionIdentityReorderedPlan) == tools::ModelImportPlanResult::Success &&
              tools::DecodeModelImportSettings(compositionIdentityReorderedPlan.GetImportDescription().settingsVersion,
                                               {compositionIdentityReorderedPlan.GetImportDescription().settings.TypedData(),
                                                compositionIdentityReorderedPlan.GetImportDescription().settings.Size()},
                                               compositionIdentityReorderedSettings) == tools::ModelImportSettingsResult::Success &&
              compositionIdentityReorderedSettings.compositionNodes.Size() == 3u &&
              compositionIdentityReorderedSettings.compositionMeshes.Size() == 3u &&
              compositionIdentityReorderedSettings.compositionNodes[0].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[0].stableNodeId &&
              compositionIdentityReorderedSettings.compositionNodes[1].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[1].stableNodeId &&
              compositionIdentityReorderedSettings.compositionNodes[2].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[2].stableNodeId &&
              compositionIdentityReorderedSettings.compositionMeshes[0].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[0].stableComponentId &&
              compositionIdentityReorderedSettings.compositionMeshes[1].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[1].stableComponentId &&
              compositionIdentityReorderedSettings.compositionMeshes[2].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[2].stableComponentId,
          "P4 preserve node and multi-mesh component IDs across unambiguous source-node rename and sibling/instance reorder");

    tools::ModelImportManifest compositionIdentityReplacementSource;
    buildCompositionIdentityManifest(compositionIdentityReplacementSource, "p4-composition-one-replacement", false, false, 2u);
    tools::ModelImportPlan compositionIdentityReplacementPlan;
    tools::ModelImportSettings compositionIdentityReplacementSettings;
    Check(tools::PlanModelImport(compositionIdentityReplacementSource, compositionIdentityBaselineSettings, compositionIdentityContext,
                                 compositionIdentityReplacementPlan) == tools::ModelImportPlanResult::Success &&
              tools::DecodeModelImportSettings(compositionIdentityReplacementPlan.GetImportDescription().settingsVersion,
                                               {compositionIdentityReplacementPlan.GetImportDescription().settings.TypedData(),
                                                compositionIdentityReplacementPlan.GetImportDescription().settings.Size()},
                                               compositionIdentityReplacementSettings) == tools::ModelImportSettingsResult::Success &&
              compositionIdentityReplacementSettings.compositionNodes.Size() == 3u &&
              compositionIdentityReplacementSettings.compositionMeshes.Size() == 3u &&
              compositionIdentityReplacementSettings.compositionNodes[0].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[0].stableNodeId &&
              compositionIdentityReplacementSettings.compositionNodes[1].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[1].stableNodeId &&
              compositionIdentityReplacementSettings.compositionNodes[2].stableNodeId ==
                  compositionIdentityBaselineSettings.compositionNodes[2].stableNodeId &&
              compositionIdentityReplacementSettings.compositionMeshes[0].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[0].stableComponentId &&
              compositionIdentityReplacementSettings.compositionMeshes[1].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[1].stableComponentId &&
              compositionIdentityReplacementSettings.compositionMeshes[2].stableComponentId ==
                  compositionIdentityBaselineSettings.compositionMeshes[2].stableComponentId,
          "P4 preserve authored node identity and the one unambiguous multi-mesh component slot across mesh payload replacement");

    tools::ModelImportManifest compositionIdentityAmbiguousReplacementSource;
    buildCompositionIdentityManifest(compositionIdentityAmbiguousReplacementSource, "p4-composition-two-replacements", false, false, 3u);
    tools::ModelImportPlan compositionIdentityAmbiguousReplacementPlan;
    tools::ModelImportSettings compositionIdentityAmbiguousReplacementSettings;
    const bool ambiguousReplacementPlanned =
        tools::PlanModelImport(compositionIdentityAmbiguousReplacementSource, compositionIdentityBaselineSettings, compositionIdentityContext,
                               compositionIdentityAmbiguousReplacementPlan) == tools::ModelImportPlanResult::Success;
    const bool ambiguousReplacementDecoded =
        ambiguousReplacementPlanned &&
        tools::DecodeModelImportSettings(compositionIdentityAmbiguousReplacementPlan.GetImportDescription().settingsVersion,
                                         {compositionIdentityAmbiguousReplacementPlan.GetImportDescription().settings.TypedData(),
                                          compositionIdentityAmbiguousReplacementPlan.GetImportDescription().settings.Size()},
                                         compositionIdentityAmbiguousReplacementSettings) == tools::ModelImportSettingsResult::Success;
    vanguard::u32 preservedAmbiguousMeshIds = 0u;
    if (ambiguousReplacementDecoded)
        for (const tools::ModelImportCompositionMeshCorrespondence& current : compositionIdentityAmbiguousReplacementSettings.compositionMeshes)
            for (const tools::ModelImportCompositionMeshCorrespondence& previous : compositionIdentityBaselineSettings.compositionMeshes)
                preservedAmbiguousMeshIds += current.stableComponentId == previous.stableComponentId ? 1u : 0u;
    Check(ambiguousReplacementDecoded && compositionIdentityAmbiguousReplacementSettings.compositionMeshes.Size() == 3u &&
              preservedAmbiguousMeshIds == 1u,
          "P4 allocate fresh component IDs instead of guessing when multiple mesh-slot replacements on one node are ambiguous");

    tools::ModelImportNode rigidNode;
    rigidNode.localTransform.values[3] = 2.0f;
    rigidNode.localTransform.values[7] = -3.0f;
    rigidNode.localTransform.values[11] = 4.0f;
    tools::ModelImportRigidTransform rigidTransform;
    tools::ModelGeometryExtractionSettings rigidSettings;
    Check(tools::ConvertModelImportRigidLocalTransform(rigidNode, rigidSettings, rigidTransform) &&
              std::fabs(rigidTransform.translation[0] - 2.0f) < 0.0001f &&
              std::fabs(rigidTransform.translation[1] + 3.0f) < 0.0001f &&
              std::fabs(rigidTransform.translation[2] - 4.0f) < 0.0001f,
          "P4 convert a representable rigid source-node local transform without losing translation");
    rigidNode.flags = tools::ModelImportNode_NonUniformScale;
    Check(!tools::ConvertModelImportRigidLocalTransform(rigidNode, rigidSettings, rigidTransform),
          "P4 reject source non-uniform scale instead of silently dropping unsupported affine semantics");
    rigidNode.flags = tools::ModelImportNode_ShearTransform;
    Check(!tools::ConvertModelImportRigidLocalTransform(rigidNode, rigidSettings, rigidTransform),
          "P4 reject source shear instead of silently degrading authored placement");
    rigidNode.flags = 0u;
    rigidNode.localTransform.values[0] = 2.0f;
    rigidNode.localTransform.values[5] = 2.0f;
    rigidNode.localTransform.values[10] = 2.0f;
    Check(!tools::ConvertModelImportRigidLocalTransform(rigidNode, rigidSettings, rigidTransform),
          "P4 reject residual authored uniform scale even when source flags fail to classify it");

    const bool diagnosticsWereInitialized = vanguard::diagnostics::IsInitialized();
    const bool ioWasInitialized = vanguard::io::IsInitialized();
    const bool filesystemWasInitialized = filesystem::IsInitialized();
    const bool initializedDiagnosticsForP1 = diagnosticsWereInitialized || vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous);
    const bool initializedIoForP1 = ioWasInitialized || vanguard::io::Initialize();
    const filesystem::Config p1FilesystemConfig{p1PlanningRoot, p1PlanningRoot, p1PlanningRoot};
    const bool initializedFilesystemForP1 = filesystemWasInitialized || filesystem::Initialize(p1FilesystemConfig);
    Check(initializedDiagnosticsForP1 && initializedIoForP1 && initializedFilesystemForP1,
          "initialize filesystem services for exact SourceDatabase reference-blocker fixture");
    if (initializedFilesystemForP1)
    {
        const filesystem::AbsolutePath modelCatalogPath = p1PlanningRoot.AddFilePath("catalog_model.gltf");
        const filesystem::AbsolutePath modelMetadataPath = p1PlanningRoot.AddFilePath("catalog_model.gltf.vmeta");
        const filesystem::AbsolutePath referencerCatalogPath = p1PlanningRoot.AddFilePath("catalog_referencer.asset");
        const filesystem::AbsolutePath referencerMetadataPath = p1PlanningRoot.AddFilePath("catalog_referencer.asset.vmeta");
        const filesystem::AbsolutePath corruptCatalogPath = p1PlanningRoot.AddFilePath("catalog_corrupt.asset");
        const filesystem::AbsolutePath corruptMetadataPath = p1PlanningRoot.AddFilePath("catalog_corrupt.asset.vmeta");
        const filesystem::AbsolutePath missingMetadataCatalogPath = p1PlanningRoot.AddFilePath("catalog_missing_metadata.asset");
        containers::String modelMetadataText;
        Check(assets::EncodeMetadata(removalBaselineMetadata, modelMetadataText) == assets::MetadataResult::Success &&
                  WriteTestFile(modelCatalogPath.AsChar(), "model") && WriteTestFile(modelMetadataPath.AsChar(), modelMetadataText.AsChar()),
              "write model catalog fixture and persisted metadata for reference planning");

        const assets::AssetId referencerId = MakeAssetId(0x60u);
        assets::AssetMetadata referencerMetadata;
        referencerMetadata.id = referencerId;
        referencerMetadata.importer = 0x7265666572656e63ull;
        referencerMetadata.importerVersion = 1u;
        referencerMetadata.settingsVersion = 1u;
        referencerMetadata.settings.PushBack(0u);
        assets::AssetOutput& referencerOutput = referencerMetadata.outputs.EmplaceBack();
        referencerOutput.key = "main";
        referencerOutput.type = p1CompositionType;
        containers::String referencerMetadataText;
        Check(assets::EncodeMetadata(referencerMetadata, referencerMetadataText) == assets::MetadataResult::Success &&
                  WriteTestFile(referencerCatalogPath.AsChar(), "reference") &&
                  WriteTestFile(referencerMetadataPath.AsChar(), referencerMetadataText.AsChar()) &&
                  WriteTestFile(corruptCatalogPath.AsChar(), "corrupt-reference-source") &&
                  WriteTestFile(corruptMetadataPath.AsChar(), "not-valid-vmeta") &&
                  WriteTestFile(missingMetadataCatalogPath.AsChar(), "missing-metadata-reference-source"),
              "write external authored referencer fixture");

        assets::SourceDatabase referenceDatabase;
        const assets::SourceRoot catalogRoot{p1PlanningRoot, false};
        const assets::SourceFile catalogSources[]{{modelCatalogPath, 0u}, {referencerCatalogPath, 0u}, {corruptCatalogPath, 0u},
                                                 {missingMetadataCatalogPath, 0u}};
        Check(referenceDatabase.Rebuild(filesystem::GetManager(), {&catalogRoot, 1u}, {catalogSources, 4u}) ==
                  assets::SourceDatabaseResult::Success,
              "build exact project source database for P1 external-reference classification");
        const filesystem::AbsolutePath identityConflictRootA = vanguard::tests::ScratchDirectory("mesh_tools_p1_identity_conflict_a");
        const filesystem::AbsolutePath identityConflictRootB = vanguard::tests::ScratchDirectory("mesh_tools_p1_identity_conflict_b");
        const filesystem::AbsolutePath identityConflictRootC = vanguard::tests::ScratchDirectory("mesh_tools_p1_identity_conflict_c");
        const filesystem::AbsolutePath identityConflictPathA = identityConflictRootA.AddFilePath("duplicate.gltf");
        const filesystem::AbsolutePath identityConflictPathB = identityConflictRootB.AddFilePath("duplicate.gltf");
        Check(WriteTestFile(identityConflictPathA.AsChar(), "identity-a") && WriteTestFile(identityConflictPathB.AsChar(), "identity-b"),
              "write duplicate root-relative source identities for first-import conflict planning");
        assets::SourceDatabase identityConflictDatabase;
        const assets::SourceRoot identityConflictRoots[]{{identityConflictRootA, false}, {identityConflictRootB, false},
                                                         {identityConflictRootC, false}};
        const assets::SourceFile identityConflictSources[]{{identityConflictPathA, 0u}, {identityConflictPathB, 1u}};
        Check(identityConflictDatabase.Rebuild(filesystem::GetManager(), {identityConflictRoots, 3u},
                                               {identityConflictSources, 2u}) == assets::SourceDatabaseResult::Success,
              "build catalog with an ambiguous source identity across writable roots");
        tools::ModelImportPlanningContext ambiguousSourceIdentityContext = p1Context;
        ambiguousSourceIdentityContext.database = &identityConflictDatabase;
        ambiguousSourceIdentityContext.parentAsset = MakeAssetId(0x90u);
        ambiguousSourceIdentityContext.sourceRoot = identityConflictRootC;
        ambiguousSourceIdentityContext.sourceOrigin = filesystem::paths::ParentAbsolutePath(identityConflictRootC).AddFilePath("picked_duplicate.gltf");
        ambiguousSourceIdentityContext.sourceDestination = identityConflictRootC.AddFilePath("duplicate.gltf");
        ambiguousSourceIdentityContext.sourceIdentity = resources::ResourcePath::FromString("duplicate.gltf");
        tools::ModelImportPlan ambiguousSourceIdentityPlan;
        Check(tools::PlanModelImport(removalSourceB, firstImportSettings, ambiguousSourceIdentityContext, ambiguousSourceIdentityPlan) ==
                  tools::ModelImportPlanResult::IdentityConflict,
              "reject first-import planning when the canonical source identity is already ambiguous in the live catalog");
        const assets::SourceRecord* const managedDestinationSource = referenceDatabase.LookupResource(referencerCatalogPath);
        const assets::SourceRecord* const unmanagedDestinationSource = referenceDatabase.LookupResource(missingMetadataCatalogPath);
        tools::ModelImportPlanningContext managedDestinationContext = p1Context;
        managedDestinationContext.database = &referenceDatabase;
        managedDestinationContext.parentAsset = MakeAssetId(0x91u);
        managedDestinationContext.sourceOrigin = referencerCatalogPath;
        managedDestinationContext.sourceDestination = referencerCatalogPath;
        if (managedDestinationSource != nullptr)
            managedDestinationContext.sourceIdentity = managedDestinationSource->sourceIdentity;
        tools::ModelImportPlan managedDestinationPlan;
        Check(managedDestinationSource != nullptr &&
                  tools::PlanModelImport(removalSourceB, firstImportSettings, managedDestinationContext, managedDestinationPlan) ==
                      tools::ModelImportPlanResult::IdentityConflict,
              "reject first-import planning onto a source destination already owned by live managed metadata");

        tools::ModelImportPlanningContext unmanagedDestinationContext = p1Context;
        unmanagedDestinationContext.database = &referenceDatabase;
        unmanagedDestinationContext.parentAsset = MakeAssetId(0x92u);
        unmanagedDestinationContext.sourceOrigin = missingMetadataCatalogPath;
        unmanagedDestinationContext.sourceDestination = missingMetadataCatalogPath;
        if (unmanagedDestinationSource != nullptr)
            unmanagedDestinationContext.sourceIdentity = unmanagedDestinationSource->sourceIdentity;
        tools::ModelImportPlan unmanagedDestinationPlan;
        Check(unmanagedDestinationSource != nullptr && (unmanagedDestinationSource->issues & assets::MetadataMissing) != 0u &&
                  tools::PlanModelImport(removalSourceB, firstImportSettings, unmanagedDestinationContext, unmanagedDestinationPlan) ==
                      tools::ModelImportPlanResult::Success,
              "allow first-import planning for an existing unmanaged project source at the exact canonical destination");
        resources::ResourceReference removedTarget;
        for (const tools::ModelImportReconciliationRecord& row : removalPlan.GetReconciliation())
            if (row.state == tools::ModelImportReconcileState::Removed)
                removedTarget = assets::GetAssetOutputReference(p1ParentAsset, row.previousOutput);
        const assets::AssetReference externalReference{referencerId, removedTarget, resources::DependencyKind::Soft,
                                                       assets::AssetReferenceLocation::Inner, true};
        Check(removedTarget.IsValid() && removedTarget.IsTyped() &&
                  referenceDatabase.SetReferences({&externalReference, 1u}) == assets::SourceDatabaseResult::Success,
              "index an exact external authored reference to the soon-to-be removed model output");

        tools::ModelImportPlanningContext referencedRemovalContext = removalContext;
        referencedRemovalContext.database = &referenceDatabase;
        referencedRemovalContext.sourceRoot = p1PlanningRoot;
        referencedRemovalContext.sourceOrigin = modelCatalogPath;
        referencedRemovalContext.sourceDestination = modelCatalogPath;
        if (const assets::SourceRecord* const modelSource = referenceDatabase.LookupAsset(p1ParentAsset))
            referencedRemovalContext.sourceIdentity = modelSource->sourceIdentity;
        tools::ModelImportPlan referencedRemovalPlan;
        Check(tools::PlanModelImport(removalSourceB, removalBaselineSettings, referencedRemovalContext, referencedRemovalPlan) ==
                  tools::ModelImportPlanResult::Success,
              "produce previewable removal plan in the presence of exact external authored references");
        bool sawExactExternalReference = false;
        bool sawCorruptReferenceCoverageBlocker = false;
        bool sawMissingMetadataReferenceCoverageBlocker = false;
        for (const tools::ModelImportPlanBlocker& blocker : referencedRemovalPlan.GetBlockers())
        {
            sawExactExternalReference = sawExactExternalReference ||
                                        (blocker.kind == tools::ModelImportPlanBlockerKind::ExternalReference &&
                                         blocker.target == removedTarget && blocker.referencer == referencerId &&
                                         blocker.referenceKind == resources::DependencyKind::Soft &&
                                         blocker.referenceLocation == assets::AssetReferenceLocation::Inner && blocker.referenceInPlace &&
                                         blocker.referencerPath == referencerCatalogPath);
            sawCorruptReferenceCoverageBlocker = sawCorruptReferenceCoverageBlocker ||
                                                 (blocker.kind == tools::ModelImportPlanBlockerKind::ReferenceStateUnavailable &&
                                                  blocker.referencerPath == corruptCatalogPath &&
                                                  blocker.referencerSourceIdentity.IsValid());
            sawMissingMetadataReferenceCoverageBlocker = sawMissingMetadataReferenceCoverageBlocker ||
                                                         (blocker.kind == tools::ModelImportPlanBlockerKind::ReferenceStateUnavailable &&
                                                          blocker.referencerPath == missingMetadataCatalogPath &&
                                                          blocker.referencerSourceIdentity.IsValid());
        }
        Check(sawExactExternalReference && referencedRemovalPlan.GetWitnesses().sourceDatabaseGeneration == referenceDatabase.GetGeneration(),
              "copy exact external reference details and source-database generation evidence into immutable plan blockers/witnesses");
        Check(sawCorruptReferenceCoverageBlocker,
              "block destructive reconciliation when a potentially managed source has unreadable/invalid reference-scan authority");
        Check(sawMissingMetadataReferenceCoverageBlocker,
              "block destructive reconciliation when a source may have lost managed metadata and reference-scan authority");
        const vanguard::u32 ownedBlockerCount = referencedRemovalPlan.GetBlockers().Size();
        Check(referenceDatabase.SetReferences({}) == assets::SourceDatabaseResult::Success &&
                  referencedRemovalPlan.GetBlockers().Size() == ownedBlockerCount && sawExactExternalReference,
              "keep copied reference blockers owned by the immutable plan after the source database reference index changes");

        constexpr assets::SourceImporterId P1ImageImporterId = 0x7031696d61676531ull;
        const resources::ResourceTypeId p1ImageSourceType = serialization::MakeFourCC('P', '1', 'I', 'M');
        assets::SourceImporterDescriptor p1ImageImporter;
        p1ImageImporter.id = P1ImageImporterId;
        p1ImageImporter.name = "p1.image.importer";
        p1ImageImporter.version = 1u;
        p1ImageImporter.sourceType = p1ImageSourceType;
        p1ImageImporter.primaryOutputType = p1TextureType;
        p1ImageImporter.recognizeSource = [](const containers::StringView path, void*) noexcept
        {
            const containers::StringView extension = filesystem::paths::GetExtension(path);
            return extension.Length() == 3u && extension.StartsWithIgnoreCase("png");
        };

        const filesystem::AbsolutePath managedImagePath = p1PlanningRoot.AddFilePath("managed.png");
        const filesystem::AbsolutePath managedImageMetadataPath = p1PlanningRoot.AddFilePath("managed.png.vmeta");
        const filesystem::AbsolutePath adoptImagePath = p1PlanningRoot.AddFilePath("adopt.png");
        const assets::AssetId managedImageId = MakeAssetId(0x80u);
        assets::AssetMetadata managedImageMetadata;
        managedImageMetadata.id = managedImageId;
        managedImageMetadata.importer = P1ImageImporterId;
        managedImageMetadata.importerVersion = 1u;
        managedImageMetadata.settingsVersion = 1u;
        assets::AssetOutput& managedImageOutput = managedImageMetadata.outputs.EmplaceBack();
        managedImageOutput.key = "main";
        managedImageOutput.type = p1TextureType;
        assets::AssetOutput& managedMaterialOutput = managedImageMetadata.outputs.EmplaceBack();
        managedMaterialOutput.key = "material";
        managedMaterialOutput.type = p1MaterialType;
        containers::String managedImageMetadataText;
        constexpr char ManagedImageBytes[] = "managed-image-source";
        constexpr char AdoptImageBytes[] = "adopt-image-source";
        Check(assets::EncodeMetadata(managedImageMetadata, managedImageMetadataText) == assets::MetadataResult::Success &&
                  WriteTestFile(managedImagePath.AsChar(), ManagedImageBytes) &&
                  WriteTestFile(managedImageMetadataPath.AsChar(), managedImageMetadataText.AsChar()) &&
                  WriteTestFile(adoptImagePath.AsChar(), AdoptImageBytes),
              "write managed and unmanaged project-image fixtures for P1 dependency planning");

        assets::SourceDatabase dependencyDatabase;
        const assets::SourceFile dependencySources[]{{managedImagePath, 0u}, {adoptImagePath, 0u}};
        Check(dependencyDatabase.Rebuild(filesystem::GetManager(), {&catalogRoot, 1u}, {dependencySources, 2u}) ==
                      assets::SourceDatabaseResult::Success &&
                  dependencyDatabase.ClassifySources({&p1ImageImporter, 1u}),
              "classify managed and unmanaged project-image sources for model dependency decisions");
        const assets::SourceRecord* const managedImageSource = dependencyDatabase.LookupAsset(managedImageId);
        const assets::SourceRecord* const adoptImageSource = dependencyDatabase.LookupResource(adoptImagePath);
        Check(managedImageSource != nullptr && managedImageSource->importState == assets::SourceImportState::MetadataSelected &&
                  adoptImageSource != nullptr && adoptImageSource->importState == assets::SourceImportState::Candidate &&
                  (adoptImageSource->issues & assets::MetadataMissing) != 0u,
              "establish exact managed-resource and adoption-candidate catalog states");

        tools::ModelImportManifest dependencyManifest;
        dependencyManifest.sourceFingerprint = vanguard::crypto::Sha256("p1-dependency-model", 19u);
        tools::ModelImportPrimitive& dependencyPrimitive = dependencyManifest.primitives.EmplaceBack();
        dependencyPrimitive.sourcePartitionOrdinal = 0u;
        dependencyPrimitive.sourceMaterialIndex = 0u;
        dependencyPrimitive.vertexCount = 3u;
        dependencyPrimitive.indices.Resize(3u);
        tools::ImportedVertexStream& dependencyPosition = dependencyPrimitive.vertexStreams.EmplaceBack();
        dependencyPosition.semantic = meshes::VertexSemantic::Position;
        dependencyPosition.format = meshes::VertexFormat::R32G32B32Float;
        dependencyPosition.stride = 12u;
        tools::ModelImportGeometryDefinition& dependencyGeometry = dependencyManifest.geometryDefinitions.EmplaceBack();
        dependencyGeometry.sourceKind = tools::ModelImportSourceKind::Generic;
        dependencyGeometry.sourceGeometryKey = 1u;
        dependencyGeometry.displayName = "DependencyMesh";
        dependencyGeometry.primitiveIndices.PushBack(0u);
        tools::ModelImportMaterial& dependencyMaterial = dependencyManifest.materials.EmplaceBack();
        dependencyMaterial.name = "DependencyMaterial";
        dependencyMaterial.textureReferenceIndices.PushBack(0u);
        tools::ModelImportTextureReference& dependencyTexture = dependencyManifest.textureReferences.EmplaceBack();
        dependencyTexture.semantic = tools::ImportedTextureSemantic::BaseColor;
        dependencyTexture.storage = tools::ImportedTextureStorage::ExternalSource;
        dependencyTexture.authoredPath = "managed.png";
        dependencyTexture.resolvedPath = managedImagePath.AsString();
        dependencyTexture.resolved = true;

        tools::ModelImportDependencyWitness managedImageWitness;
        if (managedImageSource != nullptr)
        {
            managedImageWitness.sourceIdentity = managedImageSource->sourceIdentity;
            managedImageWitness.physicalPath = managedImageSource->path;
            managedImageWitness.asset = managedImageId;
            managedImageWitness.sourceContentFingerprint = vanguard::crypto::Sha256(ManagedImageBytes, sizeof(ManagedImageBytes) - 1u);
            managedImageWitness.metadataFingerprint = vanguard::crypto::Sha256(managedImageMetadataText.AsChar(), managedImageMetadataText.Length());
            managedImageWitness.sourceExists = true;
            managedImageWitness.metadataExists = true;
            managedImageWitness.hasSourceContentFingerprint = true;
            managedImageWitness.hasMetadataFingerprint = true;
        }
        const resources::ResourceReference managedImageReference =
            assets::GetAssetOutputReference(managedImageId, managedImageMetadata.outputs[0]);
        const resources::ResourceReference managedMaterialReference =
            assets::GetAssetOutputReference(managedImageId, managedImageMetadata.outputs[1]);
        tools::ModelImportSettings managedDependencySettings;
        managedDependencySettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
        managedDependencySettings.mesh.defaultMaterial = managedMaterialReference;
        tools::ModelImportTextureResolution& managedResolution = managedDependencySettings.textureResolutions.EmplaceBack();
        managedResolution.authoredPath = "managed.png";
        managedResolution.resolution = tools::ModelImportTextureResolutionKind::ReuseManagedResource;
        managedResolution.managedResource = managedImageReference;
        Check(managedMaterialReference.IsValid() && managedMaterialReference.IsTyped() &&
                  tools::ValidateModelImportSettings(managedDependencySettings) == tools::ModelImportSettingsResult::Success,
              "accept exact typed managed default-material and texture decisions in candidate model settings");
        tools::ModelImportPlanningContext managedDependencyContext = p1Context;
        managedDependencyContext.database = &dependencyDatabase;
        managedDependencyContext.sourceDestination = p1PlanningRoot.AddFilePath("dependency_model.gltf");
        managedDependencyContext.sourceIdentity = resources::ResourcePath::FromString("dependency_model.gltf");
        managedDependencyContext.dependencyWitnesses = {&managedImageWitness, 1u};
        tools::ModelImportPlan managedDependencyPlan;
        const tools::ModelImportPlanResult managedDependencyResult =
            tools::PlanModelImport(dependencyManifest, managedDependencySettings, managedDependencyContext, managedDependencyPlan);
        Check(managedDependencyResult ==
                      tools::ModelImportPlanResult::Success &&
                  !managedDependencyPlan.HasBlockingConflicts() && managedDependencyPlan.GetTextureResolutions().Size() == 1u &&
                  managedDependencyPlan.GetTextureResolutions()[0].resolution == tools::ModelImportTextureResolutionKind::ReuseManagedResource &&
                  managedDependencyPlan.GetDependencyWitnesses().Size() == 2u,
              "plan managed model-setting and texture references only with exact owned source and metadata witnesses");
        bool sawManagedTextureWitness = false;
        bool sawManagedMaterialWitness = false;
        for (const tools::ModelImportDependencyWitness& witness : managedDependencyPlan.GetDependencyWitnesses())
        {
            const bool exactEvidence = witness.sourceContentFingerprint == managedImageWitness.sourceContentFingerprint &&
                                       witness.metadataFingerprint == managedImageWitness.metadataFingerprint;
            sawManagedTextureWitness = sawManagedTextureWitness || (exactEvidence && witness.selectedResource == managedImageReference);
            sawManagedMaterialWitness = sawManagedMaterialWitness || (exactEvidence && witness.selectedResource == managedMaterialReference);
        }
        Check(sawManagedTextureWitness && sawManagedMaterialWitness,
              "preserve distinct selected-resource witnesses for multiple managed outputs from the same source");

        tools::ModelImportSettings wrongTypeManagedSettings;
        wrongTypeManagedSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
        wrongTypeManagedSettings.mesh.defaultMaterial = managedImageReference;
        tools::ModelImportPlan wrongTypeManagedPlan;
        Check(tools::PlanModelImport(dependencyManifest, wrongTypeManagedSettings, managedDependencyContext, wrongTypeManagedPlan) ==
                  tools::ModelImportPlanResult::SettingsInvalid,
              "reject a valid managed texture output when supplied as the model default-material reference");

        tools::ModelImportDependencyWitness emptyDigestWitness = managedImageWitness;
        emptyDigestWitness.sourceContentFingerprint = {};
        tools::ModelImportPlanningContext emptyDigestDependencyContext = managedDependencyContext;
        emptyDigestDependencyContext.dependencyWitnesses = {&emptyDigestWitness, 1u};
        tools::ModelImportPlan emptyDigestDependencyPlan;
        Check(tools::PlanModelImport(dependencyManifest, managedDependencySettings, emptyDigestDependencyContext,
                                     emptyDigestDependencyPlan) == tools::ModelImportPlanResult::SettingsInvalid,
              "reject a claimed managed dependency fingerprint witness whose digest is empty");

        tools::ModelImportDependencyWitness mismatchedMetadataWitness = managedImageWitness;
        mismatchedMetadataWitness.metadataFingerprint = vanguard::crypto::Sha256("stale-managed-metadata", 22u);
        tools::ModelImportPlanningContext mismatchedMetadataDependencyContext = managedDependencyContext;
        mismatchedMetadataDependencyContext.dependencyWitnesses = {&mismatchedMetadataWitness, 1u};
        tools::ModelImportPlan mismatchedMetadataDependencyPlan;
        Check(tools::PlanModelImport(dependencyManifest, managedDependencySettings, mismatchedMetadataDependencyContext,
                                     mismatchedMetadataDependencyPlan) == tools::ModelImportPlanResult::SettingsInvalid,
              "reject a nonempty managed dependency metadata digest that does not match the live parsed metadata snapshot");

        tools::ModelImportPlanningContext staleManagedDependencyContext = managedDependencyContext;
        staleManagedDependencyContext.dependencyWitnesses = {};
        tools::ModelImportPlan staleManagedDependencyPlan;
        Check(tools::PlanModelImport(dependencyManifest, managedDependencySettings, staleManagedDependencyContext, staleManagedDependencyPlan) ==
                  tools::ModelImportPlanResult::SettingsInvalid,
              "reject managed texture selection when its exact dependency witness is absent");

        tools::ModelImportDependencyWitness adoptImageWitness;
        if (adoptImageSource != nullptr)
        {
            adoptImageWitness.sourceIdentity = adoptImageSource->sourceIdentity;
            adoptImageWitness.physicalPath = adoptImageSource->path;
            adoptImageWitness.sourceContentFingerprint = vanguard::crypto::Sha256(AdoptImageBytes, sizeof(AdoptImageBytes) - 1u);
            adoptImageWitness.sourceExists = true;
            adoptImageWitness.metadataExists = false;
            adoptImageWitness.hasSourceContentFingerprint = true;
            adoptImageWitness.hasMetadataFingerprint = false;
        }
        dependencyTexture.authoredPath = "adopt.png";
        dependencyTexture.resolvedPath = adoptImagePath.AsString();
        tools::ModelImportSettings adoptDependencySettings;
        adoptDependencySettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
        tools::ModelImportTextureResolution& adoptResolution = adoptDependencySettings.textureResolutions.EmplaceBack();
        adoptResolution.authoredPath = "adopt.png";
        adoptResolution.resolution = tools::ModelImportTextureResolutionKind::AdoptProjectSource;
        if (adoptImageSource != nullptr)
            adoptResolution.projectSourceIdentity = adoptImageSource->sourceIdentity;
        tools::ModelImportPlanningContext adoptDependencyContext = managedDependencyContext;
        adoptDependencyContext.dependencyWitnesses = {&adoptImageWitness, 1u};
        tools::ModelImportPlan adoptDependencyPlan;
        Check(tools::PlanModelImport(dependencyManifest, adoptDependencySettings, adoptDependencyContext, adoptDependencyPlan) ==
                      tools::ModelImportPlanResult::Success &&
                  !adoptDependencyPlan.HasBlockingConflicts() && adoptDependencyPlan.GetTextureResolutions().Size() == 1u &&
                  adoptDependencyPlan.GetTextureResolutions()[0].resolution == tools::ModelImportTextureResolutionKind::AdoptProjectSource &&
                  adoptDependencyPlan.GetDependencyWitnesses().Size() == 1u &&
                  adoptDependencyPlan.GetDependencyWitnesses()[0].sourceIdentity == adoptImageWitness.sourceIdentity &&
                  !adoptDependencyPlan.GetDependencyWitnesses()[0].metadataExists,
              "plan unmanaged project-image adoption with exact source-content and metadata-absence witness");

        const filesystem::AbsolutePath readOnlyDependencyRoot = vanguard::tests::ScratchDirectory("mesh_tools_p1_readonly_dependency");
        const filesystem::AbsolutePath readOnlyAdoptPath = readOnlyDependencyRoot.AddFilePath("readonly_adopt.png");
        constexpr char ReadOnlyAdoptBytes[] = "readonly-adopt-image-source";
        Check(WriteTestFile(readOnlyAdoptPath.AsChar(), ReadOnlyAdoptBytes),
              "write read-only-root adoption candidate fixture");
        assets::SourceDatabase readOnlyDependencyDatabase;
        const assets::SourceRoot dependencyRoots[]{{p1PlanningRoot, false}, {readOnlyDependencyRoot, true}};
        const assets::SourceFile readOnlyDependencySources[]{{readOnlyAdoptPath, 1u}};
        Check(readOnlyDependencyDatabase.Rebuild(filesystem::GetManager(), {dependencyRoots, 2u}, {readOnlyDependencySources, 1u}) ==
                      assets::SourceDatabaseResult::Success &&
                  readOnlyDependencyDatabase.ClassifySources({&p1ImageImporter, 1u}),
              "classify an unmanaged dependency candidate under a read-only source root");
        const assets::SourceRecord* const readOnlyAdoptSource = readOnlyDependencyDatabase.LookupResource(readOnlyAdoptPath);
        tools::ModelImportDependencyWitness readOnlyAdoptWitness;
        if (readOnlyAdoptSource != nullptr)
        {
            readOnlyAdoptWitness.sourceIdentity = readOnlyAdoptSource->sourceIdentity;
            readOnlyAdoptWitness.physicalPath = readOnlyAdoptSource->path;
            readOnlyAdoptWitness.sourceContentFingerprint =
                vanguard::crypto::Sha256(ReadOnlyAdoptBytes, sizeof(ReadOnlyAdoptBytes) - 1u);
            readOnlyAdoptWitness.sourceExists = true;
            readOnlyAdoptWitness.metadataExists = false;
            readOnlyAdoptWitness.hasSourceContentFingerprint = true;
        }
        dependencyTexture.authoredPath = "readonly_adopt.png";
        dependencyTexture.resolvedPath = readOnlyAdoptPath.AsString();
        dependencyTexture.resolved = true;
        dependencyTexture.missing = false;
        tools::ModelImportSettings readOnlyAdoptSettings;
        readOnlyAdoptSettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
        tools::ModelImportTextureResolution& readOnlyAdoptResolution = readOnlyAdoptSettings.textureResolutions.EmplaceBack();
        readOnlyAdoptResolution.authoredPath = "readonly_adopt.png";
        readOnlyAdoptResolution.resolution = tools::ModelImportTextureResolutionKind::AdoptProjectSource;
        if (readOnlyAdoptSource != nullptr)
            readOnlyAdoptResolution.projectSourceIdentity = readOnlyAdoptSource->sourceIdentity;
        tools::ModelImportPlanningContext readOnlyAdoptContext = managedDependencyContext;
        readOnlyAdoptContext.database = &readOnlyDependencyDatabase;
        readOnlyAdoptContext.dependencyWitnesses = {&readOnlyAdoptWitness, 1u};
        tools::ModelImportPlan readOnlyAdoptPlan;
        Check(readOnlyAdoptSource != nullptr && readOnlyAdoptSource->importState == assets::SourceImportState::Candidate &&
                  tools::PlanModelImport(dependencyManifest, readOnlyAdoptSettings, readOnlyAdoptContext, readOnlyAdoptPlan) ==
                      tools::ModelImportPlanResult::SettingsInvalid,
              "reject adoption of an unmanaged dependency from a read-only project source root");

        dependencyTexture.authoredPath = "missing.png";
        dependencyTexture.resolvedPath = p1PlanningRoot.AddFilePath("missing.png").AsString();
        dependencyTexture.resolved = false;
        dependencyTexture.missing = true;
        tools::ModelImportSettings missingDependencySettings;
        missingDependencySettings.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::Ignore;
        tools::ModelImportPlanningContext missingDependencyContext = managedDependencyContext;
        missingDependencyContext.dependencyWitnesses = {};
        tools::ModelImportPlan missingDependencyPlan;
        Check(tools::PlanModelImport(dependencyManifest, missingDependencySettings, missingDependencyContext, missingDependencyPlan) ==
                      tools::ModelImportPlanResult::Success &&
                  missingDependencyPlan.HasBlockingConflicts() && missingDependencyPlan.GetTextureResolutions().Size() == 1u &&
                  missingDependencyPlan.GetTextureResolutions()[0].blocking,
              "keep an unresolved missing external texture as a previewable explicit blocking dependency decision");
        bool sawMissingDependencyBlocker = false;
        for (const tools::ModelImportPlanBlocker& blocker : missingDependencyPlan.GetBlockers())
            sawMissingDependencyBlocker = sawMissingDependencyBlocker ||
                                          blocker.kind == tools::ModelImportPlanBlockerKind::MissingDependency;
        Check(sawMissingDependencyBlocker, "surface unresolved external texture provenance as an exact missing-dependency blocker");

        static_cast<void>(std::remove(corruptMetadataPath.AsChar()));
        static_cast<void>(std::remove(corruptCatalogPath.AsChar()));
        static_cast<void>(std::remove(missingMetadataCatalogPath.AsChar()));
        static_cast<void>(std::remove(identityConflictPathB.AsChar()));
        static_cast<void>(std::remove(identityConflictPathA.AsChar()));
        static_cast<void>(std::remove(readOnlyAdoptPath.AsChar()));
        static_cast<void>(std::remove(adoptImagePath.AsChar()));
        static_cast<void>(std::remove(managedImageMetadataPath.AsChar()));
        static_cast<void>(std::remove(managedImagePath.AsChar()));
        static_cast<void>(std::remove(referencerMetadataPath.AsChar()));
        static_cast<void>(std::remove(referencerCatalogPath.AsChar()));
        static_cast<void>(std::remove(modelMetadataPath.AsChar()));
        static_cast<void>(std::remove(modelCatalogPath.AsChar()));
    }
    if (!filesystemWasInitialized && filesystem::IsInitialized())
        filesystem::Shutdown();
    if (!ioWasInitialized && vanguard::io::IsInitialized())
        vanguard::io::Shutdown();
    if (!diagnosticsWereInitialized && vanguard::diagnostics::IsInitialized())
        vanguard::diagnostics::Shutdown();

    constexpr char transformEvidenceGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA","byteLength":42}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
            {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "nodes":[
            {"name":"Root","children":[1,2,3]},
            {"name":"NonUniform","scale":[2,1,1],"mesh":0},
            {"name":"Mirrored","scale":[-1,1,1],"mesh":0},
            {"name":"Sheared","matrix":[1,0,0,0,0.5,1,0,0,0,0,1,0,0,0,0,1],"mesh":0}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportManifest transformEvidenceManifest;
    Check(tools::InspectModelMemory(transformEvidenceGltf, sizeof(transformEvidenceGltf) - 1, "gltf",
                                    "memory/transform_evidence.gltf", modelOptions, modelLimits,
                                    transformEvidenceManifest) == tools::ModelImportResult::Success &&
              transformEvidenceManifest.nodes.Size() == 4 &&
              (transformEvidenceManifest.nodes[1].flags & tools::ModelImportNode_NonUniformScale) != 0 &&
              (transformEvidenceManifest.nodes[2].flags & tools::ModelImportNode_NegativeDeterminant) != 0 &&
              (transformEvidenceManifest.nodes[3].flags & tools::ModelImportNode_ShearTransform) != 0 &&
              std::fabs(transformEvidenceManifest.nodes[1].localTransform.values[0] - 2.0f) < 0.0001f &&
              std::fabs(transformEvidenceManifest.nodes[2].localTransform.values[0] + 1.0f) < 0.0001f &&
              std::fabs(transformEvidenceManifest.nodes[3].localTransform.values[1] - 0.5f) < 0.0001f,
          "preserve full local scale/reflection/shear matrices and diagnose runtime transform representability");

    tools::ModelGeometryExtractionSettings extractionSettings;
    extractionSettings.uniformScale = 2.0f;
    extractionSettings.sourceUpAxis = tools::SourceUpAxis::Y;
    extractionSettings.defaultMaterial = importedMaterial;
    tools::ImportedMesh extractedModelGeometry;
    Check(tools::ExtractModelGeometry(canonicalManifest, 0, extractionSettings, extractedModelGeometry) ==
              tools::ModelImportResult::Success &&
              extractedModelGeometry.submeshes.Size() == 2,
          "extract one reusable geometry definition as cooker-compatible multi-submesh mesh data");
    if (extractedModelGeometry.submeshes.Size() == 2)
    {
        const tools::ImportedVertexStream* positions = nullptr;
        for (const tools::ImportedVertexStream& stream : extractedModelGeometry.submeshes[0].vertexStreams)
        {
            if (stream.semantic == meshes::VertexSemantic::Position)
            {
                positions = &stream;
                break;
            }
        }
        Check(positions != nullptr && positions->floatValues.Size() == 9 &&
                  std::fabs(positions->floatValues[0]) < 0.0001f &&
                  std::fabs(positions->floatValues[1]) < 0.0001f &&
                  std::fabs(positions->floatValues[2]) < 0.0001f,
              "keep placement-node transforms out of extracted reusable geometry");
    }
    tools::ModelImportMatrix convertedPivot;
    Check(canonicalManifest.nodes.Size() == 3 &&
              tools::ConvertModelImportLocalTransform(canonicalManifest.nodes[0].localTransform, extractionSettings, convertedPivot) &&
              std::fabs(convertedPivot.values[7]) < 0.0001f &&
              std::fabs(convertedPivot.values[11] - 4.0f) < 0.0001f,
          "convert local hierarchy transforms with the same basis and uniform scale used by geometry extraction");
    containers::DynamicArray<vanguard::u8> extractedCookedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter extractedCookedWriter(extractedCookedBytes);
    tools::SourceMesh extractedSource = extractedModelGeometry.BuildSourceView();
    Check(tools::CookMesh(extractedSource, extractedCookedWriter) == tools::Result::Success,
          "feed selected canonical geometry through the existing VMESH cooker unchanged");

    constexpr char externalBufferGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"missing.bin","byteLength":42}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
            {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportManifest rejectedExternalBuffer;
    Check(tools::InspectModelMemory(externalBufferGltf, sizeof(externalBufferGltf) - 1, "gltf", "memory/external.gltf",
                                    modelOptions, modelLimits, rejectedExternalBuffer) ==
                  tools::ModelImportResult::UnsupportedExternalBuffer &&
              rejectedExternalBuffer.nodes.Empty() && rejectedExternalBuffer.geometryDefinitions.Empty(),
          "reject glTF requiring an external structural buffer and clear partial inspection output");

    containers::DynamicArray<vanguard::u8> selfContainedGlb(memory::pools::Assets::GetInstance());
    BuildTriangleGlb(selfContainedGlb);
    tools::ModelImportManifest glbManifest;
    Check(tools::InspectModelMemory(selfContainedGlb.TypedData(), selfContainedGlb.Size(), "glb", "memory/triangle.glb",
                                    modelOptions, modelLimits, glbManifest) ==
                  tools::ModelImportResult::Success &&
              glbManifest.geometryDefinitions.Size() == 1 && glbManifest.geometryInstances.Size() == 1 &&
              glbManifest.primitives.Size() == 1,
          "accept self-contained GLB through the same canonical inspection path");

    // KHR_materials_specular allows the strength and color maps independently.
    // A color-only map used to land at Assimp SPECULAR slot 1 with slot 0 absent:
    // ValidateDataStructure then rejected the otherwise valid Blender GLB.
    static constexpr const char* SpecularVariants[] = {
        R"json("specularColorTexture":{"index":0})json",
        R"json("specularTexture":{"index":0})json",
        R"json("specularTexture":{"index":0},"specularColorTexture":{"index":0})json"};
    for (vanguard::u32 variant = 0; variant < 3u; ++variant)
    {
        containers::DynamicArray<vanguard::u8> specularGlb(memory::pools::Assets::GetInstance());
        BuildTriangleGlb(specularGlb, SpecularVariants[variant]);
        tools::ModelImportManifest specularManifest;
        tools::ModelImportReport specularReport;
        const tools::ModelImportResult inspection = tools::InspectModelMemory(
            specularGlb.TypedData(), specularGlb.Size(), "glb", "memory/specular.glb",
            modelOptions, modelLimits, specularManifest, &specularReport);
        if (inspection != tools::ModelImportResult::Success)
            std::fprintf(stderr, "[meshToolsTests] KHR_materials_specular variant %u: %s (%s)\n",
                         variant, tools::ToString(inspection), specularReport.diagnostic.AsChar());
        const vanguard::u32 expectedTextureCount = variant == 2u ? 2u : 1u;
        bool validTextures = inspection == tools::ModelImportResult::Success &&
                             specularManifest.materials.Size() >= 1u &&
                             specularManifest.textureReferences.Size() == expectedTextureCount &&
                             specularManifest.embeddedTextures.Size() == 1u;
        if (validTextures)
        {
            for (vanguard::u32 texture = 0u; texture < expectedTextureCount; ++texture)
            {
                const tools::ModelImportTextureReference& reference = specularManifest.textureReferences[texture];
                const tools::ModelImportTextureReference::SpecularTextureRole expectedRole =
                    variant == 0u || (variant == 2u && texture == 1u)
                        ? tools::ModelImportTextureReference::SpecularTextureRole::ColorRgb
                        : tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha;
                validTextures = validTextures && reference.semantic == tools::ImportedTextureSemantic::Specular &&
                                reference.specularTextureRole == expectedRole &&
                                reference.storage == tools::ImportedTextureStorage::Embedded &&
                                reference.resolved && reference.sourceBindingOrdinal == texture;
            }
        }
        Check(validTextures, "KHR_materials_specular strength-only, color-only and dual maps remain dense, valid and imported");
    }

    containers::DynamicArray<vanguard::u8> materialExtensionGlb(memory::pools::Assets::GetInstance());
    BuildTriangleGlb(materialExtensionGlb,
                     R"json("specularFactor":0.6,"specularColorFactor":[0.2,0.3,0.4],"specularTexture":{"index":0},"specularColorTexture":{"index":0})json",
                     true);
    tools::ModelImportManifest materialExtensionManifest;
    Check(tools::InspectModelMemory(materialExtensionGlb.TypedData(), materialExtensionGlb.Size(), "glb", "memory/material_extensions.glb",
                                    modelOptions, modelLimits, materialExtensionManifest) == tools::ModelImportResult::Success &&
              materialExtensionManifest.materials.Size() >= 1u,
          "inspect KHR specular factor/color, IOR and occlusion strength through the canonical in-memory path");
    if (!materialExtensionManifest.materials.Empty())
    {
        const tools::ModelImportMaterial& material = materialExtensionManifest.materials[0];
        vanguard::u32 factorMaps = 0u;
        vanguard::u32 colorMaps = 0u;
        for (const vanguard::u32 textureIndex : material.textureReferenceIndices)
        {
            if (textureIndex >= materialExtensionManifest.textureReferences.Size())
                continue;
            const tools::ModelImportTextureReference& reference = materialExtensionManifest.textureReferences[textureIndex];
            if (reference.semantic != tools::ImportedTextureSemantic::Specular)
                continue;
            factorMaps += reference.specularTextureRole == tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha ? 1u : 0u;
            colorMaps += reference.specularTextureRole == tools::ModelImportTextureReference::SpecularTextureRole::ColorRgb ? 1u : 0u;
        }
        Check(std::fabs(material.specularColor[0] - 0.2f) < 0.0001f &&
                  std::fabs(material.specularColor[1] - 0.3f) < 0.0001f &&
                  std::fabs(material.specularColor[2] - 0.4f) < 0.0001f &&
                  std::fabs(material.specularFactor - 0.6f) < 0.0001f &&
                  std::fabs(material.indexOfRefraction - 1.8f) < 0.0001f &&
                  std::fabs(material.occlusionStrength - 0.35f) < 0.0001f && factorMaps == 1u && colorMaps == 1u,
              "preserve KHR specular scalar/color inputs, independent texture channels, IOR and occlusion strength losslessly");
    }

    // Optional local real-model proof, run with the original GLB on developer machines.
    // The standard test suite stays fully hermetic when this variable is absent.
    char localGlb[1024]{};
    size_t localGlbLength = 0u;
    const errno_t localGlbEnvironment = getenv_s(&localGlbLength, localGlb, sizeof(localGlb), "VANGUARD_MESH_TOOLS_LOCAL_GLB");
    Check(localGlbEnvironment == 0, "local GLB inspection source path fits the test limit");
    if (localGlbEnvironment == 0 && localGlbLength > 1u)
    {
        containers::DynamicArray<vanguard::u8> sourceBytes(memory::pools::Assets::GetInstance());
        const bool read = ReadTestFile(localGlb, sourceBytes);
        Check(read, "read opted-in real GLB source without mutating it");
        if (read)
        {
            tools::ModelImportManifest localManifest;
            tools::ModelImportReport localReport;
            const tools::ModelImportResult result = tools::InspectModelMemory(
                sourceBytes.TypedData(), sourceBytes.Size(), "glb", localGlb,
                modelOptions, modelLimits, localManifest, &localReport);
            std::printf("[meshToolsTests] local GLB inspection: %s, %u geometry definitions, %u embedded textures, %u texture refs; %s\n",
                        tools::ToString(result), localReport.geometryDefinitionCount,
                        localReport.embeddedTextureCount, localReport.textureReferenceCount,
                        localReport.diagnostic.AsChar());
            Check(result == tools::ModelImportResult::Success && localReport.geometryDefinitionCount != 0u &&
                      localReport.embeddedTextureCount != 0u,
                  "inspect opted-in real GLB via the same in-memory route used by Asset Browser");
            const tools::ModelImportMaterial* lens = nullptr;
            vanguard::u32 factorSpecularMaps = 0u;
            vanguard::u32 colorSpecularMaps = 0u;
            for (const tools::ModelImportMaterial& material : localManifest.materials)
                if (material.name == "lens")
                    lens = &material;
            for (const tools::ModelImportTextureReference& reference : localManifest.textureReferences)
            {
                if (reference.semantic != tools::ImportedTextureSemantic::Specular)
                    continue;
                factorSpecularMaps += reference.specularTextureRole == tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha ? 1u : 0u;
                colorSpecularMaps += reference.specularTextureRole == tools::ModelImportTextureReference::SpecularTextureRole::ColorRgb ? 1u : 0u;
            }
            const containers::String localGlbPath(localGlb);
            if (containers::StringView(localGlbPath.AsChar(), localGlbPath.Length()).EndsWithIgnoreCase("lieutenantHead.glb"))
            {
                Check(lens != nullptr && lens->alphaMode == tools::ModelImportAlphaMode::Blend &&
                          std::fabs(lens->specularColor[0] - 0.2f) < 0.0001f &&
                          std::fabs(lens->specularColor[1] - 0.2f) < 0.0001f &&
                          std::fabs(lens->specularColor[2] - 0.2f) < 0.0001f &&
                          std::fabs(lens->specularFactor - 1.0f) < 0.0001f &&
                          std::fabs(lens->indexOfRefraction - 1000.0f) < 0.0001f &&
                          factorSpecularMaps == 0u && colorSpecularMaps == 4u,
                      "preserve lieutenantHead.glb lens and color-only KHR specular bindings through in-memory inspection");
            }
        }
    }

    constexpr char SkinnedReuseGltfTemplate[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/","byteLength":168}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
            {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963},
            {"buffer":0,"byteOffset":44,"byteLength":12,"target":34962},
            {"buffer":0,"byteOffset":56,"byteLength":48,"target":34962},
            {"buffer":0,"byteOffset":104,"byteLength":64}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
            {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":4,"componentType":5126,"count":1,"type":"MAT4"}],
        "meshes":[{"name":"SkinnedShared","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
        "skins":[
            {"inverseBindMatrices":4,"joints":[3]},
            {"inverseBindMatrices":4,"joints":[4]}],
        "nodes":[
            {"name":"Root","children":[1,2,3,4]},
            {"name":"UseA","mesh":0,"skin":0},
            {"name":"UseB","mesh":0,"skin":%u},
            {"name":"JointA"},
            {"name":"%s"}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    const containers::String consistentSkinGltf = containers::String::Printf(SkinnedReuseGltfTemplate, 0u, "JointB");
    tools::ModelImportManifest consistentSkinManifest;
    Check(tools::InspectModelMemory(consistentSkinGltf.AsChar(), consistentSkinGltf.Length(), "gltf", "memory/skinned.gltf",
                                    modelOptions, modelLimits, consistentSkinManifest) ==
                  tools::ModelImportResult::Success &&
              consistentSkinManifest.geometryDefinitions.Size() == 1 &&
              consistentSkinManifest.geometryInstances.Size() == 2 &&
              consistentSkinManifest.primitives.Size() == 1 &&
              consistentSkinManifest.primitives[0].boneBindings.Size() == 1 &&
              consistentSkinManifest.primitives[0].boneBindings[0].weights.Size() == 3,
          "preserve one shared glTF skin binding and raw source bone weights without duplicate overwrite");
    const containers::String conflictingSkinGltf = containers::String::Printf(SkinnedReuseGltfTemplate, 1u, "JointB");
    tools::ModelImportManifest conflictingSkinManifest;
    Check(tools::InspectModelMemory(conflictingSkinGltf.AsChar(), conflictingSkinGltf.Length(), "gltf", "memory/conflicting_skin.gltf",
                                    modelOptions, modelLimits, conflictingSkinManifest) ==
                  tools::ModelImportResult::ConflictingGeometryBinding &&
              conflictingSkinManifest.nodes.Empty() && conflictingSkinManifest.geometryDefinitions.Empty(),
          "reject reused glTF geometry with conflicting per-instance skins instead of overwriting shared bone state");
    const containers::String ambiguousBoneGltf = containers::String::Printf(SkinnedReuseGltfTemplate, 0u, "JointA");
    tools::ModelImportManifest ambiguousBoneManifest;
    Check(tools::InspectModelMemory(ambiguousBoneGltf.AsChar(), ambiguousBoneGltf.Length(), "gltf", "memory/ambiguous_bone.gltf",
                                    modelOptions, modelLimits, ambiguousBoneManifest) ==
                  tools::ModelImportResult::Success &&
              ambiguousBoneManifest.primitives.Size() == 1 &&
              ambiguousBoneManifest.primitives[0].boneBindings.Size() == 1 &&
              ambiguousBoneManifest.primitives[0].boneBindings[0].nodeIndex == 3,
          "preserve exact glTF joint identity even when multiple hierarchy nodes share the same display name");

    constexpr char OptionalInverseBindGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAA=","byteLength":104}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
            {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963},
            {"buffer":0,"byteOffset":44,"byteLength":12,"target":34962},
            {"buffer":0,"byteOffset":56,"byteLength":48,"target":34962}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
            {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3},"indices":1}]}],
        "skins":[{"joints":[2]}],
        "nodes":[
            {"name":"Root","children":[1,2]},
            {"name":"Skinned","mesh":0,"skin":0},
            {"name":"Joint"}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportManifest optionalInverseBindManifest;
    Check(tools::InspectModelMemory(OptionalInverseBindGltf, sizeof(OptionalInverseBindGltf) - 1, "gltf", "memory/optional_bind.gltf",
                                    modelOptions, modelLimits, optionalInverseBindManifest) ==
                  tools::ModelImportResult::Success &&
              optionalInverseBindManifest.primitives.Size() == 1 &&
              optionalInverseBindManifest.primitives[0].boneBindings.Size() == 1 &&
              std::fabs(optionalInverseBindManifest.primitives[0].boneBindings[0].inverseBind.values[0] - 1.0f) < 0.0001f &&
              std::fabs(optionalInverseBindManifest.primitives[0].boneBindings[0].inverseBind.values[5] - 1.0f) < 0.0001f &&
              std::fabs(optionalInverseBindManifest.primitives[0].boneBindings[0].inverseBind.values[10] - 1.0f) < 0.0001f &&
              std::fabs(optionalInverseBindManifest.primitives[0].boneBindings[0].inverseBind.values[15] - 1.0f) < 0.0001f,
          "accept valid glTF skin without inverseBindMatrices and synthesize identity inverse bind");

    constexpr char ExtendedWeightsGltf[] = R"json({
        "asset":{"version":"2.0"},
        "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAQIDAAECAwABAgPNzMw+mpmZPs3MTD7NzEw9zczMPpqZmT7NzEw+zcxMPc3MzD6amZk+zcxMPs3MTD0EAAAABAAAAAQAAADNzEw9AAAAAAAAAAAAAAAAzcxMPQAAAAAAAAAAAAAAAM3MTD0AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAACAPw==","byteLength":484}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},
            {"buffer":0,"byteOffset":36,"byteLength":6,"target":34963},
            {"buffer":0,"byteOffset":44,"byteLength":12,"target":34962},
            {"buffer":0,"byteOffset":56,"byteLength":48,"target":34962},
            {"buffer":0,"byteOffset":104,"byteLength":12,"target":34962},
            {"buffer":0,"byteOffset":116,"byteLength":48,"target":34962},
            {"buffer":0,"byteOffset":164,"byteLength":320}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":2,"componentType":5121,"count":3,"type":"VEC4"},
            {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":4,"componentType":5121,"count":3,"type":"VEC4"},
            {"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"},
            {"bufferView":6,"componentType":5126,"count":5,"type":"MAT4"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":2,"WEIGHTS_0":3,"JOINTS_1":4,"WEIGHTS_1":5},"indices":1}]}],
        "skins":[{"inverseBindMatrices":6,"joints":[2,3,4,5,6]}],
        "nodes":[
            {"name":"Root","children":[1,2,3,4,5,6]},
            {"name":"Skinned","mesh":0,"skin":0},
            {"name":"Joint0"},{"name":"Joint1"},{"name":"Joint2"},{"name":"Joint3"},{"name":"Joint4"}],
        "scenes":[{"nodes":[0]}],"scene":0
    })json";
    tools::ModelImportManifest extendedWeightsManifest;
    Check(tools::InspectModelMemory(ExtendedWeightsGltf, sizeof(ExtendedWeightsGltf) - 1, "gltf", "memory/weights.gltf",
                                    modelOptions, modelLimits, extendedWeightsManifest) ==
                  tools::ModelImportResult::Success &&
              extendedWeightsManifest.primitives.Size() == 1 &&
              extendedWeightsManifest.primitives[0].boneBindings.Size() == 5,
          "preserve source skinning beyond the runtime four-influence profile during canonical inspection");
    vanguard::u32 firstVertexInfluenceCount = 0;
    if (extendedWeightsManifest.primitives.Size() == 1)
    {
        for (const tools::ModelImportBoneBinding& binding : extendedWeightsManifest.primitives[0].boneBindings)
        {
            for (const tools::ModelImportBoneWeight& weight : binding.weights)
            {
                firstVertexInfluenceCount += weight.vertexIndex == 0 && weight.weight > 0.0f ? 1u : 0u;
            }
        }
    }
    Check(firstVertexInfluenceCount == 5,
          "retain all five positive source influences for a vertex before extraction policy");
    tools::ImportedMesh reducedWeightsGeometry;
    Check(tools::ExtractModelGeometry(extendedWeightsManifest, 0, extractionSettings, reducedWeightsGeometry) ==
                  tools::ModelImportResult::Success &&
              reducedWeightsGeometry.submeshes.Size() == 1,
          "apply runtime skinning reduction only while extracting selected geometry");
    const tools::ImportedVertexStream* reducedWeights = nullptr;
    if (reducedWeightsGeometry.submeshes.Size() == 1)
    {
        for (const tools::ImportedVertexStream& stream : reducedWeightsGeometry.submeshes[0].vertexStreams)
        {
            if (stream.semantic == meshes::VertexSemantic::JointWeights)
            {
                reducedWeights = &stream;
                break;
            }
        }
    }
    Check(reducedWeights != nullptr && reducedWeights->floatValues.Size() >= 4 &&
              std::fabs(reducedWeights->floatValues[0] + reducedWeights->floatValues[1] +
                            reducedWeights->floatValues[2] + reducedWeights->floatValues[3] -
                        1.0f) <
                  0.0001f &&
              reducedWeights->floatValues[3] > 0.0f,
          "reduce to four normalized influences only at cooker extraction boundary");

    tools::ModelImportLimits tinyModelLimits = modelLimits;
    tinyModelLimits.maximumNodes = 1;
    tools::ModelImportManifest limitedManifest;
    Check(tools::InspectModelMemory(canonicalModelGltf, sizeof(canonicalModelGltf) - 1, "gltf", "memory/limited.gltf",
                                    modelOptions, tinyModelLimits, limitedManifest) ==
                  tools::ModelImportResult::LimitExceeded &&
              limitedManifest.nodes.Empty(),
          "enforce aggregate model manifest limits without leaking partial output");

    tools::ModelImportLimits tinyPayloadLimits = modelLimits;
    tinyPayloadLimits.maximumOwnedPayloadBytes = 1;
    tools::ModelImportManifest payloadLimitedManifest;
    Check(tools::InspectModelMemory(canonicalModelGltf, sizeof(canonicalModelGltf) - 1, "gltf", "memory/payload_limited.gltf",
                                    modelOptions, tinyPayloadLimits, payloadLimitedManifest) ==
                  tools::ModelImportResult::LimitExceeded &&
              payloadLimitedManifest.primitives.Empty(),
          "enforce low aggregate owned-payload limits without unsigned-underflow bypass");

    tools::ModelImportLimits tinyBoneLimits = modelLimits;
    tinyBoneLimits.maximumBoneBindings = 1;
    tools::ModelImportManifest boneLimitedManifest;
    Check(tools::InspectModelMemory(ExtendedWeightsGltf, sizeof(ExtendedWeightsGltf) - 1, "gltf", "memory/bone_limited.gltf",
                                    modelOptions, tinyBoneLimits, boneLimitedManifest) ==
                  tools::ModelImportResult::LimitExceeded &&
              boneLimitedManifest.primitives.Empty(),
          "enforce aggregate bone-binding limits even when one primitive exceeds the configured cap");

    tools::ModelImportOptions cancelledOptions = modelOptions;
    cancelledOptions.progress = CancelModelImportProgress;
    tools::ModelImportManifest cancelledManifest;
    Check(tools::InspectModelMemory(canonicalModelGltf, sizeof(canonicalModelGltf) - 1, "gltf", "memory/cancelled.gltf",
                                    cancelledOptions, modelLimits, cancelledManifest) ==
                  tools::ModelImportResult::Cancelled &&
              cancelledManifest.nodes.Empty(),
          "cancel model inspection without publishing partial canonical state");

    const filesystem::AbsolutePath importTestRoot = vanguard::tests::ScratchDirectory("mesh_tools_tests");
    const filesystem::AbsolutePath canonicalModelPath = importTestRoot.AddFilePath("vanguard_model_import_test.gltf");
    Check(WriteTestFile(canonicalModelPath.AsChar(), canonicalModelGltf), "create exact-snapshot model inspection fixture");
    tools::ModelImportManifest fileModelManifest;
    tools::ModelImportReport fileModelReport;
    Check(tools::InspectModelFile(canonicalModelPath, modelOptions, modelLimits, fileModelManifest, &fileModelReport) ==
                  tools::ModelImportResult::Success &&
              fileModelManifest.sourceFingerprint ==
                  vanguard::crypto::Sha256(canonicalModelGltf, sizeof(canonicalModelGltf) - 1),
          "inspect and fingerprint the exact physical model source snapshot");
    bool foundMainSourceDependency = false;
    bool foundMissingImageDependency = false;
    for (const tools::ModelImportDependency& dependency : fileModelManifest.dependencies)
    {
        if (dependency.kind == tools::ModelImportDependencyKind::MainSource && dependency.exists)
        {
            foundMainSourceDependency = true;
        }
        if (dependency.kind == tools::ModelImportDependencyKind::ExternalTexture && !dependency.exists)
        {
            foundMissingImageDependency = true;
        }
    }
    Check(foundMainSourceDependency && foundMissingImageDependency &&
              fileModelManifest.textureReferences.Size() == 2 && fileModelManifest.textureReferences[0].missing &&
              fileModelManifest.textureReferences[1].missing &&
              fileModelManifest.textureReferences[1].semantic == tools::ImportedTextureSemantic::MetallicRoughness,
          "classify main source and unresolved external image provenance without rejecting model inspection");
    static_cast<void>(std::remove(canonicalModelPath.AsChar()));
    const filesystem::AbsolutePath importObjPath = importTestRoot.AddFilePath("vanguard_mesh_import_test.obj");
    const filesystem::AbsolutePath importMtlPath = importTestRoot.AddFilePath("vanguard_mesh_import_test.mtl");
    const filesystem::AbsolutePath importTexturePath = importTestRoot.AddFilePath("vanguard_mesh_import_test.png");
    constexpr char fileObj[] =
        "mtllib vanguard_mesh_import_test.mtl\n"
        "o FileTriangle\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "usemtl tracked_material\n"
        "f 1 2 3\n";
    constexpr char fileMtl[] =
        "newmtl tracked_material\n"
        "Kd 1 1 1\n"
        "map_Kd vanguard_mesh_import_test.png\n";
    Check(WriteTestFile(importObjPath.AsChar(), fileObj) && WriteTestFile(importMtlPath.AsChar(), fileMtl) &&
              WriteTestFile(importTexturePath.AsChar(), "fixture"),
          "create mesh import dependency fixtures");
    tools::ImportedMesh fileImportedMesh;
    tools::MeshImportReport fileImportReport;
    Check(tools::ImportMeshFile(importObjPath, importSettings, fileImportedMesh, &fileImportReport) == tools::MeshImportResult::Success &&
              fileImportReport.externalDependencyCount >= 3,
          "track source, material library, and external texture dependencies");
    static_cast<void>(std::remove(importTexturePath.AsChar()));
    tools::ImportedMesh missingDependencyMesh;
    Check(tools::ImportMeshFile(importObjPath, importSettings, missingDependencyMesh) == tools::MeshImportResult::MissingDependency,
          "reject a strict mesh import with a missing external texture");
    static_cast<void>(std::remove(importObjPath.AsChar()));
    static_cast<void>(std::remove(importMtlPath.AsChar()));

    const filesystem::AbsolutePath compilerSourcePath = importTestRoot.AddFilePath("vanguard_mesh_compiler_test.obj");
    Check(WriteTestFile(compilerSourcePath.AsChar(), importedObj), "create mesh asset compiler source fixture");
    const resources::ResourceReference compilerSource(resources::ResourcePath::FromString("meshes/compiler_triangle.obj"),
                                                      tools::MeshSourceResourceType);
    const resources::ResourceReference compilerOutput(resources::ResourcePath::FromString("meshes/compiler_triangle.vmesh"),
                                                      meshes::MeshResourceType);
    MeshCompilerFixture compilerFixture{compilerSourcePath, compilerSource, reinterpret_cast<const vanguard::u8*>(importedObj),
                                        static_cast<vanguard::u32>(sizeof(importedObj) - 1u)};
    tools::MeshAssetCompilerConfig compilerConfig;
    compilerConfig.resolveSourceFile = ResolveMeshSourceFile;
    compilerConfig.resolveSourceFileUserData = &compilerFixture;
    compilerConfig.resolveDependency = ResolveMeshDependency;
    compilerConfig.resolveDependencyUserData = &compilerFixture;
    tools::MeshAssetCompiler meshCompiler;
    Check(meshCompiler.Initialize(compilerConfig), "initialize mesh asset compiler against project source resolver boundary");
    vanguard::assets::Config assetBuildConfig;
    assetBuildConfig.maximumCacheEntries = 4;
    assetBuildConfig.maximumCacheBytes = 4u * 1024u * 1024u;
    vanguard::assets::BuildSystem meshBuildSystem;
    Check(meshBuildSystem.Initialize(assetBuildConfig) && meshCompiler.Register(meshBuildSystem) == vanguard::assets::Result::Success,
          "register mesh compiler with generic asset build system");
    tools::MeshBuildDescription buildDescription;
    buildDescription.sourceName = "meshes/compiler_triangle.obj";
    buildDescription.formatHint = "obj";
    buildDescription.import.defaultMaterial = importedMaterial;
    containers::DynamicArray<vanguard::u8> buildSettings(memory::pools::Assets::GetInstance());
    Check(tools::EncodeMeshBuildSettings(buildDescription, buildSettings) == tools::MeshBuildSettingsResult::Success,
          "encode deterministic mesh import and cook settings");
    vanguard::assets::BuildRequest meshBuildRequest{{compilerSource,
                                                     {reinterpret_cast<const vanguard::u8*>(importedObj), sizeof(importedObj) - 1u},
                                                     {}},
                                                    compilerOutput,
                                                    vanguard::assets::TargetPlatform::WindowsD3D12,
                                                    {buildSettings.TypedData(), buildSettings.Size()}};
    vanguard::assets::BuildPlan meshBuildPlan;
    const vanguard::crypto::Digest256 materialContent = vanguard::crypto::Sha256("fixture material", 16);
    Check(meshBuildSystem.Prepare(meshBuildRequest, meshBuildPlan) == vanguard::assets::Result::Success && meshBuildPlan.HasResourceEstimate() &&
              meshBuildPlan.GetResourceEstimate().artifactBytes == compilerConfig.maximumArtifactBytes &&
              meshBuildPlan.SetGeneratedDependencyContent(importedMaterial, materialContent) == vanguard::assets::Result::Success,
          "prepare an admission-estimated mesh build and resolve its generated material dependency");
    vanguard::assets::BuildOutput meshBuildOutput;
    const vanguard::assets::Result meshBuildResult = meshBuildSystem.Execute(meshBuildRequest, meshBuildPlan, meshBuildOutput);
    if (meshBuildResult != vanguard::assets::Result::Success)
        std::fprintf(stderr, "[meshToolsTests] mesh compiler build result: %s\n", vanguard::assets::ToString(meshBuildResult));
    Check(meshBuildResult == vanguard::assets::Result::Success &&
              meshBuildOutput.disposition == vanguard::assets::BuildDisposition::Built && meshBuildOutput.artifacts.Size() > 1 &&
              meshBuildOutput.artifacts[0].resource == compilerOutput &&
              vanguard::assets::HasFlag(meshBuildOutput.artifacts[0].flags, vanguard::assets::ArtifactFlags::Primary) &&
              vanguard::assets::HasFlag(meshBuildOutput.artifacts[0].flags, vanguard::assets::ArtifactFlags::MemoryResident) &&
              vanguard::assets::HasFlag(meshBuildOutput.artifacts[1].flags, vanguard::assets::ArtifactFlags::Streamable),
          "build imported mesh as metadata and page artifacts through BuildSystem and ArtifactWriter");
    if (!meshBuildOutput.artifacts.Empty())
    {
        containers::DynamicArray<vanguard::u8> compiledMeshBytes(memory::pools::Assets::GetInstance());
        for (const vanguard::assets::Artifact& artifact : meshBuildOutput.artifacts)
            for (const vanguard::u8 byte : artifact.bytes)
                compiledMeshBytes.PushBack(byte);
        filesystem::MemoryFileReader compiledMeshReader(compiledMeshBytes, 0);
        meshes::MeshFile compiledMesh;
        Check(compiledMesh.Open(compiledMeshReader) == meshes::Result::Success && compiledMesh.GetSubmeshes().Size() == 1 &&
                  compiledMesh.GetLods().Size() == 1 && !compiledMesh.GetPages().Empty(),
              "open BuildSystem artifact through the runtime vmesh reader");
    }
    vanguard::assets::BuildOutput cachedMeshBuild;
    Check(meshBuildSystem.Execute(meshBuildRequest, meshBuildPlan, cachedMeshBuild) == vanguard::assets::Result::Success &&
              cachedMeshBuild.disposition == vanguard::assets::BuildDisposition::CacheHit &&
              cachedMeshBuild.contentFingerprint == meshBuildOutput.contentFingerprint,
          "reuse the deterministic mesh artifact through the generic derived-data cache");

    const filesystem::AbsolutePath bunnySourcePath = filesystem::paths::GetCurrentWorkingDirectory().AddFilePath("tests/data_assets/bunny/bunny.obj");
    containers::DynamicArray<vanguard::u8> bunnySourceBytes(memory::pools::Assets::GetInstance());
    Check(ReadTestFile(bunnySourcePath.AsChar(), bunnySourceBytes), "read the real Stanford Bunny source asset");
    const resources::ResourceReference bunnySource(resources::ResourcePath::FromString("meshes/stanford_bunny.obj"), tools::MeshSourceResourceType);
    const resources::ResourceReference bunnyOutput(resources::ResourcePath::FromString("meshes/stanford_bunny.vmesh"), meshes::MeshResourceType);
    compilerFixture.sourceFile = bunnySourcePath;
    compilerFixture.source = bunnySource;
    compilerFixture.bytes = bunnySourceBytes.TypedData();
    compilerFixture.byteCount = bunnySourceBytes.Size();
    tools::MeshBuildDescription bunnyDescription;
    bunnyDescription.sourceName = "meshes/stanford_bunny.obj";
    bunnyDescription.formatHint = "obj";
    bunnyDescription.import.sourceUpAxis = tools::SourceUpAxis::Y;
    bunnyDescription.import.defaultMaterial = importedMaterial;
    containers::DynamicArray<vanguard::u8> bunnySettings(memory::pools::Assets::GetInstance());
    Check(tools::EncodeMeshBuildSettings(bunnyDescription, bunnySettings) == tools::MeshBuildSettingsResult::Success,
          "encode Stanford Bunny build settings");
    vanguard::assets::BuildRequest bunnyRequest{{bunnySource, {bunnySourceBytes.TypedData(), bunnySourceBytes.Size()}, {}},
                                                bunnyOutput,
                                                vanguard::assets::TargetPlatform::WindowsD3D12,
                                                {bunnySettings.TypedData(), bunnySettings.Size()}};
    vanguard::assets::BuildPlan bunnyPlan;
    Check(meshBuildSystem.Prepare(bunnyRequest, bunnyPlan) == vanguard::assets::Result::Success &&
              bunnyPlan.SetGeneratedDependencyContent(importedMaterial, materialContent) == vanguard::assets::Result::Success,
          "prepare Stanford Bunny through the registered mesh compiler");
    vanguard::assets::BuildOutput bunnyBuild;
    Check(meshBuildSystem.Execute(bunnyRequest, bunnyPlan, bunnyBuild) == vanguard::assets::Result::Success && bunnyBuild.artifacts.Size() > 1,
          "import, cook, and segment the Stanford Bunny through BuildSystem");
    containers::DynamicArray<vanguard::u8> bunnyCookedBytes(memory::pools::Assets::GetInstance());
    vanguard::u64 bunnyArtifactBytes = 0;
    for (const vanguard::assets::Artifact& artifact : bunnyBuild.artifacts)
    {
        bunnyArtifactBytes += artifact.bytes.Size();
        for (const vanguard::u8 byte : artifact.bytes)
            bunnyCookedBytes.PushBack(byte);
    }
    filesystem::MemoryFileReader bunnyReader(bunnyCookedBytes, 0);
    meshes::MeshFile bunnyMesh;
    Check(bunnyMesh.Open(bunnyReader) == meshes::Result::Success && bunnyMesh.GetSubmeshes().Size() == 1 &&
              bunnyMesh.GetLods().Size() == 1 && bunnyMesh.GetVertexStreams().Size() == 2 && !bunnyMesh.GetPages().Empty(),
          "open and validate the cooked Stanford Bunny metadata");
    bool bunnyPagesValid = bunnyMesh.IsOpen();
    for (vanguard::u32 pageIndex = 0; pageIndex < bunnyMesh.GetPages().Size(); ++pageIndex)
    {
        const meshes::PageRecord& page = bunnyMesh.GetPages()[pageIndex];
        containers::DynamicArray<vanguard::u8> pageBytes(memory::pools::Assets::GetInstance());
        pageBytes.Resize(static_cast<vanguard::u32>(page.byteSize));
        bunnyPagesValid = bunnyPagesValid &&
                          bunnyMesh.ReadPage(bunnyReader, pageIndex, pageBytes.TypedData(), pageBytes.Size()) == meshes::Result::Success;
    }
    Check(bunnyPagesValid, "read and hash-validate every cooked Stanford Bunny geometry page");
    if (bunnyMesh.IsOpen() && !bunnyMesh.GetSubmeshes().Empty())
    {
        const meshes::SubmeshRecord& bunnySubmesh = bunnyMesh.GetSubmeshes()[0];
        const meshes::Bounds& bunnyBounds = bunnyMesh.GetMeshBounds();
        std::printf("[meshToolsTests] bunny: source=%u bytes, cooked=%llu bytes, vertices=%u, indices=%u, streams=%u, pages=%u, artifacts=%u, "
                    "bounds=[(%g,%g,%g)-(%g,%g,%g)]\n",
                    bunnySourceBytes.Size(), static_cast<unsigned long long>(bunnyArtifactBytes), bunnySubmesh.vertexCount, bunnySubmesh.indexCount,
                    bunnyMesh.GetVertexStreams().Size(), bunnyMesh.GetPages().Size(), bunnyBuild.artifacts.Size(), bunnyBounds.minimum[0],
                    bunnyBounds.minimum[1], bunnyBounds.minimum[2], bunnyBounds.maximum[0], bunnyBounds.maximum[1], bunnyBounds.maximum[2]);
    }
    Check(meshCompiler.Unregister() == vanguard::assets::Result::Success && meshCompiler.Shutdown() && meshBuildSystem.Shutdown(),
          "shut down mesh asset compiler and build system cleanly");
    static_cast<void>(std::remove(compilerSourcePath.AsChar()));

    constexpr std::array<vanguard::f32, 18> positions{-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 0.0f,
                                                      -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  0.0f, -1.0f, 1.0f, 0.0f};
    constexpr std::array<vanguard::f32, 18> normals{0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    constexpr std::array<vanguard::u32, 6> indices{0, 1, 2, 3, 4, 5};
    const std::array<tools::SourceVertexStream, 2> streams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, positions.data(), 6, 12},
         {meshes::VertexSemantic::Normal, 0, meshes::VertexFormat::R32G32B32Float, normals.data(), 6, 12}}};
    const std::array<tools::SourceSubmesh, 1> submeshes{
        {{0x9d23u,
          0x71756164u,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {streams.data(), static_cast<vanguard::u32>(streams.size())},
          {indices.data(), static_cast<vanguard::u32>(indices.size())}}}};
    tools::SourceMesh source;
    source.name = 0x71756164u;
    source.sourceFingerprint = vanguard::crypto::Sha256(positions.data(), sizeof(positions));
    source.submeshes = {submeshes.data(), static_cast<vanguard::u32>(submeshes.size())};

    containers::DynamicArray<vanguard::u8> bytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter writer(bytes);
    tools::CookReport report;
    Check(tools::CookMesh(source, writer, {}, &report) == tools::Result::Success, "cook importer-neutral static mesh");
    Check(report.submeshes.Size() == 1 && report.submeshes[0].sourceVertexCount == 6 && report.submeshes[0].cookedVertexCount == 4 &&
              report.meshCookingProfile == tools::profiles::RuntimeStatic && report.meshCookingProfileVersion == 1,
          "deduplicate vertices and report the selected cooking profile");
    tools::MeshCookingProfile lateProfile = customDeformableProfile;
    lateProfile.id = 0x6c61746500000001ull;
    Check(tools::RegisterMeshCookingProfile(lateProfile) == tools::ProfileRegistrationResult::RegistrySealed,
          "seal the cooking profile registry when parallel-safe cooking begins");

    containers::DynamicArray<vanguard::u8> repeatedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter repeatedWriter(repeatedBytes);
    Check(tools::CookMesh(source, repeatedWriter) == tools::Result::Success && bytes.Size() == repeatedBytes.Size(), "repeat deterministic mesh cook");
    if (bytes.Size() == repeatedBytes.Size())
    {
        bool identical = true;
        for (vanguard::u32 index = 0; index < bytes.Size(); ++index)
        {
            if (bytes[index] != repeatedBytes[index])
            {
                identical = false;
                break;
            }
        }
        Check(identical, "deterministic cooked bytes");
    }

    filesystem::MemoryFileReader reader(bytes, 0);
    meshes::MeshFile mesh;
    Check(mesh.Open(reader) == meshes::Result::Success, "open cooked vmesh");
    Check(mesh.GetVertexLayouts().Size() == 1 && mesh.GetVertexStreams().Size() == 2 && mesh.GetSubmeshes().Size() == 1,
          "emit packed vertex layout and preserve submesh identity");
    if (mesh.GetSubmeshes().Size() == 1)
    {
        Check(mesh.GetSubmeshes()[0].stableId == submeshes[0].stableId && mesh.GetSubmeshes()[0].vertexCount == 4 && mesh.GetSubmeshes()[0].indexCount == 6 &&
                  mesh.GetSubmeshes()[0].indexFormat == meshes::IndexFormat::UInt16,
              "emit GPU-ready submesh ranges and compact indices");
    }
    if (mesh.GetVertexStreams().Size() == 2)
    {
        const meshes::VertexStream& positionStream = mesh.GetVertexStreams()[0];
        const meshes::VertexStream& normalStream = mesh.GetVertexStreams()[1];
        Check(positionStream.semantic == meshes::VertexSemantic::Position && positionStream.format == meshes::VertexFormat::R16G16B16A16SNorm &&
                  positionStream.binding == 0 && positionStream.stride == 8,
              "pack position into a dedicated 16-bit normalized fetch stream");
        Check(normalStream.semantic == meshes::VertexSemantic::Normal && normalStream.format == meshes::VertexFormat::R10G10B10A2UNorm &&
                  normalStream.binding == 1 && normalStream.stride == 4,
              "pack normal into the interleaved shading stream");
        Check(mesh.GetQuantization().scale[0] == 1.0f && mesh.GetQuantization().scale[1] == 1.0f && mesh.GetQuantization().scale[2] == 1.0f,
              "store mesh-wide position decode scale");
        if (positionStream.buffer < mesh.GetBuffers().Size())
        {
            const meshes::BufferRecord& positionBuffer = mesh.GetBuffers()[positionStream.buffer];
            containers::DynamicArray<vanguard::u8> packedPositions(memory::pools::Assets::GetInstance());
            packedPositions.Resize(static_cast<vanguard::u32>(positionBuffer.byteSize));
            Check(positionBuffer.pageCount == 1 &&
                      mesh.ReadPage(reader, positionBuffer.firstPage, packedPositions.TypedData(), packedPositions.Size()) == meshes::Result::Success,
                  "read direct-upload packed position page");
            if (packedPositions.Size() == 32)
            {
                bool decodedCorners = true;
                for (vanguard::u32 vertex = 0; vertex < 4; ++vertex)
                {
                    const auto* packed = reinterpret_cast<const vanguard::i16*>(packedPositions.TypedData() + vertex * 8);
                    const vanguard::f32 x = static_cast<vanguard::f32>(packed[0]) / 32767.0f * mesh.GetQuantization().scale[0] + mesh.GetQuantization().bias[0];
                    const vanguard::f32 y = static_cast<vanguard::f32>(packed[1]) / 32767.0f * mesh.GetQuantization().scale[1] + mesh.GetQuantization().bias[1];
                    const vanguard::f32 z = static_cast<vanguard::f32>(packed[2]) / 32767.0f * mesh.GetQuantization().scale[2] + mesh.GetQuantization().bias[2];
                    decodedCorners =
                        decodedCorners && std::fabs(std::fabs(x) - 1.0f) < 0.0001f && std::fabs(std::fabs(y) - 1.0f) < 0.0001f && std::fabs(z) < 0.0001f;
                }
                Check(decodedCorners, "decode quantized positions with vmesh scale and bias");
            }
        }
    }

    tools::CookSettings preserveSettings;
    preserveSettings.meshCookingProfile = tools::profiles::PreserveSource;
    containers::DynamicArray<vanguard::u8> preservedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter preservedWriter(preservedBytes);
    Check(tools::CookMesh(source, preservedWriter, preserveSettings) == tools::Result::Success, "retain an explicit lossless source packing profile");
    filesystem::MemoryFileReader preservedReader(preservedBytes, 0);
    meshes::MeshFile preservedMesh;
    Check(preservedMesh.Open(preservedReader) == meshes::Result::Success && preservedMesh.GetVertexStreams().Size() == 2 &&
              preservedMesh.GetVertexStreams()[0].format == meshes::VertexFormat::R32G32B32Float &&
              preservedMesh.GetVertexStreams()[1].format == meshes::VertexFormat::R32G32B32Float,
          "preserve source formats only when explicitly requested");

    tools::CookSettings customDeformableSettings;
    customDeformableSettings.meshCookingProfile = CustomDeformableProfileId;
    constexpr std::array<vanguard::u8, 24> undeclaredProfileData{};
    const std::array<tools::SourceVertexStream, 2> undeclaredProfileStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, positions.data(), 6, 12},
         {meshes::VertexSemantic::Color, 0, meshes::VertexFormat::R8G8B8A8UInt, undeclaredProfileData.data(), 6, 4}}};
    const std::array<tools::SourceSubmesh, 1> undeclaredProfileSubmeshes{
        {{0x756e6465636c6172ull,
          0x756e6465636c6172ull,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {undeclaredProfileStreams.data(), static_cast<vanguard::u32>(undeclaredProfileStreams.size())},
          {indices.data(), static_cast<vanguard::u32>(indices.size())}}}};
    tools::SourceMesh undeclaredProfileSource = source;
    undeclaredProfileSource.submeshes = {undeclaredProfileSubmeshes.data(), static_cast<vanguard::u32>(undeclaredProfileSubmeshes.size())};
    containers::DynamicArray<vanguard::u8> rejectedCustomBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter rejectedCustomWriter(rejectedCustomBytes);
    Check(tools::CookMesh(undeclaredProfileSource, rejectedCustomWriter, customDeformableSettings) == tools::Result::InvalidVertexStream,
          "reject source streams not declared by a strict custom cooking profile");

    constexpr std::array<vanguard::f32, 24> deformationData{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                                                            0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
    const std::array<tools::SourceVertexStream, 2> customDeformableStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, positions.data(), 6, 12},
         {meshes::VertexSemantic::Custom, 0, meshes::VertexFormat::R32G32B32A32Float, deformationData.data(), 6, 16}}};
    const std::array<tools::SourceSubmesh, 1> customDeformableSubmeshes{
        {{0x6465666f726d0001ull,
          0x6465666f726d0001ull,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {customDeformableStreams.data(), static_cast<vanguard::u32>(customDeformableStreams.size())},
          {indices.data(), static_cast<vanguard::u32>(indices.size())}}}};
    tools::SourceMesh customDeformableSource = source;
    customDeformableSource.submeshes = {customDeformableSubmeshes.data(), static_cast<vanguard::u32>(customDeformableSubmeshes.size())};
    containers::DynamicArray<vanguard::u8> customDeformableBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter customDeformableWriter(customDeformableBytes);
    tools::CookReport customDeformableReport;
    Check(tools::CookMesh(customDeformableSource, customDeformableWriter, customDeformableSettings, &customDeformableReport) == tools::Result::Success &&
              customDeformableReport.meshCookingProfile == CustomDeformableProfileId && customDeformableReport.meshCookingProfileVersion == 7,
          "cook a project-defined deformable vertex layout through the registered profile");
    filesystem::MemoryFileReader customDeformableReader(customDeformableBytes, 0);
    meshes::MeshFile customDeformableMesh;
    Check(customDeformableMesh.Open(customDeformableReader) == meshes::Result::Success && customDeformableMesh.GetVertexStreams().Size() == 2 &&
              customDeformableMesh.GetVertexStreams()[0].format == meshes::VertexFormat::R16G16B16A16SNorm &&
              customDeformableMesh.GetVertexStreams()[1].format == meshes::VertexFormat::R32G32B32A32Float &&
              customDeformableMesh.GetVertexStreams()[0].binding != customDeformableMesh.GetVertexStreams()[1].binding,
          "inherit production position packing and persist a self-describing custom stream");

    constexpr std::array<vanguard::u8, 24> jointIndices{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
    constexpr std::array<vanguard::f32, 24> jointWeights{1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.25f, 0.25f, 0.0f,
                                                         1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.25f, 0.25f, 0.0f};
    const std::array<tools::SourceVertexStream, 4> skinnedStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, positions.data(), 6, 12},
         {meshes::VertexSemantic::Normal, 0, meshes::VertexFormat::R32G32B32Float, normals.data(), 6, 12},
         {meshes::VertexSemantic::JointIndices, 0, meshes::VertexFormat::R8G8B8A8UInt, jointIndices.data(), 6, 4},
         {meshes::VertexSemantic::JointWeights, 0, meshes::VertexFormat::R32G32B32A32Float, jointWeights.data(), 6, 16}}};
    const std::array<tools::SourceSubmesh, 1> skinnedSubmeshes{
        {{0x736b696e6e656434ull,
          0x736b696e6e656434ull,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {skinnedStreams.data(), static_cast<vanguard::u32>(skinnedStreams.size())},
          {indices.data(), static_cast<vanguard::u32>(indices.size())}}}};
    tools::SourceMesh skinnedSource = source;
    skinnedSource.submeshes = {skinnedSubmeshes.data(), static_cast<vanguard::u32>(skinnedSubmeshes.size())};
    skinnedSource.skeleton =
        resources::ResourceReference(resources::ResourcePath::FromString("skeletons/test.vskel"), serialization::MakeFourCC('V', 'S', 'K', 'L'));
    tools::CookSettings skinnedSettings;
    skinnedSettings.meshCookingProfile = tools::profiles::RuntimeSkinned4;
    tools::SourceMesh missingSkinSource = source;
    missingSkinSource.skeleton = skinnedSource.skeleton;
    containers::DynamicArray<vanguard::u8> missingSkinBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter missingSkinWriter(missingSkinBytes);
    Check(tools::CookMesh(missingSkinSource, missingSkinWriter, skinnedSettings) == tools::Result::InvalidVertexStream,
          "enforce required streams declared by the four-influence skinned profile");
    containers::DynamicArray<vanguard::u8> skinnedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter skinnedWriter(skinnedBytes);
    Check(tools::CookMesh(skinnedSource, skinnedWriter, skinnedSettings) == tools::Result::Success, "cook the built-in four-influence skinned layout");
    filesystem::MemoryFileReader skinnedReader(skinnedBytes, 0);
    meshes::MeshFile skinnedMesh;
    Check(skinnedMesh.Open(skinnedReader) == meshes::Result::Success && skinnedMesh.GetVertexStreams().Size() == 4 &&
              skinnedMesh.GetKind() == meshes::MeshKind::Skinned && skinnedMesh.GetSkeleton() == skinnedSource.skeleton,
          "persist the skinned mesh kind and external skeleton reference");
    const meshes::VertexStream* cookedJointIndices = nullptr;
    const meshes::VertexStream* cookedJointWeights = nullptr;
    for (const meshes::VertexStream& stream : skinnedMesh.GetVertexStreams())
    {
        if (stream.semantic == meshes::VertexSemantic::JointIndices)
        {
            cookedJointIndices = &stream;
        }
        else if (stream.semantic == meshes::VertexSemantic::JointWeights)
        {
            cookedJointWeights = &stream;
        }
    }
    Check(cookedJointIndices != nullptr && cookedJointWeights != nullptr && cookedJointIndices->binding == cookedJointWeights->binding &&
              cookedJointIndices->format == meshes::VertexFormat::R8G8B8A8UInt && cookedJointWeights->format == meshes::VertexFormat::R8G8B8A8UNorm,
          "interleave and quantize the skinned profile's required influence streams");

    constexpr vanguard::u32 GridWidth = 9;
    constexpr vanguard::u32 GridVertexCount = GridWidth * GridWidth;
    constexpr vanguard::u32 GridIndexCount = (GridWidth - 1) * (GridWidth - 1) * 6;
    std::array<vanguard::f32, GridVertexCount * 3> gridPositions{};
    std::array<vanguard::f32, GridVertexCount * 3> gridNormals{};
    std::array<vanguard::f32, GridVertexCount * 2> gridTexCoords{};
    std::array<vanguard::u32, GridIndexCount> gridIndices{};
    for (vanguard::u32 y = 0; y < GridWidth; ++y)
    {
        for (vanguard::u32 x = 0; x < GridWidth; ++x)
        {
            const vanguard::u32 vertex = y * GridWidth + x;
            gridPositions[vertex * 3 + 0] = static_cast<vanguard::f32>(x);
            gridPositions[vertex * 3 + 1] = static_cast<vanguard::f32>(y);
            gridPositions[vertex * 3 + 2] = (x > 2 && x < 6 && y > 2 && y < 6) ? 1.0f : 0.0f;
            gridNormals[vertex * 3 + 2] = 1.0f;
            gridTexCoords[vertex * 2 + 0] = static_cast<vanguard::f32>(x) / static_cast<vanguard::f32>(GridWidth - 1);
            gridTexCoords[vertex * 2 + 1] = static_cast<vanguard::f32>(y) / static_cast<vanguard::f32>(GridWidth - 1);
        }
    }
    vanguard::u32 nextGridIndex = 0;
    for (vanguard::u32 y = 0; y + 1 < GridWidth; ++y)
    {
        for (vanguard::u32 x = 0; x + 1 < GridWidth; ++x)
        {
            const vanguard::u32 topLeft = y * GridWidth + x;
            const vanguard::u32 topRight = topLeft + 1;
            const vanguard::u32 bottomLeft = topLeft + GridWidth;
            const vanguard::u32 bottomRight = bottomLeft + 1;
            gridIndices[nextGridIndex++] = topLeft;
            gridIndices[nextGridIndex++] = topRight;
            gridIndices[nextGridIndex++] = bottomRight;
            gridIndices[nextGridIndex++] = topLeft;
            gridIndices[nextGridIndex++] = bottomRight;
            gridIndices[nextGridIndex++] = bottomLeft;
        }
    }

    const std::array<tools::SourceVertexStream, 3> gridStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, gridPositions.data(), GridVertexCount, 12},
         {meshes::VertexSemantic::Normal, 0, meshes::VertexFormat::R32G32B32Float, gridNormals.data(), GridVertexCount, 12},
         {meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, gridTexCoords.data(), GridVertexCount, 8}}};
    const std::array<tools::SourceSubmesh, 1> gridSubmeshes{
        {{0x6c6f645f67726964u,
          0x67726964u,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {gridStreams.data(), static_cast<vanguard::u32>(gridStreams.size())},
          {gridIndices.data(), static_cast<vanguard::u32>(gridIndices.size())}}}};
    tools::SourceMesh gridSource;
    gridSource.name = 0x67726964u;
    gridSource.sourceFingerprint = vanguard::crypto::Sha256(gridPositions.data(), sizeof(gridPositions));
    gridSource.submeshes = {gridSubmeshes.data(), static_cast<vanguard::u32>(gridSubmeshes.size())};
    constexpr std::array<tools::LodLevelSettings, 2> lodLevels{{{0.5f, 0.5f, 0.5f}, {0.25f, 1.0f, 0.2f}}};
    tools::CookSettings lodSettings;
    lodSettings.lodLevels = {lodLevels.data(), static_cast<vanguard::u32>(lodLevels.size())};

    constexpr std::array<tools::LodLevelSettings, 1> invalidLodLevels{{{0.5f, 0.5f, 0.0f}}};
    tools::CookSettings invalidLodSettings;
    invalidLodSettings.lodLevels = {invalidLodLevels.data(), static_cast<vanguard::u32>(invalidLodLevels.size())};
    containers::DynamicArray<vanguard::u8> invalidLodBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter invalidLodWriter(invalidLodBytes);
    Check(tools::CookMesh(gridSource, invalidLodWriter, invalidLodSettings) == tools::Result::InvalidArgument,
          "reject zero LOD screen coverage at the cooker boundary");

    containers::DynamicArray<vanguard::u8> lodBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter lodWriter(lodBytes);
    tools::CookReport lodReport;
    Check(tools::CookMesh(gridSource, lodWriter, lodSettings, &lodReport) == tools::Result::Success, "cook attribute-aware LOD chain");
    filesystem::MemoryFileReader lodReader(lodBytes, 0);
    meshes::MeshFile lodMesh;
    Check(lodMesh.Open(lodReader) == meshes::Result::Success, "open cooked LOD vmesh");
    Check(lodMesh.GetLods().Size() == 3 && lodMesh.GetSubmeshes().Size() == 3 && lodMesh.GetVertexLayouts().Size() == 3,
          "emit one stable submesh and full vertex layout per LOD");
    if (lodMesh.GetSubmeshes().Size() == 3)
    {
        Check(lodMesh.GetSubmeshes()[0].stableId == gridSubmeshes[0].stableId && lodMesh.GetSubmeshes()[1].stableId == gridSubmeshes[0].stableId &&
                  lodMesh.GetSubmeshes()[2].stableId == gridSubmeshes[0].stableId,
              "preserve stable submesh identity across LODs");
        Check(lodMesh.GetSubmeshes()[1].indexCount < lodMesh.GetSubmeshes()[0].indexCount && lodMesh.GetSubmeshes()[2].indexCount < lodMesh.GetSubmeshes()[1].indexCount,
              "reduce triangle count across sequential LODs");
    }
    Check(lodReport.lods.Size() == 3 && lodReport.lods[1].attributeComponentCount == 5 &&
              lodReport.lods[1].normalizedError <= lodLevels[0].maximumNormalizedError &&
              lodReport.lods[2].normalizedError <= lodLevels[1].maximumNormalizedError,
          "account for normal and texture attributes and report direct LOD0 simplification error");
    if (lodMesh.GetVertexStreams().Size() >= 3)
    {
        Check(lodMesh.GetVertexStreams()[0].format == meshes::VertexFormat::R16G16B16A16SNorm &&
                  lodMesh.GetVertexStreams()[1].format == meshes::VertexFormat::R10G10B10A2UNorm &&
                  lodMesh.GetVertexStreams()[2].format == meshes::VertexFormat::R16G16Float &&
                  lodMesh.GetVertexStreams()[1].binding == lodMesh.GetVertexStreams()[2].binding && lodMesh.GetVertexStreams()[1].stride == 8 &&
                  lodMesh.GetVertexStreams()[2].stride == 8,
              "interleave packed normal and UV attributes after full-precision LOD generation");
    }

    std::array<vanguard::u8, GridVertexCount * 4> packedColors{};
    for (vanguard::u32 vertex = 0; vertex < GridVertexCount; ++vertex)
    {
        packedColors[vertex * 4 + 0] = static_cast<vanguard::u8>(vertex % 256u);
        packedColors[vertex * 4 + 1] = static_cast<vanguard::u8>((vertex * 3u) % 256u);
        packedColors[vertex * 4 + 2] = static_cast<vanguard::u8>((vertex * 7u) % 256u);
        packedColors[vertex * 4 + 3] = 255u;
    }
    const std::array<tools::SourceVertexStream, 2> packedAttributeStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, gridPositions.data(), GridVertexCount, 12},
         {meshes::VertexSemantic::Color, 0, meshes::VertexFormat::R8G8B8A8UNorm, packedColors.data(), GridVertexCount, 4}}};
    const std::array<tools::SourceSubmesh, 1> packedAttributeSubmeshes{
        {{0x7061636b65645f61u,
          0x7061636b6564u,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {packedAttributeStreams.data(), static_cast<vanguard::u32>(packedAttributeStreams.size())},
          {gridIndices.data(), static_cast<vanguard::u32>(gridIndices.size())}}}};
    tools::SourceMesh packedAttributeSource;
    packedAttributeSource.name = 0x7061636b6564u;
    packedAttributeSource.sourceFingerprint = vanguard::crypto::Sha256(packedColors.data(), sizeof(packedColors));
    packedAttributeSource.submeshes = {packedAttributeSubmeshes.data(), static_cast<vanguard::u32>(packedAttributeSubmeshes.size())};
    tools::CookSettings packedAttributeSettings;
    packedAttributeSettings.lodLevels = {lodLevels.data(), static_cast<vanguard::u32>(lodLevels.size())};
    packedAttributeSettings.lodAttributeWeights.normal = 0.0f;
    packedAttributeSettings.lodAttributeWeights.tangent = 0.0f;
    packedAttributeSettings.lodAttributeWeights.texCoord = 0.0f;
    packedAttributeSettings.lodAttributeWeights.jointWeights = 0.0f;
    packedAttributeSettings.lodAttributeWeights.morphPosition = 0.0f;
    containers::DynamicArray<vanguard::u8> packedAttributeBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packedAttributeWriter(packedAttributeBytes);
    tools::CookReport packedAttributeReport;
    Check(tools::CookMesh(packedAttributeSource, packedAttributeWriter, packedAttributeSettings, &packedAttributeReport) == tools::Result::Success &&
              packedAttributeReport.lods.Size() == 3 && packedAttributeReport.lods[1].attributeComponentCount == 4,
          "decode normalized packed attributes into temporary LOD simplification data");

    std::array<tools::SourceVertexStream, 2> unsupportedWeightedStreams = packedAttributeStreams;
    unsupportedWeightedStreams[1].format = meshes::VertexFormat::R8G8B8A8UInt;
    const std::array<tools::SourceSubmesh, 1> unsupportedWeightedSubmeshes{
        {{0x756e737570706f72u,
          0x756e737570706f72u,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {unsupportedWeightedStreams.data(), static_cast<vanguard::u32>(unsupportedWeightedStreams.size())},
          {gridIndices.data(), static_cast<vanguard::u32>(gridIndices.size())}}}};
    tools::SourceMesh unsupportedWeightedSource = packedAttributeSource;
    unsupportedWeightedSource.submeshes = {unsupportedWeightedSubmeshes.data(), static_cast<vanguard::u32>(unsupportedWeightedSubmeshes.size())};
    containers::DynamicArray<vanguard::u8> unsupportedWeightedBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter unsupportedWeightedWriter(unsupportedWeightedBytes);
    Check(tools::CookMesh(unsupportedWeightedSource, unsupportedWeightedWriter, packedAttributeSettings) == tools::Result::InvalidVertexStream,
          "reject a weighted attribute format that cannot be decoded for LOD simplification");

    std::array<vanguard::u8, GridVertexCount * 4> passthroughTangents{};
    const std::array<tools::SourceVertexStream, 4> separatedGroupStreams{
        {{meshes::VertexSemantic::Position, 0, meshes::VertexFormat::R32G32B32Float, gridPositions.data(), GridVertexCount, 12},
         {meshes::VertexSemantic::Normal, 0, meshes::VertexFormat::R32G32B32Float, gridNormals.data(), GridVertexCount, 12},
         {meshes::VertexSemantic::Tangent, 0, meshes::VertexFormat::R8G8B8A8UInt, passthroughTangents.data(), GridVertexCount, 4},
         {meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, gridTexCoords.data(), GridVertexCount, 8}}};
    const std::array<tools::SourceSubmesh, 1> separatedGroupSubmeshes{
        {{0x67726f75705f6f72u,
          0x67726f75705f6f72u,
          0x64656661756c74u,
          resources::ResourceReference(resources::ResourcePath::FromString("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T')),
          meshes::SubmeshFlags::CastsShadow,
          {separatedGroupStreams.data(), static_cast<vanguard::u32>(separatedGroupStreams.size())},
          {gridIndices.data(), static_cast<vanguard::u32>(gridIndices.size())}}}};
    tools::SourceMesh separatedGroupSource = gridSource;
    separatedGroupSource.submeshes = {separatedGroupSubmeshes.data(), static_cast<vanguard::u32>(separatedGroupSubmeshes.size())};
    tools::CookSettings separatedGroupSettings;
    separatedGroupSettings.lodAttributeWeights.tangent = 0.0f;
    containers::DynamicArray<vanguard::u8> separatedGroupBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter separatedGroupWriter(separatedGroupBytes);
    Check(tools::CookMesh(separatedGroupSource, separatedGroupWriter, separatedGroupSettings) == tools::Result::Success,
          "cook a layout whose canonical semantic order does not match packed binding groups");
    filesystem::MemoryFileReader separatedGroupReader(separatedGroupBytes, 0);
    meshes::MeshFile separatedGroupMesh;
    Check(separatedGroupMesh.Open(separatedGroupReader) == meshes::Result::Success, "open packed-binding group regression vmesh");
    const meshes::VertexStream* groupedNormal = nullptr;
    const meshes::VertexStream* groupedTexCoord = nullptr;
    for (const meshes::VertexStream& stream : separatedGroupMesh.GetVertexStreams())
    {
        if (stream.semantic == meshes::VertexSemantic::Normal)
        {
            groupedNormal = &stream;
        }
        else if (stream.semantic == meshes::VertexSemantic::TexCoord)
        {
            groupedTexCoord = &stream;
        }
    }
    Check(groupedNormal != nullptr && groupedTexCoord != nullptr && groupedNormal->binding == groupedTexCoord->binding && groupedNormal->stride == 8 &&
              groupedTexCoord->stride == 8,
          "keep every shading stream in one contiguous binding regardless of semantic ordering");
    vanguard::u32 lowestLodPageCount = 0;
    for (const meshes::PageRecord& page : lodMesh.GetPages())
    {
        if (meshes::HasFlag(page.flags, meshes::PageFlags::RequiredForLowestLod))
        {
            ++lowestLodPageCount;
        }
    }
    Check(lowestLodPageCount == 3, "mark only the coarsest packed position, shading, and index pages as required residency");

    containers::DynamicArray<meshes::StorageSegment> meshStorageSegments(memory::pools::Assets::GetInstance());
    Check(meshes::BuildStorageSegments(lodMesh, lodBytes.Size(), meshStorageSegments) == meshes::Result::Success &&
              meshStorageSegments.Size() == lodMesh.GetPages().Size() + 1u,
          "plan byte-exact metadata and independently decodable vmesh page segments");
    containers::DynamicArray<vanguard::packages::BuildSegment> packagedMeshSegments(memory::pools::Assets::GetInstance());
    packagedMeshSegments.Resize(meshStorageSegments.Size());
    for (vanguard::u32 index = 0; index < meshStorageSegments.Size(); ++index)
    {
        const meshes::StorageSegment& segment = meshStorageSegments[index];
        vanguard::packages::SegmentFlags flags = vanguard::packages::SegmentFlags::Streamable;
        if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::Metadata))
        {
            flags = vanguard::packages::SegmentFlags::Inline | vanguard::packages::SegmentFlags::MemoryResident;
        }
        else if (meshes::HasFlag(segment.flags, meshes::StorageSegmentFlags::RequiredForLowestLod))
        {
            flags = flags | vanguard::packages::SegmentFlags::MemoryResident;
        }
        packagedMeshSegments[index] = {lodBytes.TypedData() + segment.offset, static_cast<vanguard::usize>(segment.byteSize), vanguard::packages::Codec::Lz4,
                                       segment.alignmentLog2, flags};
    }
    const std::array<vanguard::packages::Dependency, 1> packagedMeshDependencies{
        {{vanguard::packages::HashResourcePath("materials/default.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T'), resources::DependencyKind::Required}}};
    vanguard::packages::BuildResource packagedMesh;
    packagedMesh.path = "meshes/grid.vmesh";
    packagedMesh.type = meshes::MeshResourceType;
    packagedMesh.flags = vanguard::packages::ResourceFlags::Streamable;
    packagedMesh.segments = {packagedMeshSegments.TypedData(), packagedMeshSegments.Size()};
    packagedMesh.dependencies = {packagedMeshDependencies.data(), static_cast<vanguard::u32>(packagedMeshDependencies.size())};

    containers::DynamicArray<vanguard::u8> packageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packageWriterFile(packageBytes);
    vanguard::packages::PackageWriter packageWriter;
    Check(packageWriter.Begin(packageWriterFile) == vanguard::packages::Result::Success &&
              packageWriter.Add(packagedMesh) == vanguard::packages::Result::Success && packageWriter.Finalize() == vanguard::packages::Result::Success,
          "package vmesh as an opaque typed VPAK resource");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    vanguard::packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == vanguard::packages::Result::Success, "open VPAK containing vmesh");
    const vanguard::packages::Resource* packagedMeshRecord = packageReader.Find("meshes/grid.vmesh");
    Check(packagedMeshRecord != nullptr && packagedMeshRecord->type == meshes::MeshResourceType && packageReader.GetDependencies(*packagedMeshRecord).Size() == 1,
          "preserve vmesh type and material dependency in VPAK");
    if (packagedMeshRecord != nullptr)
    {
        vanguard::packages::ResourceFileReader packagedMeshView;
        Check(packagedMeshView.Open(packageReader, *packagedMeshRecord, packageReaderFile) == vanguard::packages::Result::Success,
              "open seekable logical vmesh view over VPAK segments");
        meshes::MeshFile residentMesh;
        Check(residentMesh.Open(packagedMeshView) == meshes::Result::Success && residentMesh.GetContentFingerprint() == lodMesh.GetContentFingerprint() &&
                  packagedMeshView.GetDecodedSegmentCount() == 1,
              "open vmesh metadata by decoding only the metadata package segment");
        const vanguard::u64 metadataStoredBytes = packagedMeshView.GetStoredBytesRead();
        Check(metadataStoredBytes < packagedMeshRecord->logicalSize, "avoid reconstructing geometry while opening package-backed vmesh metadata");
        if (!residentMesh.GetPages().Empty())
        {
            containers::DynamicArray<vanguard::u8> residentPage(memory::pools::Assets::GetInstance());
            residentPage.Resize(static_cast<vanguard::u32>(residentMesh.GetPages()[0].byteSize));
            Check(residentMesh.ReadPage(packagedMeshView, 0, residentPage.TypedData(), residentPage.Size()) == meshes::Result::Success &&
                      packagedMeshView.GetDecodedSegmentCount() == 2 && packagedMeshView.GetStoredBytesRead() > metadataStoredBytes,
                  "decode and validate only the requested VPAK-backed vmesh page");
        }
        containers::DynamicArray<vanguard::u32> residentLowestLodPages(memory::pools::Assets::GetInstance());
        Check(meshes::CollectLodPages(residentMesh, static_cast<vanguard::u16>(residentMesh.GetLods().Size() - 1u), residentLowestLodPages) ==
                      meshes::Result::Success &&
                  residentLowestLodPages.Size() == 3,
              "collect the exact deduplicated vertex and index page set for the lowest LOD");
        bool lowestLodPagesLoaded = true;
        for (const vanguard::u32 pageIndex : residentLowestLodPages)
        {
            const meshes::PageRecord& page = residentMesh.GetPages()[pageIndex];
            containers::DynamicArray<vanguard::u8> residentPage(memory::pools::Assets::GetInstance());
            residentPage.Resize(static_cast<vanguard::u32>(page.byteSize));
            lowestLodPagesLoaded = lowestLodPagesLoaded && meshes::HasFlag(page.flags, meshes::PageFlags::RequiredForLowestLod) &&
                                   residentMesh.ReadPage(packagedMeshView, pageIndex, residentPage.TypedData(), residentPage.Size()) == meshes::Result::Success;
        }
        Check(lowestLodPagesLoaded && packagedMeshView.GetStoredBytesRead() < packagedMeshRecord->logicalSize,
              "load the bootstrap LOD without reconstructing unrelated vmesh pages");
        if (!residentLowestLodPages.Empty())
        {
            const vanguard::u32 corruptPageIndex = residentLowestLodPages[0];
            vanguard::u32 corruptSegmentIndex = 0xffffffffu;
            for (vanguard::u32 index = 1; index < meshStorageSegments.Size(); ++index)
            {
                if (meshStorageSegments[index].page == corruptPageIndex)
                {
                    corruptSegmentIndex = index;
                    break;
                }
            }
            containers::DynamicArray<vanguard::u8> corruptPackageBytes(packageBytes);
            const auto packagedSegments = packageReader.GetSegments(*packagedMeshRecord);
            if (corruptSegmentIndex < packagedSegments.Count() && packagedSegments[corruptSegmentIndex].storedSize != 0)
            {
                corruptPackageBytes[static_cast<vanguard::u32>(packagedSegments[corruptSegmentIndex].offset)] ^= 0x5au;
                filesystem::MemoryFileReader corruptPackageFile(corruptPackageBytes, 0);
                vanguard::packages::ResourceFileReader corruptMeshView;
                Check(corruptMeshView.Open(packageReader, *packagedMeshRecord, corruptPackageFile) == vanguard::packages::Result::Success,
                      "open package view with corruption isolated to an unloaded geometry segment");
                const meshes::PageRecord& corruptPage = residentMesh.GetPages()[corruptPageIndex];
                containers::DynamicArray<vanguard::u8> corruptPageBytes(memory::pools::Assets::GetInstance());
                corruptPageBytes.Resize(static_cast<vanguard::u32>(corruptPage.byteSize));
                Check(residentMesh.ReadPage(corruptMeshView, corruptPageIndex, corruptPageBytes.TypedData(), corruptPageBytes.Size()) ==
                              meshes::Result::IoFailure &&
                          corruptMeshView.GetLastResult() == vanguard::packages::Result::IntegrityFailure,
                      "report integrity failure only when the corrupted VPAK-backed page is requested");
            }
            else
            {
                Check(false, "resolve packaged segment for localized corruption test");
            }
        }

        containers::DynamicArray<vanguard::u8> unpackedMesh(memory::pools::Assets::GetInstance());
        unpackedMesh.Resize(static_cast<vanguard::u32>(packagedMeshRecord->logicalSize));
        vanguard::u64 maximumStoredSegmentSize = 0;
        for (const vanguard::packages::Segment& segment : packageReader.GetSegments(*packagedMeshRecord))
        {
            if (segment.storedSize > maximumStoredSegmentSize)
            {
                maximumStoredSegmentSize = segment.storedSize;
            }
        }
        containers::DynamicArray<vanguard::u8> packageScratch(memory::pools::Assets::GetInstance());
        packageScratch.Resize(static_cast<vanguard::u32>(maximumStoredSegmentSize));
        Check(packageReader.ReadResource(packageReaderFile, *packagedMeshRecord, unpackedMesh.TypedData(), unpackedMesh.Size(), packageScratch.TypedData(),
                                         packageScratch.Size()) == vanguard::packages::Result::Success,
              "reconstruct exact logical vmesh bytes from VPAK");
        filesystem::MemoryFileReader unpackedMeshReader(unpackedMesh, 0);
        meshes::MeshFile packagedMeshFile;
        Check(packagedMeshFile.Open(unpackedMeshReader) == meshes::Result::Success && packagedMeshFile.GetContentFingerprint() == lodMesh.GetContentFingerprint(),
              "open packaged vmesh without format translation");
        if (!packagedMeshFile.GetPages().Empty())
        {
            containers::DynamicArray<vanguard::u8> packagedPage(memory::pools::Assets::GetInstance());
            packagedPage.Resize(static_cast<vanguard::u32>(packagedMeshFile.GetPages()[0].byteSize));
            Check(packagedMeshFile.ReadPage(unpackedMeshReader, 0, packagedPage.TypedData(), packagedPage.Size()) == meshes::Result::Success,
                  "validate packaged vmesh geometry page after reconstruction");
        }
    }

    containers::DynamicArray<vanguard::u8> repeatedLodBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter repeatedLodWriter(repeatedLodBytes);
    Check(tools::CookMesh(gridSource, repeatedLodWriter, lodSettings) == tools::Result::Success && repeatedLodBytes.Size() == lodBytes.Size(),
          "repeat deterministic LOD cook");
    if (repeatedLodBytes.Size() == lodBytes.Size())
    {
        bool identical = true;
        for (vanguard::u32 index = 0; index < lodBytes.Size(); ++index)
        {
            if (repeatedLodBytes[index] != lodBytes[index])
            {
                identical = false;
                break;
            }
        }
        Check(identical, "emit deterministic LOD bytes");
    }

    std::printf("[meshToolsTests] %s\n", g_failures == 0 ? "all checks passed" : "checks failed");
    return g_failures == 0 ? 0 : 1;
}
