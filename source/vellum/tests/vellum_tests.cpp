#include "../../../tests/support/scratch_directory.hpp"

#include <vanguard/containers/containers.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/streaming/streaming.hpp>
#include <vanguard/vellum/document_internal.hpp>
#include <vanguard/vellum/instance_storage.hpp>
#include <vanguard/vellum/runtime.hpp>
#include <vanguard/vellum/runtime_internal.hpp>
#include <vanguard/vellum/spawn_storage.hpp>

#include <bit>
#include <cstdio>
#include <vector>

namespace
{
    using namespace vanguard;
    using namespace vanguard::vellum;

    constexpr u32 ExhaustedGeneration = 0xffffffffu;

    u32 g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            ++g_failures;
            std::fprintf(stderr, "[vellumTests] FAILED: %s\n", message);
        }
    }

    struct WireProperty
    {
        PropertyId id = PropertyId::Invalid;
        u8 type = 0;
        u32 bits = 0;
    };

    struct WireNode
    {
        u64 authoredId = 0;
        u32 parent = InvalidDocumentNodeIndex;
        u8 kind = 0;
        std::vector<WireProperty> properties;
    };

    void StoreU16(std::vector<u8>& bytes, const usize offset, const u16 value)
    {
        bytes[offset + 0u] = static_cast<u8>(value & 0xffu);
        bytes[offset + 1u] = static_cast<u8>((value >> 8u) & 0xffu);
    }

    void StoreU32(std::vector<u8>& bytes, const usize offset, const u32 value)
    {
        StoreU16(bytes, offset, static_cast<u16>(value & 0xffffu));
        StoreU16(bytes, offset + 2u, static_cast<u16>((value >> 16u) & 0xffffu));
    }

    void StoreU64(std::vector<u8>& bytes, const usize offset, const u64 value)
    {
        StoreU32(bytes, offset, static_cast<u32>(value & 0xffffffffull));
        StoreU32(bytes, offset + 4u, static_cast<u32>(value >> 32u));
    }

    void AppendU8(std::vector<u8>& bytes, const u8 value)
    {
        bytes.push_back(value);
    }

    void AppendU16(std::vector<u8>& bytes, const u16 value)
    {
        const usize offset = bytes.size();
        bytes.resize(offset + 2u);
        StoreU16(bytes, offset, value);
    }

    void AppendU32(std::vector<u8>& bytes, const u32 value)
    {
        const usize offset = bytes.size();
        bytes.resize(offset + 4u);
        StoreU32(bytes, offset, value);
    }

    void AppendU64(std::vector<u8>& bytes, const u64 value)
    {
        const usize offset = bytes.size();
        bytes.resize(offset + 8u);
        StoreU64(bytes, offset, value);
    }

    [[nodiscard]] WireProperty FloatProperty(const PropertyId id, const f32 value)
    {
        return {id, static_cast<u8>(detail::WirePropertyType::Float), std::bit_cast<u32>(value)};
    }

    [[nodiscard]] WireProperty VisibilityProperty(const Visibility value)
    {
        return {PropertyId::Visibility, static_cast<u8>(detail::WirePropertyType::Visibility), static_cast<u32>(value) + 1u};
    }

    [[nodiscard]] WireProperty OrientationProperty(const Orientation value)
    {
        return {PropertyId::PanelOrientation, static_cast<u8>(detail::WirePropertyType::Orientation), static_cast<u32>(value) + 1u};
    }

    [[nodiscard]] std::vector<u8> BuildDocument(const std::vector<WireNode>& nodes,
                                                const vanguard::serialization::Version fileVersion = DocumentFileVersion,
                                                const vanguard::serialization::Version treeVersion = detail::TreeSectionVersion)
    {
        u32 propertyCount = 0;
        for (const WireNode& node : nodes)
            propertyCount += static_cast<u32>(node.properties.size());

        std::vector<u8> body;
        body.reserve(static_cast<usize>(detail::TreeBodyHeaderWireSize) + nodes.size() * detail::TreeNodeWireSize +
                     static_cast<usize>(propertyCount) * detail::TreePropertyWireSize);
        AppendU32(body, detail::TreeBodyWireVersion);
        AppendU32(body, detail::RuntimePropertySchemaVersion);
        AppendU32(body, detail::SupportedRequiredFeatureBits);
        AppendU32(body, 0u);
        AppendU32(body, static_cast<u32>(nodes.size()));
        AppendU32(body, propertyCount);

        u32 firstProperty = 0;
        for (const WireNode& node : nodes)
        {
            AppendU64(body, node.authoredId);
            AppendU32(body, node.parent);
            AppendU32(body, firstProperty);
            AppendU32(body, static_cast<u32>(node.properties.size()));
            AppendU8(body, node.kind);
            AppendU8(body, 0u);
            AppendU16(body, 0u);
            firstProperty += static_cast<u32>(node.properties.size());
        }
        for (const WireNode& node : nodes)
        {
            for (const WireProperty& property : node.properties)
            {
                AppendU16(body, static_cast<u16>(property.id));
                AppendU8(body, property.type);
                AppendU8(body, static_cast<u8>(sizeof(u32)));
                AppendU32(body, property.bits);
            }
        }

        constexpr usize headerOffset = 0u;
        constexpr usize sectionOffset = vanguard::serialization::DocumentHeader::WireSize;
        constexpr usize bodyOffset = detail::CanonicalTreeOffset;
        std::vector<u8> bytes(bodyOffset, 0u);
        bytes.insert(bytes.end(), body.begin(), body.end());

        StoreU32(bytes, headerOffset + 0u, DocumentMagic);
        bytes[headerOffset + 4u] = vanguard::serialization::DocumentHeader::LittleEndian;
        bytes[headerOffset + 5u] = vanguard::serialization::DocumentHeader::EncodingVersion;
        StoreU16(bytes, headerOffset + 6u, vanguard::serialization::DocumentHeader::WireSize);
        StoreU16(bytes, headerOffset + 8u, fileVersion.major);
        StoreU16(bytes, headerOffset + 10u, fileVersion.minor);
        StoreU16(bytes, headerOffset + 12u, static_cast<u16>(vanguard::serialization::DocumentFlags::Deterministic));
        StoreU16(bytes, headerOffset + 14u, 0u);
        StoreU64(bytes, headerOffset + 16u, static_cast<u64>(bytes.size()));
        StoreU64(bytes, headerOffset + 24u, detail::CanonicalSectionTableOffset);
        StoreU32(bytes, headerOffset + 32u, 1u);
        StoreU32(bytes, headerOffset + 36u, vanguard::serialization::Crc32(bytes.data(), 36u));

        StoreU32(bytes, sectionOffset + 0u, detail::TreeSectionId);
        StoreU16(bytes, sectionOffset + 4u, treeVersion.major);
        StoreU16(bytes, sectionOffset + 6u, treeVersion.minor);
        StoreU32(bytes, sectionOffset + 8u, static_cast<u32>(vanguard::serialization::SectionFlags::None));
        StoreU16(bytes, sectionOffset + 12u, static_cast<u16>(vanguard::serialization::Codec::None));
        bytes[sectionOffset + 14u] = detail::CanonicalTreeAlignmentLog2;
        bytes[sectionOffset + 15u] = 0u;
        StoreU64(bytes, sectionOffset + 16u, detail::CanonicalTreeOffset);
        StoreU64(bytes, sectionOffset + 24u, static_cast<u64>(body.size()));
        StoreU64(bytes, sectionOffset + 32u, static_cast<u64>(body.size()));
        StoreU64(bytes, sectionOffset + 40u, vanguard::serialization::Crc64(body.data(), body.size()));
        return bytes;
    }

    [[nodiscard]] std::vector<u8> MakeBasicDocument(const f32 firstWidth = 10.0f)
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel),
                         {OrientationProperty(Orientation::Horizontal), FloatProperty(PropertyId::PanelSpacing, 2.0f)}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer),
                         {FloatProperty(PropertyId::Width, firstWidth), FloatProperty(PropertyId::Height, 5.0f)}});
        nodes.push_back({3u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer),
                         {FloatProperty(PropertyId::Width, 20.0f), FloatProperty(PropertyId::Height, 7.0f)}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeSingleSpacerDocument(const f32 width)
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer),
                         {FloatProperty(PropertyId::Width, width), FloatProperty(PropertyId::Height, 4.0f)}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeCollapsedDocument(const u32 leafCount)
    {
        std::vector<WireNode> nodes;
        nodes.reserve(static_cast<usize>(leafCount) + 2u);
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::StackPanel), {VisibilityProperty(Visibility::Visible)}});
        for (u32 index = 0; index < leafCount; ++index)
        {
            nodes.push_back({static_cast<u64>(index) + 3u, 1u, static_cast<u8>(detail::WireNodeKind::Spacer),
                             {FloatProperty(PropertyId::Width, 1.0f), FloatProperty(PropertyId::Height, 1.0f)}});
        }
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeChainDocument(const u32 depth)
    {
        std::vector<WireNode> nodes;
        nodes.reserve(depth);
        for (u32 index = 0; index < depth; ++index)
        {
            const bool leaf = index + 1u == depth;
            nodes.push_back({static_cast<u64>(index) + 1u,
                             index == 0u ? InvalidDocumentNodeIndex : index - 1u,
                             static_cast<u8>(leaf ? detail::WireNodeKind::Spacer : detail::WireNodeKind::StackPanel),
                             leaf ? std::vector<WireProperty>{FloatProperty(PropertyId::Width, 1.0f), FloatProperty(PropertyId::Height, 1.0f)}
                                  : std::vector<WireProperty>{}});
        }
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeWideDocument(const u32 nodeCount)
    {
        std::vector<WireNode> nodes;
        nodes.reserve(nodeCount);
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        for (u32 index = 1u; index < nodeCount; ++index)
            nodes.push_back({static_cast<u64>(index) + 1u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer), {}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeConflictDocument()
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({3u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer), {}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeVersionCorruption()
    {
        std::vector<WireNode> nodes{{1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}}};
        return BuildDocument(nodes, {2u, 0u});
    }

    void RefreshTreeCrc(std::vector<u8>& bytes)
    {
        const usize treeOffset = static_cast<usize>(detail::CanonicalTreeOffset);
        const usize crcOffset = static_cast<usize>(detail::CanonicalSectionTableOffset) + 40u;
        StoreU64(bytes, crcOffset, vanguard::serialization::Crc64(bytes.data() + treeOffset, bytes.size() - treeOffset));
    }

    [[nodiscard]] std::vector<u8> MakeSectionCrcCorruption()
    {
        std::vector<u8> bytes = MakeBasicDocument();
        bytes[static_cast<usize>(detail::CanonicalTreeOffset) + detail::TreeBodyHeaderWireSize] ^= 0x01u;
        return bytes;
    }

    [[nodiscard]] std::vector<u8> MakeTreeVersionCorruption()
    {
        std::vector<WireNode> nodes{{1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}}};
        return BuildDocument(nodes, DocumentFileVersion, {2u, 0u});
    }

    [[nodiscard]] std::vector<u8> MakeBodyVersionCorruption()
    {
        std::vector<WireNode> nodes{{1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}}};
        std::vector<u8> bytes = BuildDocument(nodes);
        StoreU32(bytes, static_cast<usize>(detail::CanonicalTreeOffset), detail::TreeBodyWireVersion + 1u);
        RefreshTreeCrc(bytes);
        return bytes;
    }

    [[nodiscard]] std::vector<u8> MakePropertySchemaVersionCorruption()
    {
        std::vector<WireNode> nodes{{1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}}};
        std::vector<u8> bytes = BuildDocument(nodes);
        StoreU32(bytes, static_cast<usize>(detail::CanonicalTreeOffset) + 4u, detail::RuntimePropertySchemaVersion + 1u);
        RefreshTreeCrc(bytes);
        return bytes;
    }

    [[nodiscard]] std::vector<u8> MakeTopologyCorruption()
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 1u, static_cast<u8>(detail::WireNodeKind::Spacer), {}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakeChildUnderSpacerCorruption()
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer), {}});
        nodes.push_back({3u, 1u, static_cast<u8>(detail::WireNodeKind::Spacer), {}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] std::vector<u8> MakePropertyCorruption()
    {
        std::vector<WireNode> nodes;
        nodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel), {}});
        nodes.push_back({2u, 0u, static_cast<u8>(detail::WireNodeKind::Spacer),
                         {FloatProperty(PropertyId::Width, 1.0f), FloatProperty(PropertyId::Width, 2.0f)}});
        return BuildDocument(nodes);
    }

    [[nodiscard]] resources::ResourceReference MakeReference(const char* const path) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), DocumentResourceType);
    }

    constexpr resources::ResourceTypeId BlockingDestroyType = vanguard::serialization::MakeFourCC('V', 'D', 'S', 'T');

    class BlockingDestroyResource final : public resources::ResourceObject
    {
    public:
        VANGUARD_USE_MEMORY_POOL(memory::pools::Resources);

        [[nodiscard]] resources::ResourceTypeId GetType() const noexcept override
        {
            return BlockingDestroyType;
        }
    };

    struct BlockingDestroyState
    {
        concurrency::ManualResetEvent destroyEntered{false};
        concurrency::ManualResetEvent releaseDestroy{false};
        concurrency::Atomic<u32> destroyed{0};
    };

    resources::ResourceObject* DecodeBlockingDestroy(const resources::ResourceReference, const void*, const usize,
                                                     const resources::LoadContext&, resources::Failure& failure,
                                                     void*) noexcept
    {
        failure = resources::Failure::None;
        return VANGUARD_NEW(BlockingDestroyResource)();
    }

    void DestroyBlockingResource(resources::ResourceObject* const resource, void* const userData) noexcept
    {
        auto& state = *static_cast<BlockingDestroyState*>(userData);
        state.destroyEntered.Signal();
        state.releaseDestroy.Wait();
        VANGUARD_DELETE(static_cast<BlockingDestroyResource*>(resource));
        static_cast<void>(state.destroyed.Increment());
    }

    [[nodiscard]] bool SaveBytes(const filesystem::AbsolutePath& path, const std::vector<u8>& bytes)
    {
        auto writer = filesystem::RawFileWriter::Create(path, false);
        if (!writer)
            return false;
        if (!bytes.empty())
            writer->Serialize(const_cast<u8*>(bytes.data()), bytes.size());
        writer->Flush();
        return writer->GetSize() == bytes.size();
    }

    [[nodiscard]] RuntimeConfig SmallConfig() noexcept
    {
        RuntimeConfig config;
        config.maximumInstances = 8;
        config.maximumNodesPerDocument = 4096;
        config.maximumTotalNodes = 16384;
        config.maximumHierarchyDepth = 256;
        config.maximumStructuralCommandsPerInstance = 64;
        config.maximumConcurrentSpawns = 8;
        config.maximumMaterializedNodesPerUpdate = 4096;
        return config;
    }

    struct Harness
    {
        resources::ResourceRegistry registry;
        resources::ResourcePipeline pipeline;
        streaming::ResourceStreamer streamer;
        Runtime runtime;

        [[nodiscard]] bool Initialize(const RuntimeConfig& runtimeConfig)
        {
            if (!registry.Initialize())
                return false;
            if (!pipeline.Initialize(registry))
                return false;
            streaming::Config streamingConfig;
            streamingConfig.stagingBudgetBytes = 128ull * 1024ull * 1024ull;
            streamingConfig.maximumResourceBytes = 64ull * 1024ull * 1024ull;
            if (!streamer.Initialize(pipeline, streamingConfig))
                return false;
            Failure failure;
            return runtime.Initialize(streamer, runtimeConfig, &failure);
        }

        [[nodiscard]] bool RegisterLoose(const resources::ResourceReference reference, const filesystem::AbsolutePath& path,
                                         const std::vector<u8>& bytes)
        {
            if (!SaveBytes(path, bytes))
                return false;
            return streamer.RegisterLoose({reference, path, {}, vanguard::serialization::Crc64(bytes.data(), bytes.size()), 0});
        }

        [[nodiscard]] bool PublishLoose(const resources::ResourceReference reference, const filesystem::AbsolutePath& path,
                                        const std::vector<u8>& bytes)
        {
            if (!SaveBytes(path, bytes))
                return false;
            return streamer.PublishLoose({reference, path, {}, vanguard::serialization::Crc64(bytes.data(), bytes.size()), 0});
        }
    };

    class SpawnResetThread final : public concurrency::Thread
    {
    public:
        explicit SpawnResetThread(SpawnRequest& request) noexcept : concurrency::Thread("VellumSpawnReset"), m_request(&request) {}

        void ThreadFunction() noexcept override
        {
            if (m_request != nullptr)
                m_request->Reset();
        }

    private:
        SpawnRequest* m_request = nullptr;
    };

    class ResourceHandleResetThread final : public concurrency::Thread
    {
    public:
        explicit ResourceHandleResetThread(resources::ResourceHandle& handle) noexcept
            : concurrency::Thread("VellumResourceRelease"), m_handle(&handle)
        {
        }

        void ThreadFunction() noexcept override
        {
            if (m_handle != nullptr)
                m_handle->Reset();
        }

    private:
        resources::ResourceHandle* m_handle = nullptr;
    };

    void WaitForResourceDrain(Harness& harness)
    {
        for (u32 attempt = 0; attempt < 10000u; ++attempt)
        {
            const resources::PipelineStats pipelineStats = harness.pipeline.GetStats();
            const streaming::Stats streamingStats = harness.streamer.GetStats();
            if (pipelineStats.activeJobs == 0u && pipelineStats.activeOperations == 0u && pipelineStats.activePreparations == 0u &&
                streamingStats.activeLoads == 0u && streamingStats.activeReads == 0u)
                return;
            concurrency::SleepOnCurrentThread(1u);
        }
        Check(false, "resource and streaming work drains");
    }

    void CheckOwnershipGaugesZero(const RuntimeStats& stats)
    {
        Check(stats.liveInstances == 0u, "runtime has zero live instances");
        Check(stats.reservedNodeCapacity == 0u && stats.liveNodes == 0u, "runtime has zero node ownership");
        Check(stats.liveDetachedRoots == 0u && stats.liveDetachedNodes == 0u, "runtime has zero detached ownership");
        Check(stats.pendingStructuralCommands == 0u && stats.terminalStructuralCommands == 0u, "runtime has zero structural work");
        Check(stats.liveSpawns == 0u, "runtime has zero live spawn interests");
        Check(stats.queuedSpawns == 0u && stats.loadingSpawns == 0u && stats.materializingSpawns == 0u && stats.readyToCommitSpawns == 0u,
              "runtime has zero active spawn states");
        Check(stats.succeededSpawns == 0u && stats.failedSpawns == 0u && stats.cancelledSpawns == 0u,
              "runtime has zero retained terminal spawn states");
        Check(stats.stagedNodes == 0u, "runtime has zero staged-node ownership");
        Check(stats.deferredInstanceSlots == 0u && stats.deferredNodeSlots == 0u && stats.deferredSpawnSlots == 0u,
              "runtime has zero deferred slot ownership");
        Check(stats.freeInstanceSlots + stats.exhaustedInstanceSlots == stats.instanceCapacity,
              "all instance slots are free or permanently exhausted");
        Check(stats.freeSpawnSlots + stats.exhaustedSpawnSlots == stats.spawnCapacity,
              "all spawn slots are free or permanently exhausted");
        Check(stats.measureDirty.current == 0u && stats.arrangeDirty.current == 0u, "runtime has zero active layout frontier ownership");
        Check(stats.liveDecodedDocuments == 0u, "runtime has zero decoded document ownership");
    }

    void ShutdownHarness(Harness& harness)
    {
        Failure failure;
        if (harness.runtime.IsInitialized())
        {
            const RuntimeStats initial = harness.runtime.GetStats();
            if (initial.state == RuntimeState::Active)
                Check(harness.runtime.Quiesce(&failure), "runtime quiesce succeeds");
            if (harness.runtime.GetStats().state == RuntimeState::Quiescing)
            {
                Check(harness.runtime.BeginSemanticUpdate(&failure), "quiescing semantic update drains deferred ownership");
                Check(harness.runtime.Drain(&failure), "runtime drain succeeds");
            }
            WaitForResourceDrain(harness);
            CheckOwnershipGaugesZero(harness.runtime.GetStats());
            Check(harness.runtime.Shutdown(&failure), "runtime shutdown succeeds");
            Check(harness.streamer.GetStats().registeredDecoders == 0u, "runtime shutdown unregisters the Vellum decoder");
            Check(!harness.pipeline.HasLoader(DocumentResourceType), "runtime shutdown unregisters the Vellum pipeline loader");
            Check(!harness.registry.HasLoader(DocumentResourceType), "runtime shutdown unregisters the Vellum registry loader");
        }
        WaitForResourceDrain(harness);
        Check(harness.streamer.Shutdown(), "resource streamer shutdown succeeds");
        Check(harness.pipeline.Shutdown(), "resource pipeline shutdown succeeds");
        Check(harness.registry.Shutdown(), "resource registry shutdown succeeds");
    }

    [[nodiscard]] bool IsTerminal(const SpawnState state) noexcept
    {
        return state == SpawnState::Succeeded || state == SpawnState::Failed || state == SpawnState::Cancelled;
    }

    [[nodiscard]] bool DriveSpawn(Runtime& runtime, SpawnRequest& request, SpawnInfo& info)
    {
        Failure failure;
        for (u32 attempt = 0; attempt < 10000u; ++attempt)
        {
            if (!runtime.GetSpawnInfo(request, info, &failure))
                return false;
            if (IsTerminal(info.state))
                return true;
            if (!runtime.BeginSemanticUpdate(&failure))
                return false;
            concurrency::SleepOnCurrentThread(1u);
        }
        return false;
    }

    [[nodiscard]] bool DriveToPartialMaterialization(Runtime& runtime, SpawnRequest& request, SpawnInfo& info)
    {
        Failure failure;
        for (u32 attempt = 0; attempt < 10000u; ++attempt)
        {
            if (!runtime.GetSpawnInfo(request, info, &failure))
                return false;
            if (info.state == SpawnState::Materializing && info.materializedNodes != 0u && info.materializedNodes < info.totalNodes)
                return true;
            if (IsTerminal(info.state))
                return false;
            if (!runtime.BeginSemanticUpdate(&failure))
                return false;
            concurrency::SleepOnCurrentThread(1u);
        }
        return false;
    }

    void ReleaseSpawnInterest(Runtime& runtime, SpawnRequest& request)
    {
        if (!request.IsValid())
            return;
        request.Reset();
        Failure failure;
        Check(runtime.BeginSemanticUpdate(&failure), "spawn release intent is processed on semantic update");
    }

    [[nodiscard]] bool SpawnAndTake(Runtime& runtime, const SpawnDesc& desc, VellumInstanceHandle& instance)
    {
        SpawnRequest request;
        Failure failure;
        if (!runtime.RequestSpawn(desc, request, &failure))
            return false;
        SpawnInfo info;
        if (!DriveSpawn(runtime, request, info) || info.state != SpawnState::Succeeded)
            return false;
        return runtime.TakeSpawnResult(request, instance, &failure);
    }

    [[nodiscard]] VellumNodeHandle FirstChild(Runtime& runtime, const VellumNodeHandle node)
    {
        NodeInfo info;
        Failure failure;
        return runtime.GetNodeInfo(node, info, &failure) ? info.firstChild : VellumNodeHandle{};
    }

    void RunValidSpawnLayoutProperty(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        Check(harness.Initialize(config), "valid-path harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/basic.vlum");
        const std::vector<u8> bytes = MakeBasicDocument();
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_basic.vlum"), bytes), "basic VLUM fixture registers");

        SpawnRequest request;
        Failure failure;
        Check(harness.runtime.RequestSpawn({reference}, request, &failure), "basic spawn admission succeeds");
        SpawnInfo spawnInfo;
        Check(DriveSpawn(harness.runtime, request, spawnInfo) && spawnInfo.state == SpawnState::Succeeded,
              "basic spawn reaches success");
        Check(harness.runtime.GetStats().succeededSpawns == 1u && harness.runtime.GetStats().liveDecodedDocuments >= 1u,
              "successful spawn and decoded document ownership are observable");

        VellumInstanceHandle instance;
        Check(harness.runtime.TakeSpawnResult(request, instance, &failure), "TakeSpawnResult transfers the instance");
        Check(!request.IsValid() && harness.runtime.GetStats().liveInstances == 1u, "taken instance outlives spawn caller interest");

        InstanceInfo instanceInfo;
        Check(harness.runtime.GetInstanceInfo(instance, instanceInfo, &failure), "taken instance is queryable");
        const VellumNodeHandle first = FirstChild(harness.runtime, instanceInfo.root);
        NodeInfo firstInfo;
        Check(first.IsValid() && harness.runtime.GetNodeInfo(first, firstInfo, &failure), "first authored child is queryable");
        const VellumNodeHandle second = firstInfo.nextSibling;

        const LayoutConstraints constraints{{0.0f, 0.0f}, {1000.0f, 1000.0f}};
        Size desired;
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && desired == Size{32.0f, 7.0f},
              "stack-panel measure honors authored spacer size and spacing");
        const Rect rect{{0.0f, 0.0f}, {100.0f, 20.0f}};
        Check(harness.runtime.ArrangeInstance(instance, rect, &failure), "basic instance arranges");
        Geometry firstGeometry;
        Geometry secondGeometry;
        Check(harness.runtime.GetGeometry(first, firstGeometry, &failure) && harness.runtime.GetGeometry(second, secondGeometry, &failure),
              "arranged child geometry is queryable");
        Check(firstGeometry.rect.size.width == 10.0f && secondGeometry.rect.position.x == 12.0f && secondGeometry.rect.size.width == 20.0f,
              "horizontal stack geometry is deterministic");

        PropertyHandle width;
        Check(harness.runtime.ResolveProperty(first, PropertyId::Width, width, &failure), "spacer Width resolves to a compact property handle");
        const RuntimeStats equalBefore = harness.runtime.GetStats();
        Check(harness.runtime.WriteProperty(width, 10.0f, &failure), "equal canonical Width write succeeds");
        const RuntimeStats equalAfter = harness.runtime.GetStats();
        Check(equalAfter.measureDirty.current == equalBefore.measureDirty.current &&
                  equalAfter.arrangeDirty.current == equalBefore.arrangeDirty.current &&
                  equalAfter.measuredNodes == equalBefore.measuredNodes && equalAfter.arrangedNodes == equalBefore.arrangedNodes,
              "equal canonical Width write produces zero layout work");

        PropertyHandle opacity;
        Check(harness.runtime.ResolveProperty(first, PropertyId::Opacity, opacity, &failure), "spacer Opacity resolves");
        const RuntimeStats opacityBefore = harness.runtime.GetStats();
        Check(harness.runtime.WriteProperty(opacity, 0.5f, &failure), "Paint-only Opacity write succeeds");
        const RuntimeStats opacityAfter = harness.runtime.GetStats();
        Check(opacityAfter.measureDirty.current == opacityBefore.measureDirty.current &&
                  opacityAfter.arrangeDirty.current == opacityBefore.arrangeDirty.current,
              "Paint-only Opacity write produces no Measure/Arrange frontier work");

        Check(harness.runtime.WriteProperty(width, 15.0f, &failure), "typed Width write succeeds");
        Check(harness.runtime.GetStats().measureDirty.current != 0u || harness.runtime.GetStats().arrangeDirty.current != 0u,
              "layout-affecting Width write publishes layout work");
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && desired == Size{37.0f, 7.0f},
              "typed Width write invalidates and recomputes layout");

        Check(harness.runtime.ReleaseInstance(instance, &failure), "taken instance releases explicitly");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "released instance crosses generation quarantine boundary");

        SpawnRequest abandoned;
        Check(harness.runtime.RequestSpawn({reference}, abandoned, &failure), "abandonment spawn admission succeeds");
        Check(DriveSpawn(harness.runtime, abandoned, spawnInfo) && spawnInfo.state == SpawnState::Succeeded,
              "abandonment spawn reaches success");
        Check(harness.runtime.GetStats().liveInstances == 1u, "successful unclaimed result owns one live instance");
        SpawnResetThread resetThread(abandoned);
        resetThread.InitThread();
        Check(resetThread.IsValid(), "off-owner spawn-reset thread starts");
        resetThread.JoinThread();
        Check(!abandoned.IsValid() && harness.runtime.GetStats().liveInstances == 1u && harness.runtime.GetStats().liveSpawns == 1u,
              "off-owner SpawnRequest reset publishes intent without mutating semantic ownership");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "abandoned request release intent is processed");
        Check(harness.runtime.GetStats().liveInstances == 0u, "abandoned successful request retires its result instance");
        const RuntimeStats finalStats = harness.runtime.GetStats();
        Check(finalStats.totalSpawnRequests >= 2u && finalStats.successfulSpawnOperations >= 2u,
              "spawn request and successful-operation cumulative diagnostics are observable");
        ShutdownHarness(harness);
    }

    void RunGenerationQuarantine(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 1u;
        config.maximumConcurrentSpawns = 1u;
        config.maximumNodesPerDocument = 16u;
        config.maximumTotalNodes = 16u;
        config.maximumHierarchyDepth = 16u;
        config.maximumMaterializedNodesPerUpdate = 1u;
        Check(harness.Initialize(config), "quarantine harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/quarantine.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_quarantine.vlum"), MakeSingleSpacerDocument(2.0f)),
              "quarantine VLUM fixture registers");

        Failure failure;
        SpawnRequest first;
        Check(harness.runtime.RequestSpawn({reference}, first, &failure), "single spawn slot admits first request");
        SpawnInfo info;
        Check(DriveSpawn(harness.runtime, first, info) && info.state == SpawnState::Succeeded, "first quarantine spawn succeeds");
        const SpawnHandle firstHandle = info.spawn;
        first.Reset();
        Check(harness.runtime.BeginSemanticUpdate(&failure), "release enters deferred spawn quarantine");
        RuntimeStats stats = harness.runtime.GetStats();
        Check(stats.freeSpawnSlots == 0u && stats.deferredSpawnSlots == 1u, "released spawn slot is not reusable in the same semantic update");

        SpawnRequest blocked;
        Check(!harness.runtime.RequestSpawn({reference}, blocked, &failure) && failure.code == FailureCode::CapacityExceeded,
              "same-epoch spawn reuse is rejected while the slot is quarantined");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "next semantic update promotes deferred spawn slot");
        SpawnRequest second;
        Check(harness.runtime.RequestSpawn({reference}, second, &failure), "next epoch reuses the spawn slot");
        Check(harness.runtime.GetSpawnInfo(second, info, &failure) && info.spawn.index == firstHandle.index && info.spawn.generation > firstHandle.generation,
              "spawn slot generation advances before reuse");
        Check(harness.runtime.CancelSpawn(second, &failure), "reused spawn cancels");
        ReleaseSpawnInterest(harness.runtime, second);
        ShutdownHarness(harness);
    }

    void RunPrivateGenerationExhaustion()
    {
        {
            Runtime::Impl runtime;
            runtime.config = SmallConfig();
            runtime.config.maximumInstances = 1u;
            Failure failure;
            Check(detail::InitializeInstanceRegistry(runtime, &failure), "private instance registry initializes");
            detail::InstanceSlot& slot = runtime.instanceSlots[0];
            runtime.firstFreeInstance = InvalidIndex;
            runtime.stats.freeInstanceSlots = 0u;
            runtime.firstDeferredInstance = 0u;
            runtime.stats.deferredInstanceSlots = 1u;
            slot.generation = ExhaustedGeneration;
            slot.state = detail::SlotState::DeferredFree;
            slot.nextFree = InvalidIndex;
            detail::PromoteDeferredInstances(runtime);
            Check(slot.state == detail::SlotState::Exhausted && runtime.stats.exhaustedInstanceSlots == 1u &&
                      runtime.stats.freeInstanceSlots == 0u && runtime.stats.deferredInstanceSlots == 0u,
                  "max-generation instance slot is permanently exhausted");
            detail::DestroyInstanceRegistry(runtime);
        }

        {
            Runtime::Impl runtime;
            runtime.config = SmallConfig();
            runtime.config.maximumConcurrentSpawns = 1u;
            Failure failure;
            Check(detail::InitializeSpawnRegistry(runtime, &failure), "private spawn registry initializes");
            detail::SpawnSlot& slot = runtime.spawnSlots[0];
            runtime.firstFreeSpawn = InvalidIndex;
            runtime.stats.freeSpawnSlots = 0u;
            runtime.firstDeferredSpawn = 0u;
            runtime.stats.deferredSpawnSlots = 1u;
            slot.generation = ExhaustedGeneration;
            slot.lifecycle = detail::SpawnSlotLifecycle::DeferredFree;
            slot.nextFree = InvalidIndex;
            detail::PromoteDeferredSpawns(runtime);
            Check(slot.lifecycle == detail::SpawnSlotLifecycle::Exhausted && runtime.stats.exhaustedSpawnSlots == 1u &&
                      runtime.stats.freeSpawnSlots == 0u && runtime.stats.deferredSpawnSlots == 0u,
                  "max-generation spawn slot is permanently exhausted");
            detail::DestroySpawnRegistry(runtime);
        }

        {
            Runtime::Impl runtime;
            runtime.config = SmallConfig();
            runtime.config.maximumInstances = 1u;
            runtime.config.maximumNodesPerDocument = 1u;
            runtime.config.maximumTotalNodes = 1u;
            runtime.config.maximumHierarchyDepth = 1u;
            runtime.config.maximumMaterializedNodesPerUpdate = 1u;
            Failure failure;
            Check(detail::InitializeInstanceRegistry(runtime, &failure), "private node exhaustion registry initializes");
            VellumInstanceHandle handle;
            detail::InstanceState* instance = nullptr;
            Check(detail::AllocateInstance(runtime, 1u, handle, instance, &failure) && instance != nullptr,
                  "private node exhaustion reserves one instance");
            if (instance != nullptr)
            {
                detail::NodeSlot& slot = instance->nodeSlots[0];
                instance->firstFreeNode = InvalidIndex;
                instance->freeNodeCount = 0u;
                runtime.stats.freeNodeSlots = 0u;
                instance->firstDeferredNode = 0u;
                instance->deferredNodeCount = 1u;
                runtime.stats.deferredNodeSlots = 1u;
                slot.generation = ExhaustedGeneration;
                slot.state = detail::SlotState::DeferredFree;
                slot.nextFree = InvalidIndex;
                detail::PromoteDeferredNodes(runtime, *instance);
                Check(slot.state == detail::SlotState::Exhausted && instance->exhaustedNodeCount == 1u &&
                          runtime.stats.exhaustedNodeSlots == 1u && runtime.stats.deferredNodeSlots == 0u,
                      "max-generation node slot is permanently exhausted");
                Check(detail::RetireInstance(runtime, handle, &failure), "private exhausted-node instance retires cleanly");
                detail::PromoteDeferredInstances(runtime);
            }
            detail::DestroyInstanceRegistry(runtime);
        }
    }

    void RunStructuralSaturationAndNodeQuarantine(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumStructuralCommandsPerInstance = 1u;
        config.maximumNodesPerDocument = 4u;
        config.maximumTotalNodes = 4u;
        config.maximumHierarchyDepth = 3u;
        config.maximumMaterializedNodesPerUpdate = 4u;
        Check(harness.Initialize(config), "structural saturation harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/structural.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_structural.vlum"), MakeBasicDocument()),
              "structural VLUM fixture registers");

        VellumInstanceHandle instance;
        Check(SpawnAndTake(harness.runtime, {reference, resources::LoadPriority::Normal, 1u}, instance),
              "structural test instance spawns with one runtime node slot");
        Failure failure;
        InstanceInfo instanceInfo;
        Check(harness.runtime.GetInstanceInfo(instance, instanceInfo, &failure), "structural test instance info resolves");
        const VellumNodeHandle child = FirstChild(harness.runtime, instanceInfo.root);
        NodeInfo childInfo;
        Check(harness.runtime.GetNodeInfo(child, childInfo, &failure), "first structural child resolves");
        const VellumNodeHandle secondChild = childInfo.nextSibling;

        VellumNodeHandle detachedCreate;
        Check(harness.runtime.QueueCreate(instance, {NodeKind::Spacer}, detachedCreate, &failure), "ordinary structural lane accepts one Create");
        Check(!harness.runtime.QueueReparent(child, instanceInfo.root, {}, &failure) && failure.code == FailureCode::CapacityExceeded,
              "ordinary structural lane rejects additional work at saturation");
        Check(harness.runtime.QueueDestroy(child, &failure) && harness.runtime.QueueDestroy(secondChild, &failure),
              "distinct terminal Destroys remain admissible while the ordinary lane is saturated");
        Check(harness.runtime.GetStats().pendingStructuralCommands == 1u && harness.runtime.GetStats().terminalStructuralCommands == 2u,
              "terminal storage scales with admitted live nodes rather than ordinary-lane capacity");
        Check(harness.runtime.CommitStructuralMutations(&failure), "saturated ordinary plus terminal work commits");
        Check(harness.runtime.GetStats().pendingStructuralCommands == 0u && harness.runtime.GetStats().terminalStructuralCommands == 0u &&
                  harness.runtime.GetStats().peakTerminalStructuralCommands >= 2u,
              "multiple terminal Destroys make progress under ordinary saturation");
        NodeInfo staleInfo;
        Check(!harness.runtime.GetNodeInfo(child, staleInfo, &failure) && failure.code == FailureCode::StaleHandle,
              "first destroyed node handle is stale immediately");
        Check(!harness.runtime.GetNodeInfo(secondChild, staleInfo, &failure) && failure.code == FailureCode::StaleHandle,
              "second destroyed node handle is stale immediately");

        VellumNodeHandle blockedCreate;
        Check(!harness.runtime.QueueCreate(instance, {NodeKind::Spacer}, blockedCreate, &failure) && failure.code == FailureCode::CapacityExceeded,
              "destroyed node slot remains quarantined for the rest of the semantic epoch");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "next semantic update promotes destroyed node slot");
        VellumNodeHandle recycled;
        Check(harness.runtime.QueueCreate(instance, {NodeKind::Spacer}, recycled, &failure), "next epoch can reuse destroyed node capacity");
        const bool reusedFirst = recycled.local.slot == child.local.slot && recycled.local.generation > child.local.generation;
        const bool reusedSecond = recycled.local.slot == secondChild.local.slot && recycled.local.generation > secondChild.local.generation;
        Check(reusedFirst || reusedSecond, "node slot generation advances before destroyed-slot reuse");
        Check(harness.runtime.ReleaseInstance(instance, &failure), "structural test instance releases with queued work safely owned");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "released structural instance crosses quarantine boundary");
        ShutdownHarness(harness);
    }

    void RunMutationSequenceWitness()
    {
        Runtime::Impl runtime;
        runtime.config = SmallConfig();
        runtime.config.maximumInstances = 2u;
        runtime.config.maximumNodesPerDocument = 2u;
        runtime.config.maximumTotalNodes = 4u;
        runtime.config.maximumHierarchyDepth = 2u;
        runtime.config.maximumStructuralCommandsPerInstance = 1u;
        runtime.config.maximumMaterializedNodesPerUpdate = 2u;
        runtime.stats.state = RuntimeState::Active;
        runtime.stats.acceptingWork = true;
        Failure failure;
        Check(detail::InitializeInstanceRegistry(runtime, &failure), "mutation-sequence private registry initializes");
        VellumInstanceHandle firstHandle;
        VellumInstanceHandle secondHandle;
        detail::InstanceState* first = nullptr;
        detail::InstanceState* second = nullptr;
        Check(detail::AllocateInstance(runtime, 2u, firstHandle, first, &failure), "first private sequence instance reserves");
        Check(detail::AllocateInstance(runtime, 2u, secondHandle, second, &failure), "second private sequence instance reserves");
        if (first != nullptr && second != nullptr)
        {
            VellumNodeHandle firstNode;
            VellumNodeHandle secondNode;
            Check(detail::QueueCreate(runtime, *first, {NodeKind::Spacer}, firstNode, &failure), "first instance queues structural work");
            Check(detail::QueueCreate(runtime, *second, {NodeKind::Spacer}, secondNode, &failure), "second instance queues structural work");
            const detail::StructuralCommand* const firstCommand = first->ordinaryCommands.Front();
            const detail::StructuralCommand* const secondCommand = second->ordinaryCommands.Front();
            Check(firstCommand != nullptr && secondCommand != nullptr && secondCommand->sequence == firstCommand->sequence + 1u,
                  "separate instances draw from one runtime-global mutation sequence");
            Check(detail::RetireInstance(runtime, firstHandle, &failure), "first private sequence instance retires");
            Check(detail::RetireInstance(runtime, secondHandle, &failure), "second private sequence instance retires");
            detail::PromoteDeferredInstances(runtime);
        }
        detail::DestroyInstanceRegistry(runtime);
    }

    void RunTopologyConflictOrdering(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 2u;
        config.maximumConcurrentSpawns = 2u;
        config.maximumNodesPerDocument = 4u;
        config.maximumTotalNodes = 8u;
        config.maximumHierarchyDepth = 4u;
        config.maximumStructuralCommandsPerInstance = 4u;
        config.maximumMaterializedNodesPerUpdate = 4u;
        Check(harness.Initialize(config), "topology-conflict harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/conflict.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_conflict.vlum"), MakeConflictDocument()),
              "topology-conflict VLUM fixture registers");

        VellumInstanceHandle destroyFirst;
        VellumInstanceHandle reparentFirst;
        Check(SpawnAndTake(harness.runtime, {reference}, destroyFirst) && SpawnAndTake(harness.runtime, {reference}, reparentFirst),
              "topology-conflict instances spawn independently");
        Failure failure;

        InstanceInfo firstInfo;
        Check(harness.runtime.GetInstanceInfo(destroyFirst, firstInfo, &failure), "destroy-first instance info resolves");
        const VellumNodeHandle firstA = FirstChild(harness.runtime, firstInfo.root);
        NodeInfo firstAInfo;
        Check(harness.runtime.GetNodeInfo(firstA, firstAInfo, &failure), "destroy-first A resolves");
        const VellumNodeHandle firstB = firstAInfo.nextSibling;
        Check(harness.runtime.QueueDestroy(firstA, &failure), "destroy-first queues terminal A destroy");
        Check(harness.runtime.QueueReparent(firstB, firstA, {}, &failure), "destroy-first queues later B reparent under A");
        const u64 rejectedBefore = harness.runtime.GetStats().structuralCommandsRejected;
        Check(harness.runtime.CommitStructuralMutations(&failure), "destroy-first conflict commit completes");
        NodeInfo firstBInfo;
        Check(harness.runtime.GetNodeInfo(firstB, firstBInfo, &failure) && firstBInfo.parent == firstInfo.root,
              "Destroy A before Reparent B rejects stale parent and leaves B under root");
        Check(harness.runtime.GetStats().structuralCommandsRejected == rejectedBefore + 1u,
              "destroy-first ordering records exactly one rejected stale reparent");

        InstanceInfo secondInfo;
        Check(harness.runtime.GetInstanceInfo(reparentFirst, secondInfo, &failure), "reparent-first instance info resolves");
        const VellumNodeHandle secondA = FirstChild(harness.runtime, secondInfo.root);
        NodeInfo secondAInfo;
        Check(harness.runtime.GetNodeInfo(secondA, secondAInfo, &failure), "reparent-first A resolves");
        const VellumNodeHandle secondB = secondAInfo.nextSibling;
        Check(harness.runtime.QueueReparent(secondB, secondA, {}, &failure), "reparent-first queues B under A");
        Check(harness.runtime.QueueDestroy(secondA, &failure), "reparent-first queues later terminal A destroy");
        Check(harness.runtime.CommitStructuralMutations(&failure), "reparent-first conflict commit completes");
        NodeInfo staleInfo;
        Check(!harness.runtime.GetNodeInfo(secondA, staleInfo, &failure) && !harness.runtime.GetNodeInfo(secondB, staleInfo, &failure),
              "Reparent B before Destroy A deterministically destroys the moved subtree");

        Check(harness.runtime.ReleaseInstance(destroyFirst, &failure) && harness.runtime.ReleaseInstance(reparentFirst, &failure),
              "topology-conflict instances release");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "topology-conflict releases cross quarantine boundary");
        ShutdownHarness(harness);

        Harness cycleHarness;
        RuntimeConfig cycleConfig = SmallConfig();
        cycleConfig.maximumInstances = 1u;
        cycleConfig.maximumConcurrentSpawns = 1u;
        cycleConfig.maximumNodesPerDocument = 4u;
        cycleConfig.maximumTotalNodes = 4u;
        cycleConfig.maximumHierarchyDepth = 4u;
        cycleConfig.maximumMaterializedNodesPerUpdate = 4u;
        Check(cycleHarness.Initialize(cycleConfig), "cycle-rejection harness initializes");
        const resources::ResourceReference cycleReference = MakeReference("vellum/cycle.vlum");
        Check(cycleHarness.RegisterLoose(cycleReference, scratch.AddFilePath("vellum_cycle.vlum"), MakeChainDocument(4u)),
              "cycle-rejection VLUM fixture registers");
        VellumInstanceHandle cycleInstance;
        Check(SpawnAndTake(cycleHarness.runtime, {cycleReference}, cycleInstance), "cycle-rejection instance spawns");
        InstanceInfo cycleInfo;
        Check(cycleHarness.runtime.GetInstanceInfo(cycleInstance, cycleInfo, &failure), "cycle instance info resolves");
        const VellumNodeHandle cycleA = FirstChild(cycleHarness.runtime, cycleInfo.root);
        const VellumNodeHandle cycleB = FirstChild(cycleHarness.runtime, cycleA);
        const VellumNodeHandle cycleLeaf = FirstChild(cycleHarness.runtime, cycleB);
        NodeInfo beforeRoot;
        NodeInfo beforeA;
        NodeInfo beforeB;
        NodeInfo beforeLeaf;
        Check(cycleHarness.runtime.GetNodeInfo(cycleInfo.root, beforeRoot, &failure) &&
                  cycleHarness.runtime.GetNodeInfo(cycleA, beforeA, &failure) && cycleHarness.runtime.GetNodeInfo(cycleB, beforeB, &failure) &&
                  cycleHarness.runtime.GetNodeInfo(cycleLeaf, beforeLeaf, &failure),
              "cycle topology resolves before mutation");
        const u64 cycleRejectedBefore = cycleHarness.runtime.GetStats().structuralCommandsRejected;
        Check(cycleHarness.runtime.QueueReparent(cycleA, cycleB, {}, &failure), "cycle-forming reparent queues for commit validation");
        Check(cycleHarness.runtime.CommitStructuralMutations(&failure), "cycle-forming commit completes with command rejection");
        NodeInfo afterRoot;
        NodeInfo afterA;
        NodeInfo afterB;
        NodeInfo afterLeaf;
        const auto topologyEqual = [](const NodeInfo& left, const NodeInfo& right) noexcept {
            return left.parent == right.parent && left.firstChild == right.firstChild && left.lastChild == right.lastChild &&
                   left.previousSibling == right.previousSibling && left.nextSibling == right.nextSibling && left.childCount == right.childCount;
        };
        Check(cycleHarness.runtime.GetNodeInfo(cycleInfo.root, afterRoot, &failure) &&
                  cycleHarness.runtime.GetNodeInfo(cycleA, afterA, &failure) && cycleHarness.runtime.GetNodeInfo(cycleB, afterB, &failure) &&
                  cycleHarness.runtime.GetNodeInfo(cycleLeaf, afterLeaf, &failure) && topologyEqual(afterRoot, beforeRoot) &&
                  topologyEqual(afterA, beforeA) && topologyEqual(afterB, beforeB) && topologyEqual(afterLeaf, beforeLeaf),
              "cycle rejection leaves authoritative topology unchanged");
        Check(cycleHarness.runtime.GetStats().structuralCommandsRejected == cycleRejectedBefore + 1u,
              "cycle rejection is observable exactly once");
        Check(cycleHarness.runtime.ReleaseInstance(cycleInstance, &failure), "cycle instance releases");
        Check(cycleHarness.runtime.BeginSemanticUpdate(&failure), "cycle instance release crosses quarantine boundary");
        ShutdownHarness(cycleHarness);
    }

    void RunCollapsedWarmReveal(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        constexpr u32 collapsedLeafCount = 4096u;
        config.maximumNodesPerDocument = collapsedLeafCount + 2u;
        config.maximumTotalNodes = collapsedLeafCount + 2u;
        config.maximumHierarchyDepth = 8u;
        config.maximumMaterializedNodesPerUpdate = 4096u;
        Check(harness.Initialize(config), "collapsed-layout harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/collapsed.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_collapsed.vlum"), MakeCollapsedDocument(collapsedLeafCount)),
              "collapsed-layout VLUM fixture registers");

        VellumInstanceHandle instance;
        Check(SpawnAndTake(harness.runtime, {reference}, instance), "collapsed-layout instance spawns");
        Failure failure;
        InstanceInfo info;
        Check(harness.runtime.GetInstanceInfo(instance, info, &failure), "collapsed-layout instance info resolves");
        const VellumNodeHandle panel = FirstChild(harness.runtime, info.root);
        PropertyHandle visibility;
        Check(harness.runtime.ResolveProperty(panel, PropertyId::Visibility, visibility, &failure), "nested panel visibility resolves");

        const LayoutConstraints constraints{{0.0f, 0.0f}, {1000.0f, 1000.0f}};
        const Rect rect{{0.0f, 0.0f}, {1000.0f, 1000.0f}};
        Size desired;
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && harness.runtime.ArrangeInstance(instance, rect, &failure),
              "initial nested layout settles");
        Check(harness.runtime.WriteProperty(visibility, Visibility::Collapsed, &failure), "nested panel collapses");
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && harness.runtime.ArrangeInstance(instance, rect, &failure),
              "collapsed layout settles");
        const RuntimeStats warmBefore = harness.runtime.GetStats();
        memory::PoolMetrics poolBefore;
        memory::PoolMetrics poolAfter;
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, poolBefore), "collapsed warm Runtime-pool metrics are available");
        Check(harness.runtime.BeginSemanticUpdate(&failure) &&
                  harness.runtime.MeasureInstance(instance, constraints, desired, &failure) &&
                  harness.runtime.ArrangeInstance(instance, rect, &failure),
              "unchanged collapsed layout accepts warm update");
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, poolAfter), "collapsed warm Runtime-pool metrics remain available");
        const RuntimeStats warmAfter = harness.runtime.GetStats();
        Check(warmAfter.measuredNodes == warmBefore.measuredNodes && warmAfter.arrangedNodes == warmBefore.arrangedNodes &&
                  warmAfter.measureDirty.current == 0u && warmAfter.arrangeDirty.current == 0u &&
                  warmAfter.semanticUpdateHeapAllocations == warmBefore.semanticUpdateHeapAllocations &&
                  poolAfter.allocationCount == poolBefore.allocationCount &&
                  warmAfter.cleanNodeDiscoveries == warmBefore.cleanNodeDiscoveries,
              "large collapsed unchanged subtree performs zero warm layout work and semantic allocation");

        Check(harness.runtime.WriteProperty(visibility, Visibility::Visible, &failure), "collapsed subtree reveals");
        const RuntimeStats revealBefore = harness.runtime.GetStats();
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && harness.runtime.ArrangeInstance(instance, rect, &failure),
              "revealed subtree performs bounded repair");
        const RuntimeStats revealAfter = harness.runtime.GetStats();
        const u64 measuredDelta = revealAfter.measuredNodes - revealBefore.measuredNodes;
        const u64 arrangedDelta = revealAfter.arrangedNodes - revealBefore.arrangedNodes;
        Check(info.nodeCount == collapsedLeafCount + 2u && measuredDelta > 0u && measuredDelta <= info.nodeCount &&
                  arrangedDelta > 0u && arrangedDelta <= info.nodeCount,
              "reveal pays at most one admitted-subtree layout pass");

        Check(harness.runtime.ReleaseInstance(instance, &failure), "collapsed-layout instance releases");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "collapsed-layout release crosses quarantine boundary");
        ShutdownHarness(harness);
    }

    void CheckFailedSpawn(Harness& harness, const resources::ResourceReference reference, const resources::Failure expected,
                          const char* const message)
    {
        SpawnRequest request;
        Failure failure;
        Check(harness.runtime.RequestSpawn({reference}, request, &failure), "corrupt document spawn request is admitted for decode");
        SpawnInfo info;
        const bool terminal = DriveSpawn(harness.runtime, request, info);
        Check(terminal && info.state == SpawnState::Failed && info.resourceFailure == expected, message);
        ReleaseSpawnInterest(harness.runtime, request);
    }

    void RunDeepAndCorruptDecode(const filesystem::AbsolutePath& scratch)
    {
        constexpr u32 deepHierarchyDepth = 1024u;
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumNodesPerDocument = 2048u;
        config.maximumTotalNodes = 2048u;
        config.maximumHierarchyDepth = deepHierarchyDepth;
        config.maximumMaterializedNodesPerUpdate = 2048u;
        Check(harness.Initialize(config), "deep/corrupt harness initializes");

        const resources::ResourceReference deep = MakeReference("vellum/deep.vlum");
        const resources::ResourceReference tooDeep = MakeReference("vellum/too_deep.vlum");
        const resources::ResourceReference version = MakeReference("vellum/bad_version.vlum");
        const resources::ResourceReference treeVersion = MakeReference("vellum/bad_tree_version.vlum");
        const resources::ResourceReference bodyVersion = MakeReference("vellum/bad_body_version.vlum");
        const resources::ResourceReference propertySchemaVersion = MakeReference("vellum/bad_property_schema_version.vlum");
        const resources::ResourceReference sectionCrc = MakeReference("vellum/bad_tree_crc.vlum");
        const resources::ResourceReference topology = MakeReference("vellum/bad_topology.vlum");
        const resources::ResourceReference childUnderSpacer = MakeReference("vellum/child_under_spacer.vlum");
        const resources::ResourceReference property = MakeReference("vellum/bad_property.vlum");
        Check(harness.RegisterLoose(deep, scratch.AddFilePath("vellum_deep.vlum"), MakeChainDocument(deepHierarchyDepth)),
              "deep VLUM fixture registers");
        Check(harness.RegisterLoose(tooDeep, scratch.AddFilePath("vellum_too_deep.vlum"), MakeChainDocument(deepHierarchyDepth + 1u)),
              "over-depth VLUM fixture registers");
        Check(harness.RegisterLoose(version, scratch.AddFilePath("vellum_bad_version.vlum"), MakeVersionCorruption()), "version corruption fixture registers");
        Check(harness.RegisterLoose(treeVersion, scratch.AddFilePath("vellum_bad_tree_version.vlum"), MakeTreeVersionCorruption()),
              "TREE version corruption fixture registers");
        Check(harness.RegisterLoose(bodyVersion, scratch.AddFilePath("vellum_bad_body_version.vlum"), MakeBodyVersionCorruption()),
              "body version corruption fixture registers");
        Check(harness.RegisterLoose(propertySchemaVersion, scratch.AddFilePath("vellum_bad_property_schema_version.vlum"),
                                    MakePropertySchemaVersionCorruption()),
              "property schema version corruption fixture registers");
        Check(harness.RegisterLoose(sectionCrc, scratch.AddFilePath("vellum_bad_tree_crc.vlum"), MakeSectionCrcCorruption()),
              "TREE checksum corruption fixture registers");
        Check(harness.RegisterLoose(topology, scratch.AddFilePath("vellum_bad_topology.vlum"), MakeTopologyCorruption()), "topology corruption fixture registers");
        Check(harness.RegisterLoose(childUnderSpacer, scratch.AddFilePath("vellum_child_under_spacer.vlum"), MakeChildUnderSpacerCorruption()),
              "child-under-Spacer corruption fixture registers");
        Check(harness.RegisterLoose(property, scratch.AddFilePath("vellum_bad_property.vlum"), MakePropertyCorruption()), "property corruption fixture registers");

        VellumInstanceHandle deepInstance;
        Check(SpawnAndTake(harness.runtime, {deep}, deepInstance), "maximum-depth hierarchy spawns iteratively");
        Failure failure;
        const LayoutConstraints constraints{{0.0f, 0.0f}, {100.0f, 100.0f}};
        Size desired;
        Check(harness.runtime.MeasureInstance(deepInstance, constraints, desired, &failure) &&
                  harness.runtime.ArrangeInstance(deepInstance, {{0.0f, 0.0f}, {100.0f, 100.0f}}, &failure),
              "maximum-depth hierarchy measures and arranges iteratively");
        Check(harness.runtime.GetStats().maximumHierarchyDepthObserved == deepHierarchyDepth,
              "maximum admitted hierarchy depth is observable");
        Check(harness.runtime.ReleaseInstance(deepInstance, &failure), "deep instance releases");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "deep instance release crosses quarantine boundary");

        CheckFailedSpawn(harness, tooDeep, resources::Failure::OutOfMemory, "over-depth document decode is rejected by configured bounds");
        CheckFailedSpawn(harness, version, resources::Failure::UnsupportedVersion, "unsupported VLUM version is rejected atomically");
        CheckFailedSpawn(harness, treeVersion, resources::Failure::UnsupportedVersion, "unsupported TREE section version is rejected atomically");
        CheckFailedSpawn(harness, bodyVersion, resources::Failure::UnsupportedVersion, "unsupported TREE body version is rejected atomically");
        CheckFailedSpawn(harness, propertySchemaVersion, resources::Failure::UnsupportedVersion,
                         "unsupported runtime property schema version is rejected atomically");
        CheckFailedSpawn(harness, sectionCrc, resources::Failure::IntegrityFailure, "TREE checksum corruption is rejected atomically");
        CheckFailedSpawn(harness, topology, resources::Failure::IntegrityFailure, "invalid VLUM topology is rejected atomically");
        CheckFailedSpawn(harness, childUnderSpacer, resources::Failure::IntegrityFailure,
                         "Spacer parenting malformed topology is rejected atomically");
        CheckFailedSpawn(harness, property, resources::Failure::IntegrityFailure, "invalid VLUM property ordering is rejected atomically");
        Check(harness.runtime.GetStats().failedSpawnOperations >= 8u, "decode failures are reflected in cumulative spawn diagnostics");
        ShutdownHarness(harness);

        Harness limitHarness;
        RuntimeConfig limitConfig = SmallConfig();
        limitConfig.maximumNodesPerDocument = 2u;
        limitConfig.maximumTotalNodes = 4u;
        limitConfig.maximumHierarchyDepth = 2u;
        limitConfig.maximumCookedPropertiesPerDocument = 1u;
        limitConfig.maximumMaterializedNodesPerUpdate = 2u;
        Check(limitHarness.Initialize(limitConfig), "decode-limit harness initializes");
        const resources::ResourceReference nodeLimit = MakeReference("vellum/node_limit.vlum");
        const resources::ResourceReference propertyLimit = MakeReference("vellum/property_limit.vlum");
        std::vector<WireNode> propertyLimitNodes;
        propertyLimitNodes.push_back({1u, InvalidDocumentNodeIndex, static_cast<u8>(detail::WireNodeKind::StackPanel),
                                      {OrientationProperty(Orientation::Horizontal), FloatProperty(PropertyId::PanelSpacing, 1.0f)}});
        Check(limitHarness.RegisterLoose(nodeLimit, scratch.AddFilePath("vellum_node_limit.vlum"), MakeBasicDocument()),
              "node-count limit fixture registers");
        Check(limitHarness.RegisterLoose(propertyLimit, scratch.AddFilePath("vellum_property_limit.vlum"), BuildDocument(propertyLimitNodes)),
              "property-count limit fixture registers");
        CheckFailedSpawn(limitHarness, nodeLimit, resources::Failure::OutOfMemory, "configured node-count limit rejects decode atomically");
        CheckFailedSpawn(limitHarness, propertyLimit, resources::Failure::OutOfMemory,
                         "configured property-count limit rejects decode atomically");
        ShutdownHarness(limitHarness);
    }

    void RunLargeCardinalityAndWarmWork(const filesystem::AbsolutePath& scratch)
    {
        constexpr u32 nodeCount = 100'000u;
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 1u;
        config.maximumConcurrentSpawns = 1u;
        config.maximumNodesPerDocument = nodeCount;
        config.maximumTotalNodes = nodeCount;
        config.maximumHierarchyDepth = 2u;
        config.maximumStructuralCommandsPerInstance = 1u;
        config.maximumMaterializedNodesPerUpdate = 4096u;
        Check(harness.Initialize(config), "100k-cardinality harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/large_100k.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_large_100k.vlum"), MakeWideDocument(nodeCount)),
              "100k flat VLUM fixture registers");

        memory::PoolMetrics allocationBefore;
        memory::PoolMetrics allocationAfterSpawn;
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, allocationBefore), "100k Runtime-pool baseline metrics are available");
        const RuntimeStats statsBefore = harness.runtime.GetStats();
        VellumInstanceHandle instance;
        Check(SpawnAndTake(harness.runtime, {reference}, instance), "100k flat instance spawns through sliced materialization");
        const RuntimeStats statsAfterSpawn = harness.runtime.GetStats();
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, allocationAfterSpawn), "100k Runtime-pool post-spawn metrics are available");
        Check(statsAfterSpawn.semanticUpdateHeapAllocations - statsBefore.semanticUpdateHeapAllocations == 3u,
              "100k instance admission uses exactly three semantic-update Runtime blocks");
        Check(allocationAfterSpawn.allocationCount == allocationBefore.allocationCount + 3u,
              "100k instance storage allocation count is O(1), not one heap object per node");
        Check(statsAfterSpawn.materializedNodes >= nodeCount && statsAfterSpawn.peakStagedNodes > 0u,
              "100k materialization work and staged high-water diagnostics are observable");

        Failure failure;
        InstanceInfo info;
        Check(harness.runtime.GetInstanceInfo(instance, info, &failure) && info.nodeCount == nodeCount && info.nodeCapacity == nodeCount,
              "100k admitted node cardinality is exact");
        const LayoutConstraints constraints{{0.0f, 0.0f}, {1000.0f, 1000.0f}};
        const Rect rect{{0.0f, 0.0f}, {1000.0f, 1000.0f}};
        Size desired;
        Check(harness.runtime.MeasureInstance(instance, constraints, desired, &failure) && harness.runtime.ArrangeInstance(instance, rect, &failure),
              "100k flat instance settles iteratively");
        WaitForResourceDrain(harness);
        Check(jobs::GetOutstandingJobCount() == 0u, "100k settled instance has no outstanding per-node jobs");

        const RuntimeStats warmBefore = harness.runtime.GetStats();
        const jobs::SchedulerStats warmJobsBefore = jobs::GetSchedulerStats();
        memory::PoolMetrics warmPoolBefore;
        memory::PoolMetrics warmPoolAfter;
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, warmPoolBefore), "100k warm Runtime-pool baseline is available");
        Check(harness.runtime.BeginSemanticUpdate(&failure) &&
                  harness.runtime.MeasureInstance(instance, constraints, desired, &failure) &&
                  harness.runtime.ArrangeInstance(instance, rect, &failure),
              "100k unchanged warm update settles");
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, warmPoolAfter), "100k warm Runtime-pool result is available");
        const RuntimeStats warmAfter = harness.runtime.GetStats();
        const jobs::SchedulerStats warmJobsAfter = jobs::GetSchedulerStats();
        Check(warmAfter.semanticUpdateHeapAllocations == warmBefore.semanticUpdateHeapAllocations &&
                  warmAfter.measuredNodes == warmBefore.measuredNodes && warmAfter.arrangedNodes == warmBefore.arrangedNodes &&
                  warmAfter.cleanNodeDiscoveries == warmBefore.cleanNodeDiscoveries &&
                  warmAfter.measureDirty.current == 0u && warmAfter.arrangeDirty.current == 0u &&
                  warmPoolAfter.allocationCount == warmPoolBefore.allocationCount &&
                  warmJobsAfter.submittedJobs == warmJobsBefore.submittedJobs && jobs::GetOutstandingJobCount() == 0u,
              "100k unchanged warm update performs zero semantic allocation, layout work, frontier work, or jobs");

        Check(harness.runtime.ReleaseInstance(instance, &failure), "100k instance releases");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "100k instance release crosses quarantine boundary");
        memory::PoolMetrics allocationAfterRelease;
        Check(memory::GetPoolMetrics(memory::PoolId::Runtime, allocationAfterRelease) &&
                  allocationAfterRelease.allocationCount == allocationBefore.allocationCount,
              "100k O(1) instance blocks return to the Runtime pool after release");
        ShutdownHarness(harness);
    }

    void RunSharedLoadCancellation(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 2u;
        config.maximumConcurrentSpawns = 2u;
        config.maximumNodesPerDocument = 512u;
        config.maximumTotalNodes = 1024u;
        config.maximumHierarchyDepth = 4u;
        config.maximumMaterializedNodesPerUpdate = 1u;
        Check(harness.Initialize(config), "shared-load harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/shared.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_shared.vlum"), MakeWideDocument(512u)),
              "shared-load VLUM fixture registers");

        Failure failure;
        SpawnRequest first;
        SpawnRequest second;
        const u64 completedLoadsBefore = harness.streamer.GetStats().completedLoads;
        Check(harness.runtime.RequestSpawn({reference, resources::LoadPriority::Background}, first, &failure), "first shared spawn admits");
        Check(harness.runtime.RequestSpawn({reference, resources::LoadPriority::High}, second, &failure), "second shared spawn admits independently");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "shared spawns start resource loading");

        SpawnInfo firstInfo;
        SpawnInfo secondInfo;
        Check(harness.runtime.GetSpawnInfo(first, firstInfo, &failure) && harness.runtime.GetSpawnInfo(second, secondInfo, &failure) &&
                  firstInfo.spawn != secondInfo.spawn,
              "coalesced resource work still owns distinct Vellum spawn identities");
        Check(DriveToPartialMaterialization(harness.runtime, first, firstInfo) &&
                  firstInfo.state == SpawnState::Materializing && firstInfo.materializedNodes > 0u &&
                  firstInfo.materializedNodes < firstInfo.totalNodes && firstInfo.documentGeneration != 0u,
              "first shared-load spawn is explicitly in sliced Materializing before cancellation");
        const u32 sharedDocumentGeneration = firstInfo.documentGeneration;
        Check(harness.runtime.CancelSpawn(first, &failure), "one caller cancels without cancelling the shared resource for its peer");
        Check(harness.runtime.GetSpawnInfo(first, firstInfo, &failure) && firstInfo.state == SpawnState::Cancelled,
              "cancelled shared-load caller reaches Vellum terminal cancellation");
        Check(DriveSpawn(harness.runtime, second, secondInfo) && secondInfo.state == SpawnState::Succeeded &&
                  secondInfo.documentGeneration == sharedDocumentGeneration,
              "peer spawn completes on the same decoded generation after shared-load caller cancellation");
        Check(harness.streamer.GetStats().completedLoads == completedLoadsBefore + 1u,
              "two independent Vellum spawn interests complete exactly one underlying streamer load");
        ReleaseSpawnInterest(harness.runtime, first);
        VellumInstanceHandle instance;
        Check(harness.runtime.TakeSpawnResult(second, instance, &failure), "surviving shared-load spawn transfers its independent instance");
        Check(harness.runtime.ReleaseInstance(instance, &failure), "shared-load result instance releases");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "shared-load result crosses quarantine boundary");
        const RuntimeStats finalStats = harness.runtime.GetStats();
        Check(finalStats.cancelledSpawnOperations >= 1u && finalStats.successfulSpawnOperations >= 1u,
              "shared-load cancellation and surviving success are reflected in cumulative diagnostics");
        ShutdownHarness(harness);
    }

    void RunReplacementPin(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 2u;
        config.maximumConcurrentSpawns = 2u;
        config.maximumNodesPerDocument = 16u;
        config.maximumTotalNodes = 32u;
        config.maximumHierarchyDepth = 8u;
        config.maximumMaterializedNodesPerUpdate = 1u;
        Check(harness.Initialize(config), "replacement-pin harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/replacement.vlum");
        const std::vector<u8> firstBytes = MakeSingleSpacerDocument(10.0f);
        const std::vector<u8> secondBytes = MakeSingleSpacerDocument(20.0f);
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_replace_a.vlum"), firstBytes), "replacement generation A registers");

        Failure failure;
        SpawnRequest first;
        Check(harness.runtime.RequestSpawn({reference}, first, &failure), "generation A spawn admits");
        SpawnInfo firstInfo;
        Check(DriveToPartialMaterialization(harness.runtime, first, firstInfo),
              "generation A reaches sliced Materializing after exact strong-handle acquisition");
        const u32 firstGeneration = firstInfo.documentGeneration;
        Check(firstGeneration != 0u && firstInfo.materializedNodes > 0u && firstInfo.materializedNodes < firstInfo.totalNodes,
              "generation A exact document generation is pinned while staging remains incomplete");

        Check(harness.PublishLoose(reference, scratch.AddFilePath("vellum_replace_b.vlum"), secondBytes), "replacement loose descriptor publishes");
        resources::PipelineRequest replacement = harness.streamer.Replace(reference, resources::LoadPriority::High);
        Check(replacement.IsValid() && replacement.TryWait(10000u) && replacement.HasLoaded(), "replacement generation loads");
        resources::ResourceHandle replacementHandle = replacement.Acquire();
        Check(replacementHandle.IsValid() && replacementHandle.GetGeneration() != firstGeneration,
              "replacement publishes a distinct resource generation while generation A remains pinned");
        Check(DriveSpawn(harness.runtime, first, firstInfo) && firstInfo.state == SpawnState::Succeeded &&
                  firstInfo.documentGeneration == firstGeneration,
              "staged generation A finishes after replacement without changing its exact pin");

        SpawnRequest second;
        Check(harness.runtime.RequestSpawn({reference}, second, &failure), "generation B spawn admits");
        SpawnInfo secondInfo;
        Check(DriveSpawn(harness.runtime, second, secondInfo) && secondInfo.state == SpawnState::Succeeded &&
                  secondInfo.documentGeneration == replacementHandle.GetGeneration(),
              "new spawn observes replacement document generation");
        Check(harness.runtime.GetSpawnInfo(first, firstInfo, &failure) && firstInfo.documentGeneration == firstGeneration,
              "older successful spawn retains its exact pre-replacement generation");
        Check(harness.runtime.GetStats().liveDecodedDocuments >= 2u, "old and replacement decoded generations coexist while strongly owned");

        VellumInstanceHandle firstInstance;
        VellumInstanceHandle secondInstance;
        Check(harness.runtime.TakeSpawnResult(first, firstInstance, &failure), "generation A result transfers");
        Check(harness.runtime.TakeSpawnResult(second, secondInstance, &failure), "generation B result transfers");
        replacementHandle.Reset();
        replacement.Reset();

        const LayoutConstraints constraints{{0.0f, 0.0f}, {100.0f, 100.0f}};
        Size firstDesired;
        Size secondDesired;
        Check(harness.runtime.MeasureInstance(firstInstance, constraints, firstDesired, &failure) && firstDesired.width == 10.0f,
              "old instance still reads generation A authored content");
        Check(harness.runtime.MeasureInstance(secondInstance, constraints, secondDesired, &failure) && secondDesired.width == 20.0f,
              "replacement instance reads generation B authored content");
        Check(harness.runtime.ReleaseInstance(firstInstance, &failure) && harness.runtime.ReleaseInstance(secondInstance, &failure),
              "replacement proof instances release");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "replacement proof instances cross quarantine boundary");
        ShutdownHarness(harness);
    }

    void RunPendingDestroyUnregisterBarrier(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 1u;
        config.maximumConcurrentSpawns = 1u;
        config.maximumNodesPerDocument = 4u;
        config.maximumTotalNodes = 4u;
        config.maximumHierarchyDepth = 4u;
        config.maximumMaterializedNodesPerUpdate = 4u;
        Check(harness.Initialize(config), "pending-destroy unregister harness initializes");

        BlockingDestroyState state;
        Check(harness.streamer.RegisterDecoder(
                  {BlockingDestroyType, "blocking destroy proof", &DecodeBlockingDestroy, &DestroyBlockingResource, &state}),
              "blocking-destroy decoder registers");
        const resources::ResourceReference reference{resources::ResourcePath::FromString("vellum/blocking_destroy.bin"), BlockingDestroyType};
        const std::vector<u8> bytes{0x56u, 0x44u, 0x53u, 0x54u};
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_blocking_destroy.bin"), bytes),
              "blocking-destroy loose resource registers");

        resources::PipelineRequest request = harness.streamer.Request(reference, resources::LoadPriority::High);
        Check(request.IsValid() && request.TryWait(10000u) && request.HasLoaded(), "blocking-destroy resource loads");
        resources::ResourceHandle handle = request.Acquire();
        Check(handle.IsValid() && handle.GetType() == BlockingDestroyType, "blocking-destroy exact generation is strongly pinned");

        request.Reset();
        const resources::State afterInterestRelease = harness.registry.GetState(reference.GetPath());
        if (afterInterestRelease == resources::State::Loaded || afterInterestRelease == resources::State::Reloading)
            Check(harness.registry.Evict(reference.GetPath()), "request-first proof starts eviction while the strong generation remains pinned");
        Check(harness.registry.GetState(reference.GetPath()) == resources::State::Evicting,
              "request interest is gone before last-strong release and the published generation is Evicting");

        Check(!harness.streamer.UnregisterDecoder(BlockingDestroyType),
              "decoder unregister is busy while the Evicting generation is still strongly pinned");
        Check(harness.pipeline.GetStats().knownOperations == 0u,
              "first unregister attempt reclaims retired pipeline operation before pending-destruction race");

        ResourceHandleResetThread releaseThread(handle);
        releaseThread.InitThread();
        Check(releaseThread.IsValid(), "last-strong release thread starts");
        Check(state.destroyEntered.TryWait(10000u), "detached resource reaches blocking decoder destroy callback");
        Check(harness.registry.GetState(reference.GetPath()) == resources::State::Unloaded,
              "registry entry becomes Unloaded while detached destruction is still in flight");
        Check(!harness.streamer.UnregisterDecoder(BlockingDestroyType),
              "decoder unregister remains retryably busy while detached destroy callback is in flight");
        Check(harness.streamer.GetStats().registeredDecoders == 2u && harness.pipeline.HasLoader(BlockingDestroyType) &&
                  harness.registry.HasLoader(BlockingDestroyType),
              "failed in-flight unregister preserves streamer, pipeline, and registry callback ownership");

        state.releaseDestroy.Signal();
        releaseThread.JoinThread();
        Check(state.destroyed.GetValue() == 1u && !handle.IsValid(), "last-strong release completes exactly one resource destruction");
        WaitForResourceDrain(harness);
        Check(harness.streamer.UnregisterDecoder(BlockingDestroyType),
              "decoder unregister succeeds only after detached destruction callback returns");
        Check(harness.streamer.GetStats().registeredDecoders == 1u && !harness.pipeline.HasLoader(BlockingDestroyType) &&
                  !harness.registry.HasLoader(BlockingDestroyType),
              "successful retry removes the blocking decoder ownership chain");

        ShutdownHarness(harness);
    }

    void RunBusyDecoderShutdownRetry(const filesystem::AbsolutePath& scratch)
    {
        Harness harness;
        RuntimeConfig config = SmallConfig();
        config.maximumInstances = 1u;
        config.maximumConcurrentSpawns = 1u;
        config.maximumNodesPerDocument = 4u;
        config.maximumTotalNodes = 4u;
        config.maximumHierarchyDepth = 4u;
        config.maximumMaterializedNodesPerUpdate = 4u;
        Check(harness.Initialize(config), "busy-decoder shutdown harness initializes");
        const resources::ResourceReference reference = MakeReference("vellum/external_pin.vlum");
        Check(harness.RegisterLoose(reference, scratch.AddFilePath("vellum_external_pin.vlum"), MakeBasicDocument()),
              "external VLUM fixture registers");

        resources::PipelineRequest external = harness.streamer.Request(reference, resources::LoadPriority::High);
        Check(external.IsValid() && external.TryWait(10000u) && external.HasLoaded(), "external VLUM request loads through Vellum decoder");
        resources::ResourceHandle externalHandle = external.Acquire();
        Check(externalHandle.IsValid() && externalHandle.GetType() == DocumentResourceType, "external exact document generation is strongly pinned");

        Failure failure;
        Check(harness.runtime.Quiesce(&failure), "busy-decoder runtime quiesces without owning the external request");
        Check(harness.runtime.BeginSemanticUpdate(&failure), "busy-decoder quiescing update settles Vellum ownership");
        Check(harness.runtime.Drain(&failure), "busy-decoder runtime reaches Drained with no Vellum-owned work");
        Check(!harness.runtime.Shutdown(&failure) && failure.code == FailureCode::ResourceFailure,
              "decoder unregister is retryably busy while an external VLUM request/generation remains");
        Check(harness.streamer.GetStats().registeredDecoders == 1u && harness.pipeline.HasLoader(DocumentResourceType) &&
                  harness.registry.HasLoader(DocumentResourceType),
              "failed decoder unregister keeps all callback registration state alive");

        externalHandle.Reset();
        external.Reset();
        WaitForResourceDrain(harness);
        Check(harness.runtime.GetStats().liveDecodedDocuments == 0u, "external decoded generation retires before decoder unregister retry");
        Check(harness.runtime.Shutdown(&failure), "decoder unregister retry succeeds after external ownership drains");
        Check(harness.streamer.GetStats().registeredDecoders == 0u && !harness.pipeline.HasLoader(DocumentResourceType) &&
                  !harness.registry.HasLoader(DocumentResourceType),
              "successful retry removes streamer, pipeline, and registry decoder ownership");
        Check(harness.streamer.Shutdown(), "busy-decoder streamer shuts down");
        Check(harness.pipeline.Shutdown(), "busy-decoder pipeline shuts down");
        Check(harness.registry.Shutdown(), "busy-decoder registry shuts down");
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "vellumTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");

    const filesystem::AbsolutePath scratch = tests::ScratchDirectory("vellum_tests");
    Check(filesystem::Initialize({scratch, scratch, scratch}), "filesystem initialization");

    RunPrivateGenerationExhaustion();
    RunMutationSequenceWitness();
    RunValidSpawnLayoutProperty(scratch);
    RunGenerationQuarantine(scratch);
    RunStructuralSaturationAndNodeQuarantine(scratch);
    RunTopologyConflictOrdering(scratch);
    RunCollapsedWarmReveal(scratch);
    RunDeepAndCorruptDecode(scratch);
    RunLargeCardinalityAndWarmWork(scratch);
    RunSharedLoadCancellation(scratch);
    RunReplacementPin(scratch);
    RunPendingDestroyUnregisterBarrier(scratch);
    RunBusyDecoderShutdownRetry(scratch);

    Check(jobs::Shutdown(), "jobs shutdown");
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();

    if (g_failures == 0u)
        std::puts("[vellumTests] Vellum runtime-kernel proof checks passed");
    return g_failures == 0u ? 0 : 1;
}
