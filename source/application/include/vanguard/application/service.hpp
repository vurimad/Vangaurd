#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/system/types.hpp>

namespace vanguard::application
{
    using ModuleId = u64;
    using ServiceId = u64;
    using CapabilityId = u64;

    inline constexpr ModuleId InvalidModuleId = 0;
    inline constexpr ServiceId InvalidServiceId = 0;
    inline constexpr CapabilityId InvalidCapabilityId = 0;
    inline constexpr u32 MaximumModules = 256;
    inline constexpr u32 MaximumServices = 512;

    enum class ApplicationProfile : u32
    {
        None = 0,
        Runtime = 1u << 0u,
        Editor = 1u << 1u,
        Tool = 1u << 2u,
        Server = 1u << 3u,
        Headless = 1u << 4u,
        Test = 1u << 5u,
        All = (1u << 6u) - 1u
    };

    [[nodiscard]] constexpr ApplicationProfile operator|(const ApplicationProfile left,
                                                          const ApplicationProfile right) noexcept
    {
        return static_cast<ApplicationProfile>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr bool HasProfile(const ApplicationProfile value, const ApplicationProfile profile) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(profile)) != 0;
    }

    enum class ServiceScope : u8
    {
        Process,
        Engine,
        ApplicationSession,
        World,
        Viewport,
        Device,
        EditorWorkspace,
        TestSandbox
    };

    enum class ThreadAffinity : u8
    {
        CompositionThread,
        MainThread,
        PlatformThread,
        AnyWorker,
        IoWorker,
        RenderSubmissionThread
    };

    enum class DependencyKind : u8
    {
        Required,
        Optional,
        StartAfter
    };

    enum class CapabilityCardinality : u8
    {
        AtLeastOne,
        ExactlyOne
    };

    struct ServiceDependency
    {
        ServiceId service = InvalidServiceId;
        DependencyKind kind = DependencyKind::Required;
    };

    struct CapabilityRequirement
    {
        CapabilityId capability = InvalidCapabilityId;
        CapabilityCardinality cardinality = CapabilityCardinality::ExactlyOne;
        bool optional = false;
    };

    struct ModuleDescriptor
    {
        ModuleId id = InvalidModuleId;
        const char* name = nullptr;
        u32 version = 1;
    };

    class Service;
    class ServiceContext;

    using CreateService = Service* (*)(void* userData) noexcept;
    using DestroyService = void (*)(Service* service, void* userData) noexcept;

    struct ServiceDescriptor
    {
        ServiceId id = InvalidServiceId;
        const char* name = nullptr;
        ApplicationProfile profiles = ApplicationProfile::All;
        ServiceScope scope = ServiceScope::Engine;
        ThreadAffinity affinity = ThreadAffinity::CompositionThread;
        containers::ArraySpan<const ServiceDependency> dependencies;
        containers::ArraySpan<const CapabilityId> provides;
        containers::ArraySpan<const CapabilityRequirement> requiresCapabilities;
        containers::ArraySpan<const ServiceId> conflicts;
        CreateService create = nullptr;
        DestroyService destroy = nullptr;
        void* userData = nullptr;
    };

    enum class LifecycleResult : u8
    {
        Success,
        Failure
    };

    struct LifecycleStatus
    {
        LifecycleResult result = LifecycleResult::Success;
        const char* message = nullptr;

        [[nodiscard]] static constexpr LifecycleStatus Success() noexcept { return {}; }
        [[nodiscard]] static constexpr LifecycleStatus Failure(const char* const message) noexcept
        {
            return {LifecycleResult::Failure, message};
        }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return result == LifecycleResult::Success; }
    };

    enum class ServiceState : u8
    {
        Registered,
        Constructed,
        Initializing,
        Initialized,
        Starting,
        Running,
        Quiescing,
        Quiesced,
        Draining,
        Drained,
        Stopping,
        Stopped,
        ShuttingDown,
        Shutdown,
        Failed,
        Destroyed
    };

    class Service
    {
    public:
        virtual ~Service() = default;

        Service(const Service&) = delete;
        Service& operator=(const Service&) = delete;

    protected:
        Service() noexcept = default;
        virtual LifecycleStatus OnInitialize(ServiceContext& context) noexcept;
        virtual LifecycleStatus OnStart(ServiceContext& context) noexcept;
        virtual LifecycleStatus OnQuiesce(ServiceContext& context) noexcept;
        virtual LifecycleStatus OnDrain(ServiceContext& context) noexcept;
        virtual LifecycleStatus OnStop(ServiceContext& context) noexcept;
        virtual LifecycleStatus OnShutdown(ServiceContext& context) noexcept;

    private:
        friend class EngineHost;
    };

    struct ServiceHandle
    {
        ServiceId id = InvalidServiceId;
        u32 generation = 0;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return id != InvalidServiceId && generation != 0;
        }
    };
} // namespace vanguard::application
