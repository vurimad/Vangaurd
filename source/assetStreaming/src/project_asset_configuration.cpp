#include <vanguard/asset_streaming/project_asset_configuration.hpp>

#include <vanguard/crypto/crypto.hpp>

namespace vanguard::asset_streaming
{
    bool MakeProjectAssetPipelineConfig(const filesystem::AbsolutePath& derivedDataRoot,
                                        const filesystem::AbsolutePath& intermediateRoot,
                                        const containers::StringView target,
                                        ProjectAssetPipelineConfig& config) noexcept
    {
        if (!derivedDataRoot.IsDirectoryPath() || !intermediateRoot.IsDirectoryPath() || target != "windows-x64")
            return false;

        ProjectAssetPipelineConfig candidate;
        const filesystem::AbsolutePath derivedOwner = derivedDataRoot.AddDirPath("ProjectAssets");
        const filesystem::AbsolutePath intermediateOwner = intermediateRoot.AddDirPath("ProjectAssets");
        candidate.build.artifactCacheRoot = derivedOwner.AddDirPath("VDDC");
        candidate.build.dependencyIndexRoot = derivedOwner.AddDirPath("Index");
        candidate.build.target = assets::TargetPlatform::WindowsD3D12;
        constexpr char SettingsIdentity[] = "vanguard.editor.project-assets.texture.v1";
        candidate.build.settingsFingerprint = crypto::Sha256(SettingsIdentity, sizeof(SettingsIdentity) - 1u);
        candidate.publication.artifactCacheRoot = candidate.build.artifactCacheRoot;
        candidate.publication.looseRoot = intermediateOwner.AddDirPath("Loose");
        candidate.publication.target = candidate.build.target;
        candidate.operationJournalPath = intermediateOwner.AddFilePath("operations.vtxn");
        candidate.maximumOperationHistoryBytes = candidate.maximumOperationFileBytes;
        candidate.automaticRecook.enabled = false;
        if (!candidate.IsValid())
            return false;

        config = static_cast<ProjectAssetPipelineConfig&&>(candidate);
        return true;
    }
} // namespace vanguard::asset_streaming
