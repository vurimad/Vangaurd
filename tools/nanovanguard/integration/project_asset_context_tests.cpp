#include "../../../tests/support/scratch_directory.hpp"

#include <vanguard/nanovanguard/project_asset_context.hpp>
#include <vanguard/nanovanguard/project_asset_commands.hpp>
#include <vanguard/nanovanguard/command_cancellation.hpp>
#include <vanguard/nanovanguard/nanovanguard.hpp>
#include <vanguard/nanovanguard/project_platform.hpp>

#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/directory_observation.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/entities/component_schemas.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/schemas/schemas.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/textures.hpp>

#include <cstdio>
#include <cstring>

namespace
{
    using namespace vanguard;

    u32 g_failures = 0;

    struct Buffer
    {
        char data[65536]{};
        u32 length = 0;
    };

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        ++g_failures;
        std::fprintf(stderr, "[nanovanguardProjectAssetTests] FAILED: %s\n", message);
    }

    [[nodiscard]] bool Capture(const char* const text, const u32 length, void* const userData) noexcept
    {
        Buffer& buffer = *static_cast<Buffer*>(userData);
        if (length > sizeof(buffer.data) - buffer.length - 1u)
            return false;
        for (u32 index = 0; index < length; ++index)
            buffer.data[buffer.length++] = text[index];
        buffer.data[buffer.length] = '\0';
        return true;
    }

    [[nodiscard]] bool Contains(const char* text, const char* token) noexcept
    {
        for (; text != nullptr && *text != '\0'; ++text)
        {
            const char* left = text;
            const char* right = token;
            while (*left != '\0' && *right != '\0' && *left == *right)
            {
                ++left;
                ++right;
            }
            if (*right == '\0')
                return true;
        }
        return false;
    }

    [[nodiscard]] u32 Count(const char* text, const char* token) noexcept
    {
        u32 count = 0u;
        if (text == nullptr || token == nullptr || token[0] == '\0')
            return 0u;
        for (const char* cursor = text; *cursor != '\0'; ++cursor)
        {
            const char* left = cursor;
            const char* right = token;
            while (*left != '\0' && *right != '\0' && *left == *right)
            {
                ++left;
                ++right;
            }
            if (*right == '\0')
                ++count;
        }
        return count;
    }

    struct FreshnessFailureCapture
    {
        Buffer buffer;
        filesystem::AbsolutePath source;
        bool mutationAttempted = false;
        bool sourceDeleted = false;
    };

    [[nodiscard]] bool CaptureAndDeleteSourceAfterProjectStatus(const char* const text, const u32 length,
                                                                void* const userData) noexcept
    {
        FreshnessFailureCapture& capture = *static_cast<FreshnessFailureCapture*>(userData);
        if (!Capture(text, length, &capture.buffer))
            return false;
        if (!capture.mutationAttempted && Contains(text, "\"scope\":\"project\""))
        {
            capture.mutationAttempted = true;
            capture.sourceDeleted = nanovanguard::platform::DeleteFile(capture.source.AsStringView());
        }
        return true;
    }

    struct ActiveWatchCancellationCapture
    {
        Buffer buffer;
        bool requested = false;
    };

    [[nodiscard]] bool CaptureAndCancelActiveWatch(const char* const text, const u32 length,
                                                   void* const userData) noexcept
    {
        ActiveWatchCancellationCapture& capture = *static_cast<ActiveWatchCancellationCapture*>(userData);
        if (!Capture(text, length, &capture.buffer))
            return false;
        if (!capture.requested && Contains(text, "\"event\":\"progress\"") &&
            Contains(text, "\"scope\":\"automatic-recook\"") &&
            (Contains(text, "\"state\":\"planning\"") || Contains(text, "\"state\":\"building\"")))
        {
            capture.requested = true;
            nanovanguard::RequestCommandCancellation();
        }
        return true;
    }

    [[nodiscard]] bool DeleteDirectoryTree(filesystem::Manager& files,
                                           const filesystem::AbsolutePath& directory) noexcept
    {
        bool removed = true;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Tools::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            removed = DeleteDirectoryTree(files, child) && removed;
        containers::DynamicArray<filesystem::AbsolutePath> storedFiles(memory::pools::Tools::GetInstance());
        files.FindFiles(directory, containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            removed = files.DeleteFile(storedFile) && removed;
        return files.DeletePath(directory) && removed;
    }

    [[nodiscard]] bool DirectoryExists(filesystem::Manager& files,
                                       const filesystem::AbsolutePath& directory) noexcept
    {
        const filesystem::AbsolutePath parent = filesystem::paths::ParentAbsolutePath(directory);
        if (parent.Empty())
            return false;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Tools::GetInstance());
        files.FindDirectories(parent, children);
        for (const filesystem::AbsolutePath& child : children)
            if (child == directory)
                return true;
        return false;
    }

    [[nodiscard]] projects::ProjectWorkspace MakeWorkspace(const filesystem::AbsolutePath& root) noexcept
    {
        projects::ProjectWorkspace workspace;
        workspace.root = root;
        workspace.projectFile = root.AddFilePath("headless.vproject");
        workspace.assets = root.AddDirPath("Assets");
        workspace.derivedData = root.AddDirPath("DerivedData");
        workspace.intermediate = root.AddDirPath("Intermediate");
        workspace.saved = root.AddDirPath("Saved");
        workspace.builds = root.AddDirPath("Builds");
        workspace.config = root.AddDirPath("Config");
        workspace.plugins = root.AddDirPath("Plugins");
        return workspace;
    }

    [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
    {
        const containers::StringView view = source.AsStringView();
        containers::String path(view.Data(), view.Length());
        path.Append(".vmeta", 6u);
        return filesystem::AbsolutePath::CreateFilePath(path);
    }

    [[nodiscard]] bool ReadAssetMetadata(const filesystem::AbsolutePath& source,
                                         assets::AssetMetadata& metadata) noexcept;

    struct P3ModelFixture final
    {
        filesystem::AbsolutePath source;
        containers::String sourceText;
        assets::AssetMetadata metadata;
        u32 meshes = 0;
        u32 materials = 0;
        u32 textures = 0;
        u32 compositions = 0;
    };

    [[nodiscard]] bool ReplaceOnce(containers::String& text, const char* const needle,
                                   const char* const replacement) noexcept
    {
        if (needle == nullptr || replacement == nullptr || needle[0] == '\0')
            return false;
        const u32 needleLength = static_cast<u32>(std::strlen(needle));
        const u32 replacementLength = static_cast<u32>(std::strlen(replacement));
        if (needleLength > text.Length())
            return false;
        const char* const source = text.AsChar();
        u32 offset = text.Length();
        for (u32 index = 0u; index <= text.Length() - needleLength; ++index)
            if (std::memcmp(source + index, needle, needleLength) == 0)
            {
                offset = index;
                break;
            }
        if (offset == text.Length())
            return false;

        containers::String replaced(source, offset);
        replaced.Append(replacement, replacementLength);
        const u32 suffix = offset + needleLength;
        replaced.Append(source + suffix, text.Length() - suffix);
        text = static_cast<containers::String&&>(replaced);
        return true;
    }

    [[nodiscard]] bool MakeP5AddedGeometrySource(const containers::String& base,
                                                 containers::String& output) noexcept
    {
        output = containers::String(base);
        constexpr char LastPrimitive[] =
            "{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":3,\"material\":1}]}],";
        constexpr char AddedGeometry[] =
            "{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":3,\"material\":1}]},"
            "{\"name\":\"Added\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":2,\"material\":2}]}],";
        constexpr char RootNode[] = "\"nodes\":[{\"name\":\"Root\",\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}]";
        constexpr char AddedNode[] =
            "\"nodes\":[{\"name\":\"Root\",\"mesh\":0,\"children\":[1]},{\"name\":\"Added\",\"mesh\":1}],\"scenes\":[{\"nodes\":[0]}]";
        constexpr char LastMaterialToMeshes[] = "}],\n            \"meshes\":[";
        constexpr char AddedSurfaceMaterial[] =
            "},{\"name\":\"AddedSurface\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.8,0.2,0.1,1]}}],\n            \"meshes\":[";
        // Keep the original single root and every original material use
        // unchanged. Adding a separately authored material plus VMESH makes
        // the schema expansion provably non-destructive under P1 provenance.
        return ReplaceOnce(output, LastPrimitive, AddedGeometry) && ReplaceOnce(output, RootNode, AddedNode) &&
               ReplaceOnce(output, LastMaterialToMeshes, AddedSurfaceMaterial);
    }

    [[nodiscard]] bool MakeP5RemovedMaskedMaterialSource(const containers::String& base,
                                                         containers::String& output) noexcept
    {
        output = containers::String(base);
        constexpr char MaskedMaterial[] =
            ",\n                {\"name\":\"Masked\",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.35,"
            "\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.3,0.6,0.9,0.8]}}";
        constexpr char MaskedPrimitive[] =
            ",\n                {\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1},\"indices\":3,\"material\":1}";
        return ReplaceOnce(output, MaskedMaterial, "") && ReplaceOnce(output, MaskedPrimitive, "");
    }

    [[nodiscard]] bool MakeP5NonUniformCompositionSource(const containers::String& base,
                                                         containers::String& output) noexcept
    {
        output = containers::String(base);
        constexpr char RootNode[] = "\"nodes\":[{\"name\":\"Root\",\"mesh\":0}]";
        constexpr char InvalidRoot[] = "\"nodes\":[{\"name\":\"Root\",\"mesh\":0,\"scale\":[1,2,1]}]";
        return ReplaceOnce(output, RootNode, InvalidRoot);
    }

    [[nodiscard]] bool ProveP4GeneratedComposition() noexcept
    {
        namespace tools = vanguard::mesh_tools;

        tools::ModelImportManifest manifest;
        manifest.geometryDefinitions.Resize(2u);
        manifest.geometryInstances.Resize(3u);
        manifest.nodes.Resize(3u);
        if (manifest.geometryDefinitions.Size() != 2u || manifest.geometryInstances.Size() != 3u || manifest.nodes.Size() != 3u)
            return false;

        manifest.nodes[0].sourceName = "Root";
        manifest.nodes[0].localTransform.values[3] = 1.0f;
        manifest.nodes[1].sourceName = "Single";
        manifest.nodes[1].parentIndex = 0u;
        manifest.nodes[1].localTransform.values[7] = 2.0f;
        manifest.nodes[1].geometryInstanceIndices.PushBack(0u);
        manifest.nodes[2].sourceName = "Many";
        manifest.nodes[2].parentIndex = 1u;
        manifest.nodes[2].localTransform.values[11] = 3.0f;
        manifest.nodes[2].geometryInstanceIndices.PushBack(1u);
        manifest.nodes[2].geometryInstanceIndices.PushBack(2u);
        manifest.geometryInstances[0] = {1u, 0u, 0u};
        manifest.geometryInstances[1] = {2u, 0u, 0u};
        manifest.geometryInstances[2] = {2u, 1u, 0u};

        tools::ModelImportSettings settings;
        settings.includeComposition = true;
        settings.sourceFingerprint = crypto::Sha256("p4-generated-composition", 24u);
        constexpr u64 GeometryPersistentIds[]{101u, 102u};
        constexpr u64 NodeStableIds[]{10u, 20u, 30u};
        constexpr u64 MeshStableIds[]{201u, 301u, 302u};
        for (u32 index = 0u; index < 2u; ++index)
        {
            tools::ModelImportOutputCorrespondence& geometry = settings.outputs.EmplaceBack();
            geometry.kind = tools::ModelImportElementKind::Geometry;
            geometry.persistentId = GeometryPersistentIds[index];
            geometry.output.key = containers::String::Printf("mesh-%u", index);
            geometry.output.type = meshes::MeshResourceType;
        }
        for (u32 index = 0u; index < 3u; ++index)
        {
            tools::ModelImportCompositionNodeCorrespondence& node = settings.compositionNodes.EmplaceBack();
            node.stableNodeId = NodeStableIds[index];
            node.parentStableNodeId = index == 0u ? 0u : NodeStableIds[index - 1u];
            node.sourceNodeIndex = index;
        }
        constexpr u64 MeshGeometryPersistentIds[]{GeometryPersistentIds[0], GeometryPersistentIds[0], GeometryPersistentIds[1]};
        for (u32 index = 0u; index < 3u; ++index)
        {
            tools::ModelImportCompositionMeshCorrespondence& mesh = settings.compositionMeshes.EmplaceBack();
            mesh.stableComponentId = MeshStableIds[index];
            mesh.stableNodeId = index == 0u ? NodeStableIds[1] : NodeStableIds[2];
            mesh.geometryPersistentId = MeshGeometryPersistentIds[index];
            mesh.sourceInstanceIndex = index;
            mesh.sameGeometryOrdinal = 0u;
        }

        tools::ModelImportOutputCorrespondence composition;
        composition.kind = tools::ModelImportElementKind::Composition;
        composition.persistentId = 900u;
        composition.output.key = "composition";
        composition.output.type = prefabs::PrefabResourceType;

        assets::SourceRecord source;
        if (!assets::GenerateAssetId(source.metadata.id))
            return false;
        constexpr u8 SourceBytes[]{'p', '4'};
        const resources::ResourceReference requested = assets::GetAssetOutputReference(source.metadata.id, composition.output);
        tools::ModelOutputBuildRequestContext composeContext;
        composeContext.manifest = &manifest;
        composeContext.settings = &settings;
        composeContext.correspondence = &composition;
        composeContext.source = &source;
        composeContext.data.content = {SourceBytes, static_cast<u32>(sizeof(SourceBytes))};
        composeContext.data.target = assets::TargetPlatform::WindowsD3D12;
        composeContext.requestedOutput = requested;

        assets::OwnedBuildRequest request;
        if (!nanovanguard::ComposeModelCompositionBuildRequest(composeContext, request))
            return false;
        const assets::BuildRequest built = request.View();
        if (!built.IsValid() || built.output != requested || !built.source.identity.IsTyped() ||
            built.source.identity.ExpectedType() != vanguard::serialization::MakeFourCC('V', 'P', 'B', 'I'))
            return false;

        filesystem::MemoryFileReader reader(built.source.content.Data(), built.source.content.SizeInBytes(), 0u);
        prefabs::PrefabFile prefab;
        if (prefab.Open(reader) != prefabs::Result::Success || prefab.GetEntities().Size() != 1u ||
            prefab.GetComponents().Size() != 5u || prefab.GetPlacedComponents().Size() != 5u || prefab.GetDependencies().Size() != 2u)
            return false;

        const prefabs::ComponentRecord* const root = prefab.FindComponent(NodeStableIds[0]);
        const prefabs::ComponentRecord* const single = prefab.FindComponent(MeshStableIds[0]);
        const prefabs::ComponentRecord* const many = prefab.FindComponent(NodeStableIds[2]);
        const prefabs::ComponentRecord* const manyMesh0 = prefab.FindComponent(MeshStableIds[1]);
        const prefabs::ComponentRecord* const manyMesh1 = prefab.FindComponent(MeshStableIds[2]);
        if (root == nullptr || single == nullptr || many == nullptr || manyMesh0 == nullptr || manyMesh1 == nullptr ||
            root->schema != entities::TransformComponentType || single->schema != entities::StaticMeshComponentType ||
            many->schema != entities::TransformComponentType || manyMesh0->schema != entities::StaticMeshComponentType ||
            manyMesh1->schema != entities::StaticMeshComponentType)
            return false;

        const prefabs::PlacedComponentRecord* const rootPlacement = prefab.FindPlacedComponent(NodeStableIds[0]);
        const prefabs::PlacedComponentRecord* const singlePlacement = prefab.FindPlacedComponent(MeshStableIds[0]);
        const prefabs::PlacedComponentRecord* const manyPlacement = prefab.FindPlacedComponent(NodeStableIds[2]);
        const prefabs::PlacedComponentRecord* const manyMesh0Placement = prefab.FindPlacedComponent(MeshStableIds[1]);
        const prefabs::PlacedComponentRecord* const manyMesh1Placement = prefab.FindPlacedComponent(MeshStableIds[2]);
        if (rootPlacement == nullptr || singlePlacement == nullptr || manyPlacement == nullptr || manyMesh0Placement == nullptr ||
            manyMesh1Placement == nullptr || rootPlacement->parentComponentStableId != prefabs::InvalidStableId ||
            singlePlacement->parentComponentStableId != NodeStableIds[0] || manyPlacement->parentComponentStableId != MeshStableIds[0] ||
            manyMesh0Placement->parentComponentStableId != NodeStableIds[2] || manyMesh1Placement->parentComponentStableId != NodeStableIds[2])
            return false;
        if (rootPlacement->translation[0] != 1.0f || singlePlacement->translation[1] != 2.0f || manyPlacement->translation[2] != 3.0f ||
            manyMesh0Placement->translation[0] != 0.0f || manyMesh0Placement->translation[1] != 0.0f || manyMesh0Placement->translation[2] != 0.0f ||
            manyMesh1Placement->translation[0] != 0.0f || manyMesh1Placement->translation[1] != 0.0f || manyMesh1Placement->translation[2] != 0.0f)
            return false;

        const auto decodeMesh = [&prefab](const prefabs::ComponentRecord& component, entities::StaticMeshComponentData& data) noexcept
        {
            const containers::ArraySpan<const u8> bytes = prefab.GetComponentData(component);
            filesystem::MemoryFileReader componentReader(bytes.Data(), bytes.SizeInBytes(), 0u);
            vanguard::serialization::BinaryReader binary(componentReader);
            return schemas::ReadObject(binary, entities::GetStaticMeshComponentSchema(), &data) == schemas::Result::Success;
        };
        entities::StaticMeshComponentData decodedSingle;
        entities::StaticMeshComponentData decodedMany0;
        entities::StaticMeshComponentData decodedMany1;
        const resources::ResourceReference expected0 = assets::GetAssetOutputReference(source.metadata.id, settings.outputs[0].output);
        const resources::ResourceReference expected1 = assets::GetAssetOutputReference(source.metadata.id, settings.outputs[1].output);
        if (!decodeMesh(*single, decodedSingle) || !decodeMesh(*manyMesh0, decodedMany0) || !decodeMesh(*manyMesh1, decodedMany1) ||
            decodedSingle.mesh != expected0 || decodedMany0.mesh != expected0 || decodedMany1.mesh != expected1)
            return false;
        bool dependencies[2]{};
        for (const prefabs::DependencyRecord& dependency : prefab.GetDependencies())
        {
            dependencies[0] |= dependency.resource == expected0 && dependency.kind == resources::DependencyKind::Required;
            dependencies[1] |= dependency.resource == expected1 && dependency.kind == resources::DependencyKind::Required;
        }
        if (!dependencies[0] || !dependencies[1])
            return false;

        assets::OwnedBuildRequest duplicate;
        if (!nanovanguard::ComposeModelCompositionBuildRequest(composeContext, duplicate))
            return false;
        const assets::BuildRequest duplicateView = duplicate.View();
        if (duplicateView.source.content.SizeInBytes() != built.source.content.SizeInBytes() ||
            std::memcmp(duplicateView.source.content.Data(), built.source.content.Data(), built.source.content.SizeInBytes()) != 0)
            return false;

        manifest.nodes[0].flags = tools::ModelImportNode_NonUniformScale;
        assets::OwnedBuildRequest rejected;
        return !nanovanguard::ComposeModelCompositionBuildRequest(composeContext, rejected);
    }

    [[nodiscard]] bool AuthorP3ModelFixture(filesystem::Manager& files,
                                            const projects::ProjectWorkspace& workspace,
                                            P3ModelFixture& fixture) noexcept
    {
        namespace mt = vanguard::material_tools;
        namespace tools = vanguard::mesh_tools;
        constexpr char model[] = R"json({
            "asset":{"version":"2.0"},
            "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAwAEAAUA","byteLength":132}],
            "bufferViews":[
                {"buffer":0,"byteOffset":0,"byteLength":72,"target":34962},
                {"buffer":0,"byteOffset":72,"byteLength":48,"target":34962},
                {"buffer":0,"byteOffset":120,"byteLength":6,"target":34963},
                {"buffer":0,"byteOffset":126,"byteLength":6,"target":34963}],
            "accessors":[
                {"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
                {"bufferView":1,"componentType":5126,"count":6,"type":"VEC2"},
                {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"},
                {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
            "images":[
                {"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP4z8DwHwAFAAH/VscvDQAAAABJRU5ErkJggg=="},
                {"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYPj/HwADAgH/5ncLrgAAAABJRU5ErkJggg=="}],
            "textures":[{"source":0},{"source":1}],
            "materials":[
                {"name":"Body","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1},"metallicFactor":0.7,"roughnessFactor":0.4}},
                {"name":"Masked","alphaMode":"MASK","alphaCutoff":0.35,"pbrMetallicRoughness":{"baseColorFactor":[0.3,0.6,0.9,0.8]}}],
            "meshes":[{"name":"Compound","primitives":[
                {"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0},
                {"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":3,"material":1}]}],
            "nodes":[{"name":"Root","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
        })json";

        if (!mesh_tools::Initialize())
            return false;
        tools::ModelImportManifest manifest;
        tools::ModelImportReport report;
        const tools::ModelImportResult inspect =
            tools::InspectModelMemory(model, sizeof(model) - 1u, "gltf", "Models/p3_compound.gltf", {}, {}, manifest, &report);
        if (inspect != tools::ModelImportResult::Success || manifest.geometryDefinitions.Size() != 1u ||
            manifest.materials.Size() < 2u || manifest.materials.Size() > 4u || manifest.embeddedTextures.Size() != 2u)
        {
            std::fprintf(stderr,
                         "[nanovanguardProjectAssetTests] P3 fixture inspect=%u geometry=%u materials=%u embedded=%u refs=%u deps=%u diagnostics=%u\n",
                         static_cast<u32>(inspect), manifest.geometryDefinitions.Size(), manifest.materials.Size(),
                         manifest.embeddedTextures.Size(), manifest.textureReferences.Size(), manifest.dependencies.Size(), manifest.diagnostics.Size());
            return false;
        }

        const filesystem::AbsolutePath modelDirectory = workspace.assets.AddDirPath("Models");
        fixture.source = modelDirectory.AddFilePath("p3_compound.gltf");
        fixture.sourceText = containers::String(model);
        if (!files.CreatePath(modelDirectory) ||
            !nanovanguard::platform::WriteFileDurable(
                fixture.source.AsStringView(),
                {reinterpret_cast<const u8*>(model), static_cast<u32>(sizeof(model) - 1u)}))
            return false;

        if (!assets::GenerateAssetId(fixture.metadata.id))
            return false;
        const assets::SourceImporterId importer = assets::HashCompilerName("vanguard.model.importer");
        tools::ModelImportSettings candidate;
        candidate.materialPolicy = tools::ModelImportMaterialPolicy::ImportModelOwned;
        candidate.embeddedTexturePolicy = tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned;
        candidate.materialMapperVersion = mt::ImportedSurfaceMaterialMapperVersion;
        candidate.includeComposition = true;
        tools::ModelImportPlanningContext context;
        context.parentAsset = fixture.metadata.id;
        context.importer = importer;
        context.importerVersion = tools::ModelSourceImporterVersion;
        context.acceptedStateGeneration = 1u;
        context.sourceOrigin = fixture.source;
        context.sourceRoot = workspace.assets;
        context.sourceDestination = fixture.source;
        context.sourceIdentity = resources::ResourcePath::FromString("Models/p3_compound.gltf");
        context.outputTypes.mesh = meshes::MeshResourceType;
        context.outputTypes.material = materials::MaterialResourceType;
        context.outputTypes.texture = textures::TextureResourceType;
        context.outputTypes.composition = prefabs::PrefabResourceType;
        tools::ModelImportPlan plan;
        tools::ModelImportPlanReport planReport;
        const tools::ModelImportPlanResult planned = tools::PlanModelImport(manifest, candidate, context, plan, &planReport);
        if (planned != tools::ModelImportPlanResult::Success || plan.HasBlockingConflicts())
        {
            std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 fixture plan=%u blockers=%u diagnostic=%s\n",
                         static_cast<u32>(planned), plan.GetBlockers().Size(), planReport.diagnostic.AsChar());
            return false;
        }

        fixture.metadata.importer = importer;
        fixture.metadata.importerVersion = tools::ModelSourceImporterVersion;
        fixture.metadata.settingsVersion = plan.GetImportDescription().settingsVersion;
        fixture.metadata.settings = containers::DynamicArray<u8>(plan.GetImportDescription().settings);
        for (const assets::AssetOutput& output : plan.GetImportDescription().outputs)
        {
            assets::AssetOutput& copied = fixture.metadata.outputs.EmplaceBack();
            copied.key = output.key;
            copied.type = output.type;
            fixture.meshes += output.type == meshes::MeshResourceType ? 1u : 0u;
            fixture.materials += output.type == materials::MaterialResourceType ? 1u : 0u;
            fixture.textures += output.type == textures::TextureResourceType ? 1u : 0u;
            fixture.compositions += output.type == prefabs::PrefabResourceType ? 1u : 0u;
        }
        if (fixture.meshes != 1u || fixture.materials != manifest.materials.Size() || fixture.materials < 2u || fixture.materials > 4u ||
            fixture.textures != 2u || fixture.compositions != 1u)
            return false;

        containers::String encoded;
        return assets::EncodeMetadata(fixture.metadata, encoded) == assets::MetadataResult::Success &&
               nanovanguard::platform::WriteFileDurable(
                   MetadataPath(fixture.source).AsStringView(),
                   {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
    }

    [[nodiscard]] bool AuthorTextureFixture(const filesystem::AbsolutePath& root,
                                            const char* const fileName = "red.png") noexcept
    {
        namespace nano = vanguard::nanovanguard;
        namespace tt = vanguard::texture_tools;
        constexpr u8 redPng[]{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                              0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
                              0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf,
                              0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00,
                              0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath(fileName);
        if (!nano::platform::WriteFileDurable(source.AsStringView(), {redPng, static_cast<u32>(sizeof(redPng))}))
            return false;

        containers::DynamicArray<u8> settings(memory::pools::Assets::GetInstance());
        if (!tt::IsInitialized() && !tt::Initialize())
            return false;
        tt::TextureBuildDescription description;
        description.sourceMode = tt::TextureBuildSourceMode::Image2D;
        description.colorSpace = tt::ImportedColorSpace::SRgb;
        description.profile = tt::profiles::Ui;
        if (tt::EncodeTextureBuildSettings(description, settings) != tt::TextureBuildSettingsResult::Success)
            return false;

        assets::AssetMetadata metadata;
        if (!assets::GenerateAssetId(metadata.id))
            return false;
        metadata.importer = assets::HashCompilerName("vanguard.texture.compiler");
        metadata.importerVersion = tt::TextureAssetCompilerVersion;
        metadata.settingsVersion = 1u;
        for (const u8 byte : settings)
            metadata.settings.PushBack(byte);
        assets::AssetOutput assetOutput;
        assetOutput.key = "main";
        assetOutput.type = textures::TextureResourceType;
        metadata.outputs.PushBack(static_cast<assets::AssetOutput&&>(assetOutput));
        containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        return nano::platform::WriteFileDurable(MetadataPath(source).AsStringView(),
                                                {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
    }

    [[nodiscard]] bool RewriteTextureSourceBlue(const filesystem::AbsolutePath& source) noexcept
    {
        // Independent valid 1x1 RGBA PNG. Keeping the metadata untouched makes
        // the P5 source-change proof isolate source-content drift.
        constexpr u8 bluePng[]{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                               0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
                               0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60,
                               0xf8, 0xff, 0x1f, 0x00, 0x03, 0x02, 0x01, 0xff, 0xe6, 0x77, 0x0b, 0xae, 0x00, 0x00, 0x00, 0x00,
                               0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        auto writer = filesystem::GetManager().CreateFileWriter(source);
        if (!writer)
            return false;
        writer->Serialize(const_cast<u8*>(bluePng), sizeof(bluePng));
        writer->Flush();
        return !writer->HasErrors();
    }

    [[nodiscard]] bool RewriteTextureProfile(const filesystem::AbsolutePath& source,
                                             const texture_tools::TextureCookingProfileId profile) noexcept
    {
        assets::AssetMetadata metadata;
        if (!ReadAssetMetadata(source, metadata))
            return false;
        containers::DynamicArray<u8> settings(memory::pools::Assets::GetInstance());
        texture_tools::TextureBuildDescription description;
        description.sourceMode = texture_tools::TextureBuildSourceMode::Image2D;
        description.colorSpace = texture_tools::ImportedColorSpace::SRgb;
        description.profile = profile;
        if (texture_tools::EncodeTextureBuildSettings(description, settings) != texture_tools::TextureBuildSettingsResult::Success)
            return false;
        metadata.settings.Clear();
        for (const u8 byte : settings)
            metadata.settings.PushBack(byte);
        containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        auto writer = filesystem::GetManager().CreateFileWriter(MetadataPath(source));
        if (!writer)
            return false;
        writer->Serialize(encoded.AsChar(), encoded.Length());
        writer->Flush();
        return !writer->HasErrors();
    }

    [[nodiscard]] bool ReadAssetMetadata(const filesystem::AbsolutePath& source,
                                         assets::AssetMetadata& metadata) noexcept
    {
        containers::String text;
        return nanovanguard::platform::ReadFile(MetadataPath(source).AsStringView(), text) &&
               assets::ParseMetadata(text, metadata) == assets::MetadataResult::Success;
    }

    [[nodiscard]] bool AppearsBefore(const char* const text, const char* const first,
                                     const char* const second) noexcept
    {
        const char* const firstPosition = std::strstr(text, first);
        const char* const secondPosition = std::strstr(text, second);
        return firstPosition != nullptr && secondPosition != nullptr && firstPosition < secondPosition;
    }

    struct P5CompilerProbe final
    {
        assets::CompilerDescriptor delegate;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<bool> block;
        concurrency::Atomic<bool> started;
        concurrency::Atomic<bool> release;

        [[nodiscard]] static bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies,
                                           void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            return probe.delegate.discoverDependencies != nullptr &&
                   probe.delegate.discoverDependencies(request, dependencies, probe.delegate.userData);
        }

        [[nodiscard]] static bool Estimate(const assets::BuildRequest& request,
                                           const containers::ArraySpan<const assets::BuildDependency> dependencies,
                                           assets::BuildResourceEstimate& estimate, void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            return probe.delegate.estimateResources != nullptr &&
                   probe.delegate.estimateResources(request, dependencies, estimate, probe.delegate.userData);
        }

        [[nodiscard]] static bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& artifacts,
                                          void* const userData) noexcept
        {
            auto& probe = *static_cast<P5CompilerProbe*>(userData);
            static_cast<void>(probe.compileCalls.Increment());
            probe.started.SetValue(true);
            while (probe.block.GetValue() && !probe.release.GetValue() && !context.IsCancellationRequested())
                concurrency::YieldCurrentThread();
            if (context.IsCancellationRequested())
                return false;
            return probe.delegate.compile != nullptr && probe.delegate.compile(context, artifacts, probe.delegate.userData);
        }

        [[nodiscard]] assets::CompilerDescriptor Wrap(const assets::CompilerDescriptor& descriptor) noexcept
        {
            delegate = descriptor;
            assets::CompilerDescriptor wrapped = descriptor;
            wrapped.discoverDependencies = &Discover;
            wrapped.compile = &Compile;
            wrapped.estimateResources = &Estimate;
            wrapped.userData = this;
            return wrapped;
        }
    };

    // Test-only mirror of ProjectAssetContext's healthy GPU-free ownership graph.
    // The forwarding compiler descriptor lets P5 count/barrier the real
    // TextureAssetCompiler without adding product API.
    class P5ProjectAssetHarness final
    {
    public:
        [[nodiscard]] bool Open(const projects::ProjectWorkspace& projectWorkspace, const bool dependencyDrift = false) noexcept
        {
            if (open || retainedAfterUnsafeShutdown ||
                !asset_streaming::MakeProjectAssetPipelineConfig(projectWorkspace.derivedData, projectWorkspace.intermediate,
                                                                          "windows-x64", config))
                return false;

            if (!diagnostics::IsInitialized())
            {
                if (!diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguard-p5"))
                    return false;
                ownsDiagnostics = true;
            }
            if (!vanguard::io::IsInitialized())
            {
                if (!vanguard::io::Initialize())
                    return false;
                ownsIo = true;
            }
            if (!reflection::IsInitialized() && !reflection::Initialize())
                return false;
            const filesystem::Config filesystemConfig{projectWorkspace.root, projectWorkspace.root, projectWorkspace.derivedData};
            if (!filesystem::IsInitialized())
            {
                if (!filesystem::Initialize(filesystemConfig))
                    return false;
                ownsFilesystem = true;
            }
            else
            {
                filesystem::Manager& inherited = filesystem::GetManager();
                if (inherited.GetEngineRoot() != filesystemConfig.engineRoot || inherited.GetGameRoot() != filesystemConfig.gameRoot ||
                    inherited.GetCacheDirectory() != filesystemConfig.cacheRoot)
                    return false;
            }
            if (!jobs::IsInitialized())
            {
                if (!jobs::Initialize(jobs::ToolConfig()))
                    return false;
                ownsJobs = true;
            }
            if (!texture_tools::IsInitialized() && !texture_tools::Initialize())
                return false;
            if (dependencyDrift)
            {
                const texture_tools::TextureCookingProfile* const color = texture_tools::FindCookingProfile(texture_tools::profiles::Color);
                if (color == nullptr)
                    return false;
                texture_tools::TextureCookingProfile extra = *color;
                extra.id = 0x7035657874726101ull; // "p5extra" + version tag.
                if (texture_tools::RegisterCookingProfile(extra) != texture_tools::ProfileRegistrationResult::Success)
                    return false;
            }
            if (!compiler.Initialize())
                return false;

            filesystem::Manager& files = filesystem::GetManager();
            const assets::SourceRoot sourceRoot{projectWorkspace.assets, false};
            if (sources.Rescan(files, {&sourceRoot, 1u}) != assets::SourceDatabaseResult::Success ||
                !files.CreatePath(projectWorkspace.derivedData.AddDirPath("ProjectAssets")) ||
                !files.CreatePath(projectWorkspace.intermediate.AddDirPath("ProjectAssets")) || !registry.Initialize() ||
                !resourcePipeline.Initialize(registry) || !streamer.Initialize(resourcePipeline))
                return false;

            const assets::CompilerDescriptor wrappedCompiler = probe.Wrap(compiler.GetDescriptor());
            const assets::SourceImporterDescriptor importer = compiler.GetImporterDescriptor();
            asset_streaming::ProjectAssetPipelineFailure failure;
            const asset_streaming::ProjectAssetPipelineResult result =
                pipeline.Initialize(files, sources, streamer, {&wrappedCompiler, 1u}, {&importer, 1u}, config, &startupRestore, &failure);
            if (result != asset_streaming::ProjectAssetPipelineResult::Success)
                return false;
            open = true;
            return true;
        }

        [[nodiscard]] bool Close() noexcept
        {
            if (retainedAfterUnsafeShutdown)
                return false;
            if (pipeline.IsInitialized() && pipeline.Shutdown() != asset_streaming::ProjectAssetPipelineResult::Success)
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (compiler.IsInitialized() && !compiler.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (streamer.IsInitialized() && !streamer.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (resourcePipeline.IsInitialized() && !resourcePipeline.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (registry.IsInitialized() && !registry.Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (ownsJobs && jobs::IsInitialized() && !jobs::Shutdown())
            {
                retainedAfterUnsafeShutdown = true;
                return false;
            }
            if (ownsFilesystem && filesystem::IsInitialized())
                filesystem::Shutdown();
            if (ownsIo && vanguard::io::IsInitialized())
                vanguard::io::Shutdown();
            if (ownsDiagnostics && diagnostics::IsInitialized())
                diagnostics::Shutdown();
            open = false;
            return true;
        }

        ~P5ProjectAssetHarness() { static_cast<void>(Close()); }

        asset_streaming::ProjectAssetPipelineConfig config;
        P5CompilerProbe probe;
        texture_tools::TextureAssetCompiler compiler;
        assets::SourceDatabase sources;
        resources::ResourceRegistry registry;
        resources::ResourcePipeline resourcePipeline;
        streaming::ResourceStreamer streamer;
        asset_streaming::ProjectAssetPipeline pipeline;
        asset_streaming::ProjectAssetRestoreReport startupRestore;
        bool ownsDiagnostics = false;
        bool ownsIo = false;
        bool ownsFilesystem = false;
        bool ownsJobs = false;
        bool open = false;
        bool retainedAfterUnsafeShutdown = false;
    };

    [[nodiscard]] bool CloseAndDestroyP5Harness(P5ProjectAssetHarness*& harness) noexcept
    {
        if (harness == nullptr || !harness->Close())
            return false;
        VANGUARD_DELETE(harness);
        harness = nullptr;
        return true;
    }

    [[nodiscard]] bool RenderEquivalentTerminal(const nanovanguard::SemanticEventKind kind,
                                                const char* const operation, const char* const reason) noexcept
    {
        Buffer human;
        Buffer json;
        nanovanguard::Output humanOutput(&Capture, &human);
        nanovanguard::Output jsonOutput(&Capture, &json);
        nanovanguard::SemanticEvent event;
        event.event = kind;
        event.operation = operation;
        if (!event.AddString("reason", reason) ||
            !nanovanguard::WriteSemanticEvent(humanOutput, nanovanguard::OutputFormat::Human, event) ||
            !nanovanguard::WriteSemanticEvent(jsonOutput, nanovanguard::OutputFormat::JsonLines, event))
            return false;
        const char* const token = nanovanguard::SemanticEventToken(kind);
        return Contains(human.data, operation) && Contains(human.data, token) && Contains(human.data, reason) &&
               Contains(json.data, operation) && Contains(json.data, token) && Contains(json.data, reason) &&
               Contains(json.data, "\"schema\":1");
    }

    [[nodiscard]] bool CreateCommandFixture(nanovanguard::CommandRegistry& registry, nanovanguard::Output& output,
                                            Buffer& buffer, const filesystem::AbsolutePath& root,
                                            const bool multipleTargets = false) noexcept
    {
        if (!nanovanguard::RegisterBuiltinCommands(registry) || !nanovanguard::RegisterProjectAssetCommands(registry))
            return false;
        buffer = {};
        const char* single[]{"nanovanguard", "project", "create", "P3 Fixture", "--destination", root.AsChar(), "--target", "windows-x64"};
        const char* multiple[]{"nanovanguard", "project", "create", "P3 Fixture", "--destination", root.AsChar(), "--target", "windows-x64",
                               "--target", "linux-x64"};
        const nanovanguard::ExitCode created = multipleTargets ? registry.Dispatch(10, multiple, output) : registry.Dispatch(8, single, output);
        return created == nanovanguard::ExitCode::Success && (!multipleTargets ? AuthorTextureFixture(root) : true);
    }

    class ManualObservationSource final : public filesystem::DirectoryObservationSource
    {
    public:
        [[nodiscard]] bool Start(const containers::ArraySpan<const filesystem::AbsolutePath> roots,
                                 const filesystem::DirectoryObservationCallback callback, void* const userData,
                                 const filesystem::DirectoryObserverConfig&) noexcept override
        {
            if (m_running)
                return false;
            m_rootCount = roots.Count();
            m_callback = callback;
            m_userData = userData;
            m_running = true;
            return true;
        }

        [[nodiscard]] bool Stop() noexcept override
        {
            m_running = false;
            m_callback = nullptr;
            m_userData = nullptr;
            return true;
        }

        [[nodiscard]] bool IsRunning() const noexcept override { return m_running; }

        void Emit(const filesystem::DirectoryObservationKind kind, const filesystem::AbsolutePath& path) noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.path = path;
            observation.kind = kind;
            m_callback(observation, m_userData);
        }

        void EmitOverflow() noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.kind = filesystem::DirectoryObservationKind::Overflow;
            m_callback(observation, m_userData);
        }

        u32 m_rootCount = 0u;

    private:
        filesystem::DirectoryObservationCallback m_callback = nullptr;
        void* m_userData = nullptr;
        bool m_running = false;
    };

    [[nodiscard]] bool IsAssetCommandMode(const char* const mode) noexcept
    {
        return mode != nullptr && (std::strcmp(mode, "asset-status") == 0 || std::strcmp(mode, "asset-rescan") == 0 ||
                                   std::strcmp(mode, "asset-build") == 0 || std::strcmp(mode, "asset-reimport") == 0 ||
                                   std::strcmp(mode, "asset-watch-cancel") == 0 || std::strcmp(mode, "asset-invalid-source") == 0 ||
                                   std::strcmp(mode, "asset-output-key") == 0 || std::strcmp(mode, "asset-target-ambiguous") == 0 ||
                                   std::strcmp(mode, "asset-target-undeclared") == 0 || std::strcmp(mode, "asset-target-unsupported") == 0 ||
                                   std::strcmp(mode, "asset-status-diagnostics") == 0 || std::strcmp(mode, "asset-status-freshness-failure") == 0 ||
                                   std::strcmp(mode, "asset-reimport-failure") == 0 || std::strcmp(mode, "asset-watch-failure") == 0 ||
                                   std::strcmp(mode, "asset-watch-active-cancel") == 0 ||
                                   std::strcmp(mode, "watch-overflow") == 0 ||
                                   std::strcmp(mode, "asset-maintain-vddc") == 0 || std::strcmp(mode, "asset-maintain-loose") == 0 ||
                                   std::strcmp(mode, "asset-maintain-all") == 0 || std::strcmp(mode, "asset-maintain-cancel") == 0 ||
                                   std::strcmp(mode, "asset-maintain-invalid") == 0 ||
                                   std::strcmp(mode, "asset-move") == 0 || std::strcmp(mode, "asset-move-restart-prepare") == 0 ||
                                   std::strcmp(mode, "asset-move-restart-verify") == 0 || std::strcmp(mode, "asset-move-conflict") == 0 ||
                                   std::strcmp(mode, "asset-duplicate") == 0 ||
                                   std::strcmp(mode, "asset-retarget") == 0 || std::strcmp(mode, "asset-retarget-invalid") == 0 ||
                                   std::strcmp(mode, "asset-delete-reject") == 0 || std::strcmp(mode, "asset-delete-replace") == 0 ||
                                   std::strcmp(mode, "asset-delete-invalid-policy") == 0 || std::strcmp(mode, "asset-p4-help") == 0 ||
                                   std::strcmp(mode, "asset-p5-cold-matrix") == 0 ||
                                   std::strcmp(mode, "asset-p5-restart-prepare") == 0 ||
                                   std::strcmp(mode, "asset-p5-restart-verify") == 0 ||
                                   std::strcmp(mode, "asset-p5-dependency-change") == 0 ||
                                   std::strcmp(mode, "asset-p5-active-ownership") == 0 ||
                                   std::strcmp(mode, "asset-p5-protocol-equivalence") == 0);
    }
} // namespace

int main(const int argc, const char* const* const argv)
{
    using namespace vanguard;
    namespace nano = vanguard::nanovanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(containers::Initialize(), "containers initialization");

    const filesystem::AbsolutePath scratch = tests::ScratchDirectory("nanovanguard_project_asset_context_tests");
    const filesystem::AbsolutePath root = scratch.AddDirPath("project");
    const filesystem::AbsolutePath alternateRoot = scratch.AddDirPath("alternate");
    const projects::ProjectWorkspace workspace = MakeWorkspace(root);
    const bool rollbackMode = argc == 2 && std::strcmp(argv[1], "rollback") == 0;
    const bool inheritedMode = argc == 2 && std::strcmp(argv[1], "inherited") == 0;
    const bool p3ModelMode = argc == 2 && std::strcmp(argv[1], "p3-model") == 0;
    const bool successMode = argc == 1 || (argc == 2 && std::strcmp(argv[1], "success") == 0);
    const bool assetCommandMode = argc == 2 && IsAssetCommandMode(argv[1]);
    const bool moveRestartPrepare = argc == 2 && std::strcmp(argv[1], "asset-move-restart-prepare") == 0;
    const bool moveRestartVerify = argc == 2 && std::strcmp(argv[1], "asset-move-restart-verify") == 0;
    const bool p5RestartPrepare = argc == 2 && std::strcmp(argv[1], "asset-p5-restart-prepare") == 0;
    const bool p5RestartVerify = argc == 2 && std::strcmp(argv[1], "asset-p5-restart-verify") == 0;
    const bool p5DependencyChange = argc == 2 && std::strcmp(argv[1], "asset-p5-dependency-change") == 0;
    Check(successMode || rollbackMode || inheritedMode || p3ModelMode || assetCommandMode, "mode is supported");

    filesystem::Manager setupFiles(scratch, scratch, scratch);
    if (!moveRestartVerify && !p5RestartVerify && !p5DependencyChange && DirectoryExists(setupFiles, root))
        Check(DeleteDirectoryTree(setupFiles, root), "previous project fixture cleanup");
    if (DirectoryExists(setupFiles, alternateRoot))
        Check(DeleteDirectoryTree(setupFiles, alternateRoot), "previous alternate fixture cleanup");

    Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized() &&
              !texture_tools::IsInitialized() && !mesh_tools::IsInitialized(),
          "bootstrap facilities begin unowned");

    if (assetCommandMode)
    {
        namespace nano = vanguard::nanovanguard;
        const bool ambiguousTarget = std::strcmp(argv[1], "asset-target-ambiguous") == 0;
        const bool unsupportedTarget = std::strcmp(argv[1], "asset-target-unsupported") == 0;
        nano::CommandRegistry registry;
        Buffer buffer;
        nano::Output output(&Capture, &buffer);
        const bool p5ProtocolEquivalence = std::strcmp(argv[1], "asset-p5-protocol-equivalence") == 0;
        if (!moveRestartVerify && !p5RestartVerify && !p5DependencyChange && !p5ProtocolEquivalence)
            Check(CreateCommandFixture(registry, output, buffer, root, ambiguousTarget || unsupportedTarget), "P3 command fixture creation");
        const bool needsReplacementFixture = std::strcmp(argv[1], "asset-retarget") == 0 ||
                                             std::strcmp(argv[1], "asset-delete-replace") == 0;
        if (needsReplacementFixture)
            Check(AuthorTextureFixture(root, "blue.png"), "P4 replacement texture fixture creation");

        if (moveRestartVerify)
        {
            const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("moved.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(moved, persisted), "restart verification reads moved identity from prior process");
            nano::ProjectAssetContext context;
            nano::ProjectAssetContextFailure failure;
            Check(context.Open(workspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::Success,
                  "fresh process context opens after asset move");
            if (context.IsOpen() && context.GetPipeline() != nullptr && context.GetSources() != nullptr)
            {
                const assets::SourceRecord* const sourceRecord = context.GetSources()->LookupAsset(persisted.id);
                const asset_streaming::ProjectAssetPipeline& pipeline = *context.GetPipeline();
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(persisted.id);
                Check(sourceRecord != nullptr && sourceRecord->path == moved && accepted != nullptr &&
                          accepted->asset == persisted.id && accepted->physicalPath == moved &&
                          accepted->sourceIdentity == sourceRecord->sourceIdentity,
                      "fresh process reconstructs moved current source and accepted identity");
                Check(pipeline.GetUndoAssetOperationCount() == 0u && pipeline.GetRedoAssetOperationCount() == 0u,
                      "fresh process does not replay session-local mutation history");
                const asset_streaming::ProjectAssetVddcMaintenanceStatus vddc = pipeline.GetVddcMaintenanceStatus();
                const asset_streaming::ProjectAssetLooseMaintenanceStatus loose = pipeline.GetLooseMaintenanceStatus();
                Check(vddc.state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle && !vddc.retryRequired &&
                          loose.state == asset_streaming::ProjectAssetLooseMaintenanceState::Idle && !loose.retryRequired,
                      "fresh process does not replay transient maintenance state");
            }
            Check(context.Close() == nano::ProjectAssetContextResult::Success,
                  "fresh process restart verification closes cleanly");
        }
        else if (p5RestartVerify)
        {
            // E2.6 P5 row 2. A fresh process wraps the real compiler before any
            // build-system registration. Zero callback entries therefore proves
            // the warm request was satisfied by durable cache truth, not memory
            // state or a hidden compiler fallback.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(source, persisted), "P5 restart verification reads persisted production texture identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace),
                  "P5 fresh process opens the persisted production-texture project");
            if (harness != nullptr && harness->open)
            {
                asset_streaming::ProjectAssetPipeline& pipeline = harness->pipeline;
                Check(harness->probe.compileCalls.GetValue() == 0u,
                      "P5 clean restart performs zero production compiler calls during restore");
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(persisted.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 restart restores the accepted production-texture output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 clean restart restores committed VADI as UpToDate before a new request");
                    asset_streaming::ProjectAssetOutputDeliveryStatus restored;
                    Check(pipeline.GetOutputDeliveryStatus(outputReference, restored) &&
                              restored.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                              restored.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                              restored.committedBuildFingerprint == restored.publishedBuildFingerprint &&
                              restored.committedContentFingerprint == restored.publishedContentFingerprint &&
                              setupFiles.FileExist(restored.publishedPhysicalPath),
                          "P5 clean restart restores exact last-valid loose publication evidence");

                    asset_streaming::ProjectAssetRequest request = pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 clean restart accepts an exact warm build request");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 clean restart completes the exact warm request");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 0u &&
                              pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 exact warm request is a zero-compiler persistent hit and leaves authoritative VADI UpToDate");
                }
            }
            Check(CloseAndDestroyP5Harness(harness),
                  "P5 clean-restart verification closes cleanly");
        }
        else if (p5DependencyChange)
        {
            // The prepare/restart pair leaves a healthy persisted texture source. A
            // fresh process adds one otherwise-unused valid cooking profile before
            // TextureAssetCompiler freezes configuration. The real compiler's
            // generated Tool dependency fingerprints the registry, so this is a
            // production dependency drift without modifying product code.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata persisted;
            Check(ReadAssetMetadata(source, persisted), "P5 dependency-drift verification reads persisted identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace, true), "P5 dependency-drift process opens with one benign profile addition");
            if (harness != nullptr && harness->open)
            {
                const asset_streaming::ProjectAssetState* const accepted = harness->pipeline.LookupAcceptedAsset(persisted.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 dependency-drift process restores the production output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(harness->pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::DependenciesChanged,
                          "P5 real texture Tool dependency drift evaluates DependenciesChanged");
                    asset_streaming::ProjectAssetRequest request = harness->pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 dependency-drift rebuild is accepted");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 dependency-drift rebuild publishes through the shared owner");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 1u &&
                              harness->pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 dependency drift invokes the production compiler exactly once and restores UpToDate");
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 dependency-drift process closes cleanly");
        }
        else if (ambiguousTarget)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(6, command, output);
            Check(result == nano::ExitCode::InvalidProject, "ambiguous project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "ambiguous-project-target"),
                  "ambiguous target has exactly one semantic failed terminal");
        }
        else if (std::strcmp(argv[1], "asset-target-undeclared") == 0)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--target", "linux-x64", "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(8, command, output);
            Check(result == nano::ExitCode::InvalidProject, "explicit undeclared project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "undeclared-project-target") &&
                      !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                  "explicit undeclared target has exactly one semantic failed terminal");
        }
        else if (unsupportedTarget)
        {
            buffer = {};
            const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--target", "linux-x64", "--format", "jsonl"};
            const nano::ExitCode result = registry.Dispatch(8, command, output);
            Check(result == nano::ExitCode::InvalidProject, "explicit declared unsupported project target fails with exit 10");
            Check(Count(buffer.data, "\"event\":\"failed\"") == 1u && Contains(buffer.data, "unsupported-project-target") &&
                      !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                  "explicit declared unsupported target has exactly one semantic failed terminal");
        }
        else if (std::strcmp(argv[1], "watch-overflow") == 0)
        {
            nano::ProjectAssetContext context;
            nano::ProjectAssetContextFailure failure;
            Check(context.Open(MakeWorkspace(root), "windows-x64", &failure) == nano::ProjectAssetContextResult::Success,
                  "watch overflow context opens");
            if (context.IsOpen())
            {
                asset_streaming::ProjectAssetPipeline* const pipeline = context.GetPipeline();
                ManualObservationSource observation;
                asset_streaming::ProjectAssetObservationConfig config;
                config.maximumPendingPaths = 4u;
                config.maximumPathsPerTick = 1u;
                config.maximumReconcileOperationsPerTick = 1u;
                config.settleTicks = 2u;
                config.maximumDeferralTicks = 4u;
                Check(pipeline != nullptr && pipeline->EnableObservation(observation, config) == asset_streaming::ProjectAssetPipelineResult::Success,
                      "headless watch uses injected production observation owner");
                bool startupDrained = false;
                for (u32 tick = 0u; tick < 200000u && !startupDrained; ++tick)
                {
                    const asset_streaming::ProjectAssetPipelineResult observed = pipeline->TickObservation();
                    startupDrained = observed == asset_streaming::ProjectAssetPipelineResult::Success;
                    Check(observed == asset_streaming::ProjectAssetPipelineResult::Success || observed == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "headless watch startup catch-up remains bounded");
                    if (!startupDrained)
                        concurrency::YieldCurrentThread();
                }
                Check(startupDrained, "headless watch drains generation-zero catch-up before automatic recook admission");
                Check(pipeline->EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "headless watch enables existing automatic recook coordinator");
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                for (u32 index = 0u; index < 32u; ++index)
                    observation.Emit(filesystem::DirectoryObservationKind::Modified, source);
                const asset_streaming::ProjectAssetObservationStatus storm = pipeline->GetObservationStatus();
                Check(storm.pendingPaths <= config.maximumPendingPaths && storm.hintsReceived >= 32u && storm.hintsCoalesced != 0u,
                      "event storm remains bounded and coalesces duplicate paths");
                observation.EmitOverflow();
                bool observationDrained = false;
                bool automaticTerminal = false;
                for (u32 tick = 0u; tick < 200000u && (!observationDrained || !automaticTerminal); ++tick)
                {
                    const asset_streaming::ProjectAssetPipelineResult observed = pipeline->TickObservation();
                    observationDrained = observed == asset_streaming::ProjectAssetPipelineResult::Success;
                    Check(observed == asset_streaming::ProjectAssetPipelineResult::Success || observed == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "bounded observation tick remains valid");
                    const asset_streaming::ProjectAssetPipelineResult recook = pipeline->TickAutomaticRecook();
                    Check(recook == asset_streaming::ProjectAssetPipelineResult::Success || recook == asset_streaming::ProjectAssetPipelineResult::Pending,
                          "bounded automatic recook tick remains valid");
                    const asset_streaming::ProjectAssetAutomaticRecookStatus automatic = pipeline->GetAutomaticRecookStatus();
                    automaticTerminal = automatic.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle;
                    if (!observationDrained || !automaticTerminal)
                        concurrency::YieldCurrentThread();
                }
                const asset_streaming::ProjectAssetObservationStatus recovered = pipeline->GetObservationStatus();
                Check(observationDrained && recovered.pendingPaths == 0u && recovered.rescansCompleted >= 1u && !recovered.rescanRequired,
                      "overflow reaches authoritative bounded recovery without pending paths");
                Check(pipeline->DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "watch overflow automatic coordinator disables cleanly");
                Check(pipeline->DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "watch overflow observation disables cleanly");
                Check(context.Close() == nano::ProjectAssetContextResult::Success, "watch overflow context closes cleanly");
            }
        }
        else if (std::strcmp(argv[1], "asset-p5-cold-matrix") == 0 || p5RestartPrepare)
        {
            // E2.6 P5 rows 1/2. Exercise the actual headless composition and
            // production texture importer/compiler rather than a test compiler.
            // The cold-matrix variant adds a source after Open and proves the
            // authoritative rescan -> VADI -> loose-publication chain. The
            // restart-prepare variant builds the original source and deliberately
            // leaves the approved scratch project for restart-verify.
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace), "P5 production-texture harness opens");
            if (harness != nullptr && harness->open)
            {
                const filesystem::AbsolutePath derivedOwner = workspace.derivedData.AddDirPath("ProjectAssets");
                const filesystem::AbsolutePath intermediateOwner = workspace.intermediate.AddDirPath("ProjectAssets");
                Check(harness->config.build.artifactCacheRoot == derivedOwner.AddDirPath("VDDC") &&
                          harness->config.build.dependencyIndexRoot == derivedOwner.AddDirPath("Index") &&
                          harness->config.publication.looseRoot == intermediateOwner.AddDirPath("Loose") &&
                          harness->config.operationJournalPath == intermediateOwner.AddFilePath("operations.vtxn"),
                      "P5 generated/cache/journal authority remains contained in the approved scratch workspace");
                asset_streaming::ProjectAssetPipeline& pipeline = harness->pipeline;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath(
                    p5RestartPrepare ? "red.png" : "cold.png");
                if (!p5RestartPrepare)
                {
                    Check(AuthorTextureFixture(root, "cold.png"),
                          "P5 authors an external production-texture source after the owner is live");
                    Check(pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success,
                          "P5 authoritative rescan accepts and classifies the external texture add");
                }

                assets::AssetMetadata metadata;
                Check(ReadAssetMetadata(source, metadata), "P5 reads accepted production-texture identity");
                const asset_streaming::ProjectAssetState* const accepted = pipeline.LookupAcceptedAsset(metadata.id);
                Check(accepted != nullptr && accepted->presence == asset_streaming::ProjectAssetPresence::Present &&
                          accepted->importState == assets::SourceImportState::MetadataSelected && accepted->outputs.Count() == 1u,
                      "P5 production texture is present, metadata-selected and owns one accepted output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    asset_streaming::ProjectAssetBuildFreshness freshness;
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::Missing,
                          "P5 cold production output is Missing before first build");

                    asset_streaming::ProjectAssetRequest request = pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 production build request is accepted by the shared owner");
                    if (request.IsValid())
                    {
                        request.Wait();
                        Check(request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                              "P5 production texture compiles, commits VADI and publishes loose output");
                        request.Reset();
                    }
                    Check(harness->probe.compileCalls.GetValue() == 1u,
                          "P5 cold production build enters the real texture compiler exactly once");

                    asset_streaming::ProjectAssetOutputDeliveryStatus delivered;
                    Check(pipeline.GetOutputDeliveryStatus(outputReference, delivered) &&
                              delivered.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                              delivered.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                              delivered.committedBuildFingerprint == delivered.publishedBuildFingerprint &&
                              delivered.committedContentFingerprint == delivered.publishedContentFingerprint &&
                              !delivered.publishedPhysicalPath.Empty() && setupFiles.FileExist(delivered.publishedPhysicalPath) &&
                              filesystem::ValidateMutationPath(harness->config.publication.looseRoot, delivered.publishedPhysicalPath) ==
                                  filesystem::MutationPathResult::Success,
                          "P5 production build has exact committed fingerprints and an owned loose publication");
                    Check(setupFiles.FileExist(harness->config.build.dependencyIndexRoot.AddFilePath("asset-dependencies.vadi")),
                          "P5 production build persists the authoritative VADI");
                    Check(pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                              freshness.reason == assets::DirtyReason::UpToDate,
                          "P5 production output is UpToDate after VADI commit/publication");

                    if (!p5RestartPrepare)
                    {
                        Check(RewriteTextureSourceBlue(source) &&
                                  pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::SourceChanged,
                              "P5 production source-content change evaluates SourceChanged");
                        asset_streaming::ProjectAssetRequest sourceRequest = pipeline.Request(outputReference);
                        Check(sourceRequest.IsValid(), "P5 source-change rebuild is accepted");
                        if (sourceRequest.IsValid())
                        {
                            sourceRequest.Wait();
                            Check(sourceRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                                  "P5 source-change rebuild publishes successfully");
                            sourceRequest.Reset();
                        }
                        Check(harness->probe.compileCalls.GetValue() == 2u &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::UpToDate,
                              "P5 source change recompiles exactly once and returns UpToDate");

                        Check(RewriteTextureProfile(source, texture_tools::profiles::Color) &&
                                  pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::SourceChanged,
                              "P5 production settings change evaluates SourceChanged against VADI");
                        asset_streaming::ProjectAssetRequest settingsRequest = pipeline.Request(outputReference);
                        Check(settingsRequest.IsValid(), "P5 settings-change rebuild is accepted");
                        if (settingsRequest.IsValid())
                        {
                            settingsRequest.Wait();
                            Check(settingsRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
                                  "P5 settings-change rebuild publishes successfully");
                            settingsRequest.Reset();
                        }
                        Check(harness->probe.compileCalls.GetValue() == 3u &&
                                  pipeline.QueryBuildFreshness(outputReference, freshness) == asset_streaming::ProjectAssetPipelineResult::Success &&
                                  freshness.reason == assets::DirtyReason::UpToDate,
                              "P5 settings change recompiles exactly once and returns UpToDate");
                    }
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 production-texture harness closes cleanly");
        }
        else if (std::strcmp(argv[1], "asset-p5-active-ownership") == 0)
        {
            // E2.6 P5 rows 11/14/15: hold the *production* texture compiler at
            // its callback boundary, prove every conflicting owner refuses
            // admission, cancel through the graph owner, then prove an active
            // maintenance shutdown and clean reinitialize on the same closure.
            const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
            assets::AssetMetadata metadata;
            Check(ReadAssetMetadata(source, metadata), "P5 active-owner fixture reads production texture identity");
            P5ProjectAssetHarness* harness = VANGUARD_NEW(P5ProjectAssetHarness, memory::pools::Tools);
            Check(harness != nullptr && harness->Open(workspace), "P5 active-owner production harness opens");
            if (harness != nullptr && harness->open)
            {
                const asset_streaming::ProjectAssetState* const accepted = harness->pipeline.LookupAcceptedAsset(metadata.id);
                Check(accepted != nullptr && accepted->outputs.Count() == 1u,
                      "P5 active-owner fixture resolves one production output");
                if (accepted != nullptr && accepted->outputs.Count() == 1u)
                {
                    const resources::ResourceReference outputReference = accepted->outputs[0].resource;
                    harness->probe.block.SetValue(true);
                    harness->probe.started.SetValue(false);
                    harness->probe.release.SetValue(false);
                    asset_streaming::ProjectAssetRequest request = harness->pipeline.Request(outputReference);
                    Check(request.IsValid(), "P5 active production build request starts");
                    for (u32 spin = 0u; spin < 2000000u && !harness->probe.started.GetValue() && !request.HasFinished(); ++spin)
                        concurrency::YieldCurrentThread();
                    const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("blocked-move.png");
                    Check(harness->probe.started.GetValue() &&
                              harness->pipeline.Rescan(harness->sources.GetRoots()) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.MoveAsset(metadata.id, moved) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy &&
                              harness->pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
                          "P5 live production build excludes rescan, E1F mutation and both maintenance owners");
                    Check(request.Cancel(), "P5 active production build cancellation is accepted");
                    request.Wait();
                    request.Reset();
                    harness->probe.block.SetValue(false);
                    harness->probe.release.SetValue(true);
                    Check(harness->pipeline.CanMutateSources() && harness->probe.compileCalls.GetValue() == 1u,
                          "P5 cancelled production build drains to a quiescent mutation owner");

                    Check(harness->pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                              harness->pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
                          "P5 shutdown drains an active maintenance owner after production-build cancellation");
                    const assets::CompilerDescriptor wrappedCompiler = harness->probe.Wrap(harness->compiler.GetDescriptor());
                    const assets::SourceImporterDescriptor importer = harness->compiler.GetImporterDescriptor();
                    Check(harness->pipeline.Initialize(filesystem::GetManager(), harness->sources, harness->streamer,
                                                      {&wrappedCompiler, 1u}, {&importer, 1u}, harness->config) ==
                              asset_streaming::ProjectAssetPipelineResult::Success &&
                              harness->pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle &&
                              harness->pipeline.GetLooseMaintenanceStatus().state == asset_streaming::ProjectAssetLooseMaintenanceState::Idle,
                          "P5 clean reinitialize has no transient maintenance replay after active-work teardown");
                }
            }
            Check(CloseAndDestroyP5Harness(harness), "P5 active-owner production harness closes cleanly");
        }
        else if (std::strcmp(argv[1], "asset-p5-protocol-equivalence") == 0)
        {
            // E2.6 P5 row 18: every asset handler constructs one SemanticEvent
            // and selects a renderer only at WriteSemanticEvent. Render the same
            // representative terminal objects both ways here. Actual command exit
            // mappings are covered by the one-command-per-process modes above;
            // reopening ProjectAssetContext in-process is intentionally invalid
            // because the I/O backend is one-shot.
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Completed, "asset-build", "success"),
                  "P5 completed terminal preserves operation/event/reason semantics across human and JSONL");
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Failed, "asset-status", "invalid-source-path"),
                  "P5 failed terminal preserves operation/event/reason semantics across human and JSONL");
            Check(RenderEquivalentTerminal(nano::SemanticEventKind::Cancelled, "asset-watch", "cancelled"),
                  "P5 cancelled terminal preserves operation/event/reason semantics across human and JSONL");
        }
        else
        {
            buffer = {};
            nano::ResetCommandCancellation();
            nano::ExitCode result = nano::ExitCode::InternalFailure;
            if (std::strcmp(argv[1], "asset-status") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "Textures\\red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success, "asset status succeeds");
                Check(Contains(buffer.data, "\"source\":\"textures/red.png\"") && Contains(buffer.data, "\"key\":\"main\"") &&
                          Contains(buffer.data, "\"freshness\":\"missing\""),
                      "asset status resolves canonical source and exact output key with stable freshness token");
                Check(Contains(buffer.data, "\"scope\":\"vddc-maintenance\"") && Contains(buffer.data, "\"scope\":\"loose-maintenance\""),
                      "asset status reports both maintenance lifecycle snapshots without exposing maintenance commands");
                Check(Count(buffer.data, "\"event\":\"completed\"") == 1u && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset status emits exactly one completed terminal");
            }
            else if (std::strcmp(argv[1], "asset-status-freshness-failure") == 0)
            {
                FreshnessFailureCapture freshnessCapture;
                freshnessCapture.source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                nano::Output freshnessOutput(&CaptureAndDeleteSourceAfterProjectStatus, &freshnessCapture);
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "Textures/red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, freshnessOutput);
                Check(freshnessCapture.mutationAttempted && freshnessCapture.sourceDeleted,
                      "freshness failure fixture deletes source only after project status proves the command context opened");
                Check(result == nano::ExitCode::OperationFailed && Contains(freshnessCapture.buffer.data, "\"query\":\"freshness\"") &&
                          Count(freshnessCapture.buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(freshnessCapture.buffer.data, "\"event\":\"completed\"") &&
                          !Contains(freshnessCapture.buffer.data, "\"event\":\"cancelled\""),
                      "freshness query failure preserves diagnostic evidence and terminates failed with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-status-diagnostics") == 0)
            {
                const filesystem::AbsolutePath diagnosticsRoot = root.AddDirPath("Assets").AddDirPath("Diagnostics");
                Check(setupFiles.CreatePath(diagnosticsRoot), "diagnostic fixture directory creation");
                constexpr u8 byte = 0u;
                for (u32 index = 0u; index < 70u; ++index)
                {
                    char name[32]{};
                    std::snprintf(name, sizeof(name), "missing-%02u.png", index);
                    Check(nano::platform::WriteFileDurable(diagnosticsRoot.AddFilePath(name).AsStringView(), {&byte, 1u}),
                          "diagnostic fixture source creation");
                }
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::Success, "asset status with source diagnostics succeeds");
                Check(Contains(buffer.data, "\"diagnostics-truncated\":true") &&
                          Count(buffer.data, "\"scope\":\"source-diagnostic\"") == 64u,
                      "asset status bounds source diagnostics at 64 rows and reports truncation");
                Check(Count(buffer.data, "\"event\":\"completed\"") == 1u && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "diagnostic-heavy asset status still emits exactly one completed terminal");
            }
            else if (std::strcmp(argv[1], "asset-rescan") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "rescan", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset rescan completes exactly once");
            }
            else if (std::strcmp(argv[1], "asset-build") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "build", root.AsChar(), "Textures/red.png", "--output", "main", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-build\"") && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset build drives real texture compiler and completes once");
            }
            else if (std::strcmp(argv[1], "asset-reimport") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "reimport", root.AsChar(), "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-reimport\"") && !Contains(buffer.data, "\"event\":\"failed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset reimport drives existing coordinator and completes once");
            }
            else if (std::strcmp(argv[1], "asset-reimport-failure") == 0)
            {
                constexpr u8 invalidPng = 0u;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                Check(nano::platform::DeleteFile(source.AsStringView()) &&
                          nano::platform::WriteFileDurable(source.AsStringView(), {&invalidPng, 1u}),
                      "reimport failure fixture corrupts source bytes without changing accepted metadata");
                const char* command[]{"nanovanguard", "asset", "reimport", root.AsChar(), "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset reimport compiler failure reaches one failed terminal with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-maintain-vddc") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "vddc", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Contains(buffer.data, "\"scope\":\"vddc-maintenance\",\"state\":\"complete\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain vddc reaches satisfied complete state and one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-loose") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "loose", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Contains(buffer.data, "\"scope\":\"loose-maintenance\",\"state\":\"complete\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain loose reaches satisfied complete state and one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-all") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "all", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success &&
                          AppearsBefore(buffer.data,
                                       "\"scope\":\"vddc-maintenance\",\"state\":\"complete\"",
                                       "\"scope\":\"loose-maintenance\"") &&
                          Contains(buffer.data, "\"operation\":\"asset-maintain\"") &&
                          Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"failed\"") && !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset maintain all completes VDDC before starting loose and emits one success terminal");
            }
            else if (std::strcmp(argv[1], "asset-maintain-cancel") == 0)
            {
                nano::RequestCommandCancellation();
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "vddc", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Cancelled &&
                          Count(buffer.data, "\"event\":\"cancelled\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"failed\""),
                      "asset maintain pre-cancel is deterministic and emits one cancelled terminal with exit 30");
            }
            else if (std::strcmp(argv[1], "asset-maintain-invalid") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "maintain", root.AsChar(), "--domain", "future", "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::UsageError &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "invalid-maintenance-domain"),
                      "asset maintain semantic domain validation uses one failed terminal while preserving exit 2");
            }
            else if (std::strcmp(argv[1], "asset-move") == 0 || moveRestartPrepare)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath moved = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("moved.png");
                assets::AssetMetadata before;
                assets::AssetMetadata after;
                Check(ReadAssetMetadata(source, before), "asset move reads original identity");
                const char* command[]{"nanovanguard", "asset", "move", root.AsChar(), "Textures/red.png", "Textures/moved.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-move\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(MetadataPath(source).AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(moved.AsStringView()) == nano::platform::PathKind::File &&
                          ReadAssetMetadata(moved, after) && before.id == after.id,
                      "asset move uses the transaction owner and preserves AssetId");
            }
            else if (std::strcmp(argv[1], "asset-move-conflict") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "move", root.AsChar(), "Textures/red.png", "Textures/red.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::OperationFailed &&
                          Contains(buffer.data, "\"operation-result\":\"target-conflict\"") &&
                          Contains(buffer.data, "\"reason\":\"target-conflict\"") &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset move preserves exact lower AssetOperationResult in status and failed terminal");
            }
            else if (std::strcmp(argv[1], "asset-duplicate") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath copy = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("copy.png");
                assets::AssetMetadata original;
                assets::AssetMetadata duplicated;
                Check(ReadAssetMetadata(source, original), "asset duplicate reads original identity");
                const char* command[]{"nanovanguard", "asset", "duplicate", root.AsChar(), "Textures/red.png", "Textures/copy.png",
                                      "--format", "jsonl"};
                result = registry.Dispatch(8, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-duplicate\"") &&
                          Contains(buffer.data, "\"new-asset-id\":") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::File &&
                          nano::platform::GetPathKind(copy.AsStringView()) == nano::platform::PathKind::File &&
                          ReadAssetMetadata(copy, duplicated) && original.id != duplicated.id,
                      "asset duplicate keeps the source and uses a fresh pipeline-generated identity");
            }
            else if (std::strcmp(argv[1], "asset-retarget") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "retarget", root.AsChar(), "Textures/red.png",
                                      "--output", "main", "--replacement-source", "Textures/blue.png",
                                      "--replacement-output", "main", "--format", "jsonl"};
                result = registry.Dispatch(13, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-retarget\"") &&
                          Contains(buffer.data, "\"operation-result\":\"success\""),
                      "asset retarget resolves both sides through accepted source/output state and completes once");
            }
            else if (std::strcmp(argv[1], "asset-retarget-invalid") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "retarget", root.AsChar(), "Textures/red.png",
                                      "--output", "main", "--replacement-source", "Textures/red.png", "--format", "jsonl"};
                result = registry.Dispatch(11, command, output);
                Check(result == nano::ExitCode::UsageError && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "replacement-mapping-count-mismatch"),
                      "asset retarget semantic mapping validation uses a failed terminal before mutation");
            }
            else if (std::strcmp(argv[1], "asset-delete-reject") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "reject", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation\":\"asset-delete\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(MetadataPath(source).AsStringView()) == nano::platform::PathKind::Missing,
                      "asset delete reject delegates safe no-referencer deletion to the transaction owner");
            }
            else if (std::strcmp(argv[1], "asset-delete-replace") == 0)
            {
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                const filesystem::AbsolutePath replacement = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("blue.png");
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "replace", "--output", "main",
                                      "--replacement-source", "Textures/blue.png", "--replacement-output", "main",
                                      "--format", "jsonl"};
                result = registry.Dispatch(15, command, output);
                Check(result == nano::ExitCode::Success && Count(buffer.data, "\"event\":\"completed\"") == 1u &&
                          Contains(buffer.data, "\"operation-result\":\"success\"") &&
                          nano::platform::GetPathKind(source.AsStringView()) == nano::platform::PathKind::Missing &&
                          nano::platform::GetPathKind(replacement.AsStringView()) == nano::platform::PathKind::File,
                      "asset delete replace validates explicit accepted replacement mappings and deletes only the selected asset");
            }
            else if (std::strcmp(argv[1], "asset-delete-invalid-policy") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "delete", root.AsChar(), "Textures/red.png",
                                      "--dependents", "guess", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::UsageError && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          !Contains(buffer.data, "\"event\":\"usage-error\"") &&
                          Contains(buffer.data, "invalid-dependents-policy"),
                      "asset delete semantic policy validation uses one failed terminal while preserving exit 2");
            }
            else if (std::strcmp(argv[1], "asset-p4-help") == 0)
            {
                const char* command[]{"nanovanguard", "help", "asset"};
                result = registry.Dispatch(3, command, output);
                Check(result == nano::ExitCode::Success && Contains(buffer.data, "maintain") && Contains(buffer.data, "move") &&
                          Contains(buffer.data, "duplicate") && Contains(buffer.data, "retarget") && Contains(buffer.data, "delete") &&
                          !Contains(buffer.data, "undo") && !Contains(buffer.data, "redo"),
                      "asset help exposes P3 plus exactly the P4 mutation/maintenance family without undo/redo");
            }
            else if (std::strcmp(argv[1], "asset-watch-cancel") == 0)
            {
                nano::RequestCommandCancellation();
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                const bool validCancellation = result == nano::ExitCode::Cancelled && Count(buffer.data, "\"event\":\"cancelled\"") == 1u &&
                                               !Contains(buffer.data, "\"event\":\"completed\"") && !Contains(buffer.data, "\"event\":\"failed\"");
                if (!validCancellation)
                    std::fprintf(stderr, "[nanovanguardProjectAssetTests] watch-cancel exit=%d output=%s\n", static_cast<int>(result), buffer.data);
                Check(validCancellation, "asset watch owner-visible cancellation drains and emits one cancelled terminal");
            }
            else if (std::strcmp(argv[1], "asset-watch-active-cancel") == 0)
            {
                ActiveWatchCancellationCapture activeCapture;
                nano::Output activeOutput(&CaptureAndCancelActiveWatch, &activeCapture);
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, activeOutput);
                Check(activeCapture.requested, "asset watch active-cancel fixture requests cancellation only after automatic admission starts");
                Check(result == nano::ExitCode::Cancelled && Count(activeCapture.buffer.data, "\"event\":\"cancelled\"") == 1u &&
                          !Contains(activeCapture.buffer.data, "\"event\":\"completed\"") &&
                          !Contains(activeCapture.buffer.data, "\"event\":\"failed\""),
                      "active asset watch cancellation maps lower cancelled batch to one cancelled terminal with exit 30");
            }
            else if (std::strcmp(argv[1], "asset-watch-failure") == 0)
            {
                constexpr u8 invalidPng = 0u;
                const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddDirPath("Textures").AddFilePath("red.png");
                Check(nano::platform::DeleteFile(source.AsStringView()) &&
                          nano::platform::WriteFileDurable(source.AsStringView(), {&invalidPng, 1u}),
                      "watch failure fixture corrupts source bytes without changing accepted metadata");
                const char* command[]{"nanovanguard", "asset", "watch", root.AsChar(), "--format", "jsonl"};
                result = registry.Dispatch(6, command, output);
                Check(result == nano::ExitCode::OperationFailed && Contains(buffer.data, "\"reason\":\"failed\"") &&
                          Count(buffer.data, "\"event\":\"failed\"") == 1u && !Contains(buffer.data, "\"event\":\"completed\"") &&
                          !Contains(buffer.data, "\"event\":\"cancelled\""),
                      "asset watch sticky automatic failure closes through the shutdown boundary and emits one failed terminal with exit 20");
            }
            else if (std::strcmp(argv[1], "asset-invalid-source") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "status", root.AsChar(), "../red.png", "--format", "jsonl"};
                result = registry.Dispatch(7, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          Contains(buffer.data, "invalid-source-path"),
                      "asset locator rejects lexical parent escape with one failed terminal");
            }
            else if (std::strcmp(argv[1], "asset-output-key") == 0)
            {
                const char* command[]{"nanovanguard", "asset", "build", root.AsChar(), "Textures/red.png", "--output", "missing", "--format", "jsonl"};
                result = registry.Dispatch(9, command, output);
                Check(result == nano::ExitCode::OperationFailed && Count(buffer.data, "\"event\":\"failed\"") == 1u &&
                          Contains(buffer.data, "output-key-not-found"),
                      "asset locator rejects missing exact output key with one failed terminal");
            }
            nano::ResetCommandCancellation();
            if (result != nano::ExitCode::Success && result != nano::ExitCode::Cancelled && result != nano::ExitCode::OperationFailed)
                std::fprintf(stderr, "[nanovanguardProjectAssetTests] command mode=%s exit=%d output=%s\n", argv[1], static_cast<int>(result), buffer.data);
        }
    }
    else if (p3ModelMode)
    {
        Check(setupFiles.CreatePath(root), "P3 model project root creation");
        Check(setupFiles.CreatePath(workspace.assets), "P3 model asset root creation");
        Check(setupFiles.CreatePath(workspace.derivedData), "P3 model derived-data root creation");
        Check(setupFiles.CreatePath(workspace.intermediate), "P3 model intermediate root creation");

        P3ModelFixture fixture;
        Check(AuthorP3ModelFixture(setupFiles, workspace, fixture),
              "P3 authors persisted compound model metadata without a P2 commit implementation");

        containers::String firstImportFailureSource;
        const filesystem::AbsolutePath firstImportSource =
            workspace.assets.AddDirPath("Models").AddFilePath("p5_first_import.gltf");
        const bool firstImportFailureSourcePrepared =
            MakeP5NonUniformCompositionSource(fixture.sourceText, firstImportFailureSource) &&
            nanovanguard::platform::WriteFileDurable(
                firstImportSource.AsStringView(),
                {reinterpret_cast<const u8*>(firstImportFailureSource.AsChar()), firstImportFailureSource.Length()});
        Check(firstImportFailureSourcePrepared,
              "P5 authors an unmanaged compound model whose composition successor is intentionally unbuildable");

        // This mode intentionally reopens ProjectAssetContext multiple times to
        // prove cold invalidation. Keep the process facilities under the test's
        // ownership so each command-scoped context inherits rather than tears
        // down the non-restartable I/O backend between proofs.
        Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguardProjectAssetTests-p3"),
              "P3 inherited diagnostics initialization");
        Check(vanguard::io::Initialize(), "P3 inherited I/O initialization");
        Check(filesystem::Initialize({workspace.root, workspace.root, workspace.derivedData}),
              "P3 inherited filesystem initialization");
        Check(jobs::Initialize(jobs::ToolConfig()), "P3 inherited jobs initialization");
        Check(ProveP4GeneratedComposition(),
              "P4 exact production composer proves zero/one/many node mapping, stable parents, VMESH dependencies and affine rejection");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure openFailure;
        const nano::ProjectAssetContextResult openResult = context.Open(workspace, "windows-x64", &openFailure);
        if (openResult != nano::ProjectAssetContextResult::Success)
            std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 open result=%u stage=%u pipeline=%u build=%u\n",
                         static_cast<u32>(openResult), static_cast<u32>(openFailure.stage),
                         static_cast<u32>(openFailure.pipeline), static_cast<u32>(openFailure.pipelineFailure.build));
        Check(openResult == nano::ProjectAssetContextResult::Success,
              "P3 compound model project opens through the production composition root");

        asset_streaming::ProjectAssetPipeline* pipeline = context.GetPipeline();
        assets::SourceDatabase* sources = context.GetSources();
        bool contextClosed = false;
        Check(pipeline != nullptr && sources != nullptr, "P3 exposes live project build/source owners");
        if (pipeline != nullptr && sources != nullptr)
        {
            const asset_streaming::ProjectAssetState* const accepted = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            Check(accepted != nullptr && accepted->outputs.Count() == fixture.meshes + fixture.materials + fixture.textures + fixture.compositions,
                  "P3/P4 accepted source owns the exact inspected VMESH/VMAT/VTEX/VPFB public children");

            resources::ResourceReference meshOutput;
            resources::ResourceReference compositionOutput;
            resources::ResourceReference materialOutputs[4]{};
            resources::ResourceReference textureOutputs[2]{};
            u32 materialCount = 0u;
            u32 textureCount = 0u;
            for (const assets::AssetOutput& output : fixture.metadata.outputs)
            {
                const resources::ResourceReference reference = assets::GetAssetOutputReference(fixture.metadata.id, output);
                if (output.type == meshes::MeshResourceType)
                    meshOutput = reference;
                else if (output.type == prefabs::PrefabResourceType)
                    compositionOutput = reference;
                else if (output.type == materials::MaterialResourceType && materialCount < 4u)
                    materialOutputs[materialCount++] = reference;
                else if (output.type == textures::TextureResourceType && textureCount < 2u)
                    textureOutputs[textureCount++] = reference;
            }
            Check(meshOutput.IsValid() && compositionOutput.IsValid() && materialCount == fixture.materials && materialCount >= 2u && textureCount == 2u &&
                      materialOutputs[0] != materialOutputs[1] && textureOutputs[0] != textureOutputs[1],
                  "P3/P4 exact compound child identities are distinct and typed");

            const auto build = [&](const resources::ResourceReference output,
                                   asset_streaming::ProjectAssetOutputDeliveryStatus& delivered) noexcept
            {
                asset_streaming::ProjectAssetPipelineFailure failure;
                asset_streaming::ProjectAssetRequest request = pipeline->Request(output, assets::BuildPriority::High, &failure);
                if (!request.IsValid())
                {
                    std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 request rejected type=%08x pipeline=%u build=%u error=%u\n",
                                 output.ExpectedType(), static_cast<u32>(failure.result), static_cast<u32>(failure.build),
                                 static_cast<u32>(failure.buildError));
                    return false;
                }
                request.Wait();
                const asset_streaming::ProjectAssetPipelineResult result = request.Poll(&failure);
                request.Reset();
                if (result != asset_streaming::ProjectAssetPipelineResult::Success)
                {
                    std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 build failed type=%08x result=%u build=%u failure=%u error=%u\n",
                                 output.ExpectedType(), static_cast<u32>(result), static_cast<u32>(failure.build),
                                 static_cast<u32>(failure.buildFailure), static_cast<u32>(failure.buildError));
                    return false;
                }
                return pipeline->GetOutputDeliveryStatus(output, delivered) &&
                       delivered.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                       delivered.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                       !delivered.publishedPhysicalPath.Empty() && filesystem::GetManager().FileExist(delivered.publishedPhysicalPath);
            };

            // Build VPFB first from a cold graph. Success here proves the prefab compiler
            // discovers its generated VMESH dependency and drives that dependency (plus the
            // mesh's material/texture closure) before publishing the composition artifact.
            asset_streaming::ProjectAssetOutputDeliveryStatus compositionDelivery;
            Check(build(compositionOutput, compositionDelivery),
                  "P4 generated VPFB builds first through its generated VMESH dependency closure");
            if (!compositionDelivery.publishedPhysicalPath.Empty())
            {
                auto reader = filesystem::GetManager().CreateFileReader(compositionDelivery.publishedPhysicalPath, filesystem::FOF_Buffered);
                prefabs::PrefabFile compositionPrefab;
                const bool opened = reader && compositionPrefab.Open(*reader) == prefabs::Result::Success && compositionPrefab.IsOpen();
                Check(opened && compositionPrefab.GetEntities().Size() == 1u && compositionPrefab.GetComponents().Size() == 1u &&
                          compositionPrefab.GetPlacedComponents().Size() == 1u && compositionPrefab.GetDependencies().Size() == 1u &&
                          compositionPrefab.GetComponents()[0].schema == entities::StaticMeshComponentType &&
                          compositionPrefab.GetDependencies()[0].resource == meshOutput &&
                          compositionPrefab.GetDependencies()[0].kind == resources::DependencyKind::Required,
                      "P4 published VPFB is runtime-readable one-entity composition with the exact generated VMESH dependency");
            }

            asset_streaming::ProjectAssetOutputDeliveryStatus textureDelivery[2];
            for (u32 index = 0u; index < 2u; ++index)
            {
                Check(build(textureOutputs[index], textureDelivery[index]),
                      "P3 exact embedded VTEX child builds and publishes");
                if (!textureDelivery[index].publishedPhysicalPath.Empty())
                {
                    auto reader = filesystem::GetManager().CreateFileReader(textureDelivery[index].publishedPhysicalPath,
                                                                            filesystem::FOF_Buffered);
                    textures::TextureFile texture;
                    Check(reader && texture.Open(*reader) == textures::Result::Success && texture.IsOpen() &&
                              texture.GetDimension() == textures::TextureDimension::Texture2D &&
                              texture.GetWidth() == 1u && texture.GetHeight() == 1u,
                          "P3 published VTEX is ordinary runtime-readable texture data");
                }
            }

            asset_streaming::ProjectAssetOutputDeliveryStatus materialDelivery[4];
            bool sawTextureClosure = false;
            for (u32 index = 0u; index < materialCount; ++index)
            {
                Check(build(materialOutputs[index], materialDelivery[index]),
                      "P3 exact VMAT child builds through generated MPGI/MPLI fallback");
                if (!materialDelivery[index].publishedPhysicalPath.Empty())
                {
                    auto reader = filesystem::GetManager().CreateFileReader(materialDelivery[index].publishedPhysicalPath,
                                                                            filesystem::FOF_Buffered);
                    materials::MaterialFile material;
                    const bool opened = reader && material.Open(*reader) == materials::Result::Success && material.IsOpen();
                    Check(opened && material.GetTechniques().Count() == 2u,
                          "P3 published VMAT is runtime-readable with depth/opaque techniques");
                    if (opened)
                    {
                        u32 textureDependencies = 0u;
                        for (const materials::ResourceDependency& dependency : material.GetDependencies())
                            textureDependencies += dependency.resource.IsTyped() &&
                                                   dependency.resource.ExpectedType() == textures::TextureResourceType
                                                       ? 1u
                                                       : 0u;
                        sawTextureClosure |= textureDependencies == 2u;
                    }
                }
            }
            Check(sawTextureClosure,
                  "P3 VMAT carries actual ordinary VTEX dependencies from imported material channels");

            asset_streaming::ProjectAssetOutputDeliveryStatus meshDelivery;
            Check(build(meshOutput, meshDelivery),
                  "P3 exact VMESH child builds through its actual per-slot VMAT dependencies");
            if (!meshDelivery.publishedPhysicalPath.Empty())
            {
                auto reader = filesystem::GetManager().CreateFileReader(meshDelivery.publishedPhysicalPath, filesystem::FOF_Buffered);
                meshes::MeshFile mesh;
                const bool opened = reader && mesh.Open(*reader) == meshes::Result::Success && mesh.IsOpen();
                Check(opened && mesh.GetMaterialSlots().Count() == 2u && mesh.GetSubmeshes().Count() == 2u,
                      "P3 published VMESH is runtime-readable with stable material slots");
                if (opened && mesh.GetMaterialSlots().Count() == 2u)
                {
                    const resources::ResourceReference slot0 = mesh.GetMaterialSlots()[0].material;
                    const resources::ResourceReference slot1 = mesh.GetMaterialSlots()[1].material;
                    bool found0 = false;
                    bool found1 = false;
                    for (u32 index = 0u; index < materialCount; ++index)
                    {
                        found0 |= slot0 == materialOutputs[index];
                        found1 |= slot1 == materialOutputs[index];
                    }
                    const bool exactMaterials = found0 && found1 && slot0 != slot1;
                    Check(exactMaterials, "P3 VMESH material slots preserve exact model-owned VMAT child identities");
                }
            }

            const auto writeModelSource = [](const filesystem::AbsolutePath& path,
                                             const containers::String& text) noexcept
            {
                // This is an existing-source refresh fixture. WriteFileDurable
                // uses CREATE_NEW and intentionally refuses to overwrite; the
                // ordinary filesystem writer is the correct authored edit here.
                auto writer = filesystem::GetManager().CreateFileWriter(path);
                if (!writer)
                    return false;
                writer->Serialize(const_cast<char*>(text.AsChar()), text.Length());
                writer->Flush();
                return !writer->HasErrors();
            };
            const auto drainExplicitCohort = [&pipeline]() noexcept
            {
                // The compilation worker can legitimately spend seconds in
                // material/shader builds. Count real bounded wait time rather
                // than exhausting hundreds of thousands of zero-delay yields.
                for (u32 tick = 0u; tick < 60000u; ++tick)
                {
                    asset_streaming::ProjectAssetPipelineFailure failure;
                    const asset_streaming::ProjectAssetPipelineResult result = pipeline->TickAutomaticRecook(&failure);
                    if (result != asset_streaming::ProjectAssetPipelineResult::Success &&
                        result != asset_streaming::ProjectAssetPipelineResult::Pending)
                        return result;
                    if (result == asset_streaming::ProjectAssetPipelineResult::Success &&
                        !pipeline->IsAutomaticRecookEnabled())
                        return result;
                    concurrency::SleepOnCurrentThread(1);
                }
                return asset_streaming::ProjectAssetPipelineResult::Pending;
            };
            const auto allAcceptedOutputsInstalled = [&pipeline](const asset_streaming::ProjectAssetState& state) noexcept
            {
                for (const asset_streaming::ProjectAssetOutputState& output : state.outputs)
                {
                    asset_streaming::ProjectAssetOutputDeliveryStatus status;
                    if (!pipeline->GetOutputDeliveryStatus(output.resource, status) ||
                        status.authoredGeneration != state.acceptedGeneration ||
                        status.buildState != asset_streaming::ProjectAssetDeliveryStageState::Committed ||
                        status.publicationState != asset_streaming::ProjectAssetDeliveryStageState::Installed ||
                        status.committedBuildFingerprint.IsEmpty() || status.committedContentFingerprint.IsEmpty() ||
                        status.publishedBuildFingerprint.IsEmpty() || status.publishedContentFingerprint.IsEmpty() ||
                        status.publishedPhysicalPath.Empty() || !filesystem::GetManager().FileExist(status.publishedPhysicalPath))
                        return false;
                }
                return true;
            };
            const auto containsAcceptedOutput = [](const asset_streaming::ProjectAssetState& state,
                                                   const resources::ResourceReference resource) noexcept
            {
                for (const asset_streaming::ProjectAssetOutputState& output : state.outputs)
                    if (output.resource == resource)
                        return true;
                return false;
            };
            const auto refreshAndDrain = [&](const assets::AssetId asset,
                                             mesh_tools::ModelImportPlanReport* const report = nullptr) noexcept
            {
                asset_streaming::ProjectAssetPipelineFailure failure;
                asset_streaming::ProjectAssetRestoreReport restore;
                const asset_streaming::ProjectAssetPipelineResult refreshed =
                    context.RefreshModelFromSource(asset, report, &restore, &failure);
                if (refreshed != asset_streaming::ProjectAssetPipelineResult::Success &&
                    refreshed != asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending)
                    return refreshed;
                return drainExplicitCohort();
            };

            // P5 content refresh: the source bytes advance while the output schema
            // remains identical. Every member must land at one accepted generation
            // through the explicit cohort publication path.
            const asset_streaming::ProjectAssetState* mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            const u64 baselineAcceptedGeneration = mainState != nullptr ? mainState->acceptedGeneration : 0u;
            const u32 baselineOutputCount = mainState != nullptr ? mainState->outputs.Count() : 0u;
            containers::DynamicArray<resources::ResourceReference> baselineOutputs(memory::pools::Tools::GetInstance());
            if (mainState != nullptr)
                for (const asset_streaming::ProjectAssetOutputState& output : mainState->outputs)
                    baselineOutputs.PushBack(output.resource);
            containers::String contentRefreshSource(fixture.sourceText);
            contentRefreshSource.Append("\n ", 2u);
            Check(mainState != nullptr && baselineAcceptedGeneration != 0u && baselineOutputs.Size() == baselineOutputCount,
                  "P5 captures the exact pre-refresh compound output cohort");
            Check(writeModelSource(fixture.source, contentRefreshSource),
                  "P5 writes a structurally identical content refresh");
            Check(refreshAndDrain(fixture.metadata.id) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 RefreshModelFromSource completes a content-only successor cohort");
            mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            Check(mainState != nullptr && mainState->acceptedGeneration > baselineAcceptedGeneration &&
                      mainState->outputs.Count() == baselineOutputCount && allAcceptedOutputsInstalled(*mainState),
                  "P5 content refresh publishes every unchanged-schema child at the new accepted generation");

            // P5 first import failure: commit a real unmanaged model plan whose
            // composition cannot form a build request because of non-uniform scale.
            // Cohort admission must fail without manufacturing Installed/Ready
            // evidence for any first-generation output. Restoring valid source
            // bytes and refreshing the now-managed asset must recover normally.
            assets::AssetId firstImportAsset;
            mesh_tools::ModelImportManifest firstManifest;
            mesh_tools::ModelImportReport firstInspectReport;
            const assets::SourceRecord* const firstCandidate = sources->LookupResource(firstImportSource);
            const mesh_tools::ModelImportResult firstInspected = firstImportFailureSourcePrepared
                                                                     ? mesh_tools::InspectModelMemory(
                                                                           firstImportFailureSource.AsChar(), firstImportFailureSource.Length(),
                                                                           "gltf", firstImportSource.AsStringView(), {}, {}, firstManifest,
                                                                           &firstInspectReport)
                                                                     : mesh_tools::ModelImportResult::InvalidArgument;
            const asset_streaming::ProjectAssetPipelineResult firstIdentity =
                pipeline->GenerateImportIdentityCandidate(firstImportAsset);
            Check(firstCandidate != nullptr && firstInspected == mesh_tools::ModelImportResult::Success &&
                      firstIdentity == asset_streaming::ProjectAssetPipelineResult::Success && firstImportAsset.IsValid(),
                  "P5 prepares a real unmanaged model candidate for first-import failure coverage");
            mesh_tools::ModelImportSettings firstSettings;
            firstSettings.materialPolicy = mesh_tools::ModelImportMaterialPolicy::ImportModelOwned;
            firstSettings.embeddedTexturePolicy = mesh_tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned;
            firstSettings.materialMapperVersion = material_tools::ImportedSurfaceMaterialMapperVersion;
            firstSettings.includeComposition = true;
            mesh_tools::ModelImportPlanningContext firstPlanning;
            firstPlanning.database = sources;
            firstPlanning.parentAsset = firstImportAsset;
            firstPlanning.importer = assets::HashCompilerName("vanguard.model.importer");
            firstPlanning.importerVersion = mesh_tools::ModelSourceImporterVersion;
            firstPlanning.acceptedStateGeneration = pipeline->GetAcceptedAssetGeneration();
            firstPlanning.sourceOrigin = firstImportSource;
            firstPlanning.sourceRoot = workspace.assets;
            firstPlanning.sourceDestination = firstImportSource;
            firstPlanning.sourceIdentity = firstCandidate != nullptr ? firstCandidate->sourceIdentity : resources::ResourcePath{};
            firstPlanning.outputTypes.mesh = meshes::MeshResourceType;
            firstPlanning.outputTypes.material = materials::MaterialResourceType;
            firstPlanning.outputTypes.texture = textures::TextureResourceType;
            firstPlanning.outputTypes.composition = prefabs::PrefabResourceType;
            mesh_tools::ModelImportPlan firstPlan;
            mesh_tools::ModelImportPlanReport firstPlanReport;
            const mesh_tools::ModelImportPlanResult firstPlanned =
                firstCandidate != nullptr && firstImportAsset.IsValid()
                    ? mesh_tools::PlanModelImport(firstManifest, firstSettings, firstPlanning, firstPlan, &firstPlanReport)
                    : mesh_tools::ModelImportPlanResult::InvalidArgument;
            Check(firstPlanned == mesh_tools::ModelImportPlanResult::Success && !firstPlan.HasBlockingConflicts(),
                  "P5 first-import plan accepts source identity while leaving composition buildability to the production compiler");
            assets::AssetId firstCommittedAsset;
            asset_streaming::ProjectAssetPipelineFailure firstCommitFailure;
            const asset_streaming::ProjectAssetPipelineResult firstCommitted =
                firstPlanned == mesh_tools::ModelImportPlanResult::Success
                    ? context.CommitModelImportPlan(
                          firstPlan,
                          {reinterpret_cast<const u8*>(firstImportFailureSource.AsChar()), firstImportFailureSource.Length()},
                          firstCommittedAsset, nullptr, &firstCommitFailure)
                    : asset_streaming::ProjectAssetPipelineResult::InvalidArgument;
            Check((firstCommitted == asset_streaming::ProjectAssetPipelineResult::Success ||
                   firstCommitted == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending) &&
                      firstCommittedAsset == firstImportAsset,
                  "P5 first-import metadata/schema commits before successor delivery is attempted");
            const asset_streaming::ProjectAssetState* firstState = pipeline->LookupAcceptedAsset(firstImportAsset);
            containers::DynamicArray<resources::ResourceReference> firstCohort(memory::pools::Tools::GetInstance());
            if (firstState != nullptr)
                for (const asset_streaming::ProjectAssetOutputState& output : firstState->outputs)
                    firstCohort.PushBack(output.resource);
            asset_streaming::ProjectAssetPipelineFailure firstDeliveryFailure;
            const asset_streaming::ProjectAssetCohortDeliveryRequest firstDeliveryRequest{
                firstImportAsset, firstState != nullptr ? firstState->acceptedGeneration : 0u,
                {firstCohort.TypedData(), firstCohort.Size()}};
            const asset_streaming::ProjectAssetPipelineResult firstDelivery =
                firstState != nullptr && !firstCohort.Empty()
                    ? pipeline->RequestAssetCohortDelivery(firstDeliveryRequest, &firstDeliveryFailure)
                    : asset_streaming::ProjectAssetPipelineResult::InvalidState;
            Check(firstDelivery == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "P5 first-import successor fails through the real composition build-request gate");
            bool firstImportHasFakeReady = false;
            for (const resources::ResourceReference output : firstCohort)
            {
                asset_streaming::ProjectAssetOutputPublicationStatus status;
                if (!pipeline->GetOutputPublicationStatus(output, status))
                    continue;
                firstImportHasFakeReady |= status.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed ||
                                           status.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Current ||
                                           !status.publishedBuildFingerprint.IsEmpty() || !status.publishedContentFingerprint.IsEmpty();
            }
            Check(!firstImportHasFakeReady,
                  "P5 failed first-import successor exposes no fake Installed/Ready publication evidence");
            Check(writeModelSource(firstImportSource, fixture.sourceText),
                  "P5 restores buildable source bytes for first-import retry");
            Check(refreshAndDrain(firstImportAsset) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 first-import failure recovers through a later RefreshModelFromSource retry");
            firstState = pipeline->LookupAcceptedAsset(firstImportAsset);
            Check(firstState != nullptr && allAcceptedOutputsInstalled(*firstState),
                  "P5 later first-import retry publishes the complete exact cohort");

            // P5 schema expansion/contraction uses the same live model identity.
            // The disconnected mesh uses its own separately authored material,
            // keeping all original material-use and geometry ancestry evidence
            // stable. The atomic cohort must include both new VMESH/VMAT children.
            containers::String addedGeometrySource;
            Check(MakeP5AddedGeometrySource(contentRefreshSource, addedGeometrySource) &&
                      writeModelSource(fixture.source, addedGeometrySource),
                  "P5 writes a schema expansion with disconnected geometry and material outputs");
            Check(refreshAndDrain(fixture.metadata.id) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 schema-add refresh publishes the expanded cohort");
            mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            resources::ResourceReference addedGeometryOutput;
            resources::ResourceReference addedMaterialOutput;
            u32 addedOutputs = 0u;
            if (mainState != nullptr)
                for (const asset_streaming::ProjectAssetOutputState& current : mainState->outputs)
                {
                    bool existed = false;
                    for (const resources::ResourceReference baseline : baselineOutputs)
                        existed = existed || baseline == current.resource;
                    if (!existed)
                    {
                        if (current.resource.ExpectedType() == meshes::MeshResourceType)
                            addedGeometryOutput = current.resource;
                        else if (current.resource.ExpectedType() == materials::MaterialResourceType)
                            addedMaterialOutput = current.resource;
                        ++addedOutputs;
                    }
                }
            Check(mainState != nullptr && mainState->outputs.Count() == baselineOutputCount + 2u && addedOutputs == 2u &&
                      addedGeometryOutput.IsTyped() && addedGeometryOutput.ExpectedType() == meshes::MeshResourceType &&
                      addedMaterialOutput.IsTyped() && addedMaterialOutput.ExpectedType() == materials::MaterialResourceType &&
                      allAcceptedOutputsInstalled(*mainState),
                  "P5 schema-add cohort includes and publishes disconnected VMESH/VMAT siblings");

            Check(writeModelSource(fixture.source, contentRefreshSource),
                  "P5 restores the baseline schema to retire the added geometry");
            Check(refreshAndDrain(fixture.metadata.id) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 schema-remove refresh completes successor publication and retirement");
            mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            asset_streaming::ProjectAssetOutputPublicationStatus removedGeometryStatus;
            asset_streaming::ProjectAssetOutputPublicationStatus removedMaterialStatus;
            const bool removedGeometryTracked = addedGeometryOutput.IsValid() &&
                                                pipeline->GetOutputPublicationStatus(addedGeometryOutput, removedGeometryStatus);
            const bool removedMaterialTracked = addedMaterialOutput.IsValid() &&
                                                pipeline->GetOutputPublicationStatus(addedMaterialOutput, removedMaterialStatus);
            Check(mainState != nullptr && mainState->outputs.Count() == baselineOutputCount &&
                      !containsAcceptedOutput(*mainState, addedGeometryOutput) &&
                      !containsAcceptedOutput(*mainState, addedMaterialOutput) &&
                      (!removedGeometryTracked ||
                       (removedGeometryStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                        removedGeometryStatus.publishedBuildFingerprint.IsEmpty() &&
                        removedGeometryStatus.publishedContentFingerprint.IsEmpty())) &&
                      (!removedMaterialTracked ||
                       (removedMaterialStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                        removedMaterialStatus.publishedBuildFingerprint.IsEmpty() &&
                        removedMaterialStatus.publishedContentFingerprint.IsEmpty())) &&
                      allAcceptedOutputsInstalled(*mainState),
                  "P5 schema removal withdraws both retired siblings only after the baseline successor cohort is installed");

            // Turn the successfully retried first-import model into a real authored
            // external referencer to the main model's Masked material. The model
            // importer's production reference scanner owns this evidence.
            assets::AssetMetadata currentMainMetadata;
            mesh_tools::ModelImportSettings currentMainSettings;
            resources::ResourceReference maskedMaterial;
            if (ReadAssetMetadata(fixture.source, currentMainMetadata) &&
                mesh_tools::DecodeModelImportSettings(
                    currentMainMetadata.settingsVersion,
                    {currentMainMetadata.settings.TypedData(), currentMainMetadata.settings.Size()}, currentMainSettings) ==
                    mesh_tools::ModelImportSettingsResult::Success)
                for (const mesh_tools::ModelImportOutputCorrespondence& output : currentMainSettings.outputs)
                    if (output.kind == mesh_tools::ModelImportElementKind::Material && output.source.sourceName == "Masked")
                        maskedMaterial = assets::GetAssetOutputReference(fixture.metadata.id, output.output);
            assets::AssetMetadata referencerMetadata;
            mesh_tools::ModelImportSettings referencerSettings;
            containers::DynamicArray<u8> referencerSettingsBytes(memory::pools::Tools::GetInstance());
            bool authoredReferencer = maskedMaterial.IsTyped() && ReadAssetMetadata(firstImportSource, referencerMetadata) &&
                                      mesh_tools::DecodeModelImportSettings(
                                          referencerMetadata.settingsVersion,
                                          {referencerMetadata.settings.TypedData(), referencerMetadata.settings.Size()}, referencerSettings) ==
                                          mesh_tools::ModelImportSettingsResult::Success;
            if (authoredReferencer)
            {
                referencerSettings.mesh.defaultMaterial = maskedMaterial;
                authoredReferencer = mesh_tools::EncodeModelImportSettings(referencerSettings, referencerSettingsBytes) ==
                                     mesh_tools::ModelImportSettingsResult::Success;
            }
            if (authoredReferencer)
            {
                referencerMetadata.settings = containers::DynamicArray<u8>(referencerSettingsBytes);
                containers::String encodedReferencer;
                authoredReferencer = assets::EncodeMetadata(referencerMetadata, encodedReferencer) == assets::MetadataResult::Success;
                if (authoredReferencer)
                {
                    auto writer = filesystem::GetManager().CreateFileWriter(MetadataPath(firstImportSource));
                    if (!writer)
                        authoredReferencer = false;
                    else
                    {
                        writer->Serialize(encodedReferencer.AsChar(), encodedReferencer.Length());
                        writer->Flush();
                        authoredReferencer = !writer->HasErrors();
                    }
                }
            }
            asset_streaming::ProjectAssetRestoreReport referencerRestore;
            const asset_streaming::ProjectAssetPipelineResult referencerRescan =
                authoredReferencer ? pipeline->Rescan(sources->GetRoots(), &referencerRestore)
                                   : asset_streaming::ProjectAssetPipelineResult::InvalidState;
            bool sawExternalReference = false;
            if (referencerRescan == asset_streaming::ProjectAssetPipelineResult::Success)
                for (const assets::AssetReference& reference : sources->GetBackwardReferences(maskedMaterial))
                    sawExternalReference = sawExternalReference || reference.from == firstImportAsset;
            Check(authoredReferencer && referencerRescan == asset_streaming::ProjectAssetPipelineResult::Success && sawExternalReference,
                  "P5 fixture exposes a real external authored reference through the production model scanner");

            containers::String removedMaskedSource;
            Check(MakeP5RemovedMaskedMaterialSource(contentRefreshSource, removedMaskedSource) &&
                      writeModelSource(fixture.source, removedMaskedSource),
                  "P5 writes a source revision that would retire the externally referenced Masked material");
            mesh_tools::ModelImportPlanReport blockedRemovalReport;
            asset_streaming::ProjectAssetPipelineFailure blockedRemovalFailure;
            const asset_streaming::ProjectAssetPipelineResult blockedRemoval =
                context.RefreshModelFromSource(fixture.metadata.id, &blockedRemovalReport, nullptr, &blockedRemovalFailure);
            asset_streaming::ProjectAssetOutputPublicationStatus blockedMaterialStatus;
            const bool blockedMaterialVisible = pipeline->GetOutputPublicationStatus(maskedMaterial, blockedMaterialStatus) &&
                                                blockedMaterialStatus.publicationState ==
                                                    asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                                                !blockedMaterialStatus.publishedBuildFingerprint.IsEmpty() &&
                                                !blockedMaterialStatus.publishedContentFingerprint.IsEmpty();
            mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            Check(blockedRemoval == asset_streaming::ProjectAssetPipelineResult::OperationFailed &&
                      blockedRemovalReport.blockers != 0u && mainState != nullptr && containsAcceptedOutput(*mainState, maskedMaterial) &&
                      blockedMaterialVisible,
                  "P5 externally referenced schema removal is blocked before metadata/publication retirement");
            Check(writeModelSource(fixture.source, contentRefreshSource),
                  "P5 restores the referenced material source after the blocked removal");
            Check(refreshAndDrain(fixture.metadata.id) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 refresh recovers after a blocked referenced-removal attempt");

            // A valid immutable plan can still produce an unbuildable successor:
            // non-uniform authored placement is inspected/planned but rejected by
            // the composition build-request bridge. The commit therefore reaches
            // postcommit cohort admission, where failure must leave the exact prior
            // loose publication visible until a later valid refresh succeeds.
            asset_streaming::ProjectAssetOutputPublicationStatus priorComposition;
            const bool capturedPriorComposition = pipeline->GetOutputPublicationStatus(compositionOutput, priorComposition) &&
                                                  priorComposition.publicationState ==
                                                      asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                                                  !priorComposition.publishedBuildFingerprint.IsEmpty() &&
                                                  !priorComposition.publishedContentFingerprint.IsEmpty();
            containers::String failedSuccessorSource;
            Check(capturedPriorComposition && MakeP5NonUniformCompositionSource(contentRefreshSource, failedSuccessorSource) &&
                      writeModelSource(fixture.source, failedSuccessorSource),
                  "P5 writes a planned-but-unbuildable composition successor");
            mesh_tools::ModelImportPlanReport failedSuccessorReport;
            asset_streaming::ProjectAssetPipelineFailure failedSuccessorFailure;
            const asset_streaming::ProjectAssetPipelineResult failedSuccessor =
                context.RefreshModelFromSource(fixture.metadata.id, &failedSuccessorReport, nullptr, &failedSuccessorFailure);
            asset_streaming::ProjectAssetOutputPublicationStatus afterFailedSuccessor;
            const bool preservedPriorComposition = pipeline->GetOutputPublicationStatus(compositionOutput, afterFailedSuccessor) &&
                                                   afterFailedSuccessor.publicationState ==
                                                       asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                                                   afterFailedSuccessor.publishedBuildFingerprint == priorComposition.publishedBuildFingerprint &&
                                                   afterFailedSuccessor.publishedContentFingerprint == priorComposition.publishedContentFingerprint;
            Check(failedSuccessor == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending &&
                      failedSuccessorReport.blockers == 0u && preservedPriorComposition,
                  "P5 failed postcommit successor preserves the exact prior visible publication");

            // The failed successor is the exact pre-journal crash shape: VMETA +
            // accepted state + VPAS cohort intent are durable, but build-request
            // admission failed before IncrementalRecooker could create the VADI
            // retirement journal. A cold reopen must reconstruct the predecessor
            // publication from current VADI, keep the successor pending, and leave
            // the project usable for a later corrected refresh.
            contextClosed = context.Close() == nano::ProjectAssetContextResult::Success;
            Check(contextClosed, "P5 pre-journal successor failure closes with durable cohort intent intact");
            nano::ProjectAssetContextFailure p5RestartFailure;
            const nano::ProjectAssetContextResult p5Restart =
                contextClosed ? context.Open(workspace, "windows-x64", &p5RestartFailure)
                              : nano::ProjectAssetContextResult::InitializationFailed;
            if (p5Restart == nano::ProjectAssetContextResult::Success)
            {
                pipeline = context.GetPipeline();
                sources = context.GetSources();
                contextClosed = false;
            }
            asset_streaming::ProjectAssetOutputPublicationStatus restartedPriorComposition;
            const bool restartPreservedPriorComposition =
                pipeline != nullptr && pipeline->GetOutputPublicationStatus(compositionOutput, restartedPriorComposition) &&
                restartedPriorComposition.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                restartedPriorComposition.publishedBuildFingerprint == priorComposition.publishedBuildFingerprint &&
                restartedPriorComposition.publishedContentFingerprint == priorComposition.publishedContentFingerprint;
            Check(p5Restart == nano::ProjectAssetContextResult::Success && pipeline != nullptr && sources != nullptr &&
                      context.GetStartupPipelineResult() == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending &&
                      restartPreservedPriorComposition,
                  "P5 cold restart before VADI retirement journal restores exact predecessor LKG and keeps successor pending");
            if (pipeline != nullptr)
            {
                asset_streaming::ProjectAssetPipelineFailure fencedRequestFailure;
                asset_streaming::ProjectAssetRequest fencedRequest =
                    pipeline->Request(compositionOutput, assets::BuildPriority::High, &fencedRequestFailure);
                asset_streaming::ProjectAssetPipelineFailure fencedReimportFailure;
                const asset_streaming::ProjectAssetPipelineResult fencedReimport =
                    pipeline->RequestAssetReimport(fixture.metadata.id, &fencedReimportFailure);
                asset_streaming::ProjectAssetPipelineFailure fencedTickFailure;
                const asset_streaming::ProjectAssetPipelineResult fencedTick = pipeline->TickAutomaticRecook(&fencedTickFailure);
                const asset_streaming::ProjectAssetAutomaticRecookStatus fencedStatus = pipeline->GetAutomaticRecookStatus();
                const asset_streaming::ProjectAssetPipelineResult fencedVddc = pipeline->RequestVddcMaintenance();
                const asset_streaming::ProjectAssetPipelineResult fencedLoose = pipeline->RequestLooseMaintenance();
                Check(!fencedRequest.IsValid() &&
                          fencedRequestFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy &&
                          fencedReimport == asset_streaming::ProjectAssetPipelineResult::Busy &&
                          fencedReimportFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy &&
                          fencedVddc == asset_streaming::ProjectAssetPipelineResult::Busy &&
                          fencedLoose == asset_streaming::ProjectAssetPipelineResult::Busy &&
                          fencedTick == asset_streaming::ProjectAssetPipelineResult::Pending &&
                          fencedStatus.activeRoots == 0u,
                      "P5 durable cohort intent fences generic builds, reimport, maintenance and ordinary automatic admission");
                Check(pipeline->EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                      "P5 durable-marker restart may enable ordinary automatic policy without bypassing the cohort fence");
            }
            Check(writeModelSource(fixture.source, contentRefreshSource),
                  "P5 restores a buildable source after successor admission failure");
            asset_streaming::ProjectAssetPipelineFailure enabledRetryFailure;
            const asset_streaming::ProjectAssetPipelineResult enabledRetry =
                context.RefreshModelFromSource(fixture.metadata.id, nullptr, nullptr, &enabledRetryFailure);
            bool enabledRetryDrained = enabledRetry == asset_streaming::ProjectAssetPipelineResult::Success ||
                                       enabledRetry == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending;
            bool enabledRetryInstalled = false;
            for (u32 tick = 0u; tick < 200000u && enabledRetryDrained; ++tick)
            {
                const asset_streaming::ProjectAssetState* const retryState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
                const asset_streaming::ProjectAssetAutomaticRecookStatus retryStatus = pipeline->GetAutomaticRecookStatus();
                enabledRetryInstalled = retryState != nullptr && allAcceptedOutputsInstalled(*retryState);
                if (enabledRetryInstalled && retryStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle)
                    break;
                const asset_streaming::ProjectAssetPipelineResult tickResult = pipeline->TickAutomaticRecook();
                enabledRetryDrained = tickResult == asset_streaming::ProjectAssetPipelineResult::Success ||
                                      tickResult == asset_streaming::ProjectAssetPipelineResult::Pending;
                if (enabledRetryDrained)
                    concurrency::YieldCurrentThread();
            }
            const asset_streaming::ProjectAssetAutomaticRecookStatus enabledRetryStatus = pipeline->GetAutomaticRecookStatus();
            Check((enabledRetry == asset_streaming::ProjectAssetPipelineResult::Success ||
                   enabledRetry == asset_streaming::ProjectAssetPipelineResult::CommittedCleanupPending) &&
                      enabledRetryDrained && enabledRetryInstalled &&
                      enabledRetryStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
                  "P5 automatic-enabled restart installs the exact durable cohort then drains deferred startup recovery");
            Check(pipeline->DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
                  "P5 automatic-enabled durable cohort returns to the normal disabled test policy after recovery");
            mainState = pipeline->LookupAcceptedAsset(fixture.metadata.id);
            Check(mainState != nullptr && allAcceptedOutputsInstalled(*mainState),
                  "P5 retry converges the complete compound cohort after successor failure");
            Check(writeModelSource(fixture.source, fixture.sourceText),
                  "P5 restores the original compound source bytes before the pre-existing cold-invalidation proofs");

            // Mutation/reconciliation is a separate editor subsystem. Close the
            // graph first, then reopen against changed persisted input so these
            // checks prove cold build invalidation rather than Rescan policy.
            contextClosed = context.Close() == nano::ProjectAssetContextResult::Success;
            Check(contextClosed, "P3 compound model context closes after all graph interests are released");
            if (contextClosed)
            {
                const auto writeLocal = [&setupFiles](const filesystem::AbsolutePath& path,
                                                      const containers::ArraySpan<const u8> bytes) noexcept
                {
                    auto writer = setupFiles.CreateFileWriter(path);
                    if (!writer)
                        return false;
                    writer->Serialize(const_cast<u8*>(bytes.Data()), bytes.Count());
                    writer->Flush();
                    return !writer->HasErrors();
                };
                const auto requestRejected = [](nano::ProjectAssetContext& reopened,
                                                const resources::ResourceReference output) noexcept
                {
                    asset_streaming::ProjectAssetPipeline* const active = reopened.GetPipeline();
                    if (active == nullptr)
                        return false;
                    asset_streaming::ProjectAssetPipelineFailure failure;
                    asset_streaming::ProjectAssetRequest request = active->Request(output, assets::BuildPriority::High, &failure);
                    if (!request.IsValid())
                        return true;
                    request.Wait();
                    const bool rejected = request.Poll(&failure) != asset_streaming::ProjectAssetPipelineResult::Success;
                    request.Reset();
                    return rejected;
                };

                // Persisted mapper policy is part of the model build contract. A
                // semantically valid but old mapper version must not silently build
                // through the current mapper implementation.
                mesh_tools::ModelImportSettings staleMapper;
                containers::DynamicArray<u8> staleSettings(memory::pools::Assets::GetInstance());
                Check(mesh_tools::DecodeModelImportSettings(fixture.metadata.settingsVersion,
                                                            {fixture.metadata.settings.TypedData(), fixture.metadata.settings.Size()}, staleMapper) ==
                              mesh_tools::ModelImportSettingsResult::Success,
                      "P3 decodes persisted mapper policy for invalidation proof");
                staleMapper.materialMapperVersion = 1u;
                Check(mesh_tools::EncodeModelImportSettings(staleMapper, staleSettings) == mesh_tools::ModelImportSettingsResult::Success,
                      "P3 encodes an intentionally stale mapper policy");
                const containers::DynamicArray<u8> currentSettings(fixture.metadata.settings);
                fixture.metadata.settings = containers::DynamicArray<u8>(staleSettings);
                containers::String staleMetadata;
                const bool staleWritten = assets::EncodeMetadata(fixture.metadata, staleMetadata) == assets::MetadataResult::Success &&
                                          writeLocal(MetadataPath(fixture.source),
                                                     {reinterpret_cast<const u8*>(staleMetadata.AsChar()), staleMetadata.Length()});
                Check(staleWritten, "P3 writes a stale persisted mapper version without changing source bytes");
                if (staleWritten)
                {
                    nano::ProjectAssetContext staleMapperContext;
                    nano::ProjectAssetContextFailure staleOpenFailure;
                    const nano::ProjectAssetContextResult staleOpenResult =
                        staleMapperContext.Open(workspace, "windows-x64", &staleOpenFailure);
                    const bool staleOpened = staleOpenResult == nano::ProjectAssetContextResult::Success;
                    if (!staleOpened)
                        std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 stale-mapper open result=%u stage=%u sources=%u pipeline=%u\n",
                                     static_cast<u32>(staleOpenResult), static_cast<u32>(staleOpenFailure.stage),
                                     static_cast<u32>(staleOpenFailure.sources), static_cast<u32>(staleOpenFailure.pipeline));
                    Check(staleOpened, "P3 reopens project against stale persisted mapper policy");
                    if (staleOpened)
                    {
                        Check(requestRejected(staleMapperContext, materialOutputs[0]),
                              "P3 rejects a persisted material mapper version that does not match the production mapper");
                        Check(staleMapperContext.Close() == nano::ProjectAssetContextResult::Success,
                              "P3 stale-mapper validation context closes cleanly");
                    }
                }

                fixture.metadata.settings = containers::DynamicArray<u8>(currentSettings);
                containers::String restoredMetadata;
                const bool restored = assets::EncodeMetadata(fixture.metadata, restoredMetadata) == assets::MetadataResult::Success &&
                                      writeLocal(MetadataPath(fixture.source),
                                                 {reinterpret_cast<const u8*>(restoredMetadata.AsChar()), restoredMetadata.Length()});
                Check(restored, "P3 restores current persisted mapper policy before source-staleness proof");

                if (restored)
                {
                    containers::String sourceText(fixture.sourceText);
                    sourceText.Append("\n", 1u);
                    const bool sourceWritten = writeLocal(
                        fixture.source, {reinterpret_cast<const u8*>(sourceText.AsChar()), sourceText.Length()});
                    Check(sourceWritten, "P3 writes structurally equivalent but byte-different model source");
                    if (sourceWritten)
                    {
                        nano::ProjectAssetContext staleSourceContext;
                        nano::ProjectAssetContextFailure staleSourceOpenFailure;
                        const nano::ProjectAssetContextResult staleSourceOpenResult =
                            staleSourceContext.Open(workspace, "windows-x64", &staleSourceOpenFailure);
                        const bool staleSourceOpened = staleSourceOpenResult == nano::ProjectAssetContextResult::Success;
                        if (!staleSourceOpened)
                            std::fprintf(stderr, "[nanovanguardProjectAssetTests] P3 stale-source open result=%u stage=%u sources=%u pipeline=%u\n",
                                         static_cast<u32>(staleSourceOpenResult), static_cast<u32>(staleSourceOpenFailure.stage),
                                         static_cast<u32>(staleSourceOpenFailure.sources), static_cast<u32>(staleSourceOpenFailure.pipeline));
                        Check(staleSourceOpened, "P3 reopens project against byte-edited model source");
                        if (staleSourceOpened)
                        {
                            Check(requestRejected(staleSourceContext, meshOutput),
                                  "P3 exact source-fingerprint gate rejects edited source bytes against persisted correspondence");
                            Check(staleSourceContext.Close() == nano::ProjectAssetContextResult::Success,
                                  "P3 stale-source validation context closes cleanly");
                        }
                    }
                }
            }
        }

        if (!contextClosed)
            Check(context.Close() == nano::ProjectAssetContextResult::Success,
                  "P3 compound model context closes after all graph interests are released");
        Check(jobs::Shutdown(), "P3 inherited jobs shutdown");
        filesystem::Shutdown();
        vanguard::io::Shutdown();
        diagnostics::Shutdown();
    }
    else if (successMode)
    {
        Check(setupFiles.CreatePath(root), "project root creation");
        Check(setupFiles.CreatePath(workspace.assets), "asset root creation");
        Check(setupFiles.CreatePath(workspace.derivedData), "derived-data root creation");
        Check(setupFiles.CreatePath(workspace.intermediate), "intermediate root creation");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(workspace, "linux-x64", &failure) == nano::ProjectAssetContextResult::InvalidArgument,
              "unsupported target fails closed before backend acquisition");
        Check(!context.IsOpen(), "unsupported target leaves context closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
              "unsupported target acquires no process facilities");
        const nano::ProjectAssetContextResult openResult = context.Open(workspace, "windows-x64", &failure);
        if (openResult != nano::ProjectAssetContextResult::Success)
            std::fprintf(stderr,
                         "[nanovanguardProjectAssetTests] open result=%u stage=%u sources=%u pipeline=%u failure=%u build=%u publication=%u operation=%u\n",
                         static_cast<u32>(openResult), static_cast<u32>(failure.stage), static_cast<u32>(failure.sources), static_cast<u32>(failure.pipeline),
                         static_cast<u32>(failure.pipelineFailure.result), static_cast<u32>(failure.pipelineFailure.build),
                         static_cast<u32>(failure.pipelineFailure.publication.result), static_cast<u32>(failure.pipelineFailure.operation.result));
        Check(openResult == nano::ProjectAssetContextResult::Success,
              "GPU-free project-asset context opens");
        Check(context.IsOpen(), "opened context reports live project pipeline");
        Check(context.GetPipeline() != nullptr && context.GetPipeline()->IsInitialized(),
              "opened context exposes initialized shared project pipeline");
        Check(context.GetSources() != nullptr, "opened context owns source database");

        const asset_streaming::ProjectAssetPipelineConfig* const config = context.GetConfig();
        Check(config != nullptr, "opened context exposes derived configuration");
        if (config != nullptr)
        {
            const filesystem::AbsolutePath derivedOwner = workspace.derivedData.AddDirPath("ProjectAssets");
            const filesystem::AbsolutePath intermediateOwner = workspace.intermediate.AddDirPath("ProjectAssets");
            constexpr char identity[] = "vanguard.editor.project-assets.texture.v1";
            Check(config->build.artifactCacheRoot == derivedOwner.AddDirPath("VDDC"),
                  "headless config VDDC matches editor identity");
            Check(config->build.dependencyIndexRoot == derivedOwner.AddDirPath("Index"),
                  "headless config index matches editor identity");
            Check(config->publication.looseRoot == intermediateOwner.AddDirPath("Loose"),
                  "headless config loose root matches editor identity");
            Check(config->operationJournalPath == intermediateOwner.AddFilePath("operations.vtxn"),
                  "headless config journal matches editor identity");
            Check(config->build.target == assets::TargetPlatform::WindowsD3D12 &&
                      config->publication.target == assets::TargetPlatform::WindowsD3D12,
                  "headless config target matches editor identity");
            Check(config->build.settingsFingerprint == crypto::Sha256(identity, sizeof(identity) - 1u),
                  "headless config fingerprint matches existing editor fingerprint");
            Check(config->maximumOperationHistoryBytes == config->maximumOperationFileBytes,
                  "headless config preserves editor operation-history byte bound");
            Check(!config->automaticRecook.enabled,
                  "headless config preserves editor automatic-recook default");
        }

        Check(context.Close() == nano::ProjectAssetContextResult::Success,
              "GPU-free project-asset context closes");
        Check(!context.IsOpen(), "closed context reports closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized() &&
                  !texture_tools::IsInitialized() && !mesh_tools::IsInitialized(),
              "clean close releases context-owned facilities including mesh/texture tool acquisitions");
        Check(context.Close() == nano::ProjectAssetContextResult::Success,
              "cleanly closed context has idempotent Close");
    }
    else if (rollbackMode)
    {
        const filesystem::AbsolutePath missingRoot = scratch.AddDirPath("missing-project");
        const projects::ProjectWorkspace missingWorkspace = MakeWorkspace(missingRoot);
        if (DirectoryExists(setupFiles, missingRoot))
            Check(DeleteDirectoryTree(setupFiles, missingRoot), "previous missing-root fixture cleanup");

        Check(texture_tools::Initialize() && mesh_tools::Initialize(),
              "rollback proof acquires inherited mesh/texture tool owners");
        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(missingWorkspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::SourceScanFailed,
              "source-scan failure rolls backend acquisition back");
        Check(failure.stage == nano::ProjectAssetContextStage::SourceScan &&
                  failure.sources != assets::SourceDatabaseResult::Success,
              "source-scan failure preserves exact stage/source evidence");
        Check(!context.IsOpen(), "failed source scan leaves context closed");
        Check(!diagnostics::IsInitialized() && !vanguard::io::IsInitialized() && !filesystem::IsInitialized() && !jobs::IsInitialized(),
              "failed source scan releases context-owned restartable facilities");
        Check(texture_tools::IsInitialized() && mesh_tools::IsInitialized(),
              "failed context releases only its mesh/texture references and preserves inherited owners");
        Check(mesh_tools::Shutdown() && texture_tools::Shutdown() && !mesh_tools::IsInitialized() && !texture_tools::IsInitialized(),
              "rollback proof releases inherited mesh/texture owners explicitly");
    }
    else if (inheritedMode)
    {
        Check(setupFiles.CreatePath(root), "project root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.assets), "asset root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.derivedData), "derived-data root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(workspace.intermediate), "intermediate root creation for inherited-owner proof");
        Check(setupFiles.CreatePath(alternateRoot), "alternate root creation");
        const filesystem::AbsolutePath alternateCache = alternateRoot.AddDirPath("Cache");
        Check(setupFiles.CreatePath(alternateCache), "alternate cache creation");
        Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "nanovanguardProjectAssetTests"),
              "inherited diagnostics initialization");
        Check(vanguard::io::Initialize(), "inherited I/O initialization");
        Check(filesystem::Initialize({alternateRoot, alternateRoot, alternateCache}),
              "inherited filesystem initialization");

        nano::ProjectAssetContext context;
        nano::ProjectAssetContextFailure failure;
        Check(context.Open(workspace, "windows-x64", &failure) == nano::ProjectAssetContextResult::InvalidState,
              "incompatible inherited filesystem fails closed");
        Check(failure.stage == nano::ProjectAssetContextStage::Filesystem,
              "incompatible inherited filesystem reports filesystem stage");
        Check(!context.IsOpen(), "incompatible inherited filesystem leaves context closed");
        Check(diagnostics::IsInitialized() && vanguard::io::IsInitialized() && filesystem::IsInitialized(),
              "failed context does not release inherited diagnostics/I/O/filesystem owners");
        Check(!jobs::IsInitialized(),
              "failed inherited-filesystem check does not acquire later jobs owner");

        filesystem::Shutdown();
        vanguard::io::Shutdown();
        diagnostics::Shutdown();
    }

    if (!moveRestartPrepare && !p5RestartPrepare && !p5RestartVerify && DirectoryExists(setupFiles, root))
        Check(DeleteDirectoryTree(setupFiles, root), "project fixture cleanup");
    if (!moveRestartPrepare && !p5RestartPrepare && !p5RestartVerify)
        Check(!DirectoryExists(setupFiles, root), "completed mode leaves no project/cache residue in the approved scratch root");
    if (DirectoryExists(setupFiles, alternateRoot))
        Check(DeleteDirectoryTree(setupFiles, alternateRoot), "alternate fixture cleanup");

    std::fprintf(stderr, "[nanovanguardProjectAssetTests] finished with %u failed checks\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
