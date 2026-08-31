#include <vanguard/rendering/render_phase.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(RenderPhaseFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(RenderPhaseFailure* const failure, const RenderPhaseFailureCode code, const char* const message, const RenderPhaseKey key = {},
                                const RenderPhaseId phase = {}) noexcept
        {
            if (failure != nullptr)
                *failure = {code, key, phase, message};
            return false;
        }

        [[nodiscard]] bool CopyName(char* const destination, const char* const source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < MaximumRenderPhaseNameBytes && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        [[nodiscard]] bool SameName(const char* left, const char* right) noexcept
        {
            if (left == nullptr || right == nullptr)
                return left == right;
            while (*left == *right)
            {
                if (*left == '\0')
                    return true;
                ++left;
                ++right;
            }
            return false;
        }

        [[nodiscard]] bool ValidSortMode(const RenderPhaseSortMode mode) noexcept
        {
            return static_cast<u32>(mode) <= static_cast<u32>(RenderPhaseSortMode::BackToFront);
        }
    } // namespace

    struct RenderPhaseRegistry::Impl
    {
        RenderPhaseDefinition definitions[MaximumRenderPhases];
        RenderPhaseRegistryStats stats;
        u32 maximumPhases = MaximumRenderPhases;
    };

    RenderPhaseRegistry::~RenderPhaseRegistry()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool RenderPhaseRegistry::Initialize(const RenderPhaseRegistryConfig& config, RenderPhaseFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, RenderPhaseFailureCode::AlreadyInitialized, "render phase registry is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderPhaseFailureCode::WrongThread, "render phase registry must initialize on the main thread");
        if (config.maximumPhases == 0 || config.maximumPhases > MaximumRenderPhases)
            return Fail(failure, RenderPhaseFailureCode::InvalidDescriptor, "render phase registry capacity is invalid");

        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, RenderPhaseFailureCode::CapacityExceeded, "render phase registry allocation failed");
        m_impl = ::new (block.address) Impl();
        m_impl->maximumPhases = config.maximumPhases;
        m_impl->stats.capacity = config.maximumPhases;
        return true;
    }

    bool RenderPhaseRegistry::Shutdown(RenderPhaseFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderPhaseFailureCode::WrongThread, "render phase registry must shutdown on the main thread");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool RenderPhaseRegistry::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }
    bool RenderPhaseRegistry::IsSealed() const noexcept
    {
        return m_impl != nullptr && m_impl->stats.sealed;
    }

    bool RenderPhaseRegistry::Register(const RenderPhaseDesc& desc, RenderPhaseId& phase, RenderPhaseFailure* const failure) noexcept
    {
        ClearFailure(failure);
        phase = {};
        if (m_impl == nullptr)
            return Fail(failure, RenderPhaseFailureCode::NotInitialized, "render phase registry is not initialized");
        if (!concurrency::IsMainThread())
        {
            ++m_impl->stats.rejectedRegistrations;
            return Fail(failure, RenderPhaseFailureCode::WrongThread, "render phases must be registered on the main thread");
        }
        if (m_impl->stats.sealed)
        {
            ++m_impl->stats.rejectedRegistrations;
            return Fail(failure, RenderPhaseFailureCode::RegistrySealed, "render phase registry is sealed");
        }
        char name[MaximumRenderPhaseNameBytes]{};
        if (!CopyName(name, desc.name) || !ValidSortMode(desc.sortMode))
        {
            ++m_impl->stats.rejectedRegistrations;
            return Fail(failure, RenderPhaseFailureCode::InvalidDescriptor, "render phase descriptor is invalid");
        }
        const RenderPhaseKey key = MakeRenderPhaseKey(name, desc.pass);
        for (u32 index = 0; index < m_impl->stats.registeredPhases; ++index)
        {
            const RenderPhaseDefinition& existing = m_impl->definitions[index];
            if (existing.key != key)
                continue;
            if (!SameName(existing.name, name) || existing.pass != desc.pass)
            {
                ++m_impl->stats.rejectedRegistrations;
                return Fail(failure, RenderPhaseFailureCode::HashCollision, "render phase stable identity collides with another definition", key, existing.id);
            }
            if (existing.sortMode != desc.sortMode)
            {
                ++m_impl->stats.rejectedRegistrations;
                return Fail(failure, RenderPhaseFailureCode::IncompatibleDefinition, "render phase was registered with incompatible policy", key, existing.id);
            }
            phase = existing.id;
            return true;
        }
        if (m_impl->stats.registeredPhases == m_impl->maximumPhases)
        {
            ++m_impl->stats.rejectedRegistrations;
            return Fail(failure, RenderPhaseFailureCode::CapacityExceeded, "render phase registry capacity exceeded", key);
        }

        const u32 index = m_impl->stats.registeredPhases++;
        RenderPhaseDefinition& definition = m_impl->definitions[index];
        definition.id = {static_cast<u8>(index)};
        definition.key = key;
        definition.pass = desc.pass;
        definition.sortMode = desc.sortMode;
        static_cast<void>(CopyName(definition.name, name));
        phase = definition.id;
        return true;
    }

    bool RenderPhaseRegistry::Seal(RenderPhaseFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(failure, RenderPhaseFailureCode::NotInitialized, "render phase registry is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(failure, RenderPhaseFailureCode::WrongThread, "render phase registry must be sealed on the main thread");
        if (m_impl->stats.sealed)
            return true;
        if (m_impl->stats.registeredPhases == 0)
            return Fail(failure, RenderPhaseFailureCode::InvalidDescriptor, "render phase registry cannot seal without definitions");
        m_impl->stats.sealed = true;
        return true;
    }

    RenderPhaseId RenderPhaseRegistry::Find(const RenderPhaseKey key) const noexcept
    {
        if (m_impl == nullptr || !key.IsValid())
            return {};
        for (u32 index = 0; index < m_impl->stats.registeredPhases; ++index)
            if (m_impl->definitions[index].key == key)
                return m_impl->definitions[index].id;
        return {};
    }

    RenderPhaseId RenderPhaseRegistry::Find(const char* const name, const u32 pass) const noexcept
    {
        return Find(MakeRenderPhaseKey(name, pass));
    }

    bool RenderPhaseRegistry::Get(const RenderPhaseId phase, RenderPhaseDefinition& definition) const noexcept
    {
        definition = {};
        if (m_impl == nullptr || !phase.IsValid() || phase.index >= m_impl->stats.registeredPhases)
            return false;
        definition = m_impl->definitions[phase.index];
        return true;
    }

    void RenderPhaseRegistry::Visit(const VisitRenderPhase visitor, void* const userData) const noexcept
    {
        if (m_impl == nullptr || visitor == nullptr)
            return;
        for (u32 index = 0; index < m_impl->stats.registeredPhases; ++index)
            visitor(m_impl->definitions[index], userData);
    }

    RenderPhaseRegistryStats RenderPhaseRegistry::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : RenderPhaseRegistryStats{};
    }

    bool RegisterStandardRenderPhases(RenderPhaseRegistry& registry, RenderPhaseFailure* const failure) noexcept
    {
        const RenderPhaseDesc phases[] = {
            {"vanguard.render.shadow_depth", 0, RenderPhaseSortMode::State},      {"vanguard.render.depth_prepass", 0, RenderPhaseSortMode::FrontToBack},
            {"vanguard.render.opaque", 0, RenderPhaseSortMode::FrontToBack},      {"vanguard.render.decal", 0, RenderPhaseSortMode::State},
            {"vanguard.render.transparent", 0, RenderPhaseSortMode::BackToFront}, {"vanguard.render.velocity", 0, RenderPhaseSortMode::State},
            {"vanguard.render.selection", 0, RenderPhaseSortMode::State}};
        for (const RenderPhaseDesc& desc : phases)
        {
            RenderPhaseId phase;
            if (!registry.Register(desc, phase, failure))
                return false;
        }
        return true;
    }
} // namespace vanguard::rendering
