#pragma once

#include <vanguard/application/engine_host.hpp>

namespace vanguard::input { class IInputBackend; }
namespace vanguard::application { class IPlatformHost; }
namespace vanguard::filesystem { struct Config; }

namespace vanguard::engine
{
    inline constexpr application::ModuleId EngineModuleId = 0x656e67696e650001ull;
    inline constexpr application::ServiceId IoServiceId = 0x696f000000000001ull;
    inline constexpr application::ServiceId FilesystemServiceId = 0x66696c6573797301ull;
    inline constexpr application::ServiceId JobsServiceId = 0x6a6f627300000001ull;
    inline constexpr application::ServiceId FramePipelineServiceId = 0x6672616d65737601ull;
    inline constexpr application::ServiceId ReflectionServiceId = 0x7265666c65637401ull;
    inline constexpr application::ServiceId InputServiceId = 0x696e707574737601ull;
    inline constexpr application::ServiceId WindowServiceId = 0x77696e646f777301ull;
    inline constexpr application::ServiceId GameInputServiceId = 0x67616d65696e7301ull;
    inline constexpr application::ServiceId ResourcesServiceId = 0x7265736f75726301ull;
    inline constexpr application::ServiceId ResourceStreamingServiceId = 0x7273747265616d01ull;
    inline constexpr application::ServiceId WorldServiceId = 0x776f726c64737601ull;
    inline constexpr application::ServiceId GameWorldServiceId = 0x67616d65776f7201ull;
    inline constexpr application::ServiceId StreamingObserverServiceId = 0x7374726f62737601ull;
    inline constexpr application::ServiceId WorldSessionServiceId = 0x7773657373696f01ull;
    inline constexpr application::ServiceId RenderSceneServiceId = 0x727363656e657301ull;
    inline constexpr application::CapabilityId InputCapabilityId = 0x696e707574636101ull;
    inline constexpr application::CapabilityId WindowCapabilityId = 0x77696e646f776301ull;
    inline constexpr application::CapabilityId GameInputCapabilityId = 0x67616d65696e6301ull;
    inline constexpr application::CapabilityId IoCapabilityId = 0x696f737973746d01ull;
    inline constexpr application::CapabilityId FilesystemCapabilityId = 0x66696c6573797302ull;
    inline constexpr application::CapabilityId JobSchedulerCapabilityId = 0x6a6f627363686401ull;
    inline constexpr application::CapabilityId FramePipelineCapabilityId = 0x6672616d65737611ull;
    inline constexpr application::CapabilityId ReflectionCapabilityId = 0x7265666c65637411ull;
    inline constexpr application::CapabilityId ResourceRegistryCapabilityId = 0x7265737265676973ull;
    inline constexpr application::CapabilityId ResourcePipelineCapabilityId = 0x726573706970656cull;
    inline constexpr application::CapabilityId ResourceStreamingCapabilityId = 0x7273747265616d02ull;
    inline constexpr application::CapabilityId WorldCapabilityId = 0x776f726c64737611ull;
    inline constexpr application::CapabilityId GameWorldCapabilityId = 0x67616d65776f7211ull;
    inline constexpr application::CapabilityId StreamingObserverCapabilityId = 0x7374726f62737611ull;
    inline constexpr application::CapabilityId WorldSessionCapabilityId = 0x7773657373696f11ull;
    inline constexpr application::CapabilityId RenderSceneCapabilityId = 0x727363656e656311ull;

    [[nodiscard]] bool RegisterEngineModule(application::EngineHost& host,
                                            application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterIoService(application::EngineHost& host,
                                         application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterFilesystemService(application::EngineHost& host,
                                                 application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterFilesystemService(application::EngineHost& host,
                                                 const filesystem::Config& config,
                                                 application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterJobsService(application::EngineHost& host,
                                           application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterFramePipelineService(application::EngineHost& host,
                                                    application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterReflectionService(application::EngineHost& host,
                                                 application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterWindowService(application::EngineHost& host, application::IPlatformHost* platform,
                                             application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterInputService(application::EngineHost& host, input::IInputBackend* backend,
                                            application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterGameInputService(application::EngineHost& host,
                                                application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterResourcesService(application::EngineHost& host,
                                                application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterResourceStreamingService(application::EngineHost& host,
                                                        application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterWorldService(application::EngineHost& host,
                                            application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterGameWorldService(application::EngineHost& host,
                                                application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterStreamingObserverService(application::EngineHost& host,
                                                        application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterWorldSessionService(application::EngineHost& host,
                                                   application::HostFailure* failure = nullptr) noexcept;
    [[nodiscard]] bool RegisterRenderSceneService(application::EngineHost& host,
                                                  application::HostFailure* failure = nullptr) noexcept;
} // namespace vanguard::engine
