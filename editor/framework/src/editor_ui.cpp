#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/editor/editor_ui_imgui_platform.hpp>
#include <vanguard/editor/editor_ui_renderer.hpp>
#include <vanguard/engine/rendering_service.hpp>
#include <vanguard/editor/editor_project_service.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace editor = vanguard::editor;

    [[nodiscard]] bool ValidName(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return false;
        vanguard::u32 length = 0;
        while (name[length] != '\0')
            if (++length >= editor::MaximumEditorPanelNameBytes)
                return false;
        return true;
    }

    void ClearFailure(editor::EditorUiFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] bool Fail(editor::EditorUiFailure* const failure, const editor::EditorUiFailureCode code, const char* const message,
                            const editor::EditorPanelTypeId panelType = editor::InvalidEditorPanelTypeId,
                            const editor::EditorPanelInstanceId panelInstance = editor::InvalidEditorPanelInstanceId,
                            const editor::EditorPanelHandle panel = {}, const editor::EditorHostWindowHandle host = {}, const editor::EditorTextureHandle texture = {}) noexcept
    {
        if (failure != nullptr)
            *failure = {code, panelType, panelInstance, panel, host, texture, message};
        return false;
    }

    [[nodiscard]] vanguard::u32 NextGeneration(const vanguard::u32 generation) noexcept
    {
        const vanguard::u32 next = generation + 1u;
        return next != 0 ? next : 1u;
    }

    class ManagedEditorUiService final : public editor::EditorUiService, public editor::EditorPanelRegistry
    {
    public:
        ManagedEditorUiService() noexcept
            : m_panelTypes(vanguard::memory::pools::Editor::GetInstance()), m_panels(vanguard::memory::pools::Editor::GetInstance()),
              m_hosts(vanguard::memory::pools::Editor::GetInstance()), m_textures(vanguard::memory::pools::Editor::GetInstance())
        {
            m_panelTypes.Reserve(editor::MaximumEditorPanelTypes);
            m_panels.Reserve(editor::MaximumEditorPanelInstances);
            m_hosts.Reserve(editor::MaximumEditorHostWindows);
            m_textures.Reserve(editor::MaximumEditorTextures);
        }

        ~ManagedEditorUiService() override
        {
            DestroyAllPanels();
        }

        [[nodiscard]] editor::EditorPanelRegistry& GetPanels() noexcept override { return *this; }
        [[nodiscard]] const editor::EditorPanelRegistry& GetPanels() const noexcept override { return *this; }

        [[nodiscard]] bool RegisterType(const editor::EditorPanelTypeDesc& desc, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            if (desc.id == editor::InvalidEditorPanelTypeId || !ValidName(desc.name) || desc.draw == nullptr || (desc.create == nullptr) != (desc.destroy == nullptr))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor panel type descriptor is invalid", desc.id);
            for (const PanelTypeRecord& type : m_panelTypes)
                if (type.active && type.desc.id == desc.id)
                    return Fail(failure, editor::EditorUiFailureCode::DuplicateIdentity, "editor panel type identity is already registered", desc.id);
            for (PanelTypeRecord& type : m_panelTypes)
            {
                if (type.active)
                    continue;
                type.desc = desc;
                type.name.Set(desc.name);
                type.desc.name = type.name.AsChar();
                type.active = true;
                return true;
            }
            if (m_panelTypes.Size() >= editor::MaximumEditorPanelTypes)
                return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor panel type capacity is exhausted", desc.id);
            PanelTypeRecord& type = m_panelTypes.EmplaceBack();
            type.desc = desc;
            type.name.Set(desc.name);
            type.desc.name = type.name.AsChar();
            type.active = true;
            return true;
        }

        [[nodiscard]] bool UnregisterType(const editor::EditorPanelTypeId typeId, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            PanelTypeRecord* const type = FindType(typeId);
            if (type == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor panel type is not registered", typeId);
            for (const PanelRecord& panel : m_panels)
                if (panel.active && panel.type == typeId)
                    return Fail(failure, editor::EditorUiFailureCode::InUse, "editor panel type still owns panel instances", typeId, panel.id, panel.handle);
            type->active = false;
            type->desc = {};
            type->name.Clear();
            return true;
        }

        [[nodiscard]] bool CreatePanel(const editor::EditorPanelDesc& desc, editor::EditorPanelHandle& output, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            output = {};
            if (!CanMutate(failure))
                return false;
            PanelTypeRecord* const type = FindType(desc.type);
            if (desc.id == editor::InvalidEditorPanelInstanceId || type == nullptr || !ValidName(desc.title))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor panel instance descriptor is invalid", desc.type, desc.id);
            for (const PanelRecord& panel : m_panels)
                if (panel.active && panel.id == desc.id)
                    return Fail(failure, editor::EditorUiFailureCode::DuplicateIdentity, "editor panel instance identity is already registered", desc.type, desc.id, panel.handle);

            vanguard::u32 index = ~vanguard::u32{0};
            for (vanguard::u32 candidate = 0; candidate < m_panels.Size(); ++candidate)
                if (!m_panels[candidate].active)
                {
                    index = candidate;
                    break;
                }
            if (index == ~vanguard::u32{0})
            {
                if (m_panels.Size() >= editor::MaximumEditorPanelInstances)
                    return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor panel instance capacity is exhausted", desc.type, desc.id);
                index = m_panels.Size();
                static_cast<void>(m_panels.EmplaceBack());
            }

            void* const state = type->desc.create != nullptr ? type->desc.create(desc.id, type->desc.factoryData) : nullptr;
            if (type->desc.create != nullptr && state == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::FactoryFailure, "editor panel instance factory failed", desc.type, desc.id);

            PanelRecord& panel = m_panels[index];
            panel.generation = NextGeneration(panel.generation);
            panel.handle = {index, panel.generation};
            panel.id = desc.id;
            panel.type = desc.type;
            panel.title.Set(desc.title);
            panel.state = state;
            panel.open = desc.open;
            panel.active = true;
            output = panel.handle;
            return true;
        }

        [[nodiscard]] bool DestroyPanel(const editor::EditorPanelHandle handle, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanMutate(failure))
                return false;
            PanelRecord* const panel = FindPanel(handle);
            if (panel == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor panel handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, handle);
            DestroyPanelState(*panel);
            return true;
        }

        [[nodiscard]] bool SetOpen(const editor::EditorPanelHandle handle, const bool open, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            PanelRecord* const panel = FindPanel(handle);
            if (panel == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor panel handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, handle);
            panel->open = open;
            return true;
        }

        [[nodiscard]] bool IsOpen(const editor::EditorPanelHandle handle) const noexcept override
        {
            const PanelRecord* const panel = FindPanel(handle);
            return panel != nullptr && panel->open;
        }

        void VisitPanels(const editor::EditorPanelVisitor visitor, void* const userData) const noexcept override
        {
            if (visitor == nullptr)
                return;
            for (const PanelRecord& panel : m_panels)
                if (panel.active)
                    visitor({panel.handle, panel.id, panel.type, panel.title.AsChar(), panel.open}, userData);
        }

        [[nodiscard]] bool RegisterHost(const editor::EditorHostWindowDesc& desc, editor::EditorHostWindowHandle& output,
                                        editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            output = {};
            if (!CanUseRegistries(failure))
                return false;
            if (!ValidHost(desc) || HasConflictingHost(desc, {}))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor host descriptor is invalid or conflicts with an existing host");
            vanguard::u32 index = ~vanguard::u32{0};
            for (vanguard::u32 candidate = 0; candidate < m_hosts.Size(); ++candidate)
                if (!m_hosts[candidate].active)
                {
                    index = candidate;
                    break;
                }
            if (index == ~vanguard::u32{0})
            {
                if (m_hosts.Size() >= editor::MaximumEditorHostWindows)
                    return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor host window capacity is exhausted");
                index = m_hosts.Size();
                static_cast<void>(m_hosts.EmplaceBack());
            }
            HostRecord& host = m_hosts[index];
            host.frameData.Reset();
            host.generation = NextGeneration(host.generation);
            host.handle = {index, host.generation};
            host.desc = desc;
            if (!m_renderer.CreateHost(host.handle, host.desc, failure))
                return false;
            host.active = true;
            output = host.handle;
            return true;
        }

        [[nodiscard]] bool UpdateHost(const editor::EditorHostWindowHandle handle, const editor::EditorHostWindowDesc& desc,
                                      editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor host window handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, handle);
            if (!ValidHost(desc) || HasConflictingHost(desc, handle))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor host update is invalid or conflicts with another host",
                            editor::InvalidEditorPanelTypeId, editor::InvalidEditorPanelInstanceId, {}, handle);
            if (desc.window != host->desc.window || desc.presentationOutput != host->desc.presentationOutput || desc.renderViewport != host->desc.renderViewport)
                return Fail(failure, editor::EditorUiFailureCode::InUse, "editor host output ownership cannot be rebound through registry metadata");
            host->desc = desc;
            return true;
        }

        [[nodiscard]] bool UnregisterHost(const editor::EditorHostWindowHandle handle, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor host window handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, handle);
            if (!m_renderer.DestroyHost(handle, failure))
                return false;
            host->frameData.Reset();
            host->active = false;
            host->desc = {};
            return true;
        }

        [[nodiscard]] bool GetHost(const editor::EditorHostWindowHandle handle, editor::EditorHostWindowInfo& info) const noexcept override
        {
            info = {};
            const HostRecord* const host = FindHost(handle);
            if (host == nullptr)
                return false;
            info = {host->handle, host->desc.window, host->desc.presentationOutput, host->desc.renderViewport, host->desc.primary};
            return true;
        }

        void VisitHosts(const editor::EditorHostWindowVisitor visitor, void* const userData) const noexcept override
        {
            if (visitor == nullptr)
                return;
            for (const HostRecord& host : m_hosts)
                if (host.active)
                    visitor({host.handle, host.desc.window, host.desc.presentationOutput, host.desc.renderViewport, host.desc.primary}, userData);
        }

        [[nodiscard]] bool RegisterTexture(const editor::EditorTextureDesc& desc, editor::EditorTextureHandle& output,
                                           editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            output = {};
            if (!CanUseRegistries(failure))
                return false;
            if (!ValidTexture(desc))
                return Fail(failure, editor::EditorUiFailureCode::InvalidDescriptor, "editor texture descriptor is invalid");
            vanguard::u32 index = ~vanguard::u32{0};
            for (vanguard::u32 candidate = 0; candidate < m_textures.Size(); ++candidate)
                if (!m_textures[candidate].active)
                {
                    index = candidate;
                    break;
                }
            if (index == ~vanguard::u32{0})
            {
                if (m_textures.Size() >= editor::MaximumEditorTextures)
                    return Fail(failure, editor::EditorUiFailureCode::CapacityExceeded, "editor texture capacity is exhausted");
                index = m_textures.Size();
                static_cast<void>(m_textures.EmplaceBack());
            }
            TextureRecord& texture = m_textures[index];
            texture.generation = NextGeneration(texture.generation);
            texture.handle = {index, texture.generation};
            texture.texture.Reset(desc.texture);
            texture.sampler.Reset(desc.sampler);
            texture.colorSpace = desc.colorSpace;
            texture.active = true;
            output = texture.handle;
            return true;
        }

        [[nodiscard]] bool UnregisterTexture(const editor::EditorTextureHandle handle, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (!CanUseRegistries(failure))
                return false;
            TextureRecord* const texture = FindTexture(handle);
            if (texture == nullptr)
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor texture handle is invalid", editor::InvalidEditorPanelTypeId,
                            editor::InvalidEditorPanelInstanceId, {}, {}, handle);
            texture->texture.Reset();
            texture->sampler.Reset();
            texture->active = false;
            return true;
        }

        [[nodiscard]] bool ResolveTexture(const editor::EditorTextureHandle handle, editor::ResolvedEditorTexture& resolved) const noexcept override
        {
            resolved = {};
            const TextureRecord* const texture = FindTexture(handle);
            if (texture == nullptr)
                return false;
            resolved = {texture->texture.GetRef(), texture->sampler.GetRef(), texture->colorSpace};
            return true;
        }

        [[nodiscard]] bool AcquireFrameData(const editor::EditorHostWindowHandle handle, editor::EditorUiFrameData& frameData) const noexcept override
        {
            frameData.Reset();
            const HostRecord* const host = FindHost(handle);
            if (host == nullptr || !host->frameData.IsValid())
                return false;
            frameData = host->frameData;
            return true;
        }

        [[nodiscard]] bool AcquireTextureFrameData(editor::EditorUiTextureFrameData& frameData) const noexcept override
        {
            frameData.Reset();
            if (!m_textureFrameData.IsValid())
                return false;
            frameData = m_textureFrameData;
            return true;
        }

        [[nodiscard]] bool BeginFrame(const vanguard::engine::FrameContext& frame, editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::Ready)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor UI frame can begin only while the service is ready");
            if (m_textureFrameData.IsValid())
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor texture uploads must be submitted before beginning another UI frame");
            if (!m_renderer.BeginFrame(failure) || !m_platform.BeginFrame(frame, failure))
                return false;
            m_frame = frame.frame;
            m_state = editor::EditorUiState::BuildingFrame;
            return true;
        }

        [[nodiscard]] bool DrawOpenPanels(editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::BuildingFrame)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor panels can be drawn only while building a UI frame");
            for (PanelRecord& panel : m_panels)
            {
                if (!panel.active || !panel.open)
                    continue;
                PanelTypeRecord* const type = FindType(panel.type);
                if (type == nullptr || type->desc.draw == nullptr)
                    return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor panel instance has no registered type", panel.type, panel.id, panel.handle);
                editor::EditorPanelContext context(*this, panel.handle, panel.id);
                type->desc.draw(context, panel.state, type->desc.factoryData);
            }
            return true;
        }

        [[nodiscard]] bool EndFrame(editor::EditorUiFailure* const failure) noexcept override
        {
            ClearFailure(failure);
            if (m_state != editor::EditorUiState::BuildingFrame)
                return Fail(failure, editor::EditorUiFailureCode::WrongPhase, "editor UI frame is not being built");
            editor::EditorUiFrameData frames[editor::MaximumEditorHostWindows]{};
            editor::EditorUiTextureFrameData textureFrame;
            vanguard::u32 frameCount = 0;
            if (!m_platform.EndFrame(frames, editor::MaximumEditorHostWindows, frameCount, textureFrame, failure))
            {
                m_platform.AbortFrame();
                m_state = editor::EditorUiState::Ready;
                return false;
            }
            for (vanguard::u32 index = 0; index < frameCount; ++index)
            {
                if (!frames[index].IsValid() || frames[index].GetFrame() != m_frame || FindHost(frames[index].GetHost()) == nullptr)
                {
                    m_state = editor::EditorUiState::Ready;
                    return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor UI platform returned frame data for an invalid host");
                }
                for (vanguard::u32 previous = 0; previous < index; ++previous)
                    if (frames[previous].GetHost() == frames[index].GetHost())
                    {
                        m_state = editor::EditorUiState::Ready;
                        return Fail(failure, editor::EditorUiFailureCode::DuplicateIdentity, "editor UI platform returned duplicate frame data for one host");
                    }
            }
            if (textureFrame.IsValid() && textureFrame.GetFrame() != m_frame)
            {
                m_state = editor::EditorUiState::Ready;
                return Fail(failure, editor::EditorUiFailureCode::InvalidHandle, "editor UI platform returned texture data for the wrong frame");
            }
            for (HostRecord& host : m_hosts)
                if (host.active)
                    host.frameData.Reset();
            for (vanguard::u32 index = 0; index < frameCount; ++index)
                FindHost(frames[index].GetHost())->frameData = static_cast<editor::EditorUiFrameData&&>(frames[index]);
            m_textureFrameData = static_cast<editor::EditorUiTextureFrameData&&>(textureFrame);
            m_state = editor::EditorUiState::Ready;
            return true;
        }

        void AbortFrame() noexcept override
        {
            if (m_state == editor::EditorUiState::BuildingFrame)
            {
                m_platform.AbortFrame();
                m_state = editor::EditorUiState::Ready;
            }
        }

        [[nodiscard]] editor::EditorUiState GetState() const noexcept override { return m_state; }
        [[nodiscard]] vanguard::u64 GetFrame() const noexcept override { return m_frame; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_framePipeline = vanguard::engine::FindFramePipelineService(context);
            m_windows = vanguard::engine::FindWindowService(context);
            m_input = vanguard::engine::FindInputService(context);
            if (m_framePipeline == nullptr || m_windows == nullptr || m_input == nullptr || context.Find(vanguard::engine::RenderingServiceId) == nullptr)
                return app::LifecycleStatus::Failure("Editor UI dependencies are unavailable");
            constexpr vanguard::engine::FrameParticipantId dependencies[]{vanguard::engine::InputFrameParticipantId};
            vanguard::engine::FrameParticipantDescriptor descriptor;
            descriptor.id = editor::EditorUiFrameParticipantId;
            descriptor.name = "editorUi";
            descriptor.phase = vanguard::engine::FramePhase::BeginFrame;
            descriptor.profiles = app::ApplicationProfile::Editor | app::ApplicationProfile::Tool;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            vanguard::engine::FrameFailure frameFailure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
                return app::LifecycleStatus::Failure(frameFailure.message != nullptr ? frameFailure.message : "Editor UI frame registration failed");
            constexpr vanguard::engine::FrameParticipantId renderDependencies[]{vanguard::engine::RenderingFrameTickParticipantId};
            descriptor.id = editor::EditorUiRenderParticipantId;
            descriptor.name = "editorUi.render";
            // Compose this frame's hosts after all scene viewport sources in Render.
            descriptor.phase = vanguard::engine::FramePhase::EndFrame;
            descriptor.after = {renderDependencies, 1};
            descriptor.execute = RenderFrame;
            if (!m_framePipeline->RegisterParticipant(descriptor, &frameFailure))
                return app::LifecycleStatus::Failure("Editor UI render participant registration failed");
            m_state = editor::EditorUiState::Ready;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext& context) noexcept override
        {
            editor::EditorUiFailure failure;
            auto* rendering = vanguard::engine::FindRenderingService(context);
            if (rendering == nullptr || !m_renderer.Initialize(*rendering, m_windows->GetManager(), &failure))
            {
                m_renderer.Shutdown();
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI renderer initialization failed");
            }
            if (!m_platform.Initialize(*this, *m_windows, *m_input, &failure))
            {
                m_platform.Shutdown();
                m_renderer.Shutdown();
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI platform initialization failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            if (m_state == editor::EditorUiState::BuildingFrame)
                return app::LifecycleStatus::Failure("Editor UI cannot quiesce while a frame is being built");
            m_platform.Shutdown();
            m_renderer.Shutdown();
            m_textureFrameData.Reset();
            m_state = editor::EditorUiState::Quiesced;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            m_platform.Shutdown();
            m_renderer.Shutdown();
            m_textureFrameData.Reset();
            DestroyAllPanels();
            for (TextureRecord& texture : m_textures)
            {
                texture.texture.Reset();
                texture.sampler.Reset();
                texture.active = false;
            }
            m_hosts.Clear();
            m_panelTypes.Clear();
            m_state = editor::EditorUiState::Offline;
            m_frame = 0;
            m_framePipeline = nullptr;
            m_windows = nullptr;
            m_input = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        struct PanelTypeRecord
        {
            editor::EditorPanelTypeDesc desc;
            vanguard::containers::String name;
            bool active = false;
        };

        struct PanelRecord
        {
            editor::EditorPanelHandle handle;
            editor::EditorPanelInstanceId id = editor::InvalidEditorPanelInstanceId;
            editor::EditorPanelTypeId type = editor::InvalidEditorPanelTypeId;
            vanguard::containers::String title;
            void* state = nullptr;
            vanguard::u32 generation = 0;
            bool open = false;
            bool active = false;
        };

        struct HostRecord
        {
            editor::EditorHostWindowHandle handle;
            editor::EditorHostWindowDesc desc;
            editor::EditorUiFrameData frameData;
            vanguard::u32 generation = 0;
            bool active = false;
        };

        struct TextureRecord
        {
            editor::EditorTextureHandle handle;
            vanguard::rhi::Texture texture;
            vanguard::rhi::SamplerState sampler;
            editor::EditorTextureColorSpace colorSpace = editor::EditorTextureColorSpace::DisplaySrgb;
            vanguard::u32 generation = 0;
            bool active = false;
        };

        [[nodiscard]] bool CanMutate(editor::EditorUiFailure* const failure) const noexcept
        {
            if (m_state == editor::EditorUiState::Ready)
                return true;
            return Fail(failure, m_state == editor::EditorUiState::BuildingFrame ? editor::EditorUiFailureCode::WrongPhase : editor::EditorUiFailureCode::NotReady,
                        "editor UI registry mutation requires a ready service");
        }

        [[nodiscard]] bool CanUseRegistries(editor::EditorUiFailure* const failure) const noexcept
        {
            if (m_state == editor::EditorUiState::Ready || m_state == editor::EditorUiState::BuildingFrame)
                return true;
            return Fail(failure, editor::EditorUiFailureCode::NotReady, "editor UI registries require an active service");
        }

        [[nodiscard]] PanelTypeRecord* FindType(const editor::EditorPanelTypeId id) noexcept
        {
            for (PanelTypeRecord& type : m_panelTypes)
                if (type.active && type.desc.id == id)
                    return &type;
            return nullptr;
        }

        [[nodiscard]] const PanelRecord* FindPanel(const editor::EditorPanelHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_panels.Size() && m_panels[handle.index].active && m_panels[handle.index].generation == handle.generation
                       ? &m_panels[handle.index]
                       : nullptr;
        }

        [[nodiscard]] PanelRecord* FindPanel(const editor::EditorPanelHandle handle) noexcept
        {
            return const_cast<PanelRecord*>(static_cast<const ManagedEditorUiService*>(this)->FindPanel(handle));
        }

        [[nodiscard]] const HostRecord* FindHost(const editor::EditorHostWindowHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_hosts.Size() && m_hosts[handle.index].active && m_hosts[handle.index].generation == handle.generation
                       ? &m_hosts[handle.index]
                       : nullptr;
        }

        [[nodiscard]] HostRecord* FindHost(const editor::EditorHostWindowHandle handle) noexcept
        {
            return const_cast<HostRecord*>(static_cast<const ManagedEditorUiService*>(this)->FindHost(handle));
        }

        [[nodiscard]] const TextureRecord* FindTexture(const editor::EditorTextureHandle handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_textures.Size() && m_textures[handle.index].active && m_textures[handle.index].generation == handle.generation
                       ? &m_textures[handle.index]
                       : nullptr;
        }

        [[nodiscard]] TextureRecord* FindTexture(const editor::EditorTextureHandle handle) noexcept
        {
            return const_cast<TextureRecord*>(static_cast<const ManagedEditorUiService*>(this)->FindTexture(handle));
        }

        [[nodiscard]] bool ValidHost(const editor::EditorHostWindowDesc& desc) const noexcept
        {
            return desc.window.IsValid() && (desc.presentationOutput.IsValid() == desc.renderViewport.IsValid());
        }

        [[nodiscard]] bool HasConflictingHost(const editor::EditorHostWindowDesc& desc, const editor::EditorHostWindowHandle ignored) const noexcept
        {
            for (const HostRecord& host : m_hosts)
                if (host.active && host.handle != ignored &&
                    (host.desc.window == desc.window || (desc.presentationOutput.IsValid() && host.desc.presentationOutput == desc.presentationOutput) ||
                     (desc.renderViewport.IsValid() && host.desc.renderViewport == desc.renderViewport) || (host.desc.primary && desc.primary)))
                    return true;
            return false;
        }

        [[nodiscard]] bool ValidTexture(const editor::EditorTextureDesc& desc) const noexcept
        {
            return desc.texture.IsValid() && desc.sampler.IsValid() && static_cast<vanguard::u32>(desc.colorSpace) <= static_cast<vanguard::u32>(editor::EditorTextureColorSpace::DisplaySrgb);
        }

        void DestroyPanelState(PanelRecord& panel) noexcept
        {
            PanelTypeRecord* const type = FindType(panel.type);
            if (type != nullptr && type->desc.destroy != nullptr)
                type->desc.destroy(panel.state, type->desc.factoryData);
            panel.id = editor::InvalidEditorPanelInstanceId;
            panel.type = editor::InvalidEditorPanelTypeId;
            panel.title.Clear();
            panel.state = nullptr;
            panel.open = false;
            panel.active = false;
        }

        void DestroyAllPanels() noexcept
        {
            for (PanelRecord& panel : m_panels)
                if (panel.active)
                    DestroyPanelState(panel);
        }

        static vanguard::engine::FrameParticipantStatus ExecuteFrame(const vanguard::engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedEditorUiService*>(userData);
            if (service == nullptr)
                return vanguard::engine::FrameParticipantStatus::Failure("Editor UI frame service is unavailable");
            editor::EditorUiFailure failure;
            if (!service->BeginFrame(context, &failure) || !service->DrawOpenPanels(&failure) || !service->EndFrame(&failure))
            {
                service->AbortFrame();
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message != nullptr ? failure.message : "Editor UI frame failed");
            }
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        static vanguard::engine::FrameParticipantStatus RenderFrame(const vanguard::engine::FrameContext&, void* userData) noexcept
        {
            auto& service = *static_cast<ManagedEditorUiService*>(userData);
            editor::EditorUiFailure failure;
            if (!service.m_renderer.SubmitTextures(service.m_textureFrameData, &failure))
                return vanguard::engine::FrameParticipantStatus::Failure(failure.message);
            // The retained upload frame now owns these pixels, including when all hosts are minimized.
            service.m_textureFrameData.Reset();
            for (const auto& host : service.m_hosts)
                if (host.active && !service.m_renderer.SubmitHost(host.frameData, &failure))
                    return vanguard::engine::FrameParticipantStatus::Failure(failure.message);
            return vanguard::engine::FrameParticipantStatus::Success();
        }

        vanguard::containers::DynamicArray<PanelTypeRecord> m_panelTypes;
        vanguard::containers::DynamicArray<PanelRecord> m_panels;
        vanguard::containers::DynamicArray<HostRecord> m_hosts;
        vanguard::containers::DynamicArray<TextureRecord> m_textures;
        editor::EditorUiTextureFrameData m_textureFrameData;
        editor::detail::EditorUiImGuiPlatform m_platform;
        editor::detail::EditorUiRenderer m_renderer;
        vanguard::engine::FramePipelineService* m_framePipeline = nullptr;
        vanguard::engine::WindowService* m_windows = nullptr;
        vanguard::engine::InputService* m_input = nullptr;
        editor::EditorUiState m_state = editor::EditorUiState::Offline;
        vanguard::u64 m_frame = 0;
    };

    app::Service* CreateEditorUiService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Editor, sizeof(ManagedEditorUiService), alignof(ManagedEditorUiService));
        return block ? ::new (block.address) ManagedEditorUiService() : nullptr;
    }

    void DestroyEditorUiService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedEditorUiService*>(service)->~ManagedEditorUiService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedEditorUiService), vanguard::memory::PoolId::Editor};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::editor
{
    EditorUiFrameData::EditorUiFrameData(const EditorHostWindowHandle host, const u64 frame, const EditorUiFramePayload& payload) noexcept
    {
        if (!host.IsValid() || !payload.IsValid())
            return;
        m_host = host;
        m_frame = frame;
        m_payload = payload;
        m_payload.retain(m_payload.data);
    }

    EditorUiFrameData::~EditorUiFrameData()
    {
        Reset();
    }

    EditorUiFrameData::EditorUiFrameData(const EditorUiFrameData& other) noexcept : m_host(other.m_host), m_frame(other.m_frame), m_payload(other.m_payload)
    {
        if (m_payload.IsValid())
            m_payload.retain(m_payload.data);
    }

    EditorUiFrameData& EditorUiFrameData::operator=(const EditorUiFrameData& other) noexcept
    {
        if (this == &other)
            return *this;
        EditorUiFrameData replacement(other);
        *this = static_cast<EditorUiFrameData&&>(replacement);
        return *this;
    }

    EditorUiFrameData::EditorUiFrameData(EditorUiFrameData&& other) noexcept : m_host(other.m_host), m_frame(other.m_frame), m_payload(other.m_payload)
    {
        other.m_host = {};
        other.m_frame = 0;
        other.m_payload = {};
    }

    EditorUiFrameData& EditorUiFrameData::operator=(EditorUiFrameData&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_host = other.m_host;
        m_frame = other.m_frame;
        m_payload = other.m_payload;
        other.m_host = {};
        other.m_frame = 0;
        other.m_payload = {};
        return *this;
    }

    bool EditorUiFrameData::IsValid() const noexcept
    {
        return m_host.IsValid() && m_payload.IsValid();
    }

    void EditorUiFrameData::Reset() noexcept
    {
        if (m_payload.IsValid())
            m_payload.release(m_payload.data);
        m_host = {};
        m_frame = 0;
        m_payload = {};
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(const u64 frame, const EditorUiFramePayload& payload) noexcept
    {
        if (!payload.IsValid())
            return;
        m_frame = frame;
        m_payload = payload;
        m_payload.retain(m_payload.data);
    }

    EditorUiTextureFrameData::~EditorUiTextureFrameData()
    {
        Reset();
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(const EditorUiTextureFrameData& other) noexcept : m_frame(other.m_frame), m_payload(other.m_payload)
    {
        if (m_payload.IsValid())
            m_payload.retain(m_payload.data);
    }

    EditorUiTextureFrameData& EditorUiTextureFrameData::operator=(const EditorUiTextureFrameData& other) noexcept
    {
        if (this == &other)
            return *this;
        EditorUiTextureFrameData replacement(other);
        *this = static_cast<EditorUiTextureFrameData&&>(replacement);
        return *this;
    }

    EditorUiTextureFrameData::EditorUiTextureFrameData(EditorUiTextureFrameData&& other) noexcept : m_frame(other.m_frame), m_payload(other.m_payload)
    {
        other.m_frame = 0;
        other.m_payload = {};
    }

    EditorUiTextureFrameData& EditorUiTextureFrameData::operator=(EditorUiTextureFrameData&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_frame = other.m_frame;
        m_payload = other.m_payload;
        other.m_frame = 0;
        other.m_payload = {};
        return *this;
    }

    bool EditorUiTextureFrameData::IsValid() const noexcept
    {
        return m_payload.IsValid();
    }

    void EditorUiTextureFrameData::Reset() noexcept
    {
        if (m_payload.IsValid())
            m_payload.release(m_payload.data);
        m_frame = 0;
        m_payload = {};
    }

    bool RegisterEditorUiService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {engine::FramePipelineServiceId, application::DependencyKind::Required},
            {engine::WindowServiceId, application::DependencyKind::Required},
            {engine::InputServiceId, application::DependencyKind::Required},
            {engine::RenderingServiceId, application::DependencyKind::Required},
            {ProjectWorkspaceServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{EditorUiCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = EditorUiServiceId;
        descriptor.name = "editorUi";
        descriptor.profiles = application::ApplicationProfile::Editor | application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::EditorWorkspace;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, static_cast<u32>(sizeof(dependencies) / sizeof(dependencies[0]))};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateEditorUiService;
        descriptor.destroy = DestroyEditorUiService;
        return host.RegisterService(EditorModuleId, descriptor, failure);
    }

    EditorUiService* FindEditorUiService(application::EngineHost& host) noexcept
    {
        return static_cast<EditorUiService*>(host.FindCapability(EditorUiCapabilityId));
    }

    EditorUiService* FindEditorUiService(application::ServiceContext& context) noexcept
    {
        return static_cast<EditorUiService*>(context.FindCapability(EditorUiCapabilityId));
    }
} // namespace vanguard::editor
