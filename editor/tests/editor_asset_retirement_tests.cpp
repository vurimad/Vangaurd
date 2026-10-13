#include "../../tests/support/scratch_directory.hpp"

#include <vanguard/editor/editor_project_service.hpp>
#include <vanguard/editor/editor_model_import_session.hpp>
#include <vanguard/editor/editor_service_ids.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/asset_streaming/project_asset_pipeline.hpp>
#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/projects/project.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace vanguard::editor::detail
{
    struct EditorProjectAssetTestAccess final
    {
        [[nodiscard]] static EditorProjectAssetAccess GetProjectAssets(ProjectWorkspaceService& workspace) noexcept
        {
            return EditorProjectAssetAccess(workspace);
        }

        [[nodiscard]] static EditorProjectTexturePreviewPresentationAccess GetPreviewPresentation(
            ProjectWorkspaceService& workspace) noexcept
        {
            return EditorProjectTexturePreviewPresentationAccess(workspace);
        }
    };
} // namespace vanguard::editor::detail

namespace
{
    namespace app = vanguard::application;
    namespace assetStreaming = vanguard::asset_streaming;
    namespace assets = vanguard::assets;
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace filesystem = vanguard::filesystem;
    namespace rendering = vanguard::rendering;
    namespace resources = vanguard::resources;
    namespace rhi = vanguard::rhi;
    namespace streaming = vanguard::streaming;
    namespace textureTools = vanguard::texture_tools;
    namespace textures = vanguard::textures;

    class PreviewPanelRegistry final : public editor::EditorPanelRegistry
    {
    public:
        [[nodiscard]] bool RegisterType(const editor::EditorPanelTypeDesc&, editor::EditorPanelTypeToken& token,
                                        editor::EditorUiFailure*) noexcept override
        {
            token = {};
            return false;
        }
        [[nodiscard]] bool UnregisterType(editor::EditorPanelTypeToken, editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool CreatePanel(const editor::EditorPanelDesc&, editor::EditorPanelHandle& panel,
                                       editor::EditorUiFailure*) noexcept override
        {
            panel = {};
            return false;
        }
        [[nodiscard]] bool CreatePlaceholder(const editor::EditorPanelPlaceholderDesc&, editor::EditorPanelHandle& panel,
                                             editor::EditorUiFailure*) noexcept override
        {
            panel = {};
            return false;
        }
        [[nodiscard]] bool DestroyPanel(editor::EditorPanelHandle, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool SetOpen(editor::EditorPanelHandle, bool, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool IsOpen(editor::EditorPanelHandle) const noexcept override { return false; }
        void VisitPanels(editor::EditorPanelVisitor, void*) const noexcept override {}
    };

    class PreviewUiService final : public editor::EditorUiService
    {
    public:
        [[nodiscard]] editor::EditorPanelRegistry& GetPanels() noexcept override { return m_panels; }
        [[nodiscard]] const editor::EditorPanelRegistry& GetPanels() const noexcept override { return m_panels; }
        [[nodiscard]] bool RegisterHost(const editor::EditorHostWindowDesc&, editor::EditorHostWindowHandle& host,
                                        editor::EditorUiFailure*) noexcept override
        {
            host = {};
            return false;
        }
        [[nodiscard]] bool UpdateHost(editor::EditorHostWindowHandle, const editor::EditorHostWindowDesc&,
                                      editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterHost(editor::EditorHostWindowHandle, editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool GetHost(editor::EditorHostWindowHandle, editor::EditorHostWindowInfo&) const noexcept override
        {
            return false;
        }
        void VisitHosts(editor::EditorHostWindowVisitor, void*) const noexcept override {}
        [[nodiscard]] bool RegisterTexture(const editor::EditorTextureDesc& desc, editor::EditorTextureHandle& texture,
                                           editor::EditorUiFailure* failure) noexcept override
        {
            texture = {};
            if (m_registered || !desc.texture.IsValid() || !desc.sampler.IsValid())
                return false;
            if (failure != nullptr)
                *failure = {};
            m_desc = desc;
            m_registered = true;
            texture = {0u, 1u};
            m_texture = texture;
            return true;
        }
        [[nodiscard]] bool UnregisterTexture(const editor::EditorTextureHandle texture,
                                             editor::EditorUiFailure* failure) noexcept override
        {
            if (!m_registered || texture != m_texture)
                return false;
            if (failure != nullptr)
                *failure = {};
            m_registered = false;
            m_texture = {};
            m_desc = {};
            return true;
        }
        [[nodiscard]] bool ResolveTexture(editor::EditorTextureHandle, editor::ResolvedEditorTexture&) const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool AcquireFrameData(editor::EditorHostWindowHandle, editor::EditorUiFrameData&) const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool AcquireTextureFrameData(editor::EditorUiTextureFrameData&) const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool BeginFrame(const engine::FrameContext&, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool DrawOpenPanels(editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] bool EndFrame(editor::EditorUiFailure*) noexcept override { return false; }
        void AbortFrame() noexcept override {}
        [[nodiscard]] editor::EditorUiInputCapture GetInputCapture() const noexcept override { return {}; }
        [[nodiscard]] editor::EditorPanelFocusSnapshot GetPanelFocus() const noexcept override { return {}; }
        [[nodiscard]] vanguard::f32 GetUserScale() const noexcept override { return 1.0f; }
        [[nodiscard]] bool SetUserScale(vanguard::f32, editor::EditorUiFailure*) noexcept override { return false; }
        [[nodiscard]] editor::EditorUiState GetState() const noexcept override { return editor::EditorUiState::Ready; }
        [[nodiscard]] vanguard::u64 GetFrame() const noexcept override { return 0u; }

        [[nodiscard]] bool HasRegisteredTexture() const noexcept { return m_registered && m_texture.IsValid(); }
        [[nodiscard]] const editor::EditorTextureDesc& RegisteredDesc() const noexcept { return m_desc; }

    private:
        [[nodiscard]] bool RegisterSurfaceForOwner(const editor::EditorExtensionOwner&,
                                                   const editor::detail::EditorUiSurfaceDesc&,
                                                   editor::detail::EditorUiSurfaceToken&,
                                                   editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterSurfaceForOwner(const editor::EditorExtensionOwner&,
                                                     const editor::detail::EditorUiSurfaceToken&,
                                                     editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool RegisterContributionForOwner(const editor::EditorExtensionOwner&,
                                                        const editor::detail::EditorUiContributionDesc&,
                                                        editor::detail::EditorUiContributionToken&,
                                                        editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterContributionForOwner(const editor::EditorExtensionOwner&,
                                                          const editor::detail::EditorUiContributionToken&,
                                                          editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        void RevokeSurfaceOwner(const editor::EditorExtensionOwner&) noexcept override {}
        [[nodiscard]] bool RegisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridge&,
                                                        editor::detail::EditorUiCommandBridgeToken&,
                                                        editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterSurfaceCommandBridge(const editor::detail::EditorUiCommandBridgeToken&,
                                                          editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool RegisterPanelFocusSink(editor::detail::EditorPanelFocusSink, void*,
                                                  editor::detail::EditorPanelFocusSinkToken&,
                                                  editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool UnregisterPanelFocusSink(editor::detail::EditorPanelFocusSinkToken,
                                                    editor::EditorUiFailure*) noexcept override
        {
            return false;
        }
        [[nodiscard]] bool RequestContextMenuForPanel(editor::EditorPanelHandle, vanguard::u64,
                                                      editor::EditorUiSurfaceId,
                                                      editor::EditorUiFailure*) noexcept override
        {
            return false;
        }

        PreviewPanelRegistry m_panels;
        editor::EditorTextureDesc m_desc;
        editor::EditorTextureHandle m_texture;
        bool m_registered = false;
    };

    constexpr char ProjectText[] =
        "vproject 1.0\n"
        "project.id = \"4c1df2e0-5e96-47f2-9e32-e6c4d8e52a01\"\n"
        "project.name = \"Asset Retirement Test\"\n"
        "project.technicalName = \"asset-retirement\"\n"
        "engine.minimum = \"0.1.0\"\n"
        "engine.maximum = \"0.1.x\"\n"
        "paths.assets = \"Assets\"\n"
        "paths.derivedData = \"DerivedData\"\n"
        "paths.intermediate = \"Intermediate\"\n"
        "paths.saved = \"Saved\"\n"
        "paths.builds = \"Builds\"\n"
        "paths.config = \"Config\"\n"
        "paths.plugins = \"Plugins\"\n"
        "target = \"windows-x64\"\n"
        "policy.cooking = \"default\"\n"
        "policy.packaging = \"default\"\n"
        "startup.editorWorld = \"\"\n"
        "startup.runtimeWorld = \"\"\n"
        "startup.input = \"\"\n";

    // Same tiny, valid source fixture used by textureToolsTests. Keeping the
    // authored image in this test makes the production-seam proof independent of
    // the process working directory.
    constexpr vanguard::u8 RedPng[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

    static_assert(editor::MaximumProjectTexturePreviewInterests == 512u);
    static_assert(editor::MaximumProjectTexturePreviewProgressingBackings == 32u);
    static_assert(editor::MaximumProjectTexturePreviewReadyBackings == 64u);
    static_assert(editor::MaximumProjectTexturePreviewStartsPerTick == 4u);
    static_assert(editor::MaximumProjectTexturePreviewStateChecksPerTick == 32u);

    [[nodiscard]] bool PreviewAdmissionThresholdPolicy() noexcept
    {
        using Priority = editor::ProjectTexturePreviewPriority;
        const auto admitted = [](const Priority priority,
                                 const vanguard::u32 background,
                                 const vanguard::u32 backgroundAndInteractive,
                                 const vanguard::u32 total,
                                 const vanguard::u32 maximumBackground,
                                 const vanguard::u32 maximumBackgroundAndInteractive,
                                 const vanguard::u32 maximumTotal) noexcept
        {
            return editor::detail::IsProjectTexturePreviewAdmissionWithinCumulativeReserves(
                priority, background, backgroundAndInteractive, total,
                maximumBackground, maximumBackgroundAndInteractive, maximumTotal);
        };

        const vanguard::u32 progressing = editor::MaximumProjectTexturePreviewProgressingBackings;
        const vanguard::u32 ready = editor::MaximumProjectTexturePreviewReadyBackings;
        return editor::detail::IsProjectTexturePreviewResidencyBelowHardLimit(ready - 1u) &&
               !editor::detail::IsProjectTexturePreviewResidencyBelowHardLimit(ready) &&
               !editor::detail::CanUseProjectTexturePreviewSupersessionResidencySlot(ready - 2u, false, true) &&
               editor::detail::CanUseProjectTexturePreviewSupersessionResidencySlot(ready - 1u, false, true) &&
               !editor::detail::CanUseProjectTexturePreviewSupersessionResidencySlot(ready - 1u, true, true) &&
               !editor::detail::CanUseProjectTexturePreviewSupersessionResidencySlot(ready - 1u, false, false) &&
               admitted(Priority::Background, progressing - 3u, progressing - 3u, progressing - 3u,
                        progressing - 2u, progressing - 1u, progressing) &&
               !admitted(Priority::Background, progressing - 2u, progressing - 2u, progressing - 2u,
                         progressing - 2u, progressing - 1u, progressing) &&
               admitted(Priority::Interactive, progressing - 2u, progressing - 2u, progressing - 2u,
                        progressing - 2u, progressing - 1u, progressing) &&
               !admitted(Priority::Interactive, progressing - 2u, progressing - 1u, progressing - 1u,
                         progressing - 2u, progressing - 1u, progressing) &&
               admitted(Priority::Focused, progressing - 2u, progressing - 1u, progressing - 1u,
                        progressing - 2u, progressing - 1u, progressing) &&
               !admitted(Priority::Focused, progressing - 2u, progressing - 1u, progressing,
                         progressing - 2u, progressing - 1u, progressing) &&
               admitted(Priority::Background, ready - 4u, ready - 4u, ready - 4u,
                        ready - 3u, ready - 2u, ready - 1u) &&
               !admitted(Priority::Background, ready - 3u, ready - 3u, ready - 3u,
                         ready - 3u, ready - 2u, ready - 1u) &&
               admitted(Priority::Interactive, ready - 3u, ready - 3u, ready - 3u,
                        ready - 3u, ready - 2u, ready - 1u) &&
               !admitted(Priority::Interactive, ready - 3u, ready - 2u, ready - 2u,
                         ready - 3u, ready - 2u, ready - 1u) &&
               admitted(Priority::Focused, ready - 3u, ready - 2u, ready - 2u,
                        ready - 3u, ready - 2u, ready - 1u) &&
               !admitted(Priority::Focused, ready - 3u, ready - 2u, ready - 1u,
                         ready - 3u, ready - 2u, ready - 1u);
    }

    struct TestClock
    {
        vanguard::u64 ticks = 1'000;
    };

    [[nodiscard]] vanguard::u64 ReadClock(void* const userData) noexcept
    {
        return static_cast<TestClock*>(userData)->ticks;
    }

    [[nodiscard]] bool FailStep(const char* const step) noexcept
    {
        std::fprintf(stderr, "[editorAssetRetirementTests] failed at %s\n", step);
        return false;
    }

    [[nodiscard]] bool WriteRawFile(const filesystem::AbsolutePath& path, const void* const data, const size_t size) noexcept
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path.AsChar(), "wb") != 0)
            file = nullptr;
        if (file == nullptr)
            return false;
        const bool written = size == 0 || std::fwrite(data, 1, size, file) == size;
        const bool closed = std::fclose(file) == 0;
        return written && closed;
    }

    [[nodiscard]] filesystem::AbsolutePath MetadataPath(const filesystem::AbsolutePath& source) noexcept
    {
        const vanguard::containers::StringView view = source.AsStringView();
        vanguard::containers::String path(view.Data(), view.Length());
        path.Append(".vmeta", 6);
        return filesystem::AbsolutePath::CreateFilePath(path);
    }

    [[nodiscard]] bool WriteMetadata(const filesystem::AbsolutePath& source, const assets::AssetMetadata& metadata) noexcept
    {
        vanguard::containers::String encoded;
        if (assets::EncodeMetadata(metadata, encoded) != assets::MetadataResult::Success)
            return false;
        return WriteRawFile(MetadataPath(source), encoded.AsChar(), encoded.Length());
    }

    [[nodiscard]] bool CopySettings(const vanguard::containers::DynamicArray<vanguard::u8>& source,
                                    vanguard::containers::DynamicArray<vanguard::u8>& destination) noexcept
    {
        destination.Clear();
        destination.Reserve(source.Size());
        if (destination.Capacity() < source.Size())
            return false;
        for (const vanguard::u8 byte : source)
            destination.PushBack(byte);
        return destination.Size() == source.Size();
    }

    void RemoveFixture(const filesystem::AbsolutePath& root) noexcept
    {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(std::filesystem::path(root.AsChar()), error));
    }

    [[nodiscard]] bool CreateFixtureDirectories(const filesystem::AbsolutePath& root) noexcept
    {
        std::error_code error;
        static_cast<void>(std::filesystem::create_directories(std::filesystem::path(root.AsChar()), error));
        if (error)
            return false;
        const char* const children[]{"Assets", "DerivedData", "Intermediate", "Saved", "Builds", "Config", "Plugins"};
        for (const char* const child : children)
        {
            error.clear();
            static_cast<void>(std::filesystem::create_directories(std::filesystem::path(root.AddDirPath(child).AsChar()), error));
            if (error)
                return false;
        }
        const filesystem::AbsolutePath assetsRoot = root.AddDirPath("Assets");
        const char* const emptyAssetFolders[]{"Textures", "StaleDestination"};
        for (const char* const child : emptyAssetFolders)
        {
            error.clear();
            static_cast<void>(std::filesystem::create_directories(
                std::filesystem::path(assetsRoot.AddDirPath(child).AsChar()), error));
            if (error)
                return false;
        }
        return true;
    }

    [[nodiscard]] engine::RenderingServiceConfig MakeRenderingConfig() noexcept
    {
        engine::RenderingServiceConfig config;
        config.deviceMode = engine::RenderingDeviceMode::Required;
        config.backendFactory = rhi::d3d12::GetBackendFactory();
        config.resourceDescriptors.capacity = 128;
        config.samplerDescriptors.capacity = 16;
        config.frameResources.descriptorCacheTarget = 32;
        config.frameResources.descriptorCacheMaximum = 64;
        config.maximumMaterialProgramLayouts = 8;
        config.gpuScene.tables.maximumPagesPerTable = 2;
        config.gpuScene.lifetime.retirementEpochCount = 4;
        config.gpuScene.lifetime.initialRetirementsPerEpoch = 8;
        config.gpuScene.upload.bytesPerSegment = 2u * 1024u * 1024u;
        config.gpuScene.upload.maximumUpdatesPerBatch = 64;
        config.gpuScene.upload.maximumCopiesPerBatch = 128;
        config.gpuScene.definitions.maximumGeometries = 4;
        config.gpuScene.definitions.maximumMaterials = 4;
        config.gpuScene.definitions.maximumRenderables = 4;
        config.gpuScene.definitions.maximumDefinitionsPerBatch = 4;
        config.gpuScene.definitions.maximumAllocationsPerBatch = 16;
        config.gpuScene.maximumExternalContributions = 2;
        config.textureResidency.residency.maximumTextures = 4;
        config.textureResidency.residency.maximumPendingInstallations = 1;
        config.textureResidency.residency.maximumPendingRetirements = 4;
        config.textureResidency.uploader.maximumRequests = 4;
        config.textureResidency.uploader.maximumAcquisitionStartsPerTick = 4;
        config.textureResidency.uploader.maximumCandidatesPerBatch = 4;
        config.textureResidency.uploader.maximumCompletionPollsPerTick = 4;
        config.textureResidency.uploader.maximumReadyCandidates = 4;
        config.textureResidency.uploader.maximumWritesPerBatch = 8;
        config.textureResidency.uploader.maximumCopiesPerBatch = 8;
        config.textureResidency.maximumResidencyRecords = 4;
        config.textureResidency.maximumDemands = 8;
        config.textureResidency.maximumInstallationsPerTick = 4;
        config.textureResidency.maximumTableInstallationsPerFrame = 4;
        config.textureResidency.maximumStateChecksPerTick = 4;
        config.textureResidency.maximumRetainedReplacementCandidateGpuBytes = 256ull * 1024ull;
        config.materialResources.maximumProviders = 4;
        config.materialResources.maximumFallbacks = 4;
        config.materialResources.maximumOperations = 8;
        config.materialResources.maximumDescriptorCacheEntries = 8;
        config.materialMaterializer.maximumOperations = 8;
        config.materialMaterializer.maximumRolesPerMaterial = 8;
        config.materialMaterializer.maximumMaterialsPerBatch = 4;
        config.materialMaterializer.maximumOperationsProgressedPerUpdate = 4;
        config.materialMaterializer.maximumResourceRolePollsPerUpdate = 16;
        config.materialResidency.maximumResidencies = 4;
        config.materialResidency.maximumDemands = 8;
        config.materialResidency.maximumTechniqueRequests = 4;
        config.materialResidency.maximumNativePrograms = 4;
        config.materialResidency.maximumResidencyChecksPerUpdate = 4;
        config.materialResidency.maximumRetirementsPerSeal = 4;
        config.materialBindings.maximumBindings = 4;
        config.materialBindings.maximumChecksPerUpdate = 4;
        return config;
    }

    [[nodiscard]] bool SubmitRetirementFences(rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        fences = {};
        const rhi::CommandListType types[]{rhi::CommandListType::Default, rhi::CommandListType::Compute, rhi::CommandListType::CopyAsync};
        for (vanguard::u32 index = 0; index < 3; ++index)
        {
            const rhi::CommandListRef list = rhi::CreateCommandList(types[index], 0x4550524554495200ull + index, &failure);
            if (!list || !rhi::BindCommandList(list, &failure))
                return false;
            rhi::UnbindCommandList();
            const rhi::CommandListRef lists[]{list};
            rhi::GpuFence completion;
            if (!rhi::CloseAndSubmitCommandLists("editor asset retirement", {lists, 1}, rhi::CommandListSyncType::None,
                                                completion, &failure))
                return false;
            fences.Include(completion);
        }
        return true;
    }

    [[nodiscard]] bool WaitRetirementFences(const rhi::ResidencyFenceSet& fences, rhi::Failure& failure) noexcept
    {
        return rhi::WaitForGpuFence({rhi::QueueType::Graphics, fences.graphics}, 5'000'000'000ull, &failure) &&
               rhi::WaitForGpuFence({rhi::QueueType::Compute, fences.compute}, 5'000'000'000ull, &failure) &&
               rhi::WaitForGpuFence({rhi::QueueType::Copy, fences.copy}, 5'000'000'000ull, &failure);
    }

    [[nodiscard]] bool RunFrame(engine::FramePipelineService& frames, TestClock& clock) noexcept
    {
        clock.ticks += 16;
        engine::FrameFailure failure;
        if (frames.RunFrame(&failure))
            return true;
        std::fprintf(stderr, "[editorAssetRetirementTests] frame failed: %s\n",
                     failure.message != nullptr ? failure.message : "unknown");
        return false;
    }

    [[nodiscard]] bool DriveUntilReady(engine::FramePipelineService& frames, TestClock& clock,
                                       rendering::TextureResidencyRuntime& runtime,
                                       const rendering::GpuTextureResidencyHandle residency) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!RunFrame(frames, clock) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.state == rendering::TextureRuntimeState::BindlessReady)
                return true;
            if (info.state == rendering::TextureRuntimeState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilReplacementCurrent(engine::FramePipelineService& frames, TestClock& clock,
                                                    rendering::TextureResidencyRuntime& runtime,
                                                    const rendering::GpuTextureResidencyHandle residency,
                                                    const vanguard::u32 expectedGeneration) noexcept
    {
        rendering::TextureResidencyRuntimeFailure failure;
        rendering::TextureRuntimeInfo info;
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!RunFrame(frames, clock) || !runtime.GetInfo(residency, info, &failure))
                return false;
            if (info.state == rendering::TextureRuntimeState::BindlessReady && info.resourceGeneration == expectedGeneration &&
                info.replacementState == rendering::TextureReplacementState::None && !info.replacementRetryRequired)
                return true;
            if (info.replacementState == rendering::TextureReplacementState::Failed)
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DrivePreviewUntilReady(editor::EditorProjectAssetAccess& assets,
                                              const editor::ProjectTexturePreviewHandle handle,
                                              engine::FramePipelineService& frames, TestClock& clock,
                                              editor::ProjectTexturePreviewInfo& info,
                                              bool* const sawGenerationReacquire = nullptr) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (!assets.QueryTexturePreview(handle, info))
                return false;
            if (sawGenerationReacquire != nullptr &&
                info.state == editor::ProjectTexturePreviewState::WaitingForCurrentGeneration)
                *sawGenerationReacquire = true;
            if (info.state == editor::ProjectTexturePreviewState::Ready)
                return true;
            if (info.state == editor::ProjectTexturePreviewState::Failed)
                return false;
            if (!RunFrame(frames, clock))
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilPreviewPublication(assetStreaming::ProjectAssetPipeline& pipeline,
                                                    const resources::ResourceReference output,
                                                    engine::FramePipelineService& frames,
                                                    TestClock& clock,
                                                    assetStreaming::ProjectAssetOutputDeliveryStatus& status,
                                                    const assets::BuildFingerprint* const differentFrom = nullptr) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            status = {};
            const assetStreaming::ProjectAssetAutomaticRecookStatus automatic = pipeline.GetAutomaticRecookStatus();
            if (automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::Idle &&
                automatic.pendingDeliveryOutputs == 0u &&
                pipeline.GetOutputDeliveryStatus(output, status) &&
                status.resource == output &&
                status.buildState == assetStreaming::ProjectAssetDeliveryStageState::Committed &&
                (status.publicationState == assetStreaming::ProjectAssetDeliveryStageState::Installed ||
                 status.publicationState == assetStreaming::ProjectAssetDeliveryStageState::Stale) &&
                !status.publishedBuildFingerprint.IsEmpty() &&
                !status.publishedContentFingerprint.IsEmpty() &&
                status.publishedPhysicalPath.IsFilePath() &&
                (differentFrom == nullptr || status.publishedBuildFingerprint != *differentFrom))
                return true;
            if (!RunFrame(frames, clock))
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilAssetMutationReady(editor::EditorProjectAssetAccess& assets,
                                                    engine::FramePipelineService& frames,
                                                    TestClock& clock) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (assets.CanMutateAssets())
                return true;
            if (!RunFrame(frames, clock))
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool DriveUntilExternalImportDestinationReady(
        editor::EditorProjectAssetAccess& assets, const vanguard::u32 root,
        const vanguard::containers::StringView folder, engine::FramePipelineService& frames,
        TestClock& clock) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            if (assets.CanImportExternalSource(root, folder))
                return true;
            if (!RunFrame(frames, clock))
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool CompleteTextureRetirements(engine::RenderingService& service,
                                                   rendering::TextureResidencyRuntime& runtime) noexcept
    {
        rhi::Failure rhiFailure;
        rhi::ResidencyFenceSet safeAfter;
        engine::RenderingRetirementFailure retirementFailure;
        if (!SubmitRetirementFences(safeAfter, rhiFailure) || !service.SealResidencyRetirements(safeAfter, &retirementFailure) ||
            !WaitRetirementFences(safeAfter, rhiFailure) || !rhi::RetireResources(&rhiFailure) ||
            !rhi::FlushRetiredResources(&rhiFailure))
            return false;
        rendering::GpuSceneLifetimeFailure lifetimeFailure;
        static_cast<void>(service.GetGpuScene().GetLifetime().Collect(&lifetimeFailure));
        rendering::TextureResidencyRuntimeFailure runtimeFailure;
        static_cast<void>(runtime.CollectRetirements(&runtimeFailure));
        return lifetimeFailure.code == rendering::GpuSceneLifetimeFailureCode::None &&
               runtimeFailure.code == rendering::TextureResidencyRuntimeFailureCode::None;
    }

    [[nodiscard]] bool RetireCurrentTexture(engine::FramePipelineService& frames, TestClock& clock,
                                            engine::RenderingService& service, rendering::TextureDemandHandle& demand) noexcept
    {
        rendering::TextureResidencyRuntime& runtime = service.GetTextureResidency();
        demand.Reset();
        if (!RunFrame(frames, clock) || !CompleteTextureRetirements(service, runtime))
            return false;
        // Do not run another workspace frame here: once renderer retirement is
        // complete, the production project-retirement participant would consume
        // the evidence before this oracle can verify the exact NotTracked handoff.
        return true;
    }

    [[nodiscard]] assetStreaming::ProjectAssetPipelineResult DrainLooseMaintenance(
        assetStreaming::ProjectAssetPipeline& pipeline, const vanguard::u32 maximumTicks = 200000u) noexcept
    {
        for (vanguard::u32 tick = 0; tick < maximumTicks; ++tick)
        {
            const assetStreaming::ProjectAssetPipelineResult result = pipeline.TickLooseMaintenance();
            if (result != assetStreaming::ProjectAssetPipelineResult::Pending)
                return result;
            vanguard::concurrency::YieldCurrentThread();
        }
        return assetStreaming::ProjectAssetPipelineResult::Pending;
    }

    [[nodiscard]] bool RequestLooseMaintenanceAfterOwnerSettlement(
        assetStreaming::ProjectAssetPipeline& pipeline) noexcept
    {
        for (vanguard::u32 attempt = 0u; attempt < 10'000u; ++attempt)
        {
            const assetStreaming::ProjectAssetPipelineResult requested = pipeline.RequestLooseMaintenance();
            if (requested == assetStreaming::ProjectAssetPipelineResult::Pending)
                return true;
            if (requested != assetStreaming::ProjectAssetPipelineResult::Busy)
                return false;

            assetStreaming::ProjectAssetPipelineFailure failure;
            const assetStreaming::ProjectAssetPipelineResult observation = pipeline.TickObservation(nullptr, &failure);
            const bool observationAllowed =
                observation == assetStreaming::ProjectAssetPipelineResult::Success ||
                observation == assetStreaming::ProjectAssetPipelineResult::Pending ||
                observation == assetStreaming::ProjectAssetPipelineResult::Busy ||
                observation == assetStreaming::ProjectAssetPipelineResult::ReconciliationFailed ||
                observation == assetStreaming::ProjectAssetPipelineResult::SnapshotSaveFailed ||
                observation == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
            if (!observationAllowed)
                return false;

            if (pipeline.IsAutomaticRecookEnabled())
            {
                const assetStreaming::ProjectAssetPipelineResult automatic = pipeline.TickAutomaticRecook(&failure);
                const bool automaticAllowed =
                    automatic == assetStreaming::ProjectAssetPipelineResult::Success ||
                    automatic == assetStreaming::ProjectAssetPipelineResult::Pending ||
                    automatic == assetStreaming::ProjectAssetPipelineResult::BuildRequestFailed ||
                    automatic == assetStreaming::ProjectAssetPipelineResult::PublicationFailed ||
                    automatic == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
                if (!automaticAllowed)
                    return false;
            }
            vanguard::concurrency::YieldCurrentThread();
        }
        return false;
    }

    [[nodiscard]] bool WaitForResourceDrain(resources::ResourcePipeline& pipeline, streaming::ResourceStreamer& streamer,
                                             engine::FramePipelineService& frames, TestClock& clock) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10'000; ++attempt)
        {
            const resources::PipelineStats pipelineStats = pipeline.GetStats();
            const streaming::Stats streamingStats = streamer.GetStats();
            if (pipelineStats.activeOperations == 0 && pipelineStats.activeJobs == 0 && pipelineStats.activePreparations == 0 &&
                pipelineStats.externalRequests == 0 && streamingStats.activeLoads == 0 && streamingStats.liveSources == 0 &&
                streamingStats.activeReads == 0 && streamingStats.stagingBytesInUse == 0)
                return true;
            if (!RunFrame(frames, clock))
                return false;
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        return false;
    }

    [[nodiscard]] bool PeekSingleRetirement(rendering::TextureResidencyRuntime& runtime,
                                            const resources::ResourcePath expectedPath,
                                            const vanguard::u32 expectedGeneration,
                                            rendering::TextureGenerationRetirement& retirement) noexcept
    {
        rendering::TextureGenerationRetirement evidence[2]{};
        rendering::TextureResidencyFailure failure;
        const vanguard::u32 count = runtime.GetResidencyManager().PeekCompletedGenerationRetirements(evidence, &failure);
        if (failure.code != rendering::TextureResidencyFailureCode::None || count != 1 ||
            evidence[0].resourcePath != expectedPath || evidence[0].resourceGeneration != expectedGeneration ||
            evidence[0].retirementId == 0)
            return false;
        retirement = evidence[0];
        return true;
    }

    [[nodiscard]] bool NoCompletedRetirements(rendering::TextureResidencyRuntime& runtime) noexcept
    {
        rendering::TextureGenerationRetirement evidence[1]{};
        rendering::TextureResidencyFailure failure;
        return runtime.GetResidencyManager().PeekCompletedGenerationRetirements(evidence, &failure) == 0 &&
               failure.code == rendering::TextureResidencyFailureCode::None &&
               runtime.GetStats().residency.pendingGenerationRetirements == 0;
    }

    [[nodiscard]] bool ExactRetirementAlreadyAcknowledged(rendering::TextureResidencyRuntime& runtime,
                                                          const vanguard::u64 retirementId) noexcept
    {
        rendering::TextureResidencyFailure failure;
        return !runtime.GetResidencyManager().AcknowledgeGenerationRetirement(retirementId, &failure) &&
               failure.code == rendering::TextureResidencyFailureCode::InvalidArgument;
    }

    [[nodiscard]] bool PrepareProject(const filesystem::AbsolutePath& root, editor::ProjectWorkspaceConfig& workspaceConfig,
                                      assets::AssetMetadata& metadata, resources::ResourceReference& output,
                                      vanguard::containers::DynamicArray<vanguard::u8>& replacementSettings) noexcept
    {
        RemoveFixture(root);
        if (!CreateFixtureDirectories(root) || !textureTools::Initialize())
            return false;

        vanguard::projects::Diagnostic diagnostic;
        if (vanguard::projects::Parse(ProjectText, workspaceConfig.project, &diagnostic) != vanguard::projects::Result::Success)
            return false;
        workspaceConfig.projectFile = root.AddFilePath("asset-retirement.vproject");
        if (!WriteRawFile(workspaceConfig.projectFile, ProjectText, sizeof(ProjectText) - 1u))
            return false;

        textureTools::TextureBuildDescription description;
        description.sourceMode = textureTools::TextureBuildSourceMode::Image2D;
        description.colorSpace = textureTools::ImportedColorSpace::Automatic;
        description.profile = textureTools::profiles::Ui;
        vanguard::containers::DynamicArray<vanguard::u8> initialSettings(vanguard::memory::pools::Assets::GetInstance());
        if (textureTools::EncodeTextureBuildSettings(description, initialSettings) != textureTools::TextureBuildSettingsResult::Success)
            return false;
        description.colorSpace = textureTools::ImportedColorSpace::SRgb;
        if (textureTools::EncodeTextureBuildSettings(description, replacementSettings) != textureTools::TextureBuildSettingsResult::Success)
            return false;

        for (vanguard::u32 index = 0; index < 16; ++index)
            metadata.id.bytes[index] = static_cast<vanguard::u8>(index + 1u);
        metadata.importer = assets::HashCompilerName("vanguard.texture.compiler");
        metadata.importerVersion = textureTools::TextureAssetCompilerVersion;
        metadata.settingsVersion = 1;
        if (!CopySettings(initialSettings, metadata.settings))
            return false;
        assets::AssetOutput assetOutput;
        assetOutput.key = vanguard::containers::String("main");
        assetOutput.type = textures::TextureResourceType;
        metadata.outputs.PushBack(static_cast<assets::AssetOutput&&>(assetOutput));
        output = assets::GetAssetOutputReference(metadata.id, metadata.outputs[0]);
        if (!output.IsValid() || !output.IsTyped())
            return false;

        const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddFilePath("resident.png");
        return WriteRawFile(source, RedPng, sizeof(RedPng)) && WriteMetadata(source, metadata);
    }

    [[nodiscard]] bool RunProjectTexturePreviewCoordinatorProof(
        const filesystem::AbsolutePath& root, editor::ProjectWorkspaceConfig& workspaceConfig,
        assets::AssetMetadata& metadata, const resources::ResourceReference output,
        const vanguard::containers::DynamicArray<vanguard::u8>& replacementSettings) noexcept
    {
        app::EngineHost host;
        app::HostFailure hostFailure;
        const filesystem::Config filesystemConfig{root, root, root.AddDirPath("DerivedData")};
        const engine::RenderingServiceConfig renderingConfig = MakeRenderingConfig();
        if (!engine::RegisterEngineModule(host, &hostFailure) ||
            !host.RegisterModule({editor::EditorModuleId, "editor", 1}, &hostFailure) ||
            !engine::RegisterIoService(host, &hostFailure) ||
            !engine::RegisterFilesystemService(host, filesystemConfig, &hostFailure) ||
            !engine::RegisterJobsService(host, &hostFailure) ||
            !engine::RegisterFramePipelineService(host, &hostFailure) ||
            !engine::RegisterReflectionService(host, &hostFailure) ||
            !engine::RegisterResourcesService(host, &hostFailure) ||
            !engine::RegisterResourceStreamingService(host, &hostFailure) ||
            !engine::RegisterRenderingService(host, renderingConfig, &hostFailure) ||
            !editor::RegisterProjectWorkspaceService(host, workspaceConfig, &hostFailure))
            return FailStep("preview-service-registration");

        const app::ApplicationProfile profile =
            app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        if (!host.Compile(profile, &hostFailure) || !host.Start(&hostFailure))
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] preview host start failure: code=%u service=%llu related=%llu message=%s\n",
                         static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                         static_cast<unsigned long long>(hostFailure.relatedService),
                         hostFailure.message != nullptr ? hostFailure.message : "<none>");
            return FailStep("preview-host-start");
        }

        editor::ProjectWorkspaceService* const workspace = editor::FindProjectWorkspaceService(host);
        engine::FramePipelineService* const frames = engine::FindFramePipelineService(host);
        engine::RenderingService* const renderingService = engine::FindRenderingService(host);
        if (workspace == nullptr || frames == nullptr || renderingService == nullptr)
            return FailStep("preview-service-find");
        assetStreaming::ProjectAssetPipeline* const pipeline = workspace->GetAssetPipeline();
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return FailStep("preview-project-pipeline");

        TestClock clock;
        engine::FrameFailure frameFailure;
        engine::FramePipelineConfig frameConfig;
        frameConfig.clock = {&ReadClock, 1'000, &clock};
        frameConfig.pacing = engine::FramePacingMode::Disabled;
        if (!frames->Configure(frameConfig, &frameFailure) || !frames->Compile(&frameFailure))
            return FailStep("preview-frame-configure");

        assetStreaming::ProjectAssetOutputDeliveryStatus statusA;
        if (!DriveUntilPreviewPublication(*pipeline, output, *frames, clock, statusA))
            return FailStep("preview-workspace-publish-a");

        editor::EditorProjectAssetAccess projectAssets =
            editor::detail::EditorProjectAssetTestAccess::GetProjectAssets(*workspace);
        editor::EditorProjectAssetRootPresentation rootPresentation;
        if (projectAssets.GetSourceRootCount() != 1u ||
            !projectAssets.GetSourceRootPresentation(0u, rootPresentation) || rootPresentation.readOnly ||
            std::strcmp(rootPresentation.displayName, "Project Assets") != 0)
            return FailStep("p0-root-presentation");
        if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
            return FailStep("p0-import-mutation-ready");

        // Optional exact local reproduction in the disposable test workspace.
        // Neither the external GLB nor Project Cardinal is ever mutated here.
        char localGlbPath[2048]{};
        size_t localGlbLength = 0u;
        if (getenv_s(&localGlbLength, localGlbPath, sizeof(localGlbPath),
                     "VANGUARD_MESH_TOOLS_LOCAL_GLB") == 0 && localGlbLength > 1u)
        {
            const auto realGlbWorkflowStart = std::chrono::steady_clock::now();
            std::FILE* localFile = nullptr;
            if (fopen_s(&localFile, localGlbPath, "rb") != 0 || localFile == nullptr)
                return FailStep("model-import-local-glb-open");
            const bool fileSizeReady = std::fseek(localFile, 0, SEEK_END) == 0;
            const long fileSize = fileSizeReady ? std::ftell(localFile) : -1L;
            if (!fileSizeReady || fileSize <= 0L || std::fseek(localFile, 0, SEEK_SET) != 0)
            {
                std::fclose(localFile);
                return FailStep("model-import-local-glb-size");
            }
            vanguard::containers::DynamicArray<vanguard::u8> localBytes(vanguard::memory::pools::Tools::GetInstance());
            localBytes.Resize(static_cast<vanguard::u32>(fileSize));
            const bool localRead = localBytes.Size() == static_cast<vanguard::u32>(fileSize) &&
                                   std::fread(localBytes.Data(), 1u, localBytes.Size(), localFile) == localBytes.Size();
            std::fclose(localFile);
            if (!localRead)
                return FailStep("model-import-local-glb-read");
            const filesystem::AbsolutePath localSource = filesystem::AbsolutePath::CreateFilePath(localGlbPath);
            vanguard::mesh_tools::ModelImportManifest localManifest;
            vanguard::mesh_tools::ModelImportReport localReport;
            const auto inspectStart = std::chrono::steady_clock::now();
            if (vanguard::mesh_tools::InspectModelMemory(
                    localBytes.Data(), localBytes.Size(), "glb", localSource.AsStringView(),
                    {}, {}, localManifest, &localReport) != vanguard::mesh_tools::ModelImportResult::Success)
                return FailStep("model-import-local-glb-inspect");
            const auto inspectMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - inspectStart).count();
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] real GLB inspect duration_ms=%lld geometries=%u embeddedTextures=%u textureRefs=%u materials=%u\n",
                         static_cast<long long>(inspectMs), localManifest.geometryDefinitions.Size(),
                         localManifest.embeddedTextures.Size(), localManifest.textureReferences.Size(), localManifest.materials.Size());
            const filesystem::AbsolutePath modelFolder = workspace->GetAssetsRoot();
            if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock) ||
                !DriveUntilExternalImportDestinationReady(projectAssets, 0u, {}, *frames, clock))
                return FailStep("model-import-local-glb-ready");
            vanguard::mesh_tools::ModelImportSettings localSettings;
            localSettings.materialPolicy = vanguard::mesh_tools::ModelImportMaterialPolicy::ImportModelOwned;
            localSettings.embeddedTexturePolicy = vanguard::mesh_tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned;
            localSettings.includeComposition = true;
            vanguard::mesh_tools::ModelImportPlan localPlan;
            vanguard::mesh_tools::ModelImportPlanReport localPlanReport;
            assetStreaming::ProjectAssetPipelineFailure localFailure;
            assets::AssetId localPlannedAsset;
            const auto planStart = std::chrono::steady_clock::now();
            const auto prefabPreflight = projectAssets.PrepareModelImportPlan(
                localSource, {localBytes.TypedData(), localBytes.Size()}, 0u, {}, "lieutenantHead.glb", {},
                0u, 0u, localManifest, localSettings, localPlan, &localPlanReport, localPlannedAsset, &localFailure);
            // The original Project Cardinal import had composition disabled.
            // A separate opt-in prefab capability check must not prevent its
            // BLEND/specular VMAT output cohort from being fully exercised.
            const filesystem::AbsolutePath destination = modelFolder.AddFilePath("lieutenantHead.glb");
            if (prefabPreflight != assetStreaming::ProjectAssetPipelineResult::OperationFailed ||
                localPlanReport.blockers == 0u ||
                std::strstr(localPlanReport.diagnostic.AsChar(), "rigid unit-scale") == nullptr ||
                filesystem::RawFileReader::Create(destination) ||
                filesystem::RawFileReader::Create(MetadataPath(destination)))
                return FailStep("model-import-local-glb-prefab-preflight");
            localSettings.includeComposition = false;
            localFailure = {};
            const auto localPrepared = projectAssets.PrepareModelImportPlan(
                localSource, {localBytes.TypedData(), localBytes.Size()}, 0u, {}, "lieutenantHead.glb", {},
                0u, 0u, localManifest, localSettings, localPlan, &localPlanReport, localPlannedAsset, &localFailure);
            if (localPrepared != assetStreaming::ProjectAssetPipelineResult::Success || localPlan.HasBlockingConflicts())
            {
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB plan result=%s blockers=%u diagnostic=%s\n",
                             assetStreaming::ToString(localPrepared), localPlan.GetBlockers().Size(),
                             localPlanReport.diagnostic.AsChar());
                return FailStep("model-import-local-glb-plan");
            }
            const auto planMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - planStart).count();
            std::fprintf(stderr, "[editorAssetRetirementTests] real GLB review/preflight duration_ms=%lld outputs=%u\n",
                         static_cast<long long>(planMs), localPlan.GetImportDescription().outputs.Size());
            assets::AssetId localImportedAsset;
            localFailure = {};
            const bool watchedBeforeLargeCommit = pipeline->IsObservingSources();
            const auto initialCommitStart = std::chrono::steady_clock::now();
            auto localCommitted = projectAssets.CommitModelImportPlan(
                localPlan, {localBytes.TypedData(), localBytes.Size()}, localImportedAsset, &localFailure);
            const auto initialCommitMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - initialCommitStart).count();
            std::fprintf(stderr, "[editorAssetRetirementTests] real GLB initial owner-thread commit duration_us=%lld result=%s\n",
                         static_cast<long long>(initialCommitMicros), assetStreaming::ToString(localCommitted));
            if (watchedBeforeLargeCommit &&
                (localCommitted == assetStreaming::ProjectAssetPipelineResult::Success ||
                 localCommitted == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending ||
                 localCommitted == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) &&
                pipeline->IsObservingSources())
                return FailStep("model-import-local-glb-watcher-paused-through-large-commit");
            for (vanguard::u32 retry = 0u;
                 retry < 8u && localCommitted == assetStreaming::ProjectAssetPipelineResult::Busy; ++retry)
            {
                bool ready = false;
                for (vanguard::u32 attempt = 0u; attempt < 10'000u; ++attempt)
                {
                    const assetStreaming::ProjectAssetObservationStatus observed = pipeline->GetObservationStatus();
                    if (!observed.rescanRequired && !observed.reconcilePending && projectAssets.CanMutateAssets())
                    {
                        ready = true;
                        break;
                    }
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-observer-frame");
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                if (!ready)
                    return FailStep("model-import-local-glb-observer-settle");
                localFailure = {};
                localCommitted = projectAssets.CommitModelImportPlan(
                    localPlan, {localBytes.TypedData(), localBytes.Size()}, localImportedAsset, &localFailure);
            }
            if (localCommitted == assetStreaming::ProjectAssetPipelineResult::Pending)
            {
                const auto preparing = pipeline->GetDeferredPlannedImportStatus();
                if (preparing.state != assetStreaming::ProjectAssetDeferredPlannedImportState::PreparingTransaction ||
                    preparing.asset != localPlannedAsset || localImportedAsset.IsValid() ||
                    filesystem::RawFileReader::Create(modelFolder.AddFilePath("lieutenantHead.glb")))
                    return FailStep("model-import-local-glb-pre-durable-pending-proof");

                char shutdownFlag[8]{};
                size_t shutdownFlagLength = 0u;
                const bool cancelThroughOwnerShutdown =
                    getenv_s(&shutdownFlagLength, shutdownFlag, sizeof(shutdownFlag),
                             "VANGUARD_EDITOR_ASYNC_SHUTDOWN_PROOF") == 0 &&
                    shutdownFlagLength > 1u && shutdownFlag[0] == '1';
                if (cancelThroughOwnerShutdown)
                {
                    const filesystem::AbsolutePath shutdownModelDestination = modelFolder.AddFilePath("lieutenantHead.glb");
                    const filesystem::AbsolutePath shutdownModelMetadata = MetadataPath(shutdownModelDestination);
                    const bool shutdownSucceeded = host.Shutdown(&hostFailure);
                    if (!shutdownSucceeded || rhi::IsInitialized())
                    {
                        std::fprintf(stderr,
                                     "[editorAssetRetirementTests] async host shutdown failed: code=%u service=%llu related=%llu message=%s rhi=%u\n",
                                     static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                                     static_cast<unsigned long long>(hostFailure.relatedService),
                                     hostFailure.message != nullptr ? hostFailure.message : "<none>",
                                     static_cast<unsigned>(rhi::IsInitialized()));
                        return FailStep("model-import-local-glb-async-shutdown");
                    }
                    std::error_code sourceCheckError;
                    const bool sourceLeft = std::filesystem::exists(std::filesystem::path(shutdownModelDestination.AsChar()), sourceCheckError);
                    std::error_code metadataCheckError;
                    const bool metadataLeft = std::filesystem::exists(std::filesystem::path(shutdownModelMetadata.AsChar()), metadataCheckError);
                    if (sourceCheckError || metadataCheckError || sourceLeft || metadataLeft)
                        return FailStep("model-import-local-glb-async-shutdown-durable-leak");
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] async pre-durable shutdown cancelled/joined and left no source or VMETA\n");
                    return true;
                }

                // Opt-in adversarial proof: an unrelated writer can create a
                // colliding destination while our worker owns only Prepare.
                // The owner MUST repeat authoritative disk validation before
                // Apply and leave the foreign file unmodified on StalePlan.
                char collisionFlag[8]{};
                size_t collisionFlagLength = 0u;
                const bool injectForeignCollision =
                    getenv_s(&collisionFlagLength, collisionFlag, sizeof(collisionFlag),
                             "VANGUARD_EDITOR_ASYNC_COLLISION_PROOF") == 0 &&
                    collisionFlagLength > 1u && collisionFlag[0] == '1';
                char metadataCollisionFlag[8]{};
                size_t metadataCollisionFlagLength = 0u;
                const bool injectForeignMetadataCollision =
                    getenv_s(&metadataCollisionFlagLength, metadataCollisionFlag, sizeof(metadataCollisionFlag),
                             "VANGUARD_EDITOR_ASYNC_METADATA_COLLISION_PROOF") == 0 &&
                    metadataCollisionFlagLength > 1u && metadataCollisionFlag[0] == '1';
                if (injectForeignCollision && injectForeignMetadataCollision)
                    return FailStep("model-import-local-glb-conflicting-collision-fixtures");
                const filesystem::AbsolutePath collidedDestination = modelFolder.AddFilePath("lieutenantHead.glb");
                constexpr char ForeignContent[] = "external-writer-collision";
                if (injectForeignCollision || injectForeignMetadataCollision)
                {
                    const filesystem::AbsolutePath foreignPath = injectForeignMetadataCollision
                        ? MetadataPath(collidedDestination) : collidedDestination;
                    std::FILE* foreign = nullptr;
                    if (fopen_s(&foreign, foreignPath.AsChar(), "wb") != 0 || foreign == nullptr)
                        return FailStep("model-import-local-glb-collision-create");
                    const bool written = std::fwrite(ForeignContent, 1u, sizeof(ForeignContent) - 1u, foreign) ==
                                         sizeof(ForeignContent) - 1u && std::fflush(foreign) == 0;
                    std::fclose(foreign);
                    if (!written)
                        return FailStep("model-import-local-glb-collision-write");
                }

                vanguard::u64 lastProgress = 0u;
                vanguard::u64 observedTotal = 0u;
                long long maximumOwnerFrameUs = 0;
                bool observedWorkerProgress = false;
                bool settled = false;
                for (vanguard::u32 tick = 0u; tick < 15'000u; ++tick)
                {
                    const auto current = pipeline->GetDeferredPlannedImportStatus();
                    if (current.asset != localPlannedAsset)
                        return FailStep("model-import-local-glb-pre-durable-identity");
                    if (current.state == assetStreaming::ProjectAssetDeferredPlannedImportState::AwaitingReconciliation ||
                        current.state == assetStreaming::ProjectAssetDeferredPlannedImportState::CohortQueued)
                    {
                        localImportedAsset = localPlannedAsset;
                        localCommitted = assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending;
                        settled = true;
                        break;
                    }
                    if (current.state == assetStreaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed)
                    {
                        if ((injectForeignCollision || injectForeignMetadataCollision) &&
                            current.result == assetStreaming::ProjectAssetPipelineResult::StalePlan)
                        {
                            const filesystem::AbsolutePath foreignMetadata = MetadataPath(collidedDestination);
                            {
                                const filesystem::AbsolutePath foreignPath = injectForeignMetadataCollision
                                    ? foreignMetadata : collidedDestination;
                                const filesystem::AbsolutePath mustStayAbsent = injectForeignMetadataCollision
                                    ? collidedDestination : foreignMetadata;
                                auto foreign = filesystem::RawFileReader::Create(foreignPath);
                                char actual[sizeof(ForeignContent)]{};
                                if (!foreign || foreign->GetSize() != sizeof(ForeignContent) - 1u)
                                    return FailStep("model-import-local-glb-collision-preserves-foreign-size");
                                foreign->Serialize(actual, sizeof(ForeignContent) - 1u);
                                if (foreign->HasErrors() ||
                                    std::memcmp(actual, ForeignContent, sizeof(ForeignContent) - 1u) != 0 ||
                                    filesystem::RawFileReader::Create(mustStayAbsent))
                                    return FailStep("model-import-local-glb-collision-preserves-foreign-bytes");
                            }
                            if (!host.Shutdown(&hostFailure) || rhi::IsInitialized())
                                return FailStep("model-import-local-glb-collision-quiesce");
                            std::fprintf(stderr,
                                         "[editorAssetRetirementTests] async %s collision rejected stale import, foreign bytes preserved; original GLB unchanged\n",
                                         injectForeignMetadataCollision ? "VMETA-only" : "source");
                            return true;
                        }
                        std::fprintf(stderr,
                                     "[editorAssetRetirementTests] real GLB pre-durable staging failed: result=%s\n",
                                     assetStreaming::ToString(current.result));
                        return FailStep("model-import-local-glb-pre-durable-prepare-failed");
                    }
                    if (current.state != assetStreaming::ProjectAssetDeferredPlannedImportState::PreparingTransaction ||
                        current.prepareCompletedBytes < lastProgress ||
                        (current.prepareTotalBytes != 0u &&
                         current.prepareCompletedBytes > current.prepareTotalBytes))
                        return FailStep("model-import-local-glb-prepare-progress-integrity");
                    if (current.prepareTotalBytes != 0u)
                    {
                        observedTotal = current.prepareTotalBytes;
                        observedWorkerProgress = observedWorkerProgress || current.prepareCompletedBytes > lastProgress;
                    }
                    lastProgress = current.prepareCompletedBytes;
                    const auto frameStart = std::chrono::steady_clock::now();
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-pre-durable-owner-frame");
                    const auto ownerFrameUs = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - frameStart).count();
                    maximumOwnerFrameUs = std::max(maximumOwnerFrameUs, static_cast<long long>(ownerFrameUs));
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                std::fprintf(stderr,
                             "[editorAssetRetirementTests] real GLB worker prepare total_bytes=%llu progress_observed=%u max_owner_frame_us=%lld elapsed_ms=%lld\n",
                             static_cast<unsigned long long>(observedTotal),
                             static_cast<unsigned>(observedWorkerProgress), maximumOwnerFrameUs,
                             static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - initialCommitStart).count()));
                if (!settled || !observedWorkerProgress || observedTotal == 0u)
                    return FailStep("model-import-local-glb-async-prepared-continuation");
                if (injectForeignCollision || injectForeignMetadataCollision)
                    return FailStep("model-import-local-glb-collision-was-not-rejected");
            }
            std::fprintf(stderr, "[editorAssetRetirementTests] real GLB commit result=%s failure=%s build=%s outputIndex=%u staleSite=%u outputs=%u\n",
                         assetStreaming::ToString(localCommitted), assetStreaming::ToString(localFailure.result),
                         assets::ToString(localFailure.build), localFailure.buildRequestOutputIndex,
                         localFailure.stalePlanSite, localPlan.GetImportDescription().outputs.Size());
            if ((localCommitted != assetStreaming::ProjectAssetPipelineResult::Success &&
                 localCommitted != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
                 localCommitted != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) ||
                localImportedAsset != localPlannedAsset)
                return FailStep("model-import-local-glb-commit");
            const auto deferredAtCommit = pipeline->GetDeferredPlannedImportStatus();
            const bool ownerDeferredImport =
                localCommitted == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending &&
                deferredAtCommit.state == assetStreaming::ProjectAssetDeferredPlannedImportState::AwaitingReconciliation &&
                deferredAtCommit.asset == localImportedAsset;
            if (watchedBeforeLargeCommit && !ownerDeferredImport)
                return FailStep("model-import-local-glb-owner-deferred-after-durability");
            const filesystem::AbsolutePath durableGlb = modelFolder.AddFilePath("lieutenantHead.glb");
            {
                // Windows readers may deny delete sharing. Keep these
                // verification handles scoped so the later observer-deletion
                // proof does not fail on its own test fixture's open handles.
                auto durableSource = filesystem::RawFileReader::Create(durableGlb);
                auto durableMetadata = filesystem::RawFileReader::Create(MetadataPath(durableGlb));
                if (!durableSource || durableSource->GetSize() != localBytes.Size() ||
                    !durableMetadata || durableMetadata->GetSize() == 0u ||
                    durableMetadata->GetSize() > assets::MaximumAssetMetadataBytes)
                    return FailStep("model-import-local-glb-durable-truth");
            }

            // Test the post-commit cohort as well as the file transaction.
            // The file/VMETA being durable alone does not prove that each
            // separately-authored model output can construct a BuildRequest.
            // CommitModelImportPlan already calls ResumeCommittedModelImport.
            // Success/CommittedCleanupPending prove it acquired the exact
            // cohort. Retrying that same explicit generation is correctly
            // Busy while its submitted build is live, not an admission fault.
            bool localCohortAdmitted = localCommitted == assetStreaming::ProjectAssetPipelineResult::Success ||
                                       localCommitted == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending;
            const auto postCommitStart = std::chrono::steady_clock::now();
            bool localRecoveryRetried = false;
            for (vanguard::u32 attempt = 0u; !localCohortAdmitted && attempt < 10'000u; ++attempt)
            {
                assetStreaming::ProjectAssetPipelineFailure cohortFailure;
                const auto deferred = pipeline->GetDeferredPlannedImportStatus();
                if (ownerDeferredImport && deferred.asset == localImportedAsset &&
                    deferred.state == assetStreaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed)
                {
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] real GLB owner-deferred admission failed: result=%s build=%s outputIndex=%u\n",
                                 assetStreaming::ToString(deferred.result), assets::ToString(deferred.build),
                                 deferred.buildRequestOutputIndex);
                    return FailStep("model-import-local-glb-deferred-cohort-admission");
                }
                if (ownerDeferredImport && deferred.asset == localImportedAsset &&
                    deferred.state == assetStreaming::ProjectAssetDeferredPlannedImportState::CohortQueued &&
                    deferred.acceptedGeneration != 0u)
                {
                    localCohortAdmitted = true;
                    break;
                }
                // Resolving the exact 31 output requests is substantial work,
                // not a cheap status poll. Retry cohort admission periodically
                // while every intervening frame advances the real coordinator.
                const auto cohortResult = !ownerDeferredImport && attempt % 32u == 0u
                                              ? projectAssets.ResumeCommittedModelImport(
                                                    localPlan, localImportedAsset, 0u, &cohortFailure)
                                              : assetStreaming::ProjectAssetPipelineResult::Pending;
                const assetStreaming::ProjectAssetAutomaticRecookStatus recook = projectAssets.GetAutomaticRecookStatus();
                if (attempt % 64u == 0u)
                {
                    const assetStreaming::ProjectAssetState* const accepted = pipeline->LookupAcceptedAsset(localImportedAsset);
                    const assetStreaming::ProjectAssetObservationStatus observation = pipeline->GetObservationStatus();
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] real GLB cohort tick=%u result=%s failure=%s build=%s outputIndex=%u acceptedGeneration=%llu acceptedOutputs=%u observation=%u rescan=%u pending=%u recook=%u activeRoots=%u pendingOutputs=%u retry=%u\n",
                                 attempt, assetStreaming::ToString(cohortResult), assetStreaming::ToString(cohortFailure.result),
                                 assets::ToString(cohortFailure.build), cohortFailure.buildRequestOutputIndex,
                                 static_cast<unsigned long long>(accepted != nullptr ? accepted->acceptedGeneration : 0u),
                                 accepted != nullptr ? accepted->outputs.Count() : 0u,
                                 static_cast<unsigned>(observation.running), static_cast<unsigned>(observation.rescanRequired),
                                 static_cast<unsigned>(observation.reconcilePending), static_cast<unsigned>(recook.state),
                                 recook.activeRoots, recook.pendingOutputs, recook.retryEntries);
                }
                if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                    recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                {
                    if (localRecoveryRetried)
                    {
                        std::fprintf(stderr,
                                     "[editorAssetRetirementTests] real GLB terminal recook persisted after one explicit retry: state=%u dirty=0x%x roots=%u finished=%u retries=%u\n",
                                     static_cast<unsigned>(recook.state), static_cast<unsigned>(recook.dirtyReasons),
                                     recook.activeRoots, recook.completedRoots, recook.retryEntries);
                        return FailStep("model-import-local-glb-recovery-terminal");
                    }
                    assetStreaming::ProjectAssetPipelineFailure recoveryFailure;
                    const auto retry = projectAssets.RetryAutomaticRecook(&recoveryFailure);
                    std::fprintf(stderr, "[editorAssetRetirementTests] real GLB coordinator recovery: result=%s failure=%s build=%s\n",
                                 assetStreaming::ToString(retry), assetStreaming::ToString(recoveryFailure.result),
                                 assets::ToString(recoveryFailure.build));
                    if (retry != assetStreaming::ProjectAssetPipelineResult::Success &&
                        retry != assetStreaming::ProjectAssetPipelineResult::Pending)
                        return FailStep("model-import-local-glb-recovery-retry");
                    localRecoveryRetried = true;
                }
                if (cohortResult == assetStreaming::ProjectAssetPipelineResult::Success)
                {
                    localCohortAdmitted = true;
                    break;
                }
                if (cohortResult == assetStreaming::ProjectAssetPipelineResult::BuildRequestFailed)
                {
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] real GLB cohort BuildRequestFailed: reason=%s outputIndex=%u path=%llu type=%llu\n",
                                 assets::ToString(cohortFailure.build), cohortFailure.buildRequestOutputIndex,
                                 static_cast<unsigned long long>(cohortFailure.buildRequestOutput.GetPath().Id()),
                                 static_cast<unsigned long long>(cohortFailure.buildRequestOutput.ExpectedType()));
                    return FailStep("model-import-local-glb-cohort-build-request");
                }
                if (cohortResult != assetStreaming::ProjectAssetPipelineResult::Busy &&
                    cohortResult != assetStreaming::ProjectAssetPipelineResult::Pending &&
                    cohortResult != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
                    return FailStep("model-import-local-glb-cohort-admission");
                if (!RunFrame(*frames, clock))
                    return FailStep("model-import-local-glb-cohort-frame");
                vanguard::concurrency::SleepOnCurrentThread(1);
            }
            if (!localCohortAdmitted)
                return FailStep("model-import-local-glb-cohort-timeout");
            std::fprintf(stderr, "[editorAssetRetirementTests] real GLB cohort admitted elapsed_ms=%lld\n",
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - postCommitStart).count()));
            // Admission alone is not enough: generated VMAT/VPROG/VPIPE,
            // embedded VTEX and VMESH outputs must all publish coherently.
            // This stays inside the disposable workspace, not Project Cardinal.
            bool localCohortPublished = false;
            for (vanguard::u32 attempt = 0u; attempt < 20'000u; ++attempt)
            {
                vanguard::u32 installed = 0u;
                for (const assets::AssetOutput& authored : localPlan.GetImportDescription().outputs)
                {
                    const resources::ResourceReference resource = assets::GetAssetOutputReference(localImportedAsset, authored);
                    assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
                    if (!projectAssets.GetOutputDeliveryStatus(resource, delivery) ||
                        delivery.publicationState != assetStreaming::ProjectAssetDeliveryStageState::Installed ||
                        delivery.publishedBuildFingerprint.IsEmpty() || delivery.publishedContentFingerprint.IsEmpty() ||
                        !delivery.publishedPhysicalPath.IsFilePath() || delivery.publicationRetryRequired)
                        continue;
                    ++installed;
                }
                if (installed == localPlan.GetImportDescription().outputs.Size())
                {
                    localCohortPublished = true;
                    break;
                }
                const auto recook = projectAssets.GetAutomaticRecookStatus();
                if (attempt % 256u == 0u)
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] real GLB publishing tick=%u installed=%u/%u state=%u dirty=0x%x roots=%u finished=%u buildOps=%u/%u delivery=%u elapsed_ms=%lld\n",
                                 attempt, installed, localPlan.GetImportDescription().outputs.Size(),
                                 static_cast<unsigned>(recook.state), static_cast<unsigned>(recook.dirtyReasons),
                                 recook.activeRoots, recook.completedRoots, recook.completedBuildOperations,
                                 recook.buildOperations, recook.pendingDeliveryOutputs,
                                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - postCommitStart).count()));
                if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                    recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                    return FailStep("model-import-local-glb-publish-failed");
                if (!RunFrame(*frames, clock))
                    return FailStep("model-import-local-glb-publish-frame");
                vanguard::concurrency::SleepOnCurrentThread(1);
            }
            if (!localCohortPublished)
                return FailStep("model-import-local-glb-publish-timeout");
            std::fprintf(stderr, "[editorAssetRetirementTests] real GLB all %u outputs installed\n",
                         localPlan.GetImportDescription().outputs.Size());
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] real GLB timing postcommit_to_installed_ms=%lld total_workflow_ms=%lld\n",
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - postCommitStart).count()),
                         static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - realGlbWorkflowStart).count()));
            char deletionProof[8]{};
            size_t deletionProofLength = 0u;
            if (getenv_s(&deletionProofLength, deletionProof, sizeof(deletionProof),
                         "VANGUARD_EDITOR_REAL_GLB_DELETE_PROOF") == 0 &&
                deletionProofLength > 1u && deletionProof[0] == '1')
            {
                if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
                    return FailStep("model-import-local-glb-delete-ready");
                // The disposable observer fixture uses an external filesystem
                // deletion so it can exercise the accepted Missing transition
                // without conflating it with asset-operation journal limits.
                if (!filesystem::GetManager().DeleteFile(destination) ||
                    !filesystem::GetManager().DeleteFile(MetadataPath(destination)))
                    return FailStep("model-import-local-glb-external-delete");
                bool deletionSettled = false;
                for (vanguard::u32 attempt = 0u; attempt < 12'000u; ++attempt)
                {
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-delete-frame");
                    const auto recook = projectAssets.GetAutomaticRecookStatus();
                    if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                    {
                        std::fprintf(stderr, "[editorAssetRetirementTests] real GLB scratch delete recook FAILED: reason=%s diagnostics=%u root=%u\n",
                                     assets::ToString(recook.lastRecookFailure), recook.pendingDiagnostics,
                                     recook.firstFailedRoot.rootIndex);
                        return FailStep("model-import-local-glb-delete-recook-failed");
                    }
                    const auto* const accepted = pipeline->LookupAcceptedAsset(localImportedAsset);
                    if (accepted != nullptr && accepted->presence == assetStreaming::ProjectAssetPresence::Missing &&
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Idle &&
                        !filesystem::RawFileReader::Create(destination) &&
                        !filesystem::RawFileReader::Create(MetadataPath(destination)))
                    {
                        deletionSettled = true;
                        break;
                    }
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                if (!deletionSettled)
                    return FailStep("model-import-local-glb-delete-settle");
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB scratch delete recook settled without a missing-root failure\n");

                // Re-review after the accepted tombstone. A plan from the
                // first import would be stale and must not be recycled here.
                const auto* const deleted = pipeline->LookupAcceptedAsset(localImportedAsset);
                if (deleted == nullptr || deleted->presence != assetStreaming::ProjectAssetPresence::Missing)
                    return FailStep("model-import-local-glb-reimport-tombstone");
                const vanguard::u64 deletedGeneration = deleted->acceptedGeneration;
                if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock) ||
                    !DriveUntilExternalImportDestinationReady(projectAssets, 0u, {}, *frames, clock))
                    return FailStep("model-import-local-glb-reimport-ready");
                vanguard::mesh_tools::ModelImportPlan reimportPlan;
                vanguard::mesh_tools::ModelImportPlanReport reimportReport;
                assetStreaming::ProjectAssetPipelineFailure reimportFailure;
                assets::AssetId reimportPlannedAsset;
                const auto reimportPrepared = projectAssets.PrepareModelImportPlan(
                    localSource, {localBytes.TypedData(), localBytes.Size()}, 0u, {}, "lieutenantHead.glb", {},
                    0u, 0u, localManifest, localSettings, reimportPlan, &reimportReport,
                    reimportPlannedAsset, &reimportFailure);
                if (reimportPrepared != assetStreaming::ProjectAssetPipelineResult::Success ||
                    reimportPlan.HasBlockingConflicts() ||
                    reimportPlan.GetImportDescription().outputs.Size() != 31u)
                {
                    std::fprintf(stderr, "[editorAssetRetirementTests] real GLB re-review failed: result=%s blockers=%u diagnostic=%s\n",
                                 assetStreaming::ToString(reimportPrepared), reimportReport.blockers,
                                 reimportReport.diagnostic.AsChar());
                    return FailStep("model-import-local-glb-reimport-plan");
                }
                assets::AssetId reimportedAsset;
                auto reimportCommit = projectAssets.CommitModelImportPlan(
                    reimportPlan, {localBytes.TypedData(), localBytes.Size()}, reimportedAsset, &reimportFailure);
                for (vanguard::u32 retry = 0u;
                     retry < 8u && reimportCommit == assetStreaming::ProjectAssetPipelineResult::Busy; ++retry)
                {
                    const auto observation = pipeline->GetObservationStatus();
                    const auto recook = projectAssets.GetAutomaticRecookStatus();
                    const auto deferred = pipeline->GetDeferredPlannedImportStatus();
                    std::fprintf(stderr, "[editorAssetRetirementTests] real GLB reimport busy retry=%u observation=%u rescan=%u reconcile=%u recook=%u deferred=%u\n",
                                 retry, static_cast<unsigned>(observation.running),
                                 static_cast<unsigned>(observation.rescanRequired),
                                 static_cast<unsigned>(observation.reconcilePending),
                                 static_cast<unsigned>(recook.state), static_cast<unsigned>(deferred.state));
                    if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
                        return FailStep("model-import-local-glb-reimport-busy-ready");
                    // CommitPlannedImport performs its full authoritative
                    // precommit validation on each attempt, never stale apply.
                    for (vanguard::u32 attempt = 0u; attempt < 32u; ++attempt)
                        if (!RunFrame(*frames, clock))
                            return FailStep("model-import-local-glb-reimport-busy-frame");
                    reimportFailure = {};
                    reimportCommit = projectAssets.CommitModelImportPlan(
                        reimportPlan, {localBytes.TypedData(), localBytes.Size()}, reimportedAsset, &reimportFailure);
                }
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB reimport commit result=%s plannedOutputs=%u\n",
                             assetStreaming::ToString(reimportCommit),
                             reimportPlan.GetImportDescription().outputs.Size());
                if (reimportCommit != assetStreaming::ProjectAssetPipelineResult::Pending &&
                    reimportCommit != assetStreaming::ProjectAssetPipelineResult::Success &&
                    reimportCommit != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
                    reimportCommit != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
                    return FailStep("model-import-local-glb-reimport-commit");

                bool reimportAdmitted = false;
                vanguard::u64 reimportGeneration = 0u;
                for (vanguard::u32 attempt = 0u; attempt < 16'000u; ++attempt)
                {
                    const auto deferred = pipeline->GetDeferredPlannedImportStatus();
                    if (deferred.asset == reimportPlannedAsset &&
                        deferred.state == assetStreaming::ProjectAssetDeferredPlannedImportState::ContinuationFailed)
                    {
                        std::fprintf(stderr, "[editorAssetRetirementTests] real GLB reimport continuation failed: result=%s build=%s index=%u\n",
                                     assetStreaming::ToString(deferred.result), assets::ToString(deferred.build),
                                     deferred.buildRequestOutputIndex);
                        return FailStep("model-import-local-glb-reimport-continuation");
                    }
                    const auto recook = projectAssets.GetAutomaticRecookStatus();
                    if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                    {
                        std::fprintf(stderr, "[editorAssetRetirementTests] real GLB reimport recook failed: reason=%s diagnostics=%u\n",
                                     assets::ToString(recook.lastRecookFailure), recook.pendingDiagnostics);
                        return FailStep("model-import-local-glb-reimport-recook-failed");
                    }
                    const auto* const accepted = pipeline->LookupAcceptedAsset(reimportPlannedAsset);
                    if (accepted != nullptr && accepted->presence == assetStreaming::ProjectAssetPresence::Present &&
                        accepted->acceptedGeneration > deletedGeneration && accepted->outputs.Count() == 31u)
                    {
                        reimportGeneration = accepted->acceptedGeneration;
                        if (deferred.asset == reimportPlannedAsset &&
                            deferred.state == assetStreaming::ProjectAssetDeferredPlannedImportState::CohortQueued &&
                            deferred.acceptedGeneration == reimportGeneration)
                        {
                            reimportAdmitted = true;
                            break;
                        }
                        if (reimportCommit == assetStreaming::ProjectAssetPipelineResult::Success ||
                            reimportCommit == assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending)
                        {
                            reimportAdmitted = true;
                            break;
                        }
                    }
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-reimport-admission-frame");
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                if (!reimportAdmitted)
                    return FailStep("model-import-local-glb-reimport-admission-timeout");

                bool reimportPublished = false;
                for (vanguard::u32 attempt = 0u; attempt < 20'000u; ++attempt)
                {
                    vanguard::u32 installed = 0u;
                    for (const assets::AssetOutput& authored : reimportPlan.GetImportDescription().outputs)
                    {
                        const resources::ResourceReference resource =
                            assets::GetAssetOutputReference(reimportPlannedAsset, authored);
                        assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
                        if (projectAssets.GetOutputDeliveryStatus(resource, delivery) &&
                            delivery.authoredGeneration == reimportGeneration &&
                            delivery.publicationState == assetStreaming::ProjectAssetDeliveryStageState::Installed &&
                            !delivery.publishedBuildFingerprint.IsEmpty() &&
                            !delivery.publishedContentFingerprint.IsEmpty() &&
                            delivery.publishedPhysicalPath.IsFilePath() && !delivery.publicationRetryRequired)
                            ++installed;
                    }
                    if (installed == 31u)
                    {
                        reimportPublished = true;
                        break;
                    }
                    const auto recook = projectAssets.GetAutomaticRecookStatus();
                    if (attempt % 512u == 0u)
                        std::fprintf(stderr, "[editorAssetRetirementTests] real GLB reimport publication tick=%u installed=%u/31 state=%u roots=%u finished=%u\n",
                                     attempt, installed, static_cast<unsigned>(recook.state),
                                     recook.activeRoots, recook.completedRoots);
                    if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                        return FailStep("model-import-local-glb-reimport-publish-failed");
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-reimport-publish-frame");
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                if (!reimportPublished)
                    return FailStep("model-import-local-glb-reimport-publish-timeout");
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB scratch reimport published all 31 current-generation outputs\n");

                // Also exercise the actual Asset Browser Delete transaction,
                // not just an external watcher event. Both tests use only the
                // disposable workspace and preserve the external source.
                if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
                    return FailStep("model-import-local-glb-operation-delete-ready");
                assetStreaming::ProjectAssetPipelineFailure operationDeleteFailure;
                const auto operationDelete = pipeline->DeleteAsset(
                    reimportPlannedAsset, {}, nullptr, &operationDeleteFailure);
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB scratch operation delete result=%s operation=%u transaction=%u\n",
                             assetStreaming::ToString(operationDelete),
                             static_cast<unsigned>(operationDeleteFailure.operation.result),
                             static_cast<unsigned>(operationDeleteFailure.operation.transaction));
                if (operationDelete != assetStreaming::ProjectAssetPipelineResult::Success &&
                    operationDelete != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
                    operationDelete != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
                    return FailStep("model-import-local-glb-operation-delete");
                bool operationDeleteSettled = false;
                for (vanguard::u32 attempt = 0u; attempt < 12'000u; ++attempt)
                {
                    if (!RunFrame(*frames, clock))
                        return FailStep("model-import-local-glb-operation-delete-frame");
                    const auto recook = projectAssets.GetAutomaticRecookStatus();
                    if (recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
                        return FailStep("model-import-local-glb-operation-delete-recook-failed");
                    const auto* const accepted = pipeline->LookupAcceptedAsset(reimportPlannedAsset);
                    if (accepted != nullptr && accepted->presence == assetStreaming::ProjectAssetPresence::Missing &&
                        accepted->acceptedGeneration > reimportGeneration &&
                        recook.state == assetStreaming::ProjectAssetAutomaticRecookState::Idle &&
                        !filesystem::RawFileReader::Create(destination) &&
                        !filesystem::RawFileReader::Create(MetadataPath(destination)))
                    {
                        operationDeleteSettled = true;
                        break;
                    }
                    vanguard::concurrency::SleepOnCurrentThread(1);
                }
                if (!operationDeleteSettled)
                    return FailStep("model-import-local-glb-operation-delete-settle");
                std::fprintf(stderr, "[editorAssetRetirementTests] real GLB scratch Asset Browser delete settled without recook failure\n");
            }
        }

        if (localGlbLength > 1u)
        {
            if (!host.Shutdown(&hostFailure) || rhi::IsInitialized())
                return FailStep("model-import-local-glb-quiesce");
            std::fprintf(stderr, "[editorAssetRetirementTests] isolated real GLB cohort admission/publication passed; original asset unchanged\n");
            return true;
        }

        if (!DriveUntilExternalImportDestinationReady(
                projectAssets, 0u, vanguard::containers::StringView("Textures"), *frames, clock) ||
            !DriveUntilExternalImportDestinationReady(
                projectAssets, 0u, vanguard::containers::StringView("StaleDestination"), *frames, clock))
            return FailStep("p0-empty-folder-import-destination-visible");

        const filesystem::AbsolutePath stalePhysicalFolder = workspace->GetAssetsRoot().AddDirPath("StaleDestination");
        std::error_code staleFolderRemoveError;
        const bool staleFolderRemoved = std::filesystem::remove(
            std::filesystem::path(stalePhysicalFolder.AsChar()), staleFolderRemoveError);
        if (staleFolderRemoveError || !staleFolderRemoved)
            return FailStep("p0-stale-empty-folder-remove");
        editor::EditorProjectExternalImportFailure removedDestinationFailure;
        assets::AssetId removedDestinationAsset;
        if (projectAssets.ImportExternalSource(
                0u, vanguard::containers::StringView("StaleDestination"),
                vanguard::containers::StringView("must_not_exist.png"), {RedPng, sizeof(RedPng)},
                removedDestinationAsset, &removedDestinationFailure) !=
                assetStreaming::ProjectAssetPipelineResult::OperationFailed ||
            removedDestinationFailure.code != editor::EditorProjectExternalImportFailureCode::StaleDestination ||
            removedDestinationAsset.IsValid() ||
            filesystem::RawFileReader::Create(stalePhysicalFolder.AddFilePath("must_not_exist.png")))
            return FailStep("p0-removed-empty-folder-physical-revalidation");

        editor::EditorProjectExternalImportFailure emptyFolderImportFailure;
        assets::AssetId emptyFolderImportedAsset;
        const assetStreaming::ProjectAssetPipelineResult emptyFolderImportResult = projectAssets.ImportExternalSource(
            0u, vanguard::containers::StringView("Textures"),
            vanguard::containers::StringView("empty_folder_import.png"), {RedPng, sizeof(RedPng)},
            emptyFolderImportedAsset, &emptyFolderImportFailure);
        if ((emptyFolderImportResult != assetStreaming::ProjectAssetPipelineResult::Success &&
             emptyFolderImportResult != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
             emptyFolderImportResult != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) ||
            !emptyFolderImportedAsset.IsValid())
            return FailStep("p0-empty-folder-import-commit");
        const filesystem::AbsolutePath emptyFolderImportedSource =
            workspace->GetAssetsRoot().AddDirPath("Textures").AddFilePath("empty_folder_import.png");
        auto emptyFolderImportedReader = filesystem::RawFileReader::Create(emptyFolderImportedSource);
        vanguard::u8 emptyFolderImportedBytes[sizeof(RedPng)]{};
        if (!emptyFolderImportedReader || emptyFolderImportedReader->GetSize() != sizeof(RedPng) ||
            !filesystem::RawFileReader::Create(MetadataPath(emptyFolderImportedSource)))
            return FailStep("p0-empty-folder-import-durable-files");
        emptyFolderImportedReader->Serialize(emptyFolderImportedBytes, sizeof(emptyFolderImportedBytes));
        if (emptyFolderImportedReader->HasErrors() ||
            std::memcmp(emptyFolderImportedBytes, RedPng, sizeof(RedPng)) != 0)
            return FailStep("p0-empty-folder-import-exact-source");
        if (!DriveUntilExternalImportDestinationReady(projectAssets, 0u, {}, *frames, clock))
            return FailStep("p0-post-empty-folder-import-catalog-settle");

        std::error_code externalDirectoryError;
        const filesystem::AbsolutePath externalRoot = root.AddDirPath("ExternalImport");
        static_cast<void>(std::filesystem::create_directories(std::filesystem::path(externalRoot.AsChar()),
                                                              externalDirectoryError));
        const filesystem::AbsolutePath externalSource = externalRoot.AddFilePath("external_import.png");
        if (externalDirectoryError || !WriteRawFile(externalSource, RedPng, sizeof(RedPng)))
            return FailStep("p0-external-input-fixture");
        editor::EditorProjectExternalImportFailure staleDestinationFailure;
        assets::AssetId staleDestinationAsset;
        if (projectAssets.ImportExternalSource(
                0u, vanguard::containers::StringView("folder-that-does-not-exist"),
                vanguard::containers::StringView("stale.png"), {RedPng, sizeof(RedPng)},
                staleDestinationAsset, &staleDestinationFailure) !=
                assetStreaming::ProjectAssetPipelineResult::OperationFailed ||
            staleDestinationFailure.code != editor::EditorProjectExternalImportFailureCode::StaleDestination ||
            staleDestinationAsset.IsValid())
            return FailStep("p0-external-import-stale-destination-revalidation");
        editor::EditorProjectExternalImportFailure missingRootFailure;
        assets::AssetId missingRootAsset;
        if (projectAssets.ImportExternalSource(
                1u, {}, vanguard::containers::StringView("missing_root.png"), {RedPng, sizeof(RedPng)},
                missingRootAsset, &missingRootFailure) !=
                assetStreaming::ProjectAssetPipelineResult::OperationFailed ||
            missingRootFailure.code != editor::EditorProjectExternalImportFailureCode::StaleDestination ||
            missingRootAsset.IsValid())
            return FailStep("p0-external-import-missing-root-revalidation");
        editor::EditorProjectExternalImportFailure externalImportFailure;
        assets::AssetId externalImportedAsset;
        const assetStreaming::ProjectAssetPipelineResult externalImportResult = projectAssets.ImportExternalSource(
            0u, {}, vanguard::containers::StringView("external_import.png"), {RedPng, sizeof(RedPng)},
            externalImportedAsset, &externalImportFailure);
        if ((externalImportResult != assetStreaming::ProjectAssetPipelineResult::Success &&
             externalImportResult != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
             externalImportResult != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) ||
            !externalImportedAsset.IsValid())
            return FailStep("p0-external-import-commit");
        const filesystem::AbsolutePath importedSource = workspace->GetAssetsRoot().AddFilePath("external_import.png");
        auto importedReader = filesystem::RawFileReader::Create(importedSource);
        vanguard::u8 importedBytes[sizeof(RedPng)]{};
        if (!importedReader || importedReader->GetSize() != sizeof(RedPng))
            return FailStep("p0-external-import-source-exists");
        importedReader->Serialize(importedBytes, sizeof(importedBytes));
        const assets::SourceRecord* const importedRecord = workspace->GetSources().LookupAsset(externalImportedAsset);
        if (importedReader->HasErrors() || std::memcmp(importedBytes, RedPng, sizeof(RedPng)) != 0 ||
            !filesystem::RawFileReader::Create(MetadataPath(importedSource)) || importedRecord == nullptr)
            return FailStep("p0-external-import-durable-truth");
        filesystem::AbsolutePath resolvedImportedSource;
        const vanguard::u64 importedSourceGeneration = workspace->GetSources().GetGeneration();
        if (!projectAssets.ResolveSourcePhysicalPath(importedRecord->sourceIdentity, importedSourceGeneration,
                                                     resolvedImportedSource) ||
            resolvedImportedSource != importedSource ||
            projectAssets.ResolveSourcePhysicalPath(importedRecord->sourceIdentity, importedSourceGeneration + 1u,
                                                    resolvedImportedSource))
            return FailStep("p0-source-location-generation-fence");
        if (!projectAssets.IsManagedProjectSourcePath(importedSource) ||
            projectAssets.IsManagedProjectSourcePath(externalSource))
            return FailStep("p0-managed-source-coordinator-preflight");
        if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
            return FailStep("p0-reimport-mutation-ready");
        vanguard::u64 importedAcceptedGeneration = 0u;
        if (!projectAssets.GetAcceptedAssetGeneration(externalImportedAsset, importedAcceptedGeneration) ||
            importedAcceptedGeneration == 0u)
            return FailStep("p0-reimport-accepted-generation");
        assetStreaming::ProjectAssetPipelineFailure reimportFailure;
        const assetStreaming::ProjectAssetPipelineResult reimportResult =
            projectAssets.RequestAssetReimport(externalImportedAsset, importedAcceptedGeneration,
                                               workspace->GetSources().GetGeneration(), &reimportFailure);
        if (reimportResult != assetStreaming::ProjectAssetPipelineResult::Success)
            return FailStep("p0-reimport-request");
        if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
            return FailStep("p0-reimport-complete");
        const assets::SourceRecord* const reimportedRecord = workspace->GetSources().LookupAsset(externalImportedAsset);
        if (reimportedRecord == nullptr || reimportedRecord->path != importedSource ||
            reimportedRecord->metadata.id != externalImportedAsset)
            return FailStep("p0-reimport-project-source-truth");

        editor::ProjectTexturePreviewHandle background;
        editor::ProjectTexturePreviewHandle focused;
        editor::ProjectTexturePreviewRequest backgroundRequest;
        backgroundRequest.priority = editor::ProjectTexturePreviewPriority::Background;
        backgroundRequest.quality = {1u, 1u};
        editor::ProjectTexturePreviewRequest focusedRequest;
        focusedRequest.priority = editor::ProjectTexturePreviewPriority::Focused;
        focusedRequest.quality = {256u, 192u};
        if (!projectAssets.RequestTexturePreview(output, backgroundRequest, background) ||
            !projectAssets.RequestTexturePreview(output, focusedRequest, focused) ||
            !background.IsValid() || !focused.IsValid() || background == focused ||
            background.ownerCookie != focused.ownerCookie ||
            !projectAssets.UpdateTexturePreviewPriority(background, editor::ProjectTexturePreviewPriority::Interactive))
            return FailStep("preview-shared-interest-request");

        editor::ProjectTexturePreviewInfo backgroundInfo;
        editor::ProjectTexturePreviewInfo focusedInfo;
        const bool backgroundReady = DrivePreviewUntilReady(projectAssets, background, *frames, clock, backgroundInfo);
        const bool focusedReady = backgroundReady && DrivePreviewUntilReady(projectAssets, focused, *frames, clock, focusedInfo);
        if (!backgroundReady || !focusedReady ||
            backgroundInfo.resource != output || focusedInfo.resource != output ||
            backgroundInfo.resourceGeneration == 0u || backgroundInfo.resourceGeneration != focusedInfo.resourceGeneration ||
            backgroundInfo.buildFingerprint != statusA.publishedBuildFingerprint ||
            backgroundInfo.contentFingerprint != statusA.publishedContentFingerprint ||
            focusedInfo.buildFingerprint != backgroundInfo.buildFingerprint ||
            focusedInfo.contentFingerprint != backgroundInfo.contentFingerprint ||
            backgroundInfo.width != 1u || backgroundInfo.height != 1u ||
            !backgroundInfo.qualitySatisfied || !focusedInfo.qualitySatisfied ||
            backgroundInfo.presentationRevision == 0u ||
            backgroundInfo.presentationRevision != focusedInfo.presentationRevision)
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] preview-ready-a detail: backgroundReady=%u focusedReady=%u bgState=%u fgState=%u bgGeneration=%u fgGeneration=%u bgSize=%ux%u bgBuildMatch=%u bgContentMatch=%u fgBuildMatch=%u fgContentMatch=%u\n",
                         backgroundReady ? 1u : 0u, focusedReady ? 1u : 0u,
                         static_cast<unsigned>(backgroundInfo.state), static_cast<unsigned>(focusedInfo.state),
                         backgroundInfo.resourceGeneration, focusedInfo.resourceGeneration,
                         backgroundInfo.width, backgroundInfo.height,
                         backgroundInfo.buildFingerprint == statusA.publishedBuildFingerprint ? 1u : 0u,
                         backgroundInfo.contentFingerprint == statusA.publishedContentFingerprint ? 1u : 0u,
                         focusedInfo.buildFingerprint == backgroundInfo.buildFingerprint ? 1u : 0u,
                         focusedInfo.contentFingerprint == backgroundInfo.contentFingerprint ? 1u : 0u);
            return FailStep("preview-ready-a");
        }

        editor::EditorProjectTexturePreviewPresentationAccess previewPresentation =
            editor::detail::EditorProjectAssetTestAccess::GetPreviewPresentation(*workspace);
        PreviewUiService previewUi;
        rhi::Failure samplerFailure;
        rhi::SamplerStateRef previewSampler = rhi::RequestSamplerState({}, &samplerFailure);
        editor::EditorTextureHandle previewTexture;
        if (!previewSampler.IsValid() ||
            !previewPresentation.Register(background, previewUi, previewSampler,
                                          editor::EditorTextureColorSpace::DisplaySrgb, previewTexture) ||
            !previewTexture.IsValid() || !previewUi.HasRegisteredTexture() ||
            !previewUi.RegisteredDesc().texture.IsValid() ||
            previewUi.RegisteredDesc().colorSpace != editor::EditorTextureColorSpace::DisplaySrgb)
            return FailStep("preview-ui-registration");

        rendering::TextureResidencyRuntime& runtime = renderingService->GetTextureResidency();
        rendering::TextureRuntimeInfo runtimeInfoA;
        if (runtime.GetStats().liveDemands != 1u || runtime.GetStats().residencyRecords != 1u ||
            runtime.FindDemandedResource(output.GetPath(), runtimeInfoA) != rendering::TextureResidentLookupResult::Found ||
            runtimeInfoA.resourceGeneration != backgroundInfo.resourceGeneration ||
            runtimeInfoA.demandCount != 1u ||
            runtimeInfoA.demandPriority != rendering::TextureDemandPriority::Focused ||
            !runtimeInfoA.qualitySatisfied)
            return FailStep("preview-shared-backing-demand");
        const vanguard::u32 generationA = backgroundInfo.resourceGeneration;

        if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock))
            return FailStep("preview-mutation-ready");
        vanguard::u64 acceptedGeneration = 0u;
        const vanguard::u64 sourceGeneration = workspace->GetSources().GetGeneration();
        if (sourceGeneration == 0u ||
            !projectAssets.GetAcceptedAssetGeneration(metadata.id, acceptedGeneration) ||
            !projectAssets.IsSourceGenerationCurrent(sourceGeneration))
            return FailStep("preview-mutation-generation");
        assetStreaming::ProjectAssetPipelineFailure mutationFailure;
        const assetStreaming::ProjectAssetPipelineResult mutation = projectAssets.UpdateAssetSettings(
            metadata.id, acceptedGeneration, sourceGeneration, metadata.settingsVersion,
            {replacementSettings.TypedData(), replacementSettings.Size()}, &mutationFailure);
        if (mutation != assetStreaming::ProjectAssetPipelineResult::Success &&
            mutation != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
            mutation != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
            return FailStep("preview-settings-b-update");
        assetStreaming::ProjectAssetOutputDeliveryStatus statusB;
        if (!DriveUntilPreviewPublication(*pipeline, output, *frames, clock, statusB, &statusA.publishedBuildFingerprint) ||
            statusB.publishedPhysicalPath == statusA.publishedPhysicalPath)
            return FailStep("preview-workspace-publish-b");

        editor::ProjectTexturePreviewHandle replacement;
        if (!projectAssets.RequestTexturePreview(output, editor::ProjectTexturePreviewPriority::Focused, replacement) ||
            !replacement.IsValid())
            return FailStep("preview-replacement-request");
        editor::ProjectTexturePreviewInfo replacementInfo;
        bool sawGenerationReacquire = false;
        if (!DrivePreviewUntilReady(projectAssets, replacement, *frames, clock, replacementInfo, &sawGenerationReacquire) ||
            !sawGenerationReacquire ||
            replacementInfo.resourceGeneration == 0u || replacementInfo.resourceGeneration == generationA ||
            replacementInfo.buildFingerprint != statusB.publishedBuildFingerprint ||
            replacementInfo.contentFingerprint != statusB.publishedContentFingerprint)
            return FailStep("preview-replacement-ready");
        rendering::TextureRuntimeInfo runtimeInfoB;
        if (runtime.GetStats().liveDemands != 2u || runtime.GetStats().residencyRecords != 1u ||
            runtime.FindDemandedResource(output.GetPath(), runtimeInfoB) != rendering::TextureResidentLookupResult::Found ||
            runtimeInfoB.resourceGeneration != replacementInfo.resourceGeneration ||
            runtimeInfoB.resourceGeneration == runtimeInfoA.resourceGeneration ||
            runtimeInfoB.replacementState != rendering::TextureReplacementState::None)
            return FailStep("preview-workspace-replacement-current");

        // Two logical A interests share one backing/demand. Releasing the first
        // must leave that owner alive; the second carries the captured-frame fence
        // and keeps the old generation retained until a later workspace frame.
        const vanguard::u64 capturedUiFrame = frames->GetStats().frames != 0u ? frames->GetStats().frames - 1u : 0u;
        editor::EditorUiFailure unregisterFailure;
        if (capturedUiFrame == 0u || !previewUi.UnregisterTexture(previewTexture, &unregisterFailure) ||
            !projectAssets.ReleaseTexturePreview(background, capturedUiFrame) || background.IsValid() ||
            runtime.GetStats().liveDemands != 2u || !projectAssets.QueryTexturePreview(focused, focusedInfo) ||
            focusedInfo.state != editor::ProjectTexturePreviewState::Ready ||
            !projectAssets.ReleaseTexturePreview(focused, 0u) || focused.IsValid() || runtime.GetStats().liveDemands != 2u)
            return FailStep("preview-captured-frame-release");
        if (!RunFrame(*frames, clock) || runtime.GetStats().liveDemands != 1u)
            return FailStep("preview-retirement-after-later-frame");
        static_cast<void>(rhi::SafeRelease(previewSampler));

        // Exercise the real reviewed model-import seam through the same production
        // EditorProjectAssetAccess/ProjectAssetPipeline owned by this workspace.
        // Planning must remain disposable until the explicit commit below.
        constexpr char reviewedModel[] = R"json({
            "asset":{"version":"2.0"},
            "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AACAPwAAAAAAAIA/AAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAwAEAAUA","byteLength":132}],
            "bufferViews":[
                {"buffer":0,"byteOffset":0,"byteLength":72,"target":34962},
                {"buffer":0,"byteOffset":72,"byteLength":48,"target":34962},
                {"buffer":0,"byteOffset":120,"byteLength":6,"target":34963},
                {"buffer":0,"byteOffset":126,"byteLength":6,"target":34963}],
            "accessors":[
                {"bufferView":0,"componentType":5126,"count":6,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},
                {"bufferView":1,"componentType":5126,"count":6,"type":"VEC2"},
                {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"},
                {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
            "images":[
                {"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP4z8DwHwAFAAH/VscvDQAAAABJRU5ErkJggg=="},
                {"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYPj/HwADAgH/5ncLrgAAAABJRU5ErkJggg=="}],
            "textures":[{"source":0},{"source":1}],
            "materials":[
                {"name":"Body","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1},"metallicFactor":0.7,"roughnessFactor":0.4}},
                {"name":"Masked","alphaMode":"MASK","alphaCutoff":0.35,"pbrMetallicRoughness":{"baseColorFactor":[0.3,0.6,0.9,0.8]}}],
            "meshes":[{"name":"Compound","primitives":[
                {"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":0},
                {"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":3,"material":1}]}],
            "nodes":[{"name":"Root","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0
        })json";
        const vanguard::containers::ArraySpan<const vanguard::u8> reviewedSnapshot{
            reinterpret_cast<const vanguard::u8*>(reviewedModel),
            static_cast<vanguard::u32>(sizeof(reviewedModel) - 1u)};
        const filesystem::AbsolutePath reviewedExternalSource =
            root.AddDirPath("ExternalImport").AddFilePath("reviewed_model.gltf");
        const filesystem::AbsolutePath reviewedManagedSource =
            workspace->GetAssetsRoot().AddFilePath("reviewed_model.gltf");
        if (!WriteRawFile(reviewedExternalSource, reviewedSnapshot.Data(), reviewedSnapshot.Size()))
            return FailStep("model-import-external-fixture");

        vanguard::mesh_tools::ModelImportManifest modelManifest;
        vanguard::mesh_tools::ModelImportReport modelReport;
        const vanguard::mesh_tools::ModelImportResult inspected = vanguard::mesh_tools::InspectModelMemory(
            reviewedSnapshot.Data(), reviewedSnapshot.Size(), "gltf", reviewedExternalSource.AsStringView(),
            {}, {}, modelManifest, &modelReport);
        if (inspected != vanguard::mesh_tools::ModelImportResult::Success ||
            modelManifest.geometryDefinitions.Size() != 1u || modelManifest.materials.Size() < 2u ||
            modelManifest.materials.Size() > 4u ||
            modelManifest.embeddedTextures.Size() != 2u)
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] model import inspect failed: result=%s geometry=%u materials=%u embedded=%u diagnostic=%s\n",
                         vanguard::mesh_tools::ToString(inspected), modelManifest.geometryDefinitions.Size(),
                         modelManifest.materials.Size(), modelManifest.embeddedTextures.Size(),
                         modelReport.diagnostic.AsChar());
            return FailStep("model-import-inspection");
        }
        if (!DriveUntilAssetMutationReady(projectAssets, *frames, clock) ||
            !DriveUntilExternalImportDestinationReady(projectAssets, 0u, {}, *frames, clock))
            return FailStep("model-import-planning-ready");

        vanguard::mesh_tools::ModelImportSettings modelSettings;
        modelSettings.materialPolicy = vanguard::mesh_tools::ModelImportMaterialPolicy::ImportModelOwned;
        modelSettings.embeddedTexturePolicy = vanguard::mesh_tools::ModelImportEmbeddedTexturePolicy::ImportModelOwned;
        modelSettings.includeComposition = true;
        vanguard::mesh_tools::ModelImportPlan modelPlan;
        vanguard::mesh_tools::ModelImportPlanReport modelPlanReport;
        assetStreaming::ProjectAssetPipelineFailure modelPlanFailure;
        assets::AssetId provisionalModelAsset;
        const assetStreaming::ProjectAssetPipelineResult firstPlan = projectAssets.PrepareModelImportPlan(
            reviewedExternalSource, reviewedSnapshot, 0u, {}, "reviewed_model.gltf", {}, 0u, 0u,
            modelManifest, modelSettings, modelPlan, &modelPlanReport, provisionalModelAsset, &modelPlanFailure);
        if (firstPlan != assetStreaming::ProjectAssetPipelineResult::Success || !provisionalModelAsset.IsValid() ||
            modelPlan.HasBlockingConflicts() || modelPlan.GetImportDescription().outputs.Size() < 6u ||
            modelPlan.GetImportDescription().outputs.Size() > 8u ||
            filesystem::RawFileReader::Create(reviewedManagedSource) ||
            filesystem::RawFileReader::Create(MetadataPath(reviewedManagedSource)))
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] first model plan failed: result=%s failure=%s outputs=%u blockers=%u diagnostic=%s\n",
                         assetStreaming::ToString(firstPlan), assetStreaming::ToString(modelPlanFailure.result),
                         modelPlan.GetImportDescription().outputs.Size(), modelPlan.GetBlockers().Size(),
                         modelPlanReport.diagnostic.AsChar());
            return FailStep("model-import-nondurable-plan");
        }

        const assets::AssetId firstProvisionalModelAsset = provisionalModelAsset;
        vanguard::mesh_tools::ModelImportPlan reviewedModelPlan;
        vanguard::mesh_tools::ModelImportPlanReport reviewedModelPlanReport;
        modelPlanFailure = {};
        const assetStreaming::ProjectAssetPipelineResult replanned = projectAssets.PrepareModelImportPlan(
            reviewedExternalSource, reviewedSnapshot, 0u, {}, "reviewed_model.gltf", {}, 0u, 0u,
            modelManifest, modelSettings, reviewedModelPlan, &reviewedModelPlanReport, provisionalModelAsset,
            &modelPlanFailure);
        if (replanned != assetStreaming::ProjectAssetPipelineResult::Success ||
            provisionalModelAsset != firstProvisionalModelAsset || reviewedModelPlan.HasBlockingConflicts() ||
            filesystem::RawFileReader::Create(MetadataPath(reviewedManagedSource)))
            return FailStep("model-import-provisional-identity-stability");

        // An unrelated, no-op source-catalog refresh must not invalidate an
        // independently reviewed first-import model whose scoped source,
        // destination, accepted state and output identities remain unchanged.
        assetStreaming::ProjectAssetPipeline* const liveModelPipeline = workspace->GetAssetPipeline();
        const vanguard::u64 modelCatalogGeneration = workspace->GetSources().GetGeneration();
        const vanguard::u64 modelAcceptedGeneration = liveModelPipeline->GetAcceptedAssetGeneration();
        const auto modelRoots = workspace->GetSources().GetRoots();
        if (liveModelPipeline->Rescan(modelRoots) != assetStreaming::ProjectAssetPipelineResult::Success ||
            workspace->GetSources().GetGeneration() == modelCatalogGeneration ||
            liveModelPipeline->GetAcceptedAssetGeneration() != modelAcceptedGeneration)
            return FailStep("model-import-unrelated-catalog-refresh");

        // An unrelated settings mutation legitimately advances the accepted
        // generation after review without modifying model import facts.
        vanguard::u64 unrelatedAcceptedGeneration = 0u;
        assetStreaming::ProjectAssetPipelineFailure unrelatedFailure;
        if (!projectAssets.GetAcceptedAssetGeneration(metadata.id, unrelatedAcceptedGeneration))
            return FailStep("model-import-unrelated-asset-generation");
        const auto unrelatedUpdate = projectAssets.UpdateAssetSettings(
            metadata.id, unrelatedAcceptedGeneration, workspace->GetSources().GetGeneration(),
            metadata.settingsVersion, {metadata.settings.TypedData(), metadata.settings.Size()}, &unrelatedFailure);
        if ((unrelatedUpdate != assetStreaming::ProjectAssetPipelineResult::Success &&
             unrelatedUpdate != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
             unrelatedUpdate != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) ||
            liveModelPipeline->GetAcceptedAssetGeneration() <= modelAcceptedGeneration)
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] unrelated accepted update=%s old=%llu new=%llu\n",
                         assetStreaming::ToString(unrelatedUpdate),
                         static_cast<unsigned long long>(modelAcceptedGeneration),
                         static_cast<unsigned long long>(liveModelPipeline->GetAcceptedAssetGeneration()));
            return FailStep("model-import-unrelated-accepted-generation");
        }
        // Let the source observer consume the unrelated transaction's watcher
        // echo. A pending observation is independently a legitimate fail-closed
        // guard and must not be conflated with global-generation false staleness.
        bool observationSettled = false;
        for (vanguard::u32 attempt = 0u; attempt < 10'000u; ++attempt)
        {
            const assetStreaming::ProjectAssetObservationStatus observed = liveModelPipeline->GetObservationStatus();
            if (!observed.rescanRequired && !observed.reconcilePending)
            {
                observationSettled = true;
                break;
            }
            if (!RunFrame(*frames, clock))
                return FailStep("model-import-unrelated-observer-frame");
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        if (!observationSettled || !DriveUntilAssetMutationReady(projectAssets, *frames, clock))
            return FailStep("model-import-unrelated-observer-settle");

        assets::AssetId importedModelAsset;
        assetStreaming::ProjectAssetPipelineFailure modelCommitFailure;
        const assetStreaming::ProjectAssetPipelineResult modelCommitted = projectAssets.CommitModelImportPlan(
            reviewedModelPlan, reviewedSnapshot, importedModelAsset, &modelCommitFailure);
        if ((modelCommitted != assetStreaming::ProjectAssetPipelineResult::Success &&
             modelCommitted != assetStreaming::ProjectAssetPipelineResult::CommittedCleanupPending &&
             modelCommitted != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending) ||
            importedModelAsset != provisionalModelAsset)
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] model commit failed: result=%s failure=%s build=%s imported=%u identityMatch=%u staleSite=%u\n",
                         assetStreaming::ToString(modelCommitted), assetStreaming::ToString(modelCommitFailure.result),
                         assets::ToString(modelCommitFailure.build), importedModelAsset.IsValid() ? 1u : 0u,
                         importedModelAsset == provisionalModelAsset ? 1u : 0u,
                         modelCommitFailure.stalePlanSite);
            return FailStep("model-import-explicit-commit");
        }
        if (modelCommitted == assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
        {
            if (modelCommitFailure.result == assetStreaming::ProjectAssetPipelineResult::BuildRequestFailed)
            {
                std::fprintf(stderr,
                             "[editorAssetRetirementTests] model cohort admission failed after durable commit: build=%s\n",
                             assets::ToString(modelCommitFailure.build));
                return FailStep("model-import-cohort-admission");
            }
            bool resumed = false;
            for (vanguard::u32 attempt = 0u; attempt < 10'000u && !resumed; ++attempt)
            {
                if (!RunFrame(*frames, clock))
                    return FailStep("model-import-reconciliation-frame");
                assetStreaming::ProjectAssetPipelineFailure resumeFailure;
                const assetStreaming::ProjectAssetPipelineResult resume = projectAssets.ResumeCommittedModelImport(
                    reviewedModelPlan, importedModelAsset, 0u, &resumeFailure);
                resumed = resume == assetStreaming::ProjectAssetPipelineResult::Success;
                if (!resumed && resume != assetStreaming::ProjectAssetPipelineResult::CommittedReconciliationPending)
                {
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] model cohort resume failed: result=%s build=%s\n",
                                 assetStreaming::ToString(resume), assets::ToString(resumeFailure.build));
                    return FailStep("model-import-cohort-resume");
                }
                if (!resumed)
                    vanguard::concurrency::SleepOnCurrentThread(1);
            }
            if (!resumed)
                return FailStep("model-import-cohort-resume-timeout");
        }

        auto reviewedSourceReader = filesystem::RawFileReader::Create(reviewedManagedSource);
        vanguard::u8 reviewedDurableBytes[sizeof(reviewedModel) - 1u]{};
        if (!reviewedSourceReader || reviewedSourceReader->GetSize() != reviewedSnapshot.Size())
            return FailStep("model-import-durable-source");
        reviewedSourceReader->Serialize(reviewedDurableBytes, sizeof(reviewedDurableBytes));
        if (reviewedSourceReader->HasErrors() ||
            std::memcmp(reviewedDurableBytes, reviewedSnapshot.Data(), reviewedSnapshot.Size()) != 0)
            return FailStep("model-import-durable-source-snapshot");

        auto reviewedMetadataReader = filesystem::RawFileReader::Create(MetadataPath(reviewedManagedSource));
        if (!reviewedMetadataReader || reviewedMetadataReader->GetSize() == 0u ||
            reviewedMetadataReader->GetSize() > assets::MaximumAssetMetadataBytes)
            return FailStep("model-import-durable-metadata");
        vanguard::containers::DynamicArray<vanguard::u8> reviewedMetadataBytes(vanguard::memory::pools::Tools::GetInstance());
        reviewedMetadataBytes.Resize(static_cast<vanguard::u32>(reviewedMetadataReader->GetSize()));
        if (reviewedMetadataBytes.Size() != reviewedMetadataReader->GetSize())
            return FailStep("model-import-durable-metadata-buffer");
        reviewedMetadataReader->Serialize(reviewedMetadataBytes.Data(), reviewedMetadataBytes.Size());
        assets::AssetMetadata reviewedMetadata;
        if (reviewedMetadataReader->HasErrors() ||
            assets::ParseMetadata(
                {reinterpret_cast<const char*>(reviewedMetadataBytes.Data()), reviewedMetadataBytes.Size()},
                reviewedMetadata) != assets::MetadataResult::Success ||
            reviewedMetadata.id != importedModelAsset ||
            reviewedMetadata.importer != assets::HashCompilerName("vanguard.model.importer") ||
            reviewedMetadata.importerVersion != vanguard::mesh_tools::ModelSourceImporterVersion ||
            reviewedMetadata.settingsVersion != vanguard::mesh_tools::ModelImportSettingsVersion ||
            reviewedMetadata.outputs.Size() != reviewedModelPlan.GetImportDescription().outputs.Size())
            return FailStep("model-import-durable-metadata-truth");
        for (vanguard::u32 index = 0u; index < reviewedMetadata.outputs.Size(); ++index)
        {
            const assets::AssetOutput& persisted = reviewedMetadata.outputs[index];
            const assets::AssetOutput& planned = reviewedModelPlan.GetImportDescription().outputs[index];
            if (persisted.key != planned.key || persisted.type != planned.type)
                return FailStep("model-import-durable-metadata-outputs");
        }

        vanguard::u64 reviewedAcceptedGeneration = 0u;
        vanguard::u32 reviewedAvailableOutputs = 0u;
        bool reviewedOutputsTruncated = false;
        static_cast<void>(projectAssets.CopyAssetOutputs(
            importedModelAsset, 0u, {}, reviewedAvailableOutputs, reviewedOutputsTruncated));
        if (!projectAssets.GetAcceptedAssetGeneration(importedModelAsset, reviewedAcceptedGeneration) ||
            reviewedAcceptedGeneration == 0u || reviewedOutputsTruncated ||
            reviewedAvailableOutputs != reviewedModelPlan.GetImportDescription().outputs.Size())
            return FailStep("model-import-accepted-cohort-size");
        vanguard::containers::DynamicArray<resources::ResourceReference> reviewedOutputs(
            vanguard::memory::pools::Tools::GetInstance());
        reviewedOutputs.Resize(reviewedAvailableOutputs);
        vanguard::u32 copiedAvailableOutputs = 0u;
        bool copiedOutputsTruncated = false;
        const vanguard::u32 copiedOutputs = projectAssets.CopyAssetOutputs(
            importedModelAsset, 0u, {reviewedOutputs.TypedData(), reviewedOutputs.Size()},
            copiedAvailableOutputs, copiedOutputsTruncated);
        if (copiedOutputs != reviewedAvailableOutputs || copiedAvailableOutputs != reviewedAvailableOutputs ||
            copiedOutputsTruncated)
            return FailStep("model-import-accepted-cohort-copy");
        for (const assets::AssetOutput& planned : reviewedModelPlan.GetImportDescription().outputs)
        {
            const resources::ResourceReference expected = assets::GetAssetOutputReference(importedModelAsset, planned);
            bool found = false;
            for (const resources::ResourceReference observed : reviewedOutputs)
                found = found || observed == expected;
            if (!expected.IsTyped() || !found)
                return FailStep("model-import-accepted-cohort-identity");
        }
        const assets::SourceRecord* const reviewedSourceRecord = workspace->GetSources().LookupAsset(importedModelAsset);
        if (reviewedSourceRecord == nullptr || reviewedSourceRecord->path != reviewedManagedSource ||
            reviewedSourceRecord->metadata.id != importedModelAsset ||
            reviewedSourceRecord->sourceIdentity != reviewedModelPlan.GetWitnesses().sourceIdentity)
            return FailStep("model-import-source-identity");

        bool reviewedCohortReady = false;
        vanguard::u32 reviewedStaleBuildRetries = 0u;
        for (vanguard::u32 attempt = 0u; attempt < 500u && !reviewedCohortReady; ++attempt)
        {
            const assetStreaming::ProjectAssetAutomaticRecookStatus automatic = projectAssets.GetAutomaticRecookStatus();
            bool allReady = automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::Idle &&
                            automatic.pendingDeliveryOutputs == 0u;
            for (const resources::ResourceReference modelOutput : reviewedOutputs)
            {
                assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
                if (!projectAssets.GetOutputDeliveryStatus(modelOutput, delivery) ||
                    delivery.authoredGeneration != reviewedAcceptedGeneration ||
                    delivery.buildState != assetStreaming::ProjectAssetDeliveryStageState::Committed ||
                    (delivery.publicationState != assetStreaming::ProjectAssetDeliveryStageState::Installed &&
                     delivery.publicationState != assetStreaming::ProjectAssetDeliveryStageState::Current &&
                     delivery.publicationState != assetStreaming::ProjectAssetDeliveryStageState::Stale) ||
                    delivery.committedBuildFingerprint.IsEmpty() || delivery.committedContentFingerprint.IsEmpty() ||
                    delivery.publishedBuildFingerprint.IsEmpty() || delivery.publishedContentFingerprint.IsEmpty() ||
                    !delivery.publishedPhysicalPath.IsFilePath() || delivery.publicationRetryRequired)
                    allReady = false;
            }
            if (allReady)
            {
                reviewedCohortReady = true;
                break;
            }
            if (automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::Failed ||
                automatic.state == assetStreaming::ProjectAssetAutomaticRecookState::RecoveryRequired)
            {
                // A concurrent catalog/observation advance can invalidate an
                // already-admitted build before its VADI commit. P5 must retain
                // the durable model and exact cohort; exercise its explicit
                // retry rather than treating a stale completion as a data loss.
                if (reviewedStaleBuildRetries < 4u &&
                    assetStreaming::HasAutomaticRecookDirtyReason(
                        automatic.dirtyReasons,
                        assetStreaming::ProjectAssetAutomaticRecookDirtyReason::StaleCompletion))
                {
                    assetStreaming::ProjectAssetPipelineFailure retryFailure;
                    const auto retried = projectAssets.RetryAutomaticRecook(&retryFailure);
                    if (retried == assetStreaming::ProjectAssetPipelineResult::Success ||
                        retried == assetStreaming::ProjectAssetPipelineResult::Pending)
                    {
                        ++reviewedStaleBuildRetries;
                        continue;
                    }
                    std::fprintf(stderr,
                                 "[editorAssetRetirementTests] model stale-cohort retry rejected: result=%s build=%s\n",
                                 assetStreaming::ToString(retried), assets::ToString(retryFailure.build));
                }
                std::fprintf(stderr,
                             "[editorAssetRetirementTests] model cohort build failed: state=%u dirty=0x%x pending=%u delivery=%u retry=%u\n",
                             static_cast<unsigned>(automatic.state), static_cast<unsigned>(automatic.dirtyReasons),
                             automatic.pendingOutputs, automatic.pendingDeliveryOutputs, automatic.retryEntries);
                for (const resources::ResourceReference modelOutput : reviewedOutputs)
                {
                    assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
                    if (projectAssets.GetOutputDeliveryStatus(modelOutput, delivery))
                        std::fprintf(stderr,
                                     "  output path=%llu type=%u generation=%llu build=%u publication=%u reason=%u pubRetry=%u residentRetry=%u\n",
                                     static_cast<unsigned long long>(modelOutput.GetPath().Id()),
                                     static_cast<unsigned>(modelOutput.ExpectedType()),
                                     static_cast<unsigned long long>(delivery.authoredGeneration),
                                     static_cast<unsigned>(delivery.buildState),
                                     static_cast<unsigned>(delivery.publicationState),
                                     static_cast<unsigned>(delivery.publicationReason),
                                     delivery.publicationRetryRequired ? 1u : 0u,
                                     delivery.residentRetryRequired ? 1u : 0u);
                }
                return FailStep("model-import-cohort-build");
            }
            if (!RunFrame(*frames, clock))
                return FailStep("model-import-cohort-frame");
            vanguard::concurrency::SleepOnCurrentThread(1);
        }
        if (!reviewedCohortReady)
        {
            const assetStreaming::ProjectAssetAutomaticRecookStatus automatic = projectAssets.GetAutomaticRecookStatus();
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] model cohort timed out: state=%u dirty=0x%x pendingAssets=%u pendingOutputs=%u delivery=%u retry=%u\n",
                         static_cast<unsigned>(automatic.state), static_cast<unsigned>(automatic.dirtyReasons),
                         automatic.pendingAssets, automatic.pendingOutputs, automatic.pendingDeliveryOutputs,
                         automatic.retryEntries);
            for (const resources::ResourceReference modelOutput : reviewedOutputs)
            {
                assetStreaming::ProjectAssetOutputDeliveryStatus delivery;
                if (projectAssets.GetOutputDeliveryStatus(modelOutput, delivery))
                    std::fprintf(stderr,
                                 "  output path=%llu type=%u generation=%llu build=%u publication=%u reason=%u pubRetry=%u\n",
                                 static_cast<unsigned long long>(modelOutput.GetPath().Id()),
                                 static_cast<unsigned>(modelOutput.ExpectedType()),
                                 static_cast<unsigned long long>(delivery.authoredGeneration),
                                 static_cast<unsigned>(delivery.buildState),
                                 static_cast<unsigned>(delivery.publicationState),
                                 static_cast<unsigned>(delivery.publicationReason),
                                 delivery.publicationRetryRequired ? 1u : 0u);
            }
            return FailStep("model-import-cohort-timeout");
        }

        // A successful inspected review must remain explicit and non-durable
        // when a separate maintenance owner momentarily blocks source mutation.
        // CancelQueuedCommit then preserves the same reviewed plan and source
        // bytes without creating the source or VMETA.
        const filesystem::AbsolutePath queuedExternalSource =
            root.AddDirPath("ExternalImport").AddFilePath("queued_review_model.gltf");
        const filesystem::AbsolutePath queuedManagedSource =
            workspace->GetAssetsRoot().AddFilePath("queued_review_model.gltf");
        if (!WriteRawFile(queuedExternalSource, reviewedSnapshot.Data(), reviewedSnapshot.Size()))
            return FailStep("model-import-queued-source");
        {
            editor::EditorModelImportSession queuedSession;
            if (!queuedSession.BeginExternalSnapshot(queuedExternalSource, reviewedSnapshot, 0u, {}, projectAssets))
                return FailStep("model-import-queued-inspection-start");
            bool queuedReviewReady = false;
            for (vanguard::u32 attempt = 0u; attempt < 10'000u; ++attempt)
            {
                queuedSession.Poll(projectAssets);
                const auto status = queuedSession.GetStatus();
                if (status.phase == editor::EditorModelImportSessionPhase::Review)
                {
                    queuedReviewReady = true;
                    break;
                }
                if (status.phase != editor::EditorModelImportSessionPhase::Inspecting &&
                    status.phase != editor::EditorModelImportSessionPhase::ProjectBusy)
                    return FailStep("model-import-queued-inspection-result");
                if (!RunFrame(*frames, clock))
                    return FailStep("model-import-queued-inspection-frame");
                vanguard::concurrency::SleepOnCurrentThread(1);
            }
            if (!queuedReviewReady || queuedSession.GetPlan().HasBlockingConflicts() ||
                !DriveUntilAssetMutationReady(projectAssets, *frames, clock))
                return FailStep("model-import-queued-review-ready");
            // Presentation submits only an immutable approval at click time.
            // Poll must allow a frame to render the committing notice before
            // running any synchronous source/transaction work.
            if (!queuedSession.QueueReviewedCommit() || !queuedSession.GetStatus().commitQueued ||
                queuedSession.GetStatus().commitAttempts != 0u)
                return FailStep("model-import-ui-commit-approval-queued");
            queuedSession.Poll(projectAssets);
            if (!queuedSession.GetStatus().commitQueued || queuedSession.GetStatus().commitAttempts != 0u ||
                filesystem::RawFileReader::Create(queuedManagedSource) ||
                !queuedSession.CancelQueuedCommit() || queuedSession.GetStatus().commitQueued)
                return FailStep("model-import-ui-commit-notice-precedes-work");
            const assets::AssetId reviewedQueuedIdentity = queuedSession.GetPlan().GetWitnesses().parentAsset;
            if (!reviewedQueuedIdentity.IsValid() || !RequestLooseMaintenanceAfterOwnerSettlement(*pipeline))
                return FailStep("model-import-queued-maintenance-admission");
            assetStreaming::ProjectAssetPipelineFailure queuedFailure;
            const auto blocked = queuedSession.CommitReviewedPlan(projectAssets, &queuedFailure);
            const auto blockedStatus = queuedSession.GetStatus();
            if (blocked != assetStreaming::ProjectAssetPipelineResult::Busy || !blockedStatus.commitQueued ||
                blockedStatus.phase != editor::EditorModelImportSessionPhase::Review ||
                blockedStatus.durableCommitted || blockedStatus.commitAttempts != 1u ||
                filesystem::RawFileReader::Create(queuedManagedSource) ||
                filesystem::RawFileReader::Create(MetadataPath(queuedManagedSource)))
                return FailStep("model-import-queued-busy-is-nondurable");
            // A retained maintenance/admission owner must not drive another
            // full-source hash or planned-request creation from the UI poll.
            // The approved review remains queued, and only an eligible retry
            // performs the exact immutable witness verification again.
            for (vanguard::u32 blockedFrame = 0u; blockedFrame < 40u; ++blockedFrame)
                queuedSession.Poll(projectAssets);
            if (!queuedSession.GetStatus().commitQueued || queuedSession.GetStatus().commitAttempts != 1u ||
                projectAssets.CanRetryReviewedModelCommit(queuedSession.GetPlan()))
                return FailStep("model-import-queued-owner-does-not-spin");
            if (!queuedSession.CancelQueuedCommit() || queuedSession.GetStatus().commitQueued ||
                queuedSession.GetStatus().phase != editor::EditorModelImportSessionPhase::Review ||
                queuedSession.GetPlan().GetWitnesses().parentAsset != reviewedQueuedIdentity ||
                queuedSession.CancelQueuedCommit())
                return FailStep("model-import-queued-cancel-retains-review");
            if (DrainLooseMaintenance(*pipeline) != assetStreaming::ProjectAssetPipelineResult::Success)
                return FailStep("model-import-queued-maintenance-drain");

            // A second explicit click is allowed after cancellation. This time
            // keep the queued approval, release the transient maintenance owner,
            // and prove Poll commits the SAME reviewed plan automatically.
            if (!RequestLooseMaintenanceAfterOwnerSettlement(*pipeline))
                return FailStep("model-import-queued-retry-admission");
            const auto queuedAgain = queuedSession.CommitReviewedPlan(projectAssets, &queuedFailure);
            if (queuedAgain != assetStreaming::ProjectAssetPipelineResult::Busy ||
                !queuedSession.GetStatus().commitQueued || queuedSession.GetStatus().commitAttempts != 2u)
                return FailStep("model-import-queued-second-click");
            if (DrainLooseMaintenance(*pipeline) != assetStreaming::ProjectAssetPipelineResult::Success)
                return FailStep("model-import-queued-retry-maintenance-drain");
            bool autoCommitted = false;
            for (vanguard::u32 attempt = 0u; attempt < 10'000u; ++attempt)
            {
                queuedSession.Poll(projectAssets);
                const auto queuedStatus = queuedSession.GetStatus();
                if (queuedStatus.durableCommitted)
                {
                    autoCommitted = true;
                    break;
                }
                if (queuedStatus.phase != editor::EditorModelImportSessionPhase::Review ||
                    !queuedStatus.commitQueued || queuedStatus.commitAttempts > 64u)
                    return FailStep("model-import-queued-auto-retry-state");
                if (!RunFrame(*frames, clock))
                    return FailStep("model-import-queued-auto-retry-frame");
                vanguard::concurrency::SleepOnCurrentThread(1);
            }
            if (!autoCommitted || queuedSession.GetStatus().commitQueued ||
                queuedSession.GetPlan().GetWitnesses().parentAsset != reviewedQueuedIdentity ||
                !filesystem::RawFileReader::Create(queuedManagedSource) ||
                !filesystem::RawFileReader::Create(MetadataPath(queuedManagedSource)))
                return FailStep("model-import-queued-auto-durable-commit");
        }

        // Leave the current-generation preview live across quiesce. Successful
        // host shutdown proves the workspace reset released its demand/root request
        // before ResourceStreaming/Rendering enforce their own zero-work teardown.
        const editor::ProjectTexturePreviewHandle stale = replacement;
        if (!host.Shutdown(&hostFailure) || rhi::IsInitialized())
            return FailStep("preview-quiesce-live-handle");

        // I/O intentionally has a one-lifetime process contract, so a second full
        // EngineHost would test an unsupported lower-layer restart rather than a
        // workspace reconstruction. Reconstruct only the production workspace
        // object after the real host destroyed its predecessor; constructor-owned
        // preview identity must change before any lifecycle/dependency access.
        editor::ProjectWorkspaceService* reopenedWorkspace =
            editor::detail::CreateUninitializedProjectWorkspaceForTest(workspaceConfig);
        if (reopenedWorkspace == nullptr ||
            editor::detail::GetProjectTexturePreviewOwnerCookieForTest(*reopenedWorkspace) == stale.ownerCookie)
            return FailStep("preview-reopen-owner-identity");
        editor::EditorProjectAssetAccess reopenedAssets =
            editor::detail::EditorProjectAssetTestAccess::GetProjectAssets(*reopenedWorkspace);
        editor::ProjectTexturePreviewInfo staleInfo;
        editor::ProjectTexturePreviewHandle staleRelease = stale;
        if (reopenedAssets.QueryTexturePreview(stale, staleInfo) || reopenedAssets.ReleaseTexturePreview(staleRelease, 0u) ||
            staleRelease != stale)
            return FailStep("preview-stale-handle-rejected");
        editor::detail::DestroyUninitializedProjectWorkspaceForTest(reopenedWorkspace);
        if (reopenedWorkspace != nullptr)
            return FailStep("preview-reopen-destroy");
        return true;
    }

    [[nodiscard]] bool RunProductionHandoffProof(const filesystem::AbsolutePath& root,
                                                 editor::ProjectWorkspaceConfig& workspaceConfig,
                                                 assets::AssetMetadata& metadata,
                                                 const resources::ResourceReference output,
                                                 const vanguard::containers::DynamicArray<vanguard::u8>& replacementSettings) noexcept
    {
        app::EngineHost host;
        app::HostFailure hostFailure;
        const filesystem::Config filesystemConfig{root, root, root.AddDirPath("DerivedData")};
        const engine::RenderingServiceConfig renderingConfig = MakeRenderingConfig();
        if (!engine::RegisterEngineModule(host, &hostFailure) ||
            !host.RegisterModule({editor::EditorModuleId, "editor", 1}, &hostFailure) ||
            !engine::RegisterIoService(host, &hostFailure) ||
            !engine::RegisterFilesystemService(host, filesystemConfig, &hostFailure) ||
            !engine::RegisterJobsService(host, &hostFailure) ||
            !engine::RegisterFramePipelineService(host, &hostFailure) ||
            !engine::RegisterReflectionService(host, &hostFailure) ||
            !engine::RegisterResourcesService(host, &hostFailure) ||
            !engine::RegisterResourceStreamingService(host, &hostFailure) ||
            !engine::RegisterRenderingService(host, renderingConfig, &hostFailure) ||
            !editor::RegisterProjectWorkspaceService(host, workspaceConfig, &hostFailure))
            return FailStep("service-registration");

        const app::ApplicationProfile profile =
            app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
        if (!host.Compile(profile, &hostFailure) || !host.Start(&hostFailure))
        {
            std::fprintf(stderr,
                         "[editorAssetRetirementTests] host start failure: code=%u service=%llu related=%llu message=%s\n",
                         static_cast<unsigned>(hostFailure.code), static_cast<unsigned long long>(hostFailure.service),
                         static_cast<unsigned long long>(hostFailure.relatedService),
                         hostFailure.message != nullptr ? hostFailure.message : "<none>");
            return FailStep("host-start");
        }

        editor::ProjectWorkspaceService* const workspace = editor::FindProjectWorkspaceService(host);
        engine::FramePipelineService* const frames = engine::FindFramePipelineService(host);
        engine::RenderingService* const renderingService = engine::FindRenderingService(host);
        engine::ResourcesService* const resourcesService = engine::FindResourcesService(host);
        engine::ResourceStreamingService* const streamingService = engine::FindResourceStreamingService(host);
        if (workspace == nullptr || frames == nullptr || renderingService == nullptr || resourcesService == nullptr ||
            streamingService == nullptr)
            return FailStep("service-find");
        assetStreaming::ProjectAssetPipeline* const pipeline = workspace->GetAssetPipeline();
        if (pipeline == nullptr || !pipeline->IsInitialized())
            return FailStep("project-pipeline");

        TestClock clock;
        engine::FrameFailure frameFailure;
        engine::FramePipelineConfig frameConfig;
        frameConfig.clock = {&ReadClock, 1'000, &clock};
        frameConfig.pacing = engine::FramePacingMode::Disabled;
        if (!frames->Configure(frameConfig, &frameFailure) || !frames->Compile(&frameFailure))
            return FailStep("frame-configure");

        const assets::SourceRoot sourceRoot{workspace->GetAssetsRoot(), false};
        assetStreaming::ProjectAssetOutputDeliveryStatus statusA;
        if (!DriveUntilPreviewPublication(*pipeline, output, *frames, clock, statusA))
            return FailStep("status-a");

        streaming::ResourceStreamer& streamer = streamingService->GetStreamer();
        resources::PipelineRequest streamA = streamer.Request(output, resources::LoadPriority::High);
        if (!streamA.TryWait(10'000) || !streamA.HasLoaded())
            return FailStep("stream-a-load");
        resources::ResourceHandle resourceA = streamA.Acquire();
        rendering::TextureResidencyRuntime& runtime = renderingService->GetTextureResidency();
        rendering::TextureResidencyRuntimeFailure runtimeFailure;
        rendering::TextureDemandHandle demand;
        if (!resourceA.IsValid() || !runtime.RequestTexture(resourceA, demand, &runtimeFailure) ||
            !DriveUntilReady(*frames, clock, runtime, demand.GetResidency()))
            return FailStep("resident-a");
        const vanguard::u32 generationA = resourceA.GetGeneration();
        const rendering::GpuTextureResidencyHandle residency = demand.GetResidency();
        const assetStreaming::ProjectAssetResidentDeliveryEvidence residentA{
            output, generationA, statusA.publishedBuildFingerprint, statusA.publishedContentFingerprint, false};
        if (!pipeline->ReportResidentDelivery(residentA))
            return FailStep("report-resident-a");

        const filesystem::AbsolutePath source = root.AddDirPath("Assets").AddFilePath("resident.png");
        if (!CopySettings(replacementSettings, metadata.settings) || !WriteMetadata(source, metadata))
            return FailStep("metadata-b-write");
        if (pipeline->Rescan({&sourceRoot, 1}) != assetStreaming::ProjectAssetPipelineResult::Success)
            return FailStep("rescan-b");
        assetStreaming::ProjectAssetRequest requestB = pipeline->Request(output);
        if (!requestB.IsValid())
            return FailStep("request-b-start");
        requestB.Wait();
        if (requestB.Poll() != assetStreaming::ProjectAssetPipelineResult::Success)
            return FailStep("request-b-publish");
        requestB.Reset();
        assetStreaming::ProjectAssetOutputDeliveryStatus statusB;
        if (!pipeline->GetOutputDeliveryStatus(output, statusB) || statusB.publishedPhysicalPath.Empty() ||
            statusB.publishedPhysicalPath == statusA.publishedPhysicalPath ||
            pipeline->GetLooseMaintenanceStatus().trackedCandidates != 1u)
            return FailStep("status-b");

        resources::PipelineRequest streamB = streamer.Replace(output, resources::LoadPriority::High);
        if (!streamB.IsValid() || !streamB.TryWait(10'000) || !streamB.HasLoaded())
            return FailStep("stream-b-load");
        resources::ResourceHandle resourceB = streamB.Acquire();
        if (!resourceB.IsValid() || resourceB.GetGeneration() == generationA ||
            !runtime.ReplaceTexture(resourceB, residency, &runtimeFailure) ||
            !DriveUntilReplacementCurrent(*frames, clock, runtime, residency, resourceB.GetGeneration()) ||
            !CompleteTextureRetirements(*renderingService, runtime))
            return FailStep("replace-a-b");
        const vanguard::u32 generationB = resourceB.GetGeneration();
        const assetStreaming::ProjectAssetResidentDeliveryEvidence residentB{
            output, generationB, statusB.publishedBuildFingerprint, statusB.publishedContentFingerprint, false};
        if (!pipeline->ReportResidentDelivery(residentB))
            return FailStep("report-resident-b");

        rendering::TextureGenerationRetirement retirementA;
        if (!PeekSingleRetirement(runtime, output.GetPath(), generationA, retirementA))
            return FailStep("retirement-a-evidence");
        resourceA.Reset();
        streamA.Reset();

        // The maintenance owner deliberately holds admission across this pump.
        // The production bridge must therefore leave the exact renderer record
        // untouched and return Busy rather than acknowledging it speculatively.
        if (!RequestLooseMaintenanceAfterOwnerSettlement(*pipeline))
            return FailStep("maintenance-busy-admission");
        if (workspace->PumpResidentRetirements() != editor::ProjectResidentRetirementPumpResult::Busy)
            return FailStep("pump-busy");
        rendering::TextureGenerationRetirement busyRetirement;
        if (!PeekSingleRetirement(runtime, output.GetPath(), generationA, busyRetirement) ||
            busyRetirement.retirementId != retirementA.retirementId ||
            runtime.GetStats().residency.pendingGenerationRetirements != 1u)
            return FailStep("busy-retains-evidence");
        if (DrainLooseMaintenance(*pipeline) != assetStreaming::ProjectAssetPipelineResult::Success)
            return FailStep("maintenance-first-drain");
        const assetStreaming::ProjectAssetLooseMaintenanceStatus protectedStatus = pipeline->GetLooseMaintenanceStatus();
        if (protectedStatus.state != assetStreaming::ProjectAssetLooseMaintenanceState::Deferred ||
            protectedStatus.residentProtectedRecords != 1u || !protectedStatus.retryRequired)
            return FailStep("resident-protection-before-accepted");

        // The exact A candidate is known resident-protected immediately before
        // this production pump. Afterward the pipeline reports it NotTracked and
        // the renderer retirement id is stale, proving Accepted was delivered and
        // that the matching renderer record was acknowledged exactly once.
        if (workspace->PumpResidentRetirements() != editor::ProjectResidentRetirementPumpResult::Success ||
            !NoCompletedRetirements(runtime) ||
            pipeline->ReportResidentRetirement({output.GetPath(), generationA}) !=
                assetStreaming::ProjectAssetResidentRetirementResult::NotTracked ||
            !ExactRetirementAlreadyAcknowledged(runtime, retirementA.retirementId))
            return FailStep("accepted-and-acked-once");
        if (pipeline->RetryLooseMaintenance() != assetStreaming::ProjectAssetPipelineResult::Pending ||
            DrainLooseMaintenance(*pipeline) != assetStreaming::ProjectAssetPipelineResult::Success)
            return FailStep("maintenance-after-accepted");
        const assetStreaming::ProjectAssetLooseMaintenanceStatus removedStatus = pipeline->GetLooseMaintenanceStatus();
        if (removedStatus.residentProtectedRecords != 0u || removedStatus.trackedCandidates != 0u ||
            filesystem::GetManager().FileExist(statusA.publishedPhysicalPath))
            return FailStep("accepted-clears-project-protection");

        // B is the current publication and has no superseded loose candidate.
        // Its terminal renderer retirement is therefore deliberately NotTracked;
        // the same production bridge must still acknowledge that terminal result.
        if (!RetireCurrentTexture(*frames, clock, *renderingService, demand))
            return FailStep("terminal-retirement-b");
        rendering::TextureGenerationRetirement retirementB;
        if (!PeekSingleRetirement(runtime, output.GetPath(), generationB, retirementB))
            return FailStep("retirement-b-evidence");
        if (pipeline->ReportResidentRetirement({output.GetPath(), generationB}) !=
            assetStreaming::ProjectAssetResidentRetirementResult::NotTracked)
            return FailStep("terminal-b-not-tracked");
        if (workspace->PumpResidentRetirements() != editor::ProjectResidentRetirementPumpResult::Success ||
            !NoCompletedRetirements(runtime) ||
            !ExactRetirementAlreadyAcknowledged(runtime, retirementB.retirementId))
            return FailStep("not-tracked-acked-once");

        resourceB.Reset();
        streamB.Reset();
        if (!WaitForResourceDrain(resourcesService->GetPipeline(), streamer, *frames, clock))
            return FailStep("resource-drain");
        if (!host.Shutdown(&hostFailure) || rhi::IsInitialized())
            return FailStep("graceful-shutdown");
        return true;
    }
    inline constexpr char PreviewProofChildMode[] = "--preview-proof";
    inline constexpr char RetirementProofChildMode[] = "--retirement-proof";

    [[nodiscard]] bool RunProofSubprocess(const char* const mode) noexcept
    {
#if defined(_WIN32)
        char executable[MAX_PATH]{};
        const DWORD executableLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (executableLength == 0u || executableLength == MAX_PATH)
            return false;
        char commandLine[MAX_PATH + 128]{};
        const int commandLength = std::snprintf(commandLine, sizeof(commandLine), "\"%s\" %s", executable, mode);
        if (commandLength <= 0 || static_cast<vanguard::usize>(commandLength) >= sizeof(commandLine))
            return false;
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 120000u);
        DWORD exitCode = 1u;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0u;
#else
        static_cast<void>(mode);
        return false;
#endif
    }

    [[nodiscard]] bool InitializeProofRuntime() noexcept
    {
        return vanguard::memory::Initialize() &&
               vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "editorAssetRetirementTests") &&
               vanguard::containers::Initialize();
    }

    [[nodiscard]] int RunPreviewProofChild() noexcept
    {
        if (!InitializeProofRuntime())
            return 2;
        const filesystem::AbsolutePath root =
            vanguard::tests::ScratchDirectory("editor_asset_retirement_tests").AddDirPath("preview_coordinator");
        editor::ProjectWorkspaceConfig workspaceConfig;
        assets::AssetMetadata metadata;
        resources::ResourceReference output;
        vanguard::containers::DynamicArray<vanguard::u8> replacementSettings(vanguard::memory::pools::Assets::GetInstance());
        const bool prepared = PrepareProject(root, workspaceConfig, metadata, output, replacementSettings);
        const bool passed = prepared && RunProjectTexturePreviewCoordinatorProof(
            root, workspaceConfig, metadata, output, replacementSettings);
        RemoveFixture(root);
        vanguard::diagnostics::Shutdown();
        return passed ? 0 : 1;
    }

    [[nodiscard]] int RunRetirementProofChild() noexcept
    {
        if (!InitializeProofRuntime())
            return 2;
        const filesystem::AbsolutePath root =
            vanguard::tests::ScratchDirectory("editor_asset_retirement_tests").AddDirPath("production_handoff");
        editor::ProjectWorkspaceConfig workspaceConfig;
        assets::AssetMetadata metadata;
        resources::ResourceReference output;
        vanguard::containers::DynamicArray<vanguard::u8> replacementSettings(vanguard::memory::pools::Assets::GetInstance());
        const bool prepared = PrepareProject(root, workspaceConfig, metadata, output, replacementSettings);
        const bool passed = prepared && RunProductionHandoffProof(root, workspaceConfig, metadata, output, replacementSettings);
        RemoveFixture(root);
        vanguard::diagnostics::Shutdown();
        return passed ? 0 : 1;
    }
} // namespace

int main(const int argc, const char* const* const argv)
{
    if (!PreviewAdmissionThresholdPolicy())
    {
        static_cast<void>(FailStep("preview-admission-threshold-policy"));
        return 1;
    }
    if (editor::ProjectAssetInitializeFailureOwnerPolicy(false) !=
            editor::ProjectAssetInitializeFailureOwnerAction::ReleaseCompilerImmediately ||
        editor::ProjectAssetInitializeFailureOwnerPolicy(true) !=
            editor::ProjectAssetInitializeFailureOwnerAction::PreserveForLifecycleShutdown)
    {
        static_cast<void>(FailStep("partial-init-owner-policy"));
        return 1;
    }

    if (argc == 2 && std::strcmp(argv[1], PreviewProofChildMode) == 0)
        return RunPreviewProofChild();
    if (argc == 2 && std::strcmp(argv[1], RetirementProofChildMode) == 0)
        return RunRetirementProofChild();
    if (argc != 1)
        return 3;

    // Vanguard I/O is deliberately one-lifetime-per-process. Keep both production
    // EngineHost proofs in the default test without weakening that contract by
    // executing each host-backed proof in its own process.
    const bool previewPassed = RunProofSubprocess(PreviewProofChildMode);
    const bool retirementPassed = previewPassed && RunProofSubprocess(RetirementProofChildMode);
    const bool passed = previewPassed && retirementPassed;

    if (passed)
        std::printf("[editorAssetRetirementTests] project texture preview/replacement and renderer-to-project retirement proofs passed.\n");
    return passed ? 0 : 1;
}
