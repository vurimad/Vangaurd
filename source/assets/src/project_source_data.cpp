#include <vanguard/assets/project_source_data.hpp>

namespace vanguard::assets
{
    namespace
    {
        [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
        {
            const containers::StringView view = source.AsStringView();
            containers::String path(view.Data(), view.Length());
            path.Append(".vmeta", 6);
            return filesystem::AbsolutePath::CreateFilePath(path);
        }

        [[nodiscard]] ProjectSourceDataResult ReadFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                       const u64 maximumBytes, containers::DynamicArray<u8>& bytes,
                                                       const ProjectSourceDataResult unavailable) noexcept
        {
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return unavailable;
            const u64 size = reader->GetSize();
            if (size > maximumBytes || size > ~u32{0})
                return ProjectSourceDataResult::LimitExceeded;
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
                return ProjectSourceDataResult::OutOfMemory;
            if (size != 0)
                reader->Serialize(bytes.TypedData(), static_cast<usize>(size));
            if (reader->HasErrors())
            {
                bytes.Clear();
                return ProjectSourceDataResult::IoFailure;
            }
            return ProjectSourceDataResult::Success;
        }

        [[nodiscard]] bool SameBytes(const containers::ArraySpan<const u8> left, const containers::ArraySpan<const u8> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0; index < left.Count(); ++index)
                if (left[index] != right[index])
                    return false;
            return true;
        }

        [[nodiscard]] bool SameMetadata(const AssetMetadata& left, const AssetMetadata& right) noexcept
        {
            if (left.id != right.id || left.importer != right.importer || left.importerVersion != right.importerVersion ||
                left.settingsVersion != right.settingsVersion || !SameBytes(left.settings, right.settings) || left.outputs.Size() != right.outputs.Size())
                return false;
            for (u32 index = 0; index < left.outputs.Size(); ++index)
                if (left.outputs[index].key != right.outputs[index].key || left.outputs[index].type != right.outputs[index].type)
                    return false;
            return true;
        }
    } // namespace

    const char* ToString(const ProjectSourceDataResult result) noexcept
    {
        switch (result)
        {
        case ProjectSourceDataResult::Success: return "Success";
        case ProjectSourceDataResult::InvalidArgument: return "InvalidArgument";
        case ProjectSourceDataResult::SourceUnavailable: return "SourceUnavailable";
        case ProjectSourceDataResult::MetadataUnavailable: return "MetadataUnavailable";
        case ProjectSourceDataResult::CatalogOutOfDate: return "CatalogOutOfDate";
        case ProjectSourceDataResult::LimitExceeded: return "LimitExceeded";
        case ProjectSourceDataResult::IoFailure: return "IoFailure";
        case ProjectSourceDataResult::OutOfMemory: return "OutOfMemory";
        }
        return "Unknown";
    }

    ProjectSourceData::ProjectSourceData() noexcept
        : m_content(memory::pools::Assets::GetInstance()), m_metadata(memory::pools::Assets::GetInstance())
    {
    }

    void ProjectSourceData::Reset() noexcept
    {
        m_content.Clear();
        m_metadata.Clear();
    }

    ProjectSourceDataView ProjectSourceData::View(const TargetPlatform target) const noexcept
    {
        return {{m_content.TypedData(), m_content.Size()}, {m_metadata.TypedData(), m_metadata.Size()}, target};
    }

    ProjectSourceDataResult LoadProjectSourceData(filesystem::Manager& files, const SourceRecord& source, ProjectSourceData& data,
                                                  const ProjectSourceDataLimits& limits) noexcept
    {
        data.Reset();
        if (!limits.IsValid() || !source.path.IsFilePath() || source.metadataResult != MetadataResult::Success ||
            (source.issues & (SourceMissing | MetadataMissing | MetadataUnreadable | MetadataInvalid | AssetIdentityConflict |
                              OutputIdentityConflict | SourceIdentityConflict)) != 0)
            return ProjectSourceDataResult::InvalidArgument;

        ProjectSourceDataResult result = ReadFile(files, source.path, limits.maximumSourceBytes, data.m_content,
                                                  ProjectSourceDataResult::SourceUnavailable);
        if (result != ProjectSourceDataResult::Success)
        {
            data.Reset();
            return result;
        }
        result = ReadFile(files, MetadataPath(source.path), limits.maximumMetadataBytes, data.m_metadata,
                          ProjectSourceDataResult::MetadataUnavailable);
        if (result != ProjectSourceDataResult::Success)
        {
            data.Reset();
            return result;
        }

        AssetMetadata current;
        const containers::StringView metadataText(reinterpret_cast<const char*>(data.m_metadata.TypedData()), data.m_metadata.Size());
        if (ParseMetadata(metadataText, current) != MetadataResult::Success || !SameMetadata(current, source.metadata))
        {
            data.Reset();
            return ProjectSourceDataResult::CatalogOutOfDate;
        }
        return ProjectSourceDataResult::Success;
    }

    bool ProjectSourceDataProvider::Initialize(filesystem::Manager& files, const TargetPlatform target,
                                               const ProjectSourceDataLimits& limits) noexcept
    {
        Reset();
        if (target >= TargetPlatform::Count || !limits.IsValid())
            return false;
        m_files = &files;
        m_target = target;
        m_limits = limits;
        m_lastResult = ProjectSourceDataResult::Success;
        return true;
    }

    void ProjectSourceDataProvider::Reset() noexcept
    {
        m_data.Reset();
        m_files = nullptr;
        m_target = TargetPlatform::WindowsD3D12;
        m_limits = {};
        m_lastResult = ProjectSourceDataResult::InvalidArgument;
    }

    bool ProjectSourceDataProvider::Load(const SourceRecord& source, ProjectSourceDataView& data) noexcept
    {
        data = {};
        if (m_files == nullptr)
        {
            m_lastResult = ProjectSourceDataResult::InvalidArgument;
            return false;
        }
        m_lastResult = LoadProjectSourceData(*m_files, source, m_data, m_limits);
        if (m_lastResult != ProjectSourceDataResult::Success)
            return false;
        data = m_data.View(m_target);
        return true;
    }

    ProjectSourceDataResult ProjectSourceDataProvider::GetLastResult() const noexcept
    {
        return m_lastResult;
    }

    bool ProjectSourceDataProvider::LoadReferenceScanData(const SourceRecord& source, SourceDataView& data,
                                                          void* const userData) noexcept
    {
        data = {};
        if (userData == nullptr)
            return false;
        ProjectSourceDataView loaded;
        ProjectSourceDataProvider& provider = *static_cast<ProjectSourceDataProvider*>(userData);
        if (!provider.Load(source, loaded))
            return false;
        data = {loaded.content, loaded.metadata, loaded.target};
        return true;
    }
} // namespace vanguard::assets
