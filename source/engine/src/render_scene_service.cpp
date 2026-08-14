#include <vanguard/engine/render_scene_service.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;

    class ManagedRenderSceneService final : public vanguard::engine::RenderSceneService
    {
    public:
        [[nodiscard]] vanguard::rendering::RenderSceneManager& Scenes() noexcept override { return m_scenes; }
        [[nodiscard]] const vanguard::rendering::RenderSceneManager& Scenes() const noexcept override { return m_scenes; }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext&) noexcept override
        {
            vanguard::rendering::RenderSceneFailure failure;
            if (!m_scenes.Initialize({}, &failure))
                return app::LifecycleStatus::Failure(
                    failure.message != nullptr ? failure.message : "RenderSceneManager initialization failed");
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnQuiesce(app::ServiceContext&) noexcept override
        {
            const vanguard::rendering::RenderSceneManagerStats stats = m_scenes.GetStats();
            return stats.activeScenes == 0 && stats.destroyingScenes == 0
                ? app::LifecycleStatus::Success()
                : app::LifecycleStatus::Failure("RenderSceneService has live scenes");
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            vanguard::rendering::RenderSceneFailure failure;
            if (!m_scenes.Shutdown(&failure))
                return app::LifecycleStatus::Failure(
                    failure.message != nullptr ? failure.message : "RenderSceneManager shutdown failed");
            return app::LifecycleStatus::Success();
        }

    private:
        vanguard::rendering::RenderSceneManager m_scenes;
    };

    app::Service* CreateRenderSceneService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block = vanguard::memory::Allocate(
            vanguard::memory::PoolId::Rendering, sizeof(ManagedRenderSceneService),
            alignof(ManagedRenderSceneService));
        return block ? ::new (block.address) ManagedRenderSceneService() : nullptr;
    }

    void DestroyRenderSceneService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr) return;
        static_cast<ManagedRenderSceneService*>(service)->~ManagedRenderSceneService();
        vanguard::memory::MemoryBlock block{
            service, sizeof(ManagedRenderSceneService), vanguard::memory::PoolId::Rendering};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterRenderSceneService(application::EngineHost& host,
                                    application::HostFailure* const failure) noexcept
    {
        constexpr application::CapabilityId capabilities[]{RenderSceneCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = RenderSceneServiceId;
        descriptor.name = "renderScene";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor |
                              application::ApplicationProfile::Tool;
        descriptor.scope = application::ServiceScope::Process;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateRenderSceneService;
        descriptor.destroy = DestroyRenderSceneService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    RenderSceneService* FindRenderSceneService(application::EngineHost& host) noexcept
    {
        return static_cast<RenderSceneService*>(host.FindCapability(RenderSceneCapabilityId));
    }

    RenderSceneService* FindRenderSceneService(application::ServiceContext& context) noexcept
    {
        return static_cast<RenderSceneService*>(context.FindCapability(RenderSceneCapabilityId));
    }
} // namespace vanguard::engine
