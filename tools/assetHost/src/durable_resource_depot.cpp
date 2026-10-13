#include "../../../imported/backend/backendData/src/build.h"

#include <vanguard/asset_host/durable_resource_depot.hpp>

#include <limits>

namespace vanguard::asset_host
{
    namespace
    {
        [[nodiscard]] assets::AssetFileTransactionResult RollbackIfPossible(
            assets::AssetFileTransaction& transaction, const assets::AssetFileTransactionResult result) noexcept
        {
            const assets::AssetFileTransactionState state = transaction.GetState();
            if (state != assets::AssetFileTransactionState::Collecting && state != assets::AssetFileTransactionState::Prepared)
                return result;
            const assets::AssetFileTransactionResult rollback = transaction.Rollback();
            return rollback == assets::AssetFileTransactionResult::Success ? result : rollback;
        }

        [[nodiscard]] bool LogicalCommitSucceeded(const assets::AssetFileTransactionResult result) noexcept
        {
            return result == assets::AssetFileTransactionResult::Success ||
                   result == assets::AssetFileTransactionResult::CommittedCleanupPending;
        }
    }

    class DurableResourceWriter final : public IFile
    {
    public:
        DurableResourceWriter(DurableResourceDepot& owner, const res::ResourcePath resourcePath,
                              filesystem::AbsolutePath target) noexcept
            : IFile(FF_FileBased | FF_Writer)
            , m_owner(owner)
            , m_resourcePath(resourcePath)
            , m_target(static_cast<filesystem::AbsolutePath&&>(target))
        {
        }

        ~DurableResourceWriter() override
        {
            m_owner.ReleaseWriter();
        }

        void Serialize(void* const buffer, const size_t size) override
        {
            if (m_committed || m_offset != 0u || (size != 0u && buffer == nullptr) ||
                size > static_cast<size_t>(std::numeric_limits<u32>::max()))
            {
                HandleIOError("VanguardAssetHost: durable RED resource writer received an invalid write sequence for '%hs'",
                              m_target.AsChar());
                return;
            }

            const auto bytes = containers::ArraySpan<const u8>(static_cast<const u8*>(buffer), static_cast<u32>(size));
            const assets::AssetFileTransactionResult result = m_owner.CommitWrite(m_resourcePath, m_target, bytes);
            if (!LogicalCommitSucceeded(result))
            {
                HandleIOError("VanguardAssetHost: durable RED resource transaction failed for '%hs' (result=%u)",
                              m_target.AsChar(), static_cast<Uint32>(result));
                return;
            }
            m_offset = static_cast<Uint64>(size);
            m_size = m_offset;
            m_committed = true;
        }

        Uint64 GetOffset() const override { return m_offset; }
        Uint64 GetSize() const override { return m_size; }

        void Seek(const Int64 offset) override
        {
            if (offset < 0 || static_cast<Uint64>(offset) != m_offset)
                HandleIOError("VanguardAssetHost: durable RED resource writer does not support seeking for '%hs'", m_target.AsChar());
        }

        void Flush() override {}
        const char* GetFileNameForDebug() const override { return m_target.AsChar(); }

    private:
        DurableResourceDepot& m_owner;
        res::ResourcePath m_resourcePath;
        filesystem::AbsolutePath m_target;
        Uint64 m_offset = 0u;
        Uint64 m_size = 0u;
        bool m_committed = false;
    };

    DurableResourceDepot::DurableResourceDepot(const red::DynArray<res::prv::DepotMapping>& mappings)
        : res::ResourceEditorDepot(mappings)
    {
    }

    DurableResourceDepot::~DurableResourceDepot()
    {
        if (m_files != nullptr && !m_writerActive && !m_cohortActive && !m_publicationLock.owns_lock())
            static_cast<void>(Shutdown());
    }

    assets::AssetFileTransactionResult DurableResourceDepot::Initialize(
        filesystem::Manager& files, const filesystem::AbsolutePath& writableRoot, const filesystem::AbsolutePath& journalPath,
        assets::AssetFileRecoveryReport* const recovery) noexcept
    {
        const filesystem::AbsolutePath roots[] = {writableRoot};
        return Initialize(files, {roots, 1u}, journalPath, recovery);
    }

    assets::AssetFileTransactionResult DurableResourceDepot::Initialize(
        filesystem::Manager& files, const containers::ArraySpan<const filesystem::AbsolutePath> writableRoots,
        const filesystem::AbsolutePath& journalPath, assets::AssetFileRecoveryReport* const recovery) noexcept
    {
        if (m_files != nullptr || writableRoots.Empty() || !journalPath.IsFilePath())
            return assets::AssetFileTransactionResult::InvalidState;
        for (const filesystem::AbsolutePath& root : writableRoots)
            if (!root.IsDirectoryPath())
                return assets::AssetFileTransactionResult::InvalidArgument;
        assets::AssetFileTransactionConfig config;
        config.journalPath = journalPath;
        config.writableRoots = writableRoots;
        config.assertionRoots = writableRoots;
        m_lastResult = m_transaction.Initialize(files, config, recovery);
        if (m_lastResult != assets::AssetFileTransactionResult::Success)
            return m_lastResult;
        m_files = &files;
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::Shutdown() noexcept
    {
        if (m_files == nullptr)
            return assets::AssetFileTransactionResult::Success;
        if (m_writerActive || m_cohortActive || m_publicationLock.owns_lock())
            return assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.Shutdown();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_files = nullptr;
        return m_lastResult;
    }

    bool DurableResourceDepot::IsReady() const noexcept
    {
        return m_files != nullptr && m_transaction.GetState() == assets::AssetFileTransactionState::Ready;
    }

    void DurableResourceDepot::SetCommittedPathSink(void* const context, const CommittedPathSink sink) noexcept
    {
        m_committedPathContext = context;
        m_committedPathSink = sink;
    }

    void DurableResourceDepot::NotifyCommittedPaths(const containers::ArraySpan<const res::ResourcePath> paths) noexcept
    {
        if (m_committedPathSink != nullptr && !paths.Empty())
            m_committedPathSink(m_committedPathContext, paths);
    }

    filesystem::AbsolutePath DurableResourceDepot::ResolveWritableTarget(const res::ResourcePath resourcePath) const noexcept
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::ResolveWritableResourcePath(resourcePath);
    }

    assets::AssetFileTransactionResult DurableResourceDepot::ApplyAndPublishPrepared(
        const containers::ArraySpan<const res::ResourcePath> committedPaths) noexcept
    {
        m_publicationLock = std::unique_lock<std::shared_mutex>(m_publicationGate);
        m_lastResult = m_transaction.ApplyPrepared();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.PublishCommit();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            NotifyCommittedPaths(committedPaths);
        if (!LogicalCommitSucceeded(m_lastResult))
            m_lastResult = RollbackIfPossible(m_transaction, m_lastResult);
        if (m_publicationLock.owns_lock() && m_transaction.GetState() != assets::AssetFileTransactionState::RecoveryRequired)
            m_publicationLock.unlock();
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::CommitWrite(
        const res::ResourcePath resourcePath, const filesystem::AbsolutePath& target,
        const containers::ArraySpan<const u8> bytes) noexcept
    {
        if (!IsReady())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.Begin();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.StageWrite(target, bytes);
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.Prepare();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
        {
            const res::ResourcePath committedPaths[] = {resourcePath};
            m_lastResult = ApplyAndPublishPrepared({committedPaths, 1u});
        }
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.CompleteCommit();
        if (!LogicalCommitSucceeded(m_lastResult))
            m_lastResult = RollbackIfPossible(m_transaction, m_lastResult);
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::CommitDelete(
        const res::ResourcePath resourcePath, const filesystem::AbsolutePath& target) noexcept
    {
        if (!IsReady())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.Begin();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.StageDelete(target);
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.Prepare();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
        {
            const res::ResourcePath committedPaths[] = {resourcePath};
            m_lastResult = ApplyAndPublishPrepared({committedPaths, 1u});
        }
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = m_transaction.CompleteCommit();
        if (!LogicalCommitSucceeded(m_lastResult))
            m_lastResult = RollbackIfPossible(m_transaction, m_lastResult);
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::BeginCohort() noexcept
    {
        if (!IsReady() || m_writerActive || m_cohortActive || m_publicationLock.owns_lock())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.Begin();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
        {
            m_cohortActive = true;
            m_pendingCommittedPaths.Clear();
        }
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::StageCohortWrite(
        const res::ResourcePath resourcePath, const containers::ArraySpan<const u8> bytes) noexcept
    {
        if (!m_cohortActive || m_transaction.GetState() != assets::AssetFileTransactionState::Collecting)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        const filesystem::AbsolutePath target = ResolveWritableTarget(resourcePath);
        if (!target.IsFilePath())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidArgument;
        m_lastResult = m_transaction.StageWrite(target, bytes);
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_pendingCommittedPaths.PushBack(resourcePath);
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::StageCohortCreate(
        const res::ResourcePath resourcePath, const containers::ArraySpan<const u8> bytes) noexcept
    {
        if (!m_cohortActive || m_transaction.GetState() != assets::AssetFileTransactionState::Collecting)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        const filesystem::AbsolutePath target = ResolveWritableTarget(resourcePath);
        if (!target.IsFilePath())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidArgument;
        m_lastResult = m_transaction.StageCreate(target, bytes);
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_pendingCommittedPaths.PushBack(resourcePath);
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::StageCohortDelete(const res::ResourcePath resourcePath) noexcept
    {
        if (!m_cohortActive || m_transaction.GetState() != assets::AssetFileTransactionState::Collecting)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        const filesystem::AbsolutePath target = ResolveWritableTarget(resourcePath);
        if (!target.IsFilePath())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidArgument;
        m_lastResult = m_transaction.StageDelete(target);
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_pendingCommittedPaths.PushBack(resourcePath);
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::PrepareCohort() noexcept
    {
        if (!m_cohortActive || m_transaction.GetState() != assets::AssetFileTransactionState::Collecting)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        return m_lastResult = m_transaction.Prepare();
    }

    assets::AssetFileTransactionResult DurableResourceDepot::ApplyCohort() noexcept
    {
        if (!m_cohortActive || m_transaction.GetState() != assets::AssetFileTransactionState::Prepared || m_publicationLock.owns_lock())
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_publicationLock = std::unique_lock<std::shared_mutex>(m_publicationGate);
        m_lastResult = m_transaction.ApplyPrepared();
        if (m_lastResult != assets::AssetFileTransactionResult::Success)
        {
            if (m_transaction.GetState() == assets::AssetFileTransactionState::Ready)
            {
                m_publicationLock.unlock();
                m_cohortActive = false;
            }
        }
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::PublishCohort() noexcept
    {
        if (!m_cohortActive || !m_publicationLock.owns_lock() ||
            m_transaction.GetState() != assets::AssetFileTransactionState::Prepared)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.PublishCommit();
        if (m_lastResult != assets::AssetFileTransactionResult::Success)
        {
            m_lastResult = RollbackIfPossible(m_transaction, m_lastResult);
            if (m_transaction.GetState() == assets::AssetFileTransactionState::Ready)
            {
                m_cohortActive = false;
                m_publicationLock.unlock();
            }
            return m_lastResult;
        }
        NotifyCommittedPaths({m_pendingCommittedPaths.TypedData(), m_pendingCommittedPaths.Size()});
        m_publicationLock.unlock();
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::CompleteCohort() noexcept
    {
        if (!m_cohortActive || m_publicationLock.owns_lock() ||
            m_transaction.GetState() != assets::AssetFileTransactionState::Committed)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        m_lastResult = m_transaction.CompleteCommit();
        m_cohortActive = false;
        m_pendingCommittedPaths.Clear();
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::CommitCohort() noexcept
    {
        if (!m_cohortActive)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        if (m_transaction.GetState() == assets::AssetFileTransactionState::Collecting)
            m_lastResult = PrepareCohort();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = ApplyCohort();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = PublishCohort();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
            m_lastResult = CompleteCohort();
        return m_lastResult;
    }

    assets::AssetFileTransactionResult DurableResourceDepot::RollbackCohort() noexcept
    {
        if (!m_cohortActive)
            return m_lastResult = assets::AssetFileTransactionResult::InvalidState;
        std::unique_lock<std::shared_mutex> localLock;
        if (!m_publicationLock.owns_lock())
            localLock = std::unique_lock<std::shared_mutex>(m_publicationGate);
        m_lastResult = m_transaction.Rollback();
        if (m_lastResult == assets::AssetFileTransactionResult::Success)
        {
            if (m_publicationLock.owns_lock())
                m_publicationLock.unlock();
            m_cohortActive = false;
            m_pendingCommittedPaths.Clear();
        }
        return m_lastResult;
    }

    red::SharedPtr<serialization::IAsyncSource> DurableResourceDepot::CreateResourceAsyncSource(
        const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params)
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::CreateResourceAsyncSource(resourcePath, params);
    }

    bool DurableResourceDepot::ResourceExists(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::ResourceExists(path); }
    bool DurableResourceDepot::ResourceIsReadOnly(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::ResourceIsReadOnly(path); }
    Uint64 DurableResourceDepot::GetResourceTimestamp(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::GetResourceTimestamp(path); }
    res::ResourcePath DurableResourceDepot::ResolveResourcePath(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::ResolveResourcePath(path); }
    const char* DurableResourceDepot::GetDebugName() const { return res::ResourceEditorDepot::GetDebugName(); }
    Uint32 DurableResourceDepot::Debug_GetResourceTotalSizeOnDisk(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::Debug_GetResourceTotalSizeOnDisk(path); }
    Uint32 DurableResourceDepot::Debug_GetResourceObjectSizeOnDisk(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::Debug_GetResourceObjectSizeOnDisk(path); }
    bool DurableResourceDepot::IsLanguageVoicePackAvailable(const CName languageCodeName) const { return res::ResourceEditorDepot::IsLanguageVoicePackAvailable(languageCodeName); }
    bool DurableResourceDepot::IsLanguageVoicePackInstalled(const CName languageCodeName) const { return res::ResourceEditorDepot::IsLanguageVoicePackInstalled(languageCodeName); }

    const red::DynArray<red::String>& DurableResourceDepot::GetDLCRootPaths() const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::GetDLCRootPaths();
    }

    red::SharedPtr<IFile> DurableResourceDepot::CreateResourceReader(const res::ResourcePath resourcePath, const bool followLinks)
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::CreateResourceReader(resourcePath, followLinks);
    }

    red::SharedPtr<IFile> DurableResourceDepot::CreateResourceWriter(const res::ResourcePath resourcePath)
    {
        if (!IsReady() || m_writerActive || m_cohortActive)
            return {};
        const filesystem::AbsolutePath target = ResolveWritableTarget(resourcePath);
        if (!target.IsFilePath())
            return {};
        red::SharedPtr<IFile> result = red::CreateSharedPtr<DurableResourceWriter>(*this, resourcePath, target);
        if (result)
            m_writerActive = true;
        return result;
    }

    void DurableResourceDepot::FindDepotSubDirectories(const String& searchDepotPath, red::DynArray<String>& outDirectoryNames) const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        res::ResourceEditorDepot::FindDepotSubDirectories(searchDepotPath, outDirectoryNames);
    }

    void DurableResourceDepot::FindDepotFiles(const red::String& searchDepotPath, const char* const pattern,
                                              red::DynArray<String>& outFileNames) const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        res::ResourceEditorDepot::FindDepotFiles(searchDepotPath, pattern, outFileNames);
    }

    bool DurableResourceDepot::RemoveResource(const res::ResourcePath path)
    {
        if (!IsReady() || m_writerActive || m_cohortActive)
            return false;
        const filesystem::AbsolutePath target = ResolveWritableTarget(path);
        return target.IsFilePath() && LogicalCommitSucceeded(CommitDelete(path, target));
    }

    red::AbsolutePath DurableResourceDepot::TranslateResourcePath(const res::ResourcePath resourcePath) const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::TranslateResourcePath(resourcePath);
    }

    red::AbsolutePath DurableResourceDepot::ResolveWritableResourcePath(const res::ResourcePath resourcePath) const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::ResolveWritableResourcePath(resourcePath);
    }

    res::ResourcePath DurableResourceDepot::TranslateAbsolutePath(const red::AbsolutePath& absolutePath) const
    {
        std::shared_lock<std::shared_mutex> lock(m_publicationGate);
        return res::ResourceEditorDepot::TranslateAbsolutePath(absolutePath);
    }

    bool DurableResourceDepot::ResourceIsLink(const res::ResourcePath path) const { std::shared_lock<std::shared_mutex> lock(m_publicationGate); return res::ResourceEditorDepot::ResourceIsLink(path); }
    void DurableResourceDepot::SetDLCRootPaths(const red::DynArray<String>& rootPaths) { std::unique_lock<std::shared_mutex> lock(m_publicationGate); res::ResourceEditorDepot::SetDLCRootPaths(rootPaths); }
    void DurableResourceDepot::ScanForDLCRootPaths() { std::unique_lock<std::shared_mutex> lock(m_publicationGate); res::ResourceEditorDepot::ScanForDLCRootPaths(); }
} // namespace vanguard::asset_host
