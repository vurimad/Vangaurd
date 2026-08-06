#include <vanguard/assets/asset_graph.hpp>
#include <vanguard/assets/asset_index.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/pool.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    enum class ResolutionMark : u8
    {
        New,
        Visiting,
        Resolved
    };

    struct OwnedBuildRequest
    {
        OwnedBuildRequest() noexcept
            : source(memory::pools::Assets::GetInstance()), metadata(memory::pools::Assets::GetInstance()),
              settings(memory::pools::Assets::GetInstance())
        {
        }

        [[nodiscard]] bool CopyFrom(const BuildRequest& request) noexcept
        {
            sourceReference = request.source.identity;
            outputReference = request.output;
            target = request.target;
            source.Resize(request.source.content.Count());
            metadata.Resize(request.source.metadata.Count());
            settings.Resize(request.settings.Count());
            if (source.Size() != request.source.content.Count() || metadata.Size() != request.source.metadata.Count() ||
                settings.Size() != request.settings.Count())
            {
                return false;
            }
            for (u32 index = 0; index < source.Size(); ++index)
            {
                source[index] = request.source.content[index];
            }
            for (u32 index = 0; index < metadata.Size(); ++index)
            {
                metadata[index] = request.source.metadata[index];
            }
            for (u32 index = 0; index < settings.Size(); ++index)
            {
                settings[index] = request.settings[index];
            }
            return true;
        }

        [[nodiscard]] BuildRequest View() const noexcept
        {
            return {{sourceReference, {source.TypedData(), source.Size()}, {metadata.TypedData(), metadata.Size()}},
                    outputReference,
                    target,
                    {settings.TypedData(), settings.Size()}};
        }

        resources::ResourceReference sourceReference;
        resources::ResourceReference outputReference;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        containers::DynamicArray<u8> source;
        containers::DynamicArray<u8> metadata;
        containers::DynamicArray<u8> settings;
    };

    struct BuildEdge
    {
        BuildOperation* operation = nullptr;
        resources::ResourceReference identity;
        DependencyRequirement requirement = DependencyRequirement::Required;
    };

    void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        static_cast<void>(hash.Update(&value, sizeof(value)));
    }

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                            static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u),
                            static_cast<u8>(value >> 24u), static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                            static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    [[nodiscard]] BuildFingerprint RequestKey(const BuildRequest& request) noexcept
    {
        constexpr char Domain[] = "vanguard.asset-graph-request.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU64(hash, request.source.identity.Path().Id());
        HashU32(hash, request.source.identity.ExpectedType());
        HashU64(hash, request.output.Path().Id());
        HashU32(hash, request.output.ExpectedType());
        HashU8(hash, static_cast<u8>(request.target));
        const BuildFingerprint source = crypto::Sha256(request.source.content.Data(), request.source.content.SizeInBytes());
        const BuildFingerprint metadata = crypto::Sha256(request.source.metadata.Data(), request.source.metadata.SizeInBytes());
        const BuildFingerprint settings = crypto::Sha256(request.settings.Data(), request.settings.SizeInBytes());
        static_cast<void>(hash.Update(source.bytes, source.ByteCount));
        static_cast<void>(hash.Update(metadata.bytes, metadata.ByteCount));
        static_cast<void>(hash.Update(settings.bytes, settings.ByteCount));
        BuildFingerprint result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    [[nodiscard]] jobs::Priority ToJobPriority(const BuildPriority priority) noexcept
    {
        switch (priority)
        {
        case BuildPriority::Background:
        case BuildPriority::Low:
            return jobs::Priority::Latent;
        case BuildPriority::Normal:
            return jobs::Priority::CriticalPath;
        case BuildPriority::High:
            return jobs::Priority::CriticalPath;
        case BuildPriority::Critical:
            return jobs::Priority::Immediate;
        }
        return jobs::Priority::CriticalPath;
    }

    [[nodiscard]] bool CopyArtifacts(const containers::ArraySpan<const Artifact> source,
                                     containers::DynamicArray<Artifact>& destination) noexcept
    {
        destination.Clear();
        destination.Resize(source.Count());
        if (destination.Size() != source.Count())
        {
            return false;
        }
        for (u32 index = 0; index < source.Count(); ++index)
        {
            destination[index] = source[index];
            if (destination[index].bytes.Size() != source[index].bytes.Size())
            {
                destination.Clear();
                return false;
            }
        }
        return true;
    }
} // namespace

namespace vanguard::assets
{
    struct BuildOperation
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        BuildOperation() noexcept : edges(memory::pools::Assets::GetInstance()) {}

        BuildFingerprint requestKey;
        OwnedBuildRequest request;
        BuildPlan plan;
        containers::DynamicArray<BuildEdge> edges;
        BuildOutput output;
        concurrency::Atomic<u32> state{static_cast<u32>(BuildState::Resolving)};
        concurrency::Atomic<u32> failure{static_cast<u32>(BuildFailure::None)};
        concurrency::Atomic<u32> buildError{static_cast<u32>(Result::Success)};
        concurrency::ManualResetEvent finished;
        jobs::Counter completion;
        jobs::CompletionDeferral completionDeferral;
        jobs::Counter stageCounter;
        ResolutionMark resolution = ResolutionMark::New;
        BuildPriority priority = BuildPriority::Normal;
        u32 externalInterests = 0;
        u32 dependencyInterests = 0;
        bool scheduled = false;
        bool dependenciesReleased = false;
        concurrency::Atomic<bool> cancelRequested;
    };

    struct BuildGraph::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        explicit Impl(const BuildGraphConfig& value) noexcept
            : config(value), completionSeedName("Assets/BuildGraph/CompletionSeed"), executeName("Assets/BuildGraph/Execute"),
              operations(memory::pools::Assets::GetInstance())
        {
        }

        BuildGraphConfig config;
        jobs::JobName completionSeedName;
        jobs::JobName executeName;
        BuildSystem* buildSystem = nullptr;
        DependencyIndex* dependencyIndex = nullptr;
        ResolveGeneratedDependencyFunction resolver = nullptr;
        void* resolverUserData = nullptr;
        mutable concurrency::Mutex lock;
        concurrency::Mutex requestLock;
        containers::DynamicArray<BuildOperation*> operations;
        BuildGraphStats stats;

        [[nodiscard]] BuildOperation* FindReusable(const BuildFingerprint& key) noexcept
        {
            for (BuildOperation* operation : operations)
            {
                const BuildState state = static_cast<BuildState>(operation->state.GetValue());
                if (operation->requestKey == key &&
                    (state == BuildState::Resolving || state == BuildState::Queued || state == BuildState::Building))
                {
                    return operation;
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool CreateCompletion(BuildOperation& operation) noexcept
        {
            jobs::Builder seed;
            jobs::Task task = jobs::Task::Create([](const jobs::JobContext&) noexcept {});
            if (!seed.IsValid() || !task || !seed.Dispatch(completionSeedName, static_cast<jobs::Task&&>(task)))
            {
                return false;
            }
            operation.completion = seed.ExtractCounter();
            if (!operation.completion.IsValid())
            {
                return false;
            }
            operation.completionDeferral = operation.completion.CreateDeferral("Assets/BuildGraph/Operation", &operation);
            return operation.completionDeferral.IsValid();
        }

        [[nodiscard]] BuildOperation* CreateOperation(const BuildRequest& request, const BuildFingerprint& key,
                                                      const BuildPriority priority) noexcept
        {
            if (operations.Size() >= config.maximumKnownOperations)
            {
                return nullptr;
            }
            BuildOperation* const operation = VANGUARD_NEW(BuildOperation);
            if (operation == nullptr)
            {
                return nullptr;
            }
            operation->requestKey = key;
            operation->priority = priority;
            if (!operation->request.CopyFrom(request) || !CreateCompletion(*operation))
            {
                VANGUARD_DELETE(operation);
                return nullptr;
            }
            const u32 previous = operations.Size();
            operations.PushBack(operation);
            if (operations.Size() != previous + 1u)
            {
                VANGUARD_DELETE(operation);
                return nullptr;
            }
            stats.knownOperations = operations.Size();
            ++stats.activeOperations;
            return operation;
        }

        void ReleaseDependenciesLocked(BuildOperation& operation) noexcept
        {
            if (operation.dependenciesReleased)
            {
                return;
            }
            operation.dependenciesReleased = true;
            for (const BuildEdge& edge : operation.edges)
            {
                if (edge.operation->dependencyInterests != 0)
                {
                    --edge.operation->dependencyInterests;
                }
                if (edge.operation->externalInterests == 0 && edge.operation->dependencyInterests == 0 &&
                    !edge.operation->finished.TryWait())
                {
                    edge.operation->cancelRequested.SetValue(true);
                    ReleaseDependenciesLocked(*edge.operation);
                }
            }
        }

        void Complete(BuildOperation& operation, const BuildState state, const BuildFailure failure, const Result buildError) noexcept
        {
            lock.Acquire();
            const BuildState previous = static_cast<BuildState>(operation.state.GetValue());
            if (previous == BuildState::Succeeded || previous == BuildState::Failed || previous == BuildState::Cancelled)
            {
                lock.Release();
                return;
            }
            operation.failure.SetValue(static_cast<u32>(failure));
            operation.buildError.SetValue(static_cast<u32>(buildError));
            operation.state.SetValue(static_cast<u32>(state));
            ReleaseDependenciesLocked(operation);
            if (stats.activeOperations != 0)
            {
                --stats.activeOperations;
            }
            if (state == BuildState::Succeeded)
            {
                ++stats.completedOperations;
            }
            else if (state == BuildState::Cancelled)
            {
                ++stats.cancelledOperations;
            }
            else
            {
                ++stats.failedOperations;
            }
            lock.Release();
            operation.finished.Signal();
            operation.completionDeferral.Finish();
        }

        [[nodiscard]] BuildOperation* ResolveRequest(const BuildRequest& request, const BuildPriority priority,
                                                     BuildFailure& failure) noexcept
        {
            const BuildFingerprint key = RequestKey(request);
            lock.Acquire();
            BuildOperation* operation = FindReusable(key);
            if (operation != nullptr)
            {
                if (priority > operation->priority && !operation->scheduled)
                {
                    operation->priority = priority;
                }
                ++stats.coalescedRequests;
                lock.Release();
                if (operation->resolution == ResolutionMark::Visiting)
                {
                    failure = BuildFailure::DependencyCycle;
                    Complete(*operation, BuildState::Failed, failure, Result::InvalidState);
                }
                return operation;
            }
            operation = CreateOperation(request, key, priority);
            if (operation == nullptr)
            {
                failure = operations.Size() >= config.maximumKnownOperations ? BuildFailure::LimitExceeded : BuildFailure::OutOfMemory;
                lock.Release();
                return nullptr;
            }
            lock.Release();
            if (!ResolveOperation(*operation, failure))
            {
                return operation;
            }
            return operation;
        }

        [[nodiscard]] bool ResolveOperation(BuildOperation& operation, BuildFailure& failure) noexcept
        {
            if (operation.resolution == ResolutionMark::Resolved)
            {
                return true;
            }
            if (operation.resolution == ResolutionMark::Visiting)
            {
                failure = BuildFailure::DependencyCycle;
                Complete(operation, BuildState::Failed, failure, Result::InvalidState);
                return false;
            }
            operation.resolution = ResolutionMark::Visiting;
            const BuildRequest request = operation.request.View();
            const Result prepared = buildSystem->Prepare(request, operation.plan);
            if (prepared != Result::Success)
            {
                failure = BuildFailure::ResolutionFailed;
                Complete(operation, BuildState::Failed, failure, prepared);
                operation.resolution = ResolutionMark::Resolved;
                return false;
            }

            for (const BuildDependency& dependency : operation.plan.Dependencies())
            {
                if (dependency.role != DependencyRole::Generated)
                {
                    continue;
                }
                if (operation.edges.Size() >= config.maximumGeneratedDependenciesPerOperation)
                {
                    failure = BuildFailure::LimitExceeded;
                    Complete(operation, BuildState::Failed, failure, Result::LimitExceeded);
                    operation.resolution = ResolutionMark::Resolved;
                    return false;
                }
                BuildRequest dependencyRequest;
                const bool resolved = resolver != nullptr && resolver(dependency, dependencyRequest, resolverUserData);
                if (!resolved || !dependencyRequest.IsValid() || dependencyRequest.output != dependency.identity)
                {
                    if (dependency.requirement == DependencyRequirement::Optional)
                    {
                        continue;
                    }
                    failure = BuildFailure::ResolutionFailed;
                    Complete(operation, BuildState::Failed, failure, Result::InvalidArgument);
                    operation.resolution = ResolutionMark::Resolved;
                    return false;
                }

                BuildFailure childFailure = BuildFailure::None;
                BuildOperation* const child = ResolveRequest(dependencyRequest, operation.priority, childFailure);
                if (child == nullptr)
                {
                    failure = childFailure;
                    Complete(operation, BuildState::Failed, failure, Result::OutOfMemory);
                    operation.resolution = ResolutionMark::Resolved;
                    return false;
                }
                const u32 previousEdgeCount = operation.edges.Size();
                operation.edges.PushBack({child, dependency.identity, dependency.requirement});
                if (operation.edges.Size() != previousEdgeCount + 1u)
                {
                    failure = BuildFailure::OutOfMemory;
                    Complete(operation, BuildState::Failed, failure, Result::OutOfMemory);
                    operation.resolution = ResolutionMark::Resolved;
                    return false;
                }
                lock.Acquire();
                ++child->dependencyInterests;
                ++stats.dependencyEdges;
                lock.Release();
                const BuildState childState = static_cast<BuildState>(child->state.GetValue());
                if (childFailure == BuildFailure::DependencyCycle || childState == BuildState::Failed)
                {
                    failure = childFailure == BuildFailure::DependencyCycle ? childFailure : BuildFailure::DependencyFailed;
                    Complete(operation, BuildState::Failed, failure, Result::InvalidState);
                    operation.resolution = ResolutionMark::Resolved;
                    return false;
                }
            }
            operation.resolution = ResolutionMark::Resolved;
            return true;
        }

        void Execute(BuildOperation& operation) noexcept
        {
            lock.Acquire();
            const bool cancelled = operation.cancelRequested.GetValue();
            if (!cancelled)
            {
                operation.state.SetValue(static_cast<u32>(BuildState::Building));
            }
            lock.Release();
            if (cancelled)
            {
                Complete(operation, BuildState::Cancelled, BuildFailure::Cancelled, Result::InvalidState);
                return;
            }

            for (const BuildEdge& edge : operation.edges)
            {
                const BuildState childState = static_cast<BuildState>(edge.operation->state.GetValue());
                if (childState == BuildState::Succeeded)
                {
                    if (operation.plan.SetGeneratedDependencyContent(edge.identity, edge.operation->output.contentFingerprint) !=
                        Result::Success)
                    {
                        Complete(operation, BuildState::Failed, BuildFailure::DependencyFailed, Result::InvalidState);
                        return;
                    }
                }
                else if (edge.requirement == DependencyRequirement::Required)
                {
                    Complete(operation, BuildState::Failed, BuildFailure::DependencyFailed,
                             static_cast<Result>(edge.operation->buildError.GetValue()));
                    return;
                }
            }

            const BuildRequest request = operation.request.View();
            const IsCancellationRequestedFunction cancellation = [](void* const userData) noexcept
            { return static_cast<BuildOperation*>(userData)->cancelRequested.GetValue(); };
            const Result result = buildSystem->Execute(request, operation.plan, operation.output, cancellation, &operation);
            if (result == Result::Success)
            {
                if (dependencyIndex != nullptr &&
                    dependencyIndex->Publish(request, operation.plan, operation.output) != IndexResult::Success)
                {
                    Complete(operation, BuildState::Failed, BuildFailure::IndexPublicationFailed, Result::InvalidState);
                    return;
                }
                Complete(operation, BuildState::Succeeded, BuildFailure::None, Result::Success);
            }
            else if (result == Result::Cancelled || operation.cancelRequested.GetValue())
            {
                Complete(operation, BuildState::Cancelled, BuildFailure::Cancelled, Result::Cancelled);
            }
            else
            {
                Complete(operation, BuildState::Failed, BuildFailure::BuildFailed, result);
            }
        }

        [[nodiscard]] bool Schedule(BuildOperation& operation) noexcept
        {
            if (operation.scheduled || operation.finished.TryWait())
            {
                return true;
            }
            for (const BuildEdge& edge : operation.edges)
            {
                if (!Schedule(*edge.operation))
                {
                    return false;
                }
            }

            jobs::Builder builder{{ToJobPriority(operation.priority), jobs::Affinity::AnyWorker}, &operation};
            for (const BuildEdge& edge : operation.edges)
            {
                builder.AddDependency(edge.operation->completion);
            }
            jobs::Task task =
                jobs::Task::Create([this, operationPointer = &operation](const jobs::JobContext&) noexcept { Execute(*operationPointer); });
            operation.scheduled = true;
            operation.state.SetValue(static_cast<u32>(BuildState::Queued));
            if (!builder.IsValid() || !task || !builder.Dispatch(executeName, static_cast<jobs::Task&&>(task)))
            {
                Complete(operation, BuildState::Failed, BuildFailure::SchedulingFailed, Result::InvalidState);
                return false;
            }
            operation.stageCounter = builder.ExtractCounter();
            return true;
        }
    };

    GraphRequest::GraphRequest(BuildGraph* const graph, BuildOperation* const operation) noexcept
        : m_graph(graph), m_operation(operation), m_hasInterest(graph != nullptr && operation != nullptr)
    {
    }

    GraphRequest::GraphRequest(GraphRequest&& other) noexcept
        : m_graph(other.m_graph), m_operation(other.m_operation), m_hasInterest(other.m_hasInterest)
    {
        other.m_graph = nullptr;
        other.m_operation = nullptr;
        other.m_hasInterest = false;
    }

    GraphRequest& GraphRequest::operator=(GraphRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_graph = other.m_graph;
            m_operation = other.m_operation;
            m_hasInterest = other.m_hasInterest;
            other.m_graph = nullptr;
            other.m_operation = nullptr;
            other.m_hasInterest = false;
        }
        return *this;
    }

    GraphRequest::~GraphRequest()
    {
        Reset();
    }

    bool GraphRequest::IsValid() const noexcept
    {
        return m_graph != nullptr && m_operation != nullptr;
    }

    GraphRequest::operator bool() const noexcept
    {
        return IsValid();
    }

    bool GraphRequest::IsSameOperation(const GraphRequest& other) const noexcept
    {
        return m_graph == other.m_graph && m_operation == other.m_operation && m_operation != nullptr;
    }

    BuildState GraphRequest::Status() const noexcept
    {
        return m_operation != nullptr ? static_cast<BuildState>(m_operation->state.GetValue()) : BuildState::Failed;
    }

    BuildFailure GraphRequest::Error() const noexcept
    {
        return m_operation != nullptr ? static_cast<BuildFailure>(m_operation->failure.GetValue()) : BuildFailure::InvalidRequest;
    }

    Result GraphRequest::BuildError() const noexcept
    {
        return m_operation != nullptr ? static_cast<Result>(m_operation->buildError.GetValue()) : Result::InvalidArgument;
    }

    bool GraphRequest::HasFinished() const noexcept
    {
        const BuildState state = Status();
        return state == BuildState::Succeeded || state == BuildState::Failed || state == BuildState::Cancelled;
    }

    bool GraphRequest::HasSucceeded() const noexcept
    {
        return Status() == BuildState::Succeeded;
    }

    void GraphRequest::Wait() const noexcept
    {
        if (m_operation != nullptr)
        {
            m_operation->finished.Wait();
        }
    }

    bool GraphRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_operation != nullptr && m_operation->finished.TryWait(timeoutMilliseconds);
    }

    bool GraphRequest::CopyOutput(BuildOutput& output) const noexcept
    {
        return m_graph != nullptr && m_operation != nullptr && m_graph->CopyOperationOutput(m_operation, output);
    }

    bool GraphRequest::Cancel() noexcept
    {
        if (!m_hasInterest || m_graph == nullptr || m_operation == nullptr)
        {
            return false;
        }
        m_graph->ReleaseInterest(m_operation, true);
        m_hasInterest = false;
        return true;
    }

    void GraphRequest::Reset() noexcept
    {
        if (m_hasInterest && m_graph != nullptr && m_operation != nullptr)
        {
            m_graph->ReleaseInterest(m_operation, false);
        }
        m_graph = nullptr;
        m_operation = nullptr;
        m_hasInterest = false;
    }

    BuildGraph::~BuildGraph()
    {
        static_cast<void>(Shutdown());
    }

    bool BuildGraph::Initialize(BuildSystem& buildSystem, const ResolveGeneratedDependencyFunction resolver, void* const resolverUserData,
                                const BuildGraphConfig& config, DependencyIndex* const dependencyIndex) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!memory::IsInitialized() || !jobs::IsInitialized() || !buildSystem.IsInitialized() || config.maximumKnownOperations == 0 ||
            config.maximumGeneratedDependenciesPerOperation == 0)
        {
            return false;
        }
        m_impl = VANGUARD_NEW(Impl)(config);
        if (m_impl == nullptr)
        {
            return false;
        }
        m_impl->buildSystem = &buildSystem;
        m_impl->dependencyIndex = dependencyIndex;
        m_impl->resolver = resolver;
        m_impl->resolverUserData = resolverUserData;
        return true;
    }

    bool BuildGraph::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.Acquire();
        if (m_impl->stats.externalRequests != 0 || m_impl->stats.activeOperations != 0)
        {
            m_impl->lock.Release();
            return false;
        }
        for (BuildOperation* operation : m_impl->operations)
        {
            VANGUARD_DELETE(operation);
        }
        m_impl->operations.Clear();
        m_impl->lock.Release();
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool BuildGraph::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool BuildGraph::IsBoundTo(const BuildSystem& buildSystem, const DependencyIndex& dependencyIndex) const noexcept
    {
        return m_impl != nullptr && m_impl->buildSystem == &buildSystem && m_impl->dependencyIndex == &dependencyIndex;
    }

    GraphRequest BuildGraph::Request(const BuildRequest& request, const BuildPriority priority) noexcept
    {
        if (m_impl == nullptr || !request.IsValid())
        {
            return {};
        }
        concurrency::ScopedLock requestGuard(m_impl->requestLock);
        m_impl->lock.Acquire();
        ++m_impl->stats.issuedRequests;
        m_impl->lock.Release();
        BuildFailure failure = BuildFailure::None;
        BuildOperation* const operation = m_impl->ResolveRequest(request, priority, failure);
        if (operation == nullptr)
        {
            return {};
        }
        m_impl->lock.Acquire();
        ++operation->externalInterests;
        ++m_impl->stats.externalRequests;
        m_impl->lock.Release();

        if (!operation->scheduled && !operation->finished.TryWait())
        {
            static_cast<void>(m_impl->Schedule(*operation));
        }
        return GraphRequest(this, operation);
    }

    BuildGraphStats BuildGraph::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        concurrency::ScopedLock guard(m_impl->lock);
        return m_impl->stats;
    }

    void BuildGraph::ReleaseInterest(BuildOperation* const operation, const bool) noexcept
    {
        if (m_impl == nullptr || operation == nullptr)
        {
            return;
        }
        m_impl->lock.Acquire();
        if (operation->externalInterests != 0)
        {
            --operation->externalInterests;
        }
        if (m_impl->stats.externalRequests != 0)
        {
            --m_impl->stats.externalRequests;
        }
        if (operation->externalInterests == 0 && operation->dependencyInterests == 0 && !operation->finished.TryWait())
        {
            operation->cancelRequested.SetValue(true);
            m_impl->ReleaseDependenciesLocked(*operation);
        }
        m_impl->lock.Release();
    }

    bool BuildGraph::CopyOperationOutput(const BuildOperation* const operation, BuildOutput& output) const noexcept
    {
        output.Reset();
        if (m_impl == nullptr || operation == nullptr || static_cast<BuildState>(operation->state.GetValue()) != BuildState::Succeeded)
        {
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        output.disposition = operation->output.disposition;
        output.buildFingerprint = operation->output.buildFingerprint;
        output.contentFingerprint = operation->output.contentFingerprint;
        return CopyArtifacts({operation->output.artifacts.TypedData(), operation->output.artifacts.Size()}, output.artifacts);
    }
} // namespace vanguard::assets
