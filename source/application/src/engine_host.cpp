#include <vanguard/application/engine_host.hpp>

#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/pool.hpp>

#include <new>

namespace
{
    using namespace vanguard;
    namespace app = vanguard::application;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateApplicationObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Runtime, sizeof(Type), alignof(Type));
        return block ? ::new (block.address) Type(static_cast<Args&&>(args)...) : nullptr;
    }

    template<typename Type>
    void DeleteApplicationObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Runtime};
        memory::Free(block);
    }

    [[nodiscard]] bool ValidName(const char* const name) noexcept
    {
        return name != nullptr && name[0] != '\0';
    }
} // namespace

namespace vanguard::application
{
    LifecycleStatus Service::OnInitialize(ServiceContext&) noexcept { return LifecycleStatus::Success(); }
    LifecycleStatus Service::OnStart(ServiceContext&) noexcept { return LifecycleStatus::Success(); }
    LifecycleStatus Service::OnQuiesce(ServiceContext&) noexcept { return LifecycleStatus::Success(); }
    LifecycleStatus Service::OnDrain(ServiceContext&) noexcept { return LifecycleStatus::Success(); }
    LifecycleStatus Service::OnStop(ServiceContext&) noexcept { return LifecycleStatus::Success(); }
    LifecycleStatus Service::OnShutdown(ServiceContext&) noexcept { return LifecycleStatus::Success(); }

    struct EngineHost::Impl
    {
        struct ModuleRecord
        {
            ModuleDescriptor descriptor;
        };

        struct ServiceRecord
        {
            ServiceRecord() noexcept
                : dependencies(memory::pools::Runtime::GetInstance()), provides(memory::pools::Runtime::GetInstance()),
                  requirements(memory::pools::Runtime::GetInstance()), conflicts(memory::pools::Runtime::GetInstance()) {}

            ModuleId module = InvalidModuleId;
            ServiceDescriptor descriptor;
            containers::DynamicArray<ServiceDependency> dependencies;
            containers::DynamicArray<CapabilityId> provides;
            containers::DynamicArray<CapabilityRequirement> requirements;
            containers::DynamicArray<ServiceId> conflicts;
            Service* instance = nullptr;
            ServiceState state = ServiceState::Registered;
            u32 generation = 0;
            bool selected = false;
            bool initializeAttempted = false;
            bool initialized = false;
            bool startAttempted = false;
            bool started = false;
        };

        Impl() noexcept
            : modules(memory::pools::Runtime::GetInstance()), services(memory::pools::Runtime::GetInstance()),
              plan(memory::pools::Runtime::GetInstance()), events(memory::pools::Runtime::GetInstance())
        {
            modules.Reserve(MaximumModules);
            services.Reserve(MaximumServices);
            plan.Reserve(MaximumServices);
            events.Reserve(MaximumServices * 10u);
        }

        [[nodiscard]] ModuleRecord* FindModule(const ModuleId id) const noexcept
        {
            for (ModuleRecord* const module : modules) if (module->descriptor.id == id) return module;
            return nullptr;
        }

        [[nodiscard]] ServiceRecord* FindService(const ServiceId id) const noexcept
        {
            for (ServiceRecord* const service : services) if (service->descriptor.id == id) return service;
            return nullptr;
        }

        [[nodiscard]] ServiceRecord* FindPlanned(const ServiceId id) const noexcept
        {
            ServiceRecord* const service = FindService(id);
            return service != nullptr && service->selected ? service : nullptr;
        }

        [[nodiscard]] bool Provides(const ServiceRecord& service, const CapabilityId capability) const noexcept
        {
            for (const CapabilityId provided : service.provides) if (provided == capability) return true;
            return false;
        }

        [[nodiscard]] ServiceRecord* CapabilityProvider(const CapabilityId capability) const noexcept
        {
            ServiceRecord* provider = nullptr;
            for (ServiceRecord* const candidate : plan)
            {
                if (!Provides(*candidate, capability)) continue;
                if (provider == nullptr || candidate->descriptor.id < provider->descriptor.id) provider = candidate;
            }
            return provider;
        }

        [[nodiscard]] ServiceRecord* SelectedCapabilityProvider(const CapabilityId capability) const noexcept
        {
            ServiceRecord* provider = nullptr;
            for (ServiceRecord* const candidate : services)
            {
                if (!candidate->selected || !Provides(*candidate, capability)) continue;
                if (provider == nullptr || candidate->descriptor.id < provider->descriptor.id) provider = candidate;
            }
            return provider;
        }

        [[nodiscard]] bool PlanContains(const ServiceId id) const noexcept
        {
            for (const ServiceRecord* const service : plan) if (service->descriptor.id == id) return true;
            return false;
        }

        [[nodiscard]] bool DependsOn(const ServiceRecord& dependent, const ServiceRecord& provider) const noexcept
        {
            for (const ServiceDependency& dependency : dependent.dependencies)
            {
                if (dependency.service != provider.descriptor.id) continue;
                if (dependency.kind == DependencyKind::Required || dependency.kind == DependencyKind::StartAfter)
                    return true;
                if (dependency.kind == DependencyKind::Optional && provider.selected) return true;
            }
            for (const CapabilityRequirement& requirement : dependent.requirements)
            {
                if (SelectedCapabilityProvider(requirement.capability) == &provider) return true;
            }
            return false;
        }

        void SetFailure(HostFailure* const output, const HostFailureCode code, const ServiceId service,
                        const ServiceId related, const CapabilityId capability, const char* const message) const noexcept
        {
            if (output != nullptr && output->code == HostFailureCode::None)
                *output = {code, service, related, capability, message};
            VG_LOG_ERROR(diagnostics::Category::Services,
                         "application lifecycle failure: code=%u service=%llu related=%llu capability=%llu message=%s",
                         static_cast<u32>(code), static_cast<unsigned long long>(service),
                         static_cast<unsigned long long>(related), static_cast<unsigned long long>(capability),
                         message != nullptr ? message : "unspecified");
        }

        void Emit(const ServiceRecord* const service, const LifecycleStage stage, const ServiceState stateValue,
                  const HostFailureCode result = HostFailureCode::None, const char* const message = nullptr) noexcept
        {
            LifecycleEvent event;
            event.sequence = ++eventSequence;
            event.module = service != nullptr ? service->module : InvalidModuleId;
            event.service = service != nullptr ? service->descriptor.id : InvalidServiceId;
            event.stage = stage;
            event.state = stateValue;
            event.result = result;
            event.message = message;
            events.PushBack(event);
            if (sink != nullptr) sink(event, sinkUserData);
        }

        [[nodiscard]] bool RunStage(ServiceRecord& service, const LifecycleStage stage, HostFailure* const failure,
                                    HostFailureCode& firstCleanupFailure) noexcept
        {
            ServiceContext context(*owner, service.descriptor.id);
            LifecycleStatus status;
            HostFailureCode failureCode = HostFailureCode::None;
            switch (stage)
            {
            case LifecycleStage::Initialize:
                service.state = ServiceState::Initializing;
                service.initializeAttempted = true;
                Emit(&service, stage, service.state);
                status = service.instance->OnInitialize(context);
                if (status) { service.state = ServiceState::Initialized; service.initialized = true; }
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::InitializeFailure; }
                break;
            case LifecycleStage::Start:
                service.state = ServiceState::Starting;
                service.startAttempted = true;
                Emit(&service, stage, service.state);
                status = service.instance->OnStart(context);
                if (status) { service.state = ServiceState::Running; service.started = true; }
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::StartFailure; }
                break;
            case LifecycleStage::Quiesce:
                service.state = ServiceState::Quiescing;
                Emit(&service, stage, service.state);
                status = service.instance->OnQuiesce(context);
                if (status) service.state = ServiceState::Quiesced;
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::QuiesceFailure; }
                break;
            case LifecycleStage::Drain:
                service.state = ServiceState::Draining;
                Emit(&service, stage, service.state);
                status = service.instance->OnDrain(context);
                if (status) service.state = ServiceState::Drained;
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::DrainFailure; }
                break;
            case LifecycleStage::Stop:
                service.state = ServiceState::Stopping;
                Emit(&service, stage, service.state);
                status = service.instance->OnStop(context);
                service.started = false;
                if (status) service.state = ServiceState::Stopped;
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::StopFailure; }
                break;
            case LifecycleStage::Shutdown:
                service.state = ServiceState::ShuttingDown;
                Emit(&service, stage, service.state);
                status = service.instance->OnShutdown(context);
                service.initialized = false;
                if (status) service.state = ServiceState::Shutdown;
                else { service.state = ServiceState::Failed; failureCode = HostFailureCode::ShutdownFailure; }
                break;
            default: return false;
            }
            Emit(&service, stage, service.state, failureCode, status.message);
            if (status) return true;
            SetFailure(failure, failureCode, service.descriptor.id, InvalidServiceId, InvalidCapabilityId,
                       status.message != nullptr ? status.message : "service lifecycle callback failed");
            if (firstCleanupFailure == HostFailureCode::None) firstCleanupFailure = failureCode;
            return false;
        }

        void DestroyInstance(ServiceRecord& service) noexcept
        {
            if (service.instance == nullptr) return;
            service.descriptor.destroy(service.instance, service.descriptor.userData);
            service.instance = nullptr;
            service.state = ServiceState::Destroyed;
            Emit(&service, LifecycleStage::Destroy, service.state);
        }

        [[nodiscard]] bool Unwind(const bool rollback, HostFailure* const cleanupFailure) noexcept
        {
            HostFailureCode firstCleanupFailure = HostFailureCode::None;
            if (rollback) Emit(nullptr, LifecycleStage::Rollback, ServiceState::Failed);
            for (u32 index = plan.Size(); index > 0; --index)
            {
                ServiceRecord& service = *plan[index - 1u];
                if (service.instance != nullptr && service.started)
                    static_cast<void>(RunStage(service, LifecycleStage::Quiesce, cleanupFailure, firstCleanupFailure));
            }
            for (u32 index = plan.Size(); index > 0; --index)
            {
                ServiceRecord& service = *plan[index - 1u];
                if (service.instance != nullptr && service.started)
                    static_cast<void>(RunStage(service, LifecycleStage::Drain, cleanupFailure, firstCleanupFailure));
            }
            for (u32 index = plan.Size(); index > 0; --index)
            {
                ServiceRecord& service = *plan[index - 1u];
                if (service.instance != nullptr && service.startAttempted)
                    static_cast<void>(RunStage(service, LifecycleStage::Stop, cleanupFailure, firstCleanupFailure));
            }
            for (u32 index = plan.Size(); index > 0; --index)
            {
                ServiceRecord& service = *plan[index - 1u];
                if (service.instance != nullptr && service.initializeAttempted)
                    static_cast<void>(RunStage(service, LifecycleStage::Shutdown, cleanupFailure, firstCleanupFailure));
            }
            for (u32 index = plan.Size(); index > 0; --index) DestroyInstance(*plan[index - 1u]);
            return firstCleanupFailure == HostFailureCode::None;
        }

        EngineHost* owner = nullptr;
        containers::DynamicArray<ModuleRecord*> modules;
        containers::DynamicArray<ServiceRecord*> services;
        containers::DynamicArray<ServiceRecord*> plan;
        containers::DynamicArray<LifecycleEvent> events;
        LifecycleSink sink = nullptr;
        void* sinkUserData = nullptr;
        HostState state = HostState::Building;
        ApplicationProfile profile = ApplicationProfile::None;
        u64 eventSequence = 0;
        u32 generationSequence = 0;
    };

    EngineHost::~EngineHost()
    {
        if (m_impl == nullptr) return;
        if (m_impl->state == HostState::Running || m_impl->state == HostState::Starting)
        {
            HostFailure ignored;
            static_cast<void>(m_impl->Unwind(false, &ignored));
        }
        for (Impl::ServiceRecord* const service : m_impl->services) DeleteApplicationObject(service);
        for (Impl::ModuleRecord* const module : m_impl->modules) DeleteApplicationObject(module);
        DeleteApplicationObject(m_impl);
        m_impl = nullptr;
    }

    bool EngineHost::RegisterModule(const ModuleDescriptor& descriptor, HostFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (descriptor.id == InvalidModuleId || !ValidName(descriptor.name) || descriptor.version == 0)
        {
            if (failure != nullptr) *failure = {HostFailureCode::InvalidArgument, InvalidServiceId,
                                                InvalidServiceId, InvalidCapabilityId, "invalid module descriptor"};
            return false;
        }
        if (m_impl == nullptr)
        {
            m_impl = AllocateApplicationObject<Impl>();
            if (m_impl == nullptr)
            {
                if (failure != nullptr) failure->code = HostFailureCode::LimitExceeded;
                return false;
            }
            m_impl->owner = this;
        }
        if (m_impl->state != HostState::Building)
        {
            m_impl->SetFailure(failure, HostFailureCode::InvalidState, InvalidServiceId, InvalidServiceId,
                               InvalidCapabilityId, "module registration is closed");
            return false;
        }
        if (m_impl->FindModule(descriptor.id) != nullptr)
        {
            m_impl->SetFailure(failure, HostFailureCode::DuplicateModule, InvalidServiceId, InvalidServiceId,
                               InvalidCapabilityId, "duplicate module ID");
            return false;
        }
        if (m_impl->modules.Size() >= MaximumModules)
        {
            m_impl->SetFailure(failure, HostFailureCode::LimitExceeded, InvalidServiceId, InvalidServiceId,
                               InvalidCapabilityId, "module limit exceeded");
            return false;
        }
        Impl::ModuleRecord* const module = AllocateApplicationObject<Impl::ModuleRecord>();
        if (module == nullptr)
        {
            m_impl->SetFailure(failure, HostFailureCode::LimitExceeded, InvalidServiceId, InvalidServiceId,
                               InvalidCapabilityId, "module record allocation failed");
            return false;
        }
        module->descriptor = descriptor;
        m_impl->modules.PushBack(module);
        return true;
    }

    bool EngineHost::RegisterService(const ModuleId owner, const ServiceDescriptor& descriptor,
                                     HostFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->state != HostState::Building || descriptor.id == InvalidServiceId ||
            !ValidName(descriptor.name) || descriptor.profiles == ApplicationProfile::None ||
            descriptor.create == nullptr || descriptor.destroy == nullptr)
        {
            if (m_impl != nullptr)
                m_impl->SetFailure(failure, m_impl->state != HostState::Building ? HostFailureCode::InvalidState
                                                                                : HostFailureCode::InvalidArgument,
                                   descriptor.id, InvalidServiceId, InvalidCapabilityId, "invalid service registration");
            else if (failure != nullptr)
                *failure = {HostFailureCode::InvalidState, descriptor.id, InvalidServiceId, InvalidCapabilityId,
                            "register a module before its services"};
            return false;
        }
        if (m_impl->FindModule(owner) == nullptr)
        {
            m_impl->SetFailure(failure, HostFailureCode::UnknownModule, descriptor.id, InvalidServiceId,
                               InvalidCapabilityId, "service owner module is not registered");
            return false;
        }
        if (m_impl->FindService(descriptor.id) != nullptr)
        {
            m_impl->SetFailure(failure, HostFailureCode::DuplicateService, descriptor.id, descriptor.id,
                               InvalidCapabilityId, "duplicate service ID");
            return false;
        }
        if (m_impl->services.Size() >= MaximumServices)
        {
            m_impl->SetFailure(failure, HostFailureCode::LimitExceeded, descriptor.id, InvalidServiceId,
                               InvalidCapabilityId, "service limit exceeded");
            return false;
        }
        Impl::ServiceRecord* const service = AllocateApplicationObject<Impl::ServiceRecord>();
        if (service == nullptr)
        {
            m_impl->SetFailure(failure, HostFailureCode::LimitExceeded, descriptor.id, InvalidServiceId,
                               InvalidCapabilityId, "service record allocation failed");
            return false;
        }
        service->module = owner;
        service->descriptor = descriptor;
        service->dependencies.Reserve(descriptor.dependencies.Size());
        service->provides.Reserve(descriptor.provides.Size());
        service->requirements.Reserve(descriptor.requiresCapabilities.Size());
        service->conflicts.Reserve(descriptor.conflicts.Size());
        for (const ServiceDependency dependency : descriptor.dependencies)
        {
            if (dependency.service == InvalidServiceId || dependency.service == descriptor.id)
            {
                DeleteApplicationObject(service);
                m_impl->SetFailure(failure, HostFailureCode::InvalidArgument, descriptor.id, dependency.service,
                                   InvalidCapabilityId, "invalid service dependency");
                return false;
            }
            service->dependencies.PushBack(dependency);
        }
        for (const CapabilityId capability : descriptor.provides)
        {
            if (capability == InvalidCapabilityId) { DeleteApplicationObject(service); return false; }
            service->provides.PushBack(capability);
        }
        for (const CapabilityRequirement requirement : descriptor.requiresCapabilities)
        {
            if (requirement.capability == InvalidCapabilityId) { DeleteApplicationObject(service); return false; }
            service->requirements.PushBack(requirement);
        }
        for (const ServiceId conflict : descriptor.conflicts)
        {
            if (conflict == InvalidServiceId || conflict == descriptor.id) { DeleteApplicationObject(service); return false; }
            service->conflicts.PushBack(conflict);
        }
        service->descriptor.dependencies = service->dependencies;
        service->descriptor.provides = service->provides;
        service->descriptor.requiresCapabilities = service->requirements;
        service->descriptor.conflicts = service->conflicts;
        m_impl->services.PushBack(service);
        return true;
    }

    bool EngineHost::Compile(const ApplicationProfile profile, HostFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->state != HostState::Building || profile == ApplicationProfile::None)
        {
            if (failure != nullptr) failure->code = HostFailureCode::InvalidState;
            return false;
        }
        m_impl->profile = profile;
        m_impl->plan.Clear();
        u32 selectedCount = 0;
        for (Impl::ServiceRecord* const service : m_impl->services)
        {
            service->selected = HasProfile(service->descriptor.profiles, profile);
            selectedCount += service->selected;
        }
        for (Impl::ServiceRecord* const service : m_impl->services)
        {
            if (!service->selected) continue;
            for (const ServiceDependency& dependency : service->dependencies)
            {
                const Impl::ServiceRecord* const target = m_impl->FindPlanned(dependency.service);
                if (target != nullptr || dependency.kind != DependencyKind::Required) continue;
                m_impl->SetFailure(failure, HostFailureCode::MissingDependency, service->descriptor.id,
                                   dependency.service, InvalidCapabilityId, "required service is unavailable in this profile");
                return false;
            }
            for (const ServiceId conflict : service->conflicts)
            {
                if (m_impl->FindPlanned(conflict) == nullptr) continue;
                m_impl->SetFailure(failure, HostFailureCode::ConflictingService, service->descriptor.id, conflict,
                                   InvalidCapabilityId, "conflicting services are selected");
                return false;
            }
            for (const CapabilityRequirement& requirement : service->requirements)
            {
                u32 providers = 0;
                for (const Impl::ServiceRecord* const candidate : m_impl->services)
                    if (candidate->selected && m_impl->Provides(*candidate, requirement.capability)) ++providers;
                if (providers == 0 && !requirement.optional)
                {
                    m_impl->SetFailure(failure, HostFailureCode::MissingCapability, service->descriptor.id,
                                       InvalidServiceId, requirement.capability, "required capability has no provider");
                    return false;
                }
                if (providers > 1 && requirement.cardinality == CapabilityCardinality::ExactlyOne)
                {
                    m_impl->SetFailure(failure, HostFailureCode::AmbiguousCapability, service->descriptor.id,
                                       InvalidServiceId, requirement.capability, "capability requires exactly one provider");
                    return false;
                }
            }
        }
        while (m_impl->plan.Size() < selectedCount)
        {
            Impl::ServiceRecord* next = nullptr;
            for (Impl::ServiceRecord* const candidate : m_impl->services)
            {
                if (!candidate->selected || m_impl->PlanContains(candidate->descriptor.id)) continue;
                bool ready = true;
                for (Impl::ServiceRecord* const provider : m_impl->services)
                {
                    if (!provider->selected || !m_impl->DependsOn(*candidate, *provider)) continue;
                    if (!m_impl->PlanContains(provider->descriptor.id)) { ready = false; break; }
                }
                if (ready && (next == nullptr || candidate->descriptor.id < next->descriptor.id)) next = candidate;
            }
            if (next == nullptr)
            {
                ServiceId blocked = InvalidServiceId;
                for (const Impl::ServiceRecord* const service : m_impl->services)
                    if (service->selected && !m_impl->PlanContains(service->descriptor.id)) { blocked = service->descriptor.id; break; }
                m_impl->SetFailure(failure, HostFailureCode::DependencyCycle, blocked, InvalidServiceId,
                                   InvalidCapabilityId, "service dependency graph contains a cycle");
                return false;
            }
            m_impl->plan.PushBack(next);
            m_impl->Emit(next, LifecycleStage::Compile, ServiceState::Registered);
        }
        m_impl->state = HostState::Compiled;
        return true;
    }

    bool EngineHost::Start(HostFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr || m_impl->state != HostState::Compiled)
        {
            if (failure != nullptr) failure->code = HostFailureCode::InvalidState;
            return false;
        }
        m_impl->state = HostState::Starting;
        for (Impl::ServiceRecord* const service : m_impl->plan)
        {
            service->instance = service->descriptor.create(service->descriptor.userData);
            if (service->instance == nullptr)
            {
                service->state = ServiceState::Failed;
                m_impl->Emit(service, LifecycleStage::Construct, service->state, HostFailureCode::FactoryFailure,
                             "service factory returned null");
                m_impl->SetFailure(failure, HostFailureCode::FactoryFailure, service->descriptor.id, InvalidServiceId,
                                   InvalidCapabilityId, "service factory returned null");
                m_impl->state = HostState::RollingBack;
                static_cast<void>(m_impl->Unwind(true, nullptr));
                m_impl->state = HostState::Failed;
                return false;
            }
            service->generation = ++m_impl->generationSequence;
            service->state = ServiceState::Constructed;
            m_impl->Emit(service, LifecycleStage::Construct, service->state);
            HostFailureCode ignored = HostFailureCode::None;
            if (!m_impl->RunStage(*service, LifecycleStage::Initialize, failure, ignored) ||
                !m_impl->RunStage(*service, LifecycleStage::Start, failure, ignored))
            {
                m_impl->state = HostState::RollingBack;
                static_cast<void>(m_impl->Unwind(true, nullptr));
                m_impl->state = HostState::Failed;
                return false;
            }
        }
        m_impl->state = HostState::Running;
        return true;
    }

    bool EngineHost::Shutdown(HostFailure* const failure) noexcept
    {
        if (failure != nullptr) *failure = {};
        if (m_impl == nullptr) return true;
        if (m_impl->state == HostState::Stopped) return true;
        if (m_impl->state != HostState::Running)
        {
            m_impl->SetFailure(failure, HostFailureCode::InvalidState, InvalidServiceId, InvalidServiceId,
                               InvalidCapabilityId, "host is not running");
            return false;
        }
        m_impl->state = HostState::Stopping;
        const bool clean = m_impl->Unwind(false, failure);
        if (!clean)
        {
            m_impl->state = HostState::Failed;
            return false;
        }
        m_impl->state = HostState::Stopped;
        return true;
    }

    HostState EngineHost::State() const noexcept { return m_impl != nullptr ? m_impl->state : HostState::Building; }
    ApplicationProfile EngineHost::Profile() const noexcept
    {
        return m_impl != nullptr ? m_impl->profile : ApplicationProfile::None;
    }

    Service* EngineHost::Find(const ServiceId service) const noexcept
    {
        if (m_impl == nullptr) return nullptr;
        const Impl::ServiceRecord* const record = m_impl->FindPlanned(service);
        return record != nullptr ? record->instance : nullptr;
    }

    Service* EngineHost::FindCapability(const CapabilityId capability) const noexcept
    {
        if (m_impl == nullptr || capability == InvalidCapabilityId) return nullptr;
        const Impl::ServiceRecord* const provider = m_impl->CapabilityProvider(capability);
        return provider != nullptr ? provider->instance : nullptr;
    }

    ServiceHandle EngineHost::Acquire(const ServiceId service) const noexcept
    {
        if (m_impl == nullptr) return {};
        const Impl::ServiceRecord* const record = m_impl->FindPlanned(service);
        return record != nullptr && record->instance != nullptr ? ServiceHandle{service, record->generation} : ServiceHandle{};
    }

    Service* EngineHost::Resolve(const ServiceHandle handle) const noexcept
    {
        if (m_impl == nullptr || !handle) return nullptr;
        const Impl::ServiceRecord* const record = m_impl->FindPlanned(handle.id);
        return record != nullptr && record->generation == handle.generation ? record->instance : nullptr;
    }

    ServiceState EngineHost::StateOf(const ServiceId service) const noexcept
    {
        if (m_impl == nullptr) return ServiceState::Destroyed;
        const Impl::ServiceRecord* const record = m_impl->FindService(service);
        return record != nullptr ? record->state : ServiceState::Destroyed;
    }

    void EngineHost::SetLifecycleSink(const LifecycleSink sink, void* const userData) noexcept
    {
        if (m_impl == nullptr)
        {
            m_impl = AllocateApplicationObject<Impl>();
            if (m_impl == nullptr) return;
            m_impl->owner = this;
        }
        m_impl->sink = sink;
        m_impl->sinkUserData = userData;
    }

    void EngineHost::VisitLifecycleEvents(const LifecycleVisitor visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr) return;
        for (const LifecycleEvent& event : m_impl->events) visitor(event, userData);
    }

    void EngineHost::VisitCompiledServices(const CompiledServiceVisitor visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr) return;
        for (u32 index = 0; index < m_impl->plan.Size(); ++index)
        {
            const Impl::ServiceRecord& service = *m_impl->plan[index];
            visitor({service.module, service.descriptor.id, service.descriptor.name, index,
                     service.descriptor.scope, service.descriptor.affinity}, userData);
        }
    }

    HostStats EngineHost::GetStats() const noexcept
    {
        HostStats stats;
        if (m_impl == nullptr) return stats;
        stats.registeredModules = m_impl->modules.Size();
        stats.registeredServices = m_impl->services.Size();
        stats.selectedServices = m_impl->plan.Size();
        stats.lifecycleEvents = m_impl->eventSequence;
        stats.state = m_impl->state;
        stats.profile = m_impl->profile;
        for (const Impl::ServiceRecord* const service : m_impl->plan)
        {
            stats.constructedServices += service->instance != nullptr;
            stats.runningServices += service->state == ServiceState::Running;
            stats.failedServices += service->state == ServiceState::Failed;
        }
        return stats;
    }

    ServiceContext::ServiceContext(EngineHost& host, const ServiceId currentService) noexcept
        : m_host(&host), m_currentService(currentService) {}
    ApplicationProfile ServiceContext::Profile() const noexcept { return m_host->Profile(); }
    ServiceId ServiceContext::CurrentService() const noexcept { return m_currentService; }
    Service* ServiceContext::Find(const ServiceId service) const noexcept { return m_host->Find(service); }
    Service* ServiceContext::FindCapability(const CapabilityId capability) const noexcept
    {
        return m_host->FindCapability(capability);
    }
    ServiceHandle ServiceContext::Acquire(const ServiceId service) const noexcept { return m_host->Acquire(service); }
    Service* ServiceContext::Resolve(const ServiceHandle handle) const noexcept { return m_host->Resolve(handle); }
} // namespace vanguard::application
