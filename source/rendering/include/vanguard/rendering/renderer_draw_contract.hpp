#pragma once

#include <vanguard/rendering/mesh_residency.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 StandardCameraMeshDrawPhaseCount = 2;

    [[nodiscard]] constexpr pipelines::AttachmentSignature MakeStandardDepthPrepassAttachments() noexcept
    {
        pipelines::AttachmentSignature attachments;
        attachments.depthStencilFormat = pipelines::Format::D32Float;
        attachments.depthStencilClass = pipelines::DepthStencilClass::Depth;
        attachments.sampleCount = 1;
        return attachments;
    }

    [[nodiscard]] constexpr pipelines::AttachmentSignature MakeStandardOpaqueAttachments() noexcept
    {
        pipelines::AttachmentSignature attachments;
        attachments.colors[0] = {pipelines::Format::R8G8B8A8UNorm, shaders::NumericClass::FloatingPoint};
        attachments.colors[1] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
        attachments.colors[2] = {pipelines::Format::R16G16B16A16Float, shaders::NumericClass::FloatingPoint};
        attachments.colorCount = 3;
        attachments.depthStencilFormat = pipelines::Format::D32Float;
        attachments.depthStencilClass = pipelines::DepthStencilClass::Depth;
        attachments.sampleCount = 1;
        return attachments;
    }

    inline constexpr MeshDrawPhaseContext StandardCameraMeshDrawPhases[StandardCameraMeshDrawPhaseCount]{
        {standardRenderPhases::DepthPrepass, MakeStandardDepthPrepassAttachments()},
        {standardRenderPhases::Opaque, MakeStandardOpaqueAttachments()},
    };
} // namespace vanguard::rendering
