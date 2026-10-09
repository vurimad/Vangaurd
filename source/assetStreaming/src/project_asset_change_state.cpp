#include <vanguard/asset_streaming/project_asset_change_state_internal.hpp>

#include <algorithm>

namespace vanguard::asset_streaming
{
    namespace
    {
        constexpr u32 ConflictIssues = assets::AssetIdentityConflict | assets::OutputIdentityConflict | assets::SourceIdentityConflict;

        struct AssetSlice
        {
            u32 outputOffset = 0;
            u32 outputCount = 0;
            u32 referenceOffset = 0;
            u32 referenceCount = 0;
        };

        struct PendingChange
        {
            assets::AssetId asset;
            ProjectAssetChangeReason reasons = ProjectAssetChangeReason::None;
            i32 previousIndex = -1;
            i32 currentIndex = -1;
        };

        [[nodiscard]] i32 CompareReferenceTarget(const resources::ResourceReference left,
                                                 const resources::ResourceReference right) noexcept
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

        [[nodiscard]] i32 CompareAssetId(const assets::AssetId& left, const assets::AssetId& right) noexcept
        {
            for (u32 index = 0; index < 16u; ++index)
            {
                if (left.bytes[index] < right.bytes[index])
                    return -1;
                if (left.bytes[index] > right.bytes[index])
                    return 1;
            }
            return 0;
        }

        [[nodiscard]] bool ReferenceDetailsLess(const assets::AssetReference& left, const assets::AssetReference& right) noexcept
        {
            if (left.location != right.location)
                return left.location < right.location;
            if (left.kind != right.kind)
                return left.kind < right.kind;
            return left.inPlace < right.inPlace;
        }

        [[nodiscard]] bool ReferenceToLess(const assets::AssetReference& left, const assets::AssetReference& right) noexcept
        {
            const i32 to = CompareReferenceTarget(left.to, right.to);
            if (to != 0)
                return to < 0;
            const i32 from = CompareAssetId(left.from, right.from);
            return from != 0 ? from < 0 : ReferenceDetailsLess(left, right);
        }

        [[nodiscard]] bool SameReference(const assets::AssetReference& left, const assets::AssetReference& right) noexcept
        {
            return left.from == right.from && left.to == right.to && left.kind == right.kind && left.location == right.location &&
                   left.inPlace == right.inPlace;
        }

        [[nodiscard]] bool SameOutput(const ProjectAssetOutputState& left, const ProjectAssetOutputState& right) noexcept
        {
            return containers::StringView(left.key) == containers::StringView(right.key) && left.resource == right.resource;
        }

        [[nodiscard]] bool OutputLess(const ProjectAssetOutputState& left, const ProjectAssetOutputState& right) noexcept
        {
            const containers::StringView leftKey(left.key);
            const containers::StringView rightKey(right.key);
            if (leftKey != rightKey)
                return leftKey < rightKey;
            return CompareReferenceTarget(left.resource, right.resource) < 0;
        }

        [[nodiscard]] bool ResourceLess(const resources::ResourceReference left, const resources::ResourceReference right) noexcept
        {
            return CompareReferenceTarget(left, right) < 0;
        }

        [[nodiscard]] bool MakeAssetLookupKey(const assets::AssetId& asset, containers::String& key) noexcept
        {
            return asset.IsValid() && assets::GetAssetOutputPath(asset, "source", key);
        }

        // Importer support and typed source-seed availability are deliberately
        // separate. A valid callback importer may create its BuildRequest without
        // declaring a descriptor-level sourceType; SourceImporterDescriptor and
        // ProjectAssetBuildSystem explicitly support that path. E2.2 must retain
        // the asset as supported and preserve its dirty change evidence. E2.3 owns
        // translating such custom-importer evidence into concrete build requests.
        [[nodiscard]] bool IsSupported(const ProjectAssetState& state) noexcept
        {
            return state.presence == ProjectAssetPresence::Present && state.sourceIssues == assets::SourceIssueNone &&
                   state.importState == assets::SourceImportState::MetadataSelected &&
                   state.importer != assets::InvalidSourceImporterId &&
                   state.authoredImporterVersion != 0u && state.authoredImporterVersion == state.availableImporterVersion;
        }

        [[nodiscard]] bool HasTypedInvalidationSeed(const ProjectAssetState& state) noexcept
        {
            return IsSupported(state) && state.sourceIdentity.IsValid() && state.sourceType != resources::InvalidResourceTypeId;
        }

        [[nodiscard]] bool SameLimits(const ProjectAssetChangeStateLimits& left, const ProjectAssetChangeStateLimits& right) noexcept
        {
            return left.maximumAssets == right.maximumAssets && left.maximumTombstones == right.maximumTombstones &&
                   left.maximumOutputs == right.maximumOutputs && left.maximumReferences == right.maximumReferences &&
                   left.maximumDiagnostics == right.maximumDiagnostics && left.maximumChanges == right.maximumChanges &&
                   left.maximumChangeOutputs == right.maximumChangeOutputs && left.maximumChangeReferences == right.maximumChangeReferences &&
                   left.maximumInvalidationSeeds == right.maximumInvalidationSeeds &&
                   left.maximumInvalidationOutputs == right.maximumInvalidationOutputs &&
                   left.maximumSerializedBytes == right.maximumSerializedBytes;
        }

        [[nodiscard]] bool SameOutputs(const containers::ArraySpan<const ProjectAssetOutputState> left,
                                       const containers::ArraySpan<const ProjectAssetOutputState> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0; index < left.Count(); ++index)
                if (!SameOutput(left[index], right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool SameReferences(const containers::ArraySpan<const assets::AssetReference> left,
                                          const containers::ArraySpan<const assets::AssetReference> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0; index < left.Count(); ++index)
                if (!SameReference(left[index], right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] ProjectAssetChangeReason CompareState(const ProjectAssetState& previous, const ProjectAssetState& current) noexcept
        {
            ProjectAssetChangeReason reasons = ProjectAssetChangeReason::None;
            if (previous.presence != current.presence &&
                (previous.presence == ProjectAssetPresence::Conflicting || current.presence == ProjectAssetPresence::Conflicting))
                reasons |= ProjectAssetChangeReason::Conflicting;
            if (previous.presence != ProjectAssetPresence::Missing && current.presence == ProjectAssetPresence::Missing)
                reasons |= ProjectAssetChangeReason::Missing;
            if (previous.presence == ProjectAssetPresence::Missing && current.presence == ProjectAssetPresence::Present)
                reasons |= ProjectAssetChangeReason::Restored;
            if (previous.sourceRoot != current.sourceRoot || previous.sourceIdentity != current.sourceIdentity)
                reasons |= ProjectAssetChangeReason::Moved;
            if (previous.hasSourceContentFingerprint != current.hasSourceContentFingerprint ||
                (previous.hasSourceContentFingerprint && previous.sourceContentFingerprint != current.sourceContentFingerprint))
                reasons |= ProjectAssetChangeReason::ContentChanged;
            if (previous.settingsVersion != current.settingsVersion || previous.settingsFingerprint != current.settingsFingerprint)
                reasons |= ProjectAssetChangeReason::SettingsChanged;
            if (previous.importer != current.importer || previous.sourceType != current.sourceType)
                reasons |= ProjectAssetChangeReason::ImporterChanged;
            if (previous.authoredImporterVersion != current.authoredImporterVersion ||
                previous.availableImporterVersion != current.availableImporterVersion)
                reasons |= ProjectAssetChangeReason::ImporterVersionChanged;
            if (!SameOutputs(previous.outputs, current.outputs))
                reasons |= ProjectAssetChangeReason::OutputsChanged;
            if (!SameReferences(previous.references, current.references) || previous.referenceScanState != current.referenceScanState)
                reasons |= ProjectAssetChangeReason::ReferencesChanged;
            const bool wasSupported = IsSupported(previous);
            const bool isSupported = IsSupported(current);
            if (current.presence == ProjectAssetPresence::Present && !isSupported &&
                (wasSupported || previous.importState != current.importState))
                reasons |= ProjectAssetChangeReason::Unsupported;
            else if (!wasSupported && isSupported)
                reasons |= ProjectAssetChangeReason::Supported;
            return reasons;
        }

        [[nodiscard]] bool AppendOutput(containers::DynamicArray<ProjectAssetOutputState>& destination,
                                        const ProjectAssetOutputState& output, const u32 limit) noexcept
        {
            if (destination.Size() >= limit)
                return false;
            const u32 before = destination.Size();
            destination.PushBack(output);
            return destination.Size() == before + 1u;
        }

        [[nodiscard]] bool AppendReference(containers::DynamicArray<assets::AssetReference>& destination,
                                           const assets::AssetReference& reference, const u32 limit) noexcept
        {
            if (destination.Size() >= limit)
                return false;
            const u32 before = destination.Size();
            destination.PushBack(reference);
            return destination.Size() == before + 1u;
        }

        [[nodiscard]] bool AppendStateStorage(ProjectAssetAcceptedBaseline& destination, const ProjectAssetState& source,
                                              AssetSlice& slice) noexcept
        {
            slice.outputOffset = destination.outputs.Size();
            for (const ProjectAssetOutputState& output : source.outputs)
                if (!AppendOutput(destination.outputs, output, destination.limits.maximumOutputs))
                    return false;
            slice.outputCount = destination.outputs.Size() - slice.outputOffset;
            slice.referenceOffset = destination.references.Size();
            for (const assets::AssetReference& reference : source.references)
                if (!AppendReference(destination.references, reference, destination.limits.maximumReferences))
                    return false;
            slice.referenceCount = destination.references.Size() - slice.referenceOffset;
            return true;
        }

        [[nodiscard]] bool AppendCandidateState(ProjectAssetAcceptedBaseline& destination, const assets::SourceDatabase& candidate,
                                                const assets::SourceRecord& row, const ProjectAssetPreparedSourceEvidence& evidence,
                                                const ProjectAssetPresence presence, AssetSlice& slice) noexcept
        {
            if (row.root >= candidate.GetRoots().Count())
                return false;
            ProjectAssetState state;
            state.asset = row.metadata.id;
            state.sourceRoot = candidate.GetRoots()[row.root].directory;
            state.sourceIdentity = row.sourceIdentity;
            state.physicalPath = row.path;
            state.sourceContentFingerprint = evidence.sourceContentFingerprint;
            state.settingsFingerprint = crypto::Sha256(row.metadata.settings.TypedData(), row.metadata.settings.Size());
            state.importer = row.metadata.importer;
            state.authoredImporterVersion = row.metadata.importerVersion;
            state.availableImporterVersion = row.availableImporterVersion;
            state.settingsVersion = row.metadata.settingsVersion;
            state.sourceType = row.sourceType;
            state.sourceIssues = row.issues;
            state.importState = row.importState;
            state.referenceScanState = row.referenceScanState;
            state.presence = presence;
            state.hasSourceContentFingerprint = evidence.hasSourceContentFingerprint;

            containers::DynamicArray<ProjectAssetOutputState> canonicalOutputs(memory::pools::Assets::GetInstance());
            canonicalOutputs.Reserve(row.metadata.outputs.Size());
            for (const assets::AssetOutput& output : row.metadata.outputs)
            {
                ProjectAssetOutputState canonical;
                canonical.key = output.key;
                canonical.resource = assets::GetAssetOutputReference(row.metadata.id, output);
                if (!canonical.resource.IsValid() || !canonical.resource.IsTyped())
                    return false;
                const u32 before = canonicalOutputs.Size();
                canonicalOutputs.PushBack(static_cast<ProjectAssetOutputState&&>(canonical));
                if (canonicalOutputs.Size() != before + 1u)
                    return false;
            }
            if (canonicalOutputs.Size() > 1u)
                std::sort(canonicalOutputs.Begin(), canonicalOutputs.End(), OutputLess);

            slice.outputOffset = destination.outputs.Size();
            for (const ProjectAssetOutputState& output : canonicalOutputs)
                if (!AppendOutput(destination.outputs, output, destination.limits.maximumOutputs))
                    return false;
            slice.outputCount = destination.outputs.Size() - slice.outputOffset;
            slice.referenceOffset = destination.references.Size();
            for (const assets::AssetReference& reference : candidate.GetForwardReferences(row.metadata.id))
                if (!AppendReference(destination.references, reference, destination.limits.maximumReferences))
                    return false;
            slice.referenceCount = destination.references.Size() - slice.referenceOffset;

            if (destination.assets.Size() >= destination.limits.maximumAssets)
                return false;
            const u32 before = destination.assets.Size();
            destination.assets.PushBack(static_cast<ProjectAssetState&&>(state));
            if (destination.assets.Size() != before + 1u)
                return false;
            ProjectAssetState& stored = destination.assets[destination.assets.Size() - 1u];
            stored.outputs = {destination.outputs.TypedData() + slice.outputOffset, slice.outputCount};
            stored.references = {destination.references.TypedData() + slice.referenceOffset, slice.referenceCount};
            return true;
        }

        [[nodiscard]] bool AppendPreviousState(ProjectAssetAcceptedBaseline& destination, const ProjectAssetState& previous,
                                               const ProjectAssetPresence presence, AssetSlice& slice) noexcept
        {
            ProjectAssetState state = previous;
            state.presence = presence;
            if (!AppendStateStorage(destination, previous, slice) || destination.assets.Size() >= destination.limits.maximumAssets)
                return false;
            const u32 before = destination.assets.Size();
            destination.assets.PushBack(static_cast<ProjectAssetState&&>(state));
            if (destination.assets.Size() != before + 1u)
                return false;
            ProjectAssetState& stored = destination.assets[destination.assets.Size() - 1u];
            stored.outputs = {destination.outputs.TypedData() + slice.outputOffset, slice.outputCount};
            stored.references = {destination.references.TypedData() + slice.referenceOffset, slice.referenceCount};
            return true;
        }

        void BindStateSlices(ProjectAssetAcceptedBaseline& snapshot, const containers::DynamicArray<AssetSlice>& slices) noexcept
        {
            for (u32 index = 0; index < snapshot.assets.Size(); ++index)
            {
                ProjectAssetState& state = snapshot.assets[index];
                const AssetSlice& slice = slices[index];
                state.outputs = {snapshot.outputs.TypedData() + slice.outputOffset, slice.outputCount};
                state.references = {snapshot.references.TypedData() + slice.referenceOffset, slice.referenceCount};
            }
        }

        [[nodiscard]] bool SameDiagnostic(const ProjectAssetSourceDiagnostic& left, const ProjectAssetSourceDiagnostic& right) noexcept
        {
            return left.sourceRoot == right.sourceRoot && left.sourceIdentity == right.sourceIdentity &&
                   left.physicalPath == right.physicalPath && left.asset == right.asset && left.sourceIssues == right.sourceIssues &&
                   left.metadataResult == right.metadataResult && left.importState == right.importState &&
                   left.referenceScanState == right.referenceScanState && left.reasons == right.reasons &&
                   left.hasParsedAssetIdentity == right.hasParsedAssetIdentity;
        }

        [[nodiscard]] bool DiagnosticLess(const ProjectAssetSourceDiagnostic& left,
                                          const ProjectAssetSourceDiagnostic& right) noexcept
        {
            if (left.sourceRoot != right.sourceRoot)
                return left.sourceRoot < right.sourceRoot;
            if (left.sourceIdentity != right.sourceIdentity)
                return left.sourceIdentity.Id() < right.sourceIdentity.Id();
            if (left.physicalPath != right.physicalPath)
                return left.physicalPath < right.physicalPath;
            const i32 asset = CompareAssetId(left.asset, right.asset);
            if (asset != 0)
                return asset < 0;
            if (left.sourceIssues != right.sourceIssues)
                return left.sourceIssues < right.sourceIssues;
            if (left.metadataResult != right.metadataResult)
                return left.metadataResult < right.metadataResult;
            if (left.importState != right.importState)
                return left.importState < right.importState;
            if (left.referenceScanState != right.referenceScanState)
                return left.referenceScanState < right.referenceScanState;
            if (left.reasons != right.reasons)
                return static_cast<u32>(left.reasons) < static_cast<u32>(right.reasons);
            return left.hasParsedAssetIdentity < right.hasParsedAssetIdentity;
        }

        [[nodiscard]] bool SameDiagnostics(const containers::ArraySpan<const ProjectAssetSourceDiagnostic> left,
                                           const containers::ArraySpan<const ProjectAssetSourceDiagnostic> right) noexcept
        {
            if (left.Count() != right.Count())
                return false;
            for (u32 index = 0; index < left.Count(); ++index)
                if (!SameDiagnostic(left[index], right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool AppendUniqueResource(containers::DynamicArray<resources::ResourceReference>& destination,
                                                const resources::ResourceReference value, const u32 limit) noexcept
        {
            if (!value.IsValid() || !value.IsTyped())
                return true;
            const auto duplicate = std::lower_bound(destination.Begin(), destination.End(), value, ResourceLess);
            if (duplicate != destination.End() && *duplicate == value)
                return true;
            if (destination.Size() >= limit)
                return false;
            return destination.Insert(duplicate, value).IsSuccessful();
        }

        void CanonicalizeResources(containers::DynamicArray<resources::ResourceReference>& resources) noexcept
        {
            if (resources.Size() <= 1u)
                return;
            std::sort(resources.Begin(), resources.End(), ResourceLess);
            resources::ResourceReference* const begin = resources.TypedData();
            resources::ResourceReference* const end = begin + resources.Size();
            resources::ResourceReference* output = begin;
            for (resources::ResourceReference* current = begin; current != end; ++current)
                if (output == begin || *(output - 1) != *current)
                    *output++ = *current;
            resources.Resize(static_cast<u32>(output - begin));
        }

        [[nodiscard]] bool DirtyBuildInput(const ProjectAssetChangeReason reasons) noexcept
        {
            constexpr ProjectAssetChangeReason dirty = ProjectAssetChangeReason::New | ProjectAssetChangeReason::ContentChanged |
                                                       ProjectAssetChangeReason::SettingsChanged | ProjectAssetChangeReason::ImporterChanged |
                                                       ProjectAssetChangeReason::ImporterVersionChanged | ProjectAssetChangeReason::Moved |
                                                       ProjectAssetChangeReason::Restored | ProjectAssetChangeReason::Supported |
                                                       ProjectAssetChangeReason::OutputsChanged | ProjectAssetChangeReason::ReferencesChanged;
            return (reasons & dirty) != ProjectAssetChangeReason::None;
        }

        [[nodiscard]] bool AppendChangeEvidence(ProjectAssetAcceptedBaseline& destination, const ProjectAssetState* source,
                                                const bool previousSide, ProjectAssetChangeEvidence& evidence) noexcept
        {
            if (source == nullptr)
                return true;
            evidence.sourceRoot = source->sourceRoot;
            evidence.sourceIdentity = source->sourceIdentity;
            evidence.physicalPath = source->physicalPath;
            evidence.sourceContentFingerprint = source->sourceContentFingerprint;
            evidence.settingsFingerprint = source->settingsFingerprint;
            evidence.importer = source->importer;
            evidence.authoredImporterVersion = source->authoredImporterVersion;
            evidence.availableImporterVersion = source->availableImporterVersion;
            evidence.settingsVersion = source->settingsVersion;
            evidence.sourceType = source->sourceType;
            evidence.sourceIssues = source->sourceIssues;
            evidence.importState = source->importState;
            evidence.referenceScanState = source->referenceScanState;
            evidence.presence = source->presence;
            evidence.available = true;
            evidence.hasSourceContentFingerprint = source->hasSourceContentFingerprint;
            auto& outputStorage = previousSide ? destination.changePreviousOutputs : destination.changeCurrentOutputs;
            auto& referenceStorage = previousSide ? destination.changePreviousReferences : destination.changeCurrentReferences;
            const u32 outputLimit = destination.limits.maximumChangeOutputs;
            const u32 referenceLimit = destination.limits.maximumChangeReferences;
            const u32 outputOffset = outputStorage.Size();
            for (const ProjectAssetOutputState& output : source->outputs)
            {
                if (destination.changePreviousOutputs.Size() + destination.changeCurrentOutputs.Size() >= outputLimit)
                    return false;
                const u32 before = outputStorage.Size();
                outputStorage.PushBack(output);
                if (outputStorage.Size() != before + 1u)
                    return false;
            }
            const u32 referenceOffset = referenceStorage.Size();
            for (const assets::AssetReference& reference : source->references)
            {
                if (destination.changePreviousReferences.Size() + destination.changeCurrentReferences.Size() >= referenceLimit)
                    return false;
                const u32 before = referenceStorage.Size();
                referenceStorage.PushBack(reference);
                if (referenceStorage.Size() != before + 1u)
                    return false;
            }
            evidence.outputs = {outputStorage.TypedData() + outputOffset, outputStorage.Size() - outputOffset};
            evidence.references = {referenceStorage.TypedData() + referenceOffset, referenceStorage.Size() - referenceOffset};
            return true;
        }
    } // namespace

    ProjectAssetAcceptedBaseline::ProjectAssetAcceptedBaseline(const ProjectAssetChangeStateLimits& stateLimits) noexcept
        : limits(stateLimits), assets(memory::pools::Assets::GetInstance()), assetLookup(memory::pools::Assets::GetInstance()),
          outputs(memory::pools::Assets::GetInstance()), references(memory::pools::Assets::GetInstance()),
          backwardReferences(memory::pools::Assets::GetInstance()), changes(memory::pools::Assets::GetInstance()),
          changePreviousOutputs(memory::pools::Assets::GetInstance()), changeCurrentOutputs(memory::pools::Assets::GetInstance()),
          changePreviousReferences(memory::pools::Assets::GetInstance()), changeCurrentReferences(memory::pools::Assets::GetInstance()),
          diagnostics(memory::pools::Assets::GetInstance()), invalidationSeeds(memory::pools::Assets::GetInstance()),
          invalidationOutputs(memory::pools::Assets::GetInstance())
    {
    }

    void ProjectAssetAcceptedBaseline::Reset() noexcept
    {
        generation = 0;
        tombstones = 0;
        assets.Clear();
        assetLookup.Clear();
        outputs.Clear();
        references.Clear();
        backwardReferences.Clear();
        changes.Clear();
        changePreviousOutputs.Clear();
        changeCurrentOutputs.Clear();
        changePreviousReferences.Clear();
        changeCurrentReferences.Clear();
        diagnostics.Clear();
        invalidationSeeds.Clear();
        invalidationOutputs.Clear();
    }

    void ProjectAssetAcceptedBaseline::Swap(ProjectAssetAcceptedBaseline& other) noexcept
    {
        const ProjectAssetChangeStateLimits previousLimits = limits;
        limits = other.limits;
        other.limits = previousLimits;
        const u64 previousGeneration = generation;
        generation = other.generation;
        other.generation = previousGeneration;
        const u32 previousTombstones = tombstones;
        tombstones = other.tombstones;
        other.tombstones = previousTombstones;
        assets.Swap(other.assets);
        assetLookup.Swap(other.assetLookup);
        outputs.Swap(other.outputs);
        references.Swap(other.references);
        backwardReferences.Swap(other.backwardReferences);
        changes.Swap(other.changes);
        changePreviousOutputs.Swap(other.changePreviousOutputs);
        changeCurrentOutputs.Swap(other.changeCurrentOutputs);
        changePreviousReferences.Swap(other.changePreviousReferences);
        changeCurrentReferences.Swap(other.changeCurrentReferences);
        diagnostics.Swap(other.diagnostics);
        invalidationSeeds.Swap(other.invalidationSeeds);
        invalidationOutputs.Swap(other.invalidationOutputs);
    }

    const ProjectAssetState* ProjectAssetAcceptedBaseline::LookupAsset(const assets::AssetId asset) const noexcept
    {
        containers::String key;
        u32 index = 0;
        if (!MakeAssetLookupKey(asset, key) || !assetLookup.Find(key, index) || index >= assets.Size())
            return nullptr;
        return &assets[index];
    }

    containers::ArraySpan<const assets::AssetReference> ProjectAssetAcceptedBaseline::GetMissingOutputReferencers(
        const resources::ResourceReference output) const noexcept
    {
        if (!output.IsValid() || !output.IsTyped() || backwardReferences.Empty())
            return {};
        const assets::AssetReference* const first = backwardReferences.TypedData();
        const assets::AssetReference* const end = first + backwardReferences.Size();
        const assets::AssetReference* const begin = std::lower_bound(
            first, end, output,
            [](const assets::AssetReference& reference, const resources::ResourceReference value) noexcept
            { return CompareReferenceTarget(reference.to, value) < 0; });
        const assets::AssetReference* const after = std::upper_bound(
            begin, end, output,
            [](const resources::ResourceReference value, const assets::AssetReference& reference) noexcept
            { return CompareReferenceTarget(value, reference.to) < 0; });
        return {begin, static_cast<u32>(after - begin)};
    }

    ProjectAssetComparisonResult CompareProjectAssetCandidate(
        const ProjectAssetAcceptedBaseline& previous, const assets::SourceDatabase& candidate,
        const containers::ArraySpan<const ProjectAssetPreparedSourceEvidence> sourceEvidence,
        ProjectAssetAcceptedBaseline& proposed) noexcept
    {
        const auto rows = candidate.GetResources();
        if (!previous.limits.IsValid() || !proposed.limits.IsValid() || sourceEvidence.Count() != rows.Count() ||
            !SameLimits(previous.limits, proposed.limits))
            return ProjectAssetComparisonResult::InvalidArgument;

        ProjectAssetAcceptedBaseline result(previous.limits);
        containers::DynamicArray<u32> candidateAssets(memory::pools::Assets::GetInstance());
        containers::DynamicArray<AssetSlice> slices(memory::pools::Assets::GetInstance());
        containers::DynamicArray<PendingChange> pendingChanges(memory::pools::Assets::GetInstance());
        containers::HashMap<containers::String, u32> blockedAssets(memory::pools::Assets::GetInstance());

        // Build the canonical diagnostic surface and the set of candidate rows
        // that can safely own an AssetId. Invalid/conflicting/unreadable rows are
        // never promoted into accepted asset ownership.
        for (u32 rowIndex = 0; rowIndex < rows.Count(); ++rowIndex)
        {
            const assets::SourceRecord& row = rows[rowIndex];
            const ProjectAssetPreparedSourceEvidence& evidence = sourceEvidence[rowIndex];
            const bool parsedIdentity = row.metadataResult == assets::MetadataResult::Success && row.metadata.id.IsValid();
            const bool conflict = (row.issues & ConflictIssues) != 0u;
            const bool sourceMissing = (row.issues & assets::SourceMissing) != 0u;
            const bool contentUnavailable = !sourceMissing && !evidence.hasSourceContentFingerprint;
            const bool eligible = parsedIdentity && !conflict && !contentUnavailable;

            if (!eligible)
            {
                if (result.diagnostics.Size() >= result.limits.maximumDiagnostics || row.root >= candidate.GetRoots().Count())
                    return ProjectAssetComparisonResult::LimitExceeded;
                ProjectAssetSourceDiagnostic diagnostic;
                diagnostic.sourceRoot = candidate.GetRoots()[row.root].directory;
                diagnostic.sourceIdentity = row.sourceIdentity;
                diagnostic.physicalPath = row.path;
                diagnostic.asset = row.metadata.id;
                diagnostic.sourceIssues = row.issues;
                diagnostic.metadataResult = row.metadataResult;
                diagnostic.importState = row.importState;
                diagnostic.referenceScanState = row.referenceScanState;
                diagnostic.hasParsedAssetIdentity = parsedIdentity;
                if (contentUnavailable)
                    diagnostic.reasons = diagnostic.reasons | ProjectAssetSourceDiagnosticReason::SourceContentUnavailable;

                if (evidence.hasSourceContentFingerprint)
                {
                    u32 matches = 0;
                    for (const ProjectAssetState& old : previous.assets)
                        if (old.hasSourceContentFingerprint && old.sourceContentFingerprint == evidence.sourceContentFingerprint)
                            ++matches;
                    if (matches > 1u)
                        diagnostic.reasons = diagnostic.reasons | ProjectAssetSourceDiagnosticReason::AmbiguousMove;
                }
                const u32 before = result.diagnostics.Size();
                result.diagnostics.PushBack(static_cast<ProjectAssetSourceDiagnostic&&>(diagnostic));
                if (result.diagnostics.Size() != before + 1u)
                    return ProjectAssetComparisonResult::LimitExceeded;
                if (parsedIdentity)
                {
                    containers::String key;
                    if (!MakeAssetLookupKey(row.metadata.id, key))
                        return ProjectAssetComparisonResult::LimitExceeded;
                    u32 ignored = 0;
                    if (!blockedAssets.Find(key, ignored) && !blockedAssets.Insert(static_cast<containers::String&&>(key), rowIndex).IsSuccessful())
                        return ProjectAssetComparisonResult::LimitExceeded;
                }
                continue;
            }

            const u32 before = candidateAssets.Size();
            if (candidateAssets.Size() >= result.limits.maximumAssets)
                return ProjectAssetComparisonResult::LimitExceeded;
            candidateAssets.PushBack(rowIndex);
            if (candidateAssets.Size() != before + 1u)
                return ProjectAssetComparisonResult::LimitExceeded;
        }

        if (candidateAssets.Size() > 1u)
            std::sort(candidateAssets.Begin(), candidateAssets.End(), [&rows](const u32 left, const u32 right) noexcept
                      { return CompareAssetId(rows[left].metadata.id, rows[right].metadata.id) < 0; });
        if (result.diagnostics.Size() > 1u)
            std::sort(result.diagnostics.Begin(), result.diagnostics.End(), DiagnosticLess);

        u32 previousIndex = 0;
        u32 candidateIndex = 0;
        while (previousIndex < previous.assets.Size() || candidateIndex < candidateAssets.Size())
        {
            const ProjectAssetState* old = previousIndex < previous.assets.Size() ? &previous.assets[previousIndex] : nullptr;
            const assets::SourceRecord* row = candidateIndex < candidateAssets.Size() ? &rows[candidateAssets[candidateIndex]] : nullptr;
            i32 ordering = old == nullptr ? 1 : row == nullptr ? -1 : CompareAssetId(old->asset, row->metadata.id);

            AssetSlice slice;
            PendingChange pending;
            if (ordering == 0)
            {
                pending.asset = old->asset;
                pending.previousIndex = static_cast<i32>(previousIndex);
                const bool missing = (row->issues & assets::SourceMissing) != 0u;
                if (missing)
                {
                    if (!AppendPreviousState(result, *old, ProjectAssetPresence::Missing, slice))
                        return ProjectAssetComparisonResult::LimitExceeded;
                    ProjectAssetState& current = result.assets[result.assets.Size() - 1u];
                    current.sourceIssues |= assets::SourceMissing;
                }
                else if (!AppendCandidateState(result, candidate, *row, sourceEvidence[candidateAssets[candidateIndex]],
                                               ProjectAssetPresence::Present, slice))
                    return ProjectAssetComparisonResult::LimitExceeded;
                if (!missing)
                    result.assets[result.assets.Size() - 1u].acceptedGeneration = old->acceptedGeneration;
                pending.currentIndex = static_cast<i32>(result.assets.Size() - 1u);
                ++previousIndex;
                ++candidateIndex;
            }
            else if (ordering < 0)
            {
                containers::String key;
                u32 blocked = 0;
                if (!MakeAssetLookupKey(old->asset, key))
                    return ProjectAssetComparisonResult::LimitExceeded;
                if (blockedAssets.Find(key, blocked))
                {
                    const bool conflicting = blocked < rows.Count() && (rows[blocked].issues & ConflictIssues) != 0u;
                    if (!AppendPreviousState(result, *old, conflicting ? ProjectAssetPresence::Conflicting : old->presence, slice))
                        return ProjectAssetComparisonResult::LimitExceeded;
                    if (conflicting)
                    {
                        ProjectAssetState& current = result.assets[result.assets.Size() - 1u];
                        current.sourceIssues |= rows[blocked].issues & ConflictIssues;
                        pending.asset = old->asset;
                        pending.previousIndex = static_cast<i32>(previousIndex);
                        pending.currentIndex = static_cast<i32>(result.assets.Size() - 1u);
                        pending.reasons = CompareState(*old, current);
                        if (pending.reasons != ProjectAssetChangeReason::None)
                        {
                            if (pendingChanges.Size() >= result.limits.maximumChanges)
                                return ProjectAssetComparisonResult::LimitExceeded;
                            const u32 before = pendingChanges.Size();
                            pendingChanges.PushBack(pending);
                            if (pendingChanges.Size() != before + 1u)
                                return ProjectAssetComparisonResult::LimitExceeded;
                        }
                    }
                    ++previousIndex;
                    slices.PushBack(slice);
                    if (slices.Size() != result.assets.Size())
                        return ProjectAssetComparisonResult::LimitExceeded;
                    continue;
                }
                pending.asset = old->asset;
                pending.previousIndex = static_cast<i32>(previousIndex);
                if (!AppendPreviousState(result, *old, ProjectAssetPresence::Missing, slice))
                    return ProjectAssetComparisonResult::LimitExceeded;
                ProjectAssetState& current = result.assets[result.assets.Size() - 1u];
                current.sourceIssues |= assets::SourceMissing;
                pending.currentIndex = static_cast<i32>(result.assets.Size() - 1u);
                ++previousIndex;
            }
            else
            {
                pending.asset = row->metadata.id;
                const bool missing = (row->issues & assets::SourceMissing) != 0u;
                if (!AppendCandidateState(result, candidate, *row, sourceEvidence[candidateAssets[candidateIndex]],
                                          missing ? ProjectAssetPresence::Missing : ProjectAssetPresence::Present, slice))
                    return ProjectAssetComparisonResult::LimitExceeded;
                pending.currentIndex = static_cast<i32>(result.assets.Size() - 1u);
                ++candidateIndex;
            }

            const u32 sliceBefore = slices.Size();
            slices.PushBack(slice);
            if (slices.Size() != sliceBefore + 1u)
                return ProjectAssetComparisonResult::LimitExceeded;
            const ProjectAssetState* current = pending.currentIndex >= 0 ? &result.assets[static_cast<u32>(pending.currentIndex)] : nullptr;
            if (pending.previousIndex < 0)
            {
                pending.reasons = ProjectAssetChangeReason::New | (current != nullptr && current->presence == ProjectAssetPresence::Missing
                                                                       ? ProjectAssetChangeReason::Missing
                                                                       : ProjectAssetChangeReason::None);
                if (current != nullptr && current->presence == ProjectAssetPresence::Present && !IsSupported(*current))
                    pending.reasons |= ProjectAssetChangeReason::Unsupported;
            }
            else
                pending.reasons = CompareState(previous.assets[static_cast<u32>(pending.previousIndex)], *current);
            if (pending.reasons != ProjectAssetChangeReason::None)
            {
                if (pendingChanges.Size() >= result.limits.maximumChanges)
                    return ProjectAssetComparisonResult::LimitExceeded;
                const u32 before = pendingChanges.Size();
                pendingChanges.PushBack(pending);
                if (pendingChanges.Size() != before + 1u)
                    return ProjectAssetComparisonResult::LimitExceeded;
            }
        }

        BindStateSlices(result, slices);
        result.tombstones = 0;
        for (const ProjectAssetState& state : result.assets)
            if (state.presence == ProjectAssetPresence::Missing)
                ++result.tombstones;
        if (result.tombstones > result.limits.maximumTombstones)
            return ProjectAssetComparisonResult::LimitExceeded;

        const bool diagnosticsChanged = !SameDiagnostics({previous.diagnostics.TypedData(), previous.diagnostics.Size()},
                                                         {result.diagnostics.TypedData(), result.diagnostics.Size()});
        const bool acceptedChanged = !pendingChanges.Empty() || diagnosticsChanged;
        result.generation = acceptedChanged ? previous.generation + 1u : previous.generation;
        for (const PendingChange& pending : pendingChanges)
            if (pending.currentIndex >= 0)
                result.assets[static_cast<u32>(pending.currentIndex)].acceptedGeneration = result.generation;

        for (u32 assetIndex = 0; assetIndex < result.assets.Size(); ++assetIndex)
        {
            containers::String key;
            if (!MakeAssetLookupKey(result.assets[assetIndex].asset, key) || !result.assetLookup.Insert(static_cast<containers::String&&>(key), assetIndex).IsSuccessful())
                return ProjectAssetComparisonResult::LimitExceeded;
            if (result.assets[assetIndex].presence == ProjectAssetPresence::Present)
                for (const assets::AssetReference& reference : result.assets[assetIndex].references)
                    if (!AppendReference(result.backwardReferences, reference, result.limits.maximumReferences))
                        return ProjectAssetComparisonResult::LimitExceeded;
        }
        if (result.backwardReferences.Size() > 1u)
            std::sort(result.backwardReferences.Begin(), result.backwardReferences.End(), ReferenceToLess);

        for (const PendingChange& pending : pendingChanges)
        {
            const ProjectAssetState* old = pending.previousIndex >= 0 ? &previous.assets[static_cast<u32>(pending.previousIndex)] : nullptr;
            const ProjectAssetState* current = pending.currentIndex >= 0 ? &result.assets[static_cast<u32>(pending.currentIndex)] : nullptr;
            ProjectAssetChangeRecord change;
            change.asset = pending.asset;
            change.reasons = pending.reasons;
            if (!AppendChangeEvidence(result, old, true, change.previous) || !AppendChangeEvidence(result, current, false, change.current))
                return ProjectAssetComparisonResult::LimitExceeded;
            const u32 before = result.changes.Size();
            result.changes.PushBack(static_cast<ProjectAssetChangeRecord&&>(change));
            if (result.changes.Size() != before + 1u)
                return ProjectAssetComparisonResult::LimitExceeded;

            if (current != nullptr && current->presence == ProjectAssetPresence::Present && HasTypedInvalidationSeed(*current) &&
                DirtyBuildInput(pending.reasons))
                if (!AppendUniqueResource(result.invalidationSeeds, resources::ResourceReference(current->sourceIdentity, current->sourceType),
                                          result.limits.maximumInvalidationSeeds))
                    return ProjectAssetComparisonResult::LimitExceeded;
            if (old != nullptr)
                for (const ProjectAssetOutputState& output : old->outputs)
                    if (!AppendUniqueResource(result.invalidationOutputs, output.resource, result.limits.maximumInvalidationOutputs))
                        return ProjectAssetComparisonResult::LimitExceeded;
            if (current != nullptr && current->presence == ProjectAssetPresence::Present)
                for (const ProjectAssetOutputState& output : current->outputs)
                    if (!AppendUniqueResource(result.invalidationOutputs, output.resource, result.limits.maximumInvalidationOutputs))
                        return ProjectAssetComparisonResult::LimitExceeded;
        }
        CanonicalizeResources(result.invalidationSeeds);
        CanonicalizeResources(result.invalidationOutputs);

        // AppendChangeEvidence stores borrowed spans into change storage. Bind
        // them again after all appends so later reallocations cannot stale spans.
        u32 previousOutput = 0, currentOutput = 0, previousReference = 0, currentReference = 0;
        for (ProjectAssetChangeRecord& change : result.changes)
        {
            const u32 previousOutputCount = change.previous.outputs.Count();
            const u32 currentOutputCount = change.current.outputs.Count();
            const u32 previousReferenceCount = change.previous.references.Count();
            const u32 currentReferenceCount = change.current.references.Count();
            change.previous.outputs = {result.changePreviousOutputs.TypedData() + previousOutput, previousOutputCount};
            change.current.outputs = {result.changeCurrentOutputs.TypedData() + currentOutput, currentOutputCount};
            change.previous.references = {result.changePreviousReferences.TypedData() + previousReference, previousReferenceCount};
            change.current.references = {result.changeCurrentReferences.TypedData() + currentReference, currentReferenceCount};
            previousOutput += previousOutputCount;
            currentOutput += currentOutputCount;
            previousReference += previousReferenceCount;
            currentReference += currentReferenceCount;
        }

        proposed.Swap(result);
        return ProjectAssetComparisonResult::Success;
    }
} // namespace vanguard::asset_streaming
