#include <vanguard/editor/editor_model_import_session.hpp>
#include <vanguard/editor/editor_model_import_frontend.hpp>
#include <vanguard/editor/editor_shell_service.hpp>
#include <vanguard/editor/editor_static_extensions.hpp>
#include <vanguard/memory/memory.hpp>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace editor = vanguard::editor;
    namespace meshTools = vanguard::mesh_tools;
    namespace assetStreaming = vanguard::asset_streaming;
    namespace assets = vanguard::assets;

    inline constexpr editor::EditorExtensionId ModelImportExtensionId{0x6564366d6f646901ull};
    inline constexpr editor::EditorPanelTypeId ModelImportPanelTypeId{0x6564366d6f647001ull};
    // Panel instance IDs are global across Editor extensions and test-created
    // panels, not scoped by panel type. A low value (1) collides with the
    // native-UI integration harness and can prevent its panels from starting.
    inline constexpr editor::EditorPanelInstanceId ModelImportPanelInstanceId = 0x6564366d6f647002ull;
    // P6 proves independent ownership without pre-empting P8's measured batch
    // concurrency policy. Four simultaneous source snapshots keeps the first
    // frontend bounded even at the current 512 MiB per-model inspection limit.
    inline constexpr vanguard::u32 MaximumModelImportSessions = 4u;
    static_assert(MaximumModelImportSessions > 1u, "P6 model import backend must not collapse to singleton session ownership");
    inline constexpr vanguard::u32 InvalidModelImportSessionIndex = ~vanguard::u32{0};

    struct ModelImportSessionEntry final
    {
        editor::EditorModelImportSession session;
        // Immutable presentation of the destination captured by Asset Browser.
        // Switching folders after opening a review must not change its target.
        vanguard::u32 destinationRoot = 0u;
        char destinationFolder[vanguard::resources::MaximumResourcePathBytes + 1u]{};
        bool hasExternalDestination = false;
    };

    struct ModelImportExtensionState final
    {
        editor::EditorProjectAssetAccess assets;
        editor::EditorOwnedPanelAccess panels;
        const editor::ProjectWorkspaceService* workspace = nullptr;
        editor::EditorPanelHandle panel;
        // P6 presents one selected source at a time, but each source owns an
        // independent bounded session. They share only the existing workspace
        // ProjectAssetPipeline through EditorProjectAssetAccess.
        ModelImportSessionEntry* sessions[MaximumModelImportSessions]{};
        vanguard::u32 sessionCount = 0u;
        vanguard::u32 activeSession = InvalidModelImportSessionIndex;
        vanguard::resources::ResourceReference archivedPublicationRetryOutput;
        bool archivedRecookRetryRequired = false;
        char lastAction[256]{};
    };

    // Cross-extension presentation route only. Durable/build ownership stays in
    // the workspace's one ProjectAssetPipeline and per-source sessions live in
    // the bounded collection above; this pointer is not a session/backend owner.
    ModelImportExtensionState* g_modelImportPresentationRoute = nullptr;

    [[nodiscard]] ModelImportSessionEntry* ActiveSession(ModelImportExtensionState& state) noexcept
    {
        return state.activeSession < state.sessionCount ? state.sessions[state.activeSession] : nullptr;
    }

    [[nodiscard]] bool CanReclaimSession(const editor::EditorModelImportSessionStatus& status) noexcept
    {
        if (status.durableCommitted)
            return status.phase == editor::EditorModelImportSessionPhase::Ready ||
                   (status.phase == editor::EditorModelImportSessionPhase::BuildFailed &&
                    !status.cohortAdmissionBuildFailed) ||
                   status.phase == editor::EditorModelImportSessionPhase::Cancelled;
        return status.phase == editor::EditorModelImportSessionPhase::Empty ||
               status.phase == editor::EditorModelImportSessionPhase::Cancelled ||
               status.phase == editor::EditorModelImportSessionPhase::Failed;
    }

    void DestroySession(ModelImportExtensionState& state, const vanguard::u32 index) noexcept
    {
        if (index >= state.sessionCount || state.sessions[index] == nullptr)
            return;
        ModelImportSessionEntry* const entry = state.sessions[index];
        const editor::EditorModelImportSessionStatus status = entry->session.GetStatus();
        if (status.durableCommitted &&
            (status.phase == editor::EditorModelImportSessionPhase::BuildFailed ||
             status.phase == editor::EditorModelImportSessionPhase::Cancelled))
        {
            if (status.publicationRetryOutput.IsValid())
                state.archivedPublicationRetryOutput = status.publicationRetryOutput;
            state.archivedRecookRetryRequired = true;
        }
        entry->~ModelImportSessionEntry();
        vanguard::memory::MemoryBlock block{entry, sizeof(ModelImportSessionEntry), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
        for (vanguard::u32 move = index + 1u; move < state.sessionCount; ++move)
            state.sessions[move - 1u] = state.sessions[move];
        --state.sessionCount;
        state.sessions[state.sessionCount] = nullptr;
        if (state.sessionCount == 0u)
            state.activeSession = InvalidModelImportSessionIndex;
        else if (state.activeSession == index)
            state.activeSession = index < state.sessionCount ? index : state.sessionCount - 1u;
        else if (state.activeSession > index && state.activeSession != InvalidModelImportSessionIndex)
            --state.activeSession;
    }

    void PollSessions(ModelImportExtensionState& state) noexcept
    {
        for (vanguard::u32 index = 0u; index < state.sessionCount; ++index)
        {
            editor::EditorModelImportSession& session = state.sessions[index]->session;
            const bool hadQueuedCommit = session.GetStatus().commitQueued;
            session.Poll(state.assets);
            const editor::EditorModelImportSessionStatus status = session.GetStatus();
            if (index == state.activeSession && status.durableCommitted &&
                std::strncmp(state.lastAction, "Queued import:", sizeof("Queued import:") - 1u) == 0)
                state.lastAction[0] = '\0';
            if (!hadQueuedCommit || index != state.activeSession)
                continue;
            if (status.commitQueued)
                continue;
            // Pending describes the owner-driven async handoff, not a second
            // import. The durable phase owns the authoritative display.
            if (status.durableCommitted && status.pipeline == assetStreaming::ProjectAssetPipelineResult::Pending)
                continue;
            if (status.durableCommitted)
                std::snprintf(state.lastAction, sizeof(state.lastAction), "Reviewed import committed: %s",
                              assetStreaming::ToString(status.pipeline));
            else if (status.phase == editor::EditorModelImportSessionPhase::StalePlan && status.stalePlanSite != 0u)
                std::snprintf(state.lastAction, sizeof(state.lastAction),
                              "Reviewed import rejected at pipeline witness line %u", status.stalePlanSite);
            else
                std::snprintf(state.lastAction, sizeof(state.lastAction), "Queued import: %s",
                              assetStreaming::ToString(status.pipeline));
        }
    }

    [[nodiscard]] ModelImportSessionEntry* CreateSession(ModelImportExtensionState& state) noexcept
    {
        if (state.sessionCount >= MaximumModelImportSessions)
        {
            // Completed/failed presentation records never apply backpressure to
            // later source drops. Preserve every review or unfinished durable
            // session; only reclaim terminal records when the bounded set is full.
            for (vanguard::u32 index = 0u; index < state.sessionCount; ++index)
            {
                if (!CanReclaimSession(state.sessions[index]->session.GetStatus()))
                    continue;
                DestroySession(state, index);
                break;
            }
        }
        if (state.sessionCount >= MaximumModelImportSessions)
            return nullptr;
        const vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Tools, sizeof(ModelImportSessionEntry), alignof(ModelImportSessionEntry));
        if (!block)
            return nullptr;
        auto* const entry = ::new (block.address) ModelImportSessionEntry();
        state.sessions[state.sessionCount] = entry;
        state.activeSession = state.sessionCount++;
        return entry;
    }

    void DestroyAllSessions(ModelImportExtensionState& state) noexcept
    {
        while (state.sessionCount != 0u)
            DestroySession(state, state.sessionCount - 1u);
    }

    [[nodiscard]] const char* PhaseName(const editor::EditorModelImportSessionPhase phase) noexcept
    {
        switch (phase)
        {
        case editor::EditorModelImportSessionPhase::Empty: return "No source selected";
        case editor::EditorModelImportSessionPhase::Inspecting: return "Inspecting";
        case editor::EditorModelImportSessionPhase::ProjectBusy: return "Waiting for project pipeline";
        case editor::EditorModelImportSessionPhase::Review: return "Ready for review";
        case editor::EditorModelImportSessionPhase::ReviewInvalid: return "Review needs changes";
        case editor::EditorModelImportSessionPhase::StalePlan: return "Plan became stale";
        case editor::EditorModelImportSessionPhase::PreparingTransaction: return "Staging reviewed source";
        case editor::EditorModelImportSessionPhase::Imported: return "Imported";
        case editor::EditorModelImportSessionPhase::Compiling: return "Compiling";
        case editor::EditorModelImportSessionPhase::Cancelling: return "Cancelling";
        case editor::EditorModelImportSessionPhase::Ready: return "Ready";
        case editor::EditorModelImportSessionPhase::BuildFailed: return "Build failed";
        case editor::EditorModelImportSessionPhase::ReconciliationPending: return "Committed; reconciliation pending";
        case editor::EditorModelImportSessionPhase::Cancelled: return "Cancelled";
        case editor::EditorModelImportSessionPhase::Failed: return "Failed";
        }
        return "Unknown";
    }

    void DrawModelImportBuildProgress(const editor::EditorModelImportSessionStatus& status,
                                      const vanguard::u32 plannedOutputCount) noexcept
    {
        const vanguard::u32 outputCount = status.outputCount != 0u ? status.outputCount : plannedOutputCount;
        if (status.phase == editor::EditorModelImportSessionPhase::ReconciliationPending)
        {
            ImGui::TextUnformatted("Step 2 / 4: Reconciling committed source with the project catalog");
            ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Authoritative source reconciliation");
            ImGui::TextDisabled("Target cohort: %u outputs. This step has no countable work units, so no fake percentage is shown.", outputCount);
            return;
        }

        if (status.phase == editor::EditorModelImportSessionPhase::Ready)
        {
            ImGui::TextUnformatted("Step 4 / 4: Publishing outputs");
            ImGui::ProgressBar(1.0f, ImVec2(-1.0f, 0.0f), "100% - complete");
            return;
        }

        if (status.phase != editor::EditorModelImportSessionPhase::Imported &&
            status.phase != editor::EditorModelImportSessionPhase::Compiling &&
            status.phase != editor::EditorModelImportSessionPhase::Cancelling)
            return;

        if (status.automaticState == assetStreaming::ProjectAssetAutomaticRecookState::Planning)
        {
            ImGui::TextUnformatted("Step 3 / 4: Planning dependency graph");
            ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Resolving build dependencies");
            ImGui::TextDisabled("The operation count becomes exact once graph planning finishes.");
            return;
        }

        if (status.buildOperations != 0u && status.completedBuildOperations < status.buildOperations)
        {
            const vanguard::u32 completed = std::min(status.completedBuildOperations, status.buildOperations);
            const vanguard::u32 percent = static_cast<vanguard::u32>(
                (static_cast<vanguard::u64>(completed) * 100u) / status.buildOperations);
            char label[96]{};
            std::snprintf(label, sizeof(label), "%u / %u build operations (%u%%)",
                          completed, status.buildOperations, percent);
            ImGui::TextUnformatted("Step 3 / 4: Compiling asset graph");
            ImGui::ProgressBar(static_cast<float>(completed) / static_cast<float>(status.buildOperations),
                               ImVec2(-1.0f, 0.0f), label);
            ImGui::TextDisabled("Includes generated dependencies such as embedded textures and materials, not only the %u final outputs.", outputCount);
            return;
        }

        const bool buildGraphComplete = status.buildOperations != 0u &&
                                        status.completedBuildOperations >= status.buildOperations;
        const bool exactOutputsBuilt = outputCount != 0u && status.builtOutputs >= outputCount;
        if (buildGraphComplete || exactOutputsBuilt || status.readyOutputs != 0u)
        {
            ImGui::TextUnformatted("Step 4 / 4: Publishing compiled outputs");
            if (outputCount != 0u)
            {
                const vanguard::u32 ready = std::min(status.readyOutputs, outputCount);
                if (ready != 0u)
                {
                    const vanguard::u32 percent = static_cast<vanguard::u32>(
                        (static_cast<vanguard::u64>(ready) * 100u) / outputCount);
                    char label[96]{};
                    std::snprintf(label, sizeof(label), "%u / %u outputs visible (%u%%)", ready, outputCount, percent);
                    ImGui::ProgressBar(static_cast<float>(ready) / static_cast<float>(outputCount), ImVec2(-1.0f, 0.0f), label);
                }
                else
                    ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Publishing atomic output cohort");
                ImGui::TextDisabled("Built: %u / %u outputs; visible: %u / %u outputs",
                                    std::min(status.builtOutputs, outputCount), outputCount, ready, outputCount);
            }
            else
                ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Publishing compiled outputs");
            return;
        }

        ImGui::TextUnformatted("Step 3 / 4: Waiting for build admission");
        ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Waiting for build graph");
        ImGui::TextDisabled("No compiler operation has been admitted yet; no percentage is available to report truthfully.");
    }

    [[nodiscard]] const char* ReconcileStateName(const meshTools::ModelImportReconcileState state) noexcept
    {
        switch (state)
        {
        case meshTools::ModelImportReconcileState::Preserved: return "Preserved";
        case meshTools::ModelImportReconcileState::Added: return "Added";
        case meshTools::ModelImportReconcileState::Removed: return "Removed";
        case meshTools::ModelImportReconcileState::Ambiguous: return "Ambiguous";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* ElementKindName(const meshTools::ModelImportElementKind kind) noexcept
    {
        switch (kind)
        {
        case meshTools::ModelImportElementKind::Geometry: return "Mesh";
        case meshTools::ModelImportElementKind::Material: return "Material";
        case meshTools::ModelImportElementKind::EmbeddedTexture: return "Embedded texture";
        case meshTools::ModelImportElementKind::Composition: return "Composition";
        case meshTools::ModelImportElementKind::ExternalTextureDependency: return "External texture";
        }
        return "Unknown";
    }

    [[nodiscard]] const char* BlockerKindName(const meshTools::ModelImportPlanBlockerKind kind) noexcept
    {
        switch (kind)
        {
        case meshTools::ModelImportPlanBlockerKind::ExternalReference: return "External reference";
        case meshTools::ModelImportPlanBlockerKind::ReferenceStateUnavailable: return "Reference state unavailable";
        case meshTools::ModelImportPlanBlockerKind::StableSlotReferenceUnavailable: return "Stable slot reference unavailable";
        case meshTools::ModelImportPlanBlockerKind::MissingDependency: return "Missing dependency";
        }
        return "Unknown";
    }

    void SetAction(ModelImportExtensionState& state, const char* const text) noexcept
    {
        std::snprintf(state.lastAction, sizeof(state.lastAction), "%s", text != nullptr ? text : "");
    }

    void DrawPlanReview(ModelImportExtensionState& state, editor::EditorModelImportSession& session,
                        const bool planValid) noexcept
    {
        const meshTools::ModelImportReport& inspection = session.GetInspectionReport();
        ImGui::Text("Inspected: %u nodes, %u meshes, %u materials, %u texture refs",
                    inspection.nodeCount, inspection.geometryDefinitionCount,
                    inspection.materialCount, inspection.textureReferenceCount);
        ImGui::TextDisabled("%llu vertices, %llu indices, %u embedded textures, %u dependencies",
                            static_cast<unsigned long long>(inspection.vertexCount),
                            static_cast<unsigned long long>(inspection.indexCount),
                            inspection.embeddedTextureCount, inspection.dependencyCount);
        if (!inspection.diagnostic.Empty())
            ImGui::TextWrapped("Inspection: %s", inspection.diagnostic.AsChar());
        ImGui::Separator();

        const editor::EditorModelImportSessionStatus queuedStatus = session.GetStatus();
        if (queuedStatus.commitQueued)
        {
            if (queuedStatus.commitAttempts == 0u)
                ImGui::TextWrapped("Preparing reviewed import: verifying the source, writing the durable project files, "
                                   "and admitting the model's output cohort. Large source transactions currently run "
                                   "on the Editor thread and may briefly block interaction. Do not close the Editor.");
            else
                ImGui::TextWrapped("Import requested. The project is reconciling another asset transaction. "
                                   "The exact reviewed source and settings are retained; automatic retry is pending.");
            ImGui::TextDisabled("Commit attempts: %u. %s", queuedStatus.commitAttempts,
                                queuedStatus.commitAttempts == 0u ? "Commit has not started yet."
                                                                 : "No project model files committed yet.");
            if (ImGui::Button("Cancel pending commit") && session.CancelQueuedCommit())
                SetAction(state, "Pending commit cancelled; reviewed source and settings preserved");
            return;
        }

        meshTools::ModelImportSettings& settings = session.GetCandidateSettings();
        bool changed = false;

        changed = ImGui::DragFloat("Uniform scale", &settings.mesh.uniformScale, 0.01f, 0.0001f, 10000.0f, "%.4f") || changed;
        int sourceUpAxis = static_cast<int>(settings.mesh.sourceUpAxis);
        const char* upAxes[] = {"Y up", "Z up"};
        if (ImGui::Combo("Source up axis", &sourceUpAxis, upAxes, 2))
        {
            settings.mesh.sourceUpAxis = static_cast<meshTools::SourceUpAxis>(sourceUpAxis);
            changed = true;
        }
        changed = ImGui::Checkbox("Generate normals", &settings.mesh.generateNormals) || changed;
        ImGui::SameLine();
        changed = ImGui::Checkbox("Generate tangents", &settings.mesh.generateTangents) || changed;
        changed = ImGui::Checkbox("Flip UVs", &settings.mesh.flipUVs) || changed;
        ImGui::SameLine();
        changed = ImGui::Checkbox("Flip winding", &settings.mesh.flipWinding) || changed;
        changed = ImGui::Checkbox("Strict dependencies", &settings.mesh.strictDependencies) || changed;
        changed = ImGui::DragFloat("Smoothing angle", &settings.mesh.smoothingAngleDegrees, 0.5f, 0.0f, 180.0f, "%.1f deg") || changed;

        int materialPolicy = static_cast<int>(settings.materialPolicy);
        const char* materialPolicies[] = {"Import model-owned materials", "Use default material"};
        if (ImGui::Combo("Materials", &materialPolicy, materialPolicies, 2))
        {
            settings.materialPolicy = static_cast<meshTools::ModelImportMaterialPolicy>(materialPolicy);
            changed = true;
        }
        int embeddedPolicy = static_cast<int>(settings.embeddedTexturePolicy);
        const char* embeddedPolicies[] = {"Import embedded textures", "Ignore embedded textures"};
        if (ImGui::Combo("Embedded textures", &embeddedPolicy, embeddedPolicies, 2))
        {
            settings.embeddedTexturePolicy = static_cast<meshTools::ModelImportEmbeddedTexturePolicy>(embeddedPolicy);
            changed = true;
        }
        changed = ImGui::Checkbox("Generate composition prefab", &settings.includeComposition) || changed;
        if (changed && !session.Replan(state.assets))
        {
            SetAction(state, "Candidate settings could not be replanned against current project state");
            return;
        }

        if (state.workspace != nullptr)
        {
            if (ImGui::Button("Adopt project image dependencies"))
            {
                const vanguard::u32 adopted = session.AdoptResolvableProjectImages(state.workspace->GetSources());
                if (adopted != 0u && !session.Replan(state.assets))
                {
                    SetAction(state, "Project image adoption became stale while replanning");
                    return;
                }
                std::snprintf(state.lastAction, sizeof(state.lastAction), "Prepared %u project image adoption(s)", adopted);
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Ignore missing textures"))
        {
            const vanguard::u32 ignored = session.IgnoreMissingTextures();
            if (ignored != 0u && !session.Replan(state.assets))
            {
                SetAction(state, "Missing-texture policy became stale while replanning");
                return;
            }
            std::snprintf(state.lastAction, sizeof(state.lastAction), "Marked %u missing texture(s) ignored", ignored);
        }

        const meshTools::ModelImportPlanReport& report = session.GetPlanReport();
        ImGui::Separator();
        ImGui::Text("Reconciliation: %u preserved, %u added, %u removed, %u ambiguous, %u blocker(s)",
                    report.preserved, report.added, report.removed, report.ambiguous, report.blockers);
        if (!report.diagnostic.Empty())
            ImGui::TextWrapped("%s", report.diagnostic.AsChar());

        const auto reconciliation = session.GetPlan().GetReconciliation();
        if (!reconciliation.Empty() && ImGui::CollapsingHeader("Reconciliation changes", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginChild("model-import-reconciliation", ImVec2(0.0f, 160.0f), true))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(reconciliation.Size()));
                while (clipper.Step())
                {
                    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    {
                        const meshTools::ModelImportReconciliationRecord& record = reconciliation[static_cast<vanguard::u32>(index)];
                        const char* const name = !record.currentSource.sourceName.Empty()
                                                     ? record.currentSource.sourceName.AsChar()
                                                     : !record.previousSource.sourceName.Empty()
                                                           ? record.previousSource.sourceName.AsChar()
                                                           : "unnamed";
                        ImGui::Text("%s | %s | %s%s", ReconcileStateName(record.state), ElementKindName(record.kind), name,
                                    record.blocking ? " | BLOCKING" : "");
                    }
                }
            }
            ImGui::EndChild();
        }

        const auto blockers = session.GetPlan().GetBlockers();
        if (!blockers.Empty() && ImGui::CollapsingHeader("Commit blockers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginChild("model-import-blockers", ImVec2(0.0f, 130.0f), true))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(blockers.Size()));
                while (clipper.Step())
                {
                    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    {
                        const meshTools::ModelImportPlanBlocker& blocker = blockers[static_cast<vanguard::u32>(index)];
                        ImGui::Text("%s | %s", BlockerKindName(blocker.kind), ElementKindName(blocker.elementKind));
                        if (blocker.referencerPath.IsFilePath())
                            ImGui::TextDisabled("  referenced by %s", blocker.referencerPath.AsChar());
                    }
                }
            }
            ImGui::EndChild();
        }

        const auto textures = session.GetPlan().GetTextureResolutions();
        if (!textures.Empty() && ImGui::CollapsingHeader("Texture dependencies", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginChild("model-import-textures", ImVec2(0.0f, 180.0f), true))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(textures.Size()));
                while (clipper.Step())
                {
                    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                    {
                        const meshTools::ModelImportPlannedTextureResolution& resolution = textures[static_cast<vanguard::u32>(index)];
                        const char* decision = "Unresolved";
                        switch (resolution.resolution)
                        {
                        case meshTools::ModelImportTextureResolutionKind::ReuseManagedResource: decision = "Managed"; break;
                        case meshTools::ModelImportTextureResolutionKind::AdoptProjectSource: decision = "Adopt"; break;
                        case meshTools::ModelImportTextureResolutionKind::IgnoreMissing: decision = "Ignored"; break;
                        case meshTools::ModelImportTextureResolutionKind::Unresolved: break;
                        }
                        ImGui::Text("%s  [%s]%s", resolution.authoredPath.AsChar(), decision,
                                    resolution.blocking ? "  BLOCKING" : "");
                    }
                }
            }
            ImGui::EndChild();
        }

        const bool canCommit = planValid && !session.GetPlan().HasBlockingConflicts();
        if (!canCommit)
            ImGui::BeginDisabled();
        const editor::EditorModelImportSessionStatus status = session.GetStatus();
        if (ImGui::Button(status.reimport ? "Apply reviewed reimport" : "Import reviewed model"))
        {
            if (session.QueueReviewedCommit())
                SetAction(state, "Reviewed import scheduled; source validation and durable commit will start shortly");
            else
                SetAction(state, "Reviewed import could not be scheduled; current review is no longer eligible");
        }
        if (!canCommit)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard review"))
        {
            DestroySession(state, state.activeSession);
            SetAction(state, "Model review discarded without project mutation");
            return;
        }
        if (!canCommit)
            ImGui::TextDisabled("Resolve all blocking reconciliation/dependency issues before import.");
    }

    void* CreateModelImportPanel(const editor::EditorPanelInstanceId, void* const factoryData) noexcept
    {
        return factoryData;
    }

    void DestroyModelImportPanel(void*, void*) noexcept
    {
    }

    void ModelImportPanelVisibilityChanged(void* const panel, const bool open, void* const factoryData) noexcept
    {
        if (open)
            return;
        auto* const state = static_cast<ModelImportExtensionState*>(panel != nullptr ? panel : factoryData);
        if (state == nullptr)
            return;
        for (vanguard::u32 index = 0u; index < state->sessionCount; ++index)
        {
            editor::EditorModelImportSession& session = state->sessions[index]->session;
            const editor::EditorModelImportSessionStatus status = session.GetStatus();
            if (status.phase == editor::EditorModelImportSessionPhase::Inspecting)
                static_cast<void>(session.Cancel(state->assets));
            // A hidden panel cannot drive presentation-owned PollSessions.
            // Stop non-durable queued retries on close instead of leaving a
            // hidden request that could publish when the panel is reopened.
            if (status.commitQueued)
                static_cast<void>(session.CancelQueuedCommit());
        }
    }

    void DrawModelImportPanel(editor::EditorPanelContext&, void* const panel, void* const factoryData) noexcept
    {
        auto* const state = static_cast<ModelImportExtensionState*>(panel != nullptr ? panel : factoryData);
        if (state == nullptr)
            return;
        PollSessions(*state);

        if (state->archivedPublicationRetryOutput.IsValid() || state->archivedRecookRetryRequired)
        {
            ImGui::TextWrapped("A previous durable model import failed after its review session was reclaimed for queue capacity. Project-pipeline recovery is still available.");
            if (ImGui::Button("Retry retained model build"))
            {
                assetStreaming::ProjectAssetPipelineFailure failure;
                const assetStreaming::ProjectAssetPipelineResult result = state->archivedPublicationRetryOutput.IsValid()
                    ? state->assets.RetryAutomaticPublication(state->archivedPublicationRetryOutput, &failure)
                    : state->assets.RetryAutomaticRecook(&failure);
                std::snprintf(state->lastAction, sizeof(state->lastAction), "Retained retry: %s", assetStreaming::ToString(result));
                if (result == assetStreaming::ProjectAssetPipelineResult::Success ||
                    result == assetStreaming::ProjectAssetPipelineResult::Pending)
                {
                    state->archivedPublicationRetryOutput = {};
                    state->archivedRecookRetryRequired = false;
                }
            }
            ImGui::Separator();
        }

        if (state->sessionCount == 0u)
        {
            ImGui::Separator();
            ImGui::TextWrapped("Use Asset Browser > Import... to select a .gltf or .glb file. "
                               "The destination is the selected Asset Browser folder; inspection and review will open here.");
            if (state->lastAction[0] != '\0')
                ImGui::TextDisabled("%s", state->lastAction);
            return;
        }

        ModelImportSessionEntry* active = ActiveSession(*state);
        if (active == nullptr)
        {
            state->activeSession = 0u;
            active = ActiveSession(*state);
        }
        if (active == nullptr)
            return;

        const char* activeLabel = active->session.GetSourcePath().IsFilePath()
                                      ? active->session.GetSourcePath().AsChar() : "Model import session";
        if (ImGui::BeginCombo("Source session", activeLabel))
        {
            for (vanguard::u32 index = 0u; index < state->sessionCount; ++index)
            {
                ModelImportSessionEntry* const entry = state->sessions[index];
                const char* label = entry->session.GetSourcePath().IsFilePath()
                                        ? entry->session.GetSourcePath().AsChar() : "Model import session";
                ImGui::PushID(entry);
                if (ImGui::Selectable(label, index == state->activeSession))
                    state->activeSession = index;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        active = ActiveSession(*state);
        if (active == nullptr)
            return;
        editor::EditorModelImportSession& session = active->session;
        const editor::EditorModelImportSessionStatus status = session.GetStatus();
        ImGui::SameLine();
        const bool closeSession = (!status.durableCommitted && !status.commitQueued &&
                                   status.phase != editor::EditorModelImportSessionPhase::PreparingTransaction) ||
                                  status.phase == editor::EditorModelImportSessionPhase::Ready;
        if (!closeSession)
            ImGui::BeginDisabled();
        if (ImGui::Button("Close session"))
        {
            DestroySession(*state, state->activeSession);
            SetAction(*state, "Model import session closed");
            if (!closeSession)
                ImGui::EndDisabled();
            return;
        }
        if (!closeSession)
            ImGui::EndDisabled();

        if (status.reimport)
            ImGui::TextDisabled("Reimport preserves the current managed source identity and path.");
        else if (active->hasExternalDestination)
        {
            editor::EditorProjectAssetRootPresentation destination;
            if (state->assets.GetSourceRootPresentation(active->destinationRoot, destination))
                ImGui::TextWrapped("Destination (selected in Asset Browser): %s / %s",
                                  destination.displayName, active->destinationFolder);
        }
        ImGui::Separator();
        ImGui::Text("State: %s", status.commitQueued ? "Commit queued" : PhaseName(status.phase));
        if (status.phase == editor::EditorModelImportSessionPhase::Inspecting)
        {
            const vanguard::u32 percent = std::min(status.inspectionProgressPermille, 1000u) / 10u;
            char label[48]{};
            std::snprintf(label, sizeof(label), "Inspecting source (%u%%)", percent);
            ImGui::ProgressBar(static_cast<float>(status.inspectionProgressPermille) / 1000.0f, ImVec2(-1.0f, 0.0f), label);
            if (ImGui::Button("Cancel inspection"))
                static_cast<void>(session.Cancel(state->assets));
        }
        else if (status.phase == editor::EditorModelImportSessionPhase::PreparingTransaction)
        {
            const char* const currentStage =
                status.transactionPreparePhase == assetStreaming::ProjectAssetDeferredPlannedImportPreparePhase::Writing
                    ? "Writing transaction-owned source staging"
                    : status.transactionPreparePhase == assetStreaming::ProjectAssetDeferredPlannedImportPreparePhase::Verifying
                          ? "Verifying staged source bytes"
                          : "Preparing durable project transaction";
            ImGui::TextUnformatted("Step 1 / 4: Staging reviewed source transaction");
            ImGui::TextWrapped("%s. The original reviewed source and settings are unchanged.", currentStage);
            if (status.transactionTotalBytes != 0u)
            {
                const double fraction = static_cast<double>(status.transactionPreparedBytes) /
                                        static_cast<double>(status.transactionTotalBytes);
                const vanguard::u32 percent = static_cast<vanguard::u32>(std::min(100.0, fraction * 100.0));
                char label[80]{};
                std::snprintf(label, sizeof(label), "Transaction staging (%u%%)", percent);
                ImGui::ProgressBar(static_cast<float>(std::min(1.0, fraction)), ImVec2(-1.0f, 0.0f), label);
                ImGui::TextDisabled("Transaction staging: %llu / %llu bytes processed",
                                    static_cast<unsigned long long>(status.transactionPreparedBytes),
                                    static_cast<unsigned long long>(status.transactionTotalBytes));
            }
            else
                ImGui::ProgressBar(-1.0f, ImVec2(-1.0f, 0.0f), "Preparing staging");
            ImGui::TextDisabled("This counter covers transaction staging only. Final durability, source reconciliation and output compilation follow.");
        }
        if (status.phase != editor::EditorModelImportSessionPhase::Empty && session.GetSourcePath().IsFilePath())
            ImGui::TextWrapped("Source: %s", session.GetSourcePath().AsChar());

        if (status.phase == editor::EditorModelImportSessionPhase::Review ||
            status.phase == editor::EditorModelImportSessionPhase::ReviewInvalid)
            DrawPlanReview(*state, session, status.phase == editor::EditorModelImportSessionPhase::Review);
        else if (status.phase == editor::EditorModelImportSessionPhase::ProjectBusy)
        {
            ImGui::TextWrapped("Project asset work is currently busy. The inspected source and candidate settings are still transient and unchanged.");
            if (ImGui::Button("Retry planning"))
            {
                if (!session.Replan(state->assets) &&
                    session.GetStatus().phase == editor::EditorModelImportSessionPhase::ProjectBusy)
                    SetAction(*state, "Project pipeline is still busy");
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard pending review"))
            {
                DestroySession(*state, state->activeSession);
                SetAction(*state, "Pending model review discarded without project mutation");
                return;
            }
        }
        else if (status.durableCommitted)
        {
            if (status.cohortAdmissionBuildFailed)
            {
                ImGui::TextWrapped("Cohort build request failed after the model source was committed: %s. Retry resumes delivery for this exact reviewed import.",
                                   assets::ToString(status.cohortAdmissionBuild));
                if (status.cohortAdmissionBuild == assets::ProjectAssetBuildResult::BuildRequestCreationFailed)
                    ImGui::TextWrapped("A declared model output could not be translated into a valid owned build request. "
                                       "The GLB and its .vmeta are already durable; check the specific output/compiler diagnostic "
                                       "before retrying this same reviewed import. No output is silently omitted.");
            }
            else
            {
                if (status.phase == editor::EditorModelImportSessionPhase::ReconciliationPending)
                {
                    if (status.pipeline != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending &&
                        status.pipeline != assetStreaming::ProjectAssetPipelineResult::Pending &&
                        status.pipeline != assetStreaming::ProjectAssetPipelineResult::Success)
                        ImGui::TextWrapped("Source and metadata are durable, but automatic cohort continuation is paused: %s. "
                                           "Use retry after project coordination settles; no source commit will repeat.",
                                           assetStreaming::ToString(status.pipeline));
                    else
                        ImGui::TextWrapped("Source and import metadata are durable. The project is reconciling the exact "
                                           "accepted generation before model output admission. This is not a build failure.");
                    DrawModelImportBuildProgress(status, session.GetPlan().GetImportDescription().outputs.Size());
                }
                else
                {
                    ImGui::Text("Outputs: %u built / %u total; %u visible; %u failed; %u retry-required",
                                status.builtOutputs, status.outputCount, status.readyOutputs,
                                status.failedOutputs, status.retryOutputs);
                    DrawModelImportBuildProgress(status, session.GetPlan().GetImportDescription().outputs.Size());
                    ImGui::TextDisabled("Percentages are for the current measurable phase, not an ETA or guessed share of total wall-clock time.");
                }
                if (status.phase == editor::EditorModelImportSessionPhase::BuildFailed)
                {
                    ImGui::TextWrapped("Build failed after cohort admission; the output status counts above reflect the admitted build/publication failure.");
                    const assetStreaming::ProjectAssetAutomaticRecookStatus automatic = state->assets.GetAutomaticRecookStatus();
                    if (automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                        automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                    {
                        ImGui::TextWrapped("Automatic recook: %s; failed graph roots: %u. This can fail before individual output statuses change.",
                                           vanguard::assets::ToString(automatic.lastRecookFailure), automatic.failedRootCount);
                        if (automatic.firstFailedRoot.rootIndex != ~vanguard::u32{0})
                        {
                            ImGui::TextWrapped("First failed graph root %u: %s; compiler result: %s",
                                               automatic.firstFailedRoot.rootIndex,
                                               vanguard::assets::ToString(automatic.firstFailedRoot.graphFailure),
                                               vanguard::assets::ToString(automatic.firstFailedRoot.buildResult));
                            if (automatic.firstFailedRoot.message[0] != '\0')
                                ImGui::TextWrapped("Compiler diagnostic: %s", automatic.firstFailedRoot.message);
                        }
                    }
                }
            }
            if ((status.phase == editor::EditorModelImportSessionPhase::Imported ||
                 status.phase == editor::EditorModelImportSessionPhase::Compiling) &&
                ImGui::Button("Cancel build"))
            {
                assetStreaming::ProjectAssetPipelineFailure failure;
                const auto result = session.Cancel(state->assets, &failure);
                std::snprintf(state->lastAction, sizeof(state->lastAction), "Cancel: %s", assetStreaming::ToString(result));
            }
            if ((status.phase == editor::EditorModelImportSessionPhase::BuildFailed ||
                 status.phase == editor::EditorModelImportSessionPhase::Cancelled ||
                 status.phase == editor::EditorModelImportSessionPhase::ReconciliationPending))
            {
                const char* const retryLabel = status.cohortAdmissionBuildFailed
                                                   ? "Retry cohort admission"
                                                   : "Retry build / publication";
                if (ImGui::Button(retryLabel))
                {
                    assetStreaming::ProjectAssetPipelineFailure failure;
                    const auto result = session.Retry(state->assets, &failure);
                    if (failure.result == assetStreaming::ProjectAssetPipelineResult::BuildRequestFailed)
                        std::snprintf(state->lastAction, sizeof(state->lastAction),
                                      "Cohort build request still failed: %s",
                                      assets::ToString(failure.build));
                    else
                        std::snprintf(state->lastAction, sizeof(state->lastAction), "Retry: %s", assetStreaming::ToString(result));
                }
            }
        }

        if (status.phase == editor::EditorModelImportSessionPhase::StalePlan)
        {
            ImGui::TextWrapped("Project/source truth changed after inspection. Reinspect before committing.");
            if (status.stalePlanSite != 0u)
                ImGui::Text("Pipeline witness line: %u", status.stalePlanSite);
            if (ImGui::Button("Reinspect source"))
            {
                vanguard::u64 acceptedGeneration = 0u;
                vanguard::u64 sourceGeneration = 0u;
                bool generationsReady = true;
                if (status.reimport)
                {
                    generationsReady = status.asset.IsValid() &&
                                       state->assets.GetAcceptedAssetGeneration(status.asset, acceptedGeneration) &&
                                       state->workspace != nullptr;
                    if (generationsReady)
                        sourceGeneration = state->workspace->GetSources().GetGeneration();
                    generationsReady = generationsReady && sourceGeneration != 0u;
                }
                if (!generationsReady ||
                    !session.Reinspect(acceptedGeneration, sourceGeneration, state->assets))
                    SetAction(*state, "Current project truth is not ready for model reinspection");
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard stale review"))
            {
                DestroySession(*state, state->activeSession);
                SetAction(*state, "Stale model review discarded without project mutation");
                return;
            }
        }
        else if (status.phase == editor::EditorModelImportSessionPhase::Failed && !status.durableCommitted)
        {
            ImGui::TextWrapped("Inspection/planning failed: %s", meshTools::ToString(status.inspection));
            const meshTools::ModelImportReport& inspection = session.GetInspectionReport();
            if (!inspection.diagnostic.Empty())
                ImGui::TextWrapped("Details: %s", inspection.diagnostic.AsChar());
        }

        if (state->lastAction[0] != '\0')
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", state->lastAction);
        }
    }

    bool ActivateModelImport(editor::EditorExtensionActivationContext& context, const void*, void*& privateState) noexcept
    {
        const vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Tools, sizeof(ModelImportExtensionState), alignof(ModelImportExtensionState));
        if (!block)
            return false;
        auto* const state = ::new (block.address) ModelImportExtensionState();
        state->assets = context.GetProjectAssets();
        state->workspace = &context.GetWorkspace();
        state->panels = context.GetOwnedPanels();
        privateState = state;

        editor::EditorUiFailure uiFailure;
        editor::EditorPanelTypeToken panelTypeToken;
        editor::EditorPanelTypeDesc panelType;
        panelType.id = ModelImportPanelTypeId;
        panelType.name = "Mesh Import";
        panelType.create = CreateModelImportPanel;
        panelType.destroy = DestroyModelImportPanel;
        panelType.draw = DrawModelImportPanel;
        panelType.visibilityChanged = ModelImportPanelVisibilityChanged;
        panelType.factoryData = state;
        panelType.policy = editor::EditorPanelTypePolicy::Singleton;
        panelType.defaultInstance = ModelImportPanelInstanceId;
        panelType.initialPlacement = editor::EditorPanelInitialPlacement::Floating;
        panelType.allowDocking = false;
        if (!context.RegisterPanelType(panelType, panelTypeToken, &uiFailure))
            return false;

        if (!state->panels.CreatePanel({ModelImportPanelInstanceId, ModelImportPanelTypeId, "Mesh Import", false},
                                       state->panel, &uiFailure))
            return false;
        g_modelImportPresentationRoute = state;
        return true;
    }

    void DeactivateModelImport(editor::EditorExtensionActivationContext&, const void*, void* const privateState) noexcept
    {
        if (privateState == nullptr)
            return;
        auto* const state = static_cast<ModelImportExtensionState*>(privateState);
        if (g_modelImportPresentationRoute == state)
            g_modelImportPresentationRoute = nullptr;
        DestroyAllSessions(*state);
        state->workspace = nullptr;
        state->~ModelImportExtensionState();
        vanguard::memory::MemoryBlock block{state, sizeof(ModelImportExtensionState), vanguard::memory::PoolId::Tools};
        vanguard::memory::Free(block);
    }

    const editor::EditorExtensionDescriptor ModelImportExtensions[]{
        {ModelImportExtensionId,
         "editor-model-import",
         "Mesh Import",
         {},
         editor::EditorExtensionSelectionPolicy::BuiltIn,
         ActivateModelImport,
         DeactivateModelImport,
         nullptr}};
} // namespace

namespace vanguard::editor
{
    EditorModelImportOfferResult OfferEditorModelImportExternalSnapshot(
        const filesystem::AbsolutePath& source,
        const containers::ArraySpan<const u8> sourceSnapshot,
        const u32 destinationRoot,
        const containers::StringView rootRelativeFolder) noexcept
    {
        ModelImportExtensionState* const state = g_modelImportPresentationRoute;
        if (state == nullptr || !state->panel.IsValid())
            return EditorModelImportOfferResult::Unavailable;
        if (!source.IsFilePath() || sourceSnapshot.Empty() || sourceSnapshot.Data() == nullptr)
            return EditorModelImportOfferResult::InvalidArgument;
        if (rootRelativeFolder.Length() >= vanguard::resources::MaximumResourcePathBytes + 1u)
            return EditorModelImportOfferResult::InvalidArgument;
        PollSessions(*state);
        ModelImportSessionEntry* const entry = CreateSession(*state);
        if (entry == nullptr)
            return EditorModelImportOfferResult::Busy;
        if (!entry->session.BeginExternalSnapshot(source, sourceSnapshot, destinationRoot, rootRelativeFolder, state->assets))
        {
            DestroySession(*state, state->activeSession);
            return EditorModelImportOfferResult::InvalidArgument;
        }
        entry->destinationRoot = destinationRoot;
        const u32 folderBytes = static_cast<u32>(rootRelativeFolder.Length());
        if (folderBytes != 0u)
            std::memcpy(entry->destinationFolder, rootRelativeFolder.Data(), folderBytes);
        entry->destinationFolder[folderBytes] = '\0';
        entry->hasExternalDestination = true;
        EditorUiFailure failure;
        if (!state->panels.SetOpen(state->panel, true, &failure))
        {
            DestroySession(*state, state->activeSession);
            return EditorModelImportOfferResult::Unavailable;
        }
        SetAction(*state, "Inspecting Asset Browser model snapshot");
        return EditorModelImportOfferResult::Accepted;
    }

    EditorModelImportOfferResult OfferEditorModelReimportFromSource(
        const assets::AssetId asset,
        const u64 expectedAcceptedGeneration,
        const u64 expectedSourceGeneration) noexcept
    {
        ModelImportExtensionState* const state = g_modelImportPresentationRoute;
        if (state == nullptr || state->workspace == nullptr || !state->panel.IsValid())
            return EditorModelImportOfferResult::Unavailable;
        if (!asset.IsValid() || expectedAcceptedGeneration == 0u || expectedSourceGeneration == 0u)
            return EditorModelImportOfferResult::InvalidArgument;

        const assets::SourceRecord* const source = state->workspace->GetSources().LookupAsset(asset);
        if (source == nullptr || !source->path.IsFilePath() ||
            source->metadataResult != assets::MetadataResult::Success ||
            source->importState != assets::SourceImportState::MetadataSelected ||
            source->metadata.importer != assets::HashCompilerName("vanguard.model.importer") ||
            source->metadata.importerVersion != meshTools::ModelSourceImporterVersion ||
            source->metadata.settingsVersion != meshTools::ModelImportSettingsVersion)
            return EditorModelImportOfferResult::InvalidArgument;
        meshTools::ModelImportSettings settings;
        if (meshTools::DecodeModelImportSettings(
                source->metadata.settingsVersion,
                {source->metadata.settings.TypedData(), source->metadata.settings.Size()}, settings) !=
            meshTools::ModelImportSettingsResult::Success)
            return EditorModelImportOfferResult::InvalidArgument;
        PollSessions(*state);
        ModelImportSessionEntry* const entry = CreateSession(*state);
        if (entry == nullptr)
            return EditorModelImportOfferResult::Busy;
        if (!entry->session.BeginExistingSource(source->path, asset, expectedAcceptedGeneration,
                                                expectedSourceGeneration, settings, state->assets))
        {
            DestroySession(*state, state->activeSession);
            return EditorModelImportOfferResult::InvalidArgument;
        }
        EditorUiFailure failure;
        if (!state->panels.SetOpen(state->panel, true, &failure))
        {
            DestroySession(*state, state->activeSession);
            return EditorModelImportOfferResult::Unavailable;
        }
        SetAction(*state, "Inspecting current model source for reviewed reimport");
        return EditorModelImportOfferResult::Accepted;
    }

    containers::ArraySpan<const EditorExtensionDescriptor> GetEditorModelImportExtensions() noexcept
    {
        return ModelImportExtensions;
    }
} // namespace vanguard::editor
