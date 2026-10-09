#include <vanguard/rendering/render_graph_scheduler_probe.hpp>

#include <vanguard/rendering/render_node_graph_factory.hpp>
#include <vanguard/rendering/render_node_impl_context.hpp>
#include <vanguard/rendering/viewport.hpp>
#include <vanguard/system/assert.hpp>

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace vanguard::rendering
{
    namespace
    {
#if VG_BUILD_PROFILE
        [[nodiscard]] bool ResolveProbeEnabled() noexcept
        {
#if defined(_MSC_VER)
            char* requested = nullptr;
            std::size_t requestedLength = 0;
            const bool enabled = _dupenv_s(&requested, &requestedLength, "VANGUARD_RENDER_GRAPH_SCHEDULER_PROBE") == 0 &&
                                 requested != nullptr && std::strcmp(requested, "1") == 0;
            std::free(requested);
            return enabled;
#else
            const char* const requested = std::getenv("VANGUARD_RENDER_GRAPH_SCHEDULER_PROBE");
            return requested != nullptr && std::strcmp(requested, "1") == 0;
#endif
        }

        struct alignas(64) ProbeSink
        {
            std::atomic<u64> value{0};
        };

        inline constexpr u32 ProbeSinkCount = 96;
        inline constexpr u32 ProbeResourceCount = 320;
        inline constexpr u32 ProbeTextureCount = 240;
        inline constexpr u32 ProbeRedMixResourceCount = 64;
        inline constexpr u32 ProbeRedMixTextureCount = 48;
        inline constexpr u32 ProbeResourcePhaseCount = 10;
        inline constexpr u32 ProbeUsesPerPhase = 64;
        std::array<ProbeSink, ProbeSinkCount> g_probeSinks;

        struct ProbeResourceWorkload
        {
            u32 resourceCount = ProbeResourceCount;
            u32 textureCount = ProbeTextureCount;
        };

        [[nodiscard]] const ProbeResourceWorkload& ActiveProbeResourceWorkload() noexcept
        {
            static const ProbeResourceWorkload workload = []() noexcept
            {
                // Keep the established 320-resource allocation-ceiling probe as
                // the Profile default. RF2 profiling can opt into the separate
                // 64-resource RED-like mix without changing production builds
                // or reinterpreting the first phase of the 320 workload.
                bool redMix = false;
#if defined(_MSC_VER)
                char* requested = nullptr;
                std::size_t requestedLength = 0;
                if (_dupenv_s(&requested, &requestedLength, "VANGUARD_RF_PROBE_RESOURCES") == 0 && requested != nullptr)
                {
                    redMix = std::strcmp(requested, "64") == 0;
                    std::free(requested);
                }
#else
                const char* const requested = std::getenv("VANGUARD_RF_PROBE_RESOURCES");
                redMix = requested != nullptr && std::strcmp(requested, "64") == 0;
#endif
                return redMix ? ProbeResourceWorkload{ProbeRedMixResourceCount, ProbeRedMixTextureCount} : ProbeResourceWorkload{};
            }();
            return workload;
        }

        struct ProbeResourceCatalog
        {
            std::array<std::array<char, 40>, ProbeResourceCount> storage{};
            std::array<LogicalResourceName, ProbeResourceCount> names{};

            ProbeResourceCatalog() noexcept
            {
                for (u32 index = 0; index < ProbeResourceCount; ++index)
                {
                    std::snprintf(storage[index].data(), storage[index].size(), "SchedulerProbe/Resource%03u", index);
                    names[index] = LogicalResourceName(storage[index].data());
                }
            }
        };

        [[nodiscard]] const LogicalResourceName& ProbeResourceName(const u32 index) noexcept
        {
            static const ProbeResourceCatalog catalog;
            VG_ASSERT(index < ProbeResourceCount);
            return catalog.names[index];
        }

        [[nodiscard]] bool ProbeResourceIsTexture(const u32 index) noexcept
        {
            return index < ActiveProbeResourceWorkload().textureCount;
        }

        [[nodiscard]] bool ProbeTextureIsCompute(const u32 index) noexcept
        {
            VG_ASSERT(ProbeResourceIsTexture(index));
            // The RED-ceiling probe distributes synthetic uses over Vanguard's
            // existing geometry-compute writers. Keep every synthetic texture
            // legal on that queue so the test measures allocator/resolve scale,
            // not an artificial cross-queue capability failure.
            return true;
        }

        [[nodiscard]] FrameTextureDesc ProbeTextureDesc(const u32 index) noexcept
        {
            FrameTextureDesc desc;
            const u32 side = 64u + (index % 4u) * 16u;
            desc.active.extent = {side, side, 1};
            desc.maximumExtent = desc.active.extent;
            desc.active.dimension = rhi::TextureDimension::Texture2D;
            desc.active.format = rhi::Format::R8G8B8A8UNorm;
            desc.active.mipCount = 1;
            desc.maximumMipCount = 1;
            desc.active.arraySize = 1;
            desc.active.sampleCount = 1;
            desc.active.usage = rhi::TextureUsage::ShaderResource |
                                (ProbeTextureIsCompute(index) ? rhi::TextureUsage::UnorderedAccess : rhi::TextureUsage::RenderTarget);
            desc.active.initialState = rhi::ResourceState::Common;
            desc.active.keepInitialState = false;
            desc.initialization = FrameResourceInitialization::Undefined;
            return desc;
        }

        [[nodiscard]] FrameBufferDesc ProbeBufferDesc(const u32 index) noexcept
        {
            FrameBufferDesc desc;
            desc.active.size = 64u * 1024u + static_cast<u64>(index % 4u) * 16u * 1024u;
            desc.maximumSize = desc.active.size;
            desc.active.structureStride = 16;
            desc.active.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess;
            desc.active.initialState = rhi::ResourceState::Common;
            desc.active.keepInitialState = false;
            desc.initialization = FrameResourceInitialization::Undefined;
            return desc;
        }

        void ProbeResourceUse(const RenderNodeImplContext& context, const u32 index, const bool write, const bool computeRead) noexcept
        {
            const RenderFlowNameTag tag = RenderNodeImplContext::RTSharedNameTag(ProbeResourceName(index));
            if (ProbeResourceIsTexture(index))
            {
                TextureUseDesc use;
                use.requiredState = write ? rhi::ResourceState::UnorderedAccess
                                          : (computeRead ? rhi::ResourceState::ShaderResourceCompute : rhi::ResourceState::ShaderResourceGraphics);
                use.subresources = {0, 1, 0, 1};
                use.access = write ? LogicalAccessIntent::Write : LogicalAccessIntent::Read;
                use.content = write ? ResourceContentIntent::Discard : ResourceContentIntent::Preserve;
                context.RTUseBegin(tag, use);
            }
            else
            {
                BufferUseDesc use;
                use.requiredState = write ? rhi::ResourceState::UnorderedAccess
                                          : (computeRead ? rhi::ResourceState::ShaderResourceCompute : rhi::ResourceState::ShaderResourceGraphics);
                use.access = write ? LogicalAccessIntent::Write : LogicalAccessIntent::Read;
                use.content = write ? ResourceContentIntent::Discard : ResourceContentIntent::Preserve;
                context.RTUseBegin(tag, use);
            }
            context.RTUseEnd(tag);
        }

        struct ProbeNodeSpec
        {
            const char* name = nullptr;
            u32 iterations = 0;
        };

        struct ProbeGroup
        {
            RenderNodeGraph::ItemId input = RenderNodeGraph::InvalidItemId;
            RenderNodeGraph::ItemId output = RenderNodeGraph::InvalidItemId;
        };

        class RenderNodeSchedulerProbe final : public RenderNodeImpl
        {
        public:
            RenderNodeSchedulerProbe(const char* const name, const u32 iterations, const u32 sinkIndex, const u64 seed) noexcept
                : m_name(name), m_iterations(iterations), m_sinkIndex(sinkIndex), m_seed(seed)
            {
                VG_ASSERT(m_name != nullptr);
                VG_ASSERT(m_sinkIndex < ProbeSinkCount);
            }

            [[nodiscard]] const char* GetName() const noexcept override { return m_name; }
            [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }

            void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override
            {
                // Intentionally dependency-heavy integer work. The result is
                // externally observable through a node-private atomic sink, so
                // optimized Profile builds cannot fold the loop away. Using
                // the frame serial and worker index also prevents a constant
                // result across frames without introducing shared hot writes.
                u64 state = m_seed ^ context.GetFrameInfo().GetSerial() ^
                            (static_cast<u64>(context.GetDispatcherThreadIndex()) << 32u);
                for (u32 iteration = 0; iteration < m_iterations; ++iteration)
                {
                    state ^= state >> 12u;
                    state ^= state << 25u;
                    state ^= state >> 27u;
                    state *= 0x2545f4914f6cdd1dull;
                    state += 0x9e3779b97f4a7c15ull + static_cast<u64>(iteration);
                }
                g_probeSinks[m_sinkIndex].value.store(state, std::memory_order_relaxed);
            }

        private:
            const char* m_name = nullptr;
            u32 m_iterations = 0;
            u32 m_sinkIndex = 0;
            u64 m_seed = 0;
        };

        [[nodiscard]] RenderNodeGraph::ItemId AddProbeNode(NodeGraphFactory& factory, const char* const name,
                                                            const u32 iterations, const u32 sinkIndex) noexcept
        {
            const u64 seed = 0x6a09e667f3bcc909ull ^ (static_cast<u64>(sinkIndex + 1u) * 0x9e3779b97f4a7c15ull);
            return factory.Create<RenderNodeSchedulerProbe>(NodeGroupId::None, RenderNodeType::Stage, RenderNodeSubtype(0),
                                                            name, iterations, sinkIndex, seed);
        }

        void LinkCpu(NodeGraphFactory& factory, const RenderNodeGraph::ItemId parent, const RenderNodeGraph::ItemId child) noexcept
        {
            factory.Link(parent, child, RenderNodeDependencyType::Cpu);
        }
#endif
    } // namespace

    bool IsRenderGraphSchedulerProbeEnabled() noexcept
    {
#if VG_BUILD_PROFILE
        static const bool enabled = ResolveProbeEnabled();
        return enabled;
#else
        return false;
#endif
    }

    void AppendRenderGraphSchedulerProbe(NodeGraphFactory& factory, const RenderNodeGraph::ItemId startRender,
                                         const RenderNodeGraph::ItemId endRender) noexcept
    {
#if VG_BUILD_PROFILE
        if (!IsRenderGraphSchedulerProbeEnabled())
            return;
        VG_ASSERT(startRender != RenderNodeGraph::InvalidItemId);
        VG_ASSERT(endRender != RenderNodeGraph::InvalidItemId);

        u32 sinkIndex = 0;
        const auto add = [&](const char* const name, const u32 iterations)
        {
            return AddProbeNode(factory, name, iterations, sinkIndex++);
        };
        const auto group = [&](const char* const inputName, const char* const outputName)
        {
            ProbeGroup result;
            result.input = add(inputName, 0);
            result.output = add(outputName, 0);
            return result;
        };
        const auto addGroupMember = [&](const ProbeGroup& target, const char* const name, const u32 iterations)
        {
            const auto member = add(name, iterations);
            LinkCpu(factory, target.input, member);
            LinkCpu(factory, member, target.output);
            return member;
        };

        // Representative camera-graph scale: ten dependency groups, matching
        // the broad culling/preparation/render/particle/post-render shape of a
        // large production renderer. Boundary nodes intentionally do no useful
        // work; they model dependency-group scheduling overhead rather than
        // inflating the CPU workload.
        const ProbeGroup cullingScene = group("SchedulerProbe/Group/CullingScene/In", "SchedulerProbe/Group/CullingScene/Out");
        const ProbeGroup cullingRayTracing = group("SchedulerProbe/Group/CullingRayTracing/In", "SchedulerProbe/Group/CullingRayTracing/Out");
        const ProbeGroup cullingCascades = group("SchedulerProbe/Group/CullingCascades/In", "SchedulerProbe/Group/CullingCascades/Out");
        const ProbeGroup cullingLocalShadows = group("SchedulerProbe/Group/CullingLocalShadows/In", "SchedulerProbe/Group/CullingLocalShadows/Out");
        const ProbeGroup postCullingDebug = group("SchedulerProbe/Group/PostCullingDebug/In", "SchedulerProbe/Group/PostCullingDebug/Out");
        const ProbeGroup renderPrep = group("SchedulerProbe/Group/RenderPrep/In", "SchedulerProbe/Group/RenderPrep/Out");
        const ProbeGroup render = group("SchedulerProbe/Group/Render/In", "SchedulerProbe/Group/Render/Out");
        const ProbeGroup particlesOnScreen = group("SchedulerProbe/Group/ParticlesOnScreen/In", "SchedulerProbe/Group/ParticlesOnScreen/Out");
        const ProbeGroup particlesOffScreen = group("SchedulerProbe/Group/ParticlesOffScreen/In", "SchedulerProbe/Group/ParticlesOffScreen/Out");
        const ProbeGroup postRender = group("SchedulerProbe/Group/PostRender/In", "SchedulerProbe/Group/PostRender/Out");

        // An intentionally empty ray-tracing group still has a dependency
        // boundary in the representative non-ray-traced path.
        LinkCpu(factory, cullingRayTracing.input, cullingRayTracing.output);

        const auto prepDistantShadows = add("SchedulerProbe/PrepDistantShadows", 50000);
        const auto prepareRainMap = add("SchedulerProbe/PrepareRainMap", 35000);
        const auto windUpdateAndRainMap = add("SchedulerProbe/WindUpdateAndRainMap", 55000);
        const auto cameraStart = add("SchedulerProbe/CameraStart", 12000);
        const auto prepCollector = add("SchedulerProbe/PrepCollector", 70000);
        const auto debugCollectHigh = add("SchedulerProbe/DebugCollectHigh", 20000);

        LinkCpu(factory, startRender, prepDistantShadows);
        LinkCpu(factory, startRender, prepareRainMap);
        LinkCpu(factory, prepareRainMap, windUpdateAndRainMap);
        LinkCpu(factory, startRender, cameraStart);
        LinkCpu(factory, cameraStart, prepCollector);
        LinkCpu(factory, prepCollector, debugCollectHigh);
        LinkCpu(factory, debugCollectHigh, cullingScene.input);
        LinkCpu(factory, debugCollectHigh, cullingRayTracing.input);
        LinkCpu(factory, debugCollectHigh, cullingCascades.input);

        static_cast<void>(addGroupMember(cullingScene, "SchedulerProbe/DoCullingScene", 140000));
        static_cast<void>(addGroupMember(cullingCascades, "SchedulerProbe/DoCullingCascades", 120000));
        static_cast<void>(addGroupMember(cullingLocalShadows, "SchedulerProbe/DoCullingLocalShadows", 120000));
        LinkCpu(factory, cullingScene.output, cullingLocalShadows.input);

        const auto flushVisibilityDebug = addGroupMember(postCullingDebug, "SchedulerProbe/FlushVisibilityDebug", 18000);
        const auto debugCollectLow = addGroupMember(postCullingDebug, "SchedulerProbe/DebugCollectLow", 18000);
        LinkCpu(factory, flushVisibilityDebug, debugCollectLow);
        LinkCpu(factory, cullingRayTracing.output, postCullingDebug.input);
        LinkCpu(factory, cullingLocalShadows.output, postCullingDebug.input);
        LinkCpu(factory, cullingCascades.output, postCullingDebug.input);

        std::array<RenderNodeGraph::ItemId, 4> onScreenParticles{};
        for (u32 index = 0; index < onScreenParticles.size(); ++index)
        {
            static constexpr const char* Names[]{
                "SchedulerProbe/SimulateOnScreenParticles0", "SchedulerProbe/SimulateOnScreenParticles1",
                "SchedulerProbe/SimulateOnScreenParticles2", "SchedulerProbe/SimulateOnScreenParticles3"};
            onScreenParticles[index] = addGroupMember(particlesOnScreen, Names[index], 70000);
        }
        LinkCpu(factory, cullingScene.output, particlesOnScreen.input);
        LinkCpu(factory, cullingCascades.output, particlesOnScreen.input);

        const auto declareCommonResources = addGroupMember(renderPrep, "SchedulerProbe/DeclareCommonResourceAllocs", 22000);
        const auto prepareRendering = addGroupMember(renderPrep, "SchedulerProbe/RenderPrep", 90000);
        LinkCpu(factory, declareCommonResources, prepareRendering);
        LinkCpu(factory, cullingRayTracing.output, renderPrep.input);
        LinkCpu(factory, cullingLocalShadows.output, renderPrep.input);
        LinkCpu(factory, cullingCascades.output, renderPrep.input);
        LinkCpu(factory, prepDistantShadows, renderPrep.input);
        LinkCpu(factory, prepareRainMap, renderPrep.input);

        LinkCpu(factory, renderPrep.output, render.input);
        LinkCpu(factory, postCullingDebug.output, render.input);
        LinkCpu(factory, particlesOnScreen.output, render.input);

        constexpr std::array<ProbeNodeSpec, 26> RenderWork{{
            {"SchedulerProbe/PrepareGloballyAccessibleSystems", 65000},
            {"SchedulerProbe/DepthPrepass", 90000},
            {"SchedulerProbe/GBufferEarlyVelocityWeapon", 60000},
            {"SchedulerProbe/GBufferSolid", 95000},
            {"SchedulerProbe/GBufferDiscard", 90000},
            {"SchedulerProbe/GBufferVelocitySolid", 65000},
            {"SchedulerProbe/GBufferVelocityDiscard", 65000},
            {"SchedulerProbe/DeferredDecals", 70000},
            {"SchedulerProbe/HairOpaque", 75000},
            {"SchedulerProbe/PostGBufferOverrides", 50000},
            {"SchedulerProbe/RenderCascade0", 80000},
            {"SchedulerProbe/RenderCascade1", 80000},
            {"SchedulerProbe/RenderCascade2", 80000},
            {"SchedulerProbe/RenderLocalShadow0", 80000},
            {"SchedulerProbe/RenderLocalShadow1", 80000},
            {"SchedulerProbe/RenderLocalShadow2", 80000},
            {"SchedulerProbe/Lighting", 110000},
            {"SchedulerProbe/DecoupledParticleLighting", 60000},
            {"SchedulerProbe/SkinPostForwardOpaque", 90000},
            {"SchedulerProbe/HologramDepthDistortion", 55000},
            {"SchedulerProbe/Transparents", 95000},
            {"SchedulerProbe/TransparentsWeaponScreen", 70000},
            {"SchedulerProbe/TXAABlurs", 65000},
            {"SchedulerProbe/ForwardNoTXAA", 75000},
            {"SchedulerProbe/PostFX", 95000},
            {"SchedulerProbe/FinalizeHUD", 50000},
        }};
        for (const ProbeNodeSpec& work : RenderWork)
            static_cast<void>(addGroupMember(render, work.name, work.iterations));

        const auto simulateOffScreenParticles = addGroupMember(particlesOffScreen, "SchedulerProbe/SimulateOffScreenParticles", 80000);
        static_cast<void>(simulateOffScreenParticles);
        LinkCpu(factory, cullingScene.output, particlesOffScreen.input);
        LinkCpu(factory, cullingCascades.output, particlesOffScreen.input);

        static_cast<void>(addGroupMember(postRender, "SchedulerProbe/RenderFinal2D", 50000));
        LinkCpu(factory, postCullingDebug.output, postRender.input);
        LinkCpu(factory, render.output, postRender.input);

        // The five-node lane is deliberately much lighter than the wide Render
        // group. Its end-to-next-start gaps are the dependency-handoff sample
        // while unrelated render jobs keep the worker pool occupied.
        const auto hotSerial0 = add("SchedulerProbe/HotSerial0", 12000);
        const auto hotSerial1 = add("SchedulerProbe/HotSerial1", 12000);
        const auto hotSerial2 = add("SchedulerProbe/HotSerial2", 12000);
        const auto hotSerial3 = add("SchedulerProbe/HotSerial3", 12000);
        const auto hotSerial4 = add("SchedulerProbe/HotSerial4", 12000);
        LinkCpu(factory, renderPrep.output, hotSerial0);
        LinkCpu(factory, hotSerial0, hotSerial1);
        LinkCpu(factory, hotSerial1, hotSerial2);
        LinkCpu(factory, hotSerial2, hotSerial3);
        LinkCpu(factory, hotSerial3, hotSerial4);
        LinkCpu(factory, hotSerial4, postRender.input);

        LinkCpu(factory, postRender.output, endRender);
        LinkCpu(factory, particlesOffScreen.output, endRender);
        LinkCpu(factory, windUpdateAndRainMap, endRender);

        VG_ASSERT(sinkIndex == 70);
#else
        static_cast<void>(factory);
        static_cast<void>(startRender);
        static_cast<void>(endRender);
#endif
    }

    bool UseRenderGraphSchedulerProbeResources(const RenderNodeImplContext& context, const u32 phase,
                                               RenderFlowResourceFailure* const failure) noexcept
    {
#if VG_BUILD_PROFILE
        if (!IsRenderGraphSchedulerProbeEnabled())
            return true;
        static constexpr const char* PhaseFailureMessages[ProbeResourcePhaseCount] = {
            "scheduler probe phase 00 exceeded writer capacity", "scheduler probe phase 01 exceeded writer capacity",
            "scheduler probe phase 02 exceeded writer capacity", "scheduler probe phase 03 exceeded writer capacity",
            "scheduler probe phase 04 exceeded writer capacity", "scheduler probe phase 05 exceeded writer capacity",
            "scheduler probe phase 06 exceeded writer capacity", "scheduler probe phase 07 exceeded writer capacity",
            "scheduler probe phase 08 exceeded writer capacity", "scheduler probe phase 09 exceeded writer capacity"};
        VG_ASSERT(phase < ProbeResourcePhaseCount);
        const ProbeResourceWorkload& workload = ActiveProbeResourceWorkload();
        const u32 firstEvent = phase * ProbeUsesPerPhase;
        const u32 totalEvents = workload.resourceCount * 2u;
        if (firstEvent >= totalEvents)
            return true;
        const u32 phaseEnd = firstEvent + ProbeUsesPerPhase;
        const u32 endEvent = phaseEnd < totalEvents ? phaseEnd : totalEvents;
        VG_ASSERT(endEvent <= ProbeResourceCount * 2u);
        const bool computeRead = true;
        for (u32 event = firstEvent; event < endEvent; ++event)
        {
            const u32 index = event % workload.resourceCount;
            const bool write = event < workload.resourceCount;
            if (write)
            {
                if (ProbeResourceIsTexture(index))
                    static_cast<void>(context.RTSharedAlloc(ProbeResourceName(index), ProbeTextureDesc(index)));
                else
                    static_cast<void>(context.RTSharedAlloc(ProbeResourceName(index), ProbeBufferDesc(index)));
                if (failure != nullptr && failure->code != RenderFlowResourceFailureCode::None)
                    return false;
            }
            ProbeResourceUse(context, index, write, computeRead);
            if (failure != nullptr && failure->code != RenderFlowResourceFailureCode::None)
            {
                failure->message = PhaseFailureMessages[phase];
                return false;
            }
        }
        if (failure != nullptr && failure->code != RenderFlowResourceFailureCode::None)
            return false;
#else
        static_cast<void>(context);
        static_cast<void>(phase);
        static_cast<void>(failure);
#endif
        return true;
    }
} // namespace vanguard::rendering
