#pragma once

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/vellum/document_internal.hpp>
#include <vanguard/vellum/runtime.hpp>

namespace vanguard::vellum
{
    namespace detail
    {
        struct CommitMergeEntry;
        struct InstanceSlot;
        struct SpawnSlot;
    }

    struct Runtime::Impl
    {
        streaming::ResourceStreamer* streamer = nullptr;
        concurrency::ThreadId ownerThread;
        RuntimeConfig config;
        RuntimeStats stats;
        detail::DocumentDecoderState documentDecoderState;
        streaming::DecoderDescriptor documentDecoder;
        memory::MemoryBlock instanceBlock;
        memory::MemoryBlock spawnBlock;
        detail::InstanceSlot* instanceSlots = nullptr;
        detail::CommitMergeEntry* structuralMergeScratch = nullptr;
        detail::SpawnSlot* spawnSlots = nullptr;
        u32 firstFreeInstance = InvalidIndex;
        u32 firstDeferredInstance = InvalidIndex;
        u32 firstFreeSpawn = InvalidIndex;
        u32 firstDeferredSpawn = InvalidIndex;
        u32 firstActiveSpawn = InvalidIndex;
        u32 lastActiveSpawn = InvalidIndex;
        u32 materializationCursor = InvalidIndex;
        u32 structuralMergeCount = 0;
        u64 nextMutationSequence = 1;
        bool documentDecoderRegistered = false;
        bool structuralCommitActive = false;
        bool semanticUpdateActive = false;
    };
} // namespace vanguard::vellum
