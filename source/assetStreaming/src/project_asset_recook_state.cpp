#include <vanguard/asset_streaming/project_asset_recook_state.hpp>

namespace vanguard::asset_streaming
{
    namespace
    {
        [[nodiscard]] bool IsSupported(const ProjectAssetAutomaticOwnedEvidence& evidence) noexcept
        {
            return evidence.available && evidence.presence == ProjectAssetPresence::Present &&
                   evidence.sourceIssues == assets::SourceIssueNone &&
                   evidence.importState == assets::SourceImportState::MetadataSelected &&
                   evidence.importer != assets::InvalidSourceImporterId && evidence.authoredImporterVersion != 0u &&
                   evidence.authoredImporterVersion == evidence.availableImporterVersion;
        }

        [[nodiscard]] bool IsSupported(const ProjectAssetChangeEvidence& evidence) noexcept
        {
            return evidence.available && evidence.presence == ProjectAssetPresence::Present &&
                   evidence.sourceIssues == assets::SourceIssueNone &&
                   evidence.importState == assets::SourceImportState::MetadataSelected &&
                   evidence.importer != assets::InvalidSourceImporterId && evidence.authoredImporterVersion != 0u &&
                   evidence.authoredImporterVersion == evidence.availableImporterVersion;
        }

        [[nodiscard]] i32 FindAsset(const ProjectAssetAutomaticPendingState& state, const assets::AssetId asset) noexcept
        {
            for (u32 index = 0; index < state.assets.Size(); ++index)
                if (state.assets[index].asset == asset)
                    return static_cast<i32>(index);
            return -1;
        }

        [[nodiscard]] bool Contains(const containers::DynamicArray<resources::ResourceReference>& values,
                                    const resources::ResourceReference value) noexcept
        {
            for (const resources::ResourceReference candidate : values)
                if (candidate == value)
                    return true;
            return false;
        }

        [[nodiscard]] bool AddUnique(containers::DynamicArray<resources::ResourceReference>& values,
                                     const resources::ResourceReference value, const u32 maximum) noexcept
        {
            if (!value.IsValid() || !value.IsTyped())
                return false;
            if (Contains(values, value))
                return true;
            if (values.Size() >= maximum)
                return false;
            const u32 before = values.Size();
            values.PushBack(value);
            return values.Size() == before + 1u;
        }

        [[nodiscard]] bool AddOutput(containers::DynamicArray<resources::ResourceReference>& outputs,
                                     const resources::ResourceReference output, const u32 maximum) noexcept
        {
            return AddUnique(outputs, output, maximum);
        }

        [[nodiscard]] bool AddIndexedDependants(assets::DependencyIndex& index, const resources::ResourceReference identity,
                                                containers::DynamicArray<resources::ResourceReference>& outputs,
                                                const u32 maximumOutputs) noexcept
        {
            containers::DynamicArray<resources::ResourceReference> direct(memory::pools::Assets::GetInstance());
            const assets::IndexResult result = index.GetDirectDependants(identity, direct);
            if (result == assets::IndexResult::NotFound)
                return true;
            if (result != assets::IndexResult::Success)
                return false;
            for (const resources::ResourceReference output : direct)
                if (!AddOutput(outputs, output, maximumOutputs))
                    return false;
            return true;
        }

        [[nodiscard]] bool AddInput(assets::DependencyIndex& index, ProjectAssetAutomaticPendingState& state,
                                    const resources::ResourceReference input, const ProjectAssetAutomaticRecookConfig& config) noexcept
        {
            if (!AddUnique(state.inputs, input, config.maximumPendingInputs))
                return false;
            return AddIndexedDependants(index, input, state.outputs, config.maximumPendingOutputs);
        }

        [[nodiscard]] bool AddExplicitOutput(assets::DependencyIndex& index, ProjectAssetAutomaticPendingState& state,
                                             const resources::ResourceReference output,
                                             const ProjectAssetAutomaticRecookConfig& config) noexcept
        {
            if (!AddOutput(state.outputs, output, config.maximumPendingOutputs))
                return false;
            // A removed/missing callback source may no longer expose its old typed
            // source identity. The committed output identity is still valid VADI
            // dependency space, so retain direct consumers from VADI rather than
            // consulting authored SourceDatabase references.
            return AddIndexedDependants(index, output, state.outputs, config.maximumPendingOutputs);
        }

        [[nodiscard]] bool AddCommittedSourceForOutput(assets::DependencyIndex& index, ProjectAssetAutomaticPendingState& state,
                                                       const resources::ResourceReference output,
                                                       const ProjectAssetAutomaticRecookConfig& config) noexcept
        {
            assets::DependencyRecord record;
            const assets::IndexResult result = index.Find(output, record);
            if (result == assets::IndexResult::NotFound)
                return true;
            if (result != assets::IndexResult::Success)
                return false;
            return record.source.IsValid() && record.source.IsTyped() ? AddInput(index, state, record.source, config) : false;
        }

        void RemoveCurrentRequest(ProjectAssetAutomaticPendingState& state, const resources::ResourceReference output) noexcept
        {
            for (u32 index = 0; index < state.currentRequests.Size(); ++index)
                if (state.currentRequests[index].outputReference == output)
                {
                    static_cast<void>(state.currentRequests.RemoveAtReorder(index));
                    return;
                }
        }

        [[nodiscard]] i32 FindForcedOutput(const ProjectAssetAutomaticPendingState& state,
                                           const resources::ResourceReference output) noexcept
        {
            for (u32 index = 0; index < state.forcedOutputs.Size(); ++index)
                if (state.forcedOutputs[index].output == output)
                    return static_cast<i32>(index);
            return -1;
        }

        [[nodiscard]] bool AddForcedOutput(ProjectAssetAutomaticPendingState& state,
                                           const resources::ResourceReference output,
                                           const ProjectAssetAutomaticForceReason reason,
                                           const u32 maximum) noexcept
        {
            if (!output.IsValid() || !output.IsTyped() || reason == ProjectAssetAutomaticForceReason::None)
                return false;
            const i32 found = FindForcedOutput(state, output);
            if (found >= 0)
            {
                ProjectAssetAutomaticForcedOutput& row = state.forcedOutputs[static_cast<u32>(found)];
                row.reasons = row.reasons | reason;
                return true;
            }
            if (state.forcedOutputs.Size() >= maximum)
                return false;
            const u32 before = state.forcedOutputs.Size();
            state.forcedOutputs.PushBack({output, reason});
            return state.forcedOutputs.Size() == before + 1u;
        }

        void PruneForcedOutputsWithoutCurrentRequest(ProjectAssetAutomaticPendingState& state) noexcept
        {
            for (u32 forcedIndex = 0; forcedIndex < state.forcedOutputs.Size();)
            {
                bool current = false;
                for (const assets::OwnedBuildRequest& request : state.currentRequests)
                    if (request.outputReference == state.forcedOutputs[forcedIndex].output)
                    {
                        current = true;
                        break;
                    }
                if (current)
                {
                    ++forcedIndex;
                    continue;
                }
                static_cast<void>(state.forcedOutputs.RemoveAtReorder(forcedIndex));
            }
        }

        [[nodiscard]] bool SetCurrentRequest(ProjectAssetAutomaticPendingState& state, const assets::OwnedBuildRequest& request,
                                             const u32 maximumOutputs) noexcept
        {
            for (assets::OwnedBuildRequest& existing : state.currentRequests)
                if (existing.outputReference == request.outputReference)
                    return existing.CopyFrom(request.View());
            if (state.currentRequests.Size() >= maximumOutputs)
                return false;
            const u32 before = state.currentRequests.Size();
            state.currentRequests.Resize(before + 1u);
            return state.currentRequests.Size() == before + 1u && state.currentRequests[before].CopyFrom(request.View());
        }

        [[nodiscard]] bool CopyAsset(const ProjectAssetAutomaticPendingAsset& source,
                                     ProjectAssetAutomaticPendingAsset& destination) noexcept
        {
            destination.asset = source.asset;
            destination.reasons = source.reasons;
            destination.firstAcceptedGeneration = source.firstAcceptedGeneration;
            destination.latestAcceptedGeneration = source.latestAcceptedGeneration;
            destination.requestResolution = source.requestResolution;
            destination.diagnostic = source.diagnostic;

            ProjectAssetChangeEvidence previous;
            previous.sourceRoot = source.previous.sourceRoot;
            previous.sourceIdentity = source.previous.sourceIdentity;
            previous.physicalPath = source.previous.physicalPath;
            previous.sourceContentFingerprint = source.previous.sourceContentFingerprint;
            previous.settingsFingerprint = source.previous.settingsFingerprint;
            previous.importer = source.previous.importer;
            previous.authoredImporterVersion = source.previous.authoredImporterVersion;
            previous.availableImporterVersion = source.previous.availableImporterVersion;
            previous.settingsVersion = source.previous.settingsVersion;
            previous.sourceType = source.previous.sourceType;
            previous.sourceIssues = source.previous.sourceIssues;
            previous.importState = source.previous.importState;
            previous.referenceScanState = source.previous.referenceScanState;
            previous.presence = source.previous.presence;
            previous.available = source.previous.available;
            previous.hasSourceContentFingerprint = source.previous.hasSourceContentFingerprint;
            containers::DynamicArray<ProjectAssetOutputState> previousOutputs(memory::pools::Assets::GetInstance());
            previousOutputs.Resize(source.previous.outputs.Size());
            if (previousOutputs.Size() != source.previous.outputs.Size())
                return false;
            for (u32 index = 0; index < source.previous.outputs.Size(); ++index)
                previousOutputs[index].resource = source.previous.outputs[index];
            previous.outputs = {previousOutputs.TypedData(), previousOutputs.Size()};
            if (!destination.previous.CopyFrom(previous))
                return false;

            ProjectAssetChangeEvidence current;
            current.sourceRoot = source.current.sourceRoot;
            current.sourceIdentity = source.current.sourceIdentity;
            current.physicalPath = source.current.physicalPath;
            current.sourceContentFingerprint = source.current.sourceContentFingerprint;
            current.settingsFingerprint = source.current.settingsFingerprint;
            current.importer = source.current.importer;
            current.authoredImporterVersion = source.current.authoredImporterVersion;
            current.availableImporterVersion = source.current.availableImporterVersion;
            current.settingsVersion = source.current.settingsVersion;
            current.sourceType = source.current.sourceType;
            current.sourceIssues = source.current.sourceIssues;
            current.importState = source.current.importState;
            current.referenceScanState = source.current.referenceScanState;
            current.presence = source.current.presence;
            current.available = source.current.available;
            current.hasSourceContentFingerprint = source.current.hasSourceContentFingerprint;
            containers::DynamicArray<ProjectAssetOutputState> currentOutputs(memory::pools::Assets::GetInstance());
            currentOutputs.Resize(source.current.outputs.Size());
            if (currentOutputs.Size() != source.current.outputs.Size())
                return false;
            for (u32 index = 0; index < source.current.outputs.Size(); ++index)
                currentOutputs[index].resource = source.current.outputs[index];
            current.outputs = {currentOutputs.TypedData(), currentOutputs.Size()};
            return destination.current.CopyFrom(current);
        }

        [[nodiscard]] bool CloneState(const ProjectAssetAutomaticPendingState& source,
                                      ProjectAssetAutomaticPendingState& destination) noexcept
        {
            destination.Reset();
            destination.lastAcceptedGeneration = source.lastAcceptedGeneration;
            destination.capturedEvidenceBytes = source.capturedEvidenceBytes;
            destination.diagnostics = source.diagnostics;

            destination.assets.Resize(source.assets.Size());
            if (destination.assets.Size() != source.assets.Size())
                return false;
            for (u32 index = 0; index < source.assets.Size(); ++index)
                if (!CopyAsset(source.assets[index], destination.assets[index]))
                    return false;

            destination.inputs.Resize(source.inputs.Size());
            destination.outputs.Resize(source.outputs.Size());
            destination.currentRequests.Resize(source.currentRequests.Size());
            destination.forcedOutputs.Resize(source.forcedOutputs.Size());
            if (destination.inputs.Size() != source.inputs.Size() || destination.outputs.Size() != source.outputs.Size() ||
                destination.currentRequests.Size() != source.currentRequests.Size() ||
                destination.forcedOutputs.Size() != source.forcedOutputs.Size())
                return false;
            for (u32 index = 0; index < source.inputs.Size(); ++index)
                destination.inputs[index] = source.inputs[index];
            for (u32 index = 0; index < source.outputs.Size(); ++index)
                destination.outputs[index] = source.outputs[index];
            for (u32 index = 0; index < source.currentRequests.Size(); ++index)
                if (!destination.currentRequests[index].CopyFrom(source.currentRequests[index].View()))
                    return false;
            for (u32 index = 0; index < source.forcedOutputs.Size(); ++index)
                destination.forcedOutputs[index] = source.forcedOutputs[index];
            return true;
        }

        [[nodiscard]] u64 ComputeBytes(const ProjectAssetAutomaticPendingState& state) noexcept
        {
            u64 bytes = static_cast<u64>(state.inputs.Size()) * sizeof(resources::ResourceReference) +
                        static_cast<u64>(state.outputs.Size()) * sizeof(resources::ResourceReference) +
                        static_cast<u64>(state.forcedOutputs.Size()) * sizeof(ProjectAssetAutomaticForcedOutput);
            for (const ProjectAssetAutomaticPendingAsset& asset : state.assets)
                bytes += sizeof(ProjectAssetAutomaticPendingAsset) + asset.previous.ByteCount() + asset.current.ByteCount();
            for (const assets::OwnedBuildRequest& request : state.currentRequests)
                bytes += sizeof(assets::OwnedBuildRequest) + request.ByteCount();
            return bytes;
        }

        [[nodiscard]] u64 EvidenceBytes(const ProjectAssetChangeEvidence& evidence) noexcept
        {
            return static_cast<u64>(evidence.sourceRoot.AsStringView().Length()) + evidence.physicalPath.AsStringView().Length() +
                   static_cast<u64>(evidence.outputs.Count()) * sizeof(resources::ResourceReference);
        }

        [[nodiscard]] u64 CurrentRequestBytesForOutputs(const ProjectAssetAutomaticPendingState& state,
                                                        const ProjectAssetAutomaticOwnedEvidence& evidence) noexcept
        {
            u64 bytes = 0;
            for (const assets::OwnedBuildRequest& request : state.currentRequests)
                if (Contains(evidence.outputs, request.outputReference))
                    bytes += sizeof(assets::OwnedBuildRequest) + request.ByteCount();
            return bytes;
        }

        [[nodiscard]] bool ApplyChange(ProjectAssetAutomaticPendingState& state, const ProjectAssetChangeRecord& change,
                                       const u64 generation, const assets::SourceDatabase& candidate,
                                       const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
                                       const ProjectAssetAutomaticRecookConfig& config) noexcept
        {
            i32 found = FindAsset(state, change.asset);
            if (change.previous.outputs.Count() > config.maximumPendingOutputs ||
                change.current.outputs.Count() > config.maximumPendingOutputs)
                return false;

            // Preflight owned evidence before any side-copy allocation. For an
            // existing asset, account for the latest current side/request payload
            // that will be replaced so repeated edits can use the full configured
            // budget without permitting an arbitrarily large accepted-side copy.
            const u64 retainedBytes = ComputeBytes(state);
            u64 replacedBytes = 0;
            u64 evidenceBytes = EvidenceBytes(change.current);
            if (found < 0)
                evidenceBytes += sizeof(ProjectAssetAutomaticPendingAsset) + EvidenceBytes(change.previous);
            else
            {
                const ProjectAssetAutomaticPendingAsset& existing = state.assets[static_cast<u32>(found)];
                replacedBytes = existing.current.ByteCount() + CurrentRequestBytesForOutputs(state, existing.current);
            }
            if (retainedBytes < replacedBytes)
                return false;
            const u64 stableBytes = retainedBytes - replacedBytes;
            if (stableBytes > config.maximumCapturedEvidenceBytes ||
                evidenceBytes > config.maximumCapturedEvidenceBytes - stableBytes)
                return false;

            if (found < 0)
            {
                if (state.assets.Size() >= config.maximumPendingAssets)
                    return false;
                const u32 before = state.assets.Size();
                state.assets.Resize(before + 1u);
                if (state.assets.Size() != before + 1u)
                    return false;
                found = static_cast<i32>(before);
                ProjectAssetAutomaticPendingAsset& added = state.assets[before];
                added.asset = change.asset;
                added.reasons = change.reasons;
                added.firstAcceptedGeneration = generation;
                if (!added.previous.CopyFrom(change.previous))
                    return false;
            }

            ProjectAssetAutomaticPendingAsset& pending = state.assets[static_cast<u32>(found)];
            if (found >= 0 && pending.firstAcceptedGeneration != generation)
                pending.reasons |= change.reasons;

            // Requests for the formerly-current side are no longer current, but
            // their input/output identities stay in the unioned invalidation set.
            for (const resources::ResourceReference output : pending.current.outputs)
                RemoveCurrentRequest(state, output);

            if (!pending.current.CopyFrom(change.current))
                return false;
            pending.latestAcceptedGeneration = generation;
            pending.requestResolution = assets::ProjectAssetBuildResult::Success;
            pending.diagnostic = !IsSupported(pending.current);

            if (IsSupported(pending.previous) && pending.previous.sourceIdentity.IsValid() &&
                pending.previous.sourceType != resources::InvalidResourceTypeId)
            {
                if (!AddInput(index, state, resources::ResourceReference(pending.previous.sourceIdentity, pending.previous.sourceType), config))
                    return false;
            }
            for (const ProjectAssetOutputState& output : change.previous.outputs)
                if (!AddExplicitOutput(index, state, output.resource, config) ||
                    !AddCommittedSourceForOutput(index, state, output.resource, config))
                    return false;
            for (const ProjectAssetOutputState& output : change.current.outputs)
                if (!AddExplicitOutput(index, state, output.resource, config))
                    return false;

            if (IsSupported(pending.current))
            {
                for (const resources::ResourceReference output : pending.current.outputs)
                {
                    assets::OwnedBuildRequest request;
                    const assets::ProjectAssetBuildResult resolved = builds.Resolve(candidate, output, request);
                    if (resolved != assets::ProjectAssetBuildResult::Success)
                    {
                        pending.requestResolution = resolved;
                        pending.diagnostic = true;
                        continue;
                    }
                    const u64 requestRetainedBytes = ComputeBytes(state);
                    const u64 requestBytes = sizeof(assets::OwnedBuildRequest) + request.ByteCount();
                    if (requestRetainedBytes > config.maximumCapturedEvidenceBytes ||
                        requestBytes > config.maximumCapturedEvidenceBytes - requestRetainedBytes ||
                        !SetCurrentRequest(state, request, config.maximumPendingOutputs) ||
                        !AddInput(index, state, request.sourceReference, config))
                        return false;
                }
            }
            return true;
        }
    } // namespace

    ProjectAssetAutomaticOwnedEvidence::ProjectAssetAutomaticOwnedEvidence() noexcept
        : outputs(memory::pools::Assets::GetInstance())
    {
    }

    bool ProjectAssetAutomaticOwnedEvidence::CopyFrom(const ProjectAssetChangeEvidence& source) noexcept
    {
        Reset();
        sourceRoot = source.sourceRoot;
        sourceIdentity = source.sourceIdentity;
        physicalPath = source.physicalPath;
        sourceContentFingerprint = source.sourceContentFingerprint;
        settingsFingerprint = source.settingsFingerprint;
        importer = source.importer;
        authoredImporterVersion = source.authoredImporterVersion;
        availableImporterVersion = source.availableImporterVersion;
        settingsVersion = source.settingsVersion;
        sourceType = source.sourceType;
        sourceIssues = source.sourceIssues;
        importState = source.importState;
        referenceScanState = source.referenceScanState;
        presence = source.presence;
        available = source.available;
        hasSourceContentFingerprint = source.hasSourceContentFingerprint;
        outputs.Resize(source.outputs.Count());
        if (outputs.Size() != source.outputs.Count())
        {
            Reset();
            return false;
        }
        for (u32 index = 0; index < outputs.Size(); ++index)
            outputs[index] = source.outputs[index].resource;
        return true;
    }

    void ProjectAssetAutomaticOwnedEvidence::Reset() noexcept
    {
        sourceRoot = {};
        sourceIdentity = {};
        physicalPath = {};
        sourceContentFingerprint = {};
        settingsFingerprint = {};
        importer = assets::InvalidSourceImporterId;
        authoredImporterVersion = 0;
        availableImporterVersion = 0;
        settingsVersion = 0;
        sourceType = resources::InvalidResourceTypeId;
        sourceIssues = assets::SourceIssueNone;
        importState = assets::SourceImportState::NotClassified;
        referenceScanState = assets::SourceReferenceScanState::NotScanned;
        presence = ProjectAssetPresence::Present;
        available = false;
        hasSourceContentFingerprint = false;
        outputs.Clear();
    }

    u64 ProjectAssetAutomaticOwnedEvidence::ByteCount() const noexcept
    {
        return static_cast<u64>(sourceRoot.AsStringView().Length()) + physicalPath.AsStringView().Length() +
               static_cast<u64>(outputs.Size()) * sizeof(resources::ResourceReference);
    }

    ProjectAssetAutomaticPendingState::ProjectAssetAutomaticPendingState() noexcept
        : assets(memory::pools::Assets::GetInstance()), inputs(memory::pools::Assets::GetInstance()),
          outputs(memory::pools::Assets::GetInstance()), currentRequests(memory::pools::Assets::GetInstance()),
          forcedOutputs(memory::pools::Assets::GetInstance())
    {
    }

    void ProjectAssetAutomaticPendingState::Reset() noexcept
    {
        assets.Clear();
        inputs.Clear();
        outputs.Clear();
        currentRequests.Clear();
        forcedOutputs.Clear();
        capturedEvidenceBytes = 0;
        lastAcceptedGeneration = 0;
        diagnostics = 0;
    }

    void ProjectAssetAutomaticPendingState::Swap(ProjectAssetAutomaticPendingState& other) noexcept
    {
        assets.Swap(other.assets);
        inputs.Swap(other.inputs);
        outputs.Swap(other.outputs);
        currentRequests.Swap(other.currentRequests);
        forcedOutputs.Swap(other.forcedOutputs);
        const u64 bytes = capturedEvidenceBytes;
        capturedEvidenceBytes = other.capturedEvidenceBytes;
        other.capturedEvidenceBytes = bytes;
        const u64 generation = lastAcceptedGeneration;
        lastAcceptedGeneration = other.lastAcceptedGeneration;
        other.lastAcceptedGeneration = generation;
        const u32 diagnosticCount = diagnostics;
        diagnostics = other.diagnostics;
        other.diagnostics = diagnosticCount;
    }

    ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticPending(
        const ProjectAssetAutomaticPendingState& existing, const ProjectAssetAcceptedBaseline& proposed,
        const assets::SourceDatabase& candidate, const assets::ProjectAssetBuildSystem& builds,
        assets::DependencyIndex& index, const ProjectAssetAutomaticRecookConfig& config,
        ProjectAssetAutomaticPendingState& staged) noexcept
    {
        staged.Reset();
        if (!config.IsValid() || proposed.generation < existing.lastAcceptedGeneration)
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        if (proposed.generation == existing.lastAcceptedGeneration)
            return ProjectAssetAutomaticPendingPrepareResult::NoChange;
        if (!CloneState(existing, staged))
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;

        for (const ProjectAssetChangeRecord& change : proposed.changes)
            if (!ApplyChange(staged, change, proposed.generation, candidate, builds, index, config))
            {
                staged.Reset();
                return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
            }

        PruneForcedOutputsWithoutCurrentRequest(staged);

        staged.lastAcceptedGeneration = proposed.generation;
        staged.diagnostics = 0;
        for (const ProjectAssetAutomaticPendingAsset& asset : staged.assets)
            if (asset.diagnostic)
                ++staged.diagnostics;
        staged.capturedEvidenceBytes = ComputeBytes(staged);
        if (staged.assets.Size() > config.maximumPendingAssets || staged.inputs.Size() > config.maximumPendingInputs ||
            staged.outputs.Size() > config.maximumPendingOutputs || staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        return ProjectAssetAutomaticPendingPrepareResult::Success;
    }

    ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticRepair(
        const ProjectAssetAutomaticPendingState& existing, const resources::ResourceReference output,
        const u64 acceptedGeneration, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept
    {
        staged.Reset();
        if (!config.IsValid() || !output.IsValid() || !output.IsTyped() ||
            acceptedGeneration < existing.lastAcceptedGeneration || !CloneState(existing, staged))
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;

        assets::OwnedBuildRequest request;
        if (builds.Resolve(sources, output, request) != assets::ProjectAssetBuildResult::Success)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        if (!AddOutput(staged.outputs, output, config.maximumPendingOutputs) ||
            !SetCurrentRequest(staged, request, config.maximumPendingOutputs) ||
            !AddInput(index, staged, request.sourceReference, config) ||
            !AddForcedOutput(staged, output, ProjectAssetAutomaticForceReason::CacheRepair, config.maximumPendingOutputs))
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }

        staged.lastAcceptedGeneration = acceptedGeneration;
        staged.capturedEvidenceBytes = ComputeBytes(staged);
        if (staged.inputs.Size() > config.maximumPendingInputs || staged.outputs.Size() > config.maximumPendingOutputs ||
            staged.currentRequests.Size() > config.maximumPendingOutputs ||
            staged.forcedOutputs.Size() > config.maximumPendingOutputs ||
            staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        return ProjectAssetAutomaticPendingPrepareResult::Success;
    }

    ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticReimport(
        const ProjectAssetAutomaticPendingState& existing, const ProjectAssetState& asset,
        const u64 acceptedGeneration, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept
    {
        staged.Reset();
        if (!config.IsValid() || acceptedGeneration < existing.lastAcceptedGeneration ||
            asset.presence != ProjectAssetPresence::Present || asset.sourceIssues != assets::SourceIssueNone ||
            asset.importState != assets::SourceImportState::MetadataSelected || asset.importer == assets::InvalidSourceImporterId ||
            asset.authoredImporterVersion == 0u || asset.authoredImporterVersion != asset.availableImporterVersion ||
            asset.outputs.Empty() || asset.outputs.Count() > config.maximumPendingOutputs || !CloneState(existing, staged))
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;

        for (const ProjectAssetOutputState& outputState : asset.outputs)
        {
            const resources::ResourceReference output = outputState.resource;
            assets::OwnedBuildRequest request;
            if (!output.IsValid() || !output.IsTyped() ||
                builds.Resolve(sources, output, request) != assets::ProjectAssetBuildResult::Success ||
                !AddOutput(staged.outputs, output, config.maximumPendingOutputs) ||
                !SetCurrentRequest(staged, request, config.maximumPendingOutputs) ||
                !AddInput(index, staged, request.sourceReference, config) ||
                !AddForcedOutput(staged, output, ProjectAssetAutomaticForceReason::ExplicitReimport,
                                 config.maximumPendingOutputs))
            {
                staged.Reset();
                return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
            }
        }

        staged.lastAcceptedGeneration = acceptedGeneration;
        staged.capturedEvidenceBytes = ComputeBytes(staged);
        if (staged.inputs.Size() > config.maximumPendingInputs || staged.outputs.Size() > config.maximumPendingOutputs ||
            staged.currentRequests.Size() > config.maximumPendingOutputs || staged.forcedOutputs.Size() > config.maximumPendingOutputs ||
            staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        return ProjectAssetAutomaticPendingPrepareResult::Success;
    }

    ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticExplicitRecovery(
        const ProjectAssetAutomaticPendingState& retainedActive,
        const ProjectAssetAutomaticPendingState& retainedPending,
        const ProjectAssetAcceptedBaseline& accepted, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept
    {
        staged.Reset();
        if (!config.IsValid())
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;

        const auto preserveExplicit = [&](const ProjectAssetAutomaticPendingState& retained) noexcept -> bool
        {
            for (const ProjectAssetAutomaticForcedOutput& forced : retained.forcedOutputs)
            {
                if (!HasProjectAssetAutomaticForceReason(forced.reasons, ProjectAssetAutomaticForceReason::ExplicitReimport))
                    continue;

                bool acceptedOutput = false;
                for (const ProjectAssetState& asset : accepted.assets)
                {
                    if (asset.presence != ProjectAssetPresence::Present || asset.sourceIssues != assets::SourceIssueNone ||
                        asset.importState != assets::SourceImportState::MetadataSelected ||
                        asset.importer == assets::InvalidSourceImporterId || asset.authoredImporterVersion == 0u ||
                        asset.authoredImporterVersion != asset.availableImporterVersion)
                        continue;
                    for (const ProjectAssetOutputState& output : asset.outputs)
                        if (output.resource == forced.output)
                        {
                            acceptedOutput = true;
                            break;
                        }
                    if (acceptedOutput)
                        break;
                }

                // A superseded output no longer belongs to current accepted truth.
                // Its transient force marker is retired rather than violating the
                // recooker's forcedOutputs subset-of explicitOutputs invariant.
                if (!acceptedOutput)
                    continue;

                assets::OwnedBuildRequest request;
                if (builds.Resolve(sources, forced.output, request) != assets::ProjectAssetBuildResult::Success ||
                    !AddOutput(staged.outputs, forced.output, config.maximumPendingOutputs) ||
                    !SetCurrentRequest(staged, request, config.maximumPendingOutputs) ||
                    !AddInput(index, staged, request.sourceReference, config) ||
                    !AddForcedOutput(staged, forced.output, ProjectAssetAutomaticForceReason::ExplicitReimport,
                                     config.maximumPendingOutputs))
                    return false;
            }
            return true;
        };

        if (!preserveExplicit(retainedActive) || !preserveExplicit(retainedPending))
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }

        staged.lastAcceptedGeneration = accepted.generation;
        staged.capturedEvidenceBytes = ComputeBytes(staged);
        if (staged.inputs.Size() > config.maximumPendingInputs || staged.outputs.Size() > config.maximumPendingOutputs ||
            staged.currentRequests.Size() > config.maximumPendingOutputs ||
            staged.forcedOutputs.Size() > config.maximumPendingOutputs ||
            staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        return ProjectAssetAutomaticPendingPrepareResult::Success;
    }

    ProjectAssetAutomaticPendingPrepareResult PrepareProjectAssetAutomaticRecovery(
        const ProjectAssetAcceptedBaseline& accepted, const assets::SourceDatabase& sources,
        const assets::ProjectAssetBuildSystem& builds, assets::DependencyIndex& index,
        const ProjectAssetAutomaticRecookConfig& config, ProjectAssetAutomaticPendingState& staged) noexcept
    {
        staged.Reset();
        if (!config.IsValid())
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;

        for (const ProjectAssetState& state : accepted.assets)
        {
            // Missing accepted rows are durable tombstones: they preserve identity/output history for deletion,
            // reference and restore semantics, but they are not current source truth to recover or cook. Live
            // Missing transitions still flow through PrepareProjectAssetAutomaticPending and remain diagnostic.
            if (state.presence == ProjectAssetPresence::Missing)
                continue;

            ProjectAssetChangeEvidence current;
            current.sourceRoot = state.sourceRoot;
            current.sourceIdentity = state.sourceIdentity;
            current.physicalPath = state.physicalPath;
            current.sourceContentFingerprint = state.sourceContentFingerprint;
            current.settingsFingerprint = state.settingsFingerprint;
            current.importer = state.importer;
            current.authoredImporterVersion = state.authoredImporterVersion;
            current.availableImporterVersion = state.availableImporterVersion;
            current.settingsVersion = state.settingsVersion;
            current.sourceType = state.sourceType;
            current.sourceIssues = state.sourceIssues;
            current.importState = state.importState;
            current.referenceScanState = state.referenceScanState;
            current.presence = state.presence;
            current.available = true;
            current.hasSourceContentFingerprint = state.hasSourceContentFingerprint;
            current.outputs = IsSupported(current) ? state.outputs : containers::ArraySpan<const ProjectAssetOutputState>{};

            ProjectAssetChangeRecord recovery;
            recovery.asset = state.asset;
            recovery.current = current;
            if (!ApplyChange(staged, recovery, accepted.generation, sources, builds, index, config))
            {
                staged.Reset();
                return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
            }
        }

        staged.lastAcceptedGeneration = accepted.generation;
        staged.diagnostics = 0;
        for (const ProjectAssetAutomaticPendingAsset& asset : staged.assets)
            if (asset.diagnostic)
                ++staged.diagnostics;
        staged.capturedEvidenceBytes = ComputeBytes(staged);
        if (staged.assets.Size() > config.maximumPendingAssets || staged.inputs.Size() > config.maximumPendingInputs ||
            staged.outputs.Size() > config.maximumPendingOutputs || staged.capturedEvidenceBytes > config.maximumCapturedEvidenceBytes)
        {
            staged.Reset();
            return ProjectAssetAutomaticPendingPrepareResult::RecoveryRequired;
        }
        return ProjectAssetAutomaticPendingPrepareResult::Success;
    }

    bool PreserveProjectAssetAutomaticRecoveryInvalidations(
        const ProjectAssetAutomaticPendingState& retainedActive,
        const ProjectAssetAutomaticPendingState& retainedPending,
        const ProjectAssetAutomaticRecookConfig& config,
        ProjectAssetAutomaticPendingState& recovered) noexcept
    {
        if (!config.IsValid())
            return false;
        for (const resources::ResourceReference input : retainedActive.inputs)
            if (!AddUnique(recovered.inputs, input, config.maximumPendingInputs))
                return false;
        for (const resources::ResourceReference input : retainedPending.inputs)
            if (!AddUnique(recovered.inputs, input, config.maximumPendingInputs))
                return false;
        for (const resources::ResourceReference output : retainedActive.outputs)
            if (!AddUnique(recovered.outputs, output, config.maximumPendingOutputs))
                return false;
        for (const resources::ResourceReference output : retainedPending.outputs)
            if (!AddUnique(recovered.outputs, output, config.maximumPendingOutputs))
                return false;
        // Cache-repair requirements are intentionally not copied from transient
        // state. Authoritative recovery reconstructs those from durable truth.
        // Explicit reimport forces, however, are current-session intent and must
        // survive retry/recovery while the process remains alive.
        for (const ProjectAssetAutomaticForcedOutput& forced : retainedActive.forcedOutputs)
            if (HasProjectAssetAutomaticForceReason(forced.reasons, ProjectAssetAutomaticForceReason::ExplicitReimport) &&
                !AddForcedOutput(recovered, forced.output, ProjectAssetAutomaticForceReason::ExplicitReimport,
                                 config.maximumPendingOutputs))
                return false;
        for (const ProjectAssetAutomaticForcedOutput& forced : retainedPending.forcedOutputs)
            if (HasProjectAssetAutomaticForceReason(forced.reasons, ProjectAssetAutomaticForceReason::ExplicitReimport) &&
                !AddForcedOutput(recovered, forced.output, ProjectAssetAutomaticForceReason::ExplicitReimport,
                                 config.maximumPendingOutputs))
                return false;
        PruneForcedOutputsWithoutCurrentRequest(recovered);
        // Repair requirements are reconstructed below by the owner after this
        // recovery snapshot is prepared.
        // Authoritative recovery revalidates each recovered current VADI record
        // against its exact VDDC and reconstructs this bounded subset from durable
        // truth. This also recovers a repair identity that could not be retained
        // when the original QueueRepair preparation overflowed or failed.
        recovered.capturedEvidenceBytes = ComputeBytes(recovered);
        return recovered.inputs.Size() <= config.maximumPendingInputs &&
               recovered.outputs.Size() <= config.maximumPendingOutputs &&
               recovered.forcedOutputs.Size() <= config.maximumPendingOutputs &&
               recovered.capturedEvidenceBytes <= config.maximumCapturedEvidenceBytes;
    }
} // namespace vanguard::asset_streaming
