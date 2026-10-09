#include <vanguard/resources/resource_pipeline.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/system/assert.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    using namespace vanguard::resources;

    template <typename T, typename... Args> [[nodiscard]] T* AllocatePipelineObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Resources, sizeof(T), alignof(T));
        if (!block)
        {
            return nullptr;
        }
        return ::new (block.address) T(static_cast<Args&&>(args)...);
    }

    template <typename T> void DeletePipelineObject(T* const object) noexcept
    {
        if (object == nullptr)
        {
            return;
        }
        object->~T();
        memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Resources};
        memory::Free(block);
    }

    [[nodiscard]] constexpr bool IsHigherPriority(const LoadPriority candidate, const LoadPriority current) noexcept
    {
        return static_cast<u8>(candidate) > static_cast<u8>(current);
    }

    [[nodiscard]] constexpr jobs::Priority ToJobsPriority(const LoadPriority priority) noexcept
    {
        switch (priority)
        {
        case LoadPriority::Background:
        case LoadPriority::Low:
            return jobs::Priority::Latent;
        case LoadPriority::Normal:
        case LoadPriority::High:
            return jobs::Priority::CriticalPath;
        case LoadPriority::Critical:
            return jobs::Priority::Immediate;
        }
        return jobs::Priority::CriticalPath;
    }
} // namespace

namespace vanguard::resources
{
    namespace
    {
        struct DependencySpec
        {
            ResourceReference reference;
            DependencyRequirement requirement = DependencyRequirement::Required;
        };

        struct DependencyEdge
        {
            PipelineOperation* operation = nullptr;
            ResourceReference reference;
            DependencyRequirement requirement = DependencyRequirement::Required;
            bool ownsInterest = false;
        };

        struct DependencyBuildState
        {
            DependencyBuildState(const u32 maximumDependencyCount, PipelineOperation* const pipelineOperation) noexcept
                : dependencies(memory::pools::Resources::GetInstance()), operation(pipelineOperation), maximumDependencies(maximumDependencyCount)
            {
            }

            containers::DynamicArray<DependencySpec> dependencies;
            PipelineOperation* operation = nullptr;
            u32 maximumDependencies = 0;
            Failure failure = Failure::None;
        };

        struct LoadContextState
        {
            PipelineOperation* operation = nullptr;
            const containers::DynamicArray<ResourceHandle>* handles = nullptr;
        };

        struct OperationActions
        {
            OperationActions() noexcept
                : cancel(memory::pools::Resources::GetInstance()), cancelPreparation(memory::pools::Resources::GetInstance()),
                  finish(memory::pools::Resources::GetInstance()), evict(memory::pools::Resources::GetInstance()),
                  retire(memory::pools::Resources::GetInstance())
            {
            }

            containers::DynamicArray<PipelineOperation*> cancel;
            containers::DynamicArray<PipelineOperation*> cancelPreparation;
            containers::DynamicArray<PipelineOperation*> finish;
            containers::DynamicArray<PipelineOperation*> evict;
            containers::DynamicArray<PipelineOperation*> retire;
        };
    } // namespace

    struct PipelineOperation
    {
        PipelineOperation(const ResourceReference resourceReference, const AsyncLoaderDescriptor& loaderDescriptor, const LoadPriority loadPriority) noexcept
            : reference(resourceReference), loader(loaderDescriptor), edges(memory::pools::Resources::GetInstance()), priority(static_cast<u32>(loadPriority))
        {
        }

        ResourceReference reference;
        AsyncLoaderDescriptor loader;
        ResourceRequest registryRequest;
        concurrency::ManualResetEvent registryReady{false};
        concurrency::ManualResetEvent gateCanComplete{false};
        concurrency::ManualResetEvent preparationGateCanComplete{false};
        concurrency::Mutex preparationCallbackLock;
        jobs::Counter completionCounter;
        jobs::CompletionDeferral completionDeferral;
        jobs::Counter preparationCounter;
        jobs::CompletionDeferral preparationDeferral;
        containers::DynamicArray<DependencyEdge> edges;
        concurrency::Atomic<u32> priority;
        concurrency::Atomic<u32> publishedFailure{static_cast<u32>(Failure::None)};
        concurrency::Atomic<bool> cancellationRequested{false};
        concurrency::Atomic<u32> preparationFailure{static_cast<u32>(Failure::None)};
        concurrency::Atomic<bool> preparationCompletedAtomic{false};
        // A terminal operation may publish its completion before the worker that
        // produced that terminal state has returned. Runtime reclamation must not
        // recycle the operation storage while a scheduled job still holds the
        // captured PipelineOperation reference.
        concurrency::Atomic<u32> activeJobs{0};
        u32 rootInterests = 0;
        u32 dependencyInterests = 0;
        bool planScheduled = false;
        bool preparationStarted = false;
        bool preparationCompleted = false;
        bool preparationCancellationNotified = false;
        bool constructionInvoked = false;
        bool terminal = false;
        bool completionFinished = false;
        bool dependenciesReleased = false;
        bool evictionIssued = false;
        bool replacementCandidate = false;
        bool replacementCutoverInProgress = false;
        bool replacementCleanupInProgress = false;
        bool retirementQueued = false;
        bool retirementActionsComplete = false;
        PipelineOperation* replacementPrevious = nullptr;
        Failure failure = Failure::None;
        PipelineOperation* failureCause = nullptr;
        void* loaderState = nullptr;
        u64 traversalMark = 0;
    };

    struct ResourcePipeline::Impl
    {
        Impl(ResourcePipeline& owningPipeline, ResourceRegistry& resourceRegistry, const PipelineConfig& pipelineConfig) noexcept
            : owner(&owningPipeline), registry(&resourceRegistry), config(pipelineConfig), loaders(memory::pools::Resources::GetInstance()),
              loaderTypes(memory::pools::Resources::GetInstance()), currentOperations(memory::pools::Resources::GetInstance()),
              replacementOperations(memory::pools::Resources::GetInstance()),
              allOperations(memory::pools::Resources::GetInstance()), traversalStack(memory::pools::Resources::GetInstance()),
              gateJobName("Vanguard.Resources.CompletionGate"), planJobName("Vanguard.Resources.DiscoverDependencies"),
              constructJobName("Vanguard.Resources.Construct")
        {
        }

        ResourcePipeline* owner = nullptr;
        ResourceRegistry* registry = nullptr;
        PipelineConfig config;
        concurrency::RWLock lock;
        containers::HashMap<ResourceTypeId, AsyncLoaderDescriptor> loaders;
        containers::DynamicArray<ResourceTypeId> loaderTypes;
        containers::HashMap<ResourceId, PipelineOperation*> currentOperations;
        containers::HashMap<ResourceId, PipelineOperation*> replacementOperations;
        containers::DynamicArray<PipelineOperation*> allOperations;
        containers::DynamicArray<PipelineOperation*> traversalStack;
        jobs::JobName gateJobName;
        jobs::JobName planJobName;
        jobs::JobName constructJobName;
        concurrency::Atomic<u32> activeJobs{0};
        concurrency::Atomic<u32> activePreparations{0};
        concurrency::Atomic<u32> externalRequests{0};
        u64 nextTraversalMark = 1;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
        u64 priorityPromotions = 0;
        u64 dependencyEdges = 0;
        u64 cancelledOperations = 0;
        u64 completedOperations = 0;
        u64 failedOperations = 0;

        [[nodiscard]] bool CreateCompletionGate(PipelineOperation& operation) noexcept
        {
            jobs::Builder builder{jobs::Schedule{ToJobsPriority(static_cast<LoadPriority>(operation.priority.GetValue())), jobs::Affinity::AnyWorker},
                                  &operation};
            jobs::Task task = jobs::Task::Create([&operation](const jobs::JobContext&) noexcept { operation.gateCanComplete.Wait(); });
            if (!task || !builder.Dispatch(gateJobName, static_cast<jobs::Task&&>(task), jobs::Fence::Full))
            {
                operation.gateCanComplete.Signal();
                return false;
            }

            operation.completionCounter = builder.ExtractCounter();
            operation.completionDeferral = operation.completionCounter.CreateDeferral("Vanguard.Resources.Operation", &operation);
            operation.gateCanComplete.Signal();
            return operation.completionCounter.IsValid() && operation.completionDeferral.IsValid();
        }

        [[nodiscard]] bool CreatePreparationGate(PipelineOperation& operation) noexcept
        {
            jobs::Builder builder{jobs::Schedule{ToJobsPriority(static_cast<LoadPriority>(operation.priority.GetValue())), jobs::Affinity::AnyWorker},
                                  &operation};
            jobs::Task task = jobs::Task::Create([&operation](const jobs::JobContext&) noexcept { operation.preparationGateCanComplete.Wait(); });
            if (!task || !builder.Dispatch(gateJobName, static_cast<jobs::Task&&>(task), jobs::Fence::Full))
            {
                operation.preparationGateCanComplete.Signal();
                return false;
            }

            operation.preparationCounter = builder.ExtractCounter();
            operation.preparationDeferral = operation.preparationCounter.CreateDeferral("Vanguard.Resources.Preparation", &operation);
            operation.preparationGateCanComplete.Signal();
            return operation.preparationCounter.IsValid() && operation.preparationDeferral.IsValid();
        }

        [[nodiscard]] PipelineOperation* CreateOperationLocked(const ResourceReference reference, const LoadPriority priority,
                                                               const bool replacement = false) noexcept
        {
            if (allOperations.Size() >= config.maximumRetainedOperations)
                return nullptr;
            AsyncLoaderDescriptor loader;
            static_cast<void>(loaders.Find(reference.ExpectedType(), loader));
            PipelineOperation* const operation = AllocatePipelineObject<PipelineOperation>(reference, loader, priority);
            if (operation == nullptr)
            {
                return nullptr;
            }
            if (!CreateCompletionGate(*operation))
            {
                if (operation->completionDeferral.IsValid())
                {
                    operation->completionDeferral.Finish();
                }
                operation->gateCanComplete.Signal();
                if (operation->completionCounter.IsValid())
                {
                    static_cast<void>(operation->completionCounter.Wait());
                }
                DeletePipelineObject(operation);
                return nullptr;
            }
            operation->replacementCandidate = replacement;

            allOperations.PushBack(operation);
            auto& operations = replacement ? replacementOperations : currentOperations;
            if (!operations.Insert(reference.GetPath().Id(), operation).IsSuccessful())
            {
                operation->completionDeferral.Finish();
                static_cast<void>(operation->completionCounter.Wait());
                allOperations.PopBack();
                DeletePipelineObject(operation);
                return nullptr;
            }
            return operation;
        }

        [[nodiscard]] bool IsCurrentLocked(const PipelineOperation& operation) const noexcept
        {
            PipelineOperation* current = nullptr;
            return currentOperations.Find(operation.reference.GetPath().Id(), current) && current == &operation;
        }

        [[nodiscard]] bool IsReplacementLocked(const PipelineOperation& operation) const noexcept
        {
            PipelineOperation* current = nullptr;
            return replacementOperations.Find(operation.reference.GetPath().Id(), current) && current == &operation;
        }

        void RemoveCurrentLocked(PipelineOperation& operation) noexcept
        {
            if (IsCurrentLocked(operation))
            {
                static_cast<void>(currentOperations.Remove(operation.reference.GetPath().Id()));
            }
        }

        void RemoveOperationLookupLocked(PipelineOperation& operation) noexcept
        {
            if (operation.replacementCandidate)
            {
                if (IsReplacementLocked(operation))
                    static_cast<void>(replacementOperations.Remove(operation.reference.GetPath().Id()));
            }
            else
                RemoveCurrentLocked(operation);
        }

        void MarkForFinishLocked(PipelineOperation& operation, OperationActions& actions) noexcept
        {
            if (!operation.completionFinished)
            {
                operation.completionFinished = true;
                actions.finish.PushBack(&operation);
            }
        }

        void MarkPreparationCancellationLocked(PipelineOperation& operation, OperationActions& actions) noexcept
        {
            if (operation.planScheduled && !operation.constructionInvoked && !operation.preparationCancellationNotified &&
                operation.loader.cancelPreparation != nullptr)
            {
                operation.preparationCancellationNotified = true;
                actions.cancelPreparation.PushBack(&operation);
            }
        }

        void ProcessNoInterestLocked(PipelineOperation& operation, OperationActions& actions) noexcept
        {
            if (operation.rootInterests != 0 || operation.dependencyInterests != 0)
            {
                return;
            }

            // CompleteSuccess temporarily owns a replacement candidate across the
            // registry path swap and the matching pipeline lookup promotion. A
            // caller may drop its last request while that short cutover is live;
            // defer removal/eviction until both owners agree on the new current op.
            if (operation.replacementCandidate && operation.replacementCutoverInProgress)
                return;

            if (!operation.terminal && operation.replacementCandidate)
                operation.replacementCleanupInProgress = true;
            if (!(operation.replacementCandidate && operation.replacementCleanupInProgress))
                RemoveOperationLookupLocked(operation);
            if (!operation.terminal)
            {
                operation.terminal = true;
                operation.failure = Failure::Cancelled;
                operation.publishedFailure.SetValue(static_cast<u32>(Failure::Cancelled));
                operation.cancellationRequested.SetValue(true);
                ++cancelledOperations;
                actions.cancel.PushBack(&operation);
                MarkPreparationCancellationLocked(operation, actions);
                MarkForFinishLocked(operation, actions);
                ReleaseDependenciesLocked(operation, actions);
            }
            else if (operation.failure == Failure::None && !operation.evictionIssued)
            {
                operation.evictionIssued = true;
                actions.evict.PushBack(&operation);
            }
            if (!operation.retirementQueued)
            {
                operation.retirementQueued = true;
                actions.retire.PushBack(&operation);
            }
        }

        [[nodiscard]] bool HasIncomingOperationReferenceLocked(const PipelineOperation& candidate) const noexcept
        {
            for (const PipelineOperation* const other : allOperations)
            {
                if (other == &candidate)
                    continue;
                if (other->failureCause == &candidate)
                    return true;
                for (const DependencyEdge& edge : other->edges)
                    if (edge.operation == &candidate)
                        return true;
            }
            return false;
        }

        [[nodiscard]] bool CanReclaimOperationLocked(const PipelineOperation& operation) const noexcept
        {
            return operation.terminal && operation.rootInterests == 0 && operation.dependencyInterests == 0 &&
                   operation.retirementActionsComplete && !IsCurrentLocked(operation) && !IsReplacementLocked(operation) &&
                   !operation.replacementCutoverInProgress && !operation.replacementCleanupInProgress &&
                   operation.replacementPrevious == nullptr && operation.loaderState == nullptr && operation.completionFinished &&
                   operation.completionCounter.IsReady() && operation.activeJobs.GetValue() == 0 &&
                   (!operation.preparationCounter.IsValid() || operation.preparationCounter.IsReady()) &&
                   !HasIncomingOperationReferenceLocked(operation);
        }

        void CollectRetiredOperations() noexcept
        {
            for (;;)
            {
                PipelineOperation* retired = nullptr;
                lock.Acquire();
                for (u32 index = 0; index < allOperations.Size(); ++index)
                {
                    PipelineOperation* const operation = allOperations[index];
                    if (!CanReclaimOperationLocked(*operation))
                        continue;
                    retired = operation;
                    static_cast<void>(allOperations.RemoveAtReorder(index));
                    break;
                }
                lock.Release();
                if (retired == nullptr)
                    return;
                retired->registryRequest.Reset();
                DeletePipelineObject(retired);
            }
        }

        void ReleaseReplacementAnchorLocked(PipelineOperation& operation, OperationActions& actions) noexcept
        {
            PipelineOperation* const previous = operation.replacementPrevious;
            if (previous == nullptr)
                return;

            operation.replacementPrevious = nullptr;
            VG_ASSERT_MSG(previous->dependencyInterests != 0, "replacement previous-current anchor must own an interest");
            if (previous->dependencyInterests != 0)
            {
                --previous->dependencyInterests;
                ProcessNoInterestLocked(*previous, actions);
            }
        }

        [[nodiscard]] PipelineOperation* ResolveCurrentOperationLocked(const ResourceReference reference) noexcept
        {
            PipelineOperation* current = nullptr;
            static_cast<void>(currentOperations.Find(reference.GetPath().Id(), current));

            PipelineOperation* replacement = nullptr;
            static_cast<void>(replacementOperations.Find(reference.GetPath().Id(), replacement));

            if (current != nullptr && current->terminal && current->failure != Failure::None)
            {
                RemoveCurrentLocked(*current);
                current = nullptr;
            }

            // Registry publication precedes pipeline lookup promotion. During that
            // short interval all pipeline consumers must resolve to the exact
            // candidate that became registry-current, never to the retired wrapper
            // and never to a fresh duplicate operation for the same generation.
            if (replacement != nullptr && replacement->reference.ExpectedType() == reference.ExpectedType())
            {
                if (current == nullptr)
                {
                    if (replacement->registryReady.TryWait(0) && registry->IsCurrent(replacement->registryRequest))
                        return replacement;
                }
                else if (current->reference.ExpectedType() == reference.ExpectedType() && current->terminal && current->failure == Failure::None &&
                         current->registryReady.TryWait(0) && !registry->IsCurrent(current->registryRequest))
                {
                    return replacement;
                }
            }
            return current;
        }

        void ReleaseDependenciesLocked(PipelineOperation& operation, OperationActions& actions) noexcept
        {
            if (operation.dependenciesReleased)
            {
                return;
            }
            operation.dependenciesReleased = true;
            for (DependencyEdge& edge : operation.edges)
            {
                if (!edge.ownsInterest || edge.operation == nullptr)
                {
                    continue;
                }
                edge.ownsInterest = false;
                if (edge.operation->dependencyInterests != 0)
                {
                    --edge.operation->dependencyInterests;
                    ProcessNoInterestLocked(*edge.operation, actions);
                }
            }
        }

        void ExecuteActions(OperationActions& actions) noexcept
        {
            for (PipelineOperation* const operation : actions.cancelPreparation)
            {
                operation->preparationCallbackLock.Acquire();
                const PreparationRequest request{owner, operation};
                operation->loader.cancelPreparation(request, operation->loader.userData);
                operation->preparationCallbackLock.Release();
            }
            for (PipelineOperation* const operation : actions.cancel)
            {
                if (operation->registryReady.TryWait(0))
                {
                    static_cast<void>(registry->Cancel(operation->registryRequest));
                }
                if (operation->replacementCleanupInProgress)
                {
                    lock.Acquire();
                    operation->replacementCleanupInProgress = false;
                    RemoveOperationLookupLocked(*operation);
                    ReleaseReplacementAnchorLocked(*operation, actions);
                    lock.Release();
                }
            }
            for (PipelineOperation* const operation : actions.finish)
            {
                if (operation->completionDeferral.IsValid())
                {
                    operation->completionDeferral.Finish();
                }
            }
            for (PipelineOperation* const operation : actions.evict)
            {
                static_cast<void>(registry->Evict(operation->registryRequest));
            }
            if (!actions.retire.Empty())
            {
                lock.Acquire();
                for (PipelineOperation* const operation : actions.retire)
                    operation->retirementActionsComplete = true;
                lock.Release();
            }
        }

        void PromoteLocked(PipelineOperation& root, const LoadPriority priority) noexcept
        {
            ++nextTraversalMark;
            if (nextTraversalMark == 0)
            {
                nextTraversalMark = 1;
                for (PipelineOperation* const operation : allOperations)
                {
                    operation->traversalMark = 0;
                }
            }

            traversalStack.Clear();
            traversalStack.PushBack(&root);
            while (!traversalStack.Empty())
            {
                PipelineOperation* const operation = traversalStack.Back();
                traversalStack.PopBack();
                if (operation->traversalMark == nextTraversalMark)
                {
                    continue;
                }
                operation->traversalMark = nextTraversalMark;

                const LoadPriority current = static_cast<LoadPriority>(operation->priority.GetValue());
                if (IsHigherPriority(priority, current))
                {
                    operation->priority.SetValue(static_cast<u32>(priority));
                    ++priorityPromotions;
                }
                for (const DependencyEdge& edge : operation->edges)
                {
                    if (edge.operation != nullptr)
                    {
                        traversalStack.PushBack(edge.operation);
                    }
                }
            }
        }

        [[nodiscard]] bool WouldCreateCycleLocked(PipelineOperation& parent, PipelineOperation& child) noexcept
        {
            if (&parent == &child)
            {
                return true;
            }

            ++nextTraversalMark;
            if (nextTraversalMark == 0)
            {
                nextTraversalMark = 1;
                for (PipelineOperation* const operation : allOperations)
                {
                    operation->traversalMark = 0;
                }
            }
            traversalStack.Clear();
            traversalStack.PushBack(&child);
            while (!traversalStack.Empty())
            {
                PipelineOperation* const operation = traversalStack.Back();
                traversalStack.PopBack();
                if (operation == &parent)
                {
                    return true;
                }
                if (operation->traversalMark == nextTraversalMark)
                {
                    continue;
                }
                operation->traversalMark = nextTraversalMark;
                for (const DependencyEdge& edge : operation->edges)
                {
                    if (edge.operation != nullptr)
                    {
                        traversalStack.PushBack(edge.operation);
                    }
                }
            }
            return false;
        }

        void StartRegistryRequest(PipelineOperation& operation) noexcept
        {
            ResourceRequest request = operation.replacementCandidate ? registry->RequestReplacement(operation.reference)
                                                                     : registry->Request(operation.reference);
            if (operation.registryReady.TryWait(0))
            {
                return;
            }

            OperationActions actions;
            lock.Acquire();
            if (!operation.registryReady.TryWait(0))
            {
                operation.registryRequest = static_cast<ResourceRequest&&>(request);
                operation.registryReady.Signal();
                if (!operation.terminal)
                {
                    if (operation.registryRequest.HasLoaded())
                    {
                        operation.terminal = true;
                        operation.failure = Failure::None;
                        operation.publishedFailure.SetValue(static_cast<u32>(Failure::None));
                        ++completedOperations;
                        MarkForFinishLocked(operation, actions);
                    }
                    else if (operation.registryRequest.HasFailed())
                    {
                        operation.terminal = true;
                        operation.failure = operation.registryRequest.GetError();
                        operation.publishedFailure.SetValue(static_cast<u32>(operation.failure));
                        ++failedOperations;
                        if (operation.replacementCandidate)
                        {
                            RemoveOperationLookupLocked(operation);
                            ReleaseReplacementAnchorLocked(operation, actions);
                        }
                        MarkForFinishLocked(operation, actions);
                    }
                    else
                    {
                        // A newly created pipeline operation must either receive
                        // the registry begin-load callback or adopt an already
                        // terminal registry request. A live request without that
                        // callback would have no pipeline plan owner.
                        operation.terminal = true;
                        operation.failure = Failure::InternalError;
                        operation.publishedFailure.SetValue(static_cast<u32>(Failure::InternalError));
                        ++failedOperations;
                        if (operation.replacementCandidate)
                        {
                            RemoveOperationLookupLocked(operation);
                            ReleaseReplacementAnchorLocked(operation, actions);
                        }
                        MarkForFinishLocked(operation, actions);
                    }
                }
            }
            lock.Release();
            ExecuteActions(actions);
        }

        void CompleteFailure(PipelineOperation& operation, const Failure failure, PipelineOperation* const cause = nullptr) noexcept
        {
            OperationActions actions;
            lock.Acquire();
            if (operation.terminal)
            {
                lock.Release();
                return;
            }
            operation.terminal = true;
            operation.failure = failure == Failure::None ? Failure::InternalError : failure;
            operation.publishedFailure.SetValue(static_cast<u32>(operation.failure));
            operation.cancellationRequested.SetValue(true);
            operation.failureCause = cause;
            ++failedOperations;
            if (operation.replacementCandidate)
                operation.replacementCleanupInProgress = true;
            MarkPreparationCancellationLocked(operation, actions);
            ReleaseDependenciesLocked(operation, actions);
            MarkForFinishLocked(operation, actions);
            ProcessNoInterestLocked(operation, actions);
            lock.Release();

            if (operation.registryReady.TryWait(0))
            {
                if (operation.failure == Failure::Cancelled)
                {
                    static_cast<void>(registry->Cancel(operation.registryRequest));
                }
                else
                {
                    static_cast<void>(registry->Fail(operation.registryRequest, operation.failure));
                }
            }
            if (operation.replacementCleanupInProgress)
            {
                lock.Acquire();
                operation.replacementCleanupInProgress = false;
                RemoveOperationLookupLocked(operation);
                ReleaseReplacementAnchorLocked(operation, actions);
                lock.Release();
            }
            ExecuteActions(actions);
        }

        void CompleteSuccess(PipelineOperation& operation, ResourceObject* const resource) noexcept
        {
            OperationActions actions;
            bool accepted = false;
            lock.Acquire();
            if (!operation.terminal)
            {
                operation.terminal = true;
                operation.failure = Failure::None;
                operation.publishedFailure.SetValue(static_cast<u32>(Failure::None));
                ++completedOperations;
                operation.replacementCutoverInProgress = operation.replacementCandidate;
                ReleaseDependenciesLocked(operation, actions);
                MarkForFinishLocked(operation, actions);
                accepted = true;
            }
            lock.Release();

            if (accepted && operation.registryReady.TryWait(0) && registry->Publish(operation.registryRequest, resource))
            {
                if (operation.replacementCandidate)
                {
                    lock.Acquire();
                    PipelineOperation* replacement = nullptr;
                    if (replacementOperations.Find(operation.reference.GetPath().Id(), replacement) && replacement == &operation)
                    {
                        PipelineOperation* previous = nullptr;
                        const bool hasCurrent = currentOperations.Find(operation.reference.GetPath().Id(), previous);
                        const bool promoted = (hasCurrent ? currentOperations.Set(operation.reference.GetPath().Id(), &operation)
                                                          : currentOperations.Insert(operation.reference.GetPath().Id(), &operation))
                                                  .IsSuccessful();
                        VG_ASSERT_MSG(promoted, "existing resource operation lookup must accept replacement cutover");
                        static_cast<void>(replacementOperations.Remove(operation.reference.GetPath().Id()));
                        operation.replacementCandidate = false;
                        operation.replacementCutoverInProgress = false;
                        ReleaseReplacementAnchorLocked(operation, actions);
                        ProcessNoInterestLocked(operation, actions);
                    }
                    lock.Release();
                }
                ExecuteActions(actions);
                return;
            }

            operation.loader.destroyResource(resource, operation.loader.userData);
            if (accepted)
            {
                static_cast<void>(registry->Fail(operation.registryRequest, Failure::InternalError));
                lock.Acquire();
                operation.failure = Failure::InternalError;
                operation.publishedFailure.SetValue(static_cast<u32>(Failure::InternalError));
                operation.replacementCutoverInProgress = false;
                if (operation.replacementCandidate)
                {
                    RemoveOperationLookupLocked(operation);
                    ReleaseReplacementAnchorLocked(operation, actions);
                }
                ProcessNoInterestLocked(operation, actions);
                if (completedOperations != 0)
                {
                    --completedOperations;
                }
                ++failedOperations;
                lock.Release();
            }
            ExecuteActions(actions);
        }

        void SchedulePlan(PipelineOperation& operation) noexcept
        {
            static_cast<void>(activeJobs.Increment());
            static_cast<void>(operation.activeJobs.Increment());
            jobs::Task task = jobs::Task::Create(
                [this, &operation](const jobs::JobContext&) noexcept
                {
                    RunPlan(operation);
                    static_cast<void>(operation.activeJobs.Decrement());
                    static_cast<void>(activeJobs.Decrement());
                });
            jobs::Builder builder{jobs::Schedule{ToJobsPriority(static_cast<LoadPriority>(operation.priority.GetValue())), jobs::Affinity::AnyWorker},
                                  &operation};
            if (!task || !builder.Dispatch(planJobName, static_cast<jobs::Task&&>(task), jobs::Fence::Full))
            {
                static_cast<void>(operation.activeJobs.Decrement());
                static_cast<void>(activeJobs.Decrement());
                CompleteFailure(operation, Failure::OutOfMemory);
            }
        }

        [[nodiscard]] bool AttachDependency(PipelineOperation& parent, const DependencySpec& dependency) noexcept
        {
            PipelineOperation* child = nullptr;
            bool created = false;

            lock.Acquire();
            if (parent.terminal)
            {
                lock.Release();
                return false;
            }

            child = ResolveCurrentOperationLocked(dependency.reference);
            if (child != nullptr && child->reference.ExpectedType() != dependency.reference.ExpectedType())
            {
                lock.Release();
                CompleteFailure(parent, Failure::DependencyFailure);
                return false;
            }

            if (child == nullptr)
            {
                child = CreateOperationLocked(dependency.reference, static_cast<LoadPriority>(parent.priority.GetValue()));
                created = child != nullptr;
            }
            if (child == nullptr)
            {
                lock.Release();
                CompleteFailure(parent, Failure::OutOfMemory);
                return false;
            }
            if (WouldCreateCycleLocked(parent, *child))
            {
                if (created)
                {
                    RemoveCurrentLocked(*child);
                }
                lock.Release();
                CompleteFailure(parent, Failure::DependencyCycle);
                return false;
            }

            ++child->dependencyInterests;
            parent.edges.PushBack(DependencyEdge{child, dependency.reference, dependency.requirement, true});
            ++dependencyEdges;
            lock.Release();

            if (created)
            {
                StartRegistryRequest(*child);
            }
            else
            {
                child->registryReady.Wait();
            }
            return true;
        }

        void RunPlan(PipelineOperation& operation) noexcept
        {
            lock.AcquireShared();
            const bool terminal = operation.terminal;
            const AsyncLoaderDescriptor loader = operation.loader;
            lock.ReleaseShared();
            if (terminal)
            {
                return;
            }
            if (!loader.IsValid())
            {
                CompleteFailure(operation, Failure::UnknownType);
                return;
            }

            DependencyBuildState buildState{config.maximumDependenciesPerResource, &operation};
            DependencyBuilder builder{&buildState};
            Failure failure = loader.discoverDependencies(operation.reference, builder, loader.userData);
            if (failure == Failure::None)
            {
                failure = buildState.failure;
            }
            if (failure != Failure::None)
            {
                CompleteFailure(operation, failure);
                return;
            }

            lock.AcquireShared();
            const bool cancelledAfterDiscovery = operation.terminal;
            lock.ReleaseShared();
            if (cancelledAfterDiscovery)
            {
                if (loader.cancelPreparation != nullptr)
                {
                    const PreparationRequest request{owner, &operation};
                    operation.preparationCallbackLock.Acquire();
                    loader.cancelPreparation(request, loader.userData);
                    operation.preparationCallbackLock.Release();
                }
                return;
            }

            for (const DependencySpec& dependency : buildState.dependencies)
            {
                if (!AttachDependency(operation, dependency))
                {
                    return;
                }
            }

            if (loader.beginPreparation != nullptr)
            {
                if (!CreatePreparationGate(operation))
                {
                    CompleteFailure(operation, Failure::OutOfMemory);
                    return;
                }

                lock.Acquire();
                if (operation.terminal)
                {
                    lock.Release();
                    if (operation.preparationDeferral.IsValid())
                    {
                        operation.preparationDeferral.Finish();
                    }
                    return;
                }
                operation.preparationStarted = true;
                static_cast<void>(activePreparations.Increment());
                lock.Release();

                const PreparationRequest preparation{owner, &operation};
                operation.preparationCallbackLock.Acquire();
                const bool started = loader.beginPreparation(preparation, loader.userData);
                operation.preparationCallbackLock.Release();
                if (!started)
                {
                    static_cast<void>(owner->CompletePreparation(&operation, Failure::InternalError));
                }
            }
            ScheduleConstruct(operation);
        }

        void ScheduleConstruct(PipelineOperation& operation) noexcept
        {
            lock.AcquireShared();
            const bool terminal = operation.terminal;
            lock.ReleaseShared();
            if (terminal)
            {
                return;
            }

            jobs::Builder builder{jobs::Schedule{ToJobsPriority(static_cast<LoadPriority>(operation.priority.GetValue())), jobs::Affinity::AnyWorker},
                                  &operation};
            for (const DependencyEdge& edge : operation.edges)
            {
                builder.AddDependency(edge.operation->completionCounter);
            }
            if (operation.preparationStarted)
            {
                builder.AddDependency(operation.preparationCounter);
            }

            static_cast<void>(activeJobs.Increment());
            static_cast<void>(operation.activeJobs.Increment());
            jobs::Task task = jobs::Task::Create(
                [this, &operation](const jobs::JobContext&) noexcept
                {
                    RunConstruct(operation);
                    static_cast<void>(operation.activeJobs.Decrement());
                    static_cast<void>(activeJobs.Decrement());
                });
            if (!task || !builder.Dispatch(constructJobName, static_cast<jobs::Task&&>(task), jobs::Fence::Full))
            {
                static_cast<void>(operation.activeJobs.Decrement());
                static_cast<void>(activeJobs.Decrement());
                CompleteFailure(operation, Failure::OutOfMemory);
            }
        }

        void RunConstruct(PipelineOperation& operation) noexcept
        {
            containers::DynamicArray<ResourceHandle> handles{memory::pools::Resources::GetInstance()};
            handles.Reserve(operation.edges.Size());

            lock.AcquireShared();
            if (operation.terminal)
            {
                lock.ReleaseShared();
                return;
            }
            const Failure preparationFailure = static_cast<Failure>(operation.preparationFailure.GetValue());
            if (preparationFailure != Failure::None)
            {
                lock.ReleaseShared();
                CompleteFailure(operation, preparationFailure);
                return;
            }
            for (const DependencyEdge& edge : operation.edges)
            {
                const Failure childFailure = edge.operation->failure;
                if (edge.requirement == DependencyRequirement::Required && childFailure != Failure::None)
                {
                    PipelineOperation* const cause = edge.operation;
                    lock.ReleaseShared();
                    CompleteFailure(operation, Failure::DependencyFailure, cause);
                    return;
                }
            }
            lock.ReleaseShared();

            for (const DependencyEdge& edge : operation.edges)
            {
                ResourceHandle handle;
                if (static_cast<Failure>(edge.operation->publishedFailure.GetValue()) == Failure::None)
                {
                    edge.operation->registryReady.Wait();
                    handle = edge.operation->registryRequest.Acquire();
                }
                if (edge.requirement == DependencyRequirement::Required && !handle)
                {
                    CompleteFailure(operation, Failure::DependencyFailure, edge.operation);
                    return;
                }
                handles.PushBack(static_cast<ResourceHandle&&>(handle));
            }

            lock.AcquireShared();
            const bool terminal = operation.terminal;
            const AsyncLoaderDescriptor loader = operation.loader;
            lock.ReleaseShared();
            if (terminal)
            {
                return;
            }

            LoadContextState contextState{&operation, &handles};
            LoadContext context{&contextState};
            Failure failure = Failure::None;
            lock.Acquire();
            if (operation.terminal)
            {
                lock.Release();
                return;
            }
            operation.constructionInvoked = true;
            lock.Release();
            ResourceObject* const resource = loader.constructResource(context, failure, loader.userData);
            if (resource == nullptr)
            {
                CompleteFailure(operation, failure == Failure::None ? Failure::InternalError : failure);
                return;
            }
            if (resource->GetType() != operation.reference.ExpectedType())
            {
                loader.destroyResource(resource, loader.userData);
                CompleteFailure(operation, Failure::InternalError);
                return;
            }

            lock.AcquireShared();
            const bool cancelled = operation.terminal;
            lock.ReleaseShared();
            if (cancelled)
            {
                loader.destroyResource(resource, loader.userData);
                return;
            }
            CompleteSuccess(operation, resource);
        }
    };

    DependencyBuilder::DependencyBuilder(void* const state) noexcept : m_state(state) {}

    bool DependencyBuilder::Add(const ResourceReference reference, const DependencyRequirement requirement) noexcept
    {
        auto* const state = static_cast<DependencyBuildState*>(m_state);
        if (state == nullptr || !reference.IsValid() || !reference.IsTyped())
        {
            if (state != nullptr)
            {
                state->failure = Failure::InvalidPath;
            }
            return false;
        }

        for (DependencySpec& dependency : state->dependencies)
        {
            if (dependency.reference.GetPath() == reference.GetPath())
            {
                if (dependency.reference.ExpectedType() != reference.ExpectedType())
                {
                    state->failure = Failure::DependencyFailure;
                    return false;
                }
                if (requirement == DependencyRequirement::Required)
                {
                    dependency.requirement = requirement;
                }
                return true;
            }
        }
        if (state->dependencies.Size() >= state->maximumDependencies)
        {
            state->failure = Failure::DependencyLimit;
            return false;
        }
        state->dependencies.PushBack(DependencySpec{reference, requirement});
        return true;
    }

    bool DependencyBuilder::SetLoaderState(void* const loaderState) noexcept
    {
        auto* const state = static_cast<DependencyBuildState*>(m_state);
        if (state == nullptr || state->operation == nullptr || loaderState == nullptr || state->operation->loaderState != nullptr)
        {
            return false;
        }
        state->operation->loaderState = loaderState;
        return true;
    }

    void* DependencyBuilder::GetLoaderState() const noexcept
    {
        const auto* const state = static_cast<const DependencyBuildState*>(m_state);
        return state != nullptr && state->operation != nullptr ? state->operation->loaderState : nullptr;
    }

    void* DependencyBuilder::TakeLoaderState() noexcept
    {
        auto* const state = static_cast<DependencyBuildState*>(m_state);
        if (state == nullptr || state->operation == nullptr)
        {
            return nullptr;
        }
        void* const loaderState = state->operation->loaderState;
        state->operation->loaderState = nullptr;
        return loaderState;
    }

    u32 DependencyBuilder::Count() const noexcept
    {
        const auto* const state = static_cast<const DependencyBuildState*>(m_state);
        return state != nullptr ? state->dependencies.Size() : 0;
    }

    LoadContext::LoadContext(void* const state) noexcept : m_state(state) {}

    ResourceReference LoadContext::Reference() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr ? state->operation->reference : ResourceReference{};
    }

    LoadPriority LoadContext::Priority() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr ? static_cast<LoadPriority>(state->operation->priority.GetValue()) : LoadPriority::Normal;
    }

    bool LoadContext::IsCancellationRequested() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state == nullptr || state->operation == nullptr || state->operation->cancellationRequested.GetValue();
    }

    u32 LoadContext::GetDependencyCount() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr ? state->operation->edges.Size() : 0;
    }

    ResourceReference LoadContext::GetDependencyReference(const u32 index) const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr && index < state->operation->edges.Size() ? state->operation->edges[index].reference
                                                                                                         : ResourceReference{};
    }

    DependencyRequirement LoadContext::GetDependencyRequirementAt(const u32 index) const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr && index < state->operation->edges.Size() ? state->operation->edges[index].requirement
                                                                                                         : DependencyRequirement::Required;
    }

    Failure LoadContext::GetDependencyError(const u32 index) const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr && index < state->operation->edges.Size() && state->operation->edges[index].operation != nullptr
                   ? static_cast<Failure>(state->operation->edges[index].operation->publishedFailure.GetValue())
                   : Failure::InternalError;
    }

    const ResourceHandle& LoadContext::GetDependency(const u32 index) const noexcept
    {
        static const ResourceHandle empty;
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->handles != nullptr && index < state->handles->Size() ? (*state->handles)[index] : empty;
    }

    void* LoadContext::GetLoaderState() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        return state != nullptr && state->operation != nullptr ? state->operation->loaderState : nullptr;
    }

    void* LoadContext::TakeLoaderState() const noexcept
    {
        const auto* const state = static_cast<const LoadContextState*>(m_state);
        if (state == nullptr || state->operation == nullptr)
        {
            return nullptr;
        }
        void* const loaderState = state->operation->loaderState;
        state->operation->loaderState = nullptr;
        return loaderState;
    }

    PreparationRequest::PreparationRequest(ResourcePipeline* const pipeline, PipelineOperation* const operation) noexcept
        : m_pipeline(pipeline), m_operation(operation)
    {
    }

    ResourceReference PreparationRequest::Reference() const noexcept
    {
        return m_operation != nullptr ? m_operation->reference : ResourceReference{};
    }

    LoadPriority PreparationRequest::Priority() const noexcept
    {
        return m_operation != nullptr ? static_cast<LoadPriority>(m_operation->priority.GetValue()) : LoadPriority::Normal;
    }

    bool PreparationRequest::IsCancellationRequested() const noexcept
    {
        return m_operation == nullptr || m_operation->cancellationRequested.GetValue();
    }

    void* PreparationRequest::GetLoaderState() const noexcept
    {
        return m_operation != nullptr ? m_operation->loaderState : nullptr;
    }

    void* PreparationRequest::TakeLoaderState() const noexcept
    {
        return m_pipeline != nullptr ? m_pipeline->TakeLoaderState(m_operation) : nullptr;
    }

    bool PreparationRequest::Complete(const Failure failure) const noexcept
    {
        return m_pipeline != nullptr && m_operation != nullptr && m_pipeline->CompletePreparation(m_operation, failure);
    }

    PipelineRequest::PipelineRequest(ResourcePipeline* const pipeline, PipelineOperation* const operation) noexcept
        : m_pipeline(pipeline), m_operation(operation), m_reference(operation != nullptr ? operation->reference : ResourceReference{}),
          m_priority(operation != nullptr ? static_cast<LoadPriority>(operation->priority.GetValue()) : LoadPriority::Normal),
          m_hasInterest(pipeline != nullptr && operation != nullptr)
    {
    }

    PipelineRequest::PipelineRequest(PipelineRequest&& other) noexcept
        : m_pipeline(other.m_pipeline), m_operation(other.m_operation), m_reference(other.m_reference), m_priority(other.m_priority),
          m_hasInterest(other.m_hasInterest), m_cancelled(other.m_cancelled)
    {
        other.m_pipeline = nullptr;
        other.m_operation = nullptr;
        other.m_reference = {};
        other.m_priority = LoadPriority::Normal;
        other.m_hasInterest = false;
        other.m_cancelled = false;
    }

    PipelineRequest::~PipelineRequest()
    {
        Reset();
    }

    PipelineRequest& PipelineRequest::operator=(PipelineRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_pipeline = other.m_pipeline;
            m_operation = other.m_operation;
            m_reference = other.m_reference;
            m_priority = other.m_priority;
            m_hasInterest = other.m_hasInterest;
            m_cancelled = other.m_cancelled;
            other.m_pipeline = nullptr;
            other.m_operation = nullptr;
            other.m_reference = {};
            other.m_priority = LoadPriority::Normal;
            other.m_hasInterest = false;
            other.m_cancelled = false;
        }
        return *this;
    }

    ResourceReference PipelineRequest::Reference() const noexcept
    {
        return m_reference;
    }

    LoadPriority PipelineRequest::Priority() const noexcept
    {
        return m_cancelled ? m_priority
                           : (m_operation != nullptr ? static_cast<LoadPriority>(m_operation->priority.GetValue()) : LoadPriority::Normal);
    }

    State PipelineRequest::GetStatus() const noexcept
    {
        if (m_cancelled)
        {
            return State::Cancelled;
        }
        if (m_operation == nullptr || !m_operation->registryReady.TryWait(0))
        {
            return State::Queued;
        }
        return m_operation->registryRequest.GetStatus();
    }

    Failure PipelineRequest::GetError() const noexcept
    {
        if (m_cancelled)
        {
            return Failure::Cancelled;
        }
        if (m_operation == nullptr)
        {
            return Failure::InternalError;
        }
        return static_cast<Failure>(m_operation->publishedFailure.GetValue());
    }

    bool PipelineRequest::HasFinished() const noexcept
    {
        return m_cancelled || (m_operation != nullptr && m_operation->completionCounter.IsReady());
    }

    bool PipelineRequest::HasLoaded() const noexcept
    {
        return !m_cancelled && HasFinished() && GetError() == Failure::None && GetStatus() == State::Loaded;
    }

    bool PipelineRequest::HasFailed() const noexcept
    {
        return HasFinished() && !HasLoaded();
    }

    bool PipelineRequest::IsValid() const noexcept
    {
        return !m_cancelled && m_pipeline != nullptr && m_operation != nullptr;
    }

    PipelineRequest::operator bool() const noexcept
    {
        return IsValid();
    }

    bool PipelineRequest::IsSameOperation(const PipelineRequest& other) const noexcept
    {
        return !m_cancelled && !other.m_cancelled && m_operation != nullptr && m_operation == other.m_operation;
    }

    void PipelineRequest::Wait() const noexcept
    {
        if (!m_cancelled && m_operation != nullptr)
        {
            static_cast<void>(m_operation->completionCounter.Wait());
        }
    }

    bool PipelineRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_cancelled || (m_operation != nullptr && m_operation->completionCounter.Wait(false, static_cast<i32>(timeoutMilliseconds)));
    }

    ResourceHandle PipelineRequest::Acquire() const noexcept
    {
        if (!HasLoaded())
        {
            return {};
        }
        return m_operation->registryRequest.Acquire();
    }

    bool PipelineRequest::Promote(const LoadPriority priority) noexcept
    {
        return !m_cancelled && m_pipeline != nullptr && m_operation != nullptr && m_pipeline->PromoteRequest(m_operation, priority);
    }

    bool PipelineRequest::Cancel() noexcept
    {
        return CancelWithStatus() != PipelineCancelStatus::Invalid;
    }

    PipelineCancelStatus PipelineRequest::CancelWithStatus(Failure* const terminalFailure) noexcept
    {
        if (terminalFailure != nullptr)
            *terminalFailure = Failure::None;
        if (m_pipeline == nullptr || m_operation == nullptr || !m_hasInterest)
            return PipelineCancelStatus::Invalid;

        Failure observedFailure = Failure::None;
        const PipelineCancelStatus status = m_pipeline->CancelRequestInterest(m_operation, observedFailure);
        if (status == PipelineCancelStatus::Invalid)
            return status;

        m_hasInterest = false;
        // The interest release may make the operation reclaimable immediately.
        // Treat this request as detached after the atomic observation and return the
        // terminal result by value rather than dereferencing m_operation afterward.
        m_cancelled = true;
        if (terminalFailure != nullptr)
            *terminalFailure = observedFailure;
        return status;
    }

    bool PipelineRequest::GetFailureTrace(FailureTrace& trace) const noexcept
    {
        trace = {};
        if (m_cancelled)
        {
            trace.entries[0] = {Reference(), Failure::Cancelled};
            trace.count = 1;
            return true;
        }
        return m_pipeline != nullptr && m_operation != nullptr && m_pipeline->BuildFailureTrace(m_operation, trace);
    }

    void PipelineRequest::Reset() noexcept
    {
        if (m_pipeline != nullptr && m_operation != nullptr)
        {
            if (m_hasInterest)
            {
                m_pipeline->ReleaseRequestInterest(m_operation, false);
            }
            if (m_pipeline->m_impl != nullptr)
            {
                static_cast<void>(m_pipeline->m_impl->externalRequests.Decrement());
            }
        }
        m_pipeline = nullptr;
        m_operation = nullptr;
        m_reference = {};
        m_priority = LoadPriority::Normal;
        m_hasInterest = false;
        m_cancelled = false;
    }

    ResourcePipeline::~ResourcePipeline()
    {
        static_cast<void>(Shutdown());
    }

    bool ResourcePipeline::Initialize(ResourceRegistry& registry, const PipelineConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!registry.IsInitialized() || config.maximumDependenciesPerResource == 0 || config.maximumFailureTraceDepth == 0 ||
            config.maximumRetainedOperations == 0 || (!jobs::IsInitialized() && !jobs::Initialize()))
        {
            return false;
        }
        m_impl = AllocatePipelineObject<Impl>(*this, registry, config);
        if (m_impl == nullptr)
            return false;
        m_impl->allOperations.Reserve(config.maximumRetainedOperations);
        if (m_impl->allOperations.Capacity() < config.maximumRetainedOperations)
        {
            DeletePipelineObject(m_impl);
            m_impl = nullptr;
            return false;
        }
        return true;
    }

    bool ResourcePipeline::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        if (m_impl->externalRequests.GetValue() != 0 || m_impl->activeJobs.GetValue() != 0 || m_impl->activePreparations.GetValue() != 0)
        {
            return false;
        }

        m_impl->lock.AcquireShared();
        for (const PipelineOperation* const operation : m_impl->allOperations)
        {
            if (!operation->terminal || operation->rootInterests != 0 || operation->dependencyInterests != 0 ||
                !operation->completionCounter.IsReady() || operation->loaderState != nullptr)
            {
                m_impl->lock.ReleaseShared();
                return false;
            }
            const State state = m_impl->registry->GetState(operation->reference.GetPath());
            if (state == State::Queued || state == State::Loading || state == State::Loaded || state == State::Reloading || state == State::Evicting)
            {
                m_impl->lock.ReleaseShared();
                return false;
            }
        }
        m_impl->lock.ReleaseShared();

        for (const ResourceTypeId type : m_impl->loaderTypes)
        {
            if (!m_impl->registry->UnregisterLoader(type))
            {
                return false;
            }
        }

        for (PipelineOperation* const operation : m_impl->allOperations)
        {
            operation->registryRequest.Reset();
            DeletePipelineObject(operation);
        }
        m_impl->allOperations.Clear();
        DeletePipelineObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ResourcePipeline::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool ResourcePipeline::RegisterLoader(const AsyncLoaderDescriptor& loader) noexcept
    {
        if (m_impl == nullptr || !loader.IsValid())
        {
            return false;
        }

        m_impl->lock.Acquire();
        if (!m_impl->loaders.Insert(loader.type, loader).IsSuccessful())
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->loaderTypes.PushBack(loader.type);
        m_impl->lock.Release();

        const LoaderDescriptor registryLoader{loader.type, loader.name, &ResourcePipeline::BeginRegistryLoad, &ResourcePipeline::DestroyRegistryResource, this};
        if (!m_impl->registry->RegisterLoader(registryLoader))
        {
            m_impl->lock.Acquire();
            static_cast<void>(m_impl->loaders.Remove(loader.type));
            m_impl->loaderTypes.PopBack();
            m_impl->lock.Release();
            return false;
        }
        return true;
    }

    bool ResourcePipeline::UnregisterLoader(const ResourceTypeId type) noexcept
    {
        if (m_impl == nullptr || !m_impl->registry->UnregisterLoader(type))
        {
            return false;
        }

        m_impl->lock.Acquire();
        const bool removed = m_impl->loaders.Remove(type).IsSuccessful();
        for (u32 index = 0; index < m_impl->loaderTypes.Size(); ++index)
        {
            if (m_impl->loaderTypes[index] == type)
            {
                static_cast<void>(m_impl->loaderTypes.RemoveAt(index));
                break;
            }
        }
        m_impl->lock.Release();
        return removed;
    }

    bool ResourcePipeline::HasLoader(const ResourceTypeId type) const noexcept
    {
        if (m_impl == nullptr)
        {
            return false;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        return m_impl->loaders.Find(type) != m_impl->loaders.End();
    }

    PipelineRequest ResourcePipeline::Request(const ResourceReference reference, const LoadPriority priority) noexcept
    {
        if (m_impl == nullptr || !reference.IsValid() || !reference.IsTyped())
        {
            return {};
        }

        m_impl->CollectRetiredOperations();
        PipelineOperation* operation = nullptr;
        bool created = false;
        m_impl->lock.Acquire();
        ++m_impl->issuedRequests;
        operation = m_impl->ResolveCurrentOperationLocked(reference);
        if (operation != nullptr && operation->reference.ExpectedType() != reference.ExpectedType())
        {
            m_impl->lock.Release();
            return {};
        }
        if (operation == nullptr)
        {
            operation = m_impl->CreateOperationLocked(reference, priority);
            created = operation != nullptr;
        }
        else
        {
            ++m_impl->coalescedRequests;
            m_impl->PromoteLocked(*operation, priority);
        }
        if (operation == nullptr)
        {
            m_impl->lock.Release();
            return {};
        }
        ++operation->rootInterests;
        static_cast<void>(m_impl->externalRequests.Increment());
        m_impl->lock.Release();

        if (created)
        {
            m_impl->StartRegistryRequest(*operation);
        }
        else
        {
            operation->registryReady.Wait();
        }
        return PipelineRequest(this, operation);
    }

    PipelineRequest ResourcePipeline::Replace(const ResourceReference reference, const LoadPriority priority) noexcept
    {
        if (m_impl == nullptr || !reference.IsValid() || !reference.IsTyped())
            return {};

        m_impl->CollectRetiredOperations();
        PipelineOperation* current = nullptr;
        PipelineOperation* operation = nullptr;
        bool created = false;
        bool anchorReserved = false;
        OperationActions actions;
        m_impl->lock.Acquire();
        ++m_impl->issuedRequests;
        static_cast<void>(m_impl->currentOperations.Find(reference.GetPath().Id(), current));

        // A runtime resident can outlive the PipelineRequest that originally
        // loaded it. Recreate a canonical pipeline wrapper for that still-live
        // current registry generation before admitting a replacement. The
        // synthetic dependency interest reserves this old-current anchor while
        // its registry request is adopted outside the pipeline lock.
        if (current == nullptr)
        {
            current = m_impl->CreateOperationLocked(reference, priority);
            if (current == nullptr)
            {
                m_impl->lock.Release();
                return {};
            }
            ++current->dependencyInterests;
            anchorReserved = true;
            m_impl->lock.Release();

            m_impl->StartRegistryRequest(*current);
            current->registryReady.Wait();
            m_impl->lock.Acquire();
        }

        if (current->reference.ExpectedType() != reference.ExpectedType() || !current->terminal || current->failure != Failure::None ||
            !current->registryReady.TryWait(0) || !m_impl->registry->IsCurrent(current->registryRequest))
        {
            if (anchorReserved)
            {
                VG_ASSERT_MSG(current->dependencyInterests != 0, "replacement anchor reservation must own an interest");
                --current->dependencyInterests;
                m_impl->ProcessNoInterestLocked(*current, actions);
            }
            m_impl->lock.Release();
            m_impl->ExecuteActions(actions);
            return {};
        }

        static_cast<void>(m_impl->replacementOperations.Find(reference.GetPath().Id(), operation));
        if (operation != nullptr && operation->reference.ExpectedType() != reference.ExpectedType())
        {
            if (anchorReserved)
            {
                --current->dependencyInterests;
                m_impl->ProcessNoInterestLocked(*current, actions);
            }
            m_impl->lock.Release();
            m_impl->ExecuteActions(actions);
            return {};
        }
        if (operation == nullptr)
        {
            operation = m_impl->CreateOperationLocked(reference, priority, true);
            created = operation != nullptr;
            if (operation != nullptr)
            {
                if (!anchorReserved)
                    ++current->dependencyInterests;
                operation->replacementPrevious = current;
                anchorReserved = false;
            }
        }
        else
        {
            if (anchorReserved)
            {
                VG_ASSERT_MSG(current->dependencyInterests != 0, "replacement anchor reservation must own an interest");
                --current->dependencyInterests;
                m_impl->ProcessNoInterestLocked(*current, actions);
                anchorReserved = false;
            }
            ++m_impl->coalescedRequests;
            m_impl->PromoteLocked(*operation, priority);
        }
        if (operation == nullptr)
        {
            if (anchorReserved)
            {
                --current->dependencyInterests;
                m_impl->ProcessNoInterestLocked(*current, actions);
            }
            m_impl->lock.Release();
            m_impl->ExecuteActions(actions);
            return {};
        }
        ++operation->rootInterests;
        static_cast<void>(m_impl->externalRequests.Increment());
        m_impl->lock.Release();

        if (created)
            m_impl->StartRegistryRequest(*operation);
        else
            operation->registryReady.Wait();
        return PipelineRequest(this, operation);
    }

    PipelineStats ResourcePipeline::GetStats() const noexcept
    {
        PipelineStats stats;
        if (m_impl == nullptr)
        {
            return stats;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        stats.registeredLoaders = m_impl->loaderTypes.Size();
        stats.knownOperations = m_impl->allOperations.Size();
        stats.activeOperations = m_impl->currentOperations.Size() + m_impl->replacementOperations.Size();
        stats.activeJobs = m_impl->activeJobs.GetValue();
        stats.activePreparations = m_impl->activePreparations.GetValue();
        stats.externalRequests = m_impl->externalRequests.GetValue();
        stats.issuedRequests = m_impl->issuedRequests;
        stats.coalescedRequests = m_impl->coalescedRequests;
        stats.priorityPromotions = m_impl->priorityPromotions;
        stats.dependencyEdges = m_impl->dependencyEdges;
        stats.cancelledOperations = m_impl->cancelledOperations;
        stats.completedOperations = m_impl->completedOperations;
        stats.failedOperations = m_impl->failedOperations;
        return stats;
    }

    void ResourcePipeline::BeginRegistryLoad(ResourceRegistry& registry, const ResourceRequest& request, void* const userData) noexcept
    {
        static_cast<ResourcePipeline*>(userData)->OnBeginRegistryLoad(registry, request);
    }

    void ResourcePipeline::DestroyRegistryResource(ResourceObject* const resource, void* const userData) noexcept
    {
        auto* const pipeline = static_cast<ResourcePipeline*>(userData);
        if (pipeline == nullptr || pipeline->m_impl == nullptr || resource == nullptr)
        {
            return;
        }

        AsyncLoaderDescriptor loader;
        pipeline->m_impl->lock.AcquireShared();
        static_cast<void>(pipeline->m_impl->loaders.Find(resource->GetType(), loader));
        pipeline->m_impl->lock.ReleaseShared();
        if (loader.destroyResource != nullptr)
        {
            loader.destroyResource(resource, loader.userData);
        }
    }

    void ResourcePipeline::OnBeginRegistryLoad(ResourceRegistry& registry, const ResourceRequest& request) noexcept
    {
        if (m_impl == nullptr)
        {
            static_cast<void>(registry.Fail(request, Failure::InternalError));
            return;
        }

        PipelineOperation* operation = nullptr;
        m_impl->lock.Acquire();
        PipelineOperation* replacement = nullptr;
        static_cast<void>(m_impl->replacementOperations.Find(request.Reference().GetPath().Id(), replacement));
        if (replacement != nullptr && !replacement->registryReady.TryWait(0))
            operation = replacement;
        else
            static_cast<void>(m_impl->currentOperations.Find(request.Reference().GetPath().Id(), operation));
        if (operation == nullptr || operation->reference.ExpectedType() != request.Reference().ExpectedType())
        {
            m_impl->lock.Release();
            static_cast<void>(registry.Fail(request, Failure::InternalError));
            return;
        }

        operation->registryRequest = request;
        const bool begin = registry.BeginLoading(request);
        operation->registryReady.Signal();
        const bool cancelled = operation->terminal && operation->failure == Failure::Cancelled;
        if (!cancelled && begin && !operation->planScheduled)
        {
            operation->planScheduled = true;
        }
        const bool schedule = !cancelled && begin && operation->planScheduled;
        m_impl->lock.Release();

        if (cancelled)
        {
            static_cast<void>(registry.Cancel(request));
        }
        else if (!begin)
        {
            m_impl->CompleteFailure(*operation, Failure::InternalError);
        }
        else if (schedule)
        {
            m_impl->SchedulePlan(*operation);
        }
    }

    void ResourcePipeline::ReleaseRequestInterest(PipelineOperation* const operation, const bool) noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return;
        }

        OperationActions actions;
        m_impl->lock.Acquire();
        if (operation->rootInterests != 0)
        {
            --operation->rootInterests;
            m_impl->ProcessNoInterestLocked(*operation, actions);
        }
        m_impl->lock.Release();
        m_impl->ExecuteActions(actions);
        m_impl->CollectRetiredOperations();
    }

    PipelineCancelStatus ResourcePipeline::CancelRequestInterest(PipelineOperation* const operation, Failure& terminalFailure) noexcept
    {
        terminalFailure = Failure::None;
        if (m_impl == nullptr || operation == nullptr)
            return PipelineCancelStatus::Invalid;

        OperationActions actions;
        m_impl->lock.Acquire();
        if (operation->rootInterests == 0)
        {
            m_impl->lock.Release();
            return PipelineCancelStatus::Invalid;
        }

        PipelineCancelStatus status = PipelineCancelStatus::Cancelled;
        if (operation->terminal)
        {
            terminalFailure = operation->failure;
            status = operation->failure == Failure::None ? PipelineCancelStatus::Completed : PipelineCancelStatus::Failed;
        }
        --operation->rootInterests;
        m_impl->ProcessNoInterestLocked(*operation, actions);
        m_impl->lock.Release();
        m_impl->ExecuteActions(actions);
        m_impl->CollectRetiredOperations();
        return status;
    }

    bool ResourcePipeline::PromoteRequest(PipelineOperation* const operation, const LoadPriority priority) noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return false;
        }
        m_impl->lock.Acquire();
        const LoadPriority current = static_cast<LoadPriority>(operation->priority.GetValue());
        if (!IsHigherPriority(priority, current))
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->PromoteLocked(*operation, priority);
        m_impl->lock.Release();
        return true;
    }

    bool ResourcePipeline::CompletePreparation(PipelineOperation* const operation, const Failure failure) noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return false;
        }

        m_impl->lock.Acquire();
        if (!operation->preparationStarted || operation->preparationCompleted)
        {
            m_impl->lock.Release();
            return false;
        }
        operation->preparationCompleted = true;
        operation->preparationFailure.SetValue(static_cast<u32>(failure));
        operation->preparationCompletedAtomic.SetValue(true);
        static_cast<void>(m_impl->activePreparations.Decrement());
        m_impl->lock.Release();

        if (operation->preparationDeferral.IsValid())
        {
            operation->preparationDeferral.Finish();
        }
        return true;
    }

    void* ResourcePipeline::TakeLoaderState(PipelineOperation* const operation) noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return nullptr;
        }
        m_impl->lock.Acquire();
        void* const state = operation->loaderState;
        operation->loaderState = nullptr;
        m_impl->lock.Release();
        return state;
    }

    bool ResourcePipeline::BuildFailureTrace(const PipelineOperation* operation, FailureTrace& trace) const noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return false;
        }

        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        const u32 maximumDepth =
            m_impl->config.maximumFailureTraceDepth < MaximumFailureTraceEntries ? m_impl->config.maximumFailureTraceDepth : MaximumFailureTraceEntries;
        const PipelineOperation* current = operation;
        while (current != nullptr && trace.count < maximumDepth)
        {
            trace.entries[trace.count++] = {current->reference, current->failure};
            current = current->failureCause;
        }
        trace.truncated = current != nullptr;
        return trace.count != 0 && trace.entries[0].failure != Failure::None;
    }
} // namespace vanguard::resources
