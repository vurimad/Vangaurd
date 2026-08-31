#pragma once

#include <vanguard/ecs/ecs.hpp>
#include <vanguard/game_world/runtime_system.hpp>

namespace vanguard::game
{
    enum class WorldMode : u8
    {
        Game,
        Preview,
        Thumbnail,
        Headless
    };

    struct GameWorldConfig
    {
        WorldMode mode = WorldMode::Game;
        ecs::WorldConfig entities;
    };

    struct GameWorldStats
    {
        ecs::WorldStats entities;
        u32 registeredSystems = 0;
        u32 activeSystems = 0;
        u64 frames = 0;
        bool initialized = false;
        bool gameAttached = false;
        bool focused = false;
        bool tickEnabled = true;
    };

    /// Runtime world composition root. Systems initialize in ascending ID order and detach,
    /// post-detach and uninitialize in reverse order. System objects remain caller-owned.
    class GameWorld final
    {
    public:
        struct Impl;

        GameWorld() noexcept = default;
        ~GameWorld();

        GameWorld(const GameWorld&) = delete;
        GameWorld& operator=(const GameWorld&) = delete;

        [[nodiscard]] bool RegisterSystem(RuntimeSystem& system) noexcept;
        [[nodiscard]] bool Initialize(const GameWorldConfig& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool AttachGame(void* gameInstance) noexcept;
        [[nodiscard]] bool DetachGame(void* gameInstance) noexcept;
        [[nodiscard]] bool PostWorldDetached() noexcept;
        [[nodiscard]] bool Tick(f32 deltaSeconds) noexcept;

        void SetTickEnabled(bool enabled) noexcept;
        void GainFocus() noexcept;
        void LoseFocus() noexcept;

        [[nodiscard]] RuntimeSystem* GetSystem(RuntimeSystemId id) noexcept;
        [[nodiscard]] const RuntimeSystem* GetSystem(RuntimeSystemId id) const noexcept;
        [[nodiscard]] const char* ReadinessBlocker() const noexcept;
        [[nodiscard]] WorldMode Mode() const noexcept;
        [[nodiscard]] ecs::World& GetEntities() noexcept;
        [[nodiscard]] const ecs::World& GetEntities() const noexcept;
        [[nodiscard]] GameWorldStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::game
