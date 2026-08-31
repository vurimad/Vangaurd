#pragma once

#include <vanguard/application/service.hpp>

namespace vanguard::application
{
    class EngineHost;

    class ServiceContext final
    {
    public:
        [[nodiscard]] ApplicationProfile GetProfile() const noexcept;
        [[nodiscard]] ServiceId GetCurrentService() const noexcept;
        [[nodiscard]] Service* Find(ServiceId service) const noexcept;
        [[nodiscard]] Service* FindCapability(CapabilityId capability) const noexcept;
        [[nodiscard]] ServiceHandle Acquire(ServiceId service) const noexcept;
        [[nodiscard]] Service* Resolve(ServiceHandle handle) const noexcept;

    private:
        ServiceContext(EngineHost& host, ServiceId currentService) noexcept;

        EngineHost* m_host = nullptr;
        ServiceId m_currentService = InvalidServiceId;

        friend class EngineHost;
    };

    enum class HostState : u8
    {
        Building,
        Compiled,
        Starting,
        Running,
        RollingBack,
        Stopping,
        Stopped,
        Failed
    };

    enum class HostFailureCode : u8
    {
        None,
        InvalidArgument,
        InvalidState,
        LimitExceeded,
        DuplicateModule,
        DuplicateService,
        UnknownModule,
        MissingDependency,
        DependencyCycle,
        ConflictingService,
        MissingCapability,
        AmbiguousCapability,
        FactoryFailure,
        InitializeFailure,
        StartFailure,
        QuiesceFailure,
        DrainFailure,
        StopFailure,
        ShutdownFailure
    };

    struct HostFailure
    {
        HostFailureCode code = HostFailureCode::None;
        ServiceId service = InvalidServiceId;
        ServiceId relatedService = InvalidServiceId;
        CapabilityId capability = InvalidCapabilityId;
        const char* message = nullptr;
    };

    enum class LifecycleStage : u8
    {
        Compile,
        Construct,
        Initialize,
        Start,
        Quiesce,
        Drain,
        Stop,
        Shutdown,
        Destroy,
        Rollback
    };

    struct LifecycleEvent
    {
        u64 sequence = 0;
        ModuleId module = InvalidModuleId;
        ServiceId service = InvalidServiceId;
        LifecycleStage stage = LifecycleStage::Compile;
        ServiceState state = ServiceState::Registered;
        HostFailureCode result = HostFailureCode::None;
        const char* message = nullptr;
    };

    using LifecycleSink = void (*)(const LifecycleEvent& event, void* userData) noexcept;
    using LifecycleVisitor = void (*)(const LifecycleEvent& event, void* userData) noexcept;

    struct HostStats
    {
        u32 registeredModules = 0;
        u32 registeredServices = 0;
        u32 selectedServices = 0;
        u32 constructedServices = 0;
        u32 runningServices = 0;
        u32 failedServices = 0;
        u64 lifecycleEvents = 0;
        HostState state = HostState::Building;
        ApplicationProfile profile = ApplicationProfile::None;
    };

    struct CompiledServiceInfo
    {
        ModuleId module = InvalidModuleId;
        ServiceId service = InvalidServiceId;
        const char* name = nullptr;
        u32 startupIndex = 0;
        ServiceScope scope = ServiceScope::Engine;
        ThreadAffinity affinity = ThreadAffinity::CompositionThread;
    };

    using CompiledServiceVisitor = void (*)(const CompiledServiceInfo& service, void* userData) noexcept;

    class EngineHost final
    {
    public:
        struct Impl;

        EngineHost() noexcept = default;
        ~EngineHost();

        EngineHost(const EngineHost&) = delete;
        EngineHost& operator=(const EngineHost&) = delete;

        [[nodiscard]] bool RegisterModule(const ModuleDescriptor& descriptor, HostFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool RegisterService(ModuleId owner, const ServiceDescriptor& descriptor, HostFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Compile(ApplicationProfile profile, HostFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Start(HostFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(HostFailure* failure = nullptr) noexcept;

        [[nodiscard]] HostState State() const noexcept;
        [[nodiscard]] ApplicationProfile GetProfile() const noexcept;
        [[nodiscard]] Service* Find(ServiceId service) const noexcept;
        [[nodiscard]] Service* FindCapability(CapabilityId capability) const noexcept;
        [[nodiscard]] ServiceHandle Acquire(ServiceId service) const noexcept;
        [[nodiscard]] Service* Resolve(ServiceHandle handle) const noexcept;
        [[nodiscard]] ServiceState GetStateOf(ServiceId service) const noexcept;

        void SetLifecycleSink(LifecycleSink sink, void* userData = nullptr) noexcept;
        void VisitLifecycleEvents(LifecycleVisitor visitor, void* userData = nullptr) const noexcept;
        void VisitCompiledServices(CompiledServiceVisitor visitor, void* userData = nullptr) const noexcept;
        [[nodiscard]] HostStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;

        friend class ServiceContext;
    };
} // namespace vanguard::application
