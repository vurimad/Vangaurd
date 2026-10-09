#include <vanguard/assets/asset_operations.hpp>

#include <cstring>

namespace vanguard::assets
{
    namespace
    {
        void SetFailure(AssetOperationFailure* const failure, const AssetOperationResult result,
                        const AssetId source = {}, const resources::ResourceReference target = {},
                        const SourceImporterId importer = InvalidSourceImporterId,
                        const ProjectSourceDataResult sourceData = ProjectSourceDataResult::Success,
                        const AssetFileTransactionResult transaction = AssetFileTransactionResult::Success) noexcept
        {
            if (failure != nullptr)
                *failure = {result, source, target, importer, sourceData, transaction};
        }

        const SourceImporterDescriptor* FindImporter(const containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                     const SourceImporterId id, bool& validRegistry) noexcept
        {
            const SourceImporterDescriptor* found = nullptr;
            for (const SourceImporterDescriptor& importer : importers)
            {
                if (!importer.IsValid())
                {
                    validRegistry = false;
                    return nullptr;
                }
                if (importer.id != id)
                    continue;
                if (found != nullptr)
                {
                    validRegistry = false;
                    return nullptr;
                }
                found = &importer;
            }
            return found;
        }

        [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
        {
            const containers::StringView view = source.AsStringView();
            containers::String path(view.Data(), view.Length());
            path.Append(".vmeta", 6);
            return filesystem::AbsolutePath::CreateFilePath(path);
        }

        [[nodiscard]] bool SameOutputs(const AssetMetadata& left, const AssetMetadata& right) noexcept
        {
            if (left.outputs.Size() != right.outputs.Size())
                return false;
            for (u32 index = 0; index < left.outputs.Size(); ++index)
                if (left.outputs[index].key != right.outputs[index].key || left.outputs[index].type != right.outputs[index].type)
                    return false;
            return true;
        }

        [[nodiscard]] bool SameMetadata(const AssetMetadata& left, const AssetMetadata& right) noexcept
        {
            return left.id == right.id && left.importer == right.importer && left.importerVersion == right.importerVersion &&
                   left.settingsVersion == right.settingsVersion && left.settings.Size() == right.settings.Size() &&
                   (left.settings.Empty() || std::memcmp(left.settings.TypedData(), right.settings.TypedData(), left.settings.Size()) == 0) &&
                   SameOutputs(left, right);
        }

        [[nodiscard]] bool ReadMetadataFile(filesystem::Manager& files, const filesystem::AbsolutePath& sourcePath,
                                            containers::DynamicArray<u8>& bytes, AssetMetadata& metadata) noexcept
        {
            const filesystem::AbsolutePath path = MetadataPath(sourcePath);
            auto reader = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!reader)
                return false;
            const u64 size = reader->GetSize();
            if (size > MaximumAssetMetadataBytes || size > 0xffffffffull)
                return false;
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
                return false;
            if (size != 0u)
                reader->Serialize(bytes.TypedData(), static_cast<usize>(size));
            if (reader->HasErrors())
                return false;
            return ParseMetadata({reinterpret_cast<const char*>(bytes.TypedData()), bytes.Size()}, metadata) == MetadataResult::Success;
        }

        [[nodiscard]] bool CopyImportDescription(const SourceImportDescription& description, AssetMetadata& metadata) noexcept
        {
            if (description.settingsVersion == 0u || description.settings.Size() > MaximumAssetSettingsBytes ||
                description.outputs.Empty() || description.outputs.Size() > MaximumAssetOutputs)
                return false;
            metadata.settingsVersion = description.settingsVersion;
            metadata.settings.Resize(description.settings.Size());
            if (metadata.settings.Size() != description.settings.Size())
                return false;
            if (!description.settings.Empty())
                std::memcpy(metadata.settings.TypedData(), description.settings.TypedData(), description.settings.Size());
            metadata.outputs.Reserve(description.outputs.Size());
            if (metadata.outputs.Capacity() < description.outputs.Size())
                return false;
            for (const AssetOutput& sourceOutput : description.outputs)
            {
                AssetOutput output;
                output.key = sourceOutput.key;
                output.type = sourceOutput.type;
                if (output.key != sourceOutput.key)
                    return false;
                const u32 previous = metadata.outputs.Size();
                metadata.outputs.PushBack(static_cast<AssetOutput&&>(output));
                if (metadata.outputs.Size() != previous + 1u)
                    return false;
            }
            return true;
        }

        [[nodiscard]] AssetOperationResult BuildImportMetadata(
            const SourceImporterDescriptor& importer, const AssetId newIdentity,
            const SourceImportDescription& description, AssetMetadata& metadata,
            containers::String& encoded, AssetOperationFailure* const failure) noexcept
        {
            const containers::ArraySpan<const u8> settings{description.settings.TypedData(), description.settings.Size()};
            if (description.settingsVersion == 0u || description.settings.Size() > MaximumAssetSettingsBytes ||
                description.outputs.Empty() || description.outputs.Size() > MaximumAssetOutputs || importer.validateSettings == nullptr ||
                !importer.validateSettings(description.settingsVersion, settings, importer.userData))
            {
                SetFailure(failure, AssetOperationResult::SettingsInvalid, {}, {}, importer.id);
                return AssetOperationResult::SettingsInvalid;
            }
            bool hasPrimaryOutput = false;
            for (const AssetOutput& output : description.outputs)
                hasPrimaryOutput = hasPrimaryOutput || output.type == importer.primaryOutputType;
            if (!hasPrimaryOutput)
            {
                SetFailure(failure, AssetOperationResult::SettingsInvalid, {}, {}, importer.id);
                return AssetOperationResult::SettingsInvalid;
            }

            AssetMetadata candidate;
            candidate.id = newIdentity;
            candidate.importer = importer.id;
            candidate.importerVersion = importer.version;
            if (!CopyImportDescription(description, candidate))
            {
                SetFailure(failure, AssetOperationResult::LimitExceeded, newIdentity, {}, importer.id);
                return AssetOperationResult::LimitExceeded;
            }
            const MetadataResult validated = ValidateMetadata(candidate);
            if (validated != MetadataResult::Success)
            {
                const AssetOperationResult result = validated == MetadataResult::LimitExceeded
                                                        ? AssetOperationResult::LimitExceeded
                                                    : validated == MetadataResult::DuplicateOutput || validated == MetadataResult::IdentityCollision
                                                        ? AssetOperationResult::IdentityConflict
                                                        : AssetOperationResult::SettingsInvalid;
                SetFailure(failure, result, newIdentity, {}, importer.id);
                return result;
            }
            containers::String text;
            if (EncodeMetadata(candidate, text) != MetadataResult::Success)
            {
                SetFailure(failure, AssetOperationResult::LimitExceeded, newIdentity, {}, importer.id);
                return AssetOperationResult::LimitExceeded;
            }
            metadata = static_cast<AssetMetadata&&>(candidate);
            encoded = static_cast<containers::String&&>(text);
            return AssetOperationResult::Success;
        }

        [[nodiscard]] bool PreservesSchema(const AssetMetadata& before, const AssetMetadata& after) noexcept
        {
            return before.id == after.id && before.importer == after.importer && before.importerVersion == after.importerVersion &&
                   before.settingsVersion == after.settingsVersion && SameOutputs(before, after) && ValidateMetadata(after) == MetadataResult::Success;
        }

        [[nodiscard]] i32 WritableRoot(const SourceDatabase& database, const filesystem::AbsolutePath& path,
                                       containers::StringView& relative) noexcept
        {
            const containers::ArraySpan<const SourceRoot> roots = database.GetRoots();
            for (u32 index = 0; index < roots.Count(); ++index)
            {
                const containers::StringView root = roots[index].directory.AsStringView();
                const containers::StringView target = path.AsStringView();
                if (!roots[index].readOnly && target.Length() > root.Length() && target.StartsWithIgnoreCase(root))
                {
                    relative = target.Slice(root.Length(), target.Length());
                    return static_cast<i32>(index);
                }
            }
            return -1;
        }

        [[nodiscard]] const SourceImporterDescriptor* CheckedImporter(const AssetOperationContext& context, const SourceRecord& source,
                                                                     AssetOperationFailure* const failure) noexcept
        {
            bool validRegistry = true;
            const SourceImporterDescriptor* const importer = FindImporter(context.importers, source.importer, validRegistry);
            if (!validRegistry || importer == nullptr || importer->version != source.availableImporterVersion ||
                source.metadataResult != MetadataResult::Success || source.metadata.importer != source.importer ||
                source.metadata.importerVersion != importer->version || source.importState != SourceImportState::MetadataSelected)
                SetFailure(failure, AssetOperationResult::ImporterUnavailable, source.metadata.id, {}, source.importer);
            return validRegistry && importer != nullptr && importer->version == source.availableImporterVersion &&
                           source.metadataResult == MetadataResult::Success && source.metadata.importer == source.importer &&
                           source.metadata.importerVersion == importer->version && source.importState == SourceImportState::MetadataSelected
                       ? importer
                       : nullptr;
        }

        [[nodiscard]] const AssetFileTransactionPostCondition* FindMutationPrecondition(
            const AssetOperationContext& context, const filesystem::AbsolutePath& path) noexcept
        {
            if (context.mutationPreconditions.Empty())
                return nullptr;
            if (context.commitTrackedPaths.Count() != context.mutationPreconditions.Count())
                return nullptr;
            for (u32 index = 0u; index < context.commitTrackedPaths.Count(); ++index)
                if (context.commitTrackedPaths[index] == path)
                    return &context.mutationPreconditions[index];
            return nullptr;
        }

        [[nodiscard]] AssetFileTransactionResult StageContextWrite(
            const AssetOperationContext& context, const filesystem::AbsolutePath& target,
            const containers::ArraySpan<const u8> bytes) noexcept
        {
            if (context.mutationPreconditions.Empty())
                return context.transaction->StageWrite(target, bytes);
            const AssetFileTransactionPostCondition* const expected = FindMutationPrecondition(context, target);
            if (expected == nullptr || !expected->known)
                return AssetFileTransactionResult::InvalidArgument;
            return expected->exists ? context.transaction->StageWriteIfDigestMatches(target, expected->contentDigest, bytes)
                                    : context.transaction->StageCreate(target, bytes);
        }

        [[nodiscard]] AssetFileTransactionResult StageContextDelete(
            const AssetOperationContext& context, const filesystem::AbsolutePath& target) noexcept
        {
            if (context.mutationPreconditions.Empty())
                return context.transaction->StageDelete(target);
            const AssetFileTransactionPostCondition* const expected = FindMutationPrecondition(context, target);
            if (expected == nullptr || !expected->known)
                return AssetFileTransactionResult::InvalidArgument;
            return expected->exists ? context.transaction->StageDeleteIfDigestMatches(target, expected->contentDigest)
                                    : AssetFileTransactionResult::Conflict;
        }

        [[nodiscard]] bool EncodeAndStage(const AssetOperationContext& context, const filesystem::AbsolutePath& sourcePath,
                                          const containers::ArraySpan<const u8> sourceBytes, const AssetMetadata& metadata,
                                          AssetOperationFailure* const failure) noexcept
        {
            containers::String text;
            if (EncodeMetadata(metadata, text) != MetadataResult::Success)
            {
                SetFailure(failure, AssetOperationResult::RewriteFailed, metadata.id);
                return false;
            }
            AssetFileTransactionResult result = StageContextWrite(context, sourcePath, sourceBytes);
            if (result == AssetFileTransactionResult::Success)
                result = StageContextWrite(context, MetadataPath(sourcePath),
                                           {reinterpret_cast<const u8*>(text.AsChar()), text.Length()});
            if (result != AssetFileTransactionResult::Success)
            {
                SetFailure(failure, AssetOperationResult::TransactionFailed, metadata.id, {}, metadata.importer,
                           ProjectSourceDataResult::Success, result);
                return false;
            }
            return true;
        }

        [[nodiscard]] AssetOperationResult FinishTransaction(AssetFileTransaction& transaction,
                                                             AssetOperationFailure* const failure,
                                                             const containers::ArraySpan<const filesystem::AbsolutePath> trackedPaths = {},
                                                             const containers::ArraySpan<AssetFileTransactionPostCondition> postConditions = {}) noexcept
        {
            AssetFileTransactionResult result = transaction.Prepare();
            if (result == AssetFileTransactionResult::Success)
                result = transaction.Commit(trackedPaths, postConditions);
            if (result == AssetFileTransactionResult::Success || result == AssetFileTransactionResult::CommittedCleanupPending)
            {
                if (failure != nullptr)
                    failure->transaction = result;
                return AssetOperationResult::Success;
            }
            if (transaction.GetState() == AssetFileTransactionState::Collecting ||
                transaction.GetState() == AssetFileTransactionState::Prepared)
                static_cast<void>(transaction.Rollback());
            SetFailure(failure, AssetOperationResult::TransactionFailed, {}, {}, InvalidSourceImporterId,
                       ProjectSourceDataResult::Success, result);
            return AssetOperationResult::TransactionFailed;
        }

        [[nodiscard]] bool ValidContext(const AssetOperationContext& context) noexcept
        {
            return context.files != nullptr && context.database != nullptr && context.transaction != nullptr &&
                   context.target < TargetPlatform::Count && context.sourceData.IsValid() && !context.importers.Empty() &&
                   context.transaction->GetState() == AssetFileTransactionState::Ready;
        }

        [[nodiscard]] bool FindReplacement(const containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                           const resources::ResourceReference previous,
                                           resources::ResourceReference& replacement) noexcept
        {
            bool found = false;
            for (const AssetReferenceReplacement& candidate : replacements)
                if (candidate.previous == previous)
                {
                    if (found && candidate.replacement != replacement)
                        return false;
                    replacement = candidate.replacement;
                    found = true;
                }
            return found;
        }

        [[nodiscard]] AssetOperationResult StageReferenceRewrites(const AssetOperationContext& context,
                                                                  const containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                                                  const AssetId excluded,
                                                                  AssetOperationFailure* const failure,
                                                                  bool* const wroteSource = nullptr) noexcept
        {
            if (wroteSource != nullptr)
                *wroteSource = false;
            for (u32 index = 0; index < replacements.Count(); ++index)
            {
                const AssetReferenceReplacement replacement = replacements[index];
                if (!replacement.previous.IsValid() || !replacement.previous.IsTyped() || !replacement.replacement.IsValid() ||
                    !replacement.replacement.IsTyped() || replacement.previous == replacement.replacement ||
                    replacement.previous.ExpectedType() != replacement.replacement.ExpectedType() ||
                    context.database->LookupOutput(replacement.replacement) == nullptr)
                {
                    SetFailure(failure, AssetOperationResult::ReplacementUnavailable, {}, replacement.replacement);
                    return AssetOperationResult::ReplacementUnavailable;
                }
                for (u32 previous = 0; previous < index; ++previous)
                    if (replacements[previous].previous == replacement.previous &&
                        replacements[previous].replacement != replacement.replacement)
                    {
                        SetFailure(failure, AssetOperationResult::InvalidArgument, {}, replacement.previous);
                        return AssetOperationResult::InvalidArgument;
                    }
            }

            for (const SourceRecord& source : context.database->GetResources())
            {
                if (source.metadata.id == excluded)
                    continue;
                containers::DynamicArray<AssetReferenceReplacement> relevant(memory::pools::Assets::GetInstance());
                for (const AssetReference& reference : context.database->GetForwardReferences(source.metadata.id))
                    for (const AssetReferenceReplacement& replacement : replacements)
                        if (reference.to == replacement.previous)
                        {
                            bool duplicate = false;
                            for (const AssetReferenceReplacement& present : relevant)
                                if (present.previous == replacement.previous && present.replacement == replacement.replacement)
                                    duplicate = true;
                            if (!duplicate)
                            {
                                const u32 oldCount = relevant.Size();
                                relevant.PushBack(replacement);
                                if (relevant.Size() != oldCount + 1u)
                                {
                                    SetFailure(failure, AssetOperationResult::LimitExceeded, source.metadata.id);
                                    return AssetOperationResult::LimitExceeded;
                                }
                            }
                        }
                if (relevant.Empty())
                    continue;
                if (source.referenceScanState != SourceReferenceScanState::Scanned)
                {
                    SetFailure(failure, AssetOperationResult::ReferenceStateUnavailable, source.metadata.id);
                    return AssetOperationResult::ReferenceStateUnavailable;
                }
                const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
                if (source.root >= roots.Count() || roots[source.root].readOnly)
                {
                    SetFailure(failure, AssetOperationResult::ReadOnlySource, source.metadata.id, {}, source.importer);
                    return AssetOperationResult::ReadOnlySource;
                }
                const SourceImporterDescriptor* const importer = CheckedImporter(context, source, failure);
                if (importer == nullptr)
                    return AssetOperationResult::ImporterUnavailable;
                if (importer->rewriteSourceReferences == nullptr)
                {
                    SetFailure(failure, AssetOperationResult::RewriteUnavailable, source.metadata.id, {}, source.importer);
                    return AssetOperationResult::RewriteUnavailable;
                }
                ProjectSourceData data;
                const ProjectSourceDataResult loaded = LoadProjectSourceData(*context.files, source, data, context.sourceData);
                if (loaded != ProjectSourceDataResult::Success)
                {
                    SetFailure(failure, AssetOperationResult::SourceUnavailable, source.metadata.id, {}, source.importer, loaded);
                    return AssetOperationResult::SourceUnavailable;
                }
                containers::DynamicArray<u8> rewritten(memory::pools::Assets::GetInstance());
                AssetMetadata metadata;
                if (!importer->rewriteSourceReferences(source, data.View(context.target), relevant, rewritten, metadata, importer->userData) ||
                    !PreservesSchema(source.metadata, metadata) ||
                    !EncodeAndStage(context, source.path, rewritten, metadata, failure))
                {
                    if (failure == nullptr || failure->result == AssetOperationResult::Success)
                        SetFailure(failure, AssetOperationResult::RewriteFailed, source.metadata.id, {}, source.importer);
                    return failure != nullptr ? failure->result : AssetOperationResult::RewriteFailed;
                }
                if (wroteSource != nullptr)
                    *wroteSource = true;
            }
            return AssetOperationResult::Success;
        }
    }

    AssetOperationResult EncodeAssetImportMetadata(
        const SourceImporterDescriptor& importer, const AssetId newIdentity,
        const SourceImportDescription& description, containers::String& encoded,
        AssetOperationFailure* const failure) noexcept
    {
        AssetMetadata metadata;
        return BuildImportMetadata(importer, newIdentity, description, metadata, encoded, failure);
    }

    AssetReferenceRewritePlan::AssetReferenceRewritePlan() noexcept
        : m_sources(memory::pools::Assets::GetInstance()), m_rewrites(memory::pools::Assets::GetInstance()) {}

    resources::ResourceReference AssetReferenceRewritePlan::GetPrevious() const noexcept { return m_previous; }
    resources::ResourceReference AssetReferenceRewritePlan::GetReplacement() const noexcept { return m_replacement; }
    containers::ArraySpan<const AssetReferenceRewriteSource> AssetReferenceRewritePlan::GetSources() const noexcept
    {
        return {m_sources.TypedData(), m_sources.Size()};
    }
    containers::ArraySpan<const AssetReferenceRewrite> AssetReferenceRewritePlan::GetRewrites() const noexcept
    {
        return {m_rewrites.TypedData(), m_rewrites.Size()};
    }

    AssetOperationResult PlanAssetReferenceReplacement(const SourceDatabase& database,
                                                        const containers::ArraySpan<const SourceImporterDescriptor> importers,
                                                        const resources::ResourceReference previous,
                                                        const resources::ResourceReference replacement,
                                                        AssetReferenceRewritePlan& plan, AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!previous.IsValid() || !previous.IsTyped() || !replacement.IsValid() || !replacement.IsTyped() ||
            previous == replacement || previous.ExpectedType() != replacement.ExpectedType())
        {
            SetFailure(failure, AssetOperationResult::InvalidArgument, {}, previous);
            return AssetOperationResult::InvalidArgument;
        }
        if (database.LookupOutput(replacement) == nullptr)
        {
            SetFailure(failure, AssetOperationResult::ReplacementUnavailable, {}, replacement);
            return AssetOperationResult::ReplacementUnavailable;
        }

        AssetReferenceRewritePlan staged;
        staged.m_previous = previous;
        staged.m_replacement = replacement;
        const containers::ArraySpan<const SourceRoot> roots = database.GetRoots();
        const containers::ArraySpan<const AssetReference> references = database.GetBackwardReferences(previous);
        for (const AssetReference& reference : references)
        {
            const SourceRecord* const source = database.LookupAsset(reference.from);
            if (source == nullptr || source->referenceScanState != SourceReferenceScanState::Scanned)
            {
                SetFailure(failure, AssetOperationResult::ReferenceStateUnavailable, reference.from, previous);
                return AssetOperationResult::ReferenceStateUnavailable;
            }
            if (source->root >= roots.Count() || roots[source->root].readOnly)
            {
                SetFailure(failure, AssetOperationResult::ReadOnlySource, reference.from, previous, source->importer);
                return AssetOperationResult::ReadOnlySource;
            }
            bool validRegistry = true;
            const SourceImporterDescriptor* const importer = FindImporter(importers, source->importer, validRegistry);
            if (!validRegistry || importer == nullptr || importer->version != source->availableImporterVersion ||
                source->metadataResult != MetadataResult::Success || source->metadata.importer != source->importer ||
                source->metadata.importerVersion != importer->version || source->importState != SourceImportState::MetadataSelected)
            {
                SetFailure(failure, AssetOperationResult::ImporterUnavailable, reference.from, previous, source->importer);
                return AssetOperationResult::ImporterUnavailable;
            }
            if (importer->rewriteSourceReferences == nullptr)
            {
                SetFailure(failure, AssetOperationResult::RewriteUnavailable, reference.from, previous, source->importer);
                return AssetOperationResult::RewriteUnavailable;
            }

            if (staged.m_sources.Empty() || !(staged.m_sources[staged.m_sources.Size() - 1u].asset == reference.from))
            {
                const u32 oldCount = staged.m_sources.Size();
                staged.m_sources.PushBack({reference.from, source->path, source->importer, staged.m_rewrites.Size(), 0u});
                if (staged.m_sources.Size() != oldCount + 1u || staged.m_sources[oldCount].path != source->path)
                {
                    SetFailure(failure, AssetOperationResult::LimitExceeded, reference.from, previous, source->importer);
                    return AssetOperationResult::LimitExceeded;
                }
            }
            const u32 oldCount = staged.m_rewrites.Size();
            staged.m_rewrites.PushBack({reference, replacement});
            if (staged.m_rewrites.Size() != oldCount + 1u)
            {
                SetFailure(failure, AssetOperationResult::LimitExceeded, reference.from, previous, source->importer);
                return AssetOperationResult::LimitExceeded;
            }
            ++staged.m_sources[staged.m_sources.Size() - 1u].rewriteCount;
        }

        plan = static_cast<AssetReferenceRewritePlan&&>(staged);
        return AssetOperationResult::Success;
    }

    AssetOperationResult ExecuteAssetAdopt(const AssetOperationContext& context, const filesystem::AbsolutePath& sourcePath,
                                           const SourceImporterId expectedImporter, const u32 expectedImporterVersion,
                                           const AssetId newIdentity, const SourceImportDescription& description,
                                           AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !sourcePath.IsFilePath() || expectedImporter == InvalidSourceImporterId ||
            expectedImporterVersion == 0u || !newIdentity.IsValid())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument)
                                      : AssetOperationResult::InvalidArgument;

        const SourceRecord* const source = context.database->LookupResource(sourcePath);
        if (source == nullptr || source->importState != SourceImportState::Candidate || source->issues != MetadataMissing ||
            source->importer != expectedImporter || source->availableImporterVersion != expectedImporterVersion)
        {
            SetFailure(failure, AssetOperationResult::CandidateUnavailable, {}, {}, expectedImporter);
            return AssetOperationResult::CandidateUnavailable;
        }
        const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
        if (source->root >= roots.Count() || roots[source->root].readOnly)
        {
            SetFailure(failure, AssetOperationResult::ReadOnlySource, {}, {}, expectedImporter);
            return AssetOperationResult::ReadOnlySource;
        }

        bool validRegistry = true;
        const SourceImporterDescriptor* const importer = FindImporter(context.importers, expectedImporter, validRegistry);
        if (!validRegistry || importer == nullptr || importer->version != expectedImporterVersion)
        {
            SetFailure(failure, AssetOperationResult::ImporterUnavailable, {}, {}, expectedImporter);
            return AssetOperationResult::ImporterUnavailable;
        }
        containers::String encoded;
        const AssetOperationResult metadataResult = EncodeAssetImportMetadata(*importer, newIdentity, description, encoded, failure);
        if (metadataResult != AssetOperationResult::Success)
            return metadataResult;
        AssetFileTransactionResult transaction = context.transaction->Begin();
        if (transaction == AssetFileTransactionResult::Success)
            transaction = context.transaction->StageCreate(
                MetadataPath(sourcePath), {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
        if (transaction != AssetFileTransactionResult::Success)
        {
            if (context.transaction->GetState() == AssetFileTransactionState::Collecting)
                static_cast<void>(context.transaction->Rollback());
            SetFailure(failure, AssetOperationResult::TransactionFailed, newIdentity, {}, expectedImporter,
                       ProjectSourceDataResult::Success, transaction);
            return AssetOperationResult::TransactionFailed;
        }
        const AssetOperationResult result = FinishTransaction(*context.transaction, failure,
                                                              context.commitTrackedPaths, context.commitPostConditions);
        if (failure != nullptr)
        {
            failure->source = newIdentity;
            failure->importer = expectedImporter;
        }
        return result;
    }

    AssetOperationResult ExecuteAssetCreateAndAdopt(
        const AssetOperationContext& context, const filesystem::AbsolutePath& destination,
        const containers::ArraySpan<const u8> sourceBytes, const SourceImporterId expectedImporter,
        const u32 expectedImporterVersion, const AssetId newIdentity,
        const SourceImportDescription& description, AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !destination.IsFilePath() ||
            (!sourceBytes.Empty() && sourceBytes.Data() == nullptr) || expectedImporter == InvalidSourceImporterId ||
            expectedImporterVersion == 0u || !newIdentity.IsValid())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument)
                                      : AssetOperationResult::InvalidArgument;

        const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
        bool writable = false;
        for (const SourceRoot& root : roots)
            if (!root.readOnly && filesystem::ValidateMutationPath(root.directory, destination) == filesystem::MutationPathResult::Success)
            {
                writable = true;
                break;
            }
        if (!writable)
        {
            SetFailure(failure, AssetOperationResult::ReadOnlySource, {}, {}, expectedImporter);
            return AssetOperationResult::ReadOnlySource;
        }
        if (context.database->LookupResource(destination) != nullptr)
        {
            SetFailure(failure, AssetOperationResult::TargetConflict, {}, {}, expectedImporter);
            return AssetOperationResult::TargetConflict;
        }

        bool validRegistry = true;
        const SourceImporterDescriptor* const importer = FindImporter(context.importers, expectedImporter, validRegistry);
        if (!validRegistry || importer == nullptr || importer->version != expectedImporterVersion ||
            importer->recognizeSource == nullptr || !importer->recognizeSource(destination.AsStringView(), importer->userData))
        {
            SetFailure(failure, AssetOperationResult::ImporterUnavailable, {}, {}, expectedImporter);
            return AssetOperationResult::ImporterUnavailable;
        }

        containers::String encoded;
        const AssetOperationResult metadataResult = EncodeAssetImportMetadata(*importer, newIdentity, description, encoded, failure);
        if (metadataResult != AssetOperationResult::Success)
            return metadataResult;

        AssetFileTransactionResult transaction = context.transaction->Begin();
        if (transaction == AssetFileTransactionResult::Success)
            transaction = context.transaction->StageCreate(destination, sourceBytes);
        if (transaction == AssetFileTransactionResult::Success)
            transaction = context.transaction->StageCreate(
                MetadataPath(destination), {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
        if (transaction != AssetFileTransactionResult::Success)
        {
            if (context.transaction->GetState() == AssetFileTransactionState::Collecting)
                static_cast<void>(context.transaction->Rollback());
            const AssetOperationResult result = transaction == AssetFileTransactionResult::Conflict
                                                    ? AssetOperationResult::TargetConflict
                                                    : AssetOperationResult::TransactionFailed;
            SetFailure(failure, result, newIdentity, {}, expectedImporter,
                       ProjectSourceDataResult::Success, transaction);
            return result;
        }

        AssetOperationResult result = FinishTransaction(*context.transaction, failure,
                                                        context.commitTrackedPaths, context.commitPostConditions);
        if (result == AssetOperationResult::TransactionFailed && failure != nullptr &&
            failure->transaction == AssetFileTransactionResult::Conflict)
        {
            failure->result = AssetOperationResult::TargetConflict;
            result = AssetOperationResult::TargetConflict;
        }
        if (failure != nullptr)
        {
            failure->source = newIdentity;
            failure->importer = expectedImporter;
        }
        return result;
    }

    AssetOperationResult ExecuteAssetUpdateSettings(const AssetOperationContext& context, const AssetId asset,
                                                    const u32 settingsVersion, const containers::ArraySpan<const u8> settings,
                                                    AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !asset.IsValid() || settingsVersion == 0u ||
            (!settings.Empty() && settings.Data() == nullptr) || settings.Count() > MaximumAssetSettingsBytes)
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument)
                                      : AssetOperationResult::InvalidArgument;
        const SourceRecord* const source = context.database->LookupAsset(asset);
        if (source == nullptr)
        {
            SetFailure(failure, AssetOperationResult::SourceUnavailable, asset);
            return AssetOperationResult::SourceUnavailable;
        }
        const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
        if (source->root >= roots.Count() || roots[source->root].readOnly)
        {
            SetFailure(failure, AssetOperationResult::ReadOnlySource, asset, {}, source->importer);
            return AssetOperationResult::ReadOnlySource;
        }
        const SourceImporterDescriptor* const importer = CheckedImporter(context, *source, failure);
        if (importer == nullptr)
            return AssetOperationResult::ImporterUnavailable;
        if (importer->validateSettings == nullptr || !importer->validateSettings(settingsVersion, settings, importer->userData))
        {
            SetFailure(failure, AssetOperationResult::SettingsInvalid, asset, {}, source->importer);
            return AssetOperationResult::SettingsInvalid;
        }

        containers::DynamicArray<u8> ownedExpected(memory::pools::Assets::GetInstance());
        containers::ArraySpan<const u8> expected = context.selectedMetadataBytes;
        AssetMetadata current;
        if (expected.Empty())
        {
            if (!ReadMetadataFile(*context.files, source->path, ownedExpected, current))
            {
                SetFailure(failure, AssetOperationResult::SourceUnavailable, asset, {}, source->importer);
                return AssetOperationResult::SourceUnavailable;
            }
            expected = {ownedExpected.TypedData(), ownedExpected.Size()};
        }
        else if (expected.Data() == nullptr || expected.Count() > MaximumAssetMetadataBytes ||
                 ParseMetadata({reinterpret_cast<const char*>(expected.Data()), expected.Count()}, current) != MetadataResult::Success)
        {
            SetFailure(failure, AssetOperationResult::SourceUnavailable, asset, {}, source->importer);
            return AssetOperationResult::SourceUnavailable;
        }
        if (!SameMetadata(current, source->metadata))
        {
            SetFailure(failure, AssetOperationResult::SourceUnavailable, asset, {}, source->importer);
            return AssetOperationResult::SourceUnavailable;
        }
        current.settingsVersion = settingsVersion;
        current.settings.Resize(settings.Count());
        if (current.settings.Size() != settings.Count())
        {
            SetFailure(failure, AssetOperationResult::LimitExceeded, asset, {}, source->importer);
            return AssetOperationResult::LimitExceeded;
        }
        if (!settings.Empty())
            std::memcpy(current.settings.TypedData(), settings.Data(), settings.Count());
        if (ValidateMetadata(current) != MetadataResult::Success)
        {
            SetFailure(failure, AssetOperationResult::SettingsInvalid, asset, {}, source->importer);
            return AssetOperationResult::SettingsInvalid;
        }
        containers::String encoded;
        if (EncodeMetadata(current, encoded) != MetadataResult::Success)
        {
            SetFailure(failure, AssetOperationResult::LimitExceeded, asset, {}, source->importer);
            return AssetOperationResult::LimitExceeded;
        }

        AssetFileTransactionResult transaction = context.transaction->Begin();
        if (transaction == AssetFileTransactionResult::Success)
            transaction = context.transaction->StageWriteIfUnchanged(
                MetadataPath(source->path), expected,
                {reinterpret_cast<const u8*>(encoded.AsChar()), encoded.Length()});
        if (transaction != AssetFileTransactionResult::Success)
        {
            if (context.transaction->GetState() == AssetFileTransactionState::Collecting)
                static_cast<void>(context.transaction->Rollback());
            SetFailure(failure, AssetOperationResult::TransactionFailed, asset, {}, source->importer,
                       ProjectSourceDataResult::Success, transaction);
            return AssetOperationResult::TransactionFailed;
        }
        const AssetOperationResult result = FinishTransaction(*context.transaction, failure,
                                                              context.commitTrackedPaths, context.commitPostConditions);
        if (failure != nullptr)
        {
            failure->source = asset;
            failure->importer = source->importer;
        }
        return result;
    }

    AssetOperationResult ExecuteAssetMove(const AssetOperationContext& context, const AssetId asset,
                                          const filesystem::AbsolutePath& target, AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !asset.IsValid() || !target.IsFilePath())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument) : AssetOperationResult::InvalidArgument;
        const SourceRecord* const source = context.database->LookupAsset(asset);
        containers::StringView relative;
        const i32 targetRoot = WritableRoot(*context.database, target, relative);
        if (source == nullptr)
            return failure != nullptr ? (failure->result = AssetOperationResult::SourceUnavailable) : AssetOperationResult::SourceUnavailable;
        const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
        if (source->root >= roots.Count() || roots[source->root].readOnly || targetRoot < 0)
            return failure != nullptr ? (failure->result = AssetOperationResult::ReadOnlySource) : AssetOperationResult::ReadOnlySource;
        if (source->path == target || context.database->LookupResource(target) != nullptr || context.files->FileExist(target) ||
            context.files->FileExist(MetadataPath(target)))
            return failure != nullptr ? (failure->result = AssetOperationResult::TargetConflict) : AssetOperationResult::TargetConflict;
        const SourceImporterDescriptor* const importer = CheckedImporter(context, *source, failure);
        if (importer == nullptr)
            return AssetOperationResult::ImporterUnavailable;
        if ((importer->recognizeSource != nullptr && !importer->recognizeSource(target.AsStringView(), importer->userData)) ||
            importer->relocateSource == nullptr)
            return failure != nullptr ? (failure->result = AssetOperationResult::RelocationUnavailable) : AssetOperationResult::RelocationUnavailable;
        ProjectSourceData data;
        const ProjectSourceDataResult loaded = LoadProjectSourceData(*context.files, *source, data, context.sourceData);
        if (loaded != ProjectSourceDataResult::Success)
        {
            SetFailure(failure, AssetOperationResult::SourceUnavailable, asset, {}, source->importer, loaded);
            return AssetOperationResult::SourceUnavailable;
        }
        containers::DynamicArray<u8> relocated(memory::pools::Assets::GetInstance());
        AssetMetadata metadata;
        if (!importer->relocateSource(*source, data.View(context.target), relative, relocated, metadata, importer->userData) ||
            !PreservesSchema(source->metadata, metadata))
            return failure != nullptr ? (failure->result = AssetOperationResult::RewriteFailed) : AssetOperationResult::RewriteFailed;
        AssetFileTransactionResult transaction = context.transaction->Begin();
        if (transaction != AssetFileTransactionResult::Success)
        {
            SetFailure(failure, AssetOperationResult::TransactionFailed, asset, {}, source->importer,
                       ProjectSourceDataResult::Success, transaction);
            return AssetOperationResult::TransactionFailed;
        }
        if (!EncodeAndStage(context, target, relocated, metadata, failure) ||
            StageContextDelete(context, source->path) != AssetFileTransactionResult::Success ||
            StageContextDelete(context, MetadataPath(source->path)) != AssetFileTransactionResult::Success)
        {
            static_cast<void>(context.transaction->Rollback());
            if (failure == nullptr || failure->result == AssetOperationResult::Success)
                SetFailure(failure, AssetOperationResult::TransactionFailed, asset, {}, source->importer);
            return AssetOperationResult::TransactionFailed;
        }
        return FinishTransaction(*context.transaction, failure, context.commitTrackedPaths, context.commitPostConditions);
    }

    AssetOperationResult ExecuteAssetDuplicate(const AssetOperationContext& context, const AssetId asset,
                                               const filesystem::AbsolutePath& target, const AssetId newIdentity,
                                               AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !asset.IsValid() || !newIdentity.IsValid() || asset == newIdentity || !target.IsFilePath())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument) : AssetOperationResult::InvalidArgument;
        const SourceRecord* const source = context.database->LookupAsset(asset);
        containers::StringView relative;
        const i32 destinationRoot = WritableRoot(*context.database, target, relative);
        if (source == nullptr || destinationRoot < 0)
            return failure != nullptr ? (failure->result = AssetOperationResult::SourceUnavailable) : AssetOperationResult::SourceUnavailable;
        if (context.database->LookupAsset(newIdentity) != nullptr || context.database->LookupResource(target) != nullptr ||
            context.files->FileExist(target) || context.files->FileExist(MetadataPath(target)))
            return failure != nullptr ? (failure->result = AssetOperationResult::IdentityConflict) : AssetOperationResult::IdentityConflict;
        for (const AssetOutput& output : source->metadata.outputs)
        {
            if (context.database->LookupOutput(GetAssetOutputReference(newIdentity, output)) != nullptr)
                return failure != nullptr ? (failure->result = AssetOperationResult::IdentityConflict) : AssetOperationResult::IdentityConflict;
        }
        const SourceImporterDescriptor* const importer = CheckedImporter(context, *source, failure);
        if (importer == nullptr || (importer->recognizeSource != nullptr && !importer->recognizeSource(target.AsStringView(), importer->userData)) ||
            importer->relocateSource == nullptr)
            return importer == nullptr ? AssetOperationResult::ImporterUnavailable
                                       : (failure != nullptr ? (failure->result = AssetOperationResult::RelocationUnavailable)
                                                             : AssetOperationResult::RelocationUnavailable);
        ProjectSourceData data;
        const ProjectSourceDataResult loaded = LoadProjectSourceData(*context.files, *source, data, context.sourceData);
        if (loaded != ProjectSourceDataResult::Success)
        {
            SetFailure(failure, AssetOperationResult::SourceUnavailable, asset, {}, source->importer, loaded);
            return AssetOperationResult::SourceUnavailable;
        }
        containers::DynamicArray<u8> relocated(memory::pools::Assets::GetInstance());
        AssetMetadata metadata;
        if (!importer->relocateSource(*source, data.View(context.target), relative, relocated, metadata, importer->userData) ||
            !PreservesSchema(source->metadata, metadata))
            return failure != nullptr ? (failure->result = AssetOperationResult::RewriteFailed) : AssetOperationResult::RewriteFailed;

        containers::DynamicArray<AssetReferenceReplacement> self(memory::pools::Assets::GetInstance());
        for (const AssetReference& reference : context.database->GetForwardReferences(asset))
            if (reference.inPlace)
                for (const AssetOutput& output : source->metadata.outputs)
                {
                    const resources::ResourceReference previous = GetAssetOutputReference(asset, output);
                    if (reference.to == previous)
                    {
                        const u32 oldCount = self.Size();
                        self.PushBack({previous, GetAssetOutputReference(newIdentity, output)});
                        if (self.Size() != oldCount + 1u)
                            return failure != nullptr ? (failure->result = AssetOperationResult::LimitExceeded)
                                                      : AssetOperationResult::LimitExceeded;
                    }
                }
        if (!self.Empty())
        {
            if (importer->rewriteSourceReferences == nullptr)
                return failure != nullptr ? (failure->result = AssetOperationResult::RewriteUnavailable) : AssetOperationResult::RewriteUnavailable;
            SourceRecord relocatedRecord = *source;
            relocatedRecord.path = target;
            relocatedRecord.root = static_cast<u32>(destinationRoot);
            relocatedRecord.metadata = metadata;
            relocatedRecord.sourceIdentity = resources::ResourcePath::FromString(relative);
            containers::String relocatedMetadataText;
            if (EncodeMetadata(metadata, relocatedMetadataText) != MetadataResult::Success)
                return failure != nullptr ? (failure->result = AssetOperationResult::RewriteFailed) : AssetOperationResult::RewriteFailed;
            const SourceDataView relocatedView{{relocated.TypedData(), relocated.Size()},
                                               {reinterpret_cast<const u8*>(relocatedMetadataText.AsChar()), relocatedMetadataText.Length()},
                                               context.target};
            containers::DynamicArray<u8> rewritten(memory::pools::Assets::GetInstance());
            AssetMetadata rewrittenMetadata;
            if (!importer->rewriteSourceReferences(relocatedRecord, relocatedView, self, rewritten, rewrittenMetadata, importer->userData) ||
                !PreservesSchema(metadata, rewrittenMetadata))
                return failure != nullptr ? (failure->result = AssetOperationResult::RewriteFailed) : AssetOperationResult::RewriteFailed;
            relocated = static_cast<containers::DynamicArray<u8>&&>(rewritten);
            metadata = static_cast<AssetMetadata&&>(rewrittenMetadata);
        }
        metadata.id = newIdentity;
        if (ValidateMetadata(metadata) != MetadataResult::Success)
            return failure != nullptr ? (failure->result = AssetOperationResult::IdentityConflict) : AssetOperationResult::IdentityConflict;
        const AssetFileTransactionResult begun = context.transaction->Begin();
        if (begun != AssetFileTransactionResult::Success)
        {
            SetFailure(failure, AssetOperationResult::TransactionFailed, asset, {}, source->importer,
                       ProjectSourceDataResult::Success, begun);
            return AssetOperationResult::TransactionFailed;
        }
        if (!EncodeAndStage(context, target, relocated, metadata, failure))
        {
            static_cast<void>(context.transaction->Rollback());
            return failure != nullptr ? failure->result : AssetOperationResult::TransactionFailed;
        }
        const AssetOperationResult result = FinishTransaction(*context.transaction, failure,
                                                              context.commitTrackedPaths, context.commitPostConditions);
        if (result == AssetOperationResult::Success && failure != nullptr)
            failure->source = newIdentity;
        return result;
    }

    AssetOperationResult ExecuteAssetRetarget(const AssetOperationContext& context,
                                              const containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                              AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || replacements.Empty())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument) : AssetOperationResult::InvalidArgument;
        const AssetFileTransactionResult begun = context.transaction->Begin();
        if (begun != AssetFileTransactionResult::Success)
        {
            SetFailure(failure, AssetOperationResult::TransactionFailed, {}, {}, InvalidSourceImporterId,
                       ProjectSourceDataResult::Success, begun);
            return AssetOperationResult::TransactionFailed;
        }
        bool wroteSource = false;
        const AssetOperationResult staged = StageReferenceRewrites(context, replacements, {}, failure, &wroteSource);
        if (staged != AssetOperationResult::Success)
        {
            static_cast<void>(context.transaction->Rollback());
            return staged;
        }
        if (!wroteSource)
        {
            static_cast<void>(context.transaction->Rollback());
            return AssetOperationResult::Success;
        }
        return FinishTransaction(*context.transaction, failure, context.commitTrackedPaths, context.commitPostConditions);
    }

    AssetOperationResult ExecuteAssetDelete(const AssetOperationContext& context, const AssetId asset,
                                            const containers::ArraySpan<const AssetReferenceReplacement> replacements,
                                            AssetOperationFailure* const failure) noexcept
    {
        SetFailure(failure, AssetOperationResult::Success);
        if (!ValidContext(context) || !asset.IsValid())
            return failure != nullptr ? (failure->result = AssetOperationResult::InvalidArgument) : AssetOperationResult::InvalidArgument;
        const SourceRecord* const source = context.database->LookupAsset(asset);
        if (source == nullptr)
            return failure != nullptr ? (failure->result = AssetOperationResult::SourceUnavailable) : AssetOperationResult::SourceUnavailable;
        const containers::ArraySpan<const SourceRoot> roots = context.database->GetRoots();
        if (source->root >= roots.Count() || roots[source->root].readOnly)
            return failure != nullptr ? (failure->result = AssetOperationResult::ReadOnlySource) : AssetOperationResult::ReadOnlySource;
        for (const AssetReferenceReplacement& replacement : replacements)
        {
            bool deletesPrevious = false;
            for (const AssetOutput& output : source->metadata.outputs)
                if (replacement.previous == GetAssetOutputReference(asset, output))
                    deletesPrevious = true;
            if (!deletesPrevious)
            {
                SetFailure(failure, AssetOperationResult::InvalidArgument, asset, replacement.previous);
                return AssetOperationResult::InvalidArgument;
            }
            if (context.database->LookupOutput(replacement.replacement) == source)
            {
                SetFailure(failure, AssetOperationResult::ReplacementUnavailable, asset, replacement.replacement);
                return AssetOperationResult::ReplacementUnavailable;
            }
        }
        for (const AssetOutput& output : source->metadata.outputs)
        {
            const resources::ResourceReference previous = GetAssetOutputReference(asset, output);
            resources::ResourceReference replacement;
            const bool hasReplacement = FindReplacement(replacements, previous, replacement);
            for (const AssetReference& reference : context.database->GetBackwardReferences(previous))
                if (!(reference.from == asset) && !hasReplacement)
                {
                    SetFailure(failure, AssetOperationResult::ReferencesRemain, reference.from, previous);
                    return AssetOperationResult::ReferencesRemain;
                }
        }
        const AssetFileTransactionResult begun = context.transaction->Begin();
        if (begun != AssetFileTransactionResult::Success)
            return failure != nullptr ? (failure->result = AssetOperationResult::TransactionFailed) : AssetOperationResult::TransactionFailed;
        const AssetOperationResult rewritten = StageReferenceRewrites(context, replacements, asset, failure);
        if (rewritten != AssetOperationResult::Success ||
            StageContextDelete(context, source->path) != AssetFileTransactionResult::Success ||
            StageContextDelete(context, MetadataPath(source->path)) != AssetFileTransactionResult::Success)
        {
            static_cast<void>(context.transaction->Rollback());
            if (rewritten != AssetOperationResult::Success)
                return rewritten;
            return failure != nullptr ? (failure->result = AssetOperationResult::TransactionFailed) : AssetOperationResult::TransactionFailed;
        }
        return FinishTransaction(*context.transaction, failure, context.commitTrackedPaths, context.commitPostConditions);
    }
} // namespace vanguard::assets
