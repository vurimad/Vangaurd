#pragma once

#include <vanguard/assets/source_database.hpp>

namespace vanguard::assets
{
    enum class ProjectSourceDataResult : u8
    {
        Success,
        InvalidArgument,
        SourceUnavailable,
        MetadataUnavailable,
        CatalogOutOfDate,
        LimitExceeded,
        IoFailure,
        OutOfMemory
    };

    [[nodiscard]] const char* ToString(ProjectSourceDataResult result) noexcept;

    struct ProjectSourceDataLimits
    {
        u64 maximumSourceBytes = 2ull * 1024ull * 1024ull * 1024ull;
        u64 maximumMetadataBytes = MaximumAssetMetadataBytes;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumSourceBytes != 0 && maximumMetadataBytes != 0 && maximumSourceBytes <= ~u32{0} && maximumMetadataBytes <= ~u32{0};
        }
    };

    using ProjectSourceDataView = SourceDataView;

    // Reads the exact source and sidecar represented by one catalog row. A
    // sidecar changed since the catalog scan is rejected instead of combining
    // new bytes with stale importer settings or output identities.
    class ProjectSourceData final
    {
    public:
        ProjectSourceData() noexcept;

        ProjectSourceData(const ProjectSourceData&) = delete;
        ProjectSourceData& operator=(const ProjectSourceData&) = delete;

        void Reset() noexcept;
        [[nodiscard]] ProjectSourceDataView View(TargetPlatform target) const noexcept;

    private:
        containers::DynamicArray<u8> m_content;
        containers::DynamicArray<u8> m_metadata;

        friend ProjectSourceDataResult LoadProjectSourceData(filesystem::Manager&, const SourceRecord&, ProjectSourceData&,
                                                             const ProjectSourceDataLimits&) noexcept;
    };

    [[nodiscard]] ProjectSourceDataResult LoadProjectSourceData(filesystem::Manager& files, const SourceRecord& source,
                                                                ProjectSourceData& data, const ProjectSourceDataLimits& limits = {}) noexcept;

    // Stateful adapter for SourceDatabase::ScanReferences. The returned spans
    // remain valid until the next provider invocation, exactly matching that
    // API's borrowing contract. The same Load method is the E1E source boundary
    // for importer-owned build-request creation.
    class ProjectSourceDataProvider final
    {
    public:
        [[nodiscard]] bool Initialize(filesystem::Manager& files, TargetPlatform target,
                                      const ProjectSourceDataLimits& limits = {}) noexcept;
        void Reset() noexcept;
        [[nodiscard]] bool Load(const SourceRecord& source, ProjectSourceDataView& data) noexcept;
        [[nodiscard]] ProjectSourceDataResult GetLastResult() const noexcept;

        [[nodiscard]] static bool LoadReferenceScanData(const SourceRecord& source, SourceDataView& data, void* userData) noexcept;

    private:
        filesystem::Manager* m_files = nullptr;
        TargetPlatform m_target = TargetPlatform::WindowsD3D12;
        ProjectSourceDataLimits m_limits;
        ProjectSourceData m_data;
        ProjectSourceDataResult m_lastResult = ProjectSourceDataResult::InvalidArgument;
    };
} // namespace vanguard::assets
