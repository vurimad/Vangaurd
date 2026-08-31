#include <vanguard/containers/containers.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/mesh_tools/mesh_asset_compiler.hpp>
#include <vanguard/mesh_tools/mesh_import.hpp>
#include <vanguard/mesh_tools/mesh_tools.hpp>
#include <vanguard/packages/packages.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

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
} // namespace

int main()
{
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

    const filesystem::AbsolutePath importTestRoot = filesystem::paths::GetCurrentWorkingDirectory();
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
    Check(meshBuildSystem.Prepare(meshBuildRequest, meshBuildPlan) == vanguard::assets::Result::Success &&
              meshBuildPlan.SetGeneratedDependencyContent(importedMaterial, materialContent) == vanguard::assets::Result::Success,
          "prepare mesh build and resolve its generated material dependency");
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

    const filesystem::AbsolutePath bunnySourcePath = importTestRoot.AddFilePath("tests/data_assets/bunny/bunny.obj");
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
