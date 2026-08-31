#pragma once

#include <vanguard/entities/materializer.hpp>
#include <vanguard/entities/root_transform_component.hpp>
#include <vanguard/entities/transform_system.hpp>
#include <vanguard/game_world/game_world.hpp>

namespace vanguard::entities
{
    inline constexpr game::RuntimeSystemId TransformRuntimeSystemId = 36;

    /// Runtime-only ECS marker proving that the entity has a stable transform root.
    /// The root itself remains in World-pool sidecar storage and is resolved through
    /// TransformRuntime so Flecs relocation can never invalidate transform pointers.
    struct TransformOwner
    {
        u64 generation = 0;
    };

    struct PlacedComponentBindingHandle
    {
        u32 index = ~u32{0};
        u32 generation = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != ~u32{0} && generation != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(const PlacedComponentBindingHandle&, const PlacedComponentBindingHandle&) noexcept = default;
    };

    struct PlacedComponentBindingSnapshot
    {
        PlacedComponentBindingHandle handle;
        ecs::EntityId entity = ecs::InvalidEntityId;
        u64 rootGeneration = 0;
        IPlacedComponent* component = nullptr;
        bool attached = false;
    };

    struct TransformRuntimeConfig
    {
        TransformSystemConfig transformSystem;
        u32 maximumTrackedEntities = 1u << 20u;
        u32 maximumPlacedComponents = 1u << 20u;
        u32 maximumChangesPerSynchronize = 64u * 1024u;
    };

    enum class TransformRuntimeFailureCode : u8
    {
        None,
        NotInitialized,
        WrongThread,
        InvalidConfiguration,
        CapacityExceeded,
        LostChanges,
        TransformFailure,
        InvalidHandle,
        InvalidState,
        Busy
    };

    struct TransformRuntimeFailure
    {
        TransformRuntimeFailureCode code = TransformRuntimeFailureCode::None;
        TransformFailure transform;
        ecs::EntityId entity = ecs::InvalidEntityId;
        PlacedComponentBindingHandle binding;
        IPlacedComponent* component = nullptr;
        const char* message = nullptr;
    };

    struct TransformRuntimeStats
    {
        u32 trackedEntities = 0;
        u32 placeholders = 0;
        u32 pendingParents = 0;
        u32 placedComponents = 0;
        u64 synchronizedChanges = 0;
        u64 lostChanges = 0;
        bool initialized = false;
        bool failed = false;
        TransformSystemStats transformSystem;
    };

    /// Entity transform runtime system owning stable PlaceholderComponents,
    /// entity-parent and floating-component HardAttachments, and change-journal synchronization.
    /// The GameWorld owns this system; attached placed components remain owned by their
    /// originating runtime and receive transform callbacks directly from worker jobs.
    class TransformRuntime final : public game::RuntimeSystem
    {
    public:
        struct Impl;

        explicit TransformRuntime(const TransformRuntimeConfig& config = {}) noexcept;
        ~TransformRuntime() override;

        TransformRuntime(const TransformRuntime&) = delete;
        TransformRuntime& operator=(const TransformRuntime&) = delete;

        /// Applies committed WorldPlacement and EntityParent changes. GameWorld invokes
        /// this automatically after its end-of-frame ECS flush.
        [[nodiscard]] bool Synchronize(TransformRuntimeFailure* failure = nullptr) noexcept;

        /// Starts the parallel root update. The caller must retain the builder
        /// counter in its CPU tail and wait for it before mutating transform-owned state.
        [[nodiscard]] bool DispatchUpdates(jobs::Builder& builder, TransformUpdateDispatch& dispatch,
                                           TransformRuntimeFailure* failure = nullptr) noexcept;

        /// Registers a stable caller-owned placed component and hard-attaches it to the
        /// entity's runtime PlaceholderComponent. TransformRuntime owns the attachment;
        /// the caller must keep the component alive until DetachPlacedComponent succeeds.
        [[nodiscard]] bool AttachPlacedComponent(ecs::EntityId entity, IPlacedComponent& component,
                                                 PlacedComponentBindingHandle& binding,
                                                 TransformRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool DetachPlacedComponent(PlacedComponentBindingHandle binding,
                                                 TransformRuntimeFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool GetPlacedComponentBinding(PlacedComponentBindingHandle binding,
                                                     PlacedComponentBindingSnapshot& snapshot) const noexcept;

        [[nodiscard]] PlaceholderComponent* GetRoot(ecs::EntityId entity) noexcept;
        [[nodiscard]] const PlaceholderComponent* GetRoot(ecs::EntityId entity) const noexcept;
        [[nodiscard]] TransformRuntimeStats GetStats() const noexcept;

    protected:
        [[nodiscard]] bool OnInitialize(game::GameWorld& world) noexcept override;
        void OnUninitialize(game::GameWorld& world) noexcept override;
        void OnAfterWorldFlush(game::GameWorld& world) noexcept override;
        [[nodiscard]] const char* ReadinessBlocker() const noexcept override;

    private:
        [[nodiscard]] bool InitializeRuntime(ecs::World& world, TransformRuntimeFailure* failure) noexcept;
        [[nodiscard]] bool ShutdownRuntime(TransformRuntimeFailure* failure) noexcept;

        TransformRuntimeConfig m_config;
        Impl* m_impl = nullptr;
        const char* m_readinessBlocker = nullptr;
    };
} // namespace vanguard::entities
