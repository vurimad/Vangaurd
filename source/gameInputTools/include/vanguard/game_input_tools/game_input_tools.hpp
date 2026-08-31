#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/game_input/mapping_resource.hpp>

namespace vanguard::game_input_tools
{
    inline constexpr resources::ResourceTypeId SourceMappingResourceType = serialization::MakeFourCC('V', 'I', 'M', 'S');
    inline constexpr assets::CompilerId MappingCompilerId = game_input::MakeId("vanguard.input_mapping.compiler");
    inline constexpr u32 MappingCompilerVersion = 1;

    enum class SourceResult : u8
    {
        Success,
        InvalidArgument,
        InvalidEncoding,
        UnsupportedVersion,
        UnknownDirective,
        InvalidFieldCount,
        InvalidIdentifier,
        InvalidEnum,
        InvalidNumber,
        DuplicateCurvePoint,
        UnknownAction,
        LimitExceeded,
        MappingValidationFailure,
        WriteFailure
    };

    struct SourceDiagnostic
    {
        SourceResult result = SourceResult::Success;
        u32 line = 0;
        u32 column = 0;
        const char* message = nullptr;
    };

    [[nodiscard]] const char* ToString(SourceResult result) noexcept;

    /// Compiles UTF-8 editor source directly to a cooked vinput document. The source is line-oriented,
    /// comments begin with '#', identifiers contain no whitespace, and declaration order is irrelevant.
    [[nodiscard]] SourceResult CompileSourceMapping(containers::ArraySpan<const u8> source, filesystem::IFile& output,
                                                    SourceDiagnostic* diagnostic = nullptr) noexcept;

    /// Descriptor for assets::BuildSystem and its DDC/build graph. The compiler has no generated dependencies.
    [[nodiscard]] assets::CompilerDescriptor MakeMappingCompilerDescriptor() noexcept;
    [[nodiscard]] assets::Result RegisterMappingCompiler(assets::BuildSystem& buildSystem) noexcept;
} // namespace vanguard::game_input_tools
