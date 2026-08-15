#pragma once

#include <vanguard/system/types.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumRenderPhases = 64;
    inline constexpr u32 MaximumRenderPhaseNameBytes = 64;
    inline constexpr u8 InvalidRenderPhaseIndex = 0xffu;

    struct RenderPhaseKey
    {
        u64 value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
        [[nodiscard]] friend constexpr bool operator==(const RenderPhaseKey&, const RenderPhaseKey&) noexcept = default;
    };

    /// Creates the durable identity used by assets and renderer configuration. The registry resolves
    /// this identity to a compact RenderPhaseId before frame execution.
    [[nodiscard]] constexpr RenderPhaseKey MakeRenderPhaseKey(const char* const name, const u32 pass = 0) noexcept
    {
        if (name == nullptr || name[0] == '\0') return {};
        u64 hash = 1469598103934665603ull;
        for (const char* character = name; *character != '\0'; ++character)
        {
            hash ^= static_cast<u8>(*character);
            hash *= 1099511628211ull;
        }
        for (u32 byteIndex = 0; byteIndex < sizeof(pass); ++byteIndex)
        {
            hash ^= static_cast<u8>((pass >> (byteIndex * 8u)) & 0xffu);
            hash *= 1099511628211ull;
        }
        return {hash != 0 ? hash : 1ull};
    }

    struct RenderPhaseId
    {
        u8 index = InvalidRenderPhaseIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return index < MaximumRenderPhases; }
        [[nodiscard]] friend constexpr bool operator==(const RenderPhaseId&, const RenderPhaseId&) noexcept = default;
    };

    inline constexpr RenderPhaseId InvalidRenderPhaseId{};

    class RenderPhaseSet final
    {
    public:
        constexpr RenderPhaseSet() noexcept = default;
        explicit constexpr RenderPhaseSet(const u64 bits) noexcept : m_bits(bits) {}

        [[nodiscard]] constexpr bool Empty() const noexcept { return m_bits == 0; }
        [[nodiscard]] constexpr u64 Bits() const noexcept { return m_bits; }
        [[nodiscard]] constexpr bool Contains(const RenderPhaseId phase) const noexcept
        {
            return phase.IsValid() && (m_bits & (1ull << phase.index)) != 0;
        }
        [[nodiscard]] constexpr bool Intersects(const RenderPhaseSet& other) const noexcept
        {
            return (m_bits & other.m_bits) != 0;
        }
        [[nodiscard]] constexpr bool ContainsAll(const RenderPhaseSet& other) const noexcept
        {
            return (m_bits & other.m_bits) == other.m_bits;
        }
        constexpr bool Add(const RenderPhaseId phase) noexcept
        {
            if (!phase.IsValid()) return false;
            m_bits |= 1ull << phase.index;
            return true;
        }
        constexpr bool Remove(const RenderPhaseId phase) noexcept
        {
            if (!phase.IsValid()) return false;
            m_bits &= ~(1ull << phase.index);
            return true;
        }
        constexpr void Clear() noexcept { m_bits = 0; }

        [[nodiscard]] friend constexpr bool operator==(const RenderPhaseSet&, const RenderPhaseSet&) noexcept = default;

    private:
        u64 m_bits = 0;
    };

    enum class RenderPhaseSortMode : u8
    {
        Unordered,
        State,
        FrontToBack,
        BackToFront
    };

    struct RenderPhaseDesc
    {
        const char* name = nullptr;
        u32 pass = 0;
        RenderPhaseSortMode sortMode = RenderPhaseSortMode::State;
    };

    struct RenderPhaseDefinition
    {
        RenderPhaseId id;
        RenderPhaseKey key;
        u32 pass = 0;
        RenderPhaseSortMode sortMode = RenderPhaseSortMode::State;
        char name[MaximumRenderPhaseNameBytes]{};
    };

    enum class RenderPhaseFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidDescriptor,
        CapacityExceeded,
        HashCollision,
        IncompatibleDefinition,
        RegistrySealed,
        RegistryNotSealed,
        InvalidPhase
    };

    struct RenderPhaseFailure
    {
        RenderPhaseFailureCode code = RenderPhaseFailureCode::None;
        RenderPhaseKey key;
        RenderPhaseId phase;
        const char* message = nullptr;
    };

    struct RenderPhaseRegistryConfig
    {
        u32 maximumPhases = MaximumRenderPhases;
    };

    struct RenderPhaseRegistryStats
    {
        u32 registeredPhases = 0;
        u32 capacity = 0;
        u64 rejectedRegistrations = 0;
        bool sealed = false;
    };

    using VisitRenderPhase = void (*)(const RenderPhaseDefinition& phase, void* userData) noexcept;

    /// Configuration is single-writer and main-thread-only. Once sealed, definitions and compact IDs
    /// are immutable and may be queried concurrently without synchronization.
    class RenderPhaseRegistry final
    {
    public:
        struct Impl;

        RenderPhaseRegistry() noexcept = default;
        ~RenderPhaseRegistry();

        RenderPhaseRegistry(const RenderPhaseRegistry&) = delete;
        RenderPhaseRegistry& operator=(const RenderPhaseRegistry&) = delete;

        [[nodiscard]] bool Initialize(const RenderPhaseRegistryConfig& config = {},
                                      RenderPhaseFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(RenderPhaseFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] bool IsSealed() const noexcept;

        [[nodiscard]] bool Register(const RenderPhaseDesc& desc, RenderPhaseId& phase,
                                    RenderPhaseFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Seal(RenderPhaseFailure* failure = nullptr) noexcept;

        [[nodiscard]] RenderPhaseId Find(RenderPhaseKey key) const noexcept;
        [[nodiscard]] RenderPhaseId Find(const char* name, u32 pass = 0) const noexcept;
        [[nodiscard]] bool Get(RenderPhaseId phase, RenderPhaseDefinition& definition) const noexcept;
        void Visit(VisitRenderPhase visitor, void* userData = nullptr) const noexcept;
        [[nodiscard]] RenderPhaseRegistryStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    namespace standardRenderPhases
    {
        inline constexpr RenderPhaseKey ShadowDepth = MakeRenderPhaseKey("vanguard.render.shadow_depth");
        inline constexpr RenderPhaseKey DepthPrepass = MakeRenderPhaseKey("vanguard.render.depth_prepass");
        inline constexpr RenderPhaseKey Opaque = MakeRenderPhaseKey("vanguard.render.opaque");
        inline constexpr RenderPhaseKey Decal = MakeRenderPhaseKey("vanguard.render.decal");
        inline constexpr RenderPhaseKey Transparent = MakeRenderPhaseKey("vanguard.render.transparent");
        inline constexpr RenderPhaseKey Velocity = MakeRenderPhaseKey("vanguard.render.velocity");
        inline constexpr RenderPhaseKey Selection = MakeRenderPhaseKey("vanguard.render.selection");
    } // namespace standardRenderPhases

    [[nodiscard]] bool RegisterStandardRenderPhases(RenderPhaseRegistry& registry,
                                                    RenderPhaseFailure* failure = nullptr) noexcept;
} // namespace vanguard::rendering
