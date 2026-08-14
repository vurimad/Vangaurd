#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/ecs/native.hpp>
#include <vanguard/memory/memory.hpp>

#include <array>
#include <cstdio>
#include <thread>

namespace
{
    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[ecsTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    struct Position
    {
        float x = 0.0f;
    };

    struct ManagedValue
    {
        inline static int liveInstances = 0;

        ManagedValue() noexcept { ++liveInstances; }
        explicit ManagedValue(const int initialValue) noexcept : value(initialValue) { ++liveInstances; }
        ManagedValue(const ManagedValue& other) noexcept : value(other.value) { ++liveInstances; }
        ManagedValue(ManagedValue&& other) noexcept : value(other.value) { ++liveInstances; }
        ManagedValue& operator=(const ManagedValue& other) noexcept { value = other.value; return *this; }
        ManagedValue& operator=(ManagedValue&& other) noexcept { value = other.value; return *this; }
        ~ManagedValue() noexcept { --liveInstances; }

        int value = 0;
    };
}

int main()
{
    namespace ecs = vanguard::ecs;
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initializes");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "ecsTests"),
          "diagnostics initialize");
    Check(vanguard::containers::Initialize(), "containers initialize");

    memory::PoolMetrics beforeMetrics;
    Check(memory::GetPoolMetrics(memory::PoolId::Gameplay, beforeMetrics), "capture Gameplay pool baseline");

    ecs::World world;
    Check(world.Initialize(), "initialize Flecs-backed world");
    Check(world.QueueCreate(100) && world.QueueCreate(200), "queue stable entity creation");
    ecs::ActionReport report;
    Check(world.FlushActions(&report) && report.created == 2 && report.rejected == 0,
          "flush deterministic entity creation batch");
    const ecs::Entity first = world.Resolve(100);
    Check(first && world.IsAlive(first) && world.Identity(first) == 100, "stable identity resolves to live runtime entity");

    const ecs::ComponentType<Position> positionType = ecs::RegisterComponent<Position>(world);
    Check(static_cast<bool>(positionType), "register a world-scoped component type during serialized setup");
    ecs::World secondWorld;
    Check(secondWorld.Initialize(), "initialize a second ECS world for token ownership conformance");
    const ecs::ComponentType<Position> foreignPositionType = ecs::RegisterComponent<Position>(secondWorld);
    Check(!world.QueueAddComponent(100, foreignPositionType),
          "reject a component token registered by a different ECS world");
    Check(secondWorld.Shutdown(), "shutdown the empty token ownership test world");
    ecs::ComponentActionReport componentReport;
    Check(world.QueueAddComponent(100, positionType) && world.FlushComponentActions(&componentReport) &&
              componentReport.added == 1 && componentReport.rejected == 0,
          "add a component at an explicit structural synchronization point");
    Check(world.QueueSetComponent(100, positionType, Position{4.0f}) &&
              world.FlushComponentActions(&componentReport) && componentReport.set == 1,
          "copy a queued component value into the live entity");

    const ecs::CommandBatch materializationBatch = world.BeginCommandBatch();
    Check(materializationBatch && world.QueueCreate(materializationBatch, 300) &&
              world.QueueAddComponent(materializationBatch, 300, positionType) &&
              world.QueueSetComponent(materializationBatch, 300, positionType, Position{30.0f}) &&
              world.SealCommandBatch(materializationBatch),
          "seal one owned entity-and-component command batch");
    ecs::CommandBatchReport batchReport;
    Check(!world.QueueCreate(300) &&
              world.GetCommandBatchStatus(materializationBatch, &batchReport) == ecs::CommandBatchStatus::Pending &&
              batchReport.queued == 3 && batchReport.completed == 0,
          "pending identity reservation rejects competing entity creation");
    Check(world.FlushActions(&report) &&
              world.GetCommandBatchStatus(materializationBatch, &batchReport) == ecs::CommandBatchStatus::Pending &&
              batchReport.completed == 1 && !world.QueueRemoveComponent(300, positionType),
          "a batch retains exclusive ownership between entity and component commits");
    Check(world.FlushComponentActions(&componentReport) &&
              world.GetCommandBatchStatus(materializationBatch, &batchReport) == ecs::CommandBatchStatus::Succeeded &&
              batchReport.completed == 3 && batchReport.rejected == 0 && world.RetireCommandBatch(materializationBatch),
          "batch receipt publishes success only after every command commits");
    Check(world.QueueDestroy(300) && world.FlushActions(&report), "release batch receipt test entity");

    const ecs::CommandBatch cancelledBatch = world.BeginCommandBatch();
    const ecs::CommandBatch survivingBatch = world.BeginCommandBatch();
    Check(cancelledBatch && survivingBatch && world.QueueCreate(cancelledBatch, 400) &&
              world.QueueAddComponent(cancelledBatch, 400, positionType) &&
              world.QueueSetComponent(cancelledBatch, 400, positionType, Position{40.0f}) &&
              world.SealCommandBatch(cancelledBatch) && world.QueueCreate(survivingBatch, 500) &&
              world.SealCommandBatch(survivingBatch) && world.CancelCommandBatch(cancelledBatch),
          "cancel only one producer-owned command batch");
    const ecs::WorldStats cancelledStats = world.GetStats();
    Check(cancelledStats.pendingActions == 1 && cancelledStats.pendingComponentActions == 0 &&
              cancelledStats.commandBatches == 1 && world.FlushActions(&report) && !world.Resolve(400) && world.Resolve(500) &&
              world.GetCommandBatchStatus(survivingBatch) == ecs::CommandBatchStatus::Succeeded &&
              world.RetireCommandBatch(survivingBatch),
          "batch cancellation preserves another producer's queued work and owned values");
    Check(world.QueueDestroy(500) && world.FlushActions(&report), "release surviving batch entity");

    const ecs::CommandBatch failedBatch = world.BeginCommandBatch();
    Check(failedBatch && world.QueueCreate(failedBatch, 100) && world.SealCommandBatch(failedBatch) &&
              world.FlushActions(&report) && report.rejected == 1 &&
              world.GetCommandBatchStatus(failedBatch, &batchReport) == ecs::CommandBatchStatus::Failed &&
              batchReport.rejected == 1 && world.RetireCommandBatch(failedBatch),
          "batch receipt exposes rejected commands instead of inferring success from world state");

    const ecs::ComponentType<ManagedValue> managedType = ecs::RegisterComponent<ManagedValue>(world);
    Check(world.QueueAddComponent(200, managedType) && world.FlushComponentActions(&componentReport),
          "add a component with non-trivial lifecycle hooks");
    {
        const ManagedValue source{77};
        Check(world.QueueSetComponent(200, managedType, source) && world.FlushComponentActions(&componentReport),
              "queued values preserve non-trivial copy and destruction semantics");
        flecs::world native = ecs::Native(world);
        const ManagedValue* const managed = native.entity(world.Resolve(200).value).try_get<ManagedValue>();
        Check(managed != nullptr && managed->value == 77, "non-trivial queued value reaches Flecs intact");
    }
    Check(world.QueueRemoveComponent(200, managedType) && world.FlushComponentActions(&componentReport) &&
              ManagedValue::liveInstances == 0,
          "removing a non-trivial component balances every queued and live lifetime");
    {
        flecs::world native = ecs::Native(world);
        native.system<Position>().each([](Position& position) { position.x += 2.0f; });
    }
    Check(world.Progress(1.0f / 60.0f), "progress Flecs system pipeline");
    {
        flecs::world native = ecs::Native(world);
        const Position* const position = native.entity(first.value).try_get<Position>();
        Check(position != nullptr && position->x == 6.0f, "native Flecs systems operate behind stable identity boundary");
    }
    Check(world.QueueAddComponent(100, positionType) && world.QueueRemoveComponent(100, positionType) &&
              world.QueueSetComponent(100, positionType, Position{8.0f}) &&
              world.FlushComponentActions(&componentReport) && componentReport.removed == 1 &&
              componentReport.rejected == 2,
          "duplicate add and set-after-remove are rejected without repairing caller ordering");

    Check(world.QueueDestroy(100) && world.FlushActions(&report) && report.destroyed == 1 &&
              !world.IsAlive(first) && !world.Resolve(100),
          "destroy invalidates previous generational runtime entity");
    Check(world.QueueCreate(100) && world.FlushActions(&report) && report.created == 1,
          "stable identity may be materialized again after unload");
    const ecs::Entity replacement = world.Resolve(100);
    Check(replacement && replacement != first && world.Identity(replacement) == 100,
          "rematerialized identity receives a different Flecs generation");

    constexpr vanguard::u32 threadCount = 4;
    constexpr vanguard::u32 entitiesPerThread = 64;
    std::array<std::thread, threadCount> threads;
    std::array<bool, threadCount> queuedSuccessfully{};
    for (vanguard::u32 thread = 0; thread < threadCount; ++thread)
    {
        threads[thread] = std::thread([thread, &world, &queuedSuccessfully, positionType]()
        {
            queuedSuccessfully[thread] = true;
            for (vanguard::u32 index = 0; index < entitiesPerThread; ++index)
            {
                const ecs::EntityId identity = 1000u + thread * entitiesPerThread + index;
                const Position position{static_cast<float>(identity)};
                queuedSuccessfully[thread] = world.QueueCreate(identity) &&
                                               world.QueueAddComponent(identity, positionType) &&
                                               world.QueueSetComponent(identity, positionType, position) &&
                                               queuedSuccessfully[thread];
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
    for (const bool queued : queuedSuccessfully)
        Check(queued, "concurrent structural requests enter command queue");
    Check(world.FlushActions(&report) && report.created == threadCount * entitiesPerThread,
          "serialized sync point commits concurrent entity requests");
    Check(world.FlushComponentActions(&componentReport) &&
              componentReport.added == threadCount * entitiesPerThread &&
              componentReport.set == threadCount * entitiesPerThread && componentReport.rejected == 0,
          "component transactions submitted concurrently commit after entity materialization");

    Check(world.QueueCreate(200) && world.QueueDestroy(999999) && world.FlushActions(&report) && report.rejected == 2,
          "duplicate creation and unknown destruction are surfaced as rejected actions");
    const ecs::WorldStats activeStats = world.GetStats();
    Check(activeStats.entities == 2 + threadCount * entitiesPerThread && activeStats.rejectedActions == 3,
          "ECS statistics expose live identities and structural misuse");
    Check(activeStats.addedComponents == 3 + threadCount * entitiesPerThread &&
              activeStats.setComponents == 3 + threadCount * entitiesPerThread &&
              activeStats.removedComponents == 2 && activeStats.rejectedComponentActions == 2,
          "ECS statistics expose component transaction outcomes");

    Check(!world.Shutdown(), "world refuses shutdown while stable entities remain");
    Check(world.QueueDestroy(100) && world.QueueDestroy(200), "queue base entity destruction");
    for (vanguard::u32 index = 0; index < threadCount * entitiesPerThread; ++index)
        Check(world.QueueDestroy(1000u + index), "queue concurrent entity destruction");
    Check(world.FlushActions(&report) && report.destroyed == 2 + threadCount * entitiesPerThread,
          "destroy every stable entity at explicit sync point");

    memory::PoolMetrics activeMetrics;
    Check(memory::GetPoolMetrics(memory::PoolId::Gameplay, activeMetrics), "capture active Gameplay pool metrics");
#if !defined(VG_BUILD_SHIPPING)
    Check(activeMetrics.allocationCount > beforeMetrics.allocationCount,
          "Flecs allocations are visible through the Gameplay memory pool telemetry");
#endif
    Check(world.Shutdown(), "shutdown empty ECS world explicitly");

    ecs::World journalWorld;
    ecs::WorldConfig journalConfig;
    journalConfig.initialEntityCapacity = 8;
    journalConfig.initialActionCapacity = 8;
    journalConfig.committedChangeCapacity = 2;
    Check(journalWorld.Initialize(journalConfig) && journalWorld.QueueCreate(1) &&
              journalWorld.QueueCreate(2) && journalWorld.QueueCreate(3) && journalWorld.FlushActions(),
          "publish committed identities into a bounded ECS change journal");
    ecs::CommittedChangeCursor slowCursor{1};
    ecs::CommittedChangeReadResult journalRead;
    vanguard::containers::DynamicArray<ecs::CommittedChange> journalChanges(
        vanguard::memory::pools::Gameplay::GetInstance());
    Check(journalWorld.ReadCommittedChanges(slowCursor, journalChanges, 8, &journalRead) &&
              journalRead.lostRecords == 1 && journalRead.records == 2 &&
              journalChanges[0].entity == 2 && journalChanges[1].entity == 3,
          "report exact subscriber loss and resume at the oldest retained committed change");
    ecs::CommittedChangeCursor independentCursor;
    journalChanges.Clear();
    Check(journalWorld.ReadCommittedChanges(independentCursor, journalChanges, 8, &journalRead) &&
              journalRead.lostRecords == 0 && journalRead.records == 2,
          "retain independent committed-change cursors without destructive consumption");
    Check(journalWorld.QueueDestroy(1) && journalWorld.QueueDestroy(2) && journalWorld.QueueDestroy(3) &&
              journalWorld.FlushActions() && journalWorld.Shutdown(),
          "drain the bounded committed-change journal test world");

    vanguard::diagnostics::Shutdown();
    std::printf("ecsTests: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
