#pragma once

#include <vanguard/rendering/render_node_graph.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;

    // The caller owns the imported source and its populated descriptor, the clamp
    // sampler descriptor, and the prepared pipeline through joined GPU retirement.
    // Descriptor domains must match those used to prepare FullscreenCopy.
    struct FullscreenCopyDesc
    {
        containers::StringView sourceName;
        containers::StringView targetName;
        rhi::TextureRef source;
        rhi::DescriptorHandle sourceDescriptor;
        rhi::DescriptorHandle samplerDescriptor;
        f32 uvScale[2]{1.0f, 1.0f};
        f32 uvOffset[2]{};
    };

    class RenderNodeFullscreenCopy final : public RenderNodeImpl
    {
    public:
        RenderNodeFullscreenCopy(const FrameRenderer& renderer, const FullscreenCopyDesc& desc) noexcept;
        [[nodiscard]] const char* GetName() const noexcept override { return "FullscreenCopy"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override;
        void Execute(const RenderNodeImplContext& context, jobs::Builder*) const override;

    private:
        containers::String m_sourceName{memory::pools::Rendering::GetInstance()};
        containers::String m_targetName{memory::pools::Rendering::GetInstance()};
        rhi::PipelineRef m_pipeline;
        rhi::TextureRef m_source;
        struct Constants
        {
            u32 sourceDescriptor;
            u32 samplerDescriptor;
            f32 uvScale[2];
            f32 uvOffset[2];
        } m_constants{};
        static_assert(sizeof(Constants) == 24);
    };

    // These frame-global nodes preserve graph topology while their renderer-specific bodies remain intentionally empty.
    class RenderNodeStartRender final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "StartRender"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        [[nodiscard]] bool GetJobBuilderUsage() const noexcept override { return true; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeEndRender final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "EndRender"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodePresent final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "Present"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeFlushTextureGrabs final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "FlushTextureGrabs"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeFlushBufferGrabs final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "FlushBufferGrabs"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeCleanupBatchDataAllocator final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "CleanupBatchDataAllocator"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeEndFrame final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "EndFrame"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeDeclareCommonResourceAllocsFinalOnly final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "DeclCommonResAllocs"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodePrepareBlankRendering final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "PrepSceneRendering"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeRenderSkyScattering final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "RenderSkyScattering"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeReflectionProbes final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "ReflectionProbes"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeGlobalIllumination final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "GlobalIllumination"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeVolumetricFog final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "VolumetricFog"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeClearFinalColorTarget final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "ClearFinalColorTarget"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeDrawComposition final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "DrawComposition"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeCompositionPostProcess final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "CompositionPostProcess"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeDrawHud final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "DrawHUD"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeFullscreenVideo final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "FullscreenVideo"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeRenderFinal2D final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "RenderFinal2D"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeExtractionFinalColor final : public RenderNodeImpl
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override { return "ExtractionFinalColor"; }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeSynchronize final : public RenderNodeImpl
    {
    public:
        RenderNodeSynchronize(rhi::CommandListSyncType sync, const char* name = nullptr);

        [[nodiscard]] const char* GetName() const noexcept override { return m_name.AsChar(); }
        [[nodiscard]] RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Sync; }
        [[nodiscard]] bool GetJobBuilderUsage() const noexcept override { return true; }
        [[nodiscard]] bool DeclareResources(const RenderNodeImplContext& context, RenderFlowResourceFailure* failure) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;

    private:
        rhi::CommandListSyncType m_sync = rhi::CommandListSyncType::None;
        containers::String m_name;
    };
} // namespace vanguard::rendering
