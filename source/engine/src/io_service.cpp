#include <vanguard/engine/engine_services.hpp>

#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class IoService final : public app::Service
    {
    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            if (vanguard::io::IsInitialized())
                return app::LifecycleStatus::Failure("I/O was initialized outside the managed engine lifecycle");
            if (!vanguard::io::Initialize()) return app::LifecycleStatus::Failure("I/O initialization failed");
            m_initialized = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            return m_initialized && vanguard::io::IsInitialized()
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("I/O did not enter a running state");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (!m_initialized) return app::LifecycleStatus::Success();
            vanguard::io::Shutdown();
            if (vanguard::io::IsInitialized()) return app::LifecycleStatus::Failure("I/O shutdown failed");
            m_initialized = false;
            return app::LifecycleStatus::Success();
        }

    private:
        bool m_initialized = false;
    };

    app::Service* CreateIoService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Io, sizeof(IoService), alignof(IoService));
        return block ? ::new (block.address) IoService() : nullptr;
    }

    void DestroyIoService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<IoService*>(service)->~IoService();
        vanguard::memory::MemoryBlock block{service, sizeof(IoService), vanguard::memory::PoolId::Io};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterIoService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::CapabilityId providedCapabilities[]{IoCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = IoServiceId;
        descriptor.name = "io";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateIoService;
        descriptor.destroy = DestroyIoService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }
} // namespace vanguard::engine
