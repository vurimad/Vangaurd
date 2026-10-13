#pragma once

#include <vanguard/assets/asset_file_transaction.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include "../../../imported/common/resource/include/resourceEditorDepotImpl.h"

#include <mutex>
#include <shared_mutex>

namespace vanguard::asset_host
{
    class DurableResourceDepot final : public res::ResourceEditorDepot
    {
    public:
        using CommittedPathSink = void (*)(void* context, containers::ArraySpan<const res::ResourcePath> paths) noexcept;

        explicit DurableResourceDepot(const red::DynArray<res::prv::DepotMapping>& mappings);
        ~DurableResourceDepot() override;
        DurableResourceDepot(const DurableResourceDepot&) = delete;
        DurableResourceDepot& operator=(const DurableResourceDepot&) = delete;

        [[nodiscard]] assets::AssetFileTransactionResult Initialize(
            filesystem::Manager& files, const filesystem::AbsolutePath& writableRoot, const filesystem::AbsolutePath& journalPath,
            assets::AssetFileRecoveryReport* recovery = nullptr) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult Initialize(
            filesystem::Manager& files, containers::ArraySpan<const filesystem::AbsolutePath> writableRoots,
            const filesystem::AbsolutePath& journalPath, assets::AssetFileRecoveryReport* recovery = nullptr) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult Shutdown() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult LastTransactionResult() const noexcept { return m_lastResult; }
        [[nodiscard]] bool IsReady() const noexcept;
        void SetCommittedPathSink(void* context, CommittedPathSink sink) noexcept;

        // Compound authored-resource publication. Serialization/semantic
        // validation happens before staging; authoritative depot readers are
        // admitted until ApplyCohort(), then blocked through the durable
        // Committed journal linearization point.
        [[nodiscard]] assets::AssetFileTransactionResult BeginCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult StageCohortWrite(
            res::ResourcePath resourcePath, containers::ArraySpan<const u8> bytes) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult StageCohortCreate(
            res::ResourcePath resourcePath, containers::ArraySpan<const u8> bytes) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult StageCohortDelete(res::ResourcePath resourcePath) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult PrepareCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult ApplyCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult PublishCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult CompleteCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult CommitCohort() noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult RollbackCohort() noexcept;
        [[nodiscard]] bool CohortActive() const noexcept { return m_cohortActive; }

        red::SharedPtr<serialization::IAsyncSource> CreateResourceAsyncSource(
            res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params) override;
        bool ResourceExists(res::ResourcePath path) const override;
        bool ResourceIsReadOnly(res::ResourcePath path) const override;
        Uint64 GetResourceTimestamp(res::ResourcePath path) const override;
        res::ResourcePath ResolveResourcePath(res::ResourcePath path) const override;
        const char* GetDebugName() const override;
        Uint32 Debug_GetResourceTotalSizeOnDisk(res::ResourcePath path) const override;
        Uint32 Debug_GetResourceObjectSizeOnDisk(res::ResourcePath path) const override;
        bool IsLanguageVoicePackAvailable(CName languageCodeName) const override;
        bool IsLanguageVoicePackInstalled(CName languageCodeName) const override;
        const red::DynArray<red::String>& GetDLCRootPaths() const override;

        red::SharedPtr<IFile> CreateResourceReader(res::ResourcePath resourcePath, bool followLinks = true) override;
        red::SharedPtr<IFile> CreateResourceWriter(res::ResourcePath resourcePath) override;
        void FindDepotSubDirectories(const String& searchDepotPath, red::DynArray<String>& outDirectoryNames) const override;
        void FindDepotFiles(const red::String& searchDepotPath, const char* pattern, red::DynArray<String>& outFileNames) const override;
        bool RemoveResource(res::ResourcePath path) override;
        red::AbsolutePath TranslateResourcePath(res::ResourcePath resourcePath) const override;
        red::AbsolutePath ResolveWritableResourcePath(res::ResourcePath resourcePath) const override;
        res::ResourcePath TranslateAbsolutePath(const red::AbsolutePath& absolutePath) const override;
        bool ResourceIsLink(res::ResourcePath path) const override;
        void SetDLCRootPaths(const red::DynArray<String>& rootPaths) override;
        void ScanForDLCRootPaths() override;

    private:
        friend class DurableResourceWriter;

        [[nodiscard]] assets::AssetFileTransactionResult CommitWrite(
            res::ResourcePath resourcePath, const filesystem::AbsolutePath& target, containers::ArraySpan<const u8> bytes) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult CommitDelete(
            res::ResourcePath resourcePath, const filesystem::AbsolutePath& target) noexcept;
        [[nodiscard]] assets::AssetFileTransactionResult ApplyAndPublishPrepared(
            containers::ArraySpan<const res::ResourcePath> committedPaths) noexcept;
        [[nodiscard]] filesystem::AbsolutePath ResolveWritableTarget(res::ResourcePath resourcePath) const noexcept;
        void NotifyCommittedPaths(containers::ArraySpan<const res::ResourcePath> paths) noexcept;
        void ReleaseWriter() noexcept { m_writerActive = false; }

        filesystem::Manager* m_files = nullptr;
        assets::AssetFileTransaction m_transaction;
        assets::AssetFileTransactionResult m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        mutable std::shared_mutex m_publicationGate;
        std::unique_lock<std::shared_mutex> m_publicationLock;
        red::DynArray<res::ResourcePath> m_pendingCommittedPaths{red::PoolBackend()};
        void* m_committedPathContext = nullptr;
        CommittedPathSink m_committedPathSink = nullptr;
        bool m_writerActive = false;
        bool m_cohortActive = false;
    };
} // namespace vanguard::asset_host
