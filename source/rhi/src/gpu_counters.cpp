#include <vanguard/rhi/gpu_counters.hpp>

#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/system/assert.hpp>
#include <vanguard/system/time.hpp>

#include <new>

namespace vanguard::rhi
{
    namespace
    {
        enum class SampleState : u8
        {
            Empty,
            Open,
            Closing,
            Closed
        };

        struct ScopeDefinition
        {
            GpuCounterScopeId id;
            char name[MaximumGpuCounterScopeNameLength]{};
        };

        struct FrameRecord
        {
            GpuCounterFrameHandle handle;
            GpuCounterFrameState state = GpuCounterFrameState::Empty;
            TimestampCalibration calibration;
            GpuFence completion;
            u64 frameNumber = 0;
            u64 sequence = 0;
            u64 gpuBegin = 0;
            u64 gpuEnd = 0;
            u32 sampleCount = 0;
            u32 openScopes = 0;
            u32 droppedSamples = 0;
        };

        void SetFailure(Failure* const failure, const FailureCode code, const char* const message) noexcept
        {
            if (failure == nullptr)
                return;
            failure->code = code;
            failure->backendCode = 0;
            u32 index = 0;
            if (message != nullptr)
                while (index + 1u < sizeof(failure->message) && message[index] != '\0')
                {
                    failure->message[index] = message[index];
                    ++index;
                }
            failure->message[index] = '\0';
        }

        void ClearFailure(Failure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        void FreeAllocation(void* const address, const usize size) noexcept
        {
            memory::MemoryBlock block{address, size, memory::PoolId::Rendering};
            memory::Free(block);
        }

        [[nodiscard]] bool CopyScopeName(char* const destination, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < MaximumGpuCounterScopeNameLength && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        [[nodiscard]] bool IsCommandListForQueue(const QueueType queue) noexcept
        {
            const CommandListType type = GetBoundCommandListType();
            return type != CommandListType::None && GetQueueType(type) == queue;
        }
    } // namespace

    struct GpuCounterSystem::Impl
    {
        concurrency::SpinLock lock;
        QueryPool timestampPool;
        QueryPool statisticsPool;
        GpuCounterConfig config;
        FrameRecord* frames = nullptr;
        GpuCounterSample* samples = nullptr;
        SampleState* sampleStates = nullptr;
        ScopeDefinition* definitions = nullptr;
        GpuCounterStats stats;
        u64 nextSequence = 1;
        u32 definitionCount = 0;

        [[nodiscard]] FrameRecord* FindFrame(const GpuCounterFrameHandle handle) noexcept
        {
            if (!handle.IsValid() || handle.index >= config.bufferedFrames)
                return nullptr;
            FrameRecord& frame = frames[handle.index];
            return frame.handle == handle && frame.state != GpuCounterFrameState::Empty ? &frame : nullptr;
        }

        [[nodiscard]] const FrameRecord* FindFrame(const GpuCounterFrameHandle handle) const noexcept
        {
            return const_cast<Impl*>(this)->FindFrame(handle);
        }

        [[nodiscard]] u32 SampleBase(const u32 frameIndex) const noexcept
        {
            return frameIndex * config.maximumScopesPerFrame;
        }
        [[nodiscard]] u32 TimestampBase(const u32 frameIndex) const noexcept
        {
            return frameIndex * (2u + config.maximumScopesPerFrame * 2u);
        }
        [[nodiscard]] u32 StatisticsBase(const u32 frameIndex) const noexcept
        {
            return frameIndex * config.maximumScopesPerFrame;
        }

        void ResetFrame(FrameRecord& frame) noexcept
        {
            const u32 base = SampleBase(frame.handle.index);
            for (u32 index = 0; index < config.maximumScopesPerFrame; ++index)
            {
                samples[base + index] = {};
                sampleStates[base + index] = SampleState::Empty;
            }
            const u32 generation = frame.handle.generation;
            const u32 slot = frame.handle.index;
            frame = {};
            frame.handle = {slot, generation};
        }

        void MarkRecordingFailure(FrameRecord& frame) noexcept
        {
            frame.state = GpuCounterFrameState::Failed;
            ++stats.recordingFailures;
        }
    };

    GpuCounterSystem::~GpuCounterSystem()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "GpuCounterSystem must be shut down before destruction");
    }

    bool GpuCounterSystem::Initialize(const GpuCounterConfig& config, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
        {
            SetFailure(failure, FailureCode::AlreadyInitialized, "GPU counter system is already initialized");
            return false;
        }
        if (!vanguard::rhi::IsInitialized())
        {
            SetFailure(failure, FailureCode::NotInitialized, "RHI must be initialized before GPU counters");
            return false;
        }
        if (!GetCapabilities().timestampQueries)
        {
            SetFailure(failure, FailureCode::Unsupported, "the active RHI backend does not support timestamp queries");
            return false;
        }
        if (config.bufferedFrames < 2 || config.bufferedFrames > MaximumGpuCounterBufferedFrames || config.maximumScopesPerFrame == 0 ||
            config.maximumRegisteredScopes == 0 || config.queue > QueueType::Copy ||
            (config.collectPipelineStatistics && config.queue != QueueType::Graphics) ||
            (config.collectPipelineStatistics && !GetCapabilities().pipelineStatisticsQueries))
        {
            SetFailure(failure, FailureCode::InvalidArgument, "invalid GPU counter configuration");
            return false;
        }
        const u64 timestampCapacity = static_cast<u64>(config.bufferedFrames) * (2ull + 2ull * config.maximumScopesPerFrame);
        const u64 statisticsCapacity = static_cast<u64>(config.bufferedFrames) * config.maximumScopesPerFrame;
        if (timestampCapacity > MaximumQueryPoolEntries || statisticsCapacity > MaximumQueryPoolEntries)
        {
            SetFailure(failure, FailureCode::CapacityExceeded, "GPU counter query capacity exceeds the RHI limit");
            return false;
        }

        memory::MemoryBlock implBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!implBlock)
        {
            SetFailure(failure, FailureCode::OutOfMemory, "failed to allocate GPU counter state");
            return false;
        }
        m_impl = new (implBlock.address) Impl();
        m_impl->config = config;
        memory::MemoryBlock frameBlock = memory::Allocate(memory::PoolId::Rendering, sizeof(FrameRecord) * config.bufferedFrames, alignof(FrameRecord));
        memory::MemoryBlock sampleBlock = memory::Allocate(
            memory::PoolId::Rendering, sizeof(GpuCounterSample) * config.bufferedFrames * config.maximumScopesPerFrame, alignof(GpuCounterSample));
        memory::MemoryBlock stateBlock =
            memory::Allocate(memory::PoolId::Rendering, sizeof(SampleState) * config.bufferedFrames * config.maximumScopesPerFrame, alignof(SampleState));
        memory::MemoryBlock definitionBlock =
            memory::Allocate(memory::PoolId::Rendering, sizeof(ScopeDefinition) * config.maximumRegisteredScopes, alignof(ScopeDefinition));
        if (!frameBlock || !sampleBlock || !stateBlock || !definitionBlock)
        {
            if (definitionBlock)
                memory::Free(definitionBlock);
            if (stateBlock)
                memory::Free(stateBlock);
            if (sampleBlock)
                memory::Free(sampleBlock);
            if (frameBlock)
                memory::Free(frameBlock);
            m_impl->~Impl();
            memory::Free(implBlock);
            m_impl = nullptr;
            SetFailure(failure, FailureCode::OutOfMemory, "failed to allocate bounded GPU counter storage");
            return false;
        }
        m_impl->frames = static_cast<FrameRecord*>(frameBlock.address);
        m_impl->samples = static_cast<GpuCounterSample*>(sampleBlock.address);
        m_impl->sampleStates = static_cast<SampleState*>(stateBlock.address);
        m_impl->definitions = static_cast<ScopeDefinition*>(definitionBlock.address);
        for (u32 index = 0; index < config.bufferedFrames; ++index)
            new (&m_impl->frames[index]) FrameRecord{};
        const u32 sampleCapacity = config.bufferedFrames * config.maximumScopesPerFrame;
        for (u32 index = 0; index < sampleCapacity; ++index)
        {
            new (&m_impl->samples[index]) GpuCounterSample{};
            new (&m_impl->sampleStates[index]) SampleState(SampleState::Empty);
        }
        for (u32 index = 0; index < config.maximumRegisteredScopes; ++index)
            new (&m_impl->definitions[index]) ScopeDefinition{};

        m_impl->timestampPool = QueryPool(AdoptReference, CreateQueryPool({QueryType::Timestamp, static_cast<u32>(timestampCapacity)}, failure));
        if (config.collectPipelineStatistics)
            m_impl->statisticsPool = QueryPool(AdoptReference, CreateQueryPool({QueryType::PipelineStatistics, static_cast<u32>(statisticsCapacity)}, failure));
        if (!m_impl->timestampPool || (config.collectPipelineStatistics && !m_impl->statisticsPool))
        {
            m_impl->timestampPool.Reset();
            m_impl->statisticsPool.Reset();
            for (u32 index = 0; index < config.maximumRegisteredScopes; ++index)
                m_impl->definitions[index].~ScopeDefinition();
            for (u32 index = 0; index < sampleCapacity; ++index)
                m_impl->samples[index].~GpuCounterSample();
            for (u32 index = 0; index < config.bufferedFrames; ++index)
                m_impl->frames[index].~FrameRecord();
            FreeAllocation(m_impl->definitions, sizeof(ScopeDefinition) * config.maximumRegisteredScopes);
            FreeAllocation(m_impl->sampleStates, sizeof(SampleState) * sampleCapacity);
            FreeAllocation(m_impl->samples, sizeof(GpuCounterSample) * sampleCapacity);
            FreeAllocation(m_impl->frames, sizeof(FrameRecord) * config.bufferedFrames);
            m_impl->~Impl();
            memory::Free(implBlock);
            m_impl = nullptr;
            if (failure == nullptr || failure->code == FailureCode::None)
                SetFailure(failure, FailureCode::BackendFailure, "failed to create GPU counter query pools");
            return false;
        }
        SetResourceDebugName(m_impl->timestampPool.GetRef(), "GPU Counter Timestamps");
        if (m_impl->statisticsPool)
            SetResourceDebugName(m_impl->statisticsPool.GetRef(), "GPU Counter Pipeline Statistics");
        return true;
    }

    bool GpuCounterSystem::Shutdown(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        {
            concurrency::ScopedLock guard(m_impl->lock);
            for (u32 index = 0; index < m_impl->config.bufferedFrames; ++index)
                if (m_impl->frames[index].state != GpuCounterFrameState::Empty)
                {
                    SetFailure(failure, FailureCode::Busy, "GPU counter shutdown requires every frame to be consumed or discarded");
                    return false;
                }
        }
        m_impl->statisticsPool.Reset();
        m_impl->timestampPool.Reset();
        const u32 sampleCapacity = m_impl->config.bufferedFrames * m_impl->config.maximumScopesPerFrame;
        for (u32 index = 0; index < m_impl->config.maximumRegisteredScopes; ++index)
            m_impl->definitions[index].~ScopeDefinition();
        for (u32 index = 0; index < sampleCapacity; ++index)
            m_impl->samples[index].~GpuCounterSample();
        for (u32 index = 0; index < m_impl->config.bufferedFrames; ++index)
            m_impl->frames[index].~FrameRecord();
        FreeAllocation(m_impl->definitions, sizeof(ScopeDefinition) * m_impl->config.maximumRegisteredScopes);
        FreeAllocation(m_impl->sampleStates, sizeof(SampleState) * sampleCapacity);
        FreeAllocation(m_impl->samples, sizeof(GpuCounterSample) * sampleCapacity);
        FreeAllocation(m_impl->frames, sizeof(FrameRecord) * m_impl->config.bufferedFrames);
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        FreeAllocation(impl, sizeof(Impl));
        return true;
    }

    bool GpuCounterSystem::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GpuCounterSystem::RegisterScope(const GpuCounterScopeId scope, const char* const name, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        if (!scope.IsValid())
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter scope identity is invalid");
            return false;
        }
        char copiedName[MaximumGpuCounterScopeNameLength]{};
        if (!CopyScopeName(copiedName, name))
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter scope name is empty or too long");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        for (u32 index = 0; index < m_impl->definitionCount; ++index)
            if (m_impl->definitions[index].id == scope)
            {
                u32 character = 0;
                while (copiedName[character] == m_impl->definitions[index].name[character] && copiedName[character] != '\0')
                    ++character;
                if (copiedName[character] != m_impl->definitions[index].name[character])
                {
                    SetFailure(failure, FailureCode::IncompatibleBinding, "GPU counter scope identity collides with another name");
                    return false;
                }
                return true;
            }
        if (m_impl->definitionCount == m_impl->config.maximumRegisteredScopes)
        {
            SetFailure(failure, FailureCode::CapacityExceeded, "GPU counter scope registry is full");
            return false;
        }
        ScopeDefinition& definition = m_impl->definitions[m_impl->definitionCount++];
        definition.id = scope;
        for (u32 index = 0; index < MaximumGpuCounterScopeNameLength; ++index)
            definition.name[index] = copiedName[index];
        m_impl->stats.registeredScopes = m_impl->definitionCount;
        return true;
    }

    const char* GpuCounterSystem::GetScopeName(const GpuCounterScopeId scope) const noexcept
    {
        if (m_impl == nullptr || !scope.IsValid())
            return nullptr;
        concurrency::ScopedLock guard(m_impl->lock);
        for (u32 index = 0; index < m_impl->definitionCount; ++index)
            if (m_impl->definitions[index].id == scope)
                return m_impl->definitions[index].name;
        return nullptr;
    }

    bool GpuCounterSystem::BeginFrame(const u64 frameNumber, GpuCounterFrameHandle& output, Failure* const failure) noexcept
    {
        output = {};
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        if (!IsCommandListForQueue(m_impl->config.queue))
        {
            SetFailure(failure, FailureCode::InvalidCommandList, "GPU counter frame requires a bound command list for its configured queue");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        u32 slot = m_impl->config.bufferedFrames;
        for (u32 index = 0; index < m_impl->config.bufferedFrames; ++index)
            if (m_impl->frames[index].state == GpuCounterFrameState::Empty)
            {
                slot = index;
                break;
            }
        if (slot == m_impl->config.bufferedFrames)
        {
            ++m_impl->stats.frameAllocationFailures;
            SetFailure(failure, FailureCode::Busy, "all buffered GPU counter frames are in use");
            return false;
        }
        FrameRecord& frame = m_impl->frames[slot];
        if (frame.handle.generation == 0xffffffffu)
        {
            ++m_impl->stats.frameAllocationFailures;
            SetFailure(failure, FailureCode::CapacityExceeded, "GPU counter frame generation is exhausted");
            return false;
        }
        ++frame.handle.generation;
        frame.handle.index = slot;
        frame.state = GpuCounterFrameState::Recording;
        frame.frameNumber = frameNumber;
        frame.sequence = m_impl->nextSequence++;
        frame.calibration.gpuFrequency = GetTimestampFrequency(m_impl->config.queue, failure);
        frame.calibration.cpuFrequency = system::GetMonotonicFrequency();
        TimestampCalibration calibrated{};
        if (GetCapabilities().timestampCalibration && CalibrateTimestamps(m_impl->config.queue, calibrated, nullptr))
            frame.calibration = calibrated;
        if (frame.calibration.gpuFrequency == 0 || !IssueQuery(m_impl->timestampPool.GetRef(), m_impl->TimestampBase(slot), failure))
        {
            m_impl->MarkRecordingFailure(frame);
            m_impl->ResetFrame(frame);
            return false;
        }
        output = frame.handle;
        ++m_impl->stats.recordedFrames;
        return true;
    }

    bool GpuCounterSystem::BeginScope(const GpuCounterFrameHandle handle, const GpuCounterScopeId scope, GpuCounterScopeToken& token,
                                      Failure* const failure) noexcept
    {
        token = {};
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        if (!IsCommandListForQueue(m_impl->config.queue))
        {
            SetFailure(failure, FailureCode::InvalidCommandList, "GPU counter scope requires a bound command list for its configured queue");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(handle);
        if (frame == nullptr || frame->state != GpuCounterFrameState::Recording)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter frame is not accepting scopes");
            return false;
        }
        bool registered = false;
        for (u32 index = 0; index < m_impl->definitionCount; ++index)
            registered = registered || m_impl->definitions[index].id == scope;
        if (!registered)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter scope is not registered");
            return false;
        }
        if (frame->sampleCount == m_impl->config.maximumScopesPerFrame)
        {
            ++frame->droppedSamples;
            ++m_impl->stats.droppedSamples;
            SetFailure(failure, FailureCode::CapacityExceeded, "GPU counter frame scope capacity is exhausted");
            return false;
        }
        const u32 sampleIndex = frame->sampleCount++;
        const u32 storageIndex = m_impl->SampleBase(handle.index) + sampleIndex;
        GpuCounterSample& sample = m_impl->samples[storageIndex];
        sample = {};
        sample.scope = scope;
        sample.cpuBegin = system::GetMonotonicTicks();
        sample.cpuThread = concurrency::ThreadId::GetCurrentThread().AsNumber();
        m_impl->sampleStates[storageIndex] = SampleState::Open;
        ++frame->openScopes;
        const u32 query = m_impl->TimestampBase(handle.index) + 2u + sampleIndex * 2u;
        if (!IssueQuery(m_impl->timestampPool.GetRef(), query, failure) ||
            (m_impl->config.collectPipelineStatistics &&
             !BeginQuery(m_impl->statisticsPool.GetRef(), m_impl->StatisticsBase(handle.index) + sampleIndex, failure)))
        {
            m_impl->MarkRecordingFailure(*frame);
            --frame->openScopes;
            m_impl->sampleStates[storageIndex] = SampleState::Empty;
            return false;
        }
        token = {handle, GetBoundCommandList(), sampleIndex};
        return true;
    }

    bool GpuCounterSystem::EndScope(GpuCounterScopeToken& token, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        if (!token.IsValid() || GetBoundCommandList() != token.commandList)
        {
            SetFailure(failure, FailureCode::InvalidCommandList, "GPU counter scope must end on the command list that began it");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(token.frame);
        if (frame == nullptr || (frame->state != GpuCounterFrameState::Recording && frame->state != GpuCounterFrameState::Failed) ||
            token.sampleIndex >= frame->sampleCount)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter scope token is stale");
            return false;
        }
        const u32 storageIndex = m_impl->SampleBase(token.frame.index) + token.sampleIndex;
        if (m_impl->sampleStates[storageIndex] != SampleState::Open)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter scope is not open");
            return false;
        }
        m_impl->sampleStates[storageIndex] = SampleState::Closing;
        const u32 query = m_impl->TimestampBase(token.frame.index) + 2u + token.sampleIndex * 2u + 1u;
        if (!IssueQuery(m_impl->timestampPool.GetRef(), query, failure) ||
            (m_impl->config.collectPipelineStatistics &&
             !EndQuery(m_impl->statisticsPool.GetRef(), m_impl->StatisticsBase(token.frame.index) + token.sampleIndex, failure)))
        {
            m_impl->MarkRecordingFailure(*frame);
            m_impl->sampleStates[storageIndex] = SampleState::Empty;
            --frame->openScopes;
            token = {};
            return false;
        }
        m_impl->samples[storageIndex].cpuEnd = system::GetMonotonicTicks();
        m_impl->sampleStates[storageIndex] = SampleState::Closed;
        --frame->openScopes;
        token = {};
        return true;
    }

    bool GpuCounterSystem::EndFrame(const GpuCounterFrameHandle handle, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        if (!IsCommandListForQueue(m_impl->config.queue))
        {
            SetFailure(failure, FailureCode::InvalidCommandList, "GPU counter frame resolution requires a bound command list for its queue");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(handle);
        if (frame == nullptr || frame->state != GpuCounterFrameState::Recording)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter frame is not recording");
            return false;
        }
        if (frame->openScopes != 0)
        {
            SetFailure(failure, FailureCode::Busy, "GPU counter frame still contains open scopes");
            return false;
        }
        const u32 timestampBase = m_impl->TimestampBase(handle.index);
        if (!IssueQuery(m_impl->timestampPool.GetRef(), timestampBase + 1u, failure) ||
            !ResolveQueries(m_impl->timestampPool.GetRef(), timestampBase, 2u + frame->sampleCount * 2u, failure) ||
            (m_impl->config.collectPipelineStatistics && frame->sampleCount != 0 &&
             !ResolveQueries(m_impl->statisticsPool.GetRef(), m_impl->StatisticsBase(handle.index), frame->sampleCount, failure)))
        {
            m_impl->MarkRecordingFailure(*frame);
            return false;
        }
        frame->state = GpuCounterFrameState::AwaitingSubmission;
        return true;
    }

    bool GpuCounterSystem::CommitFrame(const GpuCounterFrameHandle handle, const GpuFence completion, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(handle);
        if (frame == nullptr || frame->state != GpuCounterFrameState::AwaitingSubmission || !completion.IsValid() || completion.queue != m_impl->config.queue)
        {
            SetFailure(failure, FailureCode::InvalidArgument, "GPU counter frame requires its actual matching submission fence");
            return false;
        }
        frame->completion = completion;
        frame->state = GpuCounterFrameState::PendingGpu;
        return true;
    }

    bool GpuCounterSystem::DiscardFrame(const GpuCounterFrameHandle handle, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(handle);
        if (frame == nullptr || frame->state == GpuCounterFrameState::PendingGpu)
        {
            SetFailure(failure, FailureCode::Busy, "a submitted GPU counter frame cannot be discarded before completion");
            return false;
        }
        if (frame->openScopes != 0)
        {
            SetFailure(failure, FailureCode::Busy, "GPU counter frame contains open scopes");
            return false;
        }
        m_impl->ResetFrame(*frame);
        ++m_impl->stats.discardedFrames;
        return true;
    }

    u32 GpuCounterSystem::Collect(Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return 0;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        u32 collected = 0;
        for (u32 slot = 0; slot < m_impl->config.bufferedFrames; ++slot)
        {
            FrameRecord& frame = m_impl->frames[slot];
            if (frame.state != GpuCounterFrameState::PendingGpu || !IsGpuFenceComplete(frame.completion))
                continue;
            const u32 timestampBase = m_impl->TimestampBase(slot);
            const u32 timestampCount = 2u + frame.sampleCount * 2u;
            Failure queryFailure{};
            if (!AcquireQueries(m_impl->timestampPool.GetRef(), timestampBase, timestampCount, &queryFailure))
            {
                if (queryFailure.code == FailureCode::Busy)
                    continue;
                frame.state = GpuCounterFrameState::Failed;
                ++m_impl->stats.recordingFailures;
                SetFailure(failure, queryFailure.code, queryFailure.message);
                continue;
            }
            bool statisticsAcquired = false;
            if (m_impl->config.collectPipelineStatistics && frame.sampleCount != 0)
            {
                statisticsAcquired = AcquireQueries(m_impl->statisticsPool.GetRef(), m_impl->StatisticsBase(slot), frame.sampleCount, &queryFailure);
                if (!statisticsAcquired)
                {
                    ReleaseQueries(m_impl->timestampPool.GetRef());
                    if (queryFailure.code == FailureCode::Busy)
                        continue;
                    frame.state = GpuCounterFrameState::Failed;
                    ++m_impl->stats.recordingFailures;
                    SetFailure(failure, queryFailure.code, queryFailure.message);
                    continue;
                }
            }
            bool valid = GetQueryResult(m_impl->timestampPool.GetRef(), timestampBase, frame.gpuBegin, &queryFailure) &&
                         GetQueryResult(m_impl->timestampPool.GetRef(), timestampBase + 1u, frame.gpuEnd, &queryFailure);
            const u32 sampleBase = m_impl->SampleBase(slot);
            for (u32 index = 0; index < frame.sampleCount && valid; ++index)
            {
                GpuCounterSample& sample = m_impl->samples[sampleBase + index];
                valid = GetQueryResult(m_impl->timestampPool.GetRef(), timestampBase + 2u + index * 2u, sample.gpuBegin, &queryFailure) &&
                        GetQueryResult(m_impl->timestampPool.GetRef(), timestampBase + 3u + index * 2u, sample.gpuEnd, &queryFailure);
                if (valid && statisticsAcquired)
                    valid = GetQueryResult(m_impl->statisticsPool.GetRef(), m_impl->StatisticsBase(slot) + index, sample.pipelineStatistics, &queryFailure);
                sample.valid = valid && sample.gpuEnd >= sample.gpuBegin;
            }
            if (statisticsAcquired)
                ReleaseQueries(m_impl->statisticsPool.GetRef());
            ReleaseQueries(m_impl->timestampPool.GetRef());
            if (!valid)
            {
                frame.state = GpuCounterFrameState::Failed;
                ++m_impl->stats.recordingFailures;
                SetFailure(failure, queryFailure.code, queryFailure.message);
                continue;
            }
            frame.state = GpuCounterFrameState::Ready;
            ++m_impl->stats.collectedFrames;
            ++collected;
        }
        return collected;
    }

    bool GpuCounterSystem::GetOldestReadyFrame(GpuCounterFrameView& output, Failure* const failure) const noexcept
    {
        output = {};
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        const FrameRecord* selected = nullptr;
        for (u32 index = 0; index < m_impl->config.bufferedFrames; ++index)
        {
            const FrameRecord& frame = m_impl->frames[index];
            if (frame.state == GpuCounterFrameState::Ready && (selected == nullptr || frame.sequence < selected->sequence))
                selected = &frame;
        }
        if (selected == nullptr)
        {
            SetFailure(failure, FailureCode::Busy, "no GPU counter frame is ready");
            return false;
        }
        output = {selected->handle,
                  m_impl->samples + m_impl->SampleBase(selected->handle.index),
                  selected->calibration,
                  selected->completion,
                  selected->frameNumber,
                  selected->gpuBegin,
                  selected->gpuEnd,
                  selected->sampleCount,
                  selected->droppedSamples,
                  m_impl->config.queue,
                  m_impl->config.collectPipelineStatistics};
        return true;
    }

    bool GpuCounterSystem::ConsumeFrame(const GpuCounterFrameHandle handle, Failure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            SetFailure(failure, FailureCode::NotInitialized, "GPU counter system is not initialized");
            return false;
        }
        concurrency::ScopedLock guard(m_impl->lock);
        FrameRecord* const frame = m_impl->FindFrame(handle);
        if (frame == nullptr || frame->state != GpuCounterFrameState::Ready)
        {
            SetFailure(failure, FailureCode::Busy, "GPU counter frame is not ready for consumption");
            return false;
        }
        m_impl->ResetFrame(*frame);
        ++m_impl->stats.consumedFrames;
        return true;
    }

    GpuCounterFrameState GpuCounterSystem::GetFrameState(const GpuCounterFrameHandle handle) const noexcept
    {
        if (m_impl == nullptr)
            return GpuCounterFrameState::Empty;
        concurrency::ScopedLock guard(m_impl->lock);
        const FrameRecord* const frame = m_impl->FindFrame(handle);
        return frame != nullptr ? frame->state : GpuCounterFrameState::Empty;
    }

    GpuCounterStats GpuCounterSystem::GetStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        concurrency::ScopedLock guard(m_impl->lock);
        GpuCounterStats result = m_impl->stats;
        for (u32 index = 0; index < m_impl->config.bufferedFrames; ++index)
        {
            const GpuCounterFrameState state = m_impl->frames[index].state;
            result.recordingFrames += state == GpuCounterFrameState::Recording ? 1u : 0u;
            result.awaitingSubmissionFrames += state == GpuCounterFrameState::AwaitingSubmission ? 1u : 0u;
            result.pendingGpuFrames += state == GpuCounterFrameState::PendingGpu ? 1u : 0u;
            result.readyFrames += state == GpuCounterFrameState::Ready ? 1u : 0u;
        }
        return result;
    }
} // namespace vanguard::rhi
