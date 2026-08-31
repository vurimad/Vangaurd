#pragma once

#include <vanguard/ecs/ecs.hpp>

#include <flecs.h>

namespace vanguard::ecs
{
    [[nodiscard]] inline flecs::world GetNative(World& world) noexcept
    {
        return flecs::world(world.GetNative());
    }

    /// Registers a C++ component type during serialized world setup and returns a token scoped to
    /// that world. Runtime producers retain the token and queue structural changes without touching Flecs.
    template <typename Component> [[nodiscard]] ComponentType<Component> RegisterComponent(World& world, const bool toggleable = false) noexcept
    {
        if (!world.IsInitialized())
            return {};
        flecs::world native = GetNative(world);
        const ecs_entity_t component = native.component<Component>().id();
        if (component == 0)
            return {};
        if (toggleable)
            ecs_add_id(world.GetNative(), component, EcsCanToggle);
        return {component, world.GetNative()};
    }
} // namespace vanguard::ecs
