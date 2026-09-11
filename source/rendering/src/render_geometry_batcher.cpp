#include <vanguard/rendering/render_geometry_batcher.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/mesh_residency.hpp>
#include <vanguard/system/assert.hpp>

#include <new>
#include <utility>

namespace vanguard::rendering
{
    namespace
    {
        void HashWord(u32& hash, const u32 word) noexcept
        {
            hash = (hash ^ word) * 16777619u;
        }

        template <typename T, typename... Args> T* NewObject(Args&&... args) noexcept
        {
            const auto block = memory::Allocate(memory::PoolId::Rendering, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(std::forward<Args>(args)...) : nullptr;
        }

        template <typename T> void DeleteObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Rendering};
            memory::Free(block);
        }

        // Resource-time indexed interning. No full-table search during admission
        // or recording, and no material-value key or per-view output in this table.
        template <typename Key> struct BatchTable
        {
            struct Slot
            {
                Key key;
                u32 generation = 1;
                u32 references = 0;
            };

            explicit BatchTable(const u32 limit) noexcept
                : slots(memory::pools::Rendering::GetInstance()), freeSlots(memory::pools::Rendering::GetInstance()),
                  lookup(memory::pools::Rendering::GetInstance()), maximum(limit)
            {
                slots.Reserve(limit);
                freeSlots.Reserve(limit);
                lookup.Reserve(limit);
            }

            [[nodiscard]] bool Acquire(const Key& key, GeometryBatchId& output, bool& activated) noexcept
            {
                activated = false;
                u32 index = InvalidGeometryIndex;
                if (lookup.Find(key, index))
                {
                    auto& slot = slots[index];
                    if (slot.references == 0xffffffffu)
                        return false;
                    ++slot.references;
                    output = {index, slot.generation};
                    return true;
                }
                if (freeSlots.Empty() && slots.Size() >= maximum)
                    return false;
                if (!freeSlots.Empty())
                {
                    index = freeSlots.Back();
                    freeSlots.PopBack();
                }
                else
                {
                    index = slots.Size();
                    slots.PushBack({});
                }
                auto& slot = slots[index];
                slot.key = key;
                if (!lookup.Insert(key, index).IsSuccessful())
                {
                    freeSlots.PushBack(index);
                    return false;
                }
                slot.references = 1;
                output = {index, slot.generation};
                activated = true;
                return true;
            }

            [[nodiscard]] bool Release(const GeometryBatchId id) noexcept
            {
                if (!id.IsValid())
                    return false;
                if (id.index >= slots.Size())
                    VG_FATAL("geometry batch release references an out-of-range slot");
                auto& slot = slots[id.index];
                if (slot.generation != id.generation || slot.references == 0)
                    VG_FATAL("geometry batch release references a stale or unowned slot");
                if (--slot.references != 0)
                    return false;
                static_cast<void>(lookup.Remove(slot.key));
                // Exhaust a slot instead of permitting an ABA after generation wrap.
                if (slot.generation != 0xffffffffu)
                {
                    ++slot.generation;
                    freeSlots.PushBack(id.index);
                }
                return true;
            }

            [[nodiscard]] const Slot* Get(const u32 index) const noexcept { return index < slots.Size() ? &slots[index] : nullptr; }

            containers::DynamicArray<Slot> slots;
            containers::DynamicArray<u32> freeSlots;
            containers::HashMap<Key, u32> lookup;
            u32 maximum;
        };
    } // namespace

    u32 GeometryShellKey::CalcHash() const noexcept
    {
        u32 hash = 2166136261u;
        HashWord(hash, phase.index);
        HashWord(hash, pipeline.index);
        HashWord(hash, pipeline.generation);
        HashWord(hash, vertexArena.index);
        HashWord(hash, vertexArena.generation);
        HashWord(hash, indexArena.index);
        HashWord(hash, indexArena.generation);
        HashWord(hash, static_cast<u32>(indexFormat));
        HashWord(hash, depthTest);
        HashWord(hash, reverseDepth);
        return hash;
    }

    u32 GeometryBinKey::CalcHash() const noexcept
    {
        u32 hash = 2166136261u;
        HashWord(hash, shell.index);
        HashWord(hash, shell.generation);
        HashWord(hash, geometry.index);
        HashWord(hash, geometry.generation);
        HashWord(hash, allocation.index);
        HashWord(hash, allocation.generation);
        HashWord(hash, firstIndex);
        HashWord(hash, indexCount);
        HashWord(hash, firstVertex);
        HashWord(hash, vertexCount);
        return hash;
    }

    struct RetainedGeometryBatch
    {
        RenderGeometryBatcher* owner = nullptr;
        RetainedGeometryBatch* previous = nullptr;
        RetainedGeometryBatch* next = nullptr;
        u32 references = 1;
        GeometryBatchPlacement placement;
        MeshDrawPreparation preparation;
    };

    struct RenderGeometryBatcher::Impl
    {
        struct ShellCatalogState
        {
            ShellCatalogState() noexcept : bins(memory::pools::Rendering::GetInstance()) {}
            containers::DynamicArray<GeometryBatchId> bins;
            u32 phaseIndex = InvalidGeometryIndex;
            bool dirty = false;
        };

        struct BinCatalogState
        {
            u32 shellOrdinal = InvalidGeometryIndex;
            bool dirty = false;
        };

        struct PhaseShellCatalog
        {
            PhaseShellCatalog() noexcept : entries(memory::pools::Rendering::GetInstance()) {}
            containers::DynamicArray<GeometryShellCatalogEntry> entries;
        };

        explicit Impl(const RenderGeometryBatcherConfig& config) noexcept
            : shells(config.maximumShells), bins(config.maximumBins), shellCatalog(memory::pools::Rendering::GetInstance()),
              binCatalog(memory::pools::Rendering::GetInstance()), dirtyShells(memory::pools::Rendering::GetInstance()),
              dirtyBins(memory::pools::Rendering::GetInstance()), maximumPreparations(config.maximumPreparations)
        {
            shellCatalog.Reserve(config.maximumShells);
            binCatalog.Reserve(config.maximumBins);
            dirtyShells.Reserve(config.maximumShells);
            dirtyBins.Reserve(config.maximumBins);
        }

        [[nodiscard]] ShellCatalogState& EnsureShellState(const u32 index) noexcept
        {
            while (shellCatalog.Size() <= index)
                shellCatalog.EmplaceBack();
            return shellCatalog[index];
        }

        [[nodiscard]] BinCatalogState& EnsureBinState(const u32 index) noexcept
        {
            while (binCatalog.Size() <= index)
                binCatalog.PushBack({});
            return binCatalog[index];
        }

        void MarkShellDirty(const u32 index) noexcept
        {
            auto& state = EnsureShellState(index);
            if (!state.dirty)
            {
                state.dirty = true;
                dirtyShells.PushBack(index);
            }
            ++catalogRevision;
            if (catalogRevision == 0)
                VG_FATAL("geometry batch catalog revision exhausted");
        }

        void MarkBinDirty(const u32 index) noexcept
        {
            auto& state = EnsureBinState(index);
            if (!state.dirty)
            {
                state.dirty = true;
                dirtyBins.PushBack(index);
            }
            ++catalogRevision;
            if (catalogRevision == 0)
                VG_FATAL("geometry batch catalog revision exhausted");
        }

        void ActivateShell(const GeometryBatchId id, const GeometryShellKey& key) noexcept
        {
            VG_ASSERT(id.IsValid() && key.phase.IsValid());
            auto& state = EnsureShellState(id.index);
            VG_ASSERT(state.phaseIndex == InvalidGeometryIndex && state.bins.Empty());
            auto& phase = phaseShells[key.phase.index].entries;
            state.phaseIndex = phase.Size();
            phase.PushBack({id, key, 0, true});
            MarkShellDirty(id.index);
            ++activeShells;
        }

        void DeactivateShell(const GeometryBatchId id, const GeometryShellKey& key) noexcept
        {
            VG_ASSERT(id.IsValid() && key.phase.IsValid() && id.index < shellCatalog.Size());
            auto& state = shellCatalog[id.index];
            auto& phase = phaseShells[key.phase.index].entries;
            VG_ASSERT(state.phaseIndex < phase.Size() && phase[state.phaseIndex].id == id && state.bins.Empty());
            const u32 removed = state.phaseIndex;
            if (removed + 1u != phase.Size())
            {
                phase[removed] = phase.Back();
                shellCatalog[phase[removed].id.index].phaseIndex = removed;
            }
            phase.PopBack();
            state.phaseIndex = InvalidGeometryIndex;
            MarkShellDirty(id.index);
            --activeShells;
        }

        void ActivateBin(const GeometryBatchId id, const GeometryBinKey& key) noexcept
        {
            VG_ASSERT(id.IsValid() && key.shell.IsValid() && key.shell.index < shellCatalog.Size());
            auto& shell = shellCatalog[key.shell.index];
            auto& state = EnsureBinState(id.index);
            VG_ASSERT(shell.phaseIndex != InvalidGeometryIndex && state.shellOrdinal == InvalidGeometryIndex);
            state.shellOrdinal = shell.bins.Size();
            shell.bins.PushBack(id);
            const auto* shellSlot = shells.Get(key.shell.index);
            VG_ASSERT(shellSlot != nullptr && shellSlot->generation == key.shell.generation);
            phaseShells[shellSlot->key.phase.index].entries[shell.phaseIndex].binCount = shell.bins.Size();
            MarkShellDirty(key.shell.index);
            MarkBinDirty(id.index);
            ++activeBins;
        }

        void DeactivateBin(const GeometryBatchId id, const GeometryBinKey& key) noexcept
        {
            VG_ASSERT(id.IsValid() && key.shell.IsValid() && key.shell.index < shellCatalog.Size() && id.index < binCatalog.Size());
            auto& shell = shellCatalog[key.shell.index];
            auto& state = binCatalog[id.index];
            VG_ASSERT(state.shellOrdinal < shell.bins.Size() && shell.bins[state.shellOrdinal] == id);
            const u32 removed = state.shellOrdinal;
            if (removed + 1u != shell.bins.Size())
            {
                shell.bins[removed] = shell.bins.Back();
                auto& moved = binCatalog[shell.bins[removed].index];
                moved.shellOrdinal = removed;
                MarkBinDirty(shell.bins[removed].index);
            }
            shell.bins.PopBack();
            state.shellOrdinal = InvalidGeometryIndex;
            const auto* shellSlot = shells.Get(key.shell.index);
            VG_ASSERT(shellSlot != nullptr && shellSlot->generation == key.shell.generation);
            phaseShells[shellSlot->key.phase.index].entries[shell.phaseIndex].binCount = shell.bins.Size();
            MarkShellDirty(key.shell.index);
            MarkBinDirty(id.index);
            --activeBins;
        }

        BatchTable<GeometryShellKey> shells;
        BatchTable<GeometryBinKey> bins;
        PhaseShellCatalog phaseShells[MaximumRenderPhases];
        containers::DynamicArray<ShellCatalogState> shellCatalog;
        containers::DynamicArray<BinCatalogState> binCatalog;
        containers::DynamicArray<u32> dirtyShells;
        containers::DynamicArray<u32> dirtyBins;
        u64 catalogRevision = 1;
        u32 activeShells = 0;
        u32 activeBins = 0;
        u32 maximumPreparations;
        u32 preparations = 0;
        RetainedGeometryBatch* first = nullptr;
    };

    GeometryBatchLease::~GeometryBatchLease() { Reset(); }

    GeometryBatchLease::GeometryBatchLease(GeometryBatchLease&& other) noexcept : m_batch(std::exchange(other.m_batch, nullptr)) {}

    GeometryBatchLease& GeometryBatchLease::operator=(GeometryBatchLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_batch = std::exchange(other.m_batch, nullptr);
        }
        return *this;
    }

    bool GeometryBatchLease::IsValid() const noexcept { return m_batch != nullptr && m_batch->owner != nullptr; }

    const GeometryBatchPlacement* GeometryBatchLease::GetPlacement() const noexcept
    {
        return IsValid() ? &m_batch->placement : nullptr;
    }

    bool GeometryBatchLease::Retain(GeometryBatchLease& output) const noexcept
    {
        if (!concurrency::IsMainThread() || !IsValid() || output.m_batch != nullptr || m_batch->references == 0xffffffffu)
            return false;
        ++m_batch->references;
        output.m_batch = m_batch;
        return true;
    }

    void GeometryBatchLease::Reset() noexcept
    {
        if (m_batch == nullptr)
            return;
        if (!concurrency::IsMainThread())
            VG_FATAL("geometry batch leases must be released on the main thread after recording jobs join");
        auto* batch = std::exchange(m_batch, nullptr);
        if (--batch->references == 0)
        {
            if (batch->owner != nullptr)
                batch->owner->Release(batch);
            else
                DeleteObject(batch);
        }
    }

    RenderGeometryBatcher::~RenderGeometryBatcher()
    {
        VG_ASSERT_MSG(m_impl == nullptr, "geometry batcher requires explicit shutdown with no retained leases");
    }

    bool RenderGeometryBatcher::Initialize(const RenderGeometryBatcherConfig& config) noexcept
    {
        if (!concurrency::IsMainThread() || m_impl != nullptr || config.maximumShells == 0 || config.maximumBins == 0 || config.maximumPreparations == 0 ||
            config.maximumShells == InvalidGeometryIndex || config.maximumBins == InvalidGeometryIndex)
            return false;
        m_impl = NewObject<Impl>(config);
        return m_impl != nullptr;
    }

    GeometryBatchResult RenderGeometryBatcher::Shutdown() noexcept
    {
        if (!concurrency::IsMainThread())
            return GeometryBatchResult::WrongThread;
        if (HasLiveLeases())
            return GeometryBatchResult::LiveLeasesRemain;
        VG_ASSERT(m_impl == nullptr || (m_impl->activeShells == 0 && m_impl->activeBins == 0));
        DeleteObject(m_impl);
        m_impl = nullptr;
        return GeometryBatchResult::Success;
    }

    bool RenderGeometryBatcher::HasLiveLeases() const noexcept { return m_impl != nullptr && m_impl->preparations != 0; }

    void RenderGeometryBatcher::AbandonDevice() noexcept
    {
        if (!concurrency::IsMainThread())
            VG_FATAL("geometry batcher device abandonment must run after the main-thread recording join");
        while (m_impl != nullptr && m_impl->first != nullptr)
        {
            auto* batch = m_impl->first;
            Detach(batch);
            // Leases retain the small invalid CPU object, never dead manager or
            // material pointers. Their last reset can occur after owner teardown.
            batch->preparation = {};
            batch->placement = {};
            batch->owner = nullptr;
        }
        static_cast<void>(Shutdown());
    }

    GeometryBatchResult RenderGeometryBatcher::Acquire(MeshDrawPreparation& preparation, MaterialResidencyRuntime& materials, GeometryBatchLease& output) noexcept
    {
        if (m_impl == nullptr)
            return GeometryBatchResult::NotInitialized;
        if (!concurrency::IsMainThread())
            return GeometryBatchResult::WrongThread;
        if (output.m_batch != nullptr || !preparation.mesh.IsValid() || !preparation.geometry.IsValid() || !preparation.placement.IsValid() || !preparation.phase.IsValid())
            return GeometryBatchResult::InvalidPreparation;
        MaterialTechniqueInfo normal;
        MaterialTechniqueInfo mirrored;
        const bool normalResolved = materials.GetTechniqueInfo(preparation.normal, normal);
        if (!normalResolved)
            return GeometryBatchResult::InvalidPreparation;
        const bool mirroredResolved = materials.GetTechniqueInfo(preparation.mirrored, mirrored);
        if (!mirroredResolved)
            return GeometryBatchResult::InvalidPreparation;
        if (normal.state == MaterialTechniqueState::Failed || mirrored.state == MaterialTechniqueState::Failed)
            return GeometryBatchResult::FailedTechnique;
        if (normal.state == MaterialTechniqueState::Pending || mirrored.state == MaterialTechniqueState::Pending)
            return GeometryBatchResult::Pending;
        if (normal.state != MaterialTechniqueState::Ready || mirrored.state != MaterialTechniqueState::Ready || !normal.pipeline.IsValid() || !mirrored.pipeline.IsValid() ||
            !normal.material.IsValid() || normal.material != mirrored.material)
            return GeometryBatchResult::InvalidPreparation;
        if (m_impl->preparations >= m_impl->maximumPreparations)
            return GeometryBatchResult::CapacityExceeded;
        auto* batch = NewObject<RetainedGeometryBatch>();
        if (batch == nullptr)
            return GeometryBatchResult::CapacityExceeded;
        auto& placement = batch->placement;
        const auto& geometry = preparation.placement;
        placement.normalState = {preparation.phase, normal.pipeline, geometry.vertex.arena, geometry.index.arena, geometry.index.format, normal.depthTest, normal.reverseDepth};
        placement.mirroredState = {preparation.phase, mirrored.pipeline, geometry.vertex.arena, geometry.index.arena, geometry.index.format, mirrored.depthTest, mirrored.reverseDepth};
        // Acquire both variants transactionally. Only identical effective PSOs
        // share destinations; cull-none alone does not erase front-face identity.
        bool normalShellActivated = false;
        bool mirroredShellActivated = false;
        bool normalBinActivated = false;
        bool mirroredBinActivated = false;
        bool ready = m_impl->shells.Acquire(placement.normalState, placement.normalShell, normalShellActivated);
        if (ready && normalShellActivated)
            m_impl->ActivateShell(placement.normalShell, placement.normalState);
        if (ready)
            ready = m_impl->shells.Acquire(placement.mirroredState, placement.mirroredShell, mirroredShellActivated);
        if (ready && mirroredShellActivated)
            m_impl->ActivateShell(placement.mirroredShell, placement.mirroredState);
        if (ready)
        {
            placement.normalGeometry = {placement.normalShell, preparation.geometry, geometry.allocation, geometry.index.firstIndex, geometry.index.indexCount,
                                        geometry.vertex.firstVertex, geometry.vertex.vertexCount};
            placement.mirroredGeometry = placement.normalGeometry;
            placement.mirroredGeometry.shell = placement.mirroredShell;
            ready = m_impl->bins.Acquire(placement.normalGeometry, placement.normalBin, normalBinActivated);
            if (ready && normalBinActivated)
                m_impl->ActivateBin(placement.normalBin, placement.normalGeometry);
            if (ready)
                ready = m_impl->bins.Acquire(placement.mirroredGeometry, placement.mirroredBin, mirroredBinActivated);
            if (ready && mirroredBinActivated)
                m_impl->ActivateBin(placement.mirroredBin, placement.mirroredGeometry);
        }
        if (!ready)
        {
            const bool mirroredBinReleased = m_impl->bins.Release(placement.mirroredBin);
            if (mirroredBinReleased)
                m_impl->DeactivateBin(placement.mirroredBin, placement.mirroredGeometry);
            const bool normalBinReleased = m_impl->bins.Release(placement.normalBin);
            if (normalBinReleased)
                m_impl->DeactivateBin(placement.normalBin, placement.normalGeometry);
            const bool mirroredShellReleased = m_impl->shells.Release(placement.mirroredShell);
            if (mirroredShellReleased)
                m_impl->DeactivateShell(placement.mirroredShell, placement.mirroredState);
            const bool normalShellReleased = m_impl->shells.Release(placement.normalShell);
            if (normalShellReleased)
                m_impl->DeactivateShell(placement.normalShell, placement.normalState);
            DeleteObject(batch);
            return GeometryBatchResult::CapacityExceeded;
        }
        placement.material = normal.material;
        placement.sourceSubmesh = preparation.sourceSubmesh;
        batch->owner = this;
        batch->preparation = std::move(preparation);
        batch->next = m_impl->first;
        if (batch->next != nullptr)
            batch->next->previous = batch;
        m_impl->first = batch;
        ++m_impl->preparations;
        output.m_batch = batch;
        return GeometryBatchResult::Success;
    }

    void RenderGeometryBatcher::Release(RetainedGeometryBatch* batch) noexcept
    {
        Detach(batch);
        // Last release returns techniques/material/mesh demand to their existing
        // owners. No native destruction, private fence or submission is invented.
        DeleteObject(batch);
    }

    void RenderGeometryBatcher::Detach(RetainedGeometryBatch* batch) noexcept
    {
        VG_ASSERT(m_impl != nullptr && batch->owner == this && m_impl->preparations != 0);
        const auto& placement = batch->placement;
        const bool mirroredBinReleased = m_impl->bins.Release(placement.mirroredBin);
        if (mirroredBinReleased)
            m_impl->DeactivateBin(placement.mirroredBin, placement.mirroredGeometry);
        const bool normalBinReleased = m_impl->bins.Release(placement.normalBin);
        if (normalBinReleased)
            m_impl->DeactivateBin(placement.normalBin, placement.normalGeometry);
        const bool mirroredShellReleased = m_impl->shells.Release(placement.mirroredShell);
        if (mirroredShellReleased)
            m_impl->DeactivateShell(placement.mirroredShell, placement.mirroredState);
        const bool normalShellReleased = m_impl->shells.Release(placement.normalShell);
        if (normalShellReleased)
            m_impl->DeactivateShell(placement.normalShell, placement.normalState);
        if (batch->previous != nullptr)
            batch->previous->next = batch->next;
        else
            m_impl->first = batch->next;
        if (batch->next != nullptr)
            batch->next->previous = batch->previous;
        batch->previous = nullptr;
        batch->next = nullptr;
        --m_impl->preparations;
    }

    containers::ArraySpan<const GeometryShellCatalogEntry> RenderGeometryBatcher::GetPhaseShells(const RenderPhaseId phase) const noexcept
    {
        if (m_impl == nullptr || !phase.IsValid())
            return {};
        const auto& entries = m_impl->phaseShells[phase.index].entries;
        return {entries.TypedData(), entries.Size()};
    }

    containers::ArraySpan<const GeometryBatchId> RenderGeometryBatcher::GetShellBins(const GeometryBatchId shell) const noexcept
    {
        if (m_impl == nullptr || !shell.IsValid() || shell.index >= m_impl->shellCatalog.Size())
            return {};
        const auto* slot = m_impl->shells.Get(shell.index);
        const auto& state = m_impl->shellCatalog[shell.index];
        if (slot == nullptr || slot->generation != shell.generation || slot->references == 0 || state.phaseIndex == InvalidGeometryIndex)
            return {};
        return {state.bins.TypedData(), state.bins.Size()};
    }

    bool RenderGeometryBatcher::GetShellCatalogEntry(const GeometryBatchId shell, GeometryShellCatalogEntry& output) const noexcept
    {
        output = {};
        return shell.IsValid() && GetShellCatalogEntry(shell.index, output) && output.active && output.id == shell;
    }

    bool RenderGeometryBatcher::GetBinCatalogEntry(const GeometryBatchId bin, GeometryBinCatalogEntry& output) const noexcept
    {
        output = {};
        return bin.IsValid() && GetBinCatalogEntry(bin.index, output) && output.active && output.id == bin;
    }

    bool RenderGeometryBatcher::GetShellCatalogEntry(const u32 index, GeometryShellCatalogEntry& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr)
            return false;
        const auto* slot = m_impl->shells.Get(index);
        if (slot == nullptr)
            return false;
        output.id = {index, slot->generation};
        output.state = slot->key;
        output.active = slot->references != 0;
        if (output.active)
        {
            const auto& catalog = m_impl->shellCatalog[index];
            output.binCount = catalog.bins.Size();
        }
        return true;
    }

    bool RenderGeometryBatcher::GetBinCatalogEntry(const u32 index, GeometryBinCatalogEntry& output) const noexcept
    {
        output = {};
        if (m_impl == nullptr)
            return false;
        const auto* slot = m_impl->bins.Get(index);
        if (slot == nullptr)
            return false;
        output.id = {index, slot->generation};
        output.shell = slot->key.shell;
        output.geometry = slot->key.geometry;
        output.allocation = slot->key.allocation;
        output.firstIndex = slot->key.firstIndex;
        output.indexCount = slot->key.indexCount;
        output.firstVertex = slot->key.firstVertex;
        output.vertexCount = slot->key.vertexCount;
        output.active = slot->references != 0;
        if (output.active)
            output.shellOrdinal = m_impl->binCatalog[index].shellOrdinal;
        return true;
    }

    GeometryBatchCatalogChanges RenderGeometryBatcher::GetCatalogChanges() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {{m_impl->dirtyShells.TypedData(), m_impl->dirtyShells.Size()}, {m_impl->dirtyBins.TypedData(), m_impl->dirtyBins.Size()}, m_impl->catalogRevision};
    }

    bool RenderGeometryBatcher::AcknowledgeCatalogChanges(const u64 revision) noexcept
    {
        if (m_impl == nullptr || !concurrency::IsMainThread() || revision == 0 || revision != m_impl->catalogRevision)
            return false;
        for (const u32 index : m_impl->dirtyShells)
            m_impl->shellCatalog[index].dirty = false;
        for (const u32 index : m_impl->dirtyBins)
            m_impl->binCatalog[index].dirty = false;
        m_impl->dirtyShells.Clear();
        m_impl->dirtyBins.Clear();
        return true;
    }

    GeometryBatchCatalogStats RenderGeometryBatcher::GetCatalogStats() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        return {m_impl->activeShells, m_impl->activeBins, m_impl->dirtyShells.Size(), m_impl->dirtyBins.Size(), m_impl->catalogRevision};
    }
} // namespace vanguard::rendering
