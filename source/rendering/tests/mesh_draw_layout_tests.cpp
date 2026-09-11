#include <vanguard/rendering/mesh_draw_layout.hpp>
#include <vanguard/filesystem/filesystem.hpp>

#include <cstdio>
#include <cstring>

namespace vanguard::rendering::tests
{
    bool RunMeshDrawLayoutProof() noexcept
    {
        for (u32 variant = 0; variant < 8; ++variant)
        {
            const bool quantized = (variant & 1) != 0;
            const bool interleaved = (variant & 2) != 0;
            const bool index32 = (variant & 4) != 0;
            const u32 positionBytes = quantized ? 8 : 12;
            const u32 stride = positionBytes + (interleaved ? 8 : 0);
            const u32 indexBytes = index32 ? 4 : 2;
            const auto positionFormat = quantized ? meshes::VertexFormat::R16G16B16A16SNorm : meshes::VertexFormat::R32G32B32Float;
            u8 vertexBytes[64]{}, uvBytes[24]{}, indices[12]{};
            const meshes::BufferBuildRecord buffers[]{{1, meshes::BufferKind::Vertex, stride, stride * 3}, {2, meshes::BufferKind::Index, indexBytes, indexBytes * 3}, {3, meshes::BufferKind::Vertex, 8, 24}};
            const auto flags = meshes::PageFlags::RequiredForLowestLod | meshes::PageFlags::DirectGpuUpload;
            const meshes::PageBuildRecord pages[]{{1, 0, vertexBytes, stride * 3, 4, flags}, {2, 0, indices, indexBytes * 3, 4, flags}, {3, 0, uvBytes, 24, 4, flags}};
            const meshes::VertexLayoutBuildRecord layout{1};
            const meshes::VertexStreamBuildRecord streams[]{{1, meshes::VertexSemantic::Position, 0, positionFormat, 0, 1, 0, stride},
                                                            {1, meshes::VertexSemantic::TexCoord, 0, meshes::VertexFormat::R32G32Float, static_cast<u8>(interleaved ? 0 : 1), interleaved ? 1u : 3u,
                                                             interleaved ? positionBytes : 0u, interleaved ? stride : 8u}};
            const meshes::MaterialSlotBuildRecord material{1, 1, resources::ResourceReference(resources::ResourcePath::FromString("materials/layout.vmat"), serialization::MakeFourCC('V', 'M', 'A', 'T'))};
            const meshes::LodBuildRecord lod{0, 1.0f};
            meshes::Bounds bounds;
            bounds.minimum[0] = bounds.minimum[1] = bounds.minimum[2] = -1;
            bounds.maximum[0] = bounds.maximum[1] = bounds.maximum[2] = 1;
            bounds.sphereRadius = 2;
            const meshes::SubmeshBuildRecord submesh{
                1, 1, 0, 1, 1, 2, index32 ? meshes::IndexFormat::UInt32 : meshes::IndexFormat::UInt16, meshes::PrimitiveTopology::TriangleList, meshes::SubmeshFlags::None, 0, 3, 0, 3, bounds};
            meshes::BuildDescription description;
            description.name = 1;
            description.bounds = bounds;
            description.sourceFingerprint = crypto::Sha256("draw-layout", 11);
            description.buffers = {buffers, interleaved ? 2u : 3u};
            description.pages = {pages, interleaved ? 2u : 3u};
            description.vertexLayouts = {&layout, 1};
            description.vertexStreams = streams;
            description.materialSlots = {&material, 1};
            description.lods = {&lod, 1};
            description.submeshes = {&submesh, 1};
            containers::DynamicArray<u8> bytes(memory::pools::Rendering::GetInstance());
            filesystem::MemoryFileWriter writer(bytes);
            meshes::MeshFile mesh;
            if (meshes::WriteMesh(writer, description) != meshes::Result::Success)
                return false;
            filesystem::MemoryFileReader reader(bytes, 0);
            if (mesh.Open(reader) != meshes::Result::Success)
                return false;

            const auto digest = crypto::Sha256("layout-shader", 13);
            const pipelines::ShaderReference shader{1, digest, digest, digest};
            pipelines::VertexStream pipelineStreams[]{
                {0, stride, pipelines::InputRate::PerVertex, 1}, {MeshDrawInstanceBinding, sizeof(MeshDrawInstance), pipelines::InputRate::PerInstance, 1}, {1, 8, pipelines::InputRate::PerVertex, 1}};
            pipelines::VertexAttribute attributes[]{{1, 0, 0, 0, 0, shaders::NumericClass::FloatingPoint, static_cast<u8>(quantized ? 4 : 3), static_cast<u8>(quantized ? 16 : 32),
                                                     quantized ? pipelines::Format::R16G16B16A16SNorm : pipelines::Format::R32G32B32Float, "POSITION"},
                                                    {2, 0, 1, interleaved ? 0u : 1u, interleaved ? positionBytes : 0u, shaders::NumericClass::FloatingPoint, 2, 32, pipelines::Format::R32G32Float, "TEXCOORD"},
                                                    {3, 0, 2, MeshDrawInstanceBinding, 0, shaders::NumericClass::UnsignedInteger, 2, 32, pipelines::Format::R32G32UInt, "VG_DRAW"},
                                                    {3, 1, 3, MeshDrawInstanceBinding, 8, shaders::NumericClass::UnsignedInteger, 2, 32, pipelines::Format::R32G32UInt, "VG_DRAW"}};
            for (u32 mismatch = 0; mismatch < 4; ++mismatch)
            {
                pipelineStreams[0].stride = stride + (mismatch == 1 ? 4 : 0);
                std::memcpy(attributes[1].semanticName, mismatch == 2 ? "NORMAL" : "TEXCOORD", mismatch == 2 ? sizeof("NORMAL") : sizeof("TEXCOORD"));
                pipelineStreams[1].instanceStepRate = mismatch == 3 ? 2 : 1;
                pipelines::BuildDescription pipelineDescription;
                pipelineDescription.name = 1;
                pipelineDescription.shaders = {&shader, 1};
                pipelineDescription.vertexStreams = {pipelineStreams, interleaved ? 2u : 3u};
                pipelineDescription.vertexAttributes = attributes;
                pipelineDescription.graphics.blend.attachmentCount = 1;
                containers::DynamicArray<u8> pipelineBytes(memory::pools::Rendering::GetInstance());
                filesystem::MemoryFileWriter pipelineWriter(pipelineBytes);
                const auto writeResult = pipelines::WritePipeline(pipelineWriter, pipelineDescription);
                pipelines::PipelineFile pipeline;
                filesystem::MemoryFileReader pipelineReader(pipelineBytes, 0);
                if (writeResult != pipelines::Result::Success || pipeline.Open(pipelineReader) != pipelines::Result::Success || ValidateMeshDrawLayout(mesh, 0, pipeline) != (mismatch == 0) ||
                    ValidateMeshDrawLayout(mesh, 1, pipeline))
                {
                    std::fprintf(stderr, "[meshDrawLayout] variant=%u mismatch=%u write=%s\n", variant, mismatch, pipelines::ToString(writeResult));
                    return false;
                }
            }
        }
        return true;
    }
} // namespace vanguard::rendering::tests
