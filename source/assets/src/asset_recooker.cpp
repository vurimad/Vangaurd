#include <vanguard/assets/asset_recooker.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    [[nodiscard]] bool Contains(const containers::DynamicArray<resources::ResourceReference>& values, const resources::ResourceReference value) noexcept
    {
        for (const resources::ResourceReference existing : values)
        {
            if (existing == value)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool Contains(const containers::ArraySpan<const resources::ResourceReference> values,
                                const resources::ResourceReference value) noexcept
    {
        for (const resources::ResourceReference existing : values)
            if (existing == value)
                return true;
        return false;
    }

    [[nodiscard]] bool AddUnique(containers::DynamicArray<resources::ResourceReference>& values, const resources::ResourceReference value,
                                 const u32 limit) noexcept
    {
        if (Contains(values, value))
        {
            return true;
        }
        if (values.Size() >= limit)
        {
            return false;
        }
        const u32 previous = values.Size();
        values.PushBack(value);
        return values.Size() == previous + 1u;
    }
} // namespace

namespace vanguard::assets
{
    struct RecookOperation
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        RecookOperation() noexcept
            : requests(memory::pools::Assets::GetInstance()), committedRecords(memory::pools::Assets::GetInstance())
        {
        }

        mutable concurrency::Mutex lock;
        containers::DynamicArray<GraphRequest> requests;
        concurrency::Atomic<u32> state{static_cast<u32>(RecookState::Building)};
        concurrency::Atomic<u32> failure{static_cast<u32>(RecookFailure::None)};
        RecookStats stats;
        RecookPreCommitValidator validator;
        containers::DynamicArray<DependencyRecord> committedRecords;
        bool cancelRequested = false;
    };

    struct IncrementalRecooker::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        BuildSystem* buildSystem = nullptr;
        BuildGraph* graph = nullptr;
        DependencyIndex* index = nullptr;
        ResolveIndexedBuildRequestFunction resolver = nullptr;
        void* resolverUserData = nullptr;
        RecookConfig config;
        mutable concurrency::Mutex lock;
        RecookOperation* active = nullptr;
        RecookStats stats;
    };

    namespace
    {
        [[nodiscard]] bool IsTerminal(const RecookState state) noexcept
        {
            return state == RecookState::Succeeded || state == RecookState::Failed || state == RecookState::Cancelled;
        }

        void SetTerminal(RecookOperation& operation, const RecookState state, const RecookFailure failure) noexcept
        {
            operation.failure.SetValue(static_cast<u32>(failure));
            operation.state.SetValue(static_cast<u32>(state));
            if (state == RecookState::Succeeded)
            {
                operation.stats.completedBatches = 1;
            }
            else if (state == RecookState::Cancelled)
            {
                operation.stats.cancelledBatches = 1;
            }
            else
            {
                operation.stats.failedBatches = 1;
            }
        }

        [[nodiscard]] bool ResolveOwnedRequest(const IncrementalRecooker::Impl& impl, const resources::ResourceReference output,
                                               OwnedBuildRequest& owned) noexcept
        {
            if (!impl.resolver(output, owned, impl.resolverUserData))
                return false;
            const BuildRequest request = owned.View();
            return request.IsValid() && request.output == output;
        }

    } // namespace

    RecookBatch::RecookBatch(IncrementalRecooker* const owner, RecookOperation* const operation) noexcept : m_owner(owner), m_operation(operation) {}

    RecookBatch::RecookBatch(RecookBatch&& other) noexcept : m_owner(other.m_owner), m_operation(other.m_operation)
    {
        other.m_owner = nullptr;
        other.m_operation = nullptr;
    }

    RecookBatch& RecookBatch::operator=(RecookBatch&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner = other.m_owner;
            m_operation = other.m_operation;
            other.m_owner = nullptr;
            other.m_operation = nullptr;
        }
        return *this;
    }

    RecookBatch::~RecookBatch()
    {
        Reset();
    }

    bool RecookBatch::IsValid() const noexcept
    {
        return m_owner != nullptr && m_operation != nullptr;
    }

    RecookBatch::operator bool() const noexcept
    {
        return IsValid();
    }

    RecookState RecookBatch::GetStatus() const noexcept
    {
        return m_operation != nullptr ? static_cast<RecookState>(m_operation->state.GetValue()) : RecookState::Failed;
    }

    RecookFailure RecookBatch::GetError() const noexcept
    {
        return m_operation != nullptr ? static_cast<RecookFailure>(m_operation->failure.GetValue()) : RecookFailure::InvalidChange;
    }

    bool RecookBatch::HasFinished() const noexcept
    {
        return IsTerminal(GetStatus());
    }

    bool RecookBatch::Poll() noexcept
    {
        return m_owner != nullptr && m_operation != nullptr && m_owner->Poll(*m_operation, false);
    }

    void RecookBatch::Wait() noexcept
    {
        if (m_owner != nullptr && m_operation != nullptr)
        {
            static_cast<void>(m_owner->Poll(*m_operation, true));
        }
    }

    bool RecookBatch::Cancel() noexcept
    {
        return m_owner != nullptr && m_operation != nullptr && m_owner->Cancel(*m_operation);
    }

    RecookStats RecookBatch::GetStats() const noexcept
    {
        if (m_operation == nullptr)
        {
            return {};
        }
        concurrency::ScopedLock guard(m_operation->lock);
        return m_operation->stats;
    }

    bool RecookBatch::TakeCommittedRecords(containers::DynamicArray<DependencyRecord>& records) noexcept
    {
        if (m_operation == nullptr || !records.Empty())
            return false;
        concurrency::ScopedLock guard(m_operation->lock);
        if (static_cast<RecookState>(m_operation->state.GetValue()) != RecookState::Succeeded ||
            m_operation->committedRecords.Empty())
            return false;
        records.Swap(m_operation->committedRecords);
        return !records.Empty();
    }

    void RecookBatch::Reset() noexcept
    {
        if (m_owner != nullptr && m_operation != nullptr)
        {
            m_owner->Release(*m_operation);
        }
        m_owner = nullptr;
        m_operation = nullptr;
    }

    IncrementalRecooker::~IncrementalRecooker()
    {
        static_cast<void>(Shutdown());
    }

    bool IncrementalRecooker::Initialize(BuildSystem& buildSystem, BuildGraph& graph, DependencyIndex& index, const ResolveIndexedBuildRequestFunction resolver,
                                         void* const resolverUserData, const RecookConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!buildSystem.IsInitialized() || !graph.IsInitialized() || !index.IsInitialized() || !graph.IsBoundTo(buildSystem, index) || resolver == nullptr ||
            config.maximumChangesPerBatch == 0 || config.maximumAffectedOutputsPerBatch == 0 || config.maximumRootRequestsPerBatch == 0 ||
            config.maximumValidationRecordsPerBatch == 0 || config.maximumValidationEvidenceBytes == 0)
        {
            return false;
        }
        m_impl = VANGUARD_NEW(Impl);
        if (m_impl == nullptr)
        {
            return false;
        }
        m_impl->buildSystem = &buildSystem;
        m_impl->graph = &graph;
        m_impl->index = &index;
        m_impl->resolver = resolver;
        m_impl->resolverUserData = resolverUserData;
        m_impl->config = config;
        return true;
    }

    bool IncrementalRecooker::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.Acquire();
        if (m_impl->active != nullptr)
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->lock.Release();
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool IncrementalRecooker::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    RecookBatch IncrementalRecooker::Request(const containers::ArraySpan<const AssetChange> changes, const BuildPriority priority) noexcept
    {
        return RequestInternal(changes, {}, {}, true, priority, {});
    }

    RecookBatch IncrementalRecooker::Request(const containers::ArraySpan<const AssetChange> changes,
                                             const containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                             const BuildPriority priority,
                                             const RecookPreCommitValidator& validator) noexcept
    {
        return RequestInternal(changes, explicitOutputs, {}, false, priority, validator);
    }

    RecookBatch IncrementalRecooker::Request(const containers::ArraySpan<const AssetChange> changes,
                                             const containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                             const containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                             const BuildPriority priority,
                                             const RecookPreCommitValidator& validator) noexcept
    {
        return RequestInternal(changes, explicitOutputs, forcedOutputs, false, priority, validator);
    }

    RecookBatch IncrementalRecooker::RequestInternal(const containers::ArraySpan<const AssetChange> changes,
                                                     const containers::ArraySpan<const resources::ResourceReference> explicitOutputs,
                                                     const containers::ArraySpan<const resources::ResourceReference> forcedOutputs,
                                                     const bool rejectUntrackedChanges, const BuildPriority priority,
                                                     const RecookPreCommitValidator& validator) noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        m_impl->lock.Acquire();
        if (m_impl->active != nullptr)
        {
            m_impl->lock.Release();
            return {};
        }
        auto* const operation = VANGUARD_NEW(RecookOperation);
        if (operation == nullptr)
        {
            m_impl->lock.Release();
            return {};
        }
        m_impl->active = operation;
        m_impl->lock.Release();

        operation->stats.inputChanges = changes.Count();
        operation->stats.explicitOutputs = explicitOutputs.Count();
        operation->validator = validator;
        const auto fail = [&](const RecookFailure failure) noexcept
        {
            SetTerminal(*operation, RecookState::Failed, failure);
            return RecookBatch(this, operation);
        };
        if ((changes.Empty() && explicitOutputs.Empty()) ||
            (!changes.Empty() && changes.Data() == nullptr) ||
            (!explicitOutputs.Empty() && explicitOutputs.Data() == nullptr) ||
            (!forcedOutputs.Empty() && forcedOutputs.Data() == nullptr))
        {
            return fail(RecookFailure::InvalidChange);
        }
        for (u32 index = 0; index < forcedOutputs.Count(); ++index)
        {
            const resources::ResourceReference output = forcedOutputs[index];
            if (!output.IsValid() || !output.IsTyped() || !Contains(explicitOutputs, output))
                return fail(RecookFailure::InvalidChange);
            for (u32 previous = 0; previous < index; ++previous)
                if (forcedOutputs[previous] == output)
                    return fail(RecookFailure::InvalidChange);
        }
        if (changes.Count() > m_impl->config.maximumChangesPerBatch)
        {
            return fail(RecookFailure::LimitExceeded);
        }

        containers::DynamicArray<resources::ResourceReference> evaluated(memory::pools::Assets::GetInstance());
        containers::DynamicArray<resources::ResourceReference> dirty(memory::pools::Assets::GetInstance());
        containers::DynamicArray<resources::ResourceReference> direct(memory::pools::Assets::GetInstance());
        containers::DynamicArray<resources::ResourceReference> descendants(memory::pools::Assets::GetInstance());

        const auto evaluateOutput = [&](const resources::ResourceReference output, const bool force,
                                        bool& isDirty, bool& firstCook) noexcept -> RecookFailure
        {
            isDirty = false;
            firstCook = false;
            if (!output.IsValid() || !output.IsTyped())
                return RecookFailure::InvalidChange;
            if (Contains(evaluated, output))
            {
                isDirty = Contains(dirty, output);
                if (force && !isDirty)
                {
                    if (!AddUnique(dirty, output, m_impl->config.maximumAffectedOutputsPerBatch))
                        return RecookFailure::LimitExceeded;
                    isDirty = true;
                }
                return RecookFailure::None;
            }
            if (!AddUnique(evaluated, output, m_impl->config.maximumAffectedOutputsPerBatch))
                return RecookFailure::LimitExceeded;

            OwnedBuildRequest owned;
            if (!ResolveOwnedRequest(*m_impl, output, owned))
                return RecookFailure::RequestResolutionFailed;
            const BuildRequest request = owned.View();
            BuildPlan plan;
            if (m_impl->buildSystem->Prepare(request, plan) != Result::Success)
                return RecookFailure::DependencyPreparationFailed;
            const IndexResult materialized = m_impl->index->MaterializeGeneratedDependencyContent(plan);
            if (materialized != IndexResult::Success && materialized != IndexResult::NotFound)
                return RecookFailure::DependencyPreparationFailed;

            const DirtyReason reason = m_impl->index->Evaluate(request, plan);
            if (reason == DirtyReason::UpToDate)
            {
                if (!force)
                    return RecookFailure::None;
                isDirty = true;
                return AddUnique(dirty, output, m_impl->config.maximumAffectedOutputsPerBatch)
                           ? RecookFailure::None
                           : RecookFailure::LimitExceeded;
            }

            isDirty = true;
            firstCook = reason == DirtyReason::Missing;
            if (!AddUnique(dirty, output, m_impl->config.maximumAffectedOutputsPerBatch))
                return RecookFailure::LimitExceeded;
            const IndexResult affectedResult = m_impl->index->CollectAffected(output, descendants);
            if (affectedResult != IndexResult::Success && affectedResult != IndexResult::NotFound)
                return affectedResult == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory : RecookFailure::IndexQueryFailed;
            for (const resources::ResourceReference descendant : descendants)
                if (!AddUnique(dirty, descendant, m_impl->config.maximumAffectedOutputsPerBatch))
                    return RecookFailure::LimitExceeded;
            return RecookFailure::None;
        };

        for (const AssetChange& change : changes)
        {
            if (!change.identity.IsValid() || !change.identity.IsTyped())
            {
                return fail(RecookFailure::InvalidChange);
            }
            const IndexResult directResult = m_impl->index->GetDirectDependants(change.identity, direct);
            if (directResult == IndexResult::NotFound)
            {
                if (rejectUntrackedChanges || explicitOutputs.Empty())
                    return fail(RecookFailure::UntrackedChange);
                ++operation->stats.unchangedChanges;
                continue;
            }
            if (directResult != IndexResult::Success)
            {
                return fail(directResult == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory : RecookFailure::IndexQueryFailed);
            }

            bool changeIsDirty = false;
            for (const resources::ResourceReference output : direct)
            {
                if (Contains(evaluated, output))
                {
                    changeIsDirty = changeIsDirty || Contains(dirty, output);
                    continue;
                }
                bool outputDirty = false;
                bool firstCook = false;
                const RecookFailure evaluatedResult = evaluateOutput(output, false, outputDirty, firstCook);
                if (evaluatedResult == RecookFailure::RequestResolutionFailed)
                {
                    // The committed producer may have been removed or become
                    // unsupported. It is still valid invalidation evidence for
                    // downstream committed outputs, but there is no current
                    // request to fabricate for the removed node itself.
                    DependencyRecord committed;
                    const IndexResult committedResult = m_impl->index->Find(output, committed);
                    if (committedResult != IndexResult::Success)
                        return fail(committedResult == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory
                                                                                : RecookFailure::RequestResolutionFailed);
                    const IndexResult affectedResult = m_impl->index->CollectAffected(output, descendants);
                    if (affectedResult != IndexResult::Success && affectedResult != IndexResult::NotFound)
                        return fail(affectedResult == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory
                                                                              : RecookFailure::IndexQueryFailed);
                    for (const resources::ResourceReference descendant : descendants)
                        if (!AddUnique(dirty, descendant, m_impl->config.maximumAffectedOutputsPerBatch))
                            return fail(RecookFailure::LimitExceeded);
                    changeIsDirty = changeIsDirty || !descendants.Empty();
                    continue;
                }
                if (evaluatedResult != RecookFailure::None)
                    return fail(evaluatedResult);
                changeIsDirty = changeIsDirty || outputDirty;
            }
            if (changeIsDirty)
            {
                ++operation->stats.dirtySeeds;
            }
            else
            {
                ++operation->stats.unchangedChanges;
            }
        }

        for (const resources::ResourceReference output : explicitOutputs)
        {
            bool outputDirty = false;
            bool firstCook = false;
            const RecookFailure evaluatedResult = evaluateOutput(output, Contains(forcedOutputs, output), outputDirty, firstCook);
            if (evaluatedResult != RecookFailure::None)
                return fail(evaluatedResult);
            if (outputDirty)
            {
                ++operation->stats.dirtySeeds;
                if (firstCook)
                    ++operation->stats.firstCookOutputs;
            }
            else
            {
                ++operation->stats.unchangedOutputs;
            }
        }

        operation->stats.affectedOutputs = dirty.Size();
        if (dirty.Empty())
        {
            if (operation->validator.validate != nullptr)
            {
                const RecookPreCommitValidationResult validated = operation->validator.validate({}, operation->validator.userData);
                if (validated != RecookPreCommitValidationResult::Accept)
                {
                    SetTerminal(*operation, RecookState::Failed,
                                validated == RecookPreCommitValidationResult::Reject
                                    ? RecookFailure::PreCommitRejected
                                    : RecookFailure::PreCommitValidationFailed);
                    return RecookBatch(this, operation);
                }
            }
            SetTerminal(*operation, RecookState::Succeeded, RecookFailure::None);
            return RecookBatch(this, operation);
        }

        containers::DynamicArray<resources::ResourceReference> roots(memory::pools::Assets::GetInstance());
        for (const resources::ResourceReference candidate : dirty)
        {
            // A forced output represents exact current-record repair. It must be
            // issued as a graph root even when the committed VADI topology says a
            // dirty dependant exists: that dependant's current build request may
            // have dropped the old Generated edge and therefore no longer reaches
            // this repair output.
            if (Contains(forcedOutputs, candidate))
            {
                if (!AddUnique(roots, candidate, m_impl->config.maximumRootRequestsPerBatch))
                    return fail(RecookFailure::LimitExceeded);
                continue;
            }
            const IndexResult dependantResult = m_impl->index->GetDirectDependants(candidate, direct);
            if (dependantResult != IndexResult::Success && dependantResult != IndexResult::NotFound)
            {
                return fail(dependantResult == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory : RecookFailure::IndexQueryFailed);
            }
            bool hasDirtyDependant = false;
            if (dependantResult == IndexResult::Success)
            {
                for (const resources::ResourceReference dependant : direct)
                {
                    hasDirtyDependant = hasDirtyDependant || Contains(dirty, dependant);
                }
            }
            if (!hasDirtyDependant && !AddUnique(roots, candidate, m_impl->config.maximumRootRequestsPerBatch))
            {
                return fail(RecookFailure::LimitExceeded);
            }
        }
        if (roots.Empty())
        {
            return fail(RecookFailure::IndexQueryFailed);
        }
        operation->stats.rootRequests = roots.Size();

        containers::DynamicArray<OwnedBuildRequest> rootRequests(memory::pools::Assets::GetInstance());
        rootRequests.Resize(roots.Size());
        if (rootRequests.Size() != roots.Size())
        {
            return fail(RecookFailure::OutOfMemory);
        }
        for (u32 index = 0; index < roots.Size(); ++index)
        {
            if (!ResolveOwnedRequest(*m_impl, roots[index], rootRequests[index]))
            {
                return fail(RecookFailure::RequestResolutionFailed);
            }
        }
        if (m_impl->index->BeginTransaction() != IndexResult::Success)
        {
            return fail(RecookFailure::TransactionFailed);
        }

        for (const OwnedBuildRequest& request : rootRequests)
        {
            GraphRequest graphRequest = m_impl->graph->Request(request.View(), priority);
            if (!graphRequest)
            {
                for (GraphRequest& issued : operation->requests)
                {
                    static_cast<void>(issued.Cancel());
                    issued.Wait();
                }
                static_cast<void>(m_impl->index->RollbackTransaction());
                return fail(RecookFailure::OutOfMemory);
            }
            const u32 previous = operation->requests.Size();
            operation->requests.PushBack(static_cast<GraphRequest&&>(graphRequest));
            if (operation->requests.Size() != previous + 1u)
            {
                static_cast<void>(graphRequest.Cancel());
                graphRequest.Wait();
                for (GraphRequest& issued : operation->requests)
                {
                    static_cast<void>(issued.Cancel());
                    issued.Wait();
                }
                static_cast<void>(m_impl->index->RollbackTransaction());
                return fail(RecookFailure::OutOfMemory);
            }
        }
        return RecookBatch(this, operation);
    }

    RecookStats IncrementalRecooker::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->stats;
    }

    bool IncrementalRecooker::Poll(RecookOperation& operation, const bool wait) noexcept
    {
        if (wait)
        {
            for (GraphRequest& request : operation.requests)
            {
                request.Wait();
            }
        }

        concurrency::ScopedLock guard(operation.lock);
        const RecookState current = static_cast<RecookState>(operation.state.GetValue());
        if (IsTerminal(current))
        {
            return true;
        }
        for (const GraphRequest& request : operation.requests)
        {
            if (!request.HasFinished())
            {
                return false;
            }
        }

        bool cancelled = operation.cancelRequested;
        bool failed = false;
        for (const GraphRequest& request : operation.requests)
        {
            if (request.GetStatus() == BuildState::Succeeded)
            {
                ++operation.stats.succeededRoots;
            }
            else
            {
                ++operation.stats.failedRoots;
                cancelled = cancelled || request.GetStatus() == BuildState::Cancelled;
                failed = failed || request.GetStatus() == BuildState::Failed;
            }
        }
        if (cancelled)
        {
            static_cast<void>(m_impl->index->RollbackTransaction());
            SetTerminal(operation, RecookState::Cancelled, RecookFailure::Cancelled);
        }
        else if (failed)
        {
            static_cast<void>(m_impl->index->RollbackTransaction());
            SetTerminal(operation, RecookState::Failed, RecookFailure::BuildFailed);
        }
        else if (operation.validator.validate != nullptr)
        {
            containers::DynamicArray<DependencyRecord> evidence(memory::pools::Assets::GetInstance());
            const IndexResult captured = m_impl->index->CaptureTransactionRecords(
                evidence, m_impl->config.maximumValidationRecordsPerBatch, m_impl->config.maximumValidationEvidenceBytes);
            if (captured != IndexResult::Success)
            {
                static_cast<void>(m_impl->index->RollbackTransaction());
                SetTerminal(operation, RecookState::Failed,
                            captured == IndexResult::OutOfMemory ? RecookFailure::OutOfMemory
                            : captured == IndexResult::LimitExceeded ? RecookFailure::LimitExceeded
                                                                    : RecookFailure::PreCommitValidationFailed);
            }
            else
            {
                const RecookPreCommitValidationResult validated = operation.validator.validate(
                    {evidence.TypedData(), evidence.Size()}, operation.validator.userData);
                if (validated != RecookPreCommitValidationResult::Accept)
                {
                    static_cast<void>(m_impl->index->RollbackTransaction());
                    SetTerminal(operation, RecookState::Failed,
                                validated == RecookPreCommitValidationResult::Reject
                                    ? RecookFailure::PreCommitRejected
                                    : RecookFailure::PreCommitValidationFailed);
                }
                else if (m_impl->index->CommitTransaction() != IndexResult::Success)
                {
                    static_cast<void>(m_impl->index->RollbackTransaction());
                    SetTerminal(operation, RecookState::Failed, RecookFailure::TransactionFailed);
                }
                else
                {
                    // The same deep-copied snapshot that was validated is now
                    // authoritative committed-delivery evidence. Retain it on
                    // the batch by O(1) swap so integration owners can move it
                    // before releasing/resetting the completed operation.
                    operation.committedRecords.Swap(evidence);
                    SetTerminal(operation, RecookState::Succeeded, RecookFailure::None);
                }
            }
        }
        else if (m_impl->index->CommitTransaction() != IndexResult::Success)
        {
            static_cast<void>(m_impl->index->RollbackTransaction());
            SetTerminal(operation, RecookState::Failed, RecookFailure::TransactionFailed);
        }
        else
        {
            SetTerminal(operation, RecookState::Succeeded, RecookFailure::None);
        }
        return true;
    }

    bool IncrementalRecooker::Cancel(RecookOperation& operation) noexcept
    {
        concurrency::ScopedLock guard(operation.lock);
        if (IsTerminal(static_cast<RecookState>(operation.state.GetValue())))
        {
            return false;
        }
        operation.cancelRequested = true;
        for (GraphRequest& request : operation.requests)
        {
            static_cast<void>(request.Cancel());
        }
        return true;
    }

    void IncrementalRecooker::Release(RecookOperation& operation) noexcept
    {
        if (!IsTerminal(static_cast<RecookState>(operation.state.GetValue())))
        {
            static_cast<void>(Cancel(operation));
            static_cast<void>(Poll(operation, true));
        }
        m_impl->lock.Acquire();
        if (m_impl->active == &operation)
        {
            m_impl->stats.inputChanges += operation.stats.inputChanges;
            m_impl->stats.unchangedChanges += operation.stats.unchangedChanges;
            m_impl->stats.explicitOutputs += operation.stats.explicitOutputs;
            m_impl->stats.unchangedOutputs += operation.stats.unchangedOutputs;
            m_impl->stats.firstCookOutputs += operation.stats.firstCookOutputs;
            m_impl->stats.dirtySeeds += operation.stats.dirtySeeds;
            m_impl->stats.affectedOutputs += operation.stats.affectedOutputs;
            m_impl->stats.rootRequests += operation.stats.rootRequests;
            m_impl->stats.succeededRoots += operation.stats.succeededRoots;
            m_impl->stats.failedRoots += operation.stats.failedRoots;
            m_impl->stats.completedBatches += operation.stats.completedBatches;
            m_impl->stats.failedBatches += operation.stats.failedBatches;
            m_impl->stats.cancelledBatches += operation.stats.cancelledBatches;
            m_impl->active = nullptr;
        }
        m_impl->lock.Release();
        VANGUARD_DELETE(&operation);
    }
} // namespace vanguard::assets
