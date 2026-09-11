#include <vanguard/rendering/render_graph_cache.hpp>

#include <vanguard/memory/pool.hpp>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u64 HashOffset = 14695981039346656037ull;
        inline constexpr u64 HashPrime = 1099511628211ull;

        template <typename Type> void AppendHash(u64& hash, const Type& value) noexcept
        {
            const u8* const bytes = reinterpret_cast<const u8*>(&value);
            for (usize index = 0; index < sizeof(Type); ++index)
            {
                hash ^= bytes[index];
                hash *= HashPrime;
            }
        }

        void DeleteCameraBuildData(containers::DynamicArray<RenderGraphCache::CameraSetupData*>& cameraBuildData) noexcept
        {
            for (RenderGraphCache::CameraSetupData* const camera : cameraBuildData)
                VANGUARD_DELETE(camera);
            cameraBuildData.Clear();
        }
    } // namespace

    void RenderViewGraphKey::RebuildHash() noexcept
    {
        hash = HashOffset;
        AppendHash(hash, mode);
        AppendHash(hash, purpose);
        AppendHash(hash, flags);
        AppendHash(hash, phases);
        AppendHash(hash, featureMask);
        AppendHash(hash, width);
        AppendHash(hash, height);
    }

    bool RenderViewGraphKey::IsValid() const noexcept
    {
        return hash != 0 && hash != HashOffset && width != 0 && height != 0;
    }

    bool RenderViewGraphKey::operator==(const RenderViewGraphKey& other) const noexcept
    {
        return hash == other.hash && mode == other.mode && purpose == other.purpose && flags == other.flags && phases == other.phases &&
               featureMask == other.featureMask && width == other.width && height == other.height;
    }

    void RenderGraphKey::RebuildHash() noexcept
    {
        hash = HashOffset;
        AppendHash(hash, mode);
        AppendHash(hash, purpose);
        AppendHash(hash, outputKind);
        AppendHash(hash, renderExtent.width);
        AppendHash(hash, renderExtent.height);
        AppendHash(hash, outputExtent.width);
        AppendHash(hash, outputExtent.height);
        AppendHash(hash, featureMask);
        AppendHash(hash, rendererRevision);
        AppendHash(hash, viewCount);
        for (u32 index = 0; index < viewCount && index < MaximumRenderViewsPerFamily; ++index)
            AppendHash(hash, views[index].hash);
        AppendHash(hash, dependencyCount);
        for (u32 index = 0; index < dependencyCount && index < MaximumRenderViewsPerFamily * MaximumRenderCameraDependencies; ++index)
        {
            AppendHash(hash, dependencies[index].parentViewIndex);
            AppendHash(hash, dependencies[index].childViewIndex);
            AppendHash(hash, dependencies[index].outputs);
        }
        AppendHash(hash, present);
    }

    bool RenderGraphKey::IsValid() const noexcept
    {
        if (hash == 0 || hash == HashOffset || !renderExtent.IsValid() || !outputExtent.IsValid() || viewCount > MaximumRenderViewsPerFamily ||
            dependencyCount > MaximumRenderViewsPerFamily * MaximumRenderCameraDependencies)
            return false;
        for (u32 index = 0; index < viewCount; ++index)
            if (!views[index].IsValid())
                return false;
        return true;
    }

    bool RenderGraphKey::operator==(const RenderGraphKey& other) const noexcept
    {
        if (hash != other.hash || mode != other.mode || purpose != other.purpose || outputKind != other.outputKind || renderExtent != other.renderExtent ||
            outputExtent != other.outputExtent || featureMask != other.featureMask || rendererRevision != other.rendererRevision || viewCount != other.viewCount ||
            dependencyCount != other.dependencyCount || present != other.present)
            return false;
        for (u32 index = 0; index < viewCount; ++index)
            if (!(views[index] == other.views[index]))
                return false;
        for (u32 index = 0; index < dependencyCount; ++index)
            if (!(dependencies[index] == other.dependencies[index]))
                return false;
        return true;
    }

    bool RenderGraphCache::CameraSetupData::DoesNeedRebuild(const RenderViewGraphKey& other) noexcept
    {
        const bool needsRebuild = !cameraSetupKey.IsValid() || !(cameraSetupKey == other);
        cameraSetupKey = other;
        return needsRebuild;
    }

    RenderGraphCache::CacheEntry::CacheEntry() : cameraBuildData(memory::pools::Rendering::GetInstance()) {}

    RenderGraphCache::CacheEntry::~CacheEntry()
    {
        DeleteCameraBuildData(cameraBuildData);
    }

    void RenderGraphCache::CacheEntry::PostBuildClear() noexcept
    {
        for (CameraSetupData* const cameraSetupData : cameraBuildData)
            cameraSetupData->graph.Reset();
        valid = true;
    }

    RenderGraphCache::CameraSetupData* RenderGraphCache::CacheEntry::FindCameraSetup(const RenderViewGraphKey& cameraKey) const noexcept
    {
        for (CameraSetupData* const cameraSetupData : cameraBuildData)
            if (!cameraSetupData->cameraSetupKey.IsValid() || cameraSetupData->cameraSetupKey == cameraKey)
                return cameraSetupData;
        return nullptr;
    }

    void RenderGraphCache::CacheEntry::Reset() noexcept
    {
        graph.Reset();
        DeleteCameraBuildData(cameraBuildData);
        graphKey = {};
        lastUsedFrame = 0;
        valid = false;
    }

    RenderGraphCache::CacheEntry* RenderGraphCache::GetGraph(const u64 frameSerial, const RenderGraphKey& key, const u32 cameraSetupCount,
                                                              bool& needsRebuild, const bool forceClear) noexcept
    {
        needsRebuild = false;
        if (frameSerial == 0 || !key.IsValid() || cameraSetupCount > MaximumRenderViewsPerFamily)
            return nullptr;

        u64 oldestFrame = ~u64{0};
        u32 oldestEntryIndex = 0;
        for (u32 index = 0; index < m_cacheEntries.Size(); ++index)
        {
            CacheEntry& entry = m_cacheEntries[index];
            if (entry.valid && entry.graphKey.hash == key.hash && entry.graphKey == key && entry.cameraBuildData.Size() == cameraSetupCount && !forceClear)
            {
                entry.lastUsedFrame = frameSerial;
                return &entry;
            }
            if (entry.lastUsedFrame < oldestFrame)
            {
                oldestFrame = entry.lastUsedFrame;
                oldestEntryIndex = index;
            }
        }

        CacheEntry& entry = m_cacheEntries[oldestEntryIndex];
        entry.Reset();
        entry.cameraBuildData.Reserve(cameraSetupCount);
        for (u32 index = 0; index < cameraSetupCount; ++index)
        {
            CameraSetupData* const cameraSetup = VANGUARD_NEW(CameraSetupData);
            if (cameraSetup == nullptr)
            {
                entry.Reset();
                return nullptr;
            }
            entry.cameraBuildData.PushBack(cameraSetup);
        }
        entry.graphKey = key;
        entry.lastUsedFrame = frameSerial;
        needsRebuild = true;
        return &entry;
    }

    void RenderGraphCache::Clear() noexcept
    {
        for (CacheEntry& entry : m_cacheEntries)
            entry.Reset();
    }
} // namespace vanguard::rendering
