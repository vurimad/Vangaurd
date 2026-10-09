#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/diagnostics/sink_registry.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <Windows.h>

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

    struct ReentrantFixture final
    {
        vanguard::diagnostics::SinkRegistrationToken token;
        vanguard::diagnostics::SinkRegistrationToken registerToken{7u, 42u};
        std::atomic<vanguard::u32> registeredCount{0};
        vanguard::diagnostics::SinkRegistrationResult unregisterResult = vanguard::diagnostics::SinkRegistrationResult::Success;
        vanguard::diagnostics::SinkRegistrationResult registerResult = vanguard::diagnostics::SinkRegistrationResult::Success;
    };

    void ReentrantUnregister(const vanguard::diagnostics::MessageView&, void* const userData) noexcept
    {
        auto& fixture = *static_cast<ReentrantFixture*>(userData);
        fixture.unregisterResult = vanguard::diagnostics::UnregisterSinkCallback(fixture.token);
        fixture.registerResult =
            vanguard::diagnostics::RegisterSinkCallback(&CountMessage, &fixture.registeredCount, fixture.registerToken);
    }

    struct BlockingFixture final
    {
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
        std::atomic<vanguard::u32> calls{0};
    };

    void BlockingCallback(const vanguard::diagnostics::MessageView&, void* const userData) noexcept
    {
        auto& fixture = *static_cast<BlockingFixture*>(userData);
        fixture.calls.fetch_add(1u, std::memory_order_relaxed);
        fixture.entered.store(true, std::memory_order_release);
        while (!fixture.release.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    bool WaitUntil(const std::atomic<bool>& value) noexcept
    {
        for (vanguard::u32 attempt = 0; attempt < 10000; ++attempt)
        {
            if (value.load(std::memory_order_acquire))
                return true;
            std::this_thread::yield();
        }
        return false;
    }

    bool TestPrivateSinkRegistryCore() noexcept
    {
        namespace diagnostics = vanguard::diagnostics;
        namespace detail = vanguard::diagnostics::detail;

        std::atomic<vanguard::u64> generation{~vanguard::u64{0} - 1u};
        detail::SinkRegistryCore exhaustion({&generation});
        diagnostics::SinkRegistrationToken finalToken;
        std::atomic<vanguard::u32> count{0};
        if (exhaustion.Register(&CountMessage, &count, finalToken) != diagnostics::SinkRegistrationResult::Success ||
            finalToken.generation != ~vanguard::u64{0} ||
            exhaustion.Unregister(finalToken) != diagnostics::SinkRegistrationResult::Success)
            return false;
        diagnostics::SinkRegistrationToken untouched{7u, 42u};
        if (exhaustion.Register(&CountMessage, &count, untouched) != diagnostics::SinkRegistrationResult::GenerationExhausted ||
            untouched.index != 7u || untouched.generation != 42u)
            return false;

        std::atomic<vanguard::u64> completedGeneration{17u};
        detail::PublishSinkCompletedGeneration(completedGeneration, 23u);
        detail::PublishSinkCompletedGeneration(completedGeneration, 17u);
        if (completedGeneration.load(std::memory_order_acquire) != 23u)
            return false;

        std::atomic<vanguard::u64> concurrencyGeneration{0};
        detail::SinkRegistryCore registry({&concurrencyGeneration});
        BlockingFixture blocking;
        diagnostics::SinkRegistrationToken blockedToken;
        if (registry.Register(&BlockingCallback, &blocking, blockedToken) != diagnostics::SinkRegistrationResult::Success)
            return false;
        diagnostics::SinkRegistrationToken fillers[diagnostics::MaximumSinkRegistrations - 1u]{};
        for (auto& filler : fillers)
            if (registry.Register(&CountMessage, &count, filler) != diagnostics::SinkRegistrationResult::Success)
                return false;

        diagnostics::MessageView view;
        std::thread dispatch([&registry, &view]() noexcept { registry.Dispatch(view); });
        if (!WaitUntil(blocking.entered))
        {
            blocking.release.store(true, std::memory_order_release);
            dispatch.join();
            return false;
        }

        std::atomic<bool> firstFinished{false};
        std::atomic<bool> secondFinished{false};
        diagnostics::SinkRegistrationResult firstResult = diagnostics::SinkRegistrationResult::InternalFailure;
        diagnostics::SinkRegistrationResult secondResult = diagnostics::SinkRegistrationResult::InternalFailure;
        std::thread firstUnregister([&]() noexcept {
            firstResult = registry.Unregister(blockedToken);
            firstFinished.store(true, std::memory_order_release);
        });
        while (registry.IsRegistered(blockedToken))
            std::this_thread::yield();

        std::thread secondUnregister([&]() noexcept {
            secondResult = registry.Unregister(blockedToken);
            secondFinished.store(true, std::memory_order_release);
        });

        bool secondJoinedRetirement = false;
        for (vanguard::u32 attempt = 0; attempt < 10000u; ++attempt)
        {
            if (registry.HasRetirementJoiner(blockedToken))
            {
                secondJoinedRetirement = true;
                break;
            }
            std::this_thread::yield();
        }
        diagnostics::SinkRegistrationToken whileRetiring;
        const diagnostics::SinkRegistrationResult capacityResult = registry.Register(&CountMessage, &count, whileRetiring);
        const bool fencedBeforeRelease = !firstFinished.load(std::memory_order_acquire) && !secondFinished.load(std::memory_order_acquire);
        blocking.release.store(true, std::memory_order_release);
        dispatch.join();
        firstUnregister.join();
        secondUnregister.join();
        if (!secondJoinedRetirement || capacityResult != diagnostics::SinkRegistrationResult::CapacityExceeded || !fencedBeforeRelease ||
            firstResult != diagnostics::SinkRegistrationResult::Success || secondResult != diagnostics::SinkRegistrationResult::Success)
            return false;

        if (registry.Register(&CountMessage, &count, whileRetiring) != diagnostics::SinkRegistrationResult::Success)
            return false;
        if (whileRetiring.generation == blockedToken.generation)
            return false;
        if (registry.Unregister(whileRetiring) != diagnostics::SinkRegistrationResult::Success)
            return false;
        for (const auto filler : fillers)
            if (registry.Unregister(filler) != diagnostics::SinkRegistrationResult::Success)
                return false;
        return true;
    }
} // namespace

int main()
{
    namespace diagnostics = vanguard::diagnostics;

    // This low-level test deliberately has no filesystem-module dependency.
    char tempPath[MAX_PATH]{};
    const DWORD tempLength = GetTempPathA(MAX_PATH, tempPath);
    if (tempLength == 0 || tempLength >= MAX_PATH)
        return 1;
    char directory[MAX_PATH]{};
    const int directoryLength = std::snprintf(directory, sizeof(directory), "%sVanguardTests", tempPath);
    if (directoryLength < 0 || directoryLength >= static_cast<int>(sizeof(directory)))
        return 1;
    const BOOL created = CreateDirectoryA(directory, nullptr);
    if (!created && GetLastError() != ERROR_ALREADY_EXISTS)
        return 1;
    char logPath[MAX_PATH]{};
    const int logLength = std::snprintf(logPath, sizeof(logPath), "%s\\diagnostics_smoke_%lu.log", directory, GetCurrentProcessId());
    if (logLength < 0 || logLength >= static_cast<int>(sizeof(logPath)))
        return 1;

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

    std::atomic<vanguard::u32> registeredOne{0};
    std::atomic<vanguard::u32> registeredTwo{0};
    diagnostics::SinkRegistrationToken registeredOneToken;
    diagnostics::SinkRegistrationToken registeredTwoToken;
    if (diagnostics::RegisterSinkCallback(&CountMessage, &registeredOne, registeredOneToken) != diagnostics::SinkRegistrationResult::Success ||
        diagnostics::RegisterSinkCallback(&CountMessage, &registeredTwo, registeredTwoToken) != diagnostics::SinkRegistrationResult::Success ||
        !diagnostics::IsSinkCallbackRegistered(registeredOneToken) || !diagnostics::IsSinkCallbackRegistered(registeredTwoToken))
        return 13;

    VG_LOG_INFO(diagnostics::Category::Core, "diagnostics initialized");
    VG_LOG_WARNING(diagnostics::Category::Jobs, "worker count: %u", 7u);

    diagnostics::Flush(diagnostics::FlushMode::Synchronous);

    if (captured.count != 2 || captured.lastLevel != diagnostics::Level::Warning || captured.lastCategory != diagnostics::Category::Jobs ||
        std::strcmp(captured.lastMessage, "worker count: 7") != 0)
    {
        return 3;
    }
    if (registeredOne.load(std::memory_order_relaxed) != 2u || registeredTwo.load(std::memory_order_relaxed) != 2u)
        return 14;

    if (diagnostics::UnregisterSinkCallback(registeredOneToken) != diagnostics::SinkRegistrationResult::Success ||
        diagnostics::IsSinkCallbackRegistered(registeredOneToken) ||
        diagnostics::UnregisterSinkCallback(registeredOneToken) != diagnostics::SinkRegistrationResult::StaleToken)
        return 15;

    VG_LOG_INFO(diagnostics::Category::Core, "registered sink removal fence");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);
    if (registeredOne.load(std::memory_order_relaxed) != 2u || registeredTwo.load(std::memory_order_relaxed) != 3u)
        return 16;

    ReentrantFixture reentrant;
    if (diagnostics::RegisterSinkCallback(&ReentrantUnregister, &reentrant, reentrant.token) != diagnostics::SinkRegistrationResult::Success)
        return 17;
    VG_LOG_INFO(diagnostics::Category::Core, "registered sink reentrant unregister");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);
    if (reentrant.unregisterResult != diagnostics::SinkRegistrationResult::ReentrantOperation ||
        reentrant.registerResult != diagnostics::SinkRegistrationResult::ReentrantOperation ||
        reentrant.registerToken.index != 7u || reentrant.registerToken.generation != 42u ||
        diagnostics::UnregisterSinkCallback(reentrant.token) != diagnostics::SinkRegistrationResult::Success)
        return 18;

    if (diagnostics::UnregisterSinkCallback(registeredTwoToken) != diagnostics::SinkRegistrationResult::Success)
        return 19;

    diagnostics::SinkRegistrationToken capacityTokens[diagnostics::MaximumSinkRegistrations]{};
    for (auto& token : capacityTokens)
        if (diagnostics::RegisterSinkCallback(&CountMessage, &registeredOne, token) != diagnostics::SinkRegistrationResult::Success)
            return 20;
    diagnostics::SinkRegistrationToken capacityOverflow{3u, 99u};
    if (diagnostics::RegisterSinkCallback(&CountMessage, &registeredOne, capacityOverflow) != diagnostics::SinkRegistrationResult::CapacityExceeded ||
        capacityOverflow.index != 3u || capacityOverflow.generation != 99u)
        return 21;
    for (const auto token : capacityTokens)
        if (diagnostics::UnregisterSinkCallback(token) != diagnostics::SinkRegistrationResult::Success)
            return 22;

    if (!TestPrivateSinkRegistryCore())
        return 23;

    const vanguard::u32 legacyCountBeforeFilter = captured.count;
    diagnostics::Disable();
    VG_LOG_ERROR(diagnostics::Category::Core, "this message must be filtered: %u", ++suppressedArgumentEvaluations);

    if (captured.count != legacyCountBeforeFilter || suppressedArgumentEvaluations != 0)
    {
        return 4;
    }

    diagnostics::Enable();
    diagnostics::EnableCategory(diagnostics::Category::Jobs, false);
    VG_LOG_INFO(diagnostics::Category::Jobs, "this category must be filtered");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);

    if (captured.count != legacyCountBeforeFilter)
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

    diagnostics::SinkRegistrationToken shutdownToken;
    if (diagnostics::RegisterSinkCallback(&CountMessage, &registeredOne, shutdownToken) != diagnostics::SinkRegistrationResult::Success)
        return 24;
    diagnostics::Shutdown();
    if (diagnostics::IsSinkCallbackRegistered(shutdownToken))
        return 25;

    diagnostics::SinkRegistrationToken preInitializeToken;
    if (diagnostics::RegisterSinkCallback(&CountMessage, &registeredOne, preInitializeToken) != diagnostics::SinkRegistrationResult::Success)
        return 26;
    diagnostics::Shutdown();
    if (diagnostics::IsSinkCallbackRegistered(preInitializeToken))
        return 27;

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

    BlockingFixture asynchronousBlocking;
    diagnostics::SinkRegistrationToken asynchronousToken;
    if (diagnostics::RegisterSinkCallback(&BlockingCallback, &asynchronousBlocking, asynchronousToken) !=
        diagnostics::SinkRegistrationResult::Success)
        return 28;
    VG_LOG_INFO(diagnostics::Category::Core, "asynchronous registered sink lifetime fence");
    if (!WaitUntil(asynchronousBlocking.entered))
    {
        asynchronousBlocking.release.store(true, std::memory_order_release);
        diagnostics::Flush(diagnostics::FlushMode::Synchronous);
        return 29;
    }

    std::atomic<bool> asynchronousUnregisterFinished{false};
    diagnostics::SinkRegistrationResult asynchronousUnregisterResult = diagnostics::SinkRegistrationResult::InternalFailure;
    std::thread asynchronousUnregister([&]() noexcept {
        asynchronousUnregisterResult = diagnostics::UnregisterSinkCallback(asynchronousToken);
        asynchronousUnregisterFinished.store(true, std::memory_order_release);
    });
    bool asynchronousRetirementAdmitted = false;
    for (vanguard::u32 attempt = 0; attempt < 10000u; ++attempt)
    {
        if (!diagnostics::IsSinkCallbackRegistered(asynchronousToken))
        {
            asynchronousRetirementAdmitted = true;
            break;
        }
        std::this_thread::yield();
    }
    const bool asynchronousFenceHeld = asynchronousRetirementAdmitted &&
                                       !asynchronousUnregisterFinished.load(std::memory_order_acquire);
    asynchronousBlocking.release.store(true, std::memory_order_release);
    asynchronousUnregister.join();
    if (!asynchronousFenceHeld || asynchronousUnregisterResult != diagnostics::SinkRegistrationResult::Success)
        return 30;

    VG_LOG_INFO(diagnostics::Category::Core, "message after asynchronous registered sink detach");
    diagnostics::Flush(diagnostics::FlushMode::Synchronous);
    if (asynchronousBlocking.calls.load(std::memory_order_relaxed) != 1u)
        return 31;
    consumedMessageCount.store(0, std::memory_order_relaxed);

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
