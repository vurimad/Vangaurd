#include <vanguard/assets/source_database.hpp>

#include <algorithm>

namespace vanguard::assets
{
    namespace
    {
        constexpr u32 MaximumSources = 262144;
        constexpr u32 MaximumRoots = 128;

        bool ValidRoots(const containers::ArraySpan<const SourceRoot> roots) noexcept
        {
            if (roots.Empty() || roots.Count() > MaximumRoots)
                return false;
            for (u32 i = 0; i < roots.Count(); ++i)
            {
                if (!roots[i].directory.IsDirectoryPath() || roots[i].directory.IsOnlyRootPath())
                    return false;
                for (u32 j = 0; j < i; ++j)
                    if (roots[i].directory.AsStringView().StartsWithIgnoreCase(roots[j].directory.AsStringView()) || roots[j].directory.AsStringView().StartsWithIgnoreCase(roots[i].directory.AsStringView()))
                        return false;
            }
            return true;
        }

        containers::String PathKey(const filesystem::AbsolutePath& path) noexcept
        {
            const auto view = path.AsStringView();
            containers::String key(view.Data(), view.Length());
            key.ToLower();
            return key;
        }

        void ReadMetadata(filesystem::Manager& files, SourceRecord& record) noexcept
        {
            if (!files.FileExist(record.path))
                record.issues |= SourceMissing;
            const auto view = record.path.AsStringView();
            containers::String sidecar(view.Data(), view.Length());
            sidecar.Append(".vmeta", 6);
            const filesystem::AbsolutePath metadataPath = filesystem::AbsolutePath::CreateFilePath(sidecar);
            if (!files.FileExist(metadataPath))
            {
                record.issues |= MetadataMissing;
                return;
            }
            auto reader = files.CreateFileReader(metadataPath, filesystem::FOF_Buffered);
            if (!reader)
            {
                record.issues |= MetadataUnreadable;
                return;
            }
            const u64 size = reader->GetSize();
            if (size > MaximumAssetMetadataBytes)
            {
                record.metadataResult = MetadataResult::LimitExceeded;
                record.issues |= MetadataInvalid;
                return;
            }
            containers::String text;
            if (!text.Resize(static_cast<u32>(size)))
            {
                record.issues |= MetadataUnreadable;
                return;
            }
            if (size != 0)
                reader->Serialize(text.AsChar(), static_cast<usize>(size));
            if (reader->HasErrors())
            {
                record.issues |= MetadataUnreadable;
                return;
            }
            record.metadataResult = ParseMetadata(text, record.metadata);
            if (record.metadataResult != MetadataResult::Success)
                record.issues |= MetadataInvalid;
        }
    }

    SourceDatabase::SourceDatabase() noexcept
        : m_roots(memory::pools::Assets::GetInstance()), m_resources(memory::pools::Assets::GetInstance()), m_paths(memory::pools::Assets::GetInstance()), m_assets(memory::pools::Assets::GetInstance()), m_outputs(memory::pools::Assets::GetInstance()) {}

    SourceDatabaseResult SourceDatabase::Rebuild(filesystem::Manager& files, const containers::ArraySpan<const SourceRoot> roots, const containers::ArraySpan<const SourceFile> sources) noexcept
    {
        if (!ValidRoots(roots))
            return SourceDatabaseResult::InvalidInventory;
        if (sources.Count() > MaximumSources)
            return SourceDatabaseResult::LimitExceeded;
        SourceDatabase scanned;
        for (const SourceRoot& root : roots)
            scanned.m_roots.PushBack(root);
        if (scanned.m_roots.Size() != roots.Count())
            return SourceDatabaseResult::LimitExceeded;
        for (const SourceFile& source : sources)
        {
            if (source.root >= roots.Count() || !source.path.IsFilePath() || !source.path.AsStringView().StartsWithIgnoreCase(roots[source.root].directory.AsStringView()) || source.path.AsStringView().EndsWithIgnoreCase(".vmeta"))
                return SourceDatabaseResult::InvalidInventory;
            SourceRecord record;
            record.path = source.path;
            record.root = source.root;
            scanned.m_resources.PushBack(static_cast<SourceRecord&&>(record));
        }
        if (scanned.m_resources.Size() != sources.Count())
            return SourceDatabaseResult::LimitExceeded;
        if (scanned.m_resources.Size() > 1)
            std::sort(scanned.m_resources.Begin(), scanned.m_resources.End(), [](const SourceRecord& a, const SourceRecord& b) { return a.path < b.path; });
        for (u32 i = 0; i < scanned.m_resources.Size(); ++i)
        {
            SourceRecord& record = scanned.m_resources[i];
            u32 duplicate = 0;
            if (scanned.m_paths.Find(PathKey(record.path), duplicate))
                return SourceDatabaseResult::InvalidInventory;
            if (!scanned.m_paths.Insert(PathKey(record.path), i).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
            ReadMetadata(files, record);
            if (record.metadataResult != MetadataResult::Success)
                continue;
            containers::String identity;
            if (!GetAssetOutputPath(record.metadata.id, "source", identity))
                return SourceDatabaseResult::LimitExceeded;
            u32 previous = 0;
            if (scanned.m_assets.Find(identity, previous))
            {
                record.issues |= AssetIdentityConflict;
                scanned.m_resources[previous].issues |= AssetIdentityConflict;
            }
            else if (!scanned.m_assets.Insert(identity, i).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
            for (const AssetOutput& output : record.metadata.outputs)
            {
                const resources::ResourceReference reference = GetAssetOutputReference(record.metadata.id, output);
                if (!reference.IsValid())
                    return SourceDatabaseResult::LimitExceeded;
                if (scanned.m_outputs.Find(reference.GetPath().Id(), previous))
                {
                    record.issues |= OutputIdentityConflict;
                    scanned.m_resources[previous].issues |= OutputIdentityConflict;
                }
                else if (!scanned.m_outputs.Insert(reference.GetPath().Id(), i).IsSuccessful())
                    return SourceDatabaseResult::LimitExceeded;
            }
        }
        m_roots = static_cast<decltype(m_roots)&&>(scanned.m_roots);
        m_resources = static_cast<decltype(m_resources)&&>(scanned.m_resources);
        m_paths = static_cast<decltype(m_paths)&&>(scanned.m_paths);
        m_assets = static_cast<decltype(m_assets)&&>(scanned.m_assets);
        m_outputs = static_cast<decltype(m_outputs)&&>(scanned.m_outputs);
        return SourceDatabaseResult::Success;
    }

    containers::ArraySpan<const SourceRecord> SourceDatabase::GetResources() const noexcept { return {m_resources.TypedData(), m_resources.Size()}; }

    SourceDatabaseResult SourceDatabase::Rescan(filesystem::Manager& files, const containers::ArraySpan<const SourceRoot> roots, filesystem::ScanResult* const scanFailure) noexcept
    {
        if (scanFailure != nullptr)
            *scanFailure = filesystem::ScanResult::Success;
        if (roots.Count() == 0 || roots.Count() > MaximumRoots)
            return SourceDatabaseResult::InvalidInventory;
        if (!ValidRoots(roots))
            return SourceDatabaseResult::InvalidInventory;
        containers::DynamicArray<SourceFile> sources(memory::pools::Assets::GetInstance());
        containers::HashMap<containers::String, u32> known(memory::pools::Assets::GetInstance());
        u32 totalFiles = 0;
        for (u32 rootIndex = 0; rootIndex < roots.Count(); ++rootIndex)
        {
            containers::DynamicArray<filesystem::AbsolutePath> found(memory::pools::Assets::GetInstance());
            const filesystem::ScanResult result = filesystem::ScanFiles(roots[rootIndex].directory, found, MaximumSources * 2u);
            if (result != filesystem::ScanResult::Success)
            {
                if (scanFailure != nullptr)
                    *scanFailure = result;
                return SourceDatabaseResult::ScanFailed;
            }
            if (found.Size() > MaximumSources * 2u - totalFiles)
                return SourceDatabaseResult::LimitExceeded;
            totalFiles += found.Size();
            for (const filesystem::AbsolutePath& file : found)
            {
                filesystem::AbsolutePath sourcePath = file;
                if (file.AsStringView().EndsWithIgnoreCase(".vmeta"))
                {
                    if (filesystem::paths::GetFileName(file).Length() <= 6u)
                        return SourceDatabaseResult::InvalidInventory;
                    sourcePath = filesystem::AbsolutePath::CreateFilePath(file.AsStringView().Slice(0, file.Length() - 6u));
                }
                const containers::String key = PathKey(sourcePath);
                u32 previous = 0;
                if (known.Find(key, previous))
                    continue;
                if (sources.Size() == MaximumSources)
                    return SourceDatabaseResult::LimitExceeded;
                const u32 count = sources.Size();
                if (!known.Insert(key, count).IsSuccessful())
                    return SourceDatabaseResult::LimitExceeded;
                sources.PushBack(SourceFile{sourcePath, rootIndex});
                if (sources.Size() != count + 1u)
                    return SourceDatabaseResult::LimitExceeded;
            }
        }
        return Rebuild(files, roots, {sources.TypedData(), sources.Size()});
    }
    containers::ArraySpan<const SourceRoot> SourceDatabase::GetRoots() const noexcept { return {m_roots.TypedData(), m_roots.Size()}; }

    const SourceRecord* SourceDatabase::LookupResource(const filesystem::AbsolutePath& path) const noexcept
    {
        u32 index = 0;
        return m_paths.Find(PathKey(path), index) ? &m_resources[index] : nullptr;
    }

    const SourceRecord* SourceDatabase::LookupAsset(const AssetId& id) const noexcept
    {
        containers::String identity;
        u32 index = 0;
        if (!GetAssetOutputPath(id, "source", identity) || !m_assets.Find(identity, index) || (m_resources[index].issues & AssetIdentityConflict) != 0)
            return nullptr;
        return &m_resources[index];
    }

    const SourceRecord* SourceDatabase::LookupOutput(const resources::ResourceReference output) const noexcept
    {
        u32 index = 0;
        if (!output.IsValid() || !output.IsTyped() || !m_outputs.Find(output.GetPath().Id(), index) || m_resources[index].issues != SourceIssueNone)
            return nullptr;
        const SourceRecord& record = m_resources[index];
        for (const AssetOutput& declared : record.metadata.outputs)
            if (GetAssetOutputReference(record.metadata.id, declared) == output)
                return &record;
        return nullptr;
    }

    bool SourceDatabase::ClassifySources(const containers::ArraySpan<const CompilerDescriptor> compilers) noexcept
    {
        // Validate the supplied registry view before changing any record.
        for (u32 i = 0; i < compilers.Count(); ++i)
        {
            if (!compilers[i].IsValid())
                return false;
            for (u32 j = 0; j < i; ++j)
                if (compilers[i].id == compilers[j].id)
                    return false;
        }
        for (SourceRecord& record : m_resources)
        {
            record.importer = InvalidCompilerId;
            record.availableImporterVersion = 0;
            record.sourceType = resources::InvalidResourceTypeId;
            record.primaryOutputType = resources::InvalidResourceTypeId;
            record.importState = SourceImportState::NotClassified;
            if ((record.issues & (MetadataInvalid | MetadataUnreadable)) != 0)
            {
                record.importState = SourceImportState::InvalidMetadata;
                continue;
            }
            const CompilerDescriptor* selected = nullptr;
            if (record.metadataResult == MetadataResult::Success)
            {
                for (const CompilerDescriptor& compiler : compilers)
                    if (compiler.id == record.metadata.importer)
                    {
                        selected = &compiler;
                        break;
                    }
                if (selected == nullptr)
                {
                    record.importState = SourceImportState::ImporterUnavailable;
                    continue;
                }
                record.importState = record.metadata.importerVersion == selected->version ? SourceImportState::MetadataSelected : SourceImportState::VersionMismatch;
                if (selected->recognizeSource != nullptr && !selected->recognizeSource(record.path.AsStringView(), selected->userData))
                    record.importState = SourceImportState::SourceMismatch;
            }
            else
            {
                bool unavailable = compilers.Empty();
                for (const CompilerDescriptor& compiler : compilers)
                {
                    if (compiler.recognizeSource == nullptr)
                    {
                        unavailable = true;
                        continue;
                    }
                    if (compiler.recognizeSource(record.path.AsStringView(), compiler.userData))
                    {
                        if (selected != nullptr)
                        {
                            selected = nullptr;
                            record.importState = SourceImportState::Ambiguous;
                            break;
                        }
                        selected = &compiler;
                    }
                }
                if (record.importState == SourceImportState::Ambiguous)
                    continue;
                record.importState = selected != nullptr ? SourceImportState::Candidate : unavailable ? SourceImportState::DiscoveryUnavailable : SourceImportState::Unsupported;
            }
            if (selected != nullptr)
            {
                record.importer = selected->id;
                record.availableImporterVersion = selected->version;
                record.sourceType = selected->sourceType;
                record.primaryOutputType = selected->outputType;
            }
        }
        return true;
    }

    u32 SourceDatabase::VisitResources(const SourceQuery& query, VisitSourceFunction visitor, void* const userData) const noexcept
    {
        if (visitor == nullptr || (!query.folder.Empty() && !query.folder.IsDirectoryPath()))
            return 0;
        u32 visited = 0;
        for (const SourceRecord& record : m_resources)
        {
            if (!query.folder.Empty())
            {
                if (!record.path.AsStringView().StartsWithIgnoreCase(query.folder.AsStringView()))
                    continue;
                if (!query.recursive && filesystem::paths::GetParentPath(record.path).Length() != query.folder.Length())
                    continue;
            }
            if ((record.issues & query.requiredIssues) != query.requiredIssues || (record.issues & query.excludedIssues) != 0 ||
                (query.filterImportState && record.importState != query.importState) ||
                (query.sourceType != resources::InvalidResourceTypeId && record.sourceType != query.sourceType))
                continue;
            if (query.outputType != resources::InvalidResourceTypeId)
            {
                bool matches = record.primaryOutputType == query.outputType;
                for (const AssetOutput& output : record.metadata.outputs)
                    matches = matches || output.type == query.outputType;
                if (!matches)
                    continue;
            }
            ++visited;
            if (!visitor(record, userData))
                break;
        }
        return visited;
    }
} // namespace vanguard::assets
