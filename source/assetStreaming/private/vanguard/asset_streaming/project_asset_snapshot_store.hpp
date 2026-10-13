#pragma once

#include <vanguard/asset_streaming/project_asset_change_state_internal.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::asset_streaming
{
    inline constexpr u32 ProjectAssetSnapshotMagic = serialization::MakeFourCC('V', 'P', 'A', 'S');
    inline constexpr u16 ProjectAssetSnapshotMajorVersion = 1;
    inline constexpr u16 ProjectAssetSnapshotMinorVersion = 1;
    inline constexpr u32 ProjectAssetSnapshotHeaderSize = 128;
    inline constexpr char ProjectAssetSnapshotFileName[] = "project-asset-accepted.vpas";
    inline constexpr char ProjectAssetSnapshotStageFileName[] = "project-asset-accepted.vpas.tmp";

    [[nodiscard]] filesystem::AbsolutePath ProjectAssetSnapshotPath(const filesystem::AbsolutePath& root) noexcept;
    [[nodiscard]] filesystem::AbsolutePath ProjectAssetSnapshotStagePath(const filesystem::AbsolutePath& root) noexcept;

    // Load is failure-atomic: destination changes only after the complete image
    // validates. Corrupt/incompatible/over-limit files are removed so the caller
    // can conservatively rebuild from the current catalog; true I/O failures are
    // never reported as recovery.
    [[nodiscard]] ProjectAssetSnapshotResult LoadProjectAssetAcceptedSnapshot(
        filesystem::Manager& files, const filesystem::AbsolutePath& root,
        const ProjectAssetChangeStateLimits& limits, ProjectAssetAcceptedBaseline& destination) noexcept;

    // Stage performs all project-sized serialization, write, flush and read-back
    // verification. Commit is only the same-directory atomic replacement.
    [[nodiscard]] ProjectAssetSnapshotResult StageProjectAssetAcceptedSnapshot(
        filesystem::Manager& files, const filesystem::AbsolutePath& root,
        const ProjectAssetAcceptedBaseline& snapshot) noexcept;
    [[nodiscard]] ProjectAssetSnapshotResult CommitStagedProjectAssetAcceptedSnapshot(
        filesystem::Manager& files, const filesystem::AbsolutePath& root) noexcept;
    [[nodiscard]] bool DiscardStagedProjectAssetAcceptedSnapshot(
        filesystem::Manager& files, const filesystem::AbsolutePath& root) noexcept;
} // namespace vanguard::asset_streaming
