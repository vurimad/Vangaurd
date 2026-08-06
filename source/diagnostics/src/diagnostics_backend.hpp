#pragma once

#include <vanguard/diagnostics/diagnostics.hpp>

namespace vanguard::diagnostics::backend
{
    [[nodiscard]] bool Initialize(Mode mode, const char* prefix) noexcept;
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

    void SetSinkCallback(SinkCallback callback, void* userData) noexcept;
    [[nodiscard]] bool OpenFileSink(const char* path, FileMode mode) noexcept;
    void CloseFileSink() noexcept;

    void Log(Level level, Category category, const char* message) noexcept;
    void Flush(FlushMode mode) noexcept;
} // namespace vanguard::diagnostics::backend
