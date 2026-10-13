#pragma once

#include <vanguard/rendering/render_node_graph.hpp>
#include <vanguard/rendering/render_phase.hpp>
#include <vanguard/rendering/viewport.hpp>

namespace vanguard::rendering
{
    class FrameRenderer;
    [[nodiscard]] bool AreGeometryGraphPipelinesReady(const FrameRenderer& renderer, bool gBufferOnly) noexcept;

    enum class CameraOutputResolveKind : u8
    {
        SceneLinearCopy,
        DisplaySrgb
    };

    /// Selects resolve behavior from the viewport lifecycle, neutral output encoding and
    /// actual acquired output format. DisplaySrgb is limited to supported non-sRGB SDR
    /// UNorm targets; presentation without an explicit supported transfer fails closed.
    [[nodiscard]] constexpr bool SelectCameraOutputResolve(const RenderViewportOutputKind outputKind,
                                                           const RenderOutputColorEncoding encoding, const rhi::Format format,
                                                           CameraOutputResolveKind& resolve) noexcept
    {
        if (encoding == RenderOutputColorEncoding::SceneLinear)
        {
            if (outputKind == RenderViewportOutputKind::Presentation || format == rhi::Format::Unknown ||
                format == rhi::Format::R8G8B8A8UNormSrgb ||
                format == rhi::Format::B8G8R8A8UNormSrgb)
                return false;
            resolve = CameraOutputResolveKind::SceneLinearCopy;
            return true;
        }
        if (encoding == RenderOutputColorEncoding::DisplaySrgb &&
            (format == rhi::Format::R8G8B8A8UNorm || format == rhi::Format::B8G8R8A8UNorm))
        {
            resolve = CameraOutputResolveKind::DisplaySrgb;
            return true;
        }
        return false;
    }

    // One implementation for the fixed shader stages; each occurrence declares
    // its own dependencies. Frame sizes and descriptor indices are never cached.
    enum class GeometryComputeStage : u8
    {
        InitializeShells, Cull, ExpandAppend, AllocateBins, ScatterInstances, Count
    };

    struct GeometryCpuTimingSample final
    {
        u64 initializeTicks = 0;
        u64 initializeUploadTicks = 0;
        u64 initializeClearTicks = 0;
        u32 initializeCalls = 0;
        u64 stageTicks[static_cast<u32>(GeometryComputeStage::Count)]{};
        u32 stageCalls[static_cast<u32>(GeometryComputeStage::Count)]{};
    };

    [[nodiscard]] GeometryCpuTimingSample ConsumeGeometryCpuTimingSample() noexcept;

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

    class RenderNodeDrawTransparent final : public RenderNodeImpl
    {
    public:
        explicit RenderNodeDrawTransparent(const FrameRenderer& renderer) noexcept;
        const char* GetName() const noexcept override { return "DrawTransparent"; }
        RenderNodeCommandListUsage GetCommandListUsage() const noexcept override { return RenderNodeCommandListUsage::Require; }
        bool DeclareResources(const RenderNodeImplContext&, RenderFlowResourceFailure*) const noexcept override;
        void Execute(const RenderNodeImplContext&, jobs::Builder*) const override;
    private:
        const FrameRenderer& m_renderer;
        RenderPhaseId m_phase;
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
        rhi::PipelineRef m_sceneLinearPipeline;
        rhi::PipelineRef m_displaySrgbPipeline;
    };
}
