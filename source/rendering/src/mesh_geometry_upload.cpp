#include <vanguard/rendering/mesh_geometry_upload.hpp>

#include <vanguard/memory/memory.hpp>

#include <cstring>

namespace vanguard::rendering
{
    namespace
    {
        struct PlannedGeometry
        {
            rhi::VertexBindingDesc bindings[rhi::MaximumVertexBindings];
            u32 bindingBuffers[rhi::MaximumVertexBindings]{};
            GeometryAllocationRequest request;
            u32 submesh = meshes::InvalidRecordIndex;
            u32 bindingCount = 0;
        };

        struct CopyFragment
        {
            u32 payload = meshes::InvalidRecordIndex;
            u32 geometry = InvalidGeometryIndex;
            u32 binding = 0;
            u64 sourceOffset = 0;
            u64 destinationOffset = 0;
            u64 size = 0;
            bool indices = false;
        };

        void ClearFailure(MeshGeometryUploadFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MeshGeometryUploadFailure* const failure, const MeshGeometryUploadFailureCode code, const char* const message, const u32 submesh = meshes::InvalidRecordIndex,
                                const u32 page = meshes::InvalidRecordIndex, const meshes::Result meshFailure = meshes::Result::Success,
                                const GeometryAllocatorFailure& allocatorFailure = {}, const GeometryUploadFailure& uploadFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, submesh, page, message, meshFailure, allocatorFailure, uploadFailure};
            return false;
        }

        [[nodiscard]] bool AddChecked(const u64 left, const u64 right, u64& result) noexcept
        {
            if (left > ~u64{0} - right)
                return false;
            result = left + right;
            return true;
        }

        [[nodiscard]] bool MultiplyChecked(const u64 left, const u64 right, u64& result) noexcept
        {
            if (left != 0 && right > ~u64{0} / left)
                return false;
            result = left * right;
            return true;
        }

        [[nodiscard]] bool AppendRangeFragments(const meshes::MeshFile& mesh, const containers::ArraySpan<const u32> payloadByPage, const u32 bufferIndex, const u64 rangeOffset,
                                                const u64 rangeSize, const u32 geometry, const u32 binding, const bool indices, containers::DynamicArray<CopyFragment>& fragments,
                                                MeshGeometryUploadFailure* const failure, const u32 submesh) noexcept
        {
            const auto buffers = mesh.GetBuffers();
            const auto pages = mesh.GetPages();
            if (bufferIndex >= buffers.Size() || rangeSize == 0)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidGeometryLayout, "mesh geometry upload references an invalid or empty buffer range", submesh);
            const meshes::BufferRecord& buffer = buffers[bufferIndex];
            u64 rangeEnd = 0;
            if (!AddChecked(rangeOffset, rangeSize, rangeEnd) || rangeEnd > buffer.byteSize)
                return Fail(failure, MeshGeometryUploadFailureCode::ArithmeticOverflow, "mesh geometry upload buffer range overflowed", submesh);

            u64 covered = 0;
            for (u32 localPage = 0; localPage < buffer.pageCount; ++localPage)
            {
                const u32 pageIndex = buffer.firstPage + localPage;
                const meshes::PageRecord& page = pages[pageIndex];
                u64 pageEnd = 0;
                if (!AddChecked(page.bufferOffset, page.byteSize, pageEnd))
                    return Fail(failure, MeshGeometryUploadFailureCode::ArithmeticOverflow, "mesh page range overflowed while planning geometry upload", submesh, pageIndex);
                const u64 begin = rangeOffset > page.bufferOffset ? rangeOffset : page.bufferOffset;
                const u64 end = rangeEnd < pageEnd ? rangeEnd : pageEnd;
                if (begin >= end)
                    continue;
                if (pageIndex >= payloadByPage.Size() || payloadByPage[pageIndex] == meshes::InvalidRecordIndex)
                    return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "mesh LOD is missing a required verified page payload", submesh, pageIndex);
                const u64 fragmentSize = end - begin;
                fragments.PushBack({payloadByPage[pageIndex], geometry, binding, begin - page.bufferOffset, begin - rangeOffset, fragmentSize, indices});
                if (!AddChecked(covered, fragmentSize, covered))
                    return Fail(failure, MeshGeometryUploadFailureCode::ArithmeticOverflow, "mesh geometry upload coverage overflowed", submesh, pageIndex);
            }
            if (covered != rangeSize)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "verified pages do not completely cover a mesh geometry range", submesh);
            return true;
        }

        void Rollback(GeometryAllocator& allocator, GeometryUploader& uploader, const containers::ArraySpan<const GeometryReservation> reservations, const bool ownsUploaderBatch) noexcept
        {
            if (ownsUploaderBatch)
            {
                GeometryUploadFailure uploadFailure;
                static_cast<void>(uploader.Cancel(&uploadFailure));
            }
            GeometryAllocatorFailure allocatorFailure;
            static_cast<void>(allocator.CancelBatch(reservations, &allocatorFailure));
        }
    } // namespace

    PreparedMeshLodUpload::PreparedMeshLodUpload() noexcept
        : pages(memory::pools::Rendering::GetInstance()), geometries(memory::pools::Rendering::GetInstance()), submeshes(memory::pools::Rendering::GetInstance()),
          uploadReservations(memory::pools::Rendering::GetInstance()), stagingCopies(memory::pools::Rendering::GetInstance())
    {
    }

    bool PreparedMeshLodUpload::IsValid() const noexcept
    {
        return !meshContentFingerprint.IsEmpty() && lod != 0xffffu && !pages.Empty() && !geometries.Empty() && geometries.Size() == submeshes.Size();
    }

    void PreparedMeshLodUpload::Reset() noexcept
    {
        meshContentFingerprint = {};
        pages.Clear();
        geometries.Clear();
        submeshes.Clear();
        uploadReservations.Clear();
        stagingCopies.Clear();
        lod = 0xffffu;
        vertexBytes = 0;
        indexBytes = 0;
        stagingFilled = false;
    }

    bool PrepareMeshLodGeometryUpload(const meshes::MeshFile& mesh, const u16 lod, const containers::ArraySpan<const VerifiedMeshPagePayload> payloads, GeometryAllocator& allocator,
                                      GeometryUploader& uploader, PreparedMeshLodUpload& prepared, MeshGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!mesh.IsOpen() || !allocator.IsInitialized() || !uploader.IsInitialized())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidArgument, "mesh, geometry allocator, and geometry uploader must be initialized");
        if (prepared.IsValid() || !prepared.pages.Empty() || !prepared.geometries.Empty() || !prepared.submeshes.Empty() ||
            !prepared.uploadReservations.Empty() || !prepared.stagingCopies.Empty())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidState, "prepared mesh LOD upload output is already in use");
        if (lod >= mesh.GetLods().Size())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidLod, "mesh LOD index is invalid");

        containers::DynamicArray<u32> requiredPages{memory::pools::Rendering::GetInstance()};
        const meshes::Result pageResult = meshes::CollectLodPages(mesh, lod, requiredPages);
        if (pageResult != meshes::Result::Success)
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidLod, "mesh LOD install set could not be derived", meshes::InvalidRecordIndex, meshes::InvalidRecordIndex, pageResult);
        if (payloads.Size() != requiredPages.Size())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "verified page payload count does not match the complete LOD install set");

        containers::DynamicArray<u32> payloadByPage{memory::pools::Rendering::GetInstance()};
        payloadByPage.Resize(mesh.GetPages().Size());
        if (payloadByPage.Size() != mesh.GetPages().Size())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "mesh page lookup allocation failed");
        for (u32& payload : payloadByPage)
            payload = meshes::InvalidRecordIndex;
        for (u32 payloadIndex = 0; payloadIndex < payloads.Size(); ++payloadIndex)
        {
            const VerifiedMeshPagePayload& payload = payloads[payloadIndex];
            if (payload.page >= mesh.GetPages().Size() || payload.bytes.Data() == nullptr || payload.bytes.Size() != mesh.GetPages()[payload.page].byteSize)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPagePayload, "verified mesh page payload has an invalid page or byte size", meshes::InvalidRecordIndex,
                            payload.page);
            if (payloadByPage[payload.page] != meshes::InvalidRecordIndex)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "mesh LOD page payload set contains a duplicate page", meshes::InvalidRecordIndex, payload.page);
            payloadByPage[payload.page] = payloadIndex;
        }
        for (const u32 page : requiredPages)
            if (payloadByPage[page] == meshes::InvalidRecordIndex)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPageSet, "mesh LOD page payload set is incomplete or contains an unrelated page", meshes::InvalidRecordIndex,
                            page);

        const meshes::LodRecord& selectedLod = mesh.GetLods()[lod];
        containers::DynamicArray<PlannedGeometry> plans{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<CopyFragment> fragments{memory::pools::Rendering::GetInstance()};
        plans.Resize(selectedLod.submeshCount);
        if (plans.Size() != selectedLod.submeshCount)
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidGeometryLayout, "mesh geometry upload plan allocation failed");

        const auto layouts = mesh.GetVertexLayouts();
        const auto streams = mesh.GetVertexStreams();
        const auto submeshes = mesh.GetSubmeshes();
        u64 vertexBytes = 0;
        u64 indexBytes = 0;
        for (u32 geometry = 0; geometry < plans.Size(); ++geometry)
        {
            PlannedGeometry& plan = plans[geometry];
            plan.submesh = selectedLod.firstSubmesh + geometry;
            const meshes::SubmeshRecord& submesh = submeshes[plan.submesh];
            const meshes::VertexLayoutRecord& layout = layouts[submesh.vertexLayout];
            bool bindingSeen[rhi::MaximumVertexBindings]{};
            u32 highestBinding = 0;
            for (u32 localStream = 0; localStream < layout.streamCount; ++localStream)
            {
                const meshes::VertexStream& stream = streams[layout.firstStream + localStream];
                if (stream.binding >= rhi::MaximumVertexBindings || stream.stride == 0 || stream.stride > 0xffffu)
                    return Fail(failure, MeshGeometryUploadFailureCode::InvalidGeometryLayout, "mesh vertex binding exceeds fixed-function geometry limits", plan.submesh);
                highestBinding = stream.binding > highestBinding ? stream.binding : highestBinding;
                if (bindingSeen[stream.binding])
                {
                    if (plan.bindingBuffers[stream.binding] != stream.buffer || plan.bindings[stream.binding].stride != stream.stride)
                        return Fail(failure, MeshGeometryUploadFailureCode::InvalidGeometryLayout, "attributes sharing one vertex binding use different buffers or strides", plan.submesh);
                    continue;
                }
                bindingSeen[stream.binding] = true;
                plan.bindingBuffers[stream.binding] = stream.buffer;
                plan.bindings[stream.binding] = {stream.binding, static_cast<u16>(stream.stride), rhi::VertexInputRate::PerVertex, 1};
            }
            plan.bindingCount = highestBinding + 1u;
            for (u32 binding = 0; binding < plan.bindingCount; ++binding)
                if (!bindingSeen[binding])
                    return Fail(failure, MeshGeometryUploadFailureCode::InvalidGeometryLayout, "mesh vertex bindings must be contiguous from binding zero", plan.submesh);

            plan.request.vertexLayout = {layout.fingerprint, {plan.bindings, plan.bindingCount}};
            plan.request.vertexCount = submesh.vertexCount;
            plan.request.indexFormat = submesh.indexFormat == meshes::IndexFormat::UInt32 ? rhi::IndexFormat::UInt32 : rhi::IndexFormat::UInt16;
            plan.request.indexCount = submesh.indexCount;
            for (u32 binding = 0; binding < plan.bindingCount; ++binding)
            {
                const u64 stride = plan.bindings[binding].stride;
                u64 rangeOffset = 0;
                u64 rangeSize = 0;
                if (!MultiplyChecked(submesh.firstVertex, stride, rangeOffset) || !MultiplyChecked(submesh.vertexCount, stride, rangeSize) ||
                    !AddChecked(vertexBytes, rangeSize, vertexBytes))
                    return Fail(failure, MeshGeometryUploadFailureCode::ArithmeticOverflow, "mesh vertex upload byte range overflowed", plan.submesh);
                if (!AppendRangeFragments(mesh, {payloadByPage.TypedData(), payloadByPage.Size()}, plan.bindingBuffers[binding], rangeOffset, rangeSize, geometry, binding, false, fragments,
                                          failure, plan.submesh))
                    return false;
            }
            const u64 indexStride = plan.request.indexFormat == rhi::IndexFormat::UInt32 ? 4u : 2u;
            u64 indexOffset = 0;
            u64 indexSize = 0;
            if (!MultiplyChecked(submesh.firstIndex, indexStride, indexOffset) || !MultiplyChecked(submesh.indexCount, indexStride, indexSize) ||
                !AddChecked(indexBytes, indexSize, indexBytes))
                return Fail(failure, MeshGeometryUploadFailureCode::ArithmeticOverflow, "mesh index upload byte range overflowed", plan.submesh);
            if (!AppendRangeFragments(mesh, {payloadByPage.TypedData(), payloadByPage.Size()}, submesh.indexBuffer, indexOffset, indexSize, geometry, 0, true, fragments, failure,
                                      plan.submesh))
                return false;
        }

        containers::DynamicArray<GeometryAllocationRequest> allocationRequests{memory::pools::Rendering::GetInstance()};
        containers::DynamicArray<GeometryReservation> reservations{memory::pools::Rendering::GetInstance()};
        allocationRequests.Resize(plans.Size());
        reservations.Resize(plans.Size());
        if (allocationRequests.Size() != plans.Size() || reservations.Size() != plans.Size())
            return Fail(failure, MeshGeometryUploadFailureCode::AllocationFailure, "mesh geometry reservation arrays could not be allocated");
        for (u32 geometry = 0; geometry < plans.Size(); ++geometry)
            allocationRequests[geometry] = plans[geometry].request;

        GeometryAllocatorFailure allocatorFailure;
        if (!allocator.ReserveBatch({allocationRequests.TypedData(), allocationRequests.Size()}, {reservations.TypedData(), reservations.Size()}, &allocatorFailure))
            return Fail(failure, MeshGeometryUploadFailureCode::AllocationFailure, "complete mesh LOD geometry reservation failed", meshes::InvalidRecordIndex, meshes::InvalidRecordIndex,
                        meshes::Result::Success, allocatorFailure);

        containers::DynamicArray<GeometryUploadRequest> uploadRequests{memory::pools::Rendering::GetInstance()};
        uploadRequests.Resize(plans.Size());
        prepared.uploadReservations.Resize(plans.Size());
        if (uploadRequests.Size() != plans.Size() || prepared.uploadReservations.Size() != plans.Size())
        {
            Rollback(allocator, uploader, {reservations.TypedData(), reservations.Size()}, false);
            return Fail(failure, MeshGeometryUploadFailureCode::UploadFailure, "mesh geometry upload ticket arrays could not be allocated");
        }
        for (u32 geometry = 0; geometry < plans.Size(); ++geometry)
            uploadRequests[geometry] = {reservations[geometry]};

        GeometryUploadFailure uploadFailure;
        if (!uploader.Begin({uploadRequests.TypedData(), uploadRequests.Size()}, {prepared.uploadReservations.TypedData(), prepared.uploadReservations.Size()}, &uploadFailure))
        {
            Rollback(allocator, uploader, {reservations.TypedData(), reservations.Size()}, false);
            prepared.Reset();
            return Fail(failure, MeshGeometryUploadFailureCode::UploadFailure, "mesh geometry uploader could not reserve staging", meshes::InvalidRecordIndex, meshes::InvalidRecordIndex,
                        meshes::Result::Success, {}, uploadFailure);
        }

        for (const CopyFragment& fragment : fragments)
        {
            const VerifiedMeshPagePayload& payload = payloads[fragment.payload];
            const GeometryUploadReservation& destination = prepared.uploadReservations[fragment.geometry];
            const GeometryUploadSlice slice = fragment.indices ? destination.indices : destination.vertexStreams[fragment.binding];
            if (!slice.IsValid() || fragment.sourceOffset > payload.bytes.Size() || fragment.size > payload.bytes.Size() - fragment.sourceOffset || fragment.destinationOffset > slice.size ||
                fragment.size > slice.size - fragment.destinationOffset)
            {
                Rollback(allocator, uploader, {reservations.TypedData(), reservations.Size()}, true);
                prepared.Reset();
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPagePayload, "mesh page fragment escaped its verified source or upload destination",
                            plans[fragment.geometry].submesh, payload.page);
            }
            prepared.stagingCopies.PushBack({payload.bytes.Data() + fragment.sourceOffset,
                                             static_cast<u8*>(slice.destination) + fragment.destinationOffset, fragment.size});
        }

        prepared.pages.Reserve(requiredPages.Size());
        for (const u32 page : requiredPages)
            prepared.pages.PushBack(page);
        prepared.geometries.Reserve(reservations.Size());
        prepared.submeshes.Reserve(plans.Size());
        for (u32 geometry = 0; geometry < plans.Size(); ++geometry)
        {
            prepared.geometries.PushBack(reservations[geometry]);
            prepared.submeshes.PushBack({plans[geometry].submesh, geometry});
        }
        prepared.meshContentFingerprint = mesh.GetContentFingerprint();
        prepared.lod = lod;
        prepared.vertexBytes = vertexBytes;
        prepared.indexBytes = indexBytes;
        return true;
    }

    bool FillPreparedMeshLodUpload(GeometryUploader& uploader, PreparedMeshLodUpload& prepared,
                                   MeshGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!prepared.IsValid() || prepared.stagingFilled || prepared.uploadReservations.Size() != prepared.geometries.Size())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidState, "prepared mesh LOD upload is not fillable");
        for (const MeshGeometryStagingCopy& copy : prepared.stagingCopies)
        {
            if (copy.source == nullptr || copy.destination == nullptr || copy.size == 0)
                return Fail(failure, MeshGeometryUploadFailureCode::InvalidPagePayload, "prepared mesh LOD contains an invalid staging copy");
            std::memcpy(copy.destination, copy.source, static_cast<usize>(copy.size));
        }
        GeometryUploadFailure uploadFailure;
        for (u32 geometry = 0; geometry < prepared.uploadReservations.Size(); ++geometry)
            if (!uploader.Complete(prepared.uploadReservations[geometry], &uploadFailure))
                return Fail(failure, MeshGeometryUploadFailureCode::UploadFailure, "mesh geometry upload ticket completion failed",
                            prepared.submeshes[geometry].submesh, meshes::InvalidRecordIndex, meshes::Result::Success, {}, uploadFailure);
        prepared.stagingCopies.Clear();
        prepared.uploadReservations.Clear();
        prepared.stagingFilled = true;
        return true;
    }

    bool CancelPreparedMeshLodUpload(GeometryAllocator& allocator, GeometryUploader& uploader, PreparedMeshLodUpload& prepared, MeshGeometryUploadFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!prepared.IsValid())
            return Fail(failure, MeshGeometryUploadFailureCode::InvalidState, "prepared mesh LOD upload is invalid");
        GeometryUploadFailure uploadFailure;
        if (!uploader.Cancel(&uploadFailure))
            return Fail(failure, MeshGeometryUploadFailureCode::UploadFailure, "prepared mesh LOD uploader batch could not be cancelled", meshes::InvalidRecordIndex,
                        meshes::InvalidRecordIndex, meshes::Result::Success, {}, uploadFailure);
        GeometryAllocatorFailure allocatorFailure;
        if (!allocator.CancelBatch({prepared.geometries.TypedData(), prepared.geometries.Size()}, &allocatorFailure))
            return Fail(failure, MeshGeometryUploadFailureCode::AllocationFailure, "prepared mesh LOD geometry reservations could not be cancelled", meshes::InvalidRecordIndex,
                        meshes::InvalidRecordIndex, meshes::Result::Success, allocatorFailure);
        prepared.Reset();
        return true;
    }
} // namespace vanguard::rendering
