#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/world/cells.hpp>
#include <vanguard/world/streaming_executor.hpp>
#include <vanguard/world/streaming_grid.hpp>
#include <vanguard/world/worlds.hpp>

#include <array>
#include <cstddef>
#include <cstdio>

namespace
{
    namespace packages = vanguard::packages;
    namespace reflection = vanguard::reflection;
    namespace resources = vanguard::resources;
    namespace world = vanguard::world;
    using ByteArray = vanguard::containers::DynamicArray<vanguard::u8>;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[worldTests] FAILED: %s\n", message);
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

    struct VisualOverride
    {
        resources::ResourceReference material;
        vanguard::f32 tint[4]{};
    };

    class StreamingTestResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(vanguard::memory::pools::Resources);

        StreamingTestResource(const resources::ResourceTypeId type, const resources::ResourceId identity) noexcept
            : m_type(type), m_identity(identity)
        {
        }

        [[nodiscard]] resources::ResourceTypeId Type() const noexcept override { return m_type; }
        [[nodiscard]] resources::ResourceId Identity() const noexcept { return m_identity; }

    private:
        resources::ResourceTypeId m_type = resources::InvalidResourceTypeId;
        resources::ResourceId m_identity = resources::InvalidResourceId;
    };

    struct StreamingLoaderHarness
    {
        resources::ResourceId failure = resources::InvalidResourceId;
        resources::ResourceId slow = resources::InvalidResourceId;
        vanguard::concurrency::ManualResetEvent slowEntered{false};
        vanguard::concurrency::ManualResetEvent releaseSlow{false};
        vanguard::concurrency::Atomic<vanguard::u32> constructions{0};
        vanguard::concurrency::Atomic<vanguard::u32> destructions{0};
        vanguard::concurrency::Atomic<vanguard::u32> sharedPriority{
            static_cast<vanguard::u32>(resources::LoadPriority::Background)};
    };

    resources::Failure DiscoverStreamingDependencies(const resources::ResourceReference, resources::DependencyBuilder&,
                                                      void*) noexcept
    {
        return resources::Failure::None;
    }

    resources::ResourceObject* ConstructStreamingResource(const resources::LoadContext& context,
                                                           resources::Failure& failure, void* const userData) noexcept
    {
        auto& harness = *static_cast<StreamingLoaderHarness*>(userData);
        const resources::ResourceReference reference = context.Reference();
        if (reference.Path().Id() == harness.failure)
        {
            failure = resources::Failure::IoFailure;
            return nullptr;
        }
        if (reference.Path().Id() == harness.slow)
        {
            harness.slowEntered.Signal();
            harness.releaseSlow.Wait();
            if (context.IsCancellationRequested())
            {
                failure = resources::Failure::Cancelled;
                return nullptr;
            }
        }
        if (reference.Path().Id() != harness.slow)
            harness.sharedPriority.SetValue(static_cast<vanguard::u32>(context.Priority()));
        static_cast<void>(harness.constructions.Increment());
        return VANGUARD_NEW(StreamingTestResource)(reference.ExpectedType(), reference.Path().Id());
    }

    void DestroyStreamingResource(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        auto& harness = *static_cast<StreamingLoaderHarness*>(userData);
        static_cast<void>(harness.destructions.Increment());
        VANGUARD_DELETE(static_cast<StreamingTestResource*>(resource));
    }

    constexpr reflection::SchemaTypeId VisualOverrideType =
        reflection::HashSchemaName("vanguard.visual_instance_override");
    const std::array<reflection::SchemaField, 2> VisualOverrideFields{{
        reflection::MakeField("material", reflection::builtin::ResourceReference,
                              reflection::ValueKind::ResourceReference,
                              static_cast<vanguard::u32>(offsetof(VisualOverride, material)),
                              static_cast<vanguard::u32>(sizeof(resources::ResourceReference)),
                              static_cast<vanguard::u32>(alignof(resources::ResourceReference)), 1, 0,
                              reflection::FieldFlags::Required),
        reflection::MakeField("tint", reflection::builtin::Blob, reflection::ValueKind::Blob,
                              static_cast<vanguard::u32>(offsetof(VisualOverride, tint)),
                              static_cast<vanguard::u32>(sizeof(VisualOverride::tint)),
                              static_cast<vanguard::u32>(alignof(vanguard::f32)), 1, 0,
                              reflection::FieldFlags::Required)}};
    const reflection::Schema VisualOverrideSchema{
        VisualOverrideType, "vanguard.visual_instance_override", sizeof(VisualOverride), alignof(VisualOverride), 1, 1,
        VisualOverrideFields.data(), static_cast<vanguard::u32>(VisualOverrideFields.size())};

    world::Bounds MakeBounds(const vanguard::f32 minimum, const vanguard::f32 maximum) noexcept
    {
        world::Bounds bounds;
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = minimum;
            bounds.maximum[axis] = maximum;
        }
        return bounds;
    }

    world::WorldBounds MakeWorldBounds(const vanguard::f64 minimum, const vanguard::f64 maximum) noexcept
    {
        world::WorldBounds bounds;
        for (vanguard::u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = minimum;
            bounds.maximum[axis] = maximum;
        }
        return bounds;
    }

    world::PlacementBuildRecord MakePlacement(const vanguard::u64 entityId, const vanguard::u64 parentId,
                                               const vanguard::u64 group,
                                               const resources::ResourceReference prefab) noexcept
    {
        world::PlacementBuildRecord placement;
        placement.entityId = entityId;
        placement.parentEntityId = parentId;
        placement.name = entityId + 0x1000;
        placement.activationGroup = group;
        placement.prefab = prefab;
        placement.transform.translation[0] = static_cast<vanguard::f32>(entityId);
        placement.bounds = MakeBounds(-1.0f, 1.0f);
        placement.streamingReferencePoint[0] = placement.transform.translation[0];
        placement.streamingDistance = 250.0f;
        placement.visibilityDistance = 300.0f;
        return placement;
    }
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
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "worldTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath root = filesystem::paths::GetCurrentWorkingDirectory();
    Check(filesystem::Initialize({root, root, root}), "filesystem initialization");
    Check(reflection::Initialize(), "reflection initialization");
    Check(reflection::RegisterSchema(VisualOverrideSchema), "register override schema");

    const resources::ResourceReference vehiclePrefab(resources::ResourcePath::FromString("entities/vehicle.vprefab"),
                                                       vanguard::prefabs::PrefabResourceType);
    const resources::ResourceReference propPrefab(resources::ResourcePath::FromString("entities/prop.vprefab"),
                                                    vanguard::prefabs::PrefabResourceType);
    const resources::ResourceReference material(resources::ResourcePath::FromString("materials/red.vmat"),
                                                 vanguard::serialization::MakeFourCC('V', 'M', 'A', 'T'));
    VisualOverride visual{material, {1.0f, 0.0f, 0.0f, 1.0f}};
    const std::array<world::ActivationGroupBuildRecord, 1> groups{{
        {7, 0x7000, world::ActivationGroupFlags::DefaultActive}}};
    const std::array<world::PlacementBuildRecord, 3> placements{{
        MakePlacement(300, world::InvalidEntityId, 7, propPrefab),
        MakePlacement(100, world::InvalidEntityId, world::AlwaysActiveGroup, vehiclePrefab),
        MakePlacement(200, 100, 7, propPrefab)}};
    const std::array<world::ComponentOverrideBuildRecord, 2> overrides{{
        {200, 0x2200, world::OverrideMode::Remove, nullptr, nullptr, world::OverrideFlags::None},
        {100, 0x1100, world::OverrideMode::Replace, &VisualOverrideSchema, &visual, world::OverrideFlags::None}}};
    const std::array<world::EntityReferenceBuildRecord, 2> references{{
        {200, 0x2001, 300, world::EntityReferenceKind::RequiredLocal},
        {100, 0x1001, 9999, world::EntityReferenceKind::OptionalWorld}}};
    const std::array<world::ExplicitDependency, 1> explicitDependencies{{
        {material, resources::DependencyKind::Soft}}};

    world::CellBuildDescription description;
    description.cellId = 0xabc;
    description.worldId = 0xdef;
    description.gridCoordinate[0] = 12;
    description.gridCoordinate[1] = -4;
    description.hierarchyLevel = 2;
    description.category = world::CellCategory::Exterior;
    description.origin[0] = 123456789.0;
    description.origin[1] = -987654321.0;
    description.bounds = MakeBounds(-512.0f, 512.0f);
    description.activationGroups = {groups.data(), static_cast<vanguard::u32>(groups.size())};
    description.placements = {placements.data(), static_cast<vanguard::u32>(placements.size())};
    description.overrides = {overrides.data(), static_cast<vanguard::u32>(overrides.size())};
    description.entityReferences = {references.data(), static_cast<vanguard::u32>(references.size())};
    description.explicitDependencies = {explicitDependencies.data(),
                                        static_cast<vanguard::u32>(explicitDependencies.size())};
    description.sourceFingerprint = vanguard::crypto::Sha256("cell source", 11);

    ByteArray first(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter firstWriter(first);
    Check(world::CookCell(description, firstWriter) == world::Result::Success, "cook vcell");

    const std::array<world::PlacementBuildRecord, 3> reorderedPlacements{{placements[1], placements[2], placements[0]}};
    const std::array<world::ComponentOverrideBuildRecord, 2> reorderedOverrides{{overrides[1], overrides[0]}};
    const std::array<world::EntityReferenceBuildRecord, 2> reorderedReferences{{references[1], references[0]}};
    world::CellBuildDescription reordered = description;
    reordered.placements = {reorderedPlacements.data(), static_cast<vanguard::u32>(reorderedPlacements.size())};
    reordered.overrides = {reorderedOverrides.data(), static_cast<vanguard::u32>(reorderedOverrides.size())};
    reordered.entityReferences = {reorderedReferences.data(), static_cast<vanguard::u32>(reorderedReferences.size())};
    ByteArray second(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter secondWriter(second);
    Check(world::CookCell(reordered, secondWriter) == world::Result::Success, "cook reordered vcell");
    Check(Equal(first, second), "canonical cell bytes are independent of input order");

    filesystem::MemoryFileReader reader(first, 0);
    world::CellFile file;
    Check(file.Open(reader) == world::Result::Success, "open cooked vcell");
    Check(file.CellId() == description.cellId && file.WorldId() == description.worldId, "cell and world identity");
    Check(file.Origin()[0] == description.origin[0] && file.GridCoordinate()[1] == -4, "large-world origin and grid coordinate");
    Check(file.ActivationGroups().Size() == 2 && file.ActivationGroups()[0].placementCount == 1 &&
              file.ActivationGroups()[1].placementCount == 2,
          "always-active and variant placement ranges");
    Check(file.PlacementsInGroup(file.ActivationGroups()[1]).Size() == 2,
          "direct contiguous activation-group placement view");
    Check(file.FindPlacement(100) != nullptr && file.FindPlacement(200) != nullptr &&
              file.FindPlacement(200)->parentEntityId == 100,
          "stable placement lookup and hierarchy");
    Check(file.FindPlacement(100)->firstOverride == 0 && file.FindPlacement(100)->overrideCount == 1 &&
              file.FindPlacement(300)->overrideCount == 0 && file.FindPlacement(300)->firstOverride == 2,
          "contiguous sparse override ranges including empty ranges");
    Check(file.FindPlacement(100)->referenceCount == 1 && file.FindPlacement(200)->referenceCount == 1,
          "contiguous entity-reference ranges");
    Check(file.OverridesFor(*file.FindPlacement(100)).Size() == 1 &&
              file.ReferencesFor(*file.FindPlacement(200)).Size() == 1,
          "direct placement override and reference views");

    const world::ComponentOverrideRecord& visualRecord = file.Overrides()[0];
    Check(visualRecord.mode == world::OverrideMode::Replace && visualRecord.schema == VisualOverrideType,
          "typed replacement override");
    const auto visualBytes = file.OverrideData(visualRecord);
    ByteArray serializedVisual(memory::pools::Serialization::GetInstance());
    serializedVisual.Resize(visualBytes.Size());
    for (vanguard::u32 index = 0; index < visualBytes.Size(); ++index) serializedVisual[index] = visualBytes[index];
    filesystem::MemoryFileReader visualFile(serializedVisual, 0);
    vanguard::serialization::BinaryReader visualReader(visualFile);
    VisualOverride decoded;
    Check(schemas::ReadObject(visualReader, VisualOverrideSchema, &decoded) == schemas::Result::Success &&
              decoded.material == material && decoded.tint[0] == 1.0f,
          "schema override round trip");
    Check(file.Overrides()[1].mode == world::OverrideMode::Remove && file.OverrideData(file.Overrides()[1]).Empty(),
          "component removal has no payload");
    Check(file.Dependencies().Size() == 3, "prefab and override resources are deduplicated dependencies");

    std::array<packages::Dependency, 8> packageDependencies{};
    vanguard::u32 packageDependencyCount = 0;
    for (const world::DependencyRecord& dependency : file.Dependencies())
        packageDependencies[packageDependencyCount++] =
            {dependency.resource.Path().Id(), dependency.resource.ExpectedType(), dependency.kind};
    const packages::BuildSegment cellSegment{first.TypedData(), first.Size(), packages::Codec::Lz4, 4,
                                              packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
    packages::BuildResource packagedCell;
    packagedCell.path = "world/cells/12_-4_0.vcell";
    packagedCell.type = world::CellResourceType;
    packagedCell.flags = packages::ResourceFlags::Streamable;
    packagedCell.segments = {&cellSegment, 1};
    packagedCell.dependencies = {packageDependencies.data(), packageDependencyCount};
    ByteArray packageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter packageFile(packageBytes);
    packages::PackageWriter packageWriter;
    Check(packageWriter.Begin(packageFile) == packages::Result::Success &&
              packageWriter.Add(packagedCell) == packages::Result::Success &&
              packageWriter.Finalize() == packages::Result::Success,
          "package vcell as an opaque VPAK resource");
    filesystem::MemoryFileReader packageReaderFile(packageBytes, 0);
    packages::PackageReader packageReader;
    Check(packageReader.Open(packageReaderFile) == packages::Result::Success, "open VPAK containing vcell");
    const packages::Resource* packagedRecord = packageReader.Find("world/cells/12_-4_0.vcell");
    Check(packagedRecord != nullptr && packagedRecord->type == world::CellResourceType &&
              packageReader.Dependencies(*packagedRecord).Size() == file.Dependencies().Size(),
          "VPAK preserves cell dependency metadata");
    if (packagedRecord != nullptr)
    {
        packages::ResourceFileReader packagedView;
        Check(packagedView.Open(packageReader, *packagedRecord, packageReaderFile) == packages::Result::Success,
              "open logical vcell over VPAK");
        world::CellFile packaged;
        Check(packaged.Open(packagedView) == world::Result::Success &&
                  packaged.ContentFingerprint() == file.ContentFingerprint(),
              "package-backed vcell opens without translation");
    }

    auto invalidPlacements = placements;
    invalidPlacements[0].entityId = invalidPlacements[1].entityId;
    world::CellBuildDescription invalid = description;
    invalid.placements = {invalidPlacements.data(), static_cast<vanguard::u32>(invalidPlacements.size())};
    ByteArray scratch(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter duplicateWriter(scratch);
    Check(world::CookCell(invalid, duplicateWriter) == world::Result::DuplicateIdentifier,
          "reject duplicate entity identity");

    invalidPlacements = placements;
    invalidPlacements[2].parentEntityId = 999;
    invalid.placements = {invalidPlacements.data(), static_cast<vanguard::u32>(invalidPlacements.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter parentWriter(scratch);
    Check(world::CookCell(invalid, parentWriter) == world::Result::MissingParent, "reject missing hierarchy parent");

    invalidPlacements = placements;
    invalidPlacements[0].parentEntityId = 200;
    invalidPlacements[2].parentEntityId = 300;
    invalid.placements = {invalidPlacements.data(), static_cast<vanguard::u32>(invalidPlacements.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter cycleWriter(scratch);
    Check(world::CookCell(invalid, cycleWriter) == world::Result::HierarchyCycle, "reject hierarchy cycle");

    auto invalidReferences = references;
    invalidReferences[0].targetEntityId = 999;
    invalid = description;
    invalid.entityReferences = {invalidReferences.data(), static_cast<vanguard::u32>(invalidReferences.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter referenceWriter(scratch);
    Check(world::CookCell(invalid, referenceWriter) == world::Result::InvalidReference,
          "reject unresolved local entity reference");

    ByteArray corrupted(memory::pools::World::GetInstance());
    corrupted = first;
    if (corrupted.Size() > 80) corrupted[80] ^= 0x40u;
    filesystem::MemoryFileReader corruptedReader(corrupted, 0);
    world::CellFile corruptedFile;
    Check(corruptedFile.Open(corruptedReader) == world::Result::IntegrityFailure, "reject corrupted vcell");

    const resources::ResourceReference coarseCellResource(
        resources::ResourcePath::FromString("world/cells/city_coarse.vcell"), world::CellResourceType);
    const resources::ResourceReference eastCellResource(
        resources::ResourcePath::FromString("world/cells/city_east.vcell"), world::CellResourceType);
    const resources::ResourceReference westCellResource(
        resources::ResourcePath::FromString("world/cells/city_west.vcell"), world::CellResourceType);
    const resources::ResourceReference skylineMesh(
        resources::ResourcePath::FromString("world/proxies/city_skyline.vmesh"), vanguard::meshes::MeshResourceType);
    const resources::ResourceReference towerMesh(
        resources::ResourcePath::FromString("world/proxies/tower_mid.vmesh"), vanguard::meshes::MeshResourceType);

    std::array<world::WorldCellBuildRecord, 3> worldCells{};
    worldCells[0].cellId = 20;
    worldCells[0].parentCellId = 10;
    worldCells[0].cell = eastCellResource;
    worldCells[0].hierarchyLevel = 0;
    worldCells[0].bounds = MakeWorldBounds(0.0, 1000.0);
    worldCells[0].streamingReferencePoint[0] = 500.0;
    worldCells[0].activationDistance = 750.0f;
    worldCells[0].retentionDistance = 900.0f;
    worldCells[1].cellId = 10;
    worldCells[1].cell = coarseCellResource;
    worldCells[1].hierarchyLevel = 2;
    worldCells[1].bounds = MakeWorldBounds(-1000.0, 1000.0);
    worldCells[1].activationDistance = 5000.0f;
    worldCells[1].retentionDistance = 5500.0f;
    worldCells[2].cellId = 30;
    worldCells[2].parentCellId = 10;
    worldCells[2].cell = westCellResource;
    worldCells[2].hierarchyLevel = 0;
    worldCells[2].bounds = MakeWorldBounds(-1000.0, 0.0);
    worldCells[2].streamingReferencePoint[0] = -500.0;
    worldCells[2].activationDistance = 750.0f;
    worldCells[2].retentionDistance = 900.0f;

    std::array<world::DistantProxyBuildRecord, 2> distantProxies{};
    distantProxies[0].proxyId = 200;
    distantProxies[0].parentProxyId = 100;
    distantProxies[0].ownerCellId = 10;
    distantProxies[0].mesh = towerMesh;
    distantProxies[0].bounds = MakeWorldBounds(-250.0, 250.0);
    distantProxies[0].streamingDistance = 2600.0f;
    distantProxies[0].secondaryReferencePointDistance = 2400.0f;
    distantProxies[0].nearHideDistance = 700.0f;
    distantProxies[0].streamingPriority = world::StreamingPriority::BuildingMesh;
    distantProxies[0].flags = world::DistantProxyFlags::Building;
    distantProxies[1].proxyId = 100;
    distantProxies[1].ownerCellId = 10;
    distantProxies[1].mesh = skylineMesh;
    distantProxies[1].bounds = MakeWorldBounds(-1000.0, 1000.0);
    distantProxies[1].streamingDistance = 6200.0f;
    distantProxies[1].secondaryReferencePointDistance = 6000.0f;
    distantProxies[1].nearHideDistance = 2200.0f;
    distantProxies[1].streamingPriority = world::StreamingPriority::Critical;
    distantProxies[1].flags = world::DistantProxyFlags::Building;

    const std::array<world::ProxyChildBuildRecord, 3> proxyChildren{{
        {200, 30, world::ProxyChildKind::Cell, world::ProxyChildFlags::RequiredForReplacement},
        {100, 200, world::ProxyChildKind::Proxy, world::ProxyChildFlags::RequiredForReplacement},
        {200, 20, world::ProxyChildKind::Cell, world::ProxyChildFlags::RequiredForReplacement}}};
    world::WorldBuildDescription worldDescription;
    worldDescription.worldId = description.worldId;
    worldDescription.bounds = MakeWorldBounds(-10000.0, 10000.0);
    worldDescription.cells = {worldCells.data(), static_cast<vanguard::u32>(worldCells.size())};
    worldDescription.distantProxies = {distantProxies.data(), static_cast<vanguard::u32>(distantProxies.size())};
    worldDescription.proxyChildren = {proxyChildren.data(), static_cast<vanguard::u32>(proxyChildren.size())};
    worldDescription.sourceFingerprint = vanguard::crypto::Sha256("world source", 12);

    ByteArray worldBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter worldWriter(worldBytes);
    Check(world::CookWorld(worldDescription, worldWriter) == world::Result::Success, "cook hierarchical vworld");
    const std::array<world::WorldCellBuildRecord, 3> reorderedWorldCells{{worldCells[2], worldCells[0], worldCells[1]}};
    const std::array<world::DistantProxyBuildRecord, 2> reorderedProxies{{distantProxies[1], distantProxies[0]}};
    const std::array<world::ProxyChildBuildRecord, 3> reorderedChildren{{proxyChildren[2], proxyChildren[0], proxyChildren[1]}};
    world::WorldBuildDescription reorderedWorld = worldDescription;
    reorderedWorld.cells = {reorderedWorldCells.data(), static_cast<vanguard::u32>(reorderedWorldCells.size())};
    reorderedWorld.distantProxies = {reorderedProxies.data(), static_cast<vanguard::u32>(reorderedProxies.size())};
    reorderedWorld.proxyChildren = {reorderedChildren.data(), static_cast<vanguard::u32>(reorderedChildren.size())};
    ByteArray reorderedWorldBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter reorderedWorldWriter(reorderedWorldBytes);
    Check(world::CookWorld(reorderedWorld, reorderedWorldWriter) == world::Result::Success &&
              Equal(worldBytes, reorderedWorldBytes),
          "canonical world bytes are independent of cell, proxy and child order");

    filesystem::MemoryFileReader worldReader(worldBytes, 0);
    world::WorldFile worldFile;
    Check(worldFile.Open(worldReader) == world::Result::Success, "open cooked vworld");
    const world::DistantProxyRecord* skyline = worldFile.FindDistantProxy(100);
    const world::DistantProxyRecord* tower = worldFile.FindDistantProxy(200);
    Check(worldFile.Cells().Size() == 3 && worldFile.FindCell(20) != nullptr &&
              worldFile.FindCell(20)->parentCellId == 10,
          "stable world cell lookup and coarse-to-fine hierarchy");
    Check(skyline != nullptr && tower != nullptr && tower->parentProxyId == skyline->proxyId &&
              worldFile.ChildrenOf(*skyline).Size() == 1 && worldFile.ChildrenOf(*tower).Size() == 2,
          "nested distant proxies and contiguous readiness children");
    Check(worldFile.Dependencies().Size() == 5, "vworld records streamable cell and proxy dependencies without eager ownership");

    world::WorldStreamingGrid streamingGrid;
    world::StreamingGridConfig streamingConfig;
    streamingConfig.maximumStreamInsPerUpdate = 8;
    Check(streamingGrid.Initialize(worldFile, streamingConfig), "initialize RED-style world streaming grid");
    containers::DynamicArray<world::StreamingCommand> streamingCommands(memory::pools::World::GetInstance());
    world::StreamingObserver streamingObserver;
    streamingObserver.predictedPosition[0] = 4000.0;
    world::StreamingProcessInput streamingInput;
    streamingInput.observers = {&streamingObserver, 1};
    streamingInput.cameraPosition[0] = 4000.0;
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 2 &&
              streamingCommands[0].key == world::StreamingNodeKey{100, world::StreamingNodeKind::DistantProxy} &&
              streamingCommands[0].priority == world::StreamingPriority::Critical,
          "far skyline proxy streams before ordinary coarse cell by RED priority");
    for (const world::StreamingCommand& command : streamingCommands)
        Check(streamingGrid.NotifyStreamInComplete(command.key, true, true), "complete far representation stream-in");

    streamingObserver.predictedPosition[0] = 1500.0;
    streamingInput.cameraPosition[0] = 1500.0;
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 1 &&
              streamingCommands[0].type == world::StreamingCommandType::StreamIn &&
              streamingCommands[0].key == world::StreamingNodeKey{200, world::StreamingNodeKind::DistantProxy} &&
              streamingGrid.IsAntiStreamingLocked(100),
          "coarse skyline remains anti-streaming locked while finer tower proxy loads");
    Check(streamingGrid.NotifyStreamInComplete(streamingCommands[0].key, true, true),
          "mark finer tower proxy render-ready");
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 1 &&
              streamingCommands[0].type == world::StreamingCommandType::StreamOut &&
              streamingCommands[0].key == world::StreamingNodeKey{100, world::StreamingNodeKind::DistantProxy} &&
              !streamingGrid.IsAntiStreamingLocked(100),
          "coarse skyline leaves only after required finer proxy is render-ready");
    Check(streamingGrid.NotifyStreamOutComplete(streamingCommands[0].key), "complete coarse proxy stream-out");

    streamingObserver.predictedPosition[0] = 0.0;
    streamingInput.cameraPosition[0] = 0.0;
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 2 &&
              streamingGrid.IsAntiStreamingLocked(200),
          "near cells stream while tower proxy remains locked against premature removal");
    for (const world::StreamingCommand& command : streamingCommands)
        Check(streamingGrid.NotifyStreamInComplete(command.key, true, true), "mark detailed cell render-ready");
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 1 &&
              streamingCommands[0].type == world::StreamingCommandType::StreamOut &&
              streamingCommands[0].key == world::StreamingNodeKey{200, world::StreamingNodeKind::DistantProxy},
          "tower proxy leaves only after every required detailed cell is render-ready");
    Check(streamingGrid.NotifyStreamOutComplete(streamingCommands[0].key), "complete tower proxy stream-out");
    Check(streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Empty() &&
              !streamingGrid.IsAntiStreamingLocked(100),
          "coarse proxy remains replaced through a ready descendant subtree without streaming back near camera");

    Check(streamingGrid.RequestShutdown() && streamingGrid.IsShutdownRequested() &&
              streamingGrid.Process(streamingInput, streamingCommands) && streamingCommands.Size() == 3,
          "explicit world shutdown drains every remaining node regardless of residency locks");
    for (const world::StreamingCommand& command : streamingCommands)
        Check(streamingGrid.NotifyStreamOutComplete(command.key), "complete cell stream-out");
    Check(streamingGrid.Shutdown(), "shutdown empty world streaming grid explicitly");

    const resources::ResourceReference sharedStreamedCell(
        resources::ResourcePath::FromString("world/executor/shared.vcell"), world::CellResourceType);
    const resources::ResourceReference failedStreamedCell(
        resources::ResourcePath::FromString("world/executor/failure.vcell"), world::CellResourceType);
    const resources::ResourceReference slowStreamedCell(
        resources::ResourcePath::FromString("world/executor/slow.vcell"), world::CellResourceType);
    std::array<world::WorldCellBuildRecord, 4> executorCells{};
    for (vanguard::u32 index = 0; index < executorCells.size(); ++index)
    {
        executorCells[index].cellId = 700u + index;
        executorCells[index].bounds = MakeWorldBounds(-100.0, 4100.0);
        executorCells[index].streamingReferencePoint[0] = index < 2 ? 0.0 : static_cast<vanguard::f64>(index - 1u) * 2000.0;
        executorCells[index].activationDistance = 100.0f;
        executorCells[index].retentionDistance = 150.0f;
    }
    executorCells[0].cell = sharedStreamedCell;
    executorCells[1].cell = sharedStreamedCell;
    executorCells[1].streamingPriority = world::StreamingPriority::Critical;
    executorCells[2].cell = failedStreamedCell;
    executorCells[3].cell = slowStreamedCell;
    world::WorldBuildDescription executorDescription;
    executorDescription.worldId = 0x45584543;
    executorDescription.bounds = MakeWorldBounds(-1000.0, 5000.0);
    executorDescription.cells = {executorCells.data(), static_cast<vanguard::u32>(executorCells.size())};
    ByteArray executorWorldBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter executorWorldWriter(executorWorldBytes);
    Check(world::CookWorld(executorDescription, executorWorldWriter) == world::Result::Success,
          "cook resource-executor test world");
    filesystem::MemoryFileReader executorWorldReader(executorWorldBytes, 0);
    world::WorldFile executorWorld;
    Check(executorWorld.Open(executorWorldReader) == world::Result::Success, "open resource-executor test world");

    resources::ResourceRegistry executorRegistry;
    Check(executorRegistry.Initialize(), "initialize world streaming resource registry");
    resources::ResourcePipeline executorPipeline;
    Check(executorPipeline.Initialize(executorRegistry), "initialize Jobs-backed world resource pipeline");
    StreamingLoaderHarness streamingHarness;
    streamingHarness.failure = failedStreamedCell.Path().Id();
    streamingHarness.slow = slowStreamedCell.Path().Id();
    const resources::AsyncLoaderDescriptor cellLoader{
        world::CellResourceType, "world streaming test cell loader", &DiscoverStreamingDependencies,
        &ConstructStreamingResource, &DestroyStreamingResource, &streamingHarness};
    Check(executorPipeline.RegisterLoader(cellLoader), "register streamed-cell resource loader");

    world::WorldStreamingGrid executorGrid;
    Check(executorGrid.Initialize(executorWorld), "initialize executor streaming grid");
    world::WorldStreamingExecutor executor;
    Check(executor.Initialize(executorGrid, executorPipeline), "initialize explicit world streaming executor");
    containers::DynamicArray<world::StreamingResourceEvent> streamingEvents(memory::pools::Streaming::GetInstance());
    world::StreamingObserver executorObserver;
    world::StreamingProcessInput executorInput;
    executorInput.observers = {&executorObserver, 1};
    Check(executor.Process(executorInput, streamingEvents) && streamingEvents.Empty(),
          "executor submits shared cell requests without blocking the world update");
    vanguard::u32 availableEvents = 0;
    for (vanguard::u32 attempt = 0; attempt < 500 && availableEvents != 2; ++attempt)
    {
        vanguard::concurrency::SleepOnCurrentThread(1);
        Check(executor.Process(executorInput, streamingEvents), "poll shared streamed-cell requests");
        for (const world::StreamingResourceEvent& event : streamingEvents)
            availableEvents += event.type == world::StreamingResourceEventType::ResourceAvailable;
    }
    const resources::ResourceHandle* firstSharedHandle = executor.Resource({700, world::StreamingNodeKind::Cell});
    const resources::ResourceHandle* secondSharedHandle = executor.Resource({701, world::StreamingNodeKind::Cell});
    Check(availableEvents == 2 && firstSharedHandle != nullptr && secondSharedHandle != nullptr &&
              firstSharedHandle->Get() == secondSharedHandle->Get() && streamingHarness.constructions.GetValue() == 1 &&
              streamingHarness.sharedPriority.GetValue() == static_cast<vanguard::u32>(resources::LoadPriority::Critical),
          "world requests coalesce, preserve critical priority and retain one generational resource object");
    Check(executor.SetReady({700, world::StreamingNodeKind::Cell}, true) &&
              executor.SetReady({701, world::StreamingNodeKind::Cell}, true),
          "downstream activation explicitly marks streamed cells ready");
    executorObserver.predictedPosition[0] = 1000.0;
    executorInput.cameraPosition[0] = 1000.0;
    Check(executor.Process(executorInput, streamingEvents) && streamingEvents.Size() == 2 &&
              streamingEvents[0].type == world::StreamingResourceEventType::ReleaseRequested &&
              streamingEvents[1].type == world::StreamingResourceEventType::ReleaseRequested &&
              executor.Resource({700, world::StreamingNodeKind::Cell}) != nullptr,
          "stream-out requests preserve handles until downstream detachment acknowledges release");
    for (const world::StreamingResourceEvent& event : streamingEvents)
        Check(executor.CompleteRelease(event.key), "complete explicit downstream resource release");

    executorObserver.predictedPosition[0] = 2000.0;
    executorInput.cameraPosition[0] = 2000.0;
    Check(executor.Process(executorInput, streamingEvents), "submit failing streamed-cell request");
    bool observedFailure = false;
    for (vanguard::u32 attempt = 0; attempt < 500 && !observedFailure; ++attempt)
    {
        vanguard::concurrency::SleepOnCurrentThread(1);
        Check(executor.Process(executorInput, streamingEvents), "poll failing streamed-cell request");
        for (const world::StreamingResourceEvent& event : streamingEvents)
            observedFailure |= event.type == world::StreamingResourceEventType::ResourceFailed &&
                               event.failure == resources::Failure::IoFailure;
    }
    resources::FailureTrace executorFailureTrace;
    Check(observedFailure && executor.LastFailure({702, world::StreamingNodeKind::Cell}) == resources::Failure::IoFailure &&
              executor.GetFailureTrace({702, world::StreamingNodeKind::Cell}, executorFailureTrace) &&
              executorFailureTrace.count != 0,
          "resource failure and its dependency trace remain inspectable at the world boundary");
    executorObserver.predictedPosition[0] = 1000.0;
    executorInput.cameraPosition[0] = 1000.0;
    Check(executor.Process(executorInput, streamingEvents), "failed streamed cell returns to unloaded state after leaving range");

    executorObserver.predictedPosition[0] = 4000.0;
    executorInput.cameraPosition[0] = 4000.0;
    Check(executor.Process(executorInput, streamingEvents), "submit cancellable streamed-cell request");
    Check(streamingHarness.slowEntered.TryWait(5000), "slow streamed-cell construction begins on Jobs");
    executorObserver.predictedPosition[0] = 1000.0;
    executorInput.cameraPosition[0] = 1000.0;
    Check(executor.Process(executorInput, streamingEvents) && streamingEvents.Size() == 1 &&
              streamingEvents[0].type == world::StreamingResourceEventType::RequestCancelled &&
              executorGrid.State({703, world::StreamingNodeKind::Cell}) == world::StreamingNodeState::Unloaded,
          "leaving range explicitly cancels in-flight resource interest without waiting for I/O");
    streamingHarness.releaseSlow.Signal();
    for (vanguard::u32 attempt = 0; attempt < 500 && executorPipeline.GetStats().activeJobs != 0; ++attempt)
        vanguard::concurrency::SleepOnCurrentThread(1);
    const world::StreamingExecutorStats executorStats = executor.GetStats();
    Check(executorStats.submittedRequests == 4 && executorStats.completedRequests == 2 &&
              executorStats.failedRequests == 1 && executorStats.cancelledRequests == 1 &&
              executorStats.releasedResources == 2,
          "world executor exposes request, failure, cancellation and residency statistics");
    Check(executor.Shutdown(), "shutdown empty world streaming executor explicitly");
    Check(executorGrid.Shutdown(), "shutdown executor grid after every node unloads");
    Check(executorPipeline.Shutdown(), "shutdown drained world resource pipeline");
    Check(executorRegistry.Shutdown(), "shutdown empty world resource registry");
    Check(vanguard::jobs::Shutdown(), "shutdown Jobs after world resource execution drains");
    executorWorld.Close();

    std::array<world::WorldCellBuildRecord, 17> simdCells{};
    for (vanguard::u32 index = 0; index < simdCells.size(); ++index)
    {
        simdCells[index].cellId = 1000u + index;
        simdCells[index].cell = resources::ResourceReference(resources::ResourcePath::FromId(5000u + index),
                                                              world::CellResourceType);
        simdCells[index].bounds = MakeWorldBounds(-100.0, 17000.0);
        simdCells[index].streamingReferencePoint[0] = static_cast<vanguard::f64>(index) * 1000.0;
        simdCells[index].activationDistance = 100.0f;
        simdCells[index].retentionDistance = 150.0f;
    }
    simdCells.back().streamingPriority = world::StreamingPriority::Critical;
    world::WorldBuildDescription simdDescription;
    simdDescription.worldId = 0x51514d44;
    simdDescription.bounds = MakeWorldBounds(-1000.0, 20000.0);
    simdDescription.cells = {simdCells.data(), static_cast<vanguard::u32>(simdCells.size())};
    ByteArray simdWorldBytes(memory::pools::World::GetInstance());
    filesystem::MemoryFileWriter simdWorldWriter(simdWorldBytes);
    Check(world::CookWorld(simdDescription, simdWorldWriter) == world::Result::Success, "cook SIMD query world");
    filesystem::MemoryFileReader simdWorldReader(simdWorldBytes, 0);
    world::WorldFile simdWorld;
    Check(simdWorld.Open(simdWorldReader) == world::Result::Success, "open SIMD query world");
    world::WorldStreamingGrid simdGrid;
    world::StreamingGridConfig simdConfig;
    simdConfig.maximumStreamInsPerUpdate = 1;
    Check(simdGrid.Initialize(simdWorld, simdConfig), "initialize SIMD multi-observer grid");
    std::array<world::StreamingObserver, 2> simdObservers{};
    simdObservers[1].predictedPosition[0] = 16000.0;
    world::StreamingProcessInput simdInput;
    simdInput.observers = {simdObservers.data(), static_cast<vanguard::u32>(simdObservers.size())};
    simdInput.cameraPosition[0] = 8000.0;
    Check(simdGrid.Process(simdInput, streamingCommands) && streamingCommands.Size() == 1 &&
              streamingCommands[0].key == world::StreamingNodeKey{1016, world::StreamingNodeKind::Cell},
          "SIMD multi-observer query selects both ranges and priority throttle chooses critical node");
    Check(simdGrid.NotifyStreamInComplete(streamingCommands[0].key, true, true), "complete critical SIMD cell");
    Check(simdGrid.Process(simdInput, streamingCommands) && streamingCommands.Size() == 1 &&
              streamingCommands[0].key == world::StreamingNodeKey{1000, world::StreamingNodeKind::Cell},
          "throttled SIMD query carries remaining desired node to the next update");
    Check(simdGrid.NotifyStreamInComplete(streamingCommands[0].key, true, true), "complete normal SIMD cell");
    simdObservers[0].predictedPosition[0] = 50000.0;
    simdObservers[1].predictedPosition[0] = 50000.0;
    simdInput.cameraPosition[0] = 50000.0;
    Check(simdGrid.Process(simdInput, streamingCommands) && streamingCommands.Size() == 2,
          "SIMD multi-observer cells leave their retention ranges");
    for (const world::StreamingCommand& command : streamingCommands)
        Check(simdGrid.NotifyStreamOutComplete(command.key), "complete SIMD cell stream-out");
    Check(simdGrid.Shutdown(), "shutdown SIMD world streaming grid");

    std::array<packages::Dependency, 8> worldPackageDependencies{};
    vanguard::u32 worldPackageDependencyCount = 0;
    for (const world::DependencyRecord& dependency : worldFile.Dependencies())
        worldPackageDependencies[worldPackageDependencyCount++] =
            {dependency.resource.Path().Id(), dependency.resource.ExpectedType(), dependency.kind};
    const packages::BuildSegment worldSegment{worldBytes.TypedData(), worldBytes.Size(), packages::Codec::Lz4, 4,
                                               packages::SegmentFlags::Inline | packages::SegmentFlags::MemoryResident};
    packages::BuildResource packagedWorld;
    packagedWorld.path = "world/city.vworld";
    packagedWorld.type = world::WorldResourceType;
    packagedWorld.flags = packages::ResourceFlags::Streamable;
    packagedWorld.segments = {&worldSegment, 1};
    packagedWorld.dependencies = {worldPackageDependencies.data(), worldPackageDependencyCount};
    ByteArray worldPackageBytes(memory::pools::Assets::GetInstance());
    filesystem::MemoryFileWriter worldPackageWriterFile(worldPackageBytes);
    packages::PackageWriter worldPackageWriter;
    Check(worldPackageWriter.Begin(worldPackageWriterFile) == packages::Result::Success &&
              worldPackageWriter.Add(packagedWorld) == packages::Result::Success &&
              worldPackageWriter.Finalize() == packages::Result::Success,
          "package vworld and its soft streaming dependencies in VPAK");
    filesystem::MemoryFileReader worldPackageReaderFile(worldPackageBytes, 0);
    packages::PackageReader worldPackageReader;
    Check(worldPackageReader.Open(worldPackageReaderFile) == packages::Result::Success, "open VPAK containing vworld");
    const packages::Resource* packagedWorldRecord = worldPackageReader.Find("world/city.vworld");
    Check(packagedWorldRecord != nullptr && packagedWorldRecord->type == world::WorldResourceType &&
              worldPackageReader.Dependencies(*packagedWorldRecord).Size() == worldFile.Dependencies().Size(),
          "VPAK preserves vworld cell and proxy dependency metadata");
    if (packagedWorldRecord != nullptr)
    {
        packages::ResourceFileReader packagedWorldView;
        Check(packagedWorldView.Open(worldPackageReader, *packagedWorldRecord, worldPackageReaderFile) == packages::Result::Success,
              "open logical vworld over VPAK");
        world::WorldFile packageBackedWorld;
        Check(packageBackedWorld.Open(packagedWorldView) == world::Result::Success &&
                  packageBackedWorld.ContentFingerprint() == worldFile.ContentFingerprint(),
              "package-backed vworld opens without reconstruction or translation");
    }

    auto invalidWorldCells = worldCells;
    invalidWorldCells[0].parentCellId = 999;
    world::WorldBuildDescription invalidWorld = worldDescription;
    invalidWorld.cells = {invalidWorldCells.data(), static_cast<vanguard::u32>(invalidWorldCells.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter invalidWorldWriter(scratch);
    Check(world::CookWorld(invalidWorld, invalidWorldWriter) == world::Result::MissingParent,
          "reject missing vworld cell parent");

    auto invalidProxyChildren = proxyChildren;
    invalidProxyChildren[1].childId = 999;
    invalidWorld = worldDescription;
    invalidWorld.proxyChildren = {invalidProxyChildren.data(), static_cast<vanguard::u32>(invalidProxyChildren.size())};
    scratch.Clear();
    filesystem::MemoryFileWriter invalidProxyWriter(scratch);
    Check(world::CookWorld(invalidWorld, invalidProxyWriter) == world::Result::InvalidReference,
          "reject unresolved proxy replacement child");

    ByteArray corruptedWorld(memory::pools::World::GetInstance());
    corruptedWorld = worldBytes;
    if (corruptedWorld.Size() > 96) corruptedWorld[96] ^= 0x20u;
    filesystem::MemoryFileReader corruptedWorldReader(corruptedWorld, 0);
    world::WorldFile corruptedWorldFile;
    Check(corruptedWorldFile.Open(corruptedWorldReader) == world::Result::IntegrityFailure, "reject corrupted vworld");

    file.Close();
    corruptedFile.Close();
    worldFile.Close();
    corruptedWorldFile.Close();
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();
    std::printf("worldTests: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
