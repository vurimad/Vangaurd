#include <vanguard/diagnostics/diagnostics.hpp>

#include "diagnostics_backend.hpp"

#include <cstdio>

namespace vanguard::diagnostics
{
    bool Initialize(const Mode mode, const char* const prefix) noexcept
    {
        return backend::Initialize(mode, prefix);
    }

    void Shutdown() noexcept
    {
        backend::Shutdown();
    }

    bool IsInitialized() noexcept
    {
        return backend::IsInitialized();
    }

    void Enable() noexcept
    {
        backend::Enable();
    }

    void Disable() noexcept
    {
        backend::Disable();
    }

    bool IsEnabled() noexcept
    {
        return backend::IsEnabled();
    }

    void SetLevel(const Level level) noexcept
    {
        backend::SetLevel(level);
    }

    void RestoreDefaultLevel() noexcept
    {
        backend::RestoreDefaultLevel();
    }

    void EnableCategory(const Category category, const bool enabled) noexcept
    {
        backend::EnableCategory(category, enabled);
    }

    bool CanLog(const Level level, const Category category) noexcept
    {
        return backend::CanLog(level, category);
    }

    void SetThreadContextId(const u32 contextId) noexcept
    {
        backend::SetThreadContextId(contextId);
    }

    void ClearThreadContextId() noexcept
    {
        backend::ClearThreadContextId();
    }

    void SetFrameNumberRetriever(const FrameNumberRetriever retriever) noexcept
    {
        backend::SetFrameNumberRetriever(retriever);
    }

    void SetNetworkPeerId(const u32 peerId) noexcept
    {
        backend::SetNetworkPeerId(peerId);
    }

    void ClearNetworkPeerId() noexcept
    {
        backend::ClearNetworkPeerId();
    }

    void SetSinkCallback(const SinkCallback callback, void* const userData) noexcept
    {
        backend::SetSinkCallback(callback, userData);
    }

    bool OpenFileSink(const char* const path, const FileMode mode) noexcept
    {
        return backend::OpenFileSink(path, mode);
    }

    void CloseFileSink() noexcept
    {
        backend::CloseFileSink();
    }

    void Log(const Level level, const Category category, const char* const message) noexcept
    {
        if (message != nullptr)
        {
            backend::Log(level, category, message);
        }
    }

    void LogV(const Level level, const Category category, const char* const format, std::va_list arguments) noexcept
    {
        if (format == nullptr || !CanLog(level, category))
        {
            return;
        }

        char message[MaxMessageLength] = {};
        const int result = std::vsnprintf(message, sizeof(message), format, arguments);

        if (result < 0)
        {
            return;
        }

        message[MaxMessageLength - 1] = '\0';
        backend::Log(level, category, message);
    }

    void Logf(const Level level, const Category category, const char* const format, ...) noexcept
    {
        std::va_list arguments;
        va_start(arguments, format);
        LogV(level, category, format, arguments);
        va_end(arguments);
    }

    void Flush(const FlushMode mode) noexcept
    {
        backend::Flush(mode);
    }
} // namespace vanguard::diagnostics
