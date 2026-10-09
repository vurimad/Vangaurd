#include <vanguard/assets/source_database.hpp>
#include <vanguard/assets/asset_file_transaction.hpp>

#include <algorithm>

namespace vanguard::assets
{
    namespace
    {
        constexpr u32 MaximumSources = SourceDatabase::MaximumSources;
        constexpr u32 MaximumFolders = SourceDatabase::MaximumFolders;
        constexpr u32 MaximumScannedFiles = MaximumSources * 2u;
        constexpr u32 MaximumScanEntries = MaximumScannedFiles + MaximumFolders;
        constexpr u32 MaximumRoots = SourceDatabase::MaximumRoots;
        constexpr u32 MaximumAssetReferences = SourceDatabase::MaximumAssetReferences;

        i32 CompareAssetId(const AssetId& left, const AssetId& right) noexcept
        {
            for (u32 index = 0; index < 16; ++index)
            {
                if (left.bytes[index] < right.bytes[index])
                    return -1;
                if (left.bytes[index] > right.bytes[index])
                    return 1;
            }
            return 0;
        }

        i32 CompareReferenceTarget(const resources::ResourceReference left, const resources::ResourceReference right) noexcept
        {
            if (left.GetPath().Id() < right.GetPath().Id())
                return -1;
            if (left.GetPath().Id() > right.GetPath().Id())
                return 1;
            if (left.ExpectedType() < right.ExpectedType())
                return -1;
            if (left.ExpectedType() > right.ExpectedType())
                return 1;
            return 0;
        }

        bool ReferenceDetailsLess(const AssetReference& left, const AssetReference& right) noexcept
        {
            if (left.location != right.location)
                return left.location < right.location;
            if (left.kind != right.kind)
                return left.kind < right.kind;
            return left.inPlace < right.inPlace;
        }

        bool ReferenceFromLess(const AssetReference& left, const AssetReference& right) noexcept
        {
            const i32 from = CompareAssetId(left.from, right.from);
            if (from != 0)
                return from < 0;
            const i32 to = CompareReferenceTarget(left.to, right.to);
            return to != 0 ? to < 0 : ReferenceDetailsLess(left, right);
        }

        bool ReferenceToLess(const AssetReference& left, const AssetReference& right) noexcept
        {
            const i32 to = CompareReferenceTarget(left.to, right.to);
            if (to != 0)
                return to < 0;
            const i32 from = CompareAssetId(left.from, right.from);
            return from != 0 ? from < 0 : ReferenceDetailsLess(left, right);
        }

        bool SameReference(const AssetReference& left, const AssetReference& right) noexcept
        {
            return left.from == right.from && left.to == right.to && left.kind == right.kind && left.location == right.location &&
                   left.inPlace == right.inPlace;
        }

        bool AppendReference(containers::DynamicArray<AssetReference>& destination, const AssetReference& reference) noexcept
        {
            const u32 previous = destination.Size();
            destination.PushBack(reference);
            return destination.Size() == previous + 1u;
        }

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

        [[nodiscard]] bool IsTransactionArtifact(const filesystem::AbsolutePath& path) noexcept
        {
            const containers::StringView name = filesystem::paths::GetFileName(path);
            return name.EndsWithIgnoreCase(AssetTransactionStageSuffix) ||
                   name.EndsWithIgnoreCase(AssetTransactionBackupSuffix);
        }

        [[nodiscard]] bool IsExcludedFile(
            const filesystem::AbsolutePath& path,
            const containers::ArraySpan<const filesystem::AbsolutePath> excludedFiles) noexcept
        {
            const containers::StringView pathView = path.AsStringView();
            for (const filesystem::AbsolutePath& excluded : excludedFiles)
            {
                const containers::StringView excludedView = excluded.AsStringView();
                if (pathView.Length() == excludedView.Length() && pathView.StartsWithIgnoreCase(excludedView))
                    return true;
            }
            return false;
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

    SourceImporterDescriptor MakeDirectSourceImporterDescriptor(const CompilerDescriptor& compiler, const RecognizeSourceFunction recognizeSource,
                                                                 const ScanBuildRequestReferencesFunction scanReferences,
                                                                 const RewriteSourceReferencesFunction rewriteReferences,
                                                                 const RelocateSourceFunction relocateSource,
                                                                 const CreateSourceImportDescriptionFunction createImportDescription,
                                                                 const ValidateSourceSettingsFunction validateSettings) noexcept
    {
        if (!compiler.IsValid())
            return {};
        return {compiler.id, compiler.name, compiler.version, compiler.sourceType, compiler.outputType, recognizeSource, nullptr, scanReferences, nullptr,
                rewriteReferences, relocateSource, compiler.userData, createImportDescription, validateSettings};
    }

    bool RelocateSourceUnchanged(const SourceRecord& source, const SourceDataView& data, const containers::StringView newSourceIdentity,
                                 containers::DynamicArray<u8>& relocatedSource, AssetMetadata& relocatedMetadata, void*) noexcept
    {
        if (!data.IsValid() || !source.metadata.id.IsValid() || newSourceIdentity.Empty() ||
            !resources::ResourcePath::FromString(newSourceIdentity).IsValid())
            return false;
        relocatedSource.Resize(data.content.Size());
        if (relocatedSource.Size() != data.content.Size())
            return false;
        for (u32 index = 0; index < data.content.Size(); ++index)
            relocatedSource[index] = data.content[index];
        relocatedMetadata = source.metadata;
        return relocatedMetadata.id == source.metadata.id;
    }

    bool CreateSourceBuildRequest(const SourceImporterDescriptor& importer, const SourceRecord& source, const SourceDataView& data,
                                  const resources::ResourceReference output, BuildRequest& request) noexcept
    {
        request = {};
        if (!importer.IsValid() || source.metadataResult != MetadataResult::Success || !data.IsValid() || !output.IsValid() || !output.IsTyped())
            return false;
        bool declared = false;
        for (const AssetOutput& candidate : source.metadata.outputs)
            if (GetAssetOutputReference(source.metadata.id, candidate) == output)
            {
                declared = true;
                break;
            }
        if (!declared)
            return false;
        if (importer.createBuildRequest != nullptr)
        {
            if (!importer.createBuildRequest(source, data, output, request, importer.userData))
                return false;
            return request.IsValid() && request.output == output && request.target == data.target;
        }
        if (output.ExpectedType() != importer.primaryOutputType)
            return false;
        if (!source.sourceIdentity.IsValid())
            return false;
        request = {{resources::ResourceReference(source.sourceIdentity, importer.sourceType), data.content, data.metadata}, output,
                   data.target, {source.metadata.settings.TypedData(), source.metadata.settings.Size()}};
        return request.IsValid();
    }

    bool ResolveUniqueAssetOutputOfType(const AssetMetadata& metadata, const resources::ResourceTypeId type,
                                        resources::ResourceReference& output, bool& ambiguous) noexcept
    {
        output = {};
        ambiguous = false;
        if (!metadata.id.IsValid() || type == resources::InvalidResourceTypeId)
            return false;

        u32 matches = 0u;
        for (const AssetOutput& declared : metadata.outputs)
        {
            if (declared.type != type)
                continue;
            ++matches;
            if (matches == 1u)
                output = GetAssetOutputReference(metadata.id, declared);
            else
            {
                ambiguous = true;
                output = {};
                return false;
            }
        }
        return matches == 1u && output.IsValid() && output.IsTyped();
    }

    SourceDatabase::SourceDatabase() noexcept
        : m_roots(memory::pools::Assets::GetInstance()), m_folders(memory::pools::Assets::GetInstance()),
          m_resources(memory::pools::Assets::GetInstance()), m_paths(memory::pools::Assets::GetInstance()),
          m_sourcePaths(memory::pools::Assets::GetInstance()),
          m_assets(memory::pools::Assets::GetInstance()), m_outputs(memory::pools::Assets::GetInstance()),
          m_forwardReferences(memory::pools::Assets::GetInstance()), m_backwardReferences(memory::pools::Assets::GetInstance()) {}

    void SourceDatabase::Swap(SourceDatabase& other) noexcept
    {
        if (this == &other)
            return;
        m_roots.Swap(other.m_roots);
        m_folders.Swap(other.m_folders);
        m_resources.Swap(other.m_resources);
        m_paths.Swap(other.m_paths);
        m_sourcePaths.Swap(other.m_sourcePaths);
        m_assets.Swap(other.m_assets);
        m_outputs.Swap(other.m_outputs);
        m_forwardReferences.Swap(other.m_forwardReferences);
        m_backwardReferences.Swap(other.m_backwardReferences);
        AdvanceGeneration();
        other.AdvanceGeneration();
    }

    SourceDatabaseResult SourceDatabase::Rebuild(
        filesystem::Manager& files, const containers::ArraySpan<const SourceRoot> roots,
        const containers::ArraySpan<const SourceFile> sources,
        const containers::ArraySpan<const SourceFolder> folders) noexcept
    {
        if (!ValidRoots(roots))
            return SourceDatabaseResult::InvalidInventory;
        if (sources.Count() > MaximumSources)
            return SourceDatabaseResult::LimitExceeded;
        if (folders.Count() > MaximumFolders)
            return SourceDatabaseResult::LimitExceeded;
        SourceDatabase scanned;
        for (const SourceRoot& root : roots)
            scanned.m_roots.PushBack(root);
        if (scanned.m_roots.Size() != roots.Count())
            return SourceDatabaseResult::LimitExceeded;

        containers::HashMap<containers::String, u32> knownFolders(memory::pools::Assets::GetInstance());
        const auto appendFolder = [&scanned, &knownFolders, roots](const filesystem::AbsolutePath& path,
                                                                  const u32 root) noexcept
        {
            if (root >= roots.Count() || !path.IsDirectoryPath())
                return SourceDatabaseResult::InvalidInventory;
            const containers::StringView rootPath = roots[root].directory.AsStringView();
            const containers::StringView folderPath = path.AsStringView();
            if (folderPath.Length() <= rootPath.Length() || !folderPath.StartsWithIgnoreCase(rootPath))
                return SourceDatabaseResult::InvalidInventory;
            const containers::String key = PathKey(path);
            u32 previous = 0u;
            if (knownFolders.Find(key, previous))
                return previous < scanned.m_folders.Size() && scanned.m_folders[previous].root == root
                           ? SourceDatabaseResult::Success
                           : SourceDatabaseResult::InvalidInventory;
            if (scanned.m_folders.Size() == MaximumFolders)
                return SourceDatabaseResult::LimitExceeded;
            const u32 index = scanned.m_folders.Size();
            if (!knownFolders.Insert(key, index).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
            scanned.m_folders.PushBack(SourceFolder{path, root});
            return scanned.m_folders.Size() == index + 1u ? SourceDatabaseResult::Success
                                                           : SourceDatabaseResult::LimitExceeded;
        };

        for (const SourceFolder& folder : folders)
        {
            const SourceDatabaseResult result = appendFolder(folder.path, folder.root);
            if (result != SourceDatabaseResult::Success)
                return result;
        }
        for (const SourceFile& source : sources)
        {
            if (source.root >= roots.Count() || !source.path.IsFilePath() ||
                !source.path.AsStringView().StartsWithIgnoreCase(roots[source.root].directory.AsStringView()) ||
                source.path.AsStringView().Length() <= roots[source.root].directory.AsStringView().Length() ||
                source.path.AsStringView().EndsWithIgnoreCase(".vmeta") || IsTransactionArtifact(source.path))
                return SourceDatabaseResult::InvalidInventory;
            SourceRecord record;
            record.path = source.path;
            record.root = source.root;
            const containers::StringView rootPath = roots[source.root].directory.AsStringView();
            const containers::StringView relativePath = source.path.AsStringView().Slice(rootPath.Length(), source.path.AsStringView().Length());
            record.sourceIdentity = resources::ResourcePath::FromString(relativePath);
            if (!record.sourceIdentity.IsValid())
                return SourceDatabaseResult::InvalidInventory;
            scanned.m_resources.PushBack(static_cast<SourceRecord&&>(record));
            filesystem::AbsolutePath parent = filesystem::paths::ParentAbsolutePath(source.path);
            while (!parent.Empty() && parent.AsStringView().Length() > rootPath.Length())
            {
                if (!parent.AsStringView().StartsWithIgnoreCase(rootPath))
                    return SourceDatabaseResult::InvalidInventory;
                const SourceDatabaseResult folderResult = appendFolder(parent, source.root);
                if (folderResult != SourceDatabaseResult::Success)
                    return folderResult;
                parent = filesystem::paths::ParentAbsolutePath(parent);
            }
            if (parent.Empty() || parent.AsStringView().Length() != rootPath.Length() ||
                !parent.AsStringView().StartsWithIgnoreCase(rootPath))
                return SourceDatabaseResult::InvalidInventory;
        }
        if (scanned.m_resources.Size() != sources.Count())
            return SourceDatabaseResult::LimitExceeded;
        if (scanned.m_resources.Size() > 1)
            std::sort(scanned.m_resources.Begin(), scanned.m_resources.End(), [](const SourceRecord& a, const SourceRecord& b) { return a.path < b.path; });
        if (scanned.m_folders.Size() > 1)
            std::sort(scanned.m_folders.Begin(), scanned.m_folders.End(), [](const SourceFolder& a, const SourceFolder& b)
            {
                return a.root != b.root ? a.root < b.root : a.path < b.path;
            });
        for (u32 i = 0; i < scanned.m_resources.Size(); ++i)
        {
            SourceRecord& record = scanned.m_resources[i];
            u32 duplicate = 0;
            if (scanned.m_paths.Find(PathKey(record.path), duplicate))
                return SourceDatabaseResult::InvalidInventory;
            if (!scanned.m_paths.Insert(PathKey(record.path), i).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
            if (scanned.m_sourcePaths.Find(record.sourceIdentity.Id(), duplicate))
            {
                record.issues |= SourceIdentityConflict;
                scanned.m_resources[duplicate].issues |= SourceIdentityConflict;
            }
            else if (!scanned.m_sourcePaths.Insert(record.sourceIdentity.Id(), i).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
            ReadMetadata(files, record);
            if (record.metadataResult != MetadataResult::Success)
                continue;
            u32 previous = 0;
            if (scanned.m_assets.Find(record.metadata.id, previous))
            {
                record.issues |= AssetIdentityConflict;
                scanned.m_resources[previous].issues |= AssetIdentityConflict;
            }
            else if (!scanned.m_assets.Insert(record.metadata.id, i).IsSuccessful())
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
        m_folders = static_cast<decltype(m_folders)&&>(scanned.m_folders);
        m_resources = static_cast<decltype(m_resources)&&>(scanned.m_resources);
        m_paths = static_cast<decltype(m_paths)&&>(scanned.m_paths);
        m_sourcePaths = static_cast<decltype(m_sourcePaths)&&>(scanned.m_sourcePaths);
        m_assets = static_cast<decltype(m_assets)&&>(scanned.m_assets);
        m_outputs = static_cast<decltype(m_outputs)&&>(scanned.m_outputs);
        m_forwardReferences = static_cast<decltype(m_forwardReferences)&&>(scanned.m_forwardReferences);
        m_backwardReferences = static_cast<decltype(m_backwardReferences)&&>(scanned.m_backwardReferences);
        AdvanceGeneration();
        return SourceDatabaseResult::Success;
    }

    containers::ArraySpan<const SourceRecord> SourceDatabase::GetResources() const noexcept { return {m_resources.TypedData(), m_resources.Size()}; }
    containers::ArraySpan<const SourceFolder> SourceDatabase::GetFolders() const noexcept { return {m_folders.TypedData(), m_folders.Size()}; }

    SourceDatabaseResult SourceDatabase::Rescan(
        filesystem::Manager& files, const containers::ArraySpan<const SourceRoot> roots,
        filesystem::ScanResult* const scanFailure,
        const containers::ArraySpan<const filesystem::AbsolutePath> excludedFiles) noexcept
    {
        if (scanFailure != nullptr)
            *scanFailure = filesystem::ScanResult::Success;
        if (roots.Count() == 0 || roots.Count() > MaximumRoots)
            return SourceDatabaseResult::InvalidInventory;
        if (!ValidRoots(roots))
            return SourceDatabaseResult::InvalidInventory;
        containers::DynamicArray<SourceFile> sources(memory::pools::Assets::GetInstance());
        containers::DynamicArray<SourceFolder> folders(memory::pools::Assets::GetInstance());
        containers::HashMap<containers::String, u32> known(memory::pools::Assets::GetInstance());
        u32 totalFiles = 0;
        u32 totalFolders = 0;
        for (u32 rootIndex = 0; rootIndex < roots.Count(); ++rootIndex)
        {
            containers::DynamicArray<filesystem::AbsolutePath> found(memory::pools::Assets::GetInstance());
            containers::DynamicArray<filesystem::AbsolutePath> foundFolders(memory::pools::Assets::GetInstance());
            const filesystem::ScanResult result = filesystem::ScanFilesAndDirectories(
                roots[rootIndex].directory, found, foundFolders, MaximumScanEntries);
            if (result != filesystem::ScanResult::Success)
            {
                if (scanFailure != nullptr)
                    *scanFailure = result;
                return SourceDatabaseResult::ScanFailed;
            }
            if (found.Size() > MaximumScannedFiles - totalFiles)
                return SourceDatabaseResult::LimitExceeded;
            totalFiles += found.Size();
            if (foundFolders.Size() > MaximumFolders - totalFolders)
                return SourceDatabaseResult::LimitExceeded;
            totalFolders += foundFolders.Size();
            for (const filesystem::AbsolutePath& folder : foundFolders)
            {
                const u32 count = folders.Size();
                folders.PushBack(SourceFolder{folder, rootIndex});
                if (folders.Size() != count + 1u)
                    return SourceDatabaseResult::LimitExceeded;
            }
            for (const filesystem::AbsolutePath& file : found)
            {
                if (IsTransactionArtifact(file) || IsExcludedFile(file, excludedFiles))
                    continue;
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
        return Rebuild(files, roots, {sources.TypedData(), sources.Size()}, {folders.TypedData(), folders.Size()});
    }
    containers::ArraySpan<const SourceRoot> SourceDatabase::GetRoots() const noexcept { return {m_roots.TypedData(), m_roots.Size()}; }

    const SourceRecord* SourceDatabase::LookupResource(const filesystem::AbsolutePath& path) const noexcept
    {
        u32 index = 0;
        return m_paths.Find(PathKey(path), index) ? &m_resources[index] : nullptr;
    }

    const SourceRecord* SourceDatabase::LookupSource(const resources::ResourcePath identity) const noexcept
    {
        u32 index = 0;
        if (!identity.IsValid() || !m_sourcePaths.Find(identity.Id(), index) || (m_resources[index].issues & SourceIdentityConflict) != 0)
            return nullptr;
        return &m_resources[index];
    }

    const SourceRecord* SourceDatabase::LookupAsset(const AssetId& id) const noexcept
    {
        u32 index = 0;
        if (!id.IsValid() || !m_assets.Find(id, index) || (m_resources[index].issues & AssetIdentityConflict) != 0)
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

    bool SourceDatabase::ContainsAssetIdentity(const AssetId& id) const noexcept
    {
        u32 index = 0;
        return id.IsValid() && m_assets.Find(id, index);
    }

    bool SourceDatabase::ContainsOutputPath(const resources::ResourcePath path) const noexcept
    {
        u32 index = 0;
        return path.IsValid() && m_outputs.Find(path.Id(), index);
    }

    u64 SourceDatabase::GetGeneration() const noexcept { return m_generation; }

    void SourceDatabase::AdvanceGeneration() noexcept
    {
        ++m_generation;
        if (m_generation == 0)
            m_generation = 1;
    }

    SourceDatabaseResult SourceDatabase::SetReferencesInternal(const containers::ArraySpan<const AssetReference> references) noexcept
    {
        if (references.Count() > MaximumAssetReferences)
            return SourceDatabaseResult::LimitExceeded;

        containers::DynamicArray<AssetReference> forward(memory::pools::Assets::GetInstance());
        forward.Reserve(references.Count());
        for (const AssetReference& reference : references)
        {
            if (!reference.from.IsValid() || LookupAsset(reference.from) == nullptr || !reference.to.IsValid() || !reference.to.IsTyped() ||
                reference.kind > resources::DependencyKind::Soft || reference.location > AssetReferenceLocation::Inner)
                return SourceDatabaseResult::InvalidReferences;
            const u32 previousCount = forward.Size();
            forward.PushBack(reference);
            if (forward.Size() != previousCount + 1u)
                return SourceDatabaseResult::LimitExceeded;
        }

        if (forward.Size() > 1u)
        {
            AssetReference* const forwardData = forward.TypedData();
            AssetReference* const forwardEnd = forwardData + forward.Size();
            std::sort(forwardData, forwardEnd, ReferenceFromLess);
            AssetReference* output = forwardData;
            for (AssetReference* current = forwardData; current != forwardEnd; ++current)
                if (output == forwardData || !SameReference(*(output - 1), *current))
                    *output++ = *current;
            forward.Resize(static_cast<u32>(output - forwardData));
        }

        containers::DynamicArray<AssetReference> backward(memory::pools::Assets::GetInstance());
        backward.Reserve(forward.Size());
        for (const AssetReference& reference : forward)
        {
            const u32 previousCount = backward.Size();
            backward.PushBack(reference);
            if (backward.Size() != previousCount + 1u)
                return SourceDatabaseResult::LimitExceeded;
        }
        if (backward.Size() > 1u)
            std::sort(backward.TypedData(), backward.TypedData() + backward.Size(), ReferenceToLess);

        m_forwardReferences = static_cast<decltype(m_forwardReferences)&&>(forward);
        m_backwardReferences = static_cast<decltype(m_backwardReferences)&&>(backward);
        for (SourceRecord& resource : m_resources)
            resource.referenceScanState = SourceReferenceScanState::NotScanned;
        return SourceDatabaseResult::Success;
    }

    SourceDatabaseResult SourceDatabase::SetReferences(const containers::ArraySpan<const AssetReference> references) noexcept
    {
        const SourceDatabaseResult result = SetReferencesInternal(references);
        if (result == SourceDatabaseResult::Success)
            AdvanceGeneration();
        return result;
    }

    containers::ArraySpan<const AssetReference> SourceDatabase::GetForwardReferences(const AssetId& source) const noexcept
    {
        const AssetReference* const forwardData = m_forwardReferences.TypedData();
        const AssetReference* const forwardEnd = forwardData + m_forwardReferences.Size();
        const AssetReference* const begin = std::lower_bound(
            forwardData, forwardEnd, source,
            [](const AssetReference& reference, const AssetId& value) noexcept { return CompareAssetId(reference.from, value) < 0; });
        const AssetReference* const end = std::upper_bound(
            begin, forwardEnd, source,
            [](const AssetId& value, const AssetReference& reference) noexcept { return CompareAssetId(value, reference.from) < 0; });
        return {begin, static_cast<u32>(end - begin)};
    }

    containers::ArraySpan<const AssetReference> SourceDatabase::GetBackwardReferences(const resources::ResourceReference target) const noexcept
    {
        if (!target.IsValid() || !target.IsTyped())
            return {};
        const AssetReference* const backwardData = m_backwardReferences.TypedData();
        const AssetReference* const backwardEnd = backwardData + m_backwardReferences.Size();
        const AssetReference* const begin = std::lower_bound(
            backwardData, backwardEnd, target,
            [](const AssetReference& reference, const resources::ResourceReference value) noexcept { return CompareReferenceTarget(reference.to, value) < 0; });
        const AssetReference* const end = std::upper_bound(
            begin, backwardEnd, target,
            [](const resources::ResourceReference value, const AssetReference& reference) noexcept { return CompareReferenceTarget(value, reference.to) < 0; });
        return {begin, static_cast<u32>(end - begin)};
    }

    containers::ArraySpan<const AssetReference> SourceDatabase::GetAllForwardReferences() const noexcept
    {
        return {m_forwardReferences.TypedData(), m_forwardReferences.Size()};
    }

    containers::ArraySpan<const AssetReference> SourceDatabase::GetAllBackwardReferences() const noexcept
    {
        return {m_backwardReferences.TypedData(), m_backwardReferences.Size()};
    }

    SourceDatabaseResult SourceDatabase::ScanReferences(const containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                         const LoadSourceDataFunction load, void* const userData) noexcept
    {
        if (load == nullptr)
            return SourceDatabaseResult::InvalidReferences;

        containers::HashMap<SourceImporterId, u32> importerIndices(memory::pools::Assets::GetInstance());
        for (u32 index = 0; index < importers.Count(); ++index)
        {
            if (!importers[index].IsValid())
                return SourceDatabaseResult::InvalidReferences;
            u32 duplicate = 0;
            if (importerIndices.Find(importers[index].id, duplicate))
                return SourceDatabaseResult::InvalidReferences;
            if (!importerIndices.Insert(importers[index].id, index).IsSuccessful())
                return SourceDatabaseResult::LimitExceeded;
        }

        containers::DynamicArray<SourceReferenceScanState> states(memory::pools::Assets::GetInstance());
        states.Resize(m_resources.Size());
        if (states.Size() != m_resources.Size())
            return SourceDatabaseResult::LimitExceeded;
        containers::DynamicArray<AssetReference> links(memory::pools::Assets::GetInstance());
        links.Reserve(m_forwardReferences.Size());

        for (u32 sourceIndex = 0; sourceIndex < m_resources.Size(); ++sourceIndex)
        {
            const SourceRecord& source = m_resources[sourceIndex];
            SourceReferenceScanState state = SourceReferenceScanState::NotScanned;
            u32 importerIndex = 0;
            const SourceImporterDescriptor* importer = nullptr;
            if (source.metadataResult == MetadataResult::Success && (source.issues & AssetIdentityConflict) == 0)
            {
                if (!importerIndices.Find(source.metadata.importer, importerIndex) || importers[importerIndex].version != source.metadata.importerVersion)
                    state = SourceReferenceScanState::ImporterUnavailable;
                else
                    importer = &importers[importerIndex];
            }

            if (importer != nullptr)
            {
                if (importer->scanSourceReferences == nullptr && importer->scanBuildReferences == nullptr)
                    state = SourceReferenceScanState::ScannerUnavailable;
                else
                {
                    SourceDataView data;
                    if (!load(source, data, userData) || !data.IsValid())
                    {
                        state = SourceReferenceScanState::InputUnavailable;
                    }
                    else
                    {
                        AssetReferenceCollector found;
                        bool scanned = false;
                        if (importer->scanSourceReferences != nullptr)
                            scanned = importer->scanSourceReferences(source, data, found, importer->userData);
                        else
                        {
                            resources::ResourceReference output;
                            bool ambiguous = false;
                            BuildRequest request;
                            scanned = ResolveUniqueAssetOutputOfType(source.metadata, importer->primaryOutputType,
                                                                    output, ambiguous) &&
                                      CreateSourceBuildRequest(*importer, source, data, output, request) &&
                                      importer->scanBuildReferences(request, found, importer->userData);
                        }
                        if (!scanned || found.GetStatus() != Result::Success)
                            state = SourceReferenceScanState::ScanFailed;
                        else
                        {
                            for (const ScannedAssetReference& reference : found.GetReferences())
                                if (!AppendReference(links, {source.metadata.id, reference.target, reference.kind, reference.location, reference.inPlace}))
                                    return SourceDatabaseResult::LimitExceeded;
                            state = SourceReferenceScanState::Scanned;
                        }
                    }
                }
            }

            if (state != SourceReferenceScanState::Scanned)
                for (const AssetReference& reference : GetForwardReferences(source.metadata.id))
                    if (!AppendReference(links, reference))
                        return SourceDatabaseResult::LimitExceeded;
            states[sourceIndex] = state;
            if (links.Size() > MaximumAssetReferences)
                return SourceDatabaseResult::LimitExceeded;
        }

        const SourceDatabaseResult applied = SetReferencesInternal({links.TypedData(), links.Size()});
        if (applied != SourceDatabaseResult::Success)
            return applied;
        for (u32 index = 0; index < m_resources.Size(); ++index)
            m_resources[index].referenceScanState = states[index];
        AdvanceGeneration();
        return SourceDatabaseResult::Success;
    }

    bool SourceDatabase::ClassifySources(const containers::ArraySpan<const SourceImporterDescriptor> importers) noexcept
    {
        // Validate the supplied registry view before changing any record.
        for (u32 i = 0; i < importers.Count(); ++i)
        {
            if (!importers[i].IsValid())
                return false;
            for (u32 j = 0; j < i; ++j)
                if (importers[i].id == importers[j].id)
                    return false;
        }
        for (SourceRecord& record : m_resources)
        {
            record.importer = InvalidSourceImporterId;
            record.availableImporterVersion = 0;
            record.sourceType = resources::InvalidResourceTypeId;
            record.primaryOutputType = resources::InvalidResourceTypeId;
            record.importState = SourceImportState::NotClassified;
            record.referenceScanState = SourceReferenceScanState::NotScanned;
            if ((record.issues & (MetadataInvalid | MetadataUnreadable)) != 0)
            {
                record.importState = SourceImportState::InvalidMetadata;
                continue;
            }
            const SourceImporterDescriptor* selected = nullptr;
            if (record.metadataResult == MetadataResult::Success)
            {
                for (const SourceImporterDescriptor& importer : importers)
                    if (importer.id == record.metadata.importer)
                    {
                        selected = &importer;
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
                bool unavailable = importers.Empty();
                for (const SourceImporterDescriptor& importer : importers)
                {
                    if (importer.recognizeSource == nullptr)
                    {
                        unavailable = true;
                        continue;
                    }
                    if (importer.recognizeSource(record.path.AsStringView(), importer.userData))
                    {
                        if (selected != nullptr)
                        {
                            selected = nullptr;
                            record.importState = SourceImportState::Ambiguous;
                            break;
                        }
                        selected = &importer;
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
                record.primaryOutputType = selected->primaryOutputType;
            }
        }
        m_forwardReferences.Clear();
        m_backwardReferences.Clear();
        AdvanceGeneration();
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
                (query.filterReferenceScanState && record.referenceScanState != query.referenceScanState) ||
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
