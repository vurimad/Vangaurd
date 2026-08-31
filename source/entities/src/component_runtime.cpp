#include <vanguard/entities/component_runtime.hpp>
#include <vanguard/entities/entity_state.hpp>

#include <vanguard/ecs/native.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::entities
{
    ComponentRuntime::ComponentRuntime(const ComponentDirectoryConfig& config) noexcept
        : RuntimeSystem({ComponentRuntimeSystemId, "ComponentRuntime", game::RuntimeSystemFlags::All}), m_config(config),
          m_changes(memory::pools::World::GetInstance())
    {
    }

    ComponentDirectory& ComponentRuntime::GetDirectory() noexcept
    {
        return m_directory;
    }

    const ComponentDirectory& ComponentRuntime::GetDirectory() const noexcept
    {
        return m_directory;
    }

    bool ComponentRuntime::OnInitialize(game::GameWorld& world) noexcept
    {
        m_readinessBlocker = nullptr;
        m_changeCursor = {};
        m_changes.Clear();
        const ecs::ComponentType<DisabledEntity> disabledEntity = ecs::RegisterComponent<DisabledEntity>(world.GetEntities());
        if (!disabledEntity)
        {
            m_readinessBlocker = "DisabledEntity ECS component registration failed";
            return false;
        }
        m_disabledEntityComponent = disabledEntity.id;
        ComponentDirectoryFailure failure;
        if (m_directory.Initialize(world, m_config, &failure))
            return true;
        m_readinessBlocker = failure.message != nullptr ? failure.message : "stable component directory initialization failed";
        return false;
    }

    void ComponentRuntime::OnUninitialize(game::GameWorld&) noexcept
    {
        ComponentDirectoryFailure failure;
        if (!m_directory.Shutdown(&failure))
            m_readinessBlocker = failure.message != nullptr ? failure.message : "stable component directory shutdown failed";
        m_changes.Clear();
        m_changeCursor = {};
        m_disabledEntityComponent = ecs::InvalidComponentId;
    }

    void ComponentRuntime::OnAfterWorldFlush(game::GameWorld& world) noexcept
    {
        if (!m_directory.IsInitialized() || m_readinessBlocker != nullptr || m_disabledEntityComponent == ecs::InvalidComponentId)
            return;

        constexpr u32 MaximumChangesPerRead = 4096u;
        for (;;)
        {
            m_changes.Clear();
            ecs::CommittedChangeReadResult read;
            if (!world.GetEntities().ReadCommittedChanges(m_changeCursor, m_changes, MaximumChangesPerRead, &read))
            {
                m_readinessBlocker = "ComponentRuntime failed to read committed ECS changes";
                return;
            }
            if (read.lostRecords != 0)
            {
                m_readinessBlocker = "ComponentRuntime lost committed ECS enabled-state changes";
                return;
            }

            for (const ecs::CommittedChange& change : m_changes)
            {
                if (change.component != m_disabledEntityComponent)
                    continue;
                bool entityEnabled = true;
                if (change.kind == ecs::CommittedChangeKind::ComponentAdded || change.kind == ecs::CommittedChangeKind::ComponentSet ||
                    change.kind == ecs::CommittedChangeKind::ComponentEnabled)
                    entityEnabled = false;
                else if (change.kind != ecs::CommittedChangeKind::ComponentRemoved &&
                         change.kind != ecs::CommittedChangeKind::ComponentDisabled)
                    continue;

                const ecs::Entity runtimeEntity = world.GetEntities().Resolve(change.entity);
                if (!runtimeEntity)
                {
                    if (m_directory.GetEntityComponentCount(change.entity) != 0)
                    {
                        m_readinessBlocker = "disabled-state change lost its live ECS entity before stable components were updated";
                        return;
                    }
                    continue;
                }
                ecs_enable(world.GetEntities().GetNative(), runtimeEntity.value, entityEnabled);
                if (ecs_has_id(world.GetEntities().GetNative(), runtimeEntity.value, EcsDisabled) == entityEnabled)
                {
                    m_readinessBlocker = "Flecs entity enabled-state transition failed";
                    return;
                }
                ComponentDirectoryFailure failure;
                if (!m_directory.SetEntityEnabled(change.entity, entityEnabled, &failure))
                {
                    m_readinessBlocker = failure.message != nullptr ? failure.message : "stable component entity-enabled transition failed";
                    return;
                }
            }
            if (read.records < MaximumChangesPerRead)
                return;
        }
    }

    const char* ComponentRuntime::ReadinessBlocker() const noexcept
    {
        return m_readinessBlocker;
    }
} // namespace vanguard::entities
