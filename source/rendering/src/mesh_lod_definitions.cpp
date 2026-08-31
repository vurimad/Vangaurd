#include <vanguard/rendering/mesh_lod_definitions.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/meshes/mesh_resource.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/system/assert.hpp>

#include <cstring>
#include <limits>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(MeshLodDefinitionFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MeshLodDefinitionFailure* const failure, const MeshLodDefinitionFailureCode code,
                                const char* const message, const u32 submesh = meshes::InvalidRecordIndex,
                                const GpuSceneDefinitionFailure& definitionFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, submesh, message, definitionFailure};
            return false;
        }

        [[nodiscard]] GpuSceneDefinitionKey BuildKey(const PendingMeshLodGeometry& source, const u32 submesh,
                                                      const GeometryPlacement& placement, const GpuGeometryRange& range,
                                                      const containers::ArraySpan<const GpuVertexStream> streams,
                                                      const GpuPositionDecode* const decode) noexcept
        {
            crypto::Sha256Builder builder;
            static_cast<void>(builder.Update(source.meshContentFingerprint.bytes, sizeof(source.meshContentFingerprint.bytes)));
            static_cast<void>(builder.Update(&source.lod, sizeof(source.lod)));
            static_cast<void>(builder.Update(&submesh, sizeof(submesh)));
            static_cast<void>(builder.Update(&placement, sizeof(placement)));
            static_cast<void>(builder.Update(&range, sizeof(range)));
            static_cast<void>(builder.Update(streams.Data(), streams.SizeInBytes()));
            if (decode != nullptr)
                static_cast<void>(builder.Update(decode, sizeof(*decode)));
            crypto::Digest256 digest;
            static_cast<void>(builder.Finalize(digest));
            GpuSceneDefinitionKey key;
            std::memcpy(key.words, digest.bytes, sizeof(key.words));
            return key;
        }
    } // namespace

    PendingMeshLodDefinitions::PendingMeshLodDefinitions() noexcept
        : geometryDefinitions(memory::pools::Rendering::GetInstance())
    {
    }

    PendingMeshLodDefinitions::~PendingMeshLodDefinitions()
    {
        VG_ASSERT_MSG(!HasOwnership(), "pending mesh LOD definitions must be committed or explicitly aborted before destruction");
    }

    PendingMeshLodDefinitions::PendingMeshLodDefinitions(PendingMeshLodDefinitions&& other) noexcept : PendingMeshLodDefinitions()
    {
        *this = std::move(other);
    }

    PendingMeshLodDefinitions& PendingMeshLodDefinitions::operator=(PendingMeshLodDefinitions&& other) noexcept
    {
        if (this != &other)
        {
            VG_ASSERT_MSG(!HasOwnership(), "move assignment cannot overwrite live pending mesh LOD definitions");
            geometry = std::move(other.geometry);
            geometryDefinitions = std::move(other.geometryDefinitions);
            definitionBatch = other.definitionBatch;
            other.Reset();
        }
        return *this;
    }

    bool PendingMeshLodDefinitions::IsValid() const noexcept
    {
        return geometry.IsValid() && !geometryDefinitions.Empty() && geometryDefinitions.Size() == geometry.geometries.Size();
    }

    bool PendingMeshLodDefinitions::HasOwnership() const noexcept
    {
        return geometry.HasOwnership() || !geometryDefinitions.Empty();
    }

    void PendingMeshLodDefinitions::Reset() noexcept
    {
        VG_ASSERT_MSG(!HasOwnership(), "live pending mesh LOD definitions must be explicitly aborted, not reset");
        geometry.Reset();
        geometryDefinitions.Clear();
        definitionBatch = {};
    }

    bool PreparePendingMeshLodDefinitions(PendingMeshLodGeometry& uploadedGeometry, GpuSceneDefinitions& definitions,
                                          PendingMeshLodDefinitions& pendingDefinitions, MeshLodDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!uploadedGeometry.IsValid() || pendingDefinitions.geometry.IsValid() || !pendingDefinitions.geometryDefinitions.Empty() ||
            pendingDefinitions.definitionBatch.completion.IsValid() || !definitions.IsInitialized())
            return Fail(failure, MeshLodDefinitionFailureCode::InvalidArgument, "pending mesh LOD definition arguments are invalid");

        resources::ResourceHandle resource = uploadedGeometry.resource.Lock();
        auto* const mesh = resource.IsValid() && resource.GetType() == meshes::MeshResourceType
                               ? static_cast<meshes::MeshResourceObject*>(resource.Get())
                               : nullptr;
        if (mesh == nullptr || !mesh->IsOpen() || !(mesh->GetMetadata().GetContentFingerprint() == uploadedGeometry.meshContentFingerprint))
            return Fail(failure, MeshLodDefinitionFailureCode::StaleResourceGeneration,
                        "mesh resource generation changed before geometry-definition acquisition");

        const meshes::MeshFile& metadata = mesh->GetMetadata();
        const auto lods = metadata.GetLods();
        const auto submeshes = metadata.GetSubmeshes();
        const auto layouts = metadata.GetVertexLayouts();
        const auto sourceStreams = metadata.GetVertexStreams();
        if (uploadedGeometry.lod >= lods.Size())
            return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata, "pending geometry references an invalid mesh LOD");
        const meshes::LodRecord& lod = lods[uploadedGeometry.lod];
        if (lod.firstSubmesh > submeshes.Size() || lod.submeshCount > submeshes.Size() - lod.firstSubmesh ||
            lod.submeshCount != uploadedGeometry.geometries.Size() || uploadedGeometry.submeshes.Size() != uploadedGeometry.geometries.Size())
            return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                        "pending geometry does not cover the complete authored LOD");

        containers::DynamicArray<u32> sourceSubmeshByGeometry{memory::pools::Rendering::GetInstance()};
        sourceSubmeshByGeometry.Resize(uploadedGeometry.geometries.Size());
        for (u32& value : sourceSubmeshByGeometry)
            value = meshes::InvalidRecordIndex;
        u32 totalStreams = 0;
        for (const MeshGeometrySubmeshUpload mapping : uploadedGeometry.submeshes)
        {
            if (mapping.geometry >= sourceSubmeshByGeometry.Size() || mapping.submesh < lod.firstSubmesh ||
                mapping.submesh - lod.firstSubmesh >= lod.submeshCount ||
                sourceSubmeshByGeometry[mapping.geometry] != meshes::InvalidRecordIndex)
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "pending geometry has a duplicate or out-of-LOD submesh mapping", mapping.submesh);
            sourceSubmeshByGeometry[mapping.geometry] = mapping.submesh;
            const meshes::SubmeshRecord& submesh = submeshes[mapping.submesh];
            if (submesh.vertexLayout >= layouts.Size())
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "pending geometry references an invalid vertex layout", mapping.submesh);
            const meshes::VertexLayoutRecord& layout = layouts[submesh.vertexLayout];
            if (layout.streamCount == 0 || layout.firstStream > sourceStreams.Size() || layout.streamCount > sourceStreams.Size() - layout.firstStream)
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "pending geometry vertex layout has an invalid stream range", mapping.submesh);
            u32 highestBinding = 0;
            for (u32 stream = 0; stream < layout.streamCount; ++stream)
                highestBinding = sourceStreams[layout.firstStream + stream].binding > highestBinding
                                     ? sourceStreams[layout.firstStream + stream].binding
                                     : highestBinding;
            if (highestBinding >= rhi::MaximumVertexBindings || totalStreams > std::numeric_limits<u32>::max() - highestBinding - 1u)
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "pending geometry vertex binding count is invalid", mapping.submesh);
            totalStreams += highestBinding + 1u;
        }

        containers::DynamicArray<GpuGeometryDefinition> geometryDefinitions{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GpuVertexStream> vertexStreams{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GpuPositionDecode> positionDecodes{memory::pools::Rendering::GetInstance()};
        geometryDefinitions.Resize(uploadedGeometry.geometries.Size());
        vertexStreams.Resize(totalStreams);
        positionDecodes.Resize(uploadedGeometry.geometries.Size());

        u32 firstStream = 0;
        for (u32 geometry = 0; geometry < uploadedGeometry.geometries.Size(); ++geometry)
        {
            const u32 submeshIndex = sourceSubmeshByGeometry[geometry];
            if (submeshIndex == meshes::InvalidRecordIndex)
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "pending geometry is missing an authored submesh mapping");
            const meshes::SubmeshRecord& submesh = submeshes[submeshIndex];
            const meshes::VertexLayoutRecord& layout = layouts[submesh.vertexLayout];
            const GeometryPlacement& placement = uploadedGeometry.geometries[geometry];
            const rhi::IndexFormat authoredIndexFormat =
                submesh.indexFormat == meshes::IndexFormat::UInt32 ? rhi::IndexFormat::UInt32 : rhi::IndexFormat::UInt16;
            if (!placement.IsValid() || placement.vertex.firstVertex > static_cast<u32>(std::numeric_limits<i32>::max()) ||
                placement.vertex.vertexCount != submesh.vertexCount || placement.index.indexCount != submesh.indexCount ||
                placement.index.format != authoredIndexFormat)
                return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                            "physical geometry placement does not match its authored submesh", submeshIndex);

            u32 highestBinding = 0;
            bool quantizedPosition = false;
            u32 bindingStrides[rhi::MaximumVertexBindings]{};
            bool bindingSeen[rhi::MaximumVertexBindings]{};
            for (u32 localStream = 0; localStream < layout.streamCount; ++localStream)
            {
                const meshes::VertexStream& stream = sourceStreams[layout.firstStream + localStream];
                highestBinding = stream.binding > highestBinding ? stream.binding : highestBinding;
                if (bindingSeen[stream.binding] && bindingStrides[stream.binding] != stream.stride)
                    return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                                "vertex attributes sharing a binding disagree on stride", submeshIndex);
                bindingSeen[stream.binding] = true;
                bindingStrides[stream.binding] = stream.stride;
                quantizedPosition = quantizedPosition ||
                                    (stream.semantic == meshes::VertexSemantic::Position &&
                                     stream.format == meshes::VertexFormat::R16G16B16A16SNorm);
            }
            const u32 bindingCount = highestBinding + 1u;
            for (u32 binding = 0; binding < bindingCount; ++binding)
            {
                if (!bindingSeen[binding] || bindingStrides[binding] == 0)
                    return Fail(failure, MeshLodDefinitionFailureCode::InvalidGeometryMetadata,
                                "vertex bindings must be contiguous and have nonzero stride", submeshIndex);
                vertexStreams[firstStream + binding] = {binding, bindingStrides[binding], InvalidGpuSceneIndex, 0};
            }

            GpuGeometryRange range;
            range.vertexArenaSet = placement.vertex.arena.index;
            range.vertexArenaGeneration = placement.vertex.arena.generation;
            range.indexArena = placement.index.arena.index;
            range.indexArenaGeneration = placement.index.arena.generation;
            range.firstIndex = placement.index.firstIndex;
            range.indexCount = placement.index.indexCount;
            range.baseVertex = static_cast<i32>(placement.vertex.firstVertex);
            range.vertexCount = placement.vertex.vertexCount;
            range.indexFormat = placement.index.format == rhi::IndexFormat::UInt32 ? GpuIndexFormat::UInt32 : GpuIndexFormat::UInt16;
            range.flags = static_cast<GpuGeometryFlags>(static_cast<u32>(GpuGeometryFlags::Resident) |
                                                        static_cast<u32>(GpuGeometryFlags::Indexed));

            GpuPositionDecode* decode = nullptr;
            if (quantizedPosition)
            {
                decode = &positionDecodes[geometry];
                const meshes::PositionQuantization& quantization = metadata.GetQuantization();
                for (u32 component = 0; component < 3; ++component)
                {
                    decode->scale[component] = quantization.scale[component];
                    decode->bias[component] = quantization.bias[component];
                }
            }
            const containers::ArraySpan<const GpuVertexStream> streams{vertexStreams.TypedData() + firstStream, bindingCount};
            GpuGeometryDefinition& definition = geometryDefinitions[geometry];
            definition.geometry = range;
            definition.vertexStreams = streams;
            definition.positionDecode = decode;
            definition.key = BuildKey(uploadedGeometry, submeshIndex, placement, range, streams, decode);
            firstStream += bindingCount;
        }

        pendingDefinitions.geometryDefinitions.Resize(geometryDefinitions.Size());
        GpuSceneDefinitionFailure definitionFailure;
        if (!definitions.AcquireGeometries({geometryDefinitions.TypedData(), geometryDefinitions.Size()},
                                           {pendingDefinitions.geometryDefinitions.TypedData(), pendingDefinitions.geometryDefinitions.Size()},
                                           pendingDefinitions.definitionBatch, &definitionFailure))
        {
            pendingDefinitions.geometryDefinitions.Clear();
            pendingDefinitions.definitionBatch = {};
            return Fail(failure, MeshLodDefinitionFailureCode::GeometryDefinitionFailure,
                        "GPU Scene geometry-definition acquisition failed", meshes::InvalidRecordIndex, definitionFailure);
        }

        pendingDefinitions.geometry = std::move(uploadedGeometry);
        uploadedGeometry.Reset();
        return true;
    }

    bool AbortPendingMeshLodDefinitions(PendingMeshLodDefinitions& pendingDefinitions, GpuSceneDefinitions& definitions,
                                        GeometryAllocator& allocator, MeshLodDefinitionFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return Fail(failure, MeshLodDefinitionFailureCode::GeometryRetirementFailure,
                        "pending mesh LOD definitions must abort on the main thread");
        for (u32 index = 0; index < pendingDefinitions.geometryDefinitions.Size(); ++index)
        {
            if (!pendingDefinitions.geometryDefinitions[index].IsValid())
                continue;
            if (!definitions.IsValid(pendingDefinitions.geometryDefinitions[index]))
                return Fail(failure, MeshLodDefinitionFailureCode::GeometryDefinitionReleaseFailure,
                            "pending mesh LOD definitions contain a stale geometry definition");
            GpuSceneDefinitionFailure definitionFailure;
            if (!definitions.Release(pendingDefinitions.geometryDefinitions[index], &definitionFailure))
                return Fail(failure, MeshLodDefinitionFailureCode::GeometryDefinitionReleaseFailure,
                            "pending mesh LOD geometry definition could not be released", meshes::InvalidRecordIndex,
                            definitionFailure);
            pendingDefinitions.geometryDefinitions[index] = {};
        }
        pendingDefinitions.geometryDefinitions.Clear();
        GeometryAllocatorFailure allocatorFailure;
        if (!AbortPendingMeshLodGeometry(pendingDefinitions.geometry, allocator, &allocatorFailure))
        {
            if (failure != nullptr)
            {
                failure->code = MeshLodDefinitionFailureCode::GeometryRetirementFailure;
                failure->message = "pending mesh LOD geometry could not be retired";
                failure->allocatorFailure = allocatorFailure;
            }
            return false;
        }
        pendingDefinitions.Reset();
        return true;
    }
} // namespace vanguard::rendering
