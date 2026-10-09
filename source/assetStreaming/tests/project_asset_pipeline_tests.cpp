#include "../../../tests/support/scratch_directory.hpp"
// E1F.V Phase 2 - focused behavioral proof for E1F asset operations and
// session-local undo/redo.
//
// STATUS: compiled and executed in focused Debug and Development validation.
// It is written against the existing production owners
// (SourceDatabase, AssetFileTransaction, ProjectAssetPipeline, BuildSystem/
// BuildGraph/VDDC through ProjectAssetBuildSystem, ProjectLoosePublisher and
// ResourceStreamer) and uses only the production command surface. No test-only
// replacement implementation of the asset system is introduced.
//
// The test format is deliberately trivial but format-owned: the "TASSET"
// importer owns recognition, authored-reference scanning, reference rewriting
// and relocation, exactly like a real mesh/shader importer would. The pipeline
// under test is entirely production code.
//
// Deliberately documented untested boundaries (no production fault-injection
// hook exists and adding one would be invasive architecture):
//   - CommittedCleanupPending / CommittedReconciliationPending: forcing the
//     transaction cleanup or post-commit rescan step to fail requires a
//     filesystem fault hook that the production transaction owner does not
//     expose. The inverse record retention path is source-reviewed in Phase 1
//     but is not exercised here.
//   - Physical VTXN crash recovery: killing the process between the Prepared
//     and Committed journal states is not injectable from a single-process
//     test. Only the clean shutdown/reinitialize path is exercised.
//   - Generated-identity collision retry: DuplicateAssetWithGeneratedIdentity
//     retries against the catalog, but the platform secure RNG is not
//     injectable, so a forced first-attempt collision cannot be constructed.
//     The test proves the canonical command and its catalog uniqueness, not the
//     retry loop itself.

#include <vanguard/asset_streaming/project_asset_configuration.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/asset_streaming/platform_directory_observer.hpp>
#include <vanguard/asset_streaming/source_change_accumulator.hpp>
#include <vanguard/asset_streaming/project_asset_change_state_internal.hpp>
#include <vanguard/asset_streaming/project_asset_snapshot_store.hpp>
#include <vanguard/asset_streaming/project_asset_recook_state.hpp>

#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/filesystem/directory_observation.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/reflection/reflection.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/streaming/streaming.hpp>

#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
    using namespace vanguard;

    constexpr resources::ResourceTypeId TestSourceType = 0x54535243u; // "TSRC"
    constexpr resources::ResourceTypeId TestOutputType = 0x544f5554u; // "TOUT"
    constexpr const char* TestCompilerName = "assetstreaming.pipeline_test";
    constexpr u32 TestImporterVersion = 1;
    constexpr u32 LargeContentSize = 60000u;

    // History bounds are tuned so that:
    //   - record-count eviction is observable at 3 committed operations, and
    //   - a ~60 KiB reverse snapshot forces destination-stack byte eviction
    //     while still fitting inside the per-record budget.
    constexpr u32 MaximumHistoryRecords = 2;
    constexpr u64 MaximumOperationFileBytes = 64ull * 1024ull;
    constexpr u64 MaximumOperationHistoryBytes = 100ull * 1024ull;
    constexpr u64 MaximumOperationStagedBytes = 128ull * 1024ull;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (!condition)
        {
            VG_LOG_ERROR(diagnostics::Category::FunctionalTests, "[assetStreamingTests] FAILED: %s", message);
            std::fprintf(stderr, "[assetStreamingTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    [[nodiscard]] containers::DynamicArray<asset_streaming::ProjectAssetPreparedSourceEvidence> MakePreparedEvidence(
        const assets::SourceDatabase& sources) noexcept
    {
        containers::DynamicArray<asset_streaming::ProjectAssetPreparedSourceEvidence> evidence(memory::pools::Assets::GetInstance());
        for (const assets::SourceRecord& row : sources.GetResources())
        {
            asset_streaming::ProjectAssetPreparedSourceEvidence item;
            if ((row.issues & assets::SourceMissing) == 0u)
            {
                const containers::StringView path = row.path.AsStringView();
                item.sourceContentFingerprint = crypto::Sha256(path.Data(), path.Length());
                item.hasSourceContentFingerprint = true;
            }
            evidence.PushBack(item);
        }
        return evidence;
    }

    [[nodiscard]] containers::DynamicArray<asset_streaming::ProjectAssetPreparedSourceEvidence> MakeConstantEvidence(
        const assets::SourceDatabase& sources, const crypto::Digest256 fingerprint) noexcept
    {
        containers::DynamicArray<asset_streaming::ProjectAssetPreparedSourceEvidence> evidence(memory::pools::Assets::GetInstance());
        for (const assets::SourceRecord& row : sources.GetResources())
        {
            asset_streaming::ProjectAssetPreparedSourceEvidence item;
            if ((row.issues & assets::SourceMissing) == 0u)
            {
                item.sourceContentFingerprint = fingerprint;
                item.hasSourceContentFingerprint = true;
            }
            evidence.PushBack(item);
        }
        return evidence;
    }

    [[nodiscard]] const asset_streaming::ProjectAssetChangeRecord* FindChange(
        const asset_streaming::ProjectAssetAcceptedBaseline& baseline, const assets::AssetId asset) noexcept
    {
        for (const asset_streaming::ProjectAssetChangeRecord& change : baseline.changes)
            if (change.asset == asset)
                return &change;
        return nullptr;
    }

    [[nodiscard]] const asset_streaming::ProjectAssetChangeRecord* FindAcceptedChange(
        const asset_streaming::ProjectAssetPipeline& pipeline, const assets::AssetId asset) noexcept
    {
        for (const asset_streaming::ProjectAssetChangeRecord& change : pipeline.GetAcceptedAssetChanges())
            if (change.asset == asset)
                return &change;
        return nullptr;
    }

    [[nodiscard]] bool HasAcceptedChange(const asset_streaming::ProjectAssetPipeline& pipeline, const assets::AssetId asset,
                                         const asset_streaming::ProjectAssetChangeReason reason) noexcept
    {
        const asset_streaming::ProjectAssetChangeRecord* const change = FindAcceptedChange(pipeline, asset);
        return change != nullptr && asset_streaming::HasChangeReason(change->reasons, reason);
    }

    [[nodiscard]] bool ContainsResource(const containers::ArraySpan<const resources::ResourceReference> values,
                                        const resources::ResourceReference value) noexcept
    {
        for (const resources::ResourceReference candidate : values)
            if (candidate == value)
                return true;
        return false;
    }

    [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DrainObservation(
        asset_streaming::ProjectAssetPipeline& pipeline, const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickObservation();
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return result;
            concurrency::YieldCurrentThread();
        }
        return asset_streaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DrainAutomaticRecook(
        asset_streaming::ProjectAssetPipeline& pipeline, const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickAutomaticRecook();
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return result;
            concurrency::YieldCurrentThread();
        }
        return asset_streaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DrainAutomaticRecookWithFailure(
        asset_streaming::ProjectAssetPipeline& pipeline, asset_streaming::ProjectAssetPipelineFailure& failure,
        const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickAutomaticRecook(&failure);
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return result;
            concurrency::YieldCurrentThread();
        }
        return asset_streaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DrainVddcMaintenance(
        asset_streaming::ProjectAssetPipeline& pipeline, asset_streaming::ProjectAssetPipelineFailure* const failure = nullptr,
        const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickVddcMaintenance(failure);
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return result;
            concurrency::YieldCurrentThread();
        }
        return asset_streaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] asset_streaming::ProjectAssetPipelineResult DrainLooseMaintenance(
        asset_streaming::ProjectAssetPipeline& pipeline, asset_streaming::ProjectAssetPipelineFailure* const failure = nullptr,
        const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickLooseMaintenance(failure);
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return result;
            concurrency::YieldCurrentThread();
        }
        return asset_streaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] bool TickUntilCacheRepairRequired(asset_streaming::ProjectAssetPipeline& pipeline,
                                                    const u32 maximumTicks = 2000000u) noexcept
    {
        for (u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickAutomaticRecook();
            if (pipeline.GetAutomaticRecookStatus().pendingRepairOutputs != 0u)
                return true;
            if (result != asset_streaming::ProjectAssetPipelineResult::Pending)
                return false;
            concurrency::YieldCurrentThread();
        }
        return false;
    }

    [[nodiscard]] bool WaitForObservationPathHint(asset_streaming::ProjectAssetPipeline& pipeline, const u32 previousPending,
                                                  const u64 previousRejected, const u32 maximumMilliseconds = 5000u) noexcept
    {
        for (u32 elapsed = 0; elapsed < maximumMilliseconds; ++elapsed)
        {
            const asset_streaming::ProjectAssetObservationStatus status = pipeline.GetObservationStatus();
            if (status.overflow || status.hintsRejected != previousRejected)
                return false;
            if (status.pendingPaths > previousPending)
                return true;
            concurrency::SleepOnCurrentThread(1u);
        }
        const asset_streaming::ProjectAssetObservationStatus status = pipeline.GetObservationStatus();
        return !status.overflow && status.hintsRejected == previousRejected && status.pendingPaths > previousPending;
    }

    [[nodiscard]] bool WaitForSuppressedObservationHint(asset_streaming::ProjectAssetPipeline& pipeline,
                                                        const u64 previousSuppressed,
                                                        const u32 maximumMilliseconds = 5000u) noexcept
    {
        for (u32 elapsed = 0; elapsed < maximumMilliseconds; ++elapsed)
        {
            if (pipeline.GetObservationStatus().hintsSuppressed > previousSuppressed)
                return true;
            concurrency::SleepOnCurrentThread(1u);
        }
        return pipeline.GetObservationStatus().hintsSuppressed > previousSuppressed;
    }

#if defined(_WIN32)
    struct NativeObservationProbe
    {
        explicit NativeObservationProbe(const filesystem::AbsolutePath& path) noexcept : expectedPath(path) {}

        static void Callback(const filesystem::DirectoryObservation& observation, void* const userData) noexcept
        {
            auto* const probe = static_cast<NativeObservationProbe*>(userData);
            if (probe == nullptr)
                return;
            if (observation.kind == filesystem::DirectoryObservationKind::Overflow)
            {
                probe->overflowSeen.Signal();
                return;
            }
            if (observation.path == probe->expectedPath)
                probe->expectedPathSeen.Signal();
        }

        filesystem::AbsolutePath expectedPath;
        concurrency::ManualResetEvent overflowSeen;
        concurrency::ManualResetEvent expectedPathSeen;
    };
#endif

    struct BuildBlockState
    {
        concurrency::Atomic<bool> started;
        concurrency::Atomic<bool> release;
        concurrency::Atomic<u32> compileCalls;
        concurrency::Atomic<bool> dependencyEnabled;
        concurrency::Atomic<bool> fail;
        resources::ResourceReference dependencyOwnerSource;
        resources::ResourceReference dependencyOutput;
        resources::ResourceReference dependencyOwnerSource2;
        resources::ResourceReference dependencyOutput2;
        bool contentFromSource = false;
        bool block = false;
    };

    class P4RemovalResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);
        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override { return TestOutputType; }
    };

    struct P4RemovalDecodeState
    {
        concurrency::ManualResetEvent entered;
        concurrency::ManualResetEvent release;
    };

    resources::ResourceObject* DecodeP4RemovalResource(const resources::ResourceReference, const void*, const usize,
                                                       const resources::LoadContext&, resources::Failure& failure,
                                                       void* const userData) noexcept
    {
        auto& state = *static_cast<P4RemovalDecodeState*>(userData);
        state.entered.Signal();
        state.release.Wait();
        failure = resources::Failure::None;
        return VANGUARD_NEW(P4RemovalResource)();
    }

    void DestroyP4RemovalResource(resources::ResourceObject* const resource, void*) noexcept
    {
        VANGUARD_DELETE(static_cast<P4RemovalResource*>(resource));
    }

    struct RequestResolveBlockState
    {
        concurrency::Atomic<bool> block;
        concurrency::Atomic<bool> started;
        concurrency::Atomic<bool> release;
        concurrency::Atomic<bool> fail;
        concurrency::Atomic<u32> resolveCalls;
        u32 blockOnCall = 0u;
    };

    // Deterministic E2.1 observation adapter. It uses the production callback
    // seam and never mutates the catalog itself. Emit() after Stop() is a no-op,
    // which also models a platform callback racing with shutdown.
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
            ++m_startCount;
            return true;
        }

        // Deterministic proof that a rejected EnableObservation never reached
        // DirectoryObservationSource::Start.
        [[nodiscard]] u32 StartCount() const noexcept { return m_startCount; }

        [[nodiscard]] bool Stop() noexcept override
        {
            if (m_emitDuringStop && m_callback != nullptr)
            {
                m_callback(m_stopObservation, m_userData);
                m_emitDuringStop = false;
            }
            m_running = false;
            m_callback = nullptr;
            m_userData = nullptr;
            return true;
        }

        [[nodiscard]] bool IsRunning() const noexcept override { return m_running; }

        void Emit(const filesystem::DirectoryObservationKind kind, const filesystem::AbsolutePath& path,
                  const u32 root = 0) noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.path = path;
            observation.root = root;
            observation.kind = kind;
            m_callback(observation, m_userData);
        }

        void EmitOverflow(const u32 root = 0) noexcept
        {
            if (!m_running || m_callback == nullptr)
                return;
            filesystem::DirectoryObservation observation;
            observation.root = root;
            observation.kind = filesystem::DirectoryObservationKind::Overflow;
            m_callback(observation, m_userData);
        }

        void EmitDuringNextStop(const filesystem::DirectoryObservationKind kind,
                                const filesystem::AbsolutePath& path, const u32 root = 0) noexcept
        {
            m_stopObservation = {};
            m_stopObservation.path = path;
            m_stopObservation.root = root;
            m_stopObservation.kind = kind;
            m_emitDuringStop = true;
        }

        u32 m_rootCount = 0;

    private:
        filesystem::DirectoryObservationCallback m_callback = nullptr;
        void* m_userData = nullptr;
        filesystem::DirectoryObservation m_stopObservation;
        u32 m_startCount = 0;
        bool m_emitDuringStop = false;
        bool m_running = false;
    };

    // ---------------------------------------------------------------------
    // Production-shaped importer/compiler for the trivial TASSET format.
    // ---------------------------------------------------------------------

    [[nodiscard]] bool RecognizeSource(const containers::StringView sourcePath, void*) noexcept
    {
        return sourcePath.EndsWithIgnoreCase(".tasset");
    }

    [[nodiscard]] bool RecognizeNoAuthorSource(const containers::StringView sourcePath, void*) noexcept
    {
        return sourcePath.EndsWithIgnoreCase(".noauthor");
    }

    [[nodiscard]] bool RecognizeAmbiguousSource(const containers::StringView sourcePath, void*) noexcept
    {
        return sourcePath.EndsWithIgnoreCase(".ambiguous");
    }

    [[nodiscard]] bool CreateCallbackBuildRequest(const assets::SourceRecord& source, const assets::SourceDataView& data,
                                                  const resources::ResourceReference output, assets::BuildRequest& request,
                                                  void* const userData) noexcept
    {
        auto* const state = static_cast<RequestResolveBlockState*>(userData);
        u32 resolveCall = 0u;
        if (state != nullptr)
        {
            static_cast<void>(state->resolveCalls.Increment());
            resolveCall = state->resolveCalls.GetValue();
        }
        if (state != nullptr && state->fail.GetValue())
            return false;
        if (state != nullptr && state->block.GetValue() && (state->blockOnCall == 0u || state->blockOnCall == resolveCall))
        {
            state->started.SetValue(true);
            while (!state->release.GetValue())
                concurrency::YieldCurrentThread();
        }
        if (!source.sourceIdentity.IsValid() || !data.IsValid() || !output.IsValid() || !output.IsTyped())
            return false;
        request = {{resources::ResourceReference(source.sourceIdentity, TestSourceType), data.content, data.metadata}, output,
                   data.target, {source.metadata.settings.TypedData(), source.metadata.settings.Size()}};
        return request.IsValid();
    }

    [[nodiscard]] bool Discover(const assets::BuildRequest& request, assets::DependencyCollector& dependencies,
                                void* const userData) noexcept
    {
        const auto* const state = static_cast<const BuildBlockState*>(userData);
        if (state != nullptr && state->dependencyEnabled.GetValue() && request.source.identity == state->dependencyOwnerSource &&
            state->dependencyOutput.IsValid())
        {
            return dependencies.Add({state->dependencyOutput, {}, assets::DependencyRole::Generated,
                                     assets::DependencyRequirement::Required}) == assets::Result::Success;
        }
        if (state != nullptr && state->dependencyEnabled.GetValue() && request.source.identity == state->dependencyOwnerSource2 &&
            state->dependencyOutput2.IsValid())
        {
            return dependencies.Add({state->dependencyOutput2, {}, assets::DependencyRole::Generated,
                                     assets::DependencyRequirement::Required}) == assets::Result::Success;
        }
        return true;
    }

    [[nodiscard]] bool Estimate(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>,
                                assets::BuildResourceEstimate& estimate, void*) noexcept
    {
        estimate = {1, 4};
        return true;
    }

    [[nodiscard]] bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
    {
        auto& state = *static_cast<BuildBlockState*>(userData);
        static_cast<void>(state.compileCalls.Increment());
        if (state.block)
        {
            state.started.SetValue(true);
            while (!state.release.GetValue() && !context.IsCancellationRequested())
            {
                concurrency::YieldCurrentThread();
            }
            if (context.IsCancellationRequested())
                return false;
        }
        if (state.fail.GetValue())
            return false;
        u8 payload[4] = {'T', 'O', 'U', 'T'};
        if (state.contentFromSource)
        {
            const crypto::Digest256 sourceDigest = crypto::Sha256(context.request.source.content.Data(),
                                                                  context.request.source.content.Count());
            for (u32 index = 0; index < 4u; ++index)
                payload[index] = sourceDigest.bytes[index];
        }
        return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 2, payload,
                          sizeof(payload)) == assets::Result::Success;
    }

    [[nodiscard]] bool ParseHex64(const u8* const digits, const u32 count, u64& value) noexcept
    {
        if (count != 16u)
            return false;
        u64 result = 0;
        for (u32 index = 0; index < count; ++index)
        {
            const u8 character = digits[index];
            u8 digit = 0;
            if (character >= '0' && character <= '9')
                digit = static_cast<u8>(character - '0');
            else if (character >= 'a' && character <= 'f')
                digit = static_cast<u8>(character - 'a' + 10);
            else
                return false;
            result = (result << 4u) | digit;
        }
        value = result;
        return value != resources::InvalidResourceId;
    }

    // Authored references are stored as one 16-hex ResourcePath id per line.
    // A '#' comment line is ignored.
    [[nodiscard]] bool ScanSourceReferences(const assets::SourceRecord&, const assets::SourceDataView& data,
                                            assets::AssetReferenceCollector& references, void*) noexcept
    {
        const u8* const bytes = data.content.Data();
        const u32 size = data.content.Size();
        u32 start = 0;
        while (start < size)
        {
            u32 end = start;
            while (end < size && bytes[end] != '\n')
                ++end;
            u32 lineEnd = end;
            if (lineEnd > start && bytes[lineEnd - 1u] == '\r')
                --lineEnd;
            if (lineEnd > start && bytes[start] != '#')
            {
                u64 value = 0;
                if (!ParseHex64(bytes + start, lineEnd - start, value))
                    return false;
                const resources::ResourceReference reference(resources::ResourcePath::FromId(value), TestOutputType);
                if (!reference.IsValid() || !reference.IsTyped())
                    return false;
                if (references.Add({reference, resources::DependencyKind::Required, assets::AssetReferenceLocation::Primary, false}) !=
                    assets::Result::Success)
                    return false;
            }
            start = end + 1u;
        }
        return true;
    }

    [[nodiscard]] bool RewriteSourceReferences(const assets::SourceRecord& source, const assets::SourceDataView& data,
                                               const containers::ArraySpan<const assets::AssetReferenceReplacement> replacements,
                                               containers::DynamicArray<u8>& rewrittenSource, assets::AssetMetadata& rewrittenMetadata,
                                               void*) noexcept
    {
        rewrittenSource.Clear();
        const u8* const bytes = data.content.Data();
        const u32 size = data.content.Size();
        u32 start = 0;
        char buffer[32];
        while (start < size)
        {
            u32 end = start;
            while (end < size && bytes[end] != '\n')
                ++end;
            u32 lineEnd = end;
            if (lineEnd > start && bytes[lineEnd - 1u] == '\r')
                --lineEnd;
            if (lineEnd > start && bytes[start] != '#')
            {
                u64 value = 0;
                if (!ParseHex64(bytes + start, lineEnd - start, value))
                    return false;
                const resources::ResourceReference current(resources::ResourcePath::FromId(value), TestOutputType);
                bool replaced = false;
                for (u32 index = 0; index < replacements.Count(); ++index)
                {
                    if (!(replacements[index].previous == current))
                        continue;
                    replaced = true;
                    if (replacements[index].replacement.IsValid() && replacements[index].replacement.IsTyped())
                    {
                        const int written = std::snprintf(buffer, sizeof(buffer), "%016llx\n",
                                                          static_cast<unsigned long long>(replacements[index].replacement.GetPath().Id()));
                        for (int digit = 0; digit < written; ++digit)
                            rewrittenSource.PushBack(static_cast<u8>(buffer[digit]));
                    }
                    break;
                }
                if (!replaced)
                {
                    for (u32 index = start; index < end; ++index)
                        rewrittenSource.PushBack(bytes[index]);
                    if (end < size)
                        rewrittenSource.PushBack('\n');
                }
            }
            else
            {
                for (u32 index = start; index < end; ++index)
                    rewrittenSource.PushBack(bytes[index]);
                if (end < size)
                    rewrittenSource.PushBack('\n');
            }
            start = end + 1u;
        }
        rewrittenMetadata = source.metadata;
        return true;
    }

    // ---------------------------------------------------------------------
    // Small filesystem helpers over the production filesystem Manager.
    // ---------------------------------------------------------------------

    [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
    {
        const containers::StringView view = source.AsStringView();
        containers::String path(view.Data(), view.Length());
        path.Append(".vmeta", 6);
        return filesystem::AbsolutePath::CreateFilePath(path);
    }

    [[nodiscard]] filesystem::AbsolutePath SuffixedPath(const filesystem::AbsolutePath& source, const char* const suffix) noexcept
    {
        const containers::StringView view = source.AsStringView();
        containers::String path(view.Data(), view.Length());
        path.Append(suffix, static_cast<u32>(std::strlen(suffix)));
        return filesystem::AbsolutePath::CreateFilePath(containers::StringView(path.AsChar(), path.Length()));
    }

    [[nodiscard]] bool WriteBytes(filesystem::Manager& files, const filesystem::AbsolutePath& path, const u8* const data,
                                  const u32 size) noexcept
    {
        // RawFileWriter resolves through the process filesystem owner; the
        // manager argument is kept for symmetry with ReadBytes and the callers.
        static_cast<void>(files);
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        if (size != 0)
            writer->Serialize(const_cast<u8*>(data), size);
        writer->Flush();
        return writer->GetSize() == size;
    }

    [[nodiscard]] bool WriteMetadata(filesystem::Manager& files, const filesystem::AbsolutePath& sourcePath,
                                     const assets::AssetMetadata& metadata) noexcept
    {
        containers::String text;
        if (assets::EncodeMetadata(metadata, text) != assets::MetadataResult::Success)
            return false;
        return WriteBytes(files, MetadataPath(sourcePath), reinterpret_cast<const u8*>(text.AsChar()), text.Length());
    }

    [[nodiscard]] bool ReadBytes(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                 containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader)
            return false;
        const u64 size = reader->GetSize();
        if (size > 0xffffffffull)
            return false;
        bytes.Resize(static_cast<u32>(size));
        if (bytes.Size() != size)
            return false;
        if (size != 0)
            reader->Serialize(bytes.TypedData(), static_cast<size_t>(size));
        return !reader->HasErrors();
    }

    [[nodiscard]] bool SameBytes(const containers::DynamicArray<u8>& left, const containers::DynamicArray<u8>& right) noexcept
    {
        if (left.Size() != right.Size())
            return false;
        return left.Size() == 0 || std::memcmp(left.TypedData(), right.TypedData(), left.Size()) == 0;
    }

    [[nodiscard]] char FingerprintHexDigit(const u8 value) noexcept
    {
        return value < 10u ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10u);
    }

    [[nodiscard]] filesystem::AbsolutePath PersistentArtifactPath(const filesystem::AbsolutePath& root,
                                                                  const assets::BuildFingerprint& fingerprint) noexcept
    {
        char first[3] = {FingerprintHexDigit(fingerprint.bytes[0] >> 4u), FingerprintHexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {FingerprintHexDigit(fingerprint.bytes[1] >> 4u), FingerprintHexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        char fileName[70];
        for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
        {
            fileName[index * 2u] = FingerprintHexDigit(fingerprint.bytes[index] >> 4u);
            fileName[index * 2u + 1u] = FingerprintHexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        fileName[64] = '.';
        fileName[65] = 'v';
        fileName[66] = 'd';
        fileName[67] = 'd';
        fileName[68] = 'c';
        fileName[69] = '\0';
        return root.AddDirPath(first).AddDirPath(second).AddFilePath(fileName);
    }

    [[nodiscard]] filesystem::AbsolutePath LooseGenerationPath(const filesystem::AbsolutePath& root,
                                                               const resources::ResourceId resource,
                                                               const assets::BuildFingerprint& fingerprint,
                                                               const bool stage = false) noexcept
    {
        char first[3] = {FingerprintHexDigit(fingerprint.bytes[0] >> 4u),
                         FingerprintHexDigit(fingerprint.bytes[0] & 0x0fu), '\0'};
        char second[3] = {FingerprintHexDigit(fingerprint.bytes[1] >> 4u),
                          FingerprintHexDigit(fingerprint.bytes[1] & 0x0fu), '\0'};
        char fileName[91]{};
        for (u32 index = 0; index < 16u; ++index)
            fileName[index] = FingerprintHexDigit(static_cast<u8>((resource >> ((15u - index) * 4u)) & 0x0full));
        fileName[16] = '-';
        for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
        {
            fileName[17u + index * 2u] = FingerprintHexDigit(fingerprint.bytes[index] >> 4u);
            fileName[18u + index * 2u] = FingerprintHexDigit(fingerprint.bytes[index] & 0x0fu);
        }
        fileName[81] = '.';
        fileName[82] = 'v';
        fileName[83] = 'r';
        fileName[84] = 'e';
        fileName[85] = 's';
        if (stage)
        {
            fileName[86] = '.';
            fileName[87] = 't';
            fileName[88] = 'm';
            fileName[89] = 'p';
        }
        return root.AddDirPath(first).AddDirPath(second).AddFilePath(fileName);
    }

    [[nodiscard]] assets::AssetOutput Output() noexcept
    {
        assets::AssetOutput output;
        output.key = containers::String("main");
        output.type = TestOutputType;
        return output;
    }

    [[nodiscard]] bool CreateDefaultImportDescription(const assets::SourceRecord& candidate,
                                                      assets::SourceImportDescription& output, void*) noexcept
    {
        if (!candidate.path.IsFilePath() || candidate.importState != assets::SourceImportState::Candidate ||
            candidate.sourceType != TestSourceType || candidate.primaryOutputType != TestOutputType)
            return false;
        output.settingsVersion = 1u;
        output.settings.PushBack(static_cast<u8>(7u));
        output.outputs.PushBack(Output());
        return output.settings.Size() == 1u && output.outputs.Size() == 1u;
    }

    [[nodiscard]] bool ValidateDefaultImportSettings(const u32 settingsVersion,
                                                     const containers::ArraySpan<const u8> settings, void*) noexcept
    {
        return settingsVersion == 1u && settings.Count() == 1u && settings.Data() != nullptr && settings[0] == 7u;
    }

    [[nodiscard]] assets::AssetMetadata Metadata(const assets::AssetId id) noexcept
    {
        assets::AssetMetadata metadata;
        metadata.id = id;
        metadata.importer = assets::HashCompilerName(TestCompilerName);
        metadata.importerVersion = TestImporterVersion;
        metadata.settingsVersion = 1;
        metadata.settings.PushBack(static_cast<u8>(7));
        metadata.outputs.PushBack(Output());
        return metadata;
    }

    [[nodiscard]] resources::ResourceReference OutputReference(const assets::AssetId id) noexcept
    {
        return assets::GetAssetOutputReference(id, Output());
    }

    [[nodiscard]] containers::String ReferenceLine(const u64 id) noexcept
    {
        char buffer[32];
        const int written = std::snprintf(buffer, sizeof(buffer), "%016llx\n", static_cast<unsigned long long>(id));
        return containers::String(buffer, static_cast<u32>(written));
    }

    [[nodiscard]] bool WriteTextAsset(filesystem::Manager& files, const filesystem::AbsolutePath& path, const containers::StringView text,
                                      const assets::AssetMetadata& metadata) noexcept
    {
        if (!WriteBytes(files, path, reinterpret_cast<const u8*>(text.Data()), text.Length()))
            return false;
        return WriteMetadata(files, path, metadata);
    }

    // Manager::DeletePath removes only an empty directory, so remove the tree
    // bottom-up. Mirrors the engine-test cleanup pattern.
    [[nodiscard]] bool DeleteDirectoryTree(filesystem::Manager& files, const filesystem::AbsolutePath& directory) noexcept
    {
        bool removed = true;
        containers::DynamicArray<filesystem::AbsolutePath> children(memory::pools::Assets::GetInstance());
        files.FindDirectories(directory, children);
        for (const filesystem::AbsolutePath& child : children)
            removed = DeleteDirectoryTree(files, child) && removed;
        containers::DynamicArray<filesystem::AbsolutePath> storedFiles(memory::pools::Assets::GetInstance());
        files.FindFiles(directory, containers::String("*"), storedFiles, false);
        for (const filesystem::AbsolutePath& storedFile : storedFiles)
            removed = files.DeleteFile(storedFile) && removed;
        return files.DeletePath(directory) && removed;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "assetStreamingTests"), "diagnostics initialization");
    diagnostics::EnableCategory(diagnostics::Category::FunctionalTests, true);
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "io initialization");
    Check(reflection::Initialize(), "reflection initialization");

    const filesystem::AbsolutePath workingDirectory = vanguard::tests::ScratchDirectory("project_asset_pipeline_tests");
    const filesystem::AbsolutePath root = workingDirectory.AddDirPath("vanguard_asset_streaming_tests");
    const filesystem::AbsolutePath assetsRoot = root.AddDirPath("Assets");
    const filesystem::AbsolutePath derivedRoot = root.AddDirPath("Derived");
    const filesystem::AbsolutePath indexRoot = root.AddDirPath("Index");
    const filesystem::AbsolutePath looseRoot = root.AddDirPath("Loose");
    // Keep the configured transaction journal inside the authored source root so
    // E2.1 proves callback suppression and authoritative-rescan exclusion, not
    // merely that the normal test layout happens to place the journal elsewhere.
    const filesystem::AbsolutePath journalPath = assetsRoot.AddFilePath("operations.vtxn");

    // E2.6 P1: the frontend-neutral helper must preserve the exact editor
    // identity and fail closed without mutating caller state.
    asset_streaming::ProjectAssetPipelineConfig sharedIdentityConfig;
    Check(asset_streaming::MakeProjectAssetPipelineConfig(derivedRoot, root.AddDirPath("Intermediate"), "windows-x64",
                                                          sharedIdentityConfig),
          "E2.6 P1 shared project-asset config derives windows-x64");
    const filesystem::AbsolutePath sharedDerivedOwner = derivedRoot.AddDirPath("ProjectAssets");
    const filesystem::AbsolutePath sharedIntermediateOwner = root.AddDirPath("Intermediate").AddDirPath("ProjectAssets");
    Check(sharedIdentityConfig.build.artifactCacheRoot == sharedDerivedOwner.AddDirPath("VDDC"),
          "E2.6 P1 shared config preserves editor VDDC root");
    Check(sharedIdentityConfig.build.dependencyIndexRoot == sharedDerivedOwner.AddDirPath("Index"),
          "E2.6 P1 shared config preserves editor index root");
    Check(sharedIdentityConfig.publication.looseRoot == sharedIntermediateOwner.AddDirPath("Loose"),
          "E2.6 P1 shared config preserves editor loose root");
    Check(sharedIdentityConfig.operationJournalPath == sharedIntermediateOwner.AddFilePath("operations.vtxn"),
          "E2.6 P1 shared config preserves editor transaction journal");
    Check(sharedIdentityConfig.build.target == assets::TargetPlatform::WindowsD3D12 &&
              sharedIdentityConfig.publication.target == assets::TargetPlatform::WindowsD3D12,
          "E2.6 P1 windows-x64 maps exactly to WindowsD3D12");
    constexpr char editorSettingsIdentity[] = "vanguard.editor.project-assets.texture.v1";
    Check(sharedIdentityConfig.build.settingsFingerprint ==
              crypto::Sha256(editorSettingsIdentity, sizeof(editorSettingsIdentity) - 1u),
          "E2.6 P1 shared config preserves editor settings fingerprint");
    Check(sharedIdentityConfig.maximumOperationHistoryBytes == sharedIdentityConfig.maximumOperationFileBytes,
          "E2.6 P1 shared config preserves editor history byte bound");
    Check(!sharedIdentityConfig.automaticRecook.enabled,
          "E2.6 P1 shared config preserves disabled automatic recook default");

    asset_streaming::ProjectAssetPipelineConfig rejectedIdentityConfig;
    rejectedIdentityConfig.maximumPublishedResources = 17u;
    rejectedIdentityConfig.maximumOperationHistoryBytes = 19u;
    Check(!asset_streaming::MakeProjectAssetPipelineConfig(derivedRoot, root.AddDirPath("Intermediate"), "linux-x64",
                                                           rejectedIdentityConfig),
          "E2.6 P1 unsupported project target fails closed");
    Check(rejectedIdentityConfig.maximumPublishedResources == 17u &&
              rejectedIdentityConfig.maximumOperationHistoryBytes == 19u &&
              rejectedIdentityConfig.build.artifactCacheRoot.Empty(),
          "E2.6 P1 failed config derivation leaves output unchanged");

    Check(filesystem::Initialize({workingDirectory, workingDirectory, root}), "filesystem initialization");
    filesystem::Manager& files = filesystem::GetManager();
    static_cast<void>(DeleteDirectoryTree(files, root));
    Check(files.CreatePath(root) && files.CreatePath(assetsRoot) && files.CreatePath(derivedRoot) && files.CreatePath(indexRoot) &&
              files.CreatePath(looseRoot),
          "test directory creation");

    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    // ---------------------------------------------------------------------
    // Source catalog: one referrer plus several plain assets.
    // ---------------------------------------------------------------------

    assets::AssetId idA;
    assets::AssetId idB;
    assets::AssetId idReferrer;
    assets::AssetId idBound;
    assets::AssetId idLarge;
    assets::AssetId idBusy;
    Check(assets::GenerateAssetId(idA) && assets::GenerateAssetId(idB) && assets::GenerateAssetId(idReferrer) &&
              assets::GenerateAssetId(idBound) && assets::GenerateAssetId(idLarge) && assets::GenerateAssetId(idBusy),
          "asset identity generation");

    const filesystem::AbsolutePath pathA = assetsRoot.AddFilePath("a.tasset");
    const filesystem::AbsolutePath pathB = assetsRoot.AddFilePath("b.tasset");
    const filesystem::AbsolutePath pathReferrer = assetsRoot.AddFilePath("referrer.tasset");
    const filesystem::AbsolutePath pathBound = assetsRoot.AddFilePath("bound.tasset");
    const filesystem::AbsolutePath pathLarge = assetsRoot.AddFilePath("large.tasset");
    const filesystem::AbsolutePath pathBusy = assetsRoot.AddFilePath("busy.tasset");

    Check(WriteTextAsset(files, pathA, containers::StringView(""), Metadata(idA)), "write asset A");
    Check(WriteTextAsset(files, pathB, containers::StringView(""), Metadata(idB)), "write asset B");
    Check(WriteTextAsset(files, pathReferrer, containers::StringView(ReferenceLine(OutputReference(idA).GetPath().Id())),
                         Metadata(idReferrer)),
          "write referrer");
    Check(WriteTextAsset(files, pathBound, containers::StringView(""), Metadata(idBound)), "write bound asset");
    Check(WriteTextAsset(files, pathBusy, containers::StringView(""), Metadata(idBusy)), "write busy asset");

    containers::DynamicArray<u8> largeContent(memory::pools::Assets::GetInstance());
    largeContent.Resize(LargeContentSize);
    largeContent[0] = '#';
    for (u32 index = 1; index < LargeContentSize; ++index)
        largeContent[index] = 'L';
    Check(WriteBytes(files, pathLarge, largeContent.TypedData(), largeContent.Size()) && WriteMetadata(files, pathLarge, Metadata(idLarge)),
          "write large asset");

    containers::DynamicArray<assets::SourceRoot> roots(memory::pools::Assets::GetInstance());
    roots.PushBack({assetsRoot, false});
    const containers::ArraySpan<const assets::SourceRoot> rootSpan(roots.TypedData(), roots.Size());
    assets::SourceDatabase sources;
    Check(sources.Rescan(files, rootSpan) == assets::SourceDatabaseResult::Success, "source catalog rescan");

    // ---------------------------------------------------------------------
    // Production pipeline composition (compiler + importer + streamer).
    // ---------------------------------------------------------------------

    BuildBlockState block;
    block.fail.SetValue(false);
    assets::CompilerDescriptor compiler{assets::HashCompilerName(TestCompilerName), TestCompilerName, TestImporterVersion, TestSourceType,
                                        TestOutputType, &Discover, &Compile, &block, &Estimate};
    assets::SourceImporterDescriptor importer = assets::MakeDirectSourceImporterDescriptor(compiler, &RecognizeSource, nullptr,
                                                                                           &RewriteSourceReferences, &assets::RelocateSourceUnchanged);
    importer.scanSourceReferences = &ScanSourceReferences;

    containers::DynamicArray<assets::CompilerDescriptor> compilers(memory::pools::Assets::GetInstance());
    compilers.PushBack(compiler);
    containers::DynamicArray<assets::SourceImporterDescriptor> importers(memory::pools::Assets::GetInstance());
    importers.PushBack(importer);
    const containers::ArraySpan<const assets::CompilerDescriptor> compilerSpan(compilers.TypedData(), compilers.Size());
    const containers::ArraySpan<const assets::SourceImporterDescriptor> importerSpan(importers.TypedData(), importers.Size());

    resources::ResourceRegistry registry;
    resources::ResourcePipeline resourcePipeline;
    streaming::ResourceStreamer streamer;
    Check(registry.Initialize(), "resource registry initialization");
    Check(resourcePipeline.Initialize(registry), "resource pipeline initialization");
    Check(streamer.Initialize(resourcePipeline), "resource streamer initialization");

    asset_streaming::ProjectAssetPipelineConfig config;
    config.build.artifactCacheRoot = derivedRoot;
    config.build.dependencyIndexRoot = indexRoot;
    config.build.target = assets::TargetPlatform::WindowsD3D12;
    const char settingsIdentity[] = "assetStreamingTests";
    config.build.settingsFingerprint = crypto::Sha256(settingsIdentity, sizeof(settingsIdentity) - 1u);
    config.publication.artifactCacheRoot = derivedRoot;
    config.publication.looseRoot = looseRoot;
    config.publication.target = assets::TargetPlatform::WindowsD3D12;
    config.operationJournalPath = journalPath;
    config.maximumOperationEntries = 4096;
    config.maximumOperationFileBytes = MaximumOperationFileBytes;
    config.maximumOperationStagedBytes = MaximumOperationStagedBytes;
    config.maximumOperationHistoryRecords = MaximumHistoryRecords;
    config.maximumOperationHistoryBytes = MaximumOperationHistoryBytes;

    asset_streaming::ProjectAssetPipeline pipeline;
    asset_streaming::ProjectAssetPipelineFailure initFailure;
    asset_streaming::ProjectAssetRestoreReport initialRestore;
    const asset_streaming::ProjectAssetPipelineResult initResult =
        pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config, &initialRestore, &initFailure);
    if (initResult != asset_streaming::ProjectAssetPipelineResult::Success)
    {
        std::fprintf(stderr,
                     "[assetStreamingTests] Initialize result=%d build=%d publication=%d sources=%d operation=%d transaction=%d\n",
                     static_cast<int>(initFailure.result), static_cast<int>(initFailure.build),
                     static_cast<int>(initFailure.publication.result), static_cast<int>(initFailure.sources),
                     static_cast<int>(initFailure.operation.result), static_cast<int>(initFailure.operation.transaction));
        std::fprintf(stderr, "[assetStreamingTests] Initialize journal=%s loose=%s derived=%s index=%s\n", journalPath.AsChar(),
                     looseRoot.AsChar(), derivedRoot.AsChar(), indexRoot.AsChar());
    }
    Check(initResult == asset_streaming::ProjectAssetPipelineResult::Success, "project asset pipeline initialization");
    Check(pipeline.IsInitialized() && pipeline.CanMutateSources(), "pipeline is initialized and quiescent");

    // E2.5 P3: VDDC maintenance is a separately bounded, off-thread owner that
    // freezes project/delivery protection during its batch and excludes source,
    // manual-build and automatic-recook admission until the owner tick adopts
    // completion. The initial derived-data root is empty, so this first pass is
    // deterministic and must complete without inventing cache state.
    Check(config.cacheMaintenance.IsValid(), "E2.5 P3 default cache-maintenance bounds are valid");
    asset_streaming::ProjectAssetCacheMaintenanceConfig invalidMaintenance = config.cacheMaintenance;
    invalidMaintenance.maximumVddcScanEntries = 0u;
    Check(!invalidMaintenance.IsValid(), "E2.5 P3 rejects a zero VDDC scan bound");
    const asset_streaming::ProjectAssetVddcMaintenanceStatus initialMaintenance = pipeline.GetVddcMaintenanceStatus();
    Check(initialMaintenance.state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle &&
              pipeline.GetVddcMaintenanceDiagnostics().Empty() &&
              pipeline.RetryVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::InvalidState &&
              pipeline.CancelVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::InvalidState,
          "E2.5 P3 maintenance starts idle with no implicit retry/cancel work");
    Check(pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
              pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Running &&
              pipeline.GetVddcMaintenanceStatus().protectionEpoch != 0u && pipeline.GetVddcMaintenanceDiagnostics().Empty() &&
              !pipeline.CanMutateSources(),
          "E2.5 P3 request captures protection and dispatches maintenance without exposing worker diagnostics on the owner tick");
    asset_streaming::ProjectAssetPipelineFailure maintenanceBusyFailure;
    asset_streaming::ProjectAssetRequest maintenanceBlockedManual =
        pipeline.Request(OutputReference(idA), assets::BuildPriority::Normal, &maintenanceBusyFailure);
    Check(!maintenanceBlockedManual.IsValid() &&
              maintenanceBusyFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy &&
              pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy &&
              pipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Busy &&
              pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Busy,
          "E2.5 P3 maintenance admission excludes manual, automatic, rescan and concurrent maintenance work");
    Check(DrainVddcMaintenance(pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
          "E2.5 P3 owner tick adopts completed maintenance");
    const asset_streaming::ProjectAssetVddcMaintenanceStatus completedMaintenance = pipeline.GetVddcMaintenanceStatus();
    Check(completedMaintenance.state == asset_streaming::ProjectAssetVddcMaintenanceState::Complete &&
              completedMaintenance.lastResult == assets::PersistentCacheMaintenanceResult::Success &&
              completedMaintenance.scannedRecords == 0u && completedMaintenance.scannedBytes == 0u &&
              completedMaintenance.removedRecords == 0u && completedMaintenance.budgetSatisfied &&
              !completedMaintenance.retryRequired && !completedMaintenance.limitExceeded &&
              pipeline.GetVddcMaintenanceDiagnostics().Empty() && pipeline.CanMutateSources(),
          "E2.5 P3 empty-cache completion is bounded, truthful and releases admission without auto-spin");

    // P3 owner/worker diagnostics handoff. Use a separate real pipeline/cache
    // root plus two standalone same-root BuildSystem publications. The candidate
    // that sorts first keeps its publication lease while the second releases its
    // lease. Once the second record disappears, the worker has necessarily
    // passed the first candidate and emitted LeaseBusy, but no owner Tick has yet
    // adopted completion. This makes the public diagnostics assertion
    // discriminate the old shared-buffer race instead of relying on timing.
    {
        const filesystem::AbsolutePath diagnosticsRoot = root.AddDirPath("E25P3Diagnostics");
        const filesystem::AbsolutePath diagnosticsAssetsRoot = diagnosticsRoot.AddDirPath("Assets");
        const filesystem::AbsolutePath diagnosticsDerivedRoot = diagnosticsRoot.AddDirPath("Derived");
        const filesystem::AbsolutePath diagnosticsIndexRoot = diagnosticsRoot.AddDirPath("Index");
        const filesystem::AbsolutePath diagnosticsLooseRoot = diagnosticsRoot.AddDirPath("Loose");
        const filesystem::AbsolutePath diagnosticsJournal = diagnosticsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(diagnosticsRoot) && files.CreatePath(diagnosticsAssetsRoot) && files.CreatePath(diagnosticsDerivedRoot) &&
                  files.CreatePath(diagnosticsIndexRoot) && files.CreatePath(diagnosticsLooseRoot),
              "E2.5 P3 diagnostics fixture roots are created");

        assets::AssetId diagnosticsIdA;
        assets::AssetId diagnosticsIdB;
        Check(assets::GenerateAssetId(diagnosticsIdA) && assets::GenerateAssetId(diagnosticsIdB),
              "E2.5 P3 diagnostics fixture identities are generated");
        const filesystem::AbsolutePath diagnosticsPathA = diagnosticsAssetsRoot.AddFilePath("diagnostics_a.tasset");
        const filesystem::AbsolutePath diagnosticsPathB = diagnosticsAssetsRoot.AddFilePath("diagnostics_b.tasset");
        Check(WriteTextAsset(files, diagnosticsPathA, containers::StringView("diagnostics-a\n"), Metadata(diagnosticsIdA)) &&
                  WriteTextAsset(files, diagnosticsPathB, containers::StringView("diagnostics-b\n"), Metadata(diagnosticsIdB)),
              "E2.5 P3 diagnostics fixture sources are written");
        containers::DynamicArray<assets::SourceRoot> diagnosticsRoots(memory::pools::Assets::GetInstance());
        diagnosticsRoots.PushBack({diagnosticsAssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> diagnosticsRootSpan(diagnosticsRoots.TypedData(), diagnosticsRoots.Size());
        assets::SourceDatabase diagnosticsSources;
        Check(diagnosticsSources.Rescan(files, diagnosticsRootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P3 diagnostics fixture source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig diagnosticsConfig = config;
        diagnosticsConfig.build.artifactCacheRoot = diagnosticsDerivedRoot;
        diagnosticsConfig.build.dependencyIndexRoot = diagnosticsIndexRoot;
        diagnosticsConfig.publication.artifactCacheRoot = diagnosticsDerivedRoot;
        diagnosticsConfig.publication.looseRoot = diagnosticsLooseRoot;
        diagnosticsConfig.operationJournalPath = diagnosticsJournal;
        diagnosticsConfig.automaticRecook.enabled = false;
        diagnosticsConfig.cacheMaintenance.maximumVddcBytes = 1u;
        diagnosticsConfig.cacheMaintenance.maximumVddcProtectionRecords = 1u;

        asset_streaming::ProjectAssetPipeline diagnosticsPipeline;
        Check(diagnosticsPipeline.Initialize(files, diagnosticsSources, streamer, compilerSpan, importerSpan, diagnosticsConfig) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 diagnostics fixture pipeline initializes");

        assets::BuildSystem diagnosticsBuilder;
        assets::Config diagnosticsBuildConfig;
        diagnosticsBuildConfig.maximumCacheEntries = 0u;
        diagnosticsBuildConfig.persistentCacheRoot = diagnosticsDerivedRoot.AsChar();
        Check(diagnosticsBuilder.Initialize(diagnosticsBuildConfig) &&
                  diagnosticsBuilder.RegisterCompiler(compiler) == assets::Result::Success,
              "E2.5 P3 diagnostics fixture shares the production root authority with a standalone writer");
        const u8 diagnosticsSourceA[] = {'p', '3', 'a'};
        const u8 diagnosticsSourceB[] = {'p', '3', 'b'};
        const assets::BuildRequest diagnosticsBuildA{
            {resources::ResourceReference(resources::ResourcePath::FromString("source/p3-diagnostics-a.asset"), TestSourceType),
             {diagnosticsSourceA, sizeof(diagnosticsSourceA)}, {}},
            resources::ResourceReference(resources::ResourcePath::FromString("cooked/p3-diagnostics-a.asset"), TestOutputType),
            assets::TargetPlatform::WindowsD3D12,
            {}};
        const assets::BuildRequest diagnosticsBuildB{
            {resources::ResourceReference(resources::ResourcePath::FromString("source/p3-diagnostics-b.asset"), TestSourceType),
             {diagnosticsSourceB, sizeof(diagnosticsSourceB)}, {}},
            resources::ResourceReference(resources::ResourcePath::FromString("cooked/p3-diagnostics-b.asset"), TestOutputType),
            assets::TargetPlatform::WindowsD3D12,
            {}};
        assets::BuildOutput diagnosticsOutputA;
        assets::BuildOutput diagnosticsOutputB;
        Check(diagnosticsBuilder.Build(diagnosticsBuildA, diagnosticsOutputA) == assets::Result::Success &&
                  diagnosticsBuilder.Build(diagnosticsBuildB, diagnosticsOutputB) == assets::Result::Success,
              "E2.5 P3 diagnostics fixture creates two real same-root VDDC candidates");

        const filesystem::AbsolutePath diagnosticsRecordA =
            PersistentArtifactPath(diagnosticsDerivedRoot, diagnosticsOutputA.buildFingerprint);
        const filesystem::AbsolutePath diagnosticsRecordB =
            PersistentArtifactPath(diagnosticsDerivedRoot, diagnosticsOutputB.buildFingerprint);
        const u64 diagnosticsTimestampA = files.GetFileTimeStamp(diagnosticsRecordA);
        const u64 diagnosticsTimestampB = files.GetFileTimeStamp(diagnosticsRecordB);
        const auto fingerprintLess = [](const assets::BuildFingerprint& left, const assets::BuildFingerprint& right) noexcept
        {
            for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
            {
                if (left.bytes[index] != right.bytes[index])
                    return left.bytes[index] < right.bytes[index];
            }
            return false;
        };
        const bool aSortsFirst = diagnosticsTimestampA < diagnosticsTimestampB ||
                                 (diagnosticsTimestampA == diagnosticsTimestampB &&
                                  fingerprintLess(diagnosticsOutputA.buildFingerprint, diagnosticsOutputB.buildFingerprint));
        assets::BuildOutput& diagnosticsBusyOutput = aSortsFirst ? diagnosticsOutputA : diagnosticsOutputB;
        assets::BuildOutput& diagnosticsFreeOutput = aSortsFirst ? diagnosticsOutputB : diagnosticsOutputA;
        const filesystem::AbsolutePath diagnosticsBusyRecord = aSortsFirst ? diagnosticsRecordA : diagnosticsRecordB;
        const filesystem::AbsolutePath diagnosticsFreeRecord = aSortsFirst ? diagnosticsRecordB : diagnosticsRecordA;
        diagnosticsFreeOutput.Reset();
        Check(diagnosticsTimestampA != 0u && diagnosticsTimestampB != 0u && files.FileExist(diagnosticsBusyRecord) &&
                  files.FileExist(diagnosticsFreeRecord),
              "E2.5 P3 diagnostics fixture has deterministic canonical victim-order evidence");

        Check(diagnosticsPipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  diagnosticsPipeline.GetVddcMaintenanceDiagnostics().Empty(),
              "E2.5 P3 diagnostics worker dispatch starts from an empty owner-visible snapshot");
        bool freeRecordRemoved = false;
        for (u32 attempt = 0; attempt < 200000u && !freeRecordRemoved; ++attempt)
        {
            freeRecordRemoved = !files.FileExist(diagnosticsFreeRecord);
            if (!freeRecordRemoved)
                concurrency::YieldCurrentThread();
        }
        Check(freeRecordRemoved &&
                  diagnosticsPipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Running &&
                  diagnosticsPipeline.GetVddcMaintenanceDiagnostics().Empty(),
              "E2.5 P3 completed worker diagnostics remain private until owner-tick adoption");
        Check(DrainVddcMaintenance(diagnosticsPipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  diagnosticsPipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Deferred &&
                  diagnosticsPipeline.GetVddcMaintenanceStatus().deferredDeletes == 1u &&
                  diagnosticsPipeline.GetVddcMaintenanceDiagnostics().Size() == 1u &&
                  diagnosticsPipeline.GetVddcMaintenanceDiagnostics()[0].reason ==
                      assets::PersistentCacheMaintenanceDiagnosticReason::LeaseBusy,
              "E2.5 P3 owner tick atomically adopts the worker LeaseBusy diagnostic");

        // Keep that adopted row visible, then create two current VADI roots. The
        // configured one-record protection bound forces the next accepted
        // maintenance request to fail before dispatch; accepted-request reset must
        // still retire the stale diagnostic immediately.
        asset_streaming::ProjectAssetRequest diagnosticsRequestA = diagnosticsPipeline.Request(OutputReference(diagnosticsIdA));
        Check(diagnosticsRequestA.IsValid(), "E2.5 P3 diagnostics first durable request starts");
        diagnosticsRequestA.Wait();
        Check(diagnosticsRequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 diagnostics first durable request publishes");
        diagnosticsRequestA.Reset();
        asset_streaming::ProjectAssetRequest diagnosticsRequestB = diagnosticsPipeline.Request(OutputReference(diagnosticsIdB));
        Check(diagnosticsRequestB.IsValid(), "E2.5 P3 diagnostics second durable request starts");
        diagnosticsRequestB.Wait();
        Check(diagnosticsRequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 diagnostics second durable request publishes");
        diagnosticsRequestB.Reset();
        Check(diagnosticsPipeline.GetVddcMaintenanceDiagnostics().Size() == 1u,
              "E2.5 P3 prior terminal diagnostic remains owner-visible until the next accepted maintenance request");
        asset_streaming::ProjectAssetPipelineFailure diagnosticsFailure;
        Check(diagnosticsPipeline.RequestVddcMaintenance(&diagnosticsFailure) ==
                  asset_streaming::ProjectAssetPipelineResult::CacheMaintenanceFailed &&
                  diagnosticsFailure.maintenance == assets::PersistentCacheMaintenanceResult::LimitExceeded &&
                  diagnosticsPipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Failed &&
                  diagnosticsPipeline.GetVddcMaintenanceDiagnostics().Empty(),
              "E2.5 P3 accepted pre-dispatch failure cannot retain diagnostics from the prior run");

        diagnosticsBusyOutput.Reset();
        Check(diagnosticsBuilder.Shutdown(), "E2.5 P3 diagnostics standalone writer shuts down cleanly");
        Check(diagnosticsPipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 diagnostics pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, diagnosticsRoot), "E2.5 P3 diagnostics fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.5 P4 exact loose-generation retirement. Replacing a manual
    // publication only records the superseded physical generation. Deletion is
    // explicit, waits for exact resident retirement, and is then still fenced by
    // ResourceStreamer's generation leases.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E25P4ExactRetirement");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.5 P4 exact-retirement fixture roots are created");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.5 P4 exact-retirement identity is generated");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("retired.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-generation-a\n"), Metadata(p4Id)),
              "E2.5 P4 exact-retirement generation A source is written");

        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P4 exact-retirement source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 exact-retirement pipeline initializes");

        asset_streaming::ProjectAssetRequest p4RequestA = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestA.IsValid(), "E2.5 P4 manual generation A request starts");
        p4RequestA.Wait();
        Check(p4RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 manual generation A publishes");
        p4RequestA.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusA;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusA) &&
                  !p4StatusA.publishedPhysicalPath.Empty() && files.FileExist(p4StatusA.publishedPhysicalPath),
              "E2.5 P4 generation A exposes one exact loose path");
        const filesystem::AbsolutePath p4GenerationA = p4StatusA.publishedPhysicalPath;

        asset_streaming::ProjectAssetResidentDeliveryEvidence p4ResidentA;
        p4ResidentA.resource = OutputReference(p4Id);
        p4ResidentA.resourceGeneration = 71u;
        p4ResidentA.buildFingerprint = p4StatusA.publishedBuildFingerprint;
        p4ResidentA.contentFingerprint = p4StatusA.publishedContentFingerprint;
        Check(p4Pipeline.ReportResidentDelivery(p4ResidentA),
              "E2.5 P4 exact old resident generation is reported before replacement");

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-generation-b\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 source advances from generation A to B");
        asset_streaming::ProjectAssetRequest p4RequestB = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestB.IsValid(), "E2.5 P4 manual generation B request starts");
        p4RequestB.Wait();
        Check(p4RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 manual generation B replaces the descriptor");
        p4RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusB;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusB) &&
                  p4StatusB.publishedPhysicalPath != p4GenerationA && files.FileExist(p4GenerationA) &&
                  files.FileExist(p4StatusB.publishedPhysicalPath) && p4Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 1u,
              "E2.5 P4 manual replacement records A for later maintenance without eagerly deleting it");
        const filesystem::AbsolutePath p4GenerationB = p4StatusB.publishedPhysicalPath;

        streaming::ResourceSource p4OldSource;
        Check(p4OldSource.OpenLoose(p4GenerationA) == streaming::ResourceSourceResult::Success && p4OldSource.IsOpen(),
              "E2.5 P4 direct old managed ResourceSource acquires an exact generation lease");

        asset_streaming::ProjectAssetResidentDeliveryEvidence p4ResidentB;
        p4ResidentB.resource = OutputReference(p4Id);
        p4ResidentB.resourceGeneration = 72u;
        p4ResidentB.buildFingerprint = p4StatusB.publishedBuildFingerprint;
        p4ResidentB.contentFingerprint = p4StatusB.publishedContentFingerprint;
        Check(p4Pipeline.ReportResidentDelivery(p4ResidentB),
              "E2.5 P4 newer resident delivery is accepted without retiring resident generation A");
        asset_streaming::ProjectAssetResidentRetirementEvidence p4WrongRetirement;
        p4WrongRetirement.resource = OutputReference(p4Id).GetPath();
        p4WrongRetirement.resourceGeneration = 72u;
        Check(p4Pipeline.ReportResidentRetirement(p4WrongRetirement) == asset_streaming::ProjectAssetResidentRetirementResult::NotTracked,
              "E2.5 P4 newer resident generation cannot retire the exact old candidate");
        p4WrongRetirement.resourceGeneration = 70u;
        Check(p4Pipeline.ReportResidentRetirement(p4WrongRetirement) == asset_streaming::ProjectAssetResidentRetirementResult::NotTracked,
              "E2.5 P4 unrelated stale resident generation cannot retire the exact old candidate");

        asset_streaming::ProjectAssetResidentRetirementEvidence p4ExactRetirement;
        p4ExactRetirement.resource = OutputReference(p4Id).GetPath();
        p4ExactRetirement.resourceGeneration = 71u;
        Check(p4Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  p4Pipeline.ReportResidentRetirement(p4ExactRetirement) == asset_streaming::ProjectAssetResidentRetirementResult::Busy &&
                  DrainLooseMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 active loose-maintenance admission retains exact resident-retirement evidence for caller retry");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4ResidentProtected = p4Pipeline.GetLooseMaintenanceStatus();
        Check(p4ResidentProtected.state == asset_streaming::ProjectAssetLooseMaintenanceState::Deferred &&
                  p4ResidentProtected.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::Deferred &&
                  p4ResidentProtected.residentProtectedRecords == 1u && p4ResidentProtected.removedRecords == 0u &&
                  p4ResidentProtected.retryRequired && files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB),
              "E2.5 P4 newer delivery alone does not clear exact old resident retirement protection");

        Check(p4Pipeline.ReportResidentRetirement(p4ExactRetirement) == asset_streaming::ProjectAssetResidentRetirementResult::Accepted &&
                  p4Pipeline.ReportResidentRetirement(p4ExactRetirement) == asset_streaming::ProjectAssetResidentRetirementResult::NotTracked,
              "E2.5 P4 exact resident retirement is accepted once and cannot be replayed");
        Check(p4Pipeline.RetryLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 retry reaches streamer authority after exact resident retirement");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4StreamerProtected = p4Pipeline.GetLooseMaintenanceStatus();
        Check(p4StreamerProtected.state == asset_streaming::ProjectAssetLooseMaintenanceState::Deferred &&
                  p4StreamerProtected.streamerProtectedRecords == 1u && p4StreamerProtected.deferredDeletes == 1u &&
                  p4StreamerProtected.removedRecords == 0u && files.FileExist(p4GenerationA),
              "E2.5 P4 old managed ResourceSource lease blocks physical deletion after resident retirement");

        p4OldSource.Close();
        Check(p4Pipeline.RetryLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 explicit retry runs after the exact streamer lease closes");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4Removed = p4Pipeline.GetLooseMaintenanceStatus();
        Check(p4Removed.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  p4Removed.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::Success &&
                  p4Removed.trackedCandidates == 0u && p4Removed.removedRecords == 1u && !p4Removed.retryRequired &&
                  !files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB),
              "E2.5 P4 explicit retry removes only superseded A once every exact owner has released it");

        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 exact-retirement pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p4Root), "E2.5 P4 exact-retirement fixture cleanup succeeds");
    }

    // P4 replacement reserves retirement capacity before descriptor visibility.
    // With one retained superseded generation already consuming the bound, the
    // next replacement must fail closed and leave the current descriptor intact.
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E25P4CandidateCapacity");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.5 P4 candidate-capacity fixture roots are created");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.5 P4 candidate-capacity identity is generated");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("bounded.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-capacity-a\n"), Metadata(p4Id)),
              "E2.5 P4 candidate-capacity generation A source is written");
        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P4 candidate-capacity source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;
        p4Config.cacheMaintenance.maximumRetiredLooseCandidates = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity pipeline initializes");

        asset_streaming::ProjectAssetRequest p4RequestA = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestA.IsValid(), "E2.5 P4 candidate-capacity generation A request starts");
        p4RequestA.Wait();
        Check(p4RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity generation A publishes");
        p4RequestA.Reset();

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-capacity-b\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity source advances to B");
        asset_streaming::ProjectAssetRequest p4RequestB = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestB.IsValid(), "E2.5 P4 candidate-capacity generation B request starts");
        p4RequestB.Wait();
        Check(p4RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity generation B publishes and retains A");
        p4RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusB;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusB) &&
                  p4Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 1u,
              "E2.5 P4 one-entry retirement capacity is fully occupied before the next replacement");
        const filesystem::AbsolutePath p4GenerationB = p4StatusB.publishedPhysicalPath;

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-capacity-c\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity source advances to C");
        asset_streaming::ProjectAssetRequest p4RequestC = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestC.IsValid(), "E2.5 P4 candidate-capacity generation C build request starts");
        p4RequestC.Wait();
        asset_streaming::ProjectAssetPipelineFailure p4CapacityFailure;
        Check(p4RequestC.Poll(&p4CapacityFailure) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                  p4CapacityFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P4 exhausted retirement capacity rejects replacement before descriptor cutover");
        p4RequestC.Reset();

        streaming::ResourceSource p4CurrentSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p4CurrentDependencies(memory::pools::Streaming::GetInstance());
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AfterRejectedC;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4AfterRejectedC) &&
                  p4AfterRejectedC.publishedPhysicalPath == p4GenerationB &&
                  streamer.OpenSource(OutputReference(p4Id), p4CurrentSource, p4CurrentDependencies) == resources::Failure::None &&
                  p4CurrentSource.GetPhysicalPath() == p4GenerationB &&
                  p4Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 1u,
              "E2.5 P4 capacity failure preserves exact generation B visibility and retained-candidate state");
        p4CurrentSource.Close();

        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 candidate-capacity pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p4Root), "E2.5 P4 candidate-capacity fixture cleanup succeeds");
    }

    // P5 restart/relocation recovery reconstructs only canonical stale complete
    // generations from disk. Exact canonical .vres.tmp stages are owned recovery
    // debris, while arbitrary .tmp, unrelated files, and canonical-looking files
    // in the wrong shard remain foreign and untouched.
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E25P4RestartRediscovery");
        const filesystem::AbsolutePath p4RelocatedRoot = root.AddDirPath("E25P5RelocatedRestartRediscovery");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        static_cast<void>(DeleteDirectoryTree(files, p4RelocatedRoot));
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.5 P4 restart-rediscovery fixture roots are created");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.5 P4 restart-rediscovery identity is generated");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("restart.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-restart-a\n"), Metadata(p4Id)),
              "E2.5 P4 restart-rediscovery generation A source is written");
        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P4 restart-rediscovery source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery first pipeline initializes");
        asset_streaming::ProjectAssetRequest p4RequestA = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestA.IsValid(), "E2.5 P4 restart-rediscovery generation A request starts");
        p4RequestA.Wait();
        Check(p4RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery generation A publishes");
        p4RequestA.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusA;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusA),
              "E2.5 P4 restart-rediscovery captures generation A delivery status");
        const filesystem::AbsolutePath p4GenerationA = p4StatusA.publishedPhysicalPath;

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-restart-b\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery source advances to B");
        asset_streaming::ProjectAssetRequest p4RequestB = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestB.IsValid(), "E2.5 P4 restart-rediscovery generation B request starts");
        p4RequestB.Wait();
        Check(p4RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery generation B publishes");
        p4RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusB;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusB) &&
                  p4StatusB.publishedPhysicalPath != p4GenerationA && files.FileExist(p4GenerationA),
              "E2.5 P4 restart-rediscovery leaves canonical stale generation A on disk");
        const filesystem::AbsolutePath p4GenerationB = p4StatusB.publishedPhysicalPath;
        const filesystem::AbsolutePath p4Stage = SuffixedPath(p4GenerationA, ".tmp");
        const filesystem::AbsolutePath p4Foreign = p4LooseRoot.AddFilePath("foreign.bin");
        const filesystem::AbsolutePath p4ForeignTemporary = p4LooseRoot.AddFilePath("foreign.tmp");
        const filesystem::AbsolutePath p4MisplacedDirectory = p4LooseRoot.AddDirPath("misplaced");
        const filesystem::AbsolutePath p4MisplacedCanonical =
            p4MisplacedDirectory.AddFilePath(filesystem::paths::GetFileName(p4GenerationA));
        const containers::StringView p4CanonicalName = filesystem::paths::GetFileName(p4GenerationA);
        char p4MalformedName[87]{};
        std::memcpy(p4MalformedName, p4CanonicalName.Data(), p4CanonicalName.Length());
        p4MalformedName[17] = 'g';
        const filesystem::AbsolutePath p4MalformedCanonical =
            filesystem::paths::ParentAbsolutePath(p4GenerationA).AddFilePath(p4MalformedName);
        const filesystem::AbsolutePath p4OutsideManagedRoot = p4Root.AddFilePath("outside.vres");
        const u8 p4Marker = 0x4u;
        Check(files.CreatePath(p4MisplacedDirectory) &&
                  WriteBytes(files, p4Stage, &p4Marker, 1u) &&
                  WriteBytes(files, p4Foreign, &p4Marker, 1u) &&
                  WriteBytes(files, p4ForeignTemporary, &p4Marker, 1u) &&
                  WriteBytes(files, p4MisplacedCanonical, &p4Marker, 1u) &&
                  WriteBytes(files, p4MalformedCanonical, &p4Marker, 1u) &&
                  WriteBytes(files, p4OutsideManagedRoot, &p4Marker, 1u),
              "E2.5 P5 restart-rediscovery creates owned stage and foreign/misplaced sentinels");
        Check(streamer.RetireLooseGeneration(p4OutsideManagedRoot) == streaming::LooseGenerationRetireResult::NotManaged &&
                  files.FileExist(p4OutsideManagedRoot),
              "E2.5 P5 managed loose authority rejects lexical outside-root retirement without mutation");
        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery first pipeline shuts down without implicit pruning");
        Check(files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB) && files.FileExist(p4Stage) &&
                  files.FileExist(p4Foreign) && files.FileExist(p4ForeignTemporary) &&
                  files.FileExist(p4MisplacedCanonical) && files.FileExist(p4MalformedCanonical) &&
                  files.FileExist(p4OutsideManagedRoot),
              "E2.5 P5 shutdown preserves stale/current and all recovery sentinels before relocation");

        Check(filesystem::SystemIO::MoveFile(p4Root.AsChar(), p4RelocatedRoot.AsChar()),
              "E2.5 P5 whole-project fixture relocates after clean shutdown");
        const filesystem::AbsolutePath p4RelocatedAssetsRoot = p4RelocatedRoot.AddDirPath("Assets");
        const filesystem::AbsolutePath p4RelocatedDerivedRoot = p4RelocatedRoot.AddDirPath("Derived");
        const filesystem::AbsolutePath p4RelocatedIndexRoot = p4RelocatedRoot.AddDirPath("Index");
        const filesystem::AbsolutePath p4RelocatedLooseRoot = p4RelocatedRoot.AddDirPath("Loose");
        const filesystem::AbsolutePath p4RelocatedJournal = p4RelocatedAssetsRoot.AddFilePath("operations.vtxn");
        const filesystem::AbsolutePath p4RelocatedGenerationA =
            LooseGenerationPath(p4RelocatedLooseRoot, OutputReference(p4Id).GetPath().Id(), p4StatusA.publishedBuildFingerprint);
        const filesystem::AbsolutePath p4RelocatedGenerationB =
            LooseGenerationPath(p4RelocatedLooseRoot, OutputReference(p4Id).GetPath().Id(), p4StatusB.publishedBuildFingerprint);
        const filesystem::AbsolutePath p4RelocatedStage = SuffixedPath(p4RelocatedGenerationA, ".tmp");
        const filesystem::AbsolutePath p4RelocatedForeign = p4RelocatedLooseRoot.AddFilePath("foreign.bin");
        const filesystem::AbsolutePath p4RelocatedForeignTemporary = p4RelocatedLooseRoot.AddFilePath("foreign.tmp");
        const filesystem::AbsolutePath p4RelocatedMisplaced =
            p4RelocatedLooseRoot.AddDirPath("misplaced").AddFilePath(filesystem::paths::GetFileName(p4GenerationA));
        const filesystem::AbsolutePath p4RelocatedMalformed =
            filesystem::paths::ParentAbsolutePath(p4RelocatedGenerationA).AddFilePath(p4MalformedName);
        const filesystem::AbsolutePath p4RelocatedOutside = p4RelocatedRoot.AddFilePath("outside.vres");
        containers::DynamicArray<assets::SourceRoot> p4RelocatedRoots(memory::pools::Assets::GetInstance());
        p4RelocatedRoots.PushBack({p4RelocatedAssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RelocatedRootSpan(
            p4RelocatedRoots.TypedData(), p4RelocatedRoots.Size());
        assets::SourceDatabase p4RelocatedSources;
        Check(p4RelocatedSources.Rescan(files, p4RelocatedRootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 relocated source catalog rescans from the new root");
        asset_streaming::ProjectAssetPipelineConfig p4RelocatedConfig = p4Config;
        p4RelocatedConfig.build.artifactCacheRoot = p4RelocatedDerivedRoot;
        p4RelocatedConfig.build.dependencyIndexRoot = p4RelocatedIndexRoot;
        p4RelocatedConfig.publication.artifactCacheRoot = p4RelocatedDerivedRoot;
        p4RelocatedConfig.publication.looseRoot = p4RelocatedLooseRoot;
        p4RelocatedConfig.operationJournalPath = p4RelocatedJournal;
        asset_streaming::ProjectAssetPipeline p4Restarted;
        Check(p4Restarted.Initialize(files, p4RelocatedSources, streamer, compilerSpan, importerSpan, p4RelocatedConfig) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 relocated restart restores the current descriptor from relocated roots");
        asset_streaming::ProjectAssetOutputDeliveryStatus p4RelocatedCurrent;
        Check(p4Restarted.GetOutputDeliveryStatus(OutputReference(p4Id), p4RelocatedCurrent) &&
                  p4RelocatedCurrent.publishedPhysicalPath == p4RelocatedGenerationB &&
                  files.FileExist(p4RelocatedGenerationA) && files.FileExist(p4RelocatedGenerationB),
              "E2.5 P5 relocation rewrites current physical ownership to the new loose root");
        Check(p4Restarted.GetLooseMaintenanceStatus().trackedCandidates == 0u,
              "E2.5 P4 process-local retired-candidate state is empty after restart before explicit rediscovery");
        Check(p4Restarted.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Restarted) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart explicit maintenance scans and adopts rediscovered stale generations");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4RestartStatus = p4Restarted.GetLooseMaintenanceStatus();
        Check(p4RestartStatus.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  p4RestartStatus.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::Success &&
                  p4RestartStatus.rediscoveredRecords == 1u && p4RestartStatus.removedRecords == 1u &&
                  p4RestartStatus.stageFiles == 1u && p4RestartStatus.foreignFiles == 4u &&
                  !files.FileExist(p4RelocatedGenerationA) && files.FileExist(p4RelocatedGenerationB) &&
                  !files.FileExist(p4RelocatedStage) && files.FileExist(p4RelocatedForeign) &&
                  files.FileExist(p4RelocatedForeignTemporary) && files.FileExist(p4RelocatedMisplaced) &&
                  files.FileExist(p4RelocatedMalformed) && files.FileExist(p4RelocatedOutside),
              "E2.5 P5 relocated maintenance recovers owned stage, retires stale A through streamer, and preserves foreign files");

        Check(p4Restarted.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 restart-rediscovery second pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p4RelocatedRoot), "E2.5 P5 relocated restart-rediscovery fixture cleanup succeeds");
    }

    // P4 scan bounds fail before retirement begins. A scan that cannot represent
    // the complete loose root must retain every stale/current generation and stay
    // sticky until an explicit retry.
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E25P4ScanLimit");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.5 P4 scan-limit fixture roots are created");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.5 P4 scan-limit identity is generated");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("scan_limit.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-scan-a\n"), Metadata(p4Id)),
              "E2.5 P4 scan-limit generation A source is written");
        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P4 scan-limit source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;
        p4Config.cacheMaintenance.maximumLooseScanEntries = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 scan-limit pipeline initializes");
        asset_streaming::ProjectAssetRequest p4RequestA = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestA.IsValid(), "E2.5 P4 scan-limit generation A request starts");
        p4RequestA.Wait();
        Check(p4RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 scan-limit generation A publishes");
        p4RequestA.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusA;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusA),
              "E2.5 P4 scan-limit captures generation A path");
        const filesystem::AbsolutePath p4GenerationA = p4StatusA.publishedPhysicalPath;

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-scan-b\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 scan-limit source advances to B");
        asset_streaming::ProjectAssetRequest p4RequestB = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestB.IsValid(), "E2.5 P4 scan-limit generation B request starts");
        p4RequestB.Wait();
        Check(p4RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 scan-limit generation B publishes");
        p4RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusB;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusB) &&
                  p4StatusB.publishedPhysicalPath != p4GenerationA && files.FileExist(p4GenerationA) &&
                  files.FileExist(p4StatusB.publishedPhysicalPath),
              "E2.5 P4 scan-limit fixture has stale A plus current B before maintenance");
        const filesystem::AbsolutePath p4GenerationB = p4StatusB.publishedPhysicalPath;

        asset_streaming::ProjectAssetPipelineFailure p4ScanFailure;
        Check(p4Pipeline.RequestLooseMaintenance(&p4ScanFailure) == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.5 P4 tiny scan-bound maintenance dispatches before worker discovers overflow");
        Check(DrainLooseMaintenance(p4Pipeline, &p4ScanFailure) == asset_streaming::ProjectAssetPipelineResult::LooseMaintenanceFailed &&
                  p4ScanFailure.result == asset_streaming::ProjectAssetPipelineResult::LooseMaintenanceFailed &&
                  p4ScanFailure.looseMaintenance == asset_streaming::ProjectAssetLooseMaintenanceResult::LimitExceeded,
              "E2.5 P4 tiny loose scan bound reports explicit LimitExceeded");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4ScanLimited = p4Pipeline.GetLooseMaintenanceStatus();
        Check(p4ScanLimited.state == asset_streaming::ProjectAssetLooseMaintenanceState::Failed &&
                  p4ScanLimited.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::LimitExceeded &&
                  p4ScanLimited.limitExceeded && p4ScanLimited.retryRequired && p4ScanLimited.removedRecords == 0u &&
                  files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB),
              "E2.5 P4 scan overflow is fail-closed with no partial loose deletion");
        Check(p4Pipeline.TickLooseMaintenance(&p4ScanFailure) == asset_streaming::ProjectAssetPipelineResult::LooseMaintenanceFailed &&
                  p4Pipeline.GetLooseMaintenanceStatus().state == asset_streaming::ProjectAssetLooseMaintenanceState::Failed &&
                  files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB),
              "E2.5 P4 failed scan status is sticky and ordinary ticks cannot auto-spin deletion");
        Check(p4Pipeline.RetryLooseMaintenance(&p4ScanFailure) == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Pipeline, &p4ScanFailure) == asset_streaming::ProjectAssetPipelineResult::LooseMaintenanceFailed &&
                  p4Pipeline.GetLooseMaintenanceStatus().lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::LimitExceeded &&
                  p4Pipeline.GetLooseMaintenanceStatus().removedRecords == 0u && files.FileExist(p4GenerationA) &&
                  files.FileExist(p4GenerationB),
              "E2.5 P4 explicit retry under the same tiny scan bound deterministically fails closed again");
        Check(p4Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainVddcMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 VDDC maintenance remains independently runnable after loose-domain overflow");
        const asset_streaming::ProjectAssetVddcMaintenanceStatus p5HealthyVddc = p4Pipeline.GetVddcMaintenanceStatus();
        Check(p5HealthyVddc.state == asset_streaming::ProjectAssetVddcMaintenanceState::Complete &&
                  p5HealthyVddc.budgetSatisfied && !p5HealthyVddc.limitExceeded &&
                  p4Pipeline.GetLooseMaintenanceStatus().state == asset_streaming::ProjectAssetLooseMaintenanceState::Failed &&
                  p4Pipeline.GetLooseMaintenanceStatus().limitExceeded,
              "E2.5 P5 loose domain can remain independently over its limit while VDDC is below budget and healthy");

        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 scan-limit pipeline shuts down cleanly");
        asset_streaming::ProjectAssetPipelineConfig p4RelaxedConfig = p4Config;
        p4RelaxedConfig.cacheMaintenance.maximumLooseScanEntries = 64u;
        asset_streaming::ProjectAssetPipeline p4Recovered;
        Check(p4Recovered.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4RelaxedConfig) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Recovered.GetLooseMaintenanceStatus().trackedCandidates == 0u,
              "E2.5 P5 relaxed restart begins without trusting lost process-local loose candidates");
        Check(p4Recovered.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Recovered) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 relaxed retry performs authoritative loose-root rediscovery");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4RecoveredStatus = p4Recovered.GetLooseMaintenanceStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4RecoveredCurrent;
        Check(p4RecoveredStatus.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  p4RecoveredStatus.rediscoveredRecords == 1u && p4RecoveredStatus.removedRecords == 1u &&
                  p4RecoveredStatus.trackedCandidates == 0u &&
                  p4Recovered.GetOutputDeliveryStatus(OutputReference(p4Id), p4RecoveredCurrent) &&
                  p4RecoveredCurrent.publishedPhysicalPath == p4GenerationB &&
                  !files.FileExist(p4GenerationA) && files.FileExist(p4GenerationB),
              "E2.5 P5 relaxed recovery rediscovers stale A before retiring it and preserves current B");
        Check(p4Recovered.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 relaxed overflow-recovery pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p4Root), "E2.5 P4 scan-limit fixture cleanup succeeds");
    }

    // P4 delete work is also bounded independently of scanning. A one-delete
    // batch removes exactly one stale generation, reports deferred retry work,
    // and leaves the current descriptor untouched until an explicit retry drains
    // the remaining stale generation.
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E25P4DeleteBatch");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.5 P4 delete-batch fixture roots are created");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.5 P4 delete-batch identity is generated");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("delete_batch.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-delete-a\n"), Metadata(p4Id)),
              "E2.5 P4 delete-batch generation A source is written");
        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P4 delete-batch source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;
        p4Config.cacheMaintenance.maximumLooseDeletesPerBatch = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch pipeline initializes");
        asset_streaming::ProjectAssetRequest p4RequestA = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestA.IsValid(), "E2.5 P4 delete-batch generation A request starts");
        p4RequestA.Wait();
        Check(p4RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch generation A publishes");
        p4RequestA.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusA;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusA),
              "E2.5 P4 delete-batch captures generation A path");
        const filesystem::AbsolutePath p4GenerationA = p4StatusA.publishedPhysicalPath;

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-delete-b\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch source advances to B");
        asset_streaming::ProjectAssetRequest p4RequestB = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestB.IsValid(), "E2.5 P4 delete-batch generation B request starts");
        p4RequestB.Wait();
        Check(p4RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch generation B publishes");
        p4RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusB;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusB),
              "E2.5 P4 delete-batch captures generation B path");
        const filesystem::AbsolutePath p4GenerationB = p4StatusB.publishedPhysicalPath;

        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-delete-c\n"), Metadata(p4Id)) &&
                  p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch source advances to C");
        asset_streaming::ProjectAssetRequest p4RequestC = p4Pipeline.Request(OutputReference(p4Id));
        Check(p4RequestC.IsValid(), "E2.5 P4 delete-batch generation C request starts");
        p4RequestC.Wait();
        Check(p4RequestC.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch generation C publishes and leaves two stale candidates");
        p4RequestC.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p4StatusC;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4StatusC) &&
                  p4Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 2u && files.FileExist(p4GenerationA) &&
                  files.FileExist(p4GenerationB) && files.FileExist(p4StatusC.publishedPhysicalPath),
              "E2.5 P4 delete-batch fixture owns two stale generations plus current C");
        const filesystem::AbsolutePath p4GenerationC = p4StatusC.publishedPhysicalPath;

        Check(p4Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 one-delete maintenance batch runs to a deferred terminal state");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4FirstBatch = p4Pipeline.GetLooseMaintenanceStatus();
        const bool p4AExistsAfterFirst = files.FileExist(p4GenerationA);
        const bool p4BExistsAfterFirst = files.FileExist(p4GenerationB);
        Check(p4FirstBatch.state == asset_streaming::ProjectAssetLooseMaintenanceState::Deferred &&
                  p4FirstBatch.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::Deferred &&
                  p4FirstBatch.deleteBatchLimitReached && p4FirstBatch.retryRequired && p4FirstBatch.removedRecords == 1u &&
                  p4FirstBatch.trackedCandidates == 1u && p4AExistsAfterFirst != p4BExistsAfterFirst && files.FileExist(p4GenerationC),
              "E2.5 P4 delete-batch bound removes exactly one stale generation and defers the other");

        Check(p4Pipeline.RetryLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 explicit retry drains the remaining stale generation");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p4SecondBatch = p4Pipeline.GetLooseMaintenanceStatus();
        Check(p4SecondBatch.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  p4SecondBatch.lastResult == asset_streaming::ProjectAssetLooseMaintenanceResult::Success &&
                  p4SecondBatch.removedRecords == 1u && p4SecondBatch.trackedCandidates == 0u && !p4SecondBatch.retryRequired &&
                  !files.FileExist(p4GenerationA) && !files.FileExist(p4GenerationB) && files.FileExist(p4GenerationC),
              "E2.5 P4 explicit delete-batch retry removes the final stale generation and preserves current C");

        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 delete-batch pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p4Root), "E2.5 P4 delete-batch fixture cleanup succeeds");
    }

    // P5 loose-maintenance lifecycle: incompatible build work refuses admission;
    // an accepted maintenance request owns admission exactly once, exposes
    // cancellation, and remains explicitly retryable. Shutdown drains an active
    // maintenance owner and reinitialization starts from clean session state.
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E25P5LooseLifecycle");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.5 P5 loose-lifecycle fixture roots are created");

        assets::AssetId p5RetiredId;
        assets::AssetId p5BusyId;
        Check(assets::GenerateAssetId(p5RetiredId) && assets::GenerateAssetId(p5BusyId),
              "E2.5 P5 loose-lifecycle identities are generated");
        const filesystem::AbsolutePath p5RetiredPath = p5AssetsRoot.AddFilePath("retired.tasset");
        const filesystem::AbsolutePath p5BusyPath = p5AssetsRoot.AddFilePath("busy.tasset");
        Check(WriteTextAsset(files, p5RetiredPath, containers::StringView("p5-lifecycle-a\n"), Metadata(p5RetiredId)) &&
                  WriteTextAsset(files, p5BusyPath, containers::StringView("p5-lifecycle-busy\n"), Metadata(p5BusyId)),
              "E2.5 P5 loose-lifecycle sources are written");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 loose-lifecycle source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = false;
        block.block = false;
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);

        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 loose-lifecycle pipeline initializes");
        asset_streaming::ProjectAssetRequest p5RequestA = p5Pipeline.Request(OutputReference(p5RetiredId));
        Check(p5RequestA.IsValid(), "E2.5 P5 loose-lifecycle generation A request starts");
        p5RequestA.Wait();
        Check(p5RequestA.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 loose-lifecycle generation A publishes");
        p5RequestA.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5StatusA;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5RetiredId), p5StatusA),
              "E2.5 P5 loose-lifecycle captures generation A");
        const filesystem::AbsolutePath p5GenerationA = p5StatusA.publishedPhysicalPath;

        Check(WriteTextAsset(files, p5RetiredPath, containers::StringView("p5-lifecycle-b\n"), Metadata(p5RetiredId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 loose-lifecycle source advances to B");
        asset_streaming::ProjectAssetRequest p5RequestB = p5Pipeline.Request(OutputReference(p5RetiredId));
        Check(p5RequestB.IsValid(), "E2.5 P5 loose-lifecycle generation B request starts");
        p5RequestB.Wait();
        Check(p5RequestB.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 loose-lifecycle generation B publishes");
        p5RequestB.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5StatusB;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5RetiredId), p5StatusB) &&
                  p5StatusB.publishedPhysicalPath != p5GenerationA &&
                  p5Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 1u,
              "E2.5 P5 loose-lifecycle owns one stale generation before maintenance");

        streaming::ResourceSource p5OldSource;
        Check(p5OldSource.OpenLoose(p5GenerationA) == streaming::ResourceSourceResult::Success,
              "E2.5 P5 loose-lifecycle pins stale A across cancellation");

        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        asset_streaming::ProjectAssetRequest p5BusyRequest = p5Pipeline.Request(OutputReference(p5BusyId));
        Check(p5BusyRequest.IsValid(), "E2.5 P5 active-build refusal fixture request starts");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue() && !p5BusyRequest.HasFinished(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue() &&
                  p5Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P5 live build ownership refuses loose-maintenance admission");
        Check(p5BusyRequest.Cancel(), "E2.5 P5 active-build refusal request cancellation is accepted");
        p5BusyRequest.Wait();
        p5BusyRequest.Reset();
        block.block = false;

        Check(p5Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  p5Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P5 accepted loose maintenance rejects a repeated request while admission is owned");
        Check(p5Pipeline.CancelLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetLooseMaintenanceStatus().cancellationRequested,
              "E2.5 P5 active loose-maintenance cancellation is accepted and owner-visible");
        Check(DrainLooseMaintenance(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 cancelled/deferred loose maintenance joins through the owner tick");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p5Cancelled = p5Pipeline.GetLooseMaintenanceStatus();
        Check((p5Cancelled.state == asset_streaming::ProjectAssetLooseMaintenanceState::Cancelled ||
               p5Cancelled.state == asset_streaming::ProjectAssetLooseMaintenanceState::Deferred) &&
                  p5Cancelled.retryRequired && files.FileExist(p5GenerationA),
              "E2.5 P5 cancellation cannot bypass the pinned stale-generation owner and remains retryable");

        p5OldSource.Close();
        Check(p5Pipeline.RetryLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 explicit loose-maintenance retry runs after the stale source releases");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p5Retried = p5Pipeline.GetLooseMaintenanceStatus();
        Check(p5Retried.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  p5Retried.trackedCandidates == 0u && p5Retried.removedRecords == 1u &&
                  !files.FileExist(p5GenerationA) && files.FileExist(p5StatusB.publishedPhysicalPath),
              "E2.5 P5 retry retires stale A through streamer and preserves current B");

        Check(p5Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 shutdown drains active loose maintenance before tearing down streamer ownership");
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetLooseMaintenanceStatus().state == asset_streaming::ProjectAssetLooseMaintenanceState::Idle &&
                  !p5Pipeline.GetLooseMaintenanceStatus().retryRequired,
              "E2.5 P5 reinitialize starts with clean session-local loose-maintenance state");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 reinitialized loose-lifecycle pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.5 P5 loose-lifecycle fixture cleanup succeeds");
    }

    // P5 VDDC-maintenance lifecycle. A current protected record keeps the byte
    // budget deterministically unsatisfied if cancellation loses the worker race,
    // so either terminal outcome remains retryable without timing assumptions.
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E25P5VddcLifecycle");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.5 P5 VDDC-lifecycle fixture roots are created");

        assets::AssetId p5Id;
        Check(assets::GenerateAssetId(p5Id), "E2.5 P5 VDDC-lifecycle identity is generated");
        const filesystem::AbsolutePath p5Path = p5AssetsRoot.AddFilePath("vddc_lifecycle.tasset");
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-vddc-lifecycle\n"), Metadata(p5Id)),
              "E2.5 P5 VDDC-lifecycle source is written");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 VDDC-lifecycle source catalog rescans");

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = false;
        p5Config.cacheMaintenance.maximumVddcBytes = 1u;

        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 VDDC-lifecycle pipeline initializes");
        asset_streaming::ProjectAssetRequest p5Request = p5Pipeline.Request(OutputReference(p5Id));
        Check(p5Request.IsValid(), "E2.5 P5 VDDC-lifecycle current build starts");
        p5Request.Wait();
        Check(p5Request.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 VDDC-lifecycle current build publishes");
        p5Request.Reset();

        Check(p5Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  p5Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P5 active VDDC maintenance owns admission and rejects a repeated request");
        Check(p5Pipeline.CancelVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetVddcMaintenanceStatus().cancellationRequested,
              "E2.5 P5 active VDDC maintenance accepts cancellation through the real owner seam");
        Check(DrainVddcMaintenance(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 cancelled/protected-over-budget VDDC maintenance joins through the owner tick");
        const asset_streaming::ProjectAssetVddcMaintenanceStatus p5Cancelled = p5Pipeline.GetVddcMaintenanceStatus();
        Check((p5Cancelled.state == asset_streaming::ProjectAssetVddcMaintenanceState::Cancelled ||
               p5Cancelled.state == asset_streaming::ProjectAssetVddcMaintenanceState::Deferred) &&
                  p5Cancelled.retryRequired,
              "E2.5 P5 VDDC cancellation remains explicitly retryable even if the protected-budget result wins the worker race");

        Check(p5Pipeline.RetryVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainVddcMaintenance(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 VDDC maintenance retries through the same owner seam");
        const asset_streaming::ProjectAssetVddcMaintenanceStatus p5Retried = p5Pipeline.GetVddcMaintenanceStatus();
        Check(p5Pipeline.RequestLooseMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainLooseMaintenance(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 loose maintenance remains independently runnable while VDDC is protected over budget");
        const asset_streaming::ProjectAssetLooseMaintenanceStatus p5LooseHealthy = p5Pipeline.GetLooseMaintenanceStatus();
        Check(p5Retried.state == asset_streaming::ProjectAssetVddcMaintenanceState::Deferred &&
                  p5Retried.protectedOverBudget && !p5Retried.budgetSatisfied && p5Retried.retryRequired &&
                  p5LooseHealthy.state == asset_streaming::ProjectAssetLooseMaintenanceState::Complete &&
                  !p5LooseHealthy.limitExceeded &&
                  p5LooseHealthy.trackedCandidateBytes < p5Config.cacheMaintenance.maximumRetiredLooseBytes,
              "E2.5 P5 VDDC can remain independently over budget while the loose domain stays below its limit");

        Check(p5Pipeline.RetryVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 shutdown drains active retried VDDC maintenance");
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle &&
                  !p5Pipeline.GetVddcMaintenanceStatus().retryRequired,
              "E2.5 P5 VDDC maintenance state is session-local across clean reinitialization");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 reinitialized VDDC-lifecycle pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.5 P5 VDDC-lifecycle fixture cleanup succeeds");
    }

    // E2.3 P1: bounded automatic-recook state is opt-in and must not change
    // existing manual/source admission while it is disabled or idle.
    Check(config.automaticRecook.IsValid(), "E2.3 P1 default automatic-recook bounds are valid");
    asset_streaming::ProjectAssetAutomaticRecookConfig invalidAutomaticRecook = config.automaticRecook;
    invalidAutomaticRecook.maximumPendingAssets = 0u;
    Check(!invalidAutomaticRecook.IsValid(), "E2.3 P1 rejects a zero automatic-recook pending bound");
    invalidAutomaticRecook = config.automaticRecook;
    invalidAutomaticRecook.maximumRetryEntries = invalidAutomaticRecook.maximumPendingAssets - 1u;
    Check(!invalidAutomaticRecook.IsValid(), "E2.3 P5 retry retention bound must cover any admitted pending asset set");
    const asset_streaming::ProjectAssetAutomaticRecookStatus initialAutomaticRecook = pipeline.GetAutomaticRecookStatus();
    Check(!pipeline.IsAutomaticRecookEnabled() &&
              initialAutomaticRecook.state == asset_streaming::ProjectAssetAutomaticRecookState::Disabled &&
              initialAutomaticRecook.pendingAssets == 0u && initialAutomaticRecook.pendingInputs == 0u &&
              initialAutomaticRecook.pendingOutputs == 0u && initialAutomaticRecook.retryEntries == 0u &&
              !initialAutomaticRecook.retryRequested && !initialAutomaticRecook.cancellationRequested &&
              !initialAutomaticRecook.recoveryRequired,
          "E2.3 P1 automatic recook defaults disabled with empty bounded state");
    Check(pipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
              pipeline.IsAutomaticRecookEnabled() && pipeline.CanMutateSources() &&
              pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
          "E2.3 P1 automatic recook enables idle without stealing manual admission");
    Check(pipeline.CancelAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::InvalidState,
          "E2.3 P1 cancellation requires an owned automatic batch");
    Check(pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::InvalidState,
          "E2.3 P1 retry requires failed or recovery-required work");
    Check(pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
              !pipeline.IsAutomaticRecookEnabled() && pipeline.CanMutateSources() &&
              pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Disabled,
          "E2.3 P1 automatic recook disables cleanly and preserves manual admission");

    // P4 initialization is itself an authoritative acceptance boundary. With no
    // durable snapshot, the current catalog bootstraps as generation-one New and
    // is durably committed before the in-memory source/snapshot swap.
    Check(config.changeState.IsValid(), "E2.2 P1 default change-state limits are valid");
    asset_streaming::ProjectAssetChangeStateLimits invalidChangeStateLimits = config.changeState;
    invalidChangeStateLimits.maximumAssets = 0u;
    Check(!invalidChangeStateLimits.IsValid(), "E2.2 P1 rejects a zero asset-state bound");
    const asset_streaming::ProjectAssetChangeReason composedReasons =
        asset_streaming::ProjectAssetChangeReason::Moved | asset_streaming::ProjectAssetChangeReason::ContentChanged;
    Check(asset_streaming::HasChangeReason(composedReasons, asset_streaming::ProjectAssetChangeReason::Moved) &&
              asset_streaming::HasChangeReason(composedReasons, asset_streaming::ProjectAssetChangeReason::ContentChanged) &&
              !asset_streaming::HasChangeReason(composedReasons, asset_streaming::ProjectAssetChangeReason::SettingsChanged),
          "E2.2 P1 change reasons compose as flags");
    Check(asset_streaming::HasSourceDiagnosticReason(asset_streaming::ProjectAssetSourceDiagnosticReason::AmbiguousMove,
                                                     asset_streaming::ProjectAssetSourceDiagnosticReason::AmbiguousMove),
          "E2.2 P1 can report ambiguous move evidence without inventing AssetId ownership");
    const asset_streaming::ProjectAssetState* const initializedA = pipeline.LookupAcceptedAsset(idA);
    Check(initialRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::NotFound &&
              pipeline.GetAcceptedAssetGeneration() == 1u && initializedA != nullptr &&
              initializedA->hasSourceContentFingerprint && initializedA->sourceContentFingerprint == crypto::Sha256("", 0u) &&
              HasAcceptedChange(pipeline, idA, asset_streaming::ProjectAssetChangeReason::New) &&
              !pipeline.GetAssetInvalidationSeeds().Empty() && !pipeline.GetAssetInvalidationOutputs().Empty(),
          "E2.2 P4 missing snapshot bootstraps the current catalog as durable generation-one New state");

    Check(sources.LookupAsset(idA) != nullptr && sources.LookupAsset(idReferrer) != nullptr, "catalog exposes required assets");
    const assets::SourceRecord* const referrerRecord = sources.LookupAsset(idReferrer);
    Check(referrerRecord != nullptr && referrerRecord->referenceScanState == assets::SourceReferenceScanState::Scanned,
          "authored reference scan produced the referrer link");

    // E2.2 P2 is deliberately exercised below the pipeline acceptance surface:
    // it compares one complete candidate into a proposed immutable snapshot but
    // does not mutate the live SourceDatabase or publish the result (P3).
    {
        using ComparisonResult = asset_streaming::ProjectAssetComparisonResult;
        const auto evidence = MakePreparedEvidence(sources);
        asset_streaming::ProjectAssetAcceptedBaseline empty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline initial(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(empty, sources, {evidence.TypedData(), evidence.Size()}, initial) ==
                  ComparisonResult::Success,
              "E2.2 P2 initial candidate comparison succeeds");
        const asset_streaming::ProjectAssetState* const initialA = initial.LookupAsset(idA);
        Check(initial.generation == 1u && initialA != nullptr && initialA->presence == asset_streaming::ProjectAssetPresence::Present &&
                  asset_streaming::HasChangeReason(initial.changes[0].reasons, asset_streaming::ProjectAssetChangeReason::New),
              "E2.2 P2 initial catalog becomes generation-one New state");
        Check(!initial.invalidationSeeds.Empty() && !initial.invalidationOutputs.Empty(),
              "E2.2 P2 initial changes expose distinct source/build and cooked-output invalidation evidence");

        asset_streaming::ProjectAssetAcceptedBaseline noOp(config.changeState);
        const ComparisonResult noOpResult =
            asset_streaming::CompareProjectAssetCandidate(initial, sources, {evidence.TypedData(), evidence.Size()}, noOp);
        Check(noOpResult == ComparisonResult::Success &&
                  noOp.generation == initial.generation && noOp.changes.Empty() && noOp.invalidationSeeds.Empty() &&
                  noOp.invalidationOutputs.Empty() && noOp.LookupAsset(idA) != nullptr &&
                  noOp.LookupAsset(idA)->acceptedGeneration == initialA->acceptedGeneration,
              "E2.2 P2 identical comparison is a true no-op without generation advance or invalidation");

        containers::DynamicArray<asset_streaming::ProjectAssetPreparedSourceEvidence> changedEvidence(memory::pools::Assets::GetInstance());
        for (const auto& item : evidence)
            changedEvidence.PushBack(item);
        u32 rowA = 0xffffffffu;
        const auto sourceRows = sources.GetResources();
        for (u32 index = 0; index < sourceRows.Count(); ++index)
            if (sourceRows[index].metadata.id == idA)
                rowA = index;
        Check(rowA != 0xffffffffu, "E2.2 P2 locates source A evidence row");
        if (rowA != 0xffffffffu)
            changedEvidence[rowA].sourceContentFingerprint = crypto::Sha256("e2.2-p2-content-change", 21u);
        asset_streaming::ProjectAssetAcceptedBaseline changed(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(initial, sources,
                                                            {changedEvidence.TypedData(), changedEvidence.Size()}, changed) ==
                  ComparisonResult::Success,
              "E2.2 P2 content comparison succeeds");
        const asset_streaming::ProjectAssetState* const changedA = changed.LookupAsset(idA);
        bool sawContentChange = false;
        for (const asset_streaming::ProjectAssetChangeRecord& change : changed.changes)
            if (change.asset == idA)
                sawContentChange = asset_streaming::HasChangeReason(change.reasons, asset_streaming::ProjectAssetChangeReason::ContentChanged);
        Check(changed.generation == initial.generation + 1u && changedA != nullptr &&
                  changedA->acceptedGeneration == changed.generation && sawContentChange && !changed.invalidationSeeds.Empty() &&
                  !changed.invalidationOutputs.Empty(),
              "E2.2 P2 content change advances accepted state and emits canonical invalidation evidence");

        assets::SourceDatabase absentCandidate;
        asset_streaming::ProjectAssetAcceptedBaseline missing(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(initial, absentCandidate, {}, missing) == ComparisonResult::Success &&
                  missing.tombstones == initial.assets.Size() && missing.invalidationSeeds.Empty() && !missing.invalidationOutputs.Empty(),
              "E2.2 P2 absent assets become retained tombstones with previous cooked-output evidence");
        const asset_streaming::ProjectAssetState* const missingA = missing.LookupAsset(idA);
        Check(missingA != nullptr && missingA->presence == asset_streaming::ProjectAssetPresence::Missing &&
                  missingA->hasSourceContentFingerprint == initialA->hasSourceContentFingerprint &&
                  missingA->sourceContentFingerprint == initialA->sourceContentFingerprint,
              "E2.2 P2 tombstone preserves previous content evidence");

        asset_streaming::ProjectAssetAcceptedBaseline missingNoOp(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(missing, absentCandidate, {}, missingNoOp) == ComparisonResult::Success &&
                  missingNoOp.generation == missing.generation && missingNoOp.changes.Empty() && missingNoOp.invalidationSeeds.Empty() &&
                  missingNoOp.invalidationOutputs.Empty() && missingNoOp.LookupAsset(idA) != nullptr &&
                  missingNoOp.LookupAsset(idA)->acceptedGeneration == missingA->acceptedGeneration,
              "E2.2 P2 an unchanged tombstone is also a true no-op");

        asset_streaming::ProjectAssetAcceptedBaseline restored(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(missing, sources, {evidence.TypedData(), evidence.Size()}, restored) ==
                  ComparisonResult::Success,
              "E2.2 P2 restored candidate comparison succeeds");
        bool sawRestored = false;
        for (const asset_streaming::ProjectAssetChangeRecord& change : restored.changes)
            if (change.asset == idA)
                sawRestored = asset_streaming::HasChangeReason(change.reasons, asset_streaming::ProjectAssetChangeReason::Restored);
        Check(sawRestored && restored.LookupAsset(idA) != nullptr &&
                  restored.LookupAsset(idA)->presence == asset_streaming::ProjectAssetPresence::Present,
              "E2.2 P2 tombstone reappearance is Restored");

        if (rowA != 0xffffffffu)
        {
            changedEvidence[rowA].hasSourceContentFingerprint = false;
            asset_streaming::ProjectAssetAcceptedBaseline unavailable(config.changeState);
            const ComparisonResult unavailableResult = asset_streaming::CompareProjectAssetCandidate(
                initial, sources, {changedEvidence.TypedData(), changedEvidence.Size()}, unavailable);
            Check(unavailableResult == ComparisonResult::Success &&
                      unavailable.LookupAsset(idA) != nullptr && unavailable.changes.Empty() && unavailable.invalidationSeeds.Empty(),
                  "E2.2 P2 unreadable current content preserves prior accepted asset evidence without fabricating a content change");
            bool sawUnavailable = false;
            for (const asset_streaming::ProjectAssetSourceDiagnostic& diagnostic : unavailable.diagnostics)
                if (diagnostic.hasParsedAssetIdentity && diagnostic.asset == idA)
                    sawUnavailable = asset_streaming::HasSourceDiagnosticReason(
                        diagnostic.reasons, asset_streaming::ProjectAssetSourceDiagnosticReason::SourceContentUnavailable);
            Check(sawUnavailable, "E2.2 P2 unreadable source remains an explicit source-row diagnostic");
        }

        asset_streaming::ProjectAssetChangeStateLimits tinyLimits = config.changeState;
        tinyLimits.maximumAssets = 1u;
        tinyLimits.maximumTombstones = 1u;
        asset_streaming::ProjectAssetAcceptedBaseline tinyPrevious(tinyLimits);
        asset_streaming::ProjectAssetAcceptedBaseline tinyProposed(tinyLimits);
        Check(asset_streaming::CompareProjectAssetCandidate(tinyPrevious, sources, {evidence.TypedData(), evidence.Size()}, tinyProposed) ==
                  ComparisonResult::LimitExceeded &&
                  tinyProposed.generation == 0u && tinyProposed.assets.Empty(),
              "E2.2 P2 bound failure leaves the caller's proposed accepted asset baseline untouched rather than truncating");
    }

    // ---------------------------------------------------------------------
    // E2.2 P3 transactional acceptance. Manual reconciliation prepares a fresh
    // catalog + accepted asset baseline before the owner swaps either one. The same
    // accepted baseline also seeds authored links when a later reference scan
    // cannot produce a new link set.
    // ---------------------------------------------------------------------
    {
        Check(pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P3 manual reconciliation accepts a prepared candidate");
        const u64 initialAcceptedGeneration = pipeline.GetAcceptedAssetGeneration();
        const asset_streaming::ProjectAssetState* const acceptedA = pipeline.LookupAcceptedAsset(idA);
        Check(initialAcceptedGeneration == 1u && acceptedA != nullptr && acceptedA->presence == asset_streaming::ProjectAssetPresence::Present &&
                  acceptedA->hasSourceContentFingerprint && acceptedA->sourceContentFingerprint == crypto::Sha256("", 0u) &&
                  pipeline.GetAcceptedAssetChanges().Empty(),
              "E2.2 P3 keeps the P4 bootstrap generation/fingerprint on an identical manual no-op");
        Check(pipeline.GetAssetInvalidationSeeds().Empty() && pipeline.GetAssetInvalidationOutputs().Empty(),
              "E2.2 P3 no-op reconciliation emits no invalidation evidence");

        Check(pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  pipeline.GetAcceptedAssetGeneration() == initialAcceptedGeneration && pipeline.GetAcceptedAssetChanges().Empty() &&
                  pipeline.GetAssetInvalidationSeeds().Empty() && pipeline.GetAssetInvalidationOutputs().Empty(),
              "E2.2 P3 identical manual reconciliation is a true accepted-state no-op");

        const containers::String malformedReference("not-a-reference\n");
        Check(WriteTextAsset(files, pathReferrer, containers::StringView(malformedReference), Metadata(idReferrer)),
              "E2.2 P3 write a source whose authored-reference scan will fail");
        Check(pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P3 accepts a candidate with a per-source reference-scan failure");
        const auto retainedReferences = sources.GetForwardReferences(idReferrer);
        const asset_streaming::ProjectAssetState* const acceptedReferrer = pipeline.LookupAcceptedAsset(idReferrer);
        Check(retainedReferences.Count() == 1u && retainedReferences[0].to == OutputReference(idA) && acceptedReferrer != nullptr &&
                  acceptedReferrer->referenceScanState == assets::SourceReferenceScanState::ScanFailed &&
                  acceptedReferrer->references.Count() == 1u && acceptedReferrer->references[0].to == OutputReference(idA),
              "E2.2 P3 seeds accepted authored references before scanning so failed-source links survive");

        const containers::String restoredReference = ReferenceLine(OutputReference(idA).GetPath().Id());
        Check(WriteTextAsset(files, pathReferrer, containers::StringView(restoredReference), Metadata(idReferrer)) &&
                  pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P3 restores the referrer source after preservation coverage");

        const u64 generationBeforeFailure = pipeline.GetAcceptedAssetGeneration();
        const u32 acceptedCountBeforeFailure = pipeline.GetAcceptedAssets().Count();
        containers::DynamicArray<assets::SourceRoot> unavailableRoots(memory::pools::Assets::GetInstance());
        unavailableRoots.PushBack({root.AddDirPath("P3UnavailableRoot"), false});
        filesystem::ScanResult p3ScanFailure = filesystem::ScanResult::Success;
        asset_streaming::ProjectAssetPipelineFailure p3Failure;
        Check(pipeline.Rescan({unavailableRoots.TypedData(), unavailableRoots.Size()}, nullptr, &p3ScanFailure, &p3Failure) ==
                  asset_streaming::ProjectAssetPipelineResult::ReconciliationFailed,
              "E2.2 P3 failed candidate preparation is reported before acceptance");
        Check(pipeline.GetAcceptedAssetGeneration() == generationBeforeFailure &&
                  pipeline.GetAcceptedAssets().Count() == acceptedCountBeforeFailure && sources.LookupAsset(idA) != nullptr &&
                  sources.GetRoots().Count() == rootSpan.Count() && sources.GetRoots()[0].directory == rootSpan[0].directory,
              "E2.2 P3 failed manual preparation preserves both live catalog and accepted asset baseline");

        // P4 restart persistence: authored references to a currently missing
        // output are durable accepted history and rebuild the backward index on
        // load, while the pre-restart change/invalidation set is never replayed.
        constexpr resources::ResourceId MissingReferencePath = 0x6f726f7068616e31ull;
        const resources::ResourceReference missingOutput(resources::ResourcePath::FromId(MissingReferencePath), TestOutputType);
        const containers::String missingReference = ReferenceLine(MissingReferencePath);
        Check(WriteTextAsset(files, pathReferrer, containers::StringView(missingReference), Metadata(idReferrer)) &&
                  pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  HasAcceptedChange(pipeline, idReferrer, asset_streaming::ProjectAssetChangeReason::ReferencesChanged),
              "E2.2 P4 establishes durable missing-output authored-reference evidence");
        const u64 cleanRestartGeneration = pipeline.GetAcceptedAssetGeneration();
        Check(pipeline.GetMissingOutputReferencers(missingOutput).Count() == 1u,
              "E2.2 P4 live accepted asset baseline indexes missing-output backward referencers");
        const filesystem::AbsolutePath acceptedSnapshotPath = asset_streaming::ProjectAssetSnapshotPath(indexRoot);
        Check(files.FileExist(acceptedSnapshotPath), "E2.2 P4 VPAS image is durably present beside dependency-index state");

        Check(pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P4 shutdown before clean restart");
        asset_streaming::ProjectAssetRestoreReport cleanRestart;
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config, &cleanRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P4 clean restart initializes from the accepted asset baseline");
        const asset_streaming::ProjectAssetState* const restartedReferrer = pipeline.LookupAcceptedAsset(idReferrer);
        Check(cleanRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  pipeline.GetAcceptedAssetGeneration() == cleanRestartGeneration && pipeline.GetAcceptedAssetChanges().Empty() &&
                  pipeline.GetAssetInvalidationSeeds().Empty() && pipeline.GetAssetInvalidationOutputs().Empty() &&
                  restartedReferrer != nullptr && restartedReferrer->references.Count() == 1u &&
                  restartedReferrer->references[0].to == missingOutput &&
                  pipeline.GetMissingOutputReferencers(missingOutput).Count() == 1u,
              "E2.2 P4 clean restart preserves durable history/backward references without replaying old work");

        // Missing snapshot is an explicit bootstrap, not a failed or partial
        // load. The current catalog is accepted as New and a fresh image is
        // durably written.
        Check(pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success && files.DeleteFile(acceptedSnapshotPath),
              "E2.2 P4 remove snapshot for missing-bootstrap coverage");
        asset_streaming::ProjectAssetRestoreReport missingRestart;
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config, &missingRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  missingRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::NotFound &&
                  pipeline.GetAcceptedAssetGeneration() == 1u && HasAcceptedChange(pipeline, idA, asset_streaming::ProjectAssetChangeReason::New) &&
                  files.FileExist(acceptedSnapshotPath),
              "E2.2 P4 missing snapshot conservatively bootstraps current catalog as New");

        // A payload mutation keeps the header shape but breaks the payload
        // checksum. The loader reports Corrupt, discards the entire image, and
        // rebuilds from current source state rather than trusting a prefix.
        Check(pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P4 shutdown before corrupt restart");
        containers::DynamicArray<u8> corruptSnapshot(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, acceptedSnapshotPath, corruptSnapshot) &&
                  corruptSnapshot.Size() > asset_streaming::ProjectAssetSnapshotHeaderSize,
              "E2.2 P4 read durable image for corruption coverage");
        if (corruptSnapshot.Size() > asset_streaming::ProjectAssetSnapshotHeaderSize)
            corruptSnapshot[corruptSnapshot.Size() - 1u] ^= 0x5au;
        Check(WriteBytes(files, acceptedSnapshotPath, corruptSnapshot.TypedData(), corruptSnapshot.Size()),
              "E2.2 P4 write checksum-corrupt snapshot");
        asset_streaming::ProjectAssetRestoreReport corruptRestart;
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config, &corruptRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  corruptRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Corrupt &&
                  pipeline.GetAcceptedAssetGeneration() == 1u && HasAcceptedChange(pipeline, idA, asset_streaming::ProjectAssetChangeReason::New),
              "E2.2 P4 corrupt snapshot is reported and conservatively rebuilt");

        // The payload checksum deliberately excludes the header so changing the
        // major format version creates a checksum-valid but incompatible image.
        Check(pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P4 shutdown before incompatible restart");
        containers::DynamicArray<u8> incompatibleSnapshot(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, acceptedSnapshotPath, incompatibleSnapshot) && incompatibleSnapshot.Size() >= 6u,
              "E2.2 P4 read durable image for incompatibility coverage");
        if (incompatibleSnapshot.Size() >= 6u)
        {
            incompatibleSnapshot[4] = static_cast<u8>(asset_streaming::ProjectAssetSnapshotMajorVersion + 1u);
            incompatibleSnapshot[5] = 0u;
        }
        Check(WriteBytes(files, acceptedSnapshotPath, incompatibleSnapshot.TypedData(), incompatibleSnapshot.Size()),
              "E2.2 P4 write incompatible snapshot version");
        asset_streaming::ProjectAssetRestoreReport incompatibleRestart;
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config, &incompatibleRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  incompatibleRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Incompatible &&
                  pipeline.GetAcceptedAssetGeneration() == 1u && HasAcceptedChange(pipeline, idA, asset_streaming::ProjectAssetChangeReason::New),
              "E2.2 P4 incompatible snapshot is reported and conservatively rebuilt");

        // Persistence itself is failure-atomic: an over-limit staged image fails
        // before replacement and cannot damage a previously committed target.
        const filesystem::AbsolutePath atomicSnapshotRoot = root.AddDirPath("SnapshotAtomic");
        Check(files.CreatePath(atomicSnapshotRoot), "E2.2 P4 create isolated snapshot-atomicity root");
        const auto persistenceEvidence = MakePreparedEvidence(sources);
        asset_streaming::ProjectAssetAcceptedBaseline persistenceEmpty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline persistenceBaseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(persistenceEmpty, sources,
                                                            {persistenceEvidence.TypedData(), persistenceEvidence.Size()},
                                                            persistenceBaseline) == asset_streaming::ProjectAssetComparisonResult::Success &&
                  asset_streaming::StageProjectAssetAcceptedSnapshot(files, atomicSnapshotRoot, persistenceBaseline) ==
                      asset_streaming::ProjectAssetSnapshotResult::Success &&
                  asset_streaming::CommitStagedProjectAssetAcceptedSnapshot(files, atomicSnapshotRoot) ==
                      asset_streaming::ProjectAssetSnapshotResult::Success,
              "E2.2 P4 commits isolated persistence baseline");
        containers::DynamicArray<u8> atomicBefore(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, asset_streaming::ProjectAssetSnapshotPath(atomicSnapshotRoot), atomicBefore),
              "E2.2 P4 read isolated durable baseline");
        asset_streaming::ProjectAssetChangeStateLimits serializedTiny = config.changeState;
        serializedTiny.maximumSerializedBytes = asset_streaming::ProjectAssetSnapshotHeaderSize;
        asset_streaming::ProjectAssetAcceptedBaseline tinyEmpty(serializedTiny);
        asset_streaming::ProjectAssetAcceptedBaseline tinySerialized(serializedTiny);
        Check(asset_streaming::CompareProjectAssetCandidate(tinyEmpty, sources,
                                                            {persistenceEvidence.TypedData(), persistenceEvidence.Size()}, tinySerialized) ==
                  asset_streaming::ProjectAssetComparisonResult::Success &&
                  asset_streaming::StageProjectAssetAcceptedSnapshot(files, atomicSnapshotRoot, tinySerialized) ==
                      asset_streaming::ProjectAssetSnapshotResult::LimitExceeded,
              "E2.2 P4 maximumSerializedBytes rejects the proposed image before replacement");
        containers::DynamicArray<u8> atomicAfter(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, asset_streaming::ProjectAssetSnapshotPath(atomicSnapshotRoot), atomicAfter) &&
                  SameBytes(atomicBefore, atomicAfter),
              "E2.2 P4 failed persistence staging preserves the previous durable accepted image");

        // A filesystem object at the snapshot path that cannot be opened as a
        // regular file is an I/O failure, not recoverable corruption/missing
        // state. The loader must not delete it while claiming bootstrap.
        const filesystem::AbsolutePath ioSnapshotRoot = root.AddDirPath("SnapshotIoFailure");
        const filesystem::AbsolutePath ioSnapshotDirectory = ioSnapshotRoot.AddDirPath(asset_streaming::ProjectAssetSnapshotFileName);
        Check(files.CreatePath(ioSnapshotDirectory), "E2.2 P4 create deterministic snapshot I/O failure fixture");
        asset_streaming::ProjectAssetAcceptedBaseline ioDestination(config.changeState);
        Check(asset_streaming::LoadProjectAssetAcceptedSnapshot(files, ioSnapshotRoot, config.changeState, ioDestination) ==
                  asset_streaming::ProjectAssetSnapshotResult::IoFailure &&
                  files.FileExist(asset_streaming::ProjectAssetSnapshotPath(ioSnapshotRoot)),
              "E2.2 P4 true snapshot filesystem failure is reported as I/O and never discarded as recovery");
        Check(DeleteDirectoryTree(files, ioSnapshotDirectory),
              "E2.2 P4 remove directory-shaped snapshot I/O failure fixture");
        Check(DeleteDirectoryTree(files, ioSnapshotRoot), "E2.2 P4 remove isolated snapshot I/O failure root");

        // Restore the primary authored reference expected by the E1F operation
        // tests that follow.
        const containers::String primaryReference = ReferenceLine(OutputReference(idA).GetPath().Id());
        const u64 beforeReferenceOnly = pipeline.GetAcceptedAssetGeneration();
        Check(WriteTextAsset(files, pathReferrer, containers::StringView(primaryReference), Metadata(idReferrer)) &&
                  pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  pipeline.GetAcceptedAssetGeneration() == beforeReferenceOnly + 1u &&
                  HasAcceptedChange(pipeline, idReferrer, asset_streaming::ProjectAssetChangeReason::ReferencesChanged),
              "E2.2 P4 restored authored reference is accepted after recovery tests");
    }

    // ---------------------------------------------------------------------
    // E2.2 P5 deterministic comparison exit matrix. These fixtures exercise
    // pure P2 classification with a stable synthetic content fingerprint so
    // metadata/location/reference reasons can be asserted independently.
    // ---------------------------------------------------------------------
    {
        using ComparisonResult = asset_streaming::ProjectAssetComparisonResult;
        using Reason = asset_streaming::ProjectAssetChangeReason;

        const filesystem::AbsolutePath matrixRoot = root.AddDirPath("E22Matrix");
        Check(files.CreatePath(matrixRoot), "E2.2 P5 create deterministic matrix root");
        assets::AssetId matrixId;
        Check(assets::GenerateAssetId(matrixId), "E2.2 P5 generate matrix identity");
        const filesystem::AbsolutePath matrixPath = matrixRoot.AddFilePath("matrix.tasset");
        containers::DynamicArray<assets::SourceRoot> matrixRoots(memory::pools::Assets::GetInstance());
        matrixRoots.PushBack({matrixRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> matrixRootSpan(matrixRoots.TypedData(), matrixRoots.Size());
        const crypto::Digest256 stableContent = crypto::Sha256("e2.2-p5-stable", 15u);
        const crypto::Digest256 changedContent = crypto::Sha256("e2.2-p5-changed", 16u);

        auto prepare = [&](assets::SourceDatabase& database, const assets::AssetMetadata& metadata,
                           const filesystem::AbsolutePath& path = filesystem::AbsolutePath()) noexcept -> bool
        {
            const filesystem::AbsolutePath sourcePath = path.Empty() ? matrixPath : path;
            return WriteTextAsset(files, sourcePath, containers::StringView("matrix\n"), metadata) &&
                   database.Rescan(files, matrixRootSpan) == assets::SourceDatabaseResult::Success &&
                   database.ClassifySources(importerSpan);
        };

        assets::AssetMetadata baselineMetadata = Metadata(matrixId);
        assets::SourceDatabase baselineDatabase;
        Check(prepare(baselineDatabase, baselineMetadata), "E2.2 P5 prepare baseline candidate");
        const auto baselineEvidence = MakeConstantEvidence(baselineDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline empty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline baseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(empty, baselineDatabase,
                                                            {baselineEvidence.TypedData(), baselineEvidence.Size()}, baseline) ==
                  ComparisonResult::Success,
              "E2.2 P5 accept deterministic baseline");

        // Settings bytes change with the settings schema version held constant.
        assets::AssetMetadata settingsBytes = Metadata(matrixId);
        settingsBytes.settings[0] = 9u;
        assets::SourceDatabase settingsBytesDatabase;
        Check(prepare(settingsBytesDatabase, settingsBytes), "E2.2 P5 prepare settings-byte variant");
        const auto settingsBytesEvidence = MakeConstantEvidence(settingsBytesDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline settingsBytesChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, settingsBytesDatabase, {settingsBytesEvidence.TypedData(), settingsBytesEvidence.Size()}, settingsBytesChanged) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare settings-byte variant");
        const auto* settingsBytesChange = FindChange(settingsBytesChanged, matrixId);
        Check(settingsBytesChange != nullptr && HasChangeReason(settingsBytesChange->reasons, Reason::SettingsChanged) &&
                  !HasChangeReason(settingsBytesChange->reasons, Reason::ContentChanged),
              "E2.2 P5 settings-byte change is SettingsChanged only, not content drift");

        // Settings version is independently authoritative even when settings
        // bytes are unchanged.
        assets::AssetMetadata settingsVersion = Metadata(matrixId);
        settingsVersion.settingsVersion = 2u;
        assets::SourceDatabase settingsVersionDatabase;
        Check(prepare(settingsVersionDatabase, settingsVersion), "E2.2 P5 prepare settings-version variant");
        const auto settingsVersionEvidence = MakeConstantEvidence(settingsVersionDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline settingsVersionChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, settingsVersionDatabase, {settingsVersionEvidence.TypedData(), settingsVersionEvidence.Size()},
                  settingsVersionChanged) == ComparisonResult::Success,
              "E2.2 P5 compare settings-version variant");
        const auto* settingsVersionChange = FindChange(settingsVersionChanged, matrixId);
        Check(settingsVersionChange != nullptr && HasChangeReason(settingsVersionChange->reasons, Reason::SettingsChanged),
              "E2.2 P5 settings-version change is authoritative SettingsChanged evidence");

        // Authored importer version change makes the source unsupported against
        // the still-available v1 importer; restoring it is Supported recovery.
        assets::AssetMetadata authoredVersion = Metadata(matrixId);
        authoredVersion.importerVersion = TestImporterVersion + 1u;
        assets::SourceDatabase authoredVersionDatabase;
        Check(prepare(authoredVersionDatabase, authoredVersion), "E2.2 P5 prepare authored importer-version variant");
        const auto authoredVersionEvidence = MakeConstantEvidence(authoredVersionDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline authoredUnsupported(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, authoredVersionDatabase, {authoredVersionEvidence.TypedData(), authoredVersionEvidence.Size()},
                  authoredUnsupported) == ComparisonResult::Success,
              "E2.2 P5 compare authored importer-version variant");
        const auto* authoredUnsupportedChange = FindChange(authoredUnsupported, matrixId);
        Check(authoredUnsupportedChange != nullptr &&
                  HasChangeReason(authoredUnsupportedChange->reasons, Reason::ImporterVersionChanged) &&
                  HasChangeReason(authoredUnsupportedChange->reasons, Reason::Unsupported),
              "E2.2 P5 authored importer-version drift is version change plus supported-to-unsupported");
        asset_streaming::ProjectAssetAcceptedBaseline authoredRecovered(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  authoredUnsupported, baselineDatabase, {baselineEvidence.TypedData(), baselineEvidence.Size()}, authoredRecovered) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare authored importer-version recovery");
        const auto* authoredRecoveredChange = FindChange(authoredRecovered, matrixId);
        Check(authoredRecoveredChange != nullptr &&
                  HasChangeReason(authoredRecoveredChange->reasons, Reason::ImporterVersionChanged) &&
                  HasChangeReason(authoredRecoveredChange->reasons, Reason::Supported),
              "E2.2 P5 authored importer-version recovery is unsupported-to-supported");

        // Available importer version can drift while authored bytes remain v1.
        assets::CompilerDescriptor compilerV2 = compiler;
        compilerV2.version = TestImporterVersion + 1u;
        assets::SourceImporterDescriptor importerV2 =
            assets::MakeDirectSourceImporterDescriptor(compilerV2, &RecognizeSource, nullptr, &RewriteSourceReferences,
                                                       &assets::RelocateSourceUnchanged);
        importerV2.scanSourceReferences = &ScanSourceReferences;
        containers::DynamicArray<assets::SourceImporterDescriptor> importersV2(memory::pools::Assets::GetInstance());
        importersV2.PushBack(importerV2);
        assets::SourceDatabase availableVersionDatabase;
        Check(WriteTextAsset(files, matrixPath, containers::StringView("matrix\n"), Metadata(matrixId)) &&
                  availableVersionDatabase.Rescan(files, matrixRootSpan) == assets::SourceDatabaseResult::Success &&
                  availableVersionDatabase.ClassifySources({importersV2.TypedData(), importersV2.Size()}),
              "E2.2 P5 prepare available importer-version variant");
        const auto availableVersionEvidence = MakeConstantEvidence(availableVersionDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline availableUnsupported(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, availableVersionDatabase, {availableVersionEvidence.TypedData(), availableVersionEvidence.Size()},
                  availableUnsupported) == ComparisonResult::Success,
              "E2.2 P5 compare available importer-version variant");
        const auto* availableUnsupportedChange = FindChange(availableUnsupported, matrixId);
        Check(availableUnsupportedChange != nullptr &&
                  HasChangeReason(availableUnsupportedChange->reasons, Reason::ImporterVersionChanged) &&
                  HasChangeReason(availableUnsupportedChange->reasons, Reason::Unsupported),
              "E2.2 P5 available importer-version drift is version change plus supported-to-unsupported");
        asset_streaming::ProjectAssetAcceptedBaseline availableRecovered(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  availableUnsupported, baselineDatabase, {baselineEvidence.TypedData(), baselineEvidence.Size()}, availableRecovered) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare available importer-version recovery");
        const auto* availableRecoveredChange = FindChange(availableRecovered, matrixId);
        Check(availableRecoveredChange != nullptr && HasChangeReason(availableRecoveredChange->reasons, Reason::Supported),
              "E2.2 P5 available importer-version recovery is unsupported-to-supported");

        // Output set changes retain exact previous/current evidence and cooked
        // invalidation is a canonical union, so the shared `main` output appears
        // once rather than once per side.
        assets::AssetMetadata outputSet = Metadata(matrixId);
        assets::AssetOutput secondary;
        secondary.key = containers::String("secondary");
        secondary.type = TestOutputType;
        outputSet.outputs.PushBack(secondary);
        assets::SourceDatabase outputSetDatabase;
        Check(prepare(outputSetDatabase, outputSet), "E2.2 P5 prepare output-set variant");
        const auto outputSetEvidence = MakeConstantEvidence(outputSetDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline outputSetChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, outputSetDatabase, {outputSetEvidence.TypedData(), outputSetEvidence.Size()}, outputSetChanged) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare output-set variant");
        const auto* outputSetChange = FindChange(outputSetChanged, matrixId);
        Check(outputSetChange != nullptr && HasChangeReason(outputSetChange->reasons, Reason::OutputsChanged) &&
                  outputSetChange->previous.outputs.Count() == 1u && outputSetChange->current.outputs.Count() == 2u &&
                  outputSetChanged.invalidationOutputs.Size() == 2u,
              "E2.2 P5 output-set change keeps previous/current evidence and deduplicates invalidation union");

        assets::AssetMetadata outputType = Metadata(matrixId);
        outputType.outputs[0].type = TestOutputType + 1u;
        assets::SourceDatabase outputTypeDatabase;
        Check(prepare(outputTypeDatabase, outputType), "E2.2 P5 prepare output-type variant");
        const auto outputTypeEvidence = MakeConstantEvidence(outputTypeDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline outputTypeChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, outputTypeDatabase, {outputTypeEvidence.TypedData(), outputTypeEvidence.Size()}, outputTypeChanged) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare output-type variant");
        const auto* outputTypeChange = FindChange(outputTypeChanged, matrixId);
        Check(outputTypeChange != nullptr && HasChangeReason(outputTypeChange->reasons, Reason::OutputsChanged),
              "E2.2 P5 output resource-type change is OutputsChanged");

        // Reordered authored outputs are canonicalized by key, so order alone
        // cannot advance accepted generation.
        assets::AssetMetadata reordered = outputSet;
        const assets::AssetOutput first = reordered.outputs[0];
        reordered.outputs[0] = reordered.outputs[1];
        reordered.outputs[1] = first;
        assets::SourceDatabase reorderedDatabase;
        Check(prepare(reorderedDatabase, reordered), "E2.2 P5 prepare reordered outputs");
        const auto reorderedEvidence = MakeConstantEvidence(reorderedDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline reorderedNoOp(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  outputSetChanged, reorderedDatabase, {reorderedEvidence.TypedData(), reorderedEvidence.Size()}, reorderedNoOp) ==
                  ComparisonResult::Success &&
                  reorderedNoOp.generation == outputSetChanged.generation && reorderedNoOp.changes.Empty(),
              "E2.2 P5 output declaration order alone is a true no-op");

        // Authored reference-only mutation with source/reference scan state held
        // constant. SetReferences canonicalizes the link set and leaves rows in
        // NotScanned on both sides.
        assets::SourceDatabase referenceBaselineDatabase;
        Check(prepare(referenceBaselineDatabase, Metadata(matrixId)) &&
                  referenceBaselineDatabase.SetReferences({}) == assets::SourceDatabaseResult::Success,
              "E2.2 P5 prepare reference-only baseline");
        const auto referenceBaselineEvidence = MakeConstantEvidence(referenceBaselineDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline referenceEmpty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline referenceBaseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  referenceEmpty, referenceBaselineDatabase,
                  {referenceBaselineEvidence.TypedData(), referenceBaselineEvidence.Size()}, referenceBaseline) ==
                  ComparisonResult::Success,
              "E2.2 P5 accept reference-only baseline");
        assets::AssetReference authoredReference;
        authoredReference.from = matrixId;
        authoredReference.to = OutputReference(idA);
        authoredReference.kind = resources::DependencyKind::Required;
        authoredReference.location = assets::AssetReferenceLocation::Primary;
        authoredReference.inPlace = false;
        assets::SourceDatabase referenceChangedDatabase;
        Check(prepare(referenceChangedDatabase, Metadata(matrixId)) &&
                  referenceChangedDatabase.SetReferences({&authoredReference, 1u}) == assets::SourceDatabaseResult::Success,
              "E2.2 P5 prepare reference-only changed link set");
        const auto referenceChangedEvidence = MakeConstantEvidence(referenceChangedDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline referenceChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  referenceBaseline, referenceChangedDatabase,
                  {referenceChangedEvidence.TypedData(), referenceChangedEvidence.Size()}, referenceChanged) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare reference-only mutation");
        const auto* referenceChange = FindChange(referenceChanged, matrixId);
        Check(referenceChange != nullptr && HasChangeReason(referenceChange->reasons, Reason::ReferencesChanged) &&
                  !HasChangeReason(referenceChange->reasons, Reason::ContentChanged) &&
                  !HasChangeReason(referenceChange->reasons, Reason::SettingsChanged),
              "E2.2 P5 authored link-set mutation is reference-only");

        // Same AssetId + same source bytes at a new root-relative identity is a
        // valid external move. A different content digest composes Moved with
        // ContentChanged rather than forcing one reason to win.
        const filesystem::AbsolutePath movedMatrixPath = matrixRoot.AddFilePath("matrix_moved.tasset");
        Check(files.DeleteFile(matrixPath) && files.DeleteFile(MetadataPath(matrixPath)) &&
                  WriteTextAsset(files, movedMatrixPath, containers::StringView("matrix\n"), Metadata(matrixId)),
              "E2.2 P5 externally move matrix source pair");
        assets::SourceDatabase movedDatabase;
        Check(movedDatabase.Rescan(files, matrixRootSpan) == assets::SourceDatabaseResult::Success &&
                  movedDatabase.ClassifySources(importerSpan),
              "E2.2 P5 prepare moved candidate");
        const auto movedEvidence = MakeConstantEvidence(movedDatabase, stableContent);
        asset_streaming::ProjectAssetAcceptedBaseline moved(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(baseline, movedDatabase, {movedEvidence.TypedData(), movedEvidence.Size()}, moved) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare valid external move");
        const auto* movedChange = FindChange(moved, matrixId);
        Check(movedChange != nullptr && HasChangeReason(movedChange->reasons, Reason::Moved) &&
                  !HasChangeReason(movedChange->reasons, Reason::ContentChanged),
              "E2.2 P5 stable-content external relocation is Moved without content change");
        const auto movedChangedEvidence = MakeConstantEvidence(movedDatabase, changedContent);
        asset_streaming::ProjectAssetAcceptedBaseline movedAndChanged(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  baseline, movedDatabase, {movedChangedEvidence.TypedData(), movedChangedEvidence.Size()}, movedAndChanged) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare move plus content change");
        const auto* movedAndChangedRecord = FindChange(movedAndChanged, matrixId);
        Check(movedAndChangedRecord != nullptr && HasChangeReason(movedAndChangedRecord->reasons, Reason::Moved) &&
                  HasChangeReason(movedAndChangedRecord->reasons, Reason::ContentChanged),
              "E2.2 P5 relocation plus byte drift composes Moved and ContentChanged");

        // Additional collection bound coverage beyond maximumAssets: a valid
        // two-output candidate cannot be silently truncated to maximumOutputs=1.
        asset_streaming::ProjectAssetChangeStateLimits outputBoundLimits = config.changeState;
        outputBoundLimits.maximumOutputs = 1u;
        asset_streaming::ProjectAssetAcceptedBaseline outputBoundPrevious(outputBoundLimits);
        asset_streaming::ProjectAssetAcceptedBaseline outputBoundProposed(outputBoundLimits);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  outputBoundPrevious, outputSetDatabase, {outputSetEvidence.TypedData(), outputSetEvidence.Size()}, outputBoundProposed) ==
                  ComparisonResult::LimitExceeded && outputBoundProposed.assets.Empty(),
              "E2.2 P5 configured output bound fails atomically instead of truncating accepted state");

        static_cast<void>(DeleteDirectoryTree(files, matrixRoot));
    }

    // Stable root identity participates in move evidence independently from the
    // root-relative identity. Moving the same AssetId from Root A to Root B at
    // the exact same relative path therefore remains a Moved transition even
    // though SourceDatabase intentionally hashes the relative path to the same
    // sourceIdentity in both roots.
    {
        using ComparisonResult = asset_streaming::ProjectAssetComparisonResult;
        using Reason = asset_streaming::ProjectAssetChangeReason;

        const filesystem::AbsolutePath rootA = root.AddDirPath("E22CrossRootA");
        const filesystem::AbsolutePath rootB = root.AddDirPath("E22CrossRootB");
        Check(files.CreatePath(rootA) && files.CreatePath(rootB), "E2.2 P5 create cross-root move fixtures");
        const filesystem::AbsolutePath crossRootPathA = rootA.AddFilePath("same.tasset");
        const filesystem::AbsolutePath crossRootPathB = rootB.AddFilePath("same.tasset");
        assets::AssetId crossRootId;
        Check(assets::GenerateAssetId(crossRootId) &&
                  WriteTextAsset(files, crossRootPathA, containers::StringView("cross-root\n"), Metadata(crossRootId)),
              "E2.2 P5 write cross-root baseline source");

        containers::DynamicArray<assets::SourceRoot> crossRoots(memory::pools::Assets::GetInstance());
        crossRoots.PushBack({rootA, false});
        crossRoots.PushBack({rootB, false});
        const containers::ArraySpan<const assets::SourceRoot> crossRootSpan(crossRoots.TypedData(), crossRoots.Size());
        const crypto::Digest256 fingerprint = crypto::Sha256("cross-root", 10u);

        assets::SourceDatabase baselineDatabase;
        Check(baselineDatabase.Rescan(files, crossRootSpan) == assets::SourceDatabaseResult::Success &&
                  baselineDatabase.ClassifySources(importerSpan),
              "E2.2 P5 prepare cross-root baseline");
        const auto baselineEvidence = MakeConstantEvidence(baselineDatabase, fingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline empty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline baseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(empty, baselineDatabase,
                                                            {baselineEvidence.TypedData(), baselineEvidence.Size()}, baseline) ==
                  ComparisonResult::Success,
              "E2.2 P5 accept cross-root baseline");
        const asset_streaming::ProjectAssetState* const acceptedBefore = baseline.LookupAsset(crossRootId);

        Check(files.DeleteFile(crossRootPathA) && files.DeleteFile(MetadataPath(crossRootPathA)) &&
                  WriteTextAsset(files, crossRootPathB, containers::StringView("cross-root\n"), Metadata(crossRootId)),
              "E2.2 P5 move source to second root at same relative identity");
        assets::SourceDatabase movedDatabase;
        Check(movedDatabase.Rescan(files, crossRootSpan) == assets::SourceDatabaseResult::Success &&
                  movedDatabase.ClassifySources(importerSpan),
              "E2.2 P5 prepare cross-root same-relative candidate");
        const auto movedEvidence = MakeConstantEvidence(movedDatabase, fingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline moved(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(baseline, movedDatabase,
                                                            {movedEvidence.TypedData(), movedEvidence.Size()}, moved) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare cross-root same-relative move");
        const asset_streaming::ProjectAssetState* const acceptedAfter = moved.LookupAsset(crossRootId);
        const auto* move = FindChange(moved, crossRootId);
        Check(acceptedBefore != nullptr && acceptedAfter != nullptr &&
                  acceptedBefore->sourceIdentity == acceptedAfter->sourceIdentity &&
                  acceptedBefore->sourceRoot != acceptedAfter->sourceRoot && acceptedAfter->sourceRoot == rootB &&
                  move != nullptr && HasChangeReason(move->reasons, Reason::Moved) &&
                  !HasChangeReason(move->reasons, Reason::ContentChanged),
              "E2.2 P5 cross-root same-relative relocation uses stable sourceRoot evidence for Moved");

        static_cast<void>(DeleteDirectoryTree(files, rootA));
        static_cast<void>(DeleteDirectoryTree(files, rootB));
    }

    // An orphan .vmeta is not an unidentified row: Rescan reconstructs the
    // source record, the valid unique AssetId remains known, and SourceMissing
    // must transition the accepted asset to a tombstone while preserving prior
    // accepted location/fingerprint/output evidence.
    {
        using ComparisonResult = asset_streaming::ProjectAssetComparisonResult;
        using Reason = asset_streaming::ProjectAssetChangeReason;

        const filesystem::AbsolutePath orphanRoot = root.AddDirPath("E22OrphanSidecar");
        Check(files.CreatePath(orphanRoot), "E2.2 P5 create orphan-sidecar fixture root");
        const filesystem::AbsolutePath sourcePath = orphanRoot.AddFilePath("orphaned.tasset");
        assets::AssetId orphanId;
        Check(assets::GenerateAssetId(orphanId) &&
                  WriteTextAsset(files, sourcePath, containers::StringView("orphan-source\n"), Metadata(orphanId)),
              "E2.2 P5 write orphan-sidecar baseline source");
        containers::DynamicArray<assets::SourceRoot> orphanRoots(memory::pools::Assets::GetInstance());
        orphanRoots.PushBack({orphanRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> orphanRootSpan(orphanRoots.TypedData(), orphanRoots.Size());
        const crypto::Digest256 fingerprint = crypto::Sha256("orphan-source", 13u);

        assets::SourceDatabase baselineDatabase;
        Check(baselineDatabase.Rescan(files, orphanRootSpan) == assets::SourceDatabaseResult::Success &&
                  baselineDatabase.ClassifySources(importerSpan),
              "E2.2 P5 prepare orphan-sidecar baseline");
        const auto baselineEvidence = MakeConstantEvidence(baselineDatabase, fingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline empty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline baseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(empty, baselineDatabase,
                                                            {baselineEvidence.TypedData(), baselineEvidence.Size()}, baseline) ==
                  ComparisonResult::Success,
              "E2.2 P5 accept orphan-sidecar baseline");
        const asset_streaming::ProjectAssetState* const acceptedBefore = baseline.LookupAsset(orphanId);

        Check(files.DeleteFile(sourcePath) && files.FileExist(MetadataPath(sourcePath)),
              "E2.2 P5 delete source while retaining its sidecar");
        assets::SourceDatabase orphanCandidate;
        Check(orphanCandidate.Rescan(files, orphanRootSpan) == assets::SourceDatabaseResult::Success &&
                  orphanCandidate.ClassifySources(importerSpan),
              "E2.2 P5 prepare SourceMissing orphan-sidecar candidate");
        const assets::SourceRecord* const orphanRow = orphanCandidate.LookupAsset(orphanId);
        Check(orphanRow != nullptr && (orphanRow->issues & assets::SourceMissing) != 0u,
              "E2.2 P5 orphan sidecar retains its valid unique AssetId as SourceMissing");
        const auto orphanEvidence = MakeConstantEvidence(orphanCandidate, fingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline tombstoned(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(baseline, orphanCandidate,
                                                            {orphanEvidence.TypedData(), orphanEvidence.Size()}, tombstoned) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare SourceMissing orphan-sidecar candidate");
        const asset_streaming::ProjectAssetState* const acceptedMissing = tombstoned.LookupAsset(orphanId);
        const auto* missing = FindChange(tombstoned, orphanId);
        Check(acceptedBefore != nullptr && acceptedMissing != nullptr &&
                  acceptedMissing->presence == asset_streaming::ProjectAssetPresence::Missing &&
                  acceptedMissing->sourceRoot == acceptedBefore->sourceRoot &&
                  acceptedMissing->sourceIdentity == acceptedBefore->sourceIdentity &&
                  acceptedMissing->physicalPath == acceptedBefore->physicalPath &&
                  acceptedMissing->hasSourceContentFingerprint == acceptedBefore->hasSourceContentFingerprint &&
                  acceptedMissing->sourceContentFingerprint == acceptedBefore->sourceContentFingerprint &&
                  acceptedMissing->outputs.Count() == acceptedBefore->outputs.Count() &&
                  missing != nullptr && HasChangeReason(missing->reasons, Reason::Missing),
              "E2.2 P5 SourceMissing orphan sidecar becomes a tombstone and preserves accepted evidence");

        static_cast<void>(DeleteDirectoryTree(files, orphanRoot));
    }

    // Diagnostics/conflict matrix: ambiguous move evidence is reported on an
    // unowned source row; missing/invalid sidecars and duplicate identities stay
    // diagnostic and never select an arbitrary AssetId winner.
    {
        using ComparisonResult = asset_streaming::ProjectAssetComparisonResult;
        const filesystem::AbsolutePath diagnosticRoot = root.AddDirPath("E22Diagnostics");
        Check(files.CreatePath(diagnosticRoot), "E2.2 P5 create diagnostics matrix root");
        containers::DynamicArray<assets::SourceRoot> diagnosticRoots(memory::pools::Assets::GetInstance());
        diagnosticRoots.PushBack({diagnosticRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> diagnosticRootSpan(diagnosticRoots.TypedData(), diagnosticRoots.Size());
        assets::AssetId diagnosticA;
        assets::AssetId diagnosticB;
        assets::AssetId duplicateId;
        Check(assets::GenerateAssetId(diagnosticA) && assets::GenerateAssetId(diagnosticB) && assets::GenerateAssetId(duplicateId),
              "E2.2 P5 generate diagnostics identities");
        const filesystem::AbsolutePath diagnosticPathA = diagnosticRoot.AddFilePath("a.tasset");
        const filesystem::AbsolutePath diagnosticPathB = diagnosticRoot.AddFilePath("b.tasset");
        const crypto::Digest256 ambiguousFingerprint = crypto::Sha256("ambiguous-e2.2", 14u);
        Check(WriteTextAsset(files, diagnosticPathA, containers::StringView("same\n"), Metadata(diagnosticA)) &&
                  WriteTextAsset(files, diagnosticPathB, containers::StringView("same\n"), Metadata(diagnosticB)),
              "E2.2 P5 write ambiguous-move accepted pair");
        assets::SourceDatabase diagnosticBaselineDatabase;
        Check(diagnosticBaselineDatabase.Rescan(files, diagnosticRootSpan) == assets::SourceDatabaseResult::Success &&
                  diagnosticBaselineDatabase.ClassifySources(importerSpan),
              "E2.2 P5 prepare diagnostics accepted baseline");
        const auto diagnosticBaselineEvidence = MakeConstantEvidence(diagnosticBaselineDatabase, ambiguousFingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline diagnosticEmpty(config.changeState);
        asset_streaming::ProjectAssetAcceptedBaseline diagnosticBaseline(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  diagnosticEmpty, diagnosticBaselineDatabase,
                  {diagnosticBaselineEvidence.TypedData(), diagnosticBaselineEvidence.Size()}, diagnosticBaseline) == ComparisonResult::Success,
              "E2.2 P5 accept ambiguous-move baseline");

        const filesystem::AbsolutePath orphanPath = diagnosticRoot.AddFilePath("orphan.tasset");
        const filesystem::AbsolutePath invalidPath = diagnosticRoot.AddFilePath("invalid.tasset");
        const filesystem::AbsolutePath duplicatePathA = diagnosticRoot.AddFilePath("duplicate_a.tasset");
        const filesystem::AbsolutePath duplicatePathB = diagnosticRoot.AddFilePath("duplicate_b.tasset");
        const char malformedMetadata[] = "not-vmeta";
        Check(WriteBytes(files, orphanPath, reinterpret_cast<const u8*>("same\n"), 5u) &&
                  WriteBytes(files, invalidPath, reinterpret_cast<const u8*>("same\n"), 5u) &&
                  WriteBytes(files, MetadataPath(invalidPath), reinterpret_cast<const u8*>(malformedMetadata),
                             static_cast<u32>(sizeof(malformedMetadata) - 1u)) &&
                  WriteTextAsset(files, duplicatePathA, containers::StringView("duplicate\n"), Metadata(duplicateId)) &&
                  WriteTextAsset(files, duplicatePathB, containers::StringView("duplicate\n"), Metadata(duplicateId)),
              "E2.2 P5 write missing/invalid/duplicate-sidecar diagnostics fixtures");
        assets::SourceDatabase diagnosticCandidate;
        Check(diagnosticCandidate.Rescan(files, diagnosticRootSpan) == assets::SourceDatabaseResult::Success &&
                  diagnosticCandidate.ClassifySources(importerSpan),
              "E2.2 P5 prepare diagnostics candidate");
        const auto diagnosticEvidence = MakeConstantEvidence(diagnosticCandidate, ambiguousFingerprint);
        asset_streaming::ProjectAssetAcceptedBaseline diagnostics(config.changeState);
        Check(asset_streaming::CompareProjectAssetCandidate(
                  diagnosticBaseline, diagnosticCandidate, {diagnosticEvidence.TypedData(), diagnosticEvidence.Size()}, diagnostics) ==
                  ComparisonResult::Success,
              "E2.2 P5 compare diagnostics candidate");
        bool sawMissingSidecar = false;
        bool sawInvalidSidecar = false;
        bool sawAmbiguousMove = false;
        u32 duplicateDiagnostics = 0u;
        for (const asset_streaming::ProjectAssetSourceDiagnostic& diagnostic : diagnostics.diagnostics)
        {
            if (diagnostic.physicalPath == orphanPath)
            {
                sawMissingSidecar = (diagnostic.sourceIssues & assets::MetadataMissing) != 0u && !diagnostic.hasParsedAssetIdentity;
                sawAmbiguousMove = asset_streaming::HasSourceDiagnosticReason(
                    diagnostic.reasons, asset_streaming::ProjectAssetSourceDiagnosticReason::AmbiguousMove);
            }
            if (diagnostic.physicalPath == invalidPath)
                sawInvalidSidecar = (diagnostic.sourceIssues & assets::MetadataInvalid) != 0u && !diagnostic.hasParsedAssetIdentity;
            if ((diagnostic.physicalPath == duplicatePathA || diagnostic.physicalPath == duplicatePathB) &&
                (diagnostic.sourceIssues & assets::AssetIdentityConflict) != 0u)
                ++duplicateDiagnostics;
        }
        Check(sawMissingSidecar && sawAmbiguousMove,
              "E2.2 P5 missing sidecar remains unowned and reports ambiguous move when fingerprint evidence has multiple owners");
        Check(sawInvalidSidecar, "E2.2 P5 invalid sidecar remains a source-row diagnostic without invented ownership");
        Check(duplicateDiagnostics == 2u && diagnosticCandidate.LookupAsset(duplicateId) == nullptr &&
                  diagnostics.LookupAsset(duplicateId) == nullptr,
              "E2.2 P5 duplicate sidecar identities diagnose both rows and never select an arbitrary winner");
        static_cast<void>(DeleteDirectoryTree(files, diagnosticRoot));
    }

    // A source deletion leaves the accepted tombstone plus the surviving
    // referrer's authored edge, so missing-output backward queries remain exact.
    {
        Check(files.DeleteFile(pathA) && files.DeleteFile(MetadataPath(pathA)) &&
                  pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 P5 externally delete referenced asset");
        const asset_streaming::ProjectAssetState* const deletedA = pipeline.LookupAcceptedAsset(idA);
        const asset_streaming::ProjectAssetState* const survivingReferrer = pipeline.LookupAcceptedAsset(idReferrer);
        Check(deletedA != nullptr && deletedA->presence == asset_streaming::ProjectAssetPresence::Missing &&
                  survivingReferrer != nullptr && survivingReferrer->references.Count() == 1u &&
                  survivingReferrer->references[0].to == OutputReference(idA) &&
                  pipeline.GetMissingOutputReferencers(OutputReference(idA)).Count() == 1u,
              "E2.2 P5 deletion preserves referencer history and missing-output backward lookup");
        Check(WriteTextAsset(files, pathA, containers::StringView(""), Metadata(idA)) &&
                  pipeline.Rescan(rootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  HasAcceptedChange(pipeline, idA, asset_streaming::ProjectAssetChangeReason::Restored),
              "E2.2 P5 restore deleted asset for downstream operation fixtures");
    }

    // Callback importers are fully supported even when the descriptor has no
    // sourceType. E2.2 retains authoritative change evidence for them, but the
    // typed source-seed list must stay empty rather than inventing a type. The
    // later E2.3 translation step owns turning this importer-specific evidence
    // into concrete build requests.
    {
        using Reason = asset_streaming::ProjectAssetChangeReason;
        const filesystem::AbsolutePath callbackRoot = root.AddDirPath("E22CallbackImporter");
        const filesystem::AbsolutePath callbackAssetsRoot = callbackRoot.AddDirPath("Assets");
        const filesystem::AbsolutePath callbackDerivedRoot = callbackRoot.AddDirPath("Derived");
        const filesystem::AbsolutePath callbackIndexRoot = callbackRoot.AddDirPath("Index");
        const filesystem::AbsolutePath callbackLooseRoot = callbackRoot.AddDirPath("Loose");
        const filesystem::AbsolutePath callbackJournal = callbackAssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(callbackRoot) && files.CreatePath(callbackAssetsRoot) && files.CreatePath(callbackDerivedRoot) &&
                  files.CreatePath(callbackIndexRoot) && files.CreatePath(callbackLooseRoot),
              "E2.2 follow-up create callback-importer fixture roots");

        assets::AssetId callbackId;
        Check(assets::GenerateAssetId(callbackId), "E2.2 follow-up callback-importer identity generation");
        const filesystem::AbsolutePath callbackPath = callbackAssetsRoot.AddFilePath("callback.tasset");
        Check(WriteTextAsset(files, callbackPath, containers::StringView("# callback-initial\n"), Metadata(callbackId)),
              "E2.2 follow-up write callback-importer asset");

        containers::DynamicArray<assets::SourceRoot> callbackRoots(memory::pools::Assets::GetInstance());
        callbackRoots.PushBack({callbackAssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> callbackRootSpan(callbackRoots.TypedData(), callbackRoots.Size());
        assets::SourceDatabase callbackSources;
        Check(callbackSources.Rescan(files, callbackRootSpan) == assets::SourceDatabaseResult::Success,
              "E2.2 follow-up scan callback-importer source catalog");

        assets::SourceImporterDescriptor callbackImporter = importer;
        RequestResolveBlockState callbackResolveBlock;
        callbackResolveBlock.block.SetValue(false);
        callbackResolveBlock.started.SetValue(false);
        callbackResolveBlock.release.SetValue(false);
        callbackResolveBlock.fail.SetValue(false);
        callbackResolveBlock.resolveCalls.SetValue(0u);
        callbackImporter.sourceType = resources::InvalidResourceTypeId;
        callbackImporter.createBuildRequest = &CreateCallbackBuildRequest;
        callbackImporter.userData = &callbackResolveBlock;
        Check(callbackImporter.IsValid(), "E2.2 follow-up callback importer is valid without descriptor sourceType");
        containers::DynamicArray<assets::SourceImporterDescriptor> callbackImporters(memory::pools::Assets::GetInstance());
        callbackImporters.PushBack(callbackImporter);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> callbackImporterSpan(callbackImporters.TypedData(),
                                                                                                   callbackImporters.Size());

        asset_streaming::ProjectAssetPipelineConfig callbackConfig = config;
        callbackConfig.build.artifactCacheRoot = callbackDerivedRoot;
        callbackConfig.build.dependencyIndexRoot = callbackIndexRoot;
        callbackConfig.publication.artifactCacheRoot = callbackDerivedRoot;
        callbackConfig.publication.looseRoot = callbackLooseRoot;
        callbackConfig.operationJournalPath = callbackJournal;

        asset_streaming::ProjectAssetPipeline callbackPipeline;
        asset_streaming::ProjectAssetRestoreReport callbackInitialRestore;
        Check(callbackPipeline.Initialize(files, callbackSources, streamer, compilerSpan, callbackImporterSpan, callbackConfig,
                                          &callbackInitialRestore) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 follow-up callback importer initializes and accepts its asset");
        const asset_streaming::ProjectAssetState* const callbackInitial = callbackPipeline.LookupAcceptedAsset(callbackId);
        const asset_streaming::ProjectAssetChangeRecord* const callbackInitialChange = FindAcceptedChange(callbackPipeline, callbackId);
        Check(callbackInitialRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::NotFound &&
                  callbackInitial != nullptr && callbackInitial->sourceType == resources::InvalidResourceTypeId &&
                  callbackInitial->importState == assets::SourceImportState::MetadataSelected && callbackInitialChange != nullptr &&
                  asset_streaming::HasChangeReason(callbackInitialChange->reasons, Reason::New) &&
                  !asset_streaming::HasChangeReason(callbackInitialChange->reasons, Reason::Unsupported) &&
                  callbackPipeline.GetAssetInvalidationSeeds().Empty() && !callbackPipeline.GetAssetInvalidationOutputs().Empty(),
              "E2.2 follow-up callback importer is supported on initial acceptance while typed seeds remain unavailable");

        // E2.3 P3: the callback descriptor still has no sourceType and VADI has
        // no record yet. Automatic enable queues one current-source recovery
        // pass; its explicit output becomes a first-cook root through the real
        // callback-produced BuildRequest.
        Check(callbackPipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  callbackPipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P2 callback automatic capture starts from the current accepted generation");
        Check(callbackPipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  !callbackPipeline.CanMutateSources(),
              "E2.3 P3 callback first-cook recovery acquires automatic admission without blocking the owner tick");
        asset_streaming::ProjectAssetPipelineFailure automaticBusyFailure;
        asset_streaming::ProjectAssetRequest blockedManual =
            callbackPipeline.Request(OutputReference(callbackId), assets::BuildPriority::Normal, &automaticBusyFailure);
        Check(!blockedManual.IsValid() && automaticBusyFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.3 P3 automatic admission excludes manual graph requests until its transaction is finalized");
        Check(DrainAutomaticRecook(callbackPipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  callbackPipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  callbackPipeline.CanMutateSources(),
              "E2.3 P3 callback first cook commits through automatic recooker and releases admission");

        const u64 callbackInitialGeneration = callbackPipeline.GetAcceptedAssetGeneration();
        Check(WriteTextAsset(files, callbackPath, containers::StringView("# callback-modified\n"), Metadata(callbackId)) &&
                  callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 follow-up callback-importer content modification is accepted");
        const asset_streaming::ProjectAssetChangeRecord* const callbackModifiedChange = FindAcceptedChange(callbackPipeline, callbackId);
        Check(callbackPipeline.GetAcceptedAssetGeneration() == callbackInitialGeneration + 1u && callbackModifiedChange != nullptr &&
                  asset_streaming::HasChangeReason(callbackModifiedChange->reasons, Reason::ContentChanged) &&
                  !asset_streaming::HasChangeReason(callbackModifiedChange->reasons, Reason::Unsupported) &&
                  callbackModifiedChange->current.available &&
                  callbackModifiedChange->current.sourceType == resources::InvalidResourceTypeId &&
                  callbackPipeline.GetAssetInvalidationSeeds().Empty() && !callbackPipeline.GetAssetInvalidationOutputs().Empty(),
              "E2.2 follow-up callback modification retains dirty evidence without fabricating a typed source seed");

        const assets::SourceRecord* const callbackModifiedSource = callbackSources.LookupAsset(callbackId);
        const resources::ResourceReference callbackOldTypedInput(
            callbackModifiedSource != nullptr ? callbackModifiedSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackPending = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackPending.state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  callbackPending.pendingAssets == 1u && callbackPending.pendingInputs >= 1u && callbackPending.pendingOutputs >= 1u &&
                  callbackPending.pendingDiagnostics == 0u && callbackPending.capturedEvidenceBytes != 0u &&
                  callbackPending.activeRoots == 0u && !callbackPending.recoveryRequired &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingInputs(), callbackOldTypedInput) &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingOutputs(), OutputReference(callbackId)) &&
                  callbackPipeline.CanMutateSources(),
              "E2.3 P2 callback change resolves a real typed BuildRequest into pending state without starting cooker work");

        // Coalesce a second accepted generation for the same asset. VADI still
        // records the original callback-produced source identity, while the
        // latest current request must resolve against the moved candidate.
        const filesystem::AbsolutePath callbackMovedPath = callbackAssetsRoot.AddFilePath("callback_moved.tasset");
        Check(files.DeleteFile(callbackPath) && files.DeleteFile(MetadataPath(callbackPath)) &&
                  WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-modified\n"), Metadata(callbackId)) &&
                  callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P2 callback external move is accepted into the existing pending asset");
        const assets::SourceRecord* const callbackMovedSource = callbackSources.LookupAsset(callbackId);
        const resources::ResourceReference callbackMovedTypedInput(
            callbackMovedSource != nullptr ? callbackMovedSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackMovedPending = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackMovedSource != nullptr && callbackMovedPending.pendingAssets == 1u && callbackMovedPending.pendingInputs >= 2u &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingInputs(), callbackOldTypedInput) &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingInputs(), callbackMovedTypedInput) &&
                  callbackMovedPending.activeRoots == 0u,
              "E2.3 P2 coalescing preserves the old VADI callback source and latest candidate request identity");

        // Missing and restored states remain explicit diagnostics; neither side
        // fabricates a current request while the source is absent.
        Check(files.DeleteFile(callbackMovedPath) && files.DeleteFile(MetadataPath(callbackMovedPath)) &&
                  callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P2 callback missing state is accepted");
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackMissingPending = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackMissingPending.pendingAssets == 1u && callbackMissingPending.pendingDiagnostics == 1u &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingInputs(), callbackOldTypedInput) &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingOutputs(), OutputReference(callbackId)),
              "E2.3 P2 missing callback source preserves old invalidation and records a diagnostic");
        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-restored\n"), Metadata(callbackId)) &&
                  callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  HasAcceptedChange(callbackPipeline, callbackId, Reason::Restored),
              "E2.3 P2 callback source restores through the canonical resolver");
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackRestoredPending = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackRestoredPending.pendingAssets == 1u && callbackRestoredPending.pendingDiagnostics == 0u &&
                  ContainsResource(callbackPipeline.GetAutomaticRecookPendingInputs(), callbackMovedTypedInput),
              "E2.3 P2 restored callback source replaces the latest current evidence without erasing earliest invalidation");

        // A true E2.2 no-op acceptance must leave the complete coalesced P2
        // capture untouched rather than consuming/clearing it.
        const u64 callbackPendingBytes = callbackRestoredPending.capturedEvidenceBytes;
        const u32 callbackPendingInputs = callbackRestoredPending.pendingInputs;
        const u32 callbackPendingOutputs = callbackRestoredPending.pendingOutputs;
        const u64 callbackRestoredGeneration = callbackPipeline.GetAcceptedAssetGeneration();
        Check(callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  callbackPipeline.GetAcceptedAssetGeneration() == callbackRestoredGeneration &&
                  callbackPipeline.GetAcceptedAssetChanges().Empty(),
              "E2.3 P2 callback no-op rescan keeps the accepted generation stable");
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackAfterNoOp = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackAfterNoOp.pendingAssets == 1u && callbackAfterNoOp.pendingInputs == callbackPendingInputs &&
                  callbackAfterNoOp.pendingOutputs == callbackPendingOutputs && callbackAfterNoOp.capturedEvidenceBytes == callbackPendingBytes,
              "E2.3 P2 no-op acceptance does not erase or replay pending work");

        // P3 consumes the whole coalesced callback burst as one active batch.
        // The old VADI source identity and newest moved request are already in
        // the P2 set; the recooker owns root minimization and the single VADI
        // transaction.
        Check(callbackPipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  !callbackPipeline.CanMutateSources(),
              "E2.3 P3 coalesced callback burst acquires one automatic batch");
        Check(DrainAutomaticRecook(callbackPipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P3 coalesced callback burst completes transactionally");
        const asset_streaming::ProjectAssetAutomaticRecookStatus callbackAfterBatch = callbackPipeline.GetAutomaticRecookStatus();
        Check(callbackAfterBatch.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  callbackAfterBatch.pendingAssets == 0u && callbackAfterBatch.pendingInputs == 0u &&
                  callbackAfterBatch.pendingOutputs == 0u && callbackAfterBatch.activeRoots == 0u &&
                  callbackPipeline.CanMutateSources(),
              "E2.3 P3 successful callback batch retires active evidence and leaves no transient replay queue");

        // A genuinely later accepted change becomes a fresh bounded pending
        // batch and is eventually processed after the prior batch releases
        // admission.
        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-later\n"), Metadata(callbackId)) &&
                  callbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P3 later callback change is accepted after the prior batch");
        Check(callbackPipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  callbackPipeline.GetAutomaticRecookStatus().pendingAssets == 1u,
              "E2.3 P3 later accepted callback change forms the next pending batch");
        Check(DrainAutomaticRecook(callbackPipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  callbackPipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P3 later callback batch is eventually processed");

        // A diagnostics-only E2.2 acceptance can advance the accepted
        // generation with an empty asset-change list. P2 must still consume
        // that generation exactly once while preserving the existing complete
        // pending set.
        {
            asset_streaming::ProjectAssetAutomaticPendingState exactOnceExisting;
            exactOnceExisting.inputs.PushBack(callbackMovedTypedInput);
            exactOnceExisting.outputs.PushBack(OutputReference(callbackId));
            // Model a repair marker whose current request was retired by an
            // accepted removal/rename. Pending-state preparation must not carry
            // that stale forced-root marker into the next recook batch.
            exactOnceExisting.forcedOutputs.PushBack(
                {OutputReference(callbackId), asset_streaming::ProjectAssetAutomaticForceReason::CacheRepair});
            exactOnceExisting.lastAcceptedGeneration = callbackRestoredGeneration;

            asset_streaming::ProjectAssetAcceptedBaseline diagnosticsOnly(callbackConfig.changeState);
            diagnosticsOnly.generation = callbackRestoredGeneration + 1u;
            Check(diagnosticsOnly.changes.Empty(),
                  "E2.3 P2 diagnostics-only fixture has no asset changes");

            assets::ProjectAssetBuildSystem unusedBuilds;
            assets::DependencyIndex unusedIndex;
            asset_streaming::ProjectAssetAutomaticPendingState exactOnceStaged;
            Check(asset_streaming::PrepareProjectAssetAutomaticPending(exactOnceExisting, diagnosticsOnly, callbackSources,
                                                                        unusedBuilds, unusedIndex,
                                                                        callbackConfig.automaticRecook, exactOnceStaged) ==
                      asset_streaming::ProjectAssetAutomaticPendingPrepareResult::Success &&
                      exactOnceStaged.lastAcceptedGeneration == diagnosticsOnly.generation &&
                      exactOnceStaged.inputs.Size() == 1u && exactOnceStaged.inputs[0] == callbackMovedTypedInput &&
                      exactOnceStaged.outputs.Size() == 1u && exactOnceStaged.outputs[0] == OutputReference(callbackId) &&
                      exactOnceStaged.forcedOutputs.Empty(),
                  "E2.3 P2 diagnostics-only acceptance advances consumption without erasing pending work");

            asset_streaming::ProjectAssetAutomaticPendingState exactOnceRepeat;
            Check(asset_streaming::PrepareProjectAssetAutomaticPending(exactOnceStaged, diagnosticsOnly, callbackSources,
                                                                        unusedBuilds, unusedIndex,
                                                                        callbackConfig.automaticRecook, exactOnceRepeat) ==
                      asset_streaming::ProjectAssetAutomaticPendingPrepareResult::NoChange &&
                      exactOnceRepeat.assets.Empty() && exactOnceRepeat.inputs.Empty() && exactOnceRepeat.outputs.Empty(),
                  "E2.3 P2 repeated accepted generation is consumed exactly once and produces no staged duplicate");
        }
        const u64 callbackModifiedGeneration = callbackPipeline.GetAcceptedAssetGeneration();
        Check(callbackPipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 follow-up callback pipeline shuts down before restart");
        asset_streaming::ProjectAssetRestoreReport callbackRestartRestore;
        Check(callbackPipeline.Initialize(files, callbackSources, streamer, compilerSpan, callbackImporterSpan, callbackConfig,
                                          &callbackRestartRestore) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 follow-up callback pipeline restarts from VPAS");
        const asset_streaming::ProjectAssetState* const callbackRestarted = callbackPipeline.LookupAcceptedAsset(callbackId);
        Check(callbackRestartRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  callbackPipeline.GetAcceptedAssetGeneration() == callbackModifiedGeneration && callbackRestarted != nullptr &&
                  callbackRestarted->sourceType == resources::InvalidResourceTypeId && callbackPipeline.GetAcceptedAssetChanges().Empty() &&
                  callbackPipeline.GetAssetInvalidationSeeds().Empty() && callbackPipeline.GetAssetInvalidationOutputs().Empty() &&
                  !callbackPipeline.IsAutomaticRecookEnabled() && callbackPipeline.GetAutomaticRecookPendingInputs().Empty() &&
                  callbackPipeline.GetAutomaticRecookPendingOutputs().Empty(),
              "E2.2 follow-up callback restart preserves the accepted asset baseline without replaying dirty work");
        Check(callbackPipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.2 follow-up callback pipeline final shutdown");

        // Config-enabled startup uses the same one-time bounded current-source
        // + committed-VADI recovery pass. The earlier automatic batches already
        // committed this callback output, so recovery should evaluate it as
        // current rather than replaying transient VPAS change history.
        asset_streaming::ProjectAssetPipelineConfig startupAutomaticConfig = callbackConfig;
        startupAutomaticConfig.automaticRecook.enabled = true;
        asset_streaming::ProjectAssetPipeline startupAutomaticPipeline;
        asset_streaming::ProjectAssetRestoreReport startupAutomaticRestore;
        Check(startupAutomaticPipeline.Initialize(files, callbackSources, streamer, compilerSpan, callbackImporterSpan,
                                                  startupAutomaticConfig, &startupAutomaticRestore) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  startupAutomaticPipeline.IsAutomaticRecookEnabled() &&
                  startupAutomaticPipeline.GetAutomaticRecookStatus().state ==
                      asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P3 startup-enabled pipeline restores VPAS while queuing bounded VADI recovery");
        Check(startupAutomaticRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  startupAutomaticPipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  DrainAutomaticRecook(startupAutomaticPipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  startupAutomaticPipeline.GetAutomaticRecookStatus().state ==
                      asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  startupAutomaticPipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u &&
                  startupAutomaticPipeline.GetAutomaticRecookStatus().pendingDeliveryEvidenceBytes == 0u &&
                  startupAutomaticPipeline.GetAutomaticRecookPendingInputs().Empty() &&
                  startupAutomaticPipeline.GetAutomaticRecookPendingOutputs().Empty(),
              "E2.4 P1 no-work startup recovery evaluates committed callback output without fabricating a delivery handoff");
        Check(startupAutomaticPipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P3 startup-enabled callback pipeline shuts down cleanly");

        // E2.3 P4: automatic completion is accepted only after freshly resolving
        // the exact build evidence represented by the transaction. A direct disk
        // write that races a blocked build must roll staged VADI state back even
        // though the accepted E2.2 generation has not advanced.
        asset_streaming::ProjectAssetPipelineConfig p4Config = callbackConfig;
        p4Config.automaticRecook.enabled = true;
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, callbackSources, streamer, compilerSpan, callbackImporterSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 fixture starts from current committed VADI state");

        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-p4-direct-base\n"), Metadata(callbackId)) &&
                  p4Pipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 direct-stale fixture accepts one dirty source generation");
        const u64 p4DirectGeneration = p4Pipeline.GetAcceptedAssetGeneration();
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p4Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.3 P4 direct-stale batch acquires automatic admission");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue(), "E2.3 P4 direct-stale compiler reaches deterministic barrier");
        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-p4-direct-raced\n"), Metadata(callbackId)),
              "E2.3 P4 direct-stale writes newer disk truth while graph work is live");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P4 stale direct evidence rejects completion before VADI commit");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p4DirectStale = p4Pipeline.GetAutomaticRecookStatus();
        Check(p4DirectStale.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p4DirectStale.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  p4Pipeline.GetAcceptedAssetGeneration() == p4DirectGeneration && p4Pipeline.CanMutateSources(),
              "E2.3 P4 stale direct completion is explicit and does not fake an accepted source generation");

        // Synchronize the deliberately newer disk truth and rebuild it so the
        // next proof isolates the watcher-generation fence from direct evidence.
        block.block = false;
        block.release.SetValue(false);
        Check(p4Pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Pipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Pipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 direct-stale fixture recovers through current source plus committed VADI");

        // Fence a validator itself: planning has already resolved the request and
        // compilation is blocked. After releasing compilation, block the callback
        // importer on the validator's fresh Resolve(), publish a watcher hint, and
        // then release validation. The source bytes are unchanged, so only the
        // monotonic hint-generation change can force the conservative rejection.
        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-p4-hint-base\n"), Metadata(callbackId)) &&
                  p4Pipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 hint-race fixture accepts one dirty source generation");
        const u64 p4HintGeneration = p4Pipeline.GetAcceptedAssetGeneration();
        ManualObservationSource p4Observation;
        asset_streaming::ProjectAssetObservationConfig p4ObservationConfig;
        p4ObservationConfig.settleTicks = 2;
        p4ObservationConfig.maximumDeferralTicks = 4;
        p4ObservationConfig.maximumPathsPerTick = 2;
        p4ObservationConfig.maximumPendingPaths = 32;
        p4ObservationConfig.maximumReconcileOperationsPerTick = 2;
        Check(p4Pipeline.EnableObservation(p4Observation, p4ObservationConfig) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainObservation(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 deterministic watcher is ready before hint-race validation");

        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        callbackResolveBlock.block.SetValue(false);
        callbackResolveBlock.started.SetValue(false);
        callbackResolveBlock.release.SetValue(false);
        Check(p4Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.3 P4 hint-race batch acquires automatic admission");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue(), "E2.3 P4 hint-race compiler reaches deterministic barrier");
        callbackResolveBlock.block.SetValue(true);
        block.release.SetValue(true);
        for (u32 spin = 0; spin < 2000000u && !callbackResolveBlock.started.GetValue(); ++spin)
        {
            static_cast<void>(p4Pipeline.TickAutomaticRecook());
            concurrency::YieldCurrentThread();
        }
        Check(callbackResolveBlock.started.GetValue(), "E2.3 P4 validator reaches fresh-resolve barrier");
        p4Observation.Emit(filesystem::DirectoryObservationKind::Modified, callbackMovedPath);
        callbackResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P4 watcher-generation change rejects an otherwise current completion");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p4HintStale = p4Pipeline.GetAutomaticRecookStatus();
        Check(p4HintStale.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p4HintStale.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  p4Pipeline.GetAcceptedAssetGeneration() == p4HintGeneration,
              "E2.3 P4 hint fence rejects without pretending the queued hint was already reconciled");

        callbackResolveBlock.block.SetValue(false);
        callbackResolveBlock.release.SetValue(false);
        block.block = false;
        block.release.SetValue(false);
        Check(p4Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 stale active evidence remains recoverable after explicit retry");
        Check(DrainObservation(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Pipeline.DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P4 fixture drains queued hint and shuts down without starting E2.4 publication");
        block.started.SetValue(false);
        block.release.SetValue(false);

        // Retained capture bounds fail atomically: the source acceptance still
        // succeeds, but no partial automatic queue is published.
        asset_streaming::ProjectAssetPipelineConfig boundedCallbackConfig = callbackConfig;
        boundedCallbackConfig.automaticRecook.maximumCapturedEvidenceBytes = 1u;
        asset_streaming::ProjectAssetPipeline boundedCallbackPipeline;
        Check(boundedCallbackPipeline.Initialize(files, callbackSources, streamer, compilerSpan, callbackImporterSpan,
                                                 boundedCallbackConfig) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  boundedCallbackPipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P2 bounded callback pipeline starts from clean accepted state");
        const u64 boundedGeneration = boundedCallbackPipeline.GetAcceptedAssetGeneration();
        Check(WriteTextAsset(files, callbackMovedPath, containers::StringView("# callback-overflow\n"), Metadata(callbackId)) &&
                  boundedCallbackPipeline.Rescan(callbackRootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  boundedCallbackPipeline.GetAcceptedAssetGeneration() == boundedGeneration + 1u,
              "E2.3 P2 capture-bound failure does not reject authoritative source acceptance");
        const asset_streaming::ProjectAssetAutomaticRecookStatus boundedStatus = boundedCallbackPipeline.GetAutomaticRecookStatus();
        Check(boundedStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired && boundedStatus.recoveryRequired &&
                  asset_streaming::HasAutomaticRecookDirtyReason(boundedStatus.dirtyReasons,
                                                                 asset_streaming::ProjectAssetAutomaticRecookDirtyReason::OverflowRecovery) &&
                  boundedStatus.pendingAssets == 0u && boundedStatus.pendingInputs == 0u && boundedStatus.pendingOutputs == 0u &&
                  boundedStatus.capturedEvidenceBytes == 0u && boundedStatus.activeRoots == 0u,
              "E2.3 P2 capture overflow requests recovery and never exposes a truncated pending set");
        const u32 boundedCompileCalls = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(boundedCallbackPipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  boundedCallbackPipeline.GetAutomaticRecookStatus().state ==
                      asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  boundedCallbackPipeline.GetAutomaticRecookStatus().recoveryRequired &&
                  block.compileCalls.GetValue() == boundedCompileCalls,
              "E2.3 P5 irrecoverable capture bound performs one authoritative recovery attempt then stays RecoveryRequired");
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(boundedCallbackPipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 irrecoverable recovery does not busy-loop on owner ticks");
        Check(block.compileCalls.GetValue() == boundedCompileCalls,
              "E2.3 P5 irrecoverable recovery performs no root work while sticky");
        Check(boundedCallbackPipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(boundedCallbackPipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  boundedCallbackPipeline.GetAutomaticRecookStatus().state ==
                      asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired,
              "E2.3 P5 explicit retry makes one bounded irrecoverable recovery attempt");
        Check(boundedCallbackPipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P2 bounded callback pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, callbackRoot), "E2.2 follow-up callback-importer fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.3 P6 deterministic matrix closure for test-only gaps. This isolated
    // fixture deliberately does not define P5 policy: it covers stale-evidence
    // variants, one-in-flight coordination, a source+sidecar burst arriving
    // during another batch, restart recovery from dirty/missing VADI, and stale
    // preservation of an already published last-valid loose resource.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p6Root = root.AddDirPath("E23P6Matrix");
        const filesystem::AbsolutePath p6AssetsRoot = p6Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p6DerivedRoot = p6Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p6IndexRoot = p6Root.AddDirPath("Index");
        const filesystem::AbsolutePath p6LooseRoot = p6Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p6Journal = p6AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p6Root) && files.CreatePath(p6AssetsRoot) && files.CreatePath(p6DerivedRoot) &&
                  files.CreatePath(p6IndexRoot) && files.CreatePath(p6LooseRoot),
              "E2.3 P6 create isolated matrix roots");

        assets::AssetId p6PrimaryId;
        assets::AssetId p6DependencyId;
        Check(assets::GenerateAssetId(p6PrimaryId) && assets::GenerateAssetId(p6DependencyId),
              "E2.3 P6 generate primary/dependency identities");
        const filesystem::AbsolutePath p6PrimaryPath = p6AssetsRoot.AddFilePath("primary.tasset");
        const filesystem::AbsolutePath p6DependencyPath = p6AssetsRoot.AddFilePath("dependency.tasset");
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-primary-initial\n"), Metadata(p6PrimaryId)) &&
                  WriteTextAsset(files, p6DependencyPath, containers::StringView("p6-dependency\n"), Metadata(p6DependencyId)),
              "E2.3 P6 write initial primary/dependency sources");

        containers::DynamicArray<assets::SourceRoot> p6Roots(memory::pools::Assets::GetInstance());
        p6Roots.PushBack({p6AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p6RootSpan(p6Roots.TypedData(), p6Roots.Size());
        assets::SourceDatabase p6Sources;
        Check(p6Sources.Rescan(files, p6RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.3 P6 initial source catalog scan");

        RequestResolveBlockState p6ResolveState;
        p6ResolveState.block.SetValue(false);
        p6ResolveState.started.SetValue(false);
        p6ResolveState.release.SetValue(false);
        p6ResolveState.fail.SetValue(false);
        p6ResolveState.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p6Importer = importer;
        p6Importer.sourceType = resources::InvalidResourceTypeId;
        p6Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p6Importer.userData = &p6ResolveState;
        containers::DynamicArray<assets::SourceImporterDescriptor> p6Importers(memory::pools::Assets::GetInstance());
        p6Importers.PushBack(p6Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p6ImporterSpan(p6Importers.TypedData(), p6Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p6Config = config;
        p6Config.build.artifactCacheRoot = p6DerivedRoot;
        p6Config.build.dependencyIndexRoot = p6IndexRoot;
        p6Config.publication.artifactCacheRoot = p6DerivedRoot;
        p6Config.publication.looseRoot = p6LooseRoot;
        p6Config.operationJournalPath = p6Journal;
        p6Config.automaticRecook.enabled = true;
        // Keep this isolated fixture intentionally below the size of one real
        // current VDDC record so P3 can prove protected-over-budget status
        // without deleting any current project/delivery truth.
        p6Config.cacheMaintenance.maximumVddcBytes = 1u;

        block.block = false;
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.fail.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};

        asset_streaming::ProjectAssetPipeline p6Pipeline;
        Check(p6Pipeline.Initialize(files, p6Sources, streamer, compilerSpan, p6ImporterSpan, p6Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 missing-VADI startup first-cooks both current callback outputs");

        const assets::SourceRecord* const p6PrimarySource = p6Sources.LookupAsset(p6PrimaryId);
        Check(p6PrimarySource != nullptr, "E2.3 P6 primary source exists after startup recovery");
        if (p6PrimarySource != nullptr)
            block.dependencyOwnerSource = resources::ResourceReference(p6PrimarySource->sourceIdentity, TestSourceType);
        block.dependencyOutput = OutputReference(p6DependencyId);
        block.dependencyEnabled.SetValue(true);

        // Establish one committed generated-dependency edge so P6 can later
        // mutate dependency discovery between build and fresh precommit Prepare.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-primary-with-dependency\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 establish committed generated-dependency evidence");

        // Publish one manual baseline. Automatic E2.3 work must never replace it;
        // P6 uses its exact bytes as the last-valid publication invariant for
        // stale rejection without implementing E2.4 replacement.
        asset_streaming::ProjectAssetRequest p6PublishedRequest = p6Pipeline.Request(OutputReference(p6PrimaryId));
        Check(p6PublishedRequest.IsValid(), "E2.3 P6 manual last-valid publication request starts");
        p6PublishedRequest.Wait();
        Check(p6PublishedRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 manual last-valid publication succeeds");
        p6PublishedRequest.Reset();

        Check(p6Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.5 P3 protected-over-budget maintenance dispatches through the project owner");
        Check(DrainVddcMaintenance(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 protected-over-budget maintenance completes through owner-tick adoption");
        const asset_streaming::ProjectAssetVddcMaintenanceStatus protectedBudgetStatus = p6Pipeline.GetVddcMaintenanceStatus();
        Check(protectedBudgetStatus.state == asset_streaming::ProjectAssetVddcMaintenanceState::Deferred &&
                  protectedBudgetStatus.lastResult == assets::PersistentCacheMaintenanceResult::Deferred &&
                  protectedBudgetStatus.protectedOverBudget && !protectedBudgetStatus.budgetSatisfied &&
                  protectedBudgetStatus.retryRequired && protectedBudgetStatus.protectedRecords != 0u &&
                  protectedBudgetStatus.removedRecords == 0u,
              "E2.5 P3 protected-over-budget is sticky Deferred and preserves every protected record");
        Check(p6Pipeline.TickVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Deferred &&
                  p6Pipeline.GetVddcMaintenanceStatus().scannedRecords == protectedBudgetStatus.scannedRecords &&
                  p6Pipeline.GetVddcMaintenanceStatus().removedRecords == protectedBudgetStatus.removedRecords,
              "E2.5 P3 ordinary owner ticks do not auto-spin sticky protected-over-budget maintenance");

        asset_streaming::ProjectAssetOutputDeliveryStatus p1InstalledStatus;
        Check(p6Pipeline.GetOutputDeliveryStatus(OutputReference(p6PrimaryId), p1InstalledStatus) &&
                  p1InstalledStatus.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                  p1InstalledStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p1InstalledStatus.publicationReason == asset_streaming::ProjectAssetDeliveryReason::None &&
                  p1InstalledStatus.committedBuildFingerprint == p1InstalledStatus.publishedBuildFingerprint &&
                  p1InstalledStatus.committedContentFingerprint == p1InstalledStatus.publishedContentFingerprint &&
                  !p1InstalledStatus.publishedPhysicalPath.Empty() &&
                  p1InstalledStatus.residentState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                  p1InstalledStatus.residentReason == asset_streaming::ProjectAssetDeliveryReason::ResidentNotTracked,
              "E2.4 P1 manual publication records exact installed fingerprints without claiming resident current");
        const assets::BuildFingerprint p1InstalledBuild = p1InstalledStatus.publishedBuildFingerprint;
        const assets::BuildFingerprint p1InstalledContent = p1InstalledStatus.publishedContentFingerprint;
        const u32 p1PendingAfterManual = p6Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs;
        streaming::ResourceSource p6PublishedSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p6PublishedDependencies(memory::pools::Streaming::GetInstance());
        filesystem::AbsolutePath p6PublishedPath;
        Check(streamer.OpenSource(OutputReference(p6PrimaryId), p6PublishedSource, p6PublishedDependencies) == resources::Failure::None &&
                  p6PublishedSource.IsOpen() && !p6PublishedSource.GetPhysicalPath().Empty(),
              "E2.3 P6 resolves the primary output's current last-valid loose publication");
        if (p6PublishedSource.IsOpen())
        {
            p6PublishedPath = p6PublishedSource.GetPhysicalPath();
            p6PublishedSource.Close();
        }
        containers::DynamicArray<u8> p6PublishedBytes(memory::pools::Assets::GetInstance());
        Check(!p6PublishedPath.Empty() && ReadBytes(files, p6PublishedPath, p6PublishedBytes),
              "E2.3 P6 capture last-valid loose publication bytes");

        const auto waitForCompileBarrier = [&block]() noexcept
        {
            for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
                concurrency::YieldCurrentThread();
            return block.started.GetValue();
        };
        const auto publishedBytesUnchanged = [&]() noexcept
        {
            streaming::ResourceSource currentSource;
            containers::DynamicArray<streaming::DependencyDescriptor> currentDependencies(memory::pools::Streaming::GetInstance());
            if (streamer.OpenSource(OutputReference(p6PrimaryId), currentSource, currentDependencies) != resources::Failure::None ||
                !currentSource.IsOpen() || std::strcmp(currentSource.GetPhysicalPath().AsChar(), p6PublishedPath.AsChar()) != 0)
            {
                return false;
            }
            containers::DynamicArray<u8> current(memory::pools::Assets::GetInstance());
            return ReadBytes(files, currentSource.GetPhysicalPath(), current) && SameBytes(current, p6PublishedBytes);
        };
        const filesystem::AbsolutePath p6Vadi = p6IndexRoot.AddFilePath("asset-dependencies.vadi");
        const filesystem::AbsolutePath p6VadiTemp = p6IndexRoot.AddFilePath("asset-dependencies.vadi.tmp");
        const filesystem::AbsolutePath p6VadiTempDirectory = p6IndexRoot.AddDirPath("asset-dependencies.vadi.tmp");

        // Settings bytes change only on disk after the build request was accepted.
        // Fresh source loading must reject the completion before VADI commit.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-settings-stale\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 accept settings-stale source generation");
        const u64 p6SettingsGeneration = p6Pipeline.GetAcceptedAssetGeneration();
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P6 settings-stale batch reaches deterministic compiler barrier");
        assets::AssetMetadata p6SettingsMetadata = Metadata(p6PrimaryId);
        p6SettingsMetadata.settings[0] = 9u;
        Check(WriteMetadata(files, p6PrimaryPath, p6SettingsMetadata), "E2.3 P6 mutate settings bytes while build is live");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p6Pipeline.GetAcceptedAssetGeneration() == p6SettingsGeneration &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p6Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  publishedBytesUnchanged(),
              "E2.3 P6 settings drift rejects precommit and preserves last-valid publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1RejectedStatus;
        Check(p6Pipeline.GetOutputDeliveryStatus(OutputReference(p6PrimaryId), p1RejectedStatus) &&
                  p1RejectedStatus.committedBuildFingerprint == p1InstalledStatus.committedBuildFingerprint &&
                  p1RejectedStatus.committedContentFingerprint == p1InstalledStatus.committedContentFingerprint &&
                  p1RejectedStatus.publishedBuildFingerprint == p1InstalledBuild &&
                  p1RejectedStatus.publishedContentFingerprint == p1InstalledContent &&
                  p6Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == p1PendingAfterManual,
              "E2.4 P1 stale rejection creates no committed-delivery handoff or false status advance");
        block.block = false;
        block.release.SetValue(false);
        Check(WriteMetadata(files, p6PrimaryPath, Metadata(p6PrimaryId)) &&
                  p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 settings-stale retained work recovers after disk truth is restored");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1AdvancedStatus;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1AdvancedAutomatic = p6Pipeline.GetAutomaticRecookStatus();
        Check(p6Pipeline.GetOutputDeliveryStatus(OutputReference(p6PrimaryId), p1AdvancedStatus) &&
                  p1AdvancedStatus.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                  p1AdvancedStatus.committedBuildFingerprint != p1InstalledBuild &&
                  p1AdvancedStatus.publishedBuildFingerprint == p1InstalledBuild &&
                  p1AdvancedStatus.publishedContentFingerprint == p1InstalledContent &&
                  p1AdvancedStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Stale &&
                  p1AdvancedStatus.publicationReason == asset_streaming::ProjectAssetDeliveryReason::BuildAdvancedPastPublication &&
                  p1AdvancedStatus.residentState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                  p1AdvancedAutomatic.pendingDeliveryOutputs > p1PendingAfterManual &&
                  p1AdvancedAutomatic.pendingDeliveryEvidenceBytes != 0u && publishedBytesUnchanged(),
              "E2.4 P1 committed build advances bounded delivery status without reporting the old descriptor/resident current");

        // Authored importer-version drift exercises the importer freshness side
        // without advancing the accepted catalog generation.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-importer-stale\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 accept importer-stale source generation");
        const u64 p6ImporterGeneration = p6Pipeline.GetAcceptedAssetGeneration();
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P6 importer-stale batch reaches deterministic compiler barrier");
        assets::AssetMetadata p6ImporterMetadata = Metadata(p6PrimaryId);
        p6ImporterMetadata.importerVersion = TestImporterVersion + 1u;
        Check(WriteMetadata(files, p6PrimaryPath, p6ImporterMetadata),
              "E2.3 P6 mutate importer version while build is live");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p6Pipeline.GetAcceptedAssetGeneration() == p6ImporterGeneration &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p6Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  publishedBytesUnchanged(),
              "E2.3 P6 importer drift rejects precommit without replacing last-valid publication");
        block.block = false;
        block.release.SetValue(false);
        Check(WriteMetadata(files, p6PrimaryPath, Metadata(p6PrimaryId)) &&
                  p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 importer-stale retained work recovers after metadata restore");

        // Dependency discovery changes after graph preparation but before the
        // validator's fresh Prepare. This must reject even though source bytes and
        // accepted generation themselves are unchanged during the race.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-dependency-stale\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 accept dependency-stale source generation");
        const u64 p6DependencyGeneration = p6Pipeline.GetAcceptedAssetGeneration();
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P6 dependency-stale batch reaches deterministic compiler barrier");
        block.dependencyEnabled.SetValue(false);
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p6Pipeline.GetAcceptedAssetGeneration() == p6DependencyGeneration &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p6Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  publishedBytesUnchanged(),
              "E2.3 P6 dependency-plan drift rejects precommit and preserves last-valid publication");
        block.block = false;
        block.release.SetValue(false);
        block.dependencyEnabled.SetValue(true);
        Check(p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 dependency-stale retained work recovers after dependency contract restore");

        // P5: a non-stale failed root rolls back VADI, retains the batch as one
        // bounded retry entry, never auto-resubmits on owner ticks, and succeeds
        // only after an explicit retry. Automatic success still must not publish.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-root-failure\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept root-failure source generation");
        block.fail.SetValue(true);
        const u32 p5RootFailureCallsBefore = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P5 automatic root failure is terminal");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5RootFailureStatus = p6Pipeline.GetAutomaticRecookStatus();
        Check(p5RootFailureStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5RootFailureStatus.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::FailedBatch) &&
                  p5RootFailureStatus.retryEntries == 1u && publishedBytesUnchanged(),
              "E2.3 P5 failed root retains bounded retry state and last-valid publication");
        const u32 p5RootFailureCallsAfter = block.compileCalls.GetValue();
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 failed root remains sticky without an automatic retry loop");
        Check(block.compileCalls.GetValue() == p5RootFailureCallsAfter && p5RootFailureCallsAfter > p5RootFailureCallsBefore,
              "E2.3 P5 sticky failed root performs no extra compiler work");
        block.fail.SetValue(false);
        Check(p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().retryEntries == 0u && publishedBytesUnchanged(),
              "E2.3 P5 explicit root-failure retry succeeds without E2.4 publication");

        // P5: cancellation is also terminal/sticky until an explicit retry. The
        // compiler barrier observes the cancellation request without a sleep.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-cancel\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept cancellation source generation");
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P5 cancellation batch reaches deterministic compiler barrier");
        Check(p6Pipeline.CancelAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Cancelling,
              "E2.3 P5 active automatic batch accepts cancellation");
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P5 cancelled automatic batch terminates as retained failure");
        block.block = false;
        block.release.SetValue(false);
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5CancelledStatus = p6Pipeline.GetAutomaticRecookStatus();
        Check(p5CancelledStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5CancelledStatus.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch) &&
                  p5CancelledStatus.retryEntries == 1u && publishedBytesUnchanged(),
              "E2.3 P5 cancellation rolls back and retains last-valid publication");
        const u32 p5CancelledCalls = block.compileCalls.GetValue();
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 cancelled batch does not busy-retry");
        Check(block.compileCalls.GetValue() == p5CancelledCalls,
              "E2.3 P5 cancelled batch performs no compiler work before explicit retry");
        Check(p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success && publishedBytesUnchanged(),
              "E2.3 P5 cancelled batch explicitly retries without automatic publication");

        // P5: request resolution may fail after healthy accepted capture. A truly
        // newer accepted generation is itself a retry trigger; no explicit Retry
        // command is required and no stale active invalidation may be lost.
        assets::AssetMetadata p5UnavailableImporter = Metadata(p6PrimaryId);
        p5UnavailableImporter.importerVersion = TestImporterVersion + 1u;
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-resolve-failure\n"), p5UnavailableImporter) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept unavailable-importer diagnostic generation");
        const u32 p5ResolveCallsBefore = block.compileCalls.GetValue();
        const asset_streaming::ProjectAssetPipelineResult p5ResolveResult = DrainAutomaticRecook(p6Pipeline);
        Check(p5ResolveResult == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P5 automatic request-resolution diagnostic reports build failure");
        Check(p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Failed,
              "E2.3 P5 automatic request-resolution diagnostic enters retained failed state");
        Check(asset_streaming::HasAutomaticRecookDirtyReason(
                  p6Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                  asset_streaming::ProjectAssetAutomaticRecookDirtyReason::FailedBatch),
              "E2.3 P5 automatic request-resolution diagnostic records failed-batch reason");
        for (u32 repeat = 0; repeat < 4u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 request-resolution failure remains sticky");
        Check(block.compileCalls.GetValue() == p5ResolveCallsBefore && publishedBytesUnchanged(),
              "E2.3 P5 request-resolution failure starts no root and preserves publication");
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-resolve-newer\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 genuinely newer accepted change triggers retained resolver recovery");

        // P5: force VADI commit I/O failure by occupying the temporary file path
        // with a directory. The transaction must roll back atomically and retry
        // only after the filesystem condition is repaired.
        containers::DynamicArray<u8> p5VadiBefore(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, p6Vadi, p5VadiBefore), "E2.3 P5 capture committed VADI before forced commit I/O failure");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1DeliveryBeforeCommitFailure = p6Pipeline.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p1StatusBeforeCommitFailure;
        const bool p1HadStatusBeforeCommitFailure =
            p6Pipeline.GetOutputDeliveryStatus(OutputReference(p6PrimaryId), p1StatusBeforeCommitFailure);
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-vadi-io\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept VADI commit-I/O source generation");
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P5 VADI commit-I/O batch reaches compiler barrier");
        Check(files.CreatePath(p6VadiTempDirectory), "E2.3 P5 occupy VADI temporary path with directory");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.3 P5 VADI commit I/O failure rolls automatic batch back");
        block.block = false;
        block.release.SetValue(false);
        containers::DynamicArray<u8> p5VadiAfter(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, p6Vadi, p5VadiAfter) && SameBytes(p5VadiBefore, p5VadiAfter) && publishedBytesUnchanged(),
              "E2.3 P5 failed VADI commit preserves committed index and last-valid publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1StatusAfterCommitFailure;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1DeliveryAfterCommitFailure = p6Pipeline.GetAutomaticRecookStatus();
        Check(p1DeliveryAfterCommitFailure.pendingDeliveryOutputs == p1DeliveryBeforeCommitFailure.pendingDeliveryOutputs &&
                  p1DeliveryAfterCommitFailure.pendingDeliveryEvidenceBytes == p1DeliveryBeforeCommitFailure.pendingDeliveryEvidenceBytes &&
                  p6Pipeline.GetOutputDeliveryStatus(OutputReference(p6PrimaryId), p1StatusAfterCommitFailure) == p1HadStatusBeforeCommitFailure &&
                  (!p1HadStatusBeforeCommitFailure ||
                   (p1StatusAfterCommitFailure.committedBuildFingerprint == p1StatusBeforeCommitFailure.committedBuildFingerprint &&
                    p1StatusAfterCommitFailure.committedContentFingerprint == p1StatusBeforeCommitFailure.committedContentFingerprint &&
                    p1StatusAfterCommitFailure.publishedBuildFingerprint == p1StatusBeforeCommitFailure.publishedBuildFingerprint &&
                    p1StatusAfterCommitFailure.publishedContentFingerprint == p1StatusBeforeCommitFailure.publishedContentFingerprint)),
              "E2.4 P1 failed VADI commit cannot advance or partially mutate committed-delivery ownership");
        const u32 p5CommitFailureCalls = block.compileCalls.GetValue();
        for (u32 repeat = 0; repeat < 4u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 VADI commit failure remains sticky without retry spin");
        Check(block.compileCalls.GetValue() == p5CommitFailureCalls && DeleteDirectoryTree(files, p6VadiTempDirectory),
              "E2.3 P5 remove VADI commit-I/O fault after proving sticky failure");
        Check(p6Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success && publishedBytesUnchanged(),
              "E2.3 P5 VADI commit-I/O failure explicitly retries without automatic publication");

        // P5 reverse contention: a live manual graph request keeps automatic work
        // pending without stealing admission or issuing a duplicate compiler root.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-manual-contention\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept automatic work before reverse manual contention");
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        asset_streaming::ProjectAssetRequest p5ManualOwner = p6Pipeline.Request(OutputReference(p6PrimaryId));
        Check(p5ManualOwner.IsValid(), "E2.3 P5 manual request acquires graph before automatic admission");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue() && !p5ManualOwner.HasFinished(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue(), "E2.3 P5 reverse-contention manual compiler reaches barrier");
        const u32 p5ManualContentionCalls = block.compileCalls.GetValue();
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
                  "E2.3 P5 live manual graph owner defers automatic admission nonblockingly");
        Check(p6Pipeline.GetAutomaticRecookStatus().activeAcceptedGeneration == 0u &&
                  p6Pipeline.GetAutomaticRecookStatus().pendingAssets == 1u &&
                  block.compileCalls.GetValue() == p5ManualContentionCalls,
              "E2.3 P5 reverse contention does not duplicate manual work or consume pending automatic state");
        block.release.SetValue(true);
        p5ManualOwner.Wait();
        Check(p5ManualOwner.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 reverse-contention manual request completes before releasing graph ownership");
        p5ManualOwner.Reset();
        block.block = false;
        block.release.SetValue(false);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 automatic batch proceeds after manual graph ownership releases");

        // A complete source+sidecar burst for a DIFFERENT asset arrives while the
        // primary automatic batch owns admission. Repeated owner ticks must not
        // start a second batch. The first batch succeeds, then authoritative
        // observation accepts exactly one new asset and that asset becomes the
        // eventual next automatic batch.
        ManualObservationSource p6Observation;
        asset_streaming::ProjectAssetObservationConfig p6ObservationConfig;
        p6ObservationConfig.settleTicks = 2u;
        p6ObservationConfig.maximumDeferralTicks = 4u;
        p6ObservationConfig.maximumPathsPerTick = 2u;
        p6ObservationConfig.maximumPendingPaths = 32u;
        p6ObservationConfig.maximumReconcileOperationsPerTick = 2u;
        Check(p6Pipeline.EnableObservation(p6Observation, p6ObservationConfig) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainObservation(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 deterministic observation is ready for live-batch burst");
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-live-primary\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 accept primary generation before live-batch burst");
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        const u32 p6CompileCallsBefore = block.compileCalls.GetValue();
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P6 live batch reaches deterministic compiler barrier");
        const u64 p6ActiveGeneration = p6Pipeline.GetAutomaticRecookStatus().activeAcceptedGeneration;
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
                  "E2.3 P6 repeated owner tick keeps one automatic batch in flight");
        Check(p6Pipeline.GetAutomaticRecookStatus().activeAcceptedGeneration == p6ActiveGeneration &&
                  block.compileCalls.GetValue() == p6CompileCallsBefore + 1u,
              "E2.3 P6 repeated owner ticks do not duplicate the blocked root compile");
        const filesystem::AbsolutePath p5BlockedMove = p6AssetsRoot.AddFilePath("blocked_auto_move.tasset");
        asset_streaming::ProjectAssetPipelineFailure p5BusyFailure;
        asset_streaming::ProjectAssetRequest p5BlockedRequest =
            p6Pipeline.Request(OutputReference(p6PrimaryId), assets::BuildPriority::Normal, &p5BusyFailure);
        Check(!p5BlockedRequest.IsValid() && p5BusyFailure.result == asset_streaming::ProjectAssetPipelineResult::Busy &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                  p6Pipeline.MoveAsset(p6PrimaryId, p5BlockedMove) == asset_streaming::ProjectAssetPipelineResult::Busy &&
                  p6Sources.LookupAsset(p6PrimaryId) != nullptr && p6Sources.LookupAsset(p6PrimaryId)->path == p6PrimaryPath,
              "E2.3 P5 automatic admission excludes manual build, rescan, and source mutation");

        assets::AssetId p6BurstId;
        Check(assets::GenerateAssetId(p6BurstId), "E2.3 P6 live-burst identity generation");
        const filesystem::AbsolutePath p6BurstPath = p6AssetsRoot.AddFilePath("during_batch.tasset");
        const char p6BurstBytes[] = "p6-during-batch\n";
        Check(WriteBytes(files, p6BurstPath, reinterpret_cast<const u8*>(p6BurstBytes), sizeof(p6BurstBytes) - 1u) &&
                  WriteMetadata(files, p6BurstPath, Metadata(p6BurstId)),
              "E2.3 P6 write source and sidecar while another batch is live");
        p6Observation.Emit(filesystem::DirectoryObservationKind::Added, p6BurstPath);
        p6Observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(p6BurstPath));
        Check(p6Pipeline.GetObservationStatus().pendingPaths == 2u,
              "E2.3 P6 source+sidecar burst is coalesced as two pending paths while admission is owned");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 first live batch succeeds despite unrelated queued source burst");
        block.block = false;
        block.release.SetValue(false);
        const u64 p6BeforeBurstAccept = p6Pipeline.GetAcceptedAssetGeneration();
        Check(DrainObservation(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAcceptedAssetGeneration() == p6BeforeBurstAccept + 1u && p6Sources.LookupAsset(p6BurstId) != nullptr &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  p6Pipeline.GetAutomaticRecookStatus().pendingAssets == 1u,
              "E2.3 P6 source+sidecar burst accepts once and becomes the next automatic batch");
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P6 different-asset live change is eventually processed by the next batch");
        Check(p6Pipeline.DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 deterministic observation stops after next-batch proof");

        // P5 teardown is the one allowed blocking boundary. Shutdown must cancel
        // and drain live automatic work without requiring the test to release the
        // compiler barrier, then reinitialize and recover current source/VADI truth.
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p5-live-shutdown\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept source generation before live automatic shutdown");
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.block = true;
        Check(p6Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && waitForCompileBarrier(),
              "E2.3 P5 live-shutdown batch reaches deterministic compiler barrier");
        Check(p6Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success && !p6Pipeline.IsInitialized(),
              "E2.3 P5 shutdown cancels and drains live automatic work");
        block.block = false;
        block.release.SetValue(false);
        asset_streaming::ProjectAssetRestoreReport p5LiveRestart;
        Check(p6Pipeline.Initialize(files, p6Sources, streamer, compilerSpan, p6ImporterSpan, p6Config, &p5LiveRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5LiveRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P5 reinitialize performs bounded startup recovery after live-work teardown");

        // Restart with source truth newer than committed VADI. Startup accepts the
        // durable VPAS baseline transition but does not replay it as a transient
        // automatic queue; the one-time current-source + VADI recovery recooks it.
        Check(p6Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 shutdown before dirty-VADI restart");
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p6-restart-dirty\n"), Metadata(p6PrimaryId)),
              "E2.3 P6 mutate source while pipeline is down");
        const u32 p6DirtyCompileCallsBefore = block.compileCalls.GetValue();
        asset_streaming::ProjectAssetRestoreReport p6DirtyRestart;
        Check(p6Pipeline.Initialize(files, p6Sources, streamer, compilerSpan, p6ImporterSpan, p6Config, &p6DirtyRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6DirtyRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  p6Pipeline.GetAutomaticRecookPendingInputs().Empty() && p6Pipeline.GetAutomaticRecookPendingOutputs().Empty(),
              "E2.3 P6 dirty restart restores VPAS without transient automatic replay");
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() > p6DirtyCompileCallsBefore && files.FileExist(p6Vadi),
              "E2.3 P6 startup recovery recooks current source against dirty committed VADI");

        // Preserve one historical deletion in the durable VPAS before the missing-VADI restart. Startup recovery must
        // not reinterpret that settled tombstone as a current request-resolution failure and block unrelated Present
        // assets from rebuilding the missing dependency index.
        Check(p6Pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.DeleteFile(p6BurstPath) && files.DeleteFile(MetadataPath(p6BurstPath)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 missing-VADI fixture accepts one durable historical tombstone with automatic recook disabled");
        const asset_streaming::ProjectAssetState* const p6HistoricalTombstone = p6Pipeline.LookupAcceptedAsset(p6BurstId);
        Check(p6HistoricalTombstone != nullptr &&
                  p6HistoricalTombstone->presence == asset_streaming::ProjectAssetPresence::Missing,
              "E2.3 P6 missing-VADI fixture preserves deleted asset as accepted tombstone");
        Check(p6Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P6 shutdown before missing-VADI restart");

        if (files.FileExist(p6Vadi))
            Check(files.DeleteFile(p6Vadi), "E2.3 P6 remove committed VADI for missing-build-state restart");
        if (files.FileExist(p6VadiTemp))
            Check(files.DeleteFile(p6VadiTemp), "E2.3 P6 remove temporary VADI for missing-build-state restart");
        asset_streaming::ProjectAssetRestoreReport p6MissingRestart;
        Check(p6Pipeline.Initialize(files, p6Sources, streamer, compilerSpan, p6ImporterSpan, p6Config, &p6MissingRestart) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6MissingRestart.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  p6Pipeline.GetAcceptedAssetChanges().Empty() &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  p6Pipeline.GetAutomaticRecookPendingInputs().Empty() && p6Pipeline.GetAutomaticRecookPendingOutputs().Empty(),
              "E2.3 P6 missing-VADI restart restores baseline without dirty-queue replay");
        Check(p6Pipeline.LookupAcceptedAsset(p6BurstId) != nullptr &&
                  p6Pipeline.LookupAcceptedAsset(p6BurstId)->presence == asset_streaming::ProjectAssetPresence::Missing,
              "E2.3 P6 missing-VADI restart retains historical tombstone in durable accepted state");
        Check(DrainAutomaticRecook(p6Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p6Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  p6Pipeline.GetAutomaticRecookStatus().pendingDiagnostics == 0u && files.FileExist(p6Vadi),
              "E2.3 P6 missing VADI recovery skips historical tombstone and rebuilds current Present output authority");

        // P0/P3 maintenance admission reuses the same graph + VADI quiescence
        // gate as automatic recook. Prove both halves with a completed retained
        // manual request and then the unsaved committed VADI state left after its
        // caller interest is released without Poll()/Save(). Shutdown below is
        // the canonical save boundary for that intentionally dirty VADI.
        Check(p6Pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 dirty-VADI admission fixture disables automatic ownership");
        Check(WriteTextAsset(files, p6PrimaryPath, containers::StringView("p3-maintenance-dirty-vadi\n"), Metadata(p6PrimaryId)) &&
                  p6Pipeline.Rescan(p6RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P3 dirty-VADI admission fixture accepts newer source truth");
        asset_streaming::ProjectAssetRequest p3DirtyVadiRequest = p6Pipeline.Request(OutputReference(p6PrimaryId));
        Check(p3DirtyVadiRequest.IsValid(), "E2.5 P3 dirty-VADI manual request starts");
        p3DirtyVadiRequest.Wait();
        Check(p3DirtyVadiRequest.HasFinished() &&
                  p6Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P3 completed retained graph output keeps maintenance admission closed");
        p3DirtyVadiRequest.Reset();
        Check(p6Pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Busy,
              "E2.5 P3 unsaved committed VADI keeps maintenance admission closed after graph interest releases");

        Check(p6Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success && files.FileExist(p6Vadi),
              "E2.3 P6 isolated matrix pipeline shuts down cleanly and persists recovered VADI");
        assets::DependencyIndexConfig p6RecoveredIndexConfig = p6Config.build.dependencyIndex;
        p6RecoveredIndexConfig.root = p6IndexRoot.AsChar();
        p6RecoveredIndexConfig.settingsFingerprint = p6Config.build.settingsFingerprint;
        assets::DependencyIndex p6RecoveredIndex;
        assets::DependencyRecord p6RecoveredPrimary;
        assets::DependencyRecord p6RecoveredDependency;
        Check(p6RecoveredIndex.Initialize(p6RecoveredIndexConfig) == assets::IndexResult::Success &&
                  p6RecoveredIndex.Find(OutputReference(p6PrimaryId), p6RecoveredPrimary) == assets::IndexResult::Success &&
                  p6RecoveredIndex.Find(OutputReference(p6DependencyId), p6RecoveredDependency) == assets::IndexResult::Success,
              "E2.3 P6 missing VADI restart persists recovered records for both current outputs");
        Check(p6RecoveredIndex.Shutdown(), "E2.3 P6 recovered VADI verification index shuts down cleanly");

        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.block = false;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(DeleteDirectoryTree(files, p6Root), "E2.3 P6 isolated matrix fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P1 exact delivery ownership. This fixture isolates the P1 contract:
    // one pending committed output coalesces to its newest exact generation,
    // transient delivery state does not replay across restart, and a different
    // output cannot commit when the one-record delivery bound is already full.
    // No automatic descriptor publication is performed here.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p1Root = root.AddDirPath("E24P1Delivery");
        const filesystem::AbsolutePath p1AssetsRoot = p1Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p1DerivedRoot = p1Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p1IndexRoot = p1Root.AddDirPath("Index");
        const filesystem::AbsolutePath p1LooseRoot = p1Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p1Journal = p1AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p1Root) && files.CreatePath(p1AssetsRoot) && files.CreatePath(p1DerivedRoot) &&
                  files.CreatePath(p1IndexRoot) && files.CreatePath(p1LooseRoot),
              "E2.4 P1 create isolated delivery-ownership roots");

        assets::AssetId p1FirstId;
        assets::AssetId p1SecondId;
        Check(assets::GenerateAssetId(p1FirstId) && assets::GenerateAssetId(p1SecondId),
              "E2.4 P1 generate delivery-ownership identities");
        const filesystem::AbsolutePath p1FirstPath = p1AssetsRoot.AddFilePath("first.tasset");
        const filesystem::AbsolutePath p1SecondPath = p1AssetsRoot.AddFilePath("second.tasset");
        Check(WriteTextAsset(files, p1FirstPath, containers::StringView("p1-first-a\n"), Metadata(p1FirstId)),
              "E2.4 P1 write initial single-output source");

        containers::DynamicArray<assets::SourceRoot> p1Roots(memory::pools::Assets::GetInstance());
        p1Roots.PushBack({p1AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p1RootSpan(p1Roots.TypedData(), p1Roots.Size());
        assets::SourceDatabase p1Sources;
        Check(p1Sources.Rescan(files, p1RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P1 initial delivery-ownership catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p1Config = config;
        p1Config.build.artifactCacheRoot = p1DerivedRoot;
        p1Config.build.dependencyIndexRoot = p1IndexRoot;
        p1Config.publication.artifactCacheRoot = p1DerivedRoot;
        p1Config.publication.looseRoot = p1LooseRoot;
        // Keep reconciliation from consuming the first pending token while this
        // subcase proves same-output coalescing. The automatic commit itself is
        // unaffected; only manual/reconciliation materialization is bounded out.
        p1Config.publication.materializationLimits.maximumResourceBytes = 1u;
        p1Config.operationJournalPath = p1Journal;
        p1Config.automaticRecook.enabled = true;
        p1Config.automaticRecook.maximumPendingOutputs = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};

        asset_streaming::ProjectAssetPipeline p1Pipeline;
        Check(p1Pipeline.Initialize(files, p1Sources, streamer, compilerSpan, importerSpan, p1Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p1Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 first committed output enters bounded delivery ownership");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1FirstCommitted;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1FirstAutomatic = p1Pipeline.GetAutomaticRecookStatus();
        Check(p1FirstAutomatic.pendingDeliveryOutputs == 1u && p1FirstAutomatic.pendingDeliveryEvidenceBytes != 0u &&
                  p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1FirstCommitted) &&
                  p1FirstCommitted.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                  p1FirstCommitted.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Pending &&
                  p1FirstCommitted.publicationReason == asset_streaming::ProjectAssetDeliveryReason::AwaitingPublication &&
                  p1FirstCommitted.publishedBuildFingerprint.IsEmpty(),
              "E2.4 P1 initial automatic commit owns one exact unpublished delivery token");
        const assets::BuildFingerprint p1FirstBuildA = p1FirstCommitted.committedBuildFingerprint;

        Check(WriteTextAsset(files, p1FirstPath, containers::StringView("p1-first-b\n"), Metadata(p1FirstId)) &&
                  p1Pipeline.Rescan(p1RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p1Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 newer commit for the same output succeeds at one-record delivery bound");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1FirstCoalesced;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1CoalescedAutomatic = p1Pipeline.GetAutomaticRecookStatus();
        Check(p1CoalescedAutomatic.pendingDeliveryOutputs == 1u && p1CoalescedAutomatic.pendingDeliveryEvidenceBytes != 0u &&
                  p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1FirstCoalesced) &&
                  p1FirstCoalesced.committedBuildFingerprint != p1FirstBuildA &&
                  p1FirstCoalesced.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Pending &&
                  p1FirstCoalesced.publishedBuildFingerprint.IsEmpty(),
              "E2.4 P1 repeated same-output commit coalesces to one newest pending token");
        const assets::BuildFingerprint p1CoalescedBuild = p1FirstCoalesced.committedBuildFingerprint;

        Check(p1Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 shutdown releases transient pending delivery ownership");
        asset_streaming::ProjectAssetPipelineConfig p1RestartConfig = p1Config;
        p1RestartConfig.publication.materializationLimits = config.publication.materializationLimits;
        asset_streaming::ProjectAssetRestoreReport p1RestartRestore;
        Check(p1Pipeline.Initialize(files, p1Sources, streamer, compilerSpan, importerSpan, p1RestartConfig, &p1RestartRestore) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p1RestartRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  p1Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P1 restart does not replay the transient committed-delivery queue");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1RestartStatus;
        Check(p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1RestartStatus) &&
                  p1RestartStatus.committedBuildFingerprint == p1CoalescedBuild &&
                  p1RestartStatus.publishedBuildFingerprint == p1CoalescedBuild &&
                  p1RestartStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  DrainAutomaticRecook(p1Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p1Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P1 restart reconstructs installed truth from committed VADI without delivery replay");

        Check(WriteTextAsset(files, p1FirstPath, containers::StringView("p1-first-c\n"), Metadata(p1FirstId)) &&
                  p1Pipeline.Rescan(p1RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p1Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 establish one pending token before capacity-rejection proof");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1BeforeCapacityStatus;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1BeforeCapacity = p1Pipeline.GetAutomaticRecookStatus();
        Check(p1BeforeCapacity.pendingDeliveryOutputs == 1u &&
                  p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1BeforeCapacityStatus) &&
                  p1BeforeCapacityStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Stale,
              "E2.4 P1 one-record delivery bound is fully occupied by the newer first output");

        Check(WriteTextAsset(files, p1SecondPath, containers::StringView("p1-second\n"), Metadata(p1SecondId)) &&
                  p1Pipeline.Rescan(p1RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 accept second authored output before delivery-capacity validation");
        Check(DrainAutomaticRecook(p1Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.4 P1 second committed delivery is rejected when bounded ownership is full");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p1CapacityRejected = p1Pipeline.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p1AfterCapacityStatus;
        asset_streaming::ProjectAssetOutputDeliveryStatus p1UnexpectedSecondStatus;
        Check(p1CapacityRejected.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p1CapacityRejected.recoveryRequired &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p1CapacityRejected.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::DeliveryOverflow) &&
                  p1CapacityRejected.pendingDeliveryOutputs == 1u &&
                  p1CapacityRejected.pendingDeliveryEvidenceBytes == p1BeforeCapacity.pendingDeliveryEvidenceBytes &&
                  p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1AfterCapacityStatus) &&
                  p1AfterCapacityStatus.committedBuildFingerprint == p1BeforeCapacityStatus.committedBuildFingerprint &&
                  !p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1SecondId), p1UnexpectedSecondStatus),
              "E2.4 P1 capacity failure preserves the complete older token and publishes no partial second status");

        Check(p1Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 bounded delivery fixture shuts down after capacity rejection");
        assets::DependencyIndexConfig p1IndexConfig = p1Config.build.dependencyIndex;
        p1IndexConfig.root = p1IndexRoot.AsChar();
        p1IndexConfig.settingsFingerprint = p1Config.build.settingsFingerprint;
        assets::DependencyIndex p1Index;
        assets::DependencyRecord p1PersistedFirst;
        assets::DependencyRecord p1UnexpectedSecond;
        Check(p1Index.Initialize(p1IndexConfig) == assets::IndexResult::Success &&
                  p1Index.Find(OutputReference(p1FirstId), p1PersistedFirst) == assets::IndexResult::Success &&
                  p1PersistedFirst.buildFingerprint == p1BeforeCapacityStatus.committedBuildFingerprint &&
                  p1Index.Find(OutputReference(p1SecondId), p1UnexpectedSecond) == assets::IndexResult::NotFound,
              "E2.4 P1 delivery capacity is rejected before the second VADI record becomes visible");
        Check(p1Index.Shutdown(), "E2.4 P1 bounded delivery verification index shuts down cleanly");
        Check(DeleteDirectoryTree(files, p1Root), "E2.4 P1 isolated delivery-ownership fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P1 manual publication failure truth. Force committed materialization
    // to fail after the manual graph request and VADI save. The shared status must
    // still advance committed fingerprints while reporting no installed/resident
    // generation and a publication retry requirement.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p1Root = root.AddDirPath("E24P1ManualFailure");
        const filesystem::AbsolutePath p1AssetsRoot = p1Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p1DerivedRoot = p1Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p1IndexRoot = p1Root.AddDirPath("Index");
        const filesystem::AbsolutePath p1LooseRoot = p1Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p1Journal = p1AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p1Root) && files.CreatePath(p1AssetsRoot) && files.CreatePath(p1DerivedRoot) &&
                  files.CreatePath(p1IndexRoot) && files.CreatePath(p1LooseRoot),
              "E2.4 P1 create manual-publication-failure roots");

        assets::AssetId p1FailureId;
        Check(assets::GenerateAssetId(p1FailureId), "E2.4 P1 generate manual-publication-failure identity");
        const filesystem::AbsolutePath p1FailurePath = p1AssetsRoot.AddFilePath("manual_failure.tasset");
        Check(WriteTextAsset(files, p1FailurePath, containers::StringView("p1-manual-failure\n"), Metadata(p1FailureId)),
              "E2.4 P1 write manual-publication-failure source");

        containers::DynamicArray<assets::SourceRoot> p1Roots(memory::pools::Assets::GetInstance());
        p1Roots.PushBack({p1AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p1RootSpan(p1Roots.TypedData(), p1Roots.Size());
        assets::SourceDatabase p1Sources;
        Check(p1Sources.Rescan(files, p1RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P1 manual-publication-failure catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p1Config = config;
        p1Config.build.artifactCacheRoot = p1DerivedRoot;
        p1Config.build.dependencyIndexRoot = p1IndexRoot;
        p1Config.publication.artifactCacheRoot = p1DerivedRoot;
        p1Config.publication.looseRoot = p1LooseRoot;
        p1Config.publication.materializationLimits.maximumResourceBytes = 1u;
        p1Config.operationJournalPath = p1Journal;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p1Pipeline;
        Check(p1Pipeline.Initialize(files, p1Sources, streamer, compilerSpan, importerSpan, p1Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 manual-publication-failure pipeline initializes without committed output");
        asset_streaming::ProjectAssetRequest p1Request = p1Pipeline.Request(OutputReference(p1FailureId));
        Check(p1Request.IsValid(), "E2.4 P1 manual request starts before forced materialization failure");
        p1Request.Wait();
        asset_streaming::ProjectAssetPipelineFailure p1Failure;
        Check(p1Request.Poll(&p1Failure) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed &&
                  p1Failure.publication.result == asset_streaming::ProjectLoosePublicationResult::LimitExceeded &&
                  p1Failure.publication.materialization == assets::LooseMaterializationResult::LimitExceeded,
              "E2.4 P1 manual publication fails after its build is durably committed");
        p1Request.Reset();

        asset_streaming::ProjectAssetOutputDeliveryStatus p1FailureStatus;
        Check(p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FailureId), p1FailureStatus) &&
                  p1FailureStatus.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                  !p1FailureStatus.committedBuildFingerprint.IsEmpty() &&
                  !p1FailureStatus.committedContentFingerprint.IsEmpty() &&
                  p1FailureStatus.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Failed &&
                  p1FailureStatus.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFailed &&
                  p1FailureStatus.publicationRetryRequired && p1FailureStatus.publishedBuildFingerprint.IsEmpty() &&
                  p1FailureStatus.publishedContentFingerprint.IsEmpty() && p1FailureStatus.publishedPhysicalPath.Empty() &&
                  p1FailureStatus.residentState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                  p1FailureStatus.residentReason == asset_streaming::ProjectAssetDeliveryReason::ResidentNotTracked &&
                  !p1FailureStatus.residentRetryRequired,
              "E2.4 P1 manual failure reports committed truth without pretending descriptor or resident success");

        containers::DynamicArray<assets::ArtifactSetKey> p1FailureProtection(memory::pools::Assets::GetInstance());
        Check(p1Pipeline.CaptureVddcProtectionKeys(p1FailureProtection, 1u, sizeof(assets::ArtifactSetKey)) &&
                  p1FailureProtection.Size() == 1u &&
                  p1FailureProtection[0].build == p1FailureStatus.committedBuildFingerprint &&
                  p1FailureProtection[0].content == p1FailureStatus.committedContentFingerprint,
              "E2.5 P1 manual publication retry protects the current committed VDDC generation");
        p1FailureProtection.Clear();
        Check(!p1Pipeline.CaptureVddcProtectionKeys(p1FailureProtection, 1u, sizeof(assets::ArtifactSetKey) - 1u) &&
                  p1FailureProtection.Empty(),
              "E2.5 P1 manual retry protection refuses a partial bounded snapshot");

        Check(p1Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 manual-publication-failure pipeline shuts down cleanly");
        assets::DependencyIndexConfig p1IndexConfig = p1Config.build.dependencyIndex;
        p1IndexConfig.root = p1IndexRoot.AsChar();
        p1IndexConfig.settingsFingerprint = p1Config.build.settingsFingerprint;
        assets::DependencyIndex p1Index;
        assets::DependencyRecord p1Persisted;
        Check(p1Index.Initialize(p1IndexConfig) == assets::IndexResult::Success &&
                  p1Index.Find(OutputReference(p1FailureId), p1Persisted) == assets::IndexResult::Success &&
                  p1Persisted.buildFingerprint == p1FailureStatus.committedBuildFingerprint &&
                  p1Persisted.contentFingerprint == p1FailureStatus.committedContentFingerprint,
              "E2.4 P1 manual failure status fingerprints equal durable committed VADI authority");
        Check(p1Index.Shutdown(), "E2.4 P1 manual-publication-failure verification index shuts down cleanly");
        Check(DeleteDirectoryTree(files, p1Root), "E2.4 P1 manual-publication-failure fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P1 bounded status churn. Reconciliation may probe a catalog output
    // that has never committed VADI state; that must not consume a status slot.
    // Once a real installed output is later removed, its otherwise-unowned status
    // slot must recycle so a different output can use the same one-entry bound.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p1Root = root.AddDirPath("E24P1StatusChurn");
        const filesystem::AbsolutePath p1AssetsRoot = p1Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p1DerivedRoot = p1Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p1IndexRoot = p1Root.AddDirPath("Index");
        const filesystem::AbsolutePath p1LooseRoot = p1Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p1Journal = p1AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p1Root) && files.CreatePath(p1AssetsRoot) && files.CreatePath(p1DerivedRoot) &&
                  files.CreatePath(p1IndexRoot) && files.CreatePath(p1LooseRoot),
              "E2.4 P1 create bounded-status churn roots");

        assets::AssetId p1FirstId;
        assets::AssetId p1SecondId;
        Check(assets::GenerateAssetId(p1FirstId) && assets::GenerateAssetId(p1SecondId),
              "E2.4 P1 generate bounded-status churn identities");
        const filesystem::AbsolutePath p1FirstPath = p1AssetsRoot.AddFilePath("first.tasset");
        const filesystem::AbsolutePath p1SecondPath = p1AssetsRoot.AddFilePath("second.tasset");
        Check(WriteTextAsset(files, p1FirstPath, containers::StringView("p1-status-first\n"), Metadata(p1FirstId)),
              "E2.4 P1 write first bounded-status source");

        containers::DynamicArray<assets::SourceRoot> p1Roots(memory::pools::Assets::GetInstance());
        p1Roots.PushBack({p1AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p1RootSpan(p1Roots.TypedData(), p1Roots.Size());
        assets::SourceDatabase p1Sources;
        Check(p1Sources.Rescan(files, p1RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P1 bounded-status initial catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p1Config = config;
        p1Config.build.artifactCacheRoot = p1DerivedRoot;
        p1Config.build.dependencyIndexRoot = p1IndexRoot;
        p1Config.publication.artifactCacheRoot = p1DerivedRoot;
        p1Config.publication.looseRoot = p1LooseRoot;
        p1Config.operationJournalPath = p1Journal;
        p1Config.maximumPublishedResources = 1u;
        p1Config.automaticRecook.enabled = false;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p1Pipeline;
        Check(p1Pipeline.Initialize(files, p1Sources, streamer, compilerSpan, importerSpan, p1Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 bounded-status pipeline reconciles an uncooked desired output");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1UncommittedProbe;
        Check(!p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1UncommittedProbe),
              "E2.4 P1 RecordNotFound reconciliation probe consumes no bounded status slot");

        const assets::BuildFingerprint p4UnownedBuild = crypto::Sha256("p4-unowned-resident-build", 25u);
        const assets::BuildFingerprint p4UnownedContent = crypto::Sha256("p4-unowned-resident-content", 27u);
        bool p4ResidentOnlyChurnRejected = true;
        for (u32 churn = 0; churn < 4u; ++churn)
        {
            assets::AssetId unownedId;
            p4ResidentOnlyChurnRejected = p4ResidentOnlyChurnRejected && assets::GenerateAssetId(unownedId);
            asset_streaming::ProjectAssetResidentDeliveryEvidence unownedResident;
            unownedResident.resource = OutputReference(unownedId);
            unownedResident.resourceGeneration = churn + 1u;
            unownedResident.buildFingerprint = p4UnownedBuild;
            unownedResident.contentFingerprint = p4UnownedContent;
            asset_streaming::ProjectAssetOutputDeliveryStatus unexpectedUnownedStatus;
            p4ResidentOnlyChurnRejected = p4ResidentOnlyChurnRejected &&
                                          !p1Pipeline.ReportResidentDelivery(unownedResident) &&
                                          !p1Pipeline.GetOutputDeliveryStatus(unownedResident.resource, unexpectedUnownedStatus);
        }
        Check(p4ResidentOnlyChurnRejected,
              "E2.4 P4 resident-only churn beyond status capacity cannot manufacture project delivery rows");

        asset_streaming::ProjectAssetRequest p1FirstRequest = p1Pipeline.Request(OutputReference(p1FirstId));
        Check(p1FirstRequest.IsValid(), "E2.4 P4 real owner still acquires status capacity after unowned resident churn");
        p1FirstRequest.Wait();
        Check(p1FirstRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 real owner installs successfully after resident-only status churn");
        p1FirstRequest.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p1FirstInstalled;
        Check(p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1FirstInstalled) &&
                  p1FirstInstalled.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.4 P4 real owner receives the one bounded status slot after unowned churn");

        Check(files.DeleteFile(p1FirstPath) && files.DeleteFile(MetadataPath(p1FirstPath)) &&
                  p1Pipeline.Rescan(p1RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 removing first output reconciles and releases its publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1RemovedStatus;
        Check(!p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1FirstId), p1RemovedStatus),
              "E2.4 P1 unowned unpublished status row is recycled");

        Check(WriteTextAsset(files, p1SecondPath, containers::StringView("p1-status-second\n"), Metadata(p1SecondId)) &&
                  p1Pipeline.Rescan(p1RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 second source enters catalog after first status slot is recycled");
        asset_streaming::ProjectAssetOutputDeliveryStatus p1SecondUncommittedProbe;
        Check(!p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1SecondId), p1SecondUncommittedProbe),
              "E2.4 P1 second uncooked reconciliation probe also remains untracked");
        asset_streaming::ProjectAssetRequest p1SecondRequest = p1Pipeline.Request(OutputReference(p1SecondId));
        Check(p1SecondRequest.IsValid(), "E2.4 P1 recycled status capacity admits a different manual output");
        p1SecondRequest.Wait();
        Check(p1SecondRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 different output publishes after bounded status-slot reclamation");
        p1SecondRequest.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p1SecondInstalled;
        Check(p1Pipeline.GetOutputDeliveryStatus(OutputReference(p1SecondId), p1SecondInstalled) &&
                  p1SecondInstalled.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.4 P1 recycled one-entry status owner now tracks the second output");

        Check(p1Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P1 bounded-status churn pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p1Root), "E2.4 P1 bounded-status churn fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P4 ordinary multi-output removal is one visibility transaction. If
    // any member is busy, no prefix may disappear before the whole group can be
    // removed. This specifically covers the normal Rescan/Reconcile path, not
    // only Shutdown's batch unregistration.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E24P4AtomicRemoval");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.4 P4 create atomic-removal roots");

        assets::AssetId p4FirstId;
        assets::AssetId p4SecondId;
        Check(assets::GenerateAssetId(p4FirstId) && assets::GenerateAssetId(p4SecondId),
              "E2.4 P4 generate atomic-removal identities");
        const filesystem::AbsolutePath p4FirstPath = p4AssetsRoot.AddFilePath("first.tasset");
        const filesystem::AbsolutePath p4SecondPath = p4AssetsRoot.AddFilePath("second.tasset");
        Check(WriteTextAsset(files, p4FirstPath, containers::StringView("p4-remove-first\n"), Metadata(p4FirstId)) &&
                  WriteTextAsset(files, p4SecondPath, containers::StringView("p4-remove-second\n"), Metadata(p4SecondId)),
              "E2.4 P4 write atomic-removal sources");

        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P4 atomic-removal catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = false;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, importerSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 atomic-removal pipeline initializes");

        asset_streaming::ProjectAssetRequest p4FirstRequest = p4Pipeline.Request(OutputReference(p4FirstId));
        Check(p4FirstRequest.IsValid(), "E2.4 P4 first atomic-removal output request starts");
        p4FirstRequest.Wait();
        Check(p4FirstRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 first atomic-removal output publishes");
        p4FirstRequest.Reset();
        asset_streaming::ProjectAssetRequest p4SecondRequest = p4Pipeline.Request(OutputReference(p4SecondId));
        Check(p4SecondRequest.IsValid(), "E2.4 P4 second atomic-removal output request starts");
        p4SecondRequest.Wait();
        Check(p4SecondRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 second atomic-removal output publishes");
        p4SecondRequest.Reset();

        P4RemovalDecodeState p4DecodeState;
        Check(streamer.RegisterDecoder({TestOutputType, "P4 atomic removal blocker", &DecodeP4RemovalResource,
                                        &DestroyP4RemovalResource, &p4DecodeState}),
              "E2.4 P4 register atomic-removal blocking decoder");
        resources::PipelineRequest p4BusySecond = streamer.Request(OutputReference(p4SecondId));
        Check(p4BusySecond.IsValid() && p4DecodeState.entered.TryWait(5000),
              "E2.4 P4 second output owns an active stream load before removal");

        Check(files.DeleteFile(p4FirstPath) && files.DeleteFile(MetadataPath(p4FirstPath)) &&
                  files.DeleteFile(p4SecondPath) && files.DeleteFile(MetadataPath(p4SecondPath)),
              "E2.4 P4 remove both source assets before atomic unpublish");
        Check(p4Pipeline.Rescan(p4RootSpan) == asset_streaming::ProjectAssetPipelineResult::CommittedReconciliationPending,
              "E2.4 P4 normal multi-output removal retains committed reconciliation when one batch member is busy");

        streaming::ResourceSource p4FirstStillVisible;
        containers::DynamicArray<streaming::DependencyDescriptor> p4FirstDependencies(memory::pools::Streaming::GetInstance());
        Check(streamer.OpenSource(OutputReference(p4FirstId), p4FirstStillVisible, p4FirstDependencies) == resources::Failure::None,
              "E2.4 P4 failed removal batch leaves the earlier output visible instead of exposing a removed prefix");
        p4FirstStillVisible.Close();

        p4DecodeState.release.Signal();
        Check(p4BusySecond.TryWait(5000), "E2.4 P4 busy stream load finishes after release");
        p4BusySecond.Reset();
        for (u32 attempt = 0; attempt < 5000u &&
                              (streamer.GetStats().activeLoads != 0u || resourcePipeline.GetStats().activeJobs != 0u);
             ++attempt)
            concurrency::SleepOnCurrentThread(1u);

        asset_streaming::ProjectAssetRestoreReport p4RemovalReport;
        Check(p4Pipeline.Reconcile(&p4RemovalReport) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p4RemovalReport.removedPublications == 2u,
              "E2.4 P4 retry removes the complete normal multi-output publication set atomically after the busy owner drains");
        streaming::ResourceSource p4RemovedSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p4RemovedDependencies(memory::pools::Streaming::GetInstance());
        Check(streamer.OpenSource(OutputReference(p4FirstId), p4RemovedSource, p4RemovedDependencies) == resources::Failure::NotFound &&
                  streamer.OpenSource(OutputReference(p4SecondId), p4RemovedSource, p4RemovedDependencies) == resources::Failure::NotFound,
              "E2.4 P4 successful normal removal leaves no partial loose visibility");

        Check(p4Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 atomic-removal pipeline shuts down cleanly");
        Check(streamer.UnregisterDecoder(TestOutputType), "E2.4 P4 unregister atomic-removal blocking decoder");
        Check(DeleteDirectoryTree(files, p4Root), "E2.4 P4 atomic-removal fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P2 exact automatic publication. A committed delivery is materialized
    // and installed only from its retained exact record; a newer accepted/committed
    // generation supersedes the older token before publication. Publication
    // failure remains sticky until explicit publication retry, which reuses the
    // committed artifact without running the compiler again.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E24P2Publication");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.4 P2 create exact-publication roots");

        assets::AssetId p2Id;
        Check(assets::GenerateAssetId(p2Id), "E2.4 P2 generate exact-publication identity");
        const filesystem::AbsolutePath p2Path = p2AssetsRoot.AddFilePath("published.tasset");
        Check(WriteTextAsset(files, p2Path, containers::StringView("p2-a\n"), Metadata(p2Id)),
              "E2.4 P2 write initial source");

        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P2 exact-publication catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, importerSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 first automatic cook commits exact delivery token");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2CommittedA;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p2CommittedA) &&
                  p2CommittedA.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Pending &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P2 committed generation is pending before automatic publication tick");

        Check(WriteTextAsset(files, p2Path, containers::StringView("p2-b\n"), Metadata(p2Id)) &&
                  p2Pipeline.Rescan(p2RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 newer accepted change recooks before older pending descriptor can publish");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2CommittedB;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p2CommittedB) &&
                  p2CommittedB.committedBuildFingerprint != p2CommittedA.committedBuildFingerprint &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P2 newer committed token supersedes older unpublished generation");

        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 automatic publication drains exact newest committed token");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2Published;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p2Published) &&
                  p2Published.committedBuildFingerprint == p2CommittedB.committedBuildFingerprint &&
                  p2Published.publishedBuildFingerprint == p2Published.committedBuildFingerprint &&
                  p2Published.publishedContentFingerprint == p2Published.committedContentFingerprint &&
                  p2Published.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  !p2Published.publicationRetryRequired && !p2Published.publishedPhysicalPath.Empty() &&
                  files.FileExist(p2Published.publishedPhysicalPath) &&
                  p2Published.residentState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P2 descriptor-visible truth advances to exact committed generation without P3 resident claim");

        // E2.4 P3: composition reports neutral resident evidence; the delivery
        // owner derives Current/Stale against exact publication fingerprints and
        // preserves the old usable resident while a newer publication awaits or
        // fails replacement.
        asset_streaming::ProjectAssetResidentDeliveryEvidence p3ResidentOld;
        p3ResidentOld.resource = OutputReference(p2Id);
        p3ResidentOld.resourceGeneration = 41u;
        p3ResidentOld.buildFingerprint = p2Published.publishedBuildFingerprint;
        p3ResidentOld.contentFingerprint = p2Published.publishedContentFingerprint;
        Check(p2Pipeline.ReportResidentDelivery(p3ResidentOld), "E2.4 P3 current resident evidence is accepted through the neutral owner seam");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3ResidentCurrent;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3ResidentCurrent) &&
                  p3ResidentCurrent.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p3ResidentCurrent.residentReason == asset_streaming::ProjectAssetDeliveryReason::None &&
                  p3ResidentCurrent.residentResourceGeneration == 41u && !p3ResidentCurrent.residentRetryRequired,
              "E2.4 P3 resident becomes Current only when its exact fingerprints match the installed publication");

        // E2.4 P5: an identical installed-output reconciliation must be a complete
        // no-op across accepted source truth, compiler work, delivery ownership,
        // descriptor publication, and resident generation. Exercise both manual
        // Rescan and the deterministic observation path without changing bytes.
        const u64 p5NoOpGeneration = p2Pipeline.GetAcceptedAssetGeneration();
        const u32 p5NoOpCompileCalls = block.compileCalls.GetValue();
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5NoOpAutomatic = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Pipeline.Rescan(p2RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.GetAcceptedAssetGeneration() == p5NoOpGeneration &&
                  block.compileCalls.GetValue() == p5NoOpCompileCalls &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == p5NoOpAutomatic.pendingDeliveryOutputs,
              "E2.4 P5 identical manual Rescan causes no compile or delivery churn for an installed resident output");
        ManualObservationSource p5NoOpObservation;
        asset_streaming::ProjectAssetObservationConfig p5NoOpObservationConfig;
        p5NoOpObservationConfig.settleTicks = 2u;
        p5NoOpObservationConfig.maximumDeferralTicks = 4u;
        p5NoOpObservationConfig.maximumPathsPerTick = 2u;
        p5NoOpObservationConfig.maximumPendingPaths = 32u;
        p5NoOpObservationConfig.maximumReconcileOperationsPerTick = 2u;
        Check(p2Pipeline.EnableObservation(p5NoOpObservation, p5NoOpObservationConfig) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainObservation(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 deterministic observation no-op fixture starts from installed resident truth");
        p5NoOpObservation.Emit(filesystem::DirectoryObservationKind::Modified, p2Path);
        Check(DrainObservation(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 identical observation hint reconciles without authored change");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5NoOpStatus;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5NoOpAfter = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Pipeline.GetAcceptedAssetGeneration() == p5NoOpGeneration && block.compileCalls.GetValue() == p5NoOpCompileCalls &&
                  p5NoOpAfter.pendingDeliveryOutputs == p5NoOpAutomatic.pendingDeliveryOutputs &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p5NoOpStatus) &&
                  p5NoOpStatus.committedBuildFingerprint == p3ResidentCurrent.committedBuildFingerprint &&
                  p5NoOpStatus.committedContentFingerprint == p3ResidentCurrent.committedContentFingerprint &&
                  p5NoOpStatus.publishedBuildFingerprint == p3ResidentCurrent.publishedBuildFingerprint &&
                  p5NoOpStatus.publishedContentFingerprint == p3ResidentCurrent.publishedContentFingerprint &&
                  p5NoOpStatus.publishedPhysicalPath == p3ResidentCurrent.publishedPhysicalPath &&
                  p5NoOpStatus.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5NoOpStatus.residentResourceGeneration == p3ResidentCurrent.residentResourceGeneration &&
                  !p5NoOpStatus.publicationRetryRequired && !p5NoOpStatus.residentRetryRequired,
              "E2.4 P5 identical manual/observed reconciliation preserves exact committed/published/resident identity without churn");
        Check(p2Pipeline.DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 deterministic no-op observation stops cleanly");

        Check(WriteTextAsset(files, p2Path, containers::StringView("p2-c\n"), Metadata(p2Id)) &&
                  p2Pipeline.Rescan(p2RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P3 newer build commits while old published resident remains usable");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3CommittedAhead;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3CommittedAhead) &&
                  p3CommittedAhead.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p3CommittedAhead.residentResourceGeneration == 41u,
              "E2.4 P3 commit alone does not falsely stale the resident before publication advances");
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P3 newer exact publication advances before resident replacement");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3AwaitingResident;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3AwaitingResident) &&
                  p3AwaitingResident.residentState == asset_streaming::ProjectAssetDeliveryStageState::Stale &&
                  p3AwaitingResident.residentReason == asset_streaming::ProjectAssetDeliveryReason::AwaitingResidentReplacement &&
                  p3AwaitingResident.residentResourceGeneration == 41u && !p3AwaitingResident.residentRetryRequired,
              "E2.4 P3 newer publication truth marks the still-usable old resident stale without discarding its generation");

        p3ResidentOld.retryRequired = true;
        Check(p2Pipeline.ReportResidentDelivery(p3ResidentOld), "E2.4 P3 failed replacement can report sticky retry while preserving old resident evidence");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3RetryResident;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3RetryResident) &&
                  p3RetryResident.residentState == asset_streaming::ProjectAssetDeliveryStageState::Stale &&
                  p3RetryResident.residentRetryRequired && p3RetryResident.residentResourceGeneration == 41u,
              "E2.4 P3 failed replacement preserves prior usable resident and exposes explicit retry truth");

        asset_streaming::ProjectAssetResidentDeliveryEvidence p3ResidentNew;
        p3ResidentNew.resource = OutputReference(p2Id);
        p3ResidentNew.resourceGeneration = 42u;
        p3ResidentNew.buildFingerprint = p3RetryResident.publishedBuildFingerprint;
        p3ResidentNew.contentFingerprint = p3RetryResident.publishedContentFingerprint;
        Check(p2Pipeline.ReportResidentDelivery(p3ResidentNew), "E2.4 P3 accepted replacement reports the new resident generation");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3ResidentReplaced;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3ResidentReplaced) &&
                  p3ResidentReplaced.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p3ResidentReplaced.residentResourceGeneration == 42u && !p3ResidentReplaced.residentRetryRequired,
              "E2.4 P3 resident becomes Current only after composition reports the accepted published generation");
        Check(files.FileExist(p2Published.publishedPhysicalPath),
              "E2.4 P4 superseded loose generation remains physically protected instead of being eagerly pruned");

        assets::AssetId p4UnownedId;
        Check(assets::GenerateAssetId(p4UnownedId), "E2.4 P4 generate unowned resident-status identity");
        asset_streaming::ProjectAssetResidentDeliveryEvidence p4UnownedResident = p3ResidentNew;
        p4UnownedResident.resource = OutputReference(p4UnownedId);
        asset_streaming::ProjectAssetOutputDeliveryStatus p4UnownedStatus;
        Check(!p2Pipeline.ReportResidentDelivery(p4UnownedResident) &&
                  !p2Pipeline.GetOutputDeliveryStatus(p4UnownedResident.resource, p4UnownedStatus),
              "E2.4 P4 resident-only evidence cannot allocate or exhaust project delivery-status ownership");
        Check(p2Pipeline.ClearResidentDelivery(OutputReference(p2Id)), "E2.4 P3 resident tracking can be cleared after runtime retirement");
        asset_streaming::ProjectAssetOutputDeliveryStatus p3ResidentCleared;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p3ResidentCleared) &&
                  p3ResidentCleared.residentState == asset_streaming::ProjectAssetDeliveryStageState::Untracked &&
                  p3ResidentCleared.residentReason == asset_streaming::ProjectAssetDeliveryReason::ResidentNotTracked,
              "E2.4 P3 cleared resident returns to explicit untracked truth");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 exact-publication pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p2Root), "E2.4 P2 exact-publication fixture cleanup succeeds");
    }

    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E24P2Retry");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.4 P2 create publication-retry roots");

        assets::AssetId p2Id;
        Check(assets::GenerateAssetId(p2Id), "E2.4 P2 generate publication-retry identity");
        const filesystem::AbsolutePath p2Path = p2AssetsRoot.AddFilePath("retry.tasset");
        Check(WriteTextAsset(files, p2Path, containers::StringView("p2-retry\n"), Metadata(p2Id)),
              "E2.4 P2 write publication-retry source");
        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P2 publication-retry catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.publication.materializationLimits.maximumResourceBytes = 1u;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, importerSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 retry fixture commits exact token before publication failure");
        const u32 p2CompileCalls = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P2 automatic materialization failure is terminal for this publication attempt");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2Failed;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p2Failed) && p2Failed.publicationRetryRequired &&
                  p2Failed.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFailed &&
                  p2Failed.publishedBuildFingerprint.IsEmpty() &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P2 failed publication retains exact committed token and sticky retry truth");
        containers::DynamicArray<assets::ArtifactSetKey> p1ProtectionKeys(memory::pools::Assets::GetInstance());
        Check(p2Pipeline.CaptureVddcProtectionKeys(p1ProtectionKeys, 1u, sizeof(assets::ArtifactSetKey)) &&
                  p1ProtectionKeys.Size() == 1u && p1ProtectionKeys[0].build == p2Failed.committedBuildFingerprint &&
                  p1ProtectionKeys[0].content == p2Failed.committedContentFingerprint,
              "E2.5 P1 delivery owner exposes retained committed exact VDDC protection key");
        Check(!p2Pipeline.CaptureVddcProtectionKeys(p1ProtectionKeys, 1u, sizeof(assets::ArtifactSetKey) - 1u) &&
                  p1ProtectionKeys.Empty(),
              "E2.5 P1 delivery protection snapshot refuses partial byte overflow");
        Check(p2Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 failed publication does not spin or rerun compiler on later owner ticks");
        Check(p2Pipeline.RetryAutomaticPublication(OutputReference(p2Id)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 explicit publication retry reuses exact committed artifact without compiler execution");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 publication-retry pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p2Root), "E2.4 P2 publication-retry fixture cleanup succeeds");
    }

    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E24P2Group");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.4 P2 create generated-dependency group roots");

        assets::AssetId p2DependencyId;
        assets::AssetId p2ParentId;
        Check(assets::GenerateAssetId(p2DependencyId) && assets::GenerateAssetId(p2ParentId),
              "E2.4 P2 generate dependency-group identities");
        const filesystem::AbsolutePath p2DependencyPath = p2AssetsRoot.AddFilePath("dependency.tasset");
        const filesystem::AbsolutePath p2ParentPath = p2AssetsRoot.AddFilePath("parent.tasset");
        Check(WriteTextAsset(files, p2DependencyPath, containers::StringView("p2-dependency\n"), Metadata(p2DependencyId)) &&
                  WriteTextAsset(files, p2ParentPath, containers::StringView("p2-parent-a\n"), Metadata(p2ParentId)),
              "E2.4 P2 write generated-dependency group sources");

        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P2 generated-dependency catalog scan");
        const assets::SourceRecord* const p2ParentSource = p2Sources.LookupAsset(p2ParentId);
        Check(p2ParentSource != nullptr, "E2.4 P2 generated-dependency parent source resolves");

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(true);
        block.dependencyOwnerSource = p2ParentSource != nullptr
                                          ? resources::ResourceReference(p2ParentSource->sourceIdentity, TestSourceType)
                                          : resources::ResourceReference{};
        block.dependencyOutput = OutputReference(p2DependencyId);
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, importerSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 2u,
              "E2.4 P2 transaction retains both connected generated-dependency outputs before visibility");
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 connected generated-dependency publication group commits atomically");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2DependencyPublished;
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ParentPublished;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2DependencyId), p2DependencyPublished) &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ParentId), p2ParentPublished) &&
                  p2DependencyPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p2ParentPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p2DependencyPublished.publishedContentFingerprint == p2DependencyPublished.committedContentFingerprint &&
                  p2ParentPublished.publishedContentFingerprint == p2ParentPublished.committedContentFingerprint &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P2 dependency group exposes both exact committed descriptors together");

        const assets::BuildFingerprint p2DependencyBuild = p2DependencyPublished.publishedBuildFingerprint;
        Check(WriteTextAsset(files, p2ParentPath, containers::StringView("p2-parent-b\n"), Metadata(p2ParentId)) &&
                  p2Pipeline.Rescan(p2RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 parent-only recook may publish against exact already-installed generated dependency content");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2DependencyAfterParent;
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ParentAfter;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2DependencyId), p2DependencyAfterParent) &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ParentId), p2ParentAfter) &&
                  p2DependencyAfterParent.publishedBuildFingerprint == p2DependencyBuild &&
                  p2ParentAfter.publishedBuildFingerprint == p2ParentAfter.committedBuildFingerprint &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P2 missing pending dependency is accepted only through its exact installed fingerprint");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 generated-dependency group pipeline shuts down cleanly");
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        Check(DeleteDirectoryTree(files, p2Root), "E2.4 P2 generated-dependency group fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P2 final freshness fence. Freeze the publication snapshot, then block
    // the final canonical Resolve and publish a watcher hint while it is blocked.
    // The same fixed snapshot must reach CommitIfUnchanged so descriptor visibility
    // is rejected instead of accepting the newer observation generation.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E24P2FinalFence");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.4 P2 create final-freshness roots");

        assets::AssetId p2Id;
        Check(assets::GenerateAssetId(p2Id), "E2.4 P2 generate final-freshness identity");
        const filesystem::AbsolutePath p2Path = p2AssetsRoot.AddFilePath("final_fence.tasset");
        Check(WriteTextAsset(files, p2Path, containers::StringView("p2-final-fence\n"), Metadata(p2Id)),
              "E2.4 P2 write final-freshness source");
        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P2 final-freshness catalog scan");

        RequestResolveBlockState p2ResolveBlock;
        p2ResolveBlock.block.SetValue(false);
        p2ResolveBlock.started.SetValue(false);
        p2ResolveBlock.release.SetValue(false);
        p2ResolveBlock.fail.SetValue(false);
        p2ResolveBlock.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p2Importer = importer;
        p2Importer.sourceType = resources::InvalidResourceTypeId;
        p2Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p2Importer.userData = &p2ResolveBlock;
        containers::DynamicArray<assets::SourceImporterDescriptor> p2Importers(memory::pools::Assets::GetInstance());
        p2Importers.PushBack(p2Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p2ImporterSpan(p2Importers.TypedData(), p2Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        ManualObservationSource p2Observation;
        asset_streaming::ProjectAssetObservationConfig p2ObservationConfig;
        p2ObservationConfig.settleTicks = 2;
        p2ObservationConfig.maximumDeferralTicks = 4;
        p2ObservationConfig.maximumPathsPerTick = 2;
        p2ObservationConfig.maximumPendingPaths = 32;
        p2ObservationConfig.maximumReconcileOperationsPerTick = 2;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, p2ImporterSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.EnableObservation(p2Observation, p2ObservationConfig) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainObservation(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 final-freshness deterministic observation source is ready before the first cook");
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P2 final-freshness fixture retains one committed token before publication");
        const u32 p2CompileCalls = block.compileCalls.GetValue();

        p2ResolveBlock.resolveCalls.SetValue(0u);
        p2ResolveBlock.started.SetValue(false);
        p2ResolveBlock.release.SetValue(false);
        p2ResolveBlock.blockOnCall = 2u;
        p2ResolveBlock.block.SetValue(true);
        Check(p2Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.4 P2 final-freshness publication dispatches asynchronously");
        for (u32 spin = 0; spin < 2000000u && !p2ResolveBlock.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(p2ResolveBlock.started.GetValue() && p2ResolveBlock.resolveCalls.GetValue() == 2u,
              "E2.4 P2 final canonical Resolve reaches deterministic post-snapshot barrier");
        p2Observation.Emit(filesystem::DirectoryObservationKind::Modified, p2Path);
        p2ResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P2 watcher generation arriving during final validation rejects descriptor visibility");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2Rejected;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2Id), p2Rejected) && p2Rejected.publicationRetryRequired &&
                  p2Rejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFreshnessRejected &&
                  p2Rejected.publishedPhysicalPath.Empty() && p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P2 freshness rejection retains the exact committed token without advancing publication truth");
        Check(p2Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 freshness rejection is sticky and does not spin or recook");

        Check(DrainObservation(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.DisableObservation() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 final-freshness injected hint reconciles and observation disables without compiler execution");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 final-freshness fixture shuts down with its rejected token retained");
        Check(DeleteDirectoryTree(files, p2Root), "E2.4 P2 final-freshness fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P4 teardown publication race. Keep source truth stable and retain one
    // exact committed token, then hold the automatic publisher at its final
    // canonical Resolve. Shutdown owns the blocking drain while that publication
    // completes; after settlement it must unregister the descriptor it just
    // adopted, leaving no orphan-visible loose resource in ResourceStreamer.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p4Root = root.AddDirPath("E24P4ShutdownPublication");
        const filesystem::AbsolutePath p4AssetsRoot = p4Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p4DerivedRoot = p4Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p4IndexRoot = p4Root.AddDirPath("Index");
        const filesystem::AbsolutePath p4LooseRoot = p4Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p4Journal = p4AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p4Root) && files.CreatePath(p4AssetsRoot) && files.CreatePath(p4DerivedRoot) &&
                  files.CreatePath(p4IndexRoot) && files.CreatePath(p4LooseRoot),
              "E2.4 P4 create shutdown-publication roots");

        assets::AssetId p4Id;
        Check(assets::GenerateAssetId(p4Id), "E2.4 P4 generate shutdown-publication identity");
        const filesystem::AbsolutePath p4Path = p4AssetsRoot.AddFilePath("shutdown_publication.tasset");
        Check(WriteTextAsset(files, p4Path, containers::StringView("p4-shutdown-publication\n"), Metadata(p4Id)),
              "E2.4 P4 write stable shutdown-publication source");
        containers::DynamicArray<assets::SourceRoot> p4Roots(memory::pools::Assets::GetInstance());
        p4Roots.PushBack({p4AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p4RootSpan(p4Roots.TypedData(), p4Roots.Size());
        assets::SourceDatabase p4Sources;
        Check(p4Sources.Rescan(files, p4RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P4 stable shutdown-publication catalog scan");

        RequestResolveBlockState p4ResolveBlock;
        p4ResolveBlock.block.SetValue(false);
        p4ResolveBlock.started.SetValue(false);
        p4ResolveBlock.release.SetValue(false);
        p4ResolveBlock.fail.SetValue(false);
        p4ResolveBlock.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p4Importer = importer;
        p4Importer.sourceType = resources::InvalidResourceTypeId;
        p4Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p4Importer.userData = &p4ResolveBlock;
        containers::DynamicArray<assets::SourceImporterDescriptor> p4Importers(memory::pools::Assets::GetInstance());
        p4Importers.PushBack(p4Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p4ImporterSpan(p4Importers.TypedData(), p4Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p4Config = config;
        p4Config.build.artifactCacheRoot = p4DerivedRoot;
        p4Config.build.dependencyIndexRoot = p4IndexRoot;
        p4Config.publication.artifactCacheRoot = p4DerivedRoot;
        p4Config.publication.looseRoot = p4LooseRoot;
        p4Config.operationJournalPath = p4Journal;
        p4Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p4Pipeline;
        Check(p4Pipeline.Initialize(files, p4Sources, streamer, compilerSpan, p4ImporterSpan, p4Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p4Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P4 stable source first-cooks one exact committed token");
        asset_streaming::ProjectAssetOutputDeliveryStatus p4Committed;
        Check(p4Pipeline.GetOutputDeliveryStatus(OutputReference(p4Id), p4Committed) &&
                  p4Committed.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Pending &&
                  !p4Committed.committedBuildFingerprint.IsEmpty() && !p4Committed.committedContentFingerprint.IsEmpty() &&
                  p4Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 1u,
              "E2.4 P4 shutdown race starts from stable exact committed delivery truth");

        p4ResolveBlock.resolveCalls.SetValue(0u);
        p4ResolveBlock.started.SetValue(false);
        p4ResolveBlock.release.SetValue(false);
        p4ResolveBlock.blockOnCall = 2u;
        p4ResolveBlock.block.SetValue(true);
        Check(p4Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending,
              "E2.4 P4 stable exact publication dispatches before shutdown");
        for (u32 spin = 0; spin < 2000000u && !p4ResolveBlock.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(p4ResolveBlock.started.GetValue() && p4ResolveBlock.resolveCalls.GetValue() == 2u,
              "E2.4 P4 publication is blocked at the existing final canonical Resolve barrier");

        concurrency::Atomic<bool> p4ShutdownStarted(false);
        concurrency::Atomic<bool> p4ShutdownFinished(false);
        asset_streaming::ProjectAssetPipelineResult p4ShutdownResult = asset_streaming::ProjectAssetPipelineResult::InvalidState;
        std::thread p4ShutdownThread([&]()
                                     {
                                         p4ShutdownStarted.SetValue(true);
                                         p4ShutdownResult = p4Pipeline.Shutdown();
                                         p4ShutdownFinished.SetValue(true);
                                     });
        for (u32 spin = 0; spin < 2000000u && !p4ShutdownStarted.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(p4ShutdownStarted.GetValue() && !p4ShutdownFinished.GetValue(),
              "E2.4 P4 shutdown begins while final publication Resolve remains blocked");
        p4ResolveBlock.release.SetValue(true);
        p4ShutdownThread.join();
        p4ResolveBlock.block.SetValue(false);
        p4ResolveBlock.blockOnCall = 0u;
        Check(p4ShutdownResult == asset_streaming::ProjectAssetPipelineResult::Success && !p4Pipeline.IsInitialized(),
              "E2.4 P4 shutdown drains successful publication, settles owner visibility, then closes pipeline");
        streaming::ResourceSource p4ShutdownSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p4ShutdownDependencies(memory::pools::Assets::GetInstance());
        Check(streamer.OpenSource(OutputReference(p4Id), p4ShutdownSource, p4ShutdownDependencies) == resources::Failure::NotFound,
              "E2.4 P4 shutdown unregisters the publication completed during teardown");
        Check(DeleteDirectoryTree(files, p4Root), "E2.4 P4 shutdown-publication fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P2 reverse Generated closure. Force an atomic-group visibility failure,
    // then retry only the child. The retry-blocked parent is still connected via
    // a required Generated edge and must prevent child-only publication. Once the
    // conflict is removed and both retained tokens are explicitly retried, the
    // exact group may publish without rerunning the compiler.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E24P2ReverseRetryGroup");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.4 P2 create reverse-retry group roots");

        assets::AssetId p2DependencyId;
        assets::AssetId p2ParentId;
        Check(assets::GenerateAssetId(p2DependencyId) && assets::GenerateAssetId(p2ParentId),
              "E2.4 P2 generate reverse-retry group identities");
        const filesystem::AbsolutePath p2DependencyPath = p2AssetsRoot.AddFilePath("dependency.tasset");
        const filesystem::AbsolutePath p2ParentPath = p2AssetsRoot.AddFilePath("parent.tasset");
        Check(WriteTextAsset(files, p2DependencyPath, containers::StringView("p2-reverse-child\n"), Metadata(p2DependencyId)) &&
                  WriteTextAsset(files, p2ParentPath, containers::StringView("p2-reverse-parent\n"), Metadata(p2ParentId)),
              "E2.4 P2 write reverse-retry group sources");
        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P2 reverse-retry group catalog scan");
        const assets::SourceRecord* const p2ParentSource = p2Sources.LookupAsset(p2ParentId);
        Check(p2ParentSource != nullptr, "E2.4 P2 reverse-retry parent source resolves");

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(true);
        block.dependencyOwnerSource = p2ParentSource != nullptr
                                          ? resources::ResourceReference(p2ParentSource->sourceIdentity, TestSourceType)
                                          : resources::ResourceReference{};
        block.dependencyOutput = OutputReference(p2DependencyId);
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, importerSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 2u,
              "E2.4 P2 reverse-retry fixture retains both connected committed tokens");
        const u32 p2CompileCalls = block.compileCalls.GetValue();

        const resources::ResourceReference p2ConflictingParent(OutputReference(p2ParentId).GetPath(), TestSourceType);
        Check(streamer.RegisterLoose({p2ConflictingParent, p2ParentPath, {}, 0u, p2Config.publication.priority}),
              "E2.4 P2 install different-type parent path conflict before grouped visibility");
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P2 atomic group visibility failure retains both exact tokens");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2DependencyFailed;
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ParentFailed;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2DependencyId), p2DependencyFailed) &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ParentId), p2ParentFailed) &&
                  p2DependencyFailed.publicationRetryRequired && p2ParentFailed.publicationRetryRequired &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 2u,
              "E2.4 P2 failed atomic component marks both retained members explicit-retry only");
        bool p2StayedSticky = true;
        for (u32 tick = 0; tick < 4u; ++tick)
            p2StayedSticky = p2StayedSticky &&
                              p2Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success;
        Check(p2StayedSticky && block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 pre-visibility setup failure stays sticky across ordinary owner ticks without recook");

        Check(p2Pipeline.RetryAutomaticPublication(OutputReference(p2DependencyId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P2 retrying only the child cannot bypass a connected retry-blocked parent");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2DependencyBlockedAgain;
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ParentStillBlocked;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2DependencyId), p2DependencyBlockedAgain) &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ParentId), p2ParentStillBlocked) &&
                  p2DependencyBlockedAgain.publicationRetryRequired && p2ParentStillBlocked.publicationRetryRequired &&
                  p2DependencyBlockedAgain.publishedPhysicalPath.Empty() && p2ParentStillBlocked.publishedPhysicalPath.Empty() &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 reverse retry-blocked connectivity prevents child-only descriptor advancement without recook");
        Check(p2Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 retry-blocked connected component stays sticky on later owner ticks");

        Check(streamer.UnregisterLoose(OutputReference(p2ParentId).GetPath()),
              "E2.4 P2 remove parent path conflict before explicit coherent retry");
        Check(p2Pipeline.RetryAutomaticPublication(OutputReference(p2DependencyId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2Pipeline.RetryAutomaticPublication(OutputReference(p2ParentId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p2CompileCalls,
              "E2.4 P2 explicit retry of the complete connected component publishes without compiler execution");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2DependencyPublished;
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ParentPublished;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2DependencyId), p2DependencyPublished) &&
                  p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ParentId), p2ParentPublished) &&
                  p2DependencyPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p2ParentPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P2 coherent reverse-retry group advances both descriptors together");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P2 reverse-retry group pipeline shuts down cleanly");
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        Check(DeleteDirectoryTree(files, p2Root), "E2.4 P2 reverse-retry group fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P5 post-commit/pre-publication freshness matrix. Each variant first
    // advances committed truth while keeping the prior exact descriptor/resident
    // current, then mutates one canonical input only after publication starts.
    // The retained committed token must reject visibility, stay explicit-retry
    // only, and never re-enter the compiler until a genuinely newer cook exists.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E24P5PublicationFreshness");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.4 P5 create post-commit freshness roots");

        assets::AssetId p5PrimaryId;
        assets::AssetId p5DependencyId;
        Check(assets::GenerateAssetId(p5PrimaryId) && assets::GenerateAssetId(p5DependencyId),
              "E2.4 P5 generate post-commit freshness identities");
        const filesystem::AbsolutePath p5PrimaryPath = p5AssetsRoot.AddFilePath("primary.tasset");
        const filesystem::AbsolutePath p5DependencyPath = p5AssetsRoot.AddFilePath("dependency.tasset");
        Check(WriteTextAsset(files, p5PrimaryPath, containers::StringView("p5-fresh-a\n"), Metadata(p5PrimaryId)) &&
                  WriteTextAsset(files, p5DependencyPath, containers::StringView("p5-fresh-dependency\n"), Metadata(p5DependencyId)),
              "E2.4 P5 write post-commit freshness sources");

        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P5 scan post-commit freshness sources");

        RequestResolveBlockState p5ResolveBlock;
        p5ResolveBlock.block.SetValue(false);
        p5ResolveBlock.started.SetValue(false);
        p5ResolveBlock.release.SetValue(false);
        p5ResolveBlock.fail.SetValue(false);
        p5ResolveBlock.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p5Importer = importer;
        p5Importer.sourceType = resources::InvalidResourceTypeId;
        p5Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p5Importer.userData = &p5ResolveBlock;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5Importers(memory::pools::Assets::GetInstance());
        p5Importers.PushBack(p5Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5ImporterSpan(p5Importers.TypedData(), p5Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5ImporterSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 initial freshness cook commits exact tokens");
        for (u32 pass = 0; pass < 4u && p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs != 0u; ++pass)
            Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
                  "E2.4 P5 initial freshness descriptors publish");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5Baseline;
        Check(p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5Baseline) &&
                  p5Baseline.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.4 P5 post-commit freshness fixture establishes published A");
        asset_streaming::ProjectAssetResidentDeliveryEvidence p5Resident;
        p5Resident.resource = OutputReference(p5PrimaryId);
        p5Resident.resourceGeneration = 101u;
        p5Resident.buildFingerprint = p5Baseline.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5Baseline.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.4 P5 post-commit freshness fixture establishes resident A");

        const assets::SourceRecord* const p5PrimarySource = p5Sources.LookupAsset(p5PrimaryId);
        Check(p5PrimarySource != nullptr, "E2.4 P5 post-commit freshness primary source resolves");
        block.dependencyOwnerSource = p5PrimarySource != nullptr
                                          ? resources::ResourceReference(p5PrimarySource->sourceIdentity, TestSourceType)
                                          : resources::ResourceReference{};
        block.dependencyOutput = OutputReference(p5DependencyId);

        const auto WaitForResolveGate = [&p5ResolveBlock]() noexcept
        {
            for (u32 spin = 0; spin < 2000000u && !p5ResolveBlock.started.GetValue(); ++spin)
                concurrency::YieldCurrentThread();
            return p5ResolveBlock.started.GetValue();
        };
        const auto ArmResolveGate = [&p5ResolveBlock]() noexcept
        {
            p5ResolveBlock.resolveCalls.SetValue(0u);
            p5ResolveBlock.started.SetValue(false);
            p5ResolveBlock.release.SetValue(false);
            p5ResolveBlock.blockOnCall = 1u;
            p5ResolveBlock.block.SetValue(true);
        };
        const auto DisarmResolveGate = [&p5ResolveBlock]() noexcept
        {
            p5ResolveBlock.block.SetValue(false);
            p5ResolveBlock.blockOnCall = 0u;
            p5ResolveBlock.release.SetValue(false);
        };

        // Settings drift after B is already committed.
        Check(WriteTextAsset(files, p5PrimaryPath, containers::StringView("p5-fresh-settings-b\n"), Metadata(p5PrimaryId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 settings fixture commits B before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SettingsCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SettingsCommitted) &&
                  p5SettingsCommitted.committedBuildFingerprint != p5Baseline.publishedBuildFingerprint &&
                  p5SettingsCommitted.publishedBuildFingerprint == p5Baseline.publishedBuildFingerprint &&
                  p5SettingsCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5SettingsCommitted.residentResourceGeneration == 101u,
              "E2.4 P5 settings fixture retains published/resident A while committed B waits");
        const u32 p5SettingsCompileCalls = block.compileCalls.GetValue();
        ArmResolveGate();
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && WaitForResolveGate(),
              "E2.4 P5 settings publication reaches deterministic post-commit Resolve gate");
        assets::AssetMetadata p5SettingsDrift = Metadata(p5PrimaryId);
        p5SettingsDrift.settings[0] = 11u;
        Check(WriteMetadata(files, p5PrimaryPath, p5SettingsDrift), "E2.4 P5 mutate settings only after B commit");
        p5ResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P5 post-commit settings drift rejects descriptor visibility");
        DisarmResolveGate();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SettingsRejected;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SettingsRejected) &&
                  p5SettingsRejected.committedBuildFingerprint == p5SettingsCommitted.committedBuildFingerprint &&
                  p5SettingsRejected.committedContentFingerprint == p5SettingsCommitted.committedContentFingerprint &&
                  p5SettingsRejected.publishedBuildFingerprint == p5Baseline.publishedBuildFingerprint &&
                  p5SettingsRejected.publishedContentFingerprint == p5Baseline.publishedContentFingerprint &&
                  p5SettingsRejected.publishedPhysicalPath == p5Baseline.publishedPhysicalPath &&
                  p5SettingsRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5SettingsRejected.residentResourceGeneration == 101u && p5SettingsRejected.publicationRetryRequired &&
                  p5SettingsRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFreshnessRejected &&
                  p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5SettingsCompileCalls,
              "E2.4 P5 settings rejection keeps committed B plus published/resident A sticky without compiler spin");
        Check(WriteMetadata(files, p5PrimaryPath, Metadata(p5PrimaryId)) &&
                  p5Pipeline.RetryAutomaticPublication(OutputReference(p5PrimaryId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5SettingsCompileCalls,
              "E2.4 P5 settings rejection retries the retained committed artifact without recompiling");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SettingsPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SettingsPublished) &&
                  p5SettingsPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5SettingsPublished.publishedBuildFingerprint == p5SettingsPublished.committedBuildFingerprint &&
                  p5SettingsPublished.publishedContentFingerprint == p5SettingsPublished.committedContentFingerprint &&
                  !p5SettingsPublished.publishedPhysicalPath.Empty(),
              "E2.4 P5 settings retry publishes committed B");
        p5Resident.resourceGeneration = 102u;
        p5Resident.buildFingerprint = p5SettingsPublished.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5SettingsPublished.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.4 P5 settings retry advances resident baseline for next case");

        // Importer-version drift after C is already committed.
        Check(WriteTextAsset(files, p5PrimaryPath, containers::StringView("p5-fresh-importer-c\n"), Metadata(p5PrimaryId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 importer fixture commits C before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ImporterCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5ImporterCommitted) &&
                  p5ImporterCommitted.committedBuildFingerprint != p5SettingsPublished.publishedBuildFingerprint &&
                  p5ImporterCommitted.publishedBuildFingerprint == p5SettingsPublished.publishedBuildFingerprint &&
                  p5ImporterCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5ImporterCommitted.residentResourceGeneration == 102u,
              "E2.4 P5 importer fixture retains prior published/resident generation while committed C waits");
        const u32 p5ImporterCompileCalls = block.compileCalls.GetValue();
        ArmResolveGate();
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && WaitForResolveGate(),
              "E2.4 P5 importer publication reaches deterministic post-commit Resolve gate");
        assets::AssetMetadata p5ImporterDrift = Metadata(p5PrimaryId);
        p5ImporterDrift.importerVersion = TestImporterVersion + 1u;
        Check(WriteMetadata(files, p5PrimaryPath, p5ImporterDrift), "E2.4 P5 mutate importer version only after C commit");
        p5ResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P5 post-commit importer drift rejects descriptor visibility");
        DisarmResolveGate();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ImporterRejected;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5ImporterRejected) &&
                  p5ImporterRejected.committedBuildFingerprint == p5ImporterCommitted.committedBuildFingerprint &&
                  p5ImporterRejected.committedContentFingerprint == p5ImporterCommitted.committedContentFingerprint &&
                  p5ImporterRejected.publishedBuildFingerprint == p5SettingsPublished.publishedBuildFingerprint &&
                  p5ImporterRejected.publishedContentFingerprint == p5SettingsPublished.publishedContentFingerprint &&
                  p5ImporterRejected.publishedPhysicalPath == p5SettingsPublished.publishedPhysicalPath &&
                  p5ImporterRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5ImporterRejected.residentResourceGeneration == 102u && p5ImporterRejected.publicationRetryRequired &&
                  p5ImporterRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFreshnessRejected &&
                  p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5ImporterCompileCalls,
              "E2.4 P5 importer rejection keeps committed C plus prior published/resident truth sticky without compiler spin");
        Check(WriteMetadata(files, p5PrimaryPath, Metadata(p5PrimaryId)) &&
                  p5Pipeline.RetryAutomaticPublication(OutputReference(p5PrimaryId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5ImporterCompileCalls,
              "E2.4 P5 importer rejection retries retained committed C without recompiling");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ImporterPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5ImporterPublished) &&
                  p5ImporterPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5ImporterPublished.publishedBuildFingerprint == p5ImporterPublished.committedBuildFingerprint &&
                  p5ImporterPublished.publishedContentFingerprint == p5ImporterPublished.committedContentFingerprint &&
                  !p5ImporterPublished.publishedPhysicalPath.Empty(),
              "E2.4 P5 importer retry publishes committed C");
        p5Resident.resourceGeneration = 103u;
        p5Resident.buildFingerprint = p5ImporterPublished.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5ImporterPublished.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.4 P5 importer retry advances resident baseline for dependency case");

        // Dependency-plan drift after D is already committed. The dependency has
        // durable VADI/publication truth; toggling discovery at the Resolve gate
        // changes only the current canonical plan, not source bytes or metadata.
        block.dependencyEnabled.SetValue(false);
        Check(WriteTextAsset(files, p5PrimaryPath, containers::StringView("p5-fresh-dependency-d\n"), Metadata(p5PrimaryId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 dependency fixture commits D before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5DependencyCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5DependencyCommitted) &&
                  p5DependencyCommitted.committedBuildFingerprint != p5ImporterPublished.publishedBuildFingerprint &&
                  p5DependencyCommitted.publishedBuildFingerprint == p5ImporterPublished.publishedBuildFingerprint &&
                  p5DependencyCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5DependencyCommitted.residentResourceGeneration == 103u,
              "E2.4 P5 dependency fixture retains prior published/resident generation while committed D waits");
        const u32 p5DependencyCompileCalls = block.compileCalls.GetValue();
        ArmResolveGate();
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && WaitForResolveGate(),
              "E2.4 P5 dependency publication reaches deterministic post-commit Resolve gate");
        block.dependencyEnabled.SetValue(true);
        p5ResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P5 post-commit dependency-plan drift rejects descriptor visibility");
        DisarmResolveGate();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5DependencyRejected;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5DependencyRejected) &&
                  p5DependencyRejected.committedBuildFingerprint == p5DependencyCommitted.committedBuildFingerprint &&
                  p5DependencyRejected.committedContentFingerprint == p5DependencyCommitted.committedContentFingerprint &&
                  p5DependencyRejected.publishedBuildFingerprint == p5ImporterPublished.publishedBuildFingerprint &&
                  p5DependencyRejected.publishedContentFingerprint == p5ImporterPublished.publishedContentFingerprint &&
                  p5DependencyRejected.publishedPhysicalPath == p5ImporterPublished.publishedPhysicalPath &&
                  p5DependencyRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5DependencyRejected.residentResourceGeneration == 103u && p5DependencyRejected.publicationRetryRequired &&
                  p5DependencyRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFreshnessRejected &&
                  p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5DependencyCompileCalls,
              "E2.4 P5 dependency rejection keeps committed D plus prior published/resident truth sticky without compiler spin");
        block.dependencyEnabled.SetValue(false);
        Check(p5Pipeline.RetryAutomaticPublication(OutputReference(p5PrimaryId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5DependencyCompileCalls,
              "E2.4 P5 dependency rejection retries retained committed D without recompiling");

        asset_streaming::ProjectAssetOutputDeliveryStatus p5DependencyPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5DependencyPublished) &&
                  p5DependencyPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5DependencyPublished.publishedBuildFingerprint == p5DependencyPublished.committedBuildFingerprint &&
                  p5DependencyPublished.publishedContentFingerprint == p5DependencyPublished.committedContentFingerprint,
              "E2.4 P5 dependency retry publishes exact committed D before source-drift case");
        p5Resident.resourceGeneration = 104u;
        p5Resident.buildFingerprint = p5DependencyPublished.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5DependencyPublished.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident),
              "E2.4 P5 dependency retry advances resident baseline for source-drift case");

        // Source-byte drift after E is committed but before publication. Keep metadata
        // identical so the canonical Resolve succeeds and SameBuildEvidence itself must
        // reject the changed source-input fingerprint.
        constexpr containers::StringView p5CommittedSourceBytes("p5-fresh-source-e\n");
        Check(WriteTextAsset(files, p5PrimaryPath, p5CommittedSourceBytes, Metadata(p5PrimaryId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 source-drift fixture commits E before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SourceCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SourceCommitted) &&
                  p5SourceCommitted.committedBuildFingerprint != p5DependencyPublished.publishedBuildFingerprint &&
                  p5SourceCommitted.publishedBuildFingerprint == p5DependencyPublished.publishedBuildFingerprint &&
                  p5SourceCommitted.publishedContentFingerprint == p5DependencyPublished.publishedContentFingerprint &&
                  p5SourceCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5SourceCommitted.residentResourceGeneration == 104u,
              "E2.4 P5 source-drift fixture retains published/resident D while committed E waits");
        const u32 p5SourceCompileCalls = block.compileCalls.GetValue();
        ArmResolveGate();
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending && WaitForResolveGate(),
              "E2.4 P5 source publication reaches deterministic post-commit Resolve gate");
        Check(WriteTextAsset(files, p5PrimaryPath, containers::StringView("p5-fresh-source-external-drift\n"), Metadata(p5PrimaryId)),
              "E2.4 P5 mutate source bytes only after E commit");
        p5ResolveBlock.release.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P5 post-commit source-byte drift rejects descriptor visibility");
        DisarmResolveGate();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SourceRejected;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SourceRejected) &&
                  p5SourceRejected.committedBuildFingerprint == p5SourceCommitted.committedBuildFingerprint &&
                  p5SourceRejected.committedContentFingerprint == p5SourceCommitted.committedContentFingerprint &&
                  p5SourceRejected.publishedBuildFingerprint == p5DependencyPublished.publishedBuildFingerprint &&
                  p5SourceRejected.publishedContentFingerprint == p5DependencyPublished.publishedContentFingerprint &&
                  p5SourceRejected.publishedPhysicalPath == p5DependencyPublished.publishedPhysicalPath &&
                  p5SourceRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5SourceRejected.residentResourceGeneration == 104u && p5SourceRejected.publicationRetryRequired &&
                  p5SourceRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::PublicationFreshnessRejected &&
                  p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5SourceCompileCalls,
              "E2.4 P5 source rejection keeps committed E plus published/resident D sticky without compiler spin");
        Check(WriteTextAsset(files, p5PrimaryPath, p5CommittedSourceBytes, Metadata(p5PrimaryId)) &&
                  p5Pipeline.RetryAutomaticPublication(OutputReference(p5PrimaryId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5SourceCompileCalls,
              "E2.4 P5 source rejection retries retained committed E without recompiling");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5SourcePublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5PrimaryId), p5SourcePublished) &&
                  p5SourcePublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5SourcePublished.publishedBuildFingerprint == p5SourcePublished.committedBuildFingerprint &&
                  p5SourcePublished.publishedContentFingerprint == p5SourcePublished.committedContentFingerprint,
              "E2.4 P5 restored source bytes publish exact retained committed E");

        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 post-commit freshness pipeline shuts down cleanly");
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        Check(DeleteDirectoryTree(files, p5Root), "E2.4 P5 post-commit freshness fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.5 P2 committed-artifact repair. Delete then corrupt the exact VDDC
    // generation after commit but before publication. Validation must preserve
    // committed-new plus published/resident-old truth, enter one bounded repair
    // requirement, and self-heal through the existing recooker/BuildGraph. This
    // fixture disables the BuildSystem memory cache so each repair proves the
    // ordinary compiler fallback rather than P1's retained-output rematerializer.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E24P5ArtifactFailures");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.4 P5 create committed-artifact failure roots");
        assets::AssetId p5Id;
        Check(assets::GenerateAssetId(p5Id), "E2.4 P5 generate committed-artifact failure identity");
        const filesystem::AbsolutePath p5Path = p5AssetsRoot.AddFilePath("artifact_failure.tasset");
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-artifact-a\n"), Metadata(p5Id)),
              "E2.4 P5 write committed-artifact baseline source");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P5 scan committed-artifact failure source");
        RequestResolveBlockState p5RepairResolve;
        p5RepairResolve.block.SetValue(false);
        p5RepairResolve.started.SetValue(false);
        p5RepairResolve.release.SetValue(false);
        p5RepairResolve.fail.SetValue(false);
        p5RepairResolve.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p5RepairImporter = importer;
        p5RepairImporter.sourceType = resources::InvalidResourceTypeId;
        p5RepairImporter.createBuildRequest = &CreateCallbackBuildRequest;
        p5RepairImporter.userData = &p5RepairResolve;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5RepairImporters(memory::pools::Assets::GetInstance());
        p5RepairImporters.PushBack(p5RepairImporter);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5RepairImporterSpan(
            p5RepairImporters.TypedData(), p5RepairImporters.Size());
        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;
        p5Config.build.buildSystem.maximumCacheEntries = 0u;
        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5RepairImporterSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 committed-artifact baseline cooks and publishes A");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedA;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedA) &&
                  p5PublishedA.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.4 P5 committed-artifact fixture establishes published A");
        asset_streaming::ProjectAssetResidentDeliveryEvidence p5Resident;
        p5Resident.resource = OutputReference(p5Id);
        p5Resident.resourceGeneration = 201u;
        p5Resident.buildFingerprint = p5PublishedA.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedA.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.4 P5 committed-artifact fixture establishes resident A");

        // Exact committed VDDC file is missing at publication time.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-artifact-b\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 missing-artifact fixture commits B before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5MissingCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5MissingCommitted) &&
                  p5MissingCommitted.committedBuildFingerprint != p5PublishedA.publishedBuildFingerprint &&
                  p5MissingCommitted.publishedBuildFingerprint == p5PublishedA.publishedBuildFingerprint &&
                  p5MissingCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5MissingCommitted.residentResourceGeneration == 201u,
              "E2.4 P5 missing-artifact fixture retains published/resident A while committed B waits");
        const filesystem::AbsolutePath p5MissingArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5MissingCommitted.committedBuildFingerprint);
        Check(files.DeleteFile(p5MissingArtifact),
              "E2.4 P5 delete exact committed B VDDC before publication");
        const u32 p5MissingCompileCalls = block.compileCalls.GetValue();
        Check(TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P2 missing exact committed VDDC becomes automatic repair work rather than a terminal publication failure");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5MissingRepair = p5Pipeline.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5MissingRejected;
        Check(p5MissingRepair.state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  p5MissingRepair.pendingRepairOutputs == 1u &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5MissingRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5MissingRejected) &&
                  p5MissingRejected.committedBuildFingerprint == p5MissingCommitted.committedBuildFingerprint &&
                  p5MissingRejected.committedContentFingerprint == p5MissingCommitted.committedContentFingerprint &&
                  p5MissingRejected.publishedBuildFingerprint == p5PublishedA.publishedBuildFingerprint &&
                  p5MissingRejected.publishedContentFingerprint == p5PublishedA.publishedContentFingerprint &&
                  p5MissingRejected.publishedPhysicalPath == p5PublishedA.publishedPhysicalPath &&
                  p5MissingRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5MissingRejected.residentResourceGeneration == 201u && p5MissingRejected.publicationRetryRequired &&
                  p5MissingRejected.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Stale &&
                  p5MissingRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::CacheRepairRequired &&
                  block.compileCalls.GetValue() == p5MissingCompileCalls,
              "E2.5 P2 missing-artifact repair keeps committed B plus published/resident A sticky before repair executes");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p5MissingArtifact) && block.compileCalls.GetValue() == p5MissingCompileCalls + 1u,
              "E2.5 P2 missing exact B VDDC repairs through the existing recooker/compiler path");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 repaired B delivery publishes through the existing automatic publication path");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedB;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedB) &&
                  p5PublishedB.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedB.publishedBuildFingerprint == p5PublishedB.committedBuildFingerprint &&
                  p5PublishedB.publishedContentFingerprint == p5PublishedB.committedContentFingerprint &&
                  !p5PublishedB.publishedPhysicalPath.Empty() && p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 repaired exact B VDDC restores durable/publication coherence and retires repair state");
        p5Resident.resourceGeneration = 202u;
        p5Resident.buildFingerprint = p5PublishedB.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedB.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.4 P5 missing-artifact recovery advances resident baseline");

        // Exact committed VDDC file is corrupt at publication time.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-artifact-c\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 corrupt-artifact fixture commits C before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CorruptCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5CorruptCommitted) &&
                  p5CorruptCommitted.committedBuildFingerprint != p5PublishedB.publishedBuildFingerprint &&
                  p5CorruptCommitted.publishedBuildFingerprint == p5PublishedB.publishedBuildFingerprint &&
                  p5CorruptCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5CorruptCommitted.residentResourceGeneration == 202u,
              "E2.4 P5 corrupt-artifact fixture retains published/resident B while committed C waits");
        const filesystem::AbsolutePath p5CorruptArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5CorruptCommitted.committedBuildFingerprint);
        const u8 p5CorruptByte = 0xffu;
        Check(WriteBytes(files, p5CorruptArtifact, &p5CorruptByte, 1u),
              "E2.4 P5 corrupt exact committed C VDDC before publication");
        const u32 p5CorruptCompileCalls = block.compileCalls.GetValue();
        Check(TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P2 corrupt exact committed VDDC becomes automatic repair work rather than a terminal publication failure");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5CorruptRepair = p5Pipeline.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CorruptRejected;
        Check(p5CorruptRepair.state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  p5CorruptRepair.pendingRepairOutputs == 1u &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5CorruptRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5CorruptRejected) &&
                  p5CorruptRejected.committedBuildFingerprint == p5CorruptCommitted.committedBuildFingerprint &&
                  p5CorruptRejected.committedContentFingerprint == p5CorruptCommitted.committedContentFingerprint &&
                  p5CorruptRejected.publishedBuildFingerprint == p5PublishedB.publishedBuildFingerprint &&
                  p5CorruptRejected.publishedContentFingerprint == p5PublishedB.publishedContentFingerprint &&
                  p5CorruptRejected.publishedPhysicalPath == p5PublishedB.publishedPhysicalPath &&
                  p5CorruptRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5CorruptRejected.residentResourceGeneration == 202u && p5CorruptRejected.publicationRetryRequired &&
                  p5CorruptRejected.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Stale &&
                  p5CorruptRejected.publicationReason == asset_streaming::ProjectAssetDeliveryReason::CacheRepairRequired &&
                  block.compileCalls.GetValue() == p5CorruptCompileCalls,
              "E2.5 P2 corrupt-artifact repair keeps committed C plus published/resident B sticky before repair executes");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p5CorruptArtifact) && block.compileCalls.GetValue() == p5CorruptCompileCalls + 1u,
              "E2.5 P2 corrupt exact C VDDC repairs through the existing recooker/compiler path");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 repaired C delivery publishes through the existing automatic publication path");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedC;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedC) &&
                  p5PublishedC.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedC.publishedBuildFingerprint == p5PublishedC.committedBuildFingerprint &&
                  p5PublishedC.publishedContentFingerprint == p5PublishedC.committedContentFingerprint &&
                  !p5PublishedC.publishedPhysicalPath.Empty() && p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 repaired exact C VDDC restores durable/publication coherence and retires repair state");
        p5Resident.resourceGeneration = 203u;
        p5Resident.buildFingerprint = p5PublishedC.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedC.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.5 P2 corrupt-artifact recovery advances resident C baseline");

        // A repair compiler failure is terminal/sticky until an explicit retry.
        // The failed repair must not roll VADI back from committed D, must not
        // disturb installed/resident C, and repeated owner ticks must not spin
        // the compiler.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-artifact-d\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 failed-repair fixture commits D before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5FailedCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5FailedCommitted) &&
                  p5FailedCommitted.committedBuildFingerprint != p5PublishedC.publishedBuildFingerprint &&
                  p5FailedCommitted.publishedBuildFingerprint == p5PublishedC.publishedBuildFingerprint &&
                  p5FailedCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5FailedCommitted.residentResourceGeneration == 203u,
              "E2.5 P2 failed-repair fixture retains published/resident C while committed D waits");
        const filesystem::AbsolutePath p5FailedArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5FailedCommitted.committedBuildFingerprint);
        Check(files.DeleteFile(p5FailedArtifact) && TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P2 missing committed D becomes explicit repair work before compiler failure");
        const u32 p5FailedCompileCalls = block.compileCalls.GetValue();
        block.fail.SetValue(true);
        asset_streaming::ProjectAssetPipelineFailure p5RepairFailure;
        Check(DrainAutomaticRecookWithFailure(p5Pipeline, p5RepairFailure) ==
                  asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  block.compileCalls.GetValue() == p5FailedCompileCalls + 1u,
              "E2.5 P2 failed cache repair surfaces one ordinary recook failure");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5FailedRepair = p5Pipeline.GetAutomaticRecookStatus();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5FailedDelivery;
        Check(p5FailedRepair.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5FailedRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5FailedRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::FailedBatch) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5FailedDelivery) &&
                  p5FailedDelivery.committedBuildFingerprint == p5FailedCommitted.committedBuildFingerprint &&
                  p5FailedDelivery.committedContentFingerprint == p5FailedCommitted.committedContentFingerprint &&
                  p5FailedDelivery.publishedBuildFingerprint == p5PublishedC.publishedBuildFingerprint &&
                  p5FailedDelivery.publishedContentFingerprint == p5PublishedC.publishedContentFingerprint &&
                  p5FailedDelivery.publishedPhysicalPath == p5PublishedC.publishedPhysicalPath &&
                  p5FailedDelivery.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5FailedDelivery.residentResourceGeneration == 203u && p5FailedDelivery.publicationRetryRequired &&
                  p5FailedDelivery.publicationReason == asset_streaming::ProjectAssetDeliveryReason::CacheRepairRequired,
              "E2.5 P2 failed repair preserves exact committed D and last-valid published/resident C truth");
        const u32 p5StickyFailureCalls = block.compileCalls.GetValue();
        Check(p5Pipeline.TickAutomaticRecook(&p5RepairFailure) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  block.compileCalls.GetValue() == p5StickyFailureCalls,
              "E2.5 P2 failed repair is sticky and does not spin the compiler on owner ticks");
        block.fail.SetValue(false);
        Check(p5Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p5FailedArtifact) && block.compileCalls.GetValue() == p5StickyFailureCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 explicit retry reuses the existing recovery/recook path and repairs D once");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedD;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedD) &&
                  p5PublishedD.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedD.publishedBuildFingerprint == p5PublishedD.committedBuildFingerprint &&
                  p5PublishedD.publishedContentFingerprint == p5PublishedD.committedContentFingerprint &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 successful retry restores D durable/publication coherence");

        // Restart/restoration must validate the exact VADI-referenced VDDC row
        // before treating it as reusable publication truth. Delete D after a clean
        // shutdown so the restarted BuildSystem has neither a memory-cache entry
        // nor a valid persistent record to hide the repair path.
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success && files.DeleteFile(p5FailedArtifact),
              "E2.5 P2 restart fixture removes exact committed D VDDC while pipeline is down");
        const u32 p5RestartCompileCalls = block.compileCalls.GetValue();
        asset_streaming::ProjectAssetRestoreReport p5RestartRestore;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5RepairImporterSpan, p5Config, &p5RestartRestore) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5RestartRestore.repairRequiredRecords == 1u && p5RestartRestore.restored == 0u &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 1u &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired),
              "E2.5 P2 restart validates VADI-referenced VDDC and restores explicit repair-required state");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p5FailedArtifact) && block.compileCalls.GetValue() == p5RestartCompileCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 restart repair recompiles once through the existing automatic recooker and republishes D");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5RestartPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5RestartPublished) &&
                  p5RestartPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5RestartPublished.publishedBuildFingerprint == p5RestartPublished.committedBuildFingerprint &&
                  p5RestartPublished.publishedContentFingerprint == p5RestartPublished.committedContentFingerprint &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 restart repair re-establishes exact VDDC/VADI/publication coherence");

        // Manual reconciliation must revalidate an already-installed current
        // record. Force canonical repair preparation to fail while automatic
        // recook is disabled: the coordinator must stay disabled and retain only
        // recovery-required evidence. Re-enable then performs authoritative
        // VADI->VDDC recovery, reconstructs the lost repair identity, and repairs
        // the exact record through the existing recooker once.
        Check(p5Pipeline.DisableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  !p5Pipeline.IsAutomaticRecookEnabled() && files.DeleteFile(p5FailedArtifact),
              "E2.5 P2 manual-reconcile recovery fixture disables automatic recook and removes installed D VDDC");
        const u32 p5ManualRecoveryCompileCalls = block.compileCalls.GetValue();
        p5RepairResolve.fail.SetValue(true);
        asset_streaming::ProjectAssetRestoreReport p5ManualRepairReport;
        Check(p5Pipeline.Reconcile(&p5ManualRepairReport) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5ManualRepairReport.repairRequiredRecords == 1u &&
                  !p5Pipeline.IsAutomaticRecookEnabled() &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Disabled &&
                  p5Pipeline.GetAutomaticRecookStatus().recoveryRequired &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                      asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired) &&
                  block.compileCalls.GetValue() == p5ManualRecoveryCompileCalls,
              "E2.5 P2 manual reconcile validates installed exact VDDC and failed repair preparation preserves disabled state");
        p5RepairResolve.fail.SetValue(false);
        Check(p5Pipeline.EnableAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.IsAutomaticRecookEnabled() &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p5FailedArtifact) && block.compileCalls.GetValue() == p5ManualRecoveryCompileCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 authoritative recovery reconstructs lost repair identity and repairs installed D exactly once");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ManualRecoveryPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5ManualRecoveryPublished) &&
                  p5ManualRecoveryPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5ManualRecoveryPublished.publishedBuildFingerprint == p5ManualRecoveryPublished.committedBuildFingerprint &&
                  p5ManualRecoveryPublished.publishedContentFingerprint == p5ManualRecoveryPublished.committedContentFingerprint &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 recovered manual repair restores exact current publication coherence");

        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 committed-artifact repair pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.5 P2 committed-artifact repair fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.5 P5 committed-artifact repair matrix. Exercise wrong build identity,
    // wrong content identity, repair cancellation, and stale repair completion
    // through the same VADI -> VDDC validation and automatic-recooker seam.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E25P5RepairMatrix");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.5 P5 repair-matrix roots are created");

        assets::AssetId p5Id;
        Check(assets::GenerateAssetId(p5Id), "E2.5 P5 repair-matrix identity is generated");
        const filesystem::AbsolutePath p5Path = p5AssetsRoot.AddFilePath("repair_matrix.tasset");
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-repair-a\n"), Metadata(p5Id)),
              "E2.5 P5 repair-matrix baseline source is written");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 repair-matrix source catalog rescans");

        RequestResolveBlockState p5Resolve;
        p5Resolve.block.SetValue(false);
        p5Resolve.started.SetValue(false);
        p5Resolve.release.SetValue(false);
        p5Resolve.fail.SetValue(false);
        p5Resolve.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p5Importer = importer;
        p5Importer.sourceType = resources::InvalidResourceTypeId;
        p5Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p5Importer.userData = &p5Resolve;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5Importers(memory::pools::Assets::GetInstance());
        p5Importers.PushBack(p5Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5ImporterSpan(
            p5Importers.TypedData(), p5Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;
        p5Config.build.buildSystem.maximumCacheEntries = 0u;
        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};

        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5ImporterSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 repair-matrix baseline A cooks and publishes");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedA;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedA) &&
                  p5PublishedA.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.5 P5 repair-matrix establishes published A");
        asset_streaming::ProjectAssetResidentDeliveryEvidence p5Resident;
        p5Resident.resource = OutputReference(p5Id);
        p5Resident.resourceGeneration = 301u;
        p5Resident.buildFingerprint = p5PublishedA.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedA.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.5 P5 repair-matrix establishes resident A");
        containers::DynamicArray<u8> p5RecordA(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, PersistentArtifactPath(p5DerivedRoot, p5PublishedA.committedBuildFingerprint), p5RecordA) &&
                  !p5RecordA.Empty(),
              "E2.5 P5 capture valid A VDDC bytes for wrong-key corruption");

        // Store a valid A record under B's canonical build path. The path is
        // canonical, but the embedded build identity is wrong.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-repair-b\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 wrong-key fixture commits B before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5WrongKeyCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5WrongKeyCommitted) &&
                  p5WrongKeyCommitted.committedBuildFingerprint != p5PublishedA.publishedBuildFingerprint &&
                  p5WrongKeyCommitted.publishedBuildFingerprint == p5PublishedA.publishedBuildFingerprint &&
                  p5WrongKeyCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5WrongKeyCommitted.residentResourceGeneration == 301u,
              "E2.5 P5 wrong-key fixture preserves last-valid A while B waits");
        const filesystem::AbsolutePath p5WrongKeyArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5WrongKeyCommitted.committedBuildFingerprint);
        Check(WriteBytes(files, p5WrongKeyArtifact, p5RecordA.TypedData(), p5RecordA.Size()) &&
                  TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P5 current B VDDC with wrong embedded build key becomes repair work");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5WrongKeyRejected;
        Check(p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 1u &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5WrongKeyRejected) &&
                  p5WrongKeyRejected.committedBuildFingerprint == p5WrongKeyCommitted.committedBuildFingerprint &&
                  p5WrongKeyRejected.committedContentFingerprint == p5WrongKeyCommitted.committedContentFingerprint &&
                  p5WrongKeyRejected.publishedBuildFingerprint == p5PublishedA.publishedBuildFingerprint &&
                  p5WrongKeyRejected.publishedPhysicalPath == p5PublishedA.publishedPhysicalPath &&
                  p5WrongKeyRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5WrongKeyRejected.residentResourceGeneration == 301u,
              "E2.5 P5 wrong-key repair preserves committed B and last-valid descriptor/resident A");
        const u32 p5WrongKeyCompileCalls = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5WrongKeyCompileCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 wrong-key current VDDC repairs through the existing recooker");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedB;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedB) &&
                  p5PublishedB.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedB.publishedBuildFingerprint == p5PublishedB.committedBuildFingerprint &&
                  p5PublishedB.publishedContentFingerprint == p5PublishedB.committedContentFingerprint &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P5 wrong-key repair restores exact B VDDC/publication coherence");
        p5Resident.resourceGeneration = 302u;
        p5Resident.buildFingerprint = p5PublishedB.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedB.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.5 P5 wrong-key recovery advances resident B");

        // Keep C's build identity intact but change the embedded content key.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-repair-c\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 wrong-content fixture commits C before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5WrongContentCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5WrongContentCommitted) &&
                  p5WrongContentCommitted.publishedBuildFingerprint == p5PublishedB.publishedBuildFingerprint &&
                  p5WrongContentCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5WrongContentCommitted.residentResourceGeneration == 302u,
              "E2.5 P5 wrong-content fixture preserves last-valid B while C waits");
        const filesystem::AbsolutePath p5WrongContentArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5WrongContentCommitted.committedBuildFingerprint);
        containers::DynamicArray<u8> p5WrongContentBytes(memory::pools::Assets::GetInstance());
        constexpr u32 p5ContentFingerprintOffset =
            sizeof(u32) + sizeof(u16) + sizeof(u16) + sizeof(u32) + sizeof(u32) + sizeof(u64) +
            assets::BuildFingerprint::ByteCount;
        Check(ReadBytes(files, p5WrongContentArtifact, p5WrongContentBytes) &&
                  p5WrongContentBytes.Size() > p5ContentFingerprintOffset,
              "E2.5 P5 read C VDDC before content-key corruption");
        if (p5WrongContentBytes.Size() > p5ContentFingerprintOffset)
            p5WrongContentBytes[p5ContentFingerprintOffset] ^= 0xffu;
        Check(WriteBytes(files, p5WrongContentArtifact, p5WrongContentBytes.TypedData(), p5WrongContentBytes.Size()) &&
                  TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P5 current C VDDC with wrong content key becomes repair work");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5WrongContentRejected;
        Check(p5Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 1u &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5WrongContentRejected) &&
                  p5WrongContentRejected.committedBuildFingerprint == p5WrongContentCommitted.committedBuildFingerprint &&
                  p5WrongContentRejected.committedContentFingerprint == p5WrongContentCommitted.committedContentFingerprint &&
                  p5WrongContentRejected.publishedBuildFingerprint == p5PublishedB.publishedBuildFingerprint &&
                  p5WrongContentRejected.publishedPhysicalPath == p5PublishedB.publishedPhysicalPath &&
                  p5WrongContentRejected.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5WrongContentRejected.residentResourceGeneration == 302u,
              "E2.5 P5 wrong-content repair preserves committed C and last-valid descriptor/resident B");
        const u32 p5WrongContentCompileCalls = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5WrongContentCompileCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 wrong-content current VDDC repairs through the existing recooker");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedC;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedC) &&
                  p5PublishedC.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedC.publishedBuildFingerprint == p5PublishedC.committedBuildFingerprint &&
                  p5PublishedC.publishedContentFingerprint == p5PublishedC.committedContentFingerprint,
              "E2.5 P5 wrong-content repair restores exact C VDDC/publication coherence");
        p5Resident.resourceGeneration = 303u;
        p5Resident.buildFingerprint = p5PublishedC.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedC.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.5 P5 wrong-content recovery advances resident C");

        const auto waitForRepairCompile = [&block]() noexcept
        {
            for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
                concurrency::YieldCurrentThread();
            return block.started.GetValue();
        };

        // Cancellation of a repair build is sticky until explicit retry and
        // cannot roll committed D back or advance descriptor/resident C.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-repair-d\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 repair-cancel fixture commits D before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CancelledCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5CancelledCommitted) &&
                  p5CancelledCommitted.publishedBuildFingerprint == p5PublishedC.publishedBuildFingerprint &&
                  p5CancelledCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5CancelledCommitted.residentResourceGeneration == 303u,
              "E2.5 P5 repair-cancel fixture preserves last-valid C while D waits");
        const filesystem::AbsolutePath p5CancelledArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5CancelledCommitted.committedBuildFingerprint);
        Check(files.DeleteFile(p5CancelledArtifact) && TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P5 missing committed D enters repair before cancellation");
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  waitForRepairCompile(),
              "E2.5 P5 repair-cancel build reaches deterministic compiler barrier");
        Check(p5Pipeline.CancelAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Cancelling,
              "E2.5 P5 active repair accepts cancellation");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.5 P5 cancelled repair becomes sticky retained failure");
        block.block = false;
        block.release.SetValue(false);
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CancelledDelivery;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5CancelledRepair = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5CancelledRepair.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5CancelledRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5CancelledRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::CancelledBatch) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5CancelledDelivery) &&
                  p5CancelledDelivery.committedBuildFingerprint == p5CancelledCommitted.committedBuildFingerprint &&
                  p5CancelledDelivery.committedContentFingerprint == p5CancelledCommitted.committedContentFingerprint &&
                  p5CancelledDelivery.publishedBuildFingerprint == p5PublishedC.publishedBuildFingerprint &&
                  p5CancelledDelivery.publishedPhysicalPath == p5PublishedC.publishedPhysicalPath &&
                  p5CancelledDelivery.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5CancelledDelivery.residentResourceGeneration == 303u,
              "E2.5 P5 cancelled repair preserves committed D and last-valid descriptor/resident C");
        const u32 p5CancelledCompileCalls = block.compileCalls.GetValue();
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  block.compileCalls.GetValue() == p5CancelledCompileCalls,
              "E2.5 P5 cancelled repair does not auto-retry");
        Check(p5Pipeline.RetryAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5CancelledCompileCalls + 1u &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 cancelled repair requires explicit retry and then repairs D once");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5PublishedD;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5PublishedD) &&
                  p5PublishedD.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5PublishedD.publishedBuildFingerprint == p5PublishedD.committedBuildFingerprint,
              "E2.5 P5 cancelled-repair retry publishes exact D");
        p5Resident.resourceGeneration = 304u;
        p5Resident.buildFingerprint = p5PublishedD.publishedBuildFingerprint;
        p5Resident.contentFingerprint = p5PublishedD.publishedContentFingerprint;
        Check(p5Pipeline.ReportResidentDelivery(p5Resident), "E2.5 P5 cancelled-repair retry advances resident D");

        // A repair that completes against changed source metadata is stale. It
        // must leave committed E and last-valid D visible until the changed disk
        // state is accepted as a newer generation.
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-repair-e\n"), Metadata(p5Id)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 repair-stale fixture commits E before publication");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5StaleCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5StaleCommitted) &&
                  p5StaleCommitted.publishedBuildFingerprint == p5PublishedD.publishedBuildFingerprint &&
                  p5StaleCommitted.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5StaleCommitted.residentResourceGeneration == 304u,
              "E2.5 P5 repair-stale fixture preserves last-valid D while E waits");
        const filesystem::AbsolutePath p5StaleArtifact =
            PersistentArtifactPath(p5DerivedRoot, p5StaleCommitted.committedBuildFingerprint);
        Check(files.DeleteFile(p5StaleArtifact) && TickUntilCacheRepairRequired(p5Pipeline),
              "E2.5 P5 missing committed E enters repair before stale-completion race");
        const u64 p5StaleAcceptedGeneration = p5Pipeline.GetAcceptedAssetGeneration();
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  waitForRepairCompile(),
              "E2.5 P5 repair-stale build reaches deterministic compiler barrier");
        assets::AssetMetadata p5NewerMetadata = Metadata(p5Id);
        p5NewerMetadata.settings[0] = 0x5au;
        Check(WriteMetadata(files, p5Path, p5NewerMetadata),
              "E2.5 P5 mutate source settings while repair compile is live");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
              "E2.5 P5 repair completion against changed source truth is rejected as stale");
        block.block = false;
        block.release.SetValue(false);
        asset_streaming::ProjectAssetOutputDeliveryStatus p5StaleDelivery;
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5StaleRepair = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5StaleRepair.state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5StaleRepair.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion) &&
                  p5Pipeline.GetAcceptedAssetGeneration() == p5StaleAcceptedGeneration &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5StaleDelivery) &&
                  p5StaleDelivery.committedBuildFingerprint == p5StaleCommitted.committedBuildFingerprint &&
                  p5StaleDelivery.committedContentFingerprint == p5StaleCommitted.committedContentFingerprint &&
                  p5StaleDelivery.publishedBuildFingerprint == p5PublishedD.publishedBuildFingerprint &&
                  p5StaleDelivery.publishedPhysicalPath == p5PublishedD.publishedPhysicalPath &&
                  p5StaleDelivery.residentState == asset_streaming::ProjectAssetDeliveryStageState::Current &&
                  p5StaleDelivery.residentResourceGeneration == 304u,
              "E2.5 P5 stale repair preserves committed E and last-valid descriptor/resident D");
        Check(p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAcceptedAssetGeneration() > p5StaleAcceptedGeneration &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 newer accepted source state supersedes stale repair work and recooks current truth");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5NewerPublished;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5NewerPublished) &&
                  p5NewerPublished.committedBuildFingerprint != p5StaleCommitted.committedBuildFingerprint &&
                  p5NewerPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5NewerPublished.publishedBuildFingerprint == p5NewerPublished.committedBuildFingerprint &&
                  p5NewerPublished.publishedContentFingerprint == p5NewerPublished.committedContentFingerprint,
              "E2.5 P5 stale repair advances only after newer accepted truth produces a new exact key");

        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 repair-matrix pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.5 P5 repair-matrix fixture cleanup succeeds");
    }

    // P5 exact key invalidation for importer and compiler versions. Keep source
    // bytes fixed, then change one version input at a time and compare the
    // committed VADI key produced through the real project pipeline.
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E25P5ExactKeyInvalidation");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.5 P5 exact-key fixture roots are created");

        assets::AssetId p5Id;
        Check(assets::GenerateAssetId(p5Id), "E2.5 P5 exact-key identity is generated");
        const filesystem::AbsolutePath p5Path = p5AssetsRoot.AddFilePath("exact_key.tasset");
        Check(WriteTextAsset(files, p5Path, containers::StringView("p5-exact-key-source\n"), Metadata(p5Id)),
              "E2.5 P5 exact-key baseline source is written");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 exact-key baseline source rescans");

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = false;

        RequestResolveBlockState p5Resolve;
        p5Resolve.block.SetValue(false);
        p5Resolve.started.SetValue(false);
        p5Resolve.release.SetValue(false);
        p5Resolve.fail.SetValue(false);
        p5Resolve.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p5ImporterV1 = importer;
        p5ImporterV1.sourceType = resources::InvalidResourceTypeId;
        p5ImporterV1.createBuildRequest = &CreateCallbackBuildRequest;
        p5ImporterV1.userData = &p5Resolve;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5ImportersV1(memory::pools::Assets::GetInstance());
        p5ImportersV1.PushBack(p5ImporterV1);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5ImporterV1Span(
            p5ImportersV1.TypedData(), p5ImportersV1.Size());

        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5ImporterV1Span, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key baseline pipeline initializes");
        asset_streaming::ProjectAssetRequest p5BaselineRequest = p5Pipeline.Request(OutputReference(p5Id));
        Check(p5BaselineRequest.IsValid(), "E2.5 P5 exact-key baseline request starts");
        p5BaselineRequest.Wait();
        Check(p5BaselineRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key baseline request publishes");
        p5BaselineRequest.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5Baseline;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5Baseline) &&
                  !p5Baseline.committedBuildFingerprint.IsEmpty(),
              "E2.5 P5 exact-key baseline captures committed key");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key baseline pipeline shuts down");

        assets::AssetMetadata p5ImporterMetadata = Metadata(p5Id);
        p5ImporterMetadata.importerVersion = TestImporterVersion + 1u;
        Check(WriteMetadata(files, p5Path, p5ImporterMetadata) &&
                  p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P5 exact-key importer version advances with source bytes unchanged");
        assets::SourceImporterDescriptor p5ImporterV2 = p5ImporterV1;
        p5ImporterV2.version = TestImporterVersion + 1u;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5ImportersV2(memory::pools::Assets::GetInstance());
        p5ImportersV2.PushBack(p5ImporterV2);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5ImporterV2Span(
            p5ImportersV2.TypedData(), p5ImportersV2.Size());
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5ImporterV2Span, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key importer-v2 pipeline initializes");
        asset_streaming::ProjectAssetRequest p5ImporterRequest = p5Pipeline.Request(OutputReference(p5Id));
        Check(p5ImporterRequest.IsValid(), "E2.5 P5 exact-key importer-v2 request starts");
        p5ImporterRequest.Wait();
        Check(p5ImporterRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key importer-v2 request publishes");
        p5ImporterRequest.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ImporterKey;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5ImporterKey) &&
                  p5ImporterKey.committedBuildFingerprint != p5Baseline.committedBuildFingerprint,
              "E2.5 P5 valid importer-version change produces an exact new committed build key");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key importer-v2 pipeline shuts down");

        assets::CompilerDescriptor p5CompilerV2 = compiler;
        p5CompilerV2.version = compiler.version + 1u;
        containers::DynamicArray<assets::CompilerDescriptor> p5CompilersV2(memory::pools::Assets::GetInstance());
        p5CompilersV2.PushBack(p5CompilerV2);
        const containers::ArraySpan<const assets::CompilerDescriptor> p5CompilerV2Span(
            p5CompilersV2.TypedData(), p5CompilersV2.Size());
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, p5CompilerV2Span, p5ImporterV2Span, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key compiler-v2 pipeline initializes");
        asset_streaming::ProjectAssetRequest p5CompilerRequest = p5Pipeline.Request(OutputReference(p5Id));
        Check(p5CompilerRequest.IsValid(), "E2.5 P5 exact-key compiler-v2 request starts");
        p5CompilerRequest.Wait();
        Check(p5CompilerRequest.Poll() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key compiler-v2 request publishes");
        p5CompilerRequest.Reset();
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CompilerKey;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5Id), p5CompilerKey) &&
                  p5CompilerKey.committedBuildFingerprint != p5ImporterKey.committedBuildFingerprint,
              "E2.5 P5 compiler-version-only change produces an exact new committed build key");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P5 exact-key compiler-v2 pipeline shuts down");
        Check(DeleteDirectoryTree(files, p5Root), "E2.5 P5 exact-key fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.5 P2 forced repair roots are mandatory even when committed VADI still
    // contains an old dependant edge. Baseline D depends on F. Then D's current
    // request drops F while D is dirty and F's exact VDDC is removed. Repair F
    // must remain a graph root instead of being minimized away by the old D->F
    // topology.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p2Root = root.AddDirPath("E25P2ForcedRepairRoot");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.5 P2 create forced-repair-root fixture");

        assets::AssetId p2DependantId;
        assets::AssetId p2ForcedId;
        Check(assets::GenerateAssetId(p2DependantId) && assets::GenerateAssetId(p2ForcedId),
              "E2.5 P2 generate forced-repair-root identities");
        const filesystem::AbsolutePath p2DependantPath = p2AssetsRoot.AddFilePath("dependant.tasset");
        const filesystem::AbsolutePath p2ForcedPath = p2AssetsRoot.AddFilePath("forced.tasset");
        Check(WriteTextAsset(files, p2DependantPath, containers::StringView("p2-dependant-a\n"), Metadata(p2DependantId)) &&
                  WriteTextAsset(files, p2ForcedPath, containers::StringView("p2-forced-a\n"), Metadata(p2ForcedId)),
              "E2.5 P2 write forced-repair-root baseline sources");

        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.5 P2 scan forced-repair-root baseline");
        const assets::SourceRecord* const p2DependantSource = p2Sources.LookupAsset(p2DependantId);
        Check(p2DependantSource != nullptr, "E2.5 P2 resolve old dependant source identity");

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(true);
        block.dependencyOwnerSource = p2DependantSource != nullptr
                                          ? resources::ResourceReference(p2DependantSource->sourceIdentity, TestSourceType)
                                          : resources::ResourceReference{};
        block.dependencyOutput = OutputReference(p2ForcedId);
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = true;
        p2Config.build.buildSystem.maximumCacheEntries = 0u;

        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, importerSpan, p2Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 forced-repair-root baseline builds and publishes old D-to-F topology");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ForcedBaseline;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ForcedId), p2ForcedBaseline) &&
                  p2ForcedBaseline.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.5 P2 forced output baseline is installed before exact-record loss");
        const filesystem::AbsolutePath p2ForcedArtifact =
            PersistentArtifactPath(p2DerivedRoot, p2ForcedBaseline.committedBuildFingerprint);

        // Keep old VADI D->F committed, but make the current D request drop F.
        // The same accepted rescan makes D dirty and detects F's missing exact
        // record, so D and forced F enter one ordinary recook batch.
        block.dependencyEnabled.SetValue(false);
        const u32 p2ForcedCompileCalls = block.compileCalls.GetValue();
        asset_streaming::ProjectAssetRestoreReport p2RepairReport;
        Check(files.DeleteFile(p2ForcedArtifact) &&
                  WriteTextAsset(files, p2DependantPath, containers::StringView("p2-dependant-b\n"), Metadata(p2DependantId)) &&
                  p2Pipeline.Rescan(p2RootSpan, &p2RepairReport) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p2RepairReport.repairRequiredRecords == 1u &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 1u,
              "E2.5 P2 accepted current D drops old F edge while missing F becomes one forced repair");
        Check(DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  files.FileExist(p2ForcedArtifact) && block.compileCalls.GetValue() == p2ForcedCompileCalls + 2u &&
                  DrainAutomaticRecook(p2Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 forced F remains a mandatory graph root beside dirty current D and both compile once");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2ForcedRepaired;
        Check(p2Pipeline.GetOutputDeliveryStatus(OutputReference(p2ForcedId), p2ForcedRepaired) &&
                  p2ForcedRepaired.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p2ForcedRepaired.publishedBuildFingerprint == p2ForcedRepaired.committedBuildFingerprint &&
                  p2ForcedRepaired.publishedContentFingerprint == p2ForcedRepaired.committedContentFingerprint &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u,
              "E2.5 P2 forced-root repair restores exact F coherence after current D drops the old edge");

        Check(p2Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P2 forced-repair-root pipeline shuts down cleanly");
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        Check(DeleteDirectoryTree(files, p2Root), "E2.5 P2 forced-repair-root fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P5 transitive Generated publication closure: A -> B -> C. A conflict
    // on C must make the complete connected component fail before visibility;
    // after the conflict is removed, one explicit coherent retry publishes all
    // three exact committed descriptors atomically without another compile.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E24P5TransitiveGroup");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.4 P5 create transitive Generated roots");
        assets::AssetId p5AId;
        assets::AssetId p5BId;
        assets::AssetId p5CId;
        Check(assets::GenerateAssetId(p5AId) && assets::GenerateAssetId(p5BId) && assets::GenerateAssetId(p5CId),
              "E2.4 P5 generate transitive Generated identities");
        const filesystem::AbsolutePath p5APath = p5AssetsRoot.AddFilePath("a.tasset");
        const filesystem::AbsolutePath p5BPath = p5AssetsRoot.AddFilePath("b.tasset");
        const filesystem::AbsolutePath p5CPath = p5AssetsRoot.AddFilePath("c.tasset");
        Check(WriteTextAsset(files, p5APath, containers::StringView("p5-transitive-a\n"), Metadata(p5AId)) &&
                  WriteTextAsset(files, p5BPath, containers::StringView("p5-transitive-b\n"), Metadata(p5BId)) &&
                  WriteTextAsset(files, p5CPath, containers::StringView("p5-transitive-c\n"), Metadata(p5CId)),
              "E2.4 P5 write transitive Generated sources");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P5 scan transitive Generated sources");
        const assets::SourceRecord* const p5ASource = p5Sources.LookupAsset(p5AId);
        const assets::SourceRecord* const p5BSource = p5Sources.LookupAsset(p5BId);
        Check(p5ASource != nullptr && p5BSource != nullptr, "E2.4 P5 resolve transitive Generated source identities");
        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(true);
        block.dependencyOwnerSource = p5ASource != nullptr ? resources::ResourceReference(p5ASource->sourceIdentity, TestSourceType)
                                                           : resources::ResourceReference{};
        block.dependencyOutput = OutputReference(p5BId);
        block.dependencyOwnerSource2 = p5BSource != nullptr ? resources::ResourceReference(p5BSource->sourceIdentity, TestSourceType)
                                                            : resources::ResourceReference{};
        block.dependencyOutput2 = OutputReference(p5CId);
        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;
        p5Config.cacheMaintenance.maximumRetiredLooseCandidates = 3u;
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 3u,
              "E2.4 P5 transitive A-to-B-to-C build retains one three-output committed component");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ACommitted;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5BCommitted;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CCommitted;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p5ACommitted) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p5BCommitted) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5CId), p5CCommitted) &&
                  !p5ACommitted.committedBuildFingerprint.IsEmpty() && !p5ACommitted.committedContentFingerprint.IsEmpty() &&
                  !p5BCommitted.committedBuildFingerprint.IsEmpty() && !p5BCommitted.committedContentFingerprint.IsEmpty() &&
                  !p5CCommitted.committedBuildFingerprint.IsEmpty() && !p5CCommitted.committedContentFingerprint.IsEmpty() &&
                  p5ACommitted.publishedPhysicalPath.Empty() && p5BCommitted.publishedPhysicalPath.Empty() &&
                  p5CCommitted.publishedPhysicalPath.Empty(),
              "E2.4 P5 capture exact transitive committed tokens before visibility attempt");
        const u32 p5CompileCalls = block.compileCalls.GetValue();
        const resources::ResourceReference p5ConflictingC(OutputReference(p5CId).GetPath(), TestSourceType);
        Check(streamer.RegisterLoose({p5ConflictingC, p5CPath, {}, 0u, p5Config.publication.priority}),
              "E2.4 P5 install different-type conflict at transitive C before visibility");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::PublicationFailed,
              "E2.4 P5 C conflict rejects complete transitive Generated visibility group");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5ARejected;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5BRejected;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CRejected;
        streaming::ResourceSource p5UnexpectedASource;
        streaming::ResourceSource p5UnexpectedBSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p5UnexpectedADependencies(memory::pools::Streaming::GetInstance());
        containers::DynamicArray<streaming::DependencyDescriptor> p5UnexpectedBDependencies(memory::pools::Streaming::GetInstance());
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p5ARejected) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p5BRejected) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5CId), p5CRejected) &&
                  p5ARejected.committedBuildFingerprint == p5ACommitted.committedBuildFingerprint &&
                  p5ARejected.committedContentFingerprint == p5ACommitted.committedContentFingerprint &&
                  p5BRejected.committedBuildFingerprint == p5BCommitted.committedBuildFingerprint &&
                  p5BRejected.committedContentFingerprint == p5BCommitted.committedContentFingerprint &&
                  p5CRejected.committedBuildFingerprint == p5CCommitted.committedBuildFingerprint &&
                  p5CRejected.committedContentFingerprint == p5CCommitted.committedContentFingerprint &&
                  p5ARejected.publicationRetryRequired && p5BRejected.publicationRetryRequired && p5CRejected.publicationRetryRequired &&
                  p5ARejected.publishedPhysicalPath.Empty() && p5BRejected.publishedPhysicalPath.Empty() && p5CRejected.publishedPhysicalPath.Empty() &&
                  streamer.OpenSource(OutputReference(p5AId), p5UnexpectedASource, p5UnexpectedADependencies) == resources::Failure::NotFound &&
                  streamer.OpenSource(OutputReference(p5BId), p5UnexpectedBSource, p5UnexpectedBDependencies) == resources::Failure::NotFound &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 3u && block.compileCalls.GetValue() == p5CompileCalls,
              "E2.4 P5 transitive conflict exposes no A/B/C visibility prefix and retains all exact tokens");
        Check(streamer.UnregisterLoose(OutputReference(p5CId).GetPath()),
              "E2.4 P5 remove transitive C conflict before coherent retry");
        Check(p5Pipeline.RetryAutomaticPublication(OutputReference(p5AId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.RetryAutomaticPublication(OutputReference(p5BId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.RetryAutomaticPublication(OutputReference(p5CId)) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  block.compileCalls.GetValue() == p5CompileCalls,
              "E2.4 P5 complete transitive Generated component retries without compiler execution");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5APublished;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5BPublished;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5CPublished;
        streaming::ResourceSource p5APublishedSource;
        streaming::ResourceSource p5BPublishedSource;
        streaming::ResourceSource p5CPublishedSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p5APublishedDependencies(memory::pools::Streaming::GetInstance());
        containers::DynamicArray<streaming::DependencyDescriptor> p5BPublishedDependencies(memory::pools::Streaming::GetInstance());
        containers::DynamicArray<streaming::DependencyDescriptor> p5CPublishedDependencies(memory::pools::Streaming::GetInstance());
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p5APublished) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p5BPublished) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5CId), p5CPublished) &&
                  p5APublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5BPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5CPublished.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p5APublished.publishedBuildFingerprint == p5APublished.committedBuildFingerprint &&
                  p5APublished.publishedContentFingerprint == p5APublished.committedContentFingerprint &&
                  !p5APublished.publishedPhysicalPath.Empty() &&
                  p5BPublished.publishedBuildFingerprint == p5BPublished.committedBuildFingerprint &&
                  p5BPublished.publishedContentFingerprint == p5BPublished.committedContentFingerprint &&
                  !p5BPublished.publishedPhysicalPath.Empty() &&
                  p5CPublished.publishedBuildFingerprint == p5CPublished.committedBuildFingerprint &&
                  p5CPublished.publishedContentFingerprint == p5CPublished.committedContentFingerprint &&
                  !p5CPublished.publishedPhysicalPath.Empty() &&
                  streamer.OpenSource(OutputReference(p5AId), p5APublishedSource, p5APublishedDependencies) == resources::Failure::None &&
                  p5APublishedSource.GetPhysicalPath() == p5APublished.publishedPhysicalPath &&
                  streamer.OpenSource(OutputReference(p5BId), p5BPublishedSource, p5BPublishedDependencies) == resources::Failure::None &&
                  p5BPublishedSource.GetPhysicalPath() == p5BPublished.publishedPhysicalPath &&
                  streamer.OpenSource(OutputReference(p5CId), p5CPublishedSource, p5CPublishedDependencies) == resources::Failure::None &&
                  p5CPublishedSource.GetPhysicalPath() == p5CPublished.publishedPhysicalPath &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "E2.4 P5 transitive A/B/C descriptors advance together after coherent retry");
        p5APublishedSource.Close();
        p5BPublishedSource.Close();
        p5CPublishedSource.Close();

        const filesystem::AbsolutePath p4AutomaticA0 = p5APublished.publishedPhysicalPath;
        const filesystem::AbsolutePath p4AutomaticB0 = p5BPublished.publishedPhysicalPath;
        const filesystem::AbsolutePath p4AutomaticC0 = p5CPublished.publishedPhysicalPath;
        Check(WriteTextAsset(files, p5APath, containers::StringView("p5-transitive-a-p4-one\n"), Metadata(p5AId)) &&
                  WriteTextAsset(files, p5BPath, containers::StringView("p5-transitive-b-p4-one\n"), Metadata(p5BId)) &&
                  WriteTextAsset(files, p5CPath, containers::StringView("p5-transitive-c-p4-one\n"), Metadata(p5CId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 3u,
              "E2.5 P4 automatic three-output replacement commits one coherent pending delivery group");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.5 P4 automatic three-output replacement commits visibility and settles owner state");
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticA1;
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticB1;
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticC1;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p4AutomaticA1) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p4AutomaticB1) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5CId), p4AutomaticC1) &&
                  p4AutomaticA1.publishedPhysicalPath != p4AutomaticA0 &&
                  p4AutomaticB1.publishedPhysicalPath != p4AutomaticB0 &&
                  p4AutomaticC1.publishedPhysicalPath != p4AutomaticC0 &&
                  files.FileExist(p4AutomaticA0) && files.FileExist(p4AutomaticB0) && files.FileExist(p4AutomaticC0) &&
                  p5Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 3u,
              "E2.5 P4 successful automatic batch retains all three superseded generations after visibility settlement");

        const filesystem::AbsolutePath p4AutomaticA1Path = p4AutomaticA1.publishedPhysicalPath;
        const filesystem::AbsolutePath p4AutomaticB1Path = p4AutomaticB1.publishedPhysicalPath;
        const filesystem::AbsolutePath p4AutomaticC1Path = p4AutomaticC1.publishedPhysicalPath;
        Check(WriteTextAsset(files, p5APath, containers::StringView("p5-transitive-a-p4-two\n"), Metadata(p5AId)) &&
                  WriteTextAsset(files, p5BPath, containers::StringView("p5-transitive-b-p4-two\n"), Metadata(p5BId)) &&
                  WriteTextAsset(files, p5CPath, containers::StringView("p5-transitive-c-p4-two\n"), Metadata(p5CId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 3u,
              "E2.5 P4 second automatic three-output replacement reaches exact pending delivery before visibility");
        asset_streaming::ProjectAssetPipelineFailure p4AutomaticCapacityFailure;
        const asset_streaming::ProjectAssetPipelineResult p4AutomaticCapacityResult =
            DrainAutomaticRecookWithFailure(p5Pipeline, p4AutomaticCapacityFailure);
        Check(p4AutomaticCapacityResult == asset_streaming::ProjectAssetPipelineResult::PublicationFailed &&
                  p4AutomaticCapacityFailure.publication.result == asset_streaming::ProjectLoosePublicationResult::LimitExceeded,
              "E2.5 P4 automatic batch pre-reservation refuses visibility when retained-generation capacity is full");
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticAAfterFailure;
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticBAfterFailure;
        asset_streaming::ProjectAssetOutputDeliveryStatus p4AutomaticCAfterFailure;
        streaming::ResourceSource p4AutomaticCurrentA;
        streaming::ResourceSource p4AutomaticCurrentB;
        streaming::ResourceSource p4AutomaticCurrentC;
        containers::DynamicArray<streaming::DependencyDescriptor> p4AutomaticDependenciesA(memory::pools::Streaming::GetInstance());
        containers::DynamicArray<streaming::DependencyDescriptor> p4AutomaticDependenciesB(memory::pools::Streaming::GetInstance());
        containers::DynamicArray<streaming::DependencyDescriptor> p4AutomaticDependenciesC(memory::pools::Streaming::GetInstance());
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p4AutomaticAAfterFailure) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p4AutomaticBAfterFailure) &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5CId), p4AutomaticCAfterFailure) &&
                  p4AutomaticAAfterFailure.publishedPhysicalPath == p4AutomaticA1Path &&
                  p4AutomaticBAfterFailure.publishedPhysicalPath == p4AutomaticB1Path &&
                  p4AutomaticCAfterFailure.publishedPhysicalPath == p4AutomaticC1Path &&
                  p5Pipeline.GetLooseMaintenanceStatus().trackedCandidates == 3u &&
                  streamer.OpenSource(OutputReference(p5AId), p4AutomaticCurrentA, p4AutomaticDependenciesA) == resources::Failure::None &&
                  streamer.OpenSource(OutputReference(p5BId), p4AutomaticCurrentB, p4AutomaticDependenciesB) == resources::Failure::None &&
                  streamer.OpenSource(OutputReference(p5CId), p4AutomaticCurrentC, p4AutomaticDependenciesC) == resources::Failure::None &&
                  p4AutomaticCurrentA.GetPhysicalPath() == p4AutomaticA1Path &&
                  p4AutomaticCurrentB.GetPhysicalPath() == p4AutomaticB1Path &&
                  p4AutomaticCurrentC.GetPhysicalPath() == p4AutomaticC1Path,
              "E2.5 P4 automatic batch capacity failure leaves the entire prior visibility set and retirement ledger unchanged");
        p4AutomaticCurrentA.Close();
        p4AutomaticCurrentB.Close();
        p4AutomaticCurrentC.Close();
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 transitive Generated pipeline shuts down cleanly");
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};
        Check(DeleteDirectoryTree(files, p5Root), "E2.4 P5 transitive Generated fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.4 P5 publication/status capacity. One installed A owns the sole
    // maximumPublishedResources slot. A later B cook must be rejected before
    // VADI/delivery/streamer visibility can advance and must leave A exact.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E24P5PublishedCapacity");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.4 P5 create one-slot published-capacity roots");
        assets::AssetId p5AId;
        assets::AssetId p5BId;
        Check(assets::GenerateAssetId(p5AId) && assets::GenerateAssetId(p5BId),
              "E2.4 P5 generate one-slot published-capacity identities");
        const filesystem::AbsolutePath p5APath = p5AssetsRoot.AddFilePath("a.tasset");
        const filesystem::AbsolutePath p5BPath = p5AssetsRoot.AddFilePath("b.tasset");
        Check(WriteTextAsset(files, p5APath, containers::StringView("p5-capacity-a\n"), Metadata(p5AId)),
              "E2.4 P5 write one-slot published-capacity A");
        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.4 P5 scan one-slot published-capacity A");
        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.maximumPublishedResources = 1u;
        p5Config.automaticRecook.enabled = true;
        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 one-slot capacity cooks and installs A");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5AInstalled;
        Check(p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p5AInstalled) &&
                  p5AInstalled.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed,
              "E2.4 P5 installed A owns sole published/status slot");
        streaming::ResourceSource p5ASource;
        containers::DynamicArray<streaming::DependencyDescriptor> p5ADependencies(memory::pools::Streaming::GetInstance());
        Check(streamer.OpenSource(OutputReference(p5AId), p5ASource, p5ADependencies) == resources::Failure::None && p5ASource.IsOpen(),
              "E2.4 P5 sole-capacity A is descriptor-visible before B attempt");
        const filesystem::AbsolutePath p5APhysical = p5ASource.GetPhysicalPath();
        p5ASource.Close();

        // maximumPublishedResources is the shared desired/publication/status bound.
        // Therefore the second desired output must be rejected while preparing the
        // isolated candidate, before accepted source truth, delivery ownership, or
        // descriptor visibility can change. This is intentionally earlier than the
        // automatic DeliveryOverflow path covered by the P1 pending-delivery test.
        const u64 p5AcceptedBeforeB = p5Pipeline.GetAcceptedAssetGeneration();
        asset_streaming::ProjectAssetPipelineFailure p5CapacityFailure;
        Check(WriteTextAsset(files, p5BPath, containers::StringView("p5-capacity-b\n"), Metadata(p5BId)) &&
                  p5Pipeline.Rescan(p5RootSpan, nullptr, nullptr, &p5CapacityFailure) ==
                      asset_streaming::ProjectAssetPipelineResult::ReconciliationFailed &&
                  p5CapacityFailure.sources == assets::SourceDatabaseResult::LimitExceeded &&
                  p5Pipeline.GetAcceptedAssetGeneration() == p5AcceptedBeforeB,
              "E2.4 P5 one-slot project capacity rejects B before accepted or visible truth changes");
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 rejected capacity candidate creates no automatic delivery work");
        asset_streaming::ProjectAssetOutputDeliveryStatus p5AAfter;
        asset_streaming::ProjectAssetOutputDeliveryStatus p5UnexpectedB;
        streaming::ResourceSource p5UnexpectedBSource;
        containers::DynamicArray<streaming::DependencyDescriptor> p5UnexpectedBDependencies(memory::pools::Streaming::GetInstance());
        Check(p5Sources.LookupAsset(p5BId) == nullptr &&
                  p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5AId), p5AAfter) &&
                  p5AAfter.committedBuildFingerprint == p5AInstalled.committedBuildFingerprint &&
                  p5AAfter.committedContentFingerprint == p5AInstalled.committedContentFingerprint &&
                  p5AAfter.publishedBuildFingerprint == p5AInstalled.publishedBuildFingerprint &&
                  p5AAfter.publishedContentFingerprint == p5AInstalled.publishedContentFingerprint &&
                  p5AAfter.publishedPhysicalPath == p5AInstalled.publishedPhysicalPath &&
                  !p5Pipeline.GetOutputDeliveryStatus(OutputReference(p5BId), p5UnexpectedB) &&
                  streamer.OpenSource(OutputReference(p5BId), p5UnexpectedBSource, p5UnexpectedBDependencies) == resources::Failure::NotFound,
              "E2.4 P5 rejected B creates no accepted/status/descriptor visibility and preserves installed A truth");
        streaming::ResourceSource p5AStillVisible;
        containers::DynamicArray<streaming::DependencyDescriptor> p5AStillDependencies(memory::pools::Streaming::GetInstance());
        Check(streamer.OpenSource(OutputReference(p5AId), p5AStillVisible, p5AStillDependencies) == resources::Failure::None &&
                  p5AStillVisible.IsOpen() && p5AStillVisible.GetPhysicalPath() == p5APhysical,
              "E2.4 P5 rejected B leaves exact A descriptor visible");
        p5AStillVisible.Close();
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.4 P5 one-slot published-capacity pipeline shuts down cleanly");
        assets::DependencyIndexConfig p5IndexConfig = p5Config.build.dependencyIndex;
        p5IndexConfig.root = p5IndexRoot.AsChar();
        p5IndexConfig.settingsFingerprint = p5Config.build.settingsFingerprint;
        assets::DependencyIndex p5Index;
        assets::DependencyRecord p5UnexpectedBRecord;
        Check(p5Index.Initialize(p5IndexConfig) == assets::IndexResult::Success &&
                  p5Index.Find(OutputReference(p5AId), p5UnexpectedBRecord) == assets::IndexResult::Success &&
                  p5Index.Find(OutputReference(p5BId), p5UnexpectedBRecord) == assets::IndexResult::NotFound &&
                  p5Index.Shutdown(),
              "E2.4 P5 rejected over-capacity B never reaches committed VADI truth");
        Check(DeleteDirectoryTree(files, p5Root), "E2.4 P5 one-slot published-capacity fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.3 P5 authoritative overflow recovery. Keep one complete older pending
    // generation, then accept a move whose old+new typed inputs exceed the bound.
    // The coordinator must attempt current-source recovery instead of executing
    // that older pending set. With the intentionally impossible one-input bound,
    // recovery becomes sticky; reopening with a sufficient bound recovers truth.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E23P5Overflow");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.3 P5 create isolated overflow-recovery roots");

        assets::AssetId p5OverflowId;
        Check(assets::GenerateAssetId(p5OverflowId), "E2.3 P5 generate overflow-recovery asset identity");
        const filesystem::AbsolutePath p5OriginalPath = p5AssetsRoot.AddFilePath("overflow_original.tasset");
        const filesystem::AbsolutePath p5MovedPath = p5AssetsRoot.AddFilePath("overflow_moved.tasset");
        Check(WriteTextAsset(files, p5OriginalPath, containers::StringView("p5-overflow-base\n"), Metadata(p5OverflowId)),
              "E2.3 P5 write overflow-recovery source");

        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.3 P5 overflow-recovery source catalog scan");

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;
        p5Config.automaticRecook.maximumPendingInputs = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 tight-bound pipeline first-cooks baseline within one-input limit");

        Check(WriteTextAsset(files, p5OriginalPath, containers::StringView("p5-overflow-pending\n"), Metadata(p5OverflowId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  p5Pipeline.GetAutomaticRecookStatus().pendingInputs == 1u,
              "E2.3 P5 retain one complete pending generation at tight input bound");
        Check(p5Pipeline.MoveAsset(p5OverflowId, p5MovedPath) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept newer move even when automatic evidence overflows");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5OverflowStatus = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5OverflowStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5OverflowStatus.recoveryRequired &&
                  asset_streaming::HasAutomaticRecookDirtyReason(
                      p5OverflowStatus.dirtyReasons, asset_streaming::ProjectAssetAutomaticRecookDirtyReason::OverflowRecovery) &&
                  p5OverflowStatus.pendingAssets == 1u && p5OverflowStatus.pendingInputs == 1u,
              "E2.3 P5 overflow preserves prior complete pending set but marks authoritative recovery required");
        const u32 p5OverflowCompileCalls = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  block.compileCalls.GetValue() == p5OverflowCompileCalls,
              "E2.3 P5 overflow chooses authoritative recovery instead of executing older pending work");
        for (u32 repeat = 0; repeat < 8u; ++repeat)
            Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 failed authoritative overflow recovery remains sticky");
        Check(block.compileCalls.GetValue() == p5OverflowCompileCalls,
              "E2.3 P5 sticky overflow recovery has no owner-tick build loop");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 tight-bound overflow pipeline shuts down cleanly");

        asset_streaming::ProjectAssetPipelineConfig p5RelaxedConfig = p5Config;
        p5RelaxedConfig.automaticRecook.maximumPendingInputs = 2u;
        asset_streaming::ProjectAssetRestoreReport p5RelaxedRestore;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, importerSpan, p5RelaxedConfig, &p5RelaxedRestore) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5RelaxedRestore.acceptedSnapshot == asset_streaming::ProjectAssetSnapshotResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P5 startup recovery completes once configured bounds can represent authoritative work");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 relaxed overflow-recovery pipeline shuts down cleanly");

        assets::DependencyIndexConfig p5RecoveredConfig = p5RelaxedConfig.build.dependencyIndex;
        p5RecoveredConfig.root = p5IndexRoot.AsChar();
        p5RecoveredConfig.settingsFingerprint = p5RelaxedConfig.build.settingsFingerprint;
        assets::DependencyIndex p5RecoveredIndex;
        assets::DependencyRecord p5RecoveredRecord;
        const assets::SourceRecord* const p5RecoveredSource = p5Sources.LookupAsset(p5OverflowId);
        Check(p5RecoveredSource != nullptr && p5RecoveredIndex.Initialize(p5RecoveredConfig) == assets::IndexResult::Success &&
                  p5RecoveredIndex.Find(OutputReference(p5OverflowId), p5RecoveredRecord) == assets::IndexResult::Success &&
                  p5RecoveredRecord.source == resources::ResourceReference(p5RecoveredSource->sourceIdentity, TestSourceType),
              "E2.3 P5 relaxed recovery persists VADI for the current moved source identity");
        Check(p5RecoveredIndex.Shutdown(), "E2.3 P5 overflow-recovery verification index shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.3 P5 isolated overflow-recovery fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // E2.3 P5 retained failed-batch invalidations. A root fails while the asset
    // still owns its original typed source identity. A later move changes current
    // source truth. With a one-input recovery bound, authoritative recovery must
    // still retain the failed batch's old identity and therefore remain bounded
    // RecoveryRequired. Moving back to that original identity makes the retained
    // and current seeds identical, so the same pipeline can recover successfully;
    // that successful worker completion must clear recoveryRequired.
    // ---------------------------------------------------------------------
    {
        const filesystem::AbsolutePath p5Root = root.AddDirPath("E23P5RetainedFailure");
        const filesystem::AbsolutePath p5AssetsRoot = p5Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p5DerivedRoot = p5Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p5IndexRoot = p5Root.AddDirPath("Index");
        const filesystem::AbsolutePath p5LooseRoot = p5Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p5Journal = p5AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p5Root) && files.CreatePath(p5AssetsRoot) && files.CreatePath(p5DerivedRoot) &&
                  files.CreatePath(p5IndexRoot) && files.CreatePath(p5LooseRoot),
              "E2.3 P5 create isolated retained-failure roots");

        assets::AssetId p5RetainedId;
        Check(assets::GenerateAssetId(p5RetainedId), "E2.3 P5 generate retained-failure asset identity");
        const filesystem::AbsolutePath p5OriginalPath = p5AssetsRoot.AddFilePath("retained_original.tasset");
        const filesystem::AbsolutePath p5MovedPath = p5AssetsRoot.AddFilePath("retained_moved.tasset");
        const filesystem::AbsolutePath p5NewerPath = p5AssetsRoot.AddFilePath("retained_newer.tasset");
        Check(WriteTextAsset(files, p5OriginalPath, containers::StringView("p5-retained-base\n"), Metadata(p5RetainedId)),
              "E2.3 P5 write retained-failure source");

        containers::DynamicArray<assets::SourceRoot> p5Roots(memory::pools::Assets::GetInstance());
        p5Roots.PushBack({p5AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p5RootSpan(p5Roots.TypedData(), p5Roots.Size());
        assets::SourceDatabase p5Sources;
        Check(p5Sources.Rescan(files, p5RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.3 P5 retained-failure source catalog scan");

        RequestResolveBlockState p5ResolveState;
        p5ResolveState.block.SetValue(false);
        p5ResolveState.started.SetValue(false);
        p5ResolveState.release.SetValue(false);
        p5ResolveState.fail.SetValue(false);
        p5ResolveState.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p5Importer = importer;
        p5Importer.sourceType = resources::InvalidResourceTypeId;
        p5Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p5Importer.userData = &p5ResolveState;
        containers::DynamicArray<assets::SourceImporterDescriptor> p5Importers(memory::pools::Assets::GetInstance());
        p5Importers.PushBack(p5Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p5ImporterSpan(p5Importers.TypedData(), p5Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p5Config = config;
        p5Config.build.artifactCacheRoot = p5DerivedRoot;
        p5Config.build.dependencyIndexRoot = p5IndexRoot;
        p5Config.publication.artifactCacheRoot = p5DerivedRoot;
        p5Config.publication.looseRoot = p5LooseRoot;
        p5Config.operationJournalPath = p5Journal;
        p5Config.automaticRecook.enabled = true;
        p5Config.automaticRecook.maximumPendingInputs = 1u;

        block.block = false;
        block.fail.SetValue(false);
        block.started.SetValue(false);
        block.release.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        asset_streaming::ProjectAssetPipeline p5Pipeline;
        Check(p5Pipeline.Initialize(files, p5Sources, streamer, compilerSpan, p5ImporterSpan, p5Config) ==
                  asset_streaming::ProjectAssetPipelineResult::Success &&
                  DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 retained-failure pipeline first-cooks baseline");

        const assets::SourceRecord* const p5OriginalSource = p5Sources.LookupAsset(p5RetainedId);
        const resources::ResourceReference p5OldInput(
            p5OriginalSource != nullptr ? p5OriginalSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        Check(p5OriginalSource != nullptr && p5OldInput.IsValid(),
              "E2.3 P5 capture original typed source identity before failed batch");
        Check(WriteTextAsset(files, p5OriginalPath, containers::StringView("p5-retained-fail\n"), Metadata(p5RetainedId)) &&
                  p5Pipeline.Rescan(p5RootSpan) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 accept source generation that will fail before the move");
        block.fail.SetValue(true);
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Failed &&
                  p5Pipeline.GetAutomaticRecookStatus().retryEntries == 1u,
              "E2.3 P5 failed batch retains its original invalidation state");
        block.fail.SetValue(false);

        Check(p5Pipeline.MoveAsset(p5RetainedId, p5MovedPath) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 newer move is accepted while the failed batch remains retained");
        const assets::SourceRecord* const p5MovedSource = p5Sources.LookupAsset(p5RetainedId);
        const resources::ResourceReference p5MovedInput(
            p5MovedSource != nullptr ? p5MovedSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5MovedStatus = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5MovedSource != nullptr && p5MovedSource->path == p5MovedPath && p5MovedInput.IsValid() && !(p5MovedInput == p5OldInput) &&
                  p5MovedStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5MovedStatus.recoveryRequired,
              "E2.3 P5 failed batch old identity is distinct from newer moved source truth");
        const u32 p5CallsBeforeRetainedRecovery = block.compileCalls.GetValue();
        const u32 p5ResolveCallsBeforeRetainedRecovery = p5ResolveState.resolveCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5Pipeline.GetAutomaticRecookStatus().recoveryRequired &&
                  block.compileCalls.GetValue() == p5CallsBeforeRetainedRecovery &&
                  p5ResolveState.resolveCalls.GetValue() == p5ResolveCallsBeforeRetainedRecovery + 1u,
              "E2.3 P5 recovery plans current truth once, retains the failed old source identity, and hits the two-input bound before root work");
        for (u32 repeat = 0; repeat < 4u; ++repeat)
            Check(p5Pipeline.TickAutomaticRecook() == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed,
                  "E2.3 P5 retained old-identity recovery remains sticky after its bounded attempt");
        Check(block.compileCalls.GetValue() == p5CallsBeforeRetainedRecovery &&
                  p5ResolveState.resolveCalls.GetValue() == p5ResolveCallsBeforeRetainedRecovery + 1u,
              "E2.3 P5 retained old-identity failure does not busy-loop planning or compiler work");

        Check(p5Pipeline.MoveAsset(p5RetainedId, p5NewerPath) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 genuinely newer moved truth reopens exactly one authoritative recovery attempt");
        const assets::SourceRecord* const p5NewerSource = p5Sources.LookupAsset(p5RetainedId);
        const resources::ResourceReference p5NewerInput(
            p5NewerSource != nullptr ? p5NewerSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        Check(p5NewerSource != nullptr && p5NewerSource->path == p5NewerPath && p5NewerInput.IsValid() &&
                  !(p5NewerInput == p5MovedInput) && !(p5NewerInput == p5OldInput) &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5Pipeline.GetAutomaticRecookStatus().recoveryRequired,
              "E2.3 P5 newer accepted truth keeps recovery required while replacing current source identity");
        const u32 p5ResolveCallsBeforeNewerRecovery = p5ResolveState.resolveCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::BuildRequestFailed &&
                  p5Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5ResolveState.resolveCalls.GetValue() == p5ResolveCallsBeforeNewerRecovery + 1u,
              "E2.3 P5 genuinely newer accepted truth permits exactly one additional bounded recovery plan");

        Check(p5Pipeline.MoveAsset(p5RetainedId, p5OriginalPath) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 newer move back to the retained identity requests one new authoritative recovery");
        const assets::SourceRecord* const p5RestoredSource = p5Sources.LookupAsset(p5RetainedId);
        const resources::ResourceReference p5RestoredInput(
            p5RestoredSource != nullptr ? p5RestoredSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5BeforeSuccessfulRecovery = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5RestoredSource != nullptr && p5RestoredSource->path == p5OriginalPath && p5RestoredInput == p5OldInput &&
                  p5BeforeSuccessfulRecovery.state == asset_streaming::ProjectAssetAutomaticRecookState::RecoveryRequired &&
                  p5BeforeSuccessfulRecovery.recoveryRequired,
              "E2.3 P5 recoveryRequired remains true until the same-pipeline recovery actually succeeds");
        const u32 p5CallsBeforeSuccessfulRecovery = block.compileCalls.GetValue();
        Check(DrainAutomaticRecook(p5Pipeline) == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 retained old/current identity deduplication lets authoritative recovery succeed within the same bound");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p5RecoveredStatus = p5Pipeline.GetAutomaticRecookStatus();
        Check(p5RecoveredStatus.state == asset_streaming::ProjectAssetAutomaticRecookState::Idle && !p5RecoveredStatus.recoveryRequired &&
                  block.compileCalls.GetValue() > p5CallsBeforeSuccessfulRecovery,
              "E2.3 P5 successful same-pipeline recovery clears recoveryRequired only on completion");
        Check(p5Pipeline.Shutdown() == asset_streaming::ProjectAssetPipelineResult::Success,
              "E2.3 P5 retained-failure pipeline shuts down cleanly");
        Check(DeleteDirectoryTree(files, p5Root), "E2.3 P5 isolated retained-failure fixture cleanup succeeds");
    }

    const resources::ResourceReference outA = OutputReference(idA);
    const resources::ResourceReference outB = OutputReference(idB);
    const resources::ResourceReference outBusy = OutputReference(idBusy);
    const auto Success = asset_streaming::ProjectAssetPipelineResult::Success;
    const auto OperationFailed = asset_streaming::ProjectAssetPipelineResult::OperationFailed;
    const auto Busy = asset_streaming::ProjectAssetPipelineResult::Busy;

    // ---------------------------------------------------------------------
    // 1. duplicate -> undo -> redo restores exact source and sidecar bytes.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        containers::DynamicArray<u8> originalSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> originalSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathA, originalSource) && ReadBytes(files, MetadataPath(pathA), originalSidecar), "read source A baseline");

        const filesystem::AbsolutePath duplicatePath = assetsRoot.AddFilePath("a_duplicate.tasset");
        assets::AssetId duplicateId;
        Check(pipeline.DuplicateAssetWithGeneratedIdentity(idA, duplicatePath, duplicateId) == Success,
              "canonical duplicate commits");
        Check(duplicateId.IsValid() && duplicateId != idA && sources.LookupAsset(duplicateId) != nullptr,
              "canonical duplicate draws a new catalog-unique identity");
        Check(pipeline.GetUndoAssetOperationCount() == 1 && pipeline.GetRedoAssetOperationCount() == 0,
              "duplicate publishes one inverse record and leaves redo empty");

        containers::DynamicArray<u8> committedSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> committedSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, duplicatePath, committedSource) && ReadBytes(files, MetadataPath(duplicatePath), committedSidecar),
              "duplicate target pair exists");
        Check(SameBytes(committedSource, originalSource), "duplicate copies exact source bytes");
        assets::AssetMetadata committedMetadata;
        Check(assets::ParseMetadata(containers::StringView(reinterpret_cast<const char*>(committedSidecar.TypedData()), committedSidecar.Size()),
                                    committedMetadata) == assets::MetadataResult::Success &&
                  committedMetadata.id == duplicateId,
              "duplicate sidecar carries the new identity");

        Check(pipeline.UndoAssetOperation() == Success, "duplicate undo commits");
        Check(!files.FileExist(duplicatePath) && !files.FileExist(MetadataPath(duplicatePath)), "duplicate undo removes both files");
        Check(pipeline.GetUndoAssetOperationCount() == 0 && pipeline.GetRedoAssetOperationCount() == 1,
              "undo transfers the inverse record to redo");

        Check(pipeline.RedoAssetOperation() == Success, "duplicate redo commits");
        containers::DynamicArray<u8> redoneSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> redoneSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, duplicatePath, redoneSource) && ReadBytes(files, MetadataPath(duplicatePath), redoneSidecar),
              "redo restores the duplicate pair");
        Check(SameBytes(redoneSource, committedSource) && SameBytes(redoneSidecar, committedSidecar),
              "redo restores exact source and sidecar bytes");

        Check(pipeline.UndoAssetOperation() == Success, "duplicate cleanup undo");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 2. move -> undo restores the old pair and removes the destination pair,
    //    preserving the AssetId.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath movedPath = assetsRoot.AddFilePath("a_moved.tasset");
        Check(pipeline.MoveAsset(idA, movedPath) == Success, "move commits");
        const assets::SourceRecord* const moved = sources.LookupAsset(idA);
        Check(moved != nullptr && moved->path == movedPath, "move preserves the AssetId at the destination");
        Check(!files.FileExist(pathA) && !files.FileExist(MetadataPath(pathA)) && files.FileExist(movedPath) &&
                  files.FileExist(MetadataPath(movedPath)),
              "move relocates the source and sidecar");
        Check(pipeline.UndoAssetOperation() == Success, "move undo commits");
        Check(files.FileExist(pathA) && files.FileExist(MetadataPath(pathA)) && !files.FileExist(movedPath) &&
                  !files.FileExist(MetadataPath(movedPath)),
              "move undo restores the old pair and removes the destination pair");
        Check(sources.LookupAsset(idA) != nullptr && sources.LookupAsset(idA)->path == pathA, "move undo restores catalog identity at the old path");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 3. retarget -> undo restores exact prior authored bytes.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        containers::DynamicArray<u8> beforeSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> beforeSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, beforeSource) && ReadBytes(files, MetadataPath(pathReferrer), beforeSidecar),
              "read referrer baseline");

        containers::DynamicArray<assets::AssetReferenceReplacement> replacements(memory::pools::Assets::GetInstance());
        replacements.PushBack({outA, outB});
        Check(pipeline.RetargetAssetReferences({replacements.TypedData(), replacements.Size()}) == Success, "retarget commits");
        containers::DynamicArray<u8> retargetedSource(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, retargetedSource) && !SameBytes(retargetedSource, beforeSource),
              "retarget rewrites the referencing document");
        Check(pipeline.GetUndoAssetOperationCount() == 1, "retarget publishes one inverse record");

        Check(pipeline.UndoAssetOperation() == Success, "retarget undo commits");
        containers::DynamicArray<u8> restoredSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> restoredSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, restoredSource) && ReadBytes(files, MetadataPath(pathReferrer), restoredSidecar),
              "retarget undo restores the referrer pair");
        Check(SameBytes(restoredSource, beforeSource) && SameBytes(restoredSidecar, beforeSidecar),
              "retarget undo restores exact prior authored bytes");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 4. delete -> undo restores the deleted pair and the affected referrer.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        containers::DynamicArray<u8> beforeSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> beforeDeletedSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> beforeDeletedSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, beforeSource), "read referrer before delete");
        Check(ReadBytes(files, pathA, beforeDeletedSource) && ReadBytes(files, MetadataPath(pathA), beforeDeletedSidecar),
              "read delete victim before referencer rejection");

        asset_streaming::ProjectAssetPipelineFailure rejectionFailure;
        asset_streaming::ProjectAssetRestoreReport rejectionReport;
        Check(pipeline.DeleteAsset(idA, {}, &rejectionReport, &rejectionFailure) == OperationFailed &&
                  rejectionFailure.operation.result == assets::AssetOperationResult::ReferencesRemain,
              "delete without explicit replacements fails closed while authored referencers remain");
        containers::DynamicArray<u8> rejectedSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> rejectedSidecar(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> rejectedReferrer(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathA, rejectedSource) && ReadBytes(files, MetadataPath(pathA), rejectedSidecar) &&
                  ReadBytes(files, pathReferrer, rejectedReferrer) && SameBytes(rejectedSource, beforeDeletedSource) &&
                  SameBytes(rejectedSidecar, beforeDeletedSidecar) && SameBytes(rejectedReferrer, beforeSource),
              "references-remain rejection preserves the victim pair and authored referencer bytes exactly");
        Check(pipeline.GetUndoAssetOperationCount() == 0u && pipeline.GetRedoAssetOperationCount() == 0u,
              "rejected delete publishes no transient operation-history record");

        containers::DynamicArray<assets::AssetReferenceReplacement> replacements(memory::pools::Assets::GetInstance());
        replacements.PushBack({outA, outB});
        Check(pipeline.DeleteAsset(idA, {replacements.TypedData(), replacements.Size()}) == Success, "delete commits with a replacement");
        Check(!files.FileExist(pathA) && !files.FileExist(MetadataPath(pathA)), "delete removes the source and sidecar");
        containers::DynamicArray<u8> rewrittenSource(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, rewrittenSource) && !SameBytes(rewrittenSource, beforeSource),
              "delete rewrites the referencing document");

        Check(pipeline.UndoAssetOperation() == Success, "delete undo commits");
        Check(files.FileExist(pathA) && files.FileExist(MetadataPath(pathA)), "delete undo restores the deleted pair");
        containers::DynamicArray<u8> restoredSource(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, pathReferrer, restoredSource) && SameBytes(restoredSource, beforeSource),
              "delete undo restores the referencing document");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 5. a new committed command clears redo history.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath duplicatePath = assetsRoot.AddFilePath("a_duplicate_redo.tasset");
        const filesystem::AbsolutePath movedPath = assetsRoot.AddFilePath("a_moved_redo.tasset");
        assets::AssetId duplicateId;
        Check(assets::GenerateAssetId(duplicateId), "redo-clear identity generation");
        Check(pipeline.DuplicateAsset(idA, duplicatePath, duplicateId) == Success, "redo-clear duplicate commits");
        Check(pipeline.UndoAssetOperation() == Success, "redo-clear undo commits");
        Check(pipeline.GetRedoAssetOperationCount() == 1, "redo is available before the new command");
        Check(pipeline.MoveAsset(idA, movedPath) == Success, "new committed command");
        Check(pipeline.GetRedoAssetOperationCount() == 0, "a new committed command clears redo history");
        Check(pipeline.UndoAssetOperation() == Success, "undo the new command to restore the baseline");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 6. a rejected pre-commit operation leaves history counts unchanged.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const u32 undoBefore = pipeline.GetUndoAssetOperationCount();
        const u32 redoBefore = pipeline.GetRedoAssetOperationCount();
        Check(pipeline.MoveAsset(idA, pathReferrer) == OperationFailed, "move onto an occupied path is rejected before commit");
        Check(pipeline.GetUndoAssetOperationCount() == undoBefore && pipeline.GetRedoAssetOperationCount() == redoBefore,
              "rejected operation does not alter undo or redo history");
        Check(files.FileExist(pathA), "rejected operation leaves the source in place");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 7. record-count bound evicts the oldest inverse record.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath bound1 = assetsRoot.AddFilePath("bound_p1.tasset");
        const filesystem::AbsolutePath bound2 = assetsRoot.AddFilePath("bound_p2.tasset");
        const filesystem::AbsolutePath bound3 = assetsRoot.AddFilePath("bound_p3.tasset");
        Check(pipeline.MoveAsset(idBound, bound1) == Success, "record-bound move 1");
        Check(pipeline.MoveAsset(idBound, bound2) == Success, "record-bound move 2");
        Check(pipeline.MoveAsset(idBound, bound3) == Success, "record-bound move 3");
        Check(pipeline.GetUndoAssetOperationCount() == MaximumHistoryRecords,
              "record-count bound retains only the newest inverse records");
        Check(pipeline.UndoAssetOperation() == Success, "record-bound undo 1 commits");
        Check(files.FileExist(bound2) && !files.FileExist(bound3), "record-bound undo 1 reverses the newest move");
        Check(pipeline.UndoAssetOperation() == Success, "record-bound undo 2 commits");
        Check(files.FileExist(bound1) && !files.FileExist(bound2) && !files.FileExist(bound3),
              "record-bound undo 2 reverses the second move");
        Check(sources.LookupAsset(idBound) != nullptr && sources.LookupAsset(idBound)->path == bound1,
              "record-bound undo 2 leaves the asset at the second location");
        Check(pipeline.UndoAssetOperation() == asset_streaming::ProjectAssetPipelineResult::UndoUnavailable,
              "the evicted oldest inverse record is unavailable");
        Check(!files.FileExist(pathBound),
              "the oldest inverse record was evicted, so the original path is not restored");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 8. a growing reverse snapshot forces destination-stack byte eviction.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath large1 = assetsRoot.AddFilePath("large_l1.tasset");
        const filesystem::AbsolutePath large2 = assetsRoot.AddFilePath("large_l2.tasset");
        assets::AssetId largeId1;
        assets::AssetId largeId2;
        Check(assets::GenerateAssetId(largeId1) && assets::GenerateAssetId(largeId2), "large duplicate identity generation");
        Check(pipeline.DuplicateAsset(idLarge, large1, largeId1) == Success, "large duplicate 1 commits");
        Check(pipeline.DuplicateAsset(idLarge, large2, largeId2) == Success, "large duplicate 2 commits");
        Check(pipeline.GetUndoAssetOperationCount() == 2, "two inverse records retained before byte eviction");
        Check(pipeline.UndoAssetOperation() == Success, "large undo 1 commits");
        Check(pipeline.GetRedoAssetOperationCount() == 1, "first reverse snapshot retained");
        Check(pipeline.UndoAssetOperation() == Success, "large undo 2 commits");
        Check(pipeline.GetRedoAssetOperationCount() == 1,
              "growing reverse snapshot evicts the oldest destination record instead of exceeding the aggregate budget");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 9. busy build ownership rejects mutation and replay.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath busy1 = assetsRoot.AddFilePath("busy_p1.tasset");
        const filesystem::AbsolutePath busy2 = assetsRoot.AddFilePath("busy_p2.tasset");
        Check(pipeline.MoveAsset(idBusy, busy1) == Success, "busy move 1 commits");
        Check(pipeline.MoveAsset(idBusy, busy2) == Success, "busy move 2 commits");
        Check(pipeline.UndoAssetOperation() == Success, "busy pre-undo commits");
        Check(pipeline.GetUndoAssetOperationCount() == 1 && pipeline.GetRedoAssetOperationCount() == 1,
              "busy scenario has both history stacks populated");

        block.block = true;
        asset_streaming::ProjectAssetRequest request = pipeline.Request(outBusy);
        Check(request.IsValid(), "busy request accepted");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue() && !request.HasFinished(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue(), "blocking compiler started");
        Check(!pipeline.CanMutateSources(), "live graph work rejects source mutation");
        Check(!pipeline.CanUndoAssetOperation() && !pipeline.CanRedoAssetOperation(), "live graph work disables undo and redo");
        Check(pipeline.UndoAssetOperation() == Busy, "undo is rejected while graph work is live");
        Check(pipeline.RedoAssetOperation() == Busy, "redo is rejected while graph work is live");

        Check(request.Cancel(), "busy request cancels");
        request.Wait();
        request.Reset();
        block.block = false;
        Check(pipeline.CanMutateSources(), "graph is quiescent after the request is released");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 10. shutdown/reinitialize clears session history while the committed
    //     physical state remains on disk.
    // ---------------------------------------------------------------------
    {
        pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath restartPath = assetsRoot.AddFilePath("a_restart.tasset");
        assets::AssetId restartId;
        Check(assets::GenerateAssetId(restartId), "restart identity generation");
        Check(pipeline.DuplicateAsset(idA, restartPath, restartId) == Success, "restart duplicate commits");
        Check(pipeline.GetUndoAssetOperationCount() == 1, "undo history present before shutdown");

        Check(pipeline.RequestVddcMaintenance() == asset_streaming::ProjectAssetPipelineResult::Pending &&
                  pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Running,
              "E2.5 P3 active maintenance is present before shutdown without owner-tick adoption");
        Check(pipeline.Shutdown() == Success, "pipeline shutdown drains active E2.5 P3 maintenance");
        Check(!pipeline.IsInitialized(), "pipeline reports shutdown");
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config) == Success, "pipeline reinitializes");
        Check(pipeline.GetUndoAssetOperationCount() == 0 && pipeline.GetRedoAssetOperationCount() == 0,
              "session history is cleared across reinitialization");
        Check(pipeline.GetVddcMaintenanceStatus().state == asset_streaming::ProjectAssetVddcMaintenanceState::Idle,
              "E2.5 P3 maintenance state is session-local and clean after shutdown drain/reinitialize");
        Check(files.FileExist(restartPath) && files.FileExist(MetadataPath(restartPath)),
              "committed physical state persists across reinitialization");
        pipeline.ClearAssetOperationHistory();
    }

    // ---------------------------------------------------------------------
    // 11. E2.1 observation enablement excludes live graph work. A Busy enable
    // must be rejected before any observation startup side effect, then must
    // succeed once the graph is quiescent and perform its mandatory
    // generation-zero catch-up.
    // ---------------------------------------------------------------------
    {
        ManualObservationSource regressionObservation;
        asset_streaming::ProjectAssetObservationConfig regressionConfig;
        regressionConfig.settleTicks = 2;
        regressionConfig.maximumDeferralTicks = 4;
        regressionConfig.maximumPathsPerTick = 2;
        regressionConfig.maximumPendingPaths = 32;
        regressionConfig.maximumReconcileOperationsPerTick = 1;

        block.block = true;
        asset_streaming::ProjectAssetRequest request = pipeline.Request(outBusy);
        Check(request.IsValid(), "E2.1 busy-enable request accepted");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue() && !request.HasFinished(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue(), "E2.1 busy-enable blocking compiler started");
        Check(!pipeline.CanMutateSources(), "E2.1 busy-enable graph owns live work");

        const asset_streaming::ProjectAssetObservationStatus beforeRejected = pipeline.GetObservationStatus();
        const u32 startCallsBefore = regressionObservation.StartCount();
        Check(pipeline.EnableObservation(regressionObservation, regressionConfig) == Busy,
              "E2.1 enable is rejected while graph work is live");
        Check(regressionObservation.StartCount() == startCallsBefore,
              "E2.1 rejected enable never starts the observation source");
        Check(!pipeline.IsObservingSources(), "E2.1 rejected enable never publishes a running session");
        const asset_streaming::ProjectAssetObservationStatus afterRejected = pipeline.GetObservationStatus();
        Check(!afterRejected.running && afterRejected.ticks == beforeRejected.ticks &&
                  afterRejected.rescansRequested == beforeRejected.rescansRequested &&
                  afterRejected.rescansCompleted == beforeRejected.rescansCompleted &&
                  afterRejected.pendingPaths == beforeRejected.pendingPaths &&
                  afterRejected.reconcileOperationsCompleted == beforeRejected.reconcileOperationsCompleted,
              "E2.1 rejected enable causes no observation session or reconcile progress");

        Check(request.Cancel(), "E2.1 busy-enable request cancels");
        request.Wait();
        request.Reset();
        block.block = false;
        Check(pipeline.CanMutateSources(), "E2.1 graph is quiescent after the busy-enable request is released");

        Check(pipeline.EnableObservation(regressionObservation, regressionConfig) == Success,
              "E2.1 enable succeeds after the graph becomes quiescent");
        Check(regressionObservation.StartCount() == startCallsBefore + 1,
              "E2.1 successful enable starts the observation source exactly once");
        Check(DrainObservation(pipeline) == Success, "E2.1 successful enable drains generation-zero catch-up");
        Check(pipeline.DisableObservation() == Success, "E2.1 busy-enable session disables cleanly");
        Check(!regressionObservation.IsRunning(), "E2.1 busy-enable observer stopped");
    }

    // ---------------------------------------------------------------------
    // E2.1 external change observation and bounded reconciliation. All hints are
    // injected through a deterministic DirectoryObservationSource; the real
    // production callback seam, accumulator and rescan path are exercised.
    // ---------------------------------------------------------------------
    {
        const auto PendingResult = asset_streaming::ProjectAssetPipelineResult::Pending;
        const auto BusyResult = asset_streaming::ProjectAssetPipelineResult::Busy;
        const auto ObservationNotActive = asset_streaming::ProjectAssetPipelineResult::ObservationNotActive;
        const auto ObservationAlreadyActive = asset_streaming::ProjectAssetPipelineResult::ObservationAlreadyActive;

        ManualObservationSource observation;
        asset_streaming::ProjectAssetObservationConfig observationConfig;
        observationConfig.settleTicks = 2;
        observationConfig.maximumDeferralTicks = 4;
        observationConfig.maximumPathsPerTick = 2;
        observationConfig.maximumPendingPaths = 32;
        observationConfig.maximumReconcileOperationsPerTick = 1;

        // The accumulator acknowledgement is generation-scoped: an overflow
        // or ordinary hint published after Advance() must survive acknowledgement
        // of the older reconciliation. Root-index/path mismatches are untrusted
        // and degrade to the same authoritative-rescan path.
        {
            asset_streaming::SourceChangeAccumulator accumulator;
            Check(accumulator.Configure(observationConfig, rootSpan, journalPath), "E2.1 accumulator configuration");
            u32 stableCommitCalls = 0u;
            const auto stableCommit = [](void* const userData) noexcept -> bool
            {
                ++(*static_cast<u32*>(userData));
                return true;
            };
            const asset_streaming::SourceChangeSnapshot stableSnapshot = accumulator.GetChangeSnapshot();
            Check(accumulator.CommitIfUnchanged(stableSnapshot, stableCommit, &stableCommitCalls) && stableCommitCalls == 1u,
                  "E2.4 P2 stable accumulator fence executes visibility commit exactly once");

            filesystem::DirectoryObservation first;
            first.path = pathA;
            first.root = 0;
            first.kind = filesystem::DirectoryObservationKind::Modified;
            Check(accumulator.Publish(first), "E2.1 accumulator accepts initial hint");
            Check(!accumulator.CommitIfUnchanged(stableSnapshot, stableCommit, &stableCommitCalls) && stableCommitCalls == 1u,
                  "E2.4 P2 changed generation rejects stale visibility before callback execution");
            Check(!accumulator.Advance().reconcile, "E2.1 accumulator initial hint settles");
            const asset_streaming::SourceChangeAdvance reconcile = accumulator.Advance();
            Check(reconcile.reconcile && reconcile.generation != 0, "E2.1 accumulator exposes reconciliation generation");

            filesystem::DirectoryObservation late;
            late.path = pathB;
            late.root = 0;
            late.kind = filesystem::DirectoryObservationKind::Modified;
            Check(accumulator.Publish(late), "E2.1 accumulator accepts hint during reconciliation");
            filesystem::DirectoryObservation lateOverflow;
            lateOverflow.root = 0;
            lateOverflow.kind = filesystem::DirectoryObservationKind::Overflow;
            Check(accumulator.Publish(lateOverflow), "E2.1 accumulator accepts overflow during reconciliation");
            accumulator.AcknowledgeReconcile(reconcile.generation);
            Check(accumulator.GetStatus().overflow && accumulator.GetStatus().rescanRequired,
                  "E2.1 older reconciliation cannot acknowledge a newer overflow");

            const asset_streaming::SourceChangeAdvance overflowReconcile = accumulator.Advance();
            Check(overflowReconcile.reconcile && overflowReconcile.overflow,
                  "E2.1 newer overflow requests another authoritative reconciliation");
            accumulator.AcknowledgeReconcile(overflowReconcile.generation);
            Check(!accumulator.GetStatus().overflow, "E2.1 matching generation acknowledges overflow");

            accumulator.Reset();
            Check(accumulator.Configure(observationConfig, rootSpan, journalPath), "E2.1 accumulator reconfiguration");
            Check(accumulator.Publish(first), "E2.1 accumulator accepts second initial hint");
            Check(!accumulator.Advance().reconcile, "E2.1 second initial hint settles");
            const asset_streaming::SourceChangeAdvance ordinaryReconcile = accumulator.Advance();
            Check(ordinaryReconcile.reconcile, "E2.1 ordinary reconciliation becomes ready");
            Check(accumulator.Publish(late), "E2.1 ordinary late hint arrives during reconciliation");
            accumulator.AcknowledgeReconcile(ordinaryReconcile.generation);
            Check(accumulator.GetStatus().pendingPaths == 1 && accumulator.GetStatus().rescanRequired,
                  "E2.1 older reconciliation preserves a newer ordinary hint");

            filesystem::DirectoryObservation mismatchedRoot;
            mismatchedRoot.path = pathA;
            mismatchedRoot.root = 1;
            mismatchedRoot.kind = filesystem::DirectoryObservationKind::Modified;
            const u64 rejectedBefore = accumulator.GetStatus().hintsRejected;
            Check(accumulator.Publish(mismatchedRoot), "E2.1 mismatched root degrades to rescan");
            Check(accumulator.GetStatus().overflow && accumulator.GetStatus().hintsRejected == rejectedBefore + 1,
                  "E2.1 root-index/path mismatch is explicit and not silently dropped");
            const asset_streaming::SourceChangeAdvance mismatchReconcile = accumulator.Advance();
            Check(mismatchReconcile.reconcile && mismatchReconcile.overflow,
                  "E2.1 root mismatch requests authoritative reconciliation");
            accumulator.AcknowledgeReconcile(mismatchReconcile.generation);
        }

        Check(pipeline.EnableObservation(observation, observationConfig) == Success, "E2.1 enable observation");
        Check(pipeline.IsObservingSources() && pipeline.GetObservationStatus().running, "E2.1 observation is running");
        Check(pipeline.GetObservationStatus().watchedRoots >= 1 && observation.m_rootCount >= 1, "E2.1 roots are observed");
        Check(pipeline.EnableObservation(observation, observationConfig) == ObservationAlreadyActive, "E2.1 double enable rejected");
        Check(DrainObservation(pipeline) == Success, "E2.1 observation start performs authoritative catch-up");
        const u64 observedBaselineGeneration = pipeline.GetAcceptedAssetGeneration();
        Check(pipeline.GetAcceptedAssetChanges().Empty(),
              "E2.2 P3 observation catch-up against an identical catalog preserves the accepted no-op state");
        Check(pipeline.EnableAutomaticRecook() == Success &&
                  pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "E2.3 P2 enables capture after observation catch-up without replaying prior accepted history");

        // (a) external creation.
        assets::AssetId createId;
        Check(assets::GenerateAssetId(createId), "E2.1 create identity");
        const filesystem::AbsolutePath createPath = assetsRoot.AddFilePath("obs_create.tasset");
        Check(WriteTextAsset(files, createPath, containers::StringView(""), Metadata(createId)), "E2.1 external create write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, createPath);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(createPath));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 create burst deferred on the first tick");
        Check(sources.LookupAsset(createId) == nullptr, "E2.1 deferred burst is not applied early");
        Check(DrainObservation(pipeline) == Success, "E2.1 settled create reconciles");
        Check(sources.LookupAsset(createId) != nullptr, "E2.1 external create is cataloged");
        Check(pipeline.GetObservationStatus().rescansCompleted == 1, "E2.1 create accounts one rescan");
        Check(pipeline.GetAcceptedAssetGeneration() == observedBaselineGeneration + 1u && pipeline.LookupAcceptedAsset(createId) != nullptr &&
                  HasAcceptedChange(pipeline, createId, asset_streaming::ProjectAssetChangeReason::New),
              "E2.2 P3 observed creation commits catalog and accepted asset baseline in the same reconciliation");
        const assets::SourceRecord* const observedCreatedSource = sources.LookupAsset(createId);
        const resources::ResourceReference observedCreatedInput(
            observedCreatedSource != nullptr ? observedCreatedSource->sourceIdentity : resources::ResourcePath{}, TestSourceType);
        const asset_streaming::ProjectAssetAutomaticRecookStatus observedPending = pipeline.GetAutomaticRecookStatus();
        Check(observedPending.state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  observedPending.pendingAssets == 1u && observedPending.activeRoots == 0u &&
                  ContainsResource(pipeline.GetAutomaticRecookPendingInputs(), observedCreatedInput) &&
                  ContainsResource(pipeline.GetAutomaticRecookPendingOutputs(), OutputReference(createId)),
              "E2.3 P2 observation worker prepares owned request translation before O(1) owner acceptance");
        Check(pipeline.DisableAutomaticRecook() == Success && pipeline.GetAutomaticRecookPendingInputs().Empty() &&
                  pipeline.GetAutomaticRecookPendingOutputs().Empty(),
              "E2.3 P2 automatic capture can reset while the observation worker is idle");

        // (b) external modification.
        const u64 generationBeforeObservedModify = pipeline.GetAcceptedAssetGeneration();
        Check(WriteTextAsset(files, createPath, containers::StringView("modified\n"), Metadata(createId)), "E2.1 external modify write");
        observation.Emit(filesystem::DirectoryObservationKind::Modified, createPath);
        Check(pipeline.TickObservation() == PendingResult, "E2.1 modify deferred");
        Check(DrainObservation(pipeline) == Success, "E2.1 modify reconciles");
        Check(sources.LookupAsset(createId) != nullptr && sources.LookupAsset(createId)->path == createPath,
              "E2.1 modified source remains cataloged");
        Check(pipeline.GetObservationStatus().rescansCompleted == 2, "E2.1 modify accounts one rescan");
        const asset_streaming::ProjectAssetState* const observedModified = pipeline.LookupAcceptedAsset(createId);
        Check(pipeline.GetAcceptedAssetGeneration() == generationBeforeObservedModify + 1u && observedModified != nullptr &&
                  observedModified->hasSourceContentFingerprint &&
                  observedModified->sourceContentFingerprint == crypto::Sha256("modified\n", 9u) &&
                  HasAcceptedChange(pipeline, createId, asset_streaming::ProjectAssetChangeReason::ContentChanged),
              "E2.2 P3 observed modification publishes the exact scanned content fingerprint and change reason");

        // (c) external deletion.
        assets::AssetId deleteId;
        Check(assets::GenerateAssetId(deleteId), "E2.1 delete identity");
        const filesystem::AbsolutePath deletePath = assetsRoot.AddFilePath("obs_delete.tasset");
        Check(WriteTextAsset(files, deletePath, containers::StringView(""), Metadata(deleteId)), "E2.1 delete target write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, deletePath);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(deletePath));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 delete target deferred");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(deleteId) != nullptr, "E2.1 delete target cataloged");
        static_cast<void>(files.DeleteFile(deletePath));
        static_cast<void>(files.DeleteFile(MetadataPath(deletePath)));
        observation.Emit(filesystem::DirectoryObservationKind::Removed, deletePath);
        observation.Emit(filesystem::DirectoryObservationKind::Removed, MetadataPath(deletePath));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 delete burst deferred");
        Check(DrainObservation(pipeline) == Success, "E2.1 delete reconciles");
        Check(sources.LookupAsset(deleteId) == nullptr, "E2.1 external deletion removes the catalog row");

        // (d) rename burst.
        assets::AssetId renameId;
        Check(assets::GenerateAssetId(renameId), "E2.1 rename identity");
        const filesystem::AbsolutePath renameFrom = assetsRoot.AddFilePath("obs_rename.tasset");
        const filesystem::AbsolutePath renameTo = assetsRoot.AddFilePath("obs_renamed.tasset");
        Check(WriteTextAsset(files, renameFrom, containers::StringView(""), Metadata(renameId)), "E2.1 rename source write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, renameFrom);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(renameFrom));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 rename source deferred");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(renameId) != nullptr, "E2.1 rename source cataloged");
        Check(WriteTextAsset(files, renameTo, containers::StringView(""), Metadata(renameId)), "E2.1 rename destination write");
        static_cast<void>(files.DeleteFile(renameFrom));
        static_cast<void>(files.DeleteFile(MetadataPath(renameFrom)));
        observation.Emit(filesystem::DirectoryObservationKind::Renamed, renameFrom);
        observation.Emit(filesystem::DirectoryObservationKind::Removed, renameFrom);
        observation.Emit(filesystem::DirectoryObservationKind::Added, renameTo);
        observation.Emit(filesystem::DirectoryObservationKind::Renamed, renameTo);
        observation.Emit(filesystem::DirectoryObservationKind::Modified, renameTo);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(renameTo));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 rename burst deferred");
        Check(DrainObservation(pipeline) == Success, "E2.1 rename burst reconciles");
        Check(sources.LookupAsset(renameId) != nullptr && sources.LookupAsset(renameId)->path == renameTo,
              "E2.1 rename burst resolves to the destination path");
        Check(pipeline.GetObservationStatus().hintsCoalesced >= 1, "E2.1 rename burst coalesces duplicate hints");

        // (e) source and sidecar arrive separately in both orders, with an
        // authoritative reconciliation occurring while the pair is incomplete.
        assets::AssetId pairId1;
        Check(assets::GenerateAssetId(pairId1), "E2.1 pair 1 identity");
        const filesystem::AbsolutePath pairPath1 = assetsRoot.AddFilePath("obs_pair_source_first.tasset");
        Check(WriteBytes(files, pairPath1, nullptr, 0u), "E2.1 pair 1 source write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, pairPath1);
        Check(pipeline.TickObservation() == PendingResult, "E2.1 source-only pair defers first");
        Check(DrainObservation(pipeline) == Success, "E2.1 source-only pair can reconcile while incomplete");
        Check(sources.LookupAsset(pairId1) == nullptr, "E2.1 source-only pair does not invent metadata identity");
        Check(WriteMetadata(files, pairPath1, Metadata(pairId1)), "E2.1 pair 1 sidecar write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(pairPath1));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 later sidecar defers");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(pairId1) != nullptr, "E2.1 source-then-sidecar pair catalogs");

        assets::AssetId pairId2;
        Check(assets::GenerateAssetId(pairId2), "E2.1 pair 2 identity");
        const filesystem::AbsolutePath pairPath2 = assetsRoot.AddFilePath("obs_pair_sidecar_first.tasset");
        Check(WriteMetadata(files, pairPath2, Metadata(pairId2)), "E2.1 pair 2 sidecar write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(pairPath2));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 sidecar-only pair defers first");
        Check(DrainObservation(pipeline) == Success, "E2.1 sidecar-only pair can reconcile while incomplete");
        Check(WriteBytes(files, pairPath2, nullptr, 0u), "E2.1 pair 2 source write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, pairPath2);
        Check(pipeline.TickObservation() == PendingResult, "E2.1 later source defers");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(pairId2) != nullptr, "E2.1 sidecar-then-source pair catalogs");

        // (f) duplicate notifications coalesce.
        assets::AssetId duplicateId;
        Check(assets::GenerateAssetId(duplicateId), "E2.1 duplicate identity");
        const filesystem::AbsolutePath duplicatePath = assetsRoot.AddFilePath("obs_duplicate.tasset");
        Check(WriteTextAsset(files, duplicatePath, containers::StringView(""), Metadata(duplicateId)), "E2.1 duplicate write");
        for (u32 repeat = 0; repeat < 5u; ++repeat)
        {
            observation.Emit(filesystem::DirectoryObservationKind::Modified, duplicatePath);
            observation.Emit(filesystem::DirectoryObservationKind::Modified, MetadataPath(duplicatePath));
        }
        Check(pipeline.GetObservationStatus().pendingPaths == 2, "E2.1 duplicate notifications coalesce per path");
        Check(pipeline.GetObservationStatus().hintsCoalesced >= 8, "E2.1 duplicate hints are counted as coalesced");
        Check(pipeline.TickObservation() == PendingResult, "E2.1 duplicate burst deferred");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(duplicateId) != nullptr, "E2.1 duplicate burst reconciles once");

        // (g) case-only hints coalesce under the case-insensitive path key.
        assets::AssetId caseId;
        Check(assets::GenerateAssetId(caseId), "E2.1 case identity");
        const filesystem::AbsolutePath casePath = assetsRoot.AddFilePath("obs_case.tasset");
        Check(WriteTextAsset(files, casePath, containers::StringView(""), Metadata(caseId)), "E2.1 case write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, casePath);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(casePath));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 case create deferred");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(caseId) != nullptr, "E2.1 case create cataloged");
        containers::String upper(casePath.AsStringView().Data(), casePath.AsStringView().Length());
        for (u32 index = 0; index < upper.Length(); ++index)
            if (upper[index] >= 'a' && upper[index] <= 'z')
                upper[index] = static_cast<char>(upper[index] - 'a' + 'A');
        const filesystem::AbsolutePath upperPath =
            filesystem::AbsolutePath::CreateFilePath(containers::StringView(upper.AsChar(), upper.Length()));
        observation.Emit(filesystem::DirectoryObservationKind::Renamed, upperPath);
        observation.Emit(filesystem::DirectoryObservationKind::Renamed, casePath);
        Check(pipeline.GetObservationStatus().pendingPaths == 1, "E2.1 case-only hints coalesce to one path");
        Check(pipeline.TickObservation() == PendingResult, "E2.1 case-only change deferred");
        Check(DrainObservation(pipeline) == Success && sources.LookupAsset(caseId) != nullptr,
              "E2.1 case-only change reconciles without a duplicate row");

        // (h) a partial authored source may reconcile into an explicit scan
        // failure, then a later completed write is observed and repaired.
        assets::AssetId partialId;
        Check(assets::GenerateAssetId(partialId), "E2.1 partial identity");
        const filesystem::AbsolutePath partialPath = assetsRoot.AddFilePath("obs_partial.tasset");
        Check(WriteTextAsset(files, partialPath, containers::StringView("partial"), Metadata(partialId)), "E2.1 partial write");
        observation.Emit(filesystem::DirectoryObservationKind::Modified, partialPath);
        observation.Emit(filesystem::DirectoryObservationKind::Modified, MetadataPath(partialPath));
        Check(pipeline.TickObservation() == PendingResult, "E2.1 partial write deferred");
        Check(pipeline.GetObservationStatus().deferredPaths == 2 && pipeline.GetObservationStatus().pendingPaths == 2,
              "E2.1 partial write is tracked as deferred");
        Check(DrainObservation(pipeline) == Success, "E2.1 partial authored bytes reconcile after settling");
        Check(sources.LookupAsset(partialId) != nullptr &&
                  sources.LookupAsset(partialId)->referenceScanState == assets::SourceReferenceScanState::ScanFailed,
              "E2.1 partial authored bytes remain visible with explicit scan failure");
        Check(WriteBytes(files, partialPath, nullptr, 0u), "E2.1 partial source completion");
        observation.Emit(filesystem::DirectoryObservationKind::Modified, partialPath);
        Check(pipeline.TickObservation() == PendingResult, "E2.1 completed source write defers");
        Check(DrainObservation(pipeline) == Success, "E2.1 completed source write reconciles");
        Check(sources.LookupAsset(partialId) != nullptr &&
                  sources.LookupAsset(partialId)->referenceScanState == assets::SourceReferenceScanState::Scanned,
              "E2.1 completed source repairs the prior partial-write scan failure");

        // (i) overflow degrades to an authoritative rescan, and a second
        // overflow arriving after reconciliation starts is not acknowledged by
        // the older generation.
        const u64 rescansBeforeOverflow = pipeline.GetObservationStatus().rescansCompleted;
        observation.EmitOverflow();
        Check(pipeline.GetObservationStatus().overflow, "E2.1 overflow is explicit");
        Check(pipeline.TickObservation() == PendingResult, "E2.1 overflow starts authoritative reconciliation");
        observation.EmitOverflow();
        Check(pipeline.GetObservationStatus().overflow, "E2.1 late overflow remains explicit during reconciliation");
        Check(DrainObservation(pipeline) == Success, "E2.1 late overflow receives a later reconciliation");
        Check(pipeline.GetObservationStatus().rescansCompleted >= rescansBeforeOverflow + 2,
              "E2.1 late overflow cannot be acknowledged by the older reconciliation");
        Check(!pipeline.GetObservationStatus().overflow, "E2.1 overflow clears after reconciliation");

        // (j) E1F transaction artifacts are never observed as sources.
        const u64 suppressedBefore = pipeline.GetObservationStatus().hintsSuppressed;
        const u64 rescansBeforeArtifacts = pipeline.GetObservationStatus().rescansCompleted;
        observation.Emit(filesystem::DirectoryObservationKind::Added, SuffixedPath(createPath, assets::AssetTransactionStageSuffix));
        observation.Emit(filesystem::DirectoryObservationKind::Added, SuffixedPath(createPath, assets::AssetTransactionBackupSuffix));
        observation.Emit(filesystem::DirectoryObservationKind::Added, journalPath);
        observation.Emit(filesystem::DirectoryObservationKind::Added, SuffixedPath(journalPath, ".stage"));
        Check(pipeline.GetObservationStatus().pendingPaths == 0, "E2.1 transaction artifacts are not queued");
        Check(pipeline.GetObservationStatus().hintsSuppressed == suppressedBefore + 4, "E2.1 transaction artifacts are suppressed");
        Check(pipeline.TickObservation() == Success, "E2.1 suppressed artifacts need no reconciliation");
        Check(pipeline.GetObservationStatus().rescansCompleted == rescansBeforeArtifacts, "E2.1 artifacts trigger no rescan");

        // Suppression is not only a callback filter: transaction-owned files
        // physically present during an authoritative inventory must never become
        // source rows either.
        const filesystem::AbsolutePath physicalStage = SuffixedPath(createPath, assets::AssetTransactionStageSuffix);
        const filesystem::AbsolutePath physicalBackup = SuffixedPath(createPath, assets::AssetTransactionBackupSuffix);
        const filesystem::AbsolutePath physicalJournalStage = SuffixedPath(journalPath, ".stage");
        const u8 transactionSentinel = 0x5au;
        Check(WriteBytes(files, physicalStage, &transactionSentinel, 1u) &&
                  WriteBytes(files, physicalBackup, &transactionSentinel, 1u) &&
                  WriteBytes(files, journalPath, &transactionSentinel, 1u) &&
                  WriteBytes(files, physicalJournalStage, &transactionSentinel, 1u),
              "E2.1 physical transaction artifacts written");
        observation.EmitOverflow();
        Check(DrainObservation(pipeline) == Success, "E2.1 authoritative rescan with transaction artifacts completes");
        Check(sources.LookupResource(physicalStage) == nullptr && sources.LookupResource(physicalBackup) == nullptr &&
                  sources.LookupResource(journalPath) == nullptr && sources.LookupResource(physicalJournalStage) == nullptr,
              "E2.1 authoritative rescan excludes physical transaction artifacts");
        static_cast<void>(files.DeleteFile(physicalStage));
        static_cast<void>(files.DeleteFile(physicalBackup));
        static_cast<void>(files.DeleteFile(journalPath));
        static_cast<void>(files.DeleteFile(physicalJournalStage));

        // (k) bounded per-tick promotion with later hints preserved.
        assets::AssetId boundId1;
        assets::AssetId boundId2;
        assets::AssetId boundId3;
        Check(assets::GenerateAssetId(boundId1) && assets::GenerateAssetId(boundId2) && assets::GenerateAssetId(boundId3),
              "E2.1 bounded identities");
        const filesystem::AbsolutePath boundPath1 = assetsRoot.AddFilePath("obs_bound_1.tasset");
        const filesystem::AbsolutePath boundPath2 = assetsRoot.AddFilePath("obs_bound_2.tasset");
        const filesystem::AbsolutePath boundPath3 = assetsRoot.AddFilePath("obs_bound_3.tasset");
        Check(WriteTextAsset(files, boundPath1, containers::StringView(""), Metadata(boundId1)) &&
                  WriteTextAsset(files, boundPath2, containers::StringView(""), Metadata(boundId2)) &&
                  WriteTextAsset(files, boundPath3, containers::StringView(""), Metadata(boundId3)),
              "E2.1 bounded writes");
        observation.Emit(filesystem::DirectoryObservationKind::Added, boundPath1);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(boundPath1));
        observation.Emit(filesystem::DirectoryObservationKind::Added, boundPath2);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(boundPath2));
        observation.Emit(filesystem::DirectoryObservationKind::Added, boundPath3);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(boundPath3));
        const u64 rescansBeforeBounded = pipeline.GetObservationStatus().rescansCompleted;
        Check(pipeline.TickObservation() == PendingResult, "E2.1 bounded tick 1 defers");
        Check(pipeline.GetObservationStatus().pendingPaths == 6, "E2.1 all six paths are pending");
        Check(pipeline.TickObservation() == PendingResult, "E2.1 bounded tick 2 promotes at most the per-tick limit");
        Check(pipeline.GetObservationStatus().pendingPaths == 4, "E2.1 per-tick promotion is bounded");
        assets::AssetId boundId4;
        Check(assets::GenerateAssetId(boundId4), "E2.1 late identity");
        const filesystem::AbsolutePath boundPath4 = assetsRoot.AddFilePath("obs_bound_4.tasset");
        Check(WriteTextAsset(files, boundPath4, containers::StringView(""), Metadata(boundId4)), "E2.1 late write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, boundPath4);
        observation.Emit(filesystem::DirectoryObservationKind::Added, MetadataPath(boundPath4));
        Check(pipeline.GetObservationStatus().pendingPaths == 6, "E2.1 late hints accumulate without loss");

        // The catalog swap happens before staged publication reconciliation.
        // Observe the actual budgeted-record counter so background retirement
        // ticks cannot masquerade as proof of maximumReconcileOperationsPerTick.
        bool observedCatalogCommit = false;
        for (u32 tick = 0; tick < 200000u && !observedCatalogCommit; ++tick)
        {
            Check(pipeline.TickObservation() == PendingResult, "E2.1 bounded reconcile remains pending before catalog commit");
            observedCatalogCommit = sources.LookupAsset(boundId1) != nullptr;
            concurrency::YieldCurrentThread();
        }
        Check(observedCatalogCommit, "E2.1 bounded reconcile reaches asynchronous catalog commit");
        if (observedCatalogCommit)
        {
            u32 previousOperations = pipeline.GetObservationStatus().reconcileOperationsCompleted;
            u32 ticksWithProgress = 0;
            for (u32 tick = 0; tick < 200000u && ticksWithProgress < 2u; ++tick)
            {
                const asset_streaming::ProjectAssetPipelineResult result = pipeline.TickObservation();
                Check(result == PendingResult, "E2.1 budget-one reconcile stays pending while proving staged progress");
                const u32 completed = pipeline.GetObservationStatus().reconcileOperationsCompleted;
                Check(completed >= previousOperations &&
                          completed - previousOperations <= observationConfig.maximumReconcileOperationsPerTick,
                      "E2.1 owner tick completes no more than the configured reconcile-record budget");
                if (completed > previousOperations)
                    ++ticksWithProgress;
                previousOperations = completed;
                concurrency::YieldCurrentThread();
            }
            Check(ticksWithProgress >= 2u, "E2.1 budget-one reconciliation advances on distinct owner ticks");
        }
        Check(DrainObservation(pipeline) == Success, "E2.1 bounded drain completes across staged reconciliations");
        Check(pipeline.GetObservationStatus().pendingPaths == 0, "E2.1 no hint is lost across bounded ticks");
        Check(pipeline.GetObservationStatus().rescansCompleted >= rescansBeforeBounded + 1, "E2.1 bounded drain reconciles");
        Check(sources.LookupAsset(boundId1) != nullptr && sources.LookupAsset(boundId2) != nullptr &&
                  sources.LookupAsset(boundId3) != nullptr && sources.LookupAsset(boundId4) != nullptr,
              "E2.1 every bounded asset is cataloged");

        // (l) stop/restart cannot abandon an authoritative reconciliation after
        // it has started. The caller must finish TickObservation first.
        observation.EmitOverflow();
        Check(pipeline.TickObservation() == PendingResult, "E2.1 stop-safety reconciliation starts");
        Check(pipeline.GetObservationStatus().reconcilePending, "E2.1 stop-safety reconciliation is active");
        Check(pipeline.DisableObservation() == BusyResult && observation.IsRunning(),
              "E2.1 disable refuses to abandon active reconciliation");
        Check(DrainObservation(pipeline) == Success, "E2.1 active reconciliation completes before stop");

        observation.Emit(filesystem::DirectoryObservationKind::Modified, createPath);
        Check(pipeline.GetObservationStatus().pendingPaths == 1, "E2.1 stop-safety ordinary hint is queued");
        Check(pipeline.DisableObservation() == BusyResult && observation.IsRunning(),
              "E2.1 disable refuses to discard queued unreconciled hint");
        Check(DrainObservation(pipeline) == Success, "E2.1 queued stop-safety hint reconciles before stop");

        assets::AssetId stopRaceId;
        Check(assets::GenerateAssetId(stopRaceId), "E2.1 stop-race identity");
        const filesystem::AbsolutePath stopRacePath = assetsRoot.AddFilePath("obs_stop_race.tasset");
        Check(WriteTextAsset(files, stopRacePath, containers::StringView(""), Metadata(stopRaceId)), "E2.1 stop-race write");
        observation.EmitDuringNextStop(filesystem::DirectoryObservationKind::Added, stopRacePath);
        Check(pipeline.DisableObservation() == Success, "E2.1 disable reconciles callback delivered during Stop");
        Check(!observation.IsRunning() && sources.LookupAsset(stopRaceId) != nullptr,
              "E2.1 successful disable cannot discard a Stop-window filesystem change");
        Check(pipeline.EnableObservation(observation, observationConfig) == Success, "E2.1 observation restarts after stop-race disable");
        Check(DrainObservation(pipeline) == Success, "E2.1 post-stop-race restart catch-up completes");

        // (m) pipeline shutdown stops an active observer with queued work, then
        // reinitialization can start observation again.
        assets::AssetId shutdownId;
        Check(assets::GenerateAssetId(shutdownId), "E2.1 shutdown identity");
        const filesystem::AbsolutePath shutdownPath = assetsRoot.AddFilePath("obs_shutdown.tasset");
        Check(WriteTextAsset(files, shutdownPath, containers::StringView(""), Metadata(shutdownId)), "E2.1 shutdown write");
        observation.Emit(filesystem::DirectoryObservationKind::Added, shutdownPath);
        Check(pipeline.GetObservationStatus().pendingPaths == 1, "E2.1 hint queued before stop");
        Check(pipeline.Shutdown() == Success, "E2.1 active-observer pipeline shutdown");
        Check(!pipeline.IsInitialized() && !observation.IsRunning(), "E2.1 pipeline shutdown stops the borrowed observer");
        observation.Emit(filesystem::DirectoryObservationKind::Added, shutdownPath);
        Check(pipeline.Initialize(files, sources, streamer, compilerSpan, importerSpan, config) == Success,
              "E2.1 pipeline reinitializes after observed shutdown");
        Check(pipeline.TickObservation() == ObservationNotActive, "E2.1 tick after stop is rejected");
        Check(pipeline.EnableObservation(observation, observationConfig) == Success, "E2.1 observation restarts");
        Check(DrainObservation(pipeline) == Success, "E2.1 restarted observation performs authoritative catch-up");
        Check(sources.LookupAsset(shutdownId) != nullptr, "E2.1 queued pre-shutdown change is recovered after restart catch-up");
        Check(pipeline.DisableObservation() == Success, "E2.1 observation stops again");

        // (n) supplement the deterministic seam with a native Windows watcher
        // smoke: real ReadDirectoryChangesW delivery, clean stop/restart, and a
        // case-only source/sidecar rename where the platform supports it.
#if defined(_WIN32)
        {
            asset_streaming::ProjectAssetObservationConfig nativeConfig = observationConfig;
            nativeConfig.settleTicks = 1;
            nativeConfig.maximumDeferralTicks = 2;
            Check(pipeline.EnableObservation(nativeConfig) == Success, "E2.1 native Windows observation starts");
            if (pipeline.IsObservingSources())
            {
                Check(DrainObservation(pipeline) == Success, "E2.1 native observation start catch-up completes");
                const filesystem::AbsolutePath readyPath =
                    SuffixedPath(assetsRoot.AddFilePath("obs_native_ready.tasset"), assets::AssetTransactionStageSuffix);
                bool nativeReady = false;
                for (u32 attempt = 0; attempt < 50u && !nativeReady; ++attempt)
                {
                    const u64 suppressedBeforeReady = pipeline.GetObservationStatus().hintsSuppressed;
                    const u8 marker = static_cast<u8>(attempt);
                    Check(WriteBytes(files, readyPath, &marker, 1u), "E2.1 native watcher readiness sentinel write");
                    nativeReady = WaitForSuppressedObservationHint(pipeline, suppressedBeforeReady, 100u);
                }
                Check(nativeReady, "E2.1 native watcher arms before physical source event");
                assets::AssetId nativeId;
                Check(assets::GenerateAssetId(nativeId), "E2.1 native watcher identity");
                const filesystem::AbsolutePath nativePath = assetsRoot.AddFilePath("obs_native.tasset");
                const asset_streaming::ProjectAssetObservationStatus beforeNative = pipeline.GetObservationStatus();
                Check(WriteTextAsset(files, nativePath, containers::StringView(""), Metadata(nativeId)),
                      "E2.1 native watcher write");
                Check(WaitForObservationPathHint(pipeline, beforeNative.pendingPaths, beforeNative.hintsRejected),
                      "E2.1 native watcher receives a parsed path event without overflow fallback");
                Check(DrainObservation(pipeline) == Success, "E2.1 native watcher reconciles the physical event");
                Check(sources.LookupAsset(nativeId) != nullptr, "E2.1 native watcher catalogs the physical create");
                Check(pipeline.DisableObservation() == Success, "E2.1 native Windows observation stops");
                static_cast<void>(files.DeleteFile(readyPath));
            }

            Check(pipeline.EnableObservation(nativeConfig) == Success, "E2.1 native Windows observation restarts");
            if (pipeline.IsObservingSources())
            {
                Check(DrainObservation(pipeline) == Success, "E2.1 restarted native observation catch-up completes");
                const filesystem::AbsolutePath readyPath =
                    SuffixedPath(assetsRoot.AddFilePath("obs_native_restart_ready.tasset"), assets::AssetTransactionStageSuffix);
                bool nativeReady = false;
                for (u32 attempt = 0; attempt < 50u && !nativeReady; ++attempt)
                {
                    const u64 suppressedBeforeReady = pipeline.GetObservationStatus().hintsSuppressed;
                    const u8 marker = static_cast<u8>(attempt + 50u);
                    Check(WriteBytes(files, readyPath, &marker, 1u), "E2.1 restarted watcher readiness sentinel write");
                    nativeReady = WaitForSuppressedObservationHint(pipeline, suppressedBeforeReady, 100u);
                }
                Check(nativeReady, "E2.1 restarted native watcher arms before case rename");
                const filesystem::AbsolutePath upperCasePath = assetsRoot.AddFilePath("OBS_CASE.TASSET");
                const asset_streaming::ProjectAssetObservationStatus beforeCaseRename = pipeline.GetObservationStatus();
                const bool caseRename = filesystem::SystemIO::MoveFile(casePath.AsChar(), upperCasePath.AsChar()) &&
                                        filesystem::SystemIO::MoveFile(MetadataPath(casePath).AsChar(), MetadataPath(upperCasePath).AsChar());
                Check(caseRename, "E2.1 native case-only source/sidecar rename");
                if (caseRename)
                {
                    Check(WaitForObservationPathHint(pipeline, beforeCaseRename.pendingPaths, beforeCaseRename.hintsRejected),
                          "E2.1 native watcher parses the case-only rename without overflow fallback");
                    Check(DrainObservation(pipeline) == Success, "E2.1 native case-only rename reconciles");
                    const assets::SourceRecord* const caseRecord = sources.LookupAsset(caseId);
                    Check(caseRecord != nullptr && std::strcmp(caseRecord->path.AsChar(), upperCasePath.AsChar()) == 0,
                          "E2.1 case-only rename preserves identity and records on-disk casing");
                }
                Check(pipeline.DisableObservation() == Success, "E2.1 restarted native observation stops cleanly");
                static_cast<void>(files.DeleteFile(readyPath));
            }

            // A missing root must not kill its worker permanently. Start the
            // real backend on an absent directory, observe the explicit overflow,
            // then create the root and repeatedly write the expected file until a
            // real parsed path record proves recovery. No fixed readiness sleep.
            const filesystem::AbsolutePath recoveredRoot = root.AddDirPath("RecoveredWatchRoot");
            static_cast<void>(DeleteDirectoryTree(files, recoveredRoot));
            const filesystem::AbsolutePath recoveredPath = recoveredRoot.AddFilePath("recovered.tasset");
            NativeObservationProbe recoveryProbe(recoveredPath);
            asset_streaming::PlatformDirectoryObserver recoveryObserver;
            const containers::ArraySpan<const filesystem::AbsolutePath> recoveredRoots(&recoveredRoot, 1u);
            Check(recoveryObserver.Start(recoveredRoots, &NativeObservationProbe::Callback, &recoveryProbe,
                                         filesystem::DirectoryObserverConfig{}),
                  "E2.1 native missing-root observer starts");
            Check(recoveryProbe.overflowSeen.TryWait(5000u), "E2.1 missing root reports overflow while unavailable");
            Check(files.CreatePath(recoveredRoot), "E2.1 missing watched root is created later");
            bool recovered = false;
            for (u32 attempt = 0; attempt < 60u && !recovered; ++attempt)
            {
                const u8 marker = static_cast<u8>(attempt);
                Check(WriteBytes(files, recoveredPath, &marker, 1u), "E2.1 recovered-root probe write");
                recovered = recoveryProbe.expectedPathSeen.TryWait(100u);
            }
            Check(recovered, "E2.1 missing-root watcher retries and later delivers a real path record");
            Check(recoveryObserver.Stop(), "E2.1 recovered native watcher stops cleanly");
            static_cast<void>(DeleteDirectoryTree(files, recoveredRoot));
        }
#endif

        // (o) manual Rescan behavior is unchanged.
        Check(pipeline.Rescan(rootSpan) == Success, "E2.1 manual rescan still succeeds");
        Check(sources.LookupAsset(createId) != nullptr, "E2.1 manual rescan preserves the catalog");

        // (p) full pipeline shutdown may safely cancel/drain an active
        // observation reconcile because the pipeline itself is terminating.
        Check(pipeline.EnableObservation(observation, observationConfig) == Success, "E2.1 observation starts for active shutdown");
        Check(pipeline.GetObservationStatus().reconcilePending, "E2.1 active-shutdown catch-up is in flight");
        Check(pipeline.Shutdown() == Success, "E2.1 shutdown drains active observation reconcile");
        Check(!pipeline.IsInitialized() && !observation.IsRunning(), "E2.1 active-reconcile shutdown releases observer and pipeline");
    }

    // ---------------------------------------------------------------------
    // E2.6 P2 public pipeline completion. Keep this fixture isolated from the
    // older automatic/publication matrices above: it exercises only the shared
    // ProjectAssetPipeline freshness and asset-level reimport surfaces that the
    // headless frontend will consume.
    // ---------------------------------------------------------------------
    {
        using PipelineResult = asset_streaming::ProjectAssetPipelineResult;
        using AutomaticState = asset_streaming::ProjectAssetAutomaticRecookState;
        using DirtyReason = assets::DirtyReason;
        using AutomaticReason = asset_streaming::ProjectAssetAutomaticRecookDirtyReason;

        const filesystem::AbsolutePath p2Root = root.AddDirPath("E26P2PublicPipeline");
        const filesystem::AbsolutePath p2AssetsRoot = p2Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p2DerivedRoot = p2Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p2IndexRoot = p2Root.AddDirPath("Index");
        const filesystem::AbsolutePath p2LooseRoot = p2Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p2Journal = p2AssetsRoot.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p2Root) && files.CreatePath(p2AssetsRoot) && files.CreatePath(p2DerivedRoot) &&
                  files.CreatePath(p2IndexRoot) && files.CreatePath(p2LooseRoot),
              "E2.6 P2 create public-pipeline fixture roots");

        assets::AssetId p2MainId;
        assets::AssetId p2DependencyId;
        assets::AssetId p2DirtyId;
        assets::AssetId p2UnsavedId;
        assets::AssetId p2RepairId;
        assets::AssetId p2MissingId;
        Check(assets::GenerateAssetId(p2MainId) && assets::GenerateAssetId(p2DependencyId) &&
                  assets::GenerateAssetId(p2DirtyId) && assets::GenerateAssetId(p2UnsavedId) &&
                  assets::GenerateAssetId(p2RepairId) && assets::GenerateAssetId(p2MissingId),
              "E2.6 P2 generate public-pipeline identities");

        assets::AssetOutput p2SecondaryOutput;
        p2SecondaryOutput.key = containers::String("secondary");
        p2SecondaryOutput.type = TestOutputType;
        assets::AssetMetadata p2MainMetadata = Metadata(p2MainId);
        p2MainMetadata.outputs.PushBack(p2SecondaryOutput);
        const resources::ResourceReference p2MainOutput = OutputReference(p2MainId);
        const resources::ResourceReference p2SecondaryOutputReference =
            assets::GetAssetOutputReference(p2MainId, p2SecondaryOutput);
        const resources::ResourceReference p2DependencyOutput = OutputReference(p2DependencyId);
        const resources::ResourceReference p2DirtyOutput = OutputReference(p2DirtyId);
        const resources::ResourceReference p2UnsavedOutput = OutputReference(p2UnsavedId);
        const resources::ResourceReference p2RepairOutput = OutputReference(p2RepairId);

        const filesystem::AbsolutePath p2MainPath = p2AssetsRoot.AddFilePath("multi.tasset");
        const filesystem::AbsolutePath p2DependencyPath = p2AssetsRoot.AddFilePath("dependency.tasset");
        const filesystem::AbsolutePath p2DirtyPath = p2AssetsRoot.AddFilePath("unrelated.tasset");
        const filesystem::AbsolutePath p2UnsavedPath = p2AssetsRoot.AddFilePath("unsaved_index.tasset");
        const filesystem::AbsolutePath p2RepairPath = p2AssetsRoot.AddFilePath("repair.tasset");
        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-a\n"), p2MainMetadata) &&
                  WriteTextAsset(files, p2DependencyPath, containers::StringView("p2-dependency\n"), Metadata(p2DependencyId)) &&
                  WriteTextAsset(files, p2DirtyPath, containers::StringView("p2-unrelated-a\n"), Metadata(p2DirtyId)) &&
                  WriteTextAsset(files, p2UnsavedPath, containers::StringView("p2-unsaved\n"), Metadata(p2UnsavedId)) &&
                  WriteTextAsset(files, p2RepairPath, containers::StringView("p2-repair\n"), Metadata(p2RepairId)),
              "E2.6 P2 write public-pipeline source fixtures");

        containers::DynamicArray<assets::SourceRoot> p2Roots(memory::pools::Assets::GetInstance());
        p2Roots.PushBack({p2AssetsRoot, false});
        const containers::ArraySpan<const assets::SourceRoot> p2RootSpan(p2Roots.TypedData(), p2Roots.Size());
        assets::SourceDatabase p2Sources;
        Check(p2Sources.Rescan(files, p2RootSpan) == assets::SourceDatabaseResult::Success,
              "E2.6 P2 scan public-pipeline source catalog");

        RequestResolveBlockState p2Resolve;
        p2Resolve.block.SetValue(false);
        p2Resolve.started.SetValue(false);
        p2Resolve.release.SetValue(false);
        p2Resolve.fail.SetValue(false);
        p2Resolve.resolveCalls.SetValue(0u);
        assets::SourceImporterDescriptor p2Importer = importer;
        p2Importer.sourceType = resources::InvalidResourceTypeId;
        p2Importer.createBuildRequest = &CreateCallbackBuildRequest;
        p2Importer.userData = &p2Resolve;
        containers::DynamicArray<assets::SourceImporterDescriptor> p2Importers(memory::pools::Assets::GetInstance());
        p2Importers.PushBack(p2Importer);
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p2ImporterSpan(p2Importers.TypedData(), p2Importers.Size());

        asset_streaming::ProjectAssetPipelineConfig p2Config = config;
        p2Config.build.artifactCacheRoot = p2DerivedRoot;
        p2Config.build.dependencyIndexRoot = p2IndexRoot;
        p2Config.publication.artifactCacheRoot = p2DerivedRoot;
        p2Config.publication.looseRoot = p2LooseRoot;
        p2Config.operationJournalPath = p2Journal;
        p2Config.automaticRecook.enabled = false;

        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.dependencyOwnerSource2 = {};
        block.dependencyOutput2 = {};
        block.contentFromSource = true;
        asset_streaming::ProjectAssetPipeline p2Pipeline;
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, compilerSpan, p2ImporterSpan, p2Config) == PipelineResult::Success &&
                  !p2Pipeline.IsAutomaticRecookEnabled() && p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 public-pipeline fixture initializes with automatic policy disabled");

        const auto sameFreshness = [](const asset_streaming::ProjectAssetBuildFreshness& left,
                                      const asset_streaming::ProjectAssetBuildFreshness& right) noexcept
        {
            return left.output == right.output && left.reason == right.reason;
        };
        const auto sameAutomaticStatus = [](const asset_streaming::ProjectAssetAutomaticRecookStatus& left,
                                            const asset_streaming::ProjectAssetAutomaticRecookStatus& right) noexcept
        {
            return left.state == right.state && left.dirtyReasons == right.dirtyReasons && left.pendingAssets == right.pendingAssets &&
                   left.pendingInputs == right.pendingInputs && left.pendingOutputs == right.pendingOutputs &&
                   left.pendingRepairOutputs == right.pendingRepairOutputs && left.pendingReimportOutputs == right.pendingReimportOutputs &&
                   left.pendingDiagnostics == right.pendingDiagnostics && left.retryEntries == right.retryEntries &&
                   left.activeRoots == right.activeRoots &&
                   left.completedRoots == right.completedRoots && left.activeAcceptedGeneration == right.activeAcceptedGeneration &&
                   left.capturedEvidenceBytes == right.capturedEvidenceBytes &&
                   left.pendingDeliveryOutputs == right.pendingDeliveryOutputs &&
                   left.pendingDeliveryEvidenceBytes == right.pendingDeliveryEvidenceBytes &&
                   left.retryRequested == right.retryRequested && left.cancellationRequested == right.cancellationRequested &&
                   left.recoveryRequired == right.recoveryRequired;
        };
        const auto cook = [&](const resources::ResourceReference output) noexcept
        {
            asset_streaming::ProjectAssetRequest request = p2Pipeline.Request(output);
            if (!request.IsValid())
                return false;
            request.Wait();
            const bool success = request.Poll() == PipelineResult::Success;
            request.Reset();
            return success;
        };
        const auto drainToDisabled = [&]() noexcept
        {
            for (u32 pass = 0; pass < 8u; ++pass)
            {
                if (p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled)
                    return true;
                if (DrainAutomaticRecook(p2Pipeline) != PipelineResult::Success)
                    return false;
            }
            return p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled;
        };
        const auto drainToIdle = [&]() noexcept
        {
            for (u32 pass = 0; pass < 8u; ++pass)
            {
                if (DrainAutomaticRecook(p2Pipeline) != PipelineResult::Success)
                    return false;
                const asset_streaming::ProjectAssetAutomaticRecookStatus status = p2Pipeline.GetAutomaticRecookStatus();
                if (status.state != AutomaticState::Idle || status.pendingDeliveryOutputs != 0u)
                    continue;
                // A successful build tick may publish Idle before clearing the
                // transient explicit-activity bit. One quiescent owner tick is a
                // deterministic public proof that terminal Idle is fully settled.
                if (p2Pipeline.TickAutomaticRecook() == PipelineResult::Success &&
                    p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Idle &&
                    p2Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u)
                    return true;
            }
            return false;
        };

        // Freshness failure paths are semantically read-only and must not scribble
        // caller output. Use a valid current asset with an injected resolver failure
        // so this is stronger than merely querying an unknown output.
        asset_streaming::ProjectAssetBuildFreshness p2Sentinel;
        p2Sentinel.output = p2DependencyOutput;
        p2Sentinel.reason = DirtyReason::CompilerChanged;
        asset_streaming::ProjectAssetBuildFreshness p2Freshness = p2Sentinel;
        Check(p2Pipeline.QueryBuildFreshness({}, p2Freshness) == PipelineResult::InvalidArgument &&
                  sameFreshness(p2Freshness, p2Sentinel),
              "E2.6 P2 invalid freshness query leaves caller output unchanged");
        const resources::ResourceReference p2Untyped(p2MainOutput.GetPath(), resources::InvalidResourceTypeId);
        Check(p2Pipeline.QueryBuildFreshness(p2Untyped, p2Freshness) == PipelineResult::InvalidArgument &&
                  sameFreshness(p2Freshness, p2Sentinel),
              "E2.6 P2 untyped freshness query leaves caller output unchanged");
        p2Resolve.fail.SetValue(true);
        asset_streaming::ProjectAssetPipelineFailure p2ResolveFailure;
        Check(p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness, &p2ResolveFailure) == PipelineResult::BuildRequestFailed &&
                  p2ResolveFailure.build != assets::ProjectAssetBuildResult::Success && sameFreshness(p2Freshness, p2Sentinel),
              "E2.6 P2 resolver failure leaves freshness output unchanged");
        p2Resolve.fail.SetValue(false);

        Check(p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.output == p2MainOutput && p2Freshness.reason == DirtyReason::Missing,
              "E2.6 P2 public freshness reports Missing before first cook");
        const assets::SourceRecord* const p2MainSource = p2Sources.LookupAsset(p2MainId);
        block.dependencyOwnerSource = p2MainSource != nullptr
                                          ? resources::ResourceReference(p2MainSource->sourceIdentity, TestSourceType)
                                          : resources::ResourceReference{};
        block.dependencyOutput = p2DependencyOutput;
        block.dependencyEnabled.SetValue(true);
        Check(cook(p2DependencyOutput) && cook(p2MainOutput) && cook(p2SecondaryOutputReference) && cook(p2DirtyOutput),
              "E2.6 P2 establish committed freshness baseline for multi-output roots, dependency, and unrelated asset");
        Check(p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::UpToDate,
              "E2.6 P2 public freshness reports UpToDate after cook");

        Check(WriteTextAsset(files, p2DependencyPath, containers::StringView("p2-dependency-content-changed\n"), Metadata(p2DependencyId)) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success && cook(p2DependencyOutput) &&
                  p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::DependenciesChanged,
              "E2.6 P2 public freshness reports DependenciesChanged for committed Generated dependency content drift");
        Check(cook(p2MainOutput) && cook(p2SecondaryOutputReference),
              "E2.6 P2 recook committed Generated dependency content baseline");

        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-source-changed\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::SourceChanged,
              "E2.6 P2 public freshness reports SourceChanged for source-input drift");
        Check(cook(p2MainOutput) && cook(p2SecondaryOutputReference),
              "E2.6 P2 recook source-changed multi-output baseline");

        // Compiler version drift remains resolvable, so the public query can
        // distinguish CompilerChanged from unsupported/importer failure.
        Check(p2Pipeline.Shutdown() == PipelineResult::Success,
              "E2.6 P2 shutdown v1 compiler fixture before resolvable compiler drift");
        assets::CompilerDescriptor p2CompilerV2 = compiler;
        p2CompilerV2.version = compiler.version + 1u;
        containers::DynamicArray<assets::CompilerDescriptor> p2CompilersV2(memory::pools::Assets::GetInstance());
        p2CompilersV2.PushBack(p2CompilerV2);
        const containers::ArraySpan<const assets::CompilerDescriptor> p2CompilerV2Span(p2CompilersV2.TypedData(), p2CompilersV2.Size());
        Check(p2Pipeline.Initialize(files, p2Sources, streamer, p2CompilerV2Span, p2ImporterSpan, p2Config) == PipelineResult::Success &&
                  p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::CompilerChanged,
              "E2.6 P2 public freshness reports CompilerChanged for resolvable compiler-version drift");
        Check(cook(p2DependencyOutput) && cook(p2MainOutput) && cook(p2SecondaryOutputReference),
              "E2.6 P2 recook multi-output baseline under compiler v2");

        // A completed retained graph request is Busy, but a released request may
        // leave committed VADI dirty-to-disk. That unsaved committed state is still
        // valid read-only freshness state and must not be mislabeled Busy.
        asset_streaming::ProjectAssetRequest p2UnsavedRequest = p2Pipeline.Request(p2UnsavedOutput);
        Check(p2UnsavedRequest.IsValid(), "E2.6 P2 unsaved-VADI request starts");
        p2UnsavedRequest.Wait();
        p2UnsavedRequest.Reset();
        Check(p2Pipeline.QueryBuildFreshness(p2UnsavedOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::UpToDate,
              "E2.6 P2 unsaved committed VADI is queryable and is not treated as Busy");

        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-manual-busy\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success,
              "E2.6 P2 accept changed source before live-manual freshness exclusion");
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        asset_streaming::ProjectAssetRequest p2BusyRequest = p2Pipeline.Request(p2MainOutput);
        Check(p2BusyRequest.IsValid(), "E2.6 P2 live manual request starts");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue() && !p2BusyRequest.HasFinished(); ++spin)
            concurrency::YieldCurrentThread();
        p2Freshness = p2Sentinel;
        Check(block.started.GetValue() && p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Busy &&
                  sameFreshness(p2Freshness, p2Sentinel),
              "E2.6 P2 live manual BuildGraph ownership makes freshness Busy without mutating caller output");
        block.release.SetValue(true);
        p2BusyRequest.Wait();
        Check(p2BusyRequest.Poll() == PipelineResult::Success, "E2.6 P2 live manual Busy fixture completes");
        p2BusyRequest.Reset();
        block.block = false;
        block.release.SetValue(false);
        Check(cook(p2SecondaryOutputReference), "E2.6 P2 finish secondary output after manual Busy fixture");

        // Missing asset reimport is transactional and cannot create coordinator
        // state. The supported multi-output asset then proves explicit-only cache
        // hits use the one automatic coordinator without enabling automatic policy.
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2BeforeMissingReimport = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Pipeline.RequestAssetReimport(p2MissingId) == PipelineResult::BuildRequestFailed &&
                  sameAutomaticStatus(p2Pipeline.GetAutomaticRecookStatus(), p2BeforeMissingReimport),
              "E2.6 P2 missing asset reimport fails without coordinator mutation");
        const u32 p2ExactHitCompileCalls = block.compileCalls.GetValue();
        Check(p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success,
              "E2.6 P2 disabled-policy multi-output reimport is admitted");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2ExplicitPending = p2Pipeline.GetAutomaticRecookStatus();
        Check(!p2Pipeline.IsAutomaticRecookEnabled() && p2ExplicitPending.state == AutomaticState::Pending &&
                  p2ExplicitPending.pendingReimportOutputs == 2u &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitPending.dirtyReasons, AutomaticReason::ExplicitReimport) &&
                  !asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitPending.dirtyReasons, AutomaticReason::StartupRecovery) &&
                  ContainsResource(p2Pipeline.GetAutomaticRecookPendingOutputs(), p2MainOutput) &&
                  ContainsResource(p2Pipeline.GetAutomaticRecookPendingOutputs(), p2SecondaryOutputReference) &&
                  p2Pipeline.EnableAutomaticRecook() == PipelineResult::Busy &&
                  p2Pipeline.DisableAutomaticRecook() == PipelineResult::Busy,
              "E2.6 P2 explicit-only multi-output admission exposes exact markers and rejects policy transitions while active");
        Check(drainToDisabled() &&
                  block.compileCalls.GetValue() == p2ExactHitCompileCalls && !p2Pipeline.IsAutomaticRecookEnabled() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 explicit-only exact VDDC hit does not force compiler work and returns coordinator to Disabled");

        // Explicit-only failure/retry must retain reimport intent without sweeping
        // a different accepted dirty asset into the retry.
        Check(WriteTextAsset(files, p2DirtyPath, containers::StringView("p2-unrelated-dirty\n"), Metadata(p2DirtyId)) &&
                  WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-explicit-failure\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  p2Pipeline.QueryBuildFreshness(p2DirtyOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::SourceChanged,
              "E2.6 P2 prepare unrelated dirty asset beside explicit-only reimport failure");
        block.fail.SetValue(true);
        Check(p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == PipelineResult::BuildRequestFailed,
              "E2.6 P2 explicit-only compiler failure reaches retained terminal state");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2ExplicitFailed = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2ExplicitFailed.state == AutomaticState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitFailed.dirtyReasons, AutomaticReason::ExplicitReimport) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitFailed.dirtyReasons, AutomaticReason::FailedBatch),
              "E2.6 P2 failed explicit-only batch retains ExplicitReimport marker");
        block.fail.SetValue(false);
        const u32 p2RetryCompileCalls = block.compileCalls.GetValue();
        Check(p2Pipeline.RetryAutomaticRecook() == PipelineResult::Success &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                                                                 AutomaticReason::ExplicitReimport) &&
                  drainToDisabled() &&
                  block.compileCalls.GetValue() == p2RetryCompileCalls + 2u &&
                  p2Pipeline.QueryBuildFreshness(p2DirtyOutput, p2Freshness) == PipelineResult::Success &&
                  p2Freshness.reason == DirtyReason::SourceChanged &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 explicit-only retry recooks exactly the two requested outputs and leaves unrelated dirty asset untouched");

        // Cancellation retains the same explicit marker and requires an explicit
        // retry; the compiler barrier makes cancellation deterministic.
        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-explicit-cancel\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success,
              "E2.6 P2 prepare explicit-only cancellation fixture");
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(p2Pipeline.TickAutomaticRecook() == PipelineResult::Pending,
              "E2.6 P2 explicit-only cancellation acquires coordinator admission");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue() && p2Pipeline.CancelAutomaticRecook() == PipelineResult::Success &&
                  DrainAutomaticRecook(p2Pipeline) == PipelineResult::BuildRequestFailed,
              "E2.6 P2 explicit-only cancellation reaches retained terminal failure");
        block.block = false;
        block.release.SetValue(false);
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2ExplicitCancelled = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2ExplicitCancelled.state == AutomaticState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitCancelled.dirtyReasons, AutomaticReason::ExplicitReimport) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2ExplicitCancelled.dirtyReasons, AutomaticReason::CancelledBatch) &&
                  p2Pipeline.RetryAutomaticRecook() == PipelineResult::Success &&
                  drainToDisabled() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 cancelled explicit-only batch preserves reimport intent through explicit retry");

        // Enabling automatic policy now may recover the intentionally unrelated
        // dirty asset. Once idle, a normal accepted change plus explicit reimport
        // must merge into the same coordinator, exclude freshness queries while
        // admitted, reject Disable while explicit activity exists, and end Idle.
        Check(p2Pipeline.EnableAutomaticRecook() == PipelineResult::Success && p2Pipeline.IsAutomaticRecookEnabled() && drainToIdle() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Idle,
              "E2.6 P2 policy enable performs one startup recovery before merge proof");
        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-policy-merge\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success,
              "E2.6 P2 policy-enabled explicit reimport merges with accepted-change pending work");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2Merged = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Merged.state == AutomaticState::Pending && p2Merged.pendingReimportOutputs == 2u &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Merged.dirtyReasons, AutomaticReason::AcceptedChange) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Merged.dirtyReasons, AutomaticReason::ExplicitReimport) &&
                  p2Pipeline.DisableAutomaticRecook() == PipelineResult::Busy,
              "E2.6 P2 policy-enabled merge remains one coordinator and blocks disable while explicit activity is pending");
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(p2Pipeline.TickAutomaticRecook() == PipelineResult::Pending,
              "E2.6 P2 merged automatic batch acquires admission");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        p2Freshness = p2Sentinel;
        Check(block.started.GetValue() && p2Pipeline.QueryBuildFreshness(p2MainOutput, p2Freshness) == PipelineResult::Busy &&
                  sameFreshness(p2Freshness, p2Sentinel),
              "E2.6 P2 automatic admission makes freshness Busy without mutating caller output");
        block.release.SetValue(true);
        Check(drainToIdle(),
              "E2.6 P2 merged policy-enabled reimport completes through existing coordinator");
        block.block = false;
        block.release.SetValue(false);
        Check(p2Pipeline.IsAutomaticRecookEnabled() && p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Idle,
              "E2.6 P2 policy-enabled explicit activity retires to Idle without changing policy");

        // A newer accepted generation is itself the recovery trigger for a stale
        // policy-enabled explicit reimport. No separate retry command is required,
        // and the ExplicitReimport marker survives until current truth is rebuilt.
        Check(WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-stale-a\n"), p2MainMetadata) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success,
              "E2.6 P2 prepare policy-enabled stale reimport");
        block.block = true;
        block.started.SetValue(false);
        block.release.SetValue(false);
        Check(p2Pipeline.TickAutomaticRecook() == PipelineResult::Pending,
              "E2.6 P2 stale reimport acquires deterministic compiler barrier");
        for (u32 spin = 0; spin < 2000000u && !block.started.GetValue(); ++spin)
            concurrency::YieldCurrentThread();
        Check(block.started.GetValue() &&
                  WriteTextAsset(files, p2MainPath, containers::StringView("p2-main-stale-b\n"), p2MainMetadata),
              "E2.6 P2 mutate disk truth while explicit reimport compile is live");
        block.release.SetValue(true);
        Check(DrainAutomaticRecook(p2Pipeline) == PipelineResult::BuildRequestFailed,
              "E2.6 P2 stale explicit reimport rejects pre-commit completion");
        block.block = false;
        block.release.SetValue(false);
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2Stale = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Stale.state == AutomaticState::Failed &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Stale.dirtyReasons, AutomaticReason::ExplicitReimport) &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Stale.dirtyReasons, AutomaticReason::StaleCompletion),
              "E2.6 P2 stale failure retains ExplicitReimport and StaleCompletion markers");
        Check(p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                                                                 AutomaticReason::ExplicitReimport) &&
                  drainToIdle() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Idle,
              "E2.6 P2 newer accepted change recovers stale explicit reimport while preserving current-session intent");
        Check(p2Pipeline.DisableAutomaticRecook() == PipelineResult::Success && !p2Pipeline.IsAutomaticRecookEnabled() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 policy disables after explicit activity reaches terminal Idle");

        // Disabled cache-repair bookkeeping is reconstructible durable truth. It
        // must not hitchhike into an explicit-only reimport of another asset.
        Check(cook(p2RepairOutput), "E2.6 P2 establish exact repair-candidate VADI/VDDC record");
        asset_streaming::ProjectAssetOutputDeliveryStatus p2RepairStatus;
        Check(p2Pipeline.GetOutputDeliveryStatus(p2RepairOutput, p2RepairStatus),
              "E2.6 P2 capture repair-candidate committed build key");
        const filesystem::AbsolutePath p2RepairArtifact = PersistentArtifactPath(p2DerivedRoot, p2RepairStatus.committedBuildFingerprint);
        asset_streaming::ProjectAssetRestoreReport p2RepairReport;
        Check(files.DeleteFile(p2RepairArtifact) && p2Pipeline.Reconcile(&p2RepairReport) == PipelineResult::Success &&
                  p2RepairReport.repairRequiredRecords >= 1u && !p2Pipeline.IsAutomaticRecookEnabled() &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled &&
                  asset_streaming::HasAutomaticRecookDirtyReason(p2Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                                                                 AutomaticReason::CacheRepairRequired),
              "E2.6 P2 disabled reconcile retains dormant cache-repair truth");
        const u32 p2BeforeNoHitchhikeCompileCalls = block.compileCalls.GetValue();
        Check(p2Pipeline.RequestAssetReimport(p2MainId) == PipelineResult::Success &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingRepairOutputs == 0u &&
                  p2Pipeline.GetAutomaticRecookStatus().pendingReimportOutputs == 2u &&
                  !asset_streaming::HasAutomaticRecookDirtyReason(p2Pipeline.GetAutomaticRecookStatus().dirtyReasons,
                                                                  AutomaticReason::CacheRepairRequired) &&
                  drainToDisabled() &&
                  block.compileCalls.GetValue() == p2BeforeNoHitchhikeCompileCalls && !files.FileExist(p2RepairArtifact) &&
                  p2Pipeline.GetAutomaticRecookStatus().state == AutomaticState::Disabled,
              "E2.6 P2 explicit-only reimport does not hitchhike dormant disabled CacheRepair work");

        // Unsupported and conflicting current assets are added only after the
        // policy-enabled proofs so they cannot poison startup-recovery coverage.
        assets::AssetId p2UnsupportedId;
        assets::AssetId p2ConflictId;
        Check(assets::GenerateAssetId(p2UnsupportedId) && assets::GenerateAssetId(p2ConflictId),
              "E2.6 P2 generate rejected-reimport identities");
        assets::AssetMetadata p2UnsupportedMetadata = Metadata(p2UnsupportedId);
        p2UnsupportedMetadata.importerVersion = TestImporterVersion + 17u;
        const filesystem::AbsolutePath p2UnsupportedPath = p2AssetsRoot.AddFilePath("unsupported.tasset");
        const filesystem::AbsolutePath p2ConflictA = p2AssetsRoot.AddFilePath("conflict_a.tasset");
        const filesystem::AbsolutePath p2ConflictB = p2AssetsRoot.AddFilePath("conflict_b.tasset");
        Check(WriteTextAsset(files, p2UnsupportedPath, containers::StringView("unsupported\n"), p2UnsupportedMetadata) &&
                  WriteTextAsset(files, p2ConflictA, containers::StringView("conflict-a\n"), Metadata(p2ConflictId)) &&
                  WriteTextAsset(files, p2ConflictB, containers::StringView("conflict-b\n"), Metadata(p2ConflictId)) &&
                  p2Pipeline.Rescan(p2RootSpan) == PipelineResult::Success,
              "E2.6 P2 accept unsupported and conflicting current-source evidence");
        const asset_streaming::ProjectAssetAutomaticRecookStatus p2BeforeRejectedReimports = p2Pipeline.GetAutomaticRecookStatus();
        Check(p2Pipeline.RequestAssetReimport(p2UnsupportedId) == PipelineResult::BuildRequestFailed &&
                  sameAutomaticStatus(p2Pipeline.GetAutomaticRecookStatus(), p2BeforeRejectedReimports),
              "E2.6 P2 unsupported asset reimport fails without coordinator mutation");
        Check(p2Pipeline.RequestAssetReimport(p2ConflictId) == PipelineResult::BuildRequestFailed &&
                  sameAutomaticStatus(p2Pipeline.GetAutomaticRecookStatus(), p2BeforeRejectedReimports),
              "E2.6 P2 conflicting asset reimport fails without coordinator mutation");

        Check(p2Pipeline.Shutdown() == PipelineResult::Success,
              "E2.6 P2 public-pipeline fixture shuts down cleanly");
        block.block = false;
        block.fail.SetValue(false);
        block.dependencyEnabled.SetValue(false);
        block.dependencyOwnerSource = {};
        block.dependencyOutput = {};
        block.contentFromSource = false;
        Check(DeleteDirectoryTree(files, p2Root), "E2.6 P2 public-pipeline fixture cleanup succeeds");
    }

    // ---------------------------------------------------------------------
    // Asset Browser P0.1: production-backed external source import/adoption.
    // This isolated pipeline keeps the older TASSET fixtures unchanged while
    // proving the new managed-destination + owned-byte-snapshot operation.
    // ---------------------------------------------------------------------
    {
        using PipelineResult = asset_streaming::ProjectAssetPipelineResult;
        const filesystem::AbsolutePath p0Root = root.AddDirPath("P0ExternalImport");
        const filesystem::AbsolutePath p0AssetsRoot = p0Root.AddDirPath("Assets");
        const filesystem::AbsolutePath p0ReadOnlyRoot = p0Root.AddDirPath("ReadOnlyAssets");
        const filesystem::AbsolutePath p0DerivedRoot = p0Root.AddDirPath("Derived");
        const filesystem::AbsolutePath p0IndexRoot = p0Root.AddDirPath("Index");
        const filesystem::AbsolutePath p0LooseRoot = p0Root.AddDirPath("Loose");
        const filesystem::AbsolutePath p0JournalPath = p0Root.AddFilePath("operations.vtxn");
        Check(files.CreatePath(p0Root) && files.CreatePath(p0AssetsRoot) && files.CreatePath(p0ReadOnlyRoot) &&
                  files.CreatePath(p0DerivedRoot) && files.CreatePath(p0IndexRoot) && files.CreatePath(p0LooseRoot),
              "P0.1 external-import fixture directories are created");

        assets::AssetId p0BaselineId;
        Check(assets::GenerateAssetId(p0BaselineId), "P0.1 baseline identity generation");
        const filesystem::AbsolutePath p0BaselinePath = p0AssetsRoot.AddFilePath("history_baseline.tasset");
        containers::DynamicArray<u8> p0BaselineBytes(memory::pools::Assets::GetInstance());
        p0BaselineBytes.Resize(30u * 1024u);
        if (!p0BaselineBytes.Empty())
        {
            p0BaselineBytes[0] = '#';
            for (u32 index = 1u; index < p0BaselineBytes.Size(); ++index)
                p0BaselineBytes[index] = 'B';
        }
        Check(WriteBytes(files, p0BaselinePath, p0BaselineBytes.TypedData(), p0BaselineBytes.Size()) &&
                  WriteMetadata(files, p0BaselinePath, Metadata(p0BaselineId)),
              "P0.1 writes the pressure-history baseline asset");

        containers::DynamicArray<assets::SourceRoot> p0Roots(memory::pools::Assets::GetInstance());
        p0Roots.PushBack({p0AssetsRoot, false});
        p0Roots.PushBack({p0ReadOnlyRoot, true});
        const containers::ArraySpan<const assets::SourceRoot> p0RootSpan(p0Roots.TypedData(), p0Roots.Size());
        assets::SourceDatabase p0Sources;
        Check(p0Sources.Rescan(files, p0RootSpan) == assets::SourceDatabaseResult::Success,
              "P0.1 initial source catalog rescan succeeds");

        BuildBlockState p0Block;
        p0Block.fail.SetValue(false);
        constexpr const char* P0NoAuthorCompilerName = "assetstreaming.pipeline_test.p0.noauthor";
        constexpr const char* P0AmbiguousCompilerAName = "assetstreaming.pipeline_test.p0.ambiguous_a";
        constexpr const char* P0AmbiguousCompilerBName = "assetstreaming.pipeline_test.p0.ambiguous_b";
        assets::CompilerDescriptor p0Compiler{assets::HashCompilerName(TestCompilerName), TestCompilerName, TestImporterVersion,
                                              TestSourceType, TestOutputType, &Discover, &Compile, &p0Block, &Estimate};
        assets::SourceImporterDescriptor p0Importer = assets::MakeDirectSourceImporterDescriptor(
            p0Compiler, &RecognizeSource, nullptr, &RewriteSourceReferences, &assets::RelocateSourceUnchanged,
            &CreateDefaultImportDescription, &ValidateDefaultImportSettings);
        p0Importer.scanSourceReferences = &ScanSourceReferences;
        assets::SourceImporterDescriptor p0NoAuthorImporter{
            assets::HashCompilerName(P0NoAuthorCompilerName), P0NoAuthorCompilerName, TestImporterVersion,
            TestSourceType, TestOutputType, &RecognizeNoAuthorSource, &CreateCallbackBuildRequest,
            nullptr, nullptr, nullptr, &assets::RelocateSourceUnchanged, &p0Block, nullptr, nullptr};
        assets::SourceImporterDescriptor p0AmbiguousImporterA{
            assets::HashCompilerName(P0AmbiguousCompilerAName), P0AmbiguousCompilerAName, TestImporterVersion,
            TestSourceType, TestOutputType, &RecognizeAmbiguousSource, &CreateCallbackBuildRequest,
            nullptr, nullptr, nullptr, nullptr, &p0Block, nullptr, nullptr};
        assets::SourceImporterDescriptor p0AmbiguousImporterB{
            assets::HashCompilerName(P0AmbiguousCompilerBName), P0AmbiguousCompilerBName, TestImporterVersion,
            TestSourceType, TestOutputType, &RecognizeAmbiguousSource, &CreateCallbackBuildRequest,
            nullptr, nullptr, nullptr, nullptr, &p0Block, nullptr, nullptr};

        containers::DynamicArray<assets::CompilerDescriptor> p0Compilers(memory::pools::Assets::GetInstance());
        p0Compilers.PushBack(p0Compiler);
        containers::DynamicArray<assets::SourceImporterDescriptor> p0Importers(memory::pools::Assets::GetInstance());
        p0Importers.PushBack(p0Importer);
        p0Importers.PushBack(p0NoAuthorImporter);
        p0Importers.PushBack(p0AmbiguousImporterA);
        p0Importers.PushBack(p0AmbiguousImporterB);
        const containers::ArraySpan<const assets::CompilerDescriptor> p0CompilerSpan(p0Compilers.TypedData(), p0Compilers.Size());
        const containers::ArraySpan<const assets::SourceImporterDescriptor> p0ImporterSpan(p0Importers.TypedData(), p0Importers.Size());

        resources::ResourceRegistry p0Registry;
        resources::ResourcePipeline p0ResourcePipeline;
        streaming::ResourceStreamer p0Streamer;
        Check(p0Registry.Initialize() && p0ResourcePipeline.Initialize(p0Registry) && p0Streamer.Initialize(p0ResourcePipeline),
              "P0.1 isolated resource owners initialize");

        asset_streaming::ProjectAssetPipelineConfig p0Config;
        p0Config.build.artifactCacheRoot = p0DerivedRoot;
        p0Config.build.dependencyIndexRoot = p0IndexRoot;
        p0Config.build.target = assets::TargetPlatform::WindowsD3D12;
        constexpr char p0SettingsIdentity[] = "assetStreamingTests.P0ExternalImport";
        p0Config.build.settingsFingerprint = crypto::Sha256(p0SettingsIdentity, sizeof(p0SettingsIdentity) - 1u);
        p0Config.publication.artifactCacheRoot = p0DerivedRoot;
        p0Config.publication.looseRoot = p0LooseRoot;
        p0Config.publication.target = assets::TargetPlatform::WindowsD3D12;
        p0Config.operationJournalPath = p0JournalPath;
        p0Config.maximumOperationEntries = 64u;
        p0Config.maximumOperationFileBytes = MaximumOperationFileBytes;
        p0Config.maximumOperationStagedBytes = MaximumOperationStagedBytes;
        p0Config.maximumOperationHistoryRecords = 8u;
        p0Config.maximumOperationHistoryBytes = MaximumOperationHistoryBytes;

        asset_streaming::ProjectAssetPipeline p0Pipeline;
        asset_streaming::ProjectAssetPipelineFailure p0InitFailure;
        Check(p0Pipeline.Initialize(files, p0Sources, p0Streamer, p0CompilerSpan, p0ImporterSpan, p0Config, nullptr, &p0InitFailure) ==
                  PipelineResult::Success,
              "P0.1 external-import pipeline initializes");

        constexpr u8 p0SmallSource[] = {'#', 'P', '0', '-', 'i', 'm', 'p', 'o', 'r', 't', '\n'};
        const containers::ArraySpan<const u8> p0SmallBytes(p0SmallSource, sizeof(p0SmallSource));
        auto importFailure = [&](const filesystem::AbsolutePath& destination,
                                 const assets::AssetOperationResult expectedOperation,
                                 const char* const message) noexcept {
            assets::AssetId imported;
            asset_streaming::ProjectAssetPipelineFailure failure;
            const asset_streaming::ProjectAssetExternalImportRequest request{destination, p0SmallBytes};
            const PipelineResult result = p0Pipeline.ImportExternalSource(request, imported, nullptr, &failure);
            Check(result == PipelineResult::OperationFailed && failure.operation.result == expectedOperation && !imported.IsValid() &&
                      !files.FileExist(destination) && !files.FileExist(MetadataPath(destination)),
                  message);
        };

        importFailure(p0AssetsRoot.AddFilePath("unsupported.unknown"), assets::AssetOperationResult::UnsupportedSource,
                      "P0.1 unsupported external input is rejected without project files");
        importFailure(p0AssetsRoot.AddFilePath("authoring.noauthor"), assets::AssetOperationResult::ImportDescriptionUnavailable,
                      "P0.1 recognized importer without authoring hooks is rejected without project files");
        importFailure(p0AssetsRoot.AddFilePath("ambiguous.ambiguous"), assets::AssetOperationResult::AmbiguousSource,
                      "P0.1 ambiguous importer recognition is rejected without project files");
        importFailure(p0ReadOnlyRoot.AddFilePath("readonly.tasset"), assets::AssetOperationResult::ReadOnlySource,
                      "P0.1 read-only destination is rejected without project files");
        importFailure(p0Root.AddFilePath("outside.tasset"), assets::AssetOperationResult::ReadOnlySource,
                      "P0.1 destination outside managed source roots is rejected without project files");

        const filesystem::AbsolutePath p0SourceConflict = p0AssetsRoot.AddFilePath("source_conflict.tasset");
        Check(WriteBytes(files, p0SourceConflict, p0SmallSource, sizeof(p0SmallSource)),
              "P0.1 source-conflict fixture is created");
        {
            assets::AssetId imported;
            asset_streaming::ProjectAssetPipelineFailure failure;
            const asset_streaming::ProjectAssetExternalImportRequest request{p0SourceConflict, p0SmallBytes};
            Check(p0Pipeline.ImportExternalSource(request, imported, nullptr, &failure) == PipelineResult::OperationFailed &&
                      failure.operation.result == assets::AssetOperationResult::TargetConflict && files.FileExist(p0SourceConflict) &&
                      !files.FileExist(MetadataPath(p0SourceConflict)),
                  "P0.1 existing source collision is explicit and never overwritten");
        }
        Check(files.DeleteFile(p0SourceConflict), "P0.1 source-conflict fixture cleanup");

        const filesystem::AbsolutePath p0SidecarConflict = p0AssetsRoot.AddFilePath("sidecar_conflict.tasset");
        constexpr u8 p0ConflictSidecar[] = {'x'};
        Check(WriteBytes(files, MetadataPath(p0SidecarConflict), p0ConflictSidecar, sizeof(p0ConflictSidecar)),
              "P0.1 sidecar-conflict fixture is created");
        {
            assets::AssetId imported;
            asset_streaming::ProjectAssetPipelineFailure failure;
            const asset_streaming::ProjectAssetExternalImportRequest request{p0SidecarConflict, p0SmallBytes};
            Check(p0Pipeline.ImportExternalSource(request, imported, nullptr, &failure) == PipelineResult::OperationFailed &&
                      failure.operation.result == assets::AssetOperationResult::TargetConflict && !files.FileExist(p0SidecarConflict) &&
                      files.FileExist(MetadataPath(p0SidecarConflict)),
                  "P0.1 existing VMETA collision is explicit and never creates the source");
        }
        Check(files.DeleteFile(MetadataPath(p0SidecarConflict)), "P0.1 sidecar-conflict fixture cleanup");

        const filesystem::AbsolutePath p0ImportedPath = p0AssetsRoot.AddFilePath("external_success.tasset");
        assets::AssetId p0ImportedId;
        asset_streaming::ProjectAssetPipelineFailure p0ImportFailure;
        const asset_streaming::ProjectAssetExternalImportRequest p0ImportRequest{p0ImportedPath, p0SmallBytes};
        Check(p0Pipeline.ImportExternalSource(p0ImportRequest, p0ImportedId, nullptr, &p0ImportFailure) == PipelineResult::Success,
              "P0.1 external source atomically imports with exact VMETA sizing below the 1 MiB metadata ceiling");
        containers::DynamicArray<u8> p0ImportedSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> p0ImportedSidecar(memory::pools::Assets::GetInstance());
        assets::AssetMetadata p0ImportedMetadata;
        Check(p0ImportedId.IsValid() && ReadBytes(files, p0ImportedPath, p0ImportedSource) &&
                  ReadBytes(files, MetadataPath(p0ImportedPath), p0ImportedSidecar) &&
                  p0ImportedSource.Size() == sizeof(p0SmallSource) &&
                  std::memcmp(p0ImportedSource.TypedData(), p0SmallSource, sizeof(p0SmallSource)) == 0 &&
                  assets::ParseMetadata({reinterpret_cast<const char*>(p0ImportedSidecar.TypedData()), p0ImportedSidecar.Size()},
                                        p0ImportedMetadata) == assets::MetadataResult::Success &&
                  p0ImportedMetadata.id == p0ImportedId && p0ImportedMetadata.importer == p0Importer.id &&
                  p0ImportedMetadata.importerVersion == p0Importer.version && p0ImportedMetadata.settingsVersion == 1u &&
                  p0ImportedMetadata.settings.Size() == 1u && p0ImportedMetadata.settings[0] == 7u &&
                  p0ImportedMetadata.outputs.Size() == 1u && p0ImportedMetadata.outputs[0].type == TestOutputType,
              "P0.1 durable source bytes and canonical VMETA carry the generated identity/importer/settings/output");
        const assets::SourceRecord* const p0ImportedRecord = p0Sources.LookupAsset(p0ImportedId);
        Check(p0ImportedRecord != nullptr && p0ImportedRecord->path == p0ImportedPath && p0ImportedRecord->metadata.id == p0ImportedId,
              "P0.1 normal rescan reconciles the imported asset into SourceDatabase");

        // Replay-budget pressure: an older ~30 KiB move plus a ~60 KiB external
        // import fit inside the 100 KiB aggregate history budget. After one
        // Undo/Redo cycle, a second ~30 KiB move must evict the oldest move if
        // the external record preserved its ~60 KiB future replay reservation.
        p0Pipeline.ClearAssetOperationHistory();
        const filesystem::AbsolutePath p0BaselineMoved1 = p0AssetsRoot.AddFilePath("history_baseline_moved_1.tasset");
        const filesystem::AbsolutePath p0BaselineMoved2 = p0AssetsRoot.AddFilePath("history_baseline_moved_2.tasset");
        Check(p0Pipeline.MoveAsset(p0BaselineId, p0BaselineMoved1) == PipelineResult::Success,
              "P0.1 pressure history seeds an older ~30 KiB inverse");
        containers::DynamicArray<u8> p0LargeExternal(memory::pools::Assets::GetInstance());
        p0LargeExternal.Resize(LargeContentSize);
        if (!p0LargeExternal.Empty())
        {
            p0LargeExternal[0] = '#';
            for (u32 index = 1u; index < p0LargeExternal.Size(); ++index)
                p0LargeExternal[index] = 'I';
        }
        const filesystem::AbsolutePath p0HistoryImportPath = p0AssetsRoot.AddFilePath("history_external.tasset");
        const asset_streaming::ProjectAssetExternalImportRequest p0HistoryImportRequest{
            p0HistoryImportPath, {p0LargeExternal.TypedData(), p0LargeExternal.Size()}};
        assets::AssetId p0HistoryImportId;
        Check(p0Pipeline.ImportExternalSource(p0HistoryImportRequest, p0HistoryImportId) == PipelineResult::Success &&
                  p0Pipeline.GetUndoAssetOperationCount() == 2u,
              "P0.1 exact source+VMETA replay reservation admits the large import while retaining fitting older history");
        containers::DynamicArray<u8> p0HistoryCommittedSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> p0HistoryCommittedSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, p0HistoryImportPath, p0HistoryCommittedSource) &&
                  ReadBytes(files, MetadataPath(p0HistoryImportPath), p0HistoryCommittedSidecar),
              "P0.1 captures committed import bytes for replay comparison");
        Check(p0Pipeline.UndoAssetOperation() == PipelineResult::Success && !files.FileExist(p0HistoryImportPath) &&
                  !files.FileExist(MetadataPath(p0HistoryImportPath)),
              "P0.1 first Undo removes both files of the external create/adopt");
        Check(p0Pipeline.RedoAssetOperation() == PipelineResult::Success && files.FileExist(p0HistoryImportPath) &&
                  files.FileExist(MetadataPath(p0HistoryImportPath)),
              "P0.1 first Redo restores both files of the external create/adopt");
        Check(p0Pipeline.MoveAsset(p0BaselineId, p0BaselineMoved2) == PipelineResult::Success &&
                  p0Pipeline.GetUndoAssetOperationCount() == 2u,
              "P0.1 replay-stable external reservation survives Redo and evicts the oldest record under later history pressure");
        Check(p0Pipeline.UndoAssetOperation() == PipelineResult::Success &&
                  p0Sources.LookupAsset(p0BaselineId) != nullptr && p0Sources.LookupAsset(p0BaselineId)->path == p0BaselineMoved1,
              "P0.1 pressure move Undo exposes the external import record again");
        Check(p0Pipeline.UndoAssetOperation() == PipelineResult::Success && !files.FileExist(p0HistoryImportPath) &&
                  !files.FileExist(MetadataPath(p0HistoryImportPath)),
              "P0.1 second Undo after Redo remains affordable under history pressure");
        Check(p0Pipeline.RedoAssetOperation() == PipelineResult::Success,
              "P0.1 second Redo restores the external import after the pressured second Undo");
        containers::DynamicArray<u8> p0HistoryRedoneSource(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> p0HistoryRedoneSidecar(memory::pools::Assets::GetInstance());
        Check(ReadBytes(files, p0HistoryImportPath, p0HistoryRedoneSource) &&
                  ReadBytes(files, MetadataPath(p0HistoryImportPath), p0HistoryRedoneSidecar) &&
                  SameBytes(p0HistoryRedoneSource, p0HistoryCommittedSource) && SameBytes(p0HistoryRedoneSidecar, p0HistoryCommittedSidecar),
              "P0.1 repeated Redo restores exact source and VMETA bytes");
        Check(p0Pipeline.RedoAssetOperation() == PipelineResult::Success &&
                  p0Sources.LookupAsset(p0BaselineId) != nullptr && p0Sources.LookupAsset(p0BaselineId)->path == p0BaselineMoved2,
              "P0.1 redo stack restores the later pressure move after the external import");

        Check(p0Pipeline.EnableAutomaticRecook() == PipelineResult::Success &&
                  p0Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle,
              "P0.1 existing automatic-recook owner enables without import-specific machinery");
        const u32 p0CompileCallsBeforeAutomaticImport = p0Block.compileCalls.GetValue();
        const filesystem::AbsolutePath p0AutomaticImportPath = p0AssetsRoot.AddFilePath("automatic_external.tasset");
        assets::AssetId p0AutomaticImportId;
        const asset_streaming::ProjectAssetExternalImportRequest p0AutomaticImportRequest{p0AutomaticImportPath, p0SmallBytes};
        Check(p0Pipeline.ImportExternalSource(p0AutomaticImportRequest, p0AutomaticImportId) == PipelineResult::Success &&
                  p0Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Pending &&
                  p0Pipeline.GetAutomaticRecookStatus().pendingAssets >= 1u &&
                  p0Pipeline.GetAutomaticRecookStatus().pendingOutputs >= 1u,
              "P0.1 external import seeds the existing automatic-recook coordinator through normal Rescan");
        Check(DrainAutomaticRecook(p0Pipeline) == PipelineResult::Success &&
                  p0Pipeline.GetAutomaticRecookStatus().state == asset_streaming::ProjectAssetAutomaticRecookState::Idle &&
                  p0Block.compileCalls.GetValue() > p0CompileCallsBeforeAutomaticImport,
              "P0.1 existing automatic-recook owner builds the imported asset to completion");
        asset_streaming::ProjectAssetOutputDeliveryStatus p0AutomaticDelivery;
        Check(p0Pipeline.GetOutputDeliveryStatus(OutputReference(p0AutomaticImportId), p0AutomaticDelivery) &&
                  p0AutomaticDelivery.buildState == asset_streaming::ProjectAssetDeliveryStageState::Committed &&
                  p0AutomaticDelivery.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Pending &&
                  p0AutomaticDelivery.publicationReason == asset_streaming::ProjectAssetDeliveryReason::AwaitingPublication &&
                  p0AutomaticDelivery.publishedBuildFingerprint.IsEmpty() &&
                  p0Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs >= 1u,
              "P0.1 automatic recook hands the imported committed build to the existing publication queue");
        Check(DrainAutomaticRecook(p0Pipeline) == PipelineResult::Success,
              "P0.1 existing automatic publication owner drains the imported delivery token");
        Check(p0Pipeline.GetOutputDeliveryStatus(OutputReference(p0AutomaticImportId), p0AutomaticDelivery) &&
                  p0AutomaticDelivery.publicationState == asset_streaming::ProjectAssetDeliveryStageState::Installed &&
                  p0AutomaticDelivery.publishedBuildFingerprint == p0AutomaticDelivery.committedBuildFingerprint &&
                  p0AutomaticDelivery.publishedContentFingerprint == p0AutomaticDelivery.committedContentFingerprint &&
                  !p0AutomaticDelivery.publishedPhysicalPath.Empty() && files.FileExist(p0AutomaticDelivery.publishedPhysicalPath) &&
                  p0Pipeline.GetAutomaticRecookStatus().pendingDeliveryOutputs == 0u,
              "P0.1 imported asset reaches the existing loose publication owner");
        Check(p0Pipeline.DisableAutomaticRecook() == PipelineResult::Success,
              "P0.1 automatic-recook proof leaves the isolated coordinator disabled");

        p0Pipeline.ClearAssetOperationHistory();
        Check(p0Pipeline.Shutdown() == PipelineResult::Success, "P0.1 pipeline shuts down before restart proof");
        Check(p0Sources.Rescan(files, p0RootSpan) == assets::SourceDatabaseResult::Success,
              "P0.1 restart proof rebuilds source truth from durable project files");
        Check(p0Pipeline.Initialize(files, p0Sources, p0Streamer, p0CompilerSpan, p0ImporterSpan, p0Config) == PipelineResult::Success,
              "P0.1 pipeline reinitializes from durable imported state");
        Check(p0Pipeline.GetUndoAssetOperationCount() == 0u && p0Pipeline.GetRedoAssetOperationCount() == 0u &&
                  p0Sources.LookupAsset(p0ImportedId) != nullptr && p0Sources.LookupAsset(p0ImportedId)->path == p0ImportedPath &&
                  p0Sources.LookupAsset(p0HistoryImportId) != nullptr && p0Sources.LookupAsset(p0HistoryImportId)->path == p0HistoryImportPath,
              "P0.1 restart preserves durable imported AssetIds while session history stays session-local");

        Check(p0Pipeline.Shutdown() == PipelineResult::Success, "P0.1 external-import pipeline final shutdown");
        Check(p0Streamer.Shutdown(), "P0.1 isolated resource streamer shutdown");
        Check(p0ResourcePipeline.Shutdown(), "P0.1 isolated resource pipeline shutdown");
        Check(p0Registry.Shutdown(), "P0.1 isolated resource registry shutdown");
        Check(DeleteDirectoryTree(files, p0Root), "P0.1 external-import fixture cleanup succeeds");
    }

    Check(pipeline.Shutdown() == Success, "final pipeline shutdown");
    Check(DeleteDirectoryTree(files, root), "test root cleanup succeeds");
    Check(!files.FileExist(root), "test root is absent after cleanup");

    if (g_failures == 0)
    {
        VG_LOG_INFO(diagnostics::Category::FunctionalTests, "[assetStreamingTests] E1F operation and undo/redo checks passed");
    }

    // Explicit reverse-dependency shutdown so a leaked owner is reported instead
    // of being hidden by a silent destructor. ResourceStreamer requires the
    // filesystem and I/O owners to outlive it; the resource pipeline requires the
    // registry and jobs to outlive it.
    Check(streamer.Shutdown(), "resource streamer shutdown");
    Check(resourcePipeline.Shutdown(), "resource pipeline shutdown");
    Check(registry.Shutdown(), "resource registry shutdown");
    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    std::fprintf(stderr, "[assetStreamingTests] finished with %u failed checks\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
