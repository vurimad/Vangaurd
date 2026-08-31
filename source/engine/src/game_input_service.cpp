#include <vanguard/engine/game_input_service.hpp>

#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/resource_streaming_service.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace
{
    namespace app = vanguard::application;
    namespace engine = vanguard::engine;
    namespace game_input = vanguard::game_input;

    [[nodiscard]] vanguard::resources::Failure ConvertMappingFailure(const game_input::MappingResult result) noexcept
    {
        using Failure = vanguard::resources::Failure;
        switch (result)
        {
        case game_input::MappingResult::Success:
            return Failure::None;
        case game_input::MappingResult::InvalidMagic:
        case game_input::MappingResult::IntegrityFailure:
            return Failure::IntegrityFailure;
        case game_input::MappingResult::UnsupportedVersion:
            return Failure::UnsupportedVersion;
        case game_input::MappingResult::LimitExceeded:
            return Failure::OutOfMemory;
        case game_input::MappingResult::IoFailure:
            return Failure::IoFailure;
        default:
            return Failure::DeserializationFailure;
        }
    }

    [[nodiscard]] vanguard::resources::ResourceObject* DecodeMapping(const vanguard::resources::ResourceReference reference, const void* const data,
                                                                     const vanguard::usize size, const vanguard::resources::LoadContext&,
                                                                     vanguard::resources::Failure& failure, void*) noexcept
    {
        if (reference.ExpectedType() != game_input::MappingResourceType)
        {
            failure = vanguard::resources::Failure::UnknownType;
            return nullptr;
        }
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Input, sizeof(game_input::MappingResource), alignof(game_input::MappingResource));
        if (!block)
        {
            failure = vanguard::resources::Failure::OutOfMemory;
            return nullptr;
        }
        auto* const resource = ::new (block.address) game_input::MappingResource();
        const game_input::MappingResult result = resource->Open(data, size);
        if (result == game_input::MappingResult::Success)
            return resource;
        resource->~MappingResource();
        vanguard::memory::Free(block);
        failure = ConvertMappingFailure(result);
        return nullptr;
    }

    void DestroyMapping(vanguard::resources::ResourceObject* const object, void*) noexcept
    {
        if (object == nullptr)
            return;
        static_cast<game_input::MappingResource*>(object)->~MappingResource();
        vanguard::memory::MemoryBlock block{object, sizeof(game_input::MappingResource), vanguard::memory::PoolId::Input};
        vanguard::memory::Free(block);
    }

    class ManagedGameInputService final : public engine::GameInputService
    {
    public:
        [[nodiscard]] game_input::ActionMap& GetMappings() noexcept override
        {
            return m_mappings;
        }
        [[nodiscard]] const game_input::ActionMap& GetMappings() const noexcept override
        {
            return m_mappings;
        }
        [[nodiscard]] game_input::MappingResult Install(const game_input::MappingFile& mapping) noexcept override
        {
            return mapping.IsOpen() ? mapping.Install(m_mappings, true) : game_input::MappingResult::InvalidState;
        }

    protected:
        app::LifecycleStatus OnInitialize(app::ServiceContext& context) noexcept override
        {
            m_input = engine::FindInputService(context);
            m_framePipeline = engine::FindFramePipelineService(context);
            m_streaming = engine::FindResourceStreamingService(context);
            if (m_input == nullptr || m_framePipeline == nullptr || m_streaming == nullptr)
                return app::LifecycleStatus::Failure("Game input service dependencies are unavailable");
            if (!m_streaming->GetStreamer().RegisterDecoder({game_input::MappingResourceType, "Vanguard input mapping", DecodeMapping, DestroyMapping, nullptr}))
                return app::LifecycleStatus::Failure("Game input mapping decoder registration failed");
            m_decoderRegistered = true;

            constexpr engine::FrameParticipantId dependencies[]{engine::InputFrameParticipantId};
            engine::FrameParticipantDescriptor descriptor;
            descriptor.id = engine::GameInputFrameParticipantId;
            descriptor.name = "gameInput";
            descriptor.phase = engine::FramePhase::Input;
            descriptor.profiles =
                app::ApplicationProfile::Runtime | app::ApplicationProfile::Editor | app::ApplicationProfile::Tool | app::ApplicationProfile::Test;
            descriptor.affinity = app::ThreadAffinity::MainThread;
            descriptor.after = {dependencies, 1};
            descriptor.execute = ExecuteFrame;
            descriptor.userData = this;
            engine::FrameFailure failure;
            if (!m_framePipeline->RegisterParticipant(descriptor, &failure))
            {
                static_cast<void>(m_streaming->GetStreamer().UnregisterDecoder(game_input::MappingResourceType));
                m_decoderRegistered = false;
                return app::LifecycleStatus::Failure(failure.message != nullptr ? failure.message : "Game input frame registration failed");
            }
            return app::LifecycleStatus::Success();
        }

        app::LifecycleStatus OnShutdown(app::ServiceContext&) noexcept override
        {
            if (m_decoderRegistered && !m_streaming->GetStreamer().UnregisterDecoder(game_input::MappingResourceType))
                return app::LifecycleStatus::Failure("Game input mapping decoder unregistration failed");
            m_decoderRegistered = false;
            m_input = nullptr;
            m_framePipeline = nullptr;
            m_streaming = nullptr;
            return app::LifecycleStatus::Success();
        }

    private:
        static engine::FrameParticipantStatus ExecuteFrame(const engine::FrameContext& context, void* const userData) noexcept
        {
            auto* const service = static_cast<ManagedGameInputService*>(userData);
            if (service == nullptr || service->m_input == nullptr)
                return engine::FrameParticipantStatus::Failure("Game input service is unavailable");
            if (!service->m_mappings.GetStats().compiled && service->m_mappings.Compile() != game_input::Result::Success)
                return engine::FrameParticipantStatus::Failure("Game input mapping compilation failed");
            if (service->m_mappings.Update(service->m_input->GetSnapshot(), service->m_input->GetEvents(), context.realDeltaSeconds) != game_input::Result::Success)
                return engine::FrameParticipantStatus::Failure("Game input mapping update failed");
            return engine::FrameParticipantStatus::Success();
        }

        game_input::ActionMap m_mappings;
        engine::InputService* m_input = nullptr;
        engine::FramePipelineService* m_framePipeline = nullptr;
        engine::ResourceStreamingService* m_streaming = nullptr;
        bool m_decoderRegistered = false;
    };

    app::Service* CreateGameInputService(void*) noexcept
    {
        vanguard::memory::MemoryBlock block =
            vanguard::memory::Allocate(vanguard::memory::PoolId::Input, sizeof(ManagedGameInputService), alignof(ManagedGameInputService));
        return block ? ::new (block.address) ManagedGameInputService() : nullptr;
    }

    void DestroyGameInputService(app::Service* const service, void*) noexcept
    {
        if (service == nullptr)
            return;
        static_cast<ManagedGameInputService*>(service)->~ManagedGameInputService();
        vanguard::memory::MemoryBlock block{service, sizeof(ManagedGameInputService), vanguard::memory::PoolId::Input};
        vanguard::memory::Free(block);
    }
} // namespace

namespace vanguard::engine
{
    bool RegisterGameInputService(application::EngineHost& host, application::HostFailure* const failure) noexcept
    {
        constexpr application::ServiceDependency dependencies[]{{FramePipelineServiceId, application::DependencyKind::Required},
                                                                {InputServiceId, application::DependencyKind::Required},
                                                                {ResourceStreamingServiceId, application::DependencyKind::Required}};
        constexpr application::CapabilityId capabilities[]{GameInputCapabilityId};
        application::ServiceDescriptor descriptor;
        descriptor.id = GameInputServiceId;
        descriptor.name = "gameInput";
        descriptor.profiles = application::ApplicationProfile::Runtime | application::ApplicationProfile::Editor | application::ApplicationProfile::Tool |
                              application::ApplicationProfile::Test;
        descriptor.scope = application::ServiceScope::Engine;
        descriptor.affinity = application::ThreadAffinity::MainThread;
        descriptor.dependencies = {dependencies, 3};
        descriptor.provides = {capabilities, 1};
        descriptor.create = CreateGameInputService;
        descriptor.destroy = DestroyGameInputService;
        return host.RegisterService(EngineModuleId, descriptor, failure);
    }

    GameInputService* FindGameInputService(application::EngineHost& host) noexcept
    {
        return static_cast<GameInputService*>(host.FindCapability(GameInputCapabilityId));
    }
    GameInputService* FindGameInputService(application::ServiceContext& context) noexcept
    {
        return static_cast<GameInputService*>(context.FindCapability(GameInputCapabilityId));
    }
} // namespace vanguard::engine
