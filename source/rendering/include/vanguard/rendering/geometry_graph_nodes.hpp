#pragma once

#include <vanguard/rendering/render_node_graph.hpp>
#include <vanguard/rendering/render_phase.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;
    [[nodiscard]] bool AreGeometryGraphPipelinesReady(const FrameRenderer& renderer, bool gBufferOnly) noexcept;

    // One implementation for the fixed shader stages; each occurrence declares
    // its own dependencies. Frame sizes and descriptor indices are never cached.
    enum class GeometryComputeStage : u8
    {
        InitializeShells, Cull, CountWork, ScanWork, PrefixWork, ResolveWork,
        ScatterWork, CountBins, ScanBins, PrefixBins, ResolveBins,
        ScatterInstances, BuildArguments, Count
    };

    class RenderNodeDeclareGeometryWorkResources final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "DeclareGeometryWorkResources"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
    };

    class RenderNodeInitializeGeometryWork final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "InitializeGeometryWork"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeGeometryCompute final : public RenderNodeImpl
    {
    public:
        RenderNodeGeometryCompute(const FrameRenderer& renderer, GeometryComputeStage stage) noexcept;
        const char* GetName() const noexcept override;
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
        GeometryComputeStage m_stage;
        rhi::PipelineRef m_pipeline;
    };

    class RenderNodeBeginCameraDependencies final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "BeginCameraDependencies"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeEndCameraDependencies final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "EndCameraDependencies"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
    };

    class RenderNodeDeclareCommonResourceAllocs final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "DeclareCommonResourceAllocs"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::None; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override {}
    };

    class RenderNodeClearCameraTargets final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "ClearCameraTargets"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeDrawOpaqueDepth final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeDrawOpaqueDepth(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "DrawOpaqueDepth"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
        RenderPhaseId m_phase;
    };

    class RenderNodeDrawOpaqueGBuffer final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeDrawOpaqueGBuffer(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "DrawOpaqueGBuffer"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
        RenderPhaseId m_phase;
    };

    class RenderNodeDirectionalDiffuse final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeDirectionalDiffuse(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "DirectionalDiffuse"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
        rhi::PipelineRef m_pipeline;
    };

    class RenderNodeVisualizeGBuffer final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeVisualizeGBuffer(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "VisualizeGBuffer"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        rhi::PipelineRef m_pipeline;
    };

    class RenderNodeReduceGeometryDiagnostics final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeReduceGeometryDiagnostics(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "ReduceGeometryDiagnostics"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
    };

    class RenderNodeReadbackGeometryDiagnostics final : public RenderNodeImpl
    {
    public:
        const char* GetName() const noexcept override { return "ReadbackGeometryDiagnostics"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    };

    class RenderNodeComposeCameraOutput final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeComposeCameraOutput(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "ComposeCameraOutput"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        rhi::PipelineRef m_pipeline;
    };
}
