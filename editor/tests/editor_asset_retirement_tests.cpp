#include "../../tests/support/scratch_directory.hpp"

#include <vanguard/editor/editor_project_service.hpp>
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
#include <vanguard/projects/project.hpp>
#include <vanguard/rendering/texture_residency.hpp>
#include <vanguard/rhi/d3d12/backend.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <cstdio>
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
