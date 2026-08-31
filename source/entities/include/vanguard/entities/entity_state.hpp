#pragma once

namespace vanguard::entities
{
    /// Persistent ECS authority for whether an entity participates in runtime systems.
    /// ComponentRuntime mirrors committed changes to Flecs entity state and stable components.
    struct DisabledEntity
    {
    };
} // namespace vanguard::entities
