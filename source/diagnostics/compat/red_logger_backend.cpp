#include "build.h"

#include <vanguard/diagnostics/diagnostics_backend.hpp>

#include "log.h"
#include "logMessage.h"
#include "loggerFileSink.h"
#include "loggerSink.h"

#include <atomic>

namespace
{
    using namespace vanguard::diagnostics;

    static_assert(static_cast<vanguard::u8>(Level::Trace) == static_cast<vanguard::u8>(red::LoggerLevel_Trace));
    static_assert(static_cast<vanguard::u8>(Category::Count) == static_cast<vanguard::u8>(red::LoggerCategory_MAX));

    [[nodiscard]] red::LoggerMode ToImported(const Mode mode) noexcept
    {
        return mode == Mode::Asynchronous ? red::LoggerMode_Async : red::LoggerMode_Sync;
    }

    [[nodiscard]] red::LoggerLevel ToImported(const Level level) noexcept
    {
        return static_cast<red::LoggerLevel>(level);
    }

    [[nodiscard]] red::LoggerCategory ToImported(const Category category) noexcept
    {
        return static_cast<red::LoggerCategory>(category);
    }

    class VanguardSink final : public red::LoggerSink
    {
    public:
        void SinkLogLine(const char* const formattedMessage, const red::LoggerLine& message) override
        {
            const SinkCallback callback = m_callback.load(std::memory_order_acquire);
            if (callback == nullptr)
            {
                return;
            }

            MessageView view = {formattedMessage,
                                message.buffer,
                                message.prefix,
                                message.frame,
                                message.threadId,
                                message.threadContextId,
                                message.netPeerId,
                                static_cast<Level>(message.level),
                                static_cast<Category>(message.category)};

            callback(view, m_userData.load(std::memory_order_acquire));
        }

        void Flush() override {}

        void SetCallback(const SinkCallback callback, void* const userData) noexcept
        {
            m_userData.store(userData, std::memory_order_release);
            m_callback.store(callback, std::memory_order_release);
        }

    private:
        std::atomic<SinkCallback> m_callback = nullptr;
        std::atomic<void*> m_userData = nullptr;
    };

    bool g_initialized = false;
    bool g_fileSinkRegistered = false;
    VanguardSink g_vanguardSink;
    red::LoggerFileSink g_fileSink;
} // namespace

namespace vanguard::diagnostics::backend
{
    bool Initialize(const Mode mode, const char* const prefix) noexcept
    {
        if (g_initialized)
        {
            return true;
        }

        red::InitializeLogger(ToImported(mode), prefix);
        red::RegisterFilteredLoggerSink(&g_vanguardSink);
        g_initialized = true;
        return true;
    }

    void Shutdown() noexcept
    {
        if (!g_initialized)
        {
            return;
        }

        CloseFileSink();
        red::LogFlush(red::LoggerFlushMode_Sync);
        red::UnregisterFilteredLoggerSink(&g_vanguardSink);
        red::UninitializeLogger();
        g_initialized = false;
    }

    bool IsInitialized() noexcept
    {
        return g_initialized;
    }

    void Enable() noexcept
    {
        if (g_initialized)
        {
            red::EnableLogging();
        }
    }

    void Disable() noexcept
    {
        if (g_initialized)
        {
            red::DisableLogging();
        }
    }

    bool IsEnabled() noexcept
    {
        return g_initialized && red::IsLoggingEnabled();
    }

    void SetLevel(const Level level) noexcept
    {
        if (g_initialized)
        {
            red::SetLogLevel(ToImported(level));
        }
    }

    void RestoreDefaultLevel() noexcept
    {
        if (g_initialized)
        {
            red::RestoreDefaultLogLevel();
        }
    }

    void EnableCategory(const Category category, const bool enabled) noexcept
    {
        if (g_initialized && category < Category::Count)
        {
            red::EnableLogCategory(ToImported(category), enabled);
        }
    }

    bool CanLog(const Level level, const Category category) noexcept
    {
        return g_initialized && category < Category::Count && red::CanPrintLog(ToImported(level), ToImported(category));
    }

    void SetThreadContextId(const u32 contextId) noexcept
    {
        if (g_initialized)
        {
            red::SetLogThreadContextId(contextId);
        }
    }

    void ClearThreadContextId() noexcept
    {
        if (g_initialized)
        {
            red::SetLogDefaultThreadContextId();
        }
    }

    void SetFrameNumberRetriever(const FrameNumberRetriever retriever) noexcept
    {
        if (g_initialized && retriever != nullptr)
        {
            red::SetFrameNumberRetriever(retriever);
        }
    }

    void SetNetworkPeerId(const u32 peerId) noexcept
    {
        if (g_initialized)
        {
            red::SetLogNetPeerId(peerId);
        }
    }

    void ClearNetworkPeerId() noexcept
    {
        if (g_initialized)
        {
            red::SetLogDefaultNetPeerId();
        }
    }

    void SetSinkCallback(const SinkCallback callback, void* const userData) noexcept
    {
        g_vanguardSink.SetCallback(callback, userData);
    }

    bool OpenFileSink(const char* const path, const FileMode mode) noexcept
    {
        if (!g_initialized || path == nullptr || path[0] == '\0')
        {
            return false;
        }

        CloseFileSink();
        const char* const importedMode = mode == FileMode::Append ? "a" : "w";

        if (!g_fileSink.OpenFile(path, importedMode))
        {
            return false;
        }

        red::RegisterLoggerSink(&g_fileSink);
        g_fileSinkRegistered = true;
        return true;
    }

    void CloseFileSink() noexcept
    {
        if (g_fileSinkRegistered)
        {
            red::UnregisterLoggerSink(&g_fileSink);
            g_fileSinkRegistered = false;
        }

        g_fileSink.CloseFile();
    }

    void Log(const Level level, const Category category, const char* const message) noexcept
    {
        if (vanguard::diagnostics::backend::CanLog(level, category) && message != nullptr)
        {
            red::LogMessage(ToImported(level), message, ToImported(category));
        }
    }

    void Flush(const FlushMode mode) noexcept
    {
        if (g_initialized)
        {
            red::LogFlush(mode == FlushMode::Synchronous ? red::LoggerFlushMode_Sync : red::LoggerFlushMode_Async);
        }
    }
} // namespace vanguard::diagnostics::backend
