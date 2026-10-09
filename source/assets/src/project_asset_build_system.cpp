#include <vanguard/assets/project_asset_build_system.hpp>

#include <vanguard/memory/memory.hpp>

#include <algorithm>

namespace vanguard::assets
{
    namespace
    {
        [[nodiscard]] bool BuildLess(const ArtifactSetKey& left, const ArtifactSetKey& right) noexcept
        {
            for (u32 index = 0; index < BuildFingerprint::ByteCount; ++index)
            {
                if (left.build.bytes[index] != right.build.bytes[index])
                    return left.build.bytes[index] < right.build.bytes[index];
            }
            return false;
        }

        void SortAndDedupeByBuild(containers::DynamicArray<ArtifactSetKey>& keys) noexcept
        {
            std::sort(keys.Begin(), keys.End(), BuildLess);
            u32 write = 0u;
            for (u32 index = 0; index < keys.Size(); ++index)
            {
                if (write != 0u && keys[write - 1u].build == keys[index].build)
                    continue;
                if (write != index)
                    keys[write] = keys[index];
                ++write;
            }
            keys.Resize(write);
        }

        [[nodiscard]] PersistentCacheMaintenanceResult SnapshotFailure(const IndexResult result) noexcept
        {
            if (result == IndexResult::LimitExceeded)
                return PersistentCacheMaintenanceResult::LimitExceeded;
            if (result == IndexResult::OutOfMemory)
                return PersistentCacheMaintenanceResult::OutOfMemory;
            return PersistentCacheMaintenanceResult::InvalidState;
        }

        [[nodiscard]] PersistentCacheMaintenanceResult SnapshotFailure(const Result result) noexcept
        {
            if (result == Result::LimitExceeded)
                return PersistentCacheMaintenanceResult::LimitExceeded;
            if (result == Result::OutOfMemory)
                return PersistentCacheMaintenanceResult::OutOfMemory;
            return PersistentCacheMaintenanceResult::InvalidState;
        }
    } // namespace

    struct ProjectAssetBuildSystem::Impl final
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        Impl(filesystem::Manager& sourceFiles, const SourceDatabase& sourceDatabase, const TargetPlatform buildTarget,
             const ProjectSourceDataLimits& dataLimits) noexcept
            : files(&sourceFiles), sources(&sourceDatabase), target(buildTarget), sourceDataLimits(dataLimits),
              importers(memory::pools::Assets::GetInstance()),
              importerIndices(memory::pools::Assets::GetInstance())
        {
        }

        filesystem::Manager* files = nullptr;
        const SourceDatabase* sources = nullptr;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        ProjectSourceDataLimits sourceDataLimits;
        BuildSystem buildSystem;
        DependencyIndex dependencyIndex;
        BuildGraph buildGraph;
        containers::DynamicArray<SourceImporterDescriptor> importers;
        containers::HashMap<SourceImporterId, u32> importerIndices;
        IndexResult indexInitializationResult = IndexResult::InvalidState;

        [[nodiscard]] const SourceImporterDescriptor* FindImporter(const SourceImporterId id) const noexcept
        {
            u32 index = 0;
            return importerIndices.Find(id, index) ? &importers[index] : nullptr;
        }

        [[nodiscard]] ProjectAssetBuildResult ResolveRequest(const SourceDatabase& sourceDatabase,
                                                             const resources::ResourceReference output,
                                                             OwnedBuildRequest& request) const noexcept
        {
            const SourceRecord* const source = sourceDatabase.LookupOutput(output);
            if (source == nullptr)
                return ProjectAssetBuildResult::SourceNotFound;
            const SourceImporterDescriptor* const importer = FindImporter(source->metadata.importer);
            if (importer == nullptr || importer->version != source->metadata.importerVersion)
                return ProjectAssetBuildResult::ImporterUnavailable;
            ProjectSourceData data;
            if (LoadProjectSourceData(*files, *source, data, sourceDataLimits) != ProjectSourceDataResult::Success)
                return ProjectAssetBuildResult::SourceDataUnavailable;
            BuildRequest borrowed;
            if (!CreateSourceBuildRequest(*importer, *source, data.View(target), output, borrowed))
                return ProjectAssetBuildResult::BuildRequestCreationFailed;
            if (!request.CopyFrom(borrowed))
                return ProjectAssetBuildResult::BuildRequestCreationFailed;
            return ProjectAssetBuildResult::Success;
        }

        [[nodiscard]] static bool ResolveGenerated(const BuildDependency& dependency, OwnedBuildRequest& request, void* const userData) noexcept
        {
            if (userData == nullptr)
                return false;
            const Impl& implementation = *static_cast<const Impl*>(userData);
            return implementation.sources != nullptr &&
                   implementation.ResolveRequest(*implementation.sources, dependency.identity, request) == ProjectAssetBuildResult::Success;
        }
    };

    const char* ToString(const ProjectAssetBuildResult result) noexcept
    {
        switch (result)
        {
        case ProjectAssetBuildResult::Success: return "Success";
        case ProjectAssetBuildResult::InvalidArgument: return "InvalidArgument";
        case ProjectAssetBuildResult::BuildSystemInitializationFailed: return "BuildSystemInitializationFailed";
        case ProjectAssetBuildResult::CompilerRegistrationFailed: return "CompilerRegistrationFailed";
        case ProjectAssetBuildResult::DependencyIndexInitializationFailed: return "DependencyIndexInitializationFailed";
        case ProjectAssetBuildResult::BuildGraphInitializationFailed: return "BuildGraphInitializationFailed";
        case ProjectAssetBuildResult::SourceNotFound: return "SourceNotFound";
        case ProjectAssetBuildResult::ImporterUnavailable: return "ImporterUnavailable";
        case ProjectAssetBuildResult::SourceDataUnavailable: return "SourceDataUnavailable";
        case ProjectAssetBuildResult::BuildRequestCreationFailed: return "BuildRequestCreationFailed";
        case ProjectAssetBuildResult::BuildRequestRejected: return "BuildRequestRejected";
        case ProjectAssetBuildResult::Busy: return "Busy";
        case ProjectAssetBuildResult::IndexSaveFailed: return "IndexSaveFailed";
        }
        return "Unknown";
    }

    ProjectAssetBuildSystem::~ProjectAssetBuildSystem()
    {
        static_cast<void>(Shutdown());
    }

    ProjectAssetBuildResult ProjectAssetBuildSystem::Initialize(filesystem::Manager& files, const SourceDatabase& sources,
                                                                const containers::ArraySpan<const CompilerDescriptor> compilers,
                                                                const containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                                const ProjectAssetBuildConfig& config) noexcept
    {
        if (m_impl != nullptr || compilers.Empty() || importers.Empty() || !config.IsValid())
            return ProjectAssetBuildResult::InvalidArgument;
        for (u32 index = 0; index < importers.Count(); ++index)
        {
            if (!importers[index].IsValid())
                return ProjectAssetBuildResult::InvalidArgument;
            for (u32 previous = 0; previous < index; ++previous)
                if (importers[index].id == importers[previous].id)
                    return ProjectAssetBuildResult::InvalidArgument;
            if (importers[index].createBuildRequest == nullptr)
            {
                bool matchingCompiler = false;
                for (const CompilerDescriptor& compiler : compilers)
                    if (compiler.id == importers[index].id && compiler.version == importers[index].version &&
                        compiler.sourceType == importers[index].sourceType && compiler.outputType == importers[index].primaryOutputType)
                        matchingCompiler = true;
                if (!matchingCompiler)
                    return ProjectAssetBuildResult::InvalidArgument;
            }
        }

        Impl* const implementation = VANGUARD_NEW(Impl)(files, sources, config.target, config.sourceData);
        if (implementation == nullptr)
            return ProjectAssetBuildResult::BuildSystemInitializationFailed;
        for (u32 index = 0; index < importers.Count(); ++index)
        {
            implementation->importers.PushBack(importers[index]);
            if (implementation->importers.Size() != index + 1u || !implementation->importerIndices.Insert(importers[index].id, index).IsSuccessful())
            {
                VANGUARD_DELETE(implementation);
                return ProjectAssetBuildResult::BuildSystemInitializationFailed;
            }
        }

        Config buildConfig = config.buildSystem;
        buildConfig.persistentCacheRoot = config.artifactCacheRoot.AsChar();
        if (!implementation->buildSystem.Initialize(buildConfig))
        {
            VANGUARD_DELETE(implementation);
            return ProjectAssetBuildResult::BuildSystemInitializationFailed;
        }
        for (const CompilerDescriptor& compiler : compilers)
            if (implementation->buildSystem.RegisterCompiler(compiler) != Result::Success)
            {
                static_cast<void>(implementation->buildSystem.Shutdown());
                VANGUARD_DELETE(implementation);
                return ProjectAssetBuildResult::CompilerRegistrationFailed;
            }

        DependencyIndexConfig indexConfig = config.dependencyIndex;
        indexConfig.root = config.dependencyIndexRoot.AsChar();
        indexConfig.settingsFingerprint = config.settingsFingerprint;
        implementation->indexInitializationResult = implementation->dependencyIndex.Initialize(indexConfig);
        if (!IsSuccess(implementation->indexInitializationResult))
        {
            static_cast<void>(implementation->buildSystem.Shutdown());
            VANGUARD_DELETE(implementation);
            return ProjectAssetBuildResult::DependencyIndexInitializationFailed;
        }
        if (!implementation->buildGraph.Initialize(implementation->buildSystem, &Impl::ResolveGenerated, implementation, config.buildGraph,
                                                   &implementation->dependencyIndex))
        {
            static_cast<void>(implementation->dependencyIndex.Shutdown());
            static_cast<void>(implementation->buildSystem.Shutdown());
            VANGUARD_DELETE(implementation);
            return ProjectAssetBuildResult::BuildGraphInitializationFailed;
        }
        m_impl = implementation;
        return ProjectAssetBuildResult::Success;
    }

    ProjectAssetBuildResult ProjectAssetBuildSystem::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return ProjectAssetBuildResult::Success;
        if (!m_impl->buildGraph.Shutdown())
            return ProjectAssetBuildResult::Busy;
        if (m_impl->dependencyIndex.Save() != IndexResult::Success)
            return ProjectAssetBuildResult::IndexSaveFailed;
        if (!m_impl->dependencyIndex.Shutdown() || !m_impl->buildSystem.Shutdown())
            return ProjectAssetBuildResult::Busy;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return ProjectAssetBuildResult::Success;
    }

    bool ProjectAssetBuildSystem::IsInitialized() const noexcept { return m_impl != nullptr; }

    ProjectAssetBuildResult ProjectAssetBuildSystem::Resolve(const resources::ResourceReference output, OwnedBuildRequest& request) const noexcept
    {
        request.Reset();
        if (m_impl == nullptr || m_impl->sources == nullptr || !output.IsValid() || !output.IsTyped())
            return ProjectAssetBuildResult::InvalidArgument;
        return m_impl->ResolveRequest(*m_impl->sources, output, request);
    }

    ProjectAssetBuildResult ProjectAssetBuildSystem::Resolve(const SourceDatabase& sources, const resources::ResourceReference output,
                                                             OwnedBuildRequest& request) const noexcept
    {
        request.Reset();
        if (m_impl == nullptr || !output.IsValid() || !output.IsTyped())
            return ProjectAssetBuildResult::InvalidArgument;
        return m_impl->ResolveRequest(sources, output, request);
    }

    GraphRequest ProjectAssetBuildSystem::Request(const resources::ResourceReference output, const BuildPriority priority,
                                                  ProjectAssetBuildResult* const result) noexcept
    {
        if (result != nullptr)
            *result = ProjectAssetBuildResult::Success;
        OwnedBuildRequest owned;
        const ProjectAssetBuildResult resolved = Resolve(output, owned);
        if (resolved != ProjectAssetBuildResult::Success)
        {
            if (result != nullptr)
                *result = resolved;
            return {};
        }
        GraphRequest graphRequest = m_impl->buildGraph.Request(owned.View(), priority);
        if (!graphRequest && result != nullptr)
            *result = ProjectAssetBuildResult::BuildRequestRejected;
        return graphRequest;
    }

    BuildSystem* ProjectAssetBuildSystem::GetBuildSystem() noexcept { return m_impl != nullptr ? &m_impl->buildSystem : nullptr; }
    BuildGraph* ProjectAssetBuildSystem::GetBuildGraph() noexcept { return m_impl != nullptr ? &m_impl->buildGraph : nullptr; }
    DependencyIndex* ProjectAssetBuildSystem::GetDependencyIndex() noexcept { return m_impl != nullptr ? &m_impl->dependencyIndex : nullptr; }

    PersistentCacheMaintenanceResult ProjectAssetBuildSystem::CapturePersistentProtectionSnapshot(
        containers::DynamicArray<ArtifactSetKey>& keys, const u32 maximumRecords, const u64 maximumBytes,
        u64& protectionEpoch) const noexcept
    {
        keys.Clear();
        protectionEpoch = 0u;
        if (m_impl == nullptr || maximumRecords == 0u || maximumBytes == 0u)
            return PersistentCacheMaintenanceResult::InvalidArgument;

        const u64 before = m_impl->buildSystem.PersistentProtectionEpoch(&m_impl->dependencyIndex);
        if (before == 0u)
            return PersistentCacheMaintenanceResult::InvalidState;

        const IndexResult indexCapture = m_impl->dependencyIndex.CaptureProtectionKeys(keys, maximumRecords, maximumBytes);
        if (indexCapture != IndexResult::Success)
        {
            keys.Clear();
            return SnapshotFailure(indexCapture);
        }
        SortAndDedupeByBuild(keys);

        containers::DynamicArray<ArtifactSetKey> graphKeys(memory::pools::Assets::GetInstance());
        const Result graphCapture = m_impl->buildGraph.CaptureProtectionKeys(graphKeys, maximumRecords, maximumBytes);
        if (graphCapture != Result::Success)
        {
            keys.Clear();
            return SnapshotFailure(graphCapture);
        }
        SortAndDedupeByBuild(graphKeys);

        u32 left = 0u;
        u32 right = 0u;
        u32 unionCount = 0u;
        while (left < keys.Size() || right < graphKeys.Size())
        {
            if (left < keys.Size() && right < graphKeys.Size())
            {
                if (keys[left].build == graphKeys[right].build)
                {
                    ++left;
                    ++right;
                }
                else if (BuildLess(keys[left], graphKeys[right]))
                {
                    ++left;
                }
                else
                {
                    ++right;
                }
            }
            else if (left < keys.Size())
                ++left;
            else
                ++right;
            ++unionCount;
            if (unionCount > maximumRecords || static_cast<u64>(unionCount) > maximumBytes / sizeof(ArtifactSetKey))
            {
                keys.Clear();
                return PersistentCacheMaintenanceResult::LimitExceeded;
            }
        }

        const u32 leftCount = keys.Size();
        const u32 rightCount = graphKeys.Size();
        keys.Resize(unionCount);
        if (keys.Size() != unionCount)
        {
            keys.Clear();
            return PersistentCacheMaintenanceResult::OutOfMemory;
        }
        left = leftCount;
        right = rightCount;
        u32 write = unionCount;
        while (write != 0u)
        {
            if (left != 0u && right != 0u)
            {
                const ArtifactSetKey leftKey = keys[left - 1u];
                const ArtifactSetKey rightKey = graphKeys[right - 1u];
                if (leftKey.build == rightKey.build)
                {
                    keys[--write] = leftKey;
                    --left;
                    --right;
                }
                else if (BuildLess(leftKey, rightKey))
                {
                    keys[--write] = rightKey;
                    --right;
                }
                else
                {
                    keys[--write] = leftKey;
                    --left;
                }
            }
            else if (right != 0u)
                keys[--write] = graphKeys[--right];
            else
                keys[--write] = keys[--left];
        }

        const u64 after = m_impl->buildSystem.PersistentProtectionEpoch(&m_impl->dependencyIndex);
        if (after == 0u || after != before)
        {
            keys.Clear();
            return PersistentCacheMaintenanceResult::ProtectionChanged;
        }
        protectionEpoch = before;
        return PersistentCacheMaintenanceResult::Success;
    }

    PersistentCacheMaintenanceResult ProjectAssetBuildSystem::MaintainPersistentCache(
        const containers::ArraySpan<const ArtifactSetKey> protectedKeys, const u64 protectionEpoch,
        const PersistentCacheMaintenanceLimits& limits, PersistentCacheMaintenanceReport& report,
        containers::DynamicArray<PersistentCacheMaintenanceDiagnostic>& diagnostics,
        const IsCancellationRequestedFunction cancellation, void* const cancellationUserData) noexcept
    {
        if (m_impl == nullptr)
        {
            report = {};
            diagnostics.Clear();
            return PersistentCacheMaintenanceResult::InvalidState;
        }
        return m_impl->buildSystem.MaintainPersistentCache(&m_impl->dependencyIndex, protectedKeys, protectionEpoch,
                                                          limits, report, diagnostics, cancellation, cancellationUserData);
    }

    u64 ProjectAssetBuildSystem::GetPersistentProtectionEpoch() const noexcept
    {
        return m_impl != nullptr ? m_impl->buildSystem.PersistentProtectionEpoch(&m_impl->dependencyIndex) : 0u;
    }
    containers::ArraySpan<const SourceImporterDescriptor> ProjectAssetBuildSystem::GetImporters() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const SourceImporterDescriptor>(m_impl->importers.TypedData(), m_impl->importers.Size())
                                 : containers::ArraySpan<const SourceImporterDescriptor>();
    }
    IndexResult ProjectAssetBuildSystem::GetIndexInitializationResult() const noexcept
    {
        return m_impl != nullptr ? m_impl->indexInitializationResult : IndexResult::InvalidState;
    }
} // namespace vanguard::assets
