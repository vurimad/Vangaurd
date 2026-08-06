#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/prefabs/prefabs.hpp>

#include <array>
#include <cstddef>
#include <cstdio>

namespace
{
    namespace prefab = vanguard::prefabs;
    namespace reflection = vanguard::reflection;
    namespace resources = vanguard::resources;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[prefabsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    bool Equal(const ByteArray& left, const ByteArray& right) noexcept
    {
        if (left.Size() != right.Size()) return false;
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
            if (left[index] != right[index]) return false;
        return true;
    }

    struct TransformComponent
    {
        vanguard::f32 translation[3]{};
    };

    struct RenderComponent
    {
        resources::ResourceReference mesh;
    };

    constexpr reflection::SchemaTypeId TransformType = reflection::HashSchemaName("vanguard.transform_component");
    constexpr reflection::SchemaTypeId RenderType = reflection::HashSchemaName("vanguard.render_component");

    const std::array<reflection::SchemaField, 1> TransformFields{{
        reflection::MakeField("translation", reflection::builtin::Blob, reflection::ValueKind::Blob,
                              static_cast<vanguard::u32>(offsetof(TransformComponent, translation)),
                              static_cast<vanguard::u32>(sizeof(TransformComponent::translation)),
                              static_cast<vanguard::u32>(alignof(vanguard::f32)), 1, 0,
                              reflection::FieldFlags::Required)}};

    const std::array<reflection::SchemaField, 1> RenderFields{{
        reflection::MakeField("mesh", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              static_cast<vanguard::u32>(offsetof(RenderComponent, mesh)),
                              static_cast<vanguard::u32>(sizeof(resources::ResourceReference)),
                              static_cast<vanguard::u32>(alignof(resources::ResourceReference)), 1, 0,
                              reflection::FieldFlags::Required)}};

    const reflection::Schema TransformSchema{TransformType, "vanguard.transform_component", sizeof(TransformComponent),
                                             alignof(TransformComponent), 1, 1, TransformFields.data(),
                                             static_cast<vanguard::u32>(TransformFields.size())};
    const reflection::Schema RenderSchema{RenderType, "vanguard.render_component", sizeof(RenderComponent),
                                          alignof(RenderComponent), 1, 1, RenderFields.data(),
                                          static_cast<vanguard::u32>(RenderFields.size())};
}

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace memory = vanguard::memory;
    namespace schemas = vanguard::schemas;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "prefabsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");
    Check(reflection::Initialize(), "reflection initialization");
    Check(reflection::RegisterSchema(TransformSchema), "register transform schema");
    Check(reflection::RegisterSchema(RenderSchema), "register render schema");

    TransformComponent transform{{1.0f, 2.0f, 3.0f}};
    const resources::ResourceReference mesh(resources::ResourcePath::FromString("meshes/vehicle.vmesh"),
                                             vanguard::serialization::MakeFourCC('V', 'M', 'S', 'H'));
    RenderComponent render{mesh};
    const std::array<prefab::EntityBuildRecord, 2> entities{{
        {20, 10, 0x2000, prefab::EntityFlags::DisabledByDefault},
        {10, prefab::InvalidStableId, 0x1000, prefab::EntityFlags::None}}};
    const std::array<prefab::ComponentBuildRecord, 2> components{{
        {200, 20, &RenderSchema, &render, prefab::ComponentFlags::None},
        {100, 10, &TransformSchema, &transform, prefab::ComponentFlags::None}}};
    const std::array<prefab::ExplicitDependency, 1> explicitDependencies{{
        {mesh, resources::DependencyKind::Soft}}};
    prefab::CookDescription description;
    description.name = 0x12345678;
    description.entities = {entities.data(), static_cast<vanguard::u32>(entities.size())};
    description.components = {components.data(), static_cast<vanguard::u32>(components.size())};
    description.explicitDependencies = {explicitDependencies.data(),
                                        static_cast<vanguard::u32>(explicitDependencies.size())};
    description.sourceFingerprint = vanguard::crypto::Sha256("prefab source", 13);

    ByteArray first(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter firstWriter(first);
    Check(prefab::CookPrefab(description, firstWriter) == prefab::Result::Success, "cook prefab");

    const std::array<prefab::EntityBuildRecord, 2> reorderedEntities{{entities[1], entities[0]}};
    const std::array<prefab::ComponentBuildRecord, 2> reorderedComponents{{components[1], components[0]}};
    prefab::CookDescription reordered = description;
    reordered.entities = {reorderedEntities.data(), static_cast<vanguard::u32>(reorderedEntities.size())};
    reordered.components = {reorderedComponents.data(), static_cast<vanguard::u32>(reorderedComponents.size())};
    ByteArray second(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter secondWriter(second);
    Check(prefab::CookPrefab(reordered, secondWriter) == prefab::Result::Success, "cook reordered prefab");
    Check(Equal(first, second), "canonical bytes are independent of input order");

    filesystem::MemoryFileReader reader(first, 0);
    prefab::PrefabFile file;
    Check(file.Open(reader) == prefab::Result::Success, "open cooked prefab");
    Check(file.IsOpen() && file.Name() == description.name, "prefab identity");
    Check(file.Entities().Size() == 2 && file.Components().Size() == 2, "entity and component counts");
    Check(file.Entities()[0].stableId == 10 && file.Entities()[0].componentCount == 1,
          "root entity and grouped component");
    Check(file.Entities()[1].parentStableId == 10, "child hierarchy");
    Check(file.Dependencies().Size() == 1 && file.Dependencies()[0].resource == mesh &&
              file.Dependencies()[0].kind == resources::DependencyKind::Required,
          "schema dependency extraction and strongest-kind coalescing");
    Check(file.FindEntity(20) != nullptr && file.FindComponent(200) != nullptr, "stable identity lookup");

    const prefab::ComponentRecord* const transformRecord = file.FindComponent(100);
    Check(transformRecord != nullptr && transformRecord->schema == TransformType, "component schema identity");
    if (transformRecord != nullptr)
    {
        const auto bytes = file.ComponentData(*transformRecord);
        ByteArray componentBytes(memory::pools::Serialization::GetInstance());
        componentBytes.Resize(bytes.Size());
        for (vanguard::u32 index = 0; index < bytes.Size(); ++index) componentBytes[index] = bytes[index];
        filesystem::MemoryFileReader componentFile(componentBytes, 0);
        vanguard::serialization::BinaryReader componentReader(componentFile);
        TransformComponent decoded;
        Check(schemas::ReadObject(componentReader, TransformSchema, &decoded) == schemas::Result::Success,
              "deserialize component through schema");
        Check(decoded.translation[0] == 1.0f && decoded.translation[1] == 2.0f && decoded.translation[2] == 3.0f,
              "component data round trip");
    }

    std::array<vanguard::packages::Dependency, 1> packageDependencies{{
        {mesh.Path().Id(), mesh.ExpectedType(), resources::DependencyKind::Required}}};
    const vanguard::packages::BuildSegment prefabSegment{
        first.TypedData(), first.Size(), vanguard::packages::Codec::Lz4, 4,
        vanguard::packages::SegmentFlags::Inline | vanguard::packages::SegmentFlags::MemoryResident};
    vanguard::packages::BuildResource packagedPrefab;
    packagedPrefab.path = "entities/vehicle.vprefab";
    packagedPrefab.type = prefab::PrefabResourceType;
    packagedPrefab.flags = vanguard::packages::ResourceFlags::Streamable;
    packagedPrefab.segments = {&prefabSegment, 1};
    packagedPrefab.dependencies = {packageDependencies.data(), static_cast<vanguard::u32>(packageDependencies.size())};
    ByteArray packageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packageFile(packageBytes);
    vanguard::packages::PackageWriter packageWriter;
    Check(packageWriter.Begin(packageFile) == vanguard::packages::Result::Success &&
              packageWriter.Add(packagedPrefab) == vanguard::packages::Result::Success &&
              packageWriter.Finalize() == vanguard::packages::Result::Success,
          "package vprefab as an opaque VPAK resource");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    vanguard::packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == vanguard::packages::Result::Success,
          "open VPAK containing vprefab");
    const vanguard::packages::Resource* const packagedRecord = packageReader.Find("entities/vehicle.vprefab");
    Check(packagedRecord != nullptr && packagedRecord->type == prefab::PrefabResourceType &&
              packageReader.Dependencies(*packagedRecord).Size() == 1,
          "VPAK preserves prefab dependencies without interpreting component data");
    if (packagedRecord != nullptr)
    {
        vanguard::packages::ResourceFileReader packagedView;
        Check(packagedView.Open(packageReader, *packagedRecord, packageReaderFile) == vanguard::packages::Result::Success,
              "open logical vprefab directly over VPAK");
        prefab::PrefabFile packaged;
        Check(packaged.Open(packagedView) == prefab::Result::Success &&
                  packaged.ContentFingerprint() == file.ContentFingerprint(),
              "package-backed vprefab opens without format translation");
    }

    auto duplicateEntities = entities;
    duplicateEntities[0].stableId = duplicateEntities[1].stableId;
    prefab::CookDescription invalid = description;
    invalid.entities = {duplicateEntities.data(), static_cast<vanguard::u32>(duplicateEntities.size())};
    ByteArray scratch(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter scratchWriter(scratch);
    Check(prefab::CookPrefab(invalid, scratchWriter) == prefab::Result::DuplicateIdentifier,
          "reject duplicate stable entity ID");

    auto missingParent = entities;
    missingParent[0].parentStableId = 999;
    invalid.entities = {missingParent.data(), static_cast<vanguard::u32>(missingParent.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter missingWriter(scratch);
    Check(prefab::CookPrefab(invalid, missingWriter) == prefab::Result::MissingParent, "reject missing parent");

    auto cyclic = entities;
    cyclic[1].parentStableId = 20;
    invalid.entities = {cyclic.data(), static_cast<vanguard::u32>(cyclic.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter cycleWriter(scratch);
    Check(prefab::CookPrefab(invalid, cycleWriter) == prefab::Result::HierarchyCycle, "reject hierarchy cycle");

    ByteArray corrupted(memory::pools::World::GetInstance());
    corrupted = first;
    if (corrupted.Size() > 64) corrupted[64] ^= 0x80u;
    filesystem::MemoryFileReader corruptedReader(corrupted, 0);
    prefab::PrefabFile corruptedFile;
    Check(corruptedFile.Open(corruptedReader) == prefab::Result::IntegrityFailure, "reject corrupted prefab");

    file.Close();
    corruptedFile.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    std::printf("prefabsTests: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
