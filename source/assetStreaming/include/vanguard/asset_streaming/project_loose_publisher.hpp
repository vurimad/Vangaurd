#pragma once

#include <vanguard/assets/loose_resource_materializer.hpp>
#include <vanguard/streaming/streaming.hpp>

namespace vanguard::asset_streaming
{
    enum class ProjectLoosePublicationResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        RecordNotFound,
        IndexFailure,
        TargetMismatch,
        DirectoryFailure,
        MaterializationFailure,
        StreamingPublicationFailure,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(ProjectLoosePublicationResult result) noexcept;

    struct ProjectLoosePublicationFailure
    {
        ProjectLoosePublicationResult result = ProjectLoosePublicationResult::Success;
        assets::IndexResult index = assets::IndexResult::Success;
        assets::LooseMaterializationResult materialization = assets::LooseMaterializationResult::Success;
    };

    struct ProjectLoosePublicationInfo
    {
        filesystem::AbsolutePath physicalPath;
        assets::BuildFingerprint buildFingerprint;
        assets::BuildFingerprint contentFingerprint;
        i32 priority = 0;
    };

    struct ProjectLoosePublisherConfig
    {
        filesystem::AbsolutePath artifactCacheRoot;
        filesystem::AbsolutePath looseRoot;
        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        i32 priority = 100;
        u32 maximumRuntimeDependencies = 4096;
        assets::DerivedDataArtifactLimits artifactLimits;
        assets::PersistentCacheCoordinationLimits cacheCoordination;
        assets::LooseMaterializationLimits materializationLimits;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return artifactCacheRoot.IsDirectoryPath() && looseRoot.IsDirectoryPath() && target < assets::TargetPlatform::Count &&
                   maximumRuntimeDependencies != 0 && artifactLimits.IsValid() && cacheCoordination.IsValid() && materializationLimits.IsValid();
        }
    };

    // Bridges committed VADI/VDDC state to ResourceStreamer. Publication is an
    // exclusive coordinator operation; it does not scan DerivedData or schedule
    // builds. Every physical output is addressed by its committed build generation.
    class ProjectLoosePublisher final
    {
    public:
        struct Impl;

        ProjectLoosePublisher() noexcept = default;
        ~ProjectLoosePublisher();

        ProjectLoosePublisher(const ProjectLoosePublisher&) = delete;
        ProjectLoosePublisher& operator=(const ProjectLoosePublisher&) = delete;

        [[nodiscard]] bool Initialize(assets::DependencyIndex& index, streaming::ResourceStreamer& streamer,
                                      const ProjectLoosePublisherConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Call after the corresponding graph request succeeds. Lifecycle owners
        // may also call this while restoring an already validated committed index.
        [[nodiscard]] ProjectLoosePublicationResult Publish(resources::ResourceReference resource,
                                                             ProjectLoosePublicationFailure* failure = nullptr,
                                                             ProjectLoosePublicationInfo* publication = nullptr) noexcept;
        [[nodiscard]] bool IsCommittedExact(const assets::DependencyRecord& expected) const noexcept;
        // Validates the exact VADI-referenced immutable artifact set without
        // materializing or changing loose visibility. Missing/corrupt/content-
        // mismatched records are reported through `failure.materialization` so
        // the project owner can route repair into its existing recook scheduler.
        [[nodiscard]] ProjectLoosePublicationResult ValidateExact(
            const assets::DependencyRecord& expected, ProjectLoosePublicationFailure* failure = nullptr) noexcept;
        // P5 restart/LKG path. The caller supplies an exact record loaded from the
        // DependencyIndex retirement-protection journal. It is intentionally not
        // required to still be the current VADI row, but the immutable VDDC
        // artifact set and target are validated exactly before use.
        [[nodiscard]] ProjectLoosePublicationResult ValidateRetainedExact(
            const assets::DependencyRecord& expected, ProjectLoosePublicationFailure* failure = nullptr) noexcept;
        [[nodiscard]] ProjectLoosePublicationResult PrepareExact(
            const assets::DependencyRecord& expected, containers::DynamicArray<streaming::DependencyDescriptor>& dependencies,
            u64& expectedContentCrc64, ProjectLoosePublicationFailure* failure = nullptr,
            ProjectLoosePublicationInfo* publication = nullptr) noexcept;
        [[nodiscard]] ProjectLoosePublicationResult PrepareRetainedExact(
            const assets::DependencyRecord& expected, containers::DynamicArray<streaming::DependencyDescriptor>& dependencies,
            u64& expectedContentCrc64, ProjectLoosePublicationFailure* failure = nullptr,
            ProjectLoosePublicationInfo* publication = nullptr) noexcept;
        [[nodiscard]] bool PublishPrepared(containers::ArraySpan<const streaming::LooseResourceDescriptor> resources) noexcept;
        [[nodiscard]] bool PrepareVisibility(containers::ArraySpan<const streaming::LooseResourceDescriptor> resources,
                                             streaming::PreparedLooseBatch& batch) noexcept;
        [[nodiscard]] bool CommitPreparedVisibility(streaming::PreparedLooseBatch& batch) noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::asset_streaming
