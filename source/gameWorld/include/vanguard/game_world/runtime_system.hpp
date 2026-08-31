#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::game
{
    class GameWorld;

    using RuntimeSystemId = u16;
    inline constexpr RuntimeSystemId InvalidRuntimeSystemId = 0xffffu;
    inline constexpr u32 MaximumRuntimeSystems = 256;

    enum class RuntimeSystemFlags : u8
    {
        None = 0,
        Game = 1u << 0u,
        Preview = 1u << 1u,
        Thumbnail = 1u << 2u,
        Headless = 1u << 3u,
        All = Game | Preview | Thumbnail | Headless
    };

    struct RuntimeSystemDescriptor
    {
        RuntimeSystemId id = InvalidRuntimeSystemId;
        const char* name = nullptr;
        RuntimeSystemFlags modes = RuntimeSystemFlags::All;
    };

    class RuntimeSystem
    {
    public:
        explicit RuntimeSystem(const RuntimeSystemDescriptor& descriptor) noexcept;
        virtual ~RuntimeSystem() = default;

        RuntimeSystem(const RuntimeSystem&) = delete;
        RuntimeSystem& operator=(const RuntimeSystem&) = delete;

        [[nodiscard]] const RuntimeSystemDescriptor& GetDescriptor() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsGameAttached() const noexcept;

    protected:
        virtual bool OnInitialize(GameWorld& world) noexcept;
        virtual bool OnSetup(GameWorld& world) noexcept;
        virtual void OnUninitialize(GameWorld& world) noexcept;
        virtual void OnGameAttached(void* gameInstance) noexcept;
        virtual void OnGameDetached(void* gameInstance) noexcept;
        virtual void OnPostWorldDetached(GameWorld& world) noexcept;
        virtual void OnBeginFrame(GameWorld& world, f32 deltaSeconds) noexcept;
        virtual void OnEndFrame(GameWorld& world, f32 deltaSeconds) noexcept;
        /// Runs after the end-of-frame entity and component queues have committed.
        virtual void OnAfterWorldFlush(GameWorld& world) noexcept;
        virtual void OnGainFocus(GameWorld& world) noexcept;
        virtual void OnLoseFocus(GameWorld& world) noexcept;
        [[nodiscard]] virtual const char* ReadinessBlocker() const noexcept;

    private:
        RuntimeSystemDescriptor m_descriptor;
        bool m_initialized = false;
        bool m_gameAttached = false;
        bool m_needsPostWorldDetached = false;

        friend class GameWorld;
    };
} // namespace vanguard::game
