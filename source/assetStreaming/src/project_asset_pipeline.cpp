#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/asset_streaming/platform_directory_observer.hpp>
#include <vanguard/asset_streaming/project_asset_change_state_internal.hpp>
#include <vanguard/asset_streaming/project_asset_recook_state.hpp>
#include <vanguard/asset_streaming/project_asset_snapshot_store.hpp>
#include <vanguard/asset_streaming/source_change_accumulator.hpp>

#include <vanguard/assets/asset_recooker.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/directory_observation.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>

#include <algorithm>
#include <cstring>
#include <source_location>

namespace vanguard::asset_streaming
{
    namespace
    {
        struct PublishedResource
        {
            resources::ResourceReference resource;
            filesystem::AbsolutePath physicalPath;
            assets::BuildFingerprint buildFingerprint;
            assets::BuildFingerprint contentFingerprint;
        };

        struct AssetHistoryFile
        {
            AssetHistoryFile() noexcept : bytes(memory::pools::Assets::GetInstance()) {}
            AssetHistoryFile(const AssetHistoryFile&) = delete;
            AssetHistoryFile& operator=(const AssetHistoryFile&) = delete;
            AssetHistoryFile(AssetHistoryFile&&) noexcept = default;
            AssetHistoryFile& operator=(AssetHistoryFile&&) noexcept = default;

            filesystem::AbsolutePath path;
            containers::DynamicArray<u8> bytes;
            crypto::Digest256 expectedPostDigest;
            bool existed = false;
            bool expectedPostExisted = false;
            bool expectedPostKnown = false;
        };

        struct AssetHistoryRecord
        {
            AssetHistoryRecord() noexcept : files(memory::pools::Assets::GetInstance()) {}
            AssetHistoryRecord(const AssetHistoryRecord&) = delete;
            AssetHistoryRecord& operator=(const AssetHistoryRecord&) = delete;
            AssetHistoryRecord(AssetHistoryRecord&&) noexcept = default;
            AssetHistoryRecord& operator=(AssetHistoryRecord&&) noexcept = default;

            containers::DynamicArray<AssetHistoryFile> files;
            u64 byteCount = 0;
            // Aggregate history budget charged by this record. It is always at
            // least byteCount and may additionally reserve the reverse snapshot
            // that replay must capture. Replay propagates that reservation so a
            // reversible create-only command stays replayable across any number
            // of Undo/Redo cycles under history pressure.
            u64 budgetBytes = 0;
        };

        [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
        {
            const containers::StringView view = source.AsStringView();
            containers::String path(view.Data(), view.Length());
            path.Append(".vmeta", 6);
            return filesystem::AbsolutePath::CreateFilePath(path);
        }

        [[nodiscard]] filesystem::AbsolutePath SuffixedPath(const filesystem::AbsolutePath& source, const char* const suffix) noexcept
        {
            const containers::StringView view = source.AsStringView();
            containers::String path(view.Data(), view.Length());
            path.Append(suffix, static_cast<u32>(std::strlen(suffix)));
            return filesystem::AbsolutePath::CreateFilePath(path);
        }

        [[nodiscard]] bool AppendUniquePath(containers::DynamicArray<filesystem::AbsolutePath>& paths,
                                            const filesystem::AbsolutePath& path, const u32 maximum) noexcept
        {
            if (!path.IsFilePath())
                return false;
            for (const filesystem::AbsolutePath& existing : paths)
                if (existing == path)
                    return true;
            if (paths.Size() >= maximum)
                return false;
            const u32 previous = paths.Size();
            paths.PushBack(path);
            return paths.Size() == previous + 1u;
        }

        [[nodiscard]] bool AppendAssetPaths(containers::DynamicArray<filesystem::AbsolutePath>& paths,
                                             const filesystem::AbsolutePath& source, const u32 maximum) noexcept
        {
            return AppendUniquePath(paths, source, maximum) && AppendUniquePath(paths, MetadataPath(source), maximum);
        }

        [[nodiscard]] const assets::SourceImporterDescriptor* FindAuthoringImporter(
            const containers::ArraySpan<const assets::SourceImporterDescriptor> importers,
            const assets::SourceImporterId id, const u32 version) noexcept
        {
            for (const assets::SourceImporterDescriptor& importer : importers)
                if (importer.id == id)
                    return importer.version == version ? &importer : nullptr;
            return nullptr;
        }

        enum class ImportDescriptionValidation : u8
        {
            Success,
            Invalid,
            LimitExceeded
        };

        [[nodiscard]] ImportDescriptionValidation ValidateImportDescription(
            const assets::SourceImportDescription& description,
            const assets::SourceImporterDescriptor& importer) noexcept
        {
            if (description.settingsVersion == 0u || description.settings.Size() > assets::MaximumAssetSettingsBytes ||
                description.outputs.Empty() || description.outputs.Size() > assets::MaximumAssetOutputs || importer.validateSettings == nullptr)
                return ImportDescriptionValidation::Invalid;
            containers::DynamicArray<containers::StringView> keys(memory::pools::Assets::GetInstance());
            keys.Reserve(description.outputs.Size());
            if (keys.Capacity() < description.outputs.Size())
                return ImportDescriptionValidation::LimitExceeded;
            bool primary = false;
            for (const assets::AssetOutput& output : description.outputs)
            {
                if (!assets::IsValidAssetOutputKey(output.key) || output.type == resources::InvalidResourceTypeId)
                    return ImportDescriptionValidation::Invalid;
                keys.PushBack(output.key);
                primary = primary || output.type == importer.primaryOutputType;
            }
            if (keys.Size() > 1u)
            {
                std::sort(keys.TypedData(), keys.TypedData() + keys.Size());
                for (u32 index = 1u; index < keys.Size(); ++index)
                    if (keys[index] == keys[index - 1u])
                        return ImportDescriptionValidation::Invalid;
            }
            return primary && importer.validateSettings(description.settingsVersion,
                                                        {description.settings.TypedData(), description.settings.Size()}, importer.userData)
                       ? ImportDescriptionValidation::Success
                       : ImportDescriptionValidation::Invalid;
        }

        [[nodiscard]] bool PathsInWritableRoots(
            const containers::ArraySpan<const filesystem::AbsolutePath> paths,
            const containers::ArraySpan<const filesystem::AbsolutePath> roots) noexcept
        {
            for (const filesystem::AbsolutePath& path : paths)
            {
                bool writable = false;
                for (const filesystem::AbsolutePath& root : roots)
                    if (filesystem::ValidateMutationPath(root, path) == filesystem::MutationPathResult::Success)
                    {
                        writable = true;
                        break;
                    }
                if (!writable)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ReferencesAny(const assets::SourceDatabase& database, const assets::SourceRecord& source,
                                         const containers::ArraySpan<const assets::AssetReferenceReplacement> replacements) noexcept
        {
            for (const assets::AssetReference& reference : database.GetForwardReferences(source.metadata.id))
                for (const assets::AssetReferenceReplacement& replacement : replacements)
                    if (reference.to == replacement.previous)
                        return true;
            return false;
        }

        [[nodiscard]] bool CollectRewritePaths(const assets::SourceDatabase& database,
                                               const containers::ArraySpan<const assets::AssetReferenceReplacement> replacements,
                                               const assets::AssetId excluded,
                                               containers::DynamicArray<filesystem::AbsolutePath>& paths,
                                               const u32 maximum) noexcept
        {
            for (const assets::SourceRecord& source : database.GetResources())
                if (source.metadata.id != excluded && ReferencesAny(database, source, replacements) &&
                    !AppendAssetPaths(paths, source.path, maximum))
                    return false;
            return true;
        }

        [[nodiscard]] bool ReadHistoryFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                           const u64 maximumFileBytes, const u64 maximumRecordBytes,
                                           AssetHistoryRecord& record) noexcept
        {
            if (record.files.Size() == 0xffffffffu)
                return false;
            AssetHistoryFile state;
            state.path = path;
            state.existed = files.FileExist(path);
            if (state.existed)
            {
                auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
                if (!reader)
                    return false;
                const u64 size = reader->GetSize();
                if (size > maximumFileBytes || size > 0xffffffffull || record.byteCount > maximumRecordBytes ||
                    size > maximumRecordBytes - record.byteCount)
                    return false;
                state.bytes.Resize(static_cast<u32>(size));
                if (state.bytes.Size() != size)
                    return false;
                if (size != 0)
                    reader->Serialize(state.bytes.TypedData(), static_cast<usize>(size));
                if (reader->HasErrors())
                    return false;
                record.byteCount += size;
            }
            const u32 previous = record.files.Size();
            record.files.PushBack(static_cast<AssetHistoryFile&&>(state));
            return record.files.Size() == previous + 1u;
        }

        [[nodiscard]] bool CaptureHistory(filesystem::Manager& files,
                                          const containers::ArraySpan<const filesystem::AbsolutePath> paths,
                                          const ProjectAssetPipelineConfig& config, AssetHistoryRecord& record) noexcept
        {
            if (paths.Count() > config.maximumOperationEntries)
                return false;
            record.files.Reserve(paths.Count());
            if (record.files.Capacity() < paths.Count())
                return false;
            for (const filesystem::AbsolutePath& path : paths)
                if (!ReadHistoryFile(files, path, config.maximumOperationFileBytes,
                                     config.maximumOperationHistoryBytes, record))
                    return false;
            record.budgetBytes = record.byteCount;
            return true;
        }

        [[nodiscard]] bool AttachHistoryPostConditions(
            AssetHistoryRecord& record,
            const containers::ArraySpan<const assets::AssetFileTransactionPostCondition> postConditions) noexcept
        {
            if (record.files.Size() != postConditions.Count())
                return false;
            for (u32 index = 0u; index < record.files.Size(); ++index)
            {
                const assets::AssetFileTransactionPostCondition& post = postConditions[index];
                if (!post.known)
                    return false;
                record.files[index].expectedPostDigest = post.contentDigest;
                record.files[index].expectedPostExisted = post.exists;
                record.files[index].expectedPostKnown = true;
            }
            return true;
        }

        [[nodiscard]] bool PrepareHistoryPostConditions(
            const AssetHistoryRecord& record, const containers::ArraySpan<const filesystem::AbsolutePath> paths,
            containers::DynamicArray<assets::AssetFileTransactionPostCondition>& postConditions) noexcept
        {
            if (record.files.Size() != paths.Count())
                return false;
            postConditions.Resize(paths.Count());
            if (postConditions.Size() != paths.Count())
                return false;
            for (u32 index = 0u; index < paths.Count(); ++index)
            {
                const AssetHistoryFile& state = record.files[index];
                if (state.path != paths[index])
                    return false;
                assets::AssetFileTransactionPostCondition& condition = postConditions[index];
                condition = {};
                condition.exists = state.existed;
                condition.known = true;
                if (state.existed)
                {
                    crypto::Sha256Builder hash;
                    if ((!state.bytes.Empty() && !hash.Update(state.bytes.TypedData(), state.bytes.Size())) ||
                        !hash.Finalize(condition.contentDigest))
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ReadMetadataSnapshot(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                assets::AssetMetadata& metadata, crypto::Digest256& rawDigest,
                                                crypto::Digest256& canonicalDigest) noexcept
        {
            rawDigest = {};
            canonicalDigest = {};
            if (!path.IsFilePath() || !files.FileExist(path))
                return false;
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return false;
            const u64 size = reader->GetSize();
            if (size == 0u || size > assets::MaximumAssetMetadataBytes || size > ~u32{0})
                return false;
            containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
                return false;
            reader->Serialize(bytes.TypedData(), bytes.Size());
            if (reader->HasErrors())
                return false;
            rawDigest = crypto::Sha256(bytes.TypedData(), bytes.Size());
            if (rawDigest.IsEmpty() ||
                assets::ParseMetadata({reinterpret_cast<const char*>(bytes.TypedData()), bytes.Size()}, metadata) !=
                    assets::MetadataResult::Success)
                return false;
            containers::String canonical;
            if (assets::EncodeMetadata(metadata, canonical) != assets::MetadataResult::Success)
                return false;
            canonicalDigest = crypto::Sha256(canonical.AsChar(), canonical.Length());
            return !canonicalDigest.IsEmpty();
        }

        [[nodiscard]] bool DecodeMetadataSnapshot(const containers::ArraySpan<const u8> bytes,
                                                  assets::AssetMetadata& metadata,
                                                  crypto::Digest256& rawDigest,
                                                  crypto::Digest256& canonicalDigest) noexcept
        {
            rawDigest = {};
            canonicalDigest = {};
            if (bytes.Empty() || bytes.Data() == nullptr || bytes.Count() > assets::MaximumAssetMetadataBytes)
                return false;
            rawDigest = crypto::Sha256(bytes.Data(), bytes.Count());
            if (rawDigest.IsEmpty() ||
                assets::ParseMetadata({reinterpret_cast<const char*>(bytes.Data()), bytes.Count()}, metadata) !=
                    assets::MetadataResult::Success)
                return false;
            containers::String canonical;
            if (assets::EncodeMetadata(metadata, canonical) != assets::MetadataResult::Success)
                return false;
            canonicalDigest = crypto::Sha256(canonical.AsChar(), canonical.Length());
            return !canonicalDigest.IsEmpty();
        }

        [[nodiscard]] bool SourceIdentityMatchesRoot(const filesystem::AbsolutePath& root,
                                                     const filesystem::AbsolutePath& source,
                                                     const resources::ResourcePath identity) noexcept
        {
            if (!root.IsDirectoryPath() || !source.IsFilePath() || !identity.IsValid() ||
                !source.AsStringView().StartsWithIgnoreCase(root.AsStringView()) ||
                source.AsStringView().Length() <= root.AsStringView().Length())
                return false;
            const containers::StringView relative =
                source.AsStringView().Slice(root.AsStringView().Length(), source.AsStringView().Length());
            const resources::ResourcePath expected = resources::ResourcePath::FromString(relative);
            return expected.IsValid() && expected == identity;
        }

        [[nodiscard]] bool SetHistoryExpectedPostFromDesired(AssetHistoryRecord& record,
                                                              const AssetHistoryRecord& desired) noexcept
        {
            if (record.files.Size() != desired.files.Size())
                return false;
            for (u32 index = 0u; index < record.files.Size(); ++index)
            {
                if (record.files[index].path != desired.files[index].path)
                    return false;
                AssetHistoryFile& reverse = record.files[index];
                const AssetHistoryFile& replayed = desired.files[index];
                reverse.expectedPostKnown = true;
                reverse.expectedPostExisted = replayed.existed;
                if (replayed.existed)
                {
                    crypto::Sha256Builder hash;
                    if ((!replayed.bytes.Empty() && !hash.Update(replayed.bytes.TypedData(), replayed.bytes.Size())) ||
                        !hash.Finalize(reverse.expectedPostDigest))
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsHex(const char value) noexcept
        {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        }

        [[nodiscard]] char LooseHexDigit(const u8 value) noexcept
        {
            constexpr char digits[] = "0123456789abcdef";
            return digits[value & 0x0fu];
        }

        void WriteLooseResourceId(char* const output, const resources::ResourceId resource) noexcept
        {
            for (u32 index = 0; index < 16u; ++index)
                output[index] = LooseHexDigit(static_cast<u8>(resource >> ((15u - index) * 4u)));
        }

        void WriteLooseBuildFingerprint(char* const output, const assets::BuildFingerprint& build) noexcept
        {
            for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
            {
                output[index * 2u] = LooseHexDigit(build.bytes[index] >> 4u);
                output[index * 2u + 1u] = LooseHexDigit(build.bytes[index]);
            }
        }

        [[nodiscard]] filesystem::AbsolutePath CanonicalLooseGenerationPath(
            const filesystem::AbsolutePath& root, const resources::ResourceId resource,
            const assets::BuildFingerprint& build, const bool stage) noexcept
        {
            if (!root.IsDirectoryPath() || resource == resources::InvalidResourceId || build.IsEmpty())
                return {};
            char first[3]{LooseHexDigit(build.bytes[0] >> 4u), LooseHexDigit(build.bytes[0]), '\0'};
            char second[3]{LooseHexDigit(build.bytes[1] >> 4u), LooseHexDigit(build.bytes[1]), '\0'};
            char name[91]{};
            WriteLooseResourceId(name, resource);
            name[16] = '-';
            WriteLooseBuildFingerprint(name + 17, build);
            constexpr char suffix[] = ".vres";
            for (u32 index = 0; index < sizeof(suffix); ++index)
                name[81u + index] = suffix[index];
            if (stage)
            {
                name[86] = '.';
                name[87] = 't';
                name[88] = 'm';
                name[89] = 'p';
                name[90] = '\0';
            }
            return root.AddDirPath(first).AddDirPath(second).AddFilePath(name);
        }

        [[nodiscard]] bool ReadResourceId(const containers::StringView name, resources::ResourceId& id) noexcept
        {
            const bool complete = name.Length() == 86u && name.Slice(81u, 86u) == containers::StringView(".vres");
            const bool temporary = name.Length() == 90u && name.Slice(81u, 90u) == containers::StringView(".vres.tmp");
            if ((!complete && !temporary) || name[16] != '-')
                return false;
            u64 value = 0;
            for (u32 index = 0; index < 81u; ++index)
            {
                if (index == 16u)
                    continue;
                if (!IsHex(name[index]))
                    return false;
                if (index < 16u)
                {
                    const u8 digit = name[index] <= '9' ? static_cast<u8>(name[index] - '0')
                                                        : static_cast<u8>(name[index] - 'a' + 10);
                    value = (value << 4u) | digit;
                }
            }
            id = value;
            return id != resources::InvalidResourceId;
        }

        [[nodiscard]] bool ReadLooseGenerationIdentity(const containers::StringView name, resources::ResourceId& id,
                                                       assets::BuildFingerprint& build, bool& stage) noexcept
        {
            id = resources::InvalidResourceId;
            build = {};
            stage = false;
            const bool complete = name.Length() == 86u && name.Slice(81u, 86u) == containers::StringView(".vres");
            stage = name.Length() == 90u && name.Slice(81u, 90u) == containers::StringView(".vres.tmp");
            if ((!complete && !stage) || name[16] != '-')
                return false;

            u64 resource = 0u;
            for (u32 index = 0; index < 16u; ++index)
            {
                if (!IsHex(name[index]))
                    return false;
                const u8 digit = name[index] <= '9' ? static_cast<u8>(name[index] - '0')
                                                    : static_cast<u8>(name[index] - 'a' + 10);
                resource = (resource << 4u) | digit;
            }
            if (resource == resources::InvalidResourceId)
                return false;
            for (u32 byteIndex = 0; byteIndex < assets::BuildFingerprint::ByteCount; ++byteIndex)
            {
                const u32 highIndex = 17u + byteIndex * 2u;
                const u32 lowIndex = highIndex + 1u;
                if (!IsHex(name[highIndex]) || !IsHex(name[lowIndex]))
                    return false;
                const u8 high = name[highIndex] <= '9' ? static_cast<u8>(name[highIndex] - '0')
                                                       : static_cast<u8>(name[highIndex] - 'a' + 10);
                const u8 low = name[lowIndex] <= '9' ? static_cast<u8>(name[lowIndex] - '0')
                                                     : static_cast<u8>(name[lowIndex] - 'a' + 10);
                build.bytes[byteIndex] = static_cast<u8>((high << 4u) | low);
            }
            id = resource;
            return !build.IsEmpty();
        }

        [[nodiscard]] bool CollectDesiredResources(const assets::SourceDatabase& sources,
                                                   containers::DynamicArray<resources::ResourceReference>& desired,
                                                   containers::HashMap<resources::ResourceId, u8>& lookup,
                                                   const u32 maximumPublishedResources) noexcept
        {
            desired.Clear();
            lookup.Clear();
            for (const assets::SourceRecord& source : sources.GetResources())
                for (const assets::AssetOutput& output : source.metadata.outputs)
                {
                    const resources::ResourceReference resource = assets::GetAssetOutputReference(source.metadata.id, output);
                    if (!resource.IsValid() || sources.LookupOutput(resource) != &source)
                        continue;
                    u8 duplicate = 0;
                    if (lookup.Find(resource.GetPath().Id(), duplicate))
                        continue;
                    if (desired.Size() >= maximumPublishedResources)
                        return false;
                    const u32 previous = desired.Size();
                    desired.PushBack(resource);
                    if (desired.Size() != previous + 1u)
                        return false;
                    if (!lookup.Insert(resource.GetPath().Id(), 1u).IsSuccessful())
                    {
                        desired.Resize(previous);
                        return false;
                    }
                }
            return true;
        }

        // A durable cohort intent is a temporary visibility override, not a
        // second source/catalog authority. Until the exact successor cohort owns
        // the cutover, generic reconciliation must keep the previous generation
        // visible for this asset and must not restore successor-only members one
        // by one. Same-identity content refreshes remain in the set so the current
        // (still predecessor) VADI row can be restored after restart.
        [[nodiscard]] bool ApplyCohortIntentDesired(
            const ProjectAssetCohortIntent& intent,
            containers::DynamicArray<resources::ResourceReference>& desired,
            containers::HashMap<resources::ResourceId, u8>& lookup,
            const u32 maximumPublishedResources) noexcept
        {
            if (!intent.IsActive())
                return true;
            const auto contains = [](const containers::DynamicArray<resources::ResourceReference>& values,
                                     const resources::ResourceReference value) noexcept
            {
                for (const resources::ResourceReference candidate : values)
                    if (candidate == value)
                        return true;
                return false;
            };

            for (u32 index = desired.Size(); index != 0u; --index)
            {
                const resources::ResourceReference output = desired[index - 1u];
                if (!contains(intent.successorOutputs, output) || contains(intent.predecessorOutputs, output))
                    continue;
                if (!lookup.Remove(output.GetPath().Id()).IsSuccessful() || !desired.RemoveAtReorder(index - 1u).IsSuccessful())
                    return false;
            }
            for (const resources::ResourceReference predecessor : intent.predecessorOutputs)
            {
                u8 present = 0u;
                if (lookup.Find(predecessor.GetPath().Id(), present))
                    continue;
                if (desired.Size() >= maximumPublishedResources)
                    return false;
                const u32 before = desired.Size();
                desired.PushBack(predecessor);
                if (desired.Size() != before + 1u || !lookup.Insert(predecessor.GetPath().Id(), 1u).IsSuccessful())
                {
                    desired.Resize(before);
                    return false;
                }
            }
            return true;
        }

        struct ProjectAssetEvidenceProvider final
        {
            [[nodiscard]] bool Initialize(filesystem::Manager& files, const assets::TargetPlatform target,
                                          const assets::ProjectSourceDataLimits& limits,
                                          const containers::ArraySpan<const assets::SourceRecord> sourceRows,
                                          containers::DynamicArray<ProjectAssetPreparedSourceEvidence>& preparedEvidence,
                                          containers::DynamicArray<u8>& loadAttempts) noexcept
            {
                rows = sourceRows.Data();
                count = sourceRows.Count();
                evidence = &preparedEvidence;
                attempts = &loadAttempts;
                preparedEvidence.Resize(count);
                loadAttempts.Resize(count);
                if (preparedEvidence.Size() != count || loadAttempts.Size() != count || !provider.Initialize(files, target, limits))
                    return false;
                for (u32 index = 0; index < count; ++index)
                {
                    preparedEvidence[index] = {};
                    loadAttempts[index] = 0u;
                }
                return true;
            }

            [[nodiscard]] static bool LoadReferenceScanData(const assets::SourceRecord& source, assets::SourceDataView& data,
                                                            void* const userData) noexcept
            {
                data = {};
                auto* const self = static_cast<ProjectAssetEvidenceProvider*>(userData);
                if (self == nullptr || self->rows == nullptr || self->evidence == nullptr || self->attempts == nullptr ||
                    &source < self->rows || &source >= self->rows + self->count)
                    return false;
                const u32 index = static_cast<u32>(&source - self->rows);
                (*self->attempts)[index] = 1u;
                assets::ProjectSourceDataView loaded;
                if (!self->provider.Load(source, loaded))
                    return false;
                ProjectAssetPreparedSourceEvidence& item = (*self->evidence)[index];
                item.sourceContentFingerprint = crypto::Sha256(loaded.content.Data(), loaded.content.Size());
                item.hasSourceContentFingerprint = true;
                data = {loaded.content, loaded.metadata, loaded.target};
                return true;
            }

            assets::ProjectSourceDataProvider provider;
            const assets::SourceRecord* rows = nullptr;
            containers::DynamicArray<ProjectAssetPreparedSourceEvidence>* evidence = nullptr;
            containers::DynamicArray<u8>* attempts = nullptr;
            u32 count = 0;
        };

        enum class SourceFingerprintReadResult : u8
        {
            Success,
            Unavailable,
            Changed
        };

        struct SourceFingerprintStamp final
        {
            u64 size = 0;
            u64 timestamp = 0;
            bool exists = false;
        };

        [[nodiscard]] SourceFingerprintStamp CaptureSourceFingerprintStamp(filesystem::Manager& files,
                                                                           const filesystem::AbsolutePath& path) noexcept
        {
            SourceFingerprintStamp stamp;
            stamp.exists = files.FileExist(path);
            if (stamp.exists)
            {
                stamp.size = files.GetFileSize(path);
                stamp.timestamp = files.GetFileTimeStamp(path);
            }
            return stamp;
        }

        [[nodiscard]] SourceFingerprintReadResult HashSourceFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                                 const u64 maximumBytes, const SourceFingerprintStamp& baseline,
                                                                 crypto::Digest256& digest) noexcept
        {
            const SourceFingerprintStamp before = CaptureSourceFingerprintStamp(files, path);
            if (before.exists != baseline.exists || (before.exists && (before.size != baseline.size || before.timestamp != baseline.timestamp)))
                return SourceFingerprintReadResult::Changed;
            if (!before.exists)
                return SourceFingerprintReadResult::Unavailable;
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
            {
                const SourceFingerprintStamp failedOpen = CaptureSourceFingerprintStamp(files, path);
                return !failedOpen.exists || failedOpen.size != baseline.size || failedOpen.timestamp != baseline.timestamp
                           ? SourceFingerprintReadResult::Changed
                           : SourceFingerprintReadResult::Unavailable;
            }
            const u64 size = reader->GetSize();
            if (size != before.size)
                return SourceFingerprintReadResult::Changed;
            if (size > maximumBytes)
                return SourceFingerprintReadResult::Unavailable;
            crypto::Sha256Builder hash;
            u8 buffer[64u * 1024u];
            u64 remaining = size;
            while (remaining != 0u)
            {
                const usize chunk = static_cast<usize>(remaining < sizeof(buffer) ? remaining : sizeof(buffer));
                reader->Serialize(buffer, chunk);
                if (reader->HasErrors() || !hash.Update(buffer, chunk))
                {
                    reader.Reset();
                    const SourceFingerprintStamp failedRead = CaptureSourceFingerprintStamp(files, path);
                    return !failedRead.exists || failedRead.size != baseline.size || failedRead.timestamp != baseline.timestamp
                               ? SourceFingerprintReadResult::Changed
                               : SourceFingerprintReadResult::Unavailable;
                }
                remaining -= chunk;
            }
            if (!hash.Finalize(digest))
                return SourceFingerprintReadResult::Unavailable;
            reader.Reset();
            // This path is used only when no reference scanner loaded the
            // source. Recheck observable file identity after the streaming read
            // so a mid-preparation edit is not accepted as stable evidence.
            const SourceFingerprintStamp after = CaptureSourceFingerprintStamp(files, path);
            if (!after.exists || after.size != baseline.size || after.timestamp != baseline.timestamp)
                return SourceFingerprintReadResult::Changed;
            return SourceFingerprintReadResult::Success;
        }

        [[nodiscard]] assets::SourceDatabaseResult SeedAcceptedReferences(
            const ProjectAssetAcceptedBaseline& previous, assets::SourceDatabase& candidate) noexcept
        {
            containers::DynamicArray<assets::AssetReference> retained(memory::pools::Assets::GetInstance());
            retained.Reserve(previous.references.Size());
            if (retained.Capacity() < previous.references.Size())
                return assets::SourceDatabaseResult::LimitExceeded;
            for (const assets::AssetReference& reference : previous.references)
            {
                if (candidate.LookupAsset(reference.from) == nullptr)
                    continue;
                const u32 before = retained.Size();
                retained.PushBack(reference);
                if (retained.Size() != before + 1u)
                    return assets::SourceDatabaseResult::LimitExceeded;
            }
            return candidate.SetReferences({retained.TypedData(), retained.Size()});
        }

        [[nodiscard]] assets::SourceDatabaseResult PrepareProjectAssetCandidate(
            filesystem::Manager& files, const containers::ArraySpan<const assets::SourceRoot> roots,
            const containers::ArraySpan<const assets::SourceImporterDescriptor> importers, const assets::TargetPlatform target,
            const assets::ProjectSourceDataLimits& sourceDataLimits, const filesystem::AbsolutePath& journalPath,
            const ProjectAssetAcceptedBaseline& previous, const u32 maximumPublishedResources,
            const filesystem::AbsolutePath& looseRoot, const u32 maximumLooseFiles,
            assets::SourceDatabase& candidate, ProjectAssetAcceptedBaseline& proposed,
            containers::DynamicArray<resources::ResourceReference>& desired,
            containers::HashMap<resources::ResourceId, u8>& desiredLookup,
            containers::DynamicArray<filesystem::AbsolutePath>& looseFiles,
            filesystem::ScanResult* const scanFailure) noexcept
        {
            assets::SourceDatabase prepared;
            ProjectAssetAcceptedBaseline compared(previous.limits);
            containers::DynamicArray<resources::ResourceReference> preparedDesired(memory::pools::Assets::GetInstance());
            containers::HashMap<resources::ResourceId, u8> preparedDesiredLookup(memory::pools::Assets::GetInstance());
            containers::DynamicArray<filesystem::AbsolutePath> preparedLooseFiles(memory::pools::Assets::GetInstance());
            containers::DynamicArray<ProjectAssetPreparedSourceEvidence> evidence(memory::pools::Assets::GetInstance());
            containers::DynamicArray<u8> loadAttempts(memory::pools::Assets::GetInstance());
            containers::DynamicArray<SourceFingerprintStamp> sourceStamps(memory::pools::Assets::GetInstance());

            const filesystem::AbsolutePath excludedFiles[] = {journalPath, SuffixedPath(journalPath, ".stage")};
            // TODO: Whole-root candidate reconstruction is intentionally simple today. Before gigantic-project scale,
            // redesign reconciliation around the changed-path set/incremental inventory so one ordinary edit does not
            // require rebuilding evidence for the complete source catalog.
            assets::SourceDatabaseResult result = prepared.Rescan(files, roots, scanFailure, {excludedFiles, 2u});
            if (result != assets::SourceDatabaseResult::Success)
                return result;
            if (!prepared.ClassifySources(importers))
                return assets::SourceDatabaseResult::InvalidInventory;
            result = SeedAcceptedReferences(previous, prepared);
            if (result != assets::SourceDatabaseResult::Success)
                return result;

            ProjectAssetEvidenceProvider provider;
            const auto rows = prepared.GetResources();
            if (!provider.Initialize(files, target, sourceDataLimits, rows, evidence, loadAttempts))
                return assets::SourceDatabaseResult::LimitExceeded;
            sourceStamps.Resize(rows.Count());
            if (sourceStamps.Size() != rows.Count())
                return assets::SourceDatabaseResult::LimitExceeded;
            // Establish the coherence token before reference scanning. Rows that
            // the scanner does not load later must still prove that the source
            // stayed at this same observable version through their streaming
            // fingerprint read.
            for (u32 index = 0; index < rows.Count(); ++index)
                sourceStamps[index] = CaptureSourceFingerprintStamp(files, rows[index].path);
            result = prepared.ScanReferences(importers, &ProjectAssetEvidenceProvider::LoadReferenceScanData, &provider);
            if (result != assets::SourceDatabaseResult::Success)
                return result;

            // Rows actually loaded by the reference scanner use the exact same
            // source buffer for fingerprinting. Rows the scanner never loads are
            // covered by a bounded streaming read so P2 still receives content
            // evidence without allocating a whole source file solely to hash it.
            for (u32 index = 0; index < rows.Count(); ++index)
            {
                if (loadAttempts[index] != 0u || (rows[index].issues & assets::SourceMissing) != 0u)
                    continue;
                crypto::Digest256 digest;
                const SourceFingerprintReadResult fingerprint =
                    HashSourceFile(files, rows[index].path, sourceDataLimits.maximumSourceBytes, sourceStamps[index], digest);
                if (fingerprint == SourceFingerprintReadResult::Changed)
                    return assets::SourceDatabaseResult::ScanFailed;
                if (fingerprint == SourceFingerprintReadResult::Success)
                {
                    evidence[index].sourceContentFingerprint = digest;
                    evidence[index].hasSourceContentFingerprint = true;
                }
            }

            const ProjectAssetComparisonResult comparison =
                CompareProjectAssetCandidate(previous, prepared, {evidence.TypedData(), evidence.Size()}, compared);
            if (comparison != ProjectAssetComparisonResult::Success)
                return comparison == ProjectAssetComparisonResult::LimitExceeded ? assets::SourceDatabaseResult::LimitExceeded
                                                                                 : assets::SourceDatabaseResult::InvalidInventory;
            if (!CollectDesiredResources(prepared, preparedDesired, preparedDesiredLookup, maximumPublishedResources))
                return assets::SourceDatabaseResult::LimitExceeded;
            if (filesystem::ScanFiles(looseRoot, preparedLooseFiles, maximumLooseFiles) != filesystem::ScanResult::Success)
                preparedLooseFiles.Clear();

            candidate.Swap(prepared);
            proposed.Swap(compared);
            desired.Swap(preparedDesired);
            desiredLookup.Swap(preparedDesiredLookup);
            looseFiles.Swap(preparedLooseFiles);
            return assets::SourceDatabaseResult::Success;
        }

        void AcceptProjectAssetCandidate(assets::SourceDatabase& live, ProjectAssetAcceptedBaseline& accepted,
                                         assets::SourceDatabase& candidate, ProjectAssetAcceptedBaseline& proposed) noexcept
        {
            live.Swap(candidate);
            accepted.Swap(proposed);
        }

        [[nodiscard]] bool IsRecoverableSnapshotLoad(const ProjectAssetSnapshotResult result) noexcept
        {
            return result == ProjectAssetSnapshotResult::Success || result == ProjectAssetSnapshotResult::NotFound;
        }

        [[nodiscard]] ProjectAssetSnapshotResult PersistProjectAssetCandidate(
            filesystem::Manager& files, const filesystem::AbsolutePath& root,
            const ProjectAssetAcceptedBaseline& previous, const ProjectAssetAcceptedBaseline& proposed) noexcept
        {
            if (previous.generation == proposed.generation)
                return ProjectAssetSnapshotResult::Success;
            const ProjectAssetSnapshotResult staged = StageProjectAssetAcceptedSnapshot(files, root, proposed);
            if (staged != ProjectAssetSnapshotResult::Success)
                return staged;
            const ProjectAssetSnapshotResult committed = CommitStagedProjectAssetAcceptedSnapshot(files, root);
            if (committed != ProjectAssetSnapshotResult::Success)
                static_cast<void>(DiscardStagedProjectAssetAcceptedSnapshot(files, root));
            return committed;
        }

        [[nodiscard]] ProjectAssetSnapshotResult PersistProjectAssetSnapshot(
            filesystem::Manager& files, const filesystem::AbsolutePath& root,
            const ProjectAssetAcceptedBaseline& snapshot) noexcept
        {
            const ProjectAssetSnapshotResult staged = StageProjectAssetAcceptedSnapshot(files, root, snapshot);
            if (staged != ProjectAssetSnapshotResult::Success)
                return staged;
            const ProjectAssetSnapshotResult committed = CommitStagedProjectAssetAcceptedSnapshot(files, root);
            if (committed != ProjectAssetSnapshotResult::Success)
                static_cast<void>(DiscardStagedProjectAssetAcceptedSnapshot(files, root));
            return committed;
        }

        [[nodiscard]] bool SameSourceRoots(const containers::ArraySpan<const assets::SourceRoot> left,
                                           const containers::ArraySpan<const assets::SourceRoot> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0; index < left.Count(); ++index)
                if (left[index].directory != right[index].directory || left[index].readOnly != right[index].readOnly)
                    return false;
            return true;
        }

        void ClearFailure(ProjectAssetPipelineFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool IsCommittedResult(const ProjectAssetPipelineResult result) noexcept
        {
            return result == ProjectAssetPipelineResult::Success || result == ProjectAssetPipelineResult::CommittedCleanupPending ||
                   result == ProjectAssetPipelineResult::CommittedReconciliationPending;
        }

        [[nodiscard]] bool IsCacheRepairFailure(const ProjectLoosePublicationFailure& failure) noexcept
        {
            if (failure.result != ProjectLoosePublicationResult::MaterializationFailure)
                return false;
            return failure.materialization == assets::LooseMaterializationResult::ArtifactNotFound ||
                   failure.materialization == assets::LooseMaterializationResult::CorruptArtifactSet ||
                   failure.materialization == assets::LooseMaterializationResult::DescriptorMismatch;
        }

        [[nodiscard]] bool ArtifactBuildLess(const assets::ArtifactSetKey& left,
                                             const assets::ArtifactSetKey& right) noexcept
        {
            for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
            {
                if (left.build.bytes[index] != right.build.bytes[index])
                    return left.build.bytes[index] < right.build.bytes[index];
            }
            return false;
        }

        void SortAndDedupeProtectionBuilds(containers::DynamicArray<assets::ArtifactSetKey>& keys) noexcept
        {
            std::sort(keys.Begin(), keys.End(), ArtifactBuildLess);
            u32 write = 0u;
            for (u32 index = 0; index < keys.Size(); ++index)
            {
                if (write != 0u && keys[write - 1u].build == keys[index].build)
                    continue;
                if (write != index)
                    keys[write] = keys[index];
                ++write;
            }
            keys.Resize(write);
        }

        [[nodiscard]] bool MergeProtectionBuilds(containers::DynamicArray<assets::ArtifactSetKey>& keys,
                                                 containers::DynamicArray<assets::ArtifactSetKey>& additional,
                                                 const u32 maximumRecords, const u64 maximumBytes) noexcept
        {
            SortAndDedupeProtectionBuilds(keys);
            SortAndDedupeProtectionBuilds(additional);
            u32 left = 0u;
            u32 right = 0u;
            u32 unionCount = 0u;
            while (left < keys.Size() || right < additional.Size())
            {
                if (left < keys.Size() && right < additional.Size())
                {
                    if (keys[left].build == additional[right].build)
                    {
                        ++left;
                        ++right;
                    }
                    else if (ArtifactBuildLess(keys[left], additional[right]))
                        ++left;
                    else
                        ++right;
                }
                else if (left < keys.Size())
                    ++left;
                else
                    ++right;
                ++unionCount;
                if (unionCount > maximumRecords || static_cast<u64>(unionCount) > maximumBytes / sizeof(assets::ArtifactSetKey))
                    return false;
            }

            const u32 leftCount = keys.Size();
            const u32 rightCount = additional.Size();
            keys.Resize(unionCount);
            if (keys.Size() != unionCount)
                return false;
            left = leftCount;
            right = rightCount;
            u32 write = unionCount;
            while (write != 0u)
            {
                if (left != 0u && right != 0u)
                {
                    const assets::ArtifactSetKey leftKey = keys[left - 1u];
                    const assets::ArtifactSetKey rightKey = additional[right - 1u];
                    if (leftKey.build == rightKey.build)
                    {
                        keys[--write] = leftKey;
                        --left;
                        --right;
                    }
                    else if (ArtifactBuildLess(leftKey, rightKey))
                    {
                        keys[--write] = rightKey;
                        --right;
                    }
                    else
                    {
                        keys[--write] = leftKey;
                        --left;
                    }
                }
                else if (right != 0u)
                    keys[--write] = additional[--right];
                else
                    keys[--write] = keys[--left];
            }
            return true;
        }
    } // namespace

    struct ProjectAssetPipeline::Impl final
    {
        Impl(filesystem::Manager& fileManager, assets::SourceDatabase& sourceDatabase,
             streaming::ResourceStreamer& resourceStreamer, const ProjectAssetPipelineConfig& pipelineConfig) noexcept
            : files(&fileManager), sources(&sourceDatabase), streamer(&resourceStreamer), config(pipelineConfig),
              accepted(pipelineConfig.changeState),
              looseRetirement(pipelineConfig.cacheMaintenance),
              automaticRecook(pipelineConfig.automaticRecook, pipelineConfig.maximumPublishedResources),
              vddcMaintenance(pipelineConfig.cacheMaintenance),
              writableRoots(memory::pools::Assets::GetInstance()), publications(memory::pools::Assets::GetInstance()),
              publicationIndex(memory::pools::Assets::GetInstance()),
              undo(memory::pools::Assets::GetInstance()), redo(memory::pools::Assets::GetInstance())
        {
        }

        filesystem::Manager* files = nullptr;
        assets::SourceDatabase* sources = nullptr;
        streaming::ResourceStreamer* streamer = nullptr;
        bool managedLooseRootRegistered = false;
        ProjectAssetPipelineConfig config;
        ProjectAssetAcceptedBaseline accepted;
        // The planned-import owner may rescan the just-committed source state
        // while its durable cohort intent is live. All other source mutations are
        // excluded until that intent is settled or safely abandoned.
        bool plannedImportIntentOwner = false;

        struct DeferredPlannedImport final
        {
            [[nodiscard]] bool IsActive() const noexcept
            {
                return asset.IsValid() && sourceIdentity.IsValid() && sourceDestination.IsFilePath() &&
                       !sourceFingerprint.IsEmpty() && !targetMetadataFingerprint.IsEmpty() &&
                       baseAcceptedGeneration != 0u;
            }

            void Reset() noexcept
            {
                asset = {};
                sourceIdentity = {};
                sourceDestination = {};
                sourceFingerprint = {};
                targetMetadataFingerprint = {};
                baseAcceptedGeneration = 0u;
                acceptedGeneration = 0u;
                awaitingOwnedAcceptance = false;
            }

            assets::AssetId asset;
            resources::ResourcePath sourceIdentity;
            filesystem::AbsolutePath sourceDestination;
            crypto::Digest256 sourceFingerprint;
            crypto::Digest256 targetMetadataFingerprint;
            u64 baseAcceptedGeneration = 0u;
            u64 acceptedGeneration = 0u;
            bool awaitingOwnedAcceptance = false;
        };
        DeferredPlannedImport deferredPlannedImport;
        ProjectAssetDeferredPlannedImportStatus deferredPlannedImportStatus;

        struct DeferredPlannedImportPrepare final
        {
            DeferredPlannedImportPrepare() noexcept
                : historyPaths(memory::pools::Assets::GetInstance()), postConditions(memory::pools::Assets::GetInstance()),
                  stagedOutputPaths(memory::pools::Assets::GetInstance()), jobName("Assets/PlannedImportPrepare")
            {
            }

            [[nodiscard]] bool IsActive() const noexcept { return active; }
            [[nodiscard]] bool HasWork() const noexcept { return active || workRunning; }

            static bool IsCancellationRequested(void* const userData) noexcept
            {
                const auto* const state = static_cast<const DeferredPlannedImportPrepare*>(userData);
                return state != nullptr && state->cancellationRequested.GetValue();
            }

            static void ReportProgress(const assets::AssetFileTransactionPrepareProgress& progress, void* const userData) noexcept
            {
                auto* const state = static_cast<DeferredPlannedImportPrepare*>(userData);
                if (state == nullptr)
                    return;
                state->progressPhase.SetValue(
                    progress.phase == assets::AssetFileTransactionPreparePhase::Verifying ? 2u : 1u);
                state->progressCompleted.SetValue(progress.completedBytes);
                state->progressTotal.SetValue(progress.totalBytes);
            }

            struct WorkCompletion final
            {
                explicit WorkCompletion(DeferredPlannedImportPrepare& value) noexcept : owner(&value) {}
                ~WorkCompletion()
                {
                    owner->workCompleted.SetValue(true);
                    owner->workCompletedEvent.Signal();
                }
                DeferredPlannedImportPrepare* owner = nullptr;
            };

            [[nodiscard]] bool Dispatch(assets::AssetFileTransaction& transaction) noexcept
            {
                if (!active || workRunning)
                    return false;
                cancellationRequested.SetValue(false);
                workCompleted.SetValue(false);
                progressPhase.SetValue(0u);
                progressCompleted.SetValue(0u);
                progressTotal.SetValue(0u);
                workerResult = assets::AssetFileTransactionResult::InvalidState;
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this, transactionOwner = &transaction](const jobs::JobContext&) noexcept
                {
                    WorkCompletion completion(*this);
                    const assets::AssetFileTransactionPrepareObserver observer{
                        &DeferredPlannedImportPrepare::IsCancellationRequested,
                        &DeferredPlannedImportPrepare::ReportProgress, this};
                    workerResult = transactionOwner->Prepare(observer);
                });
                if (!builder.IsValid() || !task || !builder.Dispatch(jobName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                return true;
            }

            void CancelAndJoin() noexcept
            {
                if (workRunning && !workCompleted.GetValue())
                {
                    cancellationRequested.SetValue(true);
                    workCompletedEvent.Wait();
                }
                workRunning = false;
                cancellationRequested.SetValue(false);
            }

            void Reset() noexcept
            {
                CancelAndJoin();
                inverse = AssetHistoryRecord{};
                historyPaths.Clear();
                postConditions.Clear();
                cohortIntent.Reset();
                stagedOutputPaths.Clear();
                asset = {};
                importer = assets::InvalidSourceImporterId;
                sourceRoot = {};
                sourceIdentity = {};
                sourceDestination = {};
                sourceFingerprint = {};
                baseAcceptedGeneration = 0u;
                active = false;
                workerResult = assets::AssetFileTransactionResult::InvalidState;
                workCompleted.SetValue(true);
                progressPhase.SetValue(0u);
                progressCompleted.SetValue(0u);
                progressTotal.SetValue(0u);
            }

            AssetHistoryRecord inverse;
            containers::DynamicArray<filesystem::AbsolutePath> historyPaths;
            containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions;
            ProjectAssetCohortIntent cohortIntent;
            containers::DynamicArray<resources::ResourcePath> stagedOutputPaths;
            assets::AssetId asset;
            assets::SourceImporterId importer = assets::InvalidSourceImporterId;
            filesystem::AbsolutePath sourceRoot;
            resources::ResourcePath sourceIdentity;
            filesystem::AbsolutePath sourceDestination;
            crypto::Digest256 sourceFingerprint;
            u64 baseAcceptedGeneration = 0u;
            jobs::JobName jobName;
            concurrency::Atomic<bool> workCompleted{true};
            concurrency::Atomic<bool> cancellationRequested{false};
            concurrency::ManualResetEvent workCompletedEvent{true};
            concurrency::Atomic<u32> progressPhase{0u};
            concurrency::Atomic<u64> progressCompleted{0u};
            concurrency::Atomic<u64> progressTotal{0u};
            assets::AssetFileTransactionResult workerResult = assets::AssetFileTransactionResult::InvalidState;
            bool active = false;
            bool workRunning = false;
        };
        DeferredPlannedImportPrepare deferredPlannedImportPrepare;

        struct LooseGenerationKey final
        {
            resources::ResourceId resource = resources::InvalidResourceId;
            assets::BuildFingerprint build;

            [[nodiscard]] u32 CalcHash() const noexcept
            {
                u32 hash = static_cast<u32>(resource) ^ static_cast<u32>(resource >> 32u) ^ 2166136261u;
                for (const u8 byte : build.bytes)
                    hash = (hash ^ byte) * 16777619u;
                return hash;
            }

            [[nodiscard]] friend bool operator==(const LooseGenerationKey&, const LooseGenerationKey&) noexcept = default;
        };

        struct ResidentRetirementKey final
        {
            resources::ResourceId resource = resources::InvalidResourceId;
            u32 generation = 0u;

            [[nodiscard]] u32 CalcHash() const noexcept
            {
                return static_cast<u32>(resource) ^ static_cast<u32>(resource >> 32u) ^ (generation * 16777619u);
            }

            [[nodiscard]] friend bool operator==(const ResidentRetirementKey&, const ResidentRetirementKey&) noexcept = default;
        };

        struct RetiredLooseCapture final
        {
            resources::ResourceReference resource;
            assets::BuildFingerprint build;
            assets::BuildFingerprint content;
            filesystem::AbsolutePath physicalPath;
            u32 residentGeneration = 0u;

            [[nodiscard]] bool IsValid() const noexcept
            {
                return resource.IsValid() && resource.IsTyped() && !build.IsEmpty() && !content.IsEmpty() && physicalPath.IsFilePath();
            }
        };

        struct RetiredLooseCandidate final
        {
            LooseGenerationKey key;
            resources::ResourceTypeId resourceType = resources::InvalidResourceTypeId;
            assets::BuildFingerprint content;
            filesystem::AbsolutePath physicalPath;
            u64 physicalBytes = 0u;
            u64 reservation = 0u;
            u32 residentGeneration = 0u;
            bool residentRetirementPending = false;
            bool settlementPending = false;
            bool rediscovered = false;
        };

        struct LooseRetirementState final
        {
            explicit LooseRetirementState(const ProjectAssetCacheMaintenanceConfig& maintenanceConfig) noexcept
                : config(maintenanceConfig), candidates(memory::pools::Assets::GetInstance()),
                  candidateIndex(memory::pools::Assets::GetInstance()), residentIndex(memory::pools::Assets::GetInstance()),
                  diagnostics(memory::pools::Assets::GetInstance()), workerDiagnostics(memory::pools::Assets::GetInstance()),
                  workerCandidates(memory::pools::Assets::GetInstance()), workerCurrent(memory::pools::Assets::GetInstance()),
                  workerCandidateIndex(memory::pools::Assets::GetInstance()),
                  workerCurrentIndex(memory::pools::Assets::GetInstance()), jobName("Assets/ProjectLooseMaintenance")
            {
            }

            ProjectAssetCacheMaintenanceConfig config;
            containers::DynamicArray<RetiredLooseCandidate> candidates;
            containers::HashMap<LooseGenerationKey, u32> candidateIndex;
            containers::HashMap<ResidentRetirementKey, u32> residentIndex;
            mutable concurrency::Mutex mutex;
            u64 candidateBytes = 0u;
            u64 nextReservation = 1u;
            ProjectAssetLooseMaintenanceStatus status;
            containers::DynamicArray<ProjectAssetLooseMaintenanceDiagnostic> diagnostics;
            containers::DynamicArray<ProjectAssetLooseMaintenanceDiagnostic> workerDiagnostics;
            containers::DynamicArray<RetiredLooseCandidate> workerCandidates;
            containers::HashMap<LooseGenerationKey, u32> workerCandidateIndex;
            containers::DynamicArray<PublishedResource> workerCurrent;
            containers::HashMap<resources::ResourceId, u32> workerCurrentIndex;
            ProjectAssetLooseMaintenanceStatus workerStatus;
            ProjectAssetLooseMaintenanceResult workerResult = ProjectAssetLooseMaintenanceResult::Success;
            jobs::JobName jobName;
            concurrency::Atomic<bool> workCompleted{true};
            concurrency::Atomic<bool> cancellationRequested{false};
            concurrency::ManualResetEvent workCompletedEvent{true};
            bool workRunning = false;
            bool admissionOwned = false;

            [[nodiscard]] bool ReserveStorage(const u32 maximumPublishedResources) noexcept
            {
                candidates.Reserve(config.maximumRetiredLooseCandidates);
                candidateIndex.Reserve(config.maximumRetiredLooseCandidates);
                residentIndex.Reserve(config.maximumRetiredLooseCandidates);
                diagnostics.Reserve(config.maximumStatusRows);
                workerDiagnostics.Reserve(config.maximumStatusRows);
                workerCandidates.Reserve(config.maximumRetiredLooseCandidates);
                workerCandidateIndex.Reserve(config.maximumRetiredLooseCandidates);
                workerCurrent.Reserve(maximumPublishedResources);
                workerCurrentIndex.Reserve(maximumPublishedResources);
                return candidates.Capacity() >= config.maximumRetiredLooseCandidates &&
                       candidateIndex.Capacity() >= config.maximumRetiredLooseCandidates &&
                       residentIndex.Capacity() >= config.maximumRetiredLooseCandidates &&
                       diagnostics.Capacity() >= config.maximumStatusRows &&
                       workerDiagnostics.Capacity() >= config.maximumStatusRows &&
                       workerCandidates.Capacity() >= config.maximumRetiredLooseCandidates &&
                       workerCandidateIndex.Capacity() >= config.maximumRetiredLooseCandidates &&
                       workerCurrent.Capacity() >= maximumPublishedResources &&
                       workerCurrentIndex.Capacity() >= maximumPublishedResources;
            }

            [[nodiscard]] bool OwnsAdmission() const noexcept { return admissionOwned; }
            [[nodiscard]] bool HasWork() const noexcept { return admissionOwned || workRunning; }

            [[nodiscard]] static LooseGenerationKey KeyOf(const RetiredLooseCapture& capture) noexcept
            {
                return {capture.resource.GetPath().Id(), capture.build};
            }

            [[nodiscard]] bool ReindexMovedCandidateLocked(const u32 index) noexcept
            {
                if (index >= candidates.Size())
                    return true;
                RetiredLooseCandidate& moved = candidates[index];
                u32* const candidateSlot = candidateIndex.FindPtr(moved.key);
                if (candidateSlot == nullptr)
                    return false;
                *candidateSlot = index;
                if (moved.residentRetirementPending)
                {
                    const ResidentRetirementKey residentKey{moved.key.resource, moved.residentGeneration};
                    u32* const residentSlot = residentIndex.FindPtr(residentKey);
                    if (residentSlot == nullptr)
                        return false;
                    *residentSlot = index;
                }
                return true;
            }

            [[nodiscard]] bool RemoveCandidateLocked(const u32 index) noexcept
            {
                if (index >= candidates.Size())
                    return false;
                const RetiredLooseCandidate removed = candidates[index];
                if (!candidateIndex.Remove(removed.key).IsSuccessful())
                    return false;
                if (removed.residentRetirementPending)
                {
                    const ResidentRetirementKey residentKey{removed.key.resource, removed.residentGeneration};
                    if (!residentIndex.Remove(residentKey).IsSuccessful())
                        return false;
                }
                candidateBytes -= removed.physicalBytes;
                const u32 last = candidates.Size() - 1u;
                if (!candidates.RemoveAtReorder(index).IsSuccessful())
                    return false;
                return index == last || ReindexMovedCandidateLocked(index);
            }

            [[nodiscard]] bool ReserveBatch(filesystem::Manager& files,
                                            const containers::ArraySpan<const RetiredLooseCapture> captures,
                                            u64& reservation) noexcept
            {
                reservation = 0u;
                if (captures.Empty())
                    return true;
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                u32 additions = 0u;
                u64 additionalBytes = 0u;
                for (u32 index = 0; index < captures.Count(); ++index)
                {
                    const RetiredLooseCapture& capture = captures[index];
                    if (!capture.IsValid())
                        return false;
                    const LooseGenerationKey key = KeyOf(capture);
                    u32 existing = 0u;
                    if (candidateIndex.Find(key, existing))
                    {
                        if (existing >= candidates.Size() || candidates[existing].physicalPath != capture.physicalPath)
                            return false;
                        continue;
                    }
                    bool duplicate = false;
                    for (u32 previous = 0; previous < index; ++previous)
                    {
                        if (KeyOf(captures[previous]) != key)
                            continue;
                        if (captures[previous].physicalPath != capture.physicalPath)
                            return false;
                        // Exact duplicate inside one atomic publication group is
                        // already covered by the earlier element; do not charge
                        // capacity twice during the fail-before-cutover preflight.
                        duplicate = true;
                        break;
                    }
                    if (duplicate)
                        continue;
                    const u64 bytes = files.FileExist(capture.physicalPath) ? files.GetFileSize(capture.physicalPath) : 0u;
                    if (bytes > config.maximumRetiredLooseBytes ||
                        additionalBytes > config.maximumRetiredLooseBytes - bytes)
                        return false;
                    additionalBytes += bytes;
                    ++additions;
                }
                if (candidates.Size() + additions > config.maximumRetiredLooseCandidates ||
                    additionalBytes > config.maximumRetiredLooseBytes ||
                    candidateBytes > config.maximumRetiredLooseBytes - additionalBytes ||
                    candidates.Size() + additions > candidates.Capacity() ||
                    candidateIndex.Size() + additions > candidateIndex.Capacity() ||
                    residentIndex.Size() + additions > residentIndex.Capacity())
                    return false;

                if (additions == 0u)
                    return true;
                reservation = nextReservation++;
                if (reservation == 0u)
                    reservation = nextReservation++;
                const u32 originalSize = candidates.Size();
                const u64 originalBytes = candidateBytes;
                for (const RetiredLooseCapture& capture : captures)
                {
                    const LooseGenerationKey key = KeyOf(capture);
                    u32 existing = 0u;
                    if (candidateIndex.Find(key, existing))
                        continue;
                    RetiredLooseCandidate candidate;
                    candidate.key = key;
                    candidate.resourceType = capture.resource.ExpectedType();
                    candidate.content = capture.content;
                    candidate.physicalPath = capture.physicalPath;
                    candidate.physicalBytes = files.FileExist(capture.physicalPath) ? files.GetFileSize(capture.physicalPath) : 0u;
                    // The preflight stat above is advisory only: an external
                    // filesystem actor can still change a file between the two
                    // observations. Recheck the exact retained-byte invariant
                    // before adopting each candidate so a size race fails closed
                    // rather than escaping the configured ledger bound.
                    if (candidate.physicalBytes > config.maximumRetiredLooseBytes ||
                        candidateBytes > config.maximumRetiredLooseBytes - candidate.physicalBytes)
                    {
                        while (candidates.Size() > originalSize)
                            static_cast<void>(RemoveCandidateLocked(candidates.Size() - 1u));
                        candidateBytes = originalBytes;
                        reservation = 0u;
                        return false;
                    }
                    candidate.reservation = reservation;
                    candidate.residentGeneration = capture.residentGeneration;
                    candidate.residentRetirementPending = capture.residentGeneration != 0u;
                    const u32 candidateSlot = candidates.Size();
                    candidates.PushBack(static_cast<RetiredLooseCandidate&&>(candidate));
                    if (candidates.Size() != candidateSlot + 1u)
                    {
                        while (candidates.Size() > originalSize)
                            static_cast<void>(RemoveCandidateLocked(candidates.Size() - 1u));
                        candidateBytes = originalBytes;
                        reservation = 0u;
                        return false;
                    }
                    if (!candidateIndex.Insert(key, candidateSlot).IsSuccessful())
                    {
                        candidates.Resize(candidateSlot);
                        while (candidates.Size() > originalSize)
                            static_cast<void>(RemoveCandidateLocked(candidates.Size() - 1u));
                        candidateBytes = originalBytes;
                        reservation = 0u;
                        return false;
                    }
                    const ResidentRetirementKey residentKey{key.resource, capture.residentGeneration};
                    if (capture.residentGeneration != 0u &&
                        !residentIndex.Insert(residentKey, candidateSlot).IsSuccessful())
                    {
                        static_cast<void>(candidateIndex.Remove(key));
                        candidates.Resize(candidateSlot);
                        while (candidates.Size() > originalSize)
                            static_cast<void>(RemoveCandidateLocked(candidates.Size() - 1u));
                        candidateBytes = originalBytes;
                        reservation = 0u;
                        return false;
                    }
                    candidateBytes += candidates[candidateSlot].physicalBytes;
                }
                status.trackedCandidates = candidates.Size();
                status.trackedCandidateBytes = candidateBytes;
                return true;
            }

            void RollbackReservation(const containers::ArraySpan<const RetiredLooseCapture> captures, const u64 reservation) noexcept
            {
                if (reservation == 0u)
                    return;
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                for (const RetiredLooseCapture& capture : captures)
                {
                    u32 index = 0u;
                    if (candidateIndex.Find(KeyOf(capture), index) && index < candidates.Size() && candidates[index].reservation == reservation)
                        static_cast<void>(RemoveCandidateLocked(index));
                }
                status.trackedCandidates = candidates.Size();
                status.trackedCandidateBytes = candidateBytes;
            }

            [[nodiscard]] bool CommitReservation(const containers::ArraySpan<const RetiredLooseCapture> captures,
                                                 const u64 reservation) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                for (const RetiredLooseCapture& capture : captures)
                {
                    u32 index = 0u;
                    if (!candidateIndex.Find(KeyOf(capture), index) || index >= candidates.Size() ||
                        candidates[index].physicalPath != capture.physicalPath)
                        return false;
                    RetiredLooseCandidate& candidate = candidates[index];
                    if (candidate.reservation != 0u && candidate.reservation != reservation)
                        return false;
                    candidate.reservation = 0u;
                    candidate.settlementPending = true;
                }
                return true;
            }

            void Settle(const containers::ArraySpan<const RetiredLooseCapture> captures) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                for (const RetiredLooseCapture& capture : captures)
                {
                    u32 index = 0u;
                    if (candidateIndex.Find(KeyOf(capture), index) && index < candidates.Size() &&
                        candidates[index].physicalPath == capture.physicalPath)
                        candidates[index].settlementPending = false;
                }
            }

            [[nodiscard]] ProjectAssetResidentRetirementResult ReportResidentRetirement(
                const ProjectAssetResidentRetirementEvidence& evidence) noexcept
            {
                if (!evidence.IsValid())
                    return ProjectAssetResidentRetirementResult::InvalidArgument;
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                u32 index = 0u;
                const ResidentRetirementKey key{evidence.resource.Id(), evidence.resourceGeneration};
                if (!residentIndex.Find(key, index) || index >= candidates.Size())
                    return ProjectAssetResidentRetirementResult::NotTracked;
                RetiredLooseCandidate& candidate = candidates[index];
                if (!candidate.residentRetirementPending || candidate.key.resource != evidence.resource.Id() ||
                    candidate.residentGeneration != evidence.resourceGeneration)
                    return ProjectAssetResidentRetirementResult::NotTracked;
                candidate.residentRetirementPending = false;
                candidate.residentGeneration = 0u;
                static_cast<void>(residentIndex.Remove(key));
                return ProjectAssetResidentRetirementResult::Accepted;
            }

            void AddWorkerDiagnostic(const filesystem::AbsolutePath& path,
                                     const streaming::LooseGenerationRetireResult result,
                                     const bool rediscovered) noexcept
            {
                if (workerDiagnostics.Size() >= config.maximumStatusRows)
                {
                    workerStatus.statusRowsTruncated = true;
                    return;
                }
                const u32 before = workerDiagnostics.Size();
                workerDiagnostics.PushBack({path, result, rediscovered});
                if (workerDiagnostics.Size() != before + 1u)
                    workerStatus.statusRowsTruncated = true;
            }

            [[nodiscard]] bool PrepareWorkerSnapshot(const containers::ArraySpan<const PublishedResource> current) noexcept
            {
                workerCandidates.Clear();
                workerCandidateIndex.Clear();
                workerCurrent.Clear();
                workerCurrentIndex.Clear();
                workerDiagnostics.Clear();
                workerStatus = {};
                workerResult = ProjectAssetLooseMaintenanceResult::Success;

                {
                    concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                    for (const RetiredLooseCandidate& candidate : candidates)
                    {
                        const u32 slot = workerCandidates.Size();
                        workerCandidates.PushBack(candidate);
                        if (workerCandidates.Size() != slot + 1u ||
                            !workerCandidateIndex.Insert(candidate.key, slot).IsSuccessful())
                            return false;
                    }
                    workerStatus.trackedCandidates = candidates.Size();
                    workerStatus.trackedCandidateBytes = candidateBytes;
                    for (const RetiredLooseCandidate& candidate : candidates)
                    {
                        workerStatus.residentProtectedRecords += candidate.residentRetirementPending ? 1u : 0u;
                        workerStatus.settlementProtectedRecords +=
                            (candidate.settlementPending || candidate.reservation != 0u) ? 1u : 0u;
                    }
                }

                for (const PublishedResource& publication : current)
                {
                    const u32 slot = workerCurrent.Size();
                    workerCurrent.PushBack(publication);
                    if (workerCurrent.Size() != slot + 1u ||
                        !workerCurrentIndex.Insert(publication.resource.GetPath().Id(), slot).IsSuccessful())
                        return false;
                }
                return true;
            }

            [[nodiscard]] bool IsCurrentWorkerPath(const resources::ResourceId resource,
                                                   const filesystem::AbsolutePath& path) const noexcept
            {
                u32 currentIndex = 0u;
                return workerCurrentIndex.Find(resource, currentIndex) && currentIndex < workerCurrent.Size() &&
                       workerCurrent[currentIndex].physicalPath == path;
            }

            [[nodiscard]] bool RunMaintenance(filesystem::Manager& files, streaming::ResourceStreamer& streamer,
                                              const filesystem::AbsolutePath& looseRoot) noexcept
            {
                containers::DynamicArray<filesystem::AbsolutePath> scanned(memory::pools::Assets::GetInstance());
                const filesystem::ScanResult scannedResult = filesystem::ScanFiles(looseRoot, scanned, config.maximumLooseScanEntries);
                if (scannedResult != filesystem::ScanResult::Success)
                {
                    workerStatus.limitExceeded = scannedResult == filesystem::ScanResult::LimitExceeded;
                    workerStatus.retryRequired = true;
                    workerResult = workerStatus.limitExceeded ? ProjectAssetLooseMaintenanceResult::LimitExceeded
                                                              : ProjectAssetLooseMaintenanceResult::IoFailure;
                    return false;
                }

                u64 retainedBytes = 0u;
                for (const RetiredLooseCandidate& candidate : workerCandidates)
                {
                    if (candidate.physicalBytes > config.maximumRetiredLooseBytes ||
                        retainedBytes > config.maximumRetiredLooseBytes - candidate.physicalBytes)
                    {
                        workerStatus.limitExceeded = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                        return false;
                    }
                    retainedBytes += candidate.physicalBytes;
                }

                u32 deleteAttempts = 0u;
                for (const filesystem::AbsolutePath& path : scanned)
                {
                    if (cancellationRequested.GetValue())
                    {
                        workerStatus.cancellationRequested = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::Cancelled;
                        return false;
                    }
                    const u64 bytes = files.FileExist(path) ? files.GetFileSize(path) : 0u;
                    if (bytes > config.maximumLooseScanBytes || workerStatus.scannedBytes > config.maximumLooseScanBytes - bytes)
                    {
                        workerStatus.limitExceeded = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                        return false;
                    }
                    workerStatus.scannedBytes += bytes;
                    ++workerStatus.scannedRecords;

                    resources::ResourceId resource = resources::InvalidResourceId;
                    assets::BuildFingerprint build;
                    bool stage = false;
                    if (!ReadLooseGenerationIdentity(filesystem::paths::GetFileName(path), resource, build, stage))
                    {
                        ++workerStatus.foreignFiles;
                        continue;
                    }
                    if (CanonicalLooseGenerationPath(looseRoot, resource, build, stage) != path)
                    {
                        ++workerStatus.foreignFiles;
                        continue;
                    }
                    if (stage)
                    {
                        ++workerStatus.stageFiles;
                        if (deleteAttempts >= config.maximumLooseDeletesPerBatch)
                        {
                            workerStatus.deleteBatchLimitReached = true;
                            workerStatus.retryRequired = true;
                            continue;
                        }
                        ++deleteAttempts;
                        const streaming::LooseGenerationRetireResult retired = streamer.RetireLooseGeneration(path);
                        if (retired == streaming::LooseGenerationRetireResult::Removed)
                            continue;
                        AddWorkerDiagnostic(path, retired, false);
                        if (retired == streaming::LooseGenerationRetireResult::Busy ||
                            retired == streaming::LooseGenerationRetireResult::Current)
                        {
                            ++workerStatus.streamerProtectedRecords;
                            ++workerStatus.deferredDeletes;
                            workerStatus.retryRequired = true;
                        }
                        else
                        {
                            ++workerStatus.failedDeletes;
                            workerStatus.retryRequired = true;
                            if (retired == streaming::LooseGenerationRetireResult::LimitExceeded)
                                workerStatus.limitExceeded = true;
                        }
                        continue;
                    }
                    if (IsCurrentWorkerPath(resource, path))
                        continue;

                    const LooseGenerationKey key{resource, build};
                    u32 existing = 0u;
                    if (workerCandidateIndex.Find(key, existing))
                    {
                        if (existing >= workerCandidates.Size() || workerCandidates[existing].physicalPath != path)
                        {
                            workerStatus.retryRequired = true;
                            workerResult = ProjectAssetLooseMaintenanceResult::InvalidState;
                            return false;
                        }
                        continue;
                    }
                    if (workerCandidates.Size() >= config.maximumRetiredLooseCandidates || bytes > config.maximumRetiredLooseBytes ||
                        retainedBytes > config.maximumRetiredLooseBytes - bytes)
                    {
                        workerStatus.limitExceeded = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                        return false;
                    }
                    RetiredLooseCandidate candidate;
                    candidate.key = key;
                    candidate.physicalPath = path;
                    candidate.physicalBytes = bytes;
                    candidate.rediscovered = true;
                    const u32 slot = workerCandidates.Size();
                    workerCandidates.PushBack(static_cast<RetiredLooseCandidate&&>(candidate));
                    if (workerCandidates.Size() != slot + 1u || !workerCandidateIndex.Insert(key, slot).IsSuccessful())
                    {
                        workerStatus.limitExceeded = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                        return false;
                    }
                    retainedBytes += bytes;
                    ++workerStatus.rediscoveredRecords;
                }

                if (workerCandidates.Size() > 1u)
                    std::sort(workerCandidates.TypedData(), workerCandidates.TypedData() + workerCandidates.Size(),
                              [](const RetiredLooseCandidate& left, const RetiredLooseCandidate& right) noexcept
                              {
                                  if (left.key.resource != right.key.resource)
                                      return left.key.resource < right.key.resource;
                                  return left.key.build < right.key.build;
                              });

                for (RetiredLooseCandidate& candidate : workerCandidates)
                {
                    if (cancellationRequested.GetValue())
                    {
                        workerStatus.cancellationRequested = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::Cancelled;
                        break;
                    }
                    if (candidate.physicalPath.Empty())
                        continue;
                    if (candidate.reservation != 0u || candidate.settlementPending)
                        continue;
                    if (candidate.residentRetirementPending)
                        continue;
                    if (IsCurrentWorkerPath(candidate.key.resource, candidate.physicalPath))
                    {
                        ++workerStatus.streamerProtectedRecords;
                        ++workerStatus.deferredDeletes;
                        workerStatus.retryRequired = true;
                        continue;
                    }
                    if (deleteAttempts >= config.maximumLooseDeletesPerBatch)
                    {
                        workerStatus.deleteBatchLimitReached = true;
                        workerStatus.retryRequired = true;
                        break;
                    }
                    ++deleteAttempts;
                    const streaming::LooseGenerationRetireResult retired = streamer.RetireLooseGeneration(candidate.physicalPath);
                    if (retired == streaming::LooseGenerationRetireResult::Removed)
                    {
                        ++workerStatus.removedRecords;
                        workerStatus.removedBytes += candidate.physicalBytes;
                        retainedBytes -= candidate.physicalBytes;
                        candidate.physicalPath = {};
                        continue;
                    }
                    AddWorkerDiagnostic(candidate.physicalPath, retired, candidate.rediscovered);
                    if (retired == streaming::LooseGenerationRetireResult::Busy ||
                        retired == streaming::LooseGenerationRetireResult::Current)
                    {
                        ++workerStatus.streamerProtectedRecords;
                        ++workerStatus.deferredDeletes;
                        workerStatus.retryRequired = true;
                    }
                    else
                    {
                        ++workerStatus.failedDeletes;
                        workerStatus.retryRequired = true;
                        if (retired == streaming::LooseGenerationRetireResult::LimitExceeded)
                            workerStatus.limitExceeded = true;
                    }
                }

                u32 write = 0u;
                for (u32 read = 0u; read < workerCandidates.Size(); ++read)
                {
                    if (workerCandidates[read].physicalPath.Empty())
                        continue;
                    if (write != read)
                        workerCandidates[write] = static_cast<RetiredLooseCandidate&&>(workerCandidates[read]);
                    ++write;
                }
                workerCandidates.Resize(write);
                workerCandidateIndex.Clear();
                for (u32 index = 0u; index < workerCandidates.Size(); ++index)
                    if (!workerCandidateIndex.Insert(workerCandidates[index].key, index).IsSuccessful())
                    {
                        workerStatus.limitExceeded = true;
                        workerStatus.retryRequired = true;
                        workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                        return false;
                    }
                workerStatus.trackedCandidates = workerCandidates.Size();
                workerStatus.trackedCandidateBytes = retainedBytes;
                workerStatus.residentProtectedRecords = 0u;
                workerStatus.settlementProtectedRecords = 0u;
                for (const RetiredLooseCandidate& candidate : workerCandidates)
                {
                    workerStatus.residentProtectedRecords += candidate.residentRetirementPending ? 1u : 0u;
                    workerStatus.settlementProtectedRecords +=
                        (candidate.settlementPending || candidate.reservation != 0u) ? 1u : 0u;
                }

                if (workerResult == ProjectAssetLooseMaintenanceResult::Cancelled)
                    return true;
                if (workerStatus.limitExceeded)
                    workerResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
                else if (workerStatus.failedDeletes != 0u)
                    workerResult = ProjectAssetLooseMaintenanceResult::IoFailure;
                else if (workerStatus.retryRequired || workerStatus.deleteBatchLimitReached ||
                         workerStatus.streamerProtectedRecords != 0u || workerStatus.residentProtectedRecords != 0u ||
                         workerStatus.settlementProtectedRecords != 0u)
                    workerResult = ProjectAssetLooseMaintenanceResult::Deferred;
                else
                    workerResult = ProjectAssetLooseMaintenanceResult::Success;
                return true;
            }

            struct WorkCompletion final
            {
                explicit WorkCompletion(LooseRetirementState& maintenance) noexcept : owner(&maintenance) {}
                ~WorkCompletion()
                {
                    owner->workCompleted.SetValue(true);
                    owner->workCompletedEvent.Signal();
                }
                LooseRetirementState* owner = nullptr;
            };

            [[nodiscard]] bool Dispatch(filesystem::Manager& files, streaming::ResourceStreamer& streamer,
                                        const filesystem::AbsolutePath& looseRoot) noexcept
            {
                if (workRunning || !admissionOwned)
                    return false;
                cancellationRequested.SetValue(false);
                workCompleted.SetValue(false);
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this, filesOwner = &files, streamerOwner = &streamer, looseRoot](const jobs::JobContext&) noexcept
                {
                    WorkCompletion completion(*this);
                    static_cast<void>(RunMaintenance(*filesOwner, *streamerOwner, looseRoot));
                });
                if (!builder.IsValid() || !task || !builder.Dispatch(jobName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                return true;
            }

            void AdoptWorkerResult() noexcept
            {
                if (!workRunning || !workCompleted.GetValue())
                    return;
                {
                    concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                    candidates.Swap(workerCandidates);
                    candidateIndex.Clear();
                    residentIndex.Clear();
                    candidateBytes = 0u;
                    for (u32 index = 0u; index < candidates.Size(); ++index)
                    {
                        RetiredLooseCandidate& candidate = candidates[index];
                        candidateBytes += candidate.physicalBytes;
                        static_cast<void>(candidateIndex.Insert(candidate.key, index));
                        if (candidate.residentRetirementPending)
                        {
                            const ResidentRetirementKey residentKey{candidate.key.resource, candidate.residentGeneration};
                            static_cast<void>(residentIndex.Insert(residentKey, index));
                        }
                    }
                }
                diagnostics.Swap(workerDiagnostics);
                status = workerStatus;
                status.trackedCandidates = candidates.Size();
                status.trackedCandidateBytes = candidateBytes;
                status.lastResult = workerResult;
                status.retryRequired = status.retryRequired || workerResult != ProjectAssetLooseMaintenanceResult::Success;
                if (workerResult == ProjectAssetLooseMaintenanceResult::Success)
                    status.state = ProjectAssetLooseMaintenanceState::Complete;
                else if (workerResult == ProjectAssetLooseMaintenanceResult::Deferred ||
                         workerResult == ProjectAssetLooseMaintenanceResult::StreamingBusy)
                    status.state = ProjectAssetLooseMaintenanceState::Deferred;
                else if (workerResult == ProjectAssetLooseMaintenanceResult::Cancelled)
                    status.state = ProjectAssetLooseMaintenanceState::Cancelled;
                else
                    status.state = ProjectAssetLooseMaintenanceState::Failed;
                workRunning = false;
                admissionOwned = false;
                cancellationRequested.SetValue(false);
            }

            void Drain() noexcept
            {
                if (workRunning && !workCompleted.GetValue())
                {
                    cancellationRequested.SetValue(true);
                    workCompletedEvent.Wait();
                }
                if (workRunning)
                    AdoptWorkerResult();
                admissionOwned = false;
                cancellationRequested.SetValue(false);
            }

            void RefreshTrackedStatus() noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> guard(mutex);
                status.trackedCandidates = candidates.Size();
                status.trackedCandidateBytes = candidateBytes;
                status.residentProtectedRecords = 0u;
                status.settlementProtectedRecords = 0u;
                for (const RetiredLooseCandidate& candidate : candidates)
                {
                    status.residentProtectedRecords += candidate.residentRetirementPending ? 1u : 0u;
                    status.settlementProtectedRecords += (candidate.settlementPending || candidate.reservation != 0u) ? 1u : 0u;
                }
            }
        };
        LooseRetirementState looseRetirement;

        struct AutomaticRecookState
        {
            struct DeliveryKey
            {
                resources::ResourceId resource = resources::InvalidResourceId;
                resources::ResourceTypeId type = resources::InvalidResourceTypeId;

                [[nodiscard]] u32 CalcHash() const noexcept
                {
                    return static_cast<u32>(resource) ^ static_cast<u32>(resource >> 32u) ^ (type * 16777619u);
                }

                [[nodiscard]] friend bool operator==(const DeliveryKey&, const DeliveryKey&) noexcept = default;
            };

            [[nodiscard]] static DeliveryKey KeyOf(const resources::ResourceReference reference) noexcept
            {
                return {reference.GetPath().Id(), reference.ExpectedType()};
            }

            enum class WorkKind : u8
            {
                None,
                Plan,
                Poll,
                Publish
            };

            struct ExplicitDeliveryCohort final
            {
                ExplicitDeliveryCohort() noexcept
                    : outputs(memory::pools::Assets::GetInstance()), predecessorOutputs(memory::pools::Assets::GetInstance()) {}

                [[nodiscard]] bool IsActive() const noexcept
                {
                    return asset.IsValid() && acceptedGeneration != 0u && !outputs.Empty();
                }

                [[nodiscard]] bool Contains(const resources::ResourceReference output) const noexcept
                {
                    for (const resources::ResourceReference candidate : outputs)
                        if (candidate == output)
                            return true;
                    return false;
                }

                void Reset() noexcept
                {
                    asset = {};
                    acceptedGeneration = 0u;
                    outputs.Clear();
                    predecessorOutputs.Clear();
                    deliveryComplete = false;
                    publicationBlocked = false;
                }

                void Swap(ExplicitDeliveryCohort& other) noexcept
                {
                    const assets::AssetId swappedAsset = asset;
                    asset = other.asset;
                    other.asset = swappedAsset;
                    const u64 swappedGeneration = acceptedGeneration;
                    acceptedGeneration = other.acceptedGeneration;
                    other.acceptedGeneration = swappedGeneration;
                    outputs.Swap(other.outputs);
                    predecessorOutputs.Swap(other.predecessorOutputs);
                    const bool swappedComplete = deliveryComplete;
                    deliveryComplete = other.deliveryComplete;
                    other.deliveryComplete = swappedComplete;
                    const bool swappedBlocked = publicationBlocked;
                    publicationBlocked = other.publicationBlocked;
                    other.publicationBlocked = swappedBlocked;
                }

                assets::AssetId asset;
                u64 acceptedGeneration = 0u;
                containers::DynamicArray<resources::ResourceReference> outputs;
                containers::DynamicArray<resources::ResourceReference> predecessorOutputs;
                bool deliveryComplete = false;
                bool publicationBlocked = false;
            };

            explicit AutomaticRecookState(const ProjectAssetAutomaticRecookConfig& automaticConfig,
                                          const u32 outputStatusLimit) noexcept
                : config(automaticConfig), planningName("Assets/ProjectAutomaticRecook/Plan"),
                  pollingName("Assets/ProjectAutomaticRecook/Poll"), publicationName("Assets/ProjectAutomaticRecook/Publish"),
                  committedDelivery(memory::pools::Assets::GetInstance()),
                  committedDeliveryIndex(memory::pools::Assets::GetInstance()),
                  outputStatuses(memory::pools::Assets::GetInstance()),
                  outputStatusIndex(memory::pools::Assets::GetInstance()), workerPublicationOutputs(memory::pools::Assets::GetInstance()),
                  workerPublicationInfos(memory::pools::Assets::GetInstance()),
                  workerRetiredLooseCaptures(memory::pools::Assets::GetInstance()),
                  maximumOutputStatuses(outputStatusLimit)
            {
            }

            ProjectAssetAutomaticRecookConfig config;
            ProjectAssetAutomaticPendingState pending;
            ProjectAssetAutomaticPendingState active;
            ProjectAssetAutomaticPendingState retired;
            ProjectAssetAutomaticRecookStatus status;
            assets::IncrementalRecooker recooker;
            assets::RecookBatch batch;
            jobs::JobName planningName;
            jobs::JobName pollingName;
            jobs::JobName publicationName;
            assets::RecookStats workerStats;
            assets::RecookState workerState = assets::RecookState::Succeeded;
            assets::RecookFailure workerFailure = assets::RecookFailure::None;
            assets::RecookRootFailureDetail workerRootFailure;
            WorkKind workKind = WorkKind::None;
            concurrency::Atomic<bool> workCompleted{true};
            concurrency::ManualResetEvent workCompletedEvent{true};
            bool workRunning = false;
            bool workerFinished = false;
            bool workerRecoveryRequired = false;
            bool workerWasRecovery = false;
            bool recoveryRequested = false;
            bool startupRecoveryDeferred = false;
            bool admissionOwned = false;
            bool staleRejected = false;
            bool pollDispatchFailed = false;
            bool pollDispatchRetryRequested = false;
            bool recoverAfterPollDispatchFailure = false;
            bool workerDeliveryCapacityExceeded = false;
            bool workerDeliveryAdoptionFailed = false;
            bool workerPublicationSucceeded = false;
            bool workerPublicationStale = false;
            bool workerPublishedExplicitCohort = false;
            resources::ResourceReference workerRepairRequiredOutput;
            bool publicationSettlementPending = false;
            ProjectLoosePublicationResult workerPublicationResult = ProjectLoosePublicationResult::Success;
            ProjectLoosePublicationFailure workerPublicationFailure;
            u64 workerAcceptedGeneration = 0;
            bool policyEnabled = false;
            bool explicitActivity = false;
            assets::ProjectAssetBuildSystem* validationBuilds = nullptr;
            const assets::SourceDatabase* validationSources = nullptr;
            assets::DependencyIndex* validationIndex = nullptr;
            const SourceChangeAccumulator* validationChanges = nullptr;
            containers::DynamicArray<assets::DependencyRecord> committedDelivery;
            containers::HashMap<DeliveryKey, u32> committedDeliveryIndex;
            containers::DynamicArray<ProjectAssetOutputDeliveryStatus> outputStatuses;
            containers::HashMap<DeliveryKey, u32> outputStatusIndex;
            ExplicitDeliveryCohort explicitCohort;
            containers::DynamicArray<resources::ResourceReference> workerPublicationOutputs;
            containers::DynamicArray<ProjectLoosePublicationInfo> workerPublicationInfos;
            containers::DynamicArray<RetiredLooseCapture> workerRetiredLooseCaptures;
            u64 workerRetiredLooseReservation = 0u;
            mutable concurrency::Mutex deliveryMutex;
            u64 committedDeliveryBytes = 0;
            u32 maximumOutputStatuses = 0;

            struct WorkCompletion final
            {
                explicit WorkCompletion(AutomaticRecookState& state) noexcept : owner(&state) {}
                ~WorkCompletion()
                {
                    owner->workCompleted.SetValue(true);
                    owner->workCompletedEvent.Signal();
                }

                AutomaticRecookState* owner = nullptr;
            };

            [[nodiscard]] static bool Resolve(const resources::ResourceReference output, assets::OwnedBuildRequest& request,
                                              void* const userData) noexcept
            {
                auto* const state = static_cast<AutomaticRecookState*>(userData);
                if (state == nullptr || state->validationBuilds == nullptr)
                    return false;
                // The recooker resolves exactly the same immutable accepted
                // current requests the owner already retained. Share their
                // lifetime-pinned source payload instead of reconstructing
                // another 60 MB copy for every root of one imported GLB.
                // Non-current indexed dependants still resolve canonically.
                for (const assets::OwnedBuildRequest& current : state->active.currentRequests)
                    if (current.outputReference == output)
                        return request.CopyFromSharingSource(current) &&
                               request.View().IsValid() && request.outputReference == output;
                return state->validationBuilds->Resolve(output, request) == assets::ProjectAssetBuildResult::Success;
            }

            [[nodiscard]] static bool IsConfirmedHistoricalOutput(const resources::ResourceReference output,
                                                                   void* const userData) noexcept
            {
                const auto* const state = static_cast<const AutomaticRecookState*>(userData);
                // SourceDatabase is stable for the entire admitted recook. A
                // missing output owner is historical; an importer failure on a
                // PRESENT owner must remain a hard error, never silently skipped.
                return state != nullptr && state->validationSources != nullptr &&
                       state->validationSources->LookupOutput(output) == nullptr;
            }

            [[nodiscard]] static u64 DeliveryRecordBytes(const assets::DependencyRecord& record) noexcept
            {
                return sizeof(assets::DependencyRecord) +
                       static_cast<u64>(record.dependencies.Size()) * sizeof(assets::BuildDependency) +
                       static_cast<u64>(record.artifacts.Size()) * sizeof(assets::IndexedArtifact);
            }

            [[nodiscard]] bool CanTrackOutputLocked(const resources::ResourceReference output) const noexcept
            {
                if (!output.IsValid() || !output.IsTyped())
                    return false;
                u32 existing = 0;
                if (outputStatusIndex.Find(KeyOf(output), existing))
                    return existing < outputStatuses.Size() && outputStatuses[existing].resource == output;
                return outputStatuses.Size() < maximumOutputStatuses && outputStatuses.Size() < outputStatuses.Capacity() &&
                       outputStatusIndex.Size() < outputStatusIndex.Capacity();
            }

            [[nodiscard]] bool EnsureOutputStatusLocked(const resources::ResourceReference output, u32& index) noexcept
            {
                if (outputStatusIndex.Find(KeyOf(output), index))
                    return index < outputStatuses.Size() && outputStatuses[index].resource == output;
                if (!CanTrackOutputLocked(output))
                    return false;
                index = outputStatuses.Size();
                ProjectAssetOutputDeliveryStatus statusEntry;
                statusEntry.resource = output;
                outputStatuses.PushBack(static_cast<ProjectAssetOutputDeliveryStatus&&>(statusEntry));
                if (outputStatuses.Size() != index + 1u ||
                    !outputStatusIndex.Insert(KeyOf(output), index).IsSuccessful())
                {
                    outputStatuses.Resize(index);
                    return false;
                }
                return true;
            }

            [[nodiscard]] bool CanAcceptCommittedDeliveryLocked(
                const containers::ArraySpan<const assets::DependencyRecord> records) const noexcept
            {
                if (records.Empty())
                    return true;
                u32 addedRecords = 0;
                u32 addedStatuses = 0;
                u64 prospectiveBytes = committedDeliveryBytes;
                for (u32 recordIndex = 0; recordIndex < records.Count(); ++recordIndex)
                {
                    const assets::DependencyRecord& record = records[recordIndex];
                    if (!record.output.IsValid() || !record.output.IsTyped() || record.buildFingerprint.IsEmpty() ||
                        record.contentFingerprint.IsEmpty())
                        return false;
                    for (u32 previous = 0; previous < recordIndex; ++previous)
                        if (records[previous].output.GetPath().Id() == record.output.GetPath().Id())
                            return false;

                    u32 existing = 0;
                    if (committedDeliveryIndex.Find(KeyOf(record.output), existing))
                    {
                        if (existing >= committedDelivery.Size() || committedDelivery[existing].output != record.output)
                            return false;
                        const u64 previousBytes = DeliveryRecordBytes(committedDelivery[existing]);
                        if (prospectiveBytes < previousBytes)
                            return false;
                        prospectiveBytes -= previousBytes;
                    }
                    else
                    {
                        ++addedRecords;
                    }

                    const u64 recordBytes = DeliveryRecordBytes(record);
                    if (prospectiveBytes > config.maximumCapturedEvidenceBytes ||
                        recordBytes > config.maximumCapturedEvidenceBytes - prospectiveBytes)
                        return false;
                    prospectiveBytes += recordBytes;

                    if (outputStatusIndex.Find(KeyOf(record.output), existing))
                    {
                        if (existing >= outputStatuses.Size() || outputStatuses[existing].resource != record.output)
                            return false;
                    }
                    else
                    {
                        ++addedStatuses;
                    }
                }
                return committedDelivery.Size() + addedRecords <= config.maximumPendingOutputs &&
                       committedDelivery.Size() + addedRecords <= committedDelivery.Capacity() &&
                       committedDeliveryIndex.Size() + addedRecords <= committedDeliveryIndex.Capacity() &&
                       outputStatuses.Size() + addedStatuses <= maximumOutputStatuses &&
                       outputStatuses.Size() + addedStatuses <= outputStatuses.Capacity() &&
                       outputStatusIndex.Size() + addedStatuses <= outputStatusIndex.Capacity();
            }

            [[nodiscard]] bool CanAcceptCommittedDelivery(
                const containers::ArraySpan<const assets::DependencyRecord> records) const noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                return CanAcceptCommittedDeliveryLocked(records);
            }

            void RemoveCommittedDeliveryLocked(const u32 index) noexcept
            {
                if (index >= committedDelivery.Size())
                    return;
                const DeliveryKey removed = KeyOf(committedDelivery[index].output);
                const u32 last = committedDelivery.Size() - 1u;
                const DeliveryKey moved = index != last ? KeyOf(committedDelivery[last].output) : DeliveryKey{};
                const u64 removedBytes = DeliveryRecordBytes(committedDelivery[index]);
                if (!committedDeliveryIndex.Remove(removed).IsSuccessful() ||
                    !committedDelivery.RemoveAtReorder(index).IsSuccessful())
                    return;
                committedDeliveryBytes = committedDeliveryBytes >= removedBytes ? committedDeliveryBytes - removedBytes : 0u;
                if (index < committedDelivery.Size())
                {
                    u32* const movedIndex = committedDeliveryIndex.FindPtr(moved);
                    if (movedIndex != nullptr)
                        *movedIndex = index;
                }
            }

            void RemoveOutputStatusLocked(const u32 index) noexcept
            {
                if (index >= outputStatuses.Size())
                    return;
                const DeliveryKey removed = KeyOf(outputStatuses[index].resource);
                const u32 last = outputStatuses.Size() - 1u;
                const DeliveryKey moved = index != last ? KeyOf(outputStatuses[last].resource) : DeliveryKey{};
                if (!outputStatusIndex.Remove(removed).IsSuccessful() || !outputStatuses.RemoveAtReorder(index).IsSuccessful())
                    return;
                if (index < outputStatuses.Size())
                {
                    u32* const movedIndex = outputStatusIndex.FindPtr(moved);
                    if (movedIndex != nullptr)
                        *movedIndex = index;
                }
            }

            [[nodiscard]] bool AdoptCommittedDelivery(containers::DynamicArray<assets::DependencyRecord>& records,
                                                      const u64 authoredGeneration,
                                                      const bool retainInstalled = false) noexcept
            {
                if (records.Empty())
                    return true;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                if (!CanAcceptCommittedDeliveryLocked({records.TypedData(), records.Size()}))
                    return false;

                for (u32 recordIndex = 0; recordIndex < records.Size(); ++recordIndex)
                {
                    assets::DependencyRecord& incoming = records[recordIndex];
                    u32 statusIndex = 0;
                    if (!EnsureOutputStatusLocked(incoming.output, statusIndex))
                        return false;
                    ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                    statusEntry.authoredGeneration = authoredGeneration;
                    statusEntry.committedBuildFingerprint = incoming.buildFingerprint;
                    statusEntry.committedContentFingerprint = incoming.contentFingerprint;
                    statusEntry.buildState = ProjectAssetDeliveryStageState::Committed;
                    statusEntry.publicationRetryRequired = false;

                    const bool alreadyInstalled = statusEntry.publishedBuildFingerprint == incoming.buildFingerprint &&
                                                  statusEntry.publishedContentFingerprint == incoming.contentFingerprint &&
                                                  !statusEntry.publishedPhysicalPath.Empty();
                    u32 deliveryIndex = 0;
                    if (alreadyInstalled && !retainInstalled)
                    {
                        statusEntry.publicationState = ProjectAssetDeliveryStageState::Installed;
                        statusEntry.publicationReason = ProjectAssetDeliveryReason::None;
                        if (committedDeliveryIndex.Find(KeyOf(incoming.output), deliveryIndex))
                            RemoveCommittedDeliveryLocked(deliveryIndex);
                        continue;
                    }

                    statusEntry.publicationState = alreadyInstalled
                                                       ? ProjectAssetDeliveryStageState::Installed
                                                       : statusEntry.publishedBuildFingerprint.IsEmpty()
                                                             ? ProjectAssetDeliveryStageState::Pending
                                                             : ProjectAssetDeliveryStageState::Stale;
                    statusEntry.publicationReason = alreadyInstalled
                                                        ? ProjectAssetDeliveryReason::None
                                                        : statusEntry.publishedBuildFingerprint.IsEmpty()
                                                              ? ProjectAssetDeliveryReason::AwaitingPublication
                                                              : ProjectAssetDeliveryReason::BuildAdvancedPastPublication;

                    const u64 incomingBytes = DeliveryRecordBytes(incoming);
                    if (committedDeliveryIndex.Find(KeyOf(incoming.output), deliveryIndex))
                    {
                        const u64 previousBytes = DeliveryRecordBytes(committedDelivery[deliveryIndex]);
                        committedDelivery[deliveryIndex] = static_cast<assets::DependencyRecord&&>(incoming);
                        committedDeliveryBytes = committedDeliveryBytes - previousBytes + incomingBytes;
                    }
                    else
                    {
                        deliveryIndex = committedDelivery.Size();
                        committedDelivery.PushBack(static_cast<assets::DependencyRecord&&>(incoming));
                        if (committedDelivery.Size() != deliveryIndex + 1u ||
                            !committedDeliveryIndex.Insert(KeyOf(committedDelivery[deliveryIndex].output), deliveryIndex).IsSuccessful())
                            return false;
                        committedDeliveryBytes += incomingBytes;
                    }
                }
                records.Clear();
                return true;
            }

            [[nodiscard]] bool CompleteExplicitCohortDelivery() noexcept
            {
                if (!explicitCohort.IsActive())
                    return true;
                if (validationIndex == nullptr || explicitCohort.outputs.Size() > config.maximumPendingOutputs)
                    return false;

                containers::DynamicArray<assets::DependencyRecord> exact(memory::pools::Assets::GetInstance());
                exact.Reserve(explicitCohort.outputs.Size());
                if (exact.Capacity() < explicitCohort.outputs.Size())
                    return false;
                for (const resources::ResourceReference output : explicitCohort.outputs)
                {
                    assets::DependencyRecord record;
                    if (validationIndex->Find(output, record) != assets::IndexResult::Success || record.output != output)
                        return false;
                    const u32 previous = exact.Size();
                    exact.PushBack(static_cast<assets::DependencyRecord&&>(record));
                    if (exact.Size() != previous + 1u)
                        return false;
                }
                if (!AdoptCommittedDelivery(exact, explicitCohort.acceptedGeneration, true))
                    return false;

                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                for (const resources::ResourceReference output : explicitCohort.outputs)
                {
                    u32 deliveryIndex = 0u;
                    u32 statusIndex = 0u;
                    if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) || deliveryIndex >= committedDelivery.Size() ||
                        committedDelivery[deliveryIndex].output != output ||
                        !outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size() ||
                        outputStatuses[statusIndex].resource != output ||
                        outputStatuses[statusIndex].authoredGeneration != explicitCohort.acceptedGeneration ||
                        outputStatuses[statusIndex].committedBuildFingerprint != committedDelivery[deliveryIndex].buildFingerprint ||
                        outputStatuses[statusIndex].committedContentFingerprint != committedDelivery[deliveryIndex].contentFingerprint)
                        return false;
                }
                explicitCohort.deliveryComplete = true;
                return true;
            }

            void DropExplicitCohortDelivery() noexcept
            {
                if (!explicitCohort.IsActive())
                    return;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                for (const resources::ResourceReference output : explicitCohort.outputs)
                {
                    u32 deliveryIndex = 0u;
                    if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) || deliveryIndex >= committedDelivery.Size())
                        continue;
                    u32 statusIndex = 0u;
                    if (!outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size() ||
                        outputStatuses[statusIndex].authoredGeneration != explicitCohort.acceptedGeneration)
                        continue;
                    RemoveCommittedDeliveryLocked(deliveryIndex);
                }
                explicitCohort.Reset();
            }

            [[nodiscard]] bool CanTrackPublication(const resources::ResourceReference output) const noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                return CanTrackOutputLocked(output);
            }

            void RefreshResidentStateLocked(ProjectAssetOutputDeliveryStatus& statusEntry) noexcept
            {
                if (statusEntry.residentResourceGeneration == 0 || statusEntry.residentBuildFingerprint.IsEmpty() ||
                    statusEntry.residentContentFingerprint.IsEmpty())
                {
                    statusEntry.residentResourceGeneration = 0;
                    statusEntry.residentBuildFingerprint = {};
                    statusEntry.residentContentFingerprint = {};
                    statusEntry.residentState = ProjectAssetDeliveryStageState::Untracked;
                    statusEntry.residentReason = ProjectAssetDeliveryReason::ResidentNotTracked;
                    statusEntry.residentRetryRequired = false;
                    return;
                }

                const bool matchesPublished = !statusEntry.publishedPhysicalPath.Empty() &&
                                              statusEntry.residentBuildFingerprint == statusEntry.publishedBuildFingerprint &&
                                              statusEntry.residentContentFingerprint == statusEntry.publishedContentFingerprint;
                if (matchesPublished)
                {
                    statusEntry.residentState = ProjectAssetDeliveryStageState::Current;
                    statusEntry.residentReason = ProjectAssetDeliveryReason::None;
                    statusEntry.residentRetryRequired = false;
                }
                else
                {
                    statusEntry.residentState = ProjectAssetDeliveryStageState::Stale;
                    statusEntry.residentReason = ProjectAssetDeliveryReason::AwaitingResidentReplacement;
                }
            }

            [[nodiscard]] bool ReportResidentDelivery(const ProjectAssetResidentDeliveryEvidence& evidence) noexcept
            {
                if (!evidence.IsValid())
                    return false;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0;
                // Resident evidence annotates project-owned delivery truth; it
                // must never manufacture a status row for an otherwise unowned
                // runtime resource and starve committed/publication capacity.
                if (!outputStatusIndex.Find(KeyOf(evidence.resource), statusIndex) || statusIndex >= outputStatuses.Size() ||
                    outputStatuses[statusIndex].resource != evidence.resource)
                    return false;
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                statusEntry.residentResourceGeneration = evidence.resourceGeneration;
                statusEntry.residentBuildFingerprint = evidence.buildFingerprint;
                statusEntry.residentContentFingerprint = evidence.contentFingerprint;
                statusEntry.residentRetryRequired = evidence.retryRequired;
                RefreshResidentStateLocked(statusEntry);
                return true;
            }

            [[nodiscard]] bool ClearResidentDelivery(const resources::ResourceReference output) noexcept
            {
                if (!output.IsValid() || !output.IsTyped())
                    return false;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0;
                if (!outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size() ||
                    outputStatuses[statusIndex].resource != output)
                    return false;
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                statusEntry.residentResourceGeneration = 0;
                statusEntry.residentBuildFingerprint = {};
                statusEntry.residentContentFingerprint = {};
                statusEntry.residentRetryRequired = false;
                RefreshResidentStateLocked(statusEntry);
                u32 deliveryIndex = 0;
                if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) &&
                    statusEntry.publicationState == ProjectAssetDeliveryStageState::Untracked)
                    RemoveOutputStatusLocked(statusIndex);
                return true;
            }

            [[nodiscard]] bool CaptureRetiredLoose(const PublishedResource& publication,
                                                   RetiredLooseCapture& capture) const noexcept
            {
                capture = {};
                if (!publication.resource.IsValid() || !publication.resource.IsTyped() ||
                    publication.physicalPath.Empty() || publication.buildFingerprint.IsEmpty() ||
                    publication.contentFingerprint.IsEmpty())
                    return false;
                capture.resource = publication.resource;
                capture.build = publication.buildFingerprint;
                capture.content = publication.contentFingerprint;
                capture.physicalPath = publication.physicalPath;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0u;
                if (outputStatusIndex.Find(KeyOf(publication.resource), statusIndex) && statusIndex < outputStatuses.Size())
                {
                    const ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                    if (statusEntry.residentResourceGeneration != 0u &&
                        statusEntry.residentBuildFingerprint == publication.buildFingerprint &&
                        statusEntry.residentContentFingerprint == publication.contentFingerprint)
                        capture.residentGeneration = statusEntry.residentResourceGeneration;
                }
                return capture.IsValid();
            }

            [[nodiscard]] bool MarkPublished(const resources::ResourceReference output,
                                             const ProjectLoosePublicationInfo& publication) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0;
                if (!EnsureOutputStatusLocked(output, statusIndex))
                    return false;
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                const bool sameTrackedCommit = statusEntry.committedBuildFingerprint == publication.buildFingerprint &&
                                               statusEntry.committedContentFingerprint == publication.contentFingerprint;
                // The publisher reads a committed VADI record. Even when no
                // automatic handoff preceded it (manual request/restart restore),
                // these fingerprints are therefore truthful committed-build
                // evidence. P1 still does not claim source freshness, streamer
                // winner, or resident currency from this fact alone.
                if (!sameTrackedCommit)
                    statusEntry.authoredGeneration = 0u;
                statusEntry.committedBuildFingerprint = publication.buildFingerprint;
                statusEntry.committedContentFingerprint = publication.contentFingerprint;
                statusEntry.buildState = ProjectAssetDeliveryStageState::Committed;
                statusEntry.publishedBuildFingerprint = publication.buildFingerprint;
                statusEntry.publishedContentFingerprint = publication.contentFingerprint;
                statusEntry.publishedPhysicalPath = publication.physicalPath;
                statusEntry.publicationRetryRequired = false;
                statusEntry.publicationState = ProjectAssetDeliveryStageState::Installed;
                statusEntry.publicationReason = ProjectAssetDeliveryReason::None;
                RefreshResidentStateLocked(statusEntry);
                u32 deliveryIndex = 0;
                if (committedDeliveryIndex.Find(KeyOf(output), deliveryIndex))
                    RemoveCommittedDeliveryLocked(deliveryIndex);
                return true;
            }

            void MarkPublicationFailed(const resources::ResourceReference output,
                                       const ProjectLoosePublicationInfo& publication,
                                       const ProjectAssetDeliveryReason reason = ProjectAssetDeliveryReason::PublicationFailed) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0;
                const bool hasCommittedFingerprint = !publication.buildFingerprint.IsEmpty() && !publication.contentFingerprint.IsEmpty();
                if (!hasCommittedFingerprint)
                {
                    // RecordNotFound/early publisher failures have no committed
                    // generation to report. Do not consume bounded status capacity
                    // for an uncooked desired output merely because reconciliation
                    // probed it. Existing last-valid tracked state may still record
                    // the delivery failure, but an entirely untracked output stays
                    // untracked until committed evidence exists.
                    if (!outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size() ||
                        outputStatuses[statusIndex].resource != output)
                        return;
                }
                else if (!EnsureOutputStatusLocked(output, statusIndex))
                {
                    return;
                }
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                if (hasCommittedFingerprint)
                {
                    const bool sameTrackedCommit = statusEntry.committedBuildFingerprint == publication.buildFingerprint &&
                                                   statusEntry.committedContentFingerprint == publication.contentFingerprint;
                    if (!sameTrackedCommit)
                        statusEntry.authoredGeneration = 0u;
                    statusEntry.committedBuildFingerprint = publication.buildFingerprint;
                    statusEntry.committedContentFingerprint = publication.contentFingerprint;
                    statusEntry.buildState = ProjectAssetDeliveryStageState::Committed;

                    u32 deliveryIndex = 0;
                    if (committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) && deliveryIndex < committedDelivery.Size())
                    {
                        const assets::DependencyRecord& pendingRecord = committedDelivery[deliveryIndex];
                        if (pendingRecord.buildFingerprint != publication.buildFingerprint ||
                            pendingRecord.contentFingerprint != publication.contentFingerprint)
                            RemoveCommittedDeliveryLocked(deliveryIndex);
                    }
                }
                statusEntry.publicationRetryRequired = true;
                statusEntry.publicationReason = reason;
                if (statusEntry.publishedPhysicalPath.Empty())
                    statusEntry.publicationState = ProjectAssetDeliveryStageState::Failed;
                else if (reason == ProjectAssetDeliveryReason::CacheRepairRequired)
                    statusEntry.publicationState = ProjectAssetDeliveryStageState::Stale;
                else if (!statusEntry.committedBuildFingerprint.IsEmpty() &&
                         (statusEntry.publishedBuildFingerprint != statusEntry.committedBuildFingerprint ||
                          statusEntry.publishedContentFingerprint != statusEntry.committedContentFingerprint))
                    statusEntry.publicationState = ProjectAssetDeliveryStageState::Stale;
            }

            void MarkUnpublished(const resources::ResourceReference output) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 statusIndex = 0;
                if (!outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size() ||
                    outputStatuses[statusIndex].resource != output)
                    return;
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                statusEntry.publishedBuildFingerprint = {};
                statusEntry.publishedContentFingerprint = {};
                statusEntry.publishedPhysicalPath = {};
                statusEntry.publicationState = ProjectAssetDeliveryStageState::Untracked;
                statusEntry.publicationReason = ProjectAssetDeliveryReason::None;
                statusEntry.publicationRetryRequired = false;
                RefreshResidentStateLocked(statusEntry);
                u32 deliveryIndex = 0;
                if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) &&
                    statusEntry.residentState == ProjectAssetDeliveryStageState::Untracked &&
                    !statusEntry.residentRetryRequired)
                    RemoveOutputStatusLocked(statusIndex);
            }

            [[nodiscard]] bool GetOutputStatus(const resources::ResourceReference output,
                                               ProjectAssetOutputDeliveryStatus& outputStatus) const noexcept
            {
                outputStatus = {};
                if (!output.IsValid() || !output.IsTyped())
                    return false;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 index = 0;
                if (!outputStatusIndex.Find(KeyOf(output), index) || index >= outputStatuses.Size() ||
                    outputStatuses[index].resource != output)
                    return false;
                outputStatus = outputStatuses[index];
                return true;
            }

            [[nodiscard]] bool GetOutputPublicationStatus(
                const resources::ResourceReference output,
                ProjectAssetOutputPublicationStatus& publicationStatus) const noexcept
            {
                publicationStatus = {};
                if (!output.IsValid() || !output.IsTyped())
                    return false;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u32 index = 0u;
                if (!outputStatusIndex.Find(KeyOf(output), index) || index >= outputStatuses.Size() ||
                    outputStatuses[index].resource != output)
                    return false;
                const ProjectAssetOutputDeliveryStatus& source = outputStatuses[index];
                publicationStatus.resource = source.resource;
                publicationStatus.authoredGeneration = source.authoredGeneration;
                publicationStatus.committedBuildFingerprint = source.committedBuildFingerprint;
                publicationStatus.committedContentFingerprint = source.committedContentFingerprint;
                publicationStatus.publishedBuildFingerprint = source.publishedBuildFingerprint;
                publicationStatus.publishedContentFingerprint = source.publishedContentFingerprint;
                publicationStatus.buildState = source.buildState;
                publicationStatus.publicationState = source.publicationState;
                publicationStatus.publicationReason = source.publicationReason;
                publicationStatus.publicationRetryRequired = source.publicationRetryRequired;
                return true;
            }

            [[nodiscard]] bool CaptureVddcProtectionKeys(containers::DynamicArray<assets::ArtifactSetKey>& keys,
                                                         const u32 maximumRecords, const u64 maximumBytes) const noexcept
            {
                keys.Clear();
                if (maximumRecords == 0u || maximumBytes == 0u)
                    return false;
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                u64 count = committedDelivery.Size();
                for (const ProjectAssetOutputDeliveryStatus& statusEntry : outputStatuses)
                {
                    if (!statusEntry.publicationRetryRequired)
                        continue;
                    if (statusEntry.committedBuildFingerprint.IsEmpty() || statusEntry.committedContentFingerprint.IsEmpty())
                        return false;

                    bool committedCovered = false;
                    u32 deliveryIndex = 0u;
                    if (committedDeliveryIndex.Find(KeyOf(statusEntry.resource), deliveryIndex) && deliveryIndex < committedDelivery.Size())
                    {
                        const assets::DependencyRecord& delivery = committedDelivery[deliveryIndex];
                        committedCovered = delivery.output == statusEntry.resource &&
                                           delivery.buildFingerprint == statusEntry.committedBuildFingerprint &&
                                           delivery.contentFingerprint == statusEntry.committedContentFingerprint;
                    }
                    if (!committedCovered)
                        ++count;

                    const bool distinctPublished = !statusEntry.publishedBuildFingerprint.IsEmpty() &&
                                                   !statusEntry.publishedContentFingerprint.IsEmpty() &&
                                                   (statusEntry.publishedBuildFingerprint != statusEntry.committedBuildFingerprint ||
                                                    statusEntry.publishedContentFingerprint != statusEntry.committedContentFingerprint);
                    if (distinctPublished)
                        ++count;
                }
                if (count > maximumRecords || count > maximumBytes / sizeof(assets::ArtifactSetKey))
                    return false;
                keys.Resize(static_cast<u32>(count));
                if (keys.Size() != count)
                {
                    keys.Clear();
                    return false;
                }
                u32 write = 0u;
                for (const assets::DependencyRecord& record : committedDelivery)
                {
                    if (record.buildFingerprint.IsEmpty() || record.contentFingerprint.IsEmpty())
                    {
                        keys.Clear();
                        return false;
                    }
                    keys[write++] = {record.buildFingerprint, record.contentFingerprint};
                }
                for (const ProjectAssetOutputDeliveryStatus& statusEntry : outputStatuses)
                {
                    if (!statusEntry.publicationRetryRequired)
                        continue;

                    bool committedCovered = false;
                    u32 deliveryIndex = 0u;
                    if (committedDeliveryIndex.Find(KeyOf(statusEntry.resource), deliveryIndex) && deliveryIndex < committedDelivery.Size())
                    {
                        const assets::DependencyRecord& delivery = committedDelivery[deliveryIndex];
                        committedCovered = delivery.output == statusEntry.resource &&
                                           delivery.buildFingerprint == statusEntry.committedBuildFingerprint &&
                                           delivery.contentFingerprint == statusEntry.committedContentFingerprint;
                    }
                    if (!committedCovered)
                        keys[write++] = {statusEntry.committedBuildFingerprint, statusEntry.committedContentFingerprint};

                    if (!statusEntry.publishedBuildFingerprint.IsEmpty() && !statusEntry.publishedContentFingerprint.IsEmpty() &&
                        (statusEntry.publishedBuildFingerprint != statusEntry.committedBuildFingerprint ||
                         statusEntry.publishedContentFingerprint != statusEntry.committedContentFingerprint))
                        keys[write++] = {statusEntry.publishedBuildFingerprint, statusEntry.publishedContentFingerprint};
                }
                return true;
            }

            void RefreshDeliveryStatus(ProjectAssetAutomaticRecookStatus& outputStatus) const noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                outputStatus.pendingDeliveryOutputs = committedDelivery.Size();
                outputStatus.pendingDeliveryEvidenceBytes = committedDeliveryBytes;
            }

            [[nodiscard]] bool HasPublishableDelivery() const noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                if (explicitCohort.IsActive())
                {
                    if (explicitCohort.publicationBlocked)
                    {
                        for (const assets::DependencyRecord& record : committedDelivery)
                        {
                            if (explicitCohort.Contains(record.output))
                                continue;
                            u32 statusIndex = 0u;
                            if (outputStatusIndex.Find(KeyOf(record.output), statusIndex) && statusIndex < outputStatuses.Size() &&
                                !outputStatuses[statusIndex].publicationRetryRequired)
                                return true;
                        }
                        return false;
                    }
                    if (!explicitCohort.deliveryComplete)
                        return false;
                    for (const resources::ResourceReference output : explicitCohort.outputs)
                    {
                        u32 deliveryIndex = 0u;
                        u32 statusIndex = 0u;
                        if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) || deliveryIndex >= committedDelivery.Size() ||
                            committedDelivery[deliveryIndex].output != output ||
                            !outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size())
                            return false;
                        const ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                        if (statusEntry.resource != output || statusEntry.authoredGeneration != explicitCohort.acceptedGeneration ||
                            statusEntry.publicationRetryRequired)
                            return false;
                    }
                    return true;
                }
                for (const assets::DependencyRecord& record : committedDelivery)
                {
                    u32 statusIndex = 0;
                    if (!outputStatusIndex.Find(KeyOf(record.output), statusIndex) || statusIndex >= outputStatuses.Size())
                        continue;
                    if (!outputStatuses[statusIndex].publicationRetryRequired)
                        return true;
                }
                return false;
            }

            void MarkPublishableDeliveriesFailed(
                const ProjectAssetDeliveryReason reason = ProjectAssetDeliveryReason::PublicationFailed,
                const bool cohortAware = true) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                if (cohortAware && explicitCohort.IsActive())
                {
                    explicitCohort.publicationBlocked = true;
                    for (const resources::ResourceReference output : explicitCohort.outputs)
                    {
                        u32 statusIndex = 0u;
                        if (!outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size())
                            continue;
                        ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                        if (statusEntry.resource != output ||
                            statusEntry.authoredGeneration != explicitCohort.acceptedGeneration)
                            continue;
                        statusEntry.publicationRetryRequired = true;
                        statusEntry.publicationReason = reason;
                        statusEntry.publicationState = statusEntry.publishedPhysicalPath.Empty()
                                                           ? ProjectAssetDeliveryStageState::Failed
                                                           : ProjectAssetDeliveryStageState::Stale;
                    }
                    return;
                }
                for (const assets::DependencyRecord& record : committedDelivery)
                {
                    if (explicitCohort.IsActive() && explicitCohort.Contains(record.output))
                        continue;
                    u32 statusIndex = 0;
                    if (!outputStatusIndex.Find(KeyOf(record.output), statusIndex) || statusIndex >= outputStatuses.Size())
                        continue;
                    ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                    if (statusEntry.resource != record.output || statusEntry.publicationRetryRequired)
                        continue;
                    statusEntry.publicationRetryRequired = true;
                    statusEntry.publicationReason = reason;
                    statusEntry.publicationState = statusEntry.publishedPhysicalPath.Empty()
                                                       ? ProjectAssetDeliveryStageState::Failed
                                                       : ProjectAssetDeliveryStageState::Stale;
                }
            }

            [[nodiscard]] bool RetryPublication(const resources::ResourceReference output) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                if (explicitCohort.IsActive())
                {
                    if (!explicitCohort.Contains(output) || !explicitCohort.deliveryComplete)
                        return false;
                    bool anyRetry = false;
                    for (const resources::ResourceReference cohortOutput : explicitCohort.outputs)
                    {
                        u32 deliveryIndex = 0u;
                        u32 statusIndex = 0u;
                        if (!committedDeliveryIndex.Find(KeyOf(cohortOutput), deliveryIndex) ||
                            deliveryIndex >= committedDelivery.Size() || committedDelivery[deliveryIndex].output != cohortOutput ||
                            !outputStatusIndex.Find(KeyOf(cohortOutput), statusIndex) || statusIndex >= outputStatuses.Size())
                            return false;
                        ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                        if (statusEntry.resource != cohortOutput ||
                            statusEntry.authoredGeneration != explicitCohort.acceptedGeneration)
                            return false;
                        anyRetry = anyRetry || statusEntry.publicationRetryRequired;
                    }
                    if (!anyRetry)
                        return false;
                    for (const resources::ResourceReference cohortOutput : explicitCohort.outputs)
                    {
                        u32 statusIndex = 0u;
                        static_cast<void>(outputStatusIndex.Find(KeyOf(cohortOutput), statusIndex));
                        ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                        statusEntry.publicationRetryRequired = false;
                        statusEntry.publicationReason = statusEntry.publishedPhysicalPath.Empty()
                                                            ? ProjectAssetDeliveryReason::AwaitingPublication
                                                            : ProjectAssetDeliveryReason::BuildAdvancedPastPublication;
                        statusEntry.publicationState = statusEntry.publishedPhysicalPath.Empty()
                                                           ? ProjectAssetDeliveryStageState::Pending
                                                           : ProjectAssetDeliveryStageState::Stale;
                    }
                    explicitCohort.publicationBlocked = false;
                    return true;
                }
                u32 deliveryIndex = 0;
                u32 statusIndex = 0;
                if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) || deliveryIndex >= committedDelivery.Size() ||
                    !outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size())
                    return false;
                ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                if (!statusEntry.publicationRetryRequired)
                    return false;
                statusEntry.publicationRetryRequired = false;
                statusEntry.publicationReason = statusEntry.publishedPhysicalPath.Empty()
                                                    ? ProjectAssetDeliveryReason::AwaitingPublication
                                                    : ProjectAssetDeliveryReason::BuildAdvancedPastPublication;
                statusEntry.publicationState = statusEntry.publishedPhysicalPath.Empty()
                                                   ? ProjectAssetDeliveryStageState::Pending
                                                   : ProjectAssetDeliveryStageState::Stale;
                return true;
            }

            [[nodiscard]] static bool SameBuildEvidence(const assets::DependencyRecord& record,
                                                        const assets::BuildRequest& request,
                                                        const assets::BuildPlan& plan) noexcept
            {
                if (record.output != request.output || record.source != request.source.identity || record.target != request.target ||
                    record.sourceInputFingerprint != assets::ComputeSourceInputFingerprint(request) ||
                    record.compiler != plan.Compiler() || record.compilerVersion != plan.CompilerVersion() ||
                    plan.GetSourceType() != request.source.identity.ExpectedType() ||
                    plan.GetOutputType() != request.output.ExpectedType())
                    return false;
                const containers::ArraySpan<const assets::BuildDependency> dependencies = plan.GetDependencies();
                if (record.dependencies.Size() != dependencies.Count())
                    return false;
                for (u32 index = 0; index < dependencies.Count(); ++index)
                {
                    const assets::BuildDependency& left = record.dependencies[index];
                    const assets::BuildDependency& right = dependencies[index];
                    if (left.identity != right.identity || left.content != right.content || left.role != right.role ||
                        left.requirement != right.requirement)
                        return false;
                }
                return true;
            }

            enum class DeliveryValidationResult : u8
            {
                Current,
                Stale,
                RepairRequired,
                Unavailable
            };

            [[nodiscard]] DeliveryValidationResult ValidateDeliveryRecordCurrent(
                const assets::DependencyRecord& record, ProjectLoosePublisher& publisher,
                ProjectLoosePublicationFailure* const publicationFailure) const noexcept
            {
                if (validationBuilds == nullptr || validationSources == nullptr || validationIndex == nullptr ||
                    publicationFailure == nullptr)
                    return DeliveryValidationResult::Unavailable;
                *publicationFailure = {};
                const ProjectLoosePublicationResult exact = publisher.ValidateExact(record, publicationFailure);
                if (exact != ProjectLoosePublicationResult::Success)
                {
                    if (IsCacheRepairFailure(*publicationFailure))
                        return DeliveryValidationResult::RepairRequired;
                    if (exact == ProjectLoosePublicationResult::RecordNotFound || exact == ProjectLoosePublicationResult::TargetMismatch)
                        return DeliveryValidationResult::Stale;
                    return DeliveryValidationResult::Unavailable;
                }
                assets::BuildSystem* const buildSystem = validationBuilds->GetBuildSystem();
                if (buildSystem == nullptr)
                    return DeliveryValidationResult::Unavailable;
                assets::OwnedBuildRequest owned;
                if (validationBuilds->ResolveForValidation(*validationSources, record.output, owned) !=
                    assets::ProjectAssetBuildResult::Success)
                    return DeliveryValidationResult::Stale;
                const assets::BuildRequest request = owned.View();
                assets::BuildPlan plan;
                const containers::ArraySpan<const assets::DependencyRecord> noStagedRecords;
                if (buildSystem->Prepare(request, plan) != assets::Result::Success ||
                    validationIndex->MaterializeGeneratedDependencyContent(plan, noStagedRecords) != assets::IndexResult::Success)
                    return DeliveryValidationResult::Unavailable;
                return SameBuildEvidence(record, request, plan) ? DeliveryValidationResult::Current
                                                                : DeliveryValidationResult::Stale;
            }

            [[nodiscard]] static assets::RecookPreCommitValidationResult ValidatePreCommit(
                const containers::ArraySpan<const assets::DependencyRecord> records, void* const userData) noexcept
            {
                auto* const state = static_cast<AutomaticRecookState*>(userData);
                if (state == nullptr || state->validationBuilds == nullptr || state->validationSources == nullptr ||
                    state->validationIndex == nullptr || state->validationChanges == nullptr)
                    return assets::RecookPreCommitValidationResult::Failed;

                const SourceChangeSnapshot before = state->validationChanges->GetChangeSnapshot();
                if (before.overflow)
                    return assets::RecookPreCommitValidationResult::Reject;

                assets::BuildSystem* const buildSystem = state->validationBuilds->GetBuildSystem();
                if (buildSystem == nullptr)
                    return assets::RecookPreCommitValidationResult::Failed;

                // A no-work batch has no staged transaction records. Re-evaluate
                // the latest owned current requests against committed VADI so an
                // up-to-date decision still receives a P4 freshness fence before
                // the pending evidence is retired.
                if (records.Empty())
                {
                    for (const assets::OwnedBuildRequest& expected : state->active.currentRequests)
                    {
                        assets::OwnedBuildRequest owned;
                        if (state->validationBuilds->ResolveForValidation(*state->validationSources, expected.outputReference, owned) !=
                            assets::ProjectAssetBuildResult::Success)
                            return assets::RecookPreCommitValidationResult::Reject;
                        const assets::BuildRequest request = owned.View();
                        assets::BuildPlan plan;
                        if (buildSystem->Prepare(request, plan) != assets::Result::Success ||
                            state->validationIndex->MaterializeGeneratedDependencyContent(plan, records) != assets::IndexResult::Success ||
                            state->validationIndex->Evaluate(request, plan) != assets::DirtyReason::UpToDate)
                            return assets::RecookPreCommitValidationResult::Reject;
                    }
                }
                for (const assets::DependencyRecord& record : records)
                {
                    assets::OwnedBuildRequest owned;
                    if (state->validationBuilds->ResolveForValidation(*state->validationSources, record.output, owned) !=
                        assets::ProjectAssetBuildResult::Success)
                        return assets::RecookPreCommitValidationResult::Reject;
                    const assets::BuildRequest request = owned.View();
                    assets::BuildPlan plan;
                    if (buildSystem->Prepare(request, plan) != assets::Result::Success ||
                        state->validationIndex->MaterializeGeneratedDependencyContent(plan, records) != assets::IndexResult::Success ||
                        !SameBuildEvidence(record, request, plan))
                        return assets::RecookPreCommitValidationResult::Reject;
                }

                if (state->explicitCohort.IsActive())
                {
                    containers::DynamicArray<u64> cohortRecordBytes(memory::pools::Assets::GetInstance());
                    cohortRecordBytes.Resize(state->explicitCohort.outputs.Size());
                    if (cohortRecordBytes.Size() != state->explicitCohort.outputs.Size())
                        return assets::RecookPreCommitValidationResult::Failed;

                    for (u32 cohortIndex = 0u; cohortIndex < state->explicitCohort.outputs.Size(); ++cohortIndex)
                    {
                        const resources::ResourceReference output = state->explicitCohort.outputs[cohortIndex];
                        assets::OwnedBuildRequest owned;
                        if (state->validationBuilds->ResolveForValidation(*state->validationSources, output, owned) !=
                            assets::ProjectAssetBuildResult::Success)
                            return assets::RecookPreCommitValidationResult::Reject;
                        const assets::BuildRequest request = owned.View();
                        assets::BuildPlan plan;
                        if (buildSystem->Prepare(request, plan) != assets::Result::Success ||
                            state->validationIndex->MaterializeGeneratedDependencyContent(plan, records) != assets::IndexResult::Success)
                            return assets::RecookPreCommitValidationResult::Reject;

                        const assets::DependencyRecord* evidence = nullptr;
                        for (const assets::DependencyRecord& staged : records)
                            if (staged.output == output)
                            {
                                evidence = &staged;
                                break;
                            }
                        assets::DependencyRecord committed;
                        if (evidence == nullptr)
                        {
                            if (state->validationIndex->Find(output, committed) != assets::IndexResult::Success)
                                return assets::RecookPreCommitValidationResult::Reject;
                            evidence = &committed;
                        }
                        if (evidence->output != output || evidence->buildFingerprint.IsEmpty() ||
                            evidence->contentFingerprint.IsEmpty() || !SameBuildEvidence(*evidence, request, plan))
                            return assets::RecookPreCommitValidationResult::Reject;
                        cohortRecordBytes[cohortIndex] = DeliveryRecordBytes(*evidence);
                    }

                    concurrency::ScopedLock<concurrency::Mutex> lock(state->deliveryMutex);
                    u32 addedRecords = 0u;
                    u32 addedStatuses = 0u;
                    u64 prospectiveBytes = state->committedDeliveryBytes;
                    for (u32 recordIndex = 0u; recordIndex < records.Count(); ++recordIndex)
                    {
                        const assets::DependencyRecord& record = records[recordIndex];
                        if (!record.output.IsValid() || !record.output.IsTyped() || record.buildFingerprint.IsEmpty() ||
                            record.contentFingerprint.IsEmpty())
                            return assets::RecookPreCommitValidationResult::Failed;
                        for (u32 previous = 0u; previous < recordIndex; ++previous)
                            if (records[previous].output.GetPath().Id() == record.output.GetPath().Id())
                                return assets::RecookPreCommitValidationResult::Failed;

                        u32 existing = 0u;
                        if (state->committedDeliveryIndex.Find(KeyOf(record.output), existing))
                        {
                            if (existing >= state->committedDelivery.Size() || state->committedDelivery[existing].output != record.output)
                                return assets::RecookPreCommitValidationResult::Failed;
                            const u64 previousBytes = DeliveryRecordBytes(state->committedDelivery[existing]);
                            if (prospectiveBytes < previousBytes)
                                return assets::RecookPreCommitValidationResult::Failed;
                            prospectiveBytes -= previousBytes;
                        }
                        else
                        {
                            ++addedRecords;
                        }
                        const u64 recordBytes = DeliveryRecordBytes(record);
                        if (prospectiveBytes > state->config.maximumCapturedEvidenceBytes ||
                            recordBytes > state->config.maximumCapturedEvidenceBytes - prospectiveBytes)
                        {
                            state->workerDeliveryCapacityExceeded = true;
                            return assets::RecookPreCommitValidationResult::Failed;
                        }
                        prospectiveBytes += recordBytes;

                        if (state->outputStatusIndex.Find(KeyOf(record.output), existing))
                        {
                            if (existing >= state->outputStatuses.Size() || state->outputStatuses[existing].resource != record.output)
                                return assets::RecookPreCommitValidationResult::Failed;
                        }
                        else
                        {
                            ++addedStatuses;
                        }
                    }
                    for (u32 cohortIndex = 0u; cohortIndex < state->explicitCohort.outputs.Size(); ++cohortIndex)
                    {
                        const resources::ResourceReference output = state->explicitCohort.outputs[cohortIndex];
                        bool stagedOutput = false;
                        for (const assets::DependencyRecord& record : records)
                            if (record.output.GetPath().Id() == output.GetPath().Id())
                            {
                                if (record.output != output)
                                    return assets::RecookPreCommitValidationResult::Failed;
                                stagedOutput = true;
                                break;
                            }
                        if (stagedOutput)
                            continue;
                        u32 existing = 0u;
                        if (state->committedDeliveryIndex.Find(KeyOf(output), existing))
                        {
                            if (existing >= state->committedDelivery.Size() || state->committedDelivery[existing].output != output)
                                return assets::RecookPreCommitValidationResult::Failed;
                            const u64 previousBytes = DeliveryRecordBytes(state->committedDelivery[existing]);
                            if (prospectiveBytes < previousBytes)
                                return assets::RecookPreCommitValidationResult::Failed;
                            prospectiveBytes -= previousBytes;
                        }
                        else
                        {
                            ++addedRecords;
                        }
                        const u64 recordBytes = cohortRecordBytes[cohortIndex];
                        if (prospectiveBytes > state->config.maximumCapturedEvidenceBytes ||
                            recordBytes > state->config.maximumCapturedEvidenceBytes - prospectiveBytes)
                        {
                            state->workerDeliveryCapacityExceeded = true;
                            return assets::RecookPreCommitValidationResult::Failed;
                        }
                        prospectiveBytes += recordBytes;

                        if (state->outputStatusIndex.Find(KeyOf(output), existing))
                        {
                            if (existing >= state->outputStatuses.Size() || state->outputStatuses[existing].resource != output)
                                return assets::RecookPreCommitValidationResult::Failed;
                        }
                        else
                        {
                            ++addedStatuses;
                        }
                    }
                    if (state->committedDelivery.Size() + addedRecords > state->config.maximumPendingOutputs ||
                        state->committedDelivery.Size() + addedRecords > state->committedDelivery.Capacity() ||
                        state->committedDeliveryIndex.Size() + addedRecords > state->committedDeliveryIndex.Capacity() ||
                        state->outputStatuses.Size() + addedStatuses > state->maximumOutputStatuses ||
                        state->outputStatuses.Size() + addedStatuses > state->outputStatuses.Capacity() ||
                        state->outputStatusIndex.Size() + addedStatuses > state->outputStatusIndex.Capacity())
                    {
                        state->workerDeliveryCapacityExceeded = true;
                        return assets::RecookPreCommitValidationResult::Failed;
                    }
                }

                // P1 must know before VADI visibility that the exact committed
                // evidence and its per-output status can be retained. A failure
                // here rolls the transaction back through the existing validator
                // contract instead of committing an output whose delivery token
                // the pipeline cannot own.
                if (!state->explicitCohort.IsActive() && !records.Empty() && !state->CanAcceptCommittedDelivery(records))
                {
                    state->workerDeliveryCapacityExceeded = true;
                    return assets::RecookPreCommitValidationResult::Failed;
                }

                const SourceChangeSnapshot after = state->validationChanges->GetChangeSnapshot();
                return !after.overflow && after.generation == before.generation
                           ? assets::RecookPreCommitValidationResult::Accept
                           : assets::RecookPreCommitValidationResult::Reject;
            }

            [[nodiscard]] static assets::RecookPreCommitValidationResult ValidateRetirements(
                const containers::ArraySpan<const assets::DependencyRecord> records,
                const containers::ArraySpan<const resources::ResourceReference> retirements, void* const userData) noexcept
            {
                const assets::RecookPreCommitValidationResult fresh = ValidatePreCommit(records, userData);
                if (fresh != assets::RecookPreCommitValidationResult::Accept)
                    return fresh;
                auto* const state = static_cast<AutomaticRecookState*>(userData);
                if (state == nullptr)
                    return assets::RecookPreCommitValidationResult::Failed;

                containers::DynamicArray<resources::ResourceReference> expectedRetirements(memory::pools::Assets::GetInstance());
                containers::DynamicArray<resources::ResourceReference> successors(memory::pools::Assets::GetInstance());
                if (!state->BuildExplicitCohortRetirementPlan(expectedRetirements, successors) || successors.Empty() ||
                    expectedRetirements.Size() != retirements.Count())
                    return assets::RecookPreCommitValidationResult::Reject;
                for (const resources::ResourceReference expected : expectedRetirements)
                {
                    bool found = false;
                    for (const resources::ResourceReference retired : retirements)
                        if (retired == expected)
                        {
                            found = true;
                            break;
                        }
                    if (!found)
                        return assets::RecookPreCommitValidationResult::Reject;
                }
                return assets::RecookPreCommitValidationResult::Accept;
            }

            [[nodiscard]] bool Initialize(assets::ProjectAssetBuildSystem& builds, const assets::SourceDatabase& sources,
                                          const SourceChangeAccumulator& changes) noexcept
            {
                assets::BuildSystem* const buildSystem = builds.GetBuildSystem();
                assets::BuildGraph* const graph = builds.GetBuildGraph();
                assets::DependencyIndex* const index = builds.GetDependencyIndex();
                if (buildSystem == nullptr || graph == nullptr || index == nullptr)
                    return false;
                assets::RecookConfig recookConfig;
                recookConfig.maximumChangesPerBatch = config.maximumPendingInputs;
                recookConfig.maximumAffectedOutputsPerBatch = config.maximumPendingOutputs;
                recookConfig.maximumRootRequestsPerBatch = config.maximumPendingOutputs;
                recookConfig.maximumValidationRecordsPerBatch = config.maximumPendingOutputs;
                recookConfig.maximumValidationEvidenceBytes = config.maximumCapturedEvidenceBytes;
                recookConfig.isConfirmedHistoricalOutput = &IsConfirmedHistoricalOutput;
                recookConfig.historicalOutputUserData = this;
                validationBuilds = &builds;
                validationSources = &sources;
                validationIndex = index;
                validationChanges = &changes;
                committedDelivery.Reserve(config.maximumPendingOutputs);
                committedDeliveryIndex.Reserve(config.maximumPendingOutputs);
                outputStatuses.Reserve(maximumOutputStatuses);
                outputStatusIndex.Reserve(maximumOutputStatuses);
                workerRetiredLooseCaptures.Reserve(config.maximumPendingOutputs);
                if (committedDelivery.Capacity() < config.maximumPendingOutputs ||
                    committedDeliveryIndex.Capacity() < config.maximumPendingOutputs ||
                    outputStatuses.Capacity() < maximumOutputStatuses ||
                    outputStatusIndex.Capacity() < maximumOutputStatuses ||
                    workerRetiredLooseCaptures.Capacity() < config.maximumPendingOutputs)
                    return false;
                return recooker.Initialize(*buildSystem, *graph, *index, &Resolve, this, recookConfig);
            }

            [[nodiscard]] bool IsEnabled() const noexcept
            {
                return status.state != ProjectAssetAutomaticRecookState::Disabled;
            }

            [[nodiscard]] bool IsPolicyEnabled() const noexcept { return policyEnabled; }

            [[nodiscard]] bool HasExplicitActivity() const noexcept { return explicitActivity; }

            [[nodiscard]] bool HasExplicitCohort() const noexcept { return explicitCohort.IsActive(); }

            [[nodiscard]] bool MatchesExplicitCohort(const ProjectAssetCohortIntent& intent) const noexcept
            {
                if (!explicitCohort.IsActive() || !intent.IsActive() || !(explicitCohort.asset == intent.asset) ||
                    explicitCohort.outputs.Size() != intent.successorOutputs.Size())
                    return false;
                for (const resources::ResourceReference expected : intent.successorOutputs)
                {
                    bool found = false;
                    for (const resources::ResourceReference output : explicitCohort.outputs)
                        if (output == expected)
                        {
                            found = true;
                            break;
                        }
                    if (!found)
                        return false;
                }
                return true;
            }

            [[nodiscard]] bool OwnsAdmission() const noexcept { return admissionOwned; }

            [[nodiscard]] bool BuildExplicitCohortRetirementPlan(
                containers::DynamicArray<resources::ResourceReference>& retirements,
                containers::DynamicArray<resources::ResourceReference>& successors) const noexcept
            {
                retirements.Clear();
                successors.Clear();
                if (!explicitCohort.IsActive())
                    return true;

                const ProjectAssetAutomaticPendingAsset* topology = nullptr;
                for (const ProjectAssetAutomaticPendingAsset& asset : active.assets)
                    if (asset.asset == explicitCohort.asset)
                    {
                        if (topology != nullptr)
                            return false;
                        topology = &asset;
                    }

                const auto contains = [](const containers::DynamicArray<resources::ResourceReference>& values,
                                         const resources::ResourceReference value) noexcept
                {
                    for (const resources::ResourceReference candidate : values)
                        if (candidate == value)
                            return true;
                    return false;
                };
                if (topology != nullptr)
                {
                    if (topology->current.outputs.Size() != explicitCohort.outputs.Size())
                        return false;
                    for (const resources::ResourceReference output : topology->current.outputs)
                        if (!contains(explicitCohort.outputs, output))
                            return false;
                    for (const resources::ResourceReference output : explicitCohort.outputs)
                        if (!contains(topology->current.outputs, output))
                            return false;
                }

                successors.Reserve(explicitCohort.outputs.Size());
                retirements.Reserve(topology != nullptr ? topology->previous.outputs.Size() : 0u);
                if (successors.Capacity() < explicitCohort.outputs.Size() ||
                    (topology != nullptr && retirements.Capacity() < topology->previous.outputs.Size()))
                    return false;
                for (const resources::ResourceReference output : explicitCohort.outputs)
                {
                    const u32 previous = successors.Size();
                    successors.PushBack(output);
                    if (successors.Size() != previous + 1u)
                        return false;
                }
                const containers::ArraySpan<const resources::ResourceReference> predecessors =
                    !explicitCohort.predecessorOutputs.Empty()
                        ? containers::ArraySpan<const resources::ResourceReference>{explicitCohort.predecessorOutputs.TypedData(),
                                                                                   explicitCohort.predecessorOutputs.Size()}
                        : topology != nullptr
                              ? containers::ArraySpan<const resources::ResourceReference>{topology->previous.outputs.TypedData(),
                                                                                         topology->previous.outputs.Size()}
                              : containers::ArraySpan<const resources::ResourceReference>{};
                retirements.Reserve(predecessors.Count());
                if (retirements.Capacity() < predecessors.Count())
                    return false;
                for (const resources::ResourceReference output : predecessors)
                    if (!contains(explicitCohort.outputs, output))
                    {
                        const u32 previous = retirements.Size();
                        retirements.PushBack(output);
                        if (retirements.Size() != previous + 1u)
                            return false;
                    }
                return !successors.Empty();
            }

            [[nodiscard]] bool ProtectsTopologyPredecessor(const resources::ResourceReference output) const noexcept
            {
                if (explicitCohort.IsActive())
                    for (const resources::ResourceReference predecessor : explicitCohort.predecessorOutputs)
                        if (predecessor == output)
                            return true;
                const auto protectedBy = [output](const ProjectAssetAutomaticPendingState& state) noexcept
                {
                    for (const ProjectAssetAutomaticPendingAsset& asset : state.assets)
                    {
                        bool previous = false;
                        for (const resources::ResourceReference candidate : asset.previous.outputs)
                            if (candidate == output)
                            {
                                previous = true;
                                break;
                            }
                        if (!previous)
                            continue;
                        bool current = false;
                        for (const resources::ResourceReference candidate : asset.current.outputs)
                            if (candidate == output)
                            {
                                current = true;
                                break;
                            }
                        if (!current)
                            return true;
                    }
                    return false;
                };
                return protectedBy(pending) || protectedBy(active);
            }

            [[nodiscard]] bool IsRecoveryAdmission() const noexcept { return admissionOwned && workerWasRecovery; }

            void RefreshPendingStatus() noexcept
            {
                status.pendingAssets = pending.assets.Size();
                status.pendingInputs = pending.inputs.Size();
                status.pendingOutputs = pending.outputs.Size();
                status.pendingRepairOutputs = 0u;
                status.pendingReimportOutputs = 0u;
                for (const ProjectAssetAutomaticForcedOutput& forced : pending.forcedOutputs)
                {
                    status.pendingRepairOutputs += HasProjectAssetAutomaticForceReason(
                                                       forced.reasons, ProjectAssetAutomaticForceReason::CacheRepair)
                                                       ? 1u
                                                       : 0u;
                    status.pendingReimportOutputs += HasProjectAssetAutomaticForceReason(
                                                         forced.reasons, ProjectAssetAutomaticForceReason::ExplicitReimport)
                                                         ? 1u
                                                         : 0u;
                }
                status.pendingDiagnostics = pending.diagnostics;
                status.capturedEvidenceBytes = pending.capturedEvidenceBytes;
            }

            void RefreshActiveStatus() noexcept
            {
                status.activeRoots = workerStats.rootRequests;
                status.completedRoots = workerStats.succeededRoots + workerStats.failedRoots;
                status.buildOperations = workerStats.buildOperations;
                status.completedBuildOperations = workerStats.completedBuildOperations;
            }

            void RefreshRetryStatus() noexcept
            {
                status.retryEntries = status.state == ProjectAssetAutomaticRecookState::Failed ||
                                              status.state == ProjectAssetAutomaticRecookState::RecoveryRequired ||
                                              status.retryRequested || recoveryRequested
                                          ? active.assets.Size()
                                          : 0u;
            }

            void SetEnabled(const bool enabled, const u64 acceptedGeneration = 0u) noexcept
            {
                policyEnabled = enabled;
                if (enabled)
                {
                    if (status.state == ProjectAssetAutomaticRecookState::Disabled)
                    {
                        staleRejected = false;
                        bool retainRepair = false;
                        for (const ProjectAssetAutomaticForcedOutput& forced : pending.forcedOutputs)
                            retainRepair = retainRepair || HasProjectAssetAutomaticForceReason(
                                                               forced.reasons, ProjectAssetAutomaticForceReason::CacheRepair);
                        if (!retainRepair)
                            pending.Reset();
                        pending.lastAcceptedGeneration = acceptedGeneration;
                        recoveryRequested = true;
                        status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::StartupRecovery;
                        if (retainRepair)
                            status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired;
                        // Preserve the P1/P2 enable surface: recovery is queued
                        // internally and starts only when the owner explicitly
                        // ticks automatic coordination.
                        status.state = ProjectAssetAutomaticRecookState::Idle;
                        RefreshPendingStatus();
                    }
                    return;
                }
                if (explicitActivity)
                    return;
                status = {};
                staleRejected = false;
                pending.Reset();
                active.Reset();
                DropExplicitCohortDelivery();
                recoveryRequested = false;
                startupRecoveryDeferred = false;
                admissionOwned = false;
                pollDispatchFailed = false;
                pollDispatchRetryRequested = false;
                recoverAfterPollDispatchFailure = false;
            }

            void AdoptPending(ProjectAssetAutomaticPendingState& staged) noexcept
            {
                if (explicitCohort.IsActive())
                {
                    for (const ProjectAssetAutomaticPendingAsset& changed : staged.assets)
                        if (changed.asset == explicitCohort.asset && changed.latestAcceptedGeneration != 0u &&
                            changed.latestAcceptedGeneration != explicitCohort.acceptedGeneration)
                        {
                            DropExplicitCohortDelivery();
                            explicitActivity = false;
                            break;
                        }
                }
                const bool terminalBeforeAdopt = status.state == ProjectAssetAutomaticRecookState::Failed ||
                                                 status.state == ProjectAssetAutomaticRecookState::RecoveryRequired;
                pending.Swap(staged);
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::AcceptedChange;
                if (terminalBeforeAdopt)
                    recoveryRequested = true;
                RefreshPendingStatus();
                if (admissionOwned)
                    return;
                if (status.recoveryRequired)
                    status.state = ProjectAssetAutomaticRecookState::RecoveryRequired;
                else if (!pending.assets.Empty() || !pending.inputs.Empty() || !pending.outputs.Empty())
                    status.state = ProjectAssetAutomaticRecookState::Pending;
                else
                    status.state = ProjectAssetAutomaticRecookState::Idle;
            }

            [[nodiscard]] bool HasRepairRequirement(const resources::ResourceReference output) const noexcept
            {
                for (const ProjectAssetAutomaticForcedOutput& forced : pending.forcedOutputs)
                    if (forced.output == output && HasProjectAssetAutomaticForceReason(
                                                      forced.reasons, ProjectAssetAutomaticForceReason::CacheRepair))
                        return true;
                for (const ProjectAssetAutomaticForcedOutput& forced : active.forcedOutputs)
                    if (forced.output == output && HasProjectAssetAutomaticForceReason(
                                                      forced.reasons, ProjectAssetAutomaticForceReason::CacheRepair))
                        return true;
                return false;
            }

            [[nodiscard]] bool QueueRepair(const resources::ResourceReference output, const u64 acceptedGeneration) noexcept
            {
                if (!output.IsValid() || !output.IsTyped() || validationBuilds == nullptr || validationSources == nullptr ||
                    validationIndex == nullptr)
                    return false;
                if (explicitCohort.IsActive() && !explicitCohort.Contains(output) && !explicitCohort.publicationBlocked)
                    return false;
                // Repeated reconciliation/publication probes for the same broken
                // exact record must not turn a sticky failed batch back into work.
                if (HasRepairRequirement(output))
                    return true;

                ProjectAssetAutomaticPendingState staged;
                const ProjectAssetAutomaticPendingPrepareResult prepared = PrepareProjectAssetAutomaticRepair(
                    pending, output, acceptedGeneration, *validationSources, *validationBuilds, *validationIndex, config, staged);
                if (prepared != ProjectAssetAutomaticPendingPrepareResult::Success)
                    return false;

                const bool terminalBeforeAdopt = status.state == ProjectAssetAutomaticRecookState::Failed ||
                                                 status.state == ProjectAssetAutomaticRecookState::RecoveryRequired;
                pending.Swap(staged);
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired;
                if (terminalBeforeAdopt)
                    recoveryRequested = true;
                RefreshPendingStatus();
                if (status.state == ProjectAssetAutomaticRecookState::Disabled || admissionOwned)
                    return true;
                status.state = status.recoveryRequired ? ProjectAssetAutomaticRecookState::RecoveryRequired
                                                       : ProjectAssetAutomaticRecookState::Pending;
                return true;
            }

            [[nodiscard]] bool QueueExplicitReimport(const ProjectAssetState& asset, const u64 acceptedGeneration) noexcept
            {
                if (explicitCohort.IsActive() || validationBuilds == nullptr || validationSources == nullptr || validationIndex == nullptr)
                    return false;
                ProjectAssetAutomaticPendingState emptyBase;
                const bool startingExplicitOnly = !policyEnabled && !explicitActivity;
                const ProjectAssetAutomaticPendingState& base = startingExplicitOnly ? emptyBase : pending;
                ProjectAssetAutomaticPendingState staged;
                if (PrepareProjectAssetAutomaticReimport(base, asset, acceptedGeneration, *validationSources,
                                                         *validationBuilds, *validationIndex, config, staged) !=
                    ProjectAssetAutomaticPendingPrepareResult::Success)
                    return false;

                if (startingExplicitOnly)
                {
                    // Disabled cache-repair/recovery bookkeeping is reconstructible
                    // from durable VADI/VDDC truth. Do not let it implicitly turn a
                    // manual reimport into unrelated automatic project work.
                    status = {};
                    staleRejected = false;
                    recoveryRequested = false;
                }
                const bool terminalBeforeAdopt = status.state == ProjectAssetAutomaticRecookState::Failed ||
                                                 status.state == ProjectAssetAutomaticRecookState::RecoveryRequired;
                pending.Swap(staged);
                explicitActivity = true;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::ExplicitReimport;
                if (terminalBeforeAdopt)
                    recoveryRequested = true;
                RefreshPendingStatus();
                if (!admissionOwned)
                    status.state = status.recoveryRequired ? ProjectAssetAutomaticRecookState::RecoveryRequired
                                                           : ProjectAssetAutomaticRecookState::Pending;
                return true;
            }

            [[nodiscard]] bool QueueExplicitCohort(
                const ProjectAssetState& asset, const u64 acceptedGeneration, const u64 coordinationGeneration,
                const containers::ArraySpan<const resources::ResourceReference> predecessorOutputs = {},
                const bool supersedeStartupRecovery = false,
                const containers::ArraySpan<const assets::OwnedBuildRequest> alreadyResolved = {}) noexcept
            {
                const bool replaceStartupRecovery = supersedeStartupRecovery && recoveryRequested && policyEnabled &&
                                                    !explicitActivity && !workRunning && !admissionOwned &&
                                                    active.assets.Empty() && active.inputs.Empty() && active.outputs.Empty() &&
                                                    status.state != ProjectAssetAutomaticRecookState::Failed &&
                                                    status.state != ProjectAssetAutomaticRecookState::RecoveryRequired;
                // A durable reviewed cohort may be restored while an earlier
                // automatic batch has retained terminal/retry recovery evidence.
                // It cannot be recovered as *ordinary* automatic work because
                // CanAcquireAutomaticAdmission fences the durable successor
                // until its exact explicit cohort is installed. Admit that
                // cohort without discarding active/pending invalidations.
                const bool retainedRecovery = supersedeStartupRecovery && !explicitActivity &&
                                               !explicitCohort.IsActive() && !workRunning && !admissionOwned &&
                                               !publicationSettlementPending &&
                                               (status.state == ProjectAssetAutomaticRecookState::Failed ||
                                                status.state == ProjectAssetAutomaticRecookState::RecoveryRequired ||
                                                !active.assets.Empty() || !active.inputs.Empty() || !active.outputs.Empty());
                if (validationBuilds == nullptr || validationSources == nullptr || validationIndex == nullptr ||
                    workRunning || admissionOwned || (recoveryRequested && !replaceStartupRecovery && !retainedRecovery) ||
                    publicationSettlementPending ||
                    (explicitActivity && !explicitCohort.IsActive()) ||
                    ((status.state == ProjectAssetAutomaticRecookState::Failed ||
                      status.state == ProjectAssetAutomaticRecookState::RecoveryRequired) && !retainedRecovery) ||
                    ((!active.assets.Empty() || !active.inputs.Empty() || !active.outputs.Empty()) && !retainedRecovery) ||
                    acceptedGeneration == 0u || coordinationGeneration == 0u || asset.outputs.Empty() ||
                    asset.outputs.Count() > config.maximumPendingOutputs || predecessorOutputs.Count() > config.maximumPendingOutputs ||
                    (!predecessorOutputs.Empty() && predecessorOutputs.Data() == nullptr))
                    return false;

                if (explicitCohort.IsActive())
                {
                    if (!(explicitCohort.asset == asset.asset) || explicitCohort.acceptedGeneration == acceptedGeneration)
                        return false;
                    DropExplicitCohortDelivery();
                }

                ProjectAssetAutomaticPendingState emptyBase;
                const ProjectAssetAutomaticPendingState& base = replaceStartupRecovery ? emptyBase : pending;
                ProjectAssetAutomaticPendingState staged;
                if (PrepareProjectAssetAutomaticReimport(base, asset, coordinationGeneration, *validationSources,
                                                         *validationBuilds, *validationIndex, config, staged, alreadyResolved) !=
                    ProjectAssetAutomaticPendingPrepareResult::Success)
                    return false;

                const u64 cohortOutputCount = static_cast<u64>(asset.outputs.Count()) + predecessorOutputs.Count();
                const u64 cohortBytes = cohortOutputCount * sizeof(resources::ResourceReference);
                if (staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes ||
                    cohortBytes > config.maximumCapturedEvidenceBytes - staged.capturedEvidenceBytes)
                    return false;

                ExplicitDeliveryCohort cohort;
                cohort.outputs.Reserve(asset.outputs.Count());
                cohort.predecessorOutputs.Reserve(predecessorOutputs.Count());
                if (cohort.outputs.Capacity() < asset.outputs.Count() ||
                    cohort.predecessorOutputs.Capacity() < predecessorOutputs.Count())
                    return false;
                cohort.asset = asset.asset;
                cohort.acceptedGeneration = acceptedGeneration;
                for (const ProjectAssetOutputState& acceptedOutput : asset.outputs)
                {
                    const resources::ResourceReference output = acceptedOutput.resource;
                    if (!output.IsValid() || !output.IsTyped())
                        return false;
                    for (const resources::ResourceReference existing : cohort.outputs)
                        if (existing.GetPath().Id() == output.GetPath().Id())
                            return false;
                    const u32 previous = cohort.outputs.Size();
                    cohort.outputs.PushBack(output);
                    if (cohort.outputs.Size() != previous + 1u)
                        return false;
                }
                for (const resources::ResourceReference output : predecessorOutputs)
                {
                    if (!output.IsValid() || !output.IsTyped())
                        return false;
                    for (const resources::ResourceReference existing : cohort.predecessorOutputs)
                        if (existing.GetPath().Id() == output.GetPath().Id())
                            return false;
                    const u32 previous = cohort.predecessorOutputs.Size();
                    cohort.predecessorOutputs.PushBack(output);
                    if (cohort.predecessorOutputs.Size() != previous + 1u)
                        return false;
                }

                if (!policyEnabled && !retainedRecovery)
                {
                    status = {};
                    staleRejected = false;
                    recoveryRequested = false;
                }
                else if (replaceStartupRecovery)
                {
                    // The durable cohort is the only safe successor work while
                    // its VPAS marker is active. Defer, rather than discard, the
                    // generic startup recovery that SetEnabled requested; after
                    // coherent cutover it is reconstructed authoritatively.
                    recoveryRequested = false;
                    startupRecoveryDeferred = true;
                    status.recoveryRequired = false;
                }
                else if (retainedRecovery)
                {
                    // Recover retained active invalidations together with the
                    // exact newly-admitted successor; a generic retry cannot
                    // cross the durable cohort fence safely. The recovery
                    // planner merges active and pending as one authoritative
                    // set, and this explicit cohort keeps publication atomic.
                    recoveryRequested = true;
                    status.recoveryRequired = false;
                }
                pending.Swap(staged);
                explicitCohort.Swap(cohort);
                explicitActivity = true;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::ExplicitReimport;
                RefreshPendingStatus();
                status.state = ProjectAssetAutomaticRecookState::Pending;
                return true;
            }

            [[nodiscard]] bool RevalidateRecoveryRepairs(ProjectAssetAutomaticPendingState& recovered,
                                                         ProjectLoosePublisher& publisher,
                                                         const assets::SourceDatabase& sources,
                                                         assets::ProjectAssetBuildSystem& builds,
                                                         assets::DependencyIndex& index,
                                                         const u64 acceptedGeneration) noexcept
            {
                // Recovery is authoritative: reconstruct repair-required state
                // from current VADI/VDDC truth instead of trusting transient
                // markers that may have been lost when QueueRepair could not fit.
                // Missing VADI rows remain ordinary DirtyReason::Missing work.
                for (u32 requestIndex = 0; requestIndex < recovered.currentRequests.Size(); ++requestIndex)
                {
                    const resources::ResourceReference output = recovered.currentRequests[requestIndex].outputReference;
                    assets::DependencyRecord record;
                    const assets::IndexResult found = index.Find(output, record);
                    if (found == assets::IndexResult::NotFound)
                        continue;
                    if (!assets::IsSuccess(found))
                        return false;

                    ProjectLoosePublicationFailure publicationFailure;
                    const ProjectLoosePublicationResult exact = publisher.ValidateExact(record, &publicationFailure);
                    if (exact == ProjectLoosePublicationResult::Success)
                        continue;
                    if (!IsCacheRepairFailure(publicationFailure))
                        return false;

                    ProjectAssetAutomaticPendingState staged;
                    if (PrepareProjectAssetAutomaticRepair(recovered, output, acceptedGeneration, sources, builds, index,
                                                           config, staged) != ProjectAssetAutomaticPendingPrepareResult::Success)
                        return false;
                    recovered.Swap(staged);
                }
                return true;
            }

            void MarkRepairRecoveryRequired(const u64 acceptedGeneration) noexcept
            {
                const bool disabled = status.state == ProjectAssetAutomaticRecookState::Disabled;
                pending.lastAcceptedGeneration = acceptedGeneration;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::CacheRepairRequired;
                status.recoveryRequired = true;
                // Repair detection must not implicitly enable a deliberately
                // disabled coordinator. A later Enable queues authoritative
                // recovery, which revalidates durable VADI/VDDC truth.
                status.state = disabled ? ProjectAssetAutomaticRecookState::Disabled
                                        : ProjectAssetAutomaticRecookState::RecoveryRequired;
                recoveryRequested = false;
                RefreshPendingStatus();
                RefreshRetryStatus();
            }

            void MarkRecoveryRequired(const u64 acceptedGeneration, const bool requestRecovery = false) noexcept
            {
                // The accepted generation was consumed, but its complete dirty
                // evidence could not fit. Preserve the last complete pending set
                // and require a later authoritative source+VADI recovery pass.
                pending.lastAcceptedGeneration = acceptedGeneration;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::AcceptedChange;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::OverflowRecovery;
                status.recoveryRequired = true;
                status.state = ProjectAssetAutomaticRecookState::RecoveryRequired;
                recoveryRequested = requestRecovery;
                RefreshPendingStatus();
                RefreshRetryStatus();
            }

            void MarkDeliveryRecoveryRequired(const u64 acceptedGeneration) noexcept
            {
                pending.lastAcceptedGeneration = acceptedGeneration;
                status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::DeliveryOverflow;
                status.recoveryRequired = true;
                status.state = ProjectAssetAutomaticRecookState::RecoveryRequired;
                recoveryRequested = false;
                RefreshPendingStatus();
                RefreshRetryStatus();
            }

            void AdoptFinishedCommittedDelivery() noexcept
            {
                if (workerState != assets::RecookState::Succeeded || !batch)
                    return;
                containers::DynamicArray<assets::DependencyRecord> committed(memory::pools::Assets::GetInstance());
                if (batch.TakeCommittedRecords(committed) && !AdoptCommittedDelivery(committed, workerAcceptedGeneration))
                {
                    // The validator already preflighted this exact payload and
                    // all backing containers are reserved. If adoption still
                    // fails, surface explicit recovery rather than claiming the
                    // committed generation has a retained delivery token.
                    workerDeliveryAdoptionFailed = true;
                    workerRecoveryRequired = true;
                    return;
                }
                if (explicitCohort.IsActive() && !CompleteExplicitCohortDelivery())
                {
                    workerDeliveryAdoptionFailed = true;
                    workerRecoveryRequired = true;
                }
            }

            [[nodiscard]] bool DispatchPlan(const bool recovery, const bool explicitOnlyRecovery,
                                            const ProjectAssetAcceptedBaseline& accepted,
                                            const assets::SourceDatabase& sources, assets::ProjectAssetBuildSystem& builds,
                                            ProjectLoosePublisher& publisher, assets::DependencyIndex& index) noexcept
            {
                if (workRunning)
                    return false;
                workerStats = {};
                workerState = assets::RecookState::Building;
                workerFailure = assets::RecookFailure::None;
                workerRootFailure = {};
                status.lastRecookFailure = assets::RecookFailure::None;
                status.firstFailedRoot = {};
                status.failedRootCount = 0u;
                workerFinished = false;
                workerRecoveryRequired = false;
                workerDeliveryCapacityExceeded = false;
                workerDeliveryAdoptionFailed = false;
                workerWasRecovery = recovery;
                workCompleted.SetValue(false);
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create(
                    [this, recovery, explicitOnlyRecovery, acceptedPointer = &accepted, sourcesPointer = &sources, buildsPointer = &builds,
                     publisherPointer = &publisher, indexPointer = &index](const jobs::JobContext&) noexcept
                    {
                        WorkCompletion completion(*this);
                        // Owner ticks retire complete pending snapshots into this
                        // worker-only slot by O(1) swap. Reclaim that payload
                        // here, never from a normal owner tick.
                        retired.Reset();
                        if (recovery)
                        {
                            ProjectAssetAutomaticPendingState recovered;
                            const ProjectAssetAutomaticPendingPrepareResult prepared = explicitOnlyRecovery
                                ? PrepareProjectAssetAutomaticExplicitRecovery(active, pending, *acceptedPointer, *sourcesPointer,
                                                                              *buildsPointer, *indexPointer, config, recovered)
                                : PrepareProjectAssetAutomaticRecovery(*acceptedPointer, *sourcesPointer, *buildsPointer,
                                                                      *indexPointer, config, recovered);
                            if (prepared != ProjectAssetAutomaticPendingPrepareResult::Success)
                            {
                                workerWasRecovery = false;
                                workerRecoveryRequired = true;
                                workerState = assets::RecookState::Failed;
                                workerFailure = assets::RecookFailure::LimitExceeded;
                                workerFinished = true;
                                return;
                            }
                            if (!explicitOnlyRecovery &&
                                !PreserveProjectAssetAutomaticRecoveryInvalidations(active, pending, config, recovered))
                            {
                                workerWasRecovery = false;
                                workerRecoveryRequired = true;
                                workerState = assets::RecookState::Failed;
                                workerFailure = assets::RecookFailure::LimitExceeded;
                                workerFinished = true;
                                return;
                            }
                            if (!explicitOnlyRecovery &&
                                !RevalidateRecoveryRepairs(recovered, *publisherPointer, *sourcesPointer, *buildsPointer,
                                                          *indexPointer, acceptedPointer->generation))
                            {
                                workerWasRecovery = false;
                                workerRecoveryRequired = true;
                                workerState = assets::RecookState::Failed;
                                workerFailure = assets::RecookFailure::DependencyPreparationFailed;
                                workerFinished = true;
                                return;
                            }
                            active.Swap(recovered);
                        }

                        // Missing assets are accepted tombstones, not failed
                        // *current* build requests. Preserve their old indexed
                        // inputs for invalidation; the recooker may only skip a
                        // retired root with a confirmed absence witness.
                        // Present but unsupported/conflicted/request-unresolvable
                        // assets must still fail closed before any build.
                        bool blockingDiagnostic = false;
                        if (active.diagnostics != 0u)
                            for (const ProjectAssetAutomaticPendingAsset& asset : active.assets)
                                if (asset.diagnostic && asset.current.presence != ProjectAssetPresence::Missing)
                                {
                                    blockingDiagnostic = true;
                                    break;
                                }
                        if (blockingDiagnostic)
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::RequestResolutionFailed;
                            workerFinished = true;
                            return;
                        }

                        if (active.inputs.Empty() && active.currentRequests.Empty() && active.forcedOutputs.Empty())
                        {
                            workerState = assets::RecookState::Succeeded;
                            workerFailure = assets::RecookFailure::None;
                            workerFinished = true;
                            active.Reset();
                            return;
                        }

                        containers::DynamicArray<assets::AssetChange> changes(memory::pools::Assets::GetInstance());
                        changes.Resize(active.inputs.Size());
                        if (changes.Size() != active.inputs.Size())
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::OutOfMemory;
                            workerFinished = true;
                            return;
                        }
                        for (u32 changeIndex = 0; changeIndex < active.inputs.Size(); ++changeIndex)
                            changes[changeIndex].identity = active.inputs[changeIndex];

                        // Only currently resolvable metadata-declared outputs
                        // are explicit first/current roots. `active.outputs`
                        // also contains old-side and indexed dependant evidence;
                        // those remain invalidation evidence and are reached via
                        // the typed input/VADI path rather than being miscast as
                        // current build requests.
                        containers::DynamicArray<resources::ResourceReference> explicitOutputs(memory::pools::Assets::GetInstance());
                        explicitOutputs.Resize(active.currentRequests.Size());
                        if (explicitOutputs.Size() != active.currentRequests.Size())
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::OutOfMemory;
                            workerFinished = true;
                            return;
                        }
                        for (u32 outputIndex = 0; outputIndex < active.currentRequests.Size(); ++outputIndex)
                            explicitOutputs[outputIndex] = active.currentRequests[outputIndex].outputReference;

                        containers::DynamicArray<resources::ResourceReference> forcedOutputs(memory::pools::Assets::GetInstance());
                        forcedOutputs.Resize(active.forcedOutputs.Size());
                        if (forcedOutputs.Size() != active.forcedOutputs.Size())
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::OutOfMemory;
                            workerFinished = true;
                            return;
                        }
                        for (u32 forcedIndex = 0; forcedIndex < active.forcedOutputs.Size(); ++forcedIndex)
                            forcedOutputs[forcedIndex] = active.forcedOutputs[forcedIndex].output;

                        containers::DynamicArray<resources::ResourceReference> retiredOutputs(memory::pools::Assets::GetInstance());
                        containers::DynamicArray<resources::ResourceReference> retirementSuccessors(memory::pools::Assets::GetInstance());
                        if (!BuildExplicitCohortRetirementPlan(retiredOutputs, retirementSuccessors))
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::PreCommitValidationFailed;
                            workerFinished = true;
                            return;
                        }

                        const assets::RecookPreCommitValidator validator{
                            &ValidatePreCommit, this, retirementSuccessors.Empty() ? nullptr : &ValidateRetirements};
                        if (!retirementSuccessors.Empty())
                            batch = recooker.Request({changes.TypedData(), changes.Size()},
                                                     {explicitOutputs.TypedData(), explicitOutputs.Size()},
                                                     {forcedOutputs.TypedData(), forcedOutputs.Size()},
                                                     {retiredOutputs.TypedData(), retiredOutputs.Size()},
                                                     {retirementSuccessors.TypedData(), retirementSuccessors.Size()},
                                                     assets::BuildPriority::Background, validator);
                        else
                            batch = recooker.Request({changes.TypedData(), changes.Size()},
                                                     {explicitOutputs.TypedData(), explicitOutputs.Size()},
                                                     {forcedOutputs.TypedData(), forcedOutputs.Size()},
                                                     assets::BuildPriority::Background, validator);
                        if (!batch)
                        {
                            workerState = assets::RecookState::Failed;
                            workerFailure = assets::RecookFailure::OutOfMemory;
                            workerFinished = true;
                            return;
                        }
                        workerStats = batch.GetStats();
                        workerState = batch.GetStatus();
                        workerFailure = batch.GetError();
                        if (batch.HasFinished())
                            workerRootFailure = batch.GetRootFailureDetail();
                        workerFinished = batch.HasFinished();
                        if (workerFinished)
                        {
                            AdoptFinishedCommittedDelivery();
                            batch.Reset();
                            if (workerState == assets::RecookState::Succeeded)
                                active.Reset();
                        }
                    });
                if (!builder.IsValid() || !task || !builder.Dispatch(planningName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                workKind = WorkKind::Plan;
                return true;
            }

            [[nodiscard]] bool DispatchPoll(const bool cancel) noexcept
            {
                if (workRunning)
                    return false;
                workCompleted.SetValue(false);
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this, cancel](const jobs::JobContext&) noexcept
                {
                    WorkCompletion completion(*this);
                    retired.Reset();
                    if (cancel)
                        static_cast<void>(batch.Cancel());
                    workerFinished = batch.Poll();
                    workerStats = batch.GetStats();
                    workerState = batch.GetStatus();
                    workerFailure = batch.GetError();
                    if (workerFinished)
                        workerRootFailure = batch.GetRootFailureDetail();
                    if (workerFinished)
                    {
                        AdoptFinishedCommittedDelivery();
                        batch.Reset();
                        if (workerState == assets::RecookState::Succeeded)
                            active.Reset();
                    }
                });
                if (!builder.IsValid() || !task || !builder.Dispatch(pollingName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                workKind = WorkKind::Poll;
                return true;
            }

            [[nodiscard]] bool DispatchPublication(ProjectLoosePublisher& publisher, filesystem::Manager& files,
                                                   LooseRetirementState& looseRetirement,
                                                   const SourceChangeAccumulator& changes,
                                                   const ProjectAssetAcceptedBaseline& accepted) noexcept
            {
                if (workRunning)
                    return false;
                workerWasRecovery = false;
                workerPublicationSucceeded = false;
                workerPublicationStale = false;
                workerPublishedExplicitCohort = false;
                workerRepairRequiredOutput = {};
                workerPublicationResult = ProjectLoosePublicationResult::Success;
                workerPublicationFailure = {};
                workerPublicationOutputs.Clear();
                workerPublicationInfos.Clear();
                workerRetiredLooseCaptures.Clear();
                workerRetiredLooseReservation = 0u;
                workCompleted.SetValue(false);
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this, publisherPointer = &publisher, filesPointer = &files,
                                                      looseRetirementPointer = &looseRetirement,
                                                      changesPointer = &changes,
                                                      acceptedPointer = &accepted](const jobs::JobContext&) noexcept
                {
                    WorkCompletion completion(*this);
                    retired.Reset();
                    workerFinished = true;

                    containers::DynamicArray<u32> group(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<u8> included(memory::pools::Assets::GetInstance());
                    group.Reserve(config.maximumPendingOutputs);
                    included.Resize(committedDelivery.Size());
                    if (group.Capacity() < config.maximumPendingOutputs || included.Size() != committedDelivery.Size())
                    {
                        workerPublicationResult = ProjectLoosePublicationResult::OutOfMemory;
                        return;
                    }
                    for (u32 index = 0; index < included.Size(); ++index)
                        included[index] = 0u;

                    bool conflict = false;
                    {
                        concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                        bool publishExplicitCohort = explicitCohort.IsActive() && !explicitCohort.publicationBlocked;
                        // A reviewed cohort may consume generated material/shader
                        // intermediates which are not themselves authored asset
                        // outputs. Publish those committed dependencies first;
                        // their absent visibility is pending work, not evidence
                        // that the reviewed cohort is stale. The cohort remains
                        // atomic and retains its original exact successor set.
                        if (publishExplicitCohort)
                        {
                            for (const resources::ResourceReference output : explicitCohort.outputs)
                            {
                                u32 outputIndex = 0u;
                                if (!committedDeliveryIndex.Find(KeyOf(output), outputIndex) ||
                                    outputIndex >= committedDelivery.Size())
                                    continue; // The ordinary cohort validator reports this mismatch.
                                for (const assets::BuildDependency& dependency : committedDelivery[outputIndex].dependencies)
                                {
                                    if (dependency.role != assets::DependencyRole::Generated ||
                                        dependency.requirement == assets::DependencyRequirement::Soft ||
                                        explicitCohort.Contains(dependency.identity))
                                        continue;
                                    u32 dependencyIndex = 0u;
                                    u32 statusIndex = 0u;
                                    if (!committedDeliveryIndex.Find(KeyOf(dependency.identity), dependencyIndex) ||
                                        dependencyIndex >= committedDelivery.Size() ||
                                        committedDelivery[dependencyIndex].contentFingerprint != dependency.content ||
                                        !outputStatusIndex.Find(KeyOf(dependency.identity), statusIndex) ||
                                        statusIndex >= outputStatuses.Size() ||
                                        outputStatuses[statusIndex].publicationRetryRequired)
                                        continue; // Preserve the existing strict rejection below.
                                    const ProjectAssetOutputDeliveryStatus& status = outputStatuses[statusIndex];
                                    if (status.publishedContentFingerprint != dependency.content ||
                                        status.publishedPhysicalPath.Empty())
                                    {
                                        publishExplicitCohort = false;
                                        break;
                                    }
                                }
                                if (!publishExplicitCohort)
                                    break;
                            }
                        }
                        workerPublishedExplicitCohort = publishExplicitCohort;
                        if (publishExplicitCohort)
                        {
                            const ProjectAssetState* const currentAsset = acceptedPointer->LookupAsset(explicitCohort.asset);
                            if (!explicitCohort.deliveryComplete || explicitCohort.outputs.Empty() ||
                                explicitCohort.outputs.Size() > config.maximumPendingOutputs || currentAsset == nullptr ||
                                currentAsset->acceptedGeneration != explicitCohort.acceptedGeneration ||
                                currentAsset->presence != ProjectAssetPresence::Present ||
                                currentAsset->outputs.Count() != explicitCohort.outputs.Size())
                                conflict = true;

                            if (!conflict)
                            {
                                for (const resources::ResourceReference output : explicitCohort.outputs)
                                {
                                    bool acceptedOutput = false;
                                    for (const ProjectAssetOutputState& candidate : currentAsset->outputs)
                                        if (candidate.resource == output)
                                        {
                                            acceptedOutput = true;
                                            break;
                                        }
                                    if (!acceptedOutput)
                                    {
                                        conflict = true;
                                        break;
                                    }

                                    u32 deliveryIndex = 0u;
                                    u32 statusIndex = 0u;
                                    if (!committedDeliveryIndex.Find(KeyOf(output), deliveryIndex) ||
                                        deliveryIndex >= committedDelivery.Size() || committedDelivery[deliveryIndex].output != output ||
                                        !outputStatusIndex.Find(KeyOf(output), statusIndex) || statusIndex >= outputStatuses.Size())
                                    {
                                        conflict = true;
                                        break;
                                    }
                                    const ProjectAssetOutputDeliveryStatus& statusEntry = outputStatuses[statusIndex];
                                    if (statusEntry.resource != output ||
                                        statusEntry.authoredGeneration != explicitCohort.acceptedGeneration ||
                                        statusEntry.publicationRetryRequired ||
                                        statusEntry.committedBuildFingerprint != committedDelivery[deliveryIndex].buildFingerprint ||
                                        statusEntry.committedContentFingerprint != committedDelivery[deliveryIndex].contentFingerprint)
                                    {
                                        conflict = true;
                                        break;
                                    }
                                    included[deliveryIndex] = 1u;
                                    group.PushBack(deliveryIndex);
                                }
                            }

                            for (const u32 index : group)
                            {
                                const assets::DependencyRecord& record = committedDelivery[index];
                                for (const assets::BuildDependency& dependency : record.dependencies)
                                {
                                    if (dependency.role != assets::DependencyRole::Generated ||
                                        dependency.requirement == assets::DependencyRequirement::Soft)
                                        continue;
                                    if (explicitCohort.Contains(dependency.identity))
                                    {
                                        u32 dependencyIndex = 0;
                                        if (!committedDeliveryIndex.Find(KeyOf(dependency.identity), dependencyIndex) ||
                                            dependencyIndex >= committedDelivery.Size() || included[dependencyIndex] == 0u ||
                                            committedDelivery[dependencyIndex].contentFingerprint != dependency.content)
                                        {
                                            conflict = true;
                                            break;
                                        }
                                        continue;
                                    }

                                    u32 publishedStatus = 0u;
                                    if (!outputStatusIndex.Find(KeyOf(dependency.identity), publishedStatus) ||
                                        publishedStatus >= outputStatuses.Size() ||
                                        outputStatuses[publishedStatus].publishedContentFingerprint != dependency.content ||
                                        outputStatuses[publishedStatus].publishedPhysicalPath.Empty() ||
                                        outputStatuses[publishedStatus].publicationRetryRequired)
                                    {
                                        conflict = true;
                                        break;
                                    }
                                }
                                if (conflict)
                                    break;
                            }
                        }
                        else
                        {
                            u32 seed = committedDelivery.Size();
                            for (u32 index = 0; index < committedDelivery.Size(); ++index)
                            {
                                if (explicitCohort.IsActive() && explicitCohort.Contains(committedDelivery[index].output))
                                    continue;
                                u32 statusIndex = 0;
                                if (outputStatusIndex.Find(KeyOf(committedDelivery[index].output), statusIndex) &&
                                    statusIndex < outputStatuses.Size() && !outputStatuses[statusIndex].publicationRetryRequired)
                                {
                                    seed = index;
                                    break;
                                }
                            }
                            if (seed == committedDelivery.Size())
                            {
                                workerPublicationSucceeded = true;
                                return;
                            }
                            included[seed] = 1u;

                            bool changed = true;
                            while (changed && !conflict)
                            {
                                changed = false;
                                for (u32 index = 0; index < committedDelivery.Size() && !conflict; ++index)
                                {
                                    const assets::DependencyRecord& record = committedDelivery[index];
                                    if (included[index] == 0u && explicitCohort.IsActive() && explicitCohort.Contains(record.output))
                                        continue;
                                    if (included[index] != 0u)
                                    {
                                        for (const assets::BuildDependency& dependency : record.dependencies)
                                        {
                                            if (dependency.role != assets::DependencyRole::Generated ||
                                                dependency.requirement == assets::DependencyRequirement::Soft)
                                                continue;
                                            if (explicitCohort.IsActive() && explicitCohort.Contains(dependency.identity))
                                            {
                                                u32 publishedStatus = 0u;
                                                if (!outputStatusIndex.Find(KeyOf(dependency.identity), publishedStatus) ||
                                                    publishedStatus >= outputStatuses.Size() ||
                                                    outputStatuses[publishedStatus].publishedContentFingerprint != dependency.content ||
                                                    outputStatuses[publishedStatus].publishedPhysicalPath.Empty())
                                                {
                                                    conflict = true;
                                                    break;
                                                }
                                                continue;
                                            }
                                            u32 dependencyIndex = 0;
                                            if (!committedDeliveryIndex.Find(KeyOf(dependency.identity), dependencyIndex) ||
                                                dependencyIndex >= committedDelivery.Size())
                                            {
                                                u32 publishedStatus = 0;
                                                if (!outputStatusIndex.Find(KeyOf(dependency.identity), publishedStatus) ||
                                                    publishedStatus >= outputStatuses.Size() ||
                                                    outputStatuses[publishedStatus].publishedContentFingerprint != dependency.content ||
                                                    outputStatuses[publishedStatus].publishedPhysicalPath.Empty() ||
                                                    outputStatuses[publishedStatus].publicationRetryRequired)
                                                {
                                                    conflict = true;
                                                    break;
                                                }
                                                continue;
                                            }
                                            if (committedDelivery[dependencyIndex].contentFingerprint != dependency.content)
                                            {
                                                conflict = true;
                                                break;
                                            }
                                            u32 dependencyStatus = 0;
                                            if (!outputStatusIndex.Find(KeyOf(committedDelivery[dependencyIndex].output), dependencyStatus) ||
                                                dependencyStatus >= outputStatuses.Size() ||
                                                outputStatuses[dependencyStatus].publicationRetryRequired)
                                            {
                                                conflict = true;
                                                break;
                                            }
                                            if (included[dependencyIndex] == 0u)
                                            {
                                                included[dependencyIndex] = 1u;
                                                changed = true;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        bool connected = false;
                                        for (const assets::BuildDependency& dependency : record.dependencies)
                                        {
                                            if (dependency.role != assets::DependencyRole::Generated ||
                                                dependency.requirement == assets::DependencyRequirement::Soft)
                                                continue;
                                            u32 dependencyIndex = 0;
                                            if (!committedDeliveryIndex.Find(KeyOf(dependency.identity), dependencyIndex) ||
                                                dependencyIndex >= committedDelivery.Size() || included[dependencyIndex] == 0u)
                                                continue;
                                            if (committedDelivery[dependencyIndex].contentFingerprint != dependency.content)
                                            {
                                                conflict = true;
                                                break;
                                            }
                                            connected = true;
                                            break;
                                        }
                                        if (conflict || !connected)
                                            continue;
                                        u32 recordStatus = 0;
                                        if (!outputStatusIndex.Find(KeyOf(record.output), recordStatus) || recordStatus >= outputStatuses.Size() ||
                                            outputStatuses[recordStatus].publicationRetryRequired)
                                        {
                                            conflict = true;
                                            break;
                                        }
                                        included[index] = 1u;
                                        changed = true;
                                    }
                                }
                            }

                            for (u32 index = 0; index < committedDelivery.Size(); ++index)
                                if (included[index] != 0u)
                                    group.PushBack(index);
                        }

                        workerPublicationOutputs.Reserve(group.Size());
                        workerPublicationInfos.Reserve(group.Size());
                        if (workerPublicationOutputs.Capacity() < group.Size() || workerPublicationInfos.Capacity() < group.Size())
                        {
                            workerPublicationResult = ProjectLoosePublicationResult::OutOfMemory;
                            return;
                        }
                        for (const u32 index : group)
                        {
                            const assets::DependencyRecord& record = committedDelivery[index];
                            workerPublicationOutputs.PushBack(record.output);
                            ProjectLoosePublicationInfo info;
                            info.buildFingerprint = record.buildFingerprint;
                            info.contentFingerprint = record.contentFingerprint;
                            workerPublicationInfos.PushBack(static_cast<ProjectLoosePublicationInfo&&>(info));
                        }
                    }

                    if (conflict || group.Empty())
                    {
                        workerPublicationStale = conflict;
                        workerPublicationResult = conflict ? ProjectLoosePublicationResult::TargetMismatch
                                                           : ProjectLoosePublicationResult::Success;
                        workerPublicationSucceeded = !conflict;
                        return;
                    }

                    const SourceChangeSnapshot initialSnapshot = changesPointer->GetChangeSnapshot();
                    if (initialSnapshot.overflow)
                    {
                        workerPublicationStale = true;
                        workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                        return;
                    }
                    for (const u32 index : group)
                    {
                        const DeliveryValidationResult validation =
                            ValidateDeliveryRecordCurrent(committedDelivery[index], *publisherPointer, &workerPublicationFailure);
                        if (validation != DeliveryValidationResult::Current)
                        {
                            if (validation == DeliveryValidationResult::RepairRequired)
                            {
                                workerRepairRequiredOutput = committedDelivery[index].output;
                                workerPublicationResult = ProjectLoosePublicationResult::MaterializationFailure;
                            }
                            else if (validation == DeliveryValidationResult::Stale)
                            {
                                workerPublicationStale = true;
                                workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                            }
                            else
                            {
                                workerPublicationResult = workerPublicationFailure.result == ProjectLoosePublicationResult::Success
                                                              ? ProjectLoosePublicationResult::MaterializationFailure
                                                              : workerPublicationFailure.result;
                            }
                            return;
                        }
                    }
                    const SourceChangeSnapshot afterInitialValidation = changesPointer->GetChangeSnapshot();
                    if (afterInitialValidation.overflow || afterInitialValidation.generation != initialSnapshot.generation)
                    {
                        workerPublicationStale = true;
                        workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                        return;
                    }

                    u32 maximumDependencies = 0u;
                    for (const u32 index : group)
                        maximumDependencies += committedDelivery[index].dependencies.Size();
                    containers::DynamicArray<streaming::DependencyDescriptor> flattenedDependencies(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<u32> dependencyOffsets(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<u32> dependencyCounts(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<u64> contentCrc64(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<ProjectLoosePublicationInfo> preparedInfos(memory::pools::Assets::GetInstance());
                    containers::DynamicArray<streaming::LooseResourceDescriptor> descriptors(memory::pools::Assets::GetInstance());
                    flattenedDependencies.Reserve(maximumDependencies);
                    dependencyOffsets.Reserve(group.Size());
                    dependencyCounts.Reserve(group.Size());
                    contentCrc64.Reserve(group.Size());
                    preparedInfos.Reserve(group.Size());
                    descriptors.Reserve(group.Size());
                    if (flattenedDependencies.Capacity() < maximumDependencies || dependencyOffsets.Capacity() < group.Size() ||
                        dependencyCounts.Capacity() < group.Size() || contentCrc64.Capacity() < group.Size() ||
                        preparedInfos.Capacity() < group.Size() || descriptors.Capacity() < group.Size())
                    {
                        workerPublicationResult = ProjectLoosePublicationResult::OutOfMemory;
                        return;
                    }

                    containers::DynamicArray<streaming::DependencyDescriptor> preparedDependencies(memory::pools::Assets::GetInstance());
                    for (u32 groupIndex = 0; groupIndex < group.Size(); ++groupIndex)
                    {
                        const assets::DependencyRecord& record = committedDelivery[group[groupIndex]];
                        ProjectLoosePublicationInfo info;
                        u64 crc = 0u;
                        const ProjectLoosePublicationResult prepared = publisherPointer->PrepareExact(
                            record, preparedDependencies, crc, &workerPublicationFailure, &info);
                        if (prepared != ProjectLoosePublicationResult::Success)
                        {
                            workerPublicationResult = prepared;
                            if (IsCacheRepairFailure(workerPublicationFailure))
                                workerRepairRequiredOutput = record.output;
                            else
                                workerPublicationStale = prepared == ProjectLoosePublicationResult::TargetMismatch ||
                                                         prepared == ProjectLoosePublicationResult::RecordNotFound;
                            return;
                        }
                        dependencyOffsets.PushBack(flattenedDependencies.Size());
                        dependencyCounts.PushBack(preparedDependencies.Size());
                        for (const streaming::DependencyDescriptor& dependency : preparedDependencies)
                            flattenedDependencies.PushBack(dependency);
                        contentCrc64.PushBack(crc);
                        preparedInfos.PushBack(info);
                    }

                    for (u32 groupIndex = 0; groupIndex < group.Size(); ++groupIndex)
                    {
                        const assets::DependencyRecord& record = committedDelivery[group[groupIndex]];
                        const u32 offset = dependencyOffsets[groupIndex];
                        descriptors.PushBack({record.output, preparedInfos[groupIndex].physicalPath,
                                              {flattenedDependencies.TypedData() + offset, dependencyCounts[groupIndex]},
                                              contentCrc64[groupIndex], preparedInfos[groupIndex].priority});
                    }
                    streaming::PreparedLooseBatch preparedVisibility;
                    if (!publisherPointer->PrepareVisibility({descriptors.TypedData(), descriptors.Size()}, preparedVisibility))
                    {
                        workerPublicationResult = ProjectLoosePublicationResult::StreamingPublicationFailure;
                        workerPublicationFailure.result = ProjectLoosePublicationResult::StreamingPublicationFailure;
                        return;
                    }

                    // Freeze the final observation token before the last canonical
                    // source/build/VADI validation. Any watcher callback that lands
                    // during those checks advances the accumulator generation and
                    // makes CommitIfUnchanged reject visibility instead of being
                    // absorbed into a newly sampled expected baseline.
                    const SourceChangeSnapshot finalSnapshot = changesPointer->GetChangeSnapshot();
                    if (finalSnapshot.overflow || finalSnapshot.generation != afterInitialValidation.generation)
                    {
                        workerPublicationStale = true;
                        workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                        return;
                    }
                    for (const u32 index : group)
                    {
                        const DeliveryValidationResult validation =
                            ValidateDeliveryRecordCurrent(committedDelivery[index], *publisherPointer, &workerPublicationFailure);
                        if (validation != DeliveryValidationResult::Current)
                        {
                            if (validation == DeliveryValidationResult::RepairRequired)
                            {
                                workerRepairRequiredOutput = committedDelivery[index].output;
                                workerPublicationResult = ProjectLoosePublicationResult::MaterializationFailure;
                            }
                            else if (validation == DeliveryValidationResult::Stale)
                            {
                                workerPublicationStale = true;
                                workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                            }
                            else
                            {
                                workerPublicationResult = workerPublicationFailure.result == ProjectLoosePublicationResult::Success
                                                              ? ProjectLoosePublicationResult::MaterializationFailure
                                                              : workerPublicationFailure.result;
                            }
                            return;
                        }
                    }
                    // Automatic admission excludes this pipeline's VADI writers.
                    // Perform the exact-token check outside the accumulator lock so
                    // the final callback contains only the no-allocation streamer
                    // visibility mutation.
                    for (const u32 index : group)
                    {
                        if (!publisherPointer->IsCommittedExact(committedDelivery[index]))
                        {
                            workerPublicationStale = true;
                            workerPublicationResult = ProjectLoosePublicationResult::TargetMismatch;
                            return;
                        }
                    }

                    // P4 reserves every superseded physical generation before
                    // the no-allocation visibility callback can make the newer
                    // descriptor observable. Resident protection is captured
                    // from the old exact published generation only; a newer or
                    // unrelated resident generation cannot block this candidate.
                    {
                        concurrency::ScopedLock<concurrency::Mutex> lock(deliveryMutex);
                        workerRetiredLooseCaptures.Clear();
                        for (u32 groupIndex = 0; groupIndex < group.Size(); ++groupIndex)
                        {
                            const assets::DependencyRecord& record = committedDelivery[group[groupIndex]];
                            u32 statusIndex = 0u;
                            if (!outputStatusIndex.Find(KeyOf(record.output), statusIndex) || statusIndex >= outputStatuses.Size())
                                continue;
                            const ProjectAssetOutputDeliveryStatus& output = outputStatuses[statusIndex];
                            if (output.publishedPhysicalPath.Empty() || output.publishedBuildFingerprint.IsEmpty() ||
                                output.publishedContentFingerprint.IsEmpty() ||
                                output.publishedPhysicalPath == preparedInfos[groupIndex].physicalPath)
                                continue;
                            RetiredLooseCapture capture;
                            capture.resource = record.output;
                            capture.build = output.publishedBuildFingerprint;
                            capture.content = output.publishedContentFingerprint;
                            capture.physicalPath = output.publishedPhysicalPath;
                            if (output.residentResourceGeneration != 0u &&
                                output.residentBuildFingerprint == output.publishedBuildFingerprint &&
                                output.residentContentFingerprint == output.publishedContentFingerprint)
                                capture.residentGeneration = output.residentResourceGeneration;
                            workerRetiredLooseCaptures.PushBack(static_cast<RetiredLooseCapture&&>(capture));
                        }
                    }
                    if (workerRetiredLooseCaptures.Size() > group.Size() ||
                        !looseRetirementPointer->ReserveBatch(
                            *filesPointer,
                            {workerRetiredLooseCaptures.TypedData(), workerRetiredLooseCaptures.Size()},
                            workerRetiredLooseReservation))
                    {
                        workerRetiredLooseCaptures.Clear();
                        workerRetiredLooseReservation = 0u;
                        workerPublicationResult = ProjectLoosePublicationResult::LimitExceeded;
                        workerPublicationFailure.result = ProjectLoosePublicationResult::LimitExceeded;
                        return;
                    }

                    struct VisibilityCommit
                    {
                        ProjectLoosePublisher* publisher = nullptr;
                        streaming::PreparedLooseBatch* batch = nullptr;
                        bool attempted = false;
                    };
                    VisibilityCommit visibility{publisherPointer, &preparedVisibility, false};
                    const auto commit = [](void* const userData) noexcept -> bool
                    {
                        auto& state = *static_cast<VisibilityCommit*>(userData);
                        state.attempted = true;
                        return state.publisher->CommitPreparedVisibility(*state.batch);
                    };
                    if (!changesPointer->CommitIfUnchanged(finalSnapshot, commit, &visibility))
                    {
                        looseRetirementPointer->RollbackReservation(
                            {workerRetiredLooseCaptures.TypedData(), workerRetiredLooseCaptures.Size()}, workerRetiredLooseReservation);
                        workerRetiredLooseReservation = 0u;
                        workerPublicationStale = !visibility.attempted;
                        workerPublicationResult = workerPublicationStale ? ProjectLoosePublicationResult::TargetMismatch
                                                                        : ProjectLoosePublicationResult::StreamingPublicationFailure;
                        if (!workerPublicationStale)
                            workerPublicationFailure.result = ProjectLoosePublicationResult::StreamingPublicationFailure;
                        return;
                    }

                    // Visibility is now committed. From this point onward the
                    // retired reservation is durable process truth even if owner
                    // settlement later fails; maintenance treats unsettled or
                    // still-reserved candidates as protected.
                    static_cast<void>(looseRetirementPointer->CommitReservation(
                        {workerRetiredLooseCaptures.TypedData(), workerRetiredLooseCaptures.Size()}, workerRetiredLooseReservation));
                    workerRetiredLooseReservation = 0u;

                    for (u32 index = 0; index < preparedInfos.Size(); ++index)
                        workerPublicationInfos[index] = preparedInfos[index];
                    workerPublicationSucceeded = true;
                    workerPublicationResult = ProjectLoosePublicationResult::Success;
                });
                if (!builder.IsValid() || !task || !builder.Dispatch(publicationName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                workKind = WorkKind::Publish;
                publicationSettlementPending = true;
                return true;
            }

            [[nodiscard]] WorkKind Drain() noexcept
            {
                if (workRunning && !workCompleted.GetValue())
                    workCompletedEvent.Wait();
                const WorkKind completed = workKind;
                workRunning = false;
                workKind = WorkKind::None;
                if (batch)
                {
                    if (!batch.HasFinished())
                        static_cast<void>(batch.Cancel());
                    batch.Wait();
                    batch.Reset();
                }
                // Shutdown is the documented blocking/reclamation boundary.
                retired.Reset();
                admissionOwned = false;
                pollDispatchFailed = false;
                pollDispatchRetryRequested = false;
                recoverAfterPollDispatchFailure = false;
                return completed;
            }

            [[nodiscard]] bool Shutdown() noexcept
            {
                static_cast<void>(Drain());
                return recooker.Shutdown();
            }
        };
        AutomaticRecookState automaticRecook;

        struct VddcMaintenanceState final
        {
            explicit VddcMaintenanceState(const ProjectAssetCacheMaintenanceConfig& maintenanceConfig) noexcept
                : config(maintenanceConfig), protections(memory::pools::Assets::GetInstance()),
                  deliveryProtections(memory::pools::Assets::GetInstance()),
                  diagnostics(memory::pools::Assets::GetInstance()), workerDiagnostics(memory::pools::Assets::GetInstance()),
                  jobName("Assets/ProjectVddcMaintenance")
            {
            }

            ProjectAssetCacheMaintenanceConfig config;
            ProjectAssetVddcMaintenanceStatus status;
            containers::DynamicArray<assets::ArtifactSetKey> protections;
            containers::DynamicArray<assets::ArtifactSetKey> deliveryProtections;
            containers::DynamicArray<assets::PersistentCacheMaintenanceDiagnostic> diagnostics;
            containers::DynamicArray<assets::PersistentCacheMaintenanceDiagnostic> workerDiagnostics;
            assets::PersistentCacheMaintenanceReport workerReport;
            assets::PersistentCacheMaintenanceResult workerResult = assets::PersistentCacheMaintenanceResult::Success;
            jobs::JobName jobName;
            concurrency::Atomic<bool> workCompleted{true};
            concurrency::Atomic<bool> cancellationRequested{false};
            concurrency::ManualResetEvent workCompletedEvent{true};
            bool workRunning = false;
            bool admissionOwned = false;

            [[nodiscard]] bool OwnsAdmission() const noexcept { return admissionOwned; }
            [[nodiscard]] bool HasWork() const noexcept { return admissionOwned || workRunning; }

            [[nodiscard]] static bool IsCancellationRequested(void* const userData) noexcept
            {
                const auto* const state = static_cast<const VddcMaintenanceState*>(userData);
                return state != nullptr && state->cancellationRequested.GetValue();
            }

            struct WorkCompletion final
            {
                explicit WorkCompletion(VddcMaintenanceState& maintenance) noexcept : owner(&maintenance) {}
                ~WorkCompletion()
                {
                    owner->workCompleted.SetValue(true);
                    owner->workCompletedEvent.Signal();
                }

                VddcMaintenanceState* owner = nullptr;
            };

            [[nodiscard]] bool Dispatch(assets::ProjectAssetBuildSystem& builds, const u64 protectionEpoch) noexcept
            {
                if (workRunning || !admissionOwned || protectionEpoch == 0u)
                    return false;
                workerReport = {};
                workerResult = assets::PersistentCacheMaintenanceResult::InvalidState;
                workerDiagnostics.Clear();
                cancellationRequested.SetValue(false);
                workCompleted.SetValue(false);
                workCompletedEvent.Reset();
                jobs::Builder builder({jobs::Priority::Latent, jobs::Affinity::AnyWorker}, this);
                jobs::Task task = jobs::Task::Create([this, buildOwner = &builds, protectionEpoch](const jobs::JobContext&) noexcept
                {
                    WorkCompletion completion(*this);
                    workerResult = buildOwner->MaintainPersistentCache(
                        {protections.TypedData(), protections.Size()}, protectionEpoch, config.VddcLimits(), workerReport, workerDiagnostics,
                        &VddcMaintenanceState::IsCancellationRequested, this);
                });
                if (!builder.IsValid() || !task || !builder.Dispatch(jobName, static_cast<jobs::Task&&>(task)))
                {
                    workCompleted.SetValue(true);
                    workCompletedEvent.Signal();
                    return false;
                }
                workRunning = true;
                return true;
            }

            void Drain() noexcept
            {
                if (workRunning && !workCompleted.GetValue())
                {
                    cancellationRequested.SetValue(true);
                    workCompletedEvent.Wait();
                }
                workRunning = false;
                admissionOwned = false;
                cancellationRequested.SetValue(false);
            }
        };
        VddcMaintenanceState vddcMaintenance;
        containers::DynamicArray<filesystem::AbsolutePath> writableRoots;
        assets::AssetFileTransaction transaction;
        assets::ProjectAssetBuildSystem builds;
        ProjectLoosePublisher publisher;
        containers::DynamicArray<PublishedResource> publications;
        containers::HashMap<resources::ResourceId, u32> publicationIndex;
        containers::DynamicArray<AssetHistoryRecord> undo;
        containers::DynamicArray<AssetHistoryRecord> redo;
        u64 undoBytes = 0;
        u64 redoBytes = 0;

        // E2.1 external observation. The accumulator is fed only by the
        // observer callback; the owner thread drains it in TickObservation.
        enum class ReconcileStage : u8
        {
            Publish,
            Remove,
            RemoveCommit,
            Prune,
            Complete
        };

        struct ReconcileState
        {
            ReconcileState() noexcept
                : desired(memory::pools::Assets::GetInstance()), desiredLookup(memory::pools::Assets::GetInstance()),
                  looseFiles(memory::pools::Assets::GetInstance()), removeResources(memory::pools::Assets::GetInstance()),
                  removePaths(memory::pools::Assets::GetInstance())
            {
            }

            [[nodiscard]] bool ReserveRemovalCapacity(const u32 maximumPublishedResources) noexcept
            {
                removeResources.Reserve(maximumPublishedResources);
                removePaths.Reserve(maximumPublishedResources);
                return removeResources.Capacity() >= maximumPublishedResources && removePaths.Capacity() >= maximumPublishedResources;
            }

            void Reset() noexcept
            {
                desired.Clear();
                desiredLookup.Clear();
                looseFiles.Clear();
                ResetScalars();
            }

            void ResetScalars() noexcept
            {
                removeResources.Clear();
                removePaths.Clear();
                publishIndex = 0;
                removeIndex = 0;
                removeCommitIndex = 0;
                pruneIndex = 0;
                operationsCompleted = 0;
                stage = ReconcileStage::Publish;
                report = {};
            }

            containers::DynamicArray<resources::ResourceReference> desired;
            containers::HashMap<resources::ResourceId, u8> desiredLookup;
            containers::DynamicArray<filesystem::AbsolutePath> looseFiles;
            containers::DynamicArray<resources::ResourceReference> removeResources;
            containers::DynamicArray<resources::ResourcePath> removePaths;
            u32 publishIndex = 0;
            u32 removeIndex = 0;
            u32 removeCommitIndex = 0;
            u32 pruneIndex = 0;
            u32 operationsCompleted = 0;
            ReconcileStage stage = ReconcileStage::Publish;
            ProjectAssetRestoreReport report;
        };

        struct ObservationWorker final : public concurrency::Thread
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

            enum class Command : u8
            {
                None,
                Rescan,
                RetireCatalog,
                Publish,
                ValidateExact,
                RemoveBatch,
                Stop
            };

            ObservationWorker(filesystem::Manager& fileManager, ProjectLoosePublisher& publicationOwner,
                              streaming::ResourceStreamer& resourceStreamer,
                              const assets::ProjectAssetBuildSystem& buildOwner, assets::DependencyIndex& dependencyIndex,
                              const AutomaticRecookState& automaticState,
                              const containers::ArraySpan<const assets::SourceRoot> sourceRoots,
                              const containers::ArraySpan<const assets::SourceImporterDescriptor> sourceImporters,
                              const assets::TargetPlatform buildTarget, const assets::ProjectSourceDataLimits& dataLimits,
                              const ProjectAssetAcceptedBaseline& acceptedState,
                              const filesystem::AbsolutePath& acceptedSnapshotRoot,
                              const filesystem::AbsolutePath& publicationRoot, const filesystem::AbsolutePath& transactionJournal,
                              const u32 publishedLimit,
                              const u32 looseFileLimit) noexcept
                : concurrency::Thread("vanguard-asset-observation"), files(&fileManager), publisher(&publicationOwner), streamer(&resourceStreamer),
                  builds(&buildOwner), index(&dependencyIndex), automaticRecook(&automaticState),
                  target(buildTarget), sourceDataLimits(dataLimits),
                  accepted(&acceptedState), proposed(acceptedState.limits),
                  snapshotRoot(acceptedSnapshotRoot),
                  looseRoot(publicationRoot), journalPath(transactionJournal),
                  maximumPublishedResources(publishedLimit), maximumLooseFiles(looseFileLimit),
                  roots(memory::pools::Assets::GetInstance()), importers(memory::pools::Assets::GetInstance()),
                  desired(memory::pools::Assets::GetInstance()), desiredLookup(memory::pools::Assets::GetInstance()),
                  looseFiles(memory::pools::Assets::GetInstance())
            {
                roots.Reserve(sourceRoots.Count());
                importers.Reserve(sourceImporters.Count());
                for (const assets::SourceRoot& root : sourceRoots)
                    roots.PushBack(root);
                for (const assets::SourceImporterDescriptor& importer : sourceImporters)
                    importers.PushBack(importer);
                configured = roots.Size() == sourceRoots.Count() && importers.Size() == sourceImporters.Count() &&
                             !roots.Empty() && !importers.Empty();
                completed.SetValue(true);
                command.SetValue(static_cast<u32>(Command::None));
            }

            [[nodiscard]] bool Submit(const Command value, const resources::ResourceReference commandResource = {}) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(commandMutex);
                if (value == Command::None || !completed.GetValue() || (!configured && value != Command::Stop))
                    return false;
                resource = commandResource;
                operationSucceeded = false;
                publicationResult = ProjectLoosePublicationResult::InvalidState;
                publicationFailure = {};
                publicationInfo = {};
                completed.SetValue(false);
                completedEvent.Reset();
                command.SetValue(static_cast<u32>(value));
                commandEvent.Signal();
                return true;
            }

            [[nodiscard]] bool SubmitRemoveBatch(const containers::ArraySpan<const resources::ResourcePath> paths) noexcept
            {
                concurrency::ScopedLock<concurrency::Mutex> lock(commandMutex);
                if (!configured || !completed.GetValue() || paths.Empty())
                    return false;
                removePaths = paths.Data();
                removePathCount = paths.Count();
                operationSucceeded = false;
                publicationResult = ProjectLoosePublicationResult::InvalidState;
                publicationFailure = {};
                publicationInfo = {};
                completed.SetValue(false);
                completedEvent.Reset();
                command.SetValue(static_cast<u32>(Command::RemoveBatch));
                commandEvent.Signal();
                return true;
            }

            [[nodiscard]] bool IsCompleted() const noexcept { return completed.GetValue(); }
            [[nodiscard]] bool IsConfigured() const noexcept { return configured; }
            [[nodiscard]] Command CurrentCommand() const noexcept { return static_cast<Command>(command.GetValue()); }
            void WaitForCompletion() noexcept
            {
                while (!completed.GetValue())
                    completedEvent.Wait();
            }

            void ThreadFunction() noexcept override
            {
                for (;;)
                {
                    commandEvent.Wait();
                    // Reset before publishing completion. A command submitted
                    // after completed becomes visible therefore cannot have its
                    // signal consumed by this iteration.
                    commandEvent.Reset();
                    const Command current = CurrentCommand();
                    if (current == Command::Stop)
                    {
                        ReleaseAll();
                        FinishCommand();
                        return;
                    }
                    if (current == Command::Rescan)
                        ExecuteRescan();
                    else if (current == Command::RetireCatalog)
                        RetireCatalog();
                    else if (current == Command::Publish)
                    {
                        publicationResult = publisher != nullptr
                                                ? publisher->Publish(resource, &publicationFailure, &publicationInfo)
                                                : ProjectLoosePublicationResult::InvalidState;
                        operationSucceeded = publicationResult == ProjectLoosePublicationResult::Success;
                    }
                    else if (current == Command::ValidateExact)
                    {
                        assets::DependencyRecord record;
                        const assets::IndexResult found = index != nullptr ? index->Find(resource, record)
                                                                          : assets::IndexResult::InvalidState;
                        publicationFailure.index = found;
                        if (found == assets::IndexResult::NotFound)
                            publicationResult = ProjectLoosePublicationResult::RecordNotFound;
                        else if (!assets::IsSuccess(found))
                            publicationResult = ProjectLoosePublicationResult::IndexFailure;
                        else if (publisher == nullptr)
                            publicationResult = ProjectLoosePublicationResult::InvalidState;
                        else
                        {
                            publicationInfo.buildFingerprint = record.buildFingerprint;
                            publicationInfo.contentFingerprint = record.contentFingerprint;
                            publicationResult = publisher->ValidateExact(record, &publicationFailure);
                        }
                        publicationFailure.result = publicationResult;
                        operationSucceeded = publicationResult == ProjectLoosePublicationResult::Success;
                    }
                    else if (current == Command::RemoveBatch)
                    {
                        operationSucceeded = streamer != nullptr && removePaths != nullptr && removePathCount != 0u &&
                                             streamer->UnregisterLooseBatch({removePaths, removePathCount});
                    }
                    FinishCommand();
                }
            }

            void ExecuteRescan() noexcept
            {
                desired.Clear();
                desiredLookup.Clear();
                looseFiles.Clear();
                automaticPending.Reset();
                automaticPendingResult = ProjectAssetAutomaticPendingPrepareResult::NoChange;
                sourceResult = assets::SourceDatabaseResult::ScanFailed;
                scanFailure = filesystem::ScanResult::Success;
                snapshotResult = ProjectAssetSnapshotResult::Success;
                snapshotStaged = false;
                if (accepted == nullptr)
                    sourceResult = assets::SourceDatabaseResult::InvalidInventory;
                else
                    sourceResult = PrepareProjectAssetCandidate(
                        *files, {roots.TypedData(), roots.Size()}, {importers.TypedData(), importers.Size()}, target, sourceDataLimits,
                        journalPath, *accepted, maximumPublishedResources, looseRoot, maximumLooseFiles, candidate, proposed,
                        desired, desiredLookup, looseFiles, &scanFailure);
                if (sourceResult == assets::SourceDatabaseResult::Success &&
                    !ApplyCohortIntentDesired(proposed.cohortIntent, desired, desiredLookup, maximumPublishedResources))
                    sourceResult = assets::SourceDatabaseResult::LimitExceeded;
                if (sourceResult == assets::SourceDatabaseResult::Success && automaticRecook != nullptr && automaticRecook->IsPolicyEnabled() &&
                    builds != nullptr && index != nullptr)
                {
                    automaticPendingResult = PrepareProjectAssetAutomaticPending(
                        automaticRecook->pending, proposed, candidate, *builds, *index, automaticRecook->config, automaticPending);
                }
                if (sourceResult == assets::SourceDatabaseResult::Success && proposed.generation != accepted->generation)
                {
                    snapshotResult = StageProjectAssetAcceptedSnapshot(*files, snapshotRoot, proposed);
                    snapshotStaged = snapshotResult == ProjectAssetSnapshotResult::Success;
                }
                if (sourceResult != assets::SourceDatabaseResult::Success || snapshotResult != ProjectAssetSnapshotResult::Success)
                    ReleasePreparedState();
            }

            void RetireCatalog() noexcept
            {
                // After the owner O(1)-swaps the prepared candidate/payload into
                // stable state, this worker owns the previous catalog, previous
                // accepted asset baseline and previous reconcile buffers. Retire all
                // project-sized contents here, never in TickObservation.
                ReleasePreparedState();
                desired.Clear();
                desiredLookup.Clear();
                looseFiles.Clear();
            }

            void ReleaseCandidate() noexcept
            {
                {
                    assets::SourceDatabase retired;
                    candidate.Swap(retired);
                }
            }

            void ReleasePreparedState() noexcept
            {
                if (snapshotStaged && files != nullptr)
                    static_cast<void>(DiscardStagedProjectAssetAcceptedSnapshot(*files, snapshotRoot));
                snapshotStaged = false;
                ReleaseCandidate();
                ProjectAssetAcceptedBaseline retired(proposed.limits);
                proposed.Swap(retired);
                ProjectAssetAutomaticPendingState retiredAutomatic;
                automaticPending.Swap(retiredAutomatic);
                automaticPendingResult = ProjectAssetAutomaticPendingPrepareResult::NoChange;
            }

            void ReleaseAll() noexcept
            {
                ReleasePreparedState();
                {
                    containers::DynamicArray<resources::ResourceReference> retired(memory::pools::Assets::GetInstance());
                    desired.Swap(retired);
                }
                {
                    containers::HashMap<resources::ResourceId, u8> retired(memory::pools::Assets::GetInstance());
                    desiredLookup.Swap(retired);
                }
                {
                    containers::DynamicArray<filesystem::AbsolutePath> retired(memory::pools::Assets::GetInstance());
                    looseFiles.Swap(retired);
                }
                {
                    containers::DynamicArray<assets::SourceRoot> retired(memory::pools::Assets::GetInstance());
                    roots.Swap(retired);
                }
                {
                    containers::DynamicArray<assets::SourceImporterDescriptor> retired(memory::pools::Assets::GetInstance());
                    importers.Swap(retired);
                }
            }

            void FinishCommand() noexcept
            {
                // Submit and completion publication are one handoff transaction.
                // Without this lock a new Submit could observe completed=true,
                // reset the event, then receive the previous command's delayed
                // Signal(), making teardown believe the new command completed.
                concurrency::ScopedLock<concurrency::Mutex> lock(commandMutex);
                completed.SetValue(true);
                completedEvent.Signal();
            }

            filesystem::Manager* files = nullptr;
            ProjectLoosePublisher* publisher = nullptr;
            streaming::ResourceStreamer* streamer = nullptr;
            const assets::ProjectAssetBuildSystem* builds = nullptr;
            assets::DependencyIndex* index = nullptr;
            const AutomaticRecookState* automaticRecook = nullptr;
            assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
            assets::ProjectSourceDataLimits sourceDataLimits;
            const ProjectAssetAcceptedBaseline* accepted = nullptr;
            ProjectAssetAcceptedBaseline proposed;
            filesystem::AbsolutePath snapshotRoot;
            filesystem::AbsolutePath looseRoot;
            filesystem::AbsolutePath journalPath;
            u32 maximumPublishedResources = 0;
            u32 maximumLooseFiles = 0;
            containers::DynamicArray<assets::SourceRoot> roots;
            containers::DynamicArray<assets::SourceImporterDescriptor> importers;
            assets::SourceDatabase candidate;
            ProjectAssetAutomaticPendingState automaticPending;
            ProjectAssetAutomaticPendingPrepareResult automaticPendingResult = ProjectAssetAutomaticPendingPrepareResult::NoChange;
            containers::DynamicArray<resources::ResourceReference> desired;
            containers::HashMap<resources::ResourceId, u8> desiredLookup;
            containers::DynamicArray<filesystem::AbsolutePath> looseFiles;
            assets::SourceDatabaseResult sourceResult = assets::SourceDatabaseResult::ScanFailed;
            filesystem::ScanResult scanFailure = filesystem::ScanResult::Success;
            ProjectAssetSnapshotResult snapshotResult = ProjectAssetSnapshotResult::NotChecked;
            bool snapshotStaged = false;
            resources::ResourceReference resource;
            ProjectLoosePublicationResult publicationResult = ProjectLoosePublicationResult::InvalidState;
            ProjectLoosePublicationFailure publicationFailure;
            ProjectLoosePublicationInfo publicationInfo;
            const resources::ResourcePath* removePaths = nullptr;
            u32 removePathCount = 0;
            bool operationSucceeded = false;
            concurrency::Mutex commandMutex;
            concurrency::Atomic<u32> command;
            concurrency::Atomic<bool> completed;
            concurrency::ManualResetEvent commandEvent;
            concurrency::ManualResetEvent completedEvent{true};
            bool configured = false;
        };

        struct ObservationState
        {
            filesystem::DirectoryObservationSource* source = nullptr;
            bool running = false;
            ProjectAssetObservationConfig config;
            ObservationWorker* worker = nullptr;
            ReconcileState reconcile;
            bool reconcileActive = false;
            bool rescanPending = false;
            bool catalogCommitted = false;
            bool snapshotFailureRetirePending = false;
            ProjectAssetSnapshotResult snapshotFailure = ProjectAssetSnapshotResult::NotChecked;
            bool operationPending = false;
            bool initialCatchUpRequired = false;
            u64 reconcileGeneration = 0;
        };
        ObservationState observation;
        SourceChangeAccumulator accumulator;
        PlatformDirectoryObserver platformObserver;

        static void OnDirectoryObservation(const filesystem::DirectoryObservation& hint, void* const userData) noexcept
        {
            auto* const impl = static_cast<Impl*>(userData);
            if (impl != nullptr && impl->observation.running)
                static_cast<void>(impl->accumulator.Publish(hint));
        }

        // Single source of truth for source-mutation/build exclusion. An active
        // observation reconciliation, or any live BuildGraph work, forbids source
        // mutation and forbids starting a new observation session.
        [[nodiscard]] bool CanMutateSourcesCoreUnfenced() noexcept
        {
            if (observation.reconcileActive || automaticRecook.OwnsAdmission() || vddcMaintenance.OwnsAdmission() ||
                looseRetirement.OwnsAdmission())
                return false;
            const assets::BuildGraph* const graph = builds.GetBuildGraph();
            if (graph == nullptr)
                return false;
            const assets::BuildGraphStats stats = graph->GetStats();
            return stats.externalRequests == 0 && stats.activeOperations == 0 && stats.waitingForAdmission == 0 &&
                   stats.queuedRequestBytes == 0 && stats.activeExecutionBytes == 0 && stats.retainedOutputBytes == 0;
        }

        [[nodiscard]] bool CanMutateSourcesCore() noexcept
        {
            return !deferredPlannedImportPrepare.HasWork() && CanMutateSourcesCoreUnfenced();
        }

        [[nodiscard]] bool CanSettleDeferredPlannedImportPrepare() noexcept
        {
            if (!deferredPlannedImportPrepare.IsActive() || deferredPlannedImportPrepare.workRunning ||
                !CanMutateSourcesCoreUnfenced())
                return false;
            const assets::DependencyIndex* const index = builds.GetDependencyIndex();
            return index != nullptr && !index->HasRetirementProtections();
        }

        [[nodiscard]] ProjectAssetPipelineResult VerifyIndependentPlannedImportDiskWitness(
            const assets::AssetId asset, const filesystem::AbsolutePath& sourceRoot,
            const filesystem::AbsolutePath& sourceDestination, const resources::ResourcePath sourceIdentity,
            const containers::ArraySpan<const resources::ResourcePath> stagedOutputPaths,
            ProjectAssetPipelineFailure* const failure, const bool collisionOnly = false) noexcept
        {
            const auto stale = [&](const std::source_location site = std::source_location::current()) noexcept
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::StalePlan;
                    failure->stalePlanSite = site.line();
                }
                return ProjectAssetPipelineResult::StalePlan;
            };
            if (!asset.IsValid() || !sourceRoot.IsDirectoryPath() || !sourceDestination.IsFilePath() ||
                !sourceIdentity.IsValid() || (stagedOutputPaths.Count() != 0u && stagedOutputPaths.Data() == nullptr))
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                          : ProjectAssetPipelineResult::InvalidState;

            const containers::ArraySpan<const assets::SourceRoot> roots = sources->GetRoots();
            bool writableRoot = false;
            for (const assets::SourceRoot& root : roots)
                writableRoot = writableRoot || (!root.readOnly && root.directory == sourceRoot);
            if (!writableRoot || !SourceIdentityMatchesRoot(sourceRoot, sourceDestination, sourceIdentity))
                return stale();

            // This is the second authoritative admission proof for an async
            // independent first import. The earlier preflight remains required,
            // but a long Prepare window allows unrelated disk writers to race it.
            // Rebuild the complete candidate immediately before owner Apply and
            // repeat the exact independent-import collision facts. Check the two
            // create-only authored paths directly as well: an orphan metadata file
            // may not be represented as a candidate row, but it is still a stale
            // reviewed witness and ApplyPrepared must never overwrite it.
            if (files->FileExist(sourceDestination) || files->FileExist(MetadataPath(sourceDestination)))
                return stale();

            assets::SourceDatabase verifiedCatalog;
            filesystem::ScanResult verificationScan = filesystem::ScanResult::Success;
            assets::SourceDatabaseResult verification = assets::SourceDatabaseResult::Success;
            if (collisionOnly)
            {
                // The complete precommit candidate was already validated on
                // the owner before staging. The second proof is expressly for
                // the race opened by asynchronous transaction Prepare: an
                // unrelated source/VMETA/output identity may now exist on disk.
                // Rescan still inventories ALL source roots and parses EVERY
                // VMETA, rebuilding the same path/AssetId/source/output maps
                // consumed below. Avoid rebuilding references, hashing every
                // unrelated source, comparing accepted-state deltas or scanning
                // cooked loose files when none of those results participates
                // in the final independent-create collision decision.
                const filesystem::AbsolutePath excludedFiles[] = {
                    config.operationJournalPath, SuffixedPath(config.operationJournalPath, ".stage")};
                verification = verifiedCatalog.Rescan(*files, roots, &verificationScan, {excludedFiles, 2u});
            }
            else
            {
                ProjectAssetAcceptedBaseline verifiedAccepted(config.changeState);
                containers::DynamicArray<resources::ResourceReference> verifiedDesired(memory::pools::Assets::GetInstance());
                containers::HashMap<resources::ResourceId, u8> verifiedDesiredLookup(memory::pools::Assets::GetInstance());
                containers::DynamicArray<filesystem::AbsolutePath> verifiedLooseFiles(memory::pools::Assets::GetInstance());
                verification = PrepareProjectAssetCandidate(
                    *files, roots, builds.GetImporters(), config.build.target, config.build.sourceData,
                    config.operationJournalPath, accepted, config.maximumPublishedResources, config.publication.looseRoot,
                    config.maximumLooseFiles, verifiedCatalog, verifiedAccepted, verifiedDesired, verifiedDesiredLookup,
                    verifiedLooseFiles, &verificationScan);
            }
            if (verification != assets::SourceDatabaseResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                    failure->sources = verification;
                }
                return ProjectAssetPipelineResult::ReconciliationFailed;
            }
            if (verifiedCatalog.ContainsAssetIdentity(asset) || verifiedCatalog.LookupResource(sourceDestination) != nullptr)
                return stale();
            for (const assets::SourceRecord& source : verifiedCatalog.GetResources())
                if (source.sourceIdentity == sourceIdentity)
                    return stale();
            for (const resources::ResourcePath output : stagedOutputPaths)
                if (!output.IsValid() || verifiedCatalog.ContainsOutputPath(output))
                    return stale();
            return ProjectAssetPipelineResult::Success;
        }

        [[nodiscard]] bool CanReconcileSources() noexcept
        {
            if (!CanMutateSourcesCore())
                return false;
            const assets::DependencyIndex* const index = builds.GetDependencyIndex();
            return index != nullptr && !index->HasRetirementProtections();
        }

        [[nodiscard]] bool CanMutateSources() noexcept
        {
            return CanReconcileSources() && (!accepted.cohortIntent.IsActive() || plannedImportIntentOwner);
        }

        [[nodiscard]] bool CohortIntentProtectsTopologyPredecessor(
            const resources::ResourceReference output) const noexcept
        {
            if (!accepted.cohortIntent.IsActive())
                return false;
            for (const resources::ResourceReference predecessor : accepted.cohortIntent.predecessorOutputs)
                if (predecessor == output)
                    return true;
            return false;
        }

        [[nodiscard]] ProjectAssetSnapshotResult ReplaceCohortIntent(ProjectAssetCohortIntent& desired) noexcept
        {
            accepted.cohortIntent.Swap(desired);
            const ProjectAssetSnapshotResult persisted =
                PersistProjectAssetSnapshot(*files, config.build.dependencyIndexRoot, accepted);
            if (persisted != ProjectAssetSnapshotResult::Success)
                accepted.cohortIntent.Swap(desired);
            return persisted;
        }

        [[nodiscard]] ProjectAssetSnapshotResult ClearCohortIntent() noexcept
        {
            if (!accepted.cohortIntent.IsActive())
                return ProjectAssetSnapshotResult::Success;
            ProjectAssetCohortIntent empty;
            return ReplaceCohortIntent(empty);
        }

        [[nodiscard]] bool CohortIntentMatchesCurrentAcceptedState() const noexcept
        {
            const ProjectAssetCohortIntent& intent = accepted.cohortIntent;
            if (!intent.IsActive())
                return false;
            const ProjectAssetState* const state = accepted.LookupAsset(intent.asset);
            if (state == nullptr || state->presence != ProjectAssetPresence::Present ||
                state->sourceIdentity != intent.sourceIdentity || state->acceptedGeneration == 0u ||
                state->outputs.Count() != intent.successorOutputs.Size())
                return false;
            for (const resources::ResourceReference expected : intent.successorOutputs)
            {
                bool found = false;
                for (const ProjectAssetOutputState& output : state->outputs)
                    if (output.resource == expected)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool CohortIntentTargetsCurrentMetadata() const noexcept
        {
            const ProjectAssetCohortIntent& intent = accepted.cohortIntent;
            if (!intent.IsActive())
                return false;
            const assets::SourceRecord* const source = sources->LookupAsset(intent.asset);
            if (source == nullptr || source->sourceIdentity != intent.sourceIdentity ||
                source->metadataResult != assets::MetadataResult::Success || !(source->metadata.id == intent.asset))
                return false;
            containers::String canonical;
            if (assets::EncodeMetadata(source->metadata, canonical) != assets::MetadataResult::Success)
                return false;
            return crypto::Sha256(canonical.AsChar(), canonical.Length()) == intent.targetMetadataFingerprint;
        }

        [[nodiscard]] static bool SameDeferredDiagnostic(const ProjectAssetSourceDiagnostic& left,
                                                         const ProjectAssetSourceDiagnostic& right) noexcept
        {
            return left.sourceRoot == right.sourceRoot && left.sourceIdentity == right.sourceIdentity &&
                   left.physicalPath == right.physicalPath && left.asset == right.asset &&
                   left.sourceIssues == right.sourceIssues && left.metadataResult == right.metadataResult &&
                   left.importState == right.importState && left.referenceScanState == right.referenceScanState &&
                   left.reasons == right.reasons && left.hasParsedAssetIdentity == right.hasParsedAssetIdentity;
        }

        [[nodiscard]] static bool SameDeferredDiagnostics(
            const containers::ArraySpan<const ProjectAssetSourceDiagnostic> left,
            const containers::ArraySpan<const ProjectAssetSourceDiagnostic> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0u; index < left.Count(); ++index)
                if (!SameDeferredDiagnostic(left[index], right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] static bool SameDeferredCohortIntent(const ProjectAssetCohortIntent& left,
                                                           const ProjectAssetCohortIntent& right) noexcept
        {
            if (!left.IsActive() || !right.IsActive() || !(left.asset == right.asset) ||
                left.sourceIdentity != right.sourceIdentity ||
                left.reviewedAcceptedGeneration != right.reviewedAcceptedGeneration ||
                left.targetMetadataFingerprint != right.targetMetadataFingerprint ||
                left.predecessorOutputs.Size() != right.predecessorOutputs.Size() ||
                left.successorOutputs.Size() != right.successorOutputs.Size())
                return false;
            for (u32 index = 0u; index < left.predecessorOutputs.Size(); ++index)
                if (left.predecessorOutputs[index] != right.predecessorOutputs[index])
                    return false;
            for (u32 index = 0u; index < left.successorOutputs.Size(); ++index)
                if (left.successorOutputs[index] != right.successorOutputs[index])
                    return false;
            return true;
        }

        [[nodiscard]] static bool DeferredOutputsMatchIntent(
            const containers::ArraySpan<const ProjectAssetOutputState> outputs,
            const ProjectAssetCohortIntent& intent) noexcept
        {
            if (!intent.IsActive() || outputs.Count() != intent.successorOutputs.Size())
                return false;
            for (const resources::ResourceReference expected : intent.successorOutputs)
            {
                bool found = false;
                for (const ProjectAssetOutputState& output : outputs)
                    if (output.resource == expected)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool DeferredPlannedImportMatchesPreparedCatchUp(
            const assets::SourceDatabase& candidate, const ProjectAssetAcceptedBaseline& proposed) const noexcept
        {
            const DeferredPlannedImport& deferred = deferredPlannedImport;
            if (!deferred.IsActive() || !deferred.awaitingOwnedAcceptance || accepted.generation != deferred.baseAcceptedGeneration ||
                deferred.baseAcceptedGeneration == ~u64{0} || proposed.generation != deferred.baseAcceptedGeneration + 1u ||
                proposed.changes.Size() != 1u ||
                !SameDeferredDiagnostics({accepted.diagnostics.TypedData(), accepted.diagnostics.Size()},
                                         {proposed.diagnostics.TypedData(), proposed.diagnostics.Size()}))
                return false;

            const ProjectAssetChangeRecord& change = proposed.changes[0];
            if (!(change.asset == deferred.asset) || change.previous.available || !change.current.available ||
                change.current.presence != ProjectAssetPresence::Present ||
                !HasChangeReason(change.reasons, ProjectAssetChangeReason::New) ||
                change.current.sourceIdentity != deferred.sourceIdentity ||
                change.current.physicalPath != deferred.sourceDestination ||
                !change.current.hasSourceContentFingerprint ||
                change.current.sourceContentFingerprint != deferred.sourceFingerprint ||
                !SameDeferredCohortIntent(proposed.cohortIntent, accepted.cohortIntent) ||
                !(proposed.cohortIntent.asset == deferred.asset) ||
                proposed.cohortIntent.sourceIdentity != deferred.sourceIdentity ||
                proposed.cohortIntent.targetMetadataFingerprint != deferred.targetMetadataFingerprint ||
                !DeferredOutputsMatchIntent(change.current.outputs, proposed.cohortIntent))
                return false;

            const ProjectAssetState* const proposedAsset = proposed.LookupAsset(deferred.asset);
            if (proposedAsset == nullptr || proposedAsset->presence != ProjectAssetPresence::Present ||
                proposedAsset->sourceIdentity != deferred.sourceIdentity ||
                proposedAsset->physicalPath != deferred.sourceDestination ||
                !proposedAsset->hasSourceContentFingerprint ||
                proposedAsset->sourceContentFingerprint != deferred.sourceFingerprint ||
                proposedAsset->acceptedGeneration != proposed.generation ||
                !DeferredOutputsMatchIntent(proposedAsset->outputs, proposed.cohortIntent))
                return false;

            const assets::SourceRecord* const source = candidate.LookupAsset(deferred.asset);
            if (source == nullptr || source->sourceIdentity != deferred.sourceIdentity ||
                source->path != deferred.sourceDestination || source->metadataResult != assets::MetadataResult::Success ||
                !(source->metadata.id == deferred.asset))
                return false;
            containers::String canonical;
            return assets::EncodeMetadata(source->metadata, canonical) == assets::MetadataResult::Success &&
                   crypto::Sha256(canonical.AsChar(), canonical.Length()) == deferred.targetMetadataFingerprint;
        }

        [[nodiscard]] bool DeferredPlannedImportMatchesCurrentTruth() const noexcept
        {
            const DeferredPlannedImport& deferred = deferredPlannedImport;
            if (!deferred.IsActive() || deferred.awaitingOwnedAcceptance || !accepted.cohortIntent.IsActive() ||
                deferred.acceptedGeneration == 0u || accepted.generation != deferred.acceptedGeneration ||
                !(accepted.cohortIntent.asset == deferred.asset) ||
                accepted.cohortIntent.sourceIdentity != deferred.sourceIdentity ||
                accepted.cohortIntent.targetMetadataFingerprint != deferred.targetMetadataFingerprint ||
                !CohortIntentMatchesCurrentAcceptedState() || !CohortIntentTargetsCurrentMetadata())
                return false;
            const ProjectAssetState* const state = accepted.LookupAsset(deferred.asset);
            return state != nullptr && state->presence == ProjectAssetPresence::Present &&
                   state->sourceIdentity == deferred.sourceIdentity && state->physicalPath == deferred.sourceDestination &&
                   state->hasSourceContentFingerprint && state->sourceContentFingerprint == deferred.sourceFingerprint &&
                   state->acceptedGeneration == deferred.acceptedGeneration;
        }

        [[nodiscard]] bool CanAttemptDeferredPlannedImportCohort() const noexcept
        {
            if (!deferredPlannedImport.IsActive() || deferredPlannedImport.awaitingOwnedAcceptance ||
                deferredPlannedImport.acceptedGeneration == 0u || accepted.generation != deferredPlannedImport.acceptedGeneration ||
                observation.reconcileActive || observation.initialCatchUpRequired || accumulator.HasRescanRequired() ||
                vddcMaintenance.OwnsAdmission() || looseRetirement.OwnsAdmission() || automaticRecook.IsRecoveryAdmission() ||
                automaticRecook.workRunning || automaticRecook.admissionOwned || automaticRecook.publicationSettlementPending)
                return false;
            if (automaticRecook.HasExplicitCohort())
                return automaticRecook.MatchesExplicitCohort(accepted.cohortIntent);
            return !automaticRecook.HasExplicitActivity();
        }

        void BeginDeferredPlannedImport(const assets::AssetId asset, const resources::ResourcePath sourceIdentity,
                                        const filesystem::AbsolutePath& sourceDestination,
                                        const crypto::Digest256& sourceFingerprint) noexcept
        {
            deferredPlannedImport.Reset();
            deferredPlannedImport.asset = asset;
            deferredPlannedImport.sourceIdentity = sourceIdentity;
            deferredPlannedImport.sourceDestination = sourceDestination;
            deferredPlannedImport.sourceFingerprint = sourceFingerprint;
            deferredPlannedImport.targetMetadataFingerprint = accepted.cohortIntent.targetMetadataFingerprint;
            deferredPlannedImport.baseAcceptedGeneration = accepted.generation;
            deferredPlannedImport.awaitingOwnedAcceptance = true;

            deferredPlannedImportStatus = {};
            deferredPlannedImportStatus.state = ProjectAssetDeferredPlannedImportState::AwaitingReconciliation;
            deferredPlannedImportStatus.asset = asset;
            deferredPlannedImportStatus.result = ProjectAssetPipelineResult::CommittedReconciliationPending;
        }

        void FailDeferredPlannedImportContinuation(
            const ProjectAssetPipelineResult result,
            const ProjectAssetPipelineFailure* const continuationFailure = nullptr) noexcept
        {
            if (deferredPlannedImport.IsActive())
            {
                deferredPlannedImportStatus.state = ProjectAssetDeferredPlannedImportState::ContinuationFailed;
                deferredPlannedImportStatus.asset = deferredPlannedImport.asset;
                deferredPlannedImportStatus.acceptedGeneration = deferredPlannedImport.acceptedGeneration;
                deferredPlannedImportStatus.result = result;
                if (continuationFailure != nullptr)
                {
                    deferredPlannedImportStatus.build = continuationFailure->build;
                    deferredPlannedImportStatus.buildRequestOutput = continuationFailure->buildRequestOutput;
                    deferredPlannedImportStatus.buildRequestOutputIndex = continuationFailure->buildRequestOutputIndex;
                }
            }
            deferredPlannedImport.Reset();
        }

        void CompleteDeferredPlannedImportContinuation() noexcept
        {
            if (deferredPlannedImport.IsActive())
            {
                deferredPlannedImportStatus.state = ProjectAssetDeferredPlannedImportState::CohortQueued;
                deferredPlannedImportStatus.asset = deferredPlannedImport.asset;
                deferredPlannedImportStatus.acceptedGeneration = deferredPlannedImport.acceptedGeneration;
                deferredPlannedImportStatus.result = ProjectAssetPipelineResult::Success;
                deferredPlannedImportStatus.build = assets::ProjectAssetBuildResult::Success;
                deferredPlannedImportStatus.buildRequestOutput = {};
                deferredPlannedImportStatus.buildRequestOutputIndex = ~u32{0};
            }
            deferredPlannedImport.Reset();
        }

        [[nodiscard]] ProjectAssetSnapshotResult RebaseCohortIntentToCurrentAccepted() noexcept
        {
            if (!accepted.cohortIntent.IsActive())
                return ProjectAssetSnapshotResult::Success;
            const ProjectAssetState* const state = accepted.LookupAsset(accepted.cohortIntent.asset);
            if (state == nullptr)
                return accepted.cohortIntent.predecessorOutputs.Empty() ? ClearCohortIntent()
                                                                        : ProjectAssetSnapshotResult::InvalidState;
            if (state->presence != ProjectAssetPresence::Present || state->outputs.Empty() ||
                state->sourceIdentity != accepted.cohortIntent.sourceIdentity)
                return ProjectAssetSnapshotResult::InvalidState;
            const assets::SourceRecord* const source = sources->LookupAsset(state->asset);
            if (source == nullptr || source->sourceIdentity != state->sourceIdentity ||
                source->metadataResult != assets::MetadataResult::Success || !(source->metadata.id == state->asset))
                return ProjectAssetSnapshotResult::InvalidState;

            ProjectAssetCohortIntent rebased;
            rebased.predecessorOutputs.Reserve(accepted.cohortIntent.predecessorOutputs.Size());
            rebased.successorOutputs.Reserve(state->outputs.Count());
            if (rebased.predecessorOutputs.Capacity() < accepted.cohortIntent.predecessorOutputs.Size() ||
                rebased.successorOutputs.Capacity() < state->outputs.Count())
                return ProjectAssetSnapshotResult::OutOfMemory;
            for (const resources::ResourceReference predecessor : accepted.cohortIntent.predecessorOutputs)
                rebased.predecessorOutputs.PushBack(predecessor);
            for (const ProjectAssetOutputState& output : state->outputs)
                rebased.successorOutputs.PushBack(output.resource);
            if (rebased.predecessorOutputs.Size() != accepted.cohortIntent.predecessorOutputs.Size() ||
                rebased.successorOutputs.Size() != state->outputs.Count())
                return ProjectAssetSnapshotResult::OutOfMemory;

            const auto less = [](const resources::ResourceReference left, const resources::ResourceReference right) noexcept
            {
                if (left.GetPath().Id() != right.GetPath().Id())
                    return left.GetPath().Id() < right.GetPath().Id();
                return left.ExpectedType() < right.ExpectedType();
            };
            if (rebased.successorOutputs.Size() > 1u)
                std::sort(rebased.successorOutputs.Begin(), rebased.successorOutputs.End(), less);
            for (u32 index = 1u; index < rebased.successorOutputs.Size(); ++index)
                if (!less(rebased.successorOutputs[index - 1u], rebased.successorOutputs[index]))
                    return ProjectAssetSnapshotResult::InvalidState;

            containers::String canonical;
            if (assets::EncodeMetadata(source->metadata, canonical) != assets::MetadataResult::Success)
                return ProjectAssetSnapshotResult::InvalidState;
            rebased.asset = state->asset;
            rebased.sourceIdentity = state->sourceIdentity;
            rebased.reviewedAcceptedGeneration = accepted.generation;
            rebased.targetMetadataFingerprint = crypto::Sha256(canonical.AsChar(), canonical.Length());
            if (!rebased.IsActive())
                return ProjectAssetSnapshotResult::InvalidState;
            return ReplaceCohortIntent(rebased);
        }

        [[nodiscard]] bool CohortIntentSuccessorVisibleExact() noexcept
        {
            const ProjectAssetCohortIntent& intent = accepted.cohortIntent;
            const assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (!intent.IsActive() || index == nullptr || !CohortIntentMatchesCurrentAcceptedState())
                return false;
            for (const resources::ResourceReference output : intent.successorOutputs)
            {
                const i32 publicationIndexValue = FindPublication(output);
                if (publicationIndexValue < 0)
                    return false;
                const PublishedResource& publication = publications[static_cast<u32>(publicationIndexValue)];
                assets::DependencyRecord current;
                if (index->Find(output, current) != assets::IndexResult::Success || current.output != output ||
                    current.buildFingerprint != publication.buildFingerprint ||
                    current.contentFingerprint != publication.contentFingerprint || publication.physicalPath.Empty())
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool CohortIntentMatchesCurrentSuccessorWitnesses(
            const containers::ArraySpan<const assets::DependencyPublicationWitness> required) noexcept
        {
            if (!accepted.cohortIntent.IsActive() || required.Count() != accepted.cohortIntent.successorOutputs.Size() ||
                !CohortIntentMatchesCurrentAcceptedState())
                return false;
            assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (index == nullptr)
                return false;
            for (const resources::ResourceReference expected : accepted.cohortIntent.successorOutputs)
            {
                const assets::DependencyPublicationWitness* matched = nullptr;
                for (const assets::DependencyPublicationWitness& witness : required)
                    if (witness.output == expected)
                    {
                        matched = &witness;
                        break;
                    }
                if (matched == nullptr)
                    return false;
                assets::DependencyRecord current;
                if (index->Find(expected, current) != assets::IndexResult::Success || current.output != expected ||
                    current.buildFingerprint != matched->buildFingerprint ||
                    current.contentFingerprint != matched->contentFingerprint)
                    return false;
            }
            return true;
        }

        [[nodiscard]] ProjectAssetSnapshotResult ClearCohortIntentBeforeProtectionRelease(
            const containers::ArraySpan<const assets::DependencyPublicationWitness> required) noexcept
        {
            if (!accepted.cohortIntent.IsActive() || !CohortIntentMatchesCurrentSuccessorWitnesses(required))
                return ProjectAssetSnapshotResult::Success;
            return ClearCohortIntent();
        }

        [[nodiscard]] bool CanAcquireAutomaticAdmission() noexcept
        {
            if (observation.reconcileActive || automaticRecook.OwnsAdmission() || vddcMaintenance.OwnsAdmission() ||
                looseRetirement.OwnsAdmission())
                return false;
            // A durable P5 cohort intent is the only authority allowed to advance
            // build/VADI state for the committed successor schema. If cohort
            // admission failed before its transient coordinator state existed,
            // ordinary automatic pending work must remain parked until the exact
            // cohort is explicitly resumed. Otherwise a single successor could
            // replace current VADI or publish outside the atomic generation cutover.
            if (accepted.cohortIntent.IsActive() && !automaticRecook.HasExplicitCohort())
                return false;
            const assets::BuildGraph* const graph = builds.GetBuildGraph();
            const assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (graph == nullptr || index == nullptr || index->HasActiveTransaction() || index->HasChanges() ||
                index->HasRetirementProtections())
                return false;
            const assets::BuildGraphStats stats = graph->GetStats();
            return stats.externalRequests == 0 && stats.activeOperations == 0 && stats.waitingForAdmission == 0 &&
                   stats.queuedRequestBytes == 0 && stats.activeExecutionBytes == 0 && stats.retainedOutputBytes == 0;
        }

        [[nodiscard]] bool CanAcquireAutomaticPublicationAdmission() noexcept
        {
            if (observation.reconcileActive || automaticRecook.OwnsAdmission() || vddcMaintenance.OwnsAdmission() ||
                looseRetirement.OwnsAdmission())
                return false;
            if (accepted.cohortIntent.IsActive() && !automaticRecook.MatchesExplicitCohort(accepted.cohortIntent))
                return false;
            const assets::BuildGraph* const graph = builds.GetBuildGraph();
            const assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (graph == nullptr || index == nullptr || index->HasActiveTransaction() || index->HasChanges())
                return false;
            // A committed P5 retirement journal intentionally outlives the recook
            // transaction until the exact successor cohort is visible. It must not
            // admit any ordinary publication, but it also must not deadlock the
            // marker-matched cohort whose successful publication is the release
            // condition for that journal.
            if (index->HasRetirementProtections() &&
                (!accepted.cohortIntent.IsActive() || !automaticRecook.MatchesExplicitCohort(accepted.cohortIntent)))
                return false;
            const assets::BuildGraphStats stats = graph->GetStats();
            return stats.externalRequests == 0 && stats.activeOperations == 0 && stats.waitingForAdmission == 0 &&
                   stats.queuedRequestBytes == 0 && stats.activeExecutionBytes == 0 && stats.retainedOutputBytes == 0;
        }

        [[nodiscard]] bool CanAcquireVddcMaintenanceAdmission() noexcept
        {
            // P0 deliberately gives maintenance the same project-owner
            // quiescence gate as automatic recook. Reuse it instead of allowing
            // the two admission predicates to drift (notably around dirty VADI
            // state or retained BuildGraph outputs).
            return !accepted.cohortIntent.IsActive() && CanAcquireAutomaticAdmission();
        }

        [[nodiscard]] bool CanAcquireLooseMaintenanceAdmission() noexcept
        {
            return !accepted.cohortIntent.IsActive() && CanAcquireAutomaticAdmission();
        }

        [[nodiscard]] ProjectAssetPipelineResult GenerateAvailableAssetIdentity(
            const assets::SourceImportDescription& description, const assets::SourceImporterId importer,
            assets::AssetId& generated, ProjectAssetPipelineFailure* const failure) noexcept
        {
            generated = {};
            containers::HashMap<resources::ResourceId, u32> generatedOutputIds(memory::pools::Assets::GetInstance());
            generatedOutputIds.Reserve(description.outputs.Size());
            if (generatedOutputIds.Capacity() < description.outputs.Size())
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationFailed;
                    failure->operation.result = assets::AssetOperationResult::LimitExceeded;
                    failure->operation.importer = importer;
                }
                return ProjectAssetPipelineResult::OperationFailed;
            }

            bool available = false;
            for (u32 attempt = 0u; attempt < config.maximumIdentityGenerationAttempts; ++attempt)
            {
                if (!assets::GenerateAssetId(generated))
                    break;
                if (sources->ContainsAssetIdentity(generated))
                    continue;
                generatedOutputIds.Clear();
                bool collision = false;
                for (u32 outputIndex = 0u; outputIndex < description.outputs.Size(); ++outputIndex)
                {
                    const resources::ResourcePath path =
                        assets::GetAssetOutputResourcePath(generated, description.outputs[outputIndex].key);
                    if (!path.IsValid())
                    {
                        collision = true;
                        break;
                    }
                    u32 previousOutput = 0u;
                    if (generatedOutputIds.Find(path.Id(), previousOutput))
                    {
                        collision = true;
                        break;
                    }
                    if (!generatedOutputIds.Insert(path.Id(), outputIndex).IsSuccessful())
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::OperationFailed;
                            failure->operation.result = assets::AssetOperationResult::LimitExceeded;
                            failure->operation.importer = importer;
                        }
                        return ProjectAssetPipelineResult::OperationFailed;
                    }
                    if (sources->ContainsOutputPath(path) || streamer->ContainsResourcePath(path))
                    {
                        collision = true;
                        break;
                    }
                }
                if (!collision)
                {
                    available = true;
                    break;
                }
            }
            if (!available)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::IdentityGenerationFailed)
                                          : ProjectAssetPipelineResult::IdentityGenerationFailed;
            return ProjectAssetPipelineResult::Success;
        }

        void ClearRedo() noexcept
        {
            redo.Clear();
            redoBytes = 0;
        }

        void PushHistory(containers::DynamicArray<AssetHistoryRecord>& history, u64& bytes,
                         AssetHistoryRecord&& record, const u64 maximumBytes) noexcept
        {
            if (record.budgetBytes > maximumBytes)
                return;
            while (!history.Empty() &&
                   (history.Size() >= config.maximumOperationHistoryRecords ||
                    bytes > maximumBytes - record.budgetBytes))
            {
                bytes -= history[0].budgetBytes;
                static_cast<void>(history.RemoveAt(0));
            }
            const u64 recordBytes = record.budgetBytes;
            const u32 previous = history.Size();
            history.PushBack(static_cast<AssetHistoryRecord&&>(record));
            if (history.Size() == previous + 1u)
                bytes += recordBytes;
        }

        void RecordCommittedHistory(AssetHistoryRecord&& record) noexcept
        {
            if (record.files.Empty())
                return;
            ClearRedo();
            PushHistory(undo, undoBytes, static_cast<AssetHistoryRecord&&>(record), config.maximumOperationHistoryBytes);
        }

        [[nodiscard]] i32 FindPublication(const resources::ResourceReference resource) const noexcept
        {
            u32 index = 0;
            if (!resource.IsValid() || !publicationIndex.Find(resource.GetPath().Id(), index) ||
                index >= publications.Size() || publications[index].resource != resource)
                return -1;
            return static_cast<i32>(index);
        }

        [[nodiscard]] const PublishedResource* FindPublication(const resources::ResourceId id) const noexcept
        {
            u32 index = 0;
            if (id == resources::InvalidResourceId || !publicationIndex.Find(id, index) || index >= publications.Size() ||
                publications[index].resource.GetPath().Id() != id)
                return nullptr;
            return &publications[index];
        }

        [[nodiscard]] bool AddPublication(const resources::ResourceReference resource,
                                          const filesystem::AbsolutePath& physicalPath,
                                          const assets::BuildFingerprint& buildFingerprint = {},
                                          const assets::BuildFingerprint& contentFingerprint = {}) noexcept
        {
            if (publications.Size() >= config.maximumPublishedResources || publications.Size() >= publications.Capacity() ||
                publicationIndex.Size() >= publicationIndex.Capacity() || FindPublication(resource) >= 0)
                return false;
            const u32 index = publications.Size();
            publications.PushBack({resource, physicalPath, buildFingerprint, contentFingerprint});
            if (publications.Size() != index + 1u)
                return false;
            if (!publicationIndex.Insert(resource.GetPath().Id(), index).IsSuccessful())
            {
                publications.Resize(index);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool CanAdoptAutomaticPublication(
            const containers::ArraySpan<const resources::ResourceReference> outputs) const noexcept
        {
            u32 additions = 0u;
            for (u32 index = 0; index < outputs.Count(); ++index)
            {
                const resources::ResourceReference output = outputs[index];
                if (!output.IsValid() || !output.IsTyped())
                    return false;
                for (u32 previous = 0; previous < index; ++previous)
                    if (outputs[previous].GetPath() == output.GetPath())
                        return false;
                if (FindPublication(output) < 0)
                    ++additions;
            }
            return publications.Size() + additions <= config.maximumPublishedResources &&
                   publications.Size() + additions <= publications.Capacity() &&
                   publicationIndex.Size() + additions <= publicationIndex.Capacity();
        }

        [[nodiscard]] bool CanAdoptPendingAutomaticPublications() const noexcept
        {
            concurrency::ScopedLock<concurrency::Mutex> lock(automaticRecook.deliveryMutex);
            u32 additions = 0u;
            if (automaticRecook.explicitCohort.IsActive() && !automaticRecook.explicitCohort.publicationBlocked)
            {
                for (const resources::ResourceReference output : automaticRecook.explicitCohort.outputs)
                    if (FindPublication(output) < 0)
                        ++additions;
            }
            else
            {
                for (const assets::DependencyRecord& record : automaticRecook.committedDelivery)
                {
                    if (automaticRecook.explicitCohort.IsActive() && automaticRecook.explicitCohort.Contains(record.output))
                        continue;
                    if (FindPublication(record.output) < 0)
                        ++additions;
                }
            }
            return publications.Size() + additions <= config.maximumPublishedResources &&
                   publications.Size() + additions <= publications.Capacity() &&
                   publicationIndex.Size() + additions <= publicationIndex.Capacity();
        }

        [[nodiscard]] bool AdoptAutomaticPublication(
            const containers::ArraySpan<const resources::ResourceReference> outputs,
            const containers::ArraySpan<const ProjectLoosePublicationInfo> infos) noexcept
        {
            if (outputs.Count() != infos.Count() || !CanAdoptAutomaticPublication(outputs))
                return false;
            for (u32 index = 0; index < outputs.Count(); ++index)
            {
                const resources::ResourceReference output = outputs[index];
                const ProjectLoosePublicationInfo& info = infos[index];
                const i32 existing = FindPublication(output);
                if (existing >= 0)
                {
                    PublishedResource& publication = publications[static_cast<u32>(existing)];
                    publication.physicalPath = info.physicalPath;
                    publication.buildFingerprint = info.buildFingerprint;
                    publication.contentFingerprint = info.contentFingerprint;
                }
                else if (!AddPublication(output, info.physicalPath, info.buildFingerprint, info.contentFingerprint))
                {
                    return false;
                }
                if (!automaticRecook.MarkPublished(output, info))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool RetireSatisfiedPredecessorPublications(
            const containers::ArraySpan<const assets::DependencyPublicationWitness> visible,
            ProjectAssetPipelineFailure* const failure) noexcept
        {
            assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (index == nullptr || visible.Empty())
                return index != nullptr;

            containers::DynamicArray<assets::DependencyRetirementProtectionBatch> batches(memory::pools::Assets::GetInstance());
            containers::DynamicArray<assets::DependencyRecord> records(memory::pools::Assets::GetInstance());
            containers::DynamicArray<assets::DependencyPublicationWitness> successors(memory::pools::Assets::GetInstance());
            const assets::IndexResult captured = index->CaptureRetirementProtectionSnapshot(
                batches, records, successors, config.build.dependencyIndex.maximumRecords,
                config.build.dependencyIndex.maximumSerializedBytes);
            if (captured != assets::IndexResult::Success)
            {
                if (failure != nullptr)
                    failure->index = captured;
                return false;
            }

            containers::DynamicArray<resources::ResourceReference> retirements(memory::pools::Assets::GetInstance());
            containers::DynamicArray<resources::ResourcePath> paths(memory::pools::Assets::GetInstance());
            retirements.Reserve(config.maximumPublishedResources);
            paths.Reserve(config.maximumPublishedResources);
            if (retirements.Capacity() < config.maximumPublishedResources || paths.Capacity() < config.maximumPublishedResources)
            {
                if (failure != nullptr)
                    failure->index = assets::IndexResult::OutOfMemory;
                return false;
            }

            const auto visibleExact = [&](const assets::DependencyPublicationWitness& required) noexcept
            {
                for (const assets::DependencyPublicationWitness& observed : visible)
                    if (observed.output == required.output && observed.buildFingerprint == required.buildFingerprint &&
                        observed.contentFingerprint == required.contentFingerprint)
                        return true;
                return false;
            };
            for (const assets::DependencyRetirementProtectionBatch& batch : batches)
            {
                if (batch.recordOffset > records.Size() || batch.recordCount > records.Size() - batch.recordOffset ||
                    batch.successorOffset > successors.Size() || batch.successorCount == 0u ||
                    batch.successorCount > successors.Size() - batch.successorOffset)
                {
                    if (failure != nullptr)
                        failure->index = assets::IndexResult::Corrupt;
                    return false;
                }
                bool satisfied = true;
                for (u32 successorIndex = 0u; successorIndex < batch.successorCount; ++successorIndex)
                    if (!visibleExact(successors[batch.successorOffset + successorIndex]))
                    {
                        satisfied = false;
                        break;
                    }
                if (!satisfied)
                    continue;

                for (u32 recordIndex = 0u; recordIndex < batch.recordCount; ++recordIndex)
                {
                    const resources::ResourceReference old = records[batch.recordOffset + recordIndex].output;
                    bool successorPath = false;
                    for (u32 successorIndex = 0u; successorIndex < batch.successorCount; ++successorIndex)
                        if (successors[batch.successorOffset + successorIndex].output.GetPath() == old.GetPath())
                        {
                            successorPath = true;
                            break;
                        }
                    if (successorPath || FindPublication(old) < 0)
                        continue;
                    bool duplicate = false;
                    for (const resources::ResourceReference retired : retirements)
                        if (retired.GetPath() == old.GetPath())
                        {
                            duplicate = true;
                            break;
                        }
                    if (duplicate)
                        continue;
                    retirements.PushBack(old);
                    paths.PushBack(old.GetPath());
                    if (retirements.Size() != paths.Size() || retirements.Size() > config.maximumPublishedResources)
                    {
                        if (failure != nullptr)
                            failure->index = assets::IndexResult::OutOfMemory;
                        return false;
                    }
                }
            }

            if (!paths.Empty() && !streamer->UnregisterLooseBatch({paths.TypedData(), paths.Size()}))
                return false;
            for (const resources::ResourceReference old : retirements)
            {
                const i32 publication = FindPublication(old);
                if (publication < 0)
                    continue;
                automaticRecook.MarkUnpublished(old);
                if (!RemovePublicationAt(static_cast<u32>(publication)))
                    return false;
            }
            return true;
        }

        [[nodiscard]] ProjectAssetPipelineResult ReplayRetirementProtections(
            ProjectAssetPipelineFailure* const failure) noexcept
        {
            assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (index == nullptr)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->index = assets::IndexResult::InvalidState;
                }
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }

            containers::DynamicArray<assets::DependencyRetirementProtectionBatch> batches(memory::pools::Assets::GetInstance());
            containers::DynamicArray<assets::DependencyRecord> retained(memory::pools::Assets::GetInstance());
            containers::DynamicArray<assets::DependencyPublicationWitness> successors(memory::pools::Assets::GetInstance());
            const assets::IndexResult captured = index->CaptureRetirementProtectionSnapshot(
                batches, retained, successors, config.build.dependencyIndex.maximumRecords,
                config.build.dependencyIndex.maximumSerializedBytes);
            if (captured != assets::IndexResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->index = captured;
                }
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
            if (batches.Empty())
                return ProjectAssetPipelineResult::Success;

            containers::DynamicArray<resources::ResourceReference> preparedOutputs(memory::pools::Assets::GetInstance());
            containers::DynamicArray<ProjectLoosePublicationInfo> preparedInfos(memory::pools::Assets::GetInstance());
            containers::DynamicArray<u32> dependencyOffsets(memory::pools::Assets::GetInstance());
            containers::DynamicArray<u32> dependencyCounts(memory::pools::Assets::GetInstance());
            containers::DynamicArray<u64> contentCrc64(memory::pools::Assets::GetInstance());
            containers::DynamicArray<streaming::DependencyDescriptor> flattenedDependencies(memory::pools::Assets::GetInstance());
            containers::DynamicArray<streaming::DependencyDescriptor> recordDependencies(memory::pools::Assets::GetInstance());
            containers::DynamicArray<streaming::LooseResourceDescriptor> descriptors(memory::pools::Assets::GetInstance());

            const auto resetPrepared = [&]() noexcept
            {
                preparedOutputs.Clear();
                preparedInfos.Clear();
                dependencyOffsets.Clear();
                dependencyCounts.Clear();
                contentCrc64.Clear();
                flattenedDependencies.Clear();
                descriptors.Clear();
            };
            const auto appendPrepared = [&](const assets::DependencyRecord& record,
                                            ProjectLoosePublicationFailure& publicationFailure) noexcept -> bool
            {
                for (const resources::ResourceReference output : preparedOutputs)
                    if (output.GetPath() == record.output.GetPath())
                        return false;

                ProjectLoosePublicationInfo info;
                u64 crc64 = 0u;
                const ProjectLoosePublicationResult prepared = publisher.PrepareRetainedExact(
                    record, recordDependencies, crc64, &publicationFailure, &info);
                if (prepared != ProjectLoosePublicationResult::Success)
                    return false;

                const u32 outputIndex = preparedOutputs.Size();
                preparedOutputs.PushBack(record.output);
                preparedInfos.PushBack(info);
                dependencyOffsets.PushBack(flattenedDependencies.Size());
                dependencyCounts.PushBack(recordDependencies.Size());
                contentCrc64.PushBack(crc64);
                if (preparedOutputs.Size() != outputIndex + 1u || preparedInfos.Size() != outputIndex + 1u ||
                    dependencyOffsets.Size() != outputIndex + 1u || dependencyCounts.Size() != outputIndex + 1u ||
                    contentCrc64.Size() != outputIndex + 1u)
                    return false;
                for (const streaming::DependencyDescriptor& dependency : recordDependencies)
                {
                    const u32 previous = flattenedDependencies.Size();
                    flattenedDependencies.PushBack(dependency);
                    if (flattenedDependencies.Size() != previous + 1u)
                        return false;
                }
                return true;
            };
            const auto findRetainedExact = [&](const assets::DependencyPublicationWitness& witness) noexcept
                -> const assets::DependencyRecord*
            {
                for (const assets::DependencyRecord& record : retained)
                    if (record.output == witness.output && record.buildFingerprint == witness.buildFingerprint &&
                        record.contentFingerprint == witness.contentFingerprint)
                        return &record;
                return nullptr;
            };
            const auto appendWitness = [&](const assets::DependencyPublicationWitness& witness,
                                           ProjectLoosePublicationFailure& publicationFailure) noexcept -> bool
            {
                assets::DependencyRecord current;
                const assets::IndexResult found = index->Find(witness.output, current);
                if (found == assets::IndexResult::Success && current.output == witness.output &&
                    current.buildFingerprint == witness.buildFingerprint && current.contentFingerprint == witness.contentFingerprint)
                    return appendPrepared(current, publicationFailure);
                const assets::DependencyRecord* const historical = findRetainedExact(witness);
                if (historical == nullptr)
                {
                    publicationFailure = {};
                    publicationFailure.result = ProjectLoosePublicationResult::RecordNotFound;
                    publicationFailure.index = found;
                    return false;
                }
                return appendPrepared(*historical, publicationFailure);
            };
            const auto commitPrepared = [&](ProjectLoosePublicationFailure& publicationFailure) noexcept -> bool
            {
                if (preparedOutputs.Empty() || preparedOutputs.Size() != preparedInfos.Size() ||
                    preparedOutputs.Size() != dependencyOffsets.Size() || preparedOutputs.Size() != dependencyCounts.Size() ||
                    preparedOutputs.Size() != contentCrc64.Size())
                    return false;
                descriptors.Reserve(preparedOutputs.Size());
                if (descriptors.Capacity() < preparedOutputs.Size())
                {
                    publicationFailure = {};
                    publicationFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                    return false;
                }
                for (u32 outputIndex = 0u; outputIndex < preparedOutputs.Size(); ++outputIndex)
                {
                    const u32 offset = dependencyOffsets[outputIndex];
                    const u32 count = dependencyCounts[outputIndex];
                    if (offset > flattenedDependencies.Size() || count > flattenedDependencies.Size() - offset)
                        return false;
                    const u32 previous = descriptors.Size();
                    descriptors.PushBack({preparedOutputs[outputIndex], preparedInfos[outputIndex].physicalPath,
                                          {flattenedDependencies.TypedData() + offset, count}, contentCrc64[outputIndex],
                                          preparedInfos[outputIndex].priority});
                    if (descriptors.Size() != previous + 1u)
                    {
                        publicationFailure = {};
                        publicationFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                        return false;
                    }
                }
                streaming::PreparedLooseBatch visibility;
                if (!publisher.PrepareVisibility({descriptors.TypedData(), descriptors.Size()}, visibility) ||
                    !publisher.CommitPreparedVisibility(visibility))
                {
                    publicationFailure = {};
                    publicationFailure.result = ProjectLoosePublicationResult::StreamingPublicationFailure;
                    return false;
                }
                return true;
            };
            const auto setPendingPublication = [&](const ProjectLoosePublicationFailure& publicationFailure) noexcept
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->publication = publicationFailure;
                }
            };
            const auto markCurrentPublication = [&](const resources::ResourceReference output,
                                                    const ProjectLoosePublicationInfo& info) noexcept -> bool
            {
                assets::DependencyRecord current;
                if (index->Find(output, current) != assets::IndexResult::Success || current.output != output ||
                    current.buildFingerprint != info.buildFingerprint || current.contentFingerprint != info.contentFingerprint)
                    return true;
                return automaticRecook.MarkPublished(output, info);
            };

            for (const assets::DependencyRetirementProtectionBatch& batch : batches)
            {
                if (batch.recordOffset > retained.Size() || batch.recordCount > retained.Size() - batch.recordOffset ||
                    batch.successorOffset > successors.Size() || batch.successorCount == 0u ||
                    batch.successorCount > successors.Size() - batch.successorOffset)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        failure->index = assets::IndexResult::Corrupt;
                    }
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                const containers::ArraySpan<const assets::DependencyPublicationWitness> required{
                    successors.TypedData() + batch.successorOffset, batch.successorCount};

                bool exactAlreadyOwned = true;
                for (const assets::DependencyPublicationWitness& witness : required)
                {
                    const i32 published = FindPublication(witness.output);
                    if (published < 0)
                    {
                        exactAlreadyOwned = false;
                        break;
                    }
                    const PublishedResource& current = publications[static_cast<u32>(published)];
                    if (current.buildFingerprint != witness.buildFingerprint || current.contentFingerprint != witness.contentFingerprint ||
                        current.physicalPath.Empty())
                    {
                        exactAlreadyOwned = false;
                        break;
                    }
                }
                if (exactAlreadyOwned)
                {
                    for (const assets::DependencyPublicationWitness& witness : required)
                    {
                        const PublishedResource& current = publications[static_cast<u32>(FindPublication(witness.output))];
                        ProjectLoosePublicationInfo info;
                        info.physicalPath = current.physicalPath;
                        info.buildFingerprint = current.buildFingerprint;
                        info.contentFingerprint = current.contentFingerprint;
                        if (!markCurrentPublication(witness.output, info))
                        {
                            ProjectLoosePublicationFailure bookkeepingFailure;
                            bookkeepingFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                            setPendingPublication(bookkeepingFailure);
                            return ProjectAssetPipelineResult::CommittedReconciliationPending;
                        }
                    }
                    if (!RetireSatisfiedPredecessorPublications(required, failure))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    const ProjectAssetSnapshotResult clearedIntent = ClearCohortIntentBeforeProtectionRelease(required);
                    if (clearedIntent != ProjectAssetSnapshotResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                            failure->acceptedSnapshot = clearedIntent;
                        }
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    const assets::IndexResult released = index->ReleaseSatisfiedRetirementProtections(required);
                    if (released != assets::IndexResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                            failure->index = released;
                        }
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    continue;
                }

                // Prepare the complete exact successor set before changing any
                // streamer-visible predecessor. Intermediate historical successors
                // are resolved from later journal rows when current VADI has moved on.
                resetPrepared();
                ProjectLoosePublicationFailure successorFailure;
                bool successorReady = true;
                for (const assets::DependencyPublicationWitness& witness : required)
                    if (!appendWitness(witness, successorFailure))
                    {
                        successorReady = false;
                        break;
                    }

                if (successorReady && commitPrepared(successorFailure))
                {
                    u32 additions = 0u;
                    for (const resources::ResourceReference output : preparedOutputs)
                        if (FindPublication(output) < 0)
                            ++additions;

                    containers::DynamicArray<resources::ResourceReference> ownerRetirements(memory::pools::Assets::GetInstance());
                    for (u32 recordIndex = 0u; recordIndex < batch.recordCount; ++recordIndex)
                    {
                        const resources::ResourceReference old = retained[batch.recordOffset + recordIndex].output;
                        bool remains = false;
                        for (const assets::DependencyPublicationWitness& witness : required)
                            if (witness.output.GetPath() == old.GetPath())
                            {
                                remains = true;
                                break;
                            }
                        if (!remains && FindPublication(old) >= 0)
                            ownerRetirements.PushBack(old);
                    }
                    const u32 retainedOwnerCount = publications.Size() - ownerRetirements.Size();
                    if (retainedOwnerCount + additions > config.maximumPublishedResources ||
                        retainedOwnerCount + additions > publications.Capacity() ||
                        publicationIndex.Size() - ownerRetirements.Size() + additions > publicationIndex.Capacity())
                    {
                        successorFailure = {};
                        successorFailure.result = ProjectLoosePublicationResult::LimitExceeded;
                        setPendingPublication(successorFailure);
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }

                    if (publications.Size() + additions > config.maximumPublishedResources ||
                        publications.Size() + additions > publications.Capacity() ||
                        publicationIndex.Size() + additions > publicationIndex.Capacity())
                    {
                        containers::DynamicArray<resources::ResourcePath> paths(memory::pools::Assets::GetInstance());
                        paths.Reserve(ownerRetirements.Size());
                        if (paths.Capacity() < ownerRetirements.Size())
                        {
                            successorFailure = {};
                            successorFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                            setPendingPublication(successorFailure);
                            return ProjectAssetPipelineResult::CommittedReconciliationPending;
                        }
                        for (const resources::ResourceReference old : ownerRetirements)
                            paths.PushBack(old.GetPath());
                        if (!paths.Empty() && !streamer->UnregisterLooseBatch({paths.TypedData(), paths.Size()}))
                        {
                            successorFailure = {};
                            successorFailure.result = ProjectLoosePublicationResult::StreamingPublicationFailure;
                            setPendingPublication(successorFailure);
                            return ProjectAssetPipelineResult::CommittedReconciliationPending;
                        }
                        for (const resources::ResourceReference old : ownerRetirements)
                        {
                            const i32 publication = FindPublication(old);
                            if (publication >= 0)
                            {
                                automaticRecook.MarkUnpublished(old);
                                if (!RemovePublicationAt(static_cast<u32>(publication)))
                                {
                                    successorFailure = {};
                                    successorFailure.result = ProjectLoosePublicationResult::InvalidState;
                                    setPendingPublication(successorFailure);
                                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                                }
                            }
                        }
                    }

                    for (u32 outputIndex = 0u; outputIndex < preparedOutputs.Size(); ++outputIndex)
                    {
                        const resources::ResourceReference output = preparedOutputs[outputIndex];
                        const ProjectLoosePublicationInfo& info = preparedInfos[outputIndex];
                        const i32 existing = FindPublication(output);
                        if (existing >= 0)
                        {
                            PublishedResource& publication = publications[static_cast<u32>(existing)];
                            publication.physicalPath = info.physicalPath;
                            publication.buildFingerprint = info.buildFingerprint;
                            publication.contentFingerprint = info.contentFingerprint;
                        }
                        else if (!AddPublication(output, info.physicalPath, info.buildFingerprint, info.contentFingerprint))
                        {
                            successorFailure = {};
                            successorFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                            setPendingPublication(successorFailure);
                            return ProjectAssetPipelineResult::CommittedReconciliationPending;
                        }
                        if (!markCurrentPublication(output, info))
                        {
                            successorFailure = {};
                            successorFailure.result = ProjectLoosePublicationResult::OutOfMemory;
                            setPendingPublication(successorFailure);
                            return ProjectAssetPipelineResult::CommittedReconciliationPending;
                        }
                    }
                    if (!RetireSatisfiedPredecessorPublications(required, failure))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    const ProjectAssetSnapshotResult clearedIntent = ClearCohortIntentBeforeProtectionRelease(required);
                    if (clearedIntent != ProjectAssetSnapshotResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                            failure->acceptedSnapshot = clearedIntent;
                        }
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    const assets::IndexResult released = index->ReleaseSatisfiedRetirementProtections(required);
                    if (released != assets::IndexResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                            failure->index = released;
                        }
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    continue;
                }

                // Successor preparation/visibility could not complete. Restore the
                // exact predecessor cohort from the durable journal before returning
                // so restart never falls through to per-output current reconciliation.
                resetPrepared();
                ProjectLoosePublicationFailure predecessorFailure;
                bool predecessorReady = true;
                for (u32 recordIndex = 0u; recordIndex < batch.recordCount; ++recordIndex)
                    if (!appendPrepared(retained[batch.recordOffset + recordIndex], predecessorFailure))
                    {
                        predecessorReady = false;
                        break;
                    }
                for (u32 successorIndex = 0u; predecessorReady && successorIndex < required.Count(); ++successorIndex)
                {
                    const assets::DependencyPublicationWitness& witness = required[successorIndex];
                    if (!witness.predecessorPresent)
                        continue;
                    bool retainedSamePath = false;
                    for (u32 recordIndex = 0u; recordIndex < batch.recordCount; ++recordIndex)
                        if (retained[batch.recordOffset + recordIndex].output.GetPath() == witness.output.GetPath())
                        {
                            retainedSamePath = true;
                            break;
                        }
                    if (!retainedSamePath && !appendWitness(witness, predecessorFailure))
                        predecessorReady = false;
                }
                if (!predecessorReady || preparedOutputs.Empty())
                {
                    setPendingPublication(predecessorReady ? successorFailure : predecessorFailure);
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }

                u32 predecessorAdditions = 0u;
                for (const resources::ResourceReference output : preparedOutputs)
                    if (FindPublication(output) < 0)
                        ++predecessorAdditions;
                if (publications.Size() + predecessorAdditions > config.maximumPublishedResources ||
                    publications.Size() + predecessorAdditions > publications.Capacity() ||
                    publicationIndex.Size() + predecessorAdditions > publicationIndex.Capacity() ||
                    !commitPrepared(predecessorFailure))
                {
                    setPendingPublication(predecessorFailure);
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                for (u32 outputIndex = 0u; outputIndex < preparedOutputs.Size(); ++outputIndex)
                {
                    const resources::ResourceReference output = preparedOutputs[outputIndex];
                    const ProjectLoosePublicationInfo& info = preparedInfos[outputIndex];
                    const i32 existing = FindPublication(output);
                    if (existing >= 0)
                    {
                        PublishedResource& publication = publications[static_cast<u32>(existing)];
                        publication.physicalPath = info.physicalPath;
                        publication.buildFingerprint = info.buildFingerprint;
                        publication.contentFingerprint = info.contentFingerprint;
                    }
                    else if (!AddPublication(output, info.physicalPath, info.buildFingerprint, info.contentFingerprint))
                    {
                        setPendingPublication(predecessorFailure);
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                }
                setPendingPublication(successorFailure);
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
            return ProjectAssetPipelineResult::Success;
        }

        [[nodiscard]] ProjectAssetPipelineResult SettleCompletedAutomaticPublication(
            ProjectAssetPipelineFailure* const failure, const bool surfacePublicationFailure) noexcept
        {
            AutomaticRecookState& automatic = automaticRecook;
            if (!automatic.publicationSettlementPending)
                return ProjectAssetPipelineResult::Success;

            automatic.admissionOwned = false;
            if (automatic.workerPublicationSucceeded)
            {
                const containers::ArraySpan<const resources::ResourceReference> outputs{
                    automatic.workerPublicationOutputs.TypedData(), automatic.workerPublicationOutputs.Size()};
                const containers::ArraySpan<const ProjectLoosePublicationInfo> infos{
                    automatic.workerPublicationInfos.TypedData(), automatic.workerPublicationInfos.Size()};
                if (!outputs.Empty() && !AdoptAutomaticPublication(outputs, infos))
                {
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                containers::DynamicArray<assets::DependencyPublicationWitness> visible(memory::pools::Assets::GetInstance());
                visible.Resize(outputs.Count());
                if (visible.Size() != outputs.Count())
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        failure->index = assets::IndexResult::OutOfMemory;
                    }
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                for (u32 index = 0u; index < outputs.Count(); ++index)
                    visible[index] = {outputs[index], infos[index].buildFingerprint, infos[index].contentFingerprint};
                assets::DependencyIndex* const index = builds.GetDependencyIndex();
                if (!RetireSatisfiedPredecessorPublications({visible.TypedData(), visible.Size()}, failure))
                {
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                if (automatic.workerPublishedExplicitCohort && automatic.explicitCohort.IsActive() &&
                    accepted.cohortIntent.IsActive())
                {
                    if (!(accepted.cohortIntent.asset == automatic.explicitCohort.asset) ||
                        !CohortIntentMatchesCurrentSuccessorWitnesses({visible.TypedData(), visible.Size()}))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                    const ProjectAssetSnapshotResult cleared = ClearCohortIntent();
                    if (cleared != ProjectAssetSnapshotResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                            failure->acceptedSnapshot = cleared;
                        }
                        return ProjectAssetPipelineResult::CommittedReconciliationPending;
                    }
                }
                const assets::IndexResult released = index != nullptr
                                                         ? index->ReleaseSatisfiedRetirementProtections(
                                                               {visible.TypedData(), visible.Size()})
                                                         : assets::IndexResult::InvalidState;
                if (released != assets::IndexResult::Success)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                        failure->index = released;
                    }
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                looseRetirement.Settle({automatic.workerRetiredLooseCaptures.TypedData(),
                                        automatic.workerRetiredLooseCaptures.Size()});
                automatic.workerRetiredLooseCaptures.Clear();
                if (automatic.workerPublishedExplicitCohort && automatic.explicitCohort.IsActive())
                    automatic.explicitCohort.Reset();
                automatic.workerPublishedExplicitCohort = false;
                automatic.RefreshDeliveryStatus(automatic.status);
                automatic.workerFinished = false;
                automatic.publicationSettlementPending = false;
                return ProjectAssetPipelineResult::Success;
            }

            if (automatic.workerRepairRequiredOutput.IsValid() && automatic.workerRepairRequiredOutput.IsTyped())
            {
                const resources::ResourceReference repairOutput = automatic.workerRepairRequiredOutput;
                if (!automatic.QueueRepair(repairOutput, accepted.generation))
                {
                    automatic.MarkRepairRecoveryRequired(accepted.generation);
                    automatic.publicationSettlementPending = false;
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                        failure->publication = automatic.workerPublicationFailure;
                    }
                    return ProjectAssetPipelineResult::BuildRequestFailed;
                }
                for (u32 index = 0; index < automatic.workerPublicationOutputs.Size(); ++index)
                    if (automatic.workerPublicationOutputs[index] == repairOutput)
                    {
                        automatic.MarkPublicationFailed(repairOutput, automatic.workerPublicationInfos[index],
                                                        ProjectAssetDeliveryReason::CacheRepairRequired);
                        break;
                    }
                automatic.RefreshDeliveryStatus(automatic.status);
                automatic.workerRepairRequiredOutput = {};
                automatic.workerRetiredLooseCaptures.Clear();
                automatic.workerPublishedExplicitCohort = false;
                automatic.publicationSettlementPending = false;
                // Repair is now ordinary pending automatic-recook work. Do not
                // surface this validation miss as a terminal publication failure
                // and do not retry publication until the existing graph path has
                // restored exact VDDC/VADI coherence.
                return ProjectAssetPipelineResult::Success;
            }

            const ProjectAssetDeliveryReason reason = automatic.workerPublicationStale
                                                          ? ProjectAssetDeliveryReason::PublicationFreshnessRejected
                                                          : ProjectAssetDeliveryReason::PublicationFailed;
            if (automatic.workerPublishedExplicitCohort && automatic.explicitCohort.IsActive())
                automatic.MarkPublishableDeliveriesFailed(reason);
            else if (automatic.workerPublicationOutputs.Empty())
                automatic.MarkPublishableDeliveriesFailed(reason, false);
            else
            {
                for (u32 index = 0; index < automatic.workerPublicationOutputs.Size(); ++index)
                    automatic.MarkPublicationFailed(automatic.workerPublicationOutputs[index], automatic.workerPublicationInfos[index], reason);
            }
            automatic.RefreshDeliveryStatus(automatic.status);
            automatic.workerRetiredLooseCaptures.Clear();
            automatic.workerPublishedExplicitCohort = false;
            automatic.publicationSettlementPending = false;
            if (surfacePublicationFailure)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::PublicationFailed;
                    failure->publication = automatic.workerPublicationFailure;
                }
                return ProjectAssetPipelineResult::PublicationFailed;
            }
            return ProjectAssetPipelineResult::Success;
        }

        [[nodiscard]] bool RemovePublicationAt(const u32 index) noexcept
        {
            if (index >= publications.Size())
                return false;
            const resources::ResourceId removed = publications[index].resource.GetPath().Id();
            const u32 last = publications.Size() - 1u;
            const resources::ResourceId moved = index != last ? publications[last].resource.GetPath().Id() : resources::InvalidResourceId;
            if (!publicationIndex.Remove(removed).IsSuccessful() || !publications.RemoveAtReorder(index).IsSuccessful())
                return false;
            if (index < publications.Size())
            {
                u32* const movedIndex = publicationIndex.FindPtr(moved);
                if (movedIndex == nullptr)
                    return false;
                *movedIndex = index;
            }
            return true;
        }

        [[nodiscard]] ProjectLoosePublicationResult ValidateCurrentExact(
            const resources::ResourceReference resource, ProjectLoosePublicationFailure* const failure,
            ProjectLoosePublicationInfo* const publication = nullptr) noexcept
        {
            if (failure != nullptr)
                *failure = {};
            if (publication != nullptr)
                *publication = {};
            assets::DependencyIndex* const index = builds.GetDependencyIndex();
            if (index == nullptr || !resource.IsValid() || !resource.IsTyped())
            {
                if (failure != nullptr)
                    failure->result = ProjectLoosePublicationResult::InvalidState;
                return ProjectLoosePublicationResult::InvalidState;
            }
            assets::DependencyRecord record;
            const assets::IndexResult found = index->Find(resource, record);
            if (found == assets::IndexResult::NotFound)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectLoosePublicationResult::RecordNotFound;
                    failure->index = found;
                }
                return ProjectLoosePublicationResult::RecordNotFound;
            }
            if (!assets::IsSuccess(found))
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectLoosePublicationResult::IndexFailure;
                    failure->index = found;
                }
                return ProjectLoosePublicationResult::IndexFailure;
            }
            if (publication != nullptr)
            {
                publication->buildFingerprint = record.buildFingerprint;
                publication->contentFingerprint = record.contentFingerprint;
            }
            return publisher.ValidateExact(record, failure);
        }

        [[nodiscard]] ProjectAssetPipelineResult Publish(const resources::ResourceReference resource,
                                                         ProjectAssetPipelineFailure* const failure) noexcept
        {
            if (!automaticRecook.CanTrackPublication(resource))
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                return ProjectAssetPipelineResult::ReconciliationFailed;
            }
            const i32 existing = FindPublication(resource);
            if (existing < 0)
            {
                if (publications.Size() >= config.maximumPublishedResources)
                {
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                    return ProjectAssetPipelineResult::ReconciliationFailed;
                }
                if (publications.Size() >= publications.Capacity())
                    publications.Reserve(publications.Size() + 1u);
                if (publicationIndex.Size() >= publicationIndex.Capacity())
                    publicationIndex.Reserve(publicationIndex.Size() + 1u);
                if (publications.Size() >= publications.Capacity() || publicationIndex.Size() >= publicationIndex.Capacity())
                {
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                    return ProjectAssetPipelineResult::ReconciliationFailed;
                }
            }
            ProjectLoosePublicationInfo info;
            ProjectLoosePublicationFailure publicationFailure;
            RetiredLooseCapture retiredCapture;
            u64 retiredReservation = 0u;
            if (existing >= 0)
            {
                ProjectLoosePublicationInfo exact;
                const ProjectLoosePublicationResult validated = ValidateCurrentExact(resource, &publicationFailure, &exact);
                if (validated != ProjectLoosePublicationResult::Success)
                {
                    const bool repairRequired = IsCacheRepairFailure(publicationFailure);
                    automaticRecook.MarkPublicationFailed(
                        resource, exact, repairRequired ? ProjectAssetDeliveryReason::CacheRepairRequired
                                                       : ProjectAssetDeliveryReason::PublicationFailed);
                    if (repairRequired && !automaticRecook.QueueRepair(resource, accepted.generation))
                        automaticRecook.MarkRepairRecoveryRequired(accepted.generation);
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::PublicationFailed;
                        failure->publication = publicationFailure;
                    }
                    return ProjectAssetPipelineResult::PublicationFailed;
                }
                const PublishedResource& previous = publications[static_cast<u32>(existing)];
                if (previous.buildFingerprint != exact.buildFingerprint)
                {
                    if (!automaticRecook.CaptureRetiredLoose(previous, retiredCapture) ||
                        !looseRetirement.ReserveBatch(*files, {&retiredCapture, 1u}, retiredReservation))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::Busy;
                        return ProjectAssetPipelineResult::Busy;
                    }
                }
            }
            if (publisher.Publish(resource, &publicationFailure, &info) != ProjectLoosePublicationResult::Success)
            {
                if (retiredReservation != 0u)
                    looseRetirement.RollbackReservation({&retiredCapture, 1u}, retiredReservation);
                const bool repairRequired = IsCacheRepairFailure(publicationFailure);
                automaticRecook.MarkPublicationFailed(
                    resource, info, repairRequired ? ProjectAssetDeliveryReason::CacheRepairRequired
                                                   : ProjectAssetDeliveryReason::PublicationFailed);
                if (repairRequired && !automaticRecook.QueueRepair(resource, accepted.generation))
                    automaticRecook.MarkRepairRecoveryRequired(accepted.generation);
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::PublicationFailed;
                    failure->publication = publicationFailure;
                }
                return ProjectAssetPipelineResult::PublicationFailed;
            }

            if (existing >= 0)
            {
                if (retiredCapture.IsValid())
                    static_cast<void>(looseRetirement.CommitReservation({&retiredCapture, 1u}, retiredReservation));
                PublishedResource& previous = publications[static_cast<u32>(existing)];
                previous.physicalPath = info.physicalPath;
                previous.buildFingerprint = info.buildFingerprint;
                previous.contentFingerprint = info.contentFingerprint;
                if (!automaticRecook.MarkPublished(resource, info))
                {
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                    return ProjectAssetPipelineResult::ReconciliationFailed;
                }
                if (retiredCapture.IsValid())
                    looseRetirement.Settle({&retiredCapture, 1u});
                return ProjectAssetPipelineResult::Success;
            }
            if (!AddPublication(resource, info.physicalPath, info.buildFingerprint, info.contentFingerprint) ||
                !automaticRecook.MarkPublished(resource, info))
            {
                // PublishLoose has already made the resource visible. Roll that
                // back if bookkeeping cannot adopt it.
                static_cast<void>(streamer->UnregisterLoose(resource.GetPath()));
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                return ProjectAssetPipelineResult::ReconciliationFailed;
            }
            return ProjectAssetPipelineResult::Success;
        }

        [[nodiscard]] ProjectAssetPipelineResult AdvanceReconcile(ReconcileState& state, u32 budget,
                                                                  ProjectAssetPipelineFailure* const failure) noexcept
        {
            while (budget != 0u && state.stage != ReconcileStage::Complete)
            {
                if (state.stage == ReconcileStage::Publish)
                {
                    if (state.publishIndex == state.desired.Size())
                    {
                        state.removeIndex = 0u;
                        state.removeCommitIndex = 0u;
                        state.removeResources.Clear();
                        state.removePaths.Clear();
                        state.stage = ReconcileStage::Remove;
                        continue;
                    }

                    const resources::ResourceReference resource = state.desired[state.publishIndex++];
                    --budget;
                    if (FindPublication(resource) >= 0)
                    {
                        ProjectLoosePublicationFailure validationFailure;
                        ProjectLoosePublicationInfo validationInfo;
                        const ProjectLoosePublicationResult validation =
                            ValidateCurrentExact(resource, &validationFailure, &validationInfo);
                        if (validation == ProjectLoosePublicationResult::Success)
                            ++state.report.alreadyPublished;
                        else if (validation == ProjectLoosePublicationResult::RecordNotFound)
                            ++state.report.missingBuildRecords;
                        else if (IsCacheRepairFailure(validationFailure))
                        {
                            automaticRecook.MarkPublicationFailed(resource, validationInfo,
                                                                  ProjectAssetDeliveryReason::CacheRepairRequired);
                            if (!automaticRecook.QueueRepair(resource, accepted.generation))
                                automaticRecook.MarkRepairRecoveryRequired(accepted.generation);
                            ++state.report.repairRequiredRecords;
                        }
                        else
                            ++state.report.unavailableBuildRecords;
                        continue;
                    }
                    ProjectAssetPipelineFailure publicationFailure;
                    const ProjectAssetPipelineResult published = Publish(resource, &publicationFailure);
                    if (published == ProjectAssetPipelineResult::Success)
                        ++state.report.restored;
                    else if (publicationFailure.publication.result == ProjectLoosePublicationResult::RecordNotFound)
                        ++state.report.missingBuildRecords;
                    else if (IsCacheRepairFailure(publicationFailure.publication))
                        ++state.report.repairRequiredRecords;
                    else
                        ++state.report.unavailableBuildRecords;
                    continue;
                }

                if (state.stage == ReconcileStage::Remove)
                {
                    if (state.removeIndex >= publications.Size())
                    {
                        if (state.removePaths.Empty())
                        {
                            state.stage = ReconcileStage::Prune;
                            continue;
                        }
                        if (!streamer->UnregisterLooseBatch({state.removePaths.TypedData(), state.removePaths.Size()}))
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::Busy;
                            return ProjectAssetPipelineResult::Busy;
                        }
                        state.removeCommitIndex = 0u;
                        state.stage = ReconcileStage::RemoveCommit;
                        continue;
                    }

                    --budget;
                    const PublishedResource& publication = publications[state.removeIndex++];
                    u8 desired = 0;
                    if (state.desiredLookup.Find(publication.resource.GetPath().Id(), desired))
                        continue;
                    if (automaticRecook.ProtectsTopologyPredecessor(publication.resource) ||
                        CohortIntentProtectsTopologyPredecessor(publication.resource))
                        continue;
                    const u32 previousResources = state.removeResources.Size();
                    const u32 previousPaths = state.removePaths.Size();
                    state.removeResources.PushBack(publication.resource);
                    state.removePaths.PushBack(publication.resource.GetPath());
                    if (state.removeResources.Size() != previousResources + 1u || state.removePaths.Size() != previousPaths + 1u)
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    continue;
                }

                if (state.stage == ReconcileStage::RemoveCommit)
                {
                    if (state.removeCommitIndex >= state.removeResources.Size())
                    {
                        state.stage = ReconcileStage::Prune;
                        continue;
                    }
                    --budget;
                    const resources::ResourceReference resource = state.removeResources[state.removeCommitIndex++];
                    const i32 publication = FindPublication(resource);
                    if (publication < 0 || !RemovePublicationAt(static_cast<u32>(publication)))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    automaticRecook.MarkUnpublished(resource);
                    ++state.report.removedPublications;
                    continue;
                }

                if (state.pruneIndex == state.looseFiles.Size())
                {
                    state.stage = ReconcileStage::Complete;
                    continue;
                }

                const filesystem::AbsolutePath path = state.looseFiles[state.pruneIndex++];
                --budget;
                resources::ResourceId id = resources::InvalidResourceId;
                if (!ReadResourceId(filesystem::paths::GetFileName(path), id))
                    continue;
                const PublishedResource* const current = FindPublication(id);
                if (current == nullptr || path == current->physicalPath)
                    continue;
                // No owner-proven generation lease is available here. Keep the
                // physical generation and report it as deferred; E2.5 owns broad
                // disk pruning after reader/resident reachability is known.
                ++state.report.staleFilesDeferred;
            }
            return state.stage == ReconcileStage::Complete ? ProjectAssetPipelineResult::Success : ProjectAssetPipelineResult::Pending;
        }

        [[nodiscard]] ProjectAssetPipelineResult AdvanceObservationReconcile(ReconcileState& state, u32 budget,
                                                                             ProjectAssetPipelineFailure* const failure) noexcept
        {
            ObservationWorker* const worker = observation.worker;
            if (worker == nullptr)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                          : ProjectAssetPipelineResult::ReconciliationFailed;
            while (budget != 0u && state.stage != ReconcileStage::Complete)
            {
                if (observation.operationPending)
                {
                    if (!worker->IsCompleted())
                        return ProjectAssetPipelineResult::Pending;
                    const ObservationWorker::Command command = worker->CurrentCommand();
                    observation.operationPending = false;
                    --budget;
                    ++state.operationsCompleted;

                    if (command == ObservationWorker::Command::Publish)
                    {
                        const i32 publishedIndex = FindPublication(worker->resource);
                        if (publishedIndex < 0 || state.stage != ReconcileStage::Publish ||
                            state.publishIndex >= state.desired.Size() || state.desired[state.publishIndex] != worker->resource)
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                        if (worker->operationSucceeded)
                        {
                            PublishedResource& publication = publications[static_cast<u32>(publishedIndex)];
                            publication.physicalPath = static_cast<filesystem::AbsolutePath&&>(worker->publicationInfo.physicalPath);
                            publication.buildFingerprint = worker->publicationInfo.buildFingerprint;
                            publication.contentFingerprint = worker->publicationInfo.contentFingerprint;
                            if (!automaticRecook.MarkPublished(worker->resource, worker->publicationInfo))
                            {
                                if (failure != nullptr)
                                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                                return ProjectAssetPipelineResult::ReconciliationFailed;
                            }
                            ++state.report.restored;
                        }
                        else
                        {
                            const bool repairRequired = IsCacheRepairFailure(worker->publicationFailure);
                            if (repairRequired)
                            {
                                automaticRecook.MarkPublicationFailed(worker->resource, worker->publicationInfo,
                                                                      ProjectAssetDeliveryReason::CacheRepairRequired);
                                if (!automaticRecook.QueueRepair(worker->resource, accepted.generation))
                                    automaticRecook.MarkRepairRecoveryRequired(accepted.generation);
                            }
                            if (!RemovePublicationAt(static_cast<u32>(publishedIndex)))
                            {
                                if (failure != nullptr)
                                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                                return ProjectAssetPipelineResult::ReconciliationFailed;
                            }
                            if (worker->publicationFailure.result == ProjectLoosePublicationResult::RecordNotFound)
                                ++state.report.missingBuildRecords;
                            else if (repairRequired)
                                ++state.report.repairRequiredRecords;
                            else
                                ++state.report.unavailableBuildRecords;
                        }
                        ++state.publishIndex;
                    }
                    else if (command == ObservationWorker::Command::ValidateExact)
                    {
                        const i32 publishedIndex = FindPublication(worker->resource);
                        if (publishedIndex < 0 || state.stage != ReconcileStage::Publish ||
                            state.publishIndex >= state.desired.Size() || state.desired[state.publishIndex] != worker->resource)
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                        if (worker->operationSucceeded)
                            ++state.report.alreadyPublished;
                        else if (worker->publicationResult == ProjectLoosePublicationResult::RecordNotFound)
                            ++state.report.missingBuildRecords;
                        else if (IsCacheRepairFailure(worker->publicationFailure))
                        {
                            automaticRecook.MarkPublicationFailed(worker->resource, worker->publicationInfo,
                                                                  ProjectAssetDeliveryReason::CacheRepairRequired);
                            if (!automaticRecook.QueueRepair(worker->resource, accepted.generation))
                                automaticRecook.MarkRepairRecoveryRequired(accepted.generation);
                            ++state.report.repairRequiredRecords;
                        }
                        else
                            ++state.report.unavailableBuildRecords;
                        ++state.publishIndex;
                    }
                    else if (command == ObservationWorker::Command::RemoveBatch)
                    {
                        if (!worker->operationSucceeded)
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::Busy;
                            return ProjectAssetPipelineResult::Busy;
                        }
                        if (state.stage != ReconcileStage::Remove || state.removePaths.Empty())
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                        state.removeCommitIndex = 0u;
                        state.stage = ReconcileStage::RemoveCommit;
                    }
                    else
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    continue;
                }

                if (state.stage == ReconcileStage::Publish)
                {
                    if (state.publishIndex == state.desired.Size())
                    {
                        state.removeIndex = 0u;
                        state.removeCommitIndex = 0u;
                        state.removeResources.Clear();
                        state.removePaths.Clear();
                        state.stage = ReconcileStage::Remove;
                        continue;
                    }

                    const resources::ResourceReference resource = state.desired[state.publishIndex];
                    if (FindPublication(resource) >= 0)
                    {
                        if (!worker->Submit(ObservationWorker::Command::ValidateExact, resource))
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                        observation.operationPending = true;
                        return ProjectAssetPipelineResult::Pending;
                    }

                    // Reserve the O(1) owner bookkeeping before the worker makes
                    // the resource visible. Observation startup preallocates the
                    // backing containers, so no project-sized growth can occur in
                    // this owner tick.
                    if (!automaticRecook.CanTrackPublication(resource) || !AddPublication(resource, {}))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    if (!worker->Submit(ObservationWorker::Command::Publish, resource))
                    {
                        const i32 placeholder = FindPublication(resource);
                        if (placeholder >= 0)
                            static_cast<void>(RemovePublicationAt(static_cast<u32>(placeholder)));
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    observation.operationPending = true;
                    return ProjectAssetPipelineResult::Pending;
                }

                if (state.stage == ReconcileStage::Remove)
                {
                    if (state.removeIndex >= publications.Size())
                    {
                        if (state.removePaths.Empty())
                        {
                            state.stage = ReconcileStage::Prune;
                            continue;
                        }
                        if (!worker->SubmitRemoveBatch({state.removePaths.TypedData(), state.removePaths.Size()}))
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                        observation.operationPending = true;
                        return ProjectAssetPipelineResult::Pending;
                    }
                    const PublishedResource& publication = publications[state.removeIndex++];
                    u8 desired = 0;
                    if (!state.desiredLookup.Find(publication.resource.GetPath().Id(), desired) &&
                        !automaticRecook.ProtectsTopologyPredecessor(publication.resource) &&
                        !CohortIntentProtectsTopologyPredecessor(publication.resource))
                    {
                        const u32 previousResources = state.removeResources.Size();
                        const u32 previousPaths = state.removePaths.Size();
                        state.removeResources.PushBack(publication.resource);
                        state.removePaths.PushBack(publication.resource.GetPath());
                        if (state.removeResources.Size() != previousResources + 1u || state.removePaths.Size() != previousPaths + 1u)
                        {
                            if (failure != nullptr)
                                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                            return ProjectAssetPipelineResult::ReconciliationFailed;
                        }
                    }
                    --budget;
                    ++state.operationsCompleted;
                    continue;
                }

                if (state.stage == ReconcileStage::RemoveCommit)
                {
                    if (state.removeCommitIndex >= state.removeResources.Size())
                    {
                        state.stage = ReconcileStage::Prune;
                        continue;
                    }
                    const resources::ResourceReference resource = state.removeResources[state.removeCommitIndex++];
                    const i32 publication = FindPublication(resource);
                    if (publication < 0 || !RemovePublicationAt(static_cast<u32>(publication)))
                    {
                        if (failure != nullptr)
                            failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        return ProjectAssetPipelineResult::ReconciliationFailed;
                    }
                    automaticRecook.MarkUnpublished(resource);
                    ++state.report.removedPublications;
                    --budget;
                    ++state.operationsCompleted;
                    continue;
                }

                if (state.pruneIndex == state.looseFiles.Size())
                {
                    state.stage = ReconcileStage::Complete;
                    continue;
                }

                const filesystem::AbsolutePath& path = state.looseFiles[state.pruneIndex];
                resources::ResourceId id = resources::InvalidResourceId;
                if (!ReadResourceId(filesystem::paths::GetFileName(path), id))
                {
                    ++state.pruneIndex;
                    --budget;
                    ++state.operationsCompleted;
                    continue;
                }
                const PublishedResource* const current = FindPublication(id);
                if (current == nullptr || path == current->physicalPath)
                {
                    ++state.pruneIndex;
                    --budget;
                    ++state.operationsCompleted;
                    continue;
                }
                ++state.pruneIndex;
                --budget;
                ++state.operationsCompleted;
                ++state.report.staleFilesDeferred;
                continue;
            }
            return state.stage == ReconcileStage::Complete ? ProjectAssetPipelineResult::Success : ProjectAssetPipelineResult::Pending;
        }

        [[nodiscard]] bool StartObservationWorker() noexcept
        {
            if (observation.worker != nullptr)
                return false;
            assets::DependencyIndex* const dependencyIndex = builds.GetDependencyIndex();
            if (dependencyIndex == nullptr)
                return false;
            ObservationWorker* const worker = VANGUARD_NEW(ObservationWorker)(
                *files, publisher, *streamer, builds, *dependencyIndex, automaticRecook,
                sources->GetRoots(), builds.GetImporters(), config.build.target, config.build.sourceData,
                accepted, config.build.dependencyIndexRoot, config.publication.looseRoot, config.operationJournalPath,
                config.maximumPublishedResources, config.maximumLooseFiles);
            if (worker == nullptr)
                return false;
            if (!worker->IsConfigured())
            {
                VANGUARD_DELETE(worker);
                return false;
            }
            worker->InitThread();
            if (!worker->IsValid())
            {
                VANGUARD_DELETE(worker);
                return false;
            }
            observation.worker = worker;
            return true;
        }

        [[nodiscard]] bool StartObservationReconcile() noexcept
        {
            if (observation.worker == nullptr || observation.rescanPending || observation.catalogCommitted || observation.operationPending ||
                !observation.worker->Submit(ObservationWorker::Command::Rescan))
                return false;
            observation.rescanPending = true;
            return true;
        }

        [[nodiscard]] bool DrainObservationReconcile() noexcept
        {
            ObservationWorker* const worker = observation.worker;
            if (worker != nullptr)
            {
                if (!worker->IsCompleted())
                    worker->WaitForCompletion();
                if (observation.operationPending)
                {
                    const ObservationWorker::Command command = worker->CurrentCommand();
                    if (command == ObservationWorker::Command::Publish)
                    {
                        const i32 publication = FindPublication(worker->resource);
                        if (publication >= 0)
                        {
                            if (worker->operationSucceeded)
                            {
                                PublishedResource& published = publications[static_cast<u32>(publication)];
                                published.physicalPath = worker->publicationInfo.physicalPath;
                                published.buildFingerprint = worker->publicationInfo.buildFingerprint;
                                published.contentFingerprint = worker->publicationInfo.contentFingerprint;
                                static_cast<void>(automaticRecook.MarkPublished(worker->resource, worker->publicationInfo));
                            }
                            else
                                static_cast<void>(RemovePublicationAt(static_cast<u32>(publication)));
                        }
                    }
                    else if (command == ObservationWorker::Command::RemoveBatch && worker->operationSucceeded)
                    {
                        for (const resources::ResourceReference resource : observation.reconcile.removeResources)
                        {
                            const i32 publication = FindPublication(resource);
                            if (publication >= 0)
                            {
                                automaticRecook.MarkUnpublished(resource);
                                static_cast<void>(RemovePublicationAt(static_cast<u32>(publication)));
                            }
                        }
                    }
                    observation.operationPending = false;
                }

                if (!worker->Submit(ObservationWorker::Command::Stop))
                    return false;
                worker->WaitForCompletion();
                if (worker->IsValid())
                    worker->JoinThread();
                VANGUARD_DELETE(worker);
                observation.worker = nullptr;
            }
            observation.rescanPending = false;
            observation.catalogCommitted = false;
            observation.snapshotFailureRetirePending = false;
            observation.snapshotFailure = ProjectAssetSnapshotResult::NotChecked;
            observation.reconcile.Reset();
            return true;
        }
    };

    const char* ToString(const ProjectAssetPipelineResult result) noexcept
    {
        switch (result)
        {
        case ProjectAssetPipelineResult::Success: return "Success";
        case ProjectAssetPipelineResult::Pending: return "Pending";
        case ProjectAssetPipelineResult::InvalidArgument: return "InvalidArgument";
        case ProjectAssetPipelineResult::InvalidState: return "InvalidState";
        case ProjectAssetPipelineResult::Busy: return "Busy";
        case ProjectAssetPipelineResult::BuildInitializationFailed: return "BuildInitializationFailed";
        case ProjectAssetPipelineResult::BuildRequestFailed: return "BuildRequestFailed";
        case ProjectAssetPipelineResult::IndexSaveFailed: return "IndexSaveFailed";
        case ProjectAssetPipelineResult::PublicationFailed: return "PublicationFailed";
        case ProjectAssetPipelineResult::ReconciliationFailed: return "ReconciliationFailed";
        case ProjectAssetPipelineResult::SnapshotLoadFailed: return "SnapshotLoadFailed";
        case ProjectAssetPipelineResult::SnapshotSaveFailed: return "SnapshotSaveFailed";
        case ProjectAssetPipelineResult::OperationInitializationFailed: return "OperationInitializationFailed";
        case ProjectAssetPipelineResult::OperationFailed: return "OperationFailed";
        case ProjectAssetPipelineResult::StalePlan: return "StalePlan";
        case ProjectAssetPipelineResult::IdentityGenerationFailed: return "IdentityGenerationFailed";
        case ProjectAssetPipelineResult::HistoryLimitExceeded: return "HistoryLimitExceeded";
        case ProjectAssetPipelineResult::UndoUnavailable: return "UndoUnavailable";
        case ProjectAssetPipelineResult::RedoUnavailable: return "RedoUnavailable";
        case ProjectAssetPipelineResult::CommittedCleanupPending: return "CommittedCleanupPending";
        case ProjectAssetPipelineResult::CommittedReconciliationPending: return "CommittedReconciliationPending";
        case ProjectAssetPipelineResult::ShutdownFailed: return "ShutdownFailed";
        case ProjectAssetPipelineResult::ObservationUnavailable: return "ObservationUnavailable";
        case ProjectAssetPipelineResult::ObservationAlreadyActive: return "ObservationAlreadyActive";
        case ProjectAssetPipelineResult::ObservationNotActive: return "ObservationNotActive";
        case ProjectAssetPipelineResult::CacheMaintenanceFailed: return "CacheMaintenanceFailed";
        case ProjectAssetPipelineResult::LooseMaintenanceFailed: return "LooseMaintenanceFailed";
        }
        return "Unknown";
    }

    ProjectAssetRequest::ProjectAssetRequest(ProjectAssetPipeline& owner, const resources::ResourceReference resource,
                                             assets::GraphRequest&& request) noexcept
        : m_owner(&owner), m_resource(resource), m_request(static_cast<assets::GraphRequest&&>(request))
    {
    }

    ProjectAssetRequest::ProjectAssetRequest(ProjectAssetRequest&& other) noexcept
        : m_owner(other.m_owner), m_resource(other.m_resource), m_request(static_cast<assets::GraphRequest&&>(other.m_request)),
          m_published(other.m_published)
    {
        other.m_owner = nullptr;
        other.m_resource = {};
        other.m_published = false;
    }

    ProjectAssetRequest& ProjectAssetRequest::operator=(ProjectAssetRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner = other.m_owner;
            m_resource = other.m_resource;
            m_request = static_cast<assets::GraphRequest&&>(other.m_request);
            m_published = other.m_published;
            other.m_owner = nullptr;
            other.m_resource = {};
            other.m_published = false;
        }
        return *this;
    }

    ProjectAssetRequest::~ProjectAssetRequest() { Reset(); }
    bool ProjectAssetRequest::IsValid() const noexcept { return m_owner != nullptr && m_request.IsValid(); }
    bool ProjectAssetRequest::HasFinished() const noexcept { return IsValid() && m_request.HasFinished(); }
    assets::BuildState ProjectAssetRequest::GetBuildState() const noexcept { return m_request.GetStatus(); }
    resources::ResourceReference ProjectAssetRequest::GetResource() const noexcept { return m_resource; }
    void ProjectAssetRequest::Wait() const noexcept { m_request.Wait(); }
    ProjectAssetPipelineResult ProjectAssetRequest::Poll(ProjectAssetPipelineFailure* const failure) noexcept
    {
        if (m_owner != nullptr)
            return m_owner->Poll(*this, failure);
        ClearFailure(failure);
        if (failure != nullptr)
            failure->result = ProjectAssetPipelineResult::InvalidState;
        return ProjectAssetPipelineResult::InvalidState;
    }
    bool ProjectAssetRequest::Cancel() noexcept { return m_request.Cancel(); }
    void ProjectAssetRequest::Reset() noexcept
    {
        m_request.Reset();
        m_owner = nullptr;
        m_resource = {};
        m_published = false;
    }

    ProjectAssetPipeline::~ProjectAssetPipeline() { static_cast<void>(Shutdown()); }

    ProjectAssetPipelineResult ProjectAssetPipeline::Initialize(filesystem::Manager& files, assets::SourceDatabase& sources,
                                                                streaming::ResourceStreamer& streamer,
                                                                const containers::ArraySpan<const assets::CompilerDescriptor> compilers,
                                                                const containers::ArraySpan<const assets::SourceImporterDescriptor> importers,
                                                                const ProjectAssetPipelineConfig& config, ProjectAssetRestoreReport* const restore,
                                                                ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (restore != nullptr)
            *restore = {};
        if (m_impl != nullptr || !streamer.IsInitialized() || !config.IsValid() || compilers.Empty() || importers.Empty())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument) : ProjectAssetPipelineResult::InvalidArgument;
        for (u32 index = 0; index < compilers.Count(); ++index)
        {
            if (!compilers[index].IsValid())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                          : ProjectAssetPipelineResult::InvalidArgument;
            for (u32 previous = 0; previous < index; ++previous)
                if (compilers[index].id == compilers[previous].id)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                              : ProjectAssetPipelineResult::InvalidArgument;
        }
        for (u32 index = 0; index < importers.Count(); ++index)
        {
            if (!importers[index].IsValid())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                          : ProjectAssetPipelineResult::InvalidArgument;
            for (u32 previous = 0; previous < index; ++previous)
                if (importers[index].id == importers[previous].id)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                              : ProjectAssetPipelineResult::InvalidArgument;
        }
        if (!files.CreatePath(config.publication.looseRoot))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument) : ProjectAssetPipelineResult::InvalidArgument;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Assets)(files, sources, streamer, config);
        if (implementation == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildInitializationFailed)
                                      : ProjectAssetPipelineResult::BuildInitializationFailed;
        implementation->undo.Reserve(config.maximumOperationHistoryRecords);
        implementation->redo.Reserve(config.maximumOperationHistoryRecords);
        implementation->publications.Reserve(config.maximumPublishedResources);
        implementation->publicationIndex.Reserve(config.maximumPublishedResources);
        const bool looseRetirementStorageReserved =
            implementation->looseRetirement.ReserveStorage(config.maximumPublishedResources);
        const bool observationRemovalStorageReserved =
            implementation->observation.reconcile.ReserveRemovalCapacity(config.maximumPublishedResources);
        if (implementation->undo.Capacity() < config.maximumOperationHistoryRecords ||
            implementation->redo.Capacity() < config.maximumOperationHistoryRecords ||
            implementation->publications.Capacity() < config.maximumPublishedResources ||
            implementation->publicationIndex.Capacity() < config.maximumPublishedResources || !looseRetirementStorageReserved ||
            !observationRemovalStorageReserved)
        {
            VANGUARD_DELETE(implementation);
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildInitializationFailed)
                                      : ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        containers::DynamicArray<filesystem::AbsolutePath> assertionRoots(memory::pools::Assets::GetInstance());
        assertionRoots.Reserve(sources.GetRoots().Count());
        if (assertionRoots.Capacity() < sources.GetRoots().Count())
        {
            VANGUARD_DELETE(implementation);
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::OperationInitializationFailed)
                                      : ProjectAssetPipelineResult::OperationInitializationFailed;
        }
        for (const assets::SourceRoot& root : sources.GetRoots())
        {
            assertionRoots.PushBack(root.directory);
            if (!root.readOnly)
                implementation->writableRoots.PushBack(root.directory);
        }
        assets::AssetFileRecoveryReport recovery;
        if (!implementation->writableRoots.Empty())
        {
            if (!config.operationJournalPath.IsFilePath())
            {
                VANGUARD_DELETE(implementation);
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::OperationInitializationFailed)
                                          : ProjectAssetPipelineResult::OperationInitializationFailed;
            }
            const assets::AssetFileTransactionConfig transactionConfig{
                config.operationJournalPath,
                {implementation->writableRoots.TypedData(), implementation->writableRoots.Size()},
                {assertionRoots.TypedData(), assertionRoots.Size()},
                config.maximumOperationEntries,
                config.maximumOperationFileBytes,
                config.maximumOperationStagedBytes,
                config.maximumOperationJournalBytes};
            const assets::AssetFileTransactionResult transactionInitialized =
                implementation->transaction.Initialize(files, transactionConfig, &recovery);
            if (transactionInitialized != assets::AssetFileTransactionResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationInitializationFailed;
                    failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                    failure->operation.transaction = transactionInitialized;
                }
                VANGUARD_DELETE(implementation);
                return ProjectAssetPipelineResult::OperationInitializationFailed;
            }
        }
        if (recovery.journalFound)
        {
            const containers::ArraySpan<const assets::SourceRoot> roots = sources.GetRoots();
            const assets::SourceDatabaseResult recovered = sources.Rescan(files, roots);
            if (recovered != assets::SourceDatabaseResult::Success)
            {
                static_cast<void>(implementation->transaction.Shutdown());
                VANGUARD_DELETE(implementation);
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationInitializationFailed;
                    failure->sources = recovered;
                }
                return ProjectAssetPipelineResult::OperationInitializationFailed;
            }
        }
        // P4/P5 restart baseline. VPAS now also owns the durable pre-VADI cohort
        // cutover intent. A missing image is an explicit bootstrap; every present
        // but unusable image must fail closed because silently rebuilding from the
        // current catalog could discard last-known-good predecessor evidence.
        const ProjectAssetSnapshotResult snapshotLoad = LoadProjectAssetAcceptedSnapshot(
            files, config.build.dependencyIndexRoot, config.changeState, implementation->accepted);
        if (restore != nullptr)
            restore->acceptedSnapshot = snapshotLoad;
        if (!IsRecoverableSnapshotLoad(snapshotLoad))
        {
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::SnapshotLoadFailed;
                failure->acceptedSnapshot = snapshotLoad;
            }
            return ProjectAssetPipelineResult::SnapshotLoadFailed;
        }

        assets::SourceDatabase startupCandidate;
        ProjectAssetAcceptedBaseline startupProposed(config.changeState);
        Impl::ReconcileState startupState;
        if (!startupState.ReserveRemovalCapacity(config.maximumPublishedResources))
        {
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildInitializationFailed)
                                      : ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        filesystem::ScanResult startupScanFailure = filesystem::ScanResult::Success;
        const containers::ArraySpan<const assets::SourceRoot> startupRoots = sources.GetRoots();
        const assets::SourceDatabaseResult prepared = PrepareProjectAssetCandidate(
            files, startupRoots, importers, config.build.target, config.build.sourceData, config.operationJournalPath,
            implementation->accepted, config.maximumPublishedResources, config.publication.looseRoot,
            config.maximumLooseFiles, startupCandidate, startupProposed, startupState.desired,
            startupState.desiredLookup, startupState.looseFiles, &startupScanFailure);
        if (prepared != assets::SourceDatabaseResult::Success)
        {
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                failure->sources = prepared;
            }
            return ProjectAssetPipelineResult::ReconciliationFailed;
        }
        // Initialize build/publication infrastructure before the durable accepted
        // commit. These objects retain the SourceDatabase owner address, not its
        // current storage, so the later O(1) catalog swap is visible to them.
        // Any initialization failure here is therefore still a true precommit
        // failure and cannot strand a newer VPAS/catalog baseline.
        assets::ProjectAssetBuildConfig startupBuildConfig = config.build;
        if (implementation->accepted.cohortIntent.IsActive() &&
            !implementation->accepted.cohortIntent.predecessorOutputs.Empty())
        {
            // A predecessor-bearing VPAS intent still depends on exact historical
            // VADI rows for restart publication and VDDC protection. Never let
            // index self-healing erase corrupt/incompatible/temp-only evidence;
            // preserve it and fail initialization until an operator restores it.
            startupBuildConfig.dependencyIndex.allowDestructiveRecovery = false;
        }
        const assets::ProjectAssetBuildResult initialized =
            implementation->builds.Initialize(files, sources, compilers, importers, startupBuildConfig);
        if (initialized != assets::ProjectAssetBuildResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildInitializationFailed;
                failure->build = initialized;
            }
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            return ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        if (!implementation->automaticRecook.Initialize(implementation->builds, sources, implementation->accumulator))
        {
            static_cast<void>(implementation->builds.Shutdown());
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::BuildInitializationFailed;
            return ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        if (!implementation->publisher.Initialize(*implementation->builds.GetDependencyIndex(), streamer, config.publication))
        {
            static_cast<void>(implementation->automaticRecook.Shutdown());
            static_cast<void>(implementation->builds.Shutdown());
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::BuildInitializationFailed;
            return ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        if (!streamer.RegisterManagedLooseRoot(config.publication.looseRoot))
        {
            static_cast<void>(implementation->publisher.Shutdown());
            static_cast<void>(implementation->automaticRecook.Shutdown());
            static_cast<void>(implementation->builds.Shutdown());
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::BuildInitializationFailed;
            return ProjectAssetPipelineResult::BuildInitializationFailed;
        }
        implementation->managedLooseRootRegistered = true;
        const ProjectAssetSnapshotResult startupPersisted =
            PersistProjectAssetCandidate(files, config.build.dependencyIndexRoot, implementation->accepted, startupProposed);
        if (startupPersisted != ProjectAssetSnapshotResult::Success)
        {
            static_cast<void>(streamer.UnregisterManagedLooseRoot(config.publication.looseRoot));
            implementation->managedLooseRootRegistered = false;
            static_cast<void>(implementation->publisher.Shutdown());
            static_cast<void>(implementation->automaticRecook.Shutdown());
            static_cast<void>(implementation->builds.Shutdown());
            static_cast<void>(implementation->transaction.Shutdown());
            VANGUARD_DELETE(implementation);
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                failure->acceptedSnapshot = startupPersisted;
            }
            return ProjectAssetPipelineResult::SnapshotSaveFailed;
        }
        AcceptProjectAssetCandidate(sources, implementation->accepted, startupCandidate, startupProposed);
        // VPAS is historical comparison evidence, not an automatic dirty queue.
        // Start P2 consumption at the accepted startup generation rather than
        // replaying bootstrap/restart changes. P3 owns the one-time current
        // source + committed-VADI recovery planning pass.
        implementation->automaticRecook.SetEnabled(config.automaticRecook.enabled, implementation->accepted.generation);
        m_impl = implementation;

        // A write-ahead cohort marker can outlive either side of the source WAL
        // boundary. If recovery explicitly rolled the source transaction back,
        // rebase the marker to the recovered accepted metadata while preserving
        // its original predecessor LKG set. A first-import rollback has no
        // predecessor and simply clears the marker. Without a proven rollback,
        // a metadata mismatch is ambiguous and must remain fail-closed.
        if (m_impl->accepted.cohortIntent.IsActive() && !m_impl->CohortIntentTargetsCurrentMetadata())
        {
            if (!recovery.rolledBack)
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
            const ProjectAssetSnapshotResult rebased = m_impl->RebaseCohortIntentToCurrentAccepted();
            if (rebased != ProjectAssetSnapshotResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->acceptedSnapshot = rebased;
                }
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
        }

        ProjectAssetRestoreReport reconciledReport;
        const ProjectAssetPipelineResult reconciled = Reconcile(&reconciledReport, failure);
        if (reconciled != ProjectAssetPipelineResult::Success)
        {
            if (restore != nullptr)
            {
                *restore = reconciledReport;
                restore->acceptedSnapshot = snapshotLoad;
            }
            // VPAS + source/accepted state are already committed. Publication
            // reconciliation is cleanup after that boundary and may be retried;
            // never report it as if initialization rolled the acceptance back.
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (m_impl->accepted.cohortIntent.IsActive())
        {
            if (!m_impl->CohortIntentTargetsCurrentMetadata() || !m_impl->CohortIntentMatchesCurrentAcceptedState())
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
            const ProjectAssetState* const cohortAsset = m_impl->accepted.LookupAsset(m_impl->accepted.cohortIntent.asset);
            if (cohortAsset == nullptr)
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
            const ProjectAssetCohortDeliveryRequest cohortRequest{
                cohortAsset->asset, cohortAsset->acceptedGeneration,
                {m_impl->accepted.cohortIntent.successorOutputs.TypedData(),
                 m_impl->accepted.cohortIntent.successorOutputs.Size()}};
            const ProjectAssetPipelineResult resumed = RequestAssetCohortDelivery(cohortRequest, failure);
            if (resumed != ProjectAssetPipelineResult::Success)
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
        }
        if (restore != nullptr)
        {
            *restore = reconciledReport;
            restore->acceptedSnapshot = snapshotLoad;
        }
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::Shutdown(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return ProjectAssetPipelineResult::Success;
        if (QuiesceDeferredPlannedImport(failure) != ProjectAssetPipelineResult::Success)
            return ProjectAssetPipelineResult::ShutdownFailed;
        // Stop new observation first. The source's Stop() must join every
        // producer thread, so no callback can reach this Impl after this point.
        // Unlike DisableObservation, full shutdown may cancel a partially
        // reconciled observation pass: DrainObservationReconcile adopts any
        // completed in-flight publication mutation, stops/joins the worker, and
        // the normal shutdown path below unregisters every publication still
        // owned by this pipeline.
        if (m_impl->observation.running)
        {
            if (m_impl->observation.source == nullptr || !m_impl->observation.source->Stop())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
            if (!m_impl->DrainObservationReconcile())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
            m_impl->observation.source = nullptr;
            m_impl->observation.running = false;
            m_impl->observation.config = {};
            m_impl->observation.reconcileActive = false;
            m_impl->observation.reconcileGeneration = 0;
            m_impl->observation.reconcile.Reset();
        }
        // Full shutdown may block to cancel/drain automatic work. Ordinary
        // automatic owner ticks never wait; teardown must close the recooker
        // before the shared graph/index owners are destroyed. Keep the stopped
        // observer's monotonic hint generation intact until this drain finishes:
        // a P4 validator may still be fencing a pre-commit completion against it.
        const Impl::AutomaticRecookState::WorkKind drainedAutomaticWork = m_impl->automaticRecook.Drain();
        if (drainedAutomaticWork == Impl::AutomaticRecookState::WorkKind::Publish ||
            m_impl->automaticRecook.publicationSettlementPending)
        {
            const ProjectAssetPipelineResult settled = m_impl->SettleCompletedAutomaticPublication(failure, false);
            if (settled != ProjectAssetPipelineResult::Success)
                return settled;
        }
        // P3 maintenance owns no durable metadata and each exact deletion is
        // individually authority-admitted, so shutdown can safely request
        // cancellation and join the Jobs task. Do this before publisher/build
        // teardown because the worker borrows the shared cache authority.
        m_impl->vddcMaintenance.Drain();
        // P4 loose maintenance borrows both filesystem and streamer lifetime
        // authorities. Cancel/join it before descriptor/root teardown so an
        // exact-generation retire operation cannot race shutdown ownership.
        m_impl->looseRetirement.Drain();
        if (!m_impl->CanReconcileSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        // Remove every owned loose descriptor as one all-or-nothing streamer
        // mutation. A live StreamLoad makes this return Busy without changing any
        // descriptor, so shutdown remains retryable and package fallback cannot be
        // exposed for only a prefix of the project publication set.
        containers::DynamicArray<resources::ResourcePath> shutdownPublications(memory::pools::Assets::GetInstance());
        shutdownPublications.Reserve(m_impl->publications.Size());
        if (shutdownPublications.Capacity() < m_impl->publications.Size())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ShutdownFailed)
                                      : ProjectAssetPipelineResult::ShutdownFailed;
        for (const PublishedResource& publication : m_impl->publications)
            shutdownPublications.PushBack(publication.resource.GetPath());
        if (!shutdownPublications.Empty() &&
            !m_impl->streamer->UnregisterLooseBatch({shutdownPublications.TypedData(), shutdownPublications.Size()}))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        while (!m_impl->publications.Empty())
        {
            const u32 index = m_impl->publications.Size() - 1u;
            m_impl->automaticRecook.MarkUnpublished(m_impl->publications[index].resource);
            if (!m_impl->RemovePublicationAt(index))
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ShutdownFailed)
                                          : ProjectAssetPipelineResult::ShutdownFailed;
        }

        if (m_impl->managedLooseRootRegistered)
        {
            if (!m_impl->streamer->UnregisterManagedLooseRoot(m_impl->config.publication.looseRoot))
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
            m_impl->managedLooseRootRegistered = false;
        }

        m_impl->accumulator.Reset();
        const assets::AssetFileTransactionResult transactionShutdown = m_impl->transaction.Shutdown();
        if (transactionShutdown != assets::AssetFileTransactionResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::ShutdownFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.transaction = transactionShutdown;
            }
            return ProjectAssetPipelineResult::ShutdownFailed;
        }
        if (!m_impl->publisher.Shutdown())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ShutdownFailed) : ProjectAssetPipelineResult::ShutdownFailed;
        if (!m_impl->automaticRecook.Shutdown())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ShutdownFailed) : ProjectAssetPipelineResult::ShutdownFailed;
        const assets::ProjectAssetBuildResult shutdown = m_impl->builds.Shutdown();
        if (shutdown != assets::ProjectAssetBuildResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::ShutdownFailed;
                failure->build = shutdown;
            }
            return ProjectAssetPipelineResult::ShutdownFailed;
        }
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return ProjectAssetPipelineResult::Success;
    }

    bool ProjectAssetPipeline::IsInitialized() const noexcept { return m_impl != nullptr; }

    ProjectAssetPipelineResult ProjectAssetPipeline::EnableAutomaticRecook(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (m_impl->deferredPlannedImportPrepare.HasWork() || m_impl->automaticRecook.HasExplicitActivity())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->automaticRecook.IsPolicyEnabled())
            return ProjectAssetPipelineResult::Success;
        if (m_impl->observation.reconcileActive || m_impl->vddcMaintenance.OwnsAdmission() ||
            m_impl->looseRetirement.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        m_impl->automaticRecook.SetEnabled(true, m_impl->accepted.generation);
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::DisableAutomaticRecook(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (m_impl->automaticRecook.HasExplicitActivity())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (!m_impl->automaticRecook.IsPolicyEnabled())
            return ProjectAssetPipelineResult::Success;
        // Once publication visibility has committed, owner settlement is part of
        // that in-flight operation and cannot be abandoned by a policy change.
        // A merely retained publishable delivery is different: disabling the
        // automatic policy intentionally leaves that retained E2.4 delivery for
        // later re-enable/shutdown handling, matching the established lifecycle.
        if (m_impl->automaticRecook.publicationSettlementPending)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->automaticRecook.OwnsAdmission() || m_impl->observation.reconcileActive ||
            m_impl->vddcMaintenance.OwnsAdmission() || m_impl->looseRetirement.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        m_impl->automaticRecook.SetEnabled(false);
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RetryAutomaticRecook(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->automaticRecook.IsEnabled())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (m_impl->deferredPlannedImportPrepare.HasWork() || m_impl->vddcMaintenance.OwnsAdmission() ||
            m_impl->looseRetirement.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        ProjectAssetAutomaticRecookStatus& status = m_impl->automaticRecook.status;
        if (status.state != ProjectAssetAutomaticRecookState::Failed && status.state != ProjectAssetAutomaticRecookState::RecoveryRequired)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (m_impl->automaticRecook.pollDispatchFailed && m_impl->automaticRecook.OwnsAdmission())
        {
            status.retryRequested = true;
            status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::ExplicitRetry;
            status.cancellationRequested = true;
            status.state = ProjectAssetAutomaticRecookState::Cancelling;
            m_impl->automaticRecook.pollDispatchRetryRequested = true;
            return ProjectAssetPipelineResult::Success;
        }
        if (m_impl->automaticRecook.explicitCohort.IsActive())
        {
            const ProjectAssetState* const cohortAsset =
                m_impl->accepted.LookupAsset(m_impl->automaticRecook.explicitCohort.asset);
            if (cohortAsset == nullptr ||
                cohortAsset->acceptedGeneration != m_impl->automaticRecook.explicitCohort.acceptedGeneration)
                m_impl->automaticRecook.DropExplicitCohortDelivery();
        }
        status.retryRequested = true;
        status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::ExplicitRetry;
        status.recoveryRequired = false;
        m_impl->automaticRecook.recoveryRequested = true;
        status.state = ProjectAssetAutomaticRecookState::Pending;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RetryAutomaticPublication(const resources::ResourceReference output,
                                                                                ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->automaticRecook.IsEnabled() || !output.IsValid() || !output.IsTyped())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (m_impl->deferredPlannedImportPrepare.HasWork() || m_impl->automaticRecook.OwnsAdmission() ||
            m_impl->observation.reconcileActive ||
            m_impl->vddcMaintenance.OwnsAdmission() || m_impl->looseRetirement.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->automaticRecook.explicitCohort.IsActive())
        {
            const ProjectAssetState* const cohortAsset =
                m_impl->accepted.LookupAsset(m_impl->automaticRecook.explicitCohort.asset);
            if (cohortAsset == nullptr ||
                cohortAsset->acceptedGeneration != m_impl->automaticRecook.explicitCohort.acceptedGeneration)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                          : ProjectAssetPipelineResult::InvalidState;
        }
        if (!m_impl->automaticRecook.RetryPublication(output))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RequestAssetReimport(const assets::AssetId asset,
                                                                          ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!asset.IsValid())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        if (m_impl->deferredPlannedImportPrepare.HasWork() || m_impl->accepted.cohortIntent.IsActive() ||
            m_impl->observation.reconcileActive ||
            m_impl->vddcMaintenance.OwnsAdmission() ||
            m_impl->looseRetirement.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;
        if (m_impl->automaticRecook.IsRecoveryAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        const ProjectAssetState* const accepted = m_impl->accepted.LookupAsset(asset);
        if (accepted == nullptr || accepted->presence != ProjectAssetPresence::Present || accepted->outputs.Empty())
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = assets::ProjectAssetBuildResult::SourceNotFound;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }
        if (accepted->sourceIssues != assets::SourceIssueNone || accepted->importState != assets::SourceImportState::MetadataSelected ||
            accepted->importer == assets::InvalidSourceImporterId || accepted->authoredImporterVersion == 0u ||
            accepted->authoredImporterVersion != accepted->availableImporterVersion)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = assets::ProjectAssetBuildResult::ImporterUnavailable;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        // Resolve the complete accepted output set before mutating coordinator
        // state. One unsupported/currently-unresolvable output fails the whole
        // asset-level request transactionally.
        for (const ProjectAssetOutputState& output : accepted->outputs)
        {
            assets::OwnedBuildRequest request;
            const assets::ProjectAssetBuildResult resolved = m_impl->builds.Resolve(*m_impl->sources, output.resource, request);
            if (resolved != assets::ProjectAssetBuildResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                    failure->build = resolved;
                }
                return ProjectAssetPipelineResult::BuildRequestFailed;
            }
        }

        if (!m_impl->automaticRecook.QueueExplicitReimport(*accepted, m_impl->accepted.generation))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                      : ProjectAssetPipelineResult::BuildRequestFailed;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RequestAssetCohortDelivery(
        const ProjectAssetCohortDeliveryRequest& request, ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!request.asset.IsValid() || request.acceptedGeneration == 0u || request.outputs.Empty() ||
            request.outputs.Data() == nullptr || request.outputs.Count() > m_impl->config.automaticRecook.maximumPendingOutputs)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        if (m_impl->deferredPlannedImportPrepare.HasWork() || m_impl->observation.reconcileActive ||
            m_impl->vddcMaintenance.OwnsAdmission() ||
            m_impl->looseRetirement.OwnsAdmission() || m_impl->automaticRecook.IsRecoveryAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        const ProjectAssetState* const accepted = m_impl->accepted.LookupAsset(request.asset);
        if (accepted == nullptr || accepted->presence != ProjectAssetPresence::Present ||
            accepted->acceptedGeneration != request.acceptedGeneration || accepted->outputs.Empty() ||
            accepted->outputs.Count() != request.outputs.Count())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::StalePlan)
                                      : ProjectAssetPipelineResult::StalePlan;
        if (accepted->sourceIssues != assets::SourceIssueNone || accepted->importState != assets::SourceImportState::MetadataSelected ||
            accepted->importer == assets::InvalidSourceImporterId || accepted->authoredImporterVersion == 0u ||
            accepted->authoredImporterVersion != accepted->availableImporterVersion)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = assets::ProjectAssetBuildResult::ImporterUnavailable;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        for (u32 requestIndex = 0u; requestIndex < request.outputs.Count(); ++requestIndex)
        {
            const resources::ResourceReference output = request.outputs[requestIndex];
            if (!output.IsValid() || !output.IsTyped())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                          : ProjectAssetPipelineResult::InvalidArgument;
            for (u32 previous = 0u; previous < requestIndex; ++previous)
                if (request.outputs[previous].GetPath().Id() == output.GetPath().Id())
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                              : ProjectAssetPipelineResult::InvalidArgument;

            bool acceptedOutput = false;
            for (const ProjectAssetOutputState& candidate : accepted->outputs)
                if (candidate.resource == output)
                {
                    acceptedOutput = true;
                    break;
                }
            if (!acceptedOutput)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::StalePlan)
                                          : ProjectAssetPipelineResult::StalePlan;
        }

        // Resolve the entire accepted cohort once, with exact per-output error
        // attribution. For a multi-output model the canonical group resolver
        // loads the one underlying source a single time and publishes immutable
        // shared backing across all validated child requests. Pass this same
        // verified batch into pending-state staging; resolving the 60MB source
        // a second time per output would do gigabytes of redundant UI-thread IO.
        containers::DynamicArray<resources::ResourceReference> acceptedOutputs(memory::pools::Assets::GetInstance());
        acceptedOutputs.Reserve(accepted->outputs.Size());
        if (acceptedOutputs.Capacity() < accepted->outputs.Size())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed,
                                         failure->build = assets::ProjectAssetBuildResult::BuildRequestCreationFailed,
                                         ProjectAssetPipelineResult::BuildRequestFailed)
                                      : ProjectAssetPipelineResult::BuildRequestFailed;
        for (const ProjectAssetOutputState& row : accepted->outputs)
            acceptedOutputs.PushBack(row.resource);
        containers::DynamicArray<assets::OwnedBuildRequest> validated(memory::pools::Assets::GetInstance());
        u32 failedOutputIndex = ~u32{0};
        const assets::ProjectAssetBuildResult preflight = m_impl->builds.ResolveCohort(
            *m_impl->sources, {acceptedOutputs.TypedData(), acceptedOutputs.Size()}, validated, failedOutputIndex);
        if (preflight != assets::ProjectAssetBuildResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = preflight;
                if (failedOutputIndex < acceptedOutputs.Size())
                    failure->buildRequestOutput = acceptedOutputs[failedOutputIndex];
                failure->buildRequestOutputIndex = failedOutputIndex;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        containers::ArraySpan<const resources::ResourceReference> predecessorOutputs;
        const ProjectAssetCohortIntent& durableIntent = m_impl->accepted.cohortIntent;
        if (durableIntent.IsActive())
        {
            if (!(durableIntent.asset == request.asset) || durableIntent.successorOutputs.Size() != request.outputs.Count())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                          : ProjectAssetPipelineResult::Busy;
            for (const resources::ResourceReference expected : durableIntent.successorOutputs)
            {
                bool found = false;
                for (const resources::ResourceReference requested : request.outputs)
                    if (requested == expected)
                    {
                        found = true;
                        break;
                    }
                if (!found)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::StalePlan)
                                              : ProjectAssetPipelineResult::StalePlan;
            }
            predecessorOutputs = {durableIntent.predecessorOutputs.TypedData(), durableIntent.predecessorOutputs.Size()};
        }

        if (!m_impl->automaticRecook.QueueExplicitCohort(*accepted, request.acceptedGeneration,
                                                         m_impl->accepted.generation, predecessorOutputs,
                                                         durableIntent.IsActive(),
                                                         {validated.TypedData(), validated.Size()}))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::CancelAutomaticRecook(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->automaticRecook.IsEnabled())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        ProjectAssetAutomaticRecookStatus& status = m_impl->automaticRecook.status;
        if (!m_impl->automaticRecook.OwnsAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        // Publication is a visibility commit, not cancellable compiler work. Once
        // it has been dispatched (or awaits owner-side settlement), accepting a
        // cancellation request would only leave a stale cancellation flag behind
        // after the committed publication settles and could cancel later work.
        if (m_impl->automaticRecook.workKind == Impl::AutomaticRecookState::WorkKind::Publish ||
            m_impl->automaticRecook.publicationSettlementPending)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        status.cancellationRequested = true;
        status.state = ProjectAssetAutomaticRecookState::Cancelling;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::TickAutomaticRecook(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        Impl::AutomaticRecookState& automatic = m_impl->automaticRecook;
        if (!automatic.IsEnabled())
        {
            // Disabled policy must never start ordinary automatic work. Still,
            // an unresolved durable explicit cohort is a pending ownership
            // fence, not an absent operation. Report that truth without
            // switching policy or mutating the Disabled state; EnableAutomaticRecook
            // still owns the opt-in state transition on a later call.
            if (m_impl->accepted.cohortIntent.IsActive() && !automatic.explicitCohort.IsActive())
                return ProjectAssetPipelineResult::Pending;
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        }

        // Visibility may already have committed even if owner bookkeeping could
        // not be adopted on the preceding tick. Keep that exact settlement
        // retryable without redispatching publication or compiler work.
        if (!automatic.workRunning && automatic.publicationSettlementPending)
        {
            const ProjectAssetPipelineResult settled = m_impl->SettleCompletedAutomaticPublication(failure, true);
            if (settled != ProjectAssetPipelineResult::Success)
                return settled;
        }

        // Job completion is the synchronization boundary for all worker-owned
        // planning/finalization fields. The owner never waits here.
        if (automatic.workRunning)
        {
            if (!automatic.workCompleted.GetValue())
                return ProjectAssetPipelineResult::Pending;

            const Impl::AutomaticRecookState::WorkKind completedKind = automatic.workKind;
            automatic.workRunning = false;
            automatic.workKind = Impl::AutomaticRecookState::WorkKind::None;
            if (completedKind == Impl::AutomaticRecookState::WorkKind::Publish)
            {
                const ProjectAssetPipelineResult settled = m_impl->SettleCompletedAutomaticPublication(failure, true);
                if (settled != ProjectAssetPipelineResult::Success)
                    return settled;
            }
            automatic.RefreshActiveStatus();
            if (automatic.workerWasRecovery)
            {
                // The worker recovery snapshot represents current accepted
                // truth and therefore subsumes the transient pending set that
                // existed at admission. Retire it by O(1) owner swap; its
                // payload is reclaimed by the next worker task or shutdown.
                automatic.pending.Swap(automatic.retired);
                automatic.pending.lastAcceptedGeneration = automatic.status.activeAcceptedGeneration;
                automatic.RefreshPendingStatus();
                automatic.recoveryRequested = false;
            }

            if (automatic.workerFinished)
            {
                automatic.status.lastRecookFailure = automatic.workerFailure;
                automatic.status.firstFailedRoot = automatic.workerRootFailure;
                automatic.status.failedRootCount = automatic.workerStats.failedRoots;
                const bool recoverAfterPollDispatchFailure = automatic.recoverAfterPollDispatchFailure;
                automatic.recoverAfterPollDispatchFailure = false;
                automatic.admissionOwned = false;
                automatic.status.cancellationRequested = false;
                automatic.status.retryRequested = false;

                if (automatic.workerDeliveryCapacityExceeded || automatic.workerDeliveryAdoptionFailed)
                {
                    automatic.MarkDeliveryRecoveryRequired(m_impl->accepted.generation);
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                              : ProjectAssetPipelineResult::BuildRequestFailed;
                }

                if (automatic.workerRecoveryRequired)
                {
                    automatic.MarkRecoveryRequired(m_impl->accepted.generation);
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                              : ProjectAssetPipelineResult::BuildRequestFailed;
                }

                if (automatic.workerState == assets::RecookState::Succeeded)
                {
                    automatic.staleRejected = false;
                    automatic.status.recoveryRequired = false;
                    automatic.status.activeRoots = 0u;
                    automatic.status.completedRoots = 0u;
                    automatic.status.activeAcceptedGeneration = 0u;
                    automatic.status.retryEntries = 0u;
                    automatic.RefreshPendingStatus();
                    const bool hasPending = !automatic.pending.assets.Empty() || !automatic.pending.inputs.Empty() ||
                                            !automatic.pending.outputs.Empty();
                    const bool completionPending = hasPending || automatic.explicitActivity;
                    automatic.status.state = completionPending ? ProjectAssetAutomaticRecookState::Pending
                                                               : ProjectAssetAutomaticRecookState::Idle;
                    if (!completionPending)
                        automatic.status.dirtyReasons = ProjectAssetAutomaticRecookDirtyReason::None;
                    return completionPending ? ProjectAssetPipelineResult::Pending : ProjectAssetPipelineResult::Success;
                }
                else
                {
                    if (automatic.workerFailure == assets::RecookFailure::PreCommitRejected)
                    {
                        automatic.staleRejected = true;
                        automatic.status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::StaleCompletion;
                    }
                    else
                    {
                        automatic.status.dirtyReasons |= automatic.workerState == assets::RecookState::Cancelled
                                                             ? ProjectAssetAutomaticRecookDirtyReason::CancelledBatch
                                                             : ProjectAssetAutomaticRecookDirtyReason::FailedBatch;
                    }
                    automatic.status.state = ProjectAssetAutomaticRecookState::Failed;
                    automatic.status.pendingDiagnostics = automatic.pending.diagnostics + automatic.active.diagnostics;
                    if (automatic.pending.lastAcceptedGeneration > automatic.status.activeAcceptedGeneration)
                        automatic.recoveryRequested = true;
                    automatic.RefreshRetryStatus();
                    if (recoverAfterPollDispatchFailure)
                    {
                        automatic.recoveryRequested = true;
                        automatic.status.retryRequested = true;
                        automatic.status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::ExplicitRetry;
                        automatic.status.state = ProjectAssetAutomaticRecookState::Pending;
                        return ProjectAssetPipelineResult::Pending;
                    }
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                              : ProjectAssetPipelineResult::BuildRequestFailed;
                }
            }

            automatic.status.state = automatic.status.cancellationRequested ? ProjectAssetAutomaticRecookState::Cancelling
                                                                              : ProjectAssetAutomaticRecookState::Building;
        }

        if (automatic.admissionOwned)
        {
            // A single poll job performs graph completion inspection and, when
            // ready, VADI transaction finalization. Re-dispatch happens only on
            // later owner ticks, so no Jobs worker is parked in an unbounded wait.
            if (automatic.pollDispatchFailed && !automatic.pollDispatchRetryRequested)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                          : ProjectAssetPipelineResult::BuildRequestFailed;
            if (!automatic.workRunning)
            {
                const bool explicitPollRetry = automatic.pollDispatchFailed && automatic.pollDispatchRetryRequested;
                if (!automatic.DispatchPoll(automatic.status.cancellationRequested || explicitPollRetry))
                {
                    static_cast<void>(automatic.batch.Cancel());
                    automatic.pollDispatchFailed = true;
                    automatic.pollDispatchRetryRequested = false;
                    automatic.status.cancellationRequested = true;
                    automatic.status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::FailedBatch;
                    automatic.status.state = ProjectAssetAutomaticRecookState::Failed;
                    automatic.RefreshRetryStatus();
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                              : ProjectAssetPipelineResult::BuildRequestFailed;
                }
                if (explicitPollRetry)
                {
                    automatic.pollDispatchFailed = false;
                    automatic.pollDispatchRetryRequested = false;
                    automatic.recoverAfterPollDispatchFailure = true;
                    automatic.status.state = ProjectAssetAutomaticRecookState::Cancelling;
                }
            }
            return ProjectAssetPipelineResult::Pending;
        }

        const bool hasPending = !automatic.pending.assets.Empty() || !automatic.pending.inputs.Empty() ||
                                !automatic.pending.outputs.Empty();
        if (!automatic.recoveryRequested &&
            (automatic.status.state == ProjectAssetAutomaticRecookState::Failed ||
             automatic.status.state == ProjectAssetAutomaticRecookState::RecoveryRequired))
        {
            // Terminal automatic failure is sticky. Do not turn a retained
            // failed/diagnostic active set back into Idle and do not resubmit it
            // on every owner tick. Explicit RetryAutomaticRecook() or a newer
            // accepted change is the next execution trigger.
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                      : ProjectAssetPipelineResult::BuildRequestFailed;
        }
        if (!automatic.recoveryRequested && !hasPending && automatic.HasPublishableDelivery())
        {
            if (!m_impl->CanAcquireAutomaticPublicationAdmission())
                return ProjectAssetPipelineResult::Pending;
            if (!m_impl->CanAdoptPendingAutomaticPublications())
            {
                automatic.MarkPublishableDeliveriesFailed(
                    ProjectAssetDeliveryReason::PublicationFailed,
                    !(automatic.explicitCohort.IsActive() && automatic.explicitCohort.publicationBlocked));
                automatic.RefreshDeliveryStatus(automatic.status);
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                          : ProjectAssetPipelineResult::ReconciliationFailed;
            }
            automatic.admissionOwned = true;
            if (!automatic.DispatchPublication(m_impl->publisher, *m_impl->files, m_impl->looseRetirement,
                                               m_impl->accumulator, m_impl->accepted))
            {
                automatic.admissionOwned = false;
                automatic.MarkPublishableDeliveriesFailed(
                    ProjectAssetDeliveryReason::PublicationFailed,
                    !(automatic.explicitCohort.IsActive() && automatic.explicitCohort.publicationBlocked));
                automatic.RefreshDeliveryStatus(automatic.status);
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::PublicationFailed)
                                          : ProjectAssetPipelineResult::PublicationFailed;
            }
            return ProjectAssetPipelineResult::Pending;
        }
        if (!automatic.recoveryRequested && !hasPending)
        {
            if (m_impl->accepted.cohortIntent.IsActive() && !automatic.explicitCohort.IsActive())
            {
                // A durable successor intent can survive restart when its
                // build-request admission is temporarily unbuildable. It still
                // fences ordinary automatic work: reporting Idle/Success here
                // would erase the pending successor from the owner-facing
                // state even though its last-known-good predecessor remains
                // protected. Only an explicit retry may re-admit that cohort.
                automatic.status.state = ProjectAssetAutomaticRecookState::Pending;
                return ProjectAssetPipelineResult::Pending;
            }
            if (automatic.explicitCohort.IsActive())
            {
                // A failed/stale cohort publication remains an explicit atomic
                // retry unit. Do not silently retire the manual activity or let
                // disabled-policy cleanup dissolve it into per-output delivery.
                automatic.status.state = ProjectAssetAutomaticRecookState::Idle;
                return ProjectAssetPipelineResult::Success;
            }
                if (automatic.explicitActivity)
                {
                    automatic.explicitActivity = false;
                if (!automatic.policyEnabled)
                {
                        automatic.SetEnabled(false);
                        return ProjectAssetPipelineResult::Success;
                    }
                    if (automatic.startupRecoveryDeferred)
                    {
                        automatic.startupRecoveryDeferred = false;
                        automatic.recoveryRequested = true;
                        automatic.status.dirtyReasons = ProjectAssetAutomaticRecookDirtyReason::StartupRecovery;
                        automatic.status.state = ProjectAssetAutomaticRecookState::Pending;
                        automatic.RefreshRetryStatus();
                        return ProjectAssetPipelineResult::Pending;
                    }
                    automatic.status.dirtyReasons = ProjectAssetAutomaticRecookDirtyReason::None;
                }
            automatic.status.state = ProjectAssetAutomaticRecookState::Idle;
            return ProjectAssetPipelineResult::Success;
        }

        // Admission is owner-thread serialized with manual ProjectAssetPipeline
        // requests/source mutation. Lower-layer direct BuildGraph users are not
        // claimed safe: require a fully quiescent graph and clean committed VADI
        // immediately before taking ownership.
        if (!m_impl->CanAcquireAutomaticAdmission())
            return ProjectAssetPipelineResult::Pending;

        assets::DependencyIndex* const index = m_impl->builds.GetDependencyIndex();
        if (index == nullptr)
        {
            automatic.MarkRecoveryRequired(m_impl->accepted.generation);
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                      : ProjectAssetPipelineResult::BuildRequestFailed;
        }

        const bool recovery = automatic.recoveryRequested || !automatic.active.assets.Empty() || !automatic.active.inputs.Empty() ||
                              !automatic.active.outputs.Empty();
        const bool explicitOnlyRecovery = recovery && automatic.explicitActivity && !automatic.policyEnabled;
        if (!recovery)
        {
            automatic.active.Swap(automatic.pending);
            automatic.pending.lastAcceptedGeneration = automatic.active.lastAcceptedGeneration;
        }
        automatic.admissionOwned = true;
        automatic.workerWasRecovery = recovery;
        automatic.status.state = ProjectAssetAutomaticRecookState::Planning;
        automatic.status.buildOperations = 0u;
        automatic.status.completedBuildOperations = 0u;
        automatic.status.activeAcceptedGeneration = recovery ? m_impl->accepted.generation
                                                             : automatic.active.lastAcceptedGeneration;
        automatic.workerAcceptedGeneration = automatic.status.activeAcceptedGeneration;
        automatic.RefreshPendingStatus();

        if (!automatic.DispatchPlan(recovery, explicitOnlyRecovery, m_impl->accepted, *m_impl->sources, m_impl->builds,
                                    m_impl->publisher, *index))
        {
            automatic.admissionOwned = false;
            automatic.recoveryRequested = false;
            automatic.RefreshPendingStatus();
            automatic.status.dirtyReasons |= ProjectAssetAutomaticRecookDirtyReason::FailedBatch;
            automatic.status.state = ProjectAssetAutomaticRecookState::Failed;
            automatic.RefreshRetryStatus();
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::BuildRequestFailed)
                                      : ProjectAssetPipelineResult::BuildRequestFailed;
        }
        return ProjectAssetPipelineResult::Pending;
    }

    bool ProjectAssetPipeline::IsAutomaticRecookEnabled() const noexcept
    {
        return m_impl != nullptr && m_impl->automaticRecook.IsPolicyEnabled();
    }

    ProjectAssetAutomaticRecookStatus ProjectAssetPipeline::GetAutomaticRecookStatus() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        ProjectAssetAutomaticRecookStatus status = m_impl->automaticRecook.status;
        m_impl->automaticRecook.RefreshDeliveryStatus(status);
        return status;
    }

    bool ProjectAssetPipeline::GetOutputDeliveryStatus(const resources::ResourceReference output,
                                                       ProjectAssetOutputDeliveryStatus& status) const noexcept
    {
        return m_impl != nullptr && m_impl->automaticRecook.GetOutputStatus(output, status);
    }

    bool ProjectAssetPipeline::GetOutputPublicationStatus(
        const resources::ResourceReference output,
        ProjectAssetOutputPublicationStatus& status) const noexcept
    {
        return m_impl != nullptr && m_impl->automaticRecook.GetOutputPublicationStatus(output, status);
    }

    bool ProjectAssetPipeline::CaptureVddcProtectionKeys(containers::DynamicArray<assets::ArtifactSetKey>& keys,
                                                         const u32 maximumRecords, const u64 maximumBytes) const noexcept
    {
        keys.Clear();
        return m_impl != nullptr && m_impl->automaticRecook.CaptureVddcProtectionKeys(keys, maximumRecords, maximumBytes);
    }

    bool ProjectAssetPipeline::ReportResidentDelivery(const ProjectAssetResidentDeliveryEvidence& evidence) noexcept
    {
        return m_impl != nullptr && !m_impl->vddcMaintenance.OwnsAdmission() && !m_impl->looseRetirement.OwnsAdmission() &&
               m_impl->automaticRecook.ReportResidentDelivery(evidence);
    }

    bool ProjectAssetPipeline::ClearResidentDelivery(const resources::ResourceReference output) noexcept
    {
        return m_impl != nullptr && !m_impl->vddcMaintenance.OwnsAdmission() && !m_impl->looseRetirement.OwnsAdmission() &&
               m_impl->automaticRecook.ClearResidentDelivery(output);
    }

    ProjectAssetResidentRetirementResult ProjectAssetPipeline::ReportResidentRetirement(
        const ProjectAssetResidentRetirementEvidence& evidence) noexcept
    {
        if (m_impl == nullptr)
            return ProjectAssetResidentRetirementResult::InvalidState;
        if (!evidence.IsValid())
            return ProjectAssetResidentRetirementResult::InvalidArgument;
        if (m_impl->vddcMaintenance.OwnsAdmission() || m_impl->looseRetirement.OwnsAdmission())
            return ProjectAssetResidentRetirementResult::Busy;
        return m_impl->looseRetirement.ReportResidentRetirement(evidence);
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RequestVddcMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        Impl::VddcMaintenanceState& maintenance = m_impl->vddcMaintenance;
        if (maintenance.HasWork() || !m_impl->CanAcquireVddcMaintenanceAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        maintenance.status = {};
        maintenance.status.state = ProjectAssetVddcMaintenanceState::Requested;
        maintenance.diagnostics.Clear();
        maintenance.protections.Clear();
        maintenance.deliveryProtections.Clear();
        maintenance.admissionOwned = true;

        const auto failStart = [&](const assets::PersistentCacheMaintenanceResult result) noexcept -> ProjectAssetPipelineResult
        {
            maintenance.admissionOwned = false;
            maintenance.status.lastResult = result;
            maintenance.status.retryRequired = true;
            maintenance.status.limitExceeded = result == assets::PersistentCacheMaintenanceResult::LimitExceeded;
            maintenance.status.state = result == assets::PersistentCacheMaintenanceResult::ProtectionChanged
                                           ? ProjectAssetVddcMaintenanceState::Deferred
                                           : ProjectAssetVddcMaintenanceState::Failed;
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::CacheMaintenanceFailed;
                failure->maintenance = result;
            }
            return ProjectAssetPipelineResult::CacheMaintenanceFailed;
        };

        const ProjectAssetCacheMaintenanceConfig& config = maintenance.config;
        u64 protectionEpoch = 0u;
        const assets::PersistentCacheMaintenanceResult captured = m_impl->builds.CapturePersistentProtectionSnapshot(
            maintenance.protections, config.maximumVddcProtectionRecords, config.maximumVddcProtectionBytes,
            protectionEpoch);
        if (captured != assets::PersistentCacheMaintenanceResult::Success)
            return failStart(captured);

        if (!m_impl->automaticRecook.CaptureVddcProtectionKeys(
                maintenance.deliveryProtections, config.maximumVddcProtectionRecords, config.maximumVddcProtectionBytes))
            return failStart(assets::PersistentCacheMaintenanceResult::InvalidState);
        if (!MergeProtectionBuilds(maintenance.protections, maintenance.deliveryProtections,
                                   config.maximumVddcProtectionRecords, config.maximumVddcProtectionBytes))
            return failStart(assets::PersistentCacheMaintenanceResult::LimitExceeded);

        // Delivery protection is pipeline-owned and therefore not represented by
        // the root epoch. Owner admission freezes it; bracket the combined handoff
        // once more so a lower-level durable adoption cannot slip between the
        // assets snapshot and the worker dispatch.
        if (m_impl->builds.GetPersistentProtectionEpoch() != protectionEpoch)
            return failStart(assets::PersistentCacheMaintenanceResult::ProtectionChanged);

        maintenance.status.protectionEpoch = protectionEpoch;
        if (!maintenance.Dispatch(m_impl->builds, protectionEpoch))
            return failStart(assets::PersistentCacheMaintenanceResult::InvalidState);
        maintenance.status.state = ProjectAssetVddcMaintenanceState::Running;
        return ProjectAssetPipelineResult::Pending;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RetryVddcMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        const ProjectAssetVddcMaintenanceState state = m_impl->vddcMaintenance.status.state;
        if (state != ProjectAssetVddcMaintenanceState::Deferred && state != ProjectAssetVddcMaintenanceState::Failed &&
            state != ProjectAssetVddcMaintenanceState::Cancelled)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        return RequestVddcMaintenance(failure);
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::CancelVddcMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        Impl::VddcMaintenanceState& maintenance = m_impl->vddcMaintenance;
        if (!maintenance.workRunning || !maintenance.admissionOwned)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        maintenance.cancellationRequested.SetValue(true);
        maintenance.status.cancellationRequested = true;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::TickVddcMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        Impl::VddcMaintenanceState& maintenance = m_impl->vddcMaintenance;
        if (maintenance.workRunning)
        {
            if (!maintenance.workCompleted.GetValue())
                return ProjectAssetPipelineResult::Pending;

            maintenance.workRunning = false;
            maintenance.admissionOwned = false;
            maintenance.status.cancellationRequested = false;
            maintenance.diagnostics.Swap(maintenance.workerDiagnostics);
            maintenance.workerDiagnostics.Clear();
            const assets::PersistentCacheMaintenanceReport& report = maintenance.workerReport;
            maintenance.status.scannedRecords = report.scannedRecords;
            maintenance.status.scannedBytes = report.scannedBytes;
            maintenance.status.protectedRecords = report.protectedRecords;
            maintenance.status.protectedBytes = report.protectedBytes;
            maintenance.status.eligibleRecords = report.eligibleRecords;
            maintenance.status.eligibleBytes = report.eligibleBytes;
            maintenance.status.removedRecords = report.removedRecords;
            maintenance.status.removedBytes = report.removedBytes;
            maintenance.status.deferredDeletes = report.deferredDeletes;
            maintenance.status.failedDeletes = report.failedDeletes;
            maintenance.status.foreignFiles = report.foreignFiles;
            maintenance.status.stageFiles = report.stageFiles;
            maintenance.status.protectedOverBudget = report.protectedOverBudget;
            maintenance.status.deleteBatchLimitReached = report.deleteBatchLimitReached;
            maintenance.status.budgetSatisfied = report.budgetSatisfied;
            maintenance.status.statusRowsTruncated = report.diagnosticsTruncated;
            maintenance.status.lastResult = maintenance.workerResult;
            maintenance.status.limitExceeded = maintenance.workerResult == assets::PersistentCacheMaintenanceResult::LimitExceeded;

            if (maintenance.workerResult == assets::PersistentCacheMaintenanceResult::Success)
            {
                // A satisfied byte target does not erase sharing/I/O refusal:
                // P0 defines those outcomes as sticky deferred work. Likewise,
                // any still-unsatisfied budget remains explicit owner-visible
                // deferred state instead of being mislabeled Complete.
                const bool deferred = !report.budgetSatisfied || report.protectedOverBudget || report.deleteBatchLimitReached ||
                                      report.deferredDeletes != 0u || report.failedDeletes != 0u;
                maintenance.status.state = deferred ? ProjectAssetVddcMaintenanceState::Deferred
                                                    : ProjectAssetVddcMaintenanceState::Complete;
                maintenance.status.retryRequired = deferred;
                if (deferred)
                    maintenance.status.lastResult = assets::PersistentCacheMaintenanceResult::Deferred;
                return ProjectAssetPipelineResult::Success;
            }
            if (maintenance.workerResult == assets::PersistentCacheMaintenanceResult::ProtectionChanged)
            {
                maintenance.status.state = ProjectAssetVddcMaintenanceState::Deferred;
                maintenance.status.retryRequired = true;
                return ProjectAssetPipelineResult::Success;
            }
            if (maintenance.workerResult == assets::PersistentCacheMaintenanceResult::Cancelled)
            {
                maintenance.status.state = ProjectAssetVddcMaintenanceState::Cancelled;
                maintenance.status.retryRequired = true;
                return ProjectAssetPipelineResult::Success;
            }

            maintenance.status.state = ProjectAssetVddcMaintenanceState::Failed;
            maintenance.status.retryRequired = true;
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::CacheMaintenanceFailed;
                failure->maintenance = maintenance.workerResult;
            }
            return ProjectAssetPipelineResult::CacheMaintenanceFailed;
        }

        if (maintenance.status.state == ProjectAssetVddcMaintenanceState::Complete ||
            maintenance.status.state == ProjectAssetVddcMaintenanceState::Deferred ||
            maintenance.status.state == ProjectAssetVddcMaintenanceState::Cancelled)
            return ProjectAssetPipelineResult::Success;
        if (maintenance.status.state == ProjectAssetVddcMaintenanceState::Failed)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::CacheMaintenanceFailed;
                failure->maintenance = maintenance.status.lastResult;
            }
            return ProjectAssetPipelineResult::CacheMaintenanceFailed;
        }
        return maintenance.status.state == ProjectAssetVddcMaintenanceState::Idle
                   ? ProjectAssetPipelineResult::Success
                   : ProjectAssetPipelineResult::Pending;
    }

    ProjectAssetVddcMaintenanceStatus ProjectAssetPipeline::GetVddcMaintenanceStatus() const noexcept
    {
        return m_impl != nullptr ? m_impl->vddcMaintenance.status : ProjectAssetVddcMaintenanceStatus{};
    }

    containers::ArraySpan<const assets::PersistentCacheMaintenanceDiagnostic> ProjectAssetPipeline::GetVddcMaintenanceDiagnostics() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const assets::PersistentCacheMaintenanceDiagnostic>(m_impl->vddcMaintenance.diagnostics.TypedData(),
                                                                                               m_impl->vddcMaintenance.diagnostics.Size())
                   : containers::ArraySpan<const assets::PersistentCacheMaintenanceDiagnostic>();
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RequestLooseMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        Impl::LooseRetirementState& maintenance = m_impl->looseRetirement;
        if (maintenance.HasWork() || !m_impl->CanAcquireLooseMaintenanceAdmission())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        maintenance.status = {};
        maintenance.status.state = ProjectAssetLooseMaintenanceState::Requested;
        maintenance.diagnostics.Clear();
        maintenance.admissionOwned = true;
        if (!maintenance.PrepareWorkerSnapshot({m_impl->publications.TypedData(), m_impl->publications.Size()}))
        {
            maintenance.admissionOwned = false;
            maintenance.status.state = ProjectAssetLooseMaintenanceState::Failed;
            maintenance.status.lastResult = ProjectAssetLooseMaintenanceResult::LimitExceeded;
            maintenance.status.limitExceeded = true;
            maintenance.status.retryRequired = true;
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::LooseMaintenanceFailed;
                failure->looseMaintenance = ProjectAssetLooseMaintenanceResult::LimitExceeded;
            }
            return ProjectAssetPipelineResult::LooseMaintenanceFailed;
        }
        if (!maintenance.Dispatch(*m_impl->files, *m_impl->streamer, m_impl->config.publication.looseRoot))
        {
            maintenance.admissionOwned = false;
            maintenance.status.state = ProjectAssetLooseMaintenanceState::Failed;
            maintenance.status.lastResult = ProjectAssetLooseMaintenanceResult::InvalidState;
            maintenance.status.retryRequired = true;
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::LooseMaintenanceFailed;
                failure->looseMaintenance = ProjectAssetLooseMaintenanceResult::InvalidState;
            }
            return ProjectAssetPipelineResult::LooseMaintenanceFailed;
        }
        maintenance.status.state = ProjectAssetLooseMaintenanceState::Running;
        return ProjectAssetPipelineResult::Pending;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RetryLooseMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        const ProjectAssetLooseMaintenanceState state = m_impl->looseRetirement.status.state;
        if (state != ProjectAssetLooseMaintenanceState::Deferred && state != ProjectAssetLooseMaintenanceState::Failed &&
            state != ProjectAssetLooseMaintenanceState::Cancelled)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        return RequestLooseMaintenance(failure);
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::CancelLooseMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        Impl::LooseRetirementState& maintenance = m_impl->looseRetirement;
        if (!maintenance.workRunning || !maintenance.admissionOwned)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        maintenance.cancellationRequested.SetValue(true);
        maintenance.status.cancellationRequested = true;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::TickLooseMaintenance(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        Impl::LooseRetirementState& maintenance = m_impl->looseRetirement;
        if (maintenance.workRunning)
        {
            if (!maintenance.workCompleted.GetValue())
                return ProjectAssetPipelineResult::Pending;
            maintenance.AdoptWorkerResult();
            if (maintenance.status.state == ProjectAssetLooseMaintenanceState::Failed)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::LooseMaintenanceFailed;
                    failure->looseMaintenance = maintenance.status.lastResult;
                }
                return ProjectAssetPipelineResult::LooseMaintenanceFailed;
            }
            return ProjectAssetPipelineResult::Success;
        }
        if (maintenance.status.state == ProjectAssetLooseMaintenanceState::Failed)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::LooseMaintenanceFailed;
                failure->looseMaintenance = maintenance.status.lastResult;
            }
            return ProjectAssetPipelineResult::LooseMaintenanceFailed;
        }
        if (maintenance.status.state == ProjectAssetLooseMaintenanceState::Requested ||
            maintenance.status.state == ProjectAssetLooseMaintenanceState::Running)
            return ProjectAssetPipelineResult::Pending;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetLooseMaintenanceStatus ProjectAssetPipeline::GetLooseMaintenanceStatus() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        m_impl->looseRetirement.RefreshTrackedStatus();
        return m_impl->looseRetirement.status;
    }

    containers::ArraySpan<const ProjectAssetLooseMaintenanceDiagnostic> ProjectAssetPipeline::GetLooseMaintenanceDiagnostics() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const ProjectAssetLooseMaintenanceDiagnostic>(m_impl->looseRetirement.diagnostics.TypedData(),
                                                                                         m_impl->looseRetirement.diagnostics.Size())
                   : containers::ArraySpan<const ProjectAssetLooseMaintenanceDiagnostic>();
    }

    containers::ArraySpan<const resources::ResourceReference> ProjectAssetPipeline::GetAutomaticRecookPendingInputs() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const resources::ResourceReference>{m_impl->automaticRecook.pending.inputs.TypedData(),
                                                                                m_impl->automaticRecook.pending.inputs.Size()}
                   : containers::ArraySpan<const resources::ResourceReference>{};
    }

    containers::ArraySpan<const resources::ResourceReference> ProjectAssetPipeline::GetAutomaticRecookPendingOutputs() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const resources::ResourceReference>{m_impl->automaticRecook.pending.outputs.TypedData(),
                                                                                m_impl->automaticRecook.pending.outputs.Size()}
                   : containers::ArraySpan<const resources::ResourceReference>{};
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::QueryBuildFreshness(
        const resources::ResourceReference output, ProjectAssetBuildFreshness& freshness,
        ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!output.IsValid() || !output.IsTyped())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;

        assets::BuildGraph* const graph = m_impl->builds.GetBuildGraph();
        assets::DependencyIndex* const index = m_impl->builds.GetDependencyIndex();
        assets::BuildSystem* const buildSystem = m_impl->builds.GetBuildSystem();
        if (graph == nullptr || index == nullptr || buildSystem == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        const assets::BuildGraphStats stats = graph->GetStats();
        const bool graphBusy = stats.externalRequests != 0u || stats.activeOperations != 0u || stats.waitingForAdmission != 0u ||
                               stats.queuedRequestBytes != 0u || stats.activeExecutionBytes != 0u || stats.retainedOutputBytes != 0u;
        if (m_impl->observation.reconcileActive || m_impl->automaticRecook.OwnsAdmission() || graphBusy || index->HasActiveTransaction())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        assets::OwnedBuildRequest owned;
        const assets::ProjectAssetBuildResult resolved = m_impl->builds.Resolve(*m_impl->sources, output, owned);
        if (resolved != assets::ProjectAssetBuildResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = resolved;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        const assets::BuildRequest request = owned.View();
        assets::BuildPlan plan;
        const assets::Result prepared = buildSystem->Prepare(request, plan);
        if (prepared != assets::Result::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->buildError = prepared;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        const assets::IndexResult materialized = index->MaterializeGeneratedDependencyContent(plan);
        if (materialized != assets::IndexResult::Success && materialized != assets::IndexResult::NotFound)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->index = materialized;
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }

        ProjectAssetBuildFreshness candidate;
        candidate.output = output;
        candidate.reason = index->Evaluate(request, plan);
        freshness = candidate;
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetRequest ProjectAssetPipeline::Request(const resources::ResourceReference output, const assets::BuildPriority priority,
                                                      ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
        {
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::InvalidState;
            return {};
        }
        if (m_impl->accepted.cohortIntent.IsActive() || m_impl->observation.reconcileActive ||
            m_impl->automaticRecook.OwnsAdmission() ||
            m_impl->vddcMaintenance.OwnsAdmission() || m_impl->looseRetirement.OwnsAdmission() ||
            m_impl->builds.GetDependencyIndex() == nullptr || m_impl->builds.GetDependencyIndex()->HasRetirementProtections())
        {
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::Busy;
            return {};
        }
        assets::ProjectAssetBuildResult result = assets::ProjectAssetBuildResult::Success;
        assets::GraphRequest request = m_impl->builds.Request(output, priority, &result);
        if (!request)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->build = result;
            }
            return {};
        }
        return ProjectAssetRequest(*this, output, static_cast<assets::GraphRequest&&>(request));
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::Poll(ProjectAssetRequest& request, ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || request.m_owner != this || !request.m_request.IsValid())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (request.m_published)
            return ProjectAssetPipelineResult::Success;
        if (!request.m_request.HasFinished())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Pending) : ProjectAssetPipelineResult::Pending;
        if (!request.m_request.HasSucceeded())
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::BuildRequestFailed;
                failure->buildFailure = request.m_request.GetError();
                failure->buildError = request.m_request.BuildError();
            }
            return ProjectAssetPipelineResult::BuildRequestFailed;
        }
        assets::DependencyIndex* const index = m_impl->builds.GetDependencyIndex();
        if (index == nullptr || index->Save() != assets::IndexResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::IndexSaveFailed;
                failure->build = assets::ProjectAssetBuildResult::IndexSaveFailed;
            }
            return ProjectAssetPipelineResult::IndexSaveFailed;
        }
        const ProjectAssetPipelineResult published = m_impl->Publish(request.m_resource, failure);
        request.m_published = published == ProjectAssetPipelineResult::Success;
        return published;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::Reconcile(ProjectAssetRestoreReport* const output,
                                                               ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (output != nullptr)
            *output = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (!m_impl->CanMutateSourcesCore())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        assets::DependencyIndex* const retirementIndex = m_impl->builds.GetDependencyIndex();
        const bool hadRetirementProtections = retirementIndex != nullptr && retirementIndex->HasRetirementProtections();
        const ProjectAssetPipelineResult replayed = m_impl->ReplayRetirementProtections(failure);
        if (replayed != ProjectAssetPipelineResult::Success)
            return replayed;
        if (hadRetirementProtections && m_impl->accepted.cohortIntent.IsActive() && retirementIndex != nullptr &&
            !retirementIndex->HasRetirementProtections())
        {
            if (!m_impl->CohortIntentSuccessorVisibleExact())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending)
                                          : ProjectAssetPipelineResult::CommittedReconciliationPending;
            const ProjectAssetSnapshotResult cleared = m_impl->ClearCohortIntent();
            if (cleared != ProjectAssetSnapshotResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->acceptedSnapshot = cleared;
                }
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
        }
        if (!m_impl->CanReconcileSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        Impl::ReconcileState state;
        if (!state.ReserveRemovalCapacity(m_impl->config.maximumPublishedResources))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                      : ProjectAssetPipelineResult::ReconciliationFailed;
        if (!CollectDesiredResources(*m_impl->sources, state.desired, state.desiredLookup,
                                     m_impl->config.maximumPublishedResources))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                      : ProjectAssetPipelineResult::ReconciliationFailed;
        state.report.catalogOutputs = state.desired.Size();
        if (!ApplyCohortIntentDesired(m_impl->accepted.cohortIntent, state.desired, state.desiredLookup,
                                      m_impl->config.maximumPublishedResources))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                      : ProjectAssetPipelineResult::ReconciliationFailed;
        if (filesystem::ScanFiles(m_impl->config.publication.looseRoot, state.looseFiles, m_impl->config.maximumLooseFiles) !=
            filesystem::ScanResult::Success)
            state.looseFiles.Clear();

        ProjectAssetPipelineResult result = ProjectAssetPipelineResult::Pending;
        while (result == ProjectAssetPipelineResult::Pending)
            result = m_impl->AdvanceReconcile(state, 0xffffffffu, failure);
        if (output != nullptr)
            *output = state.report;
        return result;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::Rescan(const containers::ArraySpan<const assets::SourceRoot> roots,
                                                            ProjectAssetRestoreReport* const report,
                                                            filesystem::ScanResult* const scanFailure,
                                                            ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr || roots.Empty())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        // An active observer is configured for the current catalog roots. Root
        // set transitions must stop/restart observation explicitly so there is
        // never a window where callbacks are routed against stale root indices.
        if (m_impl->observation.running && !SameSourceRoots(roots, m_impl->sources->GetRoots()))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (!m_impl->CanReconcileSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        assets::SourceDatabase candidate;
        ProjectAssetAcceptedBaseline proposed(m_impl->config.changeState);
        Impl::ReconcileState state;
        if (!state.ReserveRemovalCapacity(m_impl->config.maximumPublishedResources))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                      : ProjectAssetPipelineResult::ReconciliationFailed;
        const assets::SourceDatabaseResult sourceResult = PrepareProjectAssetCandidate(
            *m_impl->files, roots, m_impl->builds.GetImporters(), m_impl->config.build.target, m_impl->config.build.sourceData,
            m_impl->config.operationJournalPath, m_impl->accepted, m_impl->config.maximumPublishedResources,
            m_impl->config.publication.looseRoot, m_impl->config.maximumLooseFiles, candidate, proposed,
            state.desired, state.desiredLookup, state.looseFiles, scanFailure);
        if (sourceResult != assets::SourceDatabaseResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                failure->sources = sourceResult;
            }
            return ProjectAssetPipelineResult::ReconciliationFailed;
        }

        ProjectAssetAutomaticPendingState automaticPending;
        ProjectAssetAutomaticPendingPrepareResult automaticPendingResult = ProjectAssetAutomaticPendingPrepareResult::NoChange;
        if (m_impl->automaticRecook.IsPolicyEnabled())
        {
            assets::DependencyIndex* const dependencyIndex = m_impl->builds.GetDependencyIndex();
            automaticPendingResult = dependencyIndex != nullptr
                                         ? PrepareProjectAssetAutomaticPending(m_impl->automaticRecook.pending, proposed, candidate,
                                                                              m_impl->builds, *dependencyIndex,
                                                                              m_impl->automaticRecook.config, automaticPending)
                                         : ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }

        // P4 extends the P3 transaction: durable serialization, staged write,
        // read-back verification and atomic replacement all complete before the
        // source catalog or accepted in-memory baseline changes.
        const ProjectAssetSnapshotResult persisted = PersistProjectAssetCandidate(
            *m_impl->files, m_impl->config.build.dependencyIndexRoot, m_impl->accepted, proposed);
        if (persisted != ProjectAssetSnapshotResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                failure->acceptedSnapshot = persisted;
            }
            return ProjectAssetPipelineResult::SnapshotSaveFailed;
        }
        AcceptProjectAssetCandidate(*m_impl->sources, m_impl->accepted, candidate, proposed);
        if (automaticPendingResult == ProjectAssetAutomaticPendingPrepareResult::Success)
            m_impl->automaticRecook.AdoptPending(automaticPending);
        else if (automaticPendingResult == ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired)
            m_impl->automaticRecook.MarkRecoveryRequired(m_impl->accepted.generation, true);
        state.report.catalogOutputs = state.desired.Size();
        if (!ApplyCohortIntentDesired(m_impl->accepted.cohortIntent, state.desired, state.desiredLookup,
                                      m_impl->config.maximumPublishedResources))
        {
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }

        ProjectAssetPipelineResult result = ProjectAssetPipelineResult::Pending;
        while (result == ProjectAssetPipelineResult::Pending)
            result = m_impl->AdvanceReconcile(state, 0xffffffffu, failure);
        if (report != nullptr)
            *report = state.report;
        if (result != ProjectAssetPipelineResult::Success)
        {
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        return ProjectAssetPipelineResult::Success;
    }

    bool ProjectAssetPipeline::CanImportCandidate(const ProjectAssetCandidateImportRequest& request) const noexcept
    {
        if (m_impl == nullptr || !request.sourcePath.IsFilePath() || !request.sourceIdentity.IsValid() ||
            request.importer == assets::InvalidSourceImporterId || request.importerVersion == 0u || request.sourceGeneration == 0u ||
            request.sourceGeneration != m_impl->sources->GetGeneration())
            return false;

        const assets::SourceRecord* const source = m_impl->sources->LookupResource(request.sourcePath);
        if (source == nullptr || source->sourceIdentity != request.sourceIdentity ||
            source->importState != assets::SourceImportState::Candidate || source->issues != assets::MetadataMissing ||
            source->importer != request.importer || source->availableImporterVersion != request.importerVersion)
            return false;

        const containers::ArraySpan<const assets::SourceRoot> sourceRoots = m_impl->sources->GetRoots();
        if (source->root >= sourceRoots.Count() || sourceRoots[source->root].readOnly)
            return false;

        const assets::SourceImporterDescriptor* const importer =
            FindAuthoringImporter(m_impl->builds.GetImporters(), request.importer, request.importerVersion);
        return importer != nullptr && importer->createImportDescription != nullptr && importer->validateSettings != nullptr;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::ImportCandidate(
        const ProjectAssetCandidateImportRequest& request, assets::AssetId& importedAsset,
        ProjectAssetRestoreReport* const report, ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!request.sourcePath.IsFilePath() || !request.sourceIdentity.IsValid() ||
            request.importer == assets::InvalidSourceImporterId || request.importerVersion == 0u || request.sourceGeneration == 0u)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        if (!CanMutateSources() || m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;
        if (request.sourceGeneration != m_impl->sources->GetGeneration())
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::CandidateUnavailable;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const assets::SourceRecord* const source = m_impl->sources->LookupResource(request.sourcePath);
        if (source == nullptr || source->sourceIdentity != request.sourceIdentity ||
            source->importState != assets::SourceImportState::Candidate || source->issues != assets::MetadataMissing ||
            source->importer != request.importer || source->availableImporterVersion != request.importerVersion)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::CandidateUnavailable;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        const containers::ArraySpan<const assets::SourceRoot> sourceRoots = m_impl->sources->GetRoots();
        if (source->root >= sourceRoots.Count() || sourceRoots[source->root].readOnly)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const containers::ArraySpan<const assets::SourceImporterDescriptor> importers = m_impl->builds.GetImporters();
        const assets::SourceImporterDescriptor* const importer =
            FindAuthoringImporter(importers, request.importer, request.importerVersion);
        if (importer == nullptr || importer->createImportDescription == nullptr || importer->validateSettings == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ImporterUnavailable;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        assets::SourceImportDescription description;
        if (!importer->createImportDescription(*source, description, importer->userData))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SettingsInvalid;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        const ImportDescriptionValidation descriptionValidation = ValidateImportDescription(description, *importer);
        if (descriptionValidation != ImportDescriptionValidation::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = descriptionValidation == ImportDescriptionValidation::LimitExceeded
                                                ? assets::AssetOperationResult::LimitExceeded
                                                : assets::AssetOperationResult::SettingsInvalid;
                failure->operation.importer = request.importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        assets::AssetId generated;
        const ProjectAssetPipelineResult identityResult =
            m_impl->GenerateAvailableAssetIdentity(description, request.importer, generated, failure);
        if (identityResult != ProjectAssetPipelineResult::Success)
            return identityResult;

        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        if (!AppendUniquePath(historyPaths, MetadataPath(request.sourcePath), m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        AssetHistoryRecord inverse;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse) ||
            !PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;

        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, importers, m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult operationResult = assets::ExecuteAssetAdopt(
            context, request.sourcePath, request.importer, request.importerVersion, generated, description, &operation);
        if (operationResult != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }

        importedAsset = generated;
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::ImportExternalSource(
        const ProjectAssetExternalImportRequest& request, assets::AssetId& importedAsset,
        ProjectAssetRestoreReport* const report, ProjectAssetPipelineFailure* const failure) noexcept
    {
        importedAsset = {};
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!request.destinationPath.IsFilePath() || (!request.sourceBytes.Empty() && request.sourceBytes.Data() == nullptr))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        if (!CanMutateSources() || m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        const containers::StringView fileName = filesystem::paths::GetFileName(request.destinationPath);
        if (fileName.Empty() || fileName.EndsWithIgnoreCase(".vmeta") ||
            fileName.EndsWithIgnoreCase(assets::AssetTransactionStageSuffix) ||
            fileName.EndsWithIgnoreCase(assets::AssetTransactionBackupSuffix))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;

        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        u32 rootIndex = 0xffffffffu;
        resources::ResourcePath sourceIdentity;
        for (u32 index = 0u; index < roots.Count(); ++index)
        {
            if (roots[index].readOnly ||
                filesystem::ValidateMutationPath(roots[index].directory, request.destinationPath) != filesystem::MutationPathResult::Success)
                continue;
            const containers::StringView root = roots[index].directory.AsStringView();
            const containers::StringView target = request.destinationPath.AsStringView();
            if (target.Length() <= root.Length())
                continue;
            const resources::ResourcePath identity =
                resources::ResourcePath::FromString(target.Slice(root.Length(), target.Length()));
            if (!identity.IsValid())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                          : ProjectAssetPipelineResult::InvalidArgument;
            rootIndex = index;
            sourceIdentity = identity;
            break;
        }
        if (rootIndex == 0xffffffffu)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const filesystem::AbsolutePath metadataPath = MetadataPath(request.destinationPath);
        if (m_impl->sources->LookupResource(request.destinationPath) != nullptr ||
            m_impl->sources->LookupSource(sourceIdentity) != nullptr || m_impl->files->FileExist(request.destinationPath) ||
            m_impl->files->FileExist(metadataPath))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TargetConflict;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const containers::ArraySpan<const assets::SourceImporterDescriptor> importers = m_impl->builds.GetImporters();
        for (u32 index = 0u; index < importers.Count(); ++index)
        {
            const assets::SourceImporterDescriptor& candidate = importers[index];
            if (!candidate.IsValid())
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationFailed;
                    failure->operation.result = assets::AssetOperationResult::ImporterUnavailable;
                }
                return ProjectAssetPipelineResult::OperationFailed;
            }
            for (u32 previous = 0u; previous < index; ++previous)
                if (importers[previous].id == candidate.id)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::OperationFailed;
                        failure->operation.result = assets::AssetOperationResult::ImporterUnavailable;
                    }
                    return ProjectAssetPipelineResult::OperationFailed;
                }
        }

        const assets::SourceImporterDescriptor* importer = nullptr;
        bool discoveryUnavailable = importers.Empty();
        for (const assets::SourceImporterDescriptor& candidate : importers)
        {
            if (candidate.recognizeSource == nullptr)
            {
                discoveryUnavailable = true;
                continue;
            }
            if (!candidate.recognizeSource(request.destinationPath.AsStringView(), candidate.userData))
                continue;
            if (importer != nullptr)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationFailed;
                    failure->operation.result = assets::AssetOperationResult::AmbiguousSource;
                }
                return ProjectAssetPipelineResult::OperationFailed;
            }
            importer = &candidate;
        }
        if (importer == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = discoveryUnavailable ? assets::AssetOperationResult::ImporterUnavailable
                                                                 : assets::AssetOperationResult::UnsupportedSource;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (importer->createImportDescription == nullptr || importer->validateSettings == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ImportDescriptionUnavailable;
                failure->operation.importer = importer->id;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        assets::SourceRecord synthetic;
        synthetic.path = request.destinationPath;
        synthetic.sourceIdentity = sourceIdentity;
        synthetic.root = rootIndex;
        synthetic.issues = assets::MetadataMissing;
        synthetic.importState = assets::SourceImportState::Candidate;
        synthetic.importer = importer->id;
        synthetic.availableImporterVersion = importer->version;
        synthetic.sourceType = importer->sourceType;
        synthetic.primaryOutputType = importer->primaryOutputType;

        assets::SourceImportDescription description;
        if (!importer->createImportDescription(synthetic, description, importer->userData))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SettingsInvalid;
                failure->operation.importer = importer->id;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        const ImportDescriptionValidation descriptionValidation = ValidateImportDescription(description, *importer);
        if (descriptionValidation != ImportDescriptionValidation::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = descriptionValidation == ImportDescriptionValidation::LimitExceeded
                                                ? assets::AssetOperationResult::LimitExceeded
                                                : assets::AssetOperationResult::SettingsInvalid;
                failure->operation.importer = importer->id;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        assets::AssetId generated;
        const ProjectAssetPipelineResult identityResult =
            m_impl->GenerateAvailableAssetIdentity(description, importer->id, generated, failure);
        if (identityResult != ProjectAssetPipelineResult::Success)
            return identityResult;

        containers::String encodedMetadata;
        assets::AssetOperationFailure metadataFailure;
        const assets::AssetOperationResult metadataResult =
            assets::EncodeAssetImportMetadata(*importer, generated, description, encodedMetadata, &metadataFailure);
        if (metadataResult != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = metadataFailure;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        const u64 sourceByteCount = request.sourceBytes.Count();
        const u64 metadataByteCount = encodedMetadata.Length();
        if (sourceByteCount > m_impl->config.maximumOperationFileBytes ||
            metadataByteCount > m_impl->config.maximumOperationFileBytes ||
            sourceByteCount > m_impl->config.maximumOperationStagedBytes ||
            metadataByteCount > m_impl->config.maximumOperationStagedBytes - sourceByteCount ||
            sourceByteCount > m_impl->config.maximumOperationHistoryBytes ||
            metadataByteCount > m_impl->config.maximumOperationHistoryBytes - sourceByteCount)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded;
                failure->operation.result = assets::AssetOperationResult::LimitExceeded;
                failure->operation.importer = importer->id;
            }
            return ProjectAssetPipelineResult::HistoryLimitExceeded;
        }
        const u64 replayBudgetBytes = sourceByteCount + metadataByteCount;

        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        if (!AppendUniquePath(historyPaths, request.destinationPath, m_impl->config.maximumOperationEntries) ||
            !AppendUniquePath(historyPaths, metadataPath, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        AssetHistoryRecord inverse;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse) ||
            inverse.files.Size() != 2u || inverse.files[0].existed || inverse.files[1].existed ||
            !PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TargetConflict;
                failure->operation.importer = importer->id;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, importers, m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult operationResult = assets::ExecuteAssetCreateAndAdopt(
            context, request.destinationPath, request.sourceBytes, importer->id, importer->version, generated,
            description, &operation);
        if (operationResult != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        else
        {
            // The desired pre-command state is two absent files (zero payload),
            // but Undo must capture the exact committed source + VMETA so Redo
            // remains lossless. Reserve that exact reverse snapshot now.
            inverse.budgetBytes = replayBudgetBytes;
        }

        importedAsset = generated;
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::GenerateImportIdentityCandidate(
        assets::AssetId& identity, ProjectAssetPipelineFailure* const failure) noexcept
    {
        identity = {};
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        for (u32 attempt = 0u; attempt < m_impl->config.maximumIdentityGenerationAttempts; ++attempt)
        {
            assets::AssetId candidate;
            if (!assets::GenerateAssetId(candidate))
                break;
            if (!m_impl->sources->ContainsAssetIdentity(candidate))
            {
                identity = candidate;
                return ProjectAssetPipelineResult::Success;
            }
        }
        return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::IdentityGenerationFailed)
                                  : ProjectAssetPipelineResult::IdentityGenerationFailed;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::UpdateAssetSettings(
        const ProjectAssetSettingsUpdateRequest& request, ProjectAssetRestoreReport* const report,
        ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!request.asset.IsValid() || request.acceptedGeneration == 0u || request.settingsVersion == 0u ||
            (!request.settings.Empty() && request.settings.Data() == nullptr) ||
            request.settings.Count() > assets::MaximumAssetSettingsBytes)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        if (!CanMutateSources() || m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;
        const ProjectAssetState* const accepted = m_impl->accepted.LookupAsset(request.asset);
        if (accepted == nullptr || request.acceptedGeneration != accepted->acceptedGeneration)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SourceUnavailable;
                failure->operation.source = request.asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const assets::SourceRecord* const source = m_impl->sources->LookupAsset(request.asset);
        if (source == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SourceUnavailable;
                failure->operation.source = request.asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        const containers::ArraySpan<const assets::SourceRoot> sourceRoots = m_impl->sources->GetRoots();
        if (source->root >= sourceRoots.Count() || sourceRoots[source->root].readOnly)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
                failure->operation.source = request.asset;
                failure->operation.importer = source->importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const containers::ArraySpan<const assets::SourceImporterDescriptor> importers = m_impl->builds.GetImporters();
        const assets::SourceImporterDescriptor* const importer =
            FindAuthoringImporter(importers, source->importer, source->availableImporterVersion);
        if (importer == nullptr || importer->validateSettings == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ImporterUnavailable;
                failure->operation.source = request.asset;
                failure->operation.importer = source->importer;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const filesystem::AbsolutePath metadataPath = MetadataPath(source->path);
        AssetHistoryRecord inverse;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!CaptureHistory(*m_impl->files, {&metadataPath, 1u}, m_impl->config, inverse) || inverse.files.Size() != 1u ||
            !inverse.files[0].existed || !PrepareHistoryPostConditions(inverse, {&metadataPath, 1u}, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;

        const containers::ArraySpan<const u8> selectedMetadata{inverse.files[0].bytes.TypedData(), inverse.files[0].bytes.Size()};
        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, importers, m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, selectedMetadata,
                                                    {&metadataPath, 1u}, {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult operationResult = assets::ExecuteAssetUpdateSettings(
            context, request.asset, request.settingsVersion, request.settings, &operation);
        if (operationResult != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }

        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    bool ProjectAssetPipeline::CanCommitPlannedImportNow(const assets::AssetId parentAsset,
                                                         const resources::ResourcePath sourceIdentity) const noexcept
    {
        if (m_impl == nullptr || !parentAsset.IsValid() || !sourceIdentity.IsValid() ||
            m_impl->deferredPlannedImport.IsActive())
            return false;
        const bool replacingCohortIntent = m_impl->accepted.cohortIntent.IsActive() &&
                                           m_impl->accepted.cohortIntent.asset == parentAsset &&
                                           m_impl->accepted.cohortIntent.sourceIdentity == sourceIdentity;
        return (replacingCohortIntent ? m_impl->CanReconcileSources() : m_impl->CanMutateSources()) &&
               m_impl->transaction.GetState() == assets::AssetFileTransactionState::Ready &&
               (!m_impl->observation.running || !m_impl->accumulator.HasRescanRequired());
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::QuiesceDeferredPlannedImport(
        ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !m_impl->deferredPlannedImportPrepare.IsActive())
            return ProjectAssetPipelineResult::Success;

        // Jobs is a dependency of Project Workspace. Its drain barrier runs
        // before workspace OnShutdown, so deferring this join to pipeline
        // Shutdown leaves an outstanding task visible to Jobs OnDrain. Do it
        // at workspace OnQuiesce while every file/transaction owner is alive.
        Impl::DeferredPlannedImportPrepare& pending = m_impl->deferredPlannedImportPrepare;
        pending.CancelAndJoin();
        const assets::AssetFileTransactionState state = m_impl->transaction.GetState();
        assets::AssetFileTransactionResult rollback = assets::AssetFileTransactionResult::Success;
        if (state == assets::AssetFileTransactionState::Collecting ||
            state == assets::AssetFileTransactionState::Prepared)
            rollback = m_impl->transaction.Rollback();
        else if (state != assets::AssetFileTransactionState::Ready)
            rollback = assets::AssetFileTransactionResult::InvalidState;
        if (rollback != assets::AssetFileTransactionResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::ShutdownFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.transaction = rollback;
            }
            return ProjectAssetPipelineResult::ShutdownFailed;
        }
        pending.Reset();
        m_impl->deferredPlannedImportStatus = {};
        return ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::TickDeferredPlannedImport(
        ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;

        Impl::DeferredPlannedImportPrepare& pending = m_impl->deferredPlannedImportPrepare;
        if (!pending.IsActive())
            return ProjectAssetPipelineResult::Success;

        const auto updateProgress = [&]() noexcept
        {
            if (m_impl->deferredPlannedImportStatus.state != ProjectAssetDeferredPlannedImportState::PreparingTransaction)
                return;
            const u32 phase = pending.progressPhase.GetValue();
            m_impl->deferredPlannedImportStatus.preparePhase =
                phase == 2u ? ProjectAssetDeferredPlannedImportPreparePhase::Verifying
                            : phase == 1u ? ProjectAssetDeferredPlannedImportPreparePhase::Writing
                                          : ProjectAssetDeferredPlannedImportPreparePhase::None;
            m_impl->deferredPlannedImportStatus.prepareCompletedBytes = pending.progressCompleted.GetValue();
            m_impl->deferredPlannedImportStatus.prepareTotalBytes = pending.progressTotal.GetValue();
        };
        const auto terminalFailure = [&](const ProjectAssetPipelineResult result) noexcept
        {
            updateProgress();
            m_impl->deferredPlannedImportStatus.state = ProjectAssetDeferredPlannedImportState::ContinuationFailed;
            m_impl->deferredPlannedImportStatus.asset = pending.asset;
            m_impl->deferredPlannedImportStatus.result = result;
            pending.Reset();
            return result;
        };
        const auto failTransaction = [&](const assets::AssetFileTransactionResult transactionResult) noexcept
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.source = pending.asset;
                failure->operation.importer = pending.importer;
                failure->operation.transaction = transactionResult;
            }
            return terminalFailure(ProjectAssetPipelineResult::OperationFailed);
        };
        const auto rollbackPrepared = [&]() noexcept -> assets::AssetFileTransactionResult
        {
            const assets::AssetFileTransactionState state = m_impl->transaction.GetState();
            if (state != assets::AssetFileTransactionState::Collecting && state != assets::AssetFileTransactionState::Prepared)
                return assets::AssetFileTransactionResult::InvalidState;
            return m_impl->transaction.Rollback();
        };

        updateProgress();
        if (pending.workRunning)
        {
            if (!pending.workCompleted.GetValue())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Pending)
                                          : ProjectAssetPipelineResult::Pending;
            pending.workRunning = false;
        }

        if (pending.workerResult != assets::AssetFileTransactionResult::Success)
        {
            const assets::AssetFileTransactionResult prepareResult = pending.workerResult;
            const assets::AssetFileTransactionResult rollback = rollbackPrepared();
            if (rollback != assets::AssetFileTransactionResult::Success)
                return failTransaction(rollback);
            if (prepareResult == assets::AssetFileTransactionResult::Conflict)
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::StalePlan;
                return terminalFailure(ProjectAssetPipelineResult::StalePlan);
            }
            return failTransaction(prepareResult);
        }

        // The worker has reached the durable Prepared journal but has not touched
        // any authored target. Keep the source/build owner quiescent until the
        // owner can perform the final disk proof and apply the transaction as one
        // continuation. Busy here is retryable and performs no expensive rescan.
        if (!m_impl->CanSettleDeferredPlannedImportPrepare())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        if (m_impl->accepted.generation != pending.baseAcceptedGeneration)
        {
            const assets::AssetFileTransactionResult rollback = rollbackPrepared();
            if (rollback != assets::AssetFileTransactionResult::Success)
                return failTransaction(rollback);
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::StalePlan;
            return terminalFailure(ProjectAssetPipelineResult::StalePlan);
        }

        ProjectAssetPipelineFailure witnessFailure;
        const ProjectAssetPipelineResult witness = m_impl->VerifyIndependentPlannedImportDiskWitness(
            pending.asset, pending.sourceRoot, pending.sourceDestination, pending.sourceIdentity,
            {pending.stagedOutputPaths.TypedData(), pending.stagedOutputPaths.Size()}, &witnessFailure, true);
        if (witness != ProjectAssetPipelineResult::Success)
        {
            const assets::AssetFileTransactionResult rollback = rollbackPrepared();
            if (rollback != assets::AssetFileTransactionResult::Success)
                return failTransaction(rollback);
            if (failure != nullptr)
                *failure = witnessFailure;
            return terminalFailure(witness);
        }

        assets::AssetFileTransactionResult transaction = m_impl->transaction.ApplyPrepared();
        bool intentPersisted = false;
        if (transaction == assets::AssetFileTransactionResult::Success && pending.cohortIntent.IsActive())
        {
            const ProjectAssetSnapshotResult persisted = m_impl->ReplaceCohortIntent(pending.cohortIntent);
            if (persisted != ProjectAssetSnapshotResult::Success)
            {
                const assets::AssetFileTransactionResult rollback = rollbackPrepared();
                if (failure != nullptr)
                {
                    failure->acceptedSnapshot = persisted;
                    failure->result = rollback == assets::AssetFileTransactionResult::Success
                                          ? ProjectAssetPipelineResult::SnapshotSaveFailed
                                          : ProjectAssetPipelineResult::OperationFailed;
                    if (rollback != assets::AssetFileTransactionResult::Success)
                    {
                        failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                        failure->operation.source = pending.asset;
                        failure->operation.importer = pending.importer;
                        failure->operation.transaction = rollback;
                    }
                }
                return terminalFailure(rollback == assets::AssetFileTransactionResult::Success
                                           ? ProjectAssetPipelineResult::SnapshotSaveFailed
                                           : ProjectAssetPipelineResult::OperationFailed);
            }
            m_impl->plannedImportIntentOwner = true;
            intentPersisted = true;
        }

        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = m_impl->transaction.PublishCommit();
        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = m_impl->transaction.CompleteCommit(
                {pending.historyPaths.TypedData(), pending.historyPaths.Size()},
                {pending.postConditions.TypedData(), pending.postConditions.Size()});

        if (transaction != assets::AssetFileTransactionResult::Success &&
            transaction != assets::AssetFileTransactionResult::CommittedCleanupPending)
        {
            bool rolledBack = false;
            const assets::AssetFileTransactionState state = m_impl->transaction.GetState();
            if (state == assets::AssetFileTransactionState::Collecting || state == assets::AssetFileTransactionState::Prepared)
            {
                const assets::AssetFileTransactionResult rollback = m_impl->transaction.Rollback();
                if (rollback != assets::AssetFileTransactionResult::Success)
                    transaction = rollback;
                else
                    rolledBack = true;
            }
            if (rolledBack && intentPersisted)
            {
                const ProjectAssetSnapshotResult restored = m_impl->ReplaceCohortIntent(pending.cohortIntent);
                if (restored != ProjectAssetSnapshotResult::Success)
                {
                    m_impl->plannedImportIntentOwner = false;
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                        failure->acceptedSnapshot = restored;
                    }
                    return terminalFailure(ProjectAssetPipelineResult::SnapshotSaveFailed);
                }
                intentPersisted = false;
            }
            m_impl->plannedImportIntentOwner = false;
            if (transaction == assets::AssetFileTransactionResult::Conflict)
            {
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::StalePlan;
                return terminalFailure(ProjectAssetPipelineResult::StalePlan);
            }
            return failTransaction(transaction);
        }

        const assets::AssetId committedAsset = pending.asset;
        const assets::SourceImporterId committedImporter = pending.importer;
        const resources::ResourcePath committedSourceIdentity = pending.sourceIdentity;
        const filesystem::AbsolutePath committedSourceDestination = pending.sourceDestination;
        const crypto::Digest256 committedSourceFingerprint = pending.sourceFingerprint;
        if (!AttachHistoryPostConditions(pending.inverse, pending.postConditions))
        {
            ClearAssetOperationHistory();
            pending.inverse.files.Clear();
        }
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(pending.inverse));
        m_impl->BeginDeferredPlannedImport(committedAsset, committedSourceIdentity,
                                           committedSourceDestination, committedSourceFingerprint);
        m_impl->plannedImportIntentOwner = false;
        pending.Reset();

        if (failure != nullptr)
        {
            failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
            failure->operation.source = committedAsset;
            failure->operation.importer = committedImporter;
            failure->operation.transaction = transaction;
        }
        return ProjectAssetPipelineResult::CommittedReconciliationPending;
    }

    ProjectAssetDeferredPlannedImportStatus ProjectAssetPipeline::GetDeferredPlannedImportStatus() const noexcept
    {
        if (m_impl == nullptr)
            return {};
        ProjectAssetDeferredPlannedImportStatus status = m_impl->deferredPlannedImportStatus;
        if (status.state == ProjectAssetDeferredPlannedImportState::PreparingTransaction &&
            m_impl->deferredPlannedImportPrepare.IsActive())
        {
            const u32 phase = m_impl->deferredPlannedImportPrepare.progressPhase.GetValue();
            status.preparePhase = phase == 2u ? ProjectAssetDeferredPlannedImportPreparePhase::Verifying
                                              : phase == 1u ? ProjectAssetDeferredPlannedImportPreparePhase::Writing
                                                            : ProjectAssetDeferredPlannedImportPreparePhase::None;
            status.prepareCompletedBytes = m_impl->deferredPlannedImportPrepare.progressCompleted.GetValue();
            status.prepareTotalBytes = m_impl->deferredPlannedImportPrepare.progressTotal.GetValue();
        }
        return status;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::CommitPlannedImport(
        const assets::PlannedAssetImportRequest& request, assets::AssetId& importedAsset,
        ProjectAssetRestoreReport* const report, ProjectAssetPipelineFailure* const failure,
        const bool deferPostCommitReconciliation) noexcept
    {
        importedAsset = {};
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (request.importDescription == nullptr || !request.parentAsset.IsValid() ||
            request.importer == assets::InvalidSourceImporterId || request.importerVersion == 0u ||
            request.sourceDatabaseGeneration == 0u || request.acceptedStateGeneration == 0u ||
            !request.sourceOrigin.IsFilePath() || !request.sourceRoot.IsDirectoryPath() ||
            !request.sourceDestination.IsFilePath() || !request.sourceIdentity.IsValid() ||
            request.sourceFingerprint.IsEmpty() || request.sourceBytes.Empty() || request.sourceBytes.Data() == nullptr ||
            (!request.dependencyWitnesses.Empty() && request.dependencyWitnesses.Data() == nullptr) ||
            request.dependencyWitnesses.Count() > m_impl->config.maximumOperationEntries ||
            request.dependencyWitnesses.Count() == ~u32{0})
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidArgument)
                                      : ProjectAssetPipelineResult::InvalidArgument;
        const bool replacingCohortIntent = m_impl->accepted.cohortIntent.IsActive() &&
                                           m_impl->accepted.cohortIntent.asset == request.parentAsset &&
                                           m_impl->accepted.cohortIntent.sourceIdentity == request.sourceIdentity;
        if (m_impl->deferredPlannedImport.IsActive() ||
            (replacingCohortIntent ? !m_impl->CanReconcileSources() : !CanMutateSources()) ||
            m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;

        const auto stale = [&](const std::source_location site = std::source_location::current()) noexcept
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::StalePlan;
                failure->stalePlanSite = site.line();
            }
            return ProjectAssetPipelineResult::StalePlan;
        };
        const bool observationFenceRequired = m_impl->observation.running;
        SourceChangeSnapshot observationSnapshot;
        SourceChangeSnapshot committedObservationSnapshot;
        // A first import copied from outside the project with no adopted project
        // dependencies is not bound to unrelated catalog revisions. The current
        // destination/parent/output identities are checked below against the
        // live catalog and the complete authoritative on-disk catalog before
        // any durable mutation, rather than comparing global generations.
        // Retain the strict catalog fence for existing project sources,
        // reimports and imports that borrow project dependency identities.
        const bool requiresGlobalCatalogFence = request.hasPersistedMetadataFingerprint ||
                                                !request.copySourceToDestination ||
                                                !request.dependencyWitnesses.Empty();
        const auto pendingObservation = [&]() noexcept
        {
            // No files have been committed on this path. A first, independent
            // external import can retain its review while a source watcher
            // catches up; strict imports still require explicit reinspection.
            if (requiresGlobalCatalogFence)
                return stale();
            if (failure != nullptr)
                failure->result = ProjectAssetPipelineResult::Busy;
            return ProjectAssetPipelineResult::Busy;
        };
        if ((requiresGlobalCatalogFence &&
             (request.sourceDatabaseGeneration != m_impl->sources->GetGeneration() ||
              request.acceptedStateGeneration != m_impl->accepted.generation)) ||
            crypto::Sha256(request.sourceBytes.Data(), request.sourceBytes.Count()) != request.sourceFingerprint)
            return stale();

        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        bool writablePlanningRoot = false;
        for (const assets::SourceRoot& root : roots)
            writablePlanningRoot = writablePlanningRoot || (!root.readOnly && root.directory == request.sourceRoot);
        if (!writablePlanningRoot || !SourceIdentityMatchesRoot(request.sourceRoot, request.sourceDestination, request.sourceIdentity) ||
            request.copySourceToDestination != !(request.sourceOrigin == request.sourceDestination))
            return stale();

        const containers::ArraySpan<const assets::SourceImporterDescriptor> importers = m_impl->builds.GetImporters();
        const assets::SourceImporterDescriptor* const importer =
            FindAuthoringImporter(importers, request.importer, request.importerVersion);
        if (importer == nullptr || importer->recognizeSource == nullptr || importer->validateSettings == nullptr ||
            !importer->recognizeSource(request.sourceDestination.AsStringView(), importer->userData))
            return stale();
        const ImportDescriptionValidation descriptionValidation = ValidateImportDescription(*request.importDescription, *importer);
        if (descriptionValidation != ImportDescriptionValidation::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = descriptionValidation == ImportDescriptionValidation::LimitExceeded
                                                ? assets::AssetOperationResult::LimitExceeded
                                                : assets::AssetOperationResult::SettingsInvalid;
                failure->operation.importer = request.importer;
                failure->operation.source = request.parentAsset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        const bool reimport = request.hasPersistedMetadataFingerprint;
        if (reimport && (request.persistedMetadataFingerprint.IsEmpty() || request.copySourceToDestination))
            return stale();
        if (!reimport && !request.persistedMetadataFingerprint.IsEmpty())
            return stale();

        const assets::SourceRecord* parentSource = nullptr;
        if (reimport)
        {
            parentSource = m_impl->sources->LookupAsset(request.parentAsset);
            if (parentSource == nullptr || parentSource->path != request.sourceDestination ||
                parentSource->sourceIdentity != request.sourceIdentity || parentSource->metadataResult != assets::MetadataResult::Success ||
                parentSource->importState != assets::SourceImportState::MetadataSelected || parentSource->importer != request.importer ||
                parentSource->availableImporterVersion != request.importerVersion || !(parentSource->metadata.id == request.parentAsset) ||
                parentSource->root >= roots.Count() || roots[parentSource->root].directory != request.sourceRoot || roots[parentSource->root].readOnly)
                return stale();
        }
        else
        {
            if (m_impl->sources->ContainsAssetIdentity(request.parentAsset))
                return stale();
            if (!request.copySourceToDestination)
            {
                parentSource = m_impl->sources->LookupResource(request.sourceDestination);
                if (parentSource == nullptr || parentSource->sourceIdentity != request.sourceIdentity ||
                    parentSource->importState != assets::SourceImportState::Candidate ||
                    parentSource->issues != assets::MetadataMissing || parentSource->metadataResult == assets::MetadataResult::Success ||
                    parentSource->importer != request.importer || parentSource->availableImporterVersion != request.importerVersion ||
                    parentSource->root >= roots.Count() || roots[parentSource->root].directory != request.sourceRoot || roots[parentSource->root].readOnly)
                    return stale();
            }
            else if (m_impl->sources->LookupResource(request.sourceDestination) != nullptr)
                return stale();
        }

        const filesystem::AbsolutePath parentMetadataPath = MetadataPath(request.sourceDestination);

        struct PlannedAssertion final
        {
            filesystem::AbsolutePath path;
            crypto::Digest256 digest;
        };
        containers::DynamicArray<PlannedAssertion> assertions(memory::pools::Assets::GetInstance());
        const u32 maximumAssertions = request.dependencyWitnesses.Count() > (~u32{0} - 1u) / 2u
                                          ? ~u32{0}
                                          : request.dependencyWitnesses.Count() * 2u + 1u;
        if (maximumAssertions == ~u32{0})
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        assertions.Reserve(maximumAssertions);
        if (assertions.Capacity() < maximumAssertions)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const auto appendAssertion = [&](const filesystem::AbsolutePath& path, const crypto::Digest256& digest) noexcept
        {
            if (!path.IsFilePath() || digest.IsEmpty())
                return false;
            for (const PlannedAssertion& existing : assertions)
                if (existing.path == path)
                    return existing.digest == digest;
            const u32 previous = assertions.Size();
            assertions.PushBack({path, digest});
            return assertions.Size() == previous + 1u;
        };
        if (!request.copySourceToDestination && !appendAssertion(request.sourceDestination, request.sourceFingerprint))
            return stale();

        struct AuxiliaryAdoption final
        {
            AuxiliaryAdoption() noexcept = default;
            const assets::SourceRecord* source = nullptr;
            const assets::SourceImporterDescriptor* importer = nullptr;
            assets::SourceImportDescription description;
            assets::AssetId identity;
            containers::String encodedMetadata;
        };
        containers::DynamicArray<AuxiliaryAdoption> adoptions(memory::pools::Assets::GetInstance());
        containers::DynamicArray<assets::PlannedAssetAdoptionReceipt> adoptionReceipts(memory::pools::Assets::GetInstance());
        containers::DynamicArray<resources::ResourcePath> stagedOutputPaths(memory::pools::Assets::GetInstance());
        containers::DynamicArray<assets::AssetId> reservedAssetIds(memory::pools::Assets::GetInstance());
        if (request.importDescription->outputs.Size() > ~u32{0} - request.dependencyWitnesses.Count())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const u32 maximumStagedOutputs = request.importDescription->outputs.Size() + request.dependencyWitnesses.Count();
        const u32 maximumReservedIds = request.dependencyWitnesses.Count() + 1u;
        adoptionReceipts.Reserve(request.dependencyWitnesses.Count());
        stagedOutputPaths.Reserve(maximumStagedOutputs);
        reservedAssetIds.Reserve(maximumReservedIds);
        if (adoptionReceipts.Capacity() < request.dependencyWitnesses.Count() ||
            stagedOutputPaths.Capacity() < maximumStagedOutputs || reservedAssetIds.Capacity() < maximumReservedIds)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        reservedAssetIds.PushBack(request.parentAsset);
        if (reservedAssetIds.Size() != 1u)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;

        const auto appendOutputPaths = [&](const assets::AssetId identity, const assets::SourceImportDescription& description,
                                           const assets::SourceRecord* const currentOwner) noexcept
        {
            const u32 originalCount = stagedOutputPaths.Size();
            for (const assets::AssetOutput& output : description.outputs)
            {
                const resources::ResourcePath path = assets::GetAssetOutputResourcePath(identity, output.key);
                if (!path.IsValid())
                {
                    stagedOutputPaths.Resize(originalCount);
                    return false;
                }
                for (const resources::ResourcePath existing : stagedOutputPaths)
                    if (existing == path)
                    {
                        stagedOutputPaths.Resize(originalCount);
                        return false;
                    }
                const bool existingCatalogPath = m_impl->sources->ContainsOutputPath(path);
                bool ownedCurrentPath = false;
                if (currentOwner != nullptr)
                    for (const assets::AssetOutput& current : currentOwner->metadata.outputs)
                        ownedCurrentPath = ownedCurrentPath || assets::GetAssetOutputResourcePath(currentOwner->metadata.id, current.key) == path;
                if ((existingCatalogPath && !ownedCurrentPath) || (m_impl->streamer->ContainsResourcePath(path) && !ownedCurrentPath))
                {
                    stagedOutputPaths.Resize(originalCount);
                    return false;
                }
                const u32 previous = stagedOutputPaths.Size();
                stagedOutputPaths.PushBack(path);
                if (stagedOutputPaths.Size() != previous + 1u)
                {
                    stagedOutputPaths.Resize(originalCount);
                    return false;
                }
            }
            return true;
        };
        if (!appendOutputPaths(request.parentAsset, *request.importDescription, reimport ? parentSource : nullptr))
            return stale();

        for (const assets::PlannedAssetDependencyWitness& witness : request.dependencyWitnesses)
        {
            if (!witness.sourceIdentity.IsValid() || !witness.physicalPath.IsFilePath() || !witness.sourceExists ||
                !witness.hasSourceContentFingerprint || witness.sourceContentFingerprint.IsEmpty())
                return stale();
            const assets::SourceRecord* const source = m_impl->sources->LookupSource(witness.sourceIdentity);
            if (source == nullptr || source->path != witness.physicalPath ||
                !appendAssertion(source->path, witness.sourceContentFingerprint))
                return stale();

            if (witness.adoptCandidate)
            {
                if (witness.metadataExists || witness.hasMetadataFingerprint || !witness.metadataFingerprint.IsEmpty() ||
                    witness.selectedResource.IsValid() || source->importState != assets::SourceImportState::Candidate ||
                    source->metadataResult == assets::MetadataResult::Success || source->issues != assets::MetadataMissing ||
                    source->path == request.sourceDestination)
                    return stale();
                const bool writable = source->root < roots.Count() && !roots[source->root].readOnly;
                if (!writable)
                    return stale();
                const assets::SourceImporterDescriptor* const dependencyImporter =
                    FindAuthoringImporter(importers, source->importer, source->availableImporterVersion);
                if (dependencyImporter == nullptr || dependencyImporter->createImportDescription == nullptr ||
                    dependencyImporter->validateSettings == nullptr)
                    return stale();
                const u32 previousAdoptions = adoptions.Size();
                AuxiliaryAdoption& adoption = adoptions.EmplaceBack();
                if (adoptions.Size() != previousAdoptions + 1u)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                              : ProjectAssetPipelineResult::HistoryLimitExceeded;
                adoption.source = source;
                adoption.importer = dependencyImporter;
                if (!dependencyImporter->createImportDescription(*source, adoption.description, dependencyImporter->userData) ||
                    ValidateImportDescription(adoption.description, *dependencyImporter) != ImportDescriptionValidation::Success)
                    return stale();

                bool identityReady = false;
                for (u32 attempt = 0u; attempt < m_impl->config.maximumIdentityGenerationAttempts && !identityReady; ++attempt)
                {
                    ProjectAssetPipelineFailure generatedFailure;
                    if (m_impl->GenerateAvailableAssetIdentity(adoption.description, dependencyImporter->id,
                                                               adoption.identity, &generatedFailure) != ProjectAssetPipelineResult::Success)
                        return failure != nullptr ? (*failure = generatedFailure, generatedFailure.result)
                                                  : generatedFailure.result;
                    bool reserved = false;
                    for (const assets::AssetId& staged : reservedAssetIds)
                        reserved = reserved || staged == adoption.identity;
                    if (reserved || !appendOutputPaths(adoption.identity, adoption.description, nullptr))
                    {
                        adoption.identity = {};
                        continue;
                    }
                    const u32 previousReserved = reservedAssetIds.Size();
                    reservedAssetIds.PushBack(adoption.identity);
                    if (reservedAssetIds.Size() != previousReserved + 1u)
                        return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                                  : ProjectAssetPipelineResult::HistoryLimitExceeded;
                    identityReady = true;
                }
                if (!identityReady)
                    return stale();

                const assets::AssetOutput* primaryOutput = nullptr;
                for (const assets::AssetOutput& candidate : adoption.description.outputs)
                {
                    if (candidate.type != dependencyImporter->primaryOutputType)
                        continue;
                    if (primaryOutput != nullptr)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::OperationFailed;
                            failure->operation.result = assets::AssetOperationResult::AmbiguousSource;
                            failure->operation.source = adoption.identity;
                            failure->operation.importer = dependencyImporter->id;
                        }
                        return ProjectAssetPipelineResult::OperationFailed;
                    }
                    primaryOutput = &candidate;
                }
                if (primaryOutput == nullptr)
                    return stale();

                const resources::ResourceReference adoptedResource =
                    assets::GetAssetOutputReference(adoption.identity, *primaryOutput);
                if (!adoptedResource.IsValid() || !adoptedResource.IsTyped())
                    return stale();
                const u32 previousReceipts = adoptionReceipts.Size();
                adoptionReceipts.PushBack({source->sourceIdentity, adoption.identity, adoptedResource});
                if (adoptionReceipts.Size() != previousReceipts + 1u)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                              : ProjectAssetPipelineResult::HistoryLimitExceeded;

                assets::AssetOperationFailure metadataFailure;
                if (assets::EncodeAssetImportMetadata(*dependencyImporter, adoption.identity, adoption.description,
                                                      adoption.encodedMetadata, &metadataFailure) != assets::AssetOperationResult::Success)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::OperationFailed;
                        failure->operation = metadataFailure;
                    }
                    return ProjectAssetPipelineResult::OperationFailed;
                }
            }
            else
            {
                if (!witness.metadataExists || !witness.hasMetadataFingerprint || witness.metadataFingerprint.IsEmpty() ||
                    !witness.asset.IsValid() || !witness.selectedResource.IsValid() || !witness.selectedResource.IsTyped() ||
                    source->metadataResult != assets::MetadataResult::Success || source->importState != assets::SourceImportState::MetadataSelected ||
                    !(source->metadata.id == witness.asset) || m_impl->sources->LookupOutput(witness.selectedResource) != source)
                    return stale();
                assets::AssetMetadata liveMetadata;
                crypto::Digest256 rawMetadataDigest;
                crypto::Digest256 canonicalMetadataDigest;
                const filesystem::AbsolutePath metadataPath = MetadataPath(source->path);
                if (!ReadMetadataSnapshot(*m_impl->files, metadataPath, liveMetadata, rawMetadataDigest, canonicalMetadataDigest) ||
                    canonicalMetadataDigest != witness.metadataFingerprint || !(liveMetadata.id == witness.asset))
                    return stale();
                if (metadataPath == parentMetadataPath)
                {
                    if (!reimport || canonicalMetadataDigest != request.persistedMetadataFingerprint)
                        return stale();
                }
                else if (!appendAssertion(metadataPath, rawMetadataDigest))
                    return stale();
            }
        }

        assets::SourceImportDescription finalizedParentDescription;
        const assets::SourceImportDescription* parentDescription = request.importDescription;
        if (request.finalizeSettings != nullptr && !adoptionReceipts.Empty())
        {
            finalizedParentDescription.settingsVersion = request.importDescription->settingsVersion;
            const assets::AssetOperationResult finalized = request.finalizeSettings(
                request.importDescription->settingsVersion,
                {request.importDescription->settings.TypedData(), request.importDescription->settings.Size()},
                {adoptionReceipts.TypedData(), adoptionReceipts.Size()}, finalizedParentDescription.settings,
                request.finalizeSettingsUserData);
            if (finalized != assets::AssetOperationResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::OperationFailed;
                    failure->operation.result = finalized;
                    failure->operation.source = request.parentAsset;
                    failure->operation.importer = request.importer;
                }
                return ProjectAssetPipelineResult::OperationFailed;
            }
            finalizedParentDescription.outputs.Reserve(request.importDescription->outputs.Size());
            if (finalizedParentDescription.outputs.Capacity() < request.importDescription->outputs.Size())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;
            for (const assets::AssetOutput& reviewed : request.importDescription->outputs)
            {
                assets::AssetOutput copied;
                copied.key = reviewed.key;
                copied.type = reviewed.type;
                if (copied.key != reviewed.key)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                              : ProjectAssetPipelineResult::HistoryLimitExceeded;
                const u32 previous = finalizedParentDescription.outputs.Size();
                finalizedParentDescription.outputs.PushBack(static_cast<assets::AssetOutput&&>(copied));
                if (finalizedParentDescription.outputs.Size() != previous + 1u)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                              : ProjectAssetPipelineResult::HistoryLimitExceeded;
            }
            parentDescription = &finalizedParentDescription;
        }

        containers::String parentMetadata;
        assets::AssetOperationFailure metadataFailure;
        if (assets::EncodeAssetImportMetadata(*importer, request.parentAsset, *parentDescription,
                                              parentMetadata, &metadataFailure) != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = metadataFailure;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        if ((request.copySourceToDestination &&
             !AppendUniquePath(historyPaths, request.sourceDestination, m_impl->config.maximumOperationEntries)) ||
            !AppendUniquePath(historyPaths, parentMetadataPath, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        for (const AuxiliaryAdoption& adoption : adoptions)
            if (!AppendUniquePath(historyPaths, MetadataPath(adoption.source->path), m_impl->config.maximumOperationEntries))
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;

        AssetHistoryRecord inverse;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse) ||
            !PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;

        crypto::Digest256 expectedParentMetadataRaw;
        if (reimport)
        {
            bool found = false;
            for (const AssetHistoryFile& state : inverse.files)
            {
                if (state.path != parentMetadataPath)
                    continue;
                assets::AssetMetadata parsed;
                crypto::Digest256 canonical;
                if (!state.existed ||
                    !DecodeMetadataSnapshot({state.bytes.TypedData(), state.bytes.Size()}, parsed,
                                            expectedParentMetadataRaw, canonical) ||
                    canonical != request.persistedMetadataFingerprint || !(parsed.id == request.parentAsset) ||
                    parsed.importer != request.importer || parsed.importerVersion != request.importerVersion)
                    return stale();
                found = true;
                break;
            }
            if (!found)
                return stale();
        }
        else
        {
            for (const AssetHistoryFile& state : inverse.files)
                if ((state.path == parentMetadataPath || (request.copySourceToDestination && state.path == request.sourceDestination)) && state.existed)
                    return stale();
            for (const AuxiliaryAdoption& adoption : adoptions)
                for (const AssetHistoryFile& state : inverse.files)
                    if (state.path == MetadataPath(adoption.source->path) && state.existed)
                        return stale();
        }

        u64 postMutationBytes = parentMetadata.Length();
        if (request.copySourceToDestination)
        {
            if (request.sourceBytes.Count() > ~u64{0} - postMutationBytes)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;
            postMutationBytes += request.sourceBytes.Count();
        }
        for (const AuxiliaryAdoption& adoption : adoptions)
        {
            if (adoption.encodedMetadata.Length() > ~u64{0} - postMutationBytes)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;
            postMutationBytes += adoption.encodedMetadata.Length();
        }
        if (postMutationBytes > m_impl->config.maximumOperationHistoryBytes)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        inverse.budgetBytes = std::max(inverse.byteCount, postMutationBytes);

        if (assertions.Size() > m_impl->config.maximumOperationEntries ||
            historyPaths.Size() > m_impl->config.maximumOperationEntries - assertions.Size())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;

        // Source-change callbacks are intentionally only advisory: a filesystem
        // edit can exist before its watcher notification is delivered. Rebuild a
        // complete, non-published candidate from disk immediately before durable
        // admission and require it to compare unchanged against the exact
        // accepted generation reviewed by the plan. This authoritatively covers
        // catalog-wide identity/output/reference facts (including retirement
        // blockers) without moving model policy into the pipeline. Changes after
        // this admission point are ordered after this source transaction and are
        // reconciled by the normal post-commit rescan.
        if (requiresGlobalCatalogFence)
        {
            assets::SourceDatabase verifiedCatalog;
            ProjectAssetAcceptedBaseline verifiedAccepted(m_impl->config.changeState);
            containers::DynamicArray<resources::ResourceReference> verifiedDesired(memory::pools::Assets::GetInstance());
            containers::HashMap<resources::ResourceId, u8> verifiedDesiredLookup(memory::pools::Assets::GetInstance());
            containers::DynamicArray<filesystem::AbsolutePath> verifiedLooseFiles(memory::pools::Assets::GetInstance());
            filesystem::ScanResult verificationScan = filesystem::ScanResult::Success;
            const assets::SourceDatabaseResult verification = PrepareProjectAssetCandidate(
                *m_impl->files, roots, importers, m_impl->config.build.target, m_impl->config.build.sourceData,
                m_impl->config.operationJournalPath, m_impl->accepted, m_impl->config.maximumPublishedResources,
                m_impl->config.publication.looseRoot, m_impl->config.maximumLooseFiles, verifiedCatalog, verifiedAccepted,
                verifiedDesired, verifiedDesiredLookup, verifiedLooseFiles, &verificationScan);
            if (verification != assets::SourceDatabaseResult::Success)
            {
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                    failure->sources = verification;
                }
                return ProjectAssetPipelineResult::ReconciliationFailed;
            }
            if (verifiedAccepted.generation != request.acceptedStateGeneration)
                return stale();
        }
        else
        {
            const ProjectAssetPipelineResult verified = m_impl->VerifyIndependentPlannedImportDiskWitness(
                request.parentAsset, request.sourceRoot, request.sourceDestination, request.sourceIdentity,
                {stagedOutputPaths.TypedData(), stagedOutputPaths.Size()}, failure);
            if (verified != ProjectAssetPipelineResult::Success)
                return verified;
        }

        // P5 durable cutover intent. Build the exact predecessor/successor sets
        // before touching the source transaction so allocation/limit failure is
        // still a clean precommit failure. If this planned import supersedes an
        // earlier failed successor, retain that intent's original predecessor set
        // rather than treating the un-published accepted schema as last-known-good.
        ProjectAssetCohortIntent preparedCohortIntent;
        if (!parentDescription->outputs.Empty())
        {
            const ProjectAssetCohortIntent& existingIntent = m_impl->accepted.cohortIntent;
            const ProjectAssetState* const previousAccepted = m_impl->accepted.LookupAsset(request.parentAsset);
            const u32 predecessorCount = replacingCohortIntent
                                             ? existingIntent.predecessorOutputs.Size()
                                             : previousAccepted != nullptr ? previousAccepted->outputs.Count() : 0u;
            const u32 successorCount = parentDescription->outputs.Size();
            if (predecessorCount > m_impl->config.automaticRecook.maximumPendingOutputs ||
                successorCount > m_impl->config.automaticRecook.maximumPendingOutputs)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;
            preparedCohortIntent.predecessorOutputs.Reserve(predecessorCount);
            preparedCohortIntent.successorOutputs.Reserve(successorCount);
            if (preparedCohortIntent.predecessorOutputs.Capacity() < predecessorCount ||
                preparedCohortIntent.successorOutputs.Capacity() < successorCount)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;

            if (replacingCohortIntent)
            {
                for (const resources::ResourceReference output : existingIntent.predecessorOutputs)
                    preparedCohortIntent.predecessorOutputs.PushBack(output);
            }
            else if (previousAccepted != nullptr)
            {
                for (const ProjectAssetOutputState& output : previousAccepted->outputs)
                    preparedCohortIntent.predecessorOutputs.PushBack(output.resource);
            }
            for (const assets::AssetOutput& output : parentDescription->outputs)
            {
                const resources::ResourceReference resource = assets::GetAssetOutputReference(request.parentAsset, output);
                if (!resource.IsValid() || !resource.IsTyped())
                    return stale();
                preparedCohortIntent.successorOutputs.PushBack(resource);
            }
            if (preparedCohortIntent.predecessorOutputs.Size() != predecessorCount ||
                preparedCohortIntent.successorOutputs.Size() != successorCount)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;

            const auto resourceLess = [](const resources::ResourceReference left,
                                         const resources::ResourceReference right) noexcept
            {
                if (left.GetPath().Id() != right.GetPath().Id())
                    return left.GetPath().Id() < right.GetPath().Id();
                return left.ExpectedType() < right.ExpectedType();
            };
            if (preparedCohortIntent.predecessorOutputs.Size() > 1u)
                std::sort(preparedCohortIntent.predecessorOutputs.Begin(), preparedCohortIntent.predecessorOutputs.End(), resourceLess);
            if (preparedCohortIntent.successorOutputs.Size() > 1u)
                std::sort(preparedCohortIntent.successorOutputs.Begin(), preparedCohortIntent.successorOutputs.End(), resourceLess);
            for (u32 index = 1u; index < preparedCohortIntent.predecessorOutputs.Size(); ++index)
                if (!resourceLess(preparedCohortIntent.predecessorOutputs[index - 1u],
                                  preparedCohortIntent.predecessorOutputs[index]))
                    return stale();
            for (u32 index = 1u; index < preparedCohortIntent.successorOutputs.Size(); ++index)
                if (!resourceLess(preparedCohortIntent.successorOutputs[index - 1u],
                                  preparedCohortIntent.successorOutputs[index]))
                    return stale();

            assets::AssetMetadata targetMetadata;
            crypto::Digest256 targetRawDigest;
            crypto::Digest256 targetCanonicalDigest;
            if (!DecodeMetadataSnapshot(
                    {reinterpret_cast<const u8*>(parentMetadata.AsChar()), parentMetadata.Length()}, targetMetadata,
                    targetRawDigest, targetCanonicalDigest) ||
                !(targetMetadata.id == request.parentAsset))
                return stale();
            preparedCohortIntent.asset = request.parentAsset;
            preparedCohortIntent.sourceIdentity = request.sourceIdentity;
            preparedCohortIntent.reviewedAcceptedGeneration = request.acceptedStateGeneration;
            preparedCohortIntent.targetMetadataFingerprint = targetCanonicalDigest;
            if (!preparedCohortIntent.IsActive())
                return stale();
        }

        if (observationFenceRequired)
        {
            observationSnapshot = m_impl->accumulator.GetChangeSnapshot();
            if (observationSnapshot.overflow)
                return stale();
            if (m_impl->accumulator.HasRescanRequired())
                return pendingObservation();
        }

        struct PlannedImportIntentOwnerGuard final
        {
            Impl* owner = nullptr;
            ~PlannedImportIntentOwnerGuard()
            {
                if (owner != nullptr)
                    owner->plannedImportIntentOwner = false;
            }
        } intentOwnerGuard;
        const auto restorePreviousIntentAfterRollback = [&]() noexcept -> ProjectAssetSnapshotResult
        {
            if (intentOwnerGuard.owner == nullptr)
                return ProjectAssetSnapshotResult::Success;
            const ProjectAssetSnapshotResult restored = m_impl->ReplaceCohortIntent(preparedCohortIntent);
            if (restored == ProjectAssetSnapshotResult::Success)
            {
                m_impl->plannedImportIntentOwner = false;
                intentOwnerGuard.owner = nullptr;
            }
            return restored;
        };

        assets::AssetFileTransactionResult transaction = m_impl->transaction.Begin();
        if (transaction != assets::AssetFileTransactionResult::Success)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                      : ProjectAssetPipelineResult::Busy;
        for (const PlannedAssertion& assertion : assertions)
        {
            transaction = m_impl->transaction.StageAssertDigestMatches(assertion.path, assertion.digest);
            if (transaction != assets::AssetFileTransactionResult::Success)
                break;
        }
        if (transaction == assets::AssetFileTransactionResult::Success && request.copySourceToDestination)
            transaction = m_impl->transaction.StageCreate(request.sourceDestination, request.sourceBytes);
        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = reimport
                              ? m_impl->transaction.StageWriteIfDigestMatches(
                                    parentMetadataPath, expectedParentMetadataRaw,
                                    {reinterpret_cast<const u8*>(parentMetadata.AsChar()), parentMetadata.Length()})
                              : m_impl->transaction.StageCreate(
                                    parentMetadataPath,
                                    {reinterpret_cast<const u8*>(parentMetadata.AsChar()), parentMetadata.Length()});
        for (const AuxiliaryAdoption& adoption : adoptions)
        {
            if (transaction != assets::AssetFileTransactionResult::Success)
                break;
            transaction = m_impl->transaction.StageCreate(
                MetadataPath(adoption.source->path),
                {reinterpret_cast<const u8*>(adoption.encodedMetadata.AsChar()), adoption.encodedMetadata.Length()});
        }
        const bool asynchronousIndependentFirstImport =
            transaction == assets::AssetFileTransactionResult::Success && deferPostCommitReconciliation &&
            report == nullptr && !observationFenceRequired && !requiresGlobalCatalogFence &&
            request.copySourceToDestination && !reimport && request.dependencyWitnesses.Empty() &&
            !replacingCohortIntent && preparedCohortIntent.IsActive() &&
            preparedCohortIntent.predecessorOutputs.Empty();
        if (asynchronousIndependentFirstImport)
        {
            Impl::DeferredPlannedImportPrepare& pending = m_impl->deferredPlannedImportPrepare;
            pending.Reset();
            pending.inverse = static_cast<AssetHistoryRecord&&>(inverse);
            pending.historyPaths.Swap(historyPaths);
            pending.postConditions.Swap(postConditions);
            pending.cohortIntent.Swap(preparedCohortIntent);
            pending.stagedOutputPaths.Swap(stagedOutputPaths);
            pending.asset = request.parentAsset;
            pending.importer = request.importer;
            pending.sourceRoot = request.sourceRoot;
            pending.sourceIdentity = request.sourceIdentity;
            pending.sourceDestination = request.sourceDestination;
            pending.sourceFingerprint = request.sourceFingerprint;
            pending.baseAcceptedGeneration = m_impl->accepted.generation;
            pending.active = true;
            if (!pending.Dispatch(m_impl->transaction))
            {
                pending.active = false;
                const assets::AssetFileTransactionResult rollback = m_impl->transaction.Rollback();
                pending.Reset();
                if (rollback != assets::AssetFileTransactionResult::Success)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::OperationFailed;
                        failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                        failure->operation.source = request.parentAsset;
                        failure->operation.importer = request.importer;
                        failure->operation.transaction = rollback;
                    }
                    return ProjectAssetPipelineResult::OperationFailed;
                }
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                          : ProjectAssetPipelineResult::Busy;
            }

            m_impl->deferredPlannedImportStatus = {};
            m_impl->deferredPlannedImportStatus.state = ProjectAssetDeferredPlannedImportState::PreparingTransaction;
            m_impl->deferredPlannedImportStatus.asset = request.parentAsset;
            m_impl->deferredPlannedImportStatus.result = ProjectAssetPipelineResult::Pending;
            return ProjectAssetPipelineResult::Pending;
        }
        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = m_impl->transaction.Prepare();
        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = m_impl->transaction.ApplyPrepared();
        if (transaction == assets::AssetFileTransactionResult::Success && preparedCohortIntent.IsActive())
        {
            const ProjectAssetSnapshotResult intentPersisted = m_impl->ReplaceCohortIntent(preparedCohortIntent);
            if (intentPersisted != ProjectAssetSnapshotResult::Success)
            {
                const assets::AssetFileTransactionResult rollback = m_impl->transaction.Rollback();
                if (failure != nullptr)
                {
                    failure->acceptedSnapshot = intentPersisted;
                    failure->result = rollback == assets::AssetFileTransactionResult::Success
                                          ? ProjectAssetPipelineResult::SnapshotSaveFailed
                                          : ProjectAssetPipelineResult::OperationFailed;
                    if (rollback != assets::AssetFileTransactionResult::Success)
                    {
                        failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                        failure->operation.source = request.parentAsset;
                        failure->operation.importer = request.importer;
                        failure->operation.transaction = rollback;
                    }
                }
                return rollback == assets::AssetFileTransactionResult::Success
                           ? ProjectAssetPipelineResult::SnapshotSaveFailed
                           : ProjectAssetPipelineResult::OperationFailed;
            }
            m_impl->plannedImportIntentOwner = true;
            intentOwnerGuard.owner = m_impl;
        }
        if (transaction == assets::AssetFileTransactionResult::Success)
        {
            if (observationFenceRequired)
            {
                struct StableTransactionPublication final
                {
                    assets::AssetFileTransaction* transaction = nullptr;
                    assets::AssetFileTransactionResult result = assets::AssetFileTransactionResult::InvalidState;
                    bool attempted = false;
                };
                StableTransactionPublication stable{&m_impl->transaction};
                const auto publishPrepared = [](void* const userData) noexcept -> bool
                {
                    auto& state = *static_cast<StableTransactionPublication*>(userData);
                    state.attempted = true;
                    state.result = state.transaction->PublishCommit();
                    return state.result == assets::AssetFileTransactionResult::Success;
                };
                const bool stableObservation =
                    m_impl->accumulator.CommitIfUnchangedOrOwnedChanges(
                        observationSnapshot, {historyPaths.TypedData(), historyPaths.Size()}, publishPrepared, &stable,
                        committedObservationSnapshot);
                if (!stableObservation && !stable.attempted)
                {
                    const assets::AssetFileTransactionResult rollback = m_impl->transaction.Rollback();
                    if (rollback != assets::AssetFileTransactionResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::OperationFailed;
                            failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                            failure->operation.source = request.parentAsset;
                            failure->operation.importer = request.importer;
                            failure->operation.transaction = rollback;
                        }
                        return ProjectAssetPipelineResult::OperationFailed;
                    }
                    const ProjectAssetSnapshotResult restoredIntent = restorePreviousIntentAfterRollback();
                    if (restoredIntent != ProjectAssetSnapshotResult::Success)
                    {
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                            failure->acceptedSnapshot = restoredIntent;
                        }
                        return ProjectAssetPipelineResult::SnapshotSaveFailed;
                    }
                    return pendingObservation();
                }
                transaction = stable.result;
            }
            else
            {
                transaction = m_impl->transaction.PublishCommit();
            }
        }
        if (transaction == assets::AssetFileTransactionResult::Success)
            transaction = m_impl->transaction.CompleteCommit(historyPaths, {postConditions.TypedData(), postConditions.Size()});
        if (transaction != assets::AssetFileTransactionResult::Success &&
            transaction != assets::AssetFileTransactionResult::CommittedCleanupPending)
        {
            bool rolledBack = false;
            if (m_impl->transaction.GetState() == assets::AssetFileTransactionState::Collecting ||
                m_impl->transaction.GetState() == assets::AssetFileTransactionState::Prepared)
            {
                const assets::AssetFileTransactionResult rollback = m_impl->transaction.Rollback();
                if (rollback != assets::AssetFileTransactionResult::Success)
                    transaction = rollback;
                else
                    rolledBack = true;
            }
            if (rolledBack)
            {
                const ProjectAssetSnapshotResult restoredIntent = restorePreviousIntentAfterRollback();
                if (restoredIntent != ProjectAssetSnapshotResult::Success)
                {
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                        failure->acceptedSnapshot = restoredIntent;
                    }
                    return ProjectAssetPipelineResult::SnapshotSaveFailed;
                }
            }
            if (transaction == assets::AssetFileTransactionResult::Conflict)
                return stale();
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.source = request.parentAsset;
                failure->operation.importer = request.importer;
                failure->operation.transaction = transaction;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }

        // A completed newer planned-import transaction supersedes any terminal
        // process-local continuation report from an earlier import. Keep the old
        // report across stale/busy/rollback failures, but never let it be mistaken
        // for the outcome of this newly durable mutation.
        m_impl->deferredPlannedImportStatus = {};

        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        importedAsset = request.parentAsset;
        const bool cleanupPending = transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        // A caller may defer only the post-commit authoritative catalog pass for
        // an independent create-only first import. Every stale witness, collision
        // proof, transaction assertion, cohort-intent write-ahead record and
        // filesystem commit above remains synchronous and authoritative. The
        // process-local marker is deliberately narrower than the durable cohort
        // intent: it grants one observation-catch-up history exemption only when
        // the worker later proves this exact import was the sole accepted change.
        const bool deferredIndependentFirstImport =
            deferPostCommitReconciliation && report == nullptr && !observationFenceRequired &&
            !requiresGlobalCatalogFence && request.copySourceToDestination && !reimport &&
            request.dependencyWitnesses.Empty() && m_impl->accepted.cohortIntent.IsActive() &&
            m_impl->accepted.cohortIntent.predecessorOutputs.Empty() &&
            m_impl->accepted.cohortIntent.asset == request.parentAsset &&
            m_impl->accepted.cohortIntent.sourceIdentity == request.sourceIdentity;
        if (deferredIndependentFirstImport)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            m_impl->BeginDeferredPlannedImport(request.parentAsset, request.sourceIdentity,
                                               request.sourceDestination, request.sourceFingerprint);
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation.source = request.parentAsset;
                failure->operation.importer = request.importer;
                failure->operation.transaction = transaction;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        ProjectAssetPipelineFailure reconcileFailure;
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation.source = request.parentAsset;
                failure->operation.importer = request.importer;
                failure->operation.transaction = transaction;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (observationFenceRequired)
            m_impl->accumulator.AcknowledgeAuthoritativeRescan(committedObservationSnapshot.generation);
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::MoveAsset(const assets::AssetId asset, const filesystem::AbsolutePath& target,
                                                               ProjectAssetRestoreReport* const report,
                                                               ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (!CanMutateSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        const assets::SourceRecord* const source = m_impl->sources->LookupAsset(asset);
        if (source == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SourceUnavailable;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        AssetHistoryRecord inverse;
        if (!AppendAssetPaths(historyPaths, source->path, m_impl->config.maximumOperationEntries) ||
            !AppendAssetPaths(historyPaths, target, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        if (!PathsInWritableRoots(historyPaths, m_impl->writableRoots))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, m_impl->builds.GetImporters(), m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult result = assets::ExecuteAssetMove(context, asset, target, &operation);
        if (result != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::DuplicateAsset(const assets::AssetId asset, const filesystem::AbsolutePath& target,
                                                                    const assets::AssetId newIdentity, ProjectAssetRestoreReport* const report,
                                                                    ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (!CanMutateSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        AssetHistoryRecord inverse;
        if (!AppendAssetPaths(historyPaths, target, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        if (!PathsInWritableRoots(historyPaths, m_impl->writableRoots))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, m_impl->builds.GetImporters(), m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult result = assets::ExecuteAssetDuplicate(context, asset, target, newIdentity, &operation);
        if (result != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RetargetAssetReferences(
        const containers::ArraySpan<const assets::AssetReferenceReplacement> replacements, ProjectAssetRestoreReport* const report,
        ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (!CanMutateSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        AssetHistoryRecord inverse;
        if (!CollectRewritePaths(*m_impl->sources, replacements, {}, historyPaths, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        if (!PathsInWritableRoots(historyPaths, m_impl->writableRoots))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, m_impl->builds.GetImporters(), m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult result = assets::ExecuteAssetRetarget(context, replacements, &operation);
        if (result != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::DeleteAsset(
        const assets::AssetId asset, const containers::ArraySpan<const assets::AssetReferenceReplacement> replacements,
        ProjectAssetRestoreReport* const report, ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState) : ProjectAssetPipelineResult::InvalidState;
        if (!CanMutateSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        const assets::SourceRecord* const source = m_impl->sources->LookupAsset(asset);
        if (source == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::InvalidArgument;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        containers::DynamicArray<assets::AssetReferenceReplacement> removed(memory::pools::Assets::GetInstance());
        for (const assets::AssetOutput& output : source->metadata.outputs)
        {
            const u32 previous = removed.Size();
            removed.PushBack({assets::GetAssetOutputReference(asset, output), {}});
            if (removed.Size() != previous + 1u)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                          : ProjectAssetPipelineResult::HistoryLimitExceeded;
        }
        containers::DynamicArray<filesystem::AbsolutePath> historyPaths(memory::pools::Assets::GetInstance());
        AssetHistoryRecord inverse;
        if (!AppendAssetPaths(historyPaths, source->path, m_impl->config.maximumOperationEntries) ||
            !CollectRewritePaths(*m_impl->sources, removed, asset, historyPaths, m_impl->config.maximumOperationEntries))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        if (!PathsInWritableRoots(historyPaths, m_impl->writableRoots))
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::ReadOnlySource;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!CaptureHistory(*m_impl->files, historyPaths, m_impl->config, inverse))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        containers::DynamicArray<assets::AssetFileTransactionPostCondition> postConditions(memory::pools::Assets::GetInstance());
        if (!PrepareHistoryPostConditions(inverse, historyPaths, postConditions))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const assets::AssetOperationContext context{m_impl->files, m_impl->sources, m_impl->builds.GetImporters(), m_impl->config.build.target,
                                                    m_impl->config.build.sourceData, &m_impl->transaction, {}, historyPaths,
                                                    {postConditions.TypedData(), postConditions.Size()},
                                                    {postConditions.TypedData(), postConditions.Size()}};
        assets::AssetOperationFailure operation;
        const assets::AssetOperationResult result = assets::ExecuteAssetDelete(context, asset, replacements, &operation);
        if (result != assets::AssetOperationResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!AttachHistoryPostConditions(inverse, postConditions))
        {
            ClearAssetOperationHistory();
            inverse.files.Clear();
        }
        const bool cleanupPending = operation.transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        ProjectAssetPipelineFailure reconcileFailure;
        const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
        if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
        {
            m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
            if (failure != nullptr)
            {
                *failure = reconcileFailure;
                failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                failure->operation = operation;
            }
            return ProjectAssetPipelineResult::CommittedReconciliationPending;
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        m_impl->RecordCommittedHistory(static_cast<AssetHistoryRecord&&>(inverse));
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::DuplicateAssetWithGeneratedIdentity(
        const assets::AssetId asset, const filesystem::AbsolutePath& target, assets::AssetId& newIdentity,
        ProjectAssetRestoreReport* const report, ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        const assets::SourceRecord* const source = m_impl->sources->LookupAsset(asset);
        if (source == nullptr)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::SourceUnavailable;
                failure->operation.source = asset;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        containers::HashMap<resources::ResourceId, u8> generatedOutputIds(memory::pools::Assets::GetInstance());
        generatedOutputIds.Reserve(source->metadata.outputs.Size());
        if (generatedOutputIds.Capacity() < source->metadata.outputs.Size())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::IdentityGenerationFailed)
                                      : ProjectAssetPipelineResult::IdentityGenerationFailed;
        assets::AssetId generated;
        bool available = false;
        for (u32 attempt = 0; attempt < m_impl->config.maximumIdentityGenerationAttempts; ++attempt)
        {
            if (!assets::GenerateAssetId(generated))
                break;
            if (m_impl->sources->ContainsAssetIdentity(generated))
                continue;
            generatedOutputIds.Clear();
            bool collision = false;
            for (u32 outputIndex = 0u; outputIndex < source->metadata.outputs.Size(); ++outputIndex)
            {
                const resources::ResourcePath path =
                    assets::GetAssetOutputResourcePath(generated, source->metadata.outputs[outputIndex].key);
                if (!path.IsValid())
                {
                    collision = true;
                    break;
                }
                u8 previous = 0u;
                if (generatedOutputIds.Find(path.Id(), previous))
                {
                    collision = true;
                    break;
                }
                if (!generatedOutputIds.Insert(path.Id(), 0u).IsSuccessful())
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::IdentityGenerationFailed)
                                              : ProjectAssetPipelineResult::IdentityGenerationFailed;
                if (m_impl->sources->ContainsOutputPath(path) || m_impl->streamer->ContainsResourcePath(path))
                {
                    collision = true;
                    break;
                }
            }
            if (!collision)
            {
                available = true;
                break;
            }
        }
        if (!available)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::IdentityGenerationFailed)
                                      : ProjectAssetPipelineResult::IdentityGenerationFailed;
        const ProjectAssetPipelineResult result = DuplicateAsset(asset, target, generated, report, failure);
        if (IsCommittedResult(result))
            newIdentity = generated;
        return result;
    }

    bool ProjectAssetPipeline::CanUndoAssetOperation() const noexcept
    {
        return m_impl != nullptr && !m_impl->undo.Empty() && CanMutateSources() &&
               m_impl->transaction.GetState() == assets::AssetFileTransactionState::Ready;
    }

    bool ProjectAssetPipeline::CanRedoAssetOperation() const noexcept
    {
        return m_impl != nullptr && !m_impl->redo.Empty() && CanMutateSources() &&
               m_impl->transaction.GetState() == assets::AssetFileTransactionState::Ready;
    }

    u32 ProjectAssetPipeline::GetUndoAssetOperationCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->undo.Size() : 0u;
    }

    u32 ProjectAssetPipeline::GetRedoAssetOperationCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->redo.Size() : 0u;
    }

    void ProjectAssetPipeline::ClearAssetOperationHistory() noexcept
    {
        if (m_impl == nullptr)
            return;
        m_impl->undo.Clear();
        m_impl->redo.Clear();
        m_impl->undoBytes = 0;
        m_impl->redoBytes = 0;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::ReplayAssetOperation(const bool undo,
                                                                          ProjectAssetRestoreReport* const report,
                                                                          ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        containers::DynamicArray<AssetHistoryRecord>& source = undo ? m_impl->undo : m_impl->redo;
        containers::DynamicArray<AssetHistoryRecord>& destination = undo ? m_impl->redo : m_impl->undo;
        u64& sourceBytes = undo ? m_impl->undoBytes : m_impl->redoBytes;
        u64& destinationBytes = undo ? m_impl->redoBytes : m_impl->undoBytes;
        if (source.Empty())
        {
            const ProjectAssetPipelineResult unavailable = undo ? ProjectAssetPipelineResult::UndoUnavailable
                                                                 : ProjectAssetPipelineResult::RedoUnavailable;
            return failure != nullptr ? (failure->result = unavailable) : unavailable;
        }
        if (!CanMutateSources() || m_impl->transaction.GetState() != assets::AssetFileTransactionState::Ready)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        const AssetHistoryRecord& desired = source.Back();
        containers::DynamicArray<filesystem::AbsolutePath> paths(memory::pools::Assets::GetInstance());
        paths.Reserve(desired.files.Size());
        if (paths.Capacity() < desired.files.Size())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        for (const AssetHistoryFile& state : desired.files)
            paths.PushBack(state.path);
        if (!PathsInWritableRoots(paths, m_impl->writableRoots) || sourceBytes < desired.budgetBytes)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        const u64 retainedSourceBytes = sourceBytes - desired.budgetBytes;
        if (retainedSourceBytes > m_impl->config.maximumOperationHistoryBytes)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        ProjectAssetPipelineConfig reverseConfig = m_impl->config;
        reverseConfig.maximumOperationHistoryBytes -= retainedSourceBytes;
        AssetHistoryRecord reverse;
        if (!CaptureHistory(*m_impl->files, paths, reverseConfig, reverse) ||
            !SetHistoryExpectedPostFromDesired(reverse, desired))
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::HistoryLimitExceeded)
                                      : ProjectAssetPipelineResult::HistoryLimitExceeded;
        // A history record represents one side of a reversible state pair. Keep
        // the larger accounting reservation when constructing the opposite side
        // so create-only records do not lose their future capture budget after
        // Redo (and subsequent Undo/Redo cycles).
        reverse.budgetBytes = std::max(reverse.byteCount, desired.budgetBytes);

        assets::AssetFileTransactionResult transaction = m_impl->transaction.Begin();
        if (transaction != assets::AssetFileTransactionResult::Success)
        {
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.transaction = transaction;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        bool staged = false;
        for (const AssetHistoryFile& state : desired.files)
        {
            if (!state.expectedPostKnown || (!state.expectedPostExisted && !state.existed))
            {
                static_cast<void>(m_impl->transaction.Rollback());
                ClearAssetOperationHistory();
                const ProjectAssetPipelineResult unavailable = undo ? ProjectAssetPipelineResult::UndoUnavailable
                                                                     : ProjectAssetPipelineResult::RedoUnavailable;
                return failure != nullptr ? (failure->result = unavailable) : unavailable;
            }
            if (state.expectedPostExisted)
            {
                transaction = state.existed
                                  ? m_impl->transaction.StageWriteIfDigestMatches(state.path, state.expectedPostDigest, state.bytes)
                                  : m_impl->transaction.StageDeleteIfDigestMatches(state.path, state.expectedPostDigest);
                staged = transaction == assets::AssetFileTransactionResult::Success;
            }
            else
            {
                transaction = m_impl->transaction.StageCreate(state.path, state.bytes);
                staged = transaction == assets::AssetFileTransactionResult::Success;
            }
            if (transaction != assets::AssetFileTransactionResult::Success)
                break;
        }
        if (transaction == assets::AssetFileTransactionResult::Success && staged)
            transaction = m_impl->transaction.Prepare();
        if (transaction == assets::AssetFileTransactionResult::Success && staged)
            transaction = m_impl->transaction.Commit();
        if (transaction != assets::AssetFileTransactionResult::Success &&
            transaction != assets::AssetFileTransactionResult::CommittedCleanupPending)
        {
            if (m_impl->transaction.GetState() == assets::AssetFileTransactionState::Collecting ||
                m_impl->transaction.GetState() == assets::AssetFileTransactionState::Prepared)
                static_cast<void>(m_impl->transaction.Rollback());
            if (transaction == assets::AssetFileTransactionResult::Conflict)
                ClearAssetOperationHistory();
            if (failure != nullptr)
            {
                failure->result = ProjectAssetPipelineResult::OperationFailed;
                failure->operation.result = assets::AssetOperationResult::TransactionFailed;
                failure->operation.transaction = transaction;
            }
            return ProjectAssetPipelineResult::OperationFailed;
        }
        if (!staged)
            static_cast<void>(m_impl->transaction.Rollback());

        const bool cleanupPending = transaction == assets::AssetFileTransactionResult::CommittedCleanupPending;
        sourceBytes -= desired.budgetBytes;
        static_cast<void>(source.PopBack());
        m_impl->PushHistory(destination, destinationBytes, static_cast<AssetHistoryRecord&&>(reverse),
                            m_impl->config.maximumOperationHistoryBytes - sourceBytes);

        if (staged)
        {
            ProjectAssetPipelineFailure reconcileFailure;
            const containers::ArraySpan<const assets::SourceRoot> roots = m_impl->sources->GetRoots();
            if (Rescan(roots, report, nullptr, &reconcileFailure) != ProjectAssetPipelineResult::Success)
            {
                if (failure != nullptr)
                {
                    *failure = reconcileFailure;
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    failure->operation.transaction = transaction;
                }
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }
        }
        if (report != nullptr)
            report->operationCleanupPending = cleanupPending;
        return cleanupPending ? ProjectAssetPipelineResult::CommittedCleanupPending : ProjectAssetPipelineResult::Success;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::UndoAssetOperation(ProjectAssetRestoreReport* const report,
                                                                        ProjectAssetPipelineFailure* const failure) noexcept
    {
        return ReplayAssetOperation(true, report, failure);
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::RedoAssetOperation(ProjectAssetRestoreReport* const report,
                                                                        ProjectAssetPipelineFailure* const failure) noexcept
    {
        return ReplayAssetOperation(false, report, failure);
    }

    bool ProjectAssetPipeline::CanMutateSources() const noexcept { return m_impl != nullptr && m_impl->CanMutateSources(); }

    u64 ProjectAssetPipeline::GetMaximumExternalImportSourceBytes() const noexcept
    {
        if (m_impl == nullptr)
            return 0u;
        return std::min(m_impl->config.maximumOperationFileBytes,
                        std::min(m_impl->config.maximumOperationStagedBytes,
                                 m_impl->config.maximumOperationHistoryBytes));
    }

    bool ProjectAssetPipeline::CanStartAssetOperation() const noexcept
    {
        return CanMutateSources() && m_impl->transaction.GetState() == assets::AssetFileTransactionState::Ready;
    }

    u64 ProjectAssetPipeline::GetAcceptedAssetGeneration() const noexcept
    {
        return m_impl != nullptr ? m_impl->accepted.generation : 0u;
    }

    containers::ArraySpan<const ProjectAssetState> ProjectAssetPipeline::GetAcceptedAssets() const noexcept
    {
        return m_impl != nullptr ? containers::ArraySpan<const ProjectAssetState>{m_impl->accepted.assets.TypedData(), m_impl->accepted.assets.Size()}
                                 : containers::ArraySpan<const ProjectAssetState>{};
    }

    const ProjectAssetState* ProjectAssetPipeline::LookupAcceptedAsset(const assets::AssetId asset) const noexcept
    {
        return m_impl != nullptr ? m_impl->accepted.LookupAsset(asset) : nullptr;
    }

    containers::ArraySpan<const ProjectAssetChangeRecord> ProjectAssetPipeline::GetAcceptedAssetChanges() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const ProjectAssetChangeRecord>{m_impl->accepted.changes.TypedData(), m_impl->accepted.changes.Size()}
                   : containers::ArraySpan<const ProjectAssetChangeRecord>{};
    }

    containers::ArraySpan<const ProjectAssetSourceDiagnostic> ProjectAssetPipeline::GetAssetSourceDiagnostics() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const ProjectAssetSourceDiagnostic>{m_impl->accepted.diagnostics.TypedData(), m_impl->accepted.diagnostics.Size()}
                   : containers::ArraySpan<const ProjectAssetSourceDiagnostic>{};
    }

    containers::ArraySpan<const resources::ResourceReference> ProjectAssetPipeline::GetAssetInvalidationSeeds() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const resources::ResourceReference>{m_impl->accepted.invalidationSeeds.TypedData(),
                                                                                m_impl->accepted.invalidationSeeds.Size()}
                   : containers::ArraySpan<const resources::ResourceReference>{};
    }

    containers::ArraySpan<const resources::ResourceReference> ProjectAssetPipeline::GetAssetInvalidationOutputs() const noexcept
    {
        return m_impl != nullptr
                   ? containers::ArraySpan<const resources::ResourceReference>{m_impl->accepted.invalidationOutputs.TypedData(),
                                                                                m_impl->accepted.invalidationOutputs.Size()}
                   : containers::ArraySpan<const resources::ResourceReference>{};
    }

    containers::ArraySpan<const assets::AssetReference> ProjectAssetPipeline::GetMissingOutputReferencers(
        const resources::ResourceReference output) const noexcept
    {
        return m_impl != nullptr ? m_impl->accepted.GetMissingOutputReferencers(output)
                                 : containers::ArraySpan<const assets::AssetReference>{};
    }

    namespace
    {
        [[nodiscard]] ProjectAssetPipelineResult EnableObservationOn(ProjectAssetPipeline::Impl& impl,
                                                                     filesystem::DirectoryObservationSource& source,
                                                                     const ProjectAssetObservationConfig& config) noexcept
        {
            if (impl.observation.running)
                return ProjectAssetPipelineResult::ObservationAlreadyActive;
            // Source reconciliation may operate under a retained durable cohort
            // intent: it preserves the predecessor set through
            // ApplyCohortIntentDesired. Using ordinary mutation admission here
            // would deadlock source catch-up against that intent and prevent a
            // reviewed replacement from ever receiving its required observer
            // clearance. Other automatic/maintenance/build owners remain fenced.
            if (!impl.CanReconcileSources())
                return ProjectAssetPipelineResult::Busy;
            if (!config.IsValid())
                return ProjectAssetPipelineResult::InvalidArgument;
            const containers::ArraySpan<const assets::SourceRoot> roots = impl.sources->GetRoots();
            if (roots.Empty())
                return ProjectAssetPipelineResult::InvalidArgument;

            filesystem::DirectoryObserverConfig observerConfig;
            observerConfig.notificationBufferBytes = config.notificationBufferBytes;
            if (roots.Count() > observerConfig.maximumRoots)
                return ProjectAssetPipelineResult::ObservationUnavailable;

            // Observation ticks may add/remove publications, so preallocate the
            // owner-only bookkeeping before callbacks begin. This keeps a later
            // TickObservation from triggering project-sized array growth or a
            // HashMap rehash.
            impl.publications.Reserve(impl.config.maximumPublishedResources);
            impl.publicationIndex.Reserve(impl.config.maximumPublishedResources);
            if (impl.publications.Capacity() < impl.config.maximumPublishedResources ||
                impl.publicationIndex.Capacity() < impl.config.maximumPublishedResources)
                return ProjectAssetPipelineResult::ObservationUnavailable;

            if (!impl.accumulator.Configure(config, roots, impl.config.operationJournalPath))
                return ProjectAssetPipelineResult::ObservationUnavailable;
            if (!impl.StartObservationWorker())
            {
                impl.accumulator.Reset();
                return ProjectAssetPipelineResult::ObservationUnavailable;
            }
            containers::DynamicArray<filesystem::AbsolutePath> rootPaths(memory::pools::Assets::GetInstance());
            rootPaths.Reserve(roots.Count());
            for (const assets::SourceRoot& root : roots)
                rootPaths.PushBack(root.directory);
            if (rootPaths.Size() != roots.Count())
            {
                static_cast<void>(impl.DrainObservationReconcile());
                impl.accumulator.Reset();
                return ProjectAssetPipelineResult::ObservationUnavailable;
            }

            // Publish state before Start so a hint produced by a worker during
            // startup is queued rather than silently dropped. A failed Start
            // discards the partial state below.
            impl.observation.source = &source;
            impl.observation.running = true;
            impl.observation.config = config;
            if (!source.Start(containers::ArraySpan<const filesystem::AbsolutePath>(rootPaths.TypedData(), rootPaths.Size()),
                              &ProjectAssetPipeline::Impl::OnDirectoryObservation, &impl, observerConfig))
            {
                impl.observation.source = nullptr;
                impl.observation.running = false;
                impl.observation.config = {};
                impl.observation.reconcileActive = false;
                impl.observation.reconcileGeneration = 0;
                static_cast<void>(impl.DrainObservationReconcile());
                impl.accumulator.Reset();
                return ProjectAssetPipelineResult::ObservationUnavailable;
            }
            // Observation may have been disabled while disk state continued to
            // change. Start every observation session with one generation-zero
            // authoritative catch-up pass. Any real callback arriving during
            // this pass receives a newer accumulator generation and therefore
            // survives its acknowledgement for a later reconciliation.
            if (!impl.StartObservationReconcile())
            {
                static_cast<void>(source.Stop());
                impl.observation.source = nullptr;
                impl.observation.running = false;
                impl.observation.config = {};
                static_cast<void>(impl.DrainObservationReconcile());
                impl.accumulator.Reset();
                return ProjectAssetPipelineResult::ObservationUnavailable;
            }
            impl.observation.reconcileActive = true;
            impl.observation.initialCatchUpRequired = true;
            impl.observation.reconcileGeneration = 0u;
            return ProjectAssetPipelineResult::Success;
        }
    } // namespace

    ProjectAssetPipelineResult ProjectAssetPipeline::EnableObservation(const ProjectAssetObservationConfig& config,
                                                                       ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        const ProjectAssetPipelineResult result = EnableObservationOn(*m_impl, m_impl->platformObserver, config);
        if (result != ProjectAssetPipelineResult::Success && failure != nullptr)
            failure->result = result;
        return result;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::EnableObservation(filesystem::DirectoryObservationSource& source,
                                                                       const ProjectAssetObservationConfig& config,
                                                                       ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        const ProjectAssetPipelineResult result = EnableObservationOn(*m_impl, source, config);
        if (result != ProjectAssetPipelineResult::Success && failure != nullptr)
            failure->result = result;
        return result;
    }

    ProjectAssetPipelineResult ProjectAssetPipeline::DisableObservation(ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!m_impl->observation.running)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ObservationNotActive)
                                      : ProjectAssetPipelineResult::ObservationNotActive;
        if (m_impl->observation.reconcileActive || m_impl->observation.initialCatchUpRequired ||
            m_impl->accumulator.HasRescanRequired())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        // A callback is still allowed to enter until DirectoryObservationSource::Stop
        // returns. Ensure we own a quiescent build graph so a last callback can be
        // repaired authoritatively before a successful disable is reported.
        // Retained durable cohort intent fences mutation, not authoritative source
        // reconciliation. A reviewed replacement must be able to pause its watcher
        // without dropping the predecessor's protected publication evidence.
        if (!m_impl->CanReconcileSources())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        if (m_impl->observation.source == nullptr || !m_impl->observation.source->Stop())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;

        ProjectAssetPipelineResult stopCatchUp = ProjectAssetPipelineResult::Success;
        ProjectAssetPipelineFailure stopCatchUpFailure;
        if (m_impl->accumulator.HasRescanRequired())
        {
            // Stop() is the callback lifetime barrier. A hint published while Stop
            // was joining its producer must not be erased by the accumulator reset
            // below. Reconcile the now-stable disk state once before disabling.
            stopCatchUp = Rescan(m_impl->sources->GetRoots(), nullptr, nullptr, &stopCatchUpFailure);
        }
        if (!m_impl->DrainObservationReconcile())
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        m_impl->observation.source = nullptr;
        m_impl->observation.running = false;
        m_impl->observation.config = {};
        m_impl->observation.reconcileActive = false;
        m_impl->observation.initialCatchUpRequired = false;
        m_impl->observation.reconcileGeneration = 0;
        m_impl->observation.reconcile.Reset();
        m_impl->accumulator.Reset();
        if (stopCatchUp != ProjectAssetPipelineResult::Success)
        {
            if (failure != nullptr)
                *failure = stopCatchUpFailure;
            return stopCatchUp;
        }
        return ProjectAssetPipelineResult::Success;
    }

    bool ProjectAssetPipeline::IsObservingSources() const noexcept { return m_impl != nullptr && m_impl->observation.running; }

    ProjectAssetPipelineResult ProjectAssetPipeline::TickObservation(ProjectAssetRestoreReport* const report,
                                                                     ProjectAssetPipelineFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (report != nullptr)
            *report = {};
        if (m_impl == nullptr)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        if (!m_impl->observation.running)
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ObservationNotActive)
                                      : ProjectAssetPipelineResult::ObservationNotActive;

        const auto continueDeferredPlannedImport = [&]() noexcept
        {
            if (!m_impl->deferredPlannedImport.IsActive())
                return;
            if (!m_impl->DeferredPlannedImportMatchesCurrentTruth())
            {
                m_impl->FailDeferredPlannedImportContinuation(ProjectAssetPipelineResult::CommittedReconciliationPending);
                return;
            }
            if (!m_impl->CanAttemptDeferredPlannedImportCohort())
            {
                m_impl->FailDeferredPlannedImportContinuation(ProjectAssetPipelineResult::Busy);
                return;
            }
            if (m_impl->automaticRecook.HasExplicitCohort())
            {
                m_impl->CompleteDeferredPlannedImportContinuation();
                return;
            }

            const ProjectAssetState* const asset = m_impl->accepted.LookupAsset(m_impl->deferredPlannedImport.asset);
            if (asset == nullptr)
            {
                m_impl->FailDeferredPlannedImportContinuation(ProjectAssetPipelineResult::CommittedReconciliationPending);
                return;
            }
            const ProjectAssetCohortDeliveryRequest request{
                asset->asset, asset->acceptedGeneration,
                {m_impl->accepted.cohortIntent.successorOutputs.TypedData(),
                 m_impl->accepted.cohortIntent.successorOutputs.Size()}};
            ProjectAssetPipelineFailure continuationFailure;
            const ProjectAssetPipelineResult continued = RequestAssetCohortDelivery(request, &continuationFailure);
            if (continued == ProjectAssetPipelineResult::Success)
                m_impl->CompleteDeferredPlannedImportContinuation();
            else
                m_impl->FailDeferredPlannedImportContinuation(continued, &continuationFailure);
        };

        if (m_impl->observation.reconcileActive)
        {
            Impl::ObservationWorker* const worker = m_impl->observation.worker;
            if (worker == nullptr)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                          : ProjectAssetPipelineResult::InvalidState;

            if (m_impl->observation.snapshotFailureRetirePending)
            {
                if (!worker->IsCompleted())
                    return ProjectAssetPipelineResult::Pending;
                const ProjectAssetSnapshotResult snapshotFailure = m_impl->observation.snapshotFailure;
                m_impl->observation.snapshotFailureRetirePending = false;
                m_impl->observation.snapshotFailure = ProjectAssetSnapshotResult::NotChecked;
                m_impl->observation.reconcileActive = false;
                m_impl->observation.reconcileGeneration = 0;
                m_impl->observation.reconcile.ResetScalars();
                m_impl->accumulator.RequeueReady();
                if (failure != nullptr)
                {
                    failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                    failure->acceptedSnapshot = snapshotFailure;
                }
                return ProjectAssetPipelineResult::SnapshotSaveFailed;
            }

            if (m_impl->observation.rescanPending)
            {
                if (!worker->IsCompleted())
                    return ProjectAssetPipelineResult::Pending;
                if (worker->sourceResult != assets::SourceDatabaseResult::Success)
                {
                    const assets::SourceDatabaseResult sourceResult = worker->sourceResult;
                    m_impl->observation.rescanPending = false;
                    m_impl->observation.catalogCommitted = false;
                    m_impl->observation.reconcileActive = false;
                    m_impl->observation.reconcileGeneration = 0;
                    m_impl->observation.reconcile.ResetScalars();
                    m_impl->accumulator.RequeueReady();
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::ReconciliationFailed;
                        failure->sources = sourceResult;
                    }
                    return ProjectAssetPipelineResult::ReconciliationFailed;
                }
                if (worker->snapshotResult != ProjectAssetSnapshotResult::Success)
                {
                    const ProjectAssetSnapshotResult snapshotResult = worker->snapshotResult;
                    m_impl->observation.rescanPending = false;
                    m_impl->observation.catalogCommitted = false;
                    m_impl->observation.reconcileActive = false;
                    m_impl->observation.reconcileGeneration = 0;
                    m_impl->observation.reconcile.ResetScalars();
                    m_impl->accumulator.RequeueReady();
                    if (failure != nullptr)
                    {
                        failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                        failure->acceptedSnapshot = snapshotResult;
                    }
                    return ProjectAssetPipelineResult::SnapshotSaveFailed;
                }

                // The worker has already serialized, written, flushed and
                // read-back-verified the proposed durable image. The owner only
                // performs the same-directory atomic replacement immediately
                // before its O(1) in-memory acceptance.
                if (worker->snapshotStaged)
                {
                    const ProjectAssetSnapshotResult committed = CommitStagedProjectAssetAcceptedSnapshot(
                        *m_impl->files, m_impl->config.build.dependencyIndexRoot);
                    if (committed != ProjectAssetSnapshotResult::Success)
                    {
                        if (worker->Submit(Impl::ObservationWorker::Command::RetireCatalog))
                        {
                            m_impl->observation.rescanPending = false;
                            m_impl->observation.snapshotFailureRetirePending = true;
                            m_impl->observation.snapshotFailure = committed;
                            return ProjectAssetPipelineResult::Pending;
                        }
                        if (failure != nullptr)
                        {
                            failure->result = ProjectAssetPipelineResult::SnapshotSaveFailed;
                            failure->acceptedSnapshot = committed;
                        }
                        return ProjectAssetPipelineResult::SnapshotSaveFailed;
                    }
                }
                const bool acceptedExternalChange = worker->proposed.generation != m_impl->accepted.generation;
                bool preserveOwnedCommitHistory = false;
                const bool evaluatingDeferredOwnedCommit =
                    m_impl->observation.reconcileGeneration == 0u &&
                    m_impl->deferredPlannedImport.IsActive() &&
                    m_impl->deferredPlannedImport.awaitingOwnedAcceptance;
                if (evaluatingDeferredOwnedCommit)
                {
                    preserveOwnedCommitHistory =
                        m_impl->DeferredPlannedImportMatchesPreparedCatchUp(worker->candidate, worker->proposed);
                    if (preserveOwnedCommitHistory)
                    {
                        m_impl->deferredPlannedImport.awaitingOwnedAcceptance = false;
                        m_impl->deferredPlannedImport.acceptedGeneration = worker->proposed.generation;
                        m_impl->deferredPlannedImportStatus.acceptedGeneration = worker->proposed.generation;
                    }
                    else
                    {
                        ClearAssetOperationHistory();
                        m_impl->FailDeferredPlannedImportContinuation(
                            ProjectAssetPipelineResult::CommittedReconciliationPending);
                    }
                }
                AcceptProjectAssetCandidate(*m_impl->sources, m_impl->accepted, worker->candidate, worker->proposed);
                // Undo/redo snapshots are session-local captures of exact authored
                // file bytes. A real authoritative observation transition means
                // those bytes may now predate an external edit, so replaying them
                // would overwrite work that did not originate from this pipeline.
                // A watcher echo of a just-committed pipeline mutation compares as
                // the same accepted generation and deliberately preserves history.
                if (acceptedExternalChange && !preserveOwnedCommitHistory)
                    ClearAssetOperationHistory();
                if (m_impl->deferredPlannedImport.IsActive() &&
                    !m_impl->deferredPlannedImport.awaitingOwnedAcceptance && acceptedExternalChange &&
                    m_impl->accepted.generation != m_impl->deferredPlannedImport.acceptedGeneration)
                    m_impl->FailDeferredPlannedImportContinuation(
                        ProjectAssetPipelineResult::CommittedReconciliationPending);
                if (worker->automaticPendingResult == ProjectAssetAutomaticPendingPrepareResult::Success)
                    m_impl->automaticRecook.AdoptPending(worker->automaticPending);
                else if (worker->automaticPendingResult == ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired)
                    m_impl->automaticRecook.MarkRecoveryRequired(m_impl->accepted.generation, true);
                m_impl->observation.reconcile.ResetScalars();
                m_impl->observation.reconcile.desired.Swap(worker->desired);
                m_impl->observation.reconcile.desiredLookup.Swap(worker->desiredLookup);
                m_impl->observation.reconcile.looseFiles.Swap(worker->looseFiles);
                m_impl->observation.reconcile.report.catalogOutputs = m_impl->observation.reconcile.desired.Size();
                m_impl->observation.rescanPending = false;
                if (!worker->Submit(Impl::ObservationWorker::Command::RetireCatalog))
                {
                    m_impl->observation.reconcileActive = false;
                    m_impl->observation.reconcileGeneration = 0;
                    m_impl->accumulator.RequeueReady();
                    if (failure != nullptr)
                        failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                    return ProjectAssetPipelineResult::CommittedReconciliationPending;
                }
                m_impl->observation.catalogCommitted = true;
                return ProjectAssetPipelineResult::Pending;
            }

            if (m_impl->observation.catalogCommitted)
            {
                if (!worker->IsCompleted())
                    return ProjectAssetPipelineResult::Pending;
                if (worker->CurrentCommand() != Impl::ObservationWorker::Command::RetireCatalog)
                    return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending)
                                              : ProjectAssetPipelineResult::CommittedReconciliationPending;
                m_impl->observation.catalogCommitted = false;
                // Catalog + previous payload retirement is complete off-thread;
                // publication reconciliation starts on a later owner tick.
                return ProjectAssetPipelineResult::Pending;
            }

            const ProjectAssetPipelineResult reconciled =
                m_impl->AdvanceObservationReconcile(m_impl->observation.reconcile,
                                                    m_impl->observation.config.maximumReconcileOperationsPerTick, failure);
            if (reconciled == ProjectAssetPipelineResult::Pending || reconciled == ProjectAssetPipelineResult::Busy)
                return reconciled;
            if (reconciled != ProjectAssetPipelineResult::Success)
            {
                m_impl->observation.reconcileActive = false;
                m_impl->observation.reconcileGeneration = 0;
                m_impl->observation.reconcile.ResetScalars();
                m_impl->accumulator.RequeueReady();
                if (failure != nullptr)
                    failure->result = ProjectAssetPipelineResult::CommittedReconciliationPending;
                return ProjectAssetPipelineResult::CommittedReconciliationPending;
            }

            const u64 completedGeneration = m_impl->observation.reconcileGeneration;
            m_impl->accumulator.AcknowledgeReconcile(completedGeneration);
            if (report != nullptr)
                *report = m_impl->observation.reconcile.report;
            m_impl->observation.reconcile.ResetScalars();
            m_impl->observation.reconcileActive = false;
            m_impl->observation.reconcileGeneration = 0;
            if (completedGeneration == 0u)
                m_impl->observation.initialCatchUpRequired = false;
            if (m_impl->accumulator.HasRescanRequired())
                return ProjectAssetPipelineResult::Pending;
            // The deferred continuation is generation-fenced and one-shot. It is
            // attempted only on a successful observation reconciliation edge,
            // never from ordinary idle ticks, so an expensive cohort resolve
            // cannot repeat frame after frame after a terminal admission failure.
            continueDeferredPlannedImport();
            return ProjectAssetPipelineResult::Success;
        }

        // The lifecycle catch-up pass is generation zero and is started directly,
        // not from SourceChangeAccumulator::Advance(). If it fails, RequeueReady()
        // has no generation-zero entry to recreate. Keep an explicit owner flag
        // until that authoritative pass actually completes, then restart it here
        // on a later tick without manufacturing a filesystem observation.
        if (m_impl->observation.initialCatchUpRequired)
        {
            if (!m_impl->CanReconcileSources())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy)
                                          : ProjectAssetPipelineResult::Busy;
            if (m_impl->observation.worker == nullptr)
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                          : ProjectAssetPipelineResult::InvalidState;
            if (!m_impl->StartObservationReconcile())
                return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                          : ProjectAssetPipelineResult::ReconciliationFailed;
            m_impl->observation.reconcileActive = true;
            m_impl->observation.reconcileGeneration = 0u;
            return ProjectAssetPipelineResult::Pending;
        }

        const SourceChangeAdvance advance = m_impl->accumulator.Advance();
        if (!advance.reconcile)
            return advance.hasPending ? ProjectAssetPipelineResult::Pending : ProjectAssetPipelineResult::Success;
        if (!m_impl->CanReconcileSources())
        {
            m_impl->accumulator.RequeueReady();
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::Busy) : ProjectAssetPipelineResult::Busy;
        }
        if (m_impl->observation.worker == nullptr)
        {
            m_impl->accumulator.RequeueReady();
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::InvalidState)
                                      : ProjectAssetPipelineResult::InvalidState;
        }
        if (!m_impl->StartObservationReconcile())
        {
            m_impl->accumulator.RequeueReady();
            return failure != nullptr ? (failure->result = ProjectAssetPipelineResult::ReconciliationFailed)
                                      : ProjectAssetPipelineResult::ReconciliationFailed;
        }
        m_impl->observation.reconcileActive = true;
        m_impl->observation.reconcileGeneration = advance.generation;
        return ProjectAssetPipelineResult::Pending;
    }

    ProjectAssetObservationStatus ProjectAssetPipeline::GetObservationStatus() const noexcept
    {
        ProjectAssetObservationStatus status;
        if (m_impl == nullptr)
            return status;
        status = m_impl->accumulator.GetStatus();
        status.running = m_impl->observation.running;
        status.reconcilePending = status.reconcilePending || m_impl->observation.reconcileActive ||
                                  m_impl->observation.initialCatchUpRequired;
        status.rescanRequired = status.rescanRequired || m_impl->observation.reconcileActive ||
                                m_impl->observation.initialCatchUpRequired;
        status.reconcileOperationsCompleted = m_impl->observation.reconcile.operationsCompleted;
        return status;
    }

} // namespace vanguard::asset_streaming
