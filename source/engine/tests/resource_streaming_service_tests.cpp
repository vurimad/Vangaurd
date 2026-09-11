#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/engine/engine_services.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/engine/resources_service.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/shaders/shaders.hpp>

#include <cstdio>

namespace
{
    inline constexpr vanguard::application::ServiceId PipelineDecoderBlockerServiceId = 0x7270000000000001ull;
    int g_failures = 0;

    void Check(const bool condition, const char* const message) noexcept
    {
        if (condition)
            return;
        std::fprintf(stderr, "[resourceStreamingServiceTests] FAILED: %s\n", message);
        ++g_failures;
    }

    class PipelineDecoderBlockerService final : public vanguard::application::Service
    {
    public:
        bool observedShaderRollback = false;
        bool observedOnlyBlockerLoader = false;

    protected:
        vanguard::application::LifecycleStatus OnInitialize(vanguard::application::ServiceContext& context) noexcept override
        {
            vanguard::engine::ResourcesService* const resources = vanguard::engine::FindResourcesService(context);
            if (resources == nullptr || !resources->GetPipeline().RegisterLoader(
                                            {vanguard::pipelines::PipelineResourceType, "3A.3 pipeline decoder blocker", &Discover,
                                             &Construct, &Destroy, nullptr}))
                return vanguard::application::LifecycleStatus::Failure("could not install 3A.3 pipeline decoder blocker");
            return vanguard::application::LifecycleStatus::Success();
        }

        vanguard::application::LifecycleStatus OnShutdown(vanguard::application::ServiceContext& context) noexcept override
        {
            vanguard::engine::ResourcesService* const resources = vanguard::engine::FindResourcesService(context);
            if (resources == nullptr)
                return vanguard::application::LifecycleStatus::Failure("3A.3 blocker lost Resources during rollback");
            vanguard::resources::ResourcePipeline& pipeline = resources->GetPipeline();
            observedShaderRollback = !pipeline.HasLoader(vanguard::shaders::ShaderResourceType);
            observedOnlyBlockerLoader = pipeline.GetStats().registeredLoaders == 1;
            return pipeline.UnregisterLoader(vanguard::pipelines::PipelineResourceType)
                       ? vanguard::application::LifecycleStatus::Success()
                       : vanguard::application::LifecycleStatus::Failure("could not remove 3A.3 pipeline decoder blocker");
        }

    private:
        static vanguard::resources::Failure Discover(vanguard::resources::ResourceReference,
                                                      vanguard::resources::DependencyBuilder&, void*) noexcept
        {
            return vanguard::resources::Failure::None;
        }

        static vanguard::resources::ResourceObject* Construct(const vanguard::resources::LoadContext&,
                                                               vanguard::resources::Failure& failure, void*) noexcept
        {
            failure = vanguard::resources::Failure::InternalError;
            return nullptr;
        }

        static void Destroy(vanguard::resources::ResourceObject*, void*) noexcept {}
    };

    vanguard::application::Service* CreatePipelineDecoderBlocker(void* const userData) noexcept
    {
        return static_cast<PipelineDecoderBlockerService*>(userData);
    }

    void DestroyPipelineDecoderBlocker(vanguard::application::Service*, void*) noexcept {}

    [[nodiscard]] bool RegisterPipelineDecoderBlocker(vanguard::application::EngineHost& host,
                                                       PipelineDecoderBlockerService& blocker,
                                                       vanguard::application::HostFailure* const failure) noexcept
    {
        constexpr vanguard::application::ServiceDependency dependencies[]{
            {vanguard::engine::ResourcesServiceId, vanguard::application::DependencyKind::Required}};
        vanguard::application::ServiceDescriptor descriptor;
        descriptor.id = PipelineDecoderBlockerServiceId;
        descriptor.name = "pipelineDecoderBlocker";
        descriptor.profiles = vanguard::application::ApplicationProfile::Runtime;
        descriptor.scope = vanguard::application::ServiceScope::Process;
        descriptor.affinity = vanguard::application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 1};
        descriptor.create = &CreatePipelineDecoderBlocker;
        descriptor.destroy = &DestroyPipelineDecoderBlocker;
        descriptor.userData = &blocker;
        return host.RegisterService(vanguard::engine::EngineModuleId, descriptor, failure);
    }
} // namespace

int main()
{
    Check(vanguard::memory::Initialize(), "memory initialization");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "resourceStreamingServiceTests"),
          "diagnostics initialization");
    Check(vanguard::containers::Initialize(), "containers initialization");

    vanguard::application::EngineHost host;
    vanguard::application::HostFailure failure;
    PipelineDecoderBlockerService blocker;
    Check(vanguard::engine::RegisterEngineModule(host, &failure), "engine module registration");
    Check(vanguard::engine::RegisterIoService(host, &failure), "I/O service registration");
    Check(vanguard::engine::RegisterFilesystemService(host, &failure), "Filesystem service registration");
    Check(vanguard::engine::RegisterJobsService(host, &failure), "Jobs service registration");
    Check(vanguard::engine::RegisterResourcesService(host, &failure), "Resources service registration");
    Check(RegisterPipelineDecoderBlocker(host, blocker, &failure), "pipeline blocker registration");
    Check(vanguard::engine::RegisterResourceStreamingService(host, &failure), "Resource Streaming service registration");
    Check(host.Compile(vanguard::application::ApplicationProfile::Runtime, &failure), "rollback graph compilation");
    Check(!host.Start(&failure) && failure.code == vanguard::application::HostFailureCode::InitializeFailure &&
              failure.service == vanguard::engine::ResourceStreamingServiceId,
          "injected VPPL conflict fails managed initialization");
    Check(blocker.observedShaderRollback && blocker.observedOnlyBlockerLoader,
          "partial initialization removes VSHADER and lower loaders before callback state is destroyed");
    Check(!vanguard::jobs::IsInitialized() && !vanguard::filesystem::IsInitialized() && !vanguard::io::IsInitialized(),
          "failed service graph leaves no managed runtime subsystem alive");

    vanguard::diagnostics::Shutdown();
    if (g_failures == 0)
        std::puts("[resourceStreamingServiceTests] service rollback proof passed");
    return g_failures == 0 ? 0 : 1;
}
