#pragma once

#include <vanguard/game_input/game_input.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::game_input
{
    inline constexpr u32 MappingMagic = serialization::MakeFourCC('V', 'I', 'N', 'P');
    inline constexpr resources::ResourceTypeId MappingResourceType = serialization::MakeFourCC('V', 'I', 'N', 'P');

    enum class MappingResult : u8
    {
        Success, InvalidArgument, InvalidState, InvalidMagic, UnsupportedVersion, InvalidLayout,
        IntegrityFailure, LimitExceeded, DuplicateId, MappingFailure, IoFailure
    };
    [[nodiscard]] const char* ToString(MappingResult result) noexcept;

    struct MappingBuildDescription
    {
        containers::ArraySpan<const ContextDescriptor> contexts;
        containers::ArraySpan<const ActionDescriptor> actions;
        containers::ArraySpan<const BindingDescriptor> bindings;
        /// Contexts are pushed in this order; each context's declared layer determines its stack.
        containers::ArraySpan<const ContextId> initialContexts;
    };

    struct MappingContextRecord { ContextDescriptor descriptor; char name[MaximumNameBytes]{}; };
    struct MappingActionRecord { ActionDescriptor descriptor; char name[MaximumNameBytes]{}; };
    struct MappingBindingRecord
    {
        BindingDescriptor descriptor;
        Control modifiers[MaximumBindingModifiers]{};
        u8 modifierCount = 0;
    };

    struct MappingReadLimits
    {
        u64 maximumFileSize = 4ull * 1024ull * 1024ull;
        u32 maximumContexts = MaximumContexts;
        u32 maximumActions = MaximumActions;
        u32 maximumBindings = MaximumBindings;
        u32 maximumInitialContexts = MaximumContextStackDepth * static_cast<u32>(ContextLayer::Count);
    };

    class MappingFile final
    {
    public:
        MappingFile() noexcept;
        MappingFile(const MappingFile&) = delete;
        MappingFile& operator=(const MappingFile&) = delete;

        [[nodiscard]] MappingResult Open(filesystem::IFile& reader, const MappingReadLimits& limits = {}) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MappingContextRecord> Contexts() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MappingActionRecord> Actions() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MappingBindingRecord> Bindings() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ContextId> InitialContexts() const noexcept;
        [[nodiscard]] MappingResult Install(ActionMap& destination, bool compile = true) const noexcept;

    private:
        containers::DynamicArray<MappingContextRecord> m_contexts;
        containers::DynamicArray<MappingActionRecord> m_actions;
        containers::DynamicArray<MappingBindingRecord> m_bindings;
        containers::DynamicArray<ContextId> m_initialContexts;
        bool m_open = false;
    };

    class MappingResource final : public resources::ResourceObject
    {
    public:
        [[nodiscard]] resources::ResourceTypeId Type() const noexcept override;
        [[nodiscard]] MappingResult Open(const void* data, usize size, const MappingReadLimits& limits = {}) noexcept;
        [[nodiscard]] const MappingFile& File() const noexcept;

    private:
        MappingFile m_file;
    };

    [[nodiscard]] MappingResult CookMapping(const MappingBuildDescription& description,
                                            filesystem::IFile& output) noexcept;
}
