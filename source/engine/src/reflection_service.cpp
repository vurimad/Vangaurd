#include <vanguard/engine/engine_services.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/reflection/reflection.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class ManagedReflectionService final : public app::Service
    {
    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            return vanguard::reflection::Initialize()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Reflection initialization failed");
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            return vanguard::reflection::IsInitialized()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Reflection did not enter a running state");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            // The imported reflection backend currently owns process-lifetime static type metadata and has no reversible
            // shutdown operation. The managed service still establishes explicit startup ordering and capability ownership.
            return vanguard::reflection::IsInitialized()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Reflection became unavailable before engine shutdown");
        }
    };

    app::Service* CreateReflectionService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Reflection, sizeof(ManagedReflectionService), alignof(ManagedReflectionService));
        return block ? ::new (block.address) ManagedReflectionService() : nullptr;
    }

    void DestroyReflectionService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedReflectionService*>(service)->~ManagedReflectionService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedReflectionService), vanguard::memory::PoolId::Reflection};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterReflectionService(application::EngineHost& host,
                                   application::HostFailure* const failure) noexcept
    {
        constexpr application::CapabilityId capabilities[]{ReflectionCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = ReflectionServiceId;
        descriptor.name = "reflection";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateReflectionService;
        descriptor.destroy = DestroyReflectionService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }
} // namespace vanguard::engine
