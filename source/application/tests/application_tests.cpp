#include <vanguard/application/application.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <sys/wait.h>
#endif

namespace
{
    namespace app = vanguard::application;
    using vanguard::u32;
    using vanguard::u64;

    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[applicationTests] FAILED: %s\n", message);
        ++g_failures;
    }

    enum class RecordedStage : u32
    {
        Initialize = 1,
        Start,
        Quiesce,
        Drain,
        Stop,
        Shutdown,
        Destroy
    };

    struct Fixture
    {
        explicit Fixture(vanguard::memory::Pool& pool) noexcept : calls(pool) {}

        vanguard::containers::DynamicArray<u64> calls;
        app::ServiceId dependencyToObserve = app::InvalidServiceId;
        app::CapabilityId capabilityToObserve = app::InvalidCapabilityId;
        bool failInitialize = false;
        bool failStart = false;
        bool failStop = false;
        bool observedDependency = false;
        bool observedCapability = false;
        u32 liveInstances = 0;
    };

    void Record(Fixture& fixture, const RecordedStage stage, const app::ServiceId id) noexcept
    {
        fixture.calls.PushBack(static_cast<u64>(stage) * 1000u + id);
    }

    class RecordingService final : public app::Service
    {
    public:
        RecordingService(const app::ServiceId id, Fixture& fixture) noexcept : m_id(id), m_fixture(fixture)
        {
            ++m_fixture.liveInstances;
        }

        ~RecordingService() override
        {
            --m_fixture.liveInstances;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            Record(m_fixture, RecordedStage::Initialize, m_id);
            if (m_fixture.dependencyToObserve != app::InvalidServiceId)
                m_fixture.observedDependency = context.Find(m_fixture.dependencyToObserve) != nullptr;
            if (m_fixture.capabilityToObserve != app::InvalidCapabilityId)
                m_fixture.observedCapability = context.FindCapability(m_fixture.capabilityToObserve) != nullptr;
            return m_fixture.failInitialize ? app::LifecycleStatus::Failure("requested initialize failure") : app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            Record(m_fixture, RecordedStage::Start, m_id);
            return m_fixture.failStart ? app::LifecycleStatus::Failure("requested start failure") : app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            Record(m_fixture, RecordedStage::Quiesce, m_id);
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            Record(m_fixture, RecordedStage::Drain, m_id);
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStop(app::ServiceContext&) noexcept override
        {
            Record(m_fixture, RecordedStage::Stop, m_id);
            return m_fixture.failStop ? app::LifecycleStatus::Failure("requested stop failure") : app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            Record(m_fixture, RecordedStage::Shutdown, m_id);
            return app::LifecycleStatus::Success();
        }

    private:
        app::ServiceId m_id;
        Fixture& m_fixture;
    };

    struct FactoryData
    {
        app::ServiceId id = app::InvalidServiceId;
        Fixture* fixture = nullptr;
    };

    app::Service* CreateRecordingService(void* const userData) noexcept
    {
        FactoryData& data = *static_cast<FactoryData*>(userData);
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Runtime, sizeof(RecordingService), alignof(RecordingService));
        return block ? ::new (block.address) RecordingService(data.id, *data.fixture) : nullptr;
    }

    void DestroyRecordingService(app::Service* const service, void* const userData) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<RecordingService*>(service)->~RecordingService();
        vanguard::memory::MemoryBlock block{service, sizeof(RecordingService), vanguard::memory::PoolId::Runtime};
        vanguard::memory::Free(block);
        FactoryData& data = *static_cast<FactoryData*>(userData);
        Record(*data.fixture, RecordedStage::Destroy, data.id);
    }

    app::ServiceDescriptor Descriptor(const app::ServiceId id, const char* const name, FactoryData& factory) noexcept
    {
        app::ServiceDescriptor descriptor;
        descriptor.id = id;
        descriptor.name = name;
        descriptor.create = CreateRecordingService;
        descriptor.destroy = DestroyRecordingService;
        descriptor.userData = &factory;
        return descriptor;
    }

    bool Contains(const vanguard::containers::DynamicArray<u64>& calls, const u64 value) noexcept
    {
        for (const u64 call : calls)
            if (call == value)
                return true;
        return false;
    }

    class HostLifecycleWorker final : public vanguard::concurrency::Thread
    {
    public:
        enum class Operation : vanguard::u8
        {
            Start,
            Shutdown
        };

        HostLifecycleWorker(app::EngineHost& host, const Operation operation) noexcept
            : Thread("hostLifecycleWorker"), m_host(host), m_operation(operation)
        {
        }

        void ThreadFunction() noexcept override
        {
            m_result = m_operation == Operation::Start ? m_host.Start(&m_failure) : m_host.Shutdown(&m_failure);
        }

        [[nodiscard]] bool Result() const noexcept { return m_result; }
        [[nodiscard]] const app::HostFailure& Failure() const noexcept { return m_failure; }

    private:
        app::EngineHost& m_host;
        Operation m_operation;
        app::HostFailure m_failure;
        bool m_result = false;
    };

    class HostDestructorWorker final : public vanguard::concurrency::Thread
    {
    public:
        explicit HostDestructorWorker(app::EngineHost& host) noexcept : Thread("hostDestructorWorker"), m_host(host) {}

        void ThreadFunction() noexcept override
        {
            m_host.~EngineHost();
        }

    private:
        app::EngineHost& m_host;
    };

    [[nodiscard]] int RunWrongThreadDestructorFatalCase() noexcept
    {
        vanguard::memory::Pool& pool = vanguard::memory::pools::Runtime::GetInstance();
        Fixture fixture(pool);
        FactoryData factory{10, &fixture};
        app::ServiceDescriptor descriptor = Descriptor(10, "main-thread-destructor-service", factory);
        descriptor.affinity = app::ThreadAffinity::MainThread;

        app::EngineHost host;
        app::HostFailure failure;
        if (!host.RegisterModule({1, "main-thread-destructor-test", 1}, &failure) || !host.RegisterService(1, descriptor, &failure) ||
            !host.Compile(app::ApplicationProfile::Test, &failure) || !host.Start(&failure))
            return 2;

        HostDestructorWorker worker(host);
        worker.InitThread();
        worker.JoinThread();
        return 3;
    }

    [[nodiscard]] bool RunWrongThreadDestructorFatalSubprocess(const char* const executable) noexcept
    {
        if (executable == nullptr || executable[0] == '\0')
            return false;
#if defined(_WIN32)
        char command[2048]{};
        const int written = std::snprintf(command, sizeof(command), "\"%s\" --wrong-thread-host-destructor", executable);
        if (written <= 0 || static_cast<vanguard::usize>(written) >= sizeof(command))
            return false;

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessA(executable, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            return false;
        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 0;
        const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return exited && exitCode == 0xc0000602u;
#else
        char command[2048]{};
        const int written = std::snprintf(command, sizeof(command), "\"%s\" --wrong-thread-host-destructor", executable);
        if (written <= 0 || static_cast<vanguard::usize>(written) >= sizeof(command))
            return false;
        const int result = std::system(command);
        return result != -1 && WIFSIGNALED(result);
#endif
    }

    void TestMainThreadLifecycleAffinity(vanguard::memory::Pool& pool) noexcept
    {
        constexpr app::ModuleId module = 1;
        constexpr app::ServiceId serviceId = 10;
        Fixture fixture(pool);
        FactoryData factory{serviceId, &fixture};
        app::ServiceDescriptor descriptor = Descriptor(serviceId, "main-thread-service", factory);
        descriptor.affinity = app::ThreadAffinity::MainThread;

        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({module, "main-thread-test", 1}, &failure), "main-thread affinity module registers");
        Check(host.RegisterService(module, descriptor, &failure), "main-thread affinity service registers");
        Check(host.Compile(app::ApplicationProfile::Test, &failure), "main-thread affinity graph compiles");

        HostLifecycleWorker wrongStart(host, HostLifecycleWorker::Operation::Start);
        wrongStart.InitThread();
        wrongStart.JoinThread();
        Check(!wrongStart.Result() && wrongStart.Failure().code == app::HostFailureCode::WrongThread,
              "worker-thread Start rejects a selected main-thread service before construction");
        Check(host.State() == app::HostState::Compiled && fixture.liveInstances == 0,
              "wrong-thread Start leaves the compiled host retryable and unconstructed");

        Check(host.Start(&failure), "main-thread retry starts the host");
        Check(host.State() == app::HostState::Running && fixture.liveInstances == 1,
              "main-thread service is running before shutdown-affinity proof");

        HostLifecycleWorker wrongShutdown(host, HostLifecycleWorker::Operation::Shutdown);
        wrongShutdown.InitThread();
        wrongShutdown.JoinThread();
        Check(!wrongShutdown.Result() && wrongShutdown.Failure().code == app::HostFailureCode::WrongThread,
              "worker-thread Shutdown rejects before destructive unwind");
        Check(host.State() == app::HostState::Running && fixture.liveInstances == 1 && !Contains(fixture.calls, 3010),
              "wrong-thread Shutdown leaves the running host retryable without quiesce callbacks");

        Check(host.Shutdown(&failure), "main-thread shutdown retry succeeds");
        Check(fixture.liveInstances == 0 && Contains(fixture.calls, 3010) && Contains(fixture.calls, 7010),
              "main-thread shutdown performs lifecycle cleanup and destroys the service");
    }

    void TestDeterministicGraphAndShutdown(vanguard::memory::Pool& pool) noexcept
    {
        constexpr app::ModuleId coreModule = 1;
        constexpr app::ModuleId editorModule = 2;
        constexpr app::CapabilityId storageCapability = 70;
        Fixture memory(pool), jobs(pool), storage(pool), resources(pool), editor(pool);
        resources.dependencyToObserve = 20;
        resources.capabilityToObserve = storageCapability;
        FactoryData memoryFactory{10, &memory}, jobsFactory{20, &jobs}, storageFactory{40, &storage};
        FactoryData resourcesFactory{30, &resources}, editorFactory{5, &editor};
        app::ServiceDependency jobsDependencies[]{{10, app::DependencyKind::Required}};
        app::ServiceDependency resourceDependencies[]{{20, app::DependencyKind::Required}};
        app::CapabilityId storageProvides[]{storageCapability};
        app::CapabilityRequirement resourceCapabilities[]{{storageCapability, app::CapabilityCardinality::ExactlyOne, false}};

        app::EngineHost host;
        app::HostFailure failure;
        Check(host.RegisterModule({coreModule, "core", 1}, &failure), "core module registers");
        Check(host.RegisterModule({editorModule, "editor", 1}, &failure), "editor module registers");

        app::ServiceDescriptor resourcesDescriptor = Descriptor(30, "resources", resourcesFactory);
        resourcesDescriptor.dependencies = {resourceDependencies, 1};
        resourcesDescriptor.requiresCapabilities = {resourceCapabilities, 1};
        Check(host.RegisterService(coreModule, resourcesDescriptor, &failure), "resource service registers");
        app::ServiceDescriptor editorDescriptor = Descriptor(5, "editor-only", editorFactory);
        editorDescriptor.profiles = app::ApplicationProfile::Editor;
        Check(host.RegisterService(editorModule, editorDescriptor, &failure), "editor-only service registers");
        app::ServiceDescriptor storageDescriptor = Descriptor(40, "storage", storageFactory);
        storageDescriptor.provides = {storageProvides, 1};
        Check(host.RegisterService(coreModule, storageDescriptor, &failure), "storage service registers");
        app::ServiceDescriptor jobsDescriptor = Descriptor(20, "jobs", jobsFactory);
        jobsDescriptor.dependencies = {jobsDependencies, 1};
        Check(host.RegisterService(coreModule, jobsDescriptor, &failure), "jobs service registers");
        Check(host.RegisterService(coreModule, Descriptor(10, "memory", memoryFactory), &failure), "memory service registers");

        Check(host.Compile(app::ApplicationProfile::Runtime, &failure), "runtime graph compiles");
        Check(host.GetStats().selectedServices == 4, "profile excludes editor service");
        Check(host.Start(&failure), "compiled graph starts");
        Check(resources.observedDependency, "required dependency exists during dependent initialization");
        Check(resources.observedCapability, "capability provider exists during dependent initialization");
        Check(!Contains(editor.calls, 1005), "excluded service is never initialized");
        Check(memory.calls[0] == 1010 && jobs.calls[0] == 1020 && storage.calls[0] == 1040 && resources.calls[0] == 1030,
              "services initialize in deterministic dependency order");

        const app::ServiceHandle resourcesHandle = host.Acquire(30);
        Check(resourcesHandle && host.Resolve(resourcesHandle) != nullptr, "live service handle resolves");
        Check(host.Shutdown(&failure), "graph shuts down cleanly");
        Check(host.Resolve(resourcesHandle) == nullptr, "destroyed service handle becomes stale");
        Check(memory.liveInstances + jobs.liveInstances + storage.liveInstances + resources.liveInstances == 0, "host destroys every owned service instance");
        Check(resources.calls[2] == 3030 && resources.calls[3] == 4030 && resources.calls[4] == 5030 && resources.calls[5] == 6030 &&
                  resources.calls[6] == 7030,
              "shutdown executes quiesce, drain, stop, shutdown, and destroy");
    }

    void TestValidationAndRollback(vanguard::memory::Pool& pool) noexcept
    {
        constexpr app::ModuleId module = 1;
        app::HostFailure failure;
        {
            Fixture first(pool), second(pool);
            FactoryData firstFactory{1, &first}, secondFactory{2, &second};
            app::ServiceDependency firstDependencies[]{{2, app::DependencyKind::Required}};
            app::ServiceDependency secondDependencies[]{{1, app::DependencyKind::Required}};
            app::ServiceDescriptor firstDescriptor = Descriptor(1, "first", firstFactory);
            app::ServiceDescriptor secondDescriptor = Descriptor(2, "second", secondFactory);
            firstDescriptor.dependencies = {firstDependencies, 1};
            secondDescriptor.dependencies = {secondDependencies, 1};
            app::EngineHost host;
            Check(host.RegisterModule({module, "cycle", 1}, &failure), "cycle module registers");
            Check(host.RegisterService(module, firstDescriptor, &failure), "first cycle service registers");
            Check(host.RegisterService(module, secondDescriptor, &failure), "second cycle service registers");
            Check(!host.Compile(app::ApplicationProfile::Runtime, &failure) && failure.code == app::HostFailureCode::DependencyCycle,
                  "compile rejects dependency cycles before construction");
            Check(first.liveInstances + second.liveInstances == 0, "invalid graph constructs nothing");
        }
        {
            Fixture base(pool), failing(pool);
            failing.failStart = true;
            FactoryData baseFactory{10, &base}, failingFactory{20, &failing};
            app::ServiceDependency dependencies[]{{10, app::DependencyKind::Required}};
            app::ServiceDescriptor failingDescriptor = Descriptor(20, "failing", failingFactory);
            failingDescriptor.dependencies = {dependencies, 1};
            app::EngineHost host;
            Check(host.RegisterModule({module, "rollback", 1}, &failure), "rollback module registers");
            Check(host.RegisterService(module, failingDescriptor, &failure), "failing service registers");
            Check(host.RegisterService(module, Descriptor(10, "base", baseFactory), &failure), "base service registers");
            Check(host.Compile(app::ApplicationProfile::Runtime, &failure), "rollback graph compiles");
            Check(!host.Start(&failure) && failure.code == app::HostFailureCode::StartFailure, "start failure is reported");
            Check(base.liveInstances + failing.liveInstances == 0, "failed startup rolls every instance back");
            Check(Contains(failing.calls, 5020) && Contains(failing.calls, 6020) && Contains(failing.calls, 7020),
                  "failed start still receives stop, shutdown, and destruction");
            Check(host.State() == app::HostState::Failed, "failed startup leaves explicit failed state");
        }
    }

    void TestCleanupFailureWithoutOutput(vanguard::memory::Pool& pool) noexcept
    {
        Fixture fixture(pool);
        fixture.failStop = true;
        FactoryData factory{10, &fixture};
        app::EngineHost host;
        Check(host.RegisterModule({1, "cleanup", 1}), "cleanup module registers");
        Check(host.RegisterService(1, Descriptor(10, "cleanup", factory)), "cleanup service registers");
        Check(host.Compile(app::ApplicationProfile::Runtime), "cleanup graph compiles");
        Check(host.Start(), "cleanup graph starts");
        Check(!host.Shutdown(), "cleanup failure is returned even without a HostFailure output");
        Check(fixture.liveInstances == 0 && Contains(fixture.calls, 7010), "cleanup failure does not prevent later shutdown and destruction stages");
    }

    class FakePlatform final : public app::IPlatformHost
    {
    public:
        bool initialized = false;
        bool shutdown = false;
        u32 pumpCalls = 0;

        const char* GetName() const noexcept override
        {
            return "Test";
        }
        app::PlatformStatus Initialize(const app::PlatformStartupInfo&) noexcept override
        {
            initialized = true;
            return app::PlatformStatus::Success();
        }
        app::PlatformPumpResult PumpEvents() noexcept override
        {
            ++pumpCalls;
            return {};
        }
        void Shutdown() noexcept override
        {
            shutdown = true;
        }
    };

    class FirstApplicationState final : public app::ApplicationState
    {
    public:
        u32 enterCalls = 0;
        u32 tickCalls = 0;
        u32 exitCalls = 0;

    protected:
        app::StateOperationStatus OnEnter(app::StateContext&) noexcept override
        {
            return ++enterCalls == 1 ? app::StateOperationStatus::Pending() : app::StateOperationStatus::Complete();
        }
        app::StateTickStatus OnTick(app::StateContext& context) noexcept override
        {
            ++tickCalls;
            static_cast<void>(context.RequestTransition(2));
            return app::StateTickStatus::Success();
        }
        app::StateOperationStatus OnExit(app::StateContext&) noexcept override
        {
            return ++exitCalls == 1 ? app::StateOperationStatus::Pending() : app::StateOperationStatus::Complete();
        }
    };

    class FinalApplicationState final : public app::ApplicationState
    {
    public:
        u32 enterCalls = 0;
        u32 tickCalls = 0;
        u32 exitCalls = 0;

    protected:
        app::StateOperationStatus OnEnter(app::StateContext&) noexcept override
        {
            ++enterCalls;
            return app::StateOperationStatus::Complete();
        }
        app::StateTickStatus OnTick(app::StateContext& context) noexcept override
        {
            ++tickCalls;
            static_cast<void>(context.RequestExit(42));
            return app::StateTickStatus::Success();
        }
        app::StateOperationStatus OnExit(app::StateContext&) noexcept override
        {
            ++exitCalls;
            return app::StateOperationStatus::Complete();
        }
    };

    class RunnerComposition final : public app::IApplicationComposition
    {
    public:
        FirstApplicationState first;
        FinalApplicationState final;
        bool receivedStartup = false;

        app::CompositionStatus Compose(const app::ApplicationStartupContext& startup, app::EngineHost& services,
                                       app::ApplicationStateMachine& states) noexcept override
        {
            receivedStartup = startup.platform != nullptr && startup.profile == app::ApplicationProfile::Test;
            if (!services.RegisterModule({1, "runner-test", 1}))
                return app::CompositionStatus::Failure("runner test module failed");
            if (!states.RegisterState({1, "first", &first}) || !states.RegisterState({2, "final", &final}) || !states.SetInitialState(1))
                return app::CompositionStatus::Failure("runner test states failed");
            return app::CompositionStatus::Success();
        }
    };

    void TestPortableRunner() noexcept
    {
        FakePlatform platform;
        RunnerComposition composition;
        app::RunnerSettings settings;
        settings.applicationName = "portable-runner-test";
        settings.profile = app::ApplicationProfile::Test;
        app::ApplicationRunner runner;
        const app::RunnerResult result = runner.Run(platform, composition, settings);
        Check(result && result.exitCode == 42, "portable runner returns the application-requested exit code");
        Check(platform.initialized && platform.shutdown && platform.pumpCalls >= 7, "portable runner owns the complete injected platform-host lifetime");
        Check(composition.receivedStartup, "composition receives portable startup and platform context");
        Check(composition.first.enterCalls == 2 && composition.first.tickCalls == 1 && composition.first.exitCalls == 2,
              "pending enter and exit operations advance explicitly across runner ticks");
        Check(composition.final.enterCalls == 1 && composition.final.tickCalls == 1 && composition.final.exitCalls == 1,
              "deferred state transition enters, ticks, and exits the target state exactly once");
    }

    class NeverFinishesExitState final : public app::ApplicationState
    {
    protected:
        app::StateTickStatus OnTick(app::StateContext& context) noexcept override
        {
            static_cast<void>(context.RequestExit());
            return app::StateTickStatus::Success();
        }
        app::StateOperationStatus OnExit(app::StateContext&) noexcept override
        {
            return app::StateOperationStatus::Pending();
        }
    };

    class TimeoutComposition final : public app::IApplicationComposition
    {
    public:
        NeverFinishesExitState state;

        app::CompositionStatus Compose(const app::ApplicationStartupContext&, app::EngineHost& services, app::ApplicationStateMachine& states) noexcept override
        {
            if (!services.RegisterModule({1, "timeout-test", 1}) || !states.RegisterState({1, "never-finishes", &state}) || !states.SetInitialState(1))
                return app::CompositionStatus::Failure("timeout test composition failed");
            return app::CompositionStatus::Success();
        }
    };

    void TestBoundedGracefulShutdown() noexcept
    {
        FakePlatform platform;
        TimeoutComposition composition;
        app::RunnerSettings settings;
        settings.applicationName = "shutdown-timeout-test";
        settings.profile = app::ApplicationProfile::Test;
        settings.maximumShutdownTicks = 3;
        app::ApplicationRunner runner;
        const app::RunnerResult result = runner.Run(platform, composition, settings);
        Check(!result && result.failure == app::RunnerFailureCode::ShutdownTimeout, "runner bounds an application state that never completes graceful exit");
        Check(platform.shutdown, "shutdown timeout still releases the platform host");
    }

    class FailingTickState final : public app::ApplicationState
    {
    public:
        u32 exitCalls = 0;

    protected:
        app::StateTickStatus OnTick(app::StateContext&) noexcept override
        {
            return app::StateTickStatus::Failure("requested state tick failure");
        }
        app::StateOperationStatus OnExit(app::StateContext&) noexcept override
        {
            ++exitCalls;
            return app::StateOperationStatus::Complete();
        }
    };

    class FailingStateComposition final : public app::IApplicationComposition
    {
    public:
        FailingTickState state;

        app::CompositionStatus Compose(const app::ApplicationStartupContext&, app::EngineHost& services, app::ApplicationStateMachine& states) noexcept override
        {
            if (!services.RegisterModule({1, "state-failure-test", 1}) || !states.RegisterState({1, "fails", &state}) || !states.SetInitialState(1))
                return app::CompositionStatus::Failure("state failure test composition failed");
            return app::CompositionStatus::Success();
        }
    };

    void TestStateFailureUnwind() noexcept
    {
        FakePlatform platform;
        FailingStateComposition composition;
        app::RunnerSettings settings;
        settings.applicationName = "state-failure-unwind-test";
        settings.profile = app::ApplicationProfile::Test;
        app::ApplicationRunner runner;
        const app::RunnerResult result = runner.Run(platform, composition, settings);
        Check(!result && result.failure == app::RunnerFailureCode::StateMachineFailure, "state callback failure remains the runner's primary failure");
        Check(composition.state.exitCalls == 1, "state tick failure still executes state exit cleanup exactly once");
        Check(platform.shutdown, "state failure unwind still releases the platform host");
    }
} // namespace

int main(const int argumentCount, char** const arguments)
{
    vanguard::concurrency::InitializeMainThread();
    Check(vanguard::memory::Initialize(), "memory initializes");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "applicationTests"), "diagnostics initializes");
    Check(vanguard::containers::Initialize(), "containers initialize");

    if (argumentCount == 2 && std::strcmp(arguments[1], "--wrong-thread-host-destructor") == 0)
        return RunWrongThreadDestructorFatalCase();

    vanguard::memory::Pool& pool = vanguard::memory::pools::Runtime::GetInstance();
    Check(RunWrongThreadDestructorFatalSubprocess(arguments[0]),
          "destroying a running host with main-thread-affine services from a worker fails fast before implicit unwind");
    TestMainThreadLifecycleAffinity(pool);
    TestDeterministicGraphAndShutdown(pool);
    TestValidationAndRollback(pool);
    TestCleanupFailureWithoutOutput(pool);
    TestPortableRunner();
    TestBoundedGracefulShutdown();
    TestStateFailureUnwind();

    vanguard::diagnostics::Shutdown();
    if (g_failures == 0)
        std::printf("[applicationTests] all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
