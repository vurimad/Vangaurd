#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/ecs/native.hpp>
#include <vanguard/game_world/game_world.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstdio>

namespace
{
    int g_failures = 0;

    struct WorldPosition
    {
        float x = 0.0f;
    };

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[gameWorldTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    class RecordingSystem final : public vanguard::game::RuntimeSystem
    {
    public:
        RecordingSystem(const vanguard::game::RuntimeSystemDescriptor& descriptor, vanguard::containers::DynamicArray<vanguard::u32>& events) noexcept
            : RuntimeSystem(descriptor), m_events(events)
        {
        }

        bool createEntityOnFirstFrame = false;
        const char* blocker = nullptr;

    protected:
        bool OnInitialize(vanguard::game::GameWorld&) noexcept override
        {
            Record(100);
            return true;
        }
        bool OnSetup(vanguard::game::GameWorld&) noexcept override
        {
            Record(200);
            return true;
        }
        void OnUninitialize(vanguard::game::GameWorld&) noexcept override
        {
            Record(1000);
        }
        void OnGameAttached(void*) noexcept override
        {
            Record(300);
        }
        void OnGameDetached(void*) noexcept override
        {
            Record(800);
        }
        void OnPostWorldDetached(vanguard::game::GameWorld&) noexcept override
        {
            Record(900);
        }
        void OnGainFocus(vanguard::game::GameWorld&) noexcept override
        {
            Record(400);
        }
        void OnLoseFocus(vanguard::game::GameWorld&) noexcept override
        {
            Record(700);
        }

        void OnBeginFrame(vanguard::game::GameWorld& world, vanguard::f32) noexcept override
        {
            Record(500);
            if (createEntityOnFirstFrame)
            {
                static_cast<void>(world.GetEntities().QueueCreate(0xabc));
                createEntityOnFirstFrame = false;
            }
        }

        void OnEndFrame(vanguard::game::GameWorld&, vanguard::f32) noexcept override
        {
            Record(600);
        }
        const char* ReadinessBlocker() const noexcept override
        {
            return blocker;
        }

    private:
        void Record(const vanguard::u32 phase) noexcept
        {
            m_events.PushBack(phase + GetDescriptor().id);
        }

        vanguard::containers::DynamicArray<vanguard::u32>& m_events;
    };

    class RetrySystem final : public vanguard::game::RuntimeSystem
    {
    public:
        RetrySystem() noexcept : RuntimeSystem({1, "retry", vanguard::game::RuntimeSystemFlags::Game}) {}

        bool rejectSetup = true;
        vanguard::u32 initializeCalls = 0;
        vanguard::u32 uninitializeCalls = 0;

    protected:
        bool OnInitialize(vanguard::game::GameWorld&) noexcept override
        {
            ++initializeCalls;
            return true;
        }
        bool OnSetup(vanguard::game::GameWorld&) noexcept override
        {
            return !rejectSetup;
        }
        void OnUninitialize(vanguard::game::GameWorld&) noexcept override
        {
            ++uninitializeCalls;
        }
    };
} // namespace

int main()
{
    namespace game = vanguard::game;
    namespace memory = vanguard::memory;

    Check(memory::Initialize(), "memory initializes");
    Check(vanguard::diagnostics::Initialize(vanguard::diagnostics::Mode::Synchronous, "gameWorldTests"), "diagnostics initialize");
    Check(vanguard::containers::Initialize(), "containers initialize");

    vanguard::containers::DynamicArray<vanguard::u32> events(memory::pools::Gameplay::GetInstance());
    RecordingSystem late({20, "late", game::RuntimeSystemFlags::Game}, events);
    RecordingSystem early({10, "early", game::RuntimeSystemFlags::Game}, events);
    RecordingSystem previewOnly({5, "preview", game::RuntimeSystemFlags::Preview}, events);
    early.createEntityOnFirstFrame = true;

    game::GameWorld world;
    Check(world.RegisterSystem(late) && world.RegisterSystem(previewOnly) && world.RegisterSystem(early),
          "register caller-owned runtime systems before initialization");
    Check(!world.RegisterSystem(early), "reject duplicate runtime system registration");
    Check(world.Initialize(), "initialize game world and active runtime systems");
    Check(events.Size() == 4 && events[0] == 110 && events[1] == 120 && events[2] == 210 && events[3] == 220 && !previewOnly.IsInitialized(),
          "systems initialize and setup in stable ID order with mode filtering");

    int gameInstance = 1;
    Check(world.AttachGame(&gameInstance), "attach opaque game instance");
    world.GainFocus();
    Check(world.Tick(1.0f / 60.0f) && world.GetEntities().Resolve(0xabc), "frame boundary commits entity actions queued by runtime systems");
    Check(world.GetStats().activeSystems == 2 && world.GetStats().frames == 1 && world.ReadinessBlocker() == nullptr,
          "game-world statistics and readiness aggregate active systems");
    late.blocker = "late system is loading";
    Check(world.ReadinessBlocker() == late.blocker, "first active system blocker gates world readiness");
    late.blocker = nullptr;

    const vanguard::ecs::ComponentType<WorldPosition> positionType = vanguard::ecs::RegisterComponent<WorldPosition>(world.GetEntities());
    Check(positionType && world.GetEntities().QueueAddComponent(0xabc, positionType) &&
              world.GetEntities().QueueSetComponent(0xabc, positionType, WorldPosition{42.0f}) && world.Tick(0.0f),
          "GameWorld commits queued component transactions at its frame synchronization point");
    {
        flecs::world native = vanguard::ecs::GetNative(world.GetEntities());
        const WorldPosition* const position = native.entity(world.GetEntities().Resolve(0xabc).value).try_get<WorldPosition>();
        Check(position != nullptr && position->x == 42.0f, "component transaction value is visible to the live Flecs world");
    }

    world.LoseFocus();
    Check(world.DetachGame(&gameInstance), "detach game in reverse system order");
    Check(!world.Shutdown(), "shutdown refuses until post-world-detach and entities complete");
    Check(world.PostWorldDetached(), "run post-world-detach in reverse system order");
    Check(world.GetEntities().QueueDestroy(0xabc) && world.Tick(0.0f), "destroy final entity through world sync point");
    Check(world.Shutdown(), "uninitialize systems in reverse order and shutdown empty ECS world");

    const vanguard::u32 expected[] = {110, 120, 210, 220, 310, 320, 410, 420, 510, 520, 610, 620, 510,  520,
                                      610, 620, 710, 720, 820, 810, 920, 910, 510, 520, 610, 620, 1020, 1010};
    constexpr vanguard::u32 expectedCount = sizeof(expected) / sizeof(expected[0]);
    Check(events.Size() == expectedCount, "record complete runtime-world lifecycle");
    for (vanguard::u32 index = 0; index < events.Size() && index < expectedCount; ++index)
        Check(events[index] == expected[index], "runtime-world lifecycle ordering matches contract");

    RetrySystem retrySystem;
    game::GameWorld retryWorld;
    Check(retryWorld.RegisterSystem(retrySystem) && !retryWorld.Initialize() && !retryWorld.IsInitialized() && !retrySystem.IsInitialized() &&
              retrySystem.initializeCalls == 1 && retrySystem.uninitializeCalls == 1,
          "failed setup rolls back both runtime systems and the Flecs world");
    retrySystem.rejectSetup = false;
    Check(retryWorld.Initialize() && retryWorld.Shutdown() && retrySystem.initializeCalls == 2 && retrySystem.uninitializeCalls == 2,
          "a rolled-back game world can initialize successfully on retry");

    vanguard::diagnostics::Shutdown();
    std::printf("gameWorldTests: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
