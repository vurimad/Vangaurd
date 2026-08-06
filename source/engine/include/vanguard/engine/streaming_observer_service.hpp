#pragma once

#include <vanguard/engine/frame_pipeline_service.hpp>
#include <vanguard/world/streaming_grid.hpp>

namespace vanguard::engine
{
    inline constexpr FrameParticipantId StreamingObserverFrameParticipantId = 0x7374726f62736672ull;
    inline constexpr u32 MaximumStreamingObserverNameBytes = 64;

    enum class StreamingObserverVelocityClass : u8
    {
        OnFoot,
        GroundVehicle,
        AirVehicle,
        Unbounded
    };

    struct StreamingObserverHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index < world::MaximumStreamingObservers && generation != 0;
        }
        [[nodiscard]] friend constexpr bool operator==(const StreamingObserverHandle&,
                                                       const StreamingObserverHandle&) noexcept = default;
    };

    struct StreamingObserverDescriptor
    {
        const char* name = nullptr;
        StreamingObserverVelocityClass velocityClass = StreamingObserverVelocityClass::OnFoot;
        f32 predictionSeconds = 1.0f;
        bool enabled = true;
    };

    struct StreamingObserverUpdate
    {
        f64 position[3]{};
        f64 velocity[3]{};
        bool positionValid = true;
    };

    struct StreamingObserverPredictionConfig
    {
        f32 maximumOnFootSpeed = 5.0f;
        f32 maximumGroundVehicleSpeed = 20.0f;
        f32 maximumAirVehicleSpeed = 0.0f;
        bool enabled = true;
    };

    struct StreamingObserverSnapshot
    {
        world::StreamingObserver observers[world::MaximumStreamingObservers]{};
        f64 cameraPosition[3]{};
        f32 globalDistanceScale = 1.0f;
        u32 observerCount = 0;
        u64 sequence = 0;
        bool usingWorldOriginFallback = false;
    };

    struct StreamingObserverServiceStats
    {
        u32 registeredObservers = 0;
        u32 validObservers = 0;
        u64 submittedSnapshots = 0;
        u64 rejectedUpdates = 0;
        u64 sequence = 0;
        bool usingWorldOriginFallback = false;
    };

    /// Owns world-streaming observer registrations and publishes one immutable spatial snapshot before Game World ticks.
    /// Producers retain only generational handles; names and observer state are copied into fixed-capacity engine storage.
    class StreamingObserverService : public application::Service
    {
    public:
        ~StreamingObserverService() override = default;

        [[nodiscard]] virtual bool ConfigurePrediction(const StreamingObserverPredictionConfig& config) noexcept = 0;
        [[nodiscard]] virtual bool RegisterObserver(const StreamingObserverDescriptor& descriptor,
                                                    StreamingObserverHandle& observer) noexcept = 0;
        [[nodiscard]] virtual bool UnregisterObserver(StreamingObserverHandle observer) noexcept = 0;
        [[nodiscard]] virtual bool UpdateObserver(StreamingObserverHandle observer,
                                                  const StreamingObserverUpdate& update) noexcept = 0;
        [[nodiscard]] virtual bool SetObserverEnabled(StreamingObserverHandle observer, bool enabled) noexcept = 0;
        [[nodiscard]] virtual bool SetPrimaryObserver(StreamingObserverHandle observer) noexcept = 0;
        virtual void ClearPrimaryObserver() noexcept = 0;
        [[nodiscard]] virtual bool SetGlobalDistanceScale(f32 scale) noexcept = 0;

        [[nodiscard]] virtual bool Snapshot(StreamingObserverSnapshot& snapshot) const noexcept = 0;
        [[nodiscard]] virtual StreamingObserverServiceStats GetStats() const noexcept = 0;

    protected:
        StreamingObserverService() noexcept = default;
    };

    [[nodiscard]] StreamingObserverService* FindStreamingObserverService(application::EngineHost& host) noexcept;
    [[nodiscard]] StreamingObserverService* FindStreamingObserverService(application::ServiceContext& context) noexcept;
} // namespace vanguard::engine
