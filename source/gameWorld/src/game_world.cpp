#include <vanguard/game_world/game_world.hpp>

#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <algorithm>
#include <cmath>
#include <new>

namespace
{
    using namespace vanguard;
    namespace game = vanguard::game;

    template<typename Type, typename... Args>
    [[nodiscard]] Type* AllocateGameObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Gameplay, sizeof(Type), alignof(Type));
        if (!block) return nullptr;
        return ::new (block.address) Type(static_cast<Args&&>(args)...);
    }

    template<typename Type>
    void DeleteGameObject(Type* const object) noexcept
    {
        if (object == nullptr) return;
        object->~Type();
        memory::MemoryBlock block{object, sizeof(Type), memory::PoolId::Gameplay};
        memory::Free(block);
    }

    [[nodiscard]] constexpr game::RuntimeSystemFlags ModeFlag(const game::WorldMode mode) noexcept
    {
        switch (mode)
        {
        case game::WorldMode::Game: return game::RuntimeSystemFlags::Game;
        case game::WorldMode::Preview: return game::RuntimeSystemFlags::Preview;
        case game::WorldMode::Thumbnail: return game::RuntimeSystemFlags::Thumbnail;
        case game::WorldMode::Headless: return game::RuntimeSystemFlags::Headless;
        }
        return game::RuntimeSystemFlags::None;
    }

    [[nodiscard]] constexpr bool HasFlag(const game::RuntimeSystemFlags value,
                                         const game::RuntimeSystemFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }
} // namespace

namespace vanguard::game
{
    RuntimeSystem::RuntimeSystem(const RuntimeSystemDescriptor& descriptor) noexcept : m_descriptor(descriptor) {}
    const RuntimeSystemDescriptor& RuntimeSystem::Descriptor() const noexcept { return m_descriptor; }
    bool RuntimeSystem::IsInitialized() const noexcept { return m_initialized; }
    bool RuntimeSystem::IsGameAttached() const noexcept { return m_gameAttached; }
    bool RuntimeSystem::OnInitialize(GameWorld&) noexcept { return true; }
    bool RuntimeSystem::OnSetup(GameWorld&) noexcept { return true; }
    void RuntimeSystem::OnUninitialize(GameWorld&) noexcept {}
    void RuntimeSystem::OnGameAttached(void*) noexcept {}
    void RuntimeSystem::OnGameDetached(void*) noexcept {}
    void RuntimeSystem::OnPostWorldDetached(GameWorld&) noexcept {}
    void RuntimeSystem::OnBeginFrame(GameWorld&, const f32) noexcept {}
    void RuntimeSystem::OnEndFrame(GameWorld&, const f32) noexcept {}
    void RuntimeSystem::OnAfterWorldFlush(GameWorld&) noexcept {}
    void RuntimeSystem::OnGainFocus(GameWorld&) noexcept {}
    void RuntimeSystem::OnLoseFocus(GameWorld&) noexcept {}
    const char* RuntimeSystem::ReadinessBlocker() const noexcept { return nullptr; }

    struct GameWorld::Impl
    {
        struct SystemEntry
        {
            RuntimeSystem* system = nullptr;
            bool active = false;
        };

        Impl() noexcept : systems(memory::pools::Gameplay::GetInstance()) { systems.Reserve(MaximumRuntimeSystems); }

        GameWorldConfig config;
        ecs::World entities;
        containers::DynamicArray<SystemEntry> systems;
        void* gameInstance = nullptr;
        u64 frames = 0;
        bool initialized = false;
        bool focused = false;
        bool tickEnabled = true;
    };

    GameWorld::~GameWorld()
    {
        if (m_impl != nullptr)
        {
            for (u32 index = m_impl->systems.Size(); index > 0; --index)
            {
                RuntimeSystem* const system = m_impl->systems[index - 1u].system;
                if (system == nullptr || !system->m_initialized) continue;
                if (system->m_gameAttached)
                {
                    system->OnGameDetached(m_impl->gameInstance);
                    system->m_gameAttached = false;
                }
                if (system->m_needsPostWorldDetached)
                {
                    system->OnPostWorldDetached(*this);
                    system->m_needsPostWorldDetached = false;
                }
                system->OnUninitialize(*this);
                system->m_initialized = false;
            }
            DeleteGameObject(m_impl);
            m_impl = nullptr;
        }
    }

    bool GameWorld::RegisterSystem(RuntimeSystem& system) noexcept
    {
        if (system.Descriptor().id == InvalidRuntimeSystemId || system.Descriptor().name == nullptr ||
            system.Descriptor().name[0] == '\0' || system.IsInitialized()) return false;
        if (m_impl == nullptr)
        {
            m_impl = AllocateGameObject<Impl>();
            if (m_impl == nullptr) return false;
        }
        if (m_impl->initialized || m_impl->systems.Size() >= MaximumRuntimeSystems) return false;
        for (const Impl::SystemEntry& entry : m_impl->systems)
            if (entry.system == &system || entry.system->Descriptor().id == system.Descriptor().id) return false;
        const u32 expected = m_impl->systems.Size() + 1u;
        m_impl->systems.PushBack({&system, false});
        return m_impl->systems.Size() == expected;
    }

    bool GameWorld::Initialize(const GameWorldConfig& config) noexcept
    {
        if (m_impl == nullptr)
        {
            m_impl = AllocateGameObject<Impl>();
            if (m_impl == nullptr) return false;
        }
        if (m_impl->initialized || !m_impl->entities.Initialize(config.entities)) return false;
        m_impl->config = config;
        std::sort(m_impl->systems.Begin(), m_impl->systems.End(), [](const Impl::SystemEntry& left,
                                                                    const Impl::SystemEntry& right) noexcept
        {
            return left.system->Descriptor().id < right.system->Descriptor().id;
        });
        const RuntimeSystemFlags mode = ModeFlag(config.mode);
        for (u32 systemIndex = 0; systemIndex < m_impl->systems.Size(); ++systemIndex)
        {
            Impl::SystemEntry& entry = m_impl->systems[systemIndex];
            entry.active = HasFlag(entry.system->Descriptor().modes, mode);
            if (!entry.active) continue;
            if (!entry.system->OnInitialize(*this))
            {
                for (u32 rollback = systemIndex; rollback > 0; --rollback)
                {
                    RuntimeSystem* const previous = m_impl->systems[rollback - 1u].system;
                    if (previous->m_initialized) { previous->OnUninitialize(*this); previous->m_initialized = false; }
                }
                for (Impl::SystemEntry& rollbackEntry : m_impl->systems) rollbackEntry.active = false;
                static_cast<void>(m_impl->entities.Shutdown());
                return false;
            }
            entry.system->m_initialized = true;
        }
        for (Impl::SystemEntry& entry : m_impl->systems)
        {
            if (!entry.active || entry.system->OnSetup(*this)) continue;
            for (u32 rollback = m_impl->systems.Size(); rollback > 0; --rollback)
            {
                RuntimeSystem* const previous = m_impl->systems[rollback - 1u].system;
                if (previous->m_initialized) { previous->OnUninitialize(*this); previous->m_initialized = false; }
            }
            for (Impl::SystemEntry& rollbackEntry : m_impl->systems) rollbackEntry.active = false;
            static_cast<void>(m_impl->entities.Shutdown());
            return false;
        }
        m_impl->initialized = true;
        return true;
    }

    bool GameWorld::Shutdown() noexcept
    {
        if (m_impl == nullptr) return true;
        if (!m_impl->initialized) { DeleteGameObject(m_impl); m_impl = nullptr; return true; }
        if (m_impl->gameInstance != nullptr || m_impl->focused) return false;
        for (const Impl::SystemEntry& entry : m_impl->systems)
            if (entry.system->m_needsPostWorldDetached) return false;
        ecs::ActionReport actions;
        ecs::ComponentActionReport componentActions;
        if (!m_impl->entities.FlushActions(&actions) ||
            !m_impl->entities.FlushComponentActions(&componentActions)) return false;
        for (Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active) entry.system->OnAfterWorldFlush(*this);
        if (m_impl->entities.GetStats().entities != 0) return false;
        for (u32 index = m_impl->systems.Size(); index > 0; --index)
        {
            RuntimeSystem* const system = m_impl->systems[index - 1u].system;
            if (!system->m_initialized) continue;
            system->OnUninitialize(*this);
            system->m_initialized = false;
        }
        if (!m_impl->entities.Shutdown()) return false;
        DeleteGameObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool GameWorld::IsInitialized() const noexcept { return m_impl != nullptr && m_impl->initialized; }

    bool GameWorld::AttachGame(void* const gameInstance) noexcept
    {
        if (!IsInitialized() || gameInstance == nullptr || m_impl->gameInstance != nullptr) return false;
        m_impl->gameInstance = gameInstance;
        for (Impl::SystemEntry& entry : m_impl->systems)
        {
            if (!entry.active) continue;
            entry.system->OnGameAttached(gameInstance);
            entry.system->m_gameAttached = true;
            entry.system->m_needsPostWorldDetached = true;
        }
        return true;
    }

    bool GameWorld::DetachGame(void* const gameInstance) noexcept
    {
        if (!IsInitialized() || gameInstance == nullptr || m_impl->gameInstance != gameInstance) return false;
        for (u32 index = m_impl->systems.Size(); index > 0; --index)
        {
            Impl::SystemEntry& entry = m_impl->systems[index - 1u];
            if (!entry.active) continue;
            entry.system->OnGameDetached(gameInstance);
            entry.system->m_gameAttached = false;
        }
        m_impl->gameInstance = nullptr;
        return true;
    }

    bool GameWorld::PostWorldDetached() noexcept
    {
        if (!IsInitialized() || m_impl->gameInstance != nullptr) return false;
        for (u32 index = m_impl->systems.Size(); index > 0; --index)
        {
            RuntimeSystem* const system = m_impl->systems[index - 1u].system;
            if (!system->m_needsPostWorldDetached) continue;
            system->OnPostWorldDetached(*this);
            system->m_needsPostWorldDetached = false;
        }
        return true;
    }

    bool GameWorld::Tick(const f32 deltaSeconds) noexcept
    {
        if (!IsInitialized() || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) return false;
        if (!m_impl->entities.FlushActions() || !m_impl->entities.FlushComponentActions()) return false;
        for (Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active) entry.system->OnBeginFrame(*this, deltaSeconds);
        if (m_impl->tickEnabled && !m_impl->entities.Progress(deltaSeconds)) return false;
        for (Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active) entry.system->OnEndFrame(*this, deltaSeconds);
        if (!m_impl->entities.FlushActions() || !m_impl->entities.FlushComponentActions()) return false;
        for (Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active) entry.system->OnAfterWorldFlush(*this);
        ++m_impl->frames;
        return true;
    }

    void GameWorld::SetTickEnabled(const bool enabled) noexcept { if (m_impl != nullptr) m_impl->tickEnabled = enabled; }

    void GameWorld::GainFocus() noexcept
    {
        if (!IsInitialized() || m_impl->focused) return;
        m_impl->focused = true;
        for (Impl::SystemEntry& entry : m_impl->systems) if (entry.active) entry.system->OnGainFocus(*this);
    }

    void GameWorld::LoseFocus() noexcept
    {
        if (!IsInitialized() || !m_impl->focused) return;
        for (Impl::SystemEntry& entry : m_impl->systems) if (entry.active) entry.system->OnLoseFocus(*this);
        m_impl->focused = false;
    }

    RuntimeSystem* GameWorld::System(const RuntimeSystemId id) noexcept
    {
        if (m_impl == nullptr) return nullptr;
        for (Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active && entry.system->Descriptor().id == id) return entry.system;
        return nullptr;
    }

    const RuntimeSystem* GameWorld::System(const RuntimeSystemId id) const noexcept
    {
        return const_cast<GameWorld*>(this)->System(id);
    }

    const char* GameWorld::ReadinessBlocker() const noexcept
    {
        if (!IsInitialized()) return "game world is not initialized";
        for (const Impl::SystemEntry& entry : m_impl->systems)
            if (entry.active) if (const char* const blocker = entry.system->ReadinessBlocker()) return blocker;
        return nullptr;
    }

    WorldMode GameWorld::Mode() const noexcept { return m_impl != nullptr ? m_impl->config.mode : WorldMode::Game; }
    ecs::World& GameWorld::Entities() noexcept { return m_impl->entities; }
    const ecs::World& GameWorld::Entities() const noexcept { return m_impl->entities; }

    GameWorldStats GameWorld::GetStats() const noexcept
    {
        GameWorldStats stats;
        if (m_impl == nullptr) return stats;
        stats.entities = m_impl->entities.GetStats();
        stats.registeredSystems = m_impl->systems.Size();
        for (const Impl::SystemEntry& entry : m_impl->systems) stats.activeSystems += entry.active;
        stats.frames = m_impl->frames;
        stats.initialized = m_impl->initialized;
        stats.gameAttached = m_impl->gameInstance != nullptr;
        stats.focused = m_impl->focused;
        stats.tickEnabled = m_impl->tickEnabled;
        return stats;
    }
} // namespace vanguard::game
