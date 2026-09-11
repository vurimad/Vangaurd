#pragma once

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/rendering/presentation_service.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::editor
{
    inline constexpr application::ServiceId EditorUiServiceId = 0x6564756973657201ull;
    inline constexpr application::CapabilityId EditorUiCapabilityId = 0x6564756963617001ull;
    inline constexpr engine::FrameParticipantId EditorUiFrameParticipantId = 0x6564756966726d01ull;
    inline constexpr engine::FrameParticipantId EditorUiRenderParticipantId = 0x6564756972656e01ull;
    inline constexpr u32 MaximumEditorPanelTypes = 256;
    inline constexpr u32 MaximumEditorPanelInstances = 512;
    inline constexpr u32 MaximumEditorHostWindows = window::MaximumWindows;
    inline constexpr u32 MaximumEditorTextures = 4096;
    inline constexpr u32 MaximumEditorPanelNameBytes = 128;

    using EditorPanelTypeId = u64;
    using EditorPanelInstanceId = u64;

    inline constexpr EditorPanelTypeId InvalidEditorPanelTypeId = 0;
    inline constexpr EditorPanelInstanceId InvalidEditorPanelInstanceId = 0;

    struct EditorPanelHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorPanelInstances && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EditorPanelHandle&, const EditorPanelHandle&) noexcept = default;
    };

    struct EditorHostWindowHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorHostWindows && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EditorHostWindowHandle&, const EditorHostWindowHandle&) noexcept = default;
    };

    struct EditorTextureHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorTextures && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EditorTextureHandle&, const EditorTextureHandle&) noexcept = default;
    };

    inline constexpr EditorPanelHandle InvalidEditorPanelHandle{};
    inline constexpr EditorHostWindowHandle InvalidEditorHostWindowHandle{};
    inline constexpr EditorTextureHandle InvalidEditorTextureHandle{};

    enum class EditorUiState : u8
    {
        Offline,
        Ready,
        BuildingFrame,
        Quiesced
    };

    enum class EditorUiFailureCode : u8
    {
        None,
        NotReady,
        InvalidDescriptor,
        InvalidHandle,
        DuplicateIdentity,
        CapacityExceeded,
        InUse,
        WrongPhase,
        FactoryFailure,
        PlatformFailure
    };

    struct EditorUiFailure
    {
        EditorUiFailureCode code = EditorUiFailureCode::None;
        EditorPanelTypeId panelType = InvalidEditorPanelTypeId;
        EditorPanelInstanceId panelInstance = InvalidEditorPanelInstanceId;
        EditorPanelHandle panel;
        EditorHostWindowHandle host;
        EditorTextureHandle texture;
        const char* message = nullptr;
    };

    enum class EditorTextureColorSpace : u8
    {
        SceneLinear,
        DisplayLinear,
        DisplaySrgb
    };

    struct EditorTextureDesc
    {
        rhi::TextureRef texture;
        rhi::SamplerStateRef sampler;
        EditorTextureColorSpace colorSpace = EditorTextureColorSpace::DisplaySrgb;
    };

    struct ResolvedEditorTexture
    {
        rhi::TextureRef texture;
        rhi::SamplerStateRef sampler;
        EditorTextureColorSpace colorSpace = EditorTextureColorSpace::DisplaySrgb;
    };

    struct EditorHostWindowDesc
    {
        window::WindowHandle window;
        rendering::PresentationOutputHandle presentationOutput;
        rendering::RenderViewportHandle renderViewport;
        bool primary = false;

        [[nodiscard]] constexpr bool HasPresentation() const noexcept
        {
            return presentationOutput.IsValid() && renderViewport.IsValid();
        }
    };

    struct EditorHostWindowInfo
    {
        EditorHostWindowHandle handle;
        window::WindowHandle window;
        rendering::PresentationOutputHandle presentationOutput;
        rendering::RenderViewportHandle renderViewport;
        bool primary = false;
    };

    class EditorUiService;

    class EditorPanelContext final
    {
    public:
        EditorPanelContext(EditorUiService& ui, EditorPanelHandle panel, EditorPanelInstanceId instance) noexcept
            : m_ui(&ui), m_panel(panel), m_instance(instance)
        {
        }

        [[nodiscard]] EditorUiService& GetUi() const noexcept { return *m_ui; }
        [[nodiscard]] EditorPanelHandle GetPanel() const noexcept { return m_panel; }
        [[nodiscard]] EditorPanelInstanceId GetInstanceId() const noexcept { return m_instance; }

    private:
        EditorUiService* m_ui = nullptr;
        EditorPanelHandle m_panel;
        EditorPanelInstanceId m_instance = InvalidEditorPanelInstanceId;
    };

    using CreateEditorPanel = void* (*)(EditorPanelInstanceId instance, void* factoryData) noexcept;
    using DestroyEditorPanel = void (*)(void* panel, void* factoryData) noexcept;
    using DrawEditorPanel = void (*)(EditorPanelContext& context, void* panel, void* factoryData) noexcept;

    struct EditorPanelTypeDesc
    {
        EditorPanelTypeId id = InvalidEditorPanelTypeId;
        const char* name = nullptr;
        CreateEditorPanel create = nullptr;
        DestroyEditorPanel destroy = nullptr;
        DrawEditorPanel draw = nullptr;
        void* factoryData = nullptr;
    };

    struct EditorPanelDesc
    {
        EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        const char* title = nullptr;
        bool open = true;
    };

    struct EditorPanelInfo
    {
        EditorPanelHandle handle;
        EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        const char* title = nullptr;
        bool open = false;
    };

    using EditorPanelVisitor = void (*)(const EditorPanelInfo& panel, void* userData) noexcept;
    using EditorHostWindowVisitor = void (*)(const EditorHostWindowInfo& host, void* userData) noexcept;

    class EditorPanelRegistry
    {
    public:
        virtual ~EditorPanelRegistry() = default;

        [[nodiscard]] virtual bool RegisterType(const EditorPanelTypeDesc& desc, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterType(EditorPanelTypeId type, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool CreatePanel(const EditorPanelDesc& desc, EditorPanelHandle& panel, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool DestroyPanel(EditorPanelHandle panel, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool SetOpen(EditorPanelHandle panel, bool open, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool IsOpen(EditorPanelHandle panel) const noexcept = 0;
        virtual void VisitPanels(EditorPanelVisitor visitor, void* userData = nullptr) const noexcept = 0;

    protected:
        EditorPanelRegistry() noexcept = default;
    };

    struct EditorUiFramePayload
    {
        void* data = nullptr;
        void (*retain)(void* data) noexcept = nullptr;
        void (*release)(void* data) noexcept = nullptr;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return data != nullptr && retain != nullptr && release != nullptr;
        }
    };

    class EditorUiFrameData final
    {
    public:
        EditorUiFrameData() noexcept = default;
        EditorUiFrameData(EditorHostWindowHandle host, u64 frame, const EditorUiFramePayload& payload) noexcept;
        ~EditorUiFrameData();
        EditorUiFrameData(const EditorUiFrameData& other) noexcept;
        EditorUiFrameData& operator=(const EditorUiFrameData& other) noexcept;
        EditorUiFrameData(EditorUiFrameData&& other) noexcept;
        EditorUiFrameData& operator=(EditorUiFrameData&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] EditorHostWindowHandle GetHost() const noexcept { return m_host; }
        [[nodiscard]] u64 GetFrame() const noexcept { return m_frame; }
        [[nodiscard]] const EditorUiFramePayload& GetPayload() const noexcept { return m_payload; }
        void Reset() noexcept;

    private:
        EditorHostWindowHandle m_host;
        u64 m_frame = 0;
        EditorUiFramePayload m_payload;
    };

    class EditorUiTextureFrameData final
    {
    public:
        EditorUiTextureFrameData() noexcept = default;
        EditorUiTextureFrameData(u64 frame, const EditorUiFramePayload& payload) noexcept;
        ~EditorUiTextureFrameData();
        EditorUiTextureFrameData(const EditorUiTextureFrameData& other) noexcept;
        EditorUiTextureFrameData& operator=(const EditorUiTextureFrameData& other) noexcept;
        EditorUiTextureFrameData(EditorUiTextureFrameData&& other) noexcept;
        EditorUiTextureFrameData& operator=(EditorUiTextureFrameData&& other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] u64 GetFrame() const noexcept { return m_frame; }
        [[nodiscard]] const EditorUiFramePayload& GetPayload() const noexcept { return m_payload; }
        void Reset() noexcept;

    private:
        u64 m_frame = 0;
        EditorUiFramePayload m_payload;
    };

    class EditorUiService : public application::Service
    {
    public:
        ~EditorUiService() override = default;

        // Registry and frame mutation are owned by the editor's main-thread participant. Panel callbacks are
        // authored once per UI frame; finalized render data is partitioned by native host afterward.
        [[nodiscard]] virtual EditorPanelRegistry& GetPanels() noexcept = 0;
        [[nodiscard]] virtual const EditorPanelRegistry& GetPanels() const noexcept = 0;
        [[nodiscard]] virtual bool RegisterHost(const EditorHostWindowDesc& desc, EditorHostWindowHandle& host, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UpdateHost(EditorHostWindowHandle host, const EditorHostWindowDesc& desc, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterHost(EditorHostWindowHandle host, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool GetHost(EditorHostWindowHandle host, EditorHostWindowInfo& info) const noexcept = 0;
        virtual void VisitHosts(EditorHostWindowVisitor visitor, void* userData = nullptr) const noexcept = 0;
        [[nodiscard]] virtual bool RegisterTexture(const EditorTextureDesc& desc, EditorTextureHandle& texture, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterTexture(EditorTextureHandle texture, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool ResolveTexture(EditorTextureHandle texture, ResolvedEditorTexture& resolved) const noexcept = 0;
        // Acquire on the editor frame owner after EndFrame and before dispatch. The retained result may then move to workers.
        [[nodiscard]] virtual bool AcquireFrameData(EditorHostWindowHandle host, EditorUiFrameData& frameData) const noexcept = 0;
        // Texture uploads are frame-global and must be consumed once before any host draw data from the same frame.
        [[nodiscard]] virtual bool AcquireTextureFrameData(EditorUiTextureFrameData& frameData) const noexcept = 0;
        [[nodiscard]] virtual bool BeginFrame(const engine::FrameContext& frame, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool DrawOpenPanels(EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool EndFrame(EditorUiFailure* failure = nullptr) noexcept = 0;
        virtual void AbortFrame() noexcept = 0;
        [[nodiscard]] virtual EditorUiState GetState() const noexcept = 0;
        [[nodiscard]] virtual u64 GetFrame() const noexcept = 0;

    protected:
        EditorUiService() noexcept = default;
    };

    [[nodiscard]] bool RegisterEditorUiService(application::EngineHost& host, application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUiService* FindEditorUiService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorUiService* FindEditorUiService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
