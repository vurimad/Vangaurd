#pragma once

#include <vanguard/assets/asset_file_transaction.hpp>
#include <vanguard/assets/project_source_data.hpp>

namespace vanguard::assets
{
    enum class AssetOperationResult : u8
    {
        Success,
        InvalidArgument,
        ReplacementUnavailable,
        ReferenceStateUnavailable,
        ReadOnlySource,
        ImporterUnavailable,
        RewriteUnavailable,
        RelocationUnavailable,
        SourceUnavailable,
        CandidateUnavailable,
        SettingsInvalid,
        TargetConflict,
        IdentityConflict,
        ReferencesRemain,
        RewriteFailed,
        TransactionFailed,
        LimitExceeded,
        UnsupportedSource,
        AmbiguousSource,
        ImportDescriptionUnavailable
    };

    struct AssetOperationFailure
    {
        AssetOperationResult result = AssetOperationResult::Success;
        AssetId source;
        resources::ResourceReference target;
        SourceImporterId importer = InvalidSourceImporterId;
        ProjectSourceDataResult sourceData = ProjectSourceDataResult::Success;
        AssetFileTransactionResult transaction = AssetFileTransactionResult::Success;
    };

    struct AssetReferenceRewrite
    {
        AssetReference reference;
        resources::ResourceReference replacement;
    };

    struct AssetReferenceRewriteSource
    {
        AssetId asset;
        filesystem::AbsolutePath path;
        SourceImporterId importer = InvalidSourceImporterId;
        u32 firstRewrite = 0;
        u32 rewriteCount = 0;
    };

    // Immutable after planning. Entries are grouped by source so an executor
    // can load and rewrite each authored document exactly once.
    class AssetReferenceRewritePlan final
    {
    public:
        AssetReferenceRewritePlan() noexcept;
        AssetReferenceRewritePlan(const AssetReferenceRewritePlan&) = delete;
        AssetReferenceRewritePlan& operator=(const AssetReferenceRewritePlan&) = delete;
        AssetReferenceRewritePlan(AssetReferenceRewritePlan&&) noexcept = default;
        AssetReferenceRewritePlan& operator=(AssetReferenceRewritePlan&&) noexcept = default;

        [[nodiscard]] resources::ResourceReference GetPrevious() const noexcept;
        [[nodiscard]] resources::ResourceReference GetReplacement() const noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReferenceRewriteSource> GetSources() const noexcept;
        [[nodiscard]] containers::ArraySpan<const AssetReferenceRewrite> GetRewrites() const noexcept;

    private:
        resources::ResourceReference m_previous;
        resources::ResourceReference m_replacement;
        containers::DynamicArray<AssetReferenceRewriteSource> m_sources;
        containers::DynamicArray<AssetReferenceRewrite> m_rewrites;

        friend AssetOperationResult PlanAssetReferenceReplacement(const SourceDatabase&, containers::ArraySpan<const SourceImporterDescriptor>,
                                                                  resources::ResourceReference, resources::ResourceReference,
                                                                  AssetReferenceRewritePlan&, AssetOperationFailure*) noexcept;
    };

    // Checked, write-free preflight. The replacement must be a current catalog
    // output of the same type. Every affected source must have a current scan,
    // live exact-version importer, format-owned rewrite hook and writable root.
    // Failure leaves the caller's plan unchanged.
    [[nodiscard]] AssetOperationResult PlanAssetReferenceReplacement(const SourceDatabase& database,
                                                                     containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                                     resources::ResourceReference previous,
                                                                     resources::ResourceReference replacement,
                                                                     AssetReferenceRewritePlan& plan,
                                                                     AssetOperationFailure* failure = nullptr) noexcept;

    struct AssetOperationContext
    {
        filesystem::Manager* files = nullptr;
        const SourceDatabase* database = nullptr;
        containers::ArraySpan<const SourceImporterDescriptor> importers;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        ProjectSourceDataLimits sourceData;
        AssetFileTransaction* transaction = nullptr;
        // Optional immediate-call snapshot used by sidecar-only mutation owners
        // that already captured the current VMETA for undo history. This avoids
        // reading the same bounded file twice before transaction CAS.
        containers::ArraySpan<const u8> selectedMetadataBytes;
        // Optional fixed-capacity commit receipt. Paths and result slots are
        // owned by the caller for the duration of this immediate operation.
        // The transaction fills exact authored postconditions without retaining
        // them after Commit returns.
        containers::ArraySpan<const filesystem::AbsolutePath> commitTrackedPaths;
        containers::ArraySpan<AssetFileTransactionPostCondition> commitPostConditions;
        // Optional exact pre-command conditions for the same tracked paths.
        // Pipeline history uses these to prevent a command from consuming bytes
        // that changed after its inverse snapshot was captured.
        containers::ArraySpan<const AssetFileTransactionPostCondition> mutationPreconditions;
    };

    // These owner-thread executors commit one complete source transaction.
    // They never rebuild the catalog or touch cooked/runtime state; the shared
    // project pipeline performs that reconciliation after commit.
    // Pure canonical first-import metadata preflight. This performs the same
    // importer/settings/schema validation used by adoption executors and returns
    // the exact VMETA bytes they will author, without touching the filesystem.
    [[nodiscard]] AssetOperationResult EncodeAssetImportMetadata(
        const SourceImporterDescriptor& importer, AssetId newIdentity,
        const SourceImportDescription& description, containers::String& encoded,
        AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetAdopt(const AssetOperationContext& context,
                                                         const filesystem::AbsolutePath& source,
                                                         SourceImporterId expectedImporter,
                                                         u32 expectedImporterVersion,
                                                         AssetId newIdentity,
                                                         const SourceImportDescription& description,
                                                         AssetOperationFailure* failure = nullptr) noexcept;
    // Atomically creates a previously unmanaged authored source and its VMETA.
    // The caller owns importer selection/default-description and AssetId/output
    // collision admission; this executor revalidates the writable destination,
    // exact importer and canonical metadata before committing either file.
    [[nodiscard]] AssetOperationResult ExecuteAssetCreateAndAdopt(
        const AssetOperationContext& context, const filesystem::AbsolutePath& destination,
        containers::ArraySpan<const u8> sourceBytes, SourceImporterId expectedImporter,
        u32 expectedImporterVersion, AssetId newIdentity, const SourceImportDescription& description,
        AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetUpdateSettings(
        const AssetOperationContext& context, AssetId asset, u32 settingsVersion,
        containers::ArraySpan<const u8> settings, AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetMove(const AssetOperationContext& context, AssetId asset,
                                                        const filesystem::AbsolutePath& target,
                                                        AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetDuplicate(const AssetOperationContext& context, AssetId asset,
                                                             const filesystem::AbsolutePath& target, AssetId newIdentity,
                                                             AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetRetarget(const AssetOperationContext& context,
                                                            containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                                            AssetOperationFailure* failure = nullptr) noexcept;
    [[nodiscard]] AssetOperationResult ExecuteAssetDelete(const AssetOperationContext& context, AssetId asset,
                                                          containers::ArraySpan<const AssetReferenceReplacement> replacements = {},
                                                          AssetOperationFailure* failure = nullptr) noexcept;
} // namespace vanguard::assets
