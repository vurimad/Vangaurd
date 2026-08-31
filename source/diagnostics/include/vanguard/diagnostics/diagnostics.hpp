#pragma once

#include <vanguard/system/types.hpp>

#include <cstdarg>

namespace vanguard::diagnostics
{
    inline constexpr usize MaxMessageLength = 3072;

    enum class Mode : u8
    {
        Asynchronous,
        Synchronous
    };

    enum class Level : u8
    {
        Fatal,
        Error,
        Warning,
        Info,
        Debug,
        Trace
    };

    // This initial category set preserves the complete imported logger
    // contract. New Vanguard categories require an explicit backend mapping.
    enum class Category : u8
    {
        Default,
        AI,
        Animation,
        Audio,
        Engine,
        Gameplay,
        GameplayProfile,
        GameStateMachine,
        Physics,
        Rendering,
        Scripts,
        ScriptRuntimeErrors,
        Tools,
        Core,
        Interop,
        Input,
        TweakDB,
        Math,
        ItemFactory,
        Localization,
        UserInterface,
        ObjectPool,
        Scenes,
        DataProcessing,
        EntityValidation,
        Jobs,
        Navigation,
        Traffic,
        Population,
        PlayerLocomotion,
        Services,
        Resources,
        Multiplayer,
        Effects,
        Workspots,
        DistributedProcess,
        DataBuild,
        Entity,
        SmartObjects,
        Scanning,
        Interactions,
        Muppet,
        PingSystem,
        Chatter,
        ScreenshotTool,
        AIDirector,
        ResourceLookupTable,
        PlayerBreadcrumbs,
        FunctionalTests,
        Characters,
        GPS,
        PlayerManager,
        Mounting,
        Projectiles,
        Telemetry,
        Garment,
        Count
    };

    enum class FlushMode : u8
    {
        Asynchronous,
        Synchronous
    };

    enum class FileMode : u8
    {
        Truncate,
        Append
    };

    struct MessageView
    {
        const char* formatted = nullptr;
        const char* message = nullptr;
        const char* prefix = nullptr;
        u64 frame = 0;
        u32 threadId = 0;
        u32 threadContextId = 0;
        u32 networkPeerId = 0;
        Level level = Level::Info;
        Category category = Category::Default;
    };

    // MessageView strings are borrowed and remain valid only for the duration
    // of the callback. In asynchronous mode this callback runs on the logger
    // worker thread.
    using SinkCallback = void (*)(const MessageView& message, void* userData) noexcept;
    using FrameNumberRetriever = u64 (*)();

    [[nodiscard]] bool Initialize(Mode mode = Mode::Asynchronous, const char* prefix = nullptr) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() noexcept;

    void Enable() noexcept;
    void Disable() noexcept;
    [[nodiscard]] bool IsEnabled() noexcept;

    void SetLevel(Level level) noexcept;
    void RestoreDefaultLevel() noexcept;
    void EnableCategory(Category category, bool enabled) noexcept;
    [[nodiscard]] bool CanLog(Level level, Category category) noexcept;

    void SetThreadContextId(u32 contextId) noexcept;
    void ClearThreadContextId() noexcept;
    void SetFrameNumberRetriever(FrameNumberRetriever retriever) noexcept;
    void SetNetworkPeerId(u32 peerId) noexcept;
    void ClearNetworkPeerId() noexcept;

    void SetSinkCallback(SinkCallback callback, void* userData = nullptr) noexcept;

    [[nodiscard]] bool OpenFileSink(const char* path, FileMode mode = FileMode::Truncate) noexcept;
    void CloseFileSink() noexcept;

    void Log(Level level, Category category, const char* message) noexcept;
    void LogV(Level level, Category category, const char* format, std::va_list arguments) noexcept;
    void Logf(Level level, Category category, const char* format, ...) noexcept;

    void Flush(FlushMode mode = FlushMode::Asynchronous) noexcept;
} // namespace vanguard::diagnostics

#define VG_INTERNAL_LOG(levelValue, categoryValue, ...)                                                                                                        \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        const ::vanguard::diagnostics::Category vgLogCategory = (categoryValue);                                                                               \
        if (::vanguard::diagnostics::CanLog((levelValue), vgLogCategory))                                                                                      \
            ::vanguard::diagnostics::Logf((levelValue), vgLogCategory, __VA_ARGS__);                                                                           \
    } while (false)

#define VG_LOG_FATAL(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Fatal, category, __VA_ARGS__)
#define VG_LOG_ERROR(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Error, category, __VA_ARGS__)
#define VG_LOG_WARNING(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Warning, category, __VA_ARGS__)
#define VG_LOG_INFO(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Info, category, __VA_ARGS__)
#define VG_LOG_DEBUG(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Debug, category, __VA_ARGS__)
#define VG_LOG_TRACE(category, ...) VG_INTERNAL_LOG(::vanguard::diagnostics::Level::Trace, category, __VA_ARGS__)
