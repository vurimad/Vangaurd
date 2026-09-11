#include <vanguard/rendering/mesh_draw_layout.hpp>

#include <vanguard/meshes/meshes.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/rhi/rhi_types.hpp>

#include <cstring>

namespace vanguard::rendering
{
    namespace
    {
        pipelines::Format Format(const meshes::VertexFormat format) noexcept
        {
            switch (format)
            {
#define VG_MESH_FORMAT(name)                                                                                                                                                                                     \
    case meshes::VertexFormat::name:                                                                                                                                                                             \
        return pipelines::Format::name
                VG_MESH_FORMAT(R32Float);
                VG_MESH_FORMAT(R32G32Float);
                VG_MESH_FORMAT(R32G32B32Float);
                VG_MESH_FORMAT(R32G32B32A32Float);
                VG_MESH_FORMAT(R16G16Float);
                VG_MESH_FORMAT(R16G16B16A16Float);
                VG_MESH_FORMAT(R16G16SNorm);
                VG_MESH_FORMAT(R16G16B16A16SNorm);
                VG_MESH_FORMAT(R16G16UNorm);
                VG_MESH_FORMAT(R16G16B16A16UNorm);
                VG_MESH_FORMAT(R8G8B8A8UNorm);
                VG_MESH_FORMAT(R8G8B8A8SNorm);
                VG_MESH_FORMAT(R8G8B8A8UInt);
                VG_MESH_FORMAT(R16G16B16A16UInt);
                VG_MESH_FORMAT(R32UInt);
                VG_MESH_FORMAT(R10G10B10A2UNorm);
#undef VG_MESH_FORMAT
            }
            return pipelines::Format::Unknown;
        }

        // The static-surface prefix consumes signed directions, except for the
        // RuntimeStatic 10-bit UNORM encoding decoded by geometry flags. Other
        // integer/unsigned direction encodings need an explicit shader contract.
        bool SupportsStaticSurfaceAttribute(const meshes::VertexStream& stream) noexcept
        {
            using meshes::VertexFormat;
            using meshes::VertexSemantic;
            switch (stream.semantic)
            {
            case VertexSemantic::Position:
                return stream.semanticIndex == 0 &&
                       (stream.format == VertexFormat::R32G32B32Float || stream.format == VertexFormat::R16G16B16A16SNorm);
            case VertexSemantic::Normal:
            case VertexSemantic::Tangent:
                return stream.semanticIndex == 0 &&
                       (stream.format == VertexFormat::R32G32B32Float || stream.format == VertexFormat::R32G32B32A32Float ||
                        stream.format == VertexFormat::R16G16B16A16Float || stream.format == VertexFormat::R16G16B16A16SNorm ||
                        stream.format == VertexFormat::R8G8B8A8SNorm || stream.format == VertexFormat::R10G10B10A2UNorm);
            case VertexSemantic::TexCoord:
                return stream.semanticIndex == 0 &&
                       (stream.format == VertexFormat::R32G32Float || stream.format == VertexFormat::R32G32B32Float ||
                        stream.format == VertexFormat::R32G32B32A32Float || stream.format == VertexFormat::R16G16Float ||
                        stream.format == VertexFormat::R16G16B16A16Float || stream.format == VertexFormat::R16G16SNorm ||
                        stream.format == VertexFormat::R16G16B16A16SNorm || stream.format == VertexFormat::R16G16UNorm ||
                        stream.format == VertexFormat::R16G16B16A16UNorm);
            default:
                return true;
            }
        }

        const char* Semantic(const meshes::VertexSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case meshes::VertexSemantic::Position:
                return "POSITION";
            case meshes::VertexSemantic::Normal:
                return "NORMAL";
            case meshes::VertexSemantic::Tangent:
                return "TANGENT";
            case meshes::VertexSemantic::TexCoord:
                return "TEXCOORD";
            case meshes::VertexSemantic::Color:
                return "COLOR";
            default:
                return nullptr;
            }
        }
    } // namespace

    bool ValidateMeshDrawLayout(const meshes::MeshFile& mesh, const u32 sourceSubmesh, const pipelines::PipelineFile& pipeline) noexcept
    {
        if (!mesh.IsOpen() || mesh.GetKind() != meshes::MeshKind::Static || sourceSubmesh >= mesh.GetSubmeshes().Size() || !pipeline.IsOpen() || pipeline.GetKind() != pipelines::PipelineKind::Graphics ||
            pipeline.GetGraphics().topology != pipelines::PrimitiveTopology::TriangleList)
            return false;
        const auto& submesh = mesh.GetSubmeshes()[sourceSubmesh];
        if (submesh.topology != meshes::PrimitiveTopology::TriangleList || submesh.vertexLayout >= mesh.GetVertexLayouts().Size())
            return false;
        const auto& layout = mesh.GetVertexLayouts()[submesh.vertexLayout];
        const containers::ArraySpan<const meshes::VertexStream> streams{mesh.GetVertexStreams().Data() + layout.firstStream, layout.streamCount};
        if (streams.Empty() || streams.Size() > rhi::MaximumVertexAttributes || pipeline.GetVertexAttributes().Size() > rhi::MaximumVertexAttributes ||
            pipeline.GetVertexStreams().Size() > rhi::MaximumVertexBindings)
            return false;
        const pipelines::VertexStream* bindings[rhi::MaximumVertexBindings]{};
        bool instanceBinding = false;
        for (const auto& binding : pipeline.GetVertexStreams())
        {
            if (binding.binding >= rhi::MaximumVertexBindings || bindings[binding.binding] != nullptr)
                return false;
            bindings[binding.binding] = &binding;
            if (binding.binding == MeshDrawInstanceBinding)
            {
                if (binding.inputRate != pipelines::InputRate::PerInstance || binding.stride != sizeof(MeshDrawInstance) || binding.instanceStepRate != 1)
                    return false;
                instanceBinding = true;
            }
            else if (binding.inputRate != pipelines::InputRate::PerVertex)
                return false;
        }
        for (const auto& stream : streams)
            if (stream.binding >= MeshDrawInstanceBinding)
                return false;
        bool position = false;
        u32 instanceFields = 0;
        for (const auto& attribute : pipeline.GetVertexAttributes())
        {
            if (attribute.streamBinding >= rhi::MaximumVertexBindings || bindings[attribute.streamBinding] == nullptr)
                return false;
            if (attribute.streamBinding == MeshDrawInstanceBinding)
            {
                if (std::strcmp(attribute.semanticName, "VG_DRAW") != 0 || attribute.semanticIndex > 1 || attribute.byteOffset != attribute.semanticIndex * 8 ||
                    attribute.format != pipelines::Format::R32G32UInt || (instanceFields & (1u << attribute.semanticIndex)) != 0)
                    return false;
                instanceFields |= 1u << attribute.semanticIndex;
                continue;
            }
            bool matched = false;
            for (const auto& stream : streams)
            {
                const char* name = Semantic(stream.semantic);
                if (name == nullptr || std::strcmp(attribute.semanticName, name) != 0 || attribute.semanticIndex != stream.semanticIndex)
                    continue;
                if (attribute.format != Format(stream.format) || attribute.streamBinding != stream.binding || attribute.byteOffset != stream.byteOffset ||
                    bindings[attribute.streamBinding]->stride != stream.stride)
                    return false;
                if (instanceBinding)
                {
                    const bool supported = SupportsStaticSurfaceAttribute(stream);
                    if (!supported)
                        return false;
                }
                if (stream.semantic == meshes::VertexSemantic::Position)
                {
                    if (stream.format != meshes::VertexFormat::R32G32B32Float && stream.format != meshes::VertexFormat::R16G16B16A16SNorm)
                        return false;
                    position = true;
                }
                matched = true;
                break;
            }
            if (!matched)
                return false;
        }
        // Production geometry always obtains instance/primitive/material identity
        // from the counted-indirect instance stream. A layout without VG_DRAW
        // cannot consume that stream, even if its ordinary vertex inputs match.
        return position && instanceBinding && instanceFields == 3;
    }
} // namespace vanguard::rendering
