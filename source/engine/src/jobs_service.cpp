#include <vanguard/engine/engine_services.hpp>

#include <vanguard/jobs/jobs.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    [[nodiscard]] vanguard::jobs::Config SelectConfig(const app::ApplicationProfile profile) noexcept
    {
        if (app::HasProfile(profile, app::ApplicationProfile::Editor)) return vanguard::jobs::EditorConfig();
        if (app::HasProfile(profile, app::ApplicationProfile::Tool)) return vanguard::jobs::ToolConfig();
        return vanguard::jobs::RuntimeConfig();
    }

    class JobsService final : public app::Service
    {
    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            if (vanguard::jobs::IsInitialized())
                return app::LifecycleStatus::Failure("Jobs was initialized outside the managed engine lifecycle");
            if (!vanguard::jobs::Initialize(SelectConfig(context.Profile())))
                return app::LifecycleStatus::Failure("Jobs initialization failed");
            m_initialized = true;
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnStart(app::ServiceContext&) noexcept override
        {
            if (!m_initialized || !vanguard::jobs::IsInitialized() || vanguard::jobs::WorkerCount() == 0)
                return app::LifecycleStatus::Failure("Jobs did not enter a runnable scheduler state");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            return vanguard::jobs::OutstandingJobCount() == 0
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Jobs still has outstanding work after dependent services quiesced");
        }

        app::LifecycleStatus OnDrain(app::ServiceContext&) noexcept override
        {
            const vanguard::jobs::SchedulerStats stats = vanguard::jobs::GetSchedulerStats();
            return stats.outstandingJobs == 0 && stats.activeDeferrals == 0
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("Jobs did not drain all work and completion deferrals");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (!m_initialized) return app::LifecycleStatus::Success();
            const vanguard::jobs::SchedulerStats stats = vanguard::jobs::GetSchedulerStats();
            if (stats.outstandingJobs != 0 || stats.liveBuilders != 0 || stats.liveCounters != 0 ||
                stats.activeDeferrals != 0)
                return app::LifecycleStatus::Failure("Jobs shutdown was blocked by live work or scheduler handles");
            if (!vanguard::jobs::Shutdown()) return app::LifecycleStatus::Failure("Jobs shutdown failed");
            m_initialized = false;
            return app::LifecycleStatus::Success();
        }

    private:
        bool m_initialized = false;
    };

    app::Service* CreateJobsService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Jobs, sizeof(JobsService), alignof(JobsService));
        return block ? ::new (block.address) JobsService() : nullptr;
    }

    void DestroyJobsService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<JobsService*>(service)->~JobsService();
        vanguard::memory::MemoryBlock block{service, sizeof(JobsService), vanguard::memory::PoolId::Jobs};
        vanguard::memory::Free(block);
    }
}

namespace vanguard::engine
{
    bool RegisterEngineModule(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        return host.RegisterModule({EngineModuleId, "engine", 1}, failure);
    }

    bool RegisterJobsService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{
            {IoServiceId, application::DependencyKind::Required},
            {FilesystemServiceId, application::DependencyKind::StartAfter}};
        constexpr application::CapabilityId providedCapabilities[]{JobSchedulerCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = JobsServiceId;
        descriptor.name = "jobs";
        descriptor.profiles = application::ApplicationProfile::All;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 2};
        descriptor.provides = {providedCapabilities, 1};
        descriptor.create = CreateJobsService;
        descriptor.destroy = DestroyJobsService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }
} // namespace vanguard::engine
