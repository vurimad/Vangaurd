#include <vanguard/rendering/render_scene.hpp>
#include <vanguard/rendering/render_scene_collector.hpp>
#include <vanguard/rendering/render_scene_feedback.hpp>
#include <vanguard/rendering/viewport.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstdio>
#include <limits>

namespace
{
    namespace concurrency = vanguard::concurrency;
    namespace containers = vanguard::containers;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace window = vanguard::window;
    using vanguard::u32;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition) return;
        std::fprintf(stderr, "[renderingTests] FAILED: %s\n", message);
        ++g_failures;
    }

    struct PayloadState
    {
        concurrency::Atomic<u32> retains{0};
        concurrency::Atomic<u32> releases{0};
        u32 expectedOrder = 0;
        bool fail = false;
    };

    struct ExecutionState
    {
        concurrency::Atomic<u32> executions{0};
        concurrency::Atomic<u32> nextOrder{0};
        concurrency::Atomic<u32> orderFailures{0};
        concurrency::Atomic<u64> lastSerial{0};
    };

    class TestResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        explicit TestResource(const resources::ResourceTypeId type) noexcept : m_type(type) {}

        [[nodiscard]] resources::ResourceTypeId Type() const noexcept override { return m_type; }

    private:
        resources::ResourceTypeId m_type = resources::InvalidResourceTypeId;
    };

    struct ResourceHarness
    {
        concurrency::Atomic<u32> starts{0};
        concurrency::Atomic<u32> destructions{0};
    };

    void BeginResourceLoad(resources::ResourceRegistry& registry, const resources::ResourceRequest& request,
                           void* const userData) noexcept
    {
        auto* const harness = static_cast<ResourceHarness*>(userData);
        static_cast<void>(harness->starts.Increment());
        static_cast<void>(registry.BeginLoading(request));
    }

    void DestroyResource(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        auto* const harness = static_cast<ResourceHarness*>(userData);
        static_cast<void>(harness->destructions.Increment());
        VANGUARD_DELETE(static_cast<TestResource*>(resource));
    }

    void RetainPayload(void* const data) noexcept
    {
        auto* const payload = static_cast<PayloadState*>(data);
        static_cast<void>(payload->retains.Increment());
    }

    void ReleasePayload(void* const data) noexcept
    {
        auto* const payload = static_cast<PayloadState*>(data);
        static_cast<void>(payload->releases.Increment());
    }

    rendering::RenderFrameExecutionStatus ExecuteFrame(const rendering::RenderFrameInfo& frame,
                                                        const jobs::JobContext& context,
                                                        void* const userData) noexcept
    {
        auto* const execution = static_cast<ExecutionState*>(userData);
        auto* const payload = static_cast<PayloadState*>(frame.Payload().data);
        const u32 order = execution->nextOrder.PostIncrement();
        if (payload == nullptr || payload->expectedOrder != order)
            static_cast<void>(execution->orderFailures.Increment());
        static_cast<void>(execution->executions.Increment());
        execution->lastSerial.SetValue(frame.Serial());
        if (context.debugName == nullptr || context.debugName[0] == '\0')
            static_cast<void>(execution->orderFailures.Increment());
        return payload != nullptr && payload->fail
                   ? rendering::RenderFrameExecutionStatus::Failure("intentional viewport test failure")
                   : rendering::RenderFrameExecutionStatus::Success();
    }

    rendering::RenderFrameInfo Begin(rendering::ViewportManager& manager,
                                     const rendering::EngineViewportHandle viewport,
                                     const bool present = true) noexcept
    {
        rendering::RenderFrameInfo frame;
        rendering::RenderFrameSetup setup;
        setup.present = present;
        rendering::ViewportFailure failure;
        Check(manager.BeginFrame(viewport, setup, frame, &failure), "begin render frame");
        return frame;
    }

    rendering::RenderFrameInfo Begin(rendering::EngineViewport& viewport, const bool present = true) noexcept
    {
        rendering::RenderFrameInfo frame;
        rendering::RenderFrameSetup setup;
        setup.present = present;
        rendering::ViewportFailure failure;
        Check(viewport.BeginFrame(setup, frame, &failure), "begin render frame through EngineViewport");
        return frame;
    }
} // namespace

int main()
{
    Check(memory::Initialize(), "memory initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");
    jobs::Config jobsConfig = jobs::ToolConfig();
    jobsConfig.maxWorkers = 2;
    Check(jobs::Initialize(jobsConfig), "Jobs initialization");

    ResourceHarness resourceHarness;
    resources::ResourceRegistry resourceRegistry;
    Check(resourceRegistry.Initialize(), "test resource registry initialization");
    const resources::ResourceTypeId meshType = resources::HashTypeName("vanguard.mesh");
    const resources::ResourceTypeId materialType = resources::HashTypeName("vanguard.material");
    Check(resourceRegistry.RegisterLoader({meshType, "rendering test mesh loader", BeginResourceLoad,
                                           DestroyResource, &resourceHarness}),
          "test mesh loader registration");
    Check(resourceRegistry.RegisterLoader({materialType, "rendering test material loader", BeginResourceLoad,
                                           DestroyResource, &resourceHarness}),
          "test material loader registration");
    const resources::ResourceReference meshReference(
        resources::ResourcePath::FromString("meshes/render_scene_payload.vmesh"), meshType);
    const resources::ResourceReference materialReference(
        resources::ResourcePath::FromString("materials/render_scene_payload.vmat"), materialType);
    resources::ResourceRequest meshRequest = resourceRegistry.Request(meshReference);
    resources::ResourceRequest materialRequest = resourceRegistry.Request(materialReference);
    Check(resourceRegistry.Publish(meshRequest, VANGUARD_NEW(TestResource)(meshType)),
          "test mesh resource publication");
    Check(resourceRegistry.Publish(materialRequest, VANGUARD_NEW(TestResource)(materialType)),
          "test material resource publication");
    meshRequest.Wait();
    materialRequest.Wait();
    resources::ResourceHandle meshHandle = meshRequest.Acquire();
    resources::ResourceHandle materialHandle = materialRequest.Acquire();
    Check(meshHandle.IsValid() && materialHandle.IsValid(), "test resource handles acquired");

    {
        rendering::RenderSceneManager scenes;
        rendering::RenderSceneFailure sceneFailure;
        Check(scenes.Initialize({}, &sceneFailure), "RenderSceneManager initialization");
        rendering::RenderSceneDesc runtimeDesc;
        runtimeDesc.name = "Runtime scene";
        runtimeDesc.mode = rendering::RenderSceneMode::Runtime;
        runtimeDesc.maximumProxies = 4096;
        runtimeDesc.maximumPendingProxyMutations = 32;
        runtimeDesc.maximumViews = 4;
        rendering::RenderSceneHandle runtimeScene;
        Check(scenes.CreateScene(runtimeDesc, runtimeScene, &sceneFailure) && runtimeScene.IsValid(),
              "runtime RenderScene creation");
        rendering::RenderSceneSnapshot runtimeSnapshot;
        Check(scenes.Snapshot(runtimeScene, runtimeSnapshot) &&
                  runtimeSnapshot.handle == runtimeScene &&
                  runtimeSnapshot.mode == rendering::RenderSceneMode::Runtime &&
                  runtimeSnapshot.state == rendering::RenderSceneState::Alive &&
                  runtimeSnapshot.maximumProxies == 4096 &&
                  runtimeSnapshot.maximumPendingProxyMutations == 32 &&
                  runtimeSnapshot.maximumViews == 4 &&
                  runtimeSnapshot.allowFramePipelineParticipation,
              "runtime RenderScene snapshot");

        rendering::RenderProxyDesc proxyDesc;
        proxyDesc.scene = runtimeScene;
        proxyDesc.typeId = 7;
        proxyDesc.producerId = 42;
        proxyDesc.producerGeneration = 3;
        proxyDesc.bounds.minimum[0] = -1.0f;
        proxyDesc.bounds.minimum[1] = -2.0f;
        proxyDesc.bounds.minimum[2] = -3.0f;
        proxyDesc.bounds.maximum[0] = 1.0f;
        proxyDesc.bounds.maximum[1] = 2.0f;
        proxyDesc.bounds.maximum[2] = 3.0f;
        proxyDesc.visibility = rendering::RenderProxyVisibilityFlags::Visible |
                               rendering::RenderProxyVisibilityFlags::CastsShadow;
        proxyDesc.layerMask = 0x15ull;
        proxyDesc.visibilityMask = 0x3u;
        proxyDesc.userDataEpoch = 11;
        proxyDesc.debugName = "Borrowed proxy name";
        rendering::RenderProxyHandle proxy;
        Check(scenes.CreateProxy(proxyDesc, proxy, &sceneFailure) && proxy.IsValid(),
              "RenderProxy creation");
        rendering::SpatialWriteIndexStats spatialStats;
        Check(scenes.ValidateSpatialIndex(runtimeScene, &spatialStats) &&
                  spatialStats.activeEntries == 1 &&
                  spatialStats.occupiedCells == 1 &&
                  spatialStats.dirtyCells == 1,
              "RenderProxy creation inserts private spatial entry");
        proxyDesc.debugName = "Mutated caller-owned string";
        rendering::RenderProxySnapshot proxySnapshot;
        Check(scenes.SnapshotProxy(proxy, proxySnapshot) &&
                  proxySnapshot.handle == proxy &&
                  proxySnapshot.state == rendering::RenderProxyState::Alive &&
                  proxySnapshot.typeId == 7 &&
                  proxySnapshot.producerId == 42 &&
                  proxySnapshot.producerGeneration == 3 &&
                  proxySnapshot.layerMask == 0x15ull &&
                  proxySnapshot.visibilityMask == 0x3u &&
                  proxySnapshot.userDataEpoch == 11 &&
                  proxySnapshot.debugName[0] == 'B',
              "RenderProxy creation synchronously internalizes borrowed descriptor data");
        Check(!scenes.DestroyScene(runtimeScene, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::ProxiesRemainAlive,
              "RenderScene destruction refuses live proxies");

        rendering::RenderProxyTransform movedTransform;
        movedTransform.row0[3] = 10.0f;
        movedTransform.row1[3] = 20.0f;
        movedTransform.row2[3] = 30.0f;
        rendering::RenderProxyBounds movedBounds = proxyDesc.bounds;
        movedBounds.minimum[0] = 9.0f;
        movedBounds.maximum[0] = 11.0f;
        Check(scenes.UpdateProxyTransform(proxy, movedTransform, movedBounds, 4, &sceneFailure),
              "RenderProxy transform mutation");
        Check(scenes.UpdateProxyVisibility(proxy, rendering::RenderProxyVisibilityFlags::QueryOnly, 0x7u,
                                           &sceneFailure),
              "RenderProxy visibility mutation");
        Check(scenes.UpdateProxyLayerMask(proxy, 0x22ull, &sceneFailure), "RenderProxy layer mutation");
        Check(scenes.UpdateProxyUserDataEpoch(proxy, 12, &sceneFailure), "RenderProxy user-data mutation");
        Check(scenes.SnapshotProxy(proxy, proxySnapshot) &&
                  proxySnapshot.transform.row0[3] == 10.0f &&
                  proxySnapshot.bounds.minimum[0] == 9.0f &&
                  proxySnapshot.producerGeneration == 4 &&
                  proxySnapshot.visibility == rendering::RenderProxyVisibilityFlags::QueryOnly &&
                  proxySnapshot.visibilityMask == 0x7u &&
                  proxySnapshot.layerMask == 0x22ull &&
                  proxySnapshot.userDataEpoch == 12,
              "RenderProxy snapshot reflects coalesced latest mutable state");

        rendering::RenderSceneFramePrepareResult prepareResult;
        rendering::RenderSceneCommitResult firstCommit;
        Check(scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  prepareResult.drainedMutations == 5 &&
                  prepareResult.mutationEpoch == 5,
              "RenderScene frame preparation drains mutation ingress pages");
        Check(!scenes.UpdateProxyLayerMask(proxy, 0x99ull, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::Busy &&
                  scenes.SnapshotProxy(proxy, proxySnapshot) && proxySnapshot.layerMask == 0x22ull,
              "prepared RenderScene epoch rejects mutations without changing mutable state");
        Check(!scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::Busy,
              "RenderScene rejects duplicate frame preparation before commit");
        Check(scenes.ValidateSpatialIndex(runtimeScene, &spatialStats) &&
                  spatialStats.activeEntries == 1 &&
                  spatialStats.cells == 1 &&
                  spatialStats.dirtyCells == 0 &&
                  spatialStats.fastMoves == 1 &&
                  spatialStats.repairedCells == 1,
              "RenderScene frame preparation repairs dirty spatial cells after fast movement");
        Check(scenes.CommitScene(runtimeScene, firstCommit, &sceneFailure) &&
                  firstCommit.version.IsValid() &&
                  firstCommit.completion.IsValid() &&
                  firstCommit.proxyCount == 1,
              "RenderScene commit publishes first immutable scene version");
        rendering::SceneReadLease firstLease;
        Check(scenes.AcquireReadLease(runtimeScene, firstCommit.version, firstLease, &sceneFailure) &&
                  firstLease.IsValid() &&
                  firstLease.proxyCount == 1,
              "exact RenderScene read lease acquisition");
        rendering::RenderProxySnapshot leasedProxy;
        Check(scenes.ReadProxy(firstLease, proxy, leasedProxy, &sceneFailure) &&
                  leasedProxy.layerMask == 0x22ull &&
                  leasedProxy.userDataEpoch == 12,
              "RenderScene read lease resolves immutable proxy data");

        Check(scenes.UpdateProxyLayerMask(proxy, 0x33ull, &sceneFailure), "post-lease RenderProxy layer mutation");
        rendering::RenderProxyTransform structuralTransform = movedTransform;
        structuralTransform.row0[3] = 130.0f;
        rendering::RenderProxyBounds structuralBounds = movedBounds;
        structuralBounds.minimum[0] = 129.0f;
        structuralBounds.maximum[0] = 131.0f;
        Check(scenes.UpdateProxyTransform(proxy, structuralTransform, structuralBounds, 5, &sceneFailure),
              "RenderProxy structural spatial movement mutation");
        Check(scenes.SnapshotProxy(proxy, proxySnapshot) && proxySnapshot.layerMask == 0x33ull,
              "mutable RenderProxy snapshot advances after post-lease mutation");
        Check(scenes.ReadProxy(firstLease, proxy, leasedProxy, &sceneFailure) &&
                  leasedProxy.layerMask == 0x22ull,
              "existing RenderScene read lease is isolated from later mutations");

        rendering::RenderSceneCommitResult secondCommit;
        Check(scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  prepareResult.drainedMutations == 2 &&
                  prepareResult.mutationEpoch == 7,
              "second RenderScene preparation drains only new mutations and structural movement");
        Check(scenes.ValidateSpatialIndex(runtimeScene, &spatialStats) &&
                  spatialStats.activeEntries == 1 &&
                  spatialStats.cells == 1 &&
                  spatialStats.dirtyCells == 0 &&
                  spatialStats.structuralMoves == 1 &&
                  spatialStats.repairedCells == 2,
              "RenderScene spatial write index repairs structural movement before publication");
        Check(scenes.CommitScene(runtimeScene, secondCommit, &sceneFailure) &&
                  secondCommit.version.value > firstCommit.version.value &&
                  secondCommit.proxyCount == 1,
              "RenderScene versions are monotonic");
        rendering::SceneReadLease latestLease;
        Check(scenes.AcquireLatestReadLease(runtimeScene, latestLease, &sceneFailure) &&
                  latestLease.version == secondCommit.version &&
                  scenes.ReadProxy(latestLease, proxy, leasedProxy, &sceneFailure) &&
                  leasedProxy.layerMask == 0x33ull,
              "latest RenderScene read lease sees newest committed proxy state");
        rendering::SceneReadLease copiedLease = latestLease;
        Check(scenes.ReleaseReadLease(copiedLease, &sceneFailure) &&
                  !scenes.ReleaseReadLease(latestLease, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::VersionNotFound,
              "copied RenderScene leases cannot release one registered reader twice");
        latestLease = {};
        Check(scenes.AcquireLatestReadLease(runtimeScene, latestLease, &sceneFailure),
              "latest RenderScene read lease can be reacquired after copied release");
        rendering::SceneReadLease occupiedLease = latestLease;
        Check(!scenes.AcquireLatestReadLease(runtimeScene, occupiedLease, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidState,
              "read lease acquisition refuses to overwrite a live lease");

        rendering::RenderProxyDesc invalidProxyDesc = proxyDesc;
        invalidProxyDesc.scene = runtimeScene;
        invalidProxyDesc.bounds.minimum[0] = 5.0f;
        invalidProxyDesc.bounds.maximum[0] = -5.0f;
        rendering::RenderProxyHandle invalidProxy;
        Check(!scenes.CreateProxy(invalidProxyDesc, invalidProxy, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "invalid RenderProxy bounds are rejected");
        invalidProxyDesc = proxyDesc;
        invalidProxyDesc.scene = runtimeScene;
        invalidProxyDesc.debugName = "Non-finite proxy";
        invalidProxyDesc.bounds.maximum[0] = std::numeric_limits<float>::infinity();
        Check(!scenes.CreateProxy(invalidProxyDesc, invalidProxy, &sceneFailure) &&
                  !invalidProxy.IsValid() &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "non-finite RenderProxy bounds are rejected");
        Check(scenes.DestroyProxy(proxy, &sceneFailure), "RenderProxy destruction");
        Check(!scenes.IsProxyAlive(proxy), "destroyed RenderProxy is no longer alive");
        rendering::RenderSceneCommitResult thirdCommit;
        Check(scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  prepareResult.drainedMutations == 1 &&
                  prepareResult.mutationEpoch == 8,
              "destroy RenderProxy mutation is prepared");
        Check(scenes.ValidateSpatialIndex(runtimeScene, &spatialStats) &&
                  spatialStats.activeEntries == 0 &&
                  spatialStats.cells == 0 &&
                  spatialStats.dirtyCells == 0,
              "RenderProxy destruction removes private spatial entry");
        Check(scenes.CommitScene(runtimeScene, thirdCommit, &sceneFailure) &&
                  thirdCommit.proxyCount == 0,
              "RenderScene commit after proxy destruction publishes an empty active proxy set");
        rendering::SceneReadLease emptyLease;
        Check(scenes.AcquireLatestReadLease(runtimeScene, emptyLease, &sceneFailure) &&
                  emptyLease.version == thirdCommit.version &&
                  emptyLease.proxyCount == 0,
              "latest lease after proxy destruction is empty");
        Check(!scenes.ReadProxy(emptyLease, proxy, leasedProxy, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "destroyed RenderProxy is absent from newer published versions");
        Check(scenes.ReadProxy(firstLease, proxy, leasedProxy, &sceneFailure) &&
                  leasedProxy.layerMask == 0x22ull,
              "old read lease keeps destroyed RenderProxy data alive");
        Check(!scenes.DestroyScene(runtimeScene, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::ReadersRemainAlive,
              "RenderScene destruction refuses live read leases");
        Check(scenes.ReleaseReadLease(emptyLease, &sceneFailure), "release empty RenderScene read lease");
        Check(scenes.ReleaseReadLease(latestLease, &sceneFailure), "release latest RenderScene read lease");
        Check(scenes.ReleaseReadLease(firstLease, &sceneFailure), "release first RenderScene read lease");
        rendering::RenderSceneVersionRetirementResult earlyRetirement;
        Check(scenes.RetirePublishedVersions(earlyRetirement, &sceneFailure) &&
                  earlyRetirement.reclaimedVersions >= 2,
              "end-frame retirement reclaims unreferenced scene versions");
        rendering::SceneReadLease staleVersionLease;
        Check(!scenes.AcquireReadLease(runtimeScene, firstCommit.version, staleVersionLease, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::VersionNotFound,
              "unreferenced retired RenderScene versions are reclaimed");
        Check(!scenes.UpdateProxyLayerMask(proxy, 0x44ull, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "destroyed RenderProxy rejects further mutations");

        rendering::RenderProxyHandle reusedProxy;
        proxyDesc.debugName = "Reused proxy slot";
        Check(scenes.CreateProxy(proxyDesc, reusedProxy, &sceneFailure) &&
                  reusedProxy.index == proxy.index &&
                  reusedProxy.generation != proxy.generation,
              "RenderProxy slot reuse advances generation");
        Check(!scenes.DestroyProxy(proxy, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "stale RenderProxy handle is rejected");
        Check(scenes.DestroyProxy(reusedProxy, &sceneFailure), "reused RenderProxy destruction");

        rendering::RenderSceneDesc finiteSceneDesc = runtimeDesc;
        finiteSceneDesc.name = "Finite spatial scene";
        finiteSceneDesc.maximumProxies = 2;
        finiteSceneDesc.spatial.origin[0] = -8.0f;
        finiteSceneDesc.spatial.origin[1] = -8.0f;
        finiteSceneDesc.spatial.origin[2] = -8.0f;
        finiteSceneDesc.spatial.cellSize = 8.0f;
        finiteSceneDesc.spatial.cellsPerAxis[0] = 2;
        finiteSceneDesc.spatial.cellsPerAxis[1] = 2;
        finiteSceneDesc.spatial.cellsPerAxis[2] = 2;
        finiteSceneDesc.spatial.outOfRangePolicy = rendering::SpatialOutOfRangePolicy::RejectProxy;
        rendering::RenderSceneHandle finiteScene;
        Check(scenes.CreateScene(finiteSceneDesc, finiteScene, &sceneFailure),
              "finite spatial RenderScene creation");
        rendering::RenderProxyDesc outOfRangeDesc = proxyDesc;
        outOfRangeDesc.scene = finiteScene;
        outOfRangeDesc.debugName = "Out of range proxy";
        outOfRangeDesc.bounds.minimum[0] = 64.0f;
        outOfRangeDesc.bounds.maximum[0] = 65.0f;
        rendering::RenderProxyHandle outOfRangeProxy;
        Check(!scenes.CreateProxy(outOfRangeDesc, outOfRangeProxy, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "finite spatial index rejects out-of-range proxy bounds");
        Check(scenes.DestroyScene(finiteScene, &sceneFailure), "finite spatial RenderScene destruction");

        {
            rendering::RenderSceneManager overflowScenes;
            Check(overflowScenes.Initialize(), "overflow spatial manager initialization");
            finiteSceneDesc.name = "Overflow spatial scene";
            finiteSceneDesc.spatial.outOfRangePolicy = rendering::SpatialOutOfRangePolicy::KeepUnindexed;
            rendering::RenderSceneHandle overflowScene;
            Check(overflowScenes.CreateScene(finiteSceneDesc, overflowScene, &sceneFailure),
                  "overflow spatial scene creation");
            outOfRangeDesc.scene = overflowScene;
            rendering::RenderProxyHandle overflowProxy;
            Check(overflowScenes.CreateProxy(outOfRangeDesc, overflowProxy, &sceneFailure),
                  "out-of-range proxy is retained in the overflow spatial lane");
            rendering::RenderSceneFramePrepareResult overflowPrepare;
            rendering::RenderSceneCommitResult overflowCommit;
            Check(overflowScenes.PrepareSceneFrame(overflowScene, overflowPrepare, &sceneFailure) &&
                      overflowScenes.CommitScene(overflowScene, overflowCommit, &sceneFailure),
                  "overflow spatial scene publication");
            rendering::SceneReadLease overflowLease;
            Check(overflowScenes.AcquireLatestReadLease(overflowScene, overflowLease, &sceneFailure),
                  "overflow spatial scene lease acquisition");
            rendering::VisibilityQueryRequest overflowQuery;
            overflowQuery.lease = overflowLease;
            overflowQuery.bounds = outOfRangeDesc.bounds;
            containers::DynamicArray<rendering::RenderProxyHandle> overflowResults(
                memory::pools::Rendering::GetInstance());
            rendering::VisibilityQueryResult overflowResult;
            Check(overflowScenes.CollectVisibleProxies(overflowQuery, overflowResults, overflowResult,
                                                       &sceneFailure) &&
                      overflowResult.acceptedProxies == 1 && overflowResults.Size() == 1 &&
                      overflowResults[0] == overflowProxy,
                  "overflow spatial lane remains visible to bounded queries");
            Check(overflowScenes.ReleaseReadLease(overflowLease, &sceneFailure) &&
                      overflowScenes.DestroyProxy(overflowProxy, &sceneFailure),
                  "overflow spatial proxy release");
            Check(overflowScenes.PrepareSceneFrame(overflowScene, overflowPrepare, &sceneFailure) &&
                      overflowScenes.CommitScene(overflowScene, overflowCommit, &sceneFailure) &&
                      overflowScenes.DestroyScene(overflowScene, &sceneFailure) && overflowScenes.Shutdown(&sceneFailure),
                  "overflow spatial manager shutdown");
        }

        rendering::MeshProxyDesc meshDesc;
        meshDesc.proxy = proxyDesc;
        meshDesc.proxy.debugName = "Typed mesh proxy";
        meshDesc.mesh = meshReference;
        meshDesc.material = materialReference;
        meshDesc.meshHandle = meshHandle;
        meshDesc.materialHandle = materialHandle;
        meshDesc.submeshMask = 0x5u;
        rendering::RenderProxyHandle meshProxy;
        Check(scenes.CreateMeshProxy(meshDesc, meshProxy, &sceneFailure), "MeshProxy creation");
        meshDesc.meshHandle.Reset();
        meshDesc.materialHandle.Reset();

        rendering::LightProxyDesc lightDesc;
        lightDesc.proxy = proxyDesc;
        lightDesc.proxy.debugName = "Typed light proxy";
        lightDesc.proxy.bounds.minimum[0] = 69.0f;
        lightDesc.proxy.bounds.maximum[0] = 71.0f;
        lightDesc.kind = rendering::RenderLightKind::Spot;
        lightDesc.intensity = 4.0f;
        lightDesc.range = 12.0f;
        lightDesc.castsShadow = true;
        rendering::RenderProxyHandle lightProxy;
        Check(scenes.CreateLightProxy(lightDesc, lightProxy, &sceneFailure), "LightProxy creation");

        rendering::DecalProxyDesc decalDesc;
        decalDesc.proxy = proxyDesc;
        decalDesc.proxy.debugName = "Typed decal proxy";
        decalDesc.material = materialReference;
        decalDesc.materialHandle = materialHandle;
        decalDesc.extents[0] = 2.0f;
        decalDesc.extents[1] = 3.0f;
        decalDesc.extents[2] = 4.0f;
        rendering::RenderProxyHandle decalProxy;
        Check(scenes.CreateDecalProxy(decalDesc, decalProxy, &sceneFailure), "DecalProxy creation");
        decalDesc.materialHandle.Reset();

        resources::ResourceHandle releasedOriginalMesh = static_cast<resources::ResourceHandle&&>(meshHandle);
        resources::ResourceHandle releasedOriginalMaterial = static_cast<resources::ResourceHandle&&>(materialHandle);
        releasedOriginalMesh.Reset();
        releasedOriginalMaterial.Reset();

        rendering::MeshProxySnapshot meshPayload;
        rendering::LightProxySnapshot lightPayload;
        rendering::DecalProxySnapshot decalPayload;
        Check(scenes.SnapshotMeshProxy(meshProxy, meshPayload) &&
                  meshPayload.mesh == meshReference &&
                  meshPayload.meshHandle.IsValid() &&
                  meshPayload.submeshMask == 0x5u,
              "MeshProxy payload snapshot");
        Check(scenes.SnapshotLightProxy(lightProxy, lightPayload) &&
                  lightPayload.kind == rendering::RenderLightKind::Spot &&
                  lightPayload.castsShadow,
              "LightProxy payload snapshot");
        Check(scenes.SnapshotDecalProxy(decalProxy, decalPayload) &&
                  decalPayload.material == materialReference &&
                  decalPayload.extents[2] == 4.0f,
              "DecalProxy payload snapshot");

        const resources::ResourceReference updatedMeshReference = meshReference;
        Check(scenes.UpdateMeshProxyResources(meshProxy, updatedMeshReference, materialReference, meshPayload.meshHandle,
                                              meshPayload.materialHandle, &sceneFailure),
              "MeshProxy resource mutation");
        lightPayload.intensity = 8.0f;
        Check(scenes.UpdateLightProxyProperties(lightProxy, lightPayload, &sceneFailure),
              "LightProxy property mutation");
        Check(scenes.UpdateDecalProxyMaterial(decalProxy, materialReference, decalPayload.materialHandle,
                                              &sceneFailure),
              "DecalProxy material mutation");
        Check(!scenes.UpdateMeshProxyResources(lightProxy, updatedMeshReference, materialReference, {}, {},
                                               &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidState,
              "cross-type payload mutation is rejected");

        rendering::RenderSceneCommitResult typedCommit;
        Check(scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  prepareResult.drainedMutations == 8,
              "typed payload mutations are prepared");
        Check(scenes.CommitScene(runtimeScene, typedCommit, &sceneFailure) &&
                  typedCommit.proxyCount == 3,
              "typed payload scene version is committed");
        rendering::SceneReadLease typedLease;
        Check(scenes.AcquireLatestReadLease(runtimeScene, typedLease, &sceneFailure) &&
                  typedLease.version == typedCommit.version &&
                  typedLease.proxyCount == 3,
              "typed payload read lease acquisition");
        Check(scenes.ReadMeshProxy(typedLease, meshProxy, meshPayload, &sceneFailure) &&
                  meshPayload.mesh == updatedMeshReference &&
                  meshPayload.materialHandle.IsValid(),
              "leased MeshProxy payload read");
        Check(scenes.ReadLightProxy(typedLease, lightProxy, lightPayload, &sceneFailure) &&
                  lightPayload.intensity == 8.0f,
              "leased LightProxy payload read");
        Check(scenes.ReadDecalProxy(typedLease, decalProxy, decalPayload, &sceneFailure) &&
                  decalPayload.materialHandle.IsValid(),
              "leased DecalProxy payload read");
        Check(!scenes.ReadMeshProxy(typedLease, lightProxy, meshPayload, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidState,
              "typed read rejects the wrong payload family");

        containers::DynamicArray<rendering::RenderProxyHandle> visibleProxies(
            memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryRequest visibilityRequest;
        visibilityRequest.lease = typedLease;
        visibilityRequest.bounds.minimum[0] = -2.0f;
        visibilityRequest.bounds.minimum[1] = -2.0f;
        visibilityRequest.bounds.minimum[2] = -2.0f;
        visibilityRequest.bounds.maximum[0] = 80.0f;
        visibilityRequest.bounds.maximum[1] = 2.0f;
        visibilityRequest.bounds.maximum[2] = 2.0f;
        rendering::VisibilityQueryResult visibilityResult;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  visibilityResult.completed &&
                  visibilityResult.visitedCells == 2 &&
                  visibilityResult.candidateProxies == 3 &&
                  visibilityResult.acceptedProxies == 3 &&
                  visibleProxies.Size() == 3,
              "visibility query collects committed spatial candidates from a read lease");

        visibilityRequest.layerMask = 0x15ull;
        visibilityRequest.payloadFilter = rendering::VisibilityQueryPayloadFilter::Mesh;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  visibilityResult.completed &&
                  visibilityResult.acceptedProxies == 1 &&
                  visibleProxies.Size() == 1 &&
                  visibleProxies[0] == meshProxy,
              "visibility query filters candidates by payload family and layer mask");

        visibilityRequest.payloadFilter = rendering::VisibilityQueryPayloadFilter::Any;
        visibilityRequest.maximumResults = 2;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  !visibilityResult.completed &&
                  visibilityResult.acceptedProxies == 2 &&
                  visibilityResult.overflowedProxies == 1 &&
                  visibleProxies.Size() == 2,
              "visibility query reports bounded result overflow without losing deterministic counts");

        visibilityRequest.layerMask = ~0ull;
        visibilityRequest.maximumResults = ~0u;
        visibilityRequest.useFrustum = true;
        visibilityRequest.frustum.planeCount = 6;
        visibilityRequest.frustum.planes[0].normal[0] = 1.0f;
        visibilityRequest.frustum.planes[0].distance = 3.0f;
        visibilityRequest.frustum.planes[1].normal[0] = -1.0f;
        visibilityRequest.frustum.planes[1].distance = 80.0f;
        visibilityRequest.frustum.planes[2].normal[1] = 1.0f;
        visibilityRequest.frustum.planes[2].distance = 3.0f;
        visibilityRequest.frustum.planes[3].normal[1] = -1.0f;
        visibilityRequest.frustum.planes[3].distance = 3.0f;
        visibilityRequest.frustum.planes[4].normal[2] = 1.0f;
        visibilityRequest.frustum.planes[4].distance = 3.0f;
        visibilityRequest.frustum.planes[5].normal[2] = -1.0f;
        visibilityRequest.frustum.planes[5].distance = 3.0f;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  visibilityResult.completed &&
                  visibilityResult.rejectedCellsByFrustum == 0 &&
                  visibilityResult.rejectedByFrustum == 0 &&
                  visibilityResult.acceptedProxies == 3,
              "visibility query accepts spatial candidates inside frustum planes");

        visibilityRequest.frustum.planes[0] = {};
        visibilityRequest.frustum.planes[0].normal[0] = 1.0f;
        visibilityRequest.frustum.planes[0].distance = -100.0f;
        visibilityRequest.frustum.planeCount = 1;
        Check(scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  visibilityResult.completed &&
                  visibilityResult.visitedCells == 0 &&
                  visibilityResult.rejectedCellsByFrustum == 2 &&
                  visibilityResult.acceptedProxies == 0 &&
                  visibleProxies.Size() == 0,
              "visibility query rejects whole spatial cells outside frustum planes");

        rendering::VisibilityQueryRequest invalidVisibilityRequest = visibilityRequest;
        invalidVisibilityRequest.frustum.planeCount = rendering::MaximumVisibilityFrustumPlanes + 1u;
        Check(!scenes.CollectVisibleProxies(invalidVisibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "visibility query rejects invalid frustum plane counts");

        visibilityRequest.useFrustum = false;
        visibilityRequest.maximumResults = ~0u;
        containers::DynamicArray<rendering::VisibilityQueryBatch> visibilityBatches(
            memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryPlan visibilityPlan;
        Check(scenes.BuildVisibilityQueryPlan(typedLease, 1, visibilityBatches, visibilityPlan, &sceneFailure) &&
                  visibilityPlan.IsValid() &&
                  visibilityPlan.cellCount == 2 &&
                  visibilityPlan.batchSize == 1 &&
                  visibilityPlan.batchCount == 2 &&
                  visibilityBatches.Size() == 2,
              "visibility query plan partitions retained spatial cells into deterministic batches");

        containers::DynamicArray<rendering::RenderProxyHandle> batchProxies(
            memory::pools::Rendering::GetInstance());
        rendering::VisibilityQueryResult firstBatchResult;
        rendering::VisibilityQueryResult secondBatchResult;
        Check(scenes.CollectVisibleProxyBatch(visibilityRequest, visibilityBatches[0], batchProxies,
                                              firstBatchResult, &sceneFailure) &&
                  firstBatchResult.completed &&
                  firstBatchResult.acceptedProxies == 2,
              "visibility query first batch collects its spatial cell range");
        Check(scenes.CollectVisibleProxyBatch(visibilityRequest, visibilityBatches[1], batchProxies,
                                               secondBatchResult, &sceneFailure) &&
                  secondBatchResult.completed &&
                  secondBatchResult.acceptedProxies == 1 &&
                  firstBatchResult.acceptedProxies + secondBatchResult.acceptedProxies == 3,
              "visibility query second batch collects the remaining spatial cell range");
        rendering::VisibilityQueryBatch foreignBatch = visibilityBatches[0];
        foreignBatch.version.value += 1;
        Check(!scenes.CollectVisibleProxyBatch(visibilityRequest, foreignBatch, batchProxies,
                                               firstBatchResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "visibility query batches are bound to their exact retained scene version");
        visibilityRequest.maximumResults = 1;
        Check(!scenes.CollectVisibleProxyBatch(visibilityRequest, visibilityBatches[0], batchProxies,
                                               firstBatchResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidDescriptor,
              "parallel visibility batches reserve result limiting for deterministic reduction");
        visibilityRequest.maximumResults = ~0u;

        rendering::RenderSceneCollector collector;
        Check(collector.Initialize(scenes, {4, 8}, &sceneFailure),
              "RenderScene collector initialization");
        rendering::RenderSceneViewHandle primaryView;
        rendering::RenderSceneViewHandle secondaryView;
        Check(collector.CreateView(runtimeScene, primaryView, &sceneFailure) &&
                  collector.CreateView(runtimeScene, secondaryView, &sceneFailure) &&
                  primaryView != secondaryView,
              "independent RenderScene collection views");

        rendering::VisibilityFeedbackService feedback;
        rendering::RenderSceneFrameLifecycle sceneFrameLifecycle;
        Check(feedback.Initialize(scenes, {16, 4}, &sceneFailure) &&
                  feedback.RegisterView({primaryView, runtimeScene, 7, true}, &sceneFailure) &&
                  feedback.RegisterView({secondaryView, runtimeScene, 7, true}, &sceneFailure) &&
                  sceneFrameLifecycle.Initialize(scenes, collector, feedback, &sceneFailure),
              "visibility feedback and end-frame lifecycle initialization");
        rendering::VisibilityProbeDesc probeDesc;
        probeDesc.scene = runtimeScene;
        probeDesc.bounds = meshDesc.proxy.bounds;
        probeDesc.viewPolicy.kind = rendering::VisibilityViewPolicyKind::StreamingAuthority;
        probeDesc.debugName = "Streaming visibility probe";
        rendering::VisibilityProbeHandle visibilityProbe;
        Check(feedback.CreateProbe(probeDesc, visibilityProbe, &sceneFailure),
              "non-renderable visibility probe creation");
        rendering::VisibilityProbeHandle staleFeedbackProbe;
        probeDesc.debugName = "Stale visibility probe";
        Check(feedback.CreateProbe(probeDesc, staleFeedbackProbe, &sceneFailure) &&
                  scenes.GetStats().activeProxies == 3,
              "visibility probes do not consume drawable proxy storage");

        rendering::ViewCollectionRequest collectionRequest;
        collectionRequest.view = primaryView;
        collectionRequest.visibility = visibilityRequest;
        collectionRequest.frameSerial = 100;
        collectionRequest.targetCellsPerJob = 1;
        rendering::RenderSceneCollectionHandle primaryCollection;
        Check(collector.Dispatch(collectionRequest, primaryCollection, &sceneFailure) &&
                  primaryCollection.IsValid() && collector.Completion(primaryCollection) != nullptr,
              "Jobs-backed RenderScene view collection dispatch");
        Check(collector.Wait(primaryCollection), "Jobs-backed RenderScene view collection completion");
        rendering::ViewCollectionOutput collectionOutput;
        Check(collector.CopyOutput(primaryCollection, collectionOutput, &sceneFailure) &&
                  collectionOutput.result.completed && collectionOutput.result.succeeded &&
                  collectionOutput.result.batchCount == 2 &&
                  collectionOutput.result.meshPackets == 1 &&
                  collectionOutput.result.lightPackets == 1 &&
                  collectionOutput.result.decalPackets == 1 &&
                  collectionOutput.meshes[0].proxy.handle == meshProxy &&
                  collectionOutput.lights[0].proxy.handle == lightProxy &&
                  collectionOutput.decals[0].proxy.handle == decalProxy,
              "typed collector pages reduce deterministically by spatial batch");
        collectionOutput.meshes[0].mesh.submeshMask = 0;
        Check(scenes.ReadMeshProxy(typedLease, meshProxy, meshPayload, &sceneFailure) &&
                  meshPayload.submeshMask == 0x5u,
              "collector output cannot mutate immutable published payloads");
        rendering::ViewProxyState primaryMeshState;
        Check(collector.ReadViewProxyState(primaryView, meshProxy, primaryMeshState, &sceneFailure) &&
                  primaryMeshState.lastVisibleFrame == 100 && primaryMeshState.proxy == meshProxy,
              "primary view retains generational per-proxy state");

        collectionRequest.view = secondaryView;
        collectionRequest.visibility.payloadFilter = rendering::VisibilityQueryPayloadFilter::Mesh;
        collectionRequest.frameSerial = 200;
        rendering::RenderSceneCollectionHandle secondaryCollection;
        Check(collector.Dispatch(collectionRequest, secondaryCollection, &sceneFailure) &&
                  collector.Wait(secondaryCollection) &&
                  collector.CopyOutput(secondaryCollection, collectionOutput, &sceneFailure) &&
                  collectionOutput.result.meshPackets == 1 &&
                  collectionOutput.result.lightPackets == 0 &&
                  collectionOutput.result.decalPackets == 0,
              "separate views collect different typed outputs from one scene version");
        rendering::ViewProxyState secondaryMeshState;
        Check(collector.ReadViewProxyState(secondaryView, meshProxy, secondaryMeshState, &sceneFailure) &&
                  secondaryMeshState.lastVisibleFrame == 200 &&
                  primaryMeshState.lastVisibleFrame == 100,
              "view-local proxy state remains isolated");

        collectionRequest.view = primaryView;
        collectionRequest.visibility.payloadFilter = rendering::VisibilityQueryPayloadFilter::Mesh;
        collectionRequest.frameSerial = 300;
        collectionRequest.maximumMeshPackets = 0;
        rendering::RenderSceneCollectionHandle overflowCollection;
        Check(collector.Dispatch(collectionRequest, overflowCollection, &sceneFailure) &&
                  collector.Wait(overflowCollection) &&
                  collector.CopyOutput(overflowCollection, collectionOutput, &sceneFailure) &&
                  collectionOutput.result.meshPackets == 0 &&
                  collectionOutput.result.overflowedMeshPackets == 1,
              "collector packet overflow is explicit in every configuration");
        Check(collector.ReadViewProxyState(primaryView, meshProxy, primaryMeshState, &sceneFailure) &&
                  primaryMeshState.previousVisibleFrame == 100 && primaryMeshState.lastVisibleFrame == 300,
              "overflowed packets still update view visibility state");

        rendering::RenderSceneEndFrameResult endFrameResult;
        Check(sceneFrameLifecycle.EndFrame(300, endFrameResult, &sceneFailure) ==
                  rendering::RenderSceneEndFrameStatus::Complete &&
                  endFrameResult.retiredCollections == 3 && endFrameResult.feedbackPublished,
              "end-frame retires completed collection storage and publishes feedback");
        rendering::VisibilityFeedback probeFeedback;
        Check(feedback.ReadFeedback(visibilityProbe, 300, probeFeedback, &sceneFailure) &&
                  probeFeedback.state == rendering::VisibilityFeedbackState::Visible &&
                  probeFeedback.sceneVersion == typedCommit.version &&
                  probeFeedback.viewPolicy.kind ==
                      rendering::VisibilityViewPolicyKind::StreamingAuthority &&
                  probeFeedback.evaluatedFrame == 300 && probeFeedback.ageInFrames == 0 &&
                  probeFeedback.viewSetRevision != 0,
              "visibility feedback names its scene version, view policy revision, and age");
        Check(!collector.CopyOutput(primaryCollection, collectionOutput, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "end-frame invalidates retired collection handles");

        collectionRequest.maximumMeshPackets = ~u32{0};
        collectionRequest.frameSerial = 400;
        collectionRequest.view = primaryView;
        collectionRequest.visibility.bounds.minimum[0] = 40.0f;
        collectionRequest.visibility.bounds.maximum[0] = 50.0f;
        rendering::RenderSceneCollectionHandle primaryAggregationCollection;
        Check(collector.Dispatch(collectionRequest, primaryAggregationCollection, &sceneFailure) &&
                  collector.Wait(primaryAggregationCollection),
              "first authoritative view collection for feedback aggregation");
        collectionRequest.view = secondaryView;
        collectionRequest.visibility.bounds.minimum[0] = -2.0f;
        collectionRequest.visibility.bounds.maximum[0] = 80.0f;
        rendering::RenderSceneCollectionHandle secondaryAggregationCollection;
        Check(collector.Dispatch(collectionRequest, secondaryAggregationCollection, &sceneFailure) &&
                  collector.Wait(secondaryAggregationCollection),
              "second authoritative view collection for feedback aggregation");
        probeDesc.bounds.minimum[0] = 100.0f;
        probeDesc.bounds.maximum[0] = 101.0f;
        Check(feedback.UpdateProbe(staleFeedbackProbe, probeDesc, &sceneFailure) &&
                  sceneFrameLifecycle.EndFrame(400, endFrameResult, &sceneFailure) ==
                      rendering::RenderSceneEndFrameStatus::Complete &&
                  endFrameResult.retiredCollections == 2,
              "multiple authoritative views publish through one deterministic end-frame reduction");
        Check(feedback.ReadFeedback(visibilityProbe, 400, probeFeedback, &sceneFailure) &&
                  probeFeedback.state == rendering::VisibilityFeedbackState::Visible &&
                  probeFeedback.contributingViews == 2,
              "any-visible aggregation prevents one hidden view from overriding a visible view");
        Check(feedback.ReadFeedback(staleFeedbackProbe, 400, probeFeedback, &sceneFailure) &&
                  probeFeedback.state == rendering::VisibilityFeedbackState::Unknown,
              "probe updates invalidate observations captured from an older descriptor revision");
        Check(feedback.UnregisterView(primaryView, &sceneFailure) &&
                  feedback.UnregisterView(secondaryView, &sceneFailure) &&
                  collector.DestroyView(primaryView, &sceneFailure) &&
                  collector.DestroyView(secondaryView, &sceneFailure),
              "feedback views unregister before collector view destruction");

        meshPayload = {};
        lightPayload = {};
        decalPayload = {};
        Check(scenes.DestroyProxy(meshProxy, &sceneFailure), "MeshProxy destruction");
        Check(scenes.DestroyProxy(lightProxy, &sceneFailure), "LightProxy destruction");
        Check(scenes.DestroyProxy(decalProxy, &sceneFailure), "DecalProxy destruction");
        rendering::RenderSceneCommitResult typedDestroyCommit;
        Check(scenes.PrepareSceneFrame(runtimeScene, prepareResult, &sceneFailure) &&
                  prepareResult.drainedMutations == 3,
              "typed payload destruction mutations are prepared");
        Check(scenes.CommitScene(runtimeScene, typedDestroyCommit, &sceneFailure) &&
                  typedDestroyCommit.proxyCount == 0,
              "typed payload destruction publishes an empty active proxy set");
        Check(resourceRegistry.Evict(updatedMeshReference.Path()) &&
                  resourceRegistry.GetState(updatedMeshReference.Path()) == resources::State::Evicting &&
                  resourceHarness.destructions.GetValue() == 0,
              "published scene lease retains mesh resource handles after mutable payload destruction");
        Check(sceneFrameLifecycle.EndFrame(401, endFrameResult, &sceneFailure) ==
                  rendering::RenderSceneEndFrameStatus::Complete &&
                  endFrameResult.versionsBlockedByReaders != 0 && !endFrameResult.fullyRetired &&
                  feedback.ReadFeedback(visibilityProbe, 401, probeFeedback, &sceneFailure) &&
                  probeFeedback.state == rendering::VisibilityFeedbackState::Unknown &&
                  probeFeedback.ageInFrames == 1,
              "end-frame with no active views publishes unknown feedback and preserves reader-pinned versions");
        Check(scenes.ReleaseReadLease(typedLease, &sceneFailure), "release typed payload read lease");
        Check(sceneFrameLifecycle.EndFrame(402, endFrameResult, &sceneFailure) ==
                  rendering::RenderSceneEndFrameStatus::Complete &&
                  endFrameResult.reclaimedVersions != 0 && endFrameResult.versionsBlockedByReaders == 0,
              "later end-frame reclaims scene versions after the final reader releases");
        Check(!scenes.CollectVisibleProxies(visibilityRequest, visibleProxies, visibilityResult, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::VersionNotFound,
              "visibility query rejects released scene read leases");
        Check(resourceRegistry.GetState(updatedMeshReference.Path()) == resources::State::Unloaded &&
                  resourceHarness.destructions.GetValue() == 1,
              "retired typed payload version releases retained mesh handles");
        const rendering::VisibilityProbeHandle staleVisibilityProbe = visibilityProbe;
        Check(feedback.DestroyProbe(visibilityProbe, &sceneFailure) &&
                  feedback.DestroyProbe(staleFeedbackProbe, &sceneFailure) &&
                  !feedback.ReadFeedback(staleVisibilityProbe, 402, probeFeedback, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "destroyed visibility probe generations cannot redirect feedback reads");
        sceneFrameLifecycle.Shutdown();
        Check(feedback.Shutdown(&sceneFailure) && collector.Shutdown(&sceneFailure),
              "visibility feedback and collector services shutdown cleanly");

        Check(!scenes.Shutdown(&sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::ScenesRemainAlive,
              "RenderSceneManager refuses shutdown with live scenes");

        rendering::RenderSceneDesc previewDesc;
        previewDesc.name = "Preview scene";
        previewDesc.mode = rendering::RenderSceneMode::Preview;
        previewDesc.maximumProxies = 128;
        previewDesc.maximumPendingProxyMutations = 4;
        previewDesc.maximumViews = 2;
        previewDesc.allowFramePipelineParticipation = false;
        rendering::RenderSceneHandle previewScene;
        Check(scenes.CreateScene(previewDesc, previewScene, &sceneFailure) &&
                  previewScene.IsValid() && previewScene.index != runtimeScene.index,
              "preview RenderScene creation");

        rendering::RenderProxyDesc budgetDesc = proxyDesc;
        budgetDesc.scene = previewScene;
        budgetDesc.debugName = "Budget proxy";
        rendering::RenderProxyHandle budgetProxy;
        Check(scenes.CreateProxy(budgetDesc, budgetProxy, &sceneFailure), "budget RenderProxy creation");
        Check(scenes.UpdateProxyLayerMask(budgetProxy, 1, &sceneFailure), "budget mutation 1");
        Check(scenes.UpdateProxyLayerMask(budgetProxy, 2, &sceneFailure), "budget mutation 2");
        Check(scenes.UpdateProxyLayerMask(budgetProxy, 3, &sceneFailure), "budget mutation 3");
        Check(!scenes.UpdateProxyLayerMask(budgetProxy, 4, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::CapacityExceeded,
              "RenderProxy mutation admission budget reports overflow");
        rendering::RenderProxyHandle rejectedCreate;
        Check(!scenes.CreateProxy(budgetDesc, rejectedCreate, &sceneFailure) &&
                  !rejectedCreate.IsValid() &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::CapacityExceeded,
              "failed RenderProxy admission clears its output handle");
        Check(scenes.DestroyProxy(budgetProxy, &sceneFailure), "budget RenderProxy destruction");

        Check(scenes.DestroyScene(runtimeScene, &sceneFailure), "runtime RenderScene destruction");
        Check(!scenes.IsAlive(runtimeScene) && !scenes.Snapshot(runtimeScene, runtimeSnapshot),
              "destroyed RenderScene handle becomes stale");
        rendering::RenderSceneHandle reusedScene;
        Check(scenes.CreateScene(runtimeDesc, reusedScene, &sceneFailure) &&
                  reusedScene.index == runtimeScene.index &&
                  reusedScene.generation != runtimeScene.generation,
              "RenderScene slot reuse advances generation");
        Check(!scenes.DestroyScene(runtimeScene, &sceneFailure) &&
                  sceneFailure.code == rendering::RenderSceneFailureCode::InvalidHandle,
              "stale RenderScene handle is rejected");
        Check(scenes.DestroyScene(reusedScene, &sceneFailure), "reused RenderScene destruction");
        Check(scenes.DestroyScene(previewScene, &sceneFailure), "preview RenderScene destruction");
        const rendering::RenderSceneManagerStats sceneStats = scenes.GetStats();
        Check(sceneStats.createdScenes == 4 && sceneStats.destroyedScenes == 4 &&
                  sceneStats.activeScenes == 0 && sceneStats.failedDestroys == 1 &&
                  sceneStats.createdProxies == 6 && sceneStats.destroyedProxies == 6 &&
                  sceneStats.activeProxies == 0 && sceneStats.failedProxyCreates == 4 &&
                  sceneStats.createdPayloads == 3 && sceneStats.destroyedPayloads == 3 &&
                  sceneStats.activeMeshPayloads == 0 && sceneStats.activeLightPayloads == 0 &&
                  sceneStats.activeDecalPayloads == 0 &&
                  sceneStats.failedProxyMutations == 5 && sceneStats.pendingProxyMutations == 0 &&
                  sceneStats.liveReadLeases == 0 && sceneStats.retainedSceneVersions == 0 &&
                  sceneStats.preparedFrames == 5 && sceneStats.committedVersions == 5 &&
                  sceneStats.failedPublishes == 1 &&
                  sceneStats.activeSpatialEntries == 0 && sceneStats.dirtySpatialCells == 0 &&
                  sceneStats.spatialFastMoves == 1 && sceneStats.spatialStructuralMoves == 1 &&
                  sceneStats.spatialRepairedCells == 4 && sceneStats.spatialOutOfRangeProxies == 1,
              "RenderSceneManager records lifecycle statistics");
        Check(scenes.Shutdown(&sceneFailure), "RenderSceneManager shutdown");
    }

    meshRequest.Reset();
    materialRequest.Reset();
    Check(resourceRegistry.Evict(materialReference.Path()) &&
              resourceRegistry.GetState(materialReference.Path()) == resources::State::Unloaded &&
              resourceHarness.destructions.GetValue() == 2,
          "remaining test material resource evicts after payload versions retire");
    Check(resourceRegistry.Shutdown(), "test resource registry shutdown");

    ExecutionState execution;
    rendering::RenderFrameDispatcher dispatcher;
    rendering::ViewportFailure failure;
    Check(dispatcher.Initialize(ExecuteFrame, &execution, &failure), "render frame dispatcher initialization");

    rendering::ViewportManager manager;
    Check(manager.Initialize(dispatcher, &failure), "viewport manager initialization");

    rendering::RenderViewportDesc outputDesc;
    outputDesc.name = "Headless test output";
    outputDesc.outputKind = rendering::RenderViewportOutputKind::Headless;
    outputDesc.renderExtent = {1920, 1080};
    outputDesc.outputExtent = {1920, 1080};
    rendering::RenderViewportHandle output;
    Check(manager.CreateRenderViewport(outputDesc, output, &failure), "headless render viewport creation");

    rendering::EngineViewportDesc engineDesc;
    engineDesc.contextName = "Game";
    engineDesc.output = output;
    rendering::EngineViewportHandle gameViewport;
    Check(manager.CreateEngineViewport(engineDesc, gameViewport, &failure), "game engine viewport creation");

    rendering::EngineViewport game;
    rendering::RenderViewport renderOutput;
    Check(manager.Resolve(gameViewport, game) && game.IsValid(), "resolve generation-checked EngineViewport facade");
    Check(manager.Resolve(output, renderOutput) && renderOutput.IsValid(),
          "resolve generation-checked RenderViewport facade");

    rendering::RenderFrameInfo first = Begin(game);
    Check(first.Serial() != 0 && first.RenderExtent() == rendering::ViewportExtent{1920, 1080},
          "begin frame captures immutable viewport dimensions");
    Check(!first.ShouldPresent(), "headless output never requests presentation");
    rendering::RenderFrameInfo duplicate;
    Check(!game.BeginFrame({}, duplicate, &failure) &&
              failure.code == rendering::ViewportFailureCode::FrameAlreadyBuilding,
          "second building frame is rejected explicitly");

    PayloadState firstPayload;
    firstPayload.expectedOrder = 0;
    Check(first.SetPayload({&firstPayload, RetainPayload, ReleasePayload}), "valid retained frame payload");
    rendering::RenderFrameSubmission firstSubmission;
    Check(game.SubmitFrame(first, firstSubmission, &failure) && firstSubmission.IsValid(),
          "first frame submission");
    Check(first.Serial() == 0, "submitted caller frame is invalidated");

    rendering::EngineViewportDesc previewDesc;
    previewDesc.contextName = "EditorPreview";
    previewDesc.output = output;
    previewDesc.presentByDefault = false;
    rendering::EngineViewportHandle previewViewport;
    Check(manager.CreateEngineViewport(previewDesc, previewViewport, &failure), "second engine viewport creation");

    rendering::RenderFrameInfo second = Begin(manager, previewViewport);
    PayloadState secondPayload;
    secondPayload.expectedOrder = 1;
    secondPayload.fail = true;
    Check(second.SetPayload({&secondPayload, RetainPayload, ReleasePayload}), "second retained frame payload");
    rendering::RenderFrameSubmission secondSubmission;
    Check(!manager.SubmitFrame(gameViewport, second, secondSubmission, &failure) &&
              failure.code == rendering::ViewportFailureCode::ForeignFrame,
          "foreign engine viewport cannot submit a frame");
    Check(manager.SubmitFrame(previewViewport, second, secondSubmission, &failure), "second frame submission");
    Check(game.FlushFrame(&failure), "render submission barrier flush through EngineViewport");

    const rendering::RenderFrameDispatcherStats dispatcherStats = dispatcher.GetStats();
    Check(dispatcherStats.submittedFrames == 2 && dispatcherStats.completedFrames == 2 &&
              dispatcherStats.failedFrames == 1 && dispatcherStats.lastCompletedSerial == secondSubmission.serial,
          "dispatcher records asynchronous render completion and failure");
    Check(execution.executions.GetValue() == 2 && execution.orderFailures.GetValue() == 0,
          "RenderPath submissions retain deterministic dependency order");
    Check(firstPayload.retains.GetValue() == 1 && firstPayload.releases.GetValue() == 1 &&
              secondPayload.retains.GetValue() == 1 && secondPayload.releases.GetValue() == 1,
          "frame payload ownership spans asynchronous execution exactly once");

    rendering::RenderFrameInfo abandoned = Begin(game, false);
    Check(game.AbandonFrame(abandoned, &failure) && abandoned.Serial() == 0,
          "building frame can be explicitly abandoned");

    rendering::RenderViewportSnapshot outputSnapshot;
    rendering::EngineViewportSnapshot gameSnapshot;
    Check(manager.Snapshot(output, outputSnapshot) && outputSnapshot.renderedFrames == 2 &&
              outputSnapshot.engineViewportReferences == 2,
          "render output tracks submissions and engine viewport references");
    Check(manager.Snapshot(gameViewport, gameSnapshot) && gameSnapshot.begunFrames == 2 &&
              gameSnapshot.submittedFrames == 1,
          "engine viewport tracks begun, submitted, and abandoned frames");
    Check(!manager.DestroyRenderViewport(output, &failure) &&
              failure.code == rendering::ViewportFailureCode::OutputStillReferenced,
          "render output cannot be destroyed while engine viewports reference it");

    rendering::RenderViewportDesc presentationDesc;
    presentationDesc.name = "Detached editor viewport";
    presentationDesc.outputKind = rendering::RenderViewportOutputKind::Presentation;
    presentationDesc.presentation = {3, 9};
    rendering::RenderViewportHandle presentation;
    Check(manager.CreateRenderViewport(presentationDesc, presentation, &failure),
          "presentation render viewport can exist before swapchain binding");
    rendering::EngineViewportDesc detachedDesc;
    detachedDesc.contextName = "DetachedEditor";
    detachedDesc.output = presentation;
    rendering::EngineViewportHandle detached;
    Check(manager.CreateEngineViewport(detachedDesc, detached, &failure), "detached engine viewport creation");
    rendering::RenderFrameInfo unavailable;
    Check(!manager.BeginFrame(detached, {}, unavailable, &failure) &&
              failure.code == rendering::ViewportFailureCode::OutputUnavailable,
          "presentation viewport does not render before swapchain binding");

    window::PresentationAttachmentSnapshot presentationState;
    presentationState.handle = presentationDesc.presentation;
    presentationState.surfaceKind = window::PresentationSurfaceKind::PlatformNative;
    presentationState.pixelExtent = {1600, 900};
    presentationState.requiredPixelExtentRevision = 4;
    presentationState.requiredSurfaceRevision = 2;
    presentationState.visible = true;
    Check(renderOutput.Handle() != presentation, "resolved facade remains tied to its original render viewport");
    rendering::RenderViewport detachedOutput;
    Check(manager.Resolve(presentation, detachedOutput) &&
              detachedOutput.UpdatePresentation(presentationState, &failure),
          "presentation state reaches the detached render viewport");
    rendering::RenderViewportSnapshot presentationSnapshot;
    Check(detachedOutput.Snapshot(presentationSnapshot) &&
              presentationSnapshot.state == rendering::RenderViewportState::AwaitingOutput &&
              presentationSnapshot.requestedOutputExtent == rendering::ViewportExtent{1600, 900} &&
              presentationSnapshot.outputExtent == presentationDesc.outputExtent &&
              presentationSnapshot.requiredPixelExtentRevision == 4 &&
              presentationSnapshot.appliedPixelExtentRevision == 0 &&
              presentationSnapshot.requiredSurfaceRevision == 2 &&
              presentationSnapshot.appliedSurfaceRevision == 0,
          "presentation viewport separates requested and successfully applied swapchain state");
    window::PresentationAcknowledgement acknowledgement;
    Check(!detachedOutput.GetPresentationAcknowledgement(acknowledgement),
          "unbound presentation viewport does not acknowledge unapplied work");
    const u64 reconciledOutputRevision = presentationSnapshot.outputRevision;
    Check(detachedOutput.UpdatePresentation(presentationState, &failure) &&
              detachedOutput.Snapshot(presentationSnapshot) &&
              presentationSnapshot.outputRevision == reconciledOutputRevision,
          "reconciling an unchanged presentation snapshot is idempotent");
    presentationState.requiredPixelExtentRevision = 3;
    Check(!detachedOutput.UpdatePresentation(presentationState, &failure) &&
              failure.code == rendering::ViewportFailureCode::InvalidDescriptor,
          "stale presentation revisions are rejected explicitly");

    Check(manager.DestroyEngineViewport(detached, &failure), "destroy detached engine viewport");
    Check(manager.DestroyRenderViewport(presentation, &failure), "destroy unbound presentation viewport");
    const rendering::RenderViewportHandle staleOutput = output;
    Check(manager.DestroyEngineViewport(previewViewport, &failure), "destroy preview engine viewport");
    Check(manager.DestroyEngineViewport(gameViewport, &failure), "destroy game engine viewport");
    Check(!game.IsValid(), "engine viewport facade detects destroyed generation");
    Check(manager.DestroyRenderViewport(output, &failure), "destroy headless render viewport");

    rendering::RenderViewportHandle replacement;
    Check(manager.CreateRenderViewport(outputDesc, replacement, &failure) && replacement.index == staleOutput.index &&
              replacement.generation != staleOutput.generation,
          "reused render viewport slot advances its generation");
    Check(!manager.Snapshot(staleOutput, outputSnapshot), "stale render viewport handle is rejected");
    Check(manager.DestroyRenderViewport(replacement, &failure), "destroy replacement viewport");

    const rendering::ViewportManagerStats managerStats = manager.GetStats();
    Check(managerStats.renderViewports == 0 && managerStats.engineViewports == 0 && managerStats.buildingFrames == 0 &&
              managerStats.begunFrames == 3 && managerStats.submittedFrames == 2,
          "viewport manager reaches a fully drained state");

    Check(manager.Shutdown(&failure), "viewport manager shutdown");
    Check(dispatcher.Shutdown(&failure), "render frame dispatcher shutdown");
    Check(jobs::Shutdown(), "Jobs shutdown");

    if (g_failures == 0)
        std::printf("Vanguard rendering viewport tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
