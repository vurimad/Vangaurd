#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/pipeline_cache/native_cache_store.hpp>
#include <vanguard/pipeline_cache/pipeline_cache.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/jobs/jobs.hpp>

#include <cstdio>

namespace
{
    namespace cache = vanguard::pipeline_cache;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[pipelineCacheTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    struct BackendState
    {
        vanguard::concurrency::Atomic<vanguard::u32> createCalls;
        vanguard::concurrency::Atomic<vanguard::u32> destroyCalls;
        vanguard::concurrency::Atomic<vanguard::u32> activeCalls;
        vanguard::concurrency::Atomic<vanguard::u32> maximumActiveCalls;
    };

    struct Payload
    {
        vanguard::concurrency::Atomic<vanguard::u32> references{1};
        vanguard::concurrency::ManualResetEvent* gate = nullptr;
        vanguard::u64 objectIdentity = 0;
        bool fail = false;
    };

    bool RetainPayload(void* const data) noexcept
    {
        auto* const payload = static_cast<Payload*>(data);
        return payload->references.Increment() > 1;
    }

    void ReleasePayload(void* const data) noexcept
    {
        auto* const payload = static_cast<Payload*>(data);
        static_cast<void>(payload->references.Decrement());
    }

    void UpdateMaximum(BackendState& state, const vanguard::u32 active) noexcept
    {
        vanguard::u32 observed = state.maximumActiveCalls.GetValue();
        while (active > observed)
        {
            const vanguard::u32 previous = state.maximumActiveCalls.CompareExchange(active, observed);
            if (previous == observed)
            {
                return;
            }
            observed = previous;
        }
    }

    bool CreatePipeline(const vanguard::pipelines::PipelineKind, const vanguard::crypto::Digest256&, void* const payloadData, cache::NativePipeline& output,
                        cache::FailureEvidence& failure, void* const userData) noexcept
    {
        auto& state = *static_cast<BackendState*>(userData);
        auto& payload = *static_cast<Payload*>(payloadData);
        static_cast<void>(state.createCalls.Increment());
        const vanguard::u32 active = state.activeCalls.Increment();
        UpdateMaximum(state, active);
        if (payload.gate != nullptr)
        {
            payload.gate->Wait();
        }
        if (payload.fail)
        {
            failure.failure = cache::Failure::BackendRejected;
            failure.backendCode = -42;
            failure.message[0] = 'f';
            failure.message[1] = 'a';
            failure.message[2] = 'i';
            failure.message[3] = 'l';
            failure.message[4] = '\0';
        }
        else
        {
            output.object = reinterpret_cast<void*>(static_cast<vanguard::usize>(payload.objectIdentity));
            output.backendType = 0x4e56524849ull;
        }
        static_cast<void>(state.activeCalls.Decrement());
        return !payload.fail;
    }

    void DestroyPipeline(const cache::NativePipeline pipeline, void* const userData) noexcept
    {
        if (pipeline)
        {
            auto& state = *static_cast<BackendState*>(userData);
            static_cast<void>(state.destroyCalls.Increment());
        }
    }

    cache::CreationPayload MakePayload(Payload& payload) noexcept
    {
        return {&payload, RetainPayload, ReleasePayload};
    }

    bool WaitForActive(const BackendState& state, const vanguard::u32 expected) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 500; ++attempt)
        {
            if (state.activeCalls.GetValue() >= expected)
            {
                return true;
            }
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    bool EqualBlob(const vanguard::containers::ArraySpan<const vanguard::u8> left, const vanguard::containers::ArraySpan<const vanguard::u8> right) noexcept
    {
        if (left.Size() != right.Size())
        {
            return false;
        }
        for (vanguard::u32 index = 0; index < left.Size(); ++index)
        {
            if (left[index] != right[index])
            {
                return false;
            }
        }
        return true;
    }

    struct NativeBlobBackend
    {
        vanguard::containers::ArraySpan<const vanguard::u8> exported;
        vanguard::containers::ArraySpan<const vanguard::u8> expectedImport;
        bool rejectImport = false;
        vanguard::u32 importCalls = 0;
        vanguard::u32 exportCalls = 0;
    };

    bool ImportNativeBlob(const vanguard::containers::ArraySpan<const vanguard::u8> blob, void* const userData) noexcept
    {
        auto& backend = *static_cast<NativeBlobBackend*>(userData);
        ++backend.importCalls;
        return !backend.rejectImport && EqualBlob(blob, backend.expectedImport);
    }

    bool ExportNativeBlob(vanguard::containers::DynamicArray<vanguard::u8>& blob, void* const userData) noexcept
    {
        auto& backend = *static_cast<NativeBlobBackend*>(userData);
        ++backend.exportCalls;
        blob.Resize(backend.exported.Size());
        if (blob.Size() != backend.exported.Size())
        {
            return false;
        }
        for (vanguard::u32 index = 0; index < blob.Size(); ++index)
        {
            blob[index] = backend.exported[index];
        }
        return true;
    }

    void DeleteNativeCacheTestFiles(vanguard::filesystem::Manager& manager, const vanguard::filesystem::AbsolutePath& root) noexcept
    {
        vanguard::containers::DynamicArray<vanguard::filesystem::AbsolutePath> files(vanguard::memory::pools::Rendering::GetInstance());
        manager.FindFiles(root, vanguard::containers::String("*"), files, false);
        for (const vanguard::filesystem::AbsolutePath& file : files)
        {
            static_cast<void>(manager.DeleteFile(file));
        }
        static_cast<void>(manager.DeletePath(root));
    }

    class RequestThread final : public vanguard::concurrency::Thread
    {
    public:
        RequestThread(const char* const name, cache::PipelineCache& cacheValue, const vanguard::crypto::Digest256& keyValue, Payload& payloadValue,
                      vanguard::concurrency::ManualResetEvent& startValue) noexcept
            : Thread(name), pipelineCache(cacheValue), key(keyValue), payload(payloadValue), start(startValue)
        {
        }

        void ThreadFunction() noexcept override
        {
            vanguard::jobs::RegisterCurrentThread(GetThreadName());
            start.Wait();
            result = pipelineCache.Request(key, vanguard::pipelines::PipelineKind::Graphics, MakePayload(payload), request);
        }

        cache::PipelineCache& pipelineCache;
        const vanguard::crypto::Digest256& key;
        Payload& payload;
        vanguard::concurrency::ManualResetEvent& start;
        cache::PipelineRequest request;
        cache::Result result = cache::Result::InvalidState;
    };
} // namespace

int main()
{
    namespace containers = vanguard::containers;
    namespace diagnostics = vanguard::diagnostics;
    namespace filesystem = vanguard::filesystem;
    namespace io = vanguard::io;
    namespace jobs = vanguard::jobs;
    namespace memory = vanguard::memory;
    namespace pipelines = vanguard::pipelines;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "pipelineCacheTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath nativeCacheRoot = working.AddDirPath("vanguard_native_pipeline_cache_tests");
    Check(filesystem::Initialize({working, working, nativeCacheRoot}), "filesystem initialization");
    filesystem::Manager& manager = filesystem::GetManager();
    DeleteNativeCacheTestFiles(manager, nativeCacheRoot);
    Check(manager.CreatePath(nativeCacheRoot), "native cache test root");

    cache::NativeCacheIdentity nativeIdentity;
    nativeIdentity.backend = 0x4e565248495f4433ull;
    nativeIdentity.backendVersion = 3;
    nativeIdentity.cacheSchema = 1;
    nativeIdentity.vendorId = 0x10de;
    nativeIdentity.deviceId = 0x2684;
    nativeIdentity.adapterId = 0x1122334455667788ull;
    nativeIdentity.driverVersion = 0x0001000200030004ull;
    nativeIdentity.backendCompatibility = vanguard::crypto::Sha256("nvrhi-d3d12-cache-contract", 25);
    nativeIdentity.engineBuild = vanguard::crypto::Sha256("vanguard-build-100", 18);
    cache::NativeCacheConfig nativeConfig;
    nativeConfig.root = nativeCacheRoot;
    nativeConfig.identity = nativeIdentity;
    nativeConfig.maximumBlobBytes = 1024;
    cache::NativeCacheStore nativeStore;
    Check(nativeStore.Initialize(nativeConfig), "native cache store initialization");
    containers::DynamicArray<vanguard::u8> loadedBlob(memory::pools::Rendering::GetInstance());
    Check(nativeStore.Load(loadedBlob) == cache::StoreResult::Miss, "missing native cache is a clean miss");

    const vanguard::u8 firstNativeBlob[] = {0x44, 0x58, 0x31, 0x32, 1, 2, 3, 4, 5};
    Check(nativeStore.Publish(firstNativeBlob) == cache::StoreResult::Success && manager.FileExist(nativeStore.RecordPath()),
          "publish checksummed native cache atomically");
    Check(nativeStore.Load(loadedBlob) == cache::StoreResult::Success && EqualBlob({loadedBlob.TypedData(), loadedBlob.Size()}, firstNativeBlob),
          "load compatible native cache");
    const vanguard::crypto::Digest256 identityFingerprint = nativeStore.GetIdentityFingerprint();
    nativeStore.Shutdown();
    Check(nativeStore.Initialize(nativeConfig) && nativeStore.GetIdentityFingerprint() == identityFingerprint &&
              nativeStore.Load(loadedBlob) == cache::StoreResult::Success,
          "native cache survives store restart");

    NativeBlobBackend blobBackend;
    blobBackend.expectedImport = firstNativeBlob;
    Check(nativeStore.Restore(ImportNativeBlob, &blobBackend) == cache::StoreResult::Success && blobBackend.importCalls == 1,
          "restore imports validated bytes into backend");
    const vanguard::u8 secondNativeBlob[] = {0x44, 0x58, 0x31, 0x32, 9, 8, 7, 6, 5, 4, 3};
    blobBackend.exported = secondNativeBlob;
    Check(nativeStore.CaptureAndPublish(ExportNativeBlob, &blobBackend) == cache::StoreResult::Success && blobBackend.exportCalls == 1 &&
              nativeStore.Load(loadedBlob) == cache::StoreResult::Success && EqualBlob({loadedBlob.TypedData(), loadedBlob.Size()}, secondNativeBlob),
          "capture publishes backend-exported bytes");

    {
        const vanguard::u64 size = manager.GetFileSize(nativeStore.RecordPath());
        containers::DynamicArray<vanguard::u8> corrupt(memory::pools::Rendering::GetInstance());
        corrupt.Resize(static_cast<vanguard::u32>(size));
        auto reader = manager.CreateFileReader(nativeStore.RecordPath(), filesystem::FOF_Buffered);
        Check(reader && corrupt.Size() == size, "open native cache for corruption");
        if (reader && !corrupt.Empty())
        {
            reader->Serialize(corrupt.Data(), corrupt.Size());
            reader.Reset();
            corrupt.Back() ^= 0xffu;
            auto writer = manager.CreateFileWriter(nativeStore.RecordPath(), filesystem::FOF_Buffered);
            if (writer)
            {
                writer->Serialize(corrupt.Data(), corrupt.Size());
                writer->Flush();
                writer.Reset();
            }
        }
    }
    Check(nativeStore.Load(loadedBlob) == cache::StoreResult::Corrupt && !manager.FileExist(nativeStore.RecordPath()),
          "corrupt native cache is rejected and removed");
    Check(nativeStore.Publish(secondNativeBlob) == cache::StoreResult::Success, "republish after corruption recovery");
    const cache::NativeCacheStats recoveredStats = nativeStore.GetStats();
    Check(recoveredStats.hits == 3 && recoveredStats.corruptRecords == 1 && recoveredStats.recoveries == 1 && recoveredStats.stores == 2 &&
              recoveredStats.loadedBytes != 0 && recoveredStats.storedBytes != 0,
          "native cache hit, store, corruption, byte, and recovery telemetry");
    nativeStore.Shutdown();

    cache::NativeCacheConfig incompatibleConfig = nativeConfig;
    ++incompatibleConfig.identity.driverVersion;
    Check(nativeStore.Initialize(incompatibleConfig) && nativeStore.Load(loadedBlob) == cache::StoreResult::Incompatible &&
              !manager.FileExist(nativeStore.RecordPath()),
          "driver identity mismatch rejects and removes stale cache");

    Check(nativeStore.Publish(firstNativeBlob) == cache::StoreResult::Success, "publish cache for backend rejection");
    blobBackend.rejectImport = true;
    blobBackend.expectedImport = firstNativeBlob;
    Check(nativeStore.Restore(ImportNativeBlob, &blobBackend) == cache::StoreResult::BackendRejected && !manager.FileExist(nativeStore.RecordPath()),
          "backend rejection triggers cache recovery");
    Check(nativeStore.GetStats().incompatibleRecords == 1 && nativeStore.GetStats().backendRejections == 1 && nativeStore.GetStats().recoveries == 2,
          "identity and backend rejection telemetry");
    nativeStore.Shutdown();

    const filesystem::AbsolutePath abandoned = nativeCacheRoot.AddFilePath("abandoned.tmp");
    {
        const vanguard::u8 stale[] = {0xde, 0xad};
        auto writer = manager.CreateFileWriter(abandoned);
        if (writer)
        {
            writer->Serialize(const_cast<vanguard::u8*>(stale), sizeof(stale));
            writer->Flush();
            writer.Reset();
        }
    }
    nativeConfig.maximumBlobBytes = 4;
    Check(nativeStore.Initialize(nativeConfig) && !manager.FileExist(abandoned) && nativeStore.GetStats().recoveries == 1,
          "startup removes abandoned atomic-publication temporary");
    Check(nativeStore.Publish(firstNativeBlob) == cache::StoreResult::LimitExceeded, "native cache size budget");
    nativeStore.Shutdown();

    jobs::Config jobsConfig = jobs::RuntimeConfig();
    jobsConfig.maxWorkers = 4;
    Check(jobs::Initialize(jobsConfig), "jobs initialization");

    BackendState backendState;
    cache::Backend backend;
    backend.create = CreatePipeline;
    backend.destroy = DestroyPipeline;
    backend.userData = &backendState;
    backend.identity = 0x445833324e565248ull;
    cache::PipelineCache pipelineCache;
    cache::Config config;
    config.maximumEntries = 16;
    config.maximumConcurrentCreations = 2;
    Check(pipelineCache.Initialize(backend, config), "pipeline cache initialization");

    vanguard::concurrency::ManualResetEvent gate;
    Payload firstPayload;
    firstPayload.gate = &gate;
    firstPayload.objectIdentity = 0x1001;
    Payload secondPayload;
    secondPayload.gate = &gate;
    secondPayload.objectIdentity = 0x1002;
    Payload thirdPayload;
    thirdPayload.gate = &gate;
    thirdPayload.objectIdentity = 0x1003;
    const vanguard::crypto::Digest256 firstKey = vanguard::crypto::Sha256("pipeline-1", 10);
    const vanguard::crypto::Digest256 secondKey = vanguard::crypto::Sha256("pipeline-2", 10);
    const vanguard::crypto::Digest256 thirdKey = vanguard::crypto::Sha256("pipeline-3", 10);

    cache::PipelineRequest first;
    cache::PipelineRequest duplicate;
    cache::PipelineRequest second;
    cache::PipelineRequest third;
    Check(pipelineCache.Request(firstKey, pipelines::PipelineKind::Graphics, MakePayload(firstPayload), first) == cache::Result::Success &&
              pipelineCache.Request(firstKey, pipelines::PipelineKind::Graphics, MakePayload(firstPayload), duplicate) == cache::Result::Success &&
              pipelineCache.Request(secondKey, pipelines::PipelineKind::Compute, MakePayload(secondPayload), second) == cache::Result::Success &&
              pipelineCache.Request(thirdKey, pipelines::PipelineKind::RayTracing, MakePayload(thirdPayload), third) == cache::Result::Success,
          "request pipelines");
    Check(first.IsSameGeneration(duplicate), "duplicate requests coalesce to one generation");
    Check(WaitForActive(backendState, 2), "bounded workers entered backend");
    Check(backendState.maximumActiveCalls.GetValue() == 2, "native creation concurrency is bounded");
    Check(backendState.createCalls.GetValue() == 2, "third pipeline remains queued at concurrency bound");
    gate.Signal();
    first.Wait();
    second.Wait();
    third.Wait();
    Check(first.HasSucceeded() && second.HasSucceeded() && third.HasSucceeded(), "asynchronous pipelines become valid");
    Check(backendState.createCalls.GetValue() == 3, "coalesced request creates one native object");
    Check(first.GetNativeObject().object == reinterpret_cast<void*>(static_cast<vanguard::usize>(0x1001)), "valid request exposes backend object");

    vanguard::concurrency::ManualResetEvent concurrentStart;
    vanguard::concurrency::ManualResetEvent concurrentCreation;
    Payload concurrentPayload;
    concurrentPayload.gate = &concurrentCreation;
    concurrentPayload.objectIdentity = 0x1801;
    const vanguard::crypto::Digest256 concurrentKey = vanguard::crypto::Sha256("pipeline-concurrent", 19);
    RequestThread concurrentA("PipelineRequestA", pipelineCache, concurrentKey, concurrentPayload, concurrentStart);
    RequestThread concurrentB("PipelineRequestB", pipelineCache, concurrentKey, concurrentPayload, concurrentStart);
    RequestThread concurrentC("PipelineRequestC", pipelineCache, concurrentKey, concurrentPayload, concurrentStart);
    RequestThread concurrentD("PipelineRequestD", pipelineCache, concurrentKey, concurrentPayload, concurrentStart);
    concurrentA.InitThread();
    concurrentB.InitThread();
    concurrentC.InitThread();
    concurrentD.InitThread();
    concurrentStart.Signal();
    concurrentA.JoinThread();
    concurrentB.JoinThread();
    concurrentC.JoinThread();
    concurrentD.JoinThread();
    Check(concurrentA.result == cache::Result::Success && concurrentB.result == cache::Result::Success && concurrentC.result == cache::Result::Success &&
              concurrentD.result == cache::Result::Success && concurrentA.request.IsSameGeneration(concurrentB.request) &&
              concurrentA.request.IsSameGeneration(concurrentC.request) && concurrentA.request.IsSameGeneration(concurrentD.request),
          "concurrent callers coalesce to one generation");
    Check(WaitForActive(backendState, 1), "concurrent coalesced generation entered backend once");
    concurrentCreation.Signal();
    concurrentA.request.Wait();
    Check(concurrentA.request.HasSucceeded(), "concurrent coalesced generation becomes valid");

    Payload failingPayload;
    failingPayload.objectIdentity = 0x2001;
    failingPayload.fail = true;
    cache::PipelineRequest failed;
    const vanguard::crypto::Digest256 failedKey = vanguard::crypto::Sha256("pipeline-failure", 16);
    Check(pipelineCache.Request(failedKey, pipelines::PipelineKind::Graphics, MakePayload(failingPayload), failed) == cache::Result::Success,
          "schedule failing pipeline");
    failed.Wait();
    Check(failed.GetStatus() == cache::State::Invalid && failed.GetError().failure == cache::Failure::BackendRejected && failed.GetError().backendCode == -42,
          "backend failure evidence is retained");

    const vanguard::u64 oldGeneration = first.GetGeneration();
    Check(pipelineCache.Invalidate(firstKey), "invalidate valid generation");
    Check(first.HasSucceeded() && duplicate.HasSucceeded() && backendState.destroyCalls.GetValue() == 0,
          "old valid object remains alive while generation handles exist");
    Payload replacementPayload;
    replacementPayload.objectIdentity = 0x3001;
    cache::PipelineRequest replacement;
    Check(pipelineCache.Request(firstKey, pipelines::PipelineKind::Graphics, MakePayload(replacementPayload), replacement) == cache::Result::Success,
          "request replacement generation");
    replacement.Wait();
    Check(replacement.HasSucceeded() && replacement.GetGeneration() != oldGeneration && !replacement.IsSameGeneration(first),
          "invalidation creates a new generation");
    first.Reset();
    Check(backendState.destroyCalls.GetValue() == 0, "old native object waits for last old-generation handle");
    duplicate.Reset();
    Check(backendState.destroyCalls.GetValue() == 1, "old native object retires after final handle");

    vanguard::concurrency::ManualResetEvent invalidationGate;
    Payload pendingPayload;
    pendingPayload.gate = &invalidationGate;
    pendingPayload.objectIdentity = 0x4001;
    const vanguard::crypto::Digest256 pendingKey = vanguard::crypto::Sha256("pipeline-pending", 16);
    cache::PipelineRequest invalidated;
    Check(pipelineCache.Request(pendingKey, pipelines::PipelineKind::Graphics, MakePayload(pendingPayload), invalidated) == cache::Result::Success,
          "schedule pipeline for pending invalidation");
    Check(WaitForActive(backendState, 1) && pipelineCache.Invalidate(pendingKey), "invalidate creating pipeline");
    Check(invalidated.GetStatus() == cache::State::Invalid && invalidated.GetError().failure == cache::Failure::Invalidated,
          "creating generation reports explicit invalidation");
    invalidationGate.Signal();
    pipelineCache.WaitIdle();
    Check(backendState.destroyCalls.GetValue() == 2, "object completed after invalidation is destroyed");

    Payload warmupPayload;
    warmupPayload.objectIdentity = 0x5001;
    cache::WarmupItem warmupItems[2];
    warmupItems[0] = {secondKey, pipelines::PipelineKind::Compute, MakePayload(secondPayload), cache::Priority::Background};
    warmupItems[1] = {vanguard::crypto::Sha256("pipeline-warmup", 15), pipelines::PipelineKind::Graphics, MakePayload(warmupPayload),
                      cache::Priority::Background};
    const cache::WarmupResult warmup = pipelineCache.Warmup(warmupItems);
    pipelineCache.WaitIdle();
    Check(warmup.accepted == 2 && warmup.rejected == 0, "warmup accepts cached and new pipelines");

    const cache::Stats stats = pipelineCache.GetStats();
    Check(stats.coalescedRequests >= 5 && stats.completedCreations == 6 && stats.failedCreations == 1 && stats.invalidations == 2 &&
              stats.warmupRequests == 2 && stats.activeWorkers == 0,
          "editor-visible cache telemetry");
    Check(!pipelineCache.Shutdown(), "shutdown refuses live request handles");

    second.Reset();
    third.Reset();
    failed.Reset();
    replacement.Reset();
    invalidated.Reset();
    concurrentA.request.Reset();
    concurrentB.request.Reset();
    concurrentC.request.Reset();
    concurrentD.request.Reset();
    Check(pipelineCache.InvalidateAll() >= 6, "invalidate all current generations");
    Check(pipelineCache.Shutdown(), "pipeline cache shutdown");
    Check(backendState.destroyCalls.GetValue() == backendState.createCalls.GetValue() - 1, "every successfully created native object is destroyed");
    Check(firstPayload.references.GetValue() == 1 && secondPayload.references.GetValue() == 1 && thirdPayload.references.GetValue() == 1 &&
              failingPayload.references.GetValue() == 1 && replacementPayload.references.GetValue() == 1 && pendingPayload.references.GetValue() == 1 &&
              warmupPayload.references.GetValue() == 1 && concurrentPayload.references.GetValue() == 1,
          "all asynchronous payload references are released");

    Check(jobs::Shutdown(), "jobs shutdown");
    DeleteNativeCacheTestFiles(manager, nativeCacheRoot);
    filesystem::Shutdown();
    io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0)
    {
        std::puts("[pipelineCacheTests] Vanguard runtime pipeline cache checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
