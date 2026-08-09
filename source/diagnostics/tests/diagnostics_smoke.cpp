#include <vanguard/diagnostics/diagnostics.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
    struct CapturedMessages
    {
        vanguard::u32 count = 0;
        vanguard::diagnostics::Level lastLevel = vanguard::diagnostics::Level::Info;
        vanguard::diagnostics::Category lastCategory = vanguard::diagnostics::Category::Default;
        char lastMessage[128] = {};
    };

    void Capture(const vanguard::diagnostics::MessageView& message, void* const userData) noexcept
    {
        auto& captured = *static_cast<CapturedMessages*>(userData);
        ++captured.count;
        captured.lastLevel = message.level;
        captured.lastCategory = message.category;

        if (message.message != nullptr)
        {
            strncpy_s(captured.lastMessage, sizeof(captured.lastMessage), message.message, _TRUNCATE);
        }
    }

    void CountMessage(const vanguard::diagnostics::MessageView&, void* const userData) noexcept
    {
        auto& count = *static_cast<std::atomic<vanguard::u32>*>(userData);
        count.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace

int main()
{
    namespace diagnostics = vanguard::diagnostics;

#if defined(VG_BUILD_DEBUG)
    constexpr const char* logPath = "diagnostics_smoke_debug.log";
#elif defined(VG_BUILD_DEVELOPMENT)
    constexpr const char* logPath = "diagnostics_smoke_development.log";
#elif defined(VG_BUILD_PROFILE)
    constexpr const char* logPath = "diagnostics_smoke_profile.log";
#else
    constexpr const char* logPath = "diagnostics_smoke_shipping.log";
#endif

    CapturedMessages captured;
    diagnostics::SetSinkCallback(&Capture, &captured);

    vanguard::u32 suppressedArgumentEvaluations = 0;
    VG_LOG_INFO(diagnostics::Category::Core, "uninitialized argument: %u", ++suppressedArgumentEvaluations);
    if (suppressedArgumentEvaluations != 0)
    {
        return 12;
    }

    if (!diagnostics::Initialize(diagnostics::Mode::Synchronous, "diagnosticsSmoke"))
    {
        return 1;
    }

    if (!diagnostics::IsInitialized() || !diagnostics::IsEnabled())
    {
        return 2;
    }

    diagnostics::SetLevel(diagnostics::Level::Trace);

    VG_LOG_INFO(diagnostics::Category::Core, "diagnostics initialized");
    VG_LOG_WARNING(diagnostics::Category::Jobs, "worker count: %u", 7u);

    diagnostics::Flush(diagnostics::FlushMode::Synchronous);

    if (captured.count != 2 || captured.lastLevel != diagnostics::Level::Warning || captured.lastCategory != diagnostics::Category::Jobs ||
        std::strcmp(captured.lastMessage, "worker count: 7") != 0)
    {
        return 3;
    }

    diagnostics::Disable();
    VG_LOG_ERROR(diagnostics::Category::Core, "this message must be filtered: %u", ++suppressedArgumentEvaluations);

    if (captured.count != 2 || suppressedArgumentEvaluations != 0)
    {
        return 4;
    }

    diagnostics::Enable();
    diagnostics::EnableCategory(diagnostics::Category::Jobs, false);
    VG_LOG_INFO(diagnostics::Category::Jobs, "this category must be filtered");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);

    if (captured.count != 2)
    {
        return 5;
    }

    diagnostics::EnableCategory(diagnostics::Category::Jobs, true);

    if (!diagnostics::OpenFileSink(logPath))
    {
        return 6;
    }

    VG_LOG_INFO(diagnostics::Category::Core, "persisted diagnostics message");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);
    diagnostics::CloseFileSink();

    std::FILE* logFile = nullptr;
    if (fopen_s(&logFile, logPath, "rb") != 0 || logFile == nullptr)
    {
        return 7;
    }

    char fileContents[4096] = {};
    const std::size_t bytesRead = std::fread(fileContents, 1, sizeof(fileContents) - 1, logFile);
    std::fclose(logFile);
    std::remove(logPath);

    if (bytesRead == 0 || std::strstr(fileContents, "persisted diagnostics message") == nullptr)
    {
        return 8;
    }

    diagnostics::Shutdown();

    if (diagnostics::IsInitialized())
    {
        return 9;
    }

    constexpr vanguard::u32 asynchronousMessageCount = 4096;
    std::atomic<vanguard::u32> consumedMessageCount = 0;
    diagnostics::SetSinkCallback(&CountMessage, &consumedMessageCount);

    if (!diagnostics::Initialize(diagnostics::Mode::Asynchronous))
    {
        return 10;
    }

    for (vanguard::u32 index = 0; index < asynchronousMessageCount; ++index)
    {
        VG_LOG_INFO(diagnostics::Category::Core, "asynchronous message %u", index);
    }

    diagnostics::Flush(diagnostics::FlushMode::Synchronous);

    if (consumedMessageCount.load(std::memory_order_relaxed) != asynchronousMessageCount)
    {
        return 11;
    }

    diagnostics::Shutdown();
    diagnostics::SetSinkCallback(nullptr);
    return 0;
}
