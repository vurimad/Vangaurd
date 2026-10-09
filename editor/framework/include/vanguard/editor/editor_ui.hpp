#pragma once

#include <vanguard/editor/editor_service_ids.hpp>

#include <vanguard/application/engine_host.hpp>
#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/rendering/presentation_service.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/window/window_types.hpp>

namespace vanguard::editor
{
    struct EditorExtensionOwner;
    class EditorExtensionRegistrationAccess;
    inline constexpr engine::FrameParticipantId EditorUiFrameParticipantId = 0x6564756966726d01ull;
    inline constexpr engine::FrameParticipantId EditorUiRenderParticipantId = 0x6564756972656e01ull;
    inline constexpr u32 MaximumEditorPanelTypes = 256;
    inline constexpr u32 MaximumEditorPanelInstances = 512;
    inline constexpr u32 MaximumEditorHostWindows = window::MaximumWindows;
    inline constexpr u32 MaximumEditorTextures = 4096;
    inline constexpr u32 MaximumEditorPanelNameBytes = 128;
    inline constexpr u32 MaximumEditorPanelStateBytes = 64u * 1024u;
    inline constexpr u32 MaximumEditorUiSurfaces = 64;
    inline constexpr u32 MaximumEditorUiContributions = 512;
    inline constexpr u32 MaximumEditorUiSurfaceDepth = 8;
    inline constexpr u32 MaximumEditorUiSurfaceStringBytes = 128;
    inline constexpr u32 MaximumPendingEditorContextMenuRequests = 16;

    using EditorPanelTypeId = u64;
    using EditorPanelInstanceId = u64;

    inline constexpr EditorPanelTypeId InvalidEditorPanelTypeId = 0;
    inline constexpr EditorPanelInstanceId InvalidEditorPanelInstanceId = 0;

    struct EditorUiSurfaceId final
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorUiSurfaceId, EditorUiSurfaceId) noexcept = default;
    };

    struct EditorUiContributionId final
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(EditorUiContributionId, EditorUiContributionId) noexcept = default;
    };

    inline constexpr EditorUiSurfaceId InvalidEditorUiSurfaceId{};
    inline constexpr EditorUiContributionId InvalidEditorUiContributionId{};

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

    struct EditorPanelTypeToken
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < MaximumEditorPanelTypes && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const EditorPanelTypeToken&, const EditorPanelTypeToken&) noexcept = default;
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

    inline constexpr EditorPanelTypeToken InvalidEditorPanelTypeToken{};
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
        PlatformFailure,
        WrongThread,
        InvalidPolicy,
        GenerationExhausted,
        SingletonViolation,
        OwnerRevoked,
        InvalidContextAssociation,
        FocusSynchronizationFailure,
        MissingAnchor,
        OwnerMismatch,
        HierarchyViolation,
        SurfaceRequestConflict,
        SurfaceCommandFailure
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
        EditorUiSurfaceId surface;
        EditorUiContributionId contribution;
    };

    enum class EditorTextureColorSpace : u8
    {
        SceneLinear,
        DisplayLinear,
        DisplaySrgb
    };

    using ResolveEditorTextureOutputReceipt = bool (*)(void* userData, rendering::RenderOutputReceipt& receipt) noexcept;

    /// Optional deferred publication contract for renderer-produced editor textures.
    /// `viewport`/`outputRevision` identify the immutable captured generation. The resolver is
    /// called later from retained render processing and must resolve the exact currently accepted
    /// producer epoch at that point; it must not return readiness captured during UI authoring.
    /// The registration owner keeps `userData` alive until every UI frame that captured this
    /// publication has crossed its retained import/submission boundary.
    struct EditorTextureRenderOutputPublication
    {
        rendering::RenderViewportHandle viewport;
        u64 outputRevision = 0;
        ResolveEditorTextureOutputReceipt resolveReceipt = nullptr;
        void* userData = nullptr;

        [[nodiscard]] constexpr bool IsSpecified() const noexcept
        {
            return viewport.IsValid() || outputRevision != 0 || resolveReceipt != nullptr || userData != nullptr;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return viewport.IsValid() && outputRevision != 0 && resolveReceipt != nullptr && userData != nullptr;
        }

        [[nodiscard]] bool ResolveReceipt(rendering::RenderOutputReceipt& receipt) const noexcept
        {
            receipt = {};
            return IsValid() && resolveReceipt(userData, receipt);
        }
    };

    struct EditorTextureDesc
    {
        rhi::TextureRef texture;
        rhi::SamplerStateRef sampler;
        EditorTextureColorSpace colorSpace = EditorTextureColorSpace::DisplaySrgb;
        EditorTextureRenderOutputPublication publication;
    };

    struct ResolvedEditorTexture
    {
        rhi::TextureRef texture;
        rhi::SamplerStateRef sampler;
        EditorTextureColorSpace colorSpace = EditorTextureColorSpace::DisplaySrgb;
        EditorTextureRenderOutputPublication publication;
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
        // Host that is authoring this panel for the current UI frame. Valid only
        // for contexts created by EditorUiService while drawing live panels.
        [[nodiscard]] EditorHostWindowHandle GetHost() const noexcept { return m_host; }
        [[nodiscard]] bool RequestContextMenu(EditorUiSurfaceId surface, EditorUiFailure* failure = nullptr) const noexcept;

    private:
        EditorPanelContext(EditorUiService& ui, EditorPanelHandle panel, EditorPanelInstanceId instance,
                           u64 authoringFrame, EditorHostWindowHandle host) noexcept
            : m_ui(&ui), m_panel(panel), m_instance(instance), m_host(host), m_authoringFrame(authoringFrame)
        {
        }

        EditorUiService* m_ui = nullptr;
        EditorPanelHandle m_panel;
        EditorPanelInstanceId m_instance = InvalidEditorPanelInstanceId;
        EditorHostWindowHandle m_host;
        u64 m_authoringFrame = 0;

        friend class EditorUiService;
    };

    using CreateEditorPanel = void* (*)(EditorPanelInstanceId instance, void* factoryData) noexcept;
    using DestroyEditorPanel = void (*)(void* panel, void* factoryData) noexcept;
    using DrawEditorPanel = void (*)(EditorPanelContext& context, void* panel, void* factoryData) noexcept;
    using EditorPanelVisibilityChanged = void (*)(void* panel, bool open, void* factoryData) noexcept;

    enum class EditorPanelStateScope : u8
    {
        Presentation,
        Content
    };

    enum class EditorPanelStateResult : u8
    {
        Success,
        NoState,
        ProviderUnavailable,
        InvalidData,
        UnsupportedVersion,
        CapacityExceeded,
        CallbackFailure
    };

    using CaptureEditorPanelState = EditorPanelStateResult (*)(
        EditorPanelInstanceId instance, void* panel, EditorPanelStateScope scope, void* destination, u32 capacity,
        u32& byteCount, void* factoryData) noexcept;
    using RestoreEditorPanelState = EditorPanelStateResult (*)(
        EditorPanelInstanceId instance, void* panel, EditorPanelStateScope scope, u32 version,
        containers::ArraySpan<const u8> bytes, void* factoryData) noexcept;
    using MigrateEditorPanelState = EditorPanelStateResult (*)(
        EditorPanelInstanceId instance, void* panel, EditorPanelStateScope scope, u32 fromVersion,
        containers::ArraySpan<const u8> source, void* destination, u32 capacity,
        u32& byteCount, void* factoryData) noexcept;
    using GetEditorPanelStateRevision = u64 (*)(EditorPanelInstanceId instance, void* panel, void* factoryData) noexcept;

    struct EditorPanelPersistenceDesc final
    {
        // Zero means this panel type has no generic persisted state. When nonzero,
        // capture/restore/revision are required and migration is optional. Revision values are
        // monotonic and nonzero; zero means the provider is temporarily unable to report state.
        u32 stateVersion = 0;
        CaptureEditorPanelState capture = nullptr;
        RestoreEditorPanelState restore = nullptr;
        MigrateEditorPanelState migrate = nullptr;
        GetEditorPanelStateRevision getRevision = nullptr;

        [[nodiscard]] constexpr bool IsEnabled() const noexcept { return stateVersion != 0; }
    };

    enum class EditorPanelTypePolicy : u8
    {
        Multiple,
        Singleton
    };

    enum class EditorPanelInitialPlacement : u8
    {
        Center,
        Left,
        Right,
        Bottom,
        Floating
    };

    struct EditorPanelTypeDesc
    {
        EditorPanelTypeId id = InvalidEditorPanelTypeId;
        const char* name = nullptr;
        CreateEditorPanel create = nullptr;
        DestroyEditorPanel destroy = nullptr;
        DrawEditorPanel draw = nullptr;
        void* factoryData = nullptr;
        EditorPanelTypePolicy policy = EditorPanelTypePolicy::Multiple;
        EditorPanelInstanceId defaultInstance = InvalidEditorPanelInstanceId;
        EditorPanelInitialPlacement initialPlacement = EditorPanelInitialPlacement::Center;
        EditorPanelVisibilityChanged visibilityChanged = nullptr;
        EditorPanelPersistenceDesc persistence;
    };

    struct EditorPanelDesc
    {
        EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        const char* title = nullptr;
        bool open = true;
    };

    struct EditorPanelPlaceholderDesc
    {
        EditorPanelTypeId unresolvedType = InvalidEditorPanelTypeId;
        EditorPanelInstanceId id = InvalidEditorPanelInstanceId;
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
        bool placeholder = false;
    };

    struct EditorPanelFocusSnapshot final
    {
        u64 frame = 0;
        EditorPanelHandle panel;
        EditorPanelTypeId type = InvalidEditorPanelTypeId;
        EditorPanelInstanceId instance = InvalidEditorPanelInstanceId;
        EditorHostWindowHandle host;

        [[nodiscard]] constexpr bool HasFocus() const noexcept { return panel.IsValid(); }
    };

    namespace detail
    {
        struct EditorUiSurfaceDesc;
        struct EditorUiSurfaceToken;
        struct EditorUiContributionDesc;
        struct EditorUiContributionToken;
        struct EditorUiCommandBridge;
        struct EditorUiCommandBridgeToken;

        struct EditorPanelFocusSinkToken final
        {
            u32 generation = 0;

            [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
            [[nodiscard]] friend constexpr bool operator==(EditorPanelFocusSinkToken, EditorPanelFocusSinkToken) noexcept = default;
        };

        struct EditorPanelFocusUpdate final
        {
            EditorPanelFocusSnapshot focus;
            void* receiverData = nullptr;
        };

        using EditorPanelFocusSink = bool (*)(const EditorPanelFocusUpdate& update, void* userData,
                                              EditorUiFailure* failure) noexcept;
    } // namespace detail

    using EditorPanelVisitor = void (*)(const EditorPanelInfo& panel, void* userData) noexcept;
    using EditorHostWindowVisitor = void (*)(const EditorHostWindowInfo& host, void* userData) noexcept;

    class EditorPanelRegistry
    {
    public:
        virtual ~EditorPanelRegistry() = default;

        [[nodiscard]] virtual bool RegisterType(const EditorPanelTypeDesc& desc, EditorPanelTypeToken& token,
                                                EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterType(EditorPanelTypeToken token, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool CreatePanel(const EditorPanelDesc& desc, EditorPanelHandle& panel, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool CreatePlaceholder(const EditorPanelPlaceholderDesc& desc, EditorPanelHandle& panel,
                                                     EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool DestroyPanel(EditorPanelHandle panel, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool SetOpen(EditorPanelHandle panel, bool open, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual bool IsOpen(EditorPanelHandle panel) const noexcept = 0;
        virtual void VisitPanels(EditorPanelVisitor visitor, void* userData = nullptr) const noexcept = 0;

    protected:
        EditorPanelRegistry() noexcept = default;

    private:
        [[nodiscard]] virtual bool RegisterTypeForOwner(const EditorExtensionOwner*, const EditorPanelTypeDesc&,
                                                        EditorPanelTypeToken& token, EditorUiFailure*) noexcept
        {
            token = {};
            return false;
        }
        [[nodiscard]] virtual bool UnregisterTypeForOwner(const EditorExtensionOwner*, EditorPanelTypeToken,
                                                          EditorUiFailure*) noexcept
        {
            return false;
        }
        [[nodiscard]] virtual bool CanRevokeOwner(const EditorExtensionOwner&) const noexcept { return true; }
        virtual void RevokeOwner(const EditorExtensionOwner&) noexcept {}

        friend class EditorExtensionRegistrationAccess;
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

    struct EditorUiInputCapture final
    {
        u64 frame = 0;
        bool textInputCaptured = false;
        bool keyboardCaptured = false;
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
        [[nodiscard]] virtual EditorUiInputCapture GetInputCapture() const noexcept = 0;
        [[nodiscard]] virtual EditorPanelFocusSnapshot GetPanelFocus() const noexcept = 0;
        [[nodiscard]] virtual f32 GetUserScale() const noexcept = 0;
        [[nodiscard]] virtual bool SetUserScale(f32 scale, EditorUiFailure* failure = nullptr) noexcept = 0;
        [[nodiscard]] virtual EditorUiState GetState() const noexcept = 0;
        [[nodiscard]] virtual u64 GetFrame() const noexcept = 0;

    protected:
        EditorUiService() noexcept = default;
        [[nodiscard]] EditorPanelContext MakePanelContext(EditorPanelHandle panel, EditorPanelInstanceId instance,
                                                         u64 authoringFrame, EditorHostWindowHandle host = {}) noexcept
        {
            return EditorPanelContext(*this, panel, instance, authoringFrame, host);
        }

    private:
        [[nodiscard]] virtual bool RegisterSurfaceForOwner(const EditorExtensionOwner& owner,
                                                          const detail::EditorUiSurfaceDesc& desc,
                                                          detail::EditorUiSurfaceToken& token,
                                                          EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterSurfaceForOwner(const EditorExtensionOwner& owner,
                                                            const detail::EditorUiSurfaceToken& token,
                                                            EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterContributionForOwner(const EditorExtensionOwner& owner,
                                                               const detail::EditorUiContributionDesc& desc,
                                                               detail::EditorUiContributionToken& token,
                                                               EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterContributionForOwner(const EditorExtensionOwner& owner,
                                                                 const detail::EditorUiContributionToken& token,
                                                                 EditorUiFailure* failure) noexcept = 0;
        virtual void RevokeSurfaceOwner(const EditorExtensionOwner& owner) noexcept = 0;
        [[nodiscard]] virtual bool RegisterSurfaceCommandBridge(const detail::EditorUiCommandBridge& bridge,
                                                              detail::EditorUiCommandBridgeToken& token,
                                                              EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterSurfaceCommandBridge(const detail::EditorUiCommandBridgeToken& token,
                                                                EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RegisterPanelFocusSink(detail::EditorPanelFocusSink sink, void* userData,
                                                         detail::EditorPanelFocusSinkToken& token,
                                                         EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterPanelFocusSink(detail::EditorPanelFocusSinkToken token,
                                                           EditorUiFailure* failure) noexcept = 0;
        [[nodiscard]] virtual bool RequestContextMenuForPanel(EditorPanelHandle panel, u64 authoringFrame,
                                                             EditorUiSurfaceId surface,
                                                             EditorUiFailure* failure) noexcept = 0;

        friend class EditorExtensionRegistrationAccess;
        friend class EditorPanelContext;
    };

    [[nodiscard]] bool RegisterEditorUiService(application::EngineHost& host, application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] EditorUiService* FindEditorUiService(application::EngineHost& host) noexcept;
    [[nodiscard]] EditorUiService* FindEditorUiService(application::ServiceContext& context) noexcept;
} // namespace vanguard::editor
