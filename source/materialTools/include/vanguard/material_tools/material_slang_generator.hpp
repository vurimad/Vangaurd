#pragma once

#include <vanguard/material_tools/material_ir.hpp>
#include <vanguard/shader_tools/shader_compiler.hpp>

namespace vanguard::material_tools
{
    enum class MaterialSlangResult : u8
    {
        Success,
        InvalidArgument,
        UnsupportedType,
        UnsupportedValue,
        MissingBinding,
        ReflectionMismatch,
        LimitExceeded
    };

    struct MaterialSlangSymbol
    {
        u64 semantic = 0;
        const char* name = nullptr;
    };

    /// Trusted domain ABI surrounding compiler-generated material evaluation.
    /// The prefix declares the input/output types plus the parameter-word,
    /// resource-descriptor, and sampler-descriptor index loaders. The generator
    /// owns the annotated parameter and resource-role structs. The suffix
    /// declares entry points that call evaluationFunctionName and carry
    /// VanguardMaterialProgram. VanguardLoadMaterialParameterWord addresses the
    /// exact reflected byte offset and returns the next 32 little-endian bits;
    /// generated 16/64-bit accessors mask or combine those words. Logical
    /// resource values use descriptor-index uints in generated value and domain
    /// output aggregates. A typed opaque Slang resource is materialized only at
    /// an operation that consumes it, such as TextureSample, so one evaluator
    /// ABI remains valid for DXIL and SPIR-V.
    struct MaterialSlangDomain
    {
        const char* stableName = nullptr;
        u32 schemaVersion = 0;
        shaders::StageMask legalStages = 0;
        shaders::MaterialShaderCapabilityMask requiredCapabilities = 0;
        const char* inputTypeName = nullptr;
        const char* outputTypeName = nullptr;
        const char* parameterTypeName = nullptr;
        const char* resourceTypeName = nullptr;
        const char* evaluationFunctionName = nullptr;
        u32 accessorAbiVersion = 1;
        containers::ArraySpan<const MaterialSlangSymbol> inputs;
        containers::ArraySpan<const MaterialSlangSymbol> outputs;
        containers::ArraySpan<const u8> prefix;
        containers::ArraySpan<const u8> suffix;
    };

    struct MaterialSlangLimits
    {
        u32 maximumSourceBytes = 16u * 1024u * 1024u;
    };

    struct MaterialGeneratedSourceRange
    {
        MaterialIrValueId value = InvalidMaterialIrValue;
        u32 firstLine = 0;
        u32 lastLine = 0;
        u64 sourceNode = 0;
        u32 sourcePin = 0;
    };

    /// Generates reflection-probe Slang from finalized, backend-neutral IR.
    /// The bounded code-generation surface accepts every finalized numeric
    /// scalar width, fixed arrays and aggregates, explicit row/column-major
    /// matrices, typed textures/samplers/buffers/acceleration structures,
    /// texture sampling, arithmetic, comparison, selection, casts,
    /// construction, and extraction. Resource values lower uniformly to
    /// descriptor indices; annotated resource-role declarations retain their
    /// complete logical types for reflection, and resource operations resolve
    /// typed objects locally at their point of use. Resource roles are sorted
    /// by semantic and assigned dense logical slots. Optional source ranges map
    /// every emitted value statement back to finalized IR and its primary
    /// authored node/pin.
    [[nodiscard]] MaterialSlangResult GenerateMaterialSlangProbe(const MaterialIrModule& module,
                                                                 const MaterialSlangDomain& domain,
                                                                 containers::DynamicArray<u8>& source,
                                                                 const MaterialSlangLimits& limits = {},
                                                                 containers::DynamicArray<MaterialGeneratedSourceRange>* sourceRanges = nullptr) noexcept;

    /// Resolves generated parameter-offset, matrix-stride, and array-stride
    /// markers from reflection. Sources with no markers are copied unchanged,
    /// preserving support for trusted handwritten material programs.
    [[nodiscard]] MaterialSlangResult FinalizeMaterialSlangSource(containers::ArraySpan<const u8> probeSource,
                                                                  const shader_tools::CompileOutput& reflection,
                                                                  containers::DynamicArray<u8>& source,
                                                                  const MaterialSlangLimits& limits = {}) noexcept;

    [[nodiscard]] const char* ToString(MaterialSlangResult result) noexcept;
} // namespace vanguard::material_tools
