#pragma once

#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/assets/asset_index.hpp>
#include <vanguard/assets/project_source_data.hpp>

namespace vanguard::assets
{
    enum class ProjectAssetBuildResult : u8
    {
        Success,
        InvalidArgument,
        BuildSystemInitializationFailed,
        CompilerRegistrationFailed,
        DependencyIndexInitializationFailed,
        BuildGraphInitializationFailed,
        SourceNotFound,
        ImporterUnavailable,
        SourceDataUnavailable,
        BuildRequestCreationFailed,
        BuildRequestRejected,
        Busy,
        IndexSaveFailed
    };

    [[nodiscard]] const char* ToString(ProjectAssetBuildResult result) noexcept;

    struct ProjectAssetBuildConfig
    {
        filesystem::AbsolutePath artifactCacheRoot;
        filesystem::AbsolutePath dependencyIndexRoot;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        BuildFingerprint settingsFingerprint;
        Config buildSystem;
        BuildGraphConfig buildGraph;
        DependencyIndexConfig dependencyIndex;
        ProjectSourceDataLimits sourceData;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return artifactCacheRoot.IsDirectoryPath() && dependencyIndexRoot.IsDirectoryPath() && target < TargetPlatform::Count &&
                   !settingsFingerprint.IsEmpty() && sourceData.IsValid();
        }
    };

    // Shared project cooker ownership. The editor and headless tools are
    // consumers; neither owns a separate compiler registry, graph, or DDC.
    // SourceDatabase mutations and shutdown require all returned requests to
    // be released. Compiler/importer callback state must outlive this object.
    class ProjectAssetBuildSystem final
    {
    public:
        struct Impl;

        ProjectAssetBuildSystem() noexcept = default;
        ~ProjectAssetBuildSystem();

        ProjectAssetBuildSystem(const ProjectAssetBuildSystem&) = delete;
        ProjectAssetBuildSystem& operator=(const ProjectAssetBuildSystem&) = delete;

        [[nodiscard]] ProjectAssetBuildResult Initialize(filesystem::Manager& files, const SourceDatabase& sources,
                                                         containers::ArraySpan<const CompilerDescriptor> compilers,
                                                         containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                         const ProjectAssetBuildConfig& config) noexcept;
        [[nodiscard]] ProjectAssetBuildResult Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Canonical ownership-safe request resolver shared by manual requests,
        // generated dependencies and E2.3 coordination.
        [[nodiscard]] ProjectAssetBuildResult Resolve(resources::ResourceReference output, OwnedBuildRequest& request) const noexcept;
        // Same resolver against a caller-owned immutable catalog candidate. This
        // keeps off-thread project reconciliation on the canonical importer path
        // without swapping that candidate into the live SourceDatabase first.
        [[nodiscard]] ProjectAssetBuildResult Resolve(const SourceDatabase& sources, resources::ResourceReference output,
                                                      OwnedBuildRequest& request) const noexcept;

        [[nodiscard]] GraphRequest Request(resources::ResourceReference output, BuildPriority priority = BuildPriority::Normal,
                                           ProjectAssetBuildResult* result = nullptr) noexcept;

        [[nodiscard]] BuildSystem* GetBuildSystem() noexcept;
        [[nodiscard]] BuildGraph* GetBuildGraph() noexcept;
        [[nodiscard]] DependencyIndex* GetDependencyIndex() noexcept;
        // One epoch-bracketed immutable protection snapshot for the durable
        // project owners in assets. Keys are deduped by VDDC build identity.
        [[nodiscard]] PersistentCacheMaintenanceResult CapturePersistentProtectionSnapshot(
            containers::DynamicArray<ArtifactSetKey>& keys, u32 maximumRecords, u64 maximumBytes,
            u64& protectionEpoch) const noexcept;
        // Destructive maintenance remains inside the BuildSystem/root authority.
        // The caller supplies only the immutable project protection snapshot and
        // the epoch captured with it; every actual delete is re-admitted by the
        // root authority against current leases and that same epoch.
        [[nodiscard]] PersistentCacheMaintenanceResult MaintainPersistentCache(
            containers::ArraySpan<const ArtifactSetKey> protectedKeys, u64 protectionEpoch,
            const PersistentCacheMaintenanceLimits& limits, PersistentCacheMaintenanceReport& report,
            containers::DynamicArray<PersistentCacheMaintenanceDiagnostic>& diagnostics,
            IsCancellationRequestedFunction cancellation = nullptr, void* cancellationUserData = nullptr) noexcept;
        // Monotonic epoch for the root protection domain. A maintenance owner
        // brackets bounded owner snapshots with this value; a changed epoch
        // invalidates the captured deletion candidate set.
        [[nodiscard]] u64 GetPersistentProtectionEpoch() const noexcept;
        [[nodiscard]] containers::ArraySpan<const SourceImporterDescriptor> GetImporters() const noexcept;
        [[nodiscard]] IndexResult GetIndexInitializationResult() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
