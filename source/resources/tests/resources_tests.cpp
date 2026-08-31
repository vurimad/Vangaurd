#include <vanguard/resources/resources.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/pool.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    vanguard::u32 g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            ++g_failures;
            std::fprintf(stderr, "[resourcesTests] FAILED: %s\n", message);
        }
    }

    class TestResource final : public vanguard::resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(vanguard::memory::pools::Resources);

        TestResource(const vanguard::resources::ResourceTypeId type, const vanguard::u32 value) noexcept : m_type(type), m_value(value) {}

        [[nodiscard]] vanguard::resources::ResourceTypeId GetType() const noexcept override
        {
            return m_type;
        }

        [[nodiscard]] vanguard::u32 Value() const noexcept
        {
            return m_value;
        }

    private:
        vanguard::resources::ResourceTypeId m_type;
        vanguard::u32 m_value;
    };

    struct LoaderHarness
    {
        vanguard::concurrency::Atomic<vanguard::u32> starts{0};
        vanguard::concurrency::Atomic<vanguard::u32> beginFailures{0};
        vanguard::concurrency::Atomic<vanguard::u32> destructions{0};
    };

    void BeginTestLoad(vanguard::resources::ResourceRegistry& registry, const vanguard::resources::ResourceRequest& request, void* const userData) noexcept
    {
        LoaderHarness& harness = *static_cast<LoaderHarness*>(userData);
        static_cast<void>(harness.starts.Increment());
        if (!registry.BeginLoading(request))
        {
            static_cast<void>(harness.beginFailures.Increment());
        }
    }

    void DestroyTestResource(vanguard::resources::ResourceObject* const resource, void* const userData) noexcept
    {
        LoaderHarness& harness = *static_cast<LoaderHarness*>(userData);
        static_cast<void>(harness.destructions.Increment());
        VANGUARD_DELETE(static_cast<TestResource*>(resource));
    }
} // namespace

int main()
{
    using namespace vanguard;
    using namespace vanguard::resources;

    char canonical[128] = {};
    usize written = 0;
    Check(CanonicalizePath("Worlds\\NightCity\\Block_A.vscene", canonical, sizeof(canonical), written) == Result::Success, "mixed input path canonicalizes");
    Check(written == std::strlen("worlds/nightcity/block_a.vscene") && std::memcmp(canonical, "worlds/nightcity/block_a.vscene", written) == 0,
          "canonical bytes use lowercase forward slashes");

    const ResourcePath first = ResourcePath::FromString("Worlds\\NightCity\\Block_A.vscene");
    const ResourcePath second = ResourcePath::FromString("worlds/nightcity/block_a.vscene");
    Check(first.IsValid(), "valid paths produce an identity");
    Check(first == second, "canonical-equivalent paths have one identity");
    Check(sizeof(ResourcePath) == 8, "runtime paths remain eight bytes");

    constexpr const char* invalidPaths[] = {"",
                                            "/absolute/resource.vmesh",
                                            "C:/absolute/resource.vmesh",
                                            "mesh//double.vmesh",
                                            "mesh/trailing/",
                                            "mesh/./dot.vmesh",
                                            "mesh/../escape.vmesh",
                                            "mesh/forbidden?.vmesh"};
    for (const char* const invalid : invalidPaths)
    {
        Check(!ResourcePath::FromString(invalid).IsValid(), "invalid paths never produce an identity");
    }

    const ResourceTypeId meshType = HashTypeName("Vanguard.Mesh");
    Check(meshType != InvalidResourceTypeId, "valid type names hash");
    Check(meshType == HashTypeName("vanguard.mesh"), "type identity is ASCII-case independent");
    Check(HashTypeName("vanguard..mesh") == InvalidResourceTypeId, "invalid type names are rejected");

    const ResourceReference untyped(first);
    const ResourceReference typed(first, meshType);
    Check(untyped.IsValid() && !untyped.IsTyped(), "untyped references work");
    Check(typed.IsValid() && typed.IsTyped() && typed.ExpectedType() == meshType, "typed references preserve expected type identity");

    Check(CanTransition(State::Unloaded, State::Queued) && CanTransition(State::Queued, State::Loading) && CanTransition(State::Loading, State::Loaded),
          "normal loading state sequence is legal");
    Check(!CanTransition(State::Unloaded, State::Loaded) && !CanTransition(State::Loaded, State::Loading) && !CanTransition(State::Loading, State::Evicting),
          "invalid implicit state jumps are rejected");
    Check(IsTerminal(State::Loaded) && IsTerminal(State::Failed) && IsTerminal(State::Cancelled) && !IsTerminal(State::Loading),
          "terminal state classification is explicit");

    constexpr usize threadCount = 8;
    constexpr usize iterations = 100000;
    std::array<ResourceId, threadCount> hashes{};
    std::array<std::thread, threadCount> threads;
    for (usize thread = 0; thread < threadCount; ++thread)
    {
        threads[thread] = std::thread(
            [thread, &hashes]()
            {
                ResourceId hash = InvalidResourceId;
                for (usize iteration = 0; iteration < iterations; ++iteration)
                {
                    hash = HashPath("worlds/nightcity/block_a.vscene");
                }
                hashes[thread] = hash;
            });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    for (const ResourceId hash : hashes)
    {
        Check(hash == first.Id(), "path hashing is deterministic across threads");
    }

    ResourceRegistry registry;
    Check(registry.Initialize(), "resource registry initializes");

    LoaderHarness loaderHarness;
    const LoaderDescriptor meshLoader{meshType, "test mesh loader", &BeginTestLoad, &DestroyTestResource, &loaderHarness};
    Check(registry.RegisterLoader(meshLoader), "valid resource loader registers");
    Check(!registry.RegisterLoader(meshLoader), "duplicate resource loader registration is rejected");
    Check(registry.HasLoader(meshType), "registered loader is discoverable");

    const ResourceReference meshReference(ResourcePath::FromString("meshes/vehicle.vmesh"), meshType);
    constexpr usize requestThreadCount = 8;
    std::array<ResourceRequest, requestThreadCount> requests;
    std::array<std::thread, requestThreadCount> requestThreads;
    for (usize thread = 0; thread < requestThreadCount; ++thread)
    {
        requestThreads[thread] = std::thread([thread, &registry, &meshReference, &requests]() { requests[thread] = registry.Request(meshReference); });
    }
    for (std::thread& thread : requestThreads)
    {
        thread.join();
    }

    Check(loaderHarness.starts.GetValue() == 1, "concurrent requests start one loader operation");
    Check(loaderHarness.beginFailures.GetValue() == 0, "loader owns the explicit queued-to-loading transition");
    for (usize requestIndex = 0; requestIndex < requestThreadCount; ++requestIndex)
    {
        Check(requests[requestIndex].IsSameOperation(requests[0]), "concurrent requests share one completion token");
        Check(requests[requestIndex].GetStatus() == State::Loading, "coalesced request observes shared loading state");
    }

    TestResource* firstResource = VANGUARD_NEW(TestResource)(meshType, 11);
    Check(registry.Publish(requests[0], firstResource), "matching resource publication completes the request");
    for (ResourceRequest& request : requests)
    {
        request.Wait();
        Check(request.HasLoaded() && request.GetError() == Failure::None, "all coalesced requests observe successful completion");
    }

    ResourceHandle strong = requests[0].Acquire();
    Check(strong.IsValid(), "completed request acquires a strong handle");
    Check(static_cast<TestResource*>(strong.Get())->Value() == 11, "strong handle resolves the published object");
    ResourceHandle strongCopy = strong;
    WeakResourceHandle weak = strong.ToWeak();
    Check(weak.IsValid(), "strong handle creates a live weak handle");

    Check(registry.Evict(meshReference.GetPath()), "loaded resource accepts explicit eviction");
    Check(registry.GetState(meshReference.GetPath()) == State::Evicting, "eviction waits while strong handles exist");
    Check(loaderHarness.destructions.GetValue() == 0, "eviction does not destroy a strongly referenced object");
    ResourceRequest revived = registry.Request(meshReference);
    ResourceHandle revivedStrong = revived.Acquire();
    Check(revived.HasLoaded() && revivedStrong.IsValid() && loaderHarness.starts.GetValue() == 1 && registry.GetState(meshReference.GetPath()) == State::Loaded,
          "retained evicting dependency is reused without reloading");
    Check(registry.Evict(meshReference.GetPath()), "reused retained resource accepts deferred eviction");
    ResourceHandle weakLock = weak.Lock();
    Check(weakLock.IsValid(), "weak handle locks while deferred eviction keeps object alive");

    const u32 firstGeneration = strong.GetGeneration();
    strongCopy.Reset();
    strong.Reset();
    weakLock.Reset();
    revivedStrong.Reset();
    revived.Reset();
    Check(registry.GetState(meshReference.GetPath()) == State::Unloaded, "last strong release finishes deferred eviction");
    Check(loaderHarness.destructions.GetValue() == 1, "resource destruction occurs exactly once");
    Check(weak.IsStale() && !weak.Lock(), "old weak handles cannot cross an eviction generation");
    Check(!requests[0].Acquire(), "old completion tokens cannot acquire a later generation");

    ResourceRequest reloaded = registry.Request(meshReference);
    Check(loaderHarness.starts.GetValue() == 2 && reloaded.GetStatus() == State::Loading, "unloaded resource starts a new explicit operation");
    TestResource* secondResource = VANGUARD_NEW(TestResource)(meshType, 22);
    Check(registry.Publish(reloaded, secondResource), "new generation publishes successfully");
    ResourceHandle secondStrong = reloaded.Acquire();
    Check(secondStrong.IsValid() && secondStrong.GetGeneration() != firstGeneration, "reloaded resource receives a new generation");
    Check(registry.Evict(meshReference.GetPath()), "reloaded resource can be evicted");
    secondStrong.Reset();
    Check(loaderHarness.destructions.GetValue() == 2, "reloaded object follows the same destruction contract");

    const ResourceReference cancelledReference(ResourcePath::FromString("meshes/cancelled.vmesh"), meshType);
    ResourceRequest cancelled = registry.Request(cancelledReference);
    Check(registry.Cancel(cancelled), "in-flight request accepts explicit cancellation");
    cancelled.Wait();
    Check(cancelled.GetStatus() == State::Cancelled && cancelled.GetError() == Failure::Cancelled, "cancelled request reports cancellation as failure");
    TestResource* lateResource = VANGUARD_NEW(TestResource)(meshType, 33);
    Check(!registry.Publish(cancelled, lateResource), "late publication after cancellation is rejected");
    VANGUARD_DELETE(lateResource);

    ResourceRequest failed = registry.Request(cancelledReference);
    Check(loaderHarness.starts.GetValue() == 4, "retry after cancellation starts a distinct operation");
    Check(registry.Fail(failed, Failure::IoFailure), "loader can explicitly fail an in-flight request");
    failed.Wait();
    Check(failed.GetStatus() == State::Failed && failed.GetError() == Failure::IoFailure, "failure reason is shared by request observers");

    const ResourceTypeId unknownType = HashTypeName("vanguard.unknown");
    ResourceRequest unknown = registry.Request(ResourceReference(ResourcePath::FromString("unknown/object.vunknown"), unknownType));
    Check(unknown.HasFailed() && unknown.GetError() == Failure::UnknownType, "unregistered resource types fail without invoking a loader");

    const RegistryStats registryStats = registry.GetStats();
    Check(registryStats.registeredLoaders == 1 && registryStats.knownResources == 2, "registry statistics expose loaders and known resources");
    Check(registryStats.coalescedRequests == requestThreadCount, "registry statistics expose request coalescing");
    Check(registry.UnregisterLoader(meshType), "loader unregisters after live work and objects are gone");
    Check(!registry.HasLoader(meshType), "unregistered loader is no longer discoverable");
    Check(!registry.Shutdown(), "shutdown refuses outstanding request and weak handles");

    weak.Reset();
    for (ResourceRequest& request : requests)
    {
        request.Reset();
    }
    reloaded.Reset();
    cancelled.Reset();
    failed.Reset();
    unknown.Reset();
    Check(registry.Shutdown(), "registry shuts down after external ownership is released");

    if (g_failures == 0)
    {
        std::puts("[resourcesTests] Vanguard resource identity and registry "
                  "checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
