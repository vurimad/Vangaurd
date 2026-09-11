#include <vanguard/rendering/gpu_scene_tables.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        struct TableLayout
        {
            u32 stride = 0;
            const char* name = nullptr;
        };

        [[nodiscard]] constexpr TableLayout GetTableLayout(const GpuSceneTableKind kind) noexcept
        {
            switch (kind)
            {
            case GpuSceneTableKind::Instance:
                return {sizeof(GpuInstance), "GPU Scene Instances"};
            case GpuSceneTableKind::Motion:
                return {sizeof(GpuMotion), "GPU Scene Motion"};
            case GpuSceneTableKind::Renderable:
                return {sizeof(GpuRenderable), "GPU Scene Renderables"};
            case GpuSceneTableKind::RenderableResidency:
                return {sizeof(GpuRenderableResidency), "GPU Scene Renderable Residency"};
            case GpuSceneTableKind::Lod:
                return {sizeof(GpuLod), "GPU Scene LODs"};
            case GpuSceneTableKind::Primitive:
                return {sizeof(GpuPrimitive), "GPU Scene Primitives"};
            case GpuSceneTableKind::PrimitivePlacement:
                return {sizeof(GpuPrimitivePlacement), "GPU Scene Primitive Placements"};
            case GpuSceneTableKind::PhaseParticipation:
                return {sizeof(GpuPhaseParticipation), "GPU Scene Phase Participation"};
            case GpuSceneTableKind::PhasePlacement:
                return {sizeof(GpuPhasePlacement), "GPU Scene Phase Placements"};
            case GpuSceneTableKind::GeometryRange:
                return {sizeof(GpuGeometryRange), "GPU Scene Geometry Ranges"};
            case GpuSceneTableKind::VertexStream:
                return {sizeof(GpuVertexStream), "GPU Scene Vertex Streams"};
            case GpuSceneTableKind::PositionDecode:
                return {sizeof(GpuPositionDecode), "GPU Scene Position Decode"};
            case GpuSceneTableKind::Material:
                return {sizeof(GpuMaterial), "GPU Scene Materials"};
            case GpuSceneTableKind::MaterialResource:
                return {sizeof(GpuMaterialResource), "GPU Scene Material Resources"};
            case GpuSceneTableKind::MaterialSet:
                return {sizeof(GpuMaterialSet), "GPU Scene Material Sets"};
            case GpuSceneTableKind::MaterialIndex:
                return {sizeof(GpuMaterialIndex), "GPU Scene Material Indices"};
            case GpuSceneTableKind::Light:
                return {sizeof(GpuLight), "GPU Scene Lights"};
            case GpuSceneTableKind::Decal:
                return {sizeof(GpuDecal), "GPU Scene Decals"};
            case GpuSceneTableKind::TextureResidency:
                return {sizeof(GpuTextureResidency), "GPU Scene Texture Residency"};
            case GpuSceneTableKind::MaterialParameterWord:
                return {sizeof(GpuMaterialParameterWord), "GPU Scene Material Parameter Words"};
            case GpuSceneTableKind::GeometryShell:
                return {sizeof(GpuGeometryShell), "GPU Scene Geometry Shells"};
            case GpuSceneTableKind::GeometryBin:
                return {sizeof(GpuGeometryBin), "GPU Scene Geometry Bins"};
            default:
                return {};
            }
        }

        void ClearFailure(GpuSceneTablesFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(GpuSceneTablesFailure* const failure, const GpuSceneTablesFailureCode code, const char* const message, const GpuSceneTableKind table = GpuSceneTableKind::Count,
                                const u32 page = 0, const rhi::Failure& rhiFailure = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, table, page, message, rhiFailure};
            return false;
        }

        [[nodiscard]] constexpr bool IsValidTable(const GpuSceneTableKind kind) noexcept
        {
            return static_cast<u32>(kind) < GpuSceneTableCount;
        }

        [[nodiscard]] constexpr u32 DirectoryPageIndex(const GpuSceneTableKind kind, const u32 page) noexcept
        {
            return static_cast<u32>(kind) * MaximumGpuScenePagesPerTable + page;
        }
    } // namespace

    struct GpuSceneTables::Impl
    {
        struct Page
        {
            rhi::BufferRef buffer;
            rhi::DescriptorHandle shaderResourceDescriptor;
            rhi::DescriptorHandle unorderedAccessDescriptor;
            bool shaderResourcePublished = false;
            bool unorderedAccessPublished = false;
        };

        struct Table
        {
            Page pages[MaximumGpuScenePagesPerTable];
            GpuSceneTableStats stats;
        };

        Table tables[GpuSceneTableCount];
        GpuSceneTableDirectory tableDirectory[GpuSceneTableCount];
        GpuScenePageDirectoryEntry pageDirectory[GpuSceneTableCount * MaximumGpuScenePagesPerTable];
        rhi::DescriptorDomainRef resourceDescriptors;
        rhi::BufferRef tableDirectoryBuffer;
        rhi::BufferRef pageDirectoryBuffer;
        rhi::DescriptorHandle tableDirectoryDescriptor;
        rhi::DescriptorHandle pageDirectoryDescriptor;
        GpuSceneTablesStats stats;
        rhi::BufferRef consumerBuffers[2 + GpuSceneTableCount * MaximumGpuScenePagesPerTable];
        u32 consumerBufferCount = 0;
        u32 maximumPagesPerTable = 0;

        [[nodiscard]] bool RetireDescriptor(rhi::DescriptorHandle& descriptor, const rhi::DescriptorRetirement& safeAfter) noexcept
        {
            if (!descriptor.IsValid())
                return true;
            if (!rhi::RetireDescriptor(resourceDescriptors, descriptor, safeAfter))
                return false;
            descriptor = {};
            return true;
        }
    };

    GpuSceneTables::~GpuSceneTables()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown({}));
    }

    bool GpuSceneTables::Initialize(const GpuSceneTablesConfig& config, GpuSceneTablesFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, GpuSceneTablesFailureCode::AlreadyInitialized, "GPU Scene tables are already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneTablesFailureCode::WrongThread, "GPU Scene tables must initialize on the main thread");
        if (!rhi::IsInitialized())
            return Fail(failure, GpuSceneTablesFailureCode::RhiUnavailable, "GPU Scene tables require an initialized RHI");
        const rhi::Capabilities& capabilities = rhi::GetCapabilities();
        if (!capabilities.bindlessResources || !capabilities.descriptorIndexing)
            return Fail(failure, GpuSceneTablesFailureCode::BindlessUnsupported, "GPU Scene tables require bindless resource descriptors");
        if (config.maximumPagesPerTable == 0 || config.maximumPagesPerTable > MaximumGpuScenePagesPerTable || !config.resourceDescriptors.IsValid() ||
            !rhi::IsResourceReferenceValid(rhi::ResourceRef(config.resourceDescriptors)))
            return Fail(failure, GpuSceneTablesFailureCode::InvalidConfiguration, "GPU Scene table configuration is invalid");

        const u32 requiredDescriptors = 2u + GpuSceneTableCount * config.maximumPagesPerTable * 2u;
        const rhi::DescriptorDomainStats domainStats = rhi::GetDescriptorDomainStats(config.resourceDescriptors);
        if (domainStats.free < requiredDescriptors)
            return Fail(failure, GpuSceneTablesFailureCode::DescriptorCapacityExceeded, "bindless resource domain cannot reserve every GPU Scene page identity");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, GpuSceneTablesFailureCode::CapacityExceeded, "GPU Scene table metadata allocation failed");
        Impl* const impl = ::new (block.address) Impl();
        impl->resourceDescriptors = config.resourceDescriptors;
        impl->maximumPagesPerTable = config.maximumPagesPerTable;
        rhi::AddRef(impl->resourceDescriptors);
        m_impl = impl;

        rhi::Failure rhiFailure;
        impl->tableDirectoryDescriptor = rhi::AllocateDescriptor(impl->resourceDescriptors, &rhiFailure);
        if (!impl->tableDirectoryDescriptor)
        {
            static_cast<void>(Shutdown({}));
            return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene table-directory descriptor reservation failed", GpuSceneTableKind::Count, 0, rhiFailure);
        }
        impl->pageDirectoryDescriptor = rhi::AllocateDescriptor(impl->resourceDescriptors, &rhiFailure);
        if (!impl->pageDirectoryDescriptor)
        {
            static_cast<void>(Shutdown({}));
            return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene page-directory descriptor reservation failed", GpuSceneTableKind::Count, 0, rhiFailure);
        }

        for (u32 tableIndex = 0; tableIndex < GpuSceneTableCount; ++tableIndex)
        {
            const GpuSceneTableKind kind = static_cast<GpuSceneTableKind>(tableIndex);
            const TableLayout layout = GetTableLayout(kind);
            const u32 elementsPerPage = GetGpuSceneElementsPerPage(kind);
            Impl::Table& table = impl->tables[tableIndex];
            table.stats.maximumPages = config.maximumPagesPerTable;
            table.stats.elementsPerPage = elementsPerPage;
            table.stats.elementStride = layout.stride;
            const u32 pageShift = GetGpuScenePageShift(kind);
            impl->tableDirectory[tableIndex] = {tableIndex * MaximumGpuScenePagesPerTable, config.maximumPagesPerTable, pageShift, elementsPerPage - 1u, elementsPerPage, layout.stride, 0, 0};
            for (u32 pageIndex = 0; pageIndex < config.maximumPagesPerTable; ++pageIndex)
            {
                Impl::Page& page = table.pages[pageIndex];
                page.shaderResourceDescriptor = rhi::AllocateDescriptor(impl->resourceDescriptors, &rhiFailure);
                if (!page.shaderResourceDescriptor)
                {
                    static_cast<void>(Shutdown({}));
                    return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene page shader-resource descriptor reservation failed", kind, pageIndex, rhiFailure);
                }
                page.unorderedAccessDescriptor = rhi::AllocateDescriptor(impl->resourceDescriptors, &rhiFailure);
                if (!page.unorderedAccessDescriptor)
                {
                    static_cast<void>(Shutdown({}));
                    return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene page unordered-access descriptor reservation failed", kind, pageIndex, rhiFailure);
                }
                impl->pageDirectory[DirectoryPageIndex(kind, pageIndex)] = {page.shaderResourceDescriptor.GpuIndex(), page.unorderedAccessDescriptor.GpuIndex(), pageIndex * elementsPerPage, elementsPerPage};
            }
        }

        rhi::BufferDesc directoryDesc;
        directoryDesc.size = sizeof(impl->tableDirectory);
        directoryDesc.structureStride = sizeof(GpuSceneTableDirectory);
        directoryDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::ShaderResource;
        directoryDesc.initialState = rhi::ResourceState::Common;
        impl->tableDirectoryBuffer = rhi::CreateBuffer(directoryDesc, {impl->tableDirectory, sizeof(impl->tableDirectory)}, &rhiFailure);
        if (!impl->tableDirectoryBuffer)
        {
            static_cast<void>(Shutdown({}));
            return Fail(failure, GpuSceneTablesFailureCode::BufferFailure, "GPU Scene table-directory buffer creation failed", GpuSceneTableKind::Count, 0, rhiFailure);
        }
        directoryDesc.size = sizeof(impl->pageDirectory);
        directoryDesc.structureStride = sizeof(GpuScenePageDirectoryEntry);
        impl->pageDirectoryBuffer = rhi::CreateBuffer(directoryDesc, {impl->pageDirectory, sizeof(impl->pageDirectory)}, &rhiFailure);
        if (!impl->pageDirectoryBuffer)
        {
            static_cast<void>(Shutdown({}));
            return Fail(failure, GpuSceneTablesFailureCode::BufferFailure, "GPU Scene page-directory buffer creation failed", GpuSceneTableKind::Count, 0, rhiFailure);
        }
        rhi::SetResourceDebugName(impl->tableDirectoryBuffer, "GPU Scene Table Directory");
        rhi::SetResourceDebugName(impl->pageDirectoryBuffer, "GPU Scene Page Directory");
        impl->consumerBuffers[impl->consumerBufferCount++] = impl->tableDirectoryBuffer;
        impl->consumerBuffers[impl->consumerBufferCount++] = impl->pageDirectoryBuffer;
        if (!rhi::WriteDescriptor(impl->resourceDescriptors, impl->tableDirectoryDescriptor, impl->tableDirectoryBuffer, rhi::BindingType::StructuredBufferShaderResource, {}, &rhiFailure) ||
            !rhi::WriteDescriptor(impl->resourceDescriptors, impl->pageDirectoryDescriptor, impl->pageDirectoryBuffer, rhi::BindingType::StructuredBufferShaderResource, {}, &rhiFailure))
        {
            static_cast<void>(Shutdown({}));
            return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene directory descriptor publication failed", GpuSceneTableKind::Count, 0, rhiFailure);
        }

        impl->stats.reservedDescriptors = requiredDescriptors;
        impl->stats.populatedDescriptors = 2;
        return true;
    }

    bool GpuSceneTables::Shutdown(const rhi::DescriptorRetirement& safeAfter, GpuSceneTablesFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, GpuSceneTablesFailureCode::WrongThread, "GPU Scene tables must shutdown on the main thread");

        Impl* const impl = m_impl;
        bool retired = true;
        retired = impl->RetireDescriptor(impl->tableDirectoryDescriptor, safeAfter) && retired;
        retired = impl->RetireDescriptor(impl->pageDirectoryDescriptor, safeAfter) && retired;
        for (u32 tableIndex = 0; tableIndex < GpuSceneTableCount; ++tableIndex)
        {
            Impl::Table& table = impl->tables[tableIndex];
            for (u32 pageIndex = 0; pageIndex < impl->maximumPagesPerTable; ++pageIndex)
            {
                Impl::Page& page = table.pages[pageIndex];
                retired = impl->RetireDescriptor(page.shaderResourceDescriptor, safeAfter) && retired;
                retired = impl->RetireDescriptor(page.unorderedAccessDescriptor, safeAfter) && retired;
                static_cast<void>(rhi::SafeRelease(page.buffer));
            }
        }
        if (!retired)
            return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene descriptor retirement failed");

        static_cast<void>(rhi::SafeRelease(impl->tableDirectoryBuffer));
        static_cast<void>(rhi::SafeRelease(impl->pageDirectoryBuffer));
        static_cast<void>(rhi::SafeRelease(impl->resourceDescriptors));
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    void GpuSceneTables::AbandonDevice() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        for (u32 tableIndex = 0; tableIndex < GpuSceneTableCount; ++tableIndex)
            for (u32 pageIndex = 0; pageIndex < impl->maximumPagesPerTable; ++pageIndex)
                static_cast<void>(rhi::SafeRelease(impl->tables[tableIndex].pages[pageIndex].buffer));
        static_cast<void>(rhi::SafeRelease(impl->tableDirectoryBuffer));
        static_cast<void>(rhi::SafeRelease(impl->pageDirectoryBuffer));
        static_cast<void>(rhi::SafeRelease(impl->resourceDescriptors));
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
    }

    bool GpuSceneTables::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool GpuSceneTables::EnsureCapacityRaw(const GpuSceneTableKind kind, const u32 stride, const u32 requiredElements, GpuSceneTablesFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, GpuSceneTablesFailureCode::NotInitialized, "GPU Scene tables are not initialized", kind);
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneTablesFailureCode::WrongThread, "GPU Scene table growth must run on the main thread", kind);
        }
        if (!IsValidTable(kind) || GetTableLayout(kind).stride != stride)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneTablesFailureCode::InvalidTable, "GPU Scene table type does not match its layout", kind);
        }
        Impl::Table& table = m_impl->tables[static_cast<u32>(kind)];
        if (requiredElements <= table.stats.elementCapacity)
            return true;
        const u64 requiredPages64 = (static_cast<u64>(requiredElements) + table.stats.elementsPerPage - 1u) / table.stats.elementsPerPage;
        if (requiredPages64 > table.stats.maximumPages)
        {
            ++m_impl->stats.rejectedOperations;
            return Fail(failure, GpuSceneTablesFailureCode::CapacityExceeded, "GPU Scene table reached its configured page capacity", kind, static_cast<u32>(requiredPages64));
        }
        const u32 requiredPages = static_cast<u32>(requiredPages64);

        const TableLayout layout = GetTableLayout(kind);
        rhi::Failure rhiFailure;
        while (table.stats.materializedPages < requiredPages)
        {
            const u32 pageIndex = table.stats.materializedPages;
            Impl::Page& page = table.pages[pageIndex];
            if (!page.buffer)
            {
                rhi::BufferDesc desc;
                desc.size = static_cast<u64>(table.stats.elementsPerPage) * stride;
                desc.structureStride = stride;
                desc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
                desc.initialState = rhi::ResourceState::Common;
                page.buffer = rhi::CreateBuffer(desc, {}, &rhiFailure);
                if (!page.buffer)
                {
                    ++table.stats.pageCreationFailures;
                    ++m_impl->stats.pageCreationFailures;
                    return Fail(failure, GpuSceneTablesFailureCode::BufferFailure, "GPU Scene page buffer creation failed", kind, pageIndex, rhiFailure);
                }
                rhi::SetResourceDebugName(page.buffer, layout.name);
            }
            if (!page.shaderResourcePublished)
            {
                if (!rhi::WriteDescriptor(m_impl->resourceDescriptors, page.shaderResourceDescriptor, page.buffer, rhi::BindingType::StructuredBufferShaderResource, {}, &rhiFailure))
                {
                    ++table.stats.pageCreationFailures;
                    ++m_impl->stats.pageCreationFailures;
                    return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene page shader-resource descriptor publication failed", kind, pageIndex, rhiFailure);
                }
                page.shaderResourcePublished = true;
                ++m_impl->stats.populatedDescriptors;
            }
            if (!page.unorderedAccessPublished)
            {
                if (!rhi::WriteDescriptor(m_impl->resourceDescriptors, page.unorderedAccessDescriptor, page.buffer, rhi::BindingType::StructuredBufferUnorderedAccess, {}, &rhiFailure))
                {
                    ++table.stats.pageCreationFailures;
                    ++m_impl->stats.pageCreationFailures;
                    return Fail(failure, GpuSceneTablesFailureCode::DescriptorFailure, "GPU Scene page unordered-access descriptor publication failed", kind, pageIndex, rhiFailure);
                }
                page.unorderedAccessPublished = true;
                ++m_impl->stats.populatedDescriptors;
            }
            const u64 pageBytes = static_cast<u64>(table.stats.elementsPerPage) * stride;
            m_impl->consumerBuffers[m_impl->consumerBufferCount++] = page.buffer;
            ++table.stats.materializedPages;
            table.stats.elementCapacity += table.stats.elementsPerPage;
            table.stats.allocatedBytes += pageBytes;
            ++m_impl->stats.materializedPages;
            m_impl->stats.allocatedBytes += pageBytes;
        }
        return true;
    }

    bool GpuSceneTables::EnsureCapacity(const GpuSceneTableKind kind, const u32 requiredElements, GpuSceneTablesFailure* const failure) noexcept
    {
        if (IsGpuSceneParallelTable(kind))
            return Fail(failure, GpuSceneTablesFailureCode::InvalidTable, "GPU Scene parallel capacity grows with its owner table", kind);
        const TableLayout layout = GetTableLayout(kind);
        if (!EnsureCapacityRaw(kind, layout.stride, requiredElements, failure))
            return false;
        const GpuSceneTableKind parallel = GetGpuSceneParallelTable(kind);
        if (parallel == GpuSceneTableKind::Count)
            return true;
        const TableLayout parallelLayout = GetTableLayout(parallel);
        return EnsureCapacityRaw(parallel, parallelLayout.stride, requiredElements, failure);
    }

    bool GpuSceneTables::GetPageRaw(const GpuSceneTableKind kind, const u32 stride, const u32 page, GpuSceneTablePage& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !IsValidTable(kind) || GetTableLayout(kind).stride != stride)
            return false;
        const Impl::Table& table = m_impl->tables[static_cast<u32>(kind)];
        if (page >= table.stats.materializedPages)
            return false;
        const Impl::Page& source = table.pages[page];
        output.buffer = source.buffer;
        output.shaderResourceDescriptor = source.shaderResourceDescriptor;
        output.unorderedAccessDescriptor = source.unorderedAccessDescriptor;
        output.firstElement = page * table.stats.elementsPerPage;
        output.elementCount = table.stats.elementsPerPage;
        return output.IsMaterialized();
    }

    bool GpuSceneTables::GetPage(const GpuSceneTableKind kind, const u32 page, GpuSceneTablePage& output) const noexcept
    {
        return GetPageRaw(kind, GetTableLayout(kind).stride, page, output);
    }

    bool GpuSceneTables::ResolveRaw(const GpuSceneTableKind kind, const u32 stride, const u32 index, GpuSceneElementAddress& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !IsValidTable(kind) || GetTableLayout(kind).stride != stride)
            return false;
        const Impl::Table& table = m_impl->tables[static_cast<u32>(kind)];
        if (index >= table.stats.elementCapacity)
            return false;
        const u32 pageIndex = index >> GetGpuScenePageShift(kind);
        const u32 localIndex = index & (table.stats.elementsPerPage - 1u);
        const Impl::Page& page = table.pages[pageIndex];
        if (!page.buffer || !page.shaderResourcePublished || !page.unorderedAccessPublished)
            return false;
        output.buffer = page.buffer;
        output.page = pageIndex;
        output.element = localIndex;
        output.byteOffset = static_cast<u64>(localIndex) * stride;
        output.shaderResourceDescriptor = page.shaderResourceDescriptor.GpuIndex();
        output.unorderedAccessDescriptor = page.unorderedAccessDescriptor.GpuIndex();
        return true;
    }

    bool GpuSceneTables::Resolve(const GpuSceneTableKind kind, const u32 index, GpuSceneElementAddress& output) const noexcept
    {
        return ResolveRaw(kind, GetTableLayout(kind).stride, index, output);
    }

    GpuSceneTableStats GpuSceneTables::GetTableStatsRaw(const GpuSceneTableKind kind, const u32 stride) const noexcept
    {
        if (m_impl == nullptr || !IsValidTable(kind) || GetTableLayout(kind).stride != stride)
            return {};
        return m_impl->tables[static_cast<u32>(kind)].stats;
    }

    GpuSceneTableStats GpuSceneTables::GetTableStats(const GpuSceneTableKind kind) const noexcept
    {
        const TableLayout layout = GetTableLayout(kind);
        return GetTableStatsRaw(kind, layout.stride);
    }

    containers::ArraySpan<const rhi::BufferRef> GpuSceneTables::GetConsumerBuffers() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const rhi::BufferRef>{m_impl->consumerBuffers, m_impl->consumerBufferCount}
                                 : containers::ArraySpan<const rhi::BufferRef>{};
    }

    GpuSceneDirectoryBinding GpuSceneTables::GetDirectoryBinding() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->tableDirectoryDescriptor.GpuIndex(), m_impl->pageDirectoryDescriptor.GpuIndex()};
    }

    bool GpuSceneTables::GetTableDirectory(const GpuSceneTableKind kind, GpuSceneTableDirectory& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !IsValidTable(kind))
            return false;
        output = m_impl->tableDirectory[static_cast<u32>(kind)];
        return true;
    }

    bool GpuSceneTables::GetPageDirectory(const GpuSceneTableKind kind, const u32 page, GpuScenePageDirectoryEntry& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr || !IsValidTable(kind) || page >= m_impl->maximumPagesPerTable)
            return false;
        output = m_impl->pageDirectory[DirectoryPageIndex(kind, page)];
        return true;
    }

    GpuSceneTablesStats GpuSceneTables::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : GpuSceneTablesStats{};
    }
} // namespace vanguard::rendering
