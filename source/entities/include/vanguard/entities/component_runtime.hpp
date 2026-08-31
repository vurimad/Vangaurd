#pragma once

#include <vanguard/entities/component_directory.hpp>
#include <vanguard/game_world/game_world.hpp>
#include <vanguard/containers/containers.hpp>

namespace vanguard::entities
{
    inline constexpr game::RuntimeSystemId ComponentRuntimeSystemId = 32;

    /// World lifecycle owner for stable runtime component instances. This is intentionally
    /// a RuntimeSystem rather than an engine service: every GameWorld owns exactly one directory.
    class ComponentRuntime final : public game::RuntimeSystem
    {
    public:
        explicit ComponentRuntime(const ComponentDirectoryConfig& config = {}) noexcept;

        [[nodiscard]] ComponentDirectory& GetDirectory() noexcept;
        [[nodiscard]] const ComponentDirectory& GetDirectory() const noexcept;

    protected:
        [[nodiscard]] bool OnInitialize(game::GameWorld& world) noexcept override;
        void OnUninitialize(game::GameWorld& world) noexcept override;
        void OnAfterWorldFlush(game::GameWorld& world) noexcept override;
        [[nodiscard]] const char* ReadinessBlocker() const noexcept override;

    private:
        ComponentDirectoryConfig m_config;
        ComponentDirectory m_directory;
        ecs::CommittedChangeCursor m_changeCursor;
        containers::DynamicArray<ecs::CommittedChange> m_changes;
        ecs::ComponentId m_disabledEntityComponent = ecs::InvalidComponentId;
        const char* m_readinessBlocker = nullptr;
    };
} // namespace vanguard::entities
