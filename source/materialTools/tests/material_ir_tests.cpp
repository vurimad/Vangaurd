#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/assets/derived_data_package_artifact_reader.hpp>
#include <vanguard/assets/derived_data_artifact_source.hpp>
#include <vanguard/assets/loose_resource_materializer.hpp>
#include <vanguard/assets/package_planner.hpp>
#include <vanguard/assets/asset_recooker.hpp>
#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/jobs/jobs.hpp>
#include <vanguard/material_tools/material_asset_compilers.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
#include <vanguard/material_tools/material_frontend.hpp>
#include <vanguard/material_tools/material_ir.hpp>
#include <vanguard/material_tools/material_preview.hpp>
#include <vanguard/material_tools/material_slang_generator.hpp>
#include <vanguard/packages/packages.hpp>

#include <array>
#include <cstdio>
#include <cstring>

#include "static_surface_proof.hpp"

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;
    namespace shaders = vanguard::shaders;
    int failures = 0;
    const char* activeSuite = "startup";

    void BeginSuite(const char* const name) noexcept
    {
        activeSuite = name;
    }

    void Check(const bool condition, const char* message) noexcept
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "[materialToolsTests][%s] failed: %s\n", activeSuite, message);
        }
    }

    bool EqualBytes(const containers::ArraySpan<const u8> left, const containers::ArraySpan<const u8> right) noexcept
    {
        return left.Count() == right.Count() && (left.Empty() || std::memcmp(left.Data(), right.Data(), left.Count()) == 0);
    }

    bool CopyBytes(const containers::ArraySpan<const u8> source, containers::DynamicArray<u8>& destination) noexcept
    {
        destination.Resize(source.Count());
        if (destination.Size() != source.Count())
            return false;
        if (!source.Empty())
            std::memcpy(destination.Data(), source.Data(), source.Count());
        return true;
    }

    u64 ArtifactBytes(const assets::BuildOutput& output) noexcept
    {
        u64 total = 0;
        for (const assets::Artifact& artifact : output.artifacts)
        {
            if (artifact.bytes.Size() > ~u64{0} - total)
                return ~u64{0};
            total += artifact.bytes.Size();
        }
        return total;
    }

    void DeleteLooseProofFiles(filesystem::Manager& manager, const filesystem::AbsolutePath& root) noexcept
    {
        containers::DynamicArray<filesystem::AbsolutePath> files(memory::pools::Assets::GetInstance());
        manager.FindFiles(root, containers::String("*"), files, true);
        for (const filesystem::AbsolutePath& file : files)
            static_cast<void>(manager.DeleteFile(file));

        containers::DynamicArray<filesystem::AbsolutePath> firstLevel(memory::pools::Assets::GetInstance());
        manager.FindDirectories(root, firstLevel);
        for (const filesystem::AbsolutePath& first : firstLevel)
        {
            containers::DynamicArray<filesystem::AbsolutePath> secondLevel(memory::pools::Assets::GetInstance());
            manager.FindDirectories(first, secondLevel);
            for (const filesystem::AbsolutePath& second : secondLevel)
                static_cast<void>(manager.DeletePath(second));
            static_cast<void>(manager.DeletePath(first));
        }
        static_cast<void>(manager.DeletePath(root));
    }

    bool FileEquals(const filesystem::AbsolutePath& path, const containers::ArraySpan<const u8> expected) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        if (!manager.FileExist(path) || manager.GetFileSize(path) != expected.Count())
            return false;
        auto reader = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader)
            return false;
        containers::DynamicArray<u8> actual(memory::pools::Assets::GetInstance());
        actual.Resize(expected.Count());
        if (actual.Size() != expected.Count())
            return false;
        if (!actual.Empty())
            reader->Serialize(actual.Data(), actual.Size());
        return !reader->HasErrors() && EqualBytes(actual, expected);
    }

    bool ReadFileBytes(const filesystem::AbsolutePath& path, containers::DynamicArray<u8>& bytes) noexcept
    {
        filesystem::Manager& manager = filesystem::GetManager();
        const u64 size = manager.GetFileSize(path);
        if (!manager.FileExist(path) || size > ~u32{0})
            return false;
        bytes.Resize(static_cast<u32>(size));
        if (bytes.Size() != size)
            return false;
        auto reader = manager.CreateFileReader(path, filesystem::FOF_Buffered);
        if (!reader)
            return false;
        if (!bytes.Empty())
            reader->Serialize(bytes.Data(), bytes.Size());
        return !reader->HasErrors();
    }

    bool CompileRuntimeMaterialAccessorContract(const filesystem::AbsolutePath& working) noexcept
    {
        const filesystem::AbsolutePath sourcePath = working.AddDirPath("source").AddDirPath("rendering").AddDirPath("shaders").AddFilePath("gpu_scene_types.hlsli");
        containers::DynamicArray<u8> source(memory::pools::Tools::GetInstance());
        if (!ReadFileBytes(sourcePath, source))
            return false;
        constexpr char probe[] = R"(
            struct RuntimeMaterialAccessorProbeConstants
            {
                uint tableDirectoryDescriptor;
                uint pageDirectoryDescriptor;
                uint materialIndex;
                uint roleSlot;
            };
            ConstantBuffer<RuntimeMaterialAccessorProbeConstants> runtimeMaterialAccessorProbeConstants;
            RWStructuredBuffer<uint4> runtimeMaterialAccessorProbeOutput;

            [shader("compute")]
            [numthreads(1, 1, 1)]
            void RuntimeMaterialAccessorProbe(uint3 threadId : SV_DispatchThreadID)
            {
                StructuredBuffer<GpuSceneTableDirectory> tableDirectory =
                    ResourceDescriptorHeap[NonUniformResourceIndex(runtimeMaterialAccessorProbeConstants.tableDirectoryDescriptor)];
                StructuredBuffer<GpuScenePageDirectoryEntry> pageDirectory =
                    ResourceDescriptorHeap[NonUniformResourceIndex(runtimeMaterialAccessorProbeConstants.pageDirectoryDescriptor)];
                GpuMaterial material = LoadGpuMaterial(runtimeMaterialAccessorProbeConstants.materialIndex, tableDirectory, pageDirectory);
                GpuMaterialResource resource = LoadGpuMaterialResource(material, runtimeMaterialAccessorProbeConstants.roleSlot, tableDirectory, pageDirectory);
                uint parameterWord = LoadGpuMaterialParameterWord(material, threadId.x, tableDirectory, pageDirectory);
                runtimeMaterialAccessorProbeOutput[0] = uint4(parameterWord, resource.resource, resource.samplerDescriptor, resource.type);
            }
        )";
        const u32 originalSize = source.Size();
        source.Resize(originalSize + sizeof(probe) - 1u);
        if (source.Size() != originalSize + sizeof(probe) - 1u)
            return false;
        std::memcpy(source.TypedData() + originalSize, probe, sizeof(probe) - 1u);

        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        const shader_tools::EntryPoint entries[]{{"RuntimeMaterialAccessorProbe", shaders::ShaderStage::Compute}};
        shader_tools::CompileRequest request;
        request.sourceName = "rendering/shaders/runtime_material_accessor_probe.slang";
        request.moduleName = "runtime_material_accessor_probe";
        request.source = source;
        request.entryPoints = entries;
        const shader_tools::Target targets[]{shader_tools::Target::D3D12Dxil, shader_tools::Target::VulkanSpirV};
        const shaders::NativeFormat formats[]{shaders::NativeFormat::Dxil, shaders::NativeFormat::SpirV};
        bool succeeded = true;
        for (u32 index = 0; index < 2; ++index)
        {
            request.settings.target = targets[index];
            shader_tools::CompileOutput output;
            const shader_tools::Result result = compiler.Compile(request, output);
            if (result != shader_tools::Result::Success)
                std::fprintf(stderr, "[materialToolsTests] runtime material accessor target %u: %s\n", index, output.GetDiagnostics());
            succeeded = succeeded && result == shader_tools::Result::Success && output.GetStages().Size() == 1 && output.GetStages()[0].format == formats[index] && output.GetStages()[0].bytecodeSize != 0;
        }
        compiler.Shutdown();
        return succeeded;
    }

    bool ReadPackageResource(const packages::PackageReader& package, const packages::Resource* const resource, filesystem::IFile& physicalPackage, containers::DynamicArray<u8>& bytes) noexcept
    {
        bytes.Clear();
        if (resource == nullptr || resource->logicalSize > ~u32{0})
            return false;
        bytes.Resize(static_cast<u32>(resource->logicalSize));
        if (bytes.Size() != resource->logicalSize)
            return false;
        packages::ResourceFileReader reader;
        if (reader.Open(package, *resource, physicalPackage) != packages::Result::Success)
            return false;
        if (!bytes.Empty())
            reader.Serialize(bytes.Data(), bytes.Size());
        const bool succeeded = !reader.HasErrors() && reader.GetLastResult() == packages::Result::Success;
        reader.Close();
        return succeeded;
    }

    mt::MaterialIrType FloatType() noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Numeric;
        type.scalarType = shaders::ScalarType::F32;
        return type;
    }

    mt::MaterialIrType FloatVector(const u8 components) noexcept
    {
        mt::MaterialIrType type = FloatType();
        type.rows = components;
        return type;
    }

    mt::MaterialIrType U32Type() noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Numeric;
        type.scalarType = shaders::ScalarType::U32;
        return type;
    }

    mt::MaterialIrType BoolType() noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Numeric;
        type.scalarType = shaders::ScalarType::Bool;
        return type;
    }

    u32 ReadU32(const containers::ArraySpan<const u8> data, const mt::MaterialIrValue& value) noexcept
    {
        const u8* const bytes = data.Data() + value.dataOffset;
        return static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) | (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
    }

    std::array<u8, 4> IndexBytes(const u32 value) noexcept
    {
        return {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
    }

    bool ContainsText(const containers::ArraySpan<const u8> bytes, const char* const text) noexcept
    {
        if (text == nullptr)
            return false;
        const u32 length = static_cast<u32>(std::strlen(text));
        if (length == 0 || length > bytes.Size())
            return false;
        for (u32 offset = 0; offset <= bytes.Size() - length; ++offset)
            if (std::memcmp(bytes.Data() + offset, text, length) == 0)
                return true;
        return false;
    }

    mt::MaterialIrResult BuildSimple(const bool reverseConstants, vanguard::crypto::Digest256& fingerprint) noexcept
    {
        mt::MaterialIrBuilder builder;
        builder.Reset(vanguard::crypto::Sha256("SurfaceDomain", 13));
        const float firstValue = reverseConstants ? 2.0f : 1.0f;
        const float secondValue = reverseConstants ? 1.0f : 2.0f;
        mt::MaterialIrValueId first = 0;
        mt::MaterialIrValueId second = 0;
        mt::MaterialIrValueBuildDescription constant;
        constant.kind = mt::MaterialIrValueKind::Constant;
        constant.type = FloatType();
        constant.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        constant.data = {reinterpret_cast<const vanguard::u8*>(&firstValue), sizeof(firstValue)};
        if (builder.AddValue(constant, first) != mt::MaterialIrResult::Success)
            return mt::MaterialIrResult::InvalidArgument;
        constant.data = {reinterpret_cast<const vanguard::u8*>(&secondValue), sizeof(secondValue)};
        if (builder.AddValue(constant, second) != mt::MaterialIrResult::Success)
            return mt::MaterialIrResult::InvalidArgument;
        const std::array<mt::MaterialIrValueId, 2> operands = reverseConstants ? std::array<mt::MaterialIrValueId, 2>{second, first} : std::array<mt::MaterialIrValueId, 2>{first, second};
        mt::MaterialIrValueBuildDescription add;
        add.kind = mt::MaterialIrValueKind::Instruction;
        add.opcode = mt::MaterialIrOpcode::Add;
        add.type = FloatType();
        add.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        add.operands = {operands.data(), static_cast<vanguard::u32>(operands.size())};
        mt::MaterialIrValueId sum = 0;
        if (builder.AddValue(add, sum) != mt::MaterialIrResult::Success || builder.AddOutput({0x1000, sum, shaders::StageBit(shaders::ShaderStage::Fragment)}) != mt::MaterialIrResult::Success)
            return mt::MaterialIrResult::InvalidArgument;
        mt::MaterialIrModule module;
        vanguard::containers::DynamicArray<mt::MaterialIrDiagnostic> diagnostics(vanguard::memory::pools::Tools::GetInstance());
        const mt::MaterialIrResult result = builder.Finalize(module, diagnostics);
        fingerprint = module.GetContentFingerprint();
        return result;
    }

    resources::ResourceReference Reference(const char* const path, const resources::ResourceTypeId type) noexcept
    {
        return resources::ResourceReference(resources::ResourcePath::FromString(path), type);
    }

    constexpr u64 ComputeInputSemantic = 0x494e5055545f4944ull;
    constexpr u64 RoughnessSemantic = 0x524f5547484e4553ull;
    constexpr u64 ComputeOutputSemantic = 0x4f55545055545f56ull;
    constexpr u64 AuthoredInputNode = 0x415554484f525f49ull;
    constexpr u32 AuthoredInputPin = 7;

    constexpr char MaterialDomainPrefix[] = R"(
        struct ComputeMaterialInput { uint index; };
        struct ComputeMaterialOutput { float value; };
        uint VanguardLoadMaterialParameterWord(uint byteOffset)
        {
            return byteOffset == 0u ? asuint(0.5f) : 0u;
        }
        uint VanguardLoadMaterialResourceDescriptor(uint roleSlot) { return 0u; }
        uint VanguardLoadMaterialSamplerDescriptor(uint roleSlot) { return 0u; }
    )";

    constexpr char MaterialDomainSuffix[] = R"(
        [VanguardMaterialProgram("EvaluateComputeMaterial", "ComputeMaterialParameters", "ComputeMaterialResources", 1)]
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void MaterialCompute(uint3 threadId : SV_DispatchThreadID)
        {
            ComputeMaterialInput input;
            input.index = threadId.x;
            ComputeMaterialOutput output = EvaluateComputeMaterial(input);
            if (output.value < 0.0) return;
        }
    )";

    constexpr char ComputeMaterialContractSource[] = R"(
        [__AttributeUsage(_AttributeTargets.Function)]
        struct VanguardMaterialDomainAttribute { string stableName; int schemaVersion; int legalStages; int requiredCapabilities; };
        [__AttributeUsage(_AttributeTargets.Function)]
        struct VanguardMaterialProgramAttribute { string domainFunction; string parameterType; string resourceType; int accessorAbi; };
        [__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialParametersAttribute {};
        [__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialResourcesAttribute {};
        [__AttributeUsage(_AttributeTargets.Var)] struct VanguardMaterialResourceAttribute { int firstSlot; int required; };

        struct ComputeMaterialInput { uint index; };
        struct ComputeMaterialOutput { float value; };
        [VanguardMaterialParameters] struct ComputeMaterialParameters {};
        [VanguardMaterialResources] struct ComputeMaterialResources {};

        [VanguardMaterialDomain("ComputeMaterial", 1, 32, 0)]
        ComputeMaterialOutput EvaluateComputeMaterial(ComputeMaterialInput input)
        {
            ComputeMaterialOutput output;
            output.value = float(input.index);
            return output;
        }

        [VanguardMaterialProgram("EvaluateComputeMaterial", "ComputeMaterialParameters", "ComputeMaterialResources", 1)]
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void MaterialCompute(uint3 threadId : SV_DispatchThreadID)
        {
            ComputeMaterialInput input;
            input.index = threadId.x;
            ComputeMaterialOutput output = EvaluateComputeMaterial(input);
            if (output.value < 0.0) return;
        }
    )";

    shaders::MaterialDomainContract MakeTestDomainContract(const u64 name, const shaders::StageMask legalStages, const char* const inputIdentity, const char* const outputIdentity,
                                                           const shaders::MaterialShaderCapabilityMask requiredCapabilities = 0) noexcept
    {
        shaders::MaterialDomainContract contract;
        contract.name = name;
        contract.schemaVersion = 1;
        contract.legalStages = legalStages;
        contract.requiredCapabilities = requiredCapabilities;
        contract.inputType = crypto::Sha256(inputIdentity, std::strlen(inputIdentity));
        contract.outputType = crypto::Sha256(outputIdentity, std::strlen(outputIdentity));
        return contract;
    }

    bool ReflectComputeMaterialContract(shaders::MaterialDomainContract& contract) noexcept
    {
        contract = {};
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        const shader_tools::EntryPoint entries[]{{"MaterialCompute", shaders::ShaderStage::Compute}};
        shader_tools::CompileRequest request;
        request.sourceName = "materials/tests/compute_material_contract.slang";
        request.moduleName = "compute_material_contract";
        request.source = {reinterpret_cast<const u8*>(ComputeMaterialContractSource), sizeof(ComputeMaterialContractSource) - 1u};
        request.entryPoints = entries;
        request.settings.target = shader_tools::Target::D3D12Dxil;
        shader_tools::CompileOutput reflection;
        const shader_tools::Result result = compiler.Reflect(request, reflection);
        if (result == shader_tools::Result::Success && reflection.HasMaterialContract())
            contract = reflection.GetMaterialDomain();
        else
            std::fprintf(stderr, "[materialToolsTests] ComputeMaterial contract reflection: %s\n", reflection.GetDiagnostics());
        compiler.Shutdown();
        return result == shader_tools::Result::Success && contract.name != 0;
    }

    mt::MaterialInputResult EncodeComputeMaterialProgram(const shaders::MaterialDomainContract& expectedDomain, const crypto::Digest256& expectedDomainFingerprint,
                                                         const shaders::MaterialShaderCapabilityMask requiredCapabilities, containers::DynamicArray<u8>& bytes) noexcept
    {
        const shader_tools::EntryPoint entries[]{{"MaterialCompute", shaders::ShaderStage::Compute}};
        mt::MaterialProgramInput input;
        input.sourceName = "materials/tests/compute_material_contract.slang";
        input.moduleName = "compute_material_contract";
        input.program = 0x434f4e5452414354ull;
        input.permutation = crypto::Sha256("compute-material-contract", sizeof("compute-material-contract") - 1u);
        input.expectedDomain = expectedDomain;
        input.expectedDomainFingerprint = expectedDomainFingerprint;
        input.requiredCapabilities = requiredCapabilities;
        input.source = {reinterpret_cast<const u8*>(ComputeMaterialContractSource), sizeof(ComputeMaterialContractSource) - 1u};
        input.entryPoints = entries;
        input.settings.target = shader_tools::Target::D3D12Dxil;
        return mt::EncodeMaterialProgramInput(input, bytes);
    }

    constexpr char TextureMaterialDomainPrefix[] = R"(
        struct TextureMaterialInput { float2 uv; };
        struct TextureMaterialOutput
        {
            float4 color;
            uint textureDescriptor;
        };
        uint VanguardLoadMaterialParameterWord(uint byteOffset) { return 0u; }
        uint VanguardLoadMaterialResourceDescriptor(uint roleSlot) { return 0u; }
        uint VanguardLoadMaterialSamplerDescriptor(uint roleSlot) { return 0u; }
    )";

    constexpr char TextureMaterialDomainSuffix[] = R"(
        [VanguardMaterialProgram("EvaluateTextureMaterial", "TextureMaterialParameters", "TextureMaterialResources", 1)]
        [shader("fragment")]
        float4 MaterialFragment(float2 uv : TEXCOORD0) : SV_Target
        {
            TextureMaterialInput input;
            input.uv = uv;
            return EvaluateTextureMaterial(input).color;
        }
    )";

    constexpr char WideNumericDomainPrefix[] = R"(
        struct WideNumericInput { uint unused; };
        struct WideNumericOutput
        {
            int16_t i16Value;
            uint16_t u16Value;
            float16_t f16Value;
            int64_t i64Value;
            uint64_t u64Value;
            double f64Value;
        };
        uint VanguardLoadMaterialParameterWord(uint byteOffset) { return 0x3c003c00u + byteOffset; }
        uint VanguardLoadMaterialResourceDescriptor(uint roleSlot) { return 0u; }
        uint VanguardLoadMaterialSamplerDescriptor(uint roleSlot) { return 0u; }
    )";

    constexpr char WideNumericDomainSuffix[] = R"(
        [VanguardMaterialProgram("EvaluateWideNumeric", "WideNumericParameters", "WideNumericResources", 1)]
        [shader("fragment")]
        float4 WideNumericFragment() : SV_Target
        {
            WideNumericInput input;
            input.unused = 0u;
            WideNumericOutput output = EvaluateWideNumeric(input);
            return float4(float(output.f16Value), float(output.i16Value), float(output.u16Value), 1.0f);
        }
    )";

    constexpr char ExtendedResourceDomainPrefix[] = R"(
        struct ExtendedResourceInput { uint unused; };
        struct ExtendedResourceOutput
        {
            uint typedRead;
            uint typedWrite;
            uint structuredRead;
            uint structuredWrite;
            uint byteRead;
            uint byteWrite;
            uint acceleration;
            uint comparisonSampler;
        };
        uint VanguardLoadMaterialParameterWord(uint byteOffset) { return 0u; }
        uint VanguardLoadMaterialResourceDescriptor(uint roleSlot) { return 0u; }
        uint VanguardLoadMaterialSamplerDescriptor(uint roleSlot) { return 0u; }
    )";

    constexpr char ExtendedResourceDomainSuffix[] = R"(
        [VanguardMaterialProgram("EvaluateExtendedResource", "ExtendedResourceParameters", "ExtendedResourceResources", 1)]
        [shader("fragment")]
        float4 ExtendedResourceFragment() : SV_Target
        {
            ExtendedResourceInput input;
            input.unused = 0u;
            ExtendedResourceOutput output = EvaluateExtendedResource(input);
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
    )";

    constexpr u64 DeclaredOutputBase = 0x44534f0000000000ull;
    constexpr shaders::MaterialShaderCapabilityMask DeclaredSurfaceCapabilities =
        shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Numeric16Bit) | shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Integer64Bit) |
        shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::FloatingPoint64Bit) | shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::WritableResources) |
        shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::AccelerationStructure);

    constexpr char DeclaredSurfacePrefix[] = R"(
        struct DeclaredSurfaceInput { uint seed; };
        struct DeclaredSurfaceOutput
        {
            bool boolValue;
            int16_t i16Value;
            uint16_t u16Value;
            float16_t f16Value;
            int i32Value;
            uint u32Value;
            float f32Value;
            int64_t i64Value;
            uint64_t u64Value;
            double f64Value;
            float4 vectorValue;
            float rowMatrixValue;
            float columnMatrixValue;
            float arrayValue;
            float aggregateValue;
            uint textureValue;
            uint samplerValue;
            uint bufferValue;
            uint accelerationValue;
            float4 defaultValue;
        };
        uint VanguardLoadMaterialParameterWord(uint byteOffset) { return byteOffset; }
        uint VanguardLoadMaterialResourceDescriptor(uint roleSlot) { return roleSlot; }
        uint VanguardLoadMaterialSamplerDescriptor(uint roleSlot) { return roleSlot; }
    )";

    constexpr char DeclaredSurfaceSuffix[] = R"(
        [VanguardMaterialProgram("EvaluateDeclaredSurface", "DeclaredSurfaceParameters", "DeclaredSurfaceResources", 1)]
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void DeclaredSurfaceCompute(uint3 threadId : SV_DispatchThreadID)
        {
            DeclaredSurfaceInput input;
            input.seed = threadId.x;
            DeclaredSurfaceOutput output = EvaluateDeclaredSurface(input);
            if (output.f32Value < -1000000.0f) return;
        }
    )";

    constexpr char DeclaredSurfaceContractSource[] = R"(
        [__AttributeUsage(_AttributeTargets.Function)]
        struct VanguardMaterialDomainAttribute { string stableName; int schemaVersion; int legalStages; int requiredCapabilities; };
        [__AttributeUsage(_AttributeTargets.Function)]
        struct VanguardMaterialProgramAttribute { string domainFunction; string parameterType; string resourceType; int accessorAbi; };
        [__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialParametersAttribute {};
        [__AttributeUsage(_AttributeTargets.Struct)] struct VanguardMaterialResourcesAttribute {};
        [__AttributeUsage(_AttributeTargets.Var)] struct VanguardMaterialResourceAttribute { int firstSlot; int required; };
        struct DeclaredSurfaceInput { uint seed; };
        struct DeclaredSurfaceOutput
        {
            bool boolValue; int16_t i16Value; uint16_t u16Value; float16_t f16Value;
            int i32Value; uint u32Value; float f32Value; int64_t i64Value; uint64_t u64Value; double f64Value;
            float4 vectorValue; float rowMatrixValue; float columnMatrixValue; float arrayValue; float aggregateValue;
            uint textureValue; uint samplerValue; uint bufferValue;
            uint accelerationValue; float4 defaultValue;
        };
        [VanguardMaterialParameters] struct DeclaredSurfaceParameters {};
        [VanguardMaterialResources] struct DeclaredSurfaceResources {};
        [VanguardMaterialDomain("DeclaredSurface", 1, 32, 79)]
        DeclaredSurfaceOutput EvaluateDeclaredSurface(DeclaredSurfaceInput input)
        {
            DeclaredSurfaceOutput output;
            output.boolValue = false;
            output.i16Value = int16_t(0);
            output.u16Value = uint16_t(0);
            output.f16Value = float16_t(0.0);
            output.i32Value = 0;
            output.u32Value = 0;
            output.f32Value = 0.0;
            output.i64Value = int64_t(0);
            output.u64Value = uint64_t(0);
            output.f64Value = 0.0;
            output.vectorValue = float4(0.0);
            output.rowMatrixValue = 0.0;
            output.columnMatrixValue = 0.0;
            output.arrayValue = 0.0;
            output.aggregateValue = 0.0;
            output.textureValue = 0u;
            output.samplerValue = 1u;
            output.bufferValue = 2u;
            output.accelerationValue = 3u;
            output.defaultValue = float4(0.0);
            return output;
        }
        [VanguardMaterialProgram("EvaluateDeclaredSurface", "DeclaredSurfaceParameters", "DeclaredSurfaceResources", 1)]
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void DeclaredSurfaceCompute(uint3 threadId : SV_DispatchThreadID)
        {
            DeclaredSurfaceInput input;
            input.seed = threadId.x;
            DeclaredSurfaceOutput output = EvaluateDeclaredSurface(input);
        }
    )";

    bool ReflectDeclaredSurfaceContract(shaders::MaterialDomainContract& contract) noexcept
    {
        contract = {};
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
            return false;
        const shader_tools::EntryPoint entries[]{{"DeclaredSurfaceCompute", shaders::ShaderStage::Compute}};
        shader_tools::CompileRequest request;
        request.sourceName = "materials/tests/declared_surface_contract.slang";
        request.moduleName = "declared_surface_contract";
        request.source = {reinterpret_cast<const u8*>(DeclaredSurfaceContractSource), sizeof(DeclaredSurfaceContractSource) - 1u};
        request.entryPoints = entries;
        request.settings.target = shader_tools::Target::D3D12Dxil;
        shader_tools::CompileOutput reflection;
        const shader_tools::Result result = compiler.Reflect(request, reflection);
        if (result == shader_tools::Result::Success && reflection.HasMaterialContract())
            contract = reflection.GetMaterialDomain();
        else
            std::fprintf(stderr, "[materialToolsTests] declared-surface contract reflection: %s\n", reflection.GetDiagnostics());
        compiler.Shutdown();
        return result == shader_tools::Result::Success && contract.name != 0;
    }

    struct CanonicalFixture
    {
        explicit CanonicalFixture(const assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12, const shader_tools::Target shaderTarget = shader_tools::Target::D3D12Dxil) noexcept
        {
            programSource = Reference("materials/tests/program.mpgi", mt::MaterialProgramInputResourceType);
            pipelineSource = Reference("materials/tests/pipeline.mpli", mt::MaterialPipelineInputResourceType);
            materialSource = Reference("materials/tests/material.mvli", mt::MaterialValueInputResourceType);
            invalidProgramSource = Reference("materials/tests/invalid_program.mpgi", mt::MaterialProgramInputResourceType);
            shader = Reference("materials/tests/program.vshader", shaders::ShaderResourceType);
            invalidShader = Reference("materials/tests/invalid_program.vshader", shaders::ShaderResourceType);
            pipeline = Reference("materials/tests/program.vppl", pipelines::PipelineResourceType);
            material = Reference("materials/tests/program.vmat", materials::MaterialResourceType);

            const shaders::StageMask computeStage = shaders::StageBit(shaders::ShaderStage::Compute);
            const mt::MaterialPinSchema inputOutput{AuthoredInputPin, U32Type(), true};
            const mt::MaterialPinSchema parameterOutput{8, FloatType(), true};
            const mt::MaterialPinSchema castInput{1, U32Type(), true};
            const mt::MaterialPinSchema castOutput{9, FloatType(), true};
            const mt::MaterialPinSchema addInputs[]{{1, FloatType(), true}, {2, FloatType(), true}};
            const mt::MaterialPinSchema addOutput{10, FloatType(), true};
            const crypto::Digest256 inputFingerprint = crypto::Sha256("canonical.input", sizeof("canonical.input") - 1u);
            const crypto::Digest256 parameterFingerprint = crypto::Sha256("canonical.parameter", sizeof("canonical.parameter") - 1u);
            const crypto::Digest256 castFingerprint = crypto::Sha256("canonical.cast", sizeof("canonical.cast") - 1u);
            const crypto::Digest256 addFingerprint = crypto::Sha256("canonical.add", sizeof("canonical.add") - 1u);
            bool built = ReflectComputeMaterialContract(domainContract) && shaders::CalculateMaterialDomainFingerprint(domainContract, domainContractFingerprint) == shaders::Result::Success &&
                         nodes.Register({1, 1, inputFingerprint, {}, {&inputOutput, 1}}) && nodes.Register({2, 1, parameterFingerprint, {}, {&parameterOutput, 1}}) &&
                         nodes.Register({3, 1, castFingerprint, {&castInput, 1}, {&castOutput, 1}}) && nodes.Register({4, 1, addFingerprint, addInputs, {&addOutput, 1}}) && nodes.Freeze();

            const mt::MaterialDomainInputSchema input{ComputeInputSemantic, U32Type(), computeStage};
            const mt::MaterialDomainOutputSchema output{ComputeOutputSemantic, FloatType(), computeStage, {}};
            const mt::MaterialTechniqueRequirement techniqueRequirement{0x434f4d505554455full, pipelineSource};
            const crypto::Digest256 implementationFingerprint = crypto::Sha256("canonical.compute.domain", sizeof("canonical.compute.domain") - 1u);
            built = built && domains.Register({domainContract.name, domainContract, implementationFingerprint, {&input, 1}, {&output, 1}, {&techniqueRequirement, 1}}) && domains.Freeze();

            const mt::MaterialIrScalarConstant roughnessValue = mt::EncodeMaterialF32(0.5f);
            const mt::MaterialIrScalarConstant changedRoughnessValue = mt::EncodeMaterialF32(0.7f);
            const mt::MaterialSourceOperand castOperands[]{{1, 1}};
            const mt::MaterialSourceOperand addOperands[]{{1, 3}, {2, 2}};
            const mt::MaterialSourceValue sourceValues[]{
                {1, AuthoredInputNode, 1, 1, AuthoredInputPin, mt::MaterialIrValueKind::DomainInput, mt::MaterialIrOpcode::None, U32Type(), computeStage, ComputeInputSemantic, {}, {}, {}, 0},
                {2,
                 0x415554484f525f50ull,
                 2,
                 1,
                 8,
                 mt::MaterialIrValueKind::DynamicParameter,
                 mt::MaterialIrOpcode::None,
                 FloatType(),
                 computeStage,
                 RoughnessSemantic,
                 {},
                 {roughnessValue.bytes, roughnessValue.size},
                 {},
                 0},
                {3, 0x415554484f525f43ull, 3, 1, 9, mt::MaterialIrValueKind::Instruction, mt::MaterialIrOpcode::Cast, FloatType(), computeStage, 0, castOperands, {}, {}, 0},
                {4, 0x415554484f525f41ull, 4, 1, 10, mt::MaterialIrValueKind::Instruction, mt::MaterialIrOpcode::Add, FloatType(), computeStage, 0, addOperands, {}, {}, 0}};
            mt::MaterialSourceValue changedValues[]{sourceValues[0], sourceValues[1], sourceValues[2], sourceValues[3]};
            changedValues[1].data = {changedRoughnessValue.bytes, changedRoughnessValue.size};
            mt::MaterialSourceValue structuralValues[]{sourceValues[0], sourceValues[1], sourceValues[2], sourceValues[3]};
            structuralValues[3].opcode = mt::MaterialIrOpcode::Multiply;
            const mt::MaterialSourceValue reorderedValues[]{sourceValues[3], sourceValues[1], sourceValues[0], sourceValues[2]};
            const mt::MaterialSourceOutput sourceOutput{ComputeOutputSemantic, 4, computeStage};
            const mt::MaterialSourceGraph sourceGraph{Reference("materials/tests/source.vmatgraph", 0x4d534752u), domainContract.name, sourceValues, {&sourceOutput, 1}};
            const mt::MaterialSourceGraph changedGraph{sourceGraph.identity, domainContract.name, changedValues, {&sourceOutput, 1}};
            const mt::MaterialSourceGraph structuralGraph{sourceGraph.identity, domainContract.name, structuralValues, {&sourceOutput, 1}};
            const mt::MaterialSourceGraph reorderedGraph{sourceGraph.identity, domainContract.name, reorderedValues, {&sourceOutput, 1}};

            const mt::MaterialSlangSymbol inputs[]{{ComputeInputSemantic, "index"}};
            const mt::MaterialSlangSymbol invalidInputs[]{{ComputeInputSemantic, "xxxxx"}};
            const mt::MaterialSlangSymbol outputs[]{{ComputeOutputSemantic, "value"}};
            mt::MaterialSlangDomain slang;
            slang.stableName = "ComputeMaterial";
            slang.schemaVersion = 1;
            slang.legalStages = computeStage;
            slang.requiredCapabilities = domainContract.requiredCapabilities;
            slang.inputTypeName = "ComputeMaterialInput";
            slang.outputTypeName = "ComputeMaterialOutput";
            slang.parameterTypeName = "ComputeMaterialParameters";
            slang.resourceTypeName = "ComputeMaterialResources";
            slang.evaluationFunctionName = "EvaluateComputeMaterial";
            slang.inputs = inputs;
            slang.outputs = outputs;
            slang.prefix = {reinterpret_cast<const u8*>(MaterialDomainPrefix), sizeof(MaterialDomainPrefix) - 1u};
            slang.suffix = {reinterpret_cast<const u8*>(MaterialDomainSuffix), sizeof(MaterialDomainSuffix) - 1u};

            const shader_tools::EntryPoint entries[]{{"MaterialCompute", shaders::ShaderStage::Compute}};
            pipelines::BuildDescription pipelineRecipe;
            pipelineRecipe.kind = pipelines::PipelineKind::Compute;
            pipelineRecipe.name = techniqueRequirement.name;
            const mt::MaterialCanonicalTechnique technique{techniqueRequirement.name, pipelineSource, pipeline, pipelineRecipe};
            mt::MaterialCanonicalDescription description;
            description.graph = sourceGraph;
            description.frontend = {&nodes, &domains};
            description.slang = slang;
            description.sourceName = "materials/tests/program.slang";
            description.moduleName = "material_tests_program";
            description.program = 0x4d41545f54455354ull;
            description.material = 0x4d4154455249414cull;
            description.entryPoints = entries;
            description.compileSettings.target = shaderTarget;
            description.target = target;
            description.programSource = programSource;
            description.shader = shader;
            description.materialSource = materialSource;
            description.materialOutput = material;
            description.techniques = {&technique, 1};

            built = built && mt::BuildMaterialCanonicalInputs(description, base, &diagnostic) == mt::MaterialCanonicalResult::Success;
            description.graph = changedGraph;
            built = built && mt::BuildMaterialCanonicalInputs(description, changed, &diagnostic) == mt::MaterialCanonicalResult::Success;
            description.graph = reorderedGraph;
            built = built && mt::BuildMaterialCanonicalInputs(description, reordered, &diagnostic) == mt::MaterialCanonicalResult::Success;

            pipelines::BuildDescription changedPipelineRecipe = pipelineRecipe;
            changedPipelineRecipe.dynamicStates = pipelines::DynamicState::None;
            const mt::MaterialCanonicalTechnique changedTechnique{techniqueRequirement.name, pipelineSource, pipeline, changedPipelineRecipe};
            description.graph = sourceGraph;
            description.techniques = {&changedTechnique, 1};
            built = built && mt::BuildMaterialCanonicalInputs(description, pipelineChanged, &diagnostic) == mt::MaterialCanonicalResult::Success;

            description.graph = structuralGraph;
            description.techniques = {&technique, 1};
            built = built && mt::BuildMaterialCanonicalInputs(description, structuralChanged, &diagnostic) == mt::MaterialCanonicalResult::Success;

            description.graph = sourceGraph;
            description.slang = slang;
            ++description.slang.schemaVersion;
            mt::MaterialCanonicalBuildSet schemaMismatch;
            schemaMismatchResult = mt::BuildMaterialCanonicalInputs(description, schemaMismatch, nullptr);
            description.slang = slang;
            description.slang.legalStages |= shaders::StageBit(shaders::ShaderStage::Fragment);
            mt::MaterialCanonicalBuildSet stageMismatch;
            stageMismatchResult = mt::BuildMaterialCanonicalInputs(description, stageMismatch, nullptr);

            description.graph = sourceGraph;
            description.slang = slang;
            description.slang.inputs = invalidInputs;
            description.sourceName = "materials/tests/invalid_program.slang";
            description.moduleName = "material_tests_invalid_program";
            description.programSource = invalidProgramSource;
            description.shader = invalidShader;
            built = built && mt::BuildMaterialCanonicalInputs(description, invalidProgram, &diagnostic) == mt::MaterialCanonicalResult::Success;

            mt::MaterialSourceValue rejectedValues[]{sourceValues[0], sourceValues[1], sourceValues[2], sourceValues[3]};
            rejectedValues[3].nodeType = 0xdead;
            const mt::MaterialSourceGraph rejectedGraph{sourceGraph.identity, domainContract.name, rejectedValues, {&sourceOutput, 1}};
            description.graph = rejectedGraph;
            description.slang = slang;
            description.sourceName = "materials/tests/rejected_program.slang";
            description.moduleName = "material_tests_rejected_program";
            description.programSource = programSource;
            description.shader = shader;
            mt::MaterialCanonicalBuildSet rejected;
            rejectedResult = mt::BuildMaterialCanonicalInputs(description, rejected, &rejectedDiagnostic);

            valid = built && base.IsValid() && changed.IsValid() && reordered.IsValid() && pipelineChanged.IsValid() && structuralChanged.IsValid() && invalidProgram.IsValid();
        }

        bool valid = false;
        mt::MaterialCanonicalResult rejectedResult = mt::MaterialCanonicalResult::Success;
        mt::MaterialCanonicalResult schemaMismatchResult = mt::MaterialCanonicalResult::Success;
        mt::MaterialCanonicalResult stageMismatchResult = mt::MaterialCanonicalResult::Success;
        mt::MaterialCanonicalDiagnostic diagnostic;
        mt::MaterialCanonicalDiagnostic rejectedDiagnostic;
        shaders::MaterialDomainContract domainContract;
        crypto::Digest256 domainContractFingerprint;
        mt::MaterialNodeRegistry nodes;
        mt::MaterialDomainRegistry domains;
        resources::ResourceReference programSource;
        resources::ResourceReference pipelineSource;
        resources::ResourceReference materialSource;
        resources::ResourceReference invalidProgramSource;
        resources::ResourceReference shader;
        resources::ResourceReference invalidShader;
        resources::ResourceReference pipeline;
        resources::ResourceReference material;
        mt::MaterialCanonicalBuildSet base;
        mt::MaterialCanonicalBuildSet changed;
        mt::MaterialCanonicalBuildSet reordered;
        mt::MaterialCanonicalBuildSet pipelineChanged;
        mt::MaterialCanonicalBuildSet structuralChanged;
        mt::MaterialCanonicalBuildSet invalidProgram;
    };

    struct DeclaredSurfaceCanonicalFixture
    {
        explicit DeclaredSurfaceCanonicalFixture(const assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12, const shader_tools::Target shaderTarget = shader_tools::Target::D3D12Dxil) noexcept
        {
            const shaders::StageMask computeStage = shaders::StageBit(shaders::ShaderStage::Compute);
            const auto numeric =
                [](const shaders::ScalarType scalar, const u8 rows = 1, const u8 columns = 1, const u32 arrayCount = 1, const mt::MaterialIrMatrixOrder order = mt::MaterialIrMatrixOrder::None) noexcept
            {
                mt::MaterialIrType type;
                type.kind = mt::MaterialIrTypeKind::Numeric;
                type.scalarType = scalar;
                type.rows = rows;
                type.columns = columns;
                type.arrayCount = arrayCount;
                type.matrixOrder = order;
                return type;
            };

            std::array<mt::MaterialIrType, 21> valueTypes{};
            valueTypes[0] = numeric(shaders::ScalarType::Bool);
            valueTypes[1] = numeric(shaders::ScalarType::I16);
            valueTypes[2] = numeric(shaders::ScalarType::U16);
            valueTypes[3] = numeric(shaders::ScalarType::F16);
            valueTypes[4] = numeric(shaders::ScalarType::I32);
            valueTypes[5] = numeric(shaders::ScalarType::U32);
            valueTypes[6] = numeric(shaders::ScalarType::F32);
            valueTypes[7] = numeric(shaders::ScalarType::I64);
            valueTypes[8] = numeric(shaders::ScalarType::U64);
            valueTypes[9] = numeric(shaders::ScalarType::F64);
            valueTypes[10] = numeric(shaders::ScalarType::F32, 4);
            valueTypes[11] = numeric(shaders::ScalarType::F32, 2, 3, 1, mt::MaterialIrMatrixOrder::RowMajor);
            valueTypes[12] = numeric(shaders::ScalarType::F32, 3, 2, 1, mt::MaterialIrMatrixOrder::ColumnMajor);
            valueTypes[13] = numeric(shaders::ScalarType::F32, 1, 1, 3);
            const mt::MaterialIrType innerAggregateFields[]{valueTypes[10], numeric(shaders::ScalarType::F32, 2, 2, 1, mt::MaterialIrMatrixOrder::RowMajor)};
            const mt::MaterialIrAggregateFieldDescription innerAggregateDescription[]{{0x564543544f525f34ull, innerAggregateFields[0]}, {0x4d41545249585f32ull, innerAggregateFields[1]}};
            bool built = types.RegisterAggregate({0x4445434c5f494e4eull, innerAggregateDescription}, valueTypes[14]);
            const mt::MaterialIrType outerAggregateFields[]{valueTypes[14], numeric(shaders::ScalarType::F32, 3, 2, 1, mt::MaterialIrMatrixOrder::ColumnMajor)};
            const mt::MaterialIrAggregateFieldDescription outerAggregateDescription[]{{0x494e4e45525f4147ull, outerAggregateFields[0]}, {0x434f4c5f4d415433ull, outerAggregateFields[1]}};
            built = built && types.RegisterAggregate({0x4445434c5f4f5554ull, outerAggregateDescription}, valueTypes[15]);
            valueTypes[16] = valueTypes[15];
            valueTypes[16].arrayCount = 2;
            built = built && types.Freeze();
            valueTypes[17].kind = mt::MaterialIrTypeKind::Texture;
            valueTypes[17].scalarType = shaders::ScalarType::F32;
            valueTypes[17].rows = 4;
            valueTypes[17].arrayCount = 2;
            valueTypes[17].textureDimension = mt::MaterialIrTextureDimension::D2;
            valueTypes[18].kind = mt::MaterialIrTypeKind::Sampler;
            valueTypes[19].kind = mt::MaterialIrTypeKind::Buffer;
            valueTypes[19].scalarType = shaders::ScalarType::F32;
            valueTypes[19].rows = 4;
            valueTypes[19].resourceAccess = mt::MaterialIrResourceAccess::ReadWrite;
            valueTypes[19].bufferKind = mt::MaterialIrBufferKind::Structured;
            valueTypes[20].kind = mt::MaterialIrTypeKind::AccelerationStructure;

            const crypto::Digest256 nodeFingerprint = crypto::Sha256("declared-surface-node", 21);
            const u32 dynamicTypeIndices[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20};
            for (u32 index = 0; built && index < std::size(dynamicTypeIndices); ++index)
            {
                const mt::MaterialPinSchema output{1, valueTypes[dynamicTypeIndices[index]], true};
                built = nodes.Register({0x1000u + index, 1, nodeFingerprint, {}, {&output, 1}});
            }

            const auto registerExtract = [&](const u64 nodeType, const mt::MaterialIrType& inputType, const mt::MaterialIrType& outputType) noexcept
            {
                const mt::MaterialPinSchema input{1, inputType, true};
                const mt::MaterialPinSchema output{1, outputType, true};
                return nodes.Register({nodeType, 1, nodeFingerprint, {&input, 1}, {&output, 1}});
            };
            mt::MaterialIrType textureElement = valueTypes[17];
            textureElement.arrayCount = 1;
            built = built && registerExtract(0x2000, valueTypes[11], FloatType()) && registerExtract(0x2001, valueTypes[12], FloatType()) && registerExtract(0x2002, valueTypes[13], FloatType()) &&
                    registerExtract(0x2003, valueTypes[16], valueTypes[15]) && registerExtract(0x2004, valueTypes[15], valueTypes[14]) && registerExtract(0x2005, valueTypes[14], innerAggregateFields[1]) &&
                    registerExtract(0x2006, innerAggregateFields[1], FloatType()) && registerExtract(0x2007, valueTypes[17], textureElement) && nodes.Freeze();

            const u8 boolValue = 1;
            const i16 i16Value = -2;
            const u16 u16Value = 3;
            const u16 f16Value = 0x3c00u;
            const i32 i32Value = -4;
            const u32 u32Value = 5;
            const float f32Value = 0.625f;
            const i64 i64Value = -6;
            const u64 u64Value = 7;
            const double f64Value = 0.75;
            const float vectorValue[4]{1.0f, 2.0f, 3.0f, 4.0f};
            const float rowMatrixValue[6]{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
            const float columnMatrixValue[6]{7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
            const float arrayValue[3]{13.0f, 14.0f, 15.0f};
            const float aggregateArrayValue[28]{100.0f, 101.0f, 102.0f, 103.0f, 104.0f, 105.0f, 106.0f, 107.0f, 108.0f, 109.0f, 110.0f, 111.0f, 112.0f, 113.0f,
                                                114.0f, 115.0f, 116.0f, 117.0f, 118.0f, 119.0f, 120.0f, 121.0f, 122.0f, 123.0f, 124.0f, 125.0f, 126.0f, 127.0f};
            const containers::ArraySpan<const u8> defaults[]{{&boolValue, sizeof(boolValue)},
                                                             {reinterpret_cast<const u8*>(&i16Value), sizeof(i16Value)},
                                                             {reinterpret_cast<const u8*>(&u16Value), sizeof(u16Value)},
                                                             {reinterpret_cast<const u8*>(&f16Value), sizeof(f16Value)},
                                                             {reinterpret_cast<const u8*>(&i32Value), sizeof(i32Value)},
                                                             {reinterpret_cast<const u8*>(&u32Value), sizeof(u32Value)},
                                                             {reinterpret_cast<const u8*>(&f32Value), sizeof(f32Value)},
                                                             {reinterpret_cast<const u8*>(&i64Value), sizeof(i64Value)},
                                                             {reinterpret_cast<const u8*>(&u64Value), sizeof(u64Value)},
                                                             {reinterpret_cast<const u8*>(&f64Value), sizeof(f64Value)},
                                                             {reinterpret_cast<const u8*>(vectorValue), sizeof(vectorValue)},
                                                             {reinterpret_cast<const u8*>(rowMatrixValue), sizeof(rowMatrixValue)},
                                                             {reinterpret_cast<const u8*>(columnMatrixValue), sizeof(columnMatrixValue)},
                                                             {reinterpret_cast<const u8*>(arrayValue), sizeof(arrayValue)}};

            std::array<mt::MaterialSourceValue, 27> values{};
            for (u32 index = 0; index < 19; ++index)
            {
                values[index].id = index + 1u;
                values[index].node = 0x44534e0000000000ull + index;
                values[index].nodeType = 0x1000u + index;
                values[index].nodeSchemaVersion = 1;
                values[index].outputPin = 1;
                values[index].kind = mt::MaterialIrValueKind::DynamicParameter;
                values[index].type = valueTypes[dynamicTypeIndices[index]];
                values[index].legalStages = computeStage;
                values[index].semantic = 0x4453500000000000ull + index + 1u;
                if (index < 14)
                    values[index].data = defaults[index];
            }
            values[14].data = {reinterpret_cast<const u8*>(aggregateArrayValue), sizeof(aggregateArrayValue)};
            const mt::MaterialSourceOperand rowOperands[]{{1, values[11].id}};
            const mt::MaterialSourceOperand columnOperands[]{{1, values[12].id}};
            const mt::MaterialSourceOperand arrayOperands[]{{1, values[13].id}};
            const mt::MaterialSourceOperand aggregateArrayOperands[]{{1, values[14].id}};
            const mt::MaterialSourceOperand outerAggregateOperands[]{{1, 23}};
            const mt::MaterialSourceOperand innerAggregateOperands[]{{1, 24}};
            const mt::MaterialSourceOperand aggregateMatrixOperands[]{{1, 25}};
            const mt::MaterialSourceOperand textureOperands[]{{1, values[15].id}};
            const std::array<u8, 4> zeroIndex = IndexBytes(0);
            const std::array<u8, 4> oneIndex = IndexBytes(1);
            const std::array<u8, 4> matrixComponent = IndexBytes(3);
            const containers::ArraySpan<const mt::MaterialSourceOperand> extractOperands[]{
                rowOperands, columnOperands, arrayOperands, aggregateArrayOperands, outerAggregateOperands, innerAggregateOperands, aggregateMatrixOperands, textureOperands};
            const mt::MaterialIrType extractTypes[]{FloatType(), FloatType(), FloatType(), valueTypes[15], valueTypes[14], innerAggregateFields[1], FloatType(), textureElement};
            for (u32 index = 0; index < 8; ++index)
            {
                mt::MaterialSourceValue& value = values[19 + index];
                value.id = 20 + index;
                value.node = 0x4453580000000000ull + index;
                value.nodeType = 0x2000u + index;
                value.nodeSchemaVersion = 1;
                value.outputPin = 1;
                value.kind = mt::MaterialIrValueKind::Instruction;
                value.opcode = mt::MaterialIrOpcode::Extract;
                value.type = extractTypes[index];
                value.legalStages = computeStage;
                value.operands = extractOperands[index];
                value.data = (index == 3 || index == 5) ? containers::ArraySpan<const u8>{oneIndex.data(), static_cast<u32>(oneIndex.size())}
                             : index == 6               ? containers::ArraySpan<const u8>{matrixComponent.data(), static_cast<u32>(matrixComponent.size())}
                                                        : containers::ArraySpan<const u8>{zeroIndex.data(), static_cast<u32>(zeroIndex.size())};
            }

            std::array<mt::MaterialDomainOutputSchema, 20> domainOutputs{};
            std::array<mt::MaterialSlangSymbol, 20> outputSymbols{};
            std::array<mt::MaterialSourceOutput, 19> graphOutputs{};
            const char* const outputNames[]{"boolValue",      "i16Value",     "u16Value",     "f16Value",    "i32Value",          "u32Value",          "f32Value",
                                            "i64Value",       "u64Value",     "f64Value",     "vectorValue", "rowMatrixValue",    "columnMatrixValue", "arrayValue",
                                            "aggregateValue", "textureValue", "samplerValue", "bufferValue", "accelerationValue", "defaultValue"};
            const mt::MaterialIrType outputTypes[]{valueTypes[0],  valueTypes[1], valueTypes[2], valueTypes[3], valueTypes[4], valueTypes[5],  valueTypes[6],  valueTypes[7],  valueTypes[8],  valueTypes[9],
                                                   valueTypes[10], FloatType(),   FloatType(),   FloatType(),   FloatType(),   textureElement, valueTypes[18], valueTypes[19], valueTypes[20], valueTypes[10]};
            const mt::MaterialSourceValueId outputValues[]{values[0].id,  values[1].id,  values[2].id,  values[3].id,  values[4].id,  values[5].id,  values[6].id,  values[7].id,  values[8].id, values[9].id,
                                                           values[10].id, values[19].id, values[20].id, values[21].id, values[25].id, values[26].id, values[16].id, values[17].id, values[18].id};
            const float defaultColor[4]{0.1f, 0.2f, 0.3f, 1.0f};
            for (u32 index = 0; index < 20; ++index)
            {
                const u64 semantic = DeclaredOutputBase + index + 1u;
                domainOutputs[index] = {semantic, outputTypes[index], computeStage,
                                        index == 19 ? containers::ArraySpan<const u8>{reinterpret_cast<const u8*>(defaultColor), sizeof(defaultColor)} : containers::ArraySpan<const u8>{}};
                outputSymbols[index] = {semantic, outputNames[index]};
                if (index < 19)
                    graphOutputs[index] = {semantic, outputValues[index], computeStage};
            }

            built = built && ReflectDeclaredSurfaceContract(domainContract) && domainContract.requiredCapabilities == DeclaredSurfaceCapabilities &&
                    shaders::CalculateMaterialDomainFingerprint(domainContract, domainContractFingerprint) == shaders::Result::Success;
            const mt::MaterialDomainInputSchema inputSchema{0x4453494e50555431ull, U32Type(), computeStage};
            const mt::MaterialSlangSymbol inputSymbol{inputSchema.name, "seed"};
            const mt::MaterialTechniqueRequirement requirements[]{{0x4453544543483031ull, Reference("materials/tests/declared_a.mpli", mt::MaterialPipelineInputResourceType)},
                                                                  {0x4453544543483032ull, Reference("materials/tests/declared_b.mpli", mt::MaterialPipelineInputResourceType)}};
            const crypto::Digest256 implementation = crypto::Sha256("declared-surface-domain", 23);
            const mt::MaterialDomainDescriptor domainDescriptor{domainContract.name, domainContract, implementation, {&inputSchema, 1}, {domainOutputs.data(), static_cast<u32>(domainOutputs.size())},
                                                                {requirements, 2}};
            built = built && domains.Register(domainDescriptor) && domains.Freeze();

            const mt::MaterialSourceGraph graph{Reference("materials/tests/declared_surface.vmatgraph", 0x4d534752u),
                                                domainContract.name,
                                                {values.data(), static_cast<u32>(values.size())},
                                                {graphOutputs.data(), static_cast<u32>(graphOutputs.size())}};
            mt::MaterialSlangDomain slang;
            slang.stableName = "DeclaredSurface";
            slang.schemaVersion = 1;
            slang.legalStages = computeStage;
            slang.requiredCapabilities = DeclaredSurfaceCapabilities;
            slang.inputTypeName = "DeclaredSurfaceInput";
            slang.outputTypeName = "DeclaredSurfaceOutput";
            slang.parameterTypeName = "DeclaredSurfaceParameters";
            slang.resourceTypeName = "DeclaredSurfaceResources";
            slang.evaluationFunctionName = "EvaluateDeclaredSurface";
            slang.inputs = {&inputSymbol, 1};
            slang.outputs = {outputSymbols.data(), static_cast<u32>(outputSymbols.size())};
            slang.prefix = {reinterpret_cast<const u8*>(DeclaredSurfacePrefix), sizeof(DeclaredSurfacePrefix) - 1u};
            slang.suffix = {reinterpret_cast<const u8*>(DeclaredSurfaceSuffix), sizeof(DeclaredSurfaceSuffix) - 1u};

            pipelines::BuildDescription recipes[2]{};
            mt::MaterialCanonicalTechnique techniques[2]{};
            for (u32 index = 0; index < 2; ++index)
            {
                recipes[index].kind = pipelines::PipelineKind::Compute;
                recipes[index].name = requirements[index].name;
                techniques[index] = {requirements[index].name, requirements[index].pipelineTemplate,
                                     Reference(index == 0 ? "materials/tests/declared_a.vppl" : "materials/tests/declared_b.vppl", pipelines::PipelineResourceType), recipes[index]};
            }
            const mt::MaterialCanonicalResourceValue resourceValues[]{{values[15].semantic, 0, Reference("materials/tests/declared_0.vtex", textures::TextureResourceType), resources::DependencyKind::Optional},
                                                                      {values[15].semantic, 1, Reference("materials/tests/declared_1.vtex", textures::TextureResourceType), resources::DependencyKind::Soft}};
            const shader_tools::EntryPoint entries[]{{"DeclaredSurfaceCompute", shaders::ShaderStage::Compute}};
            mt::MaterialCanonicalDescription description;
            description.graph = graph;
            description.frontend = {&nodes, &domains, &types};
            description.slang = slang;
            description.sourceName = "materials/tests/declared_surface.slang";
            description.moduleName = "material_declared_surface";
            description.program = 0x445350524f475241ull;
            description.material = 0x44534d415445524cull;
            description.entryPoints = entries;
            description.compileSettings.target = shaderTarget;
            description.target = target;
            description.programSource = Reference("materials/tests/declared_surface.mpgi", mt::MaterialProgramInputResourceType);
            description.shader = Reference("materials/tests/declared_surface.vshader", shaders::ShaderResourceType);
            description.materialSource = Reference("materials/tests/declared_surface.mvli", mt::MaterialValueInputResourceType);
            description.materialOutput = Reference("materials/tests/declared_surface.vmat", materials::MaterialResourceType);
            description.techniques = techniques;
            description.resources = resourceValues;
            built = built && mt::BuildMaterialCanonicalInputs(description, build, &diagnostic) == mt::MaterialCanonicalResult::Success;

            mt::MaterialCanonicalBuildSet rejected;
            mt::MaterialCanonicalResourceValue invalidBuffer{values[17].semantic, 0, Reference("materials/tests/not_a_buffer.vtex", textures::TextureResourceType), resources::DependencyKind::Optional};
            description.resources = {&invalidBuffer, 1};
            invalidBufferResult = mt::BuildMaterialCanonicalInputs(description, rejected, nullptr);
            description.resources = resourceValues;
            description.offline.targetCapabilities[static_cast<u32>(assets::TargetPlatform::WindowsD3D12)] &= ~shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::AccelerationStructure);
            unsupportedResult = mt::BuildMaterialCanonicalInputs(description, rejected, nullptr);
            valid = built && build.IsValid() && build.Pipelines().Count() == 2;
        }

        bool valid = false;
        mt::MaterialCanonicalResult invalidBufferResult = mt::MaterialCanonicalResult::Success;
        mt::MaterialCanonicalResult unsupportedResult = mt::MaterialCanonicalResult::Success;
        mt::MaterialCanonicalDiagnostic diagnostic;
        shaders::MaterialDomainContract domainContract;
        crypto::Digest256 domainContractFingerprint;
        mt::MaterialIrTypeRegistry types;
        mt::MaterialNodeRegistry nodes;
        mt::MaterialDomainRegistry domains;
        mt::MaterialCanonicalBuildSet build;
    };

    struct ArtifactFixture
    {
        explicit ArtifactFixture(const assets::TargetPlatform targetPlatform = assets::TargetPlatform::WindowsD3D12, const shader_tools::Target shaderTarget = shader_tools::Target::D3D12Dxil) noexcept
            : canonical(targetPlatform, shaderTarget), target(targetPlatform), programBytes(memory::pools::Assets::GetInstance()), pipelineBytes(memory::pools::Assets::GetInstance()),
              materialBytes(memory::pools::Assets::GetInstance()), structuralProgramBytes(memory::pools::Assets::GetInstance()), changedPipelineBytes(memory::pools::Assets::GetInstance()),
              changedMaterialBytes(memory::pools::Assets::GetInstance()), invalidMaterialBytes(memory::pools::Assets::GetInstance()), invalidProgramBytes(memory::pools::Assets::GetInstance())
        {
            programSource = Reference("materials/tests/program.mpgi", mt::MaterialProgramInputResourceType);
            pipelineSource = Reference("materials/tests/pipeline.mpli", mt::MaterialPipelineInputResourceType);
            materialSource = Reference("materials/tests/material.mvli", mt::MaterialValueInputResourceType);
            invalidProgramSource = Reference("materials/tests/invalid_program.mpgi", mt::MaterialProgramInputResourceType);
            shader = Reference("materials/tests/program.vshader", shaders::ShaderResourceType);
            invalidShader = Reference("materials/tests/invalid_program.vshader", shaders::ShaderResourceType);
            pipeline = Reference("materials/tests/program.vppl", pipelines::PipelineResourceType);
            material = Reference("materials/tests/program.vmat", materials::MaterialResourceType);

            valid = canonical.valid && canonical.base.Pipelines().Count() == 1 && canonical.changed.Pipelines().Count() == 1 && CopyBytes(canonical.base.Program().source.content, programBytes) &&
                    CopyBytes(canonical.base.Pipelines()[0].source.content, pipelineBytes) && CopyBytes(canonical.base.Material().source.content, materialBytes) &&
                    CopyBytes(canonical.structuralChanged.Program().source.content, structuralProgramBytes) && CopyBytes(canonical.pipelineChanged.Pipelines()[0].source.content, changedPipelineBytes) &&
                    CopyBytes(canonical.changed.Material().source.content, changedMaterialBytes) && CopyBytes(canonical.invalidProgram.Program().source.content, invalidProgramBytes);

            const float roughness = 0.5f;
            const u64 parameterType = shaders::HashInterfaceName("ComputeMaterialParameters");
            char roughnessField[32]{};
            std::snprintf(roughnessField, sizeof(roughnessField), "p_%016llx", static_cast<unsigned long long>(RoughnessSemantic));
            const u64 roughnessName = shaders::HashInterfaceChildName(parameterType, roughnessField);
            const materials::TechniqueBuildRecord techniques[]{{0x434f4d505554455full, pipeline, nullptr}};
            const mt::MaterialLogicalConstantValue invalidConstants[]{{roughnessName + 1u, shaders::ScalarType::F32, 1, 1, false, 1, {reinterpret_cast<const u8*>(&roughness), sizeof(roughness)}}};
            const mt::MaterialValueInput invalidInput{0x4d4154455249414cull, shader, techniques, invalidConstants, {}};
            valid = valid && mt::EncodeMaterialValueInput(invalidInput, invalidMaterialBytes) == mt::MaterialInputResult::Success;
            RefreshRequests(materialBytes);
        }

        void RefreshRequests(const containers::DynamicArray<u8>& selectedMaterialBytes) noexcept
        {
            RefreshRequests(programBytes, pipelineBytes, selectedMaterialBytes);
        }

        void RefreshRequests(const containers::DynamicArray<u8>& selectedProgramBytes, const containers::DynamicArray<u8>& selectedPipelineBytes,
                             const containers::DynamicArray<u8>& selectedMaterialBytes) noexcept
        {
            programRequest = {{programSource, selectedProgramBytes, {}}, shader, target, {}};
            invalidProgramRequest = {{invalidProgramSource, invalidProgramBytes, {}}, invalidShader, target, {}};
            pipelineRequest = {{pipelineSource, selectedPipelineBytes, {}}, pipeline, target, {}};
            materialRequest = {{materialSource, selectedMaterialBytes, {}}, material, target, {}};
        }

        static bool Resolve(const assets::BuildDependency& dependency, assets::BuildRequest& request, void* const userData) noexcept
        {
            const ArtifactFixture& fixture = *static_cast<const ArtifactFixture*>(userData);
            if (dependency.identity == fixture.shader)
                request = fixture.programRequest;
            else if (dependency.identity == fixture.pipeline)
                request = fixture.pipelineRequest;
            else
                return false;
            return true;
        }

        static bool ResolveOutput(const resources::ResourceReference output, assets::BuildRequest& request, void* const userData) noexcept
        {
            const ArtifactFixture& fixture = *static_cast<const ArtifactFixture*>(userData);
            if (output == fixture.shader)
                request = fixture.programRequest;
            else if (output == fixture.pipeline)
                request = fixture.pipelineRequest;
            else if (output == fixture.material)
                request = fixture.materialRequest;
            else
                return false;
            return true;
        }

        CanonicalFixture canonical;
        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        bool valid = false;
        resources::ResourceReference programSource;
        resources::ResourceReference pipelineSource;
        resources::ResourceReference materialSource;
        resources::ResourceReference invalidProgramSource;
        resources::ResourceReference shader;
        resources::ResourceReference invalidShader;
        resources::ResourceReference pipeline;
        resources::ResourceReference material;
        containers::DynamicArray<u8> programBytes;
        containers::DynamicArray<u8> pipelineBytes;
        containers::DynamicArray<u8> materialBytes;
        containers::DynamicArray<u8> structuralProgramBytes;
        containers::DynamicArray<u8> changedPipelineBytes;
        containers::DynamicArray<u8> changedMaterialBytes;
        containers::DynamicArray<u8> invalidMaterialBytes;
        containers::DynamicArray<u8> invalidProgramBytes;
        assets::BuildRequest programRequest;
        assets::BuildRequest invalidProgramRequest;
        assets::BuildRequest pipelineRequest;
        assets::BuildRequest materialRequest;
    };

    constexpr resources::ResourceTypeId RecookTextureSourceType = 0x52545849u;

    struct RecookTextureFixture
    {
        resources::ResourceReference source = Reference("materials/tests/declared_0.texture_source", RecookTextureSourceType);
        resources::ResourceReference output = Reference("materials/tests/declared_0.vtex", textures::TextureResourceType);
        u8 value = 1;
        bool available = true;
        bool block = false;
        concurrency::Atomic<bool> started;

        [[nodiscard]] assets::BuildRequest Request() const noexcept
        {
            return {{source, {&value, 1}, {}}, output, assets::TargetPlatform::WindowsD3D12, {}};
        }

        static bool Discover(const assets::BuildRequest& request, assets::DependencyCollector&, void* const userData) noexcept
        {
            const auto& fixture = *static_cast<const RecookTextureFixture*>(userData);
            return request.source.identity == fixture.source && request.output == fixture.output;
        }

        static bool Compile(const assets::CompileContext& context, assets::ArtifactWriter& writer, void* const userData) noexcept
        {
            auto& fixture = *static_cast<RecookTextureFixture*>(userData);
            fixture.started.SetValue(true);
            while (fixture.block && !context.IsCancellationRequested())
                concurrency::YieldCurrentThread();
            if (context.IsCancellationRequested())
                return false;
            const u8 bytes[]{context.request.source.content[0], 0x54, 0x45, 0x58};
            return writer.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4, bytes, sizeof(bytes)) == assets::Result::Success;
        }

        static bool Estimate(const assets::BuildRequest&, containers::ArraySpan<const assets::BuildDependency>, assets::BuildResourceEstimate& estimate, void*) noexcept
        {
            estimate = {4, 4};
            return true;
        }
    };

    struct DeclaredSurfaceArtifactFixture
    {
        explicit DeclaredSurfaceArtifactFixture(const assets::TargetPlatform targetPlatform = assets::TargetPlatform::WindowsD3D12,
                                                const shader_tools::Target shaderTarget = shader_tools::Target::D3D12Dxil) noexcept
            : canonical(targetPlatform, shaderTarget), target(targetPlatform)
        {
            shader = Reference("materials/tests/declared_surface.vshader", shaders::ShaderResourceType);
            pipelines[0] = Reference("materials/tests/declared_a.vppl", pipelines::PipelineResourceType);
            pipelines[1] = Reference("materials/tests/declared_b.vppl", pipelines::PipelineResourceType);
            material = Reference("materials/tests/declared_surface.vmat", materials::MaterialResourceType);
            valid = canonical.valid && canonical.build.Pipelines().Count() == 2;
            if (!valid)
                return;
            programRequest = canonical.build.Program();
            pipelineRequests[0] = canonical.build.Pipelines()[0];
            pipelineRequests[1] = canonical.build.Pipelines()[1];
            materialRequest = canonical.build.Material();
            valid = programRequest.output == shader && pipelineRequests[0].output == pipelines[0] && pipelineRequests[1].output == pipelines[1] && materialRequest.output == material;
        }

        static bool Resolve(const assets::BuildDependency& dependency, assets::BuildRequest& request, void* const userData) noexcept
        {
            const auto& fixture = *static_cast<const DeclaredSurfaceArtifactFixture*>(userData);
            if (dependency.identity == fixture.shader)
                request = fixture.programRequest;
            else if (dependency.identity == fixture.pipelines[0])
                request = fixture.pipelineRequests[0];
            else if (dependency.identity == fixture.pipelines[1])
                request = fixture.pipelineRequests[1];
            else if (fixture.texture != nullptr && fixture.texture->available && dependency.identity == fixture.texture->output)
                request = fixture.texture->Request();
            else
                return false;
            return true;
        }

        static bool ResolveOutput(const resources::ResourceReference output, assets::BuildRequest& request, void* const userData) noexcept
        {
            const auto& fixture = *static_cast<const DeclaredSurfaceArtifactFixture*>(userData);
            if (output == fixture.shader)
                request = fixture.programRequest;
            else if (output == fixture.pipelines[0])
                request = fixture.pipelineRequests[0];
            else if (output == fixture.pipelines[1])
                request = fixture.pipelineRequests[1];
            else if (output == fixture.material)
                request = fixture.materialRequest;
            else if (fixture.texture != nullptr && output == fixture.texture->output)
                request = fixture.texture->Request();
            else
                return false;
            return true;
        }

        DeclaredSurfaceCanonicalFixture canonical;
        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        bool valid = false;
        resources::ResourceReference shader;
        std::array<resources::ResourceReference, 2> pipelines;
        resources::ResourceReference material;
        assets::BuildRequest programRequest;
        std::array<assets::BuildRequest, 2> pipelineRequests;
        assets::BuildRequest materialRequest;
        RecookTextureFixture* texture = nullptr;
    };

    struct PackageProofFixture
    {
        const ArtifactFixture* material = nullptr;
        assets::DerivedDataPackageArtifactReader* artifacts = nullptr;
        bool corruptArtifactSize = false;

        static bool ResolvePath(const resources::ResourceReference resource, char* const destination, const usize capacity, usize& written, void* const userData) noexcept
        {
            const auto& fixture = *static_cast<const PackageProofFixture*>(userData);
            const char* path = nullptr;
            if (resource == fixture.material->shader)
                path = "materials/tests/program.vshader";
            else if (resource == fixture.material->pipeline)
                path = "materials/tests/program.vppl";
            else if (resource == fixture.material->material)
                path = "materials/tests/program.vmat";
            if (path == nullptr)
                return false;
            const usize length = std::strlen(path);
            if (length > capacity)
                return false;
            if (length != 0)
                std::memcpy(destination, path, length);
            written = length;
            return true;
        }

        static bool ReadArtifact(const assets::ArtifactSetKey origin, const assets::IndexedArtifact& artifact, containers::DynamicArray<u8>& bytes, void* const userData) noexcept
        {
            auto& fixture = *static_cast<PackageProofFixture*>(userData);
            if (fixture.artifacts == nullptr || !assets::DerivedDataPackageArtifactReader::ReadCallback(origin, artifact, bytes, fixture.artifacts))
                return false;
            if (fixture.corruptArtifactSize)
                bytes.PushBack(0xffu);
            return true;
        }
    };

    struct DeclaredSurfacePackageProofFixture
    {
        const DeclaredSurfaceArtifactFixture* material = nullptr;
        assets::DerivedDataPackageArtifactReader* artifacts = nullptr;

        static bool ResolvePath(const resources::ResourceReference resource, char* const destination, const usize capacity, usize& written, void* const userData) noexcept
        {
            const auto& fixture = *static_cast<const DeclaredSurfacePackageProofFixture*>(userData);
            const char* path = nullptr;
            if (resource == fixture.material->shader)
                path = "materials/tests/declared_surface.vshader";
            else if (resource == fixture.material->pipelines[0])
                path = "materials/tests/declared_a.vppl";
            else if (resource == fixture.material->pipelines[1])
                path = "materials/tests/declared_b.vppl";
            else if (resource == fixture.material->material)
                path = "materials/tests/declared_surface.vmat";
            if (path == nullptr)
                return false;
            const usize length = std::strlen(path);
            if (length > capacity)
                return false;
            if (length != 0)
                std::memcpy(destination, path, length);
            written = length;
            return true;
        }

        static bool ReadArtifact(const assets::ArtifactSetKey origin, const assets::IndexedArtifact& artifact, containers::DynamicArray<u8>& bytes, void* const userData) noexcept
        {
            auto& fixture = *static_cast<DeclaredSurfacePackageProofFixture*>(userData);
            return fixture.artifacts != nullptr && assets::DerivedDataPackageArtifactReader::ReadCallback(origin, artifact, bytes, fixture.artifacts);
        }
    };

    bool ValidateDeclaredSurfaceArtifacts(const DeclaredSurfaceArtifactFixture& fixture, const assets::BuildOutput& shaderOutput, const std::array<assets::BuildOutput, 2>& pipelineOutputs,
                                          const assets::BuildOutput& materialOutput, const shaders::NativeFormat expectedFormat = shaders::NativeFormat::Dxil) noexcept
    {
        if (shaderOutput.artifacts.Size() != 1 || pipelineOutputs[0].artifacts.Size() != 1 || pipelineOutputs[1].artifacts.Size() != 1 || materialOutput.artifacts.Size() != 1)
            return false;
        filesystem::MemoryFileReader shaderReader(shaderOutput.artifacts[0].bytes, 0);
        shaders::ShaderFile shader;
        filesystem::MemoryFileReader materialReader(materialOutput.artifacts[0].bytes, 0);
        materials::MaterialFile material;
        if (shader.Open(shaderReader) != shaders::Result::Success || !shader.HasMaterialContract() || material.Open(materialReader) != materials::Result::Success || material.GetShader() != fixture.shader)
            return false;
        if (shader.GetStages().Size() != 1 || shader.GetStages()[0].format != expectedFormat || shader.GetBytecode(shader.GetStages()[0]).Empty())
            return false;
        for (u32 index = 0; index < 2; ++index)
        {
            filesystem::MemoryFileReader pipelineReader(pipelineOutputs[index].artifacts[0].bytes, 0);
            pipelines::PipelineFile pipeline;
            if (pipeline.Open(pipelineReader) != pipelines::Result::Success || pipelines::ValidateShaderCompatibility(pipeline, shader) != pipelines::Result::Success)
                return false;
        }

        const shaders::MaterialContract* const contract = shader.GetMaterialContract();
        if (contract == nullptr || contract->domain.requiredCapabilities != DeclaredSurfaceCapabilities || shader.GetMaterialParameters().Size() != 17 || shader.GetMaterialResources().Size() != 5 ||
            material.GetParameters().Size() != shader.GetMaterialParameters().Size() || material.GetResourceParameters().Size() != shader.GetMaterialResources().Size() || material.GetTechniques().Size() != 2 ||
            material.GetTechniques()[0].pipeline != fixture.pipelines[0] || material.GetTechniques()[1].pipeline != fixture.pipelines[1] ||
            material.GetMaterialDomainFingerprint() != contract->domainFingerprint || material.GetMaterialLayoutFingerprint() != contract->layoutFingerprint)
            return false;

        u32 scalarMask = 0;
        bool boolPacked = false;
        bool fixedArrayPacked = false;
        bool rowMatrixPacked = false;
        bool columnMatrixPacked = false;
        bool aggregateVectorArrayPacked = false;
        bool aggregateRowMatrixArrayPacked = false;
        bool aggregateColumnMatrixArrayPacked = false;
        const containers::ArraySpan<const u8> parameterData = material.GetParameterData();
        for (const materials::ParameterRecord& parameter : material.GetParameters())
        {
            scalarMask |= 1u << static_cast<u32>(parameter.scalarType);
            if (parameter.byteOffset > parameterData.Size() || parameter.byteSize > parameterData.Size() - parameter.byteOffset)
                return false;
            const u8* const data = parameterData.Data() + parameter.byteOffset;
            if (parameter.scalarType == shaders::ScalarType::Bool && parameter.rows == 1 && parameter.columns == 1)
            {
                u32 value = 0;
                if (parameter.byteSize != sizeof(value))
                    return false;
                std::memcpy(&value, data, sizeof(value));
                boolPacked = value == 1;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 1 && parameter.columns == 1 && parameter.arrayStride != 0 &&
                parameter.byteSize >= parameter.arrayStride * 2u + sizeof(float))
            {
                float values[3]{};
                for (u32 index = 0; index < 3; ++index)
                    std::memcpy(&values[index], data + index * parameter.arrayStride, sizeof(float));
                fixedArrayPacked = values[0] == 13.0f && values[1] == 14.0f && values[2] == 15.0f;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 2 && parameter.columns == 3 && parameter.rowMajor && parameter.arrayStride == 0)
            {
                float first = 0;
                float last = 0;
                std::memcpy(&first, data, sizeof(float));
                std::memcpy(&last, data + parameter.matrixStride + 2u * sizeof(float), sizeof(float));
                rowMatrixPacked = first == 1.0f && last == 6.0f;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 3 && parameter.columns == 2 && !parameter.rowMajor && parameter.arrayStride == 0)
            {
                float first = 0;
                float last = 0;
                std::memcpy(&first, data, sizeof(float));
                std::memcpy(&last, data + parameter.matrixStride + 2u * sizeof(float), sizeof(float));
                columnMatrixPacked = first == 7.0f && last == 12.0f;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 1 && parameter.columns == 4 && parameter.arrayStride != 0)
            {
                float first = 0;
                float second = 0;
                std::memcpy(&first, data, sizeof(float));
                std::memcpy(&second, data + parameter.arrayStride, sizeof(float));
                aggregateVectorArrayPacked = first == 100.0f && second == 114.0f;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 2 && parameter.columns == 2 && parameter.rowMajor && parameter.arrayStride != 0)
            {
                float first = 0;
                float secondLast = 0;
                std::memcpy(&first, data, sizeof(float));
                std::memcpy(&secondLast, data + parameter.arrayStride + parameter.matrixStride + sizeof(float), sizeof(float));
                aggregateRowMatrixArrayPacked = first == 104.0f && secondLast == 121.0f;
            }
            if (parameter.scalarType == shaders::ScalarType::F32 && parameter.rows == 3 && parameter.columns == 2 && !parameter.rowMajor && parameter.arrayStride != 0)
            {
                float first = 0;
                float secondLast = 0;
                std::memcpy(&first, data, sizeof(float));
                std::memcpy(&secondLast, data + parameter.arrayStride + parameter.matrixStride + 2u * sizeof(float), sizeof(float));
                aggregateColumnMatrixArrayPacked = first == 108.0f && secondLast == 127.0f;
            }
        }

        u32 textureCount = 0;
        u32 samplerCount = 0;
        u32 bufferCount = 0;
        u32 accelerationCount = 0;
        bool optionalTexture = false;
        bool softTexture = false;
        const resources::ResourceReference texture0 = Reference("materials/tests/declared_0.vtex", textures::TextureResourceType);
        const resources::ResourceReference texture1 = Reference("materials/tests/declared_1.vtex", textures::TextureResourceType);
        for (const materials::ResourceParameterRecord& resource : material.GetResourceParameters())
        {
            switch (resource.kind)
            {
            case materials::ResourceParameterKind::Texture:
                ++textureCount;
                optionalTexture = optionalTexture || (resource.arrayIndex == 0 && resource.resource == texture0 && resource.dependency == resources::DependencyKind::Optional);
                softTexture = softTexture || (resource.arrayIndex == 1 && resource.resource == texture1 && resource.dependency == resources::DependencyKind::Soft);
                break;
            case materials::ResourceParameterKind::Sampler:
                ++samplerCount;
                break;
            case materials::ResourceParameterKind::Buffer:
                ++bufferCount;
                break;
            case materials::ResourceParameterKind::AccelerationStructure:
                ++accelerationCount;
                break;
            }
        }
        return scalarMask == ((1u << 10u) - 1u) && boolPacked && fixedArrayPacked && rowMatrixPacked && columnMatrixPacked && aggregateVectorArrayPacked && aggregateRowMatrixArrayPacked &&
               aggregateColumnMatrixArrayPacked && textureCount == 2 && samplerCount == 1 && bufferCount == 1 && accelerationCount == 1 && optionalTexture && softTexture;
    }

    bool AppliedRevision(const u64 revision, void* const userData) noexcept
    {
        *static_cast<u64*>(userData) = revision;
        return true;
    }

    bool RejectAppliedRevision(const u64 revision, void* const userData) noexcept
    {
        *static_cast<u64*>(userData) = revision;
        return false;
    }
} // namespace

int main()
{
    using namespace vanguard;
    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "materialToolsTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(vanguard::io::Initialize(), "I/O initialization");
    const filesystem::AbsolutePath working = filesystem::paths::GetCurrentWorkingDirectory();
    const filesystem::AbsolutePath looseProofRoot = working.AddDirPath("vanguard_material_tools_loose_tests");
    Check(filesystem::Initialize({working, working, looseProofRoot}), "filesystem initialization");
    filesystem::Manager& fileManager = filesystem::GetManager();
    DeleteLooseProofFiles(fileManager, looseProofRoot);
    Check(fileManager.CreatePath(looseProofRoot), "loose material proof root creation");
    Check(jobs::Initialize(jobs::ToolConfig()), "jobs initialization");

    BeginSuite("IR and frontend");

    crypto::Digest256 first;
    crypto::Digest256 reordered;
    Check(BuildSimple(false, first) == mt::MaterialIrResult::Success && BuildSimple(true, reordered) == mt::MaterialIrResult::Success && first == reordered,
          "equivalent graph identity ignores authored constant creation order");

    const shaders::StageMask fragmentStage = shaders::StageBit(shaders::ShaderStage::Fragment);
    const mt::MaterialIrType uintType = U32Type();
    mt::MaterialIrBuilder foldBuilder;
    foldBuilder.Reset(crypto::Sha256("FoldDomain", 10));
    mt::MaterialIrValueBuildDescription foldValue;
    foldValue.kind = mt::MaterialIrValueKind::Constant;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    const mt::MaterialIrScalarConstant encodedTwo = mt::EncodeMaterialU32(2);
    const mt::MaterialIrScalarConstant encodedThree = mt::EncodeMaterialU32(3);
    foldValue.data = {encodedTwo.bytes, encodedTwo.size};
    mt::MaterialIrValueId two = 0;
    mt::MaterialIrValueId three = 0;
    Check(foldBuilder.AddValue(foldValue, two) == mt::MaterialIrResult::Success, "integer folding first constant fixture");
    foldValue.data = {encodedThree.bytes, encodedThree.size};
    Check(foldBuilder.AddValue(foldValue, three) == mt::MaterialIrResult::Success, "integer folding second constant fixture");
    const mt::MaterialIrValueId addOperands[]{two, three};
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Instruction;
    foldValue.opcode = mt::MaterialIrOpcode::Add;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    foldValue.operands = addOperands;
    mt::MaterialIrValueId sum = 0;
    Check(foldBuilder.AddValue(foldValue, sum) == mt::MaterialIrResult::Success && foldBuilder.AddOutput({0x1000, sum, fragmentStage}) == mt::MaterialIrResult::Success, "integer folding operation fixture");
    mt::MaterialIrModule foldedModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> foldDiagnostics(memory::pools::Tools::GetInstance());
    Check(foldBuilder.Finalize(foldedModule, foldDiagnostics) == mt::MaterialIrResult::Success && foldedModule.GetValues().Count() == 1 &&
              foldedModule.GetValues()[0].kind == mt::MaterialIrValueKind::Constant && ReadU32(foldedModule.GetData(), foldedModule.GetValues()[0]) == 5,
          "target-stable integer arithmetic folds to one canonical reachable constant");

    mt::MaterialIrBuilder structuralFoldBuilder;
    structuralFoldBuilder.Reset(crypto::Sha256("StructuralFoldDomain", 20));
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Constant;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    foldValue.data = {encodedTwo.bytes, encodedTwo.size};
    mt::MaterialIrValueId structuralTwo = 0;
    mt::MaterialIrValueId structuralThree = 0;
    Check(structuralFoldBuilder.AddValue(foldValue, structuralTwo) == mt::MaterialIrResult::Success, "structural folding first constant fixture");
    foldValue.data = {encodedThree.bytes, encodedThree.size};
    Check(structuralFoldBuilder.AddValue(foldValue, structuralThree) == mt::MaterialIrResult::Success, "structural folding second constant fixture");
    mt::MaterialIrType uint2Type = uintType;
    uint2Type.rows = 2;
    const mt::MaterialIrValueId constructOperands[]{structuralTwo, structuralThree};
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Instruction;
    foldValue.opcode = mt::MaterialIrOpcode::Construct;
    foldValue.type = uint2Type;
    foldValue.legalStages = fragmentStage;
    foldValue.operands = constructOperands;
    mt::MaterialIrValueId constructedUint2 = 0;
    Check(structuralFoldBuilder.AddValue(foldValue, constructedUint2) == mt::MaterialIrResult::Success, "structural folding construct fixture");
    const std::array<u8, 4> secondComponent = IndexBytes(1);
    const mt::MaterialIrValueId structuralExtractOperands[]{constructedUint2};
    foldValue.opcode = mt::MaterialIrOpcode::Extract;
    foldValue.type = uintType;
    foldValue.operands = structuralExtractOperands;
    foldValue.data = {secondComponent.data(), static_cast<u32>(secondComponent.size())};
    mt::MaterialIrValueId extractedThree = 0;
    Check(structuralFoldBuilder.AddValue(foldValue, extractedThree) == mt::MaterialIrResult::Success, "structural folding extract fixture");
    const mt::MaterialIrValueId compareOperands[]{extractedThree, structuralThree};
    const u8 equalPredicate = static_cast<u8>(mt::MaterialIrComparePredicate::Equal);
    foldValue.opcode = mt::MaterialIrOpcode::Compare;
    foldValue.type = BoolType();
    foldValue.operands = compareOperands;
    foldValue.data = {&equalPredicate, 1};
    mt::MaterialIrValueId comparison = 0;
    Check(structuralFoldBuilder.AddValue(foldValue, comparison) == mt::MaterialIrResult::Success && structuralFoldBuilder.AddOutput({0x1000, comparison, fragmentStage}) == mt::MaterialIrResult::Success,
          "structural folding compare fixture");
    mt::MaterialIrModule structuralFoldModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> structuralFoldDiagnostics(memory::pools::Tools::GetInstance());
    Check(structuralFoldBuilder.Finalize(structuralFoldModule, structuralFoldDiagnostics) == mt::MaterialIrResult::Success && structuralFoldModule.GetValues().Count() == 1 &&
              structuralFoldModule.GetValues()[0].kind == mt::MaterialIrValueKind::Constant && structuralFoldModule.GetData()[structuralFoldModule.GetValues()[0].dataOffset] == 1,
          "packed numeric construct, extract, and integer comparison fold transitively");

    mt::MaterialIrBuilder floatFoldBuilder;
    floatFoldBuilder.Reset(crypto::Sha256("FloatFoldDomain", 15));
    const mt::MaterialIrScalarConstant floatOne = mt::EncodeMaterialF32(1.0f);
    const mt::MaterialIrScalarConstant floatTwo = mt::EncodeMaterialF32(2.0f);
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Constant;
    foldValue.type = FloatType();
    foldValue.legalStages = fragmentStage;
    foldValue.data = {floatOne.bytes, floatOne.size};
    mt::MaterialIrValueId floatA = 0;
    mt::MaterialIrValueId floatB = 0;
    Check(floatFoldBuilder.AddValue(foldValue, floatA) == mt::MaterialIrResult::Success, "conservative float fold first fixture");
    foldValue.data = {floatTwo.bytes, floatTwo.size};
    Check(floatFoldBuilder.AddValue(foldValue, floatB) == mt::MaterialIrResult::Success, "conservative float fold second fixture");
    const mt::MaterialIrValueId floatAddOperands[]{floatA, floatB};
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Instruction;
    foldValue.opcode = mt::MaterialIrOpcode::Add;
    foldValue.type = FloatType();
    foldValue.legalStages = fragmentStage;
    foldValue.operands = floatAddOperands;
    mt::MaterialIrValueId floatSum = 0;
    Check(floatFoldBuilder.AddValue(foldValue, floatSum) == mt::MaterialIrResult::Success && floatFoldBuilder.AddOutput({0x1000, floatSum, fragmentStage}) == mt::MaterialIrResult::Success,
          "conservative float fold operation fixture");
    mt::MaterialIrModule floatFoldModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> floatFoldDiagnostics(memory::pools::Tools::GetInstance());
    Check(floatFoldBuilder.Finalize(floatFoldModule, floatFoldDiagnostics) == mt::MaterialIrResult::Success &&
              floatFoldModule.GetValues()[floatFoldModule.GetOutputs()[0].value].kind == mt::MaterialIrValueKind::Instruction,
          "floating arithmetic remains in IR until target semantics are known");

    mt::MaterialIrBuilder poisonBuilder;
    poisonBuilder.Reset(crypto::Sha256("PoisonDomain", 12));
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Constant;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    foldValue.data = {encodedThree.bytes, encodedThree.size};
    mt::MaterialIrValueId numerator = 0;
    mt::MaterialIrValueId zero = 0;
    Check(poisonBuilder.AddValue(foldValue, numerator) == mt::MaterialIrResult::Success, "poison numerator fixture");
    const mt::MaterialIrScalarConstant encodedZero = mt::EncodeMaterialU32(0);
    foldValue.data = {encodedZero.bytes, encodedZero.size};
    Check(poisonBuilder.AddValue(foldValue, zero) == mt::MaterialIrResult::Success, "poison zero fixture");
    const mt::MaterialIrValueId divideOperands[]{numerator, zero};
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Instruction;
    foldValue.opcode = mt::MaterialIrOpcode::Divide;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    foldValue.sourceNode = 0xfeed;
    foldValue.sourcePin = 7;
    foldValue.operands = divideOperands;
    mt::MaterialIrValueId division = 0;
    Check(poisonBuilder.AddValue(foldValue, division) == mt::MaterialIrResult::Success, "poison division fixture");
    const mt::MaterialIrValueId poisonedAddOperands[]{division, numerator};
    foldValue.opcode = mt::MaterialIrOpcode::Add;
    foldValue.sourceNode = 0xbeef;
    foldValue.sourcePin = 9;
    foldValue.operands = poisonedAddOperands;
    mt::MaterialIrValueId propagatedPoison = 0;
    Check(poisonBuilder.AddValue(foldValue, propagatedPoison) == mt::MaterialIrResult::Success && poisonBuilder.AddOutput({0x1000, propagatedPoison, fragmentStage}) == mt::MaterialIrResult::Success,
          "poison propagation fixture");
    mt::MaterialIrModule poisonModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> poisonDiagnostics(memory::pools::Tools::GetInstance());
    Check(poisonBuilder.Finalize(poisonModule, poisonDiagnostics) == mt::MaterialIrResult::Poisoned && poisonDiagnostics.Size() == 1 && poisonDiagnostics[0].sourceNode == 0xfeed &&
              poisonDiagnostics[0].sourcePin == 7,
          "integer divide-by-zero propagates through dependents and reports its originating source site once");
    Check(poisonBuilder.Finalize(poisonModule, poisonDiagnostics) == mt::MaterialIrResult::Poisoned && poisonDiagnostics.Size() == 1 && poisonDiagnostics[0].sourceNode == 0xfeed,
          "poison analysis is repeatable and does not destructively rewrite authored IR");

    mt::MaterialIrBuilder lazyPoisonBuilder;
    lazyPoisonBuilder.Reset(crypto::Sha256("LazyPoisonDomain", 16));
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Constant;
    foldValue.type = BoolType();
    foldValue.legalStages = fragmentStage;
    const mt::MaterialIrScalarConstant encodedTrue = mt::EncodeMaterialBool(true);
    foldValue.data = {encodedTrue.bytes, encodedTrue.size};
    mt::MaterialIrValueId condition = 0;
    Check(lazyPoisonBuilder.AddValue(foldValue, condition) == mt::MaterialIrResult::Success, "lazy poison condition fixture");
    foldValue.type = uintType;
    foldValue.data = {encodedThree.bytes, encodedThree.size};
    mt::MaterialIrValueId selected = 0;
    mt::MaterialIrValueId lazyNumerator = 0;
    Check(lazyPoisonBuilder.AddValue(foldValue, selected) == mt::MaterialIrResult::Success && lazyPoisonBuilder.AddValue(foldValue, lazyNumerator) == mt::MaterialIrResult::Success,
          "lazy poison selected constants fixture");
    foldValue.data = {encodedZero.bytes, encodedZero.size};
    mt::MaterialIrValueId lazyZero = 0;
    Check(lazyPoisonBuilder.AddValue(foldValue, lazyZero) == mt::MaterialIrResult::Success, "lazy poison zero fixture");
    const mt::MaterialIrValueId lazyDivideOperands[]{lazyNumerator, lazyZero};
    foldValue = {};
    foldValue.kind = mt::MaterialIrValueKind::Instruction;
    foldValue.opcode = mt::MaterialIrOpcode::Divide;
    foldValue.type = uintType;
    foldValue.legalStages = fragmentStage;
    foldValue.operands = lazyDivideOperands;
    mt::MaterialIrValueId unchosenPoison = 0;
    Check(lazyPoisonBuilder.AddValue(foldValue, unchosenPoison) == mt::MaterialIrResult::Success, "lazy poison division fixture");
    const mt::MaterialIrValueId lazySelectOperands[]{condition, selected, unchosenPoison};
    foldValue.opcode = mt::MaterialIrOpcode::Select;
    foldValue.operands = lazySelectOperands;
    mt::MaterialIrValueId lazySelect = 0;
    Check(lazyPoisonBuilder.AddValue(foldValue, lazySelect) == mt::MaterialIrResult::Success && lazyPoisonBuilder.AddOutput({0x1000, lazySelect, fragmentStage}) == mt::MaterialIrResult::Success,
          "lazy poison select fixture");
    mt::MaterialIrModule lazyPoisonModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> lazyPoisonDiagnostics(memory::pools::Tools::GetInstance());
    Check(lazyPoisonBuilder.Finalize(lazyPoisonModule, lazyPoisonDiagnostics) == mt::MaterialIrResult::Success && lazyPoisonDiagnostics.Empty() && lazyPoisonModule.GetValues().Count() == 1 &&
              ReadU32(lazyPoisonModule.GetData(), lazyPoisonModule.GetValues()[0]) == 3,
          "a constant select suppresses poison in its unchosen branch and removes that branch from finalized IR");

    const mt::MaterialIrType floatType = FloatType();
    const mt::MaterialIrType float3Type = FloatVector(3);
    const mt::MaterialIrAggregateFieldDescription surfaceFields[]{{0x10, floatType}, {0x20, float3Type}};
    mt::MaterialIrTypeRegistry typeRegistry;
    mt::MaterialIrType surfaceType;
    Check(typeRegistry.RegisterAggregate({0x53555246414345ull, surfaceFields}, surfaceType) && typeRegistry.Freeze(), "aggregate type registry freezes a complete ordered field definition");

    mt::MaterialIrBuilder aggregateBuilder;
    aggregateBuilder.Reset(crypto::Sha256("AggregateDomain", 15), &typeRegistry);
    const mt::MaterialIrScalarConstant half = mt::EncodeMaterialF32(0.5f);
    const float normal[3]{0.0f, 0.0f, 1.0f};
    mt::MaterialIrValueBuildDescription aggregateValue;
    aggregateValue.kind = mt::MaterialIrValueKind::Constant;
    aggregateValue.type = floatType;
    aggregateValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    aggregateValue.data = {half.bytes, half.size};
    mt::MaterialIrValueId roughnessValue = 0;
    Check(aggregateBuilder.AddValue(aggregateValue, roughnessValue) == mt::MaterialIrResult::Success, "aggregate scalar field fixture");
    aggregateValue.type = float3Type;
    aggregateValue.data = {reinterpret_cast<const u8*>(normal), sizeof(normal)};
    mt::MaterialIrValueId normalValue = 0;
    Check(aggregateBuilder.AddValue(aggregateValue, normalValue) == mt::MaterialIrResult::Success, "aggregate vector field fixture");
    const mt::MaterialIrValueId aggregateOperands[]{roughnessValue, normalValue};
    aggregateValue = {};
    aggregateValue.kind = mt::MaterialIrValueKind::Instruction;
    aggregateValue.opcode = mt::MaterialIrOpcode::Construct;
    aggregateValue.type = surfaceType;
    aggregateValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    aggregateValue.operands = aggregateOperands;
    mt::MaterialIrValueId constructedSurface = 0;
    Check(aggregateBuilder.AddValue(aggregateValue, constructedSurface) == mt::MaterialIrResult::Success, "aggregate construct fixture");
    const std::array<u8, 4> firstFieldIndex = IndexBytes(0);
    const mt::MaterialIrValueId extractOperands[]{constructedSurface};
    aggregateValue.opcode = mt::MaterialIrOpcode::Extract;
    aggregateValue.type = floatType;
    aggregateValue.operands = extractOperands;
    aggregateValue.data = {firstFieldIndex.data(), static_cast<u32>(firstFieldIndex.size())};
    mt::MaterialIrValueId extractedRoughness = 0;
    Check(aggregateBuilder.AddValue(aggregateValue, extractedRoughness) == mt::MaterialIrResult::Success &&
              aggregateBuilder.AddOutput({0x1000, extractedRoughness, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "aggregate extract fixture");
    mt::MaterialIrModule aggregateModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> aggregateDiagnostics(memory::pools::Tools::GetInstance());
    Check(aggregateBuilder.Finalize(aggregateModule, aggregateDiagnostics) == mt::MaterialIrResult::Success && aggregateModule.GetTypes().Count() == 3 && aggregateModule.GetTypeFields().Count() == 2 &&
              aggregateModule.GetValues()[aggregateModule.GetOutputs()[0].value].typeId != mt::InvalidMaterialIrType,
          "finalized IR owns an interned code-generation-complete aggregate type table");

    mt::MaterialIrBuilder invalidExtractBuilder;
    invalidExtractBuilder.Reset(crypto::Sha256("AggregateDomain", 15), &typeRegistry);
    mt::MaterialIrValueId invalidRoughness = 0;
    aggregateValue = {};
    aggregateValue.kind = mt::MaterialIrValueKind::Constant;
    aggregateValue.type = floatType;
    aggregateValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    aggregateValue.data = {half.bytes, half.size};
    Check(invalidExtractBuilder.AddValue(aggregateValue, invalidRoughness) == mt::MaterialIrResult::Success, "invalid aggregate scalar fixture");
    mt::MaterialIrValueId invalidNormal = 0;
    aggregateValue.type = float3Type;
    aggregateValue.data = {reinterpret_cast<const u8*>(normal), sizeof(normal)};
    Check(invalidExtractBuilder.AddValue(aggregateValue, invalidNormal) == mt::MaterialIrResult::Success, "invalid aggregate vector fixture");
    const mt::MaterialIrValueId invalidAggregateOperands[]{invalidRoughness, invalidNormal};
    mt::MaterialIrValueId invalidSurface = 0;
    aggregateValue = {};
    aggregateValue.kind = mt::MaterialIrValueKind::Instruction;
    aggregateValue.opcode = mt::MaterialIrOpcode::Construct;
    aggregateValue.type = surfaceType;
    aggregateValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    aggregateValue.operands = invalidAggregateOperands;
    Check(invalidExtractBuilder.AddValue(aggregateValue, invalidSurface) == mt::MaterialIrResult::Success, "invalid aggregate construct fixture");
    const mt::MaterialIrValueId invalidExtractOperands[]{invalidSurface};
    mt::MaterialIrValueId wrongExtract = 0;
    aggregateValue.opcode = mt::MaterialIrOpcode::Extract;
    aggregateValue.type = float3Type;
    aggregateValue.operands = invalidExtractOperands;
    aggregateValue.data = {firstFieldIndex.data(), static_cast<u32>(firstFieldIndex.size())};
    Check(invalidExtractBuilder.AddValue(aggregateValue, wrongExtract) == mt::MaterialIrResult::Success &&
              invalidExtractBuilder.AddOutput({0x1000, wrongExtract, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "wrong aggregate extract fixture");
    mt::MaterialIrModule invalidExtractModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> invalidExtractDiagnostics(memory::pools::Tools::GetInstance());
    Check(invalidExtractBuilder.Finalize(invalidExtractModule, invalidExtractDiagnostics) == mt::MaterialIrResult::InvalidOperand,
          "aggregate extraction rejects a result type that disagrees with the selected field");

    mt::MaterialIrType texture2d;
    texture2d.kind = mt::MaterialIrTypeKind::Texture;
    texture2d.scalarType = shaders::ScalarType::F32;
    texture2d.rows = 4;
    texture2d.textureDimension = mt::MaterialIrTextureDimension::D2;
    mt::MaterialIrType sampler;
    sampler.kind = mt::MaterialIrTypeKind::Sampler;
    const mt::MaterialIrType float2Type = FloatVector(2);
    mt::MaterialIrBuilder sampleBuilder;
    sampleBuilder.Reset(crypto::Sha256("TextureDomain", 13));
    mt::MaterialIrValueBuildDescription sampleValue;
    sampleValue.kind = mt::MaterialIrValueKind::DynamicParameter;
    sampleValue.type = texture2d;
    sampleValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    sampleValue.semantic = 0x10;
    mt::MaterialIrValueId textureValue = 0;
    Check(sampleBuilder.AddValue(sampleValue, textureValue) == mt::MaterialIrResult::Success, "texture sample resource fixture");
    sampleValue.type = sampler;
    sampleValue.semantic = 0x20;
    mt::MaterialIrValueId samplerValue = 0;
    Check(sampleBuilder.AddValue(sampleValue, samplerValue) == mt::MaterialIrResult::Success, "texture sample sampler fixture");
    sampleValue.kind = mt::MaterialIrValueKind::DomainInput;
    sampleValue.type = float2Type;
    sampleValue.semantic = 0x30;
    mt::MaterialIrValueId uvValue = 0;
    Check(sampleBuilder.AddValue(sampleValue, uvValue) == mt::MaterialIrResult::Success, "texture sample coordinates fixture");
    const mt::MaterialIrValueId sampleOperands[]{textureValue, samplerValue, uvValue};
    sampleValue.kind = mt::MaterialIrValueKind::Instruction;
    sampleValue.opcode = mt::MaterialIrOpcode::TextureSample;
    sampleValue.type = FloatVector(4);
    sampleValue.semantic = 0;
    sampleValue.operands = sampleOperands;
    mt::MaterialIrValueId sampledColor = 0;
    Check(sampleBuilder.AddValue(sampleValue, sampledColor) == mt::MaterialIrResult::Success &&
              sampleBuilder.AddOutput({0x1000, sampledColor, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success &&
              sampleBuilder.AddOutput({0x1001, textureValue, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "one texture material can sample a resource and forward the same descriptor through its domain output");
    mt::MaterialIrModule sampleModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> sampleDiagnostics(memory::pools::Tools::GetInstance());
    Check(sampleBuilder.Finalize(sampleModule, sampleDiagnostics) == mt::MaterialIrResult::Success, "Texture2D sampling accepts a filtering sampler, float2 coordinates, and the declared sample type");

    {
        mt::MaterialIrBuilder invalidTextureShapeBuilder;
        invalidTextureShapeBuilder.Reset(crypto::Sha256("TextureShapeDomain", 18));
        mt::MaterialIrValueBuildDescription value;
        value.kind = mt::MaterialIrValueKind::DynamicParameter;
        value.type = texture2d;
        value.type.textureDimension = mt::MaterialIrTextureDimension::D3;
        value.type.textureArrayed = true;
        value.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        value.semantic = 0x10;
        mt::MaterialIrValueId invalidTexture = 0;
        Check(invalidTextureShapeBuilder.AddValue(value, invalidTexture) == mt::MaterialIrResult::Success &&
                  invalidTextureShapeBuilder.AddOutput({0x1000, invalidTexture, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
              "invalid Texture3D array fixture");
        mt::MaterialIrModule invalidTextureShapeModule;
        containers::DynamicArray<mt::MaterialIrDiagnostic> invalidTextureShapeDiagnostics(memory::pools::Tools::GetInstance());
        Check(invalidTextureShapeBuilder.Finalize(invalidTextureShapeModule, invalidTextureShapeDiagnostics) == mt::MaterialIrResult::InvalidType, "material IR rejects the non-portable Texture3D array shape");
    }
    {
        mt::MaterialIrBuilder invalidTextureShapeBuilder;
        invalidTextureShapeBuilder.Reset(crypto::Sha256("TextureShapeDomain", 18));
        mt::MaterialIrValueBuildDescription value;
        value.kind = mt::MaterialIrValueKind::DynamicParameter;
        value.type = texture2d;
        value.type.textureDimension = mt::MaterialIrTextureDimension::D1;
        value.type.textureMultisampled = true;
        value.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        value.semantic = 0x10;
        mt::MaterialIrValueId invalidTexture = 0;
        Check(invalidTextureShapeBuilder.AddValue(value, invalidTexture) == mt::MaterialIrResult::Success &&
                  invalidTextureShapeBuilder.AddOutput({0x1000, invalidTexture, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
              "invalid multisampled Texture1D fixture");
        mt::MaterialIrModule invalidTextureShapeModule;
        containers::DynamicArray<mt::MaterialIrDiagnostic> invalidTextureShapeDiagnostics(memory::pools::Tools::GetInstance());
        Check(invalidTextureShapeBuilder.Finalize(invalidTextureShapeModule, invalidTextureShapeDiagnostics) == mt::MaterialIrResult::InvalidType, "material IR restricts multisampling to Texture2D shapes");
    }

    mt::MaterialIrBuilder invalidSampleBuilder;
    invalidSampleBuilder.Reset(crypto::Sha256("TextureDomain", 13));
    mt::MaterialIrValueId invalidTextureValue = 0;
    sampleValue = {};
    sampleValue.kind = mt::MaterialIrValueKind::DynamicParameter;
    sampleValue.type = texture2d;
    sampleValue.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    sampleValue.semantic = 0x10;
    Check(invalidSampleBuilder.AddValue(sampleValue, invalidTextureValue) == mt::MaterialIrResult::Success, "invalid sample texture fixture");
    mt::MaterialIrValueId invalidSamplerValue = 0;
    sampleValue.type = sampler;
    sampleValue.semantic = 0x20;
    Check(invalidSampleBuilder.AddValue(sampleValue, invalidSamplerValue) == mt::MaterialIrResult::Success, "invalid sample sampler fixture");
    mt::MaterialIrValueId invalidCoordinates = 0;
    sampleValue.kind = mt::MaterialIrValueKind::DomainInput;
    sampleValue.type = float3Type;
    sampleValue.semantic = 0x30;
    Check(invalidSampleBuilder.AddValue(sampleValue, invalidCoordinates) == mt::MaterialIrResult::Success, "invalid sample coordinates fixture");
    const mt::MaterialIrValueId invalidSampleOperands[]{invalidTextureValue, invalidSamplerValue, invalidCoordinates};
    mt::MaterialIrValueId invalidSample = 0;
    sampleValue.kind = mt::MaterialIrValueKind::Instruction;
    sampleValue.opcode = mt::MaterialIrOpcode::TextureSample;
    sampleValue.type = FloatVector(4);
    sampleValue.semantic = 0;
    sampleValue.operands = invalidSampleOperands;
    Check(invalidSampleBuilder.AddValue(sampleValue, invalidSample) == mt::MaterialIrResult::Success &&
              invalidSampleBuilder.AddOutput({0x1000, invalidSample, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "invalid typed texture sample fixture");
    mt::MaterialIrModule invalidSampleModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> invalidSampleDiagnostics(memory::pools::Tools::GetInstance());
    Check(invalidSampleBuilder.Finalize(invalidSampleModule, invalidSampleDiagnostics) == mt::MaterialIrResult::InvalidOperand,
          "Texture2D sampling rejects float3 coordinates instead of silently accepting an ambiguous resource operation");

    mt::MaterialIrType matrixType = FloatType();
    matrixType.rows = 2;
    matrixType.columns = 2;
    matrixType.matrixOrder = mt::MaterialIrMatrixOrder::ColumnMajor;
    mt::MaterialIrBuilder matrixBuilder;
    matrixBuilder.Reset(crypto::Sha256("MatrixDomain", 12));
    mt::MaterialIrValueBuildDescription matrixScalar;
    matrixScalar.kind = mt::MaterialIrValueKind::Constant;
    matrixScalar.type = floatType;
    matrixScalar.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    matrixScalar.data = {half.bytes, half.size};
    mt::MaterialIrValueId scalarForMatrix = 0;
    Check(matrixBuilder.AddValue(matrixScalar, scalarForMatrix) == mt::MaterialIrResult::Success, "matrix scalar fixture");
    const mt::MaterialIrValueId matrixOperands[]{scalarForMatrix, scalarForMatrix, scalarForMatrix, scalarForMatrix};
    matrixScalar.kind = mt::MaterialIrValueKind::Instruction;
    matrixScalar.opcode = mt::MaterialIrOpcode::Construct;
    matrixScalar.type = matrixType;
    matrixScalar.operands = matrixOperands;
    matrixScalar.data = {};
    mt::MaterialIrValueId matrixValue = 0;
    Check(matrixBuilder.AddValue(matrixScalar, matrixValue) == mt::MaterialIrResult::Success &&
              matrixBuilder.AddOutput({0x1000, matrixValue, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "matrix construction fixture");
    mt::MaterialIrModule matrixModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> matrixDiagnostics(memory::pools::Tools::GetInstance());
    Check(matrixBuilder.Finalize(matrixModule, matrixDiagnostics) == mt::MaterialIrResult::Success && matrixModule.GetTypes().Count() == 2,
          "matrix construction and explicit matrix order survive type interning");

    mt::MaterialIrBuilder unorderedMatrixBuilder;
    unorderedMatrixBuilder.Reset(crypto::Sha256("MatrixDomain", 12));
    matrixType.matrixOrder = mt::MaterialIrMatrixOrder::None;
    const float matrixBytes[4]{1.0f, 0.0f, 0.0f, 1.0f};
    matrixScalar = {};
    matrixScalar.kind = mt::MaterialIrValueKind::Constant;
    matrixScalar.type = matrixType;
    matrixScalar.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    matrixScalar.data = {reinterpret_cast<const u8*>(matrixBytes), sizeof(matrixBytes)};
    mt::MaterialIrValueId unorderedMatrix = 0;
    Check(unorderedMatrixBuilder.AddValue(matrixScalar, unorderedMatrix) == mt::MaterialIrResult::Success &&
              unorderedMatrixBuilder.AddOutput({0x1000, unorderedMatrix, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "unordered matrix fixture");
    mt::MaterialIrModule unorderedMatrixModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> unorderedMatrixDiagnostics(memory::pools::Tools::GetInstance());
    Check(unorderedMatrixBuilder.Finalize(unorderedMatrixModule, unorderedMatrixDiagnostics) == mt::MaterialIrResult::InvalidType, "matrix types require an explicit row-major or column-major contract");

    const shaders::StageMask computeStage = shaders::StageBit(shaders::ShaderStage::Compute);
    mt::MaterialIrType codegenMatrixType = FloatType();
    codegenMatrixType.rows = 2;
    codegenMatrixType.columns = 2;
    codegenMatrixType.matrixOrder = mt::MaterialIrMatrixOrder::ColumnMajor;
    mt::MaterialIrType codegenRowMatrixType = codegenMatrixType;
    codegenRowMatrixType.matrixOrder = mt::MaterialIrMatrixOrder::RowMajor;
    mt::MaterialIrType codegenArrayType = floatType;
    codegenArrayType.arrayCount = 3;
    const mt::MaterialIrAggregateFieldDescription codegenFields[]{{0x10, codegenMatrixType}, {0x18, codegenRowMatrixType}, {0x20, floatType}};
    mt::MaterialIrTypeRegistry codegenTypes;
    mt::MaterialIrType codegenAggregateType;
    Check(codegenTypes.RegisterAggregate({0x434f444547454eull, codegenFields}, codegenAggregateType) && codegenTypes.Freeze(), "codegen aggregate registry fixture");
    mt::MaterialIrBuilder codegenBuilder;
    codegenBuilder.Reset(crypto::Sha256("ComputeMaterial", 15), &codegenTypes);
    mt::MaterialIrValueBuildDescription codegenValue;
    codegenValue.kind = mt::MaterialIrValueKind::DynamicParameter;
    codegenValue.type = codegenAggregateType;
    codegenValue.legalStages = computeStage;
    codegenValue.semantic = 0x4147475245474154ull;
    mt::MaterialIrValueId codegenParameter = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenParameter) == mt::MaterialIrResult::Success, "aggregate codegen parameter fixture");
    codegenValue.type = codegenArrayType;
    codegenValue.semantic = 0x41525241595f5641ull;
    mt::MaterialIrValueId codegenArrayParameter = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenArrayParameter) == mt::MaterialIrResult::Success, "fixed-array codegen parameter fixture");
    const mt::MaterialIrValueId codegenExtractOperands[]{codegenParameter};
    const std::array<u8, 4> matrixFieldIndex = IndexBytes(0);
    codegenValue = {};
    codegenValue.kind = mt::MaterialIrValueKind::Instruction;
    codegenValue.opcode = mt::MaterialIrOpcode::Extract;
    codegenValue.type = codegenMatrixType;
    codegenValue.legalStages = computeStage;
    codegenValue.operands = codegenExtractOperands;
    codegenValue.data = {matrixFieldIndex.data(), static_cast<u32>(matrixFieldIndex.size())};
    mt::MaterialIrValueId codegenMatrix = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenMatrix) == mt::MaterialIrResult::Success, "aggregate matrix extraction codegen fixture");
    const std::array<u8, 4> scalarFieldIndex = IndexBytes(2);
    codegenValue.type = floatType;
    codegenValue.data = {scalarFieldIndex.data(), static_cast<u32>(scalarFieldIndex.size())};
    mt::MaterialIrValueId codegenScalar = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenScalar) == mt::MaterialIrResult::Success, "aggregate scalar extraction codegen fixture");
    const mt::MaterialIrValueId matrixExtractOperands[]{codegenMatrix};
    const std::array<u8, 4> matrixComponentIndex = IndexBytes(3);
    codegenValue.operands = matrixExtractOperands;
    codegenValue.data = {matrixComponentIndex.data(), static_cast<u32>(matrixComponentIndex.size())};
    mt::MaterialIrValueId codegenMatrixScalar = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenMatrixScalar) == mt::MaterialIrResult::Success, "matrix component extraction codegen fixture");
    const mt::MaterialIrValueId arrayExtractOperands[]{codegenArrayParameter};
    const std::array<u8, 4> arrayElementIndex = IndexBytes(2);
    codegenValue.operands = arrayExtractOperands;
    codegenValue.data = {arrayElementIndex.data(), static_cast<u32>(arrayElementIndex.size())};
    mt::MaterialIrValueId codegenArrayScalar = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenArrayScalar) == mt::MaterialIrResult::Success, "fixed-array element extraction codegen fixture");
    const mt::MaterialIrValueId codegenAddOperands[]{codegenScalar, codegenMatrixScalar};
    codegenValue.opcode = mt::MaterialIrOpcode::Add;
    codegenValue.operands = codegenAddOperands;
    codegenValue.data = {};
    mt::MaterialIrValueId codegenOutput = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenOutput) == mt::MaterialIrResult::Success, "aggregate and matrix intermediate codegen fixture");
    const mt::MaterialIrValueId codegenArrayAddOperands[]{codegenOutput, codegenArrayScalar};
    codegenValue.operands = codegenArrayAddOperands;
    mt::MaterialIrValueId codegenFinalOutput = 0;
    Check(codegenBuilder.AddValue(codegenValue, codegenFinalOutput) == mt::MaterialIrResult::Success &&
              codegenBuilder.AddOutput({ComputeOutputSemantic, codegenFinalOutput, computeStage}) == mt::MaterialIrResult::Success,
          "aggregate, matrix, and fixed-array codegen output fixture");
    mt::MaterialIrModule codegenModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> codegenDiagnostics(memory::pools::Tools::GetInstance());
    Check(codegenBuilder.Finalize(codegenModule, codegenDiagnostics) == mt::MaterialIrResult::Success, "aggregate and matrix codegen IR finalization");
    const mt::MaterialSlangSymbol codegenOutputs[]{{ComputeOutputSemantic, "value"}};
    BeginSuite("Slang generation and reflection");
    mt::MaterialSlangDomain codegenDomain;
    codegenDomain.stableName = "ComputeMaterial";
    codegenDomain.schemaVersion = 1;
    codegenDomain.legalStages = computeStage;
    codegenDomain.requiredCapabilities = 0;
    codegenDomain.inputTypeName = "ComputeMaterialInput";
    codegenDomain.outputTypeName = "ComputeMaterialOutput";
    codegenDomain.parameterTypeName = "ComputeMaterialParameters";
    codegenDomain.resourceTypeName = "ComputeMaterialResources";
    codegenDomain.evaluationFunctionName = "EvaluateComputeMaterial";
    codegenDomain.outputs = codegenOutputs;
    codegenDomain.prefix = {reinterpret_cast<const u8*>(MaterialDomainPrefix), sizeof(MaterialDomainPrefix) - 1u};
    codegenDomain.suffix = {reinterpret_cast<const u8*>(MaterialDomainSuffix), sizeof(MaterialDomainSuffix) - 1u};
    containers::DynamicArray<u8> codegenProbe(memory::pools::Tools::GetInstance());
    containers::DynamicArray<mt::MaterialGeneratedSourceRange> codegenRanges(memory::pools::Tools::GetInstance());
    const mt::MaterialSlangResult codegenGenerationResult = mt::GenerateMaterialSlangProbe(codegenModule, codegenDomain, codegenProbe, {}, &codegenRanges);
    bool validCodegenRanges = codegenRanges.Size() == codegenModule.GetValues().Size();
    for (u32 index = 0; validCodegenRanges && index < codegenRanges.Size(); ++index)
        validCodegenRanges = codegenRanges[index].value == index && codegenRanges[index].firstLine != 0 && codegenRanges[index].lastLine >= codegenRanges[index].firstLine &&
                             (index == 0 || codegenRanges[index].firstLine > codegenRanges[index - 1u].lastLine);
    Check(codegenGenerationResult == mt::MaterialSlangResult::Success && validCodegenRanges && ContainsText(codegenProbe, "VANGUARD_MATERIAL_MATRIX_STRIDE") &&
              ContainsText(codegenProbe, "VANGUARD_MATERIAL_ARRAY_STRIDE"),
          "aggregate, explicit-layout matrix, and fixed-array IR generate attributed reflection-probe Slang");
    shader_tools::ShaderCompiler codegenCompiler;
    Check(codegenCompiler.Initialize() == shader_tools::Result::Success, "aggregate codegen Slang compiler initialization");
    const shader_tools::EntryPoint codegenEntries[]{{"MaterialCompute", shaders::ShaderStage::Compute}};
    shader_tools::CompileRequest codegenRequest;
    codegenRequest.sourceName = "materials/tests/aggregate_codegen.slang";
    codegenRequest.moduleName = "material_aggregate_codegen";
    codegenRequest.source = codegenProbe;
    codegenRequest.entryPoints = codegenEntries;
    codegenRequest.settings.target = shader_tools::Target::D3D12Dxil;
    shader_tools::CompileOutput codegenReflection;
    const shader_tools::Result codegenReflectionResult = codegenCompiler.Reflect(codegenRequest, codegenReflection);
    if (codegenReflectionResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] aggregate probe: %s\n", codegenReflection.GetDiagnostics());
    bool foundColumnMatrix = false;
    bool foundRowMatrix = false;
    bool foundArray = false;
    for (const shaders::ConstantMember& member : codegenReflection.GetMaterialParameters())
    {
        foundColumnMatrix = foundColumnMatrix || (member.matrixStride != 0 && !member.rowMajor);
        foundRowMatrix = foundRowMatrix || (member.matrixStride != 0 && member.rowMajor);
        foundArray = foundArray || member.arrayStride != 0;
    }
    Check(codegenReflectionResult == shader_tools::Result::Success && codegenReflection.HasMaterialContract() && foundColumnMatrix && foundRowMatrix && foundArray,
          "Slang reflection exposes nested matrix layout and fixed-array strides");
    containers::DynamicArray<u8> codegenFinal(memory::pools::Tools::GetInstance());
    Check(mt::FinalizeMaterialSlangSource(codegenProbe, codegenReflection, codegenFinal) == mt::MaterialSlangResult::Success && !ContainsText(codegenFinal, "VANGUARD_MATERIAL_") &&
              codegenFinal.Size() < codegenProbe.Size(),
          "aggregate parameter offsets plus matrix and array strides finalize from reflection");
    codegenRequest.source = codegenFinal;
    shader_tools::CompileOutput codegenCompiled;
    const shader_tools::Result codegenCompileResult = codegenCompiler.Compile(codegenRequest, codegenCompiled);
    if (codegenCompileResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] aggregate final: %s\n", codegenCompiled.GetDiagnostics());
    Check(codegenCompileResult == shader_tools::Result::Success && codegenCompiled.HasMaterialContract() && codegenCompiled.GetMaterialParameters().Size() == codegenReflection.GetMaterialParameters().Size(),
          "final aggregate and matrix material Slang compiles with its probed layout");

    const shaders::StageMask textureStage = shaders::StageBit(shaders::ShaderStage::Fragment);
    const mt::MaterialSlangSymbol textureInputs[]{{0x30, "uv"}};
    const mt::MaterialSlangSymbol textureOutputs[]{{0x1000, "color"}, {0x1001, "textureDescriptor"}};
    mt::MaterialSlangDomain textureDomain;
    textureDomain.stableName = "TextureMaterial";
    textureDomain.schemaVersion = 1;
    textureDomain.legalStages = textureStage;
    textureDomain.requiredCapabilities = 0;
    textureDomain.inputTypeName = "TextureMaterialInput";
    textureDomain.outputTypeName = "TextureMaterialOutput";
    textureDomain.parameterTypeName = "TextureMaterialParameters";
    textureDomain.resourceTypeName = "TextureMaterialResources";
    textureDomain.evaluationFunctionName = "EvaluateTextureMaterial";
    textureDomain.inputs = textureInputs;
    textureDomain.outputs = textureOutputs;
    textureDomain.prefix = {reinterpret_cast<const u8*>(TextureMaterialDomainPrefix), sizeof(TextureMaterialDomainPrefix) - 1u};
    textureDomain.suffix = {reinterpret_cast<const u8*>(TextureMaterialDomainSuffix), sizeof(TextureMaterialDomainSuffix) - 1u};
    containers::DynamicArray<u8> textureProbe(memory::pools::Tools::GetInstance());
    Check(mt::GenerateMaterialSlangProbe(sampleModule, textureDomain, textureProbe) == mt::MaterialSlangResult::Success && ContainsText(textureProbe, "return VanguardLoadMaterialResourceDescriptor(0u)") &&
              ContainsText(textureProbe, "return VanguardLoadMaterialSamplerDescriptor(1u)") && ContainsText(textureProbe, "ResourceDescriptorHeap[NonUniformResourceIndex(v_") &&
              ContainsText(textureProbe, "SamplerDescriptorHeap[NonUniformResourceIndex(v_") && ContainsText(textureProbe, ".Sample(") && ContainsText(textureProbe, "output.textureDescriptor = v_"),
          "one evaluator keeps resource values as handles, resolves TextureSample locally, and forwards a descriptor output");
    const shader_tools::EntryPoint textureEntries[]{{"MaterialFragment", shaders::ShaderStage::Fragment}};
    shader_tools::CompileRequest textureRequest;
    textureRequest.sourceName = "materials/tests/texture_codegen.slang";
    textureRequest.moduleName = "material_texture_codegen";
    textureRequest.source = textureProbe;
    textureRequest.entryPoints = textureEntries;
    textureRequest.settings.target = shader_tools::Target::D3D12Dxil;
    shader_tools::CompileOutput textureReflection;
    const shader_tools::Result textureReflectionResult = codegenCompiler.Reflect(textureRequest, textureReflection);
    if (textureReflectionResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] texture probe: %s\n", textureReflection.GetDiagnostics());
    bool foundTextureRole = false;
    bool foundSamplerRole = false;
    for (const shaders::MaterialResourceRole& role : textureReflection.GetMaterialResources())
    {
        foundTextureRole = foundTextureRole || (role.name == shaders::HashInterfaceName("r_0000000000000010") && role.slot == 0 && role.kind == shaders::MaterialResourceKind::Texture);
        foundSamplerRole = foundSamplerRole || (role.name == shaders::HashInterfaceName("r_0000000000000020") && role.slot == 1 && role.kind == shaders::MaterialResourceKind::Sampler);
    }
    Check(textureReflectionResult == shader_tools::Result::Success && textureReflection.HasMaterialContract() && textureReflection.GetMaterialResources().Size() == 2 && foundTextureRole && foundSamplerRole,
          "reflection derives two dense typed resource roles from generated texture material Slang");
    containers::DynamicArray<u8> textureFinal(memory::pools::Tools::GetInstance());
    Check(mt::FinalizeMaterialSlangSource(textureProbe, textureReflection, textureFinal) == mt::MaterialSlangResult::Success, "resource-only material source finalizes against its reflected contract");
    textureRequest.source = textureFinal;
    shader_tools::CompileOutput textureCompiled;
    const shader_tools::Result textureCompileResult = codegenCompiler.Compile(textureRequest, textureCompiled);
    if (textureCompileResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] texture final: %s\n", textureCompiled.GetDiagnostics());
    Check(textureCompileResult == shader_tools::Result::Success && textureCompiled.GetMaterialResources().Size() == 2, "final typed texture material Slang compiles with stable reflected roles");
    textureRequest.settings.target = shader_tools::Target::VulkanSpirV;
    shader_tools::CompileOutput textureSpirV;
    const shader_tools::Result textureSpirVResult = codegenCompiler.Compile(textureRequest, textureSpirV);
    if (textureSpirVResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] texture SPIR-V final: %s\n", textureSpirV.GetDiagnostics());
    Check(textureSpirVResult == shader_tools::Result::Success && textureSpirV.GetMaterialResources().Size() == 2 && textureSpirV.GetStages().Size() == 1 &&
              textureSpirV.GetStages()[0].format == shaders::NativeFormat::SpirV,
          "final bindless texture-sampling material Slang compiles to SPIR-V with stable reflected roles");

    mt::MaterialIrBuilder wideBuilder;
    wideBuilder.Reset(crypto::Sha256("WideNumeric", 11));
    const std::array<u8, 2> i16Bytes{{0xfeu, 0xffu}};
    const std::array<u8, 2> u16Bytes{{7u, 0u}};
    const std::array<u8, 2> f16Bytes{{0u, 0x3cu}};
    const mt::MaterialIrScalarConstant i64Bytes = mt::EncodeMaterialI64(-2);
    const mt::MaterialIrScalarConstant u64Bytes = mt::EncodeMaterialU64(7);
    const mt::MaterialIrScalarConstant f64Bytes = mt::EncodeMaterialF64(1.0);
    const auto addWideValue = [&wideBuilder, textureStage](const shaders::ScalarType scalarType, const u64 semantic, const containers::ArraySpan<const u8> constantData) noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Numeric;
        type.scalarType = scalarType;
        mt::MaterialIrValueBuildDescription value;
        value.kind = mt::MaterialIrValueKind::DynamicParameter;
        value.type = type;
        value.legalStages = textureStage;
        value.semantic = semantic;
        mt::MaterialIrValueId dynamicValue = 0;
        if (wideBuilder.AddValue(value, dynamicValue) != mt::MaterialIrResult::Success)
            return false;
        value = {};
        value.kind = mt::MaterialIrValueKind::Constant;
        value.type = type;
        value.legalStages = textureStage;
        value.data = constantData;
        mt::MaterialIrValueId constantValue = 0;
        if (wideBuilder.AddValue(value, constantValue) != mt::MaterialIrResult::Success)
            return false;
        const mt::MaterialIrValueId operands[]{dynamicValue, constantValue};
        value = {};
        value.kind = mt::MaterialIrValueKind::Instruction;
        value.opcode = mt::MaterialIrOpcode::Add;
        value.type = type;
        value.legalStages = textureStage;
        value.operands = operands;
        mt::MaterialIrValueId result = 0;
        return wideBuilder.AddValue(value, result) == mt::MaterialIrResult::Success && wideBuilder.AddOutput({semantic, result, textureStage}) == mt::MaterialIrResult::Success;
    };
    Check(addWideValue(shaders::ScalarType::I16, 0x110, {i16Bytes.data(), static_cast<u32>(i16Bytes.size())}) &&
              addWideValue(shaders::ScalarType::U16, 0x120, {u16Bytes.data(), static_cast<u32>(u16Bytes.size())}) &&
              addWideValue(shaders::ScalarType::F16, 0x130, {f16Bytes.data(), static_cast<u32>(f16Bytes.size())}) && addWideValue(shaders::ScalarType::I64, 0x140, {i64Bytes.bytes, i64Bytes.size}) &&
              addWideValue(shaders::ScalarType::U64, 0x150, {u64Bytes.bytes, u64Bytes.size}) && addWideValue(shaders::ScalarType::F64, 0x160, {f64Bytes.bytes, f64Bytes.size}),
          "wide numeric codegen fixture construction");
    mt::MaterialIrModule wideModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> wideDiagnostics(memory::pools::Tools::GetInstance());
    Check(wideBuilder.Finalize(wideModule, wideDiagnostics) == mt::MaterialIrResult::Success, "16/64-bit numeric codegen IR finalization");
    const mt::MaterialSlangSymbol wideOutputs[]{{0x110, "i16Value"}, {0x120, "u16Value"}, {0x130, "f16Value"}, {0x140, "i64Value"}, {0x150, "u64Value"}, {0x160, "f64Value"}};
    mt::MaterialSlangDomain wideDomain;
    wideDomain.stableName = "WideNumeric";
    wideDomain.schemaVersion = 1;
    wideDomain.legalStages = textureStage;
    wideDomain.requiredCapabilities = shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Numeric16Bit) |
                                      shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Integer64Bit) |
                                      shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::FloatingPoint64Bit);
    wideDomain.inputTypeName = "WideNumericInput";
    wideDomain.outputTypeName = "WideNumericOutput";
    wideDomain.parameterTypeName = "WideNumericParameters";
    wideDomain.resourceTypeName = "WideNumericResources";
    wideDomain.evaluationFunctionName = "EvaluateWideNumeric";
    wideDomain.outputs = wideOutputs;
    wideDomain.prefix = {reinterpret_cast<const u8*>(WideNumericDomainPrefix), sizeof(WideNumericDomainPrefix) - 1u};
    wideDomain.suffix = {reinterpret_cast<const u8*>(WideNumericDomainSuffix), sizeof(WideNumericDomainSuffix) - 1u};
    containers::DynamicArray<u8> wideProbe(memory::pools::Tools::GetInstance());
    Check(mt::GenerateMaterialSlangProbe(wideModule, wideDomain, wideProbe) == mt::MaterialSlangResult::Success && ContainsText(wideProbe, "f16tof32") && ContainsText(wideProbe, "asdouble") &&
              ContainsText(wideProbe, "<< 32u"),
          "16/64-bit constants and dynamic parameter loads generate width-aware Slang");
    const shader_tools::EntryPoint wideEntries[]{{"WideNumericFragment", shaders::ShaderStage::Fragment}};
    shader_tools::CompileRequest wideRequest;
    wideRequest.sourceName = "materials/tests/wide_numeric_codegen.slang";
    wideRequest.moduleName = "material_wide_numeric_codegen";
    wideRequest.source = wideProbe;
    wideRequest.entryPoints = wideEntries;
    wideRequest.settings.target = shader_tools::Target::D3D12Dxil;
    shader_tools::CompileOutput wideReflection;
    const shader_tools::Result wideReflectionResult = codegenCompiler.Reflect(wideRequest, wideReflection);
    if (wideReflectionResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] wide numeric probe: %s\n", wideReflection.GetDiagnostics());
    bool foundI16 = false, foundU16 = false, foundF16 = false, foundI64 = false, foundU64 = false, foundF64 = false;
    for (const shaders::ConstantMember& member : wideReflection.GetMaterialParameters())
    {
        foundI16 = foundI16 || member.scalarType == shaders::ScalarType::I16;
        foundU16 = foundU16 || member.scalarType == shaders::ScalarType::U16;
        foundF16 = foundF16 || member.scalarType == shaders::ScalarType::F16;
        foundI64 = foundI64 || member.scalarType == shaders::ScalarType::I64;
        foundU64 = foundU64 || member.scalarType == shaders::ScalarType::U64;
        foundF64 = foundF64 || member.scalarType == shaders::ScalarType::F64;
    }
    Check(wideReflectionResult == shader_tools::Result::Success && wideReflection.HasMaterialContract() && wideReflection.GetMaterialDomain().requiredCapabilities == wideDomain.requiredCapabilities &&
              foundI16 && foundU16 && foundF16 && foundI64 && foundU64 && foundF64,
          "reflection preserves every 16/64-bit material parameter scalar type");
    containers::DynamicArray<u8> wideFinal(memory::pools::Tools::GetInstance());
    Check(mt::FinalizeMaterialSlangSource(wideProbe, wideReflection, wideFinal) == mt::MaterialSlangResult::Success, "wide numeric parameter offsets finalize from reflection");
    wideRequest.source = wideFinal;
    shader_tools::CompileOutput wideCompiled;
    const shader_tools::Result wideCompileResult = codegenCompiler.Compile(wideRequest, wideCompiled);
    if (wideCompileResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] wide numeric final: %s\n", wideCompiled.GetDiagnostics());
    Check(wideCompileResult == shader_tools::Result::Success && wideCompiled.GetMaterialParameters().Size() == 6, "final 16/64-bit numeric material Slang compiles with stable reflected layout");

    mt::MaterialIrBuilder extendedResourceBuilder;
    extendedResourceBuilder.Reset(crypto::Sha256("ExtendedResource", 16));
    mt::MaterialIrType extendedResourceTypes[8]{};
    extendedResourceTypes[0].kind = mt::MaterialIrTypeKind::Buffer;
    extendedResourceTypes[0].scalarType = shaders::ScalarType::U32;
    extendedResourceTypes[0].bufferKind = mt::MaterialIrBufferKind::Typed;
    extendedResourceTypes[1].kind = mt::MaterialIrTypeKind::Buffer;
    extendedResourceTypes[1].scalarType = shaders::ScalarType::F32;
    extendedResourceTypes[1].bufferKind = mt::MaterialIrBufferKind::Typed;
    extendedResourceTypes[1].resourceAccess = mt::MaterialIrResourceAccess::Write;
    extendedResourceTypes[2].kind = mt::MaterialIrTypeKind::Buffer;
    extendedResourceTypes[2].scalarType = shaders::ScalarType::F32;
    extendedResourceTypes[2].rows = 4;
    extendedResourceTypes[2].bufferKind = mt::MaterialIrBufferKind::Structured;
    extendedResourceTypes[3].kind = mt::MaterialIrTypeKind::Buffer;
    extendedResourceTypes[3].scalarType = shaders::ScalarType::U32;
    extendedResourceTypes[3].rows = 2;
    extendedResourceTypes[3].bufferKind = mt::MaterialIrBufferKind::Structured;
    extendedResourceTypes[3].resourceAccess = mt::MaterialIrResourceAccess::ReadWrite;
    extendedResourceTypes[4].kind = mt::MaterialIrTypeKind::Buffer;
    extendedResourceTypes[4].scalarType = shaders::ScalarType::U32;
    extendedResourceTypes[4].bufferKind = mt::MaterialIrBufferKind::ByteAddress;
    extendedResourceTypes[5] = extendedResourceTypes[4];
    extendedResourceTypes[5].resourceAccess = mt::MaterialIrResourceAccess::Write;
    extendedResourceTypes[6].kind = mt::MaterialIrTypeKind::AccelerationStructure;
    extendedResourceTypes[7].kind = mt::MaterialIrTypeKind::Sampler;
    extendedResourceTypes[7].samplerKind = mt::MaterialIrSamplerKind::Comparison;
    const u64 extendedResourceSemantics[]{0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270, 0x280};
    bool builtExtendedResources = true;
    for (u32 index = 0; index < 8; ++index)
    {
        mt::MaterialIrValueBuildDescription value;
        value.kind = mt::MaterialIrValueKind::DynamicParameter;
        value.type = extendedResourceTypes[index];
        value.legalStages = textureStage;
        value.semantic = extendedResourceSemantics[index];
        mt::MaterialIrValueId resourceValue = 0;
        builtExtendedResources = builtExtendedResources && extendedResourceBuilder.AddValue(value, resourceValue) == mt::MaterialIrResult::Success &&
                                 extendedResourceBuilder.AddOutput({extendedResourceSemantics[index], resourceValue, textureStage}) == mt::MaterialIrResult::Success;
    }
    Check(builtExtendedResources, "extended typed resource codegen fixture construction");
    mt::MaterialIrModule extendedResourceModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> extendedResourceDiagnostics(memory::pools::Tools::GetInstance());
    Check(extendedResourceBuilder.Finalize(extendedResourceModule, extendedResourceDiagnostics) == mt::MaterialIrResult::Success, "buffer, acceleration-structure, and comparison-sampler IR finalization");
    const mt::MaterialSlangSymbol extendedResourceOutputs[]{{0x210, "typedRead"}, {0x220, "typedWrite"}, {0x230, "structuredRead"}, {0x240, "structuredWrite"},
                                                            {0x250, "byteRead"},  {0x260, "byteWrite"},  {0x270, "acceleration"},   {0x280, "comparisonSampler"}};
    mt::MaterialSlangDomain extendedResourceDomain;
    extendedResourceDomain.stableName = "ExtendedResource";
    extendedResourceDomain.schemaVersion = 1;
    extendedResourceDomain.legalStages = textureStage;
    extendedResourceDomain.requiredCapabilities = shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::WritableResources) |
                                                  shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::ComparisonSampling) |
                                                  shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::AccelerationStructure);
    extendedResourceDomain.inputTypeName = "ExtendedResourceInput";
    extendedResourceDomain.outputTypeName = "ExtendedResourceOutput";
    extendedResourceDomain.parameterTypeName = "ExtendedResourceParameters";
    extendedResourceDomain.resourceTypeName = "ExtendedResourceResources";
    extendedResourceDomain.evaluationFunctionName = "EvaluateExtendedResource";
    extendedResourceDomain.outputs = extendedResourceOutputs;
    extendedResourceDomain.prefix = {reinterpret_cast<const u8*>(ExtendedResourceDomainPrefix), sizeof(ExtendedResourceDomainPrefix) - 1u};
    extendedResourceDomain.suffix = {reinterpret_cast<const u8*>(ExtendedResourceDomainSuffix), sizeof(ExtendedResourceDomainSuffix) - 1u};
    containers::DynamicArray<u8> extendedResourceProbe(memory::pools::Tools::GetInstance());
    Check(mt::GenerateMaterialSlangProbe(extendedResourceModule, extendedResourceDomain, extendedResourceProbe) == mt::MaterialSlangResult::Success &&
              ContainsText(extendedResourceProbe, "RWStructuredBuffer<uint2>") && ContainsText(extendedResourceProbe, "RaytracingAccelerationStructure") &&
              ContainsText(extendedResourceProbe, "SamplerComparisonState") && ContainsText(extendedResourceProbe, "uint VanguardMaterialLoad_"),
          "extended resource IR preserves typed reflected roles while evaluator values use descriptor handles");
    const shader_tools::EntryPoint extendedResourceEntries[]{{"ExtendedResourceFragment", shaders::ShaderStage::Fragment}};
    shader_tools::CompileRequest extendedResourceRequest;
    extendedResourceRequest.sourceName = "materials/tests/extended_resource_codegen.slang";
    extendedResourceRequest.moduleName = "material_extended_resource_codegen";
    extendedResourceRequest.source = extendedResourceProbe;
    extendedResourceRequest.entryPoints = extendedResourceEntries;
    extendedResourceRequest.settings.target = shader_tools::Target::D3D12Dxil;
    shader_tools::CompileOutput extendedResourceReflection;
    const shader_tools::Result extendedResourceReflectionResult = codegenCompiler.Reflect(extendedResourceRequest, extendedResourceReflection);
    if (extendedResourceReflectionResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] extended resource probe (%s): %s\n", shader_tools::ToString(extendedResourceReflectionResult), extendedResourceReflection.GetDiagnostics());
    u32 reflectedBuffers = 0;
    bool reflectedAcceleration = false;
    bool reflectedComparisonSampler = false;
    bool reflectedStructuredStride = false;
    bool reflectedWritableBuffer = false;
    bool denseExtendedSlots = extendedResourceReflection.GetMaterialResources().Size() == 8;
    for (u32 index = 0; index < extendedResourceReflection.GetMaterialResources().Size(); ++index)
    {
        const shaders::MaterialResourceRole& role = extendedResourceReflection.GetMaterialResources()[index];
        denseExtendedSlots = denseExtendedSlots && role.slot == index;
        reflectedBuffers += role.kind == shaders::MaterialResourceKind::Buffer ? 1u : 0u;
        reflectedAcceleration = reflectedAcceleration || role.kind == shaders::MaterialResourceKind::AccelerationStructure;
        reflectedComparisonSampler = reflectedComparisonSampler || (role.kind == shaders::MaterialResourceKind::Sampler && role.shape.samplerKind == shaders::MaterialSamplerKind::Comparison);
        reflectedStructuredStride =
            reflectedStructuredStride || (role.kind == shaders::MaterialResourceKind::Buffer && role.shape.bufferKind == shaders::MaterialBufferKind::Structured && role.shape.elementStride != 0);
        reflectedWritableBuffer = reflectedWritableBuffer || (role.kind == shaders::MaterialResourceKind::Buffer && role.shape.access == shaders::MaterialResourceAccess::ReadWrite);
    }
    Check(extendedResourceReflectionResult == shader_tools::Result::Success && extendedResourceReflection.HasMaterialContract() &&
              extendedResourceReflection.GetMaterialDomain().requiredCapabilities == extendedResourceDomain.requiredCapabilities && denseExtendedSlots && reflectedBuffers == 6 && reflectedAcceleration &&
              reflectedComparisonSampler && reflectedStructuredStride && reflectedWritableBuffer,
          "reflection derives dense reconstructable buffer, acceleration, and comparison-sampler roles");
    containers::DynamicArray<u8> extendedResourceFinal(memory::pools::Tools::GetInstance());
    Check(mt::FinalizeMaterialSlangSource(extendedResourceProbe, extendedResourceReflection, extendedResourceFinal) == mt::MaterialSlangResult::Success,
          "extended resource material source finalizes against its reflected contract");
    extendedResourceRequest.source = extendedResourceFinal;
    shader_tools::CompileOutput extendedResourceCompiled;
    const shader_tools::Result extendedResourceCompileResult = codegenCompiler.Compile(extendedResourceRequest, extendedResourceCompiled);
    if (extendedResourceCompileResult != shader_tools::Result::Success)
        std::fprintf(stderr, "[materialToolsTests] extended resource final (%s): %s\n", shader_tools::ToString(extendedResourceCompileResult), extendedResourceCompiled.GetDiagnostics());
    Check(extendedResourceCompileResult == shader_tools::Result::Success && extendedResourceCompiled.GetMaterialResources().Size() == 8,
          "final extended resource material Slang compiles with stable reflected roles");
    codegenCompiler.Shutdown();

    mt::MaterialIrBuilder internBuilder;
    internBuilder.Reset(crypto::Sha256("SurfaceDomain", 13));
    const mt::MaterialIrScalarConstant encodedQuarter = mt::EncodeMaterialF32(0.25f);
    mt::MaterialIrValueBuildDescription internedConstant;
    internedConstant.kind = mt::MaterialIrValueKind::Constant;
    internedConstant.type = FloatType();
    internedConstant.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    internedConstant.data = {encodedQuarter.bytes, encodedQuarter.size};
    mt::MaterialIrValueId authoredConstantA = 0;
    mt::MaterialIrValueId authoredConstantB = 0;
    Check(internBuilder.AddValue(internedConstant, authoredConstantA) == mt::MaterialIrResult::Success && internBuilder.AddValue(internedConstant, authoredConstantB) == mt::MaterialIrResult::Success &&
              internBuilder.AddOutput({0x1000, authoredConstantA, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success &&
              internBuilder.AddOutput({0x1001, authoredConstantB, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "equal authored constants fixture construction");
    mt::MaterialIrModule internedModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> internDiagnostics(memory::pools::Tools::GetInstance());
    containers::DynamicArray<mt::MaterialIrValueId> sourceValueRemap(memory::pools::Tools::GetInstance());
    Check(internBuilder.Finalize(internedModule, internDiagnostics, {}, &sourceValueRemap) == mt::MaterialIrResult::Success && internedModule.GetValues().Count() == 1 && sourceValueRemap.Size() == 2 &&
              sourceValueRemap[authoredConstantA] == sourceValueRemap[authoredConstantB],
          "IR interning returns the compact value remap used by source diagnostics");

    mt::MaterialIrBuilder stageBuilder;
    stageBuilder.Reset(crypto::Sha256("SurfaceDomain", 13));
    const float value = 1.0f;
    mt::MaterialIrValueBuildDescription vertexOnly;
    vertexOnly.kind = mt::MaterialIrValueKind::Constant;
    vertexOnly.type = FloatType();
    vertexOnly.legalStages = shaders::StageBit(shaders::ShaderStage::Vertex);
    vertexOnly.data = {reinterpret_cast<const u8*>(&value), sizeof(value)};
    mt::MaterialIrValueId vertexValue = 0;
    Check(stageBuilder.AddValue(vertexOnly, vertexValue) == mt::MaterialIrResult::Success &&
              stageBuilder.AddOutput({0x2000, vertexValue, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "stage mismatch fixture construction");
    mt::MaterialIrModule stageModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> stageDiagnostics(memory::pools::Tools::GetInstance());
    Check(stageBuilder.Finalize(stageModule, stageDiagnostics) == mt::MaterialIrResult::InvalidStage && !stageDiagnostics.Empty(), "stage requirements propagate through reachable values");

    mt::MaterialIrBuilder cycleBuilder;
    cycleBuilder.Reset(crypto::Sha256("SurfaceDomain", 13));
    const std::array<mt::MaterialIrValueId, 1> futureOperand{{1}};
    mt::MaterialIrValueBuildDescription extract;
    extract.kind = mt::MaterialIrValueKind::Instruction;
    extract.opcode = mt::MaterialIrOpcode::Extract;
    extract.type = FloatType();
    extract.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
    extract.operands = {futureOperand.data(), static_cast<u32>(futureOperand.size())};
    mt::MaterialIrValueId cycleA = 0;
    Check(cycleBuilder.AddValue(extract, cycleA) == mt::MaterialIrResult::Success, "forward edge fixture");
    const std::array<mt::MaterialIrValueId, 1> backOperand{{0}};
    extract.operands = {backOperand.data(), static_cast<u32>(backOperand.size())};
    mt::MaterialIrValueId cycleB = 0;
    Check(cycleBuilder.AddValue(extract, cycleB) == mt::MaterialIrResult::Success && cycleBuilder.AddOutput({0x3000, cycleA, shaders::StageBit(shaders::ShaderStage::Fragment)}) == mt::MaterialIrResult::Success,
          "cycle fixture construction");
    mt::MaterialIrModule cycleModule;
    containers::DynamicArray<mt::MaterialIrDiagnostic> cycleDiagnostics(memory::pools::Tools::GetInstance());
    Check(cycleBuilder.Finalize(cycleModule, cycleDiagnostics) == mt::MaterialIrResult::CycleDetected && !cycleDiagnostics.Empty(), "cycles are rejected without recursive traversal");

    const mt::MaterialPinSchema orderedPins[]{{1, floatType, true}, {2, floatType, false}};
    const mt::MaterialPinSchema reversedPins[]{{2, floatType, false}, {1, floatType, true}};
    const crypto::Digest256 registryOrderFingerprint = crypto::Sha256("material.registry-order", 23);
    mt::MaterialNodeRegistry orderedNodeRegistry;
    mt::MaterialNodeRegistry reversedNodeRegistry;
    Check(orderedNodeRegistry.Register({7, 1, registryOrderFingerprint, {orderedPins, 2}, {orderedPins, 2}}) &&
              reversedNodeRegistry.Register({7, 1, registryOrderFingerprint, {reversedPins, 2}, {reversedPins, 2}}) && orderedNodeRegistry.Freeze() && reversedNodeRegistry.Freeze() &&
              orderedNodeRegistry.Fingerprint() == reversedNodeRegistry.Fingerprint(),
          "node registry identity is ordered by stable pin ids, not descriptor array order");

    const u8 defaultA[4]{0, 0, 0, 0};
    const u8 defaultB[4]{0, 0, 128, 63};
    const mt::MaterialDomainOutputSchema orderedDomainOutputs[]{{10, floatType, shaders::StageBit(shaders::ShaderStage::Fragment), {defaultA, 4}},
                                                                {20, floatType, shaders::StageBit(shaders::ShaderStage::Fragment), {defaultB, 4}}};
    const mt::MaterialDomainOutputSchema reversedDomainOutputs[]{orderedDomainOutputs[1], orderedDomainOutputs[0]};
    const mt::MaterialTechniqueRequirement orderedTechniques[]{{100, Reference("materials/tests/a.vppl", pipelines::PipelineResourceType)},
                                                               {200, Reference("materials/tests/b.vppl", pipelines::PipelineResourceType)}};
    const mt::MaterialTechniqueRequirement reversedTechniques[]{orderedTechniques[1], orderedTechniques[0]};
    const shaders::MaterialDomainContract registryOrderContract = MakeTestDomainContract(7, shaders::StageBit(shaders::ShaderStage::Fragment), "RegistryOrderInput", "RegistryOrderOutput");
    mt::MaterialDomainRegistry orderedDomainRegistry;
    mt::MaterialDomainRegistry reversedDomainRegistry;
    Check(orderedDomainRegistry.Register({7, registryOrderContract, registryOrderFingerprint, {}, {orderedDomainOutputs, 2}, {orderedTechniques, 2}}) &&
              reversedDomainRegistry.Register({7, registryOrderContract, registryOrderFingerprint, {}, {reversedDomainOutputs, 2}, {reversedTechniques, 2}}) && orderedDomainRegistry.Freeze() &&
              reversedDomainRegistry.Freeze() && orderedDomainRegistry.Fingerprint() == reversedDomainRegistry.Fingerprint(),
          "domain registry identity is ordered by stable output and technique ids");

    mt::MaterialNodeRegistry nodeRegistry;
    const mt::MaterialPinSchema constantOutput{1, floatType, true};
    const crypto::Digest256 constantFingerprint = crypto::Sha256("material.constant", 17);
    Check(nodeRegistry.Register({1, 1, constantFingerprint, {}, {&constantOutput, 1}}) && nodeRegistry.Freeze(), "material node registry freezes canonical schemas");
    mt::MaterialDomainRegistry domainRegistry;
    const u8 defaultFloat[4]{};
    const mt::MaterialDomainOutputSchema domainOutput{0x1000, floatType, shaders::StageBit(shaders::ShaderStage::Fragment), {defaultFloat, 4}};
    const crypto::Digest256 domainFingerprint = crypto::Sha256("material.domain", 15);
    const shaders::MaterialDomainContract frontendContract = MakeTestDomainContract(1, shaders::StageBit(shaders::ShaderStage::Fragment), "FrontendInput", "FrontendOutput");
    Check(domainRegistry.Register({1, frontendContract, domainFingerprint, {}, {&domainOutput, 1}, {}}) && domainRegistry.Freeze(), "material domain registry freezes outputs and defaults");
    const u8 staticTrue = 1;
    const float selectedFloat = 0.25f;
    const float unreachableFloat = 99.0f;
    const mt::MaterialSourceOperand selectOperands[]{{mt::MaterialStaticSelectConditionPin, 1}, {mt::MaterialStaticSelectTruePin, 2}, {mt::MaterialStaticSelectFalsePin, 3}};
    mt::MaterialIrType boolType;
    boolType.kind = mt::MaterialIrTypeKind::Numeric;
    boolType.scalarType = shaders::ScalarType::Bool;
    const mt::MaterialSourceValue sourceValues[]{
        {1, 10, 99, 1, 1, mt::MaterialIrValueKind::StaticParameter, mt::MaterialIrOpcode::None, boolType, shaders::StageBit(shaders::ShaderStage::Fragment), 0x2000, {}, {&staticTrue, 1}, {}, 0},
        {2,
         20,
         1,
         1,
         1,
         mt::MaterialIrValueKind::Constant,
         mt::MaterialIrOpcode::None,
         floatType,
         shaders::StageBit(shaders::ShaderStage::Fragment),
         0,
         {},
         {reinterpret_cast<const u8*>(&selectedFloat), sizeof(selectedFloat)},
         {},
         0},
        // This deliberately has an unknown node type. Static selection must not
        // ask the unused branch to lower.
        {3,
         30,
         0xdead,
         1,
         1,
         mt::MaterialIrValueKind::Constant,
         mt::MaterialIrOpcode::None,
         floatType,
         shaders::StageBit(shaders::ShaderStage::Fragment),
         0,
         {},
         {reinterpret_cast<const u8*>(&unreachableFloat), sizeof(unreachableFloat)},
         {},
         0},
        {4,
         40,
         mt::MaterialStaticSelectNodeType,
         1,
         1,
         mt::MaterialIrValueKind::Instruction,
         mt::MaterialIrOpcode::Select,
         floatType,
         shaders::StageBit(shaders::ShaderStage::Fragment),
         0,
         selectOperands,
         {},
         {},
         0}};
    const mt::MaterialSourceOutput sourceOutput{0x1000, 4, shaders::StageBit(shaders::ShaderStage::Fragment)};
    const mt::MaterialSourceGraph sourceGraph{Reference("materials/tests/source.vmatgraph", 0x4d534752u), 1, sourceValues, {&sourceOutput, 1}};
    mt::MaterialFrontend frontend;
    mt::MaterialIrModule frontendModule;
    mt::MaterialSourceMap sourceMap;
    containers::DynamicArray<mt::MaterialIrDiagnostic> frontendDiagnostics(memory::pools::Tools::GetInstance());
    const mt::MaterialFrontendConfig frontendConfig{&nodeRegistry, &domainRegistry};
    Check(frontend.Lower(sourceGraph, frontendConfig, frontendModule, sourceMap, frontendDiagnostics) == mt::MaterialFrontendResult::Success && frontendModule.GetValues().Count() == 1 &&
              sourceMap.Entries().Count() == 2 && sourceMap.Entries()[0].irValue == sourceMap.Entries()[1].irValue,
          "static selection prunes the unused branch and preserves both source attributions");

    BeginSuite("canonical artifact contracts");
    ArtifactFixture fixture;
    Check(fixture.valid, "canonical material compiler inputs encode");
    DeclaredSurfaceArtifactFixture declaredFixture;
    RecookTextureFixture recookTexture;
    declaredFixture.texture = &recookTexture;
    Check(declaredFixture.valid, "full declared material surface lowers through the canonical production builder");
    Check(declaredFixture.canonical.invalidBufferResult == mt::MaterialCanonicalResult::InvalidResourceValue &&
              declaredFixture.canonical.unsupportedResult == mt::MaterialCanonicalResult::UnsupportedTargetCapability,
          "declared-surface canonical construction rejects typed resource mismatch and unsupported offline capability");
    Check(fixture.canonical.schemaMismatchResult == mt::MaterialCanonicalResult::DomainMismatch && fixture.canonical.stageMismatchResult == mt::MaterialCanonicalResult::DomainMismatch,
          "canonical material construction rejects schema and legal-stage drift from the frozen domain contract");
    Check(EqualBytes(fixture.canonical.base.Program().source.content, fixture.canonical.changed.Program().source.content) &&
              EqualBytes(fixture.canonical.base.Pipelines()[0].source.content, fixture.canonical.changed.Pipelines()[0].source.content) &&
              !EqualBytes(fixture.canonical.base.Material().source.content, fixture.canonical.changed.Material().source.content),
          "authored dynamic values change only canonical MVLI bytes");
    Check(EqualBytes(fixture.canonical.base.Program().source.content, fixture.canonical.reordered.Program().source.content) &&
              EqualBytes(fixture.canonical.base.Pipelines()[0].source.content, fixture.canonical.reordered.Pipelines()[0].source.content) &&
              EqualBytes(fixture.canonical.base.Material().source.content, fixture.canonical.reordered.Material().source.content),
          "canonical MPGI, MPLI, and MVLI bytes ignore authored value ordering");
    Check(EqualBytes(fixture.canonical.base.Program().source.content, fixture.canonical.pipelineChanged.Program().source.content) &&
              !EqualBytes(fixture.canonical.base.Pipelines()[0].source.content, fixture.canonical.pipelineChanged.Pipelines()[0].source.content) &&
              EqualBytes(fixture.canonical.base.Material().source.content, fixture.canonical.pipelineChanged.Material().source.content),
          "pipeline-template edits change only canonical MPLI bytes");
    Check(!EqualBytes(fixture.canonical.base.Program().source.content, fixture.canonical.structuralChanged.Program().source.content) &&
              EqualBytes(fixture.canonical.base.Pipelines()[0].source.content, fixture.canonical.structuralChanged.Pipelines()[0].source.content) &&
              EqualBytes(fixture.canonical.base.Material().source.content, fixture.canonical.structuralChanged.Material().source.content),
          "structural graph edits change only canonical MPGI bytes");
    Check(fixture.canonical.rejectedResult == mt::MaterialCanonicalResult::FrontendFailure && fixture.canonical.rejectedDiagnostic.frontend == mt::MaterialFrontendResult::UnknownNode &&
              fixture.canonical.rejectedDiagnostic.node == 0x415554484f525f41ull && fixture.canonical.rejectedDiagnostic.pin == 10,
          "canonical frontend failures preserve authored node and pin attribution");
    assets::BuildSystem buildSystem;
    assets::Config buildConfig;
    buildConfig.persistentCacheRoot = looseProofRoot.AsChar();
    Check(buildSystem.Initialize(buildConfig), "persistent material build-system initialization");
    mt::MaterialArtifactCompilers artifactCompilers;
    mt::MaterialArtifactCompilerConfig artifactCompilerConfig;
    Check(artifactCompilers.Initialize(artifactCompilerConfig) && artifactCompilers.Register(buildSystem) == assets::Result::Success, "material artifact compilers register");
    const assets::CompilerDescriptor recookTextureCompiler{assets::HashCompilerName("material_tools.recook_texture"),
                                                           "material_tools.recook_texture",
                                                           1,
                                                           RecookTextureSourceType,
                                                           textures::TextureResourceType,
                                                           RecookTextureFixture::Discover,
                                                           RecookTextureFixture::Compile,
                                                           &recookTexture,
                                                           RecookTextureFixture::Estimate};
    Check(buildSystem.RegisterCompiler(recookTextureCompiler) == assets::Result::Success, "material recook texture proof compiler registers");

    const auto rejectedProgramBuild = [&buildSystem](const char* const sourcePath, const char* const outputPath, const containers::ArraySpan<const u8> bytes) noexcept
    {
        const assets::BuildRequest request{
            {Reference(sourcePath, mt::MaterialProgramInputResourceType), bytes, {}}, Reference(outputPath, shaders::ShaderResourceType), assets::TargetPlatform::WindowsD3D12, {}};
        assets::BuildOutput output;
        return buildSystem.Build(request, output) != assets::Result::Success && output.artifacts.Empty();
    };

    containers::DynamicArray<u8> rejectedProgramBytes(memory::pools::Assets::GetInstance());
    crypto::Digest256 wrongDomainFingerprint = fixture.canonical.domainContractFingerprint;
    wrongDomainFingerprint.bytes[0] ^= 1u;
    Check(EncodeComputeMaterialProgram(fixture.canonical.domainContract, wrongDomainFingerprint, fixture.canonical.domainContract.requiredCapabilities, rejectedProgramBytes) ==
              mt::MaterialInputResult::InvalidArgument,
          "MPGI encoding rejects a domain fingerprint that does not describe its complete contract");

    const auto rejectsReflectedDomainDrift = [&](const char* const sourcePath, const char* const outputPath, const shaders::MaterialDomainContract& expectedDomain) noexcept
    {
        crypto::Digest256 expectedFingerprint;
        rejectedProgramBytes.Clear();
        return shaders::CalculateMaterialDomainFingerprint(expectedDomain, expectedFingerprint) == shaders::Result::Success &&
               EncodeComputeMaterialProgram(expectedDomain, expectedFingerprint, expectedDomain.requiredCapabilities, rejectedProgramBytes) == mt::MaterialInputResult::Success &&
               rejectedProgramBuild(sourcePath, outputPath, rejectedProgramBytes);
    };

    shaders::MaterialDomainContract mismatchedDomain = fixture.canonical.domainContract;
    ++mismatchedDomain.schemaVersion;
    Check(rejectsReflectedDomainDrift("materials/tests/schema_mismatch.mpgi", "materials/tests/schema_mismatch.vshader", mismatchedDomain), "program cooking rejects reflected material-domain schema drift");
    mismatchedDomain = fixture.canonical.domainContract;
    mismatchedDomain.legalStages |= shaders::StageBit(shaders::ShaderStage::Fragment);
    Check(rejectsReflectedDomainDrift("materials/tests/stage_mismatch.mpgi", "materials/tests/stage_mismatch.vshader", mismatchedDomain), "program cooking rejects reflected material-domain legal-stage drift");
    mismatchedDomain = fixture.canonical.domainContract;
    mismatchedDomain.inputType.bytes[0] ^= 1u;
    Check(rejectsReflectedDomainDrift("materials/tests/input_mismatch.mpgi", "materials/tests/input_mismatch.vshader", mismatchedDomain), "program cooking rejects reflected material-domain input-type drift");
    mismatchedDomain = fixture.canonical.domainContract;
    mismatchedDomain.outputType.bytes[0] ^= 1u;
    Check(rejectsReflectedDomainDrift("materials/tests/output_mismatch.mpgi", "materials/tests/output_mismatch.vshader", mismatchedDomain),
          "program cooking rejects reflected material-domain output-type drift");

    rejectedProgramBytes.Clear();
    const bool encodedCurrentProgram = EncodeComputeMaterialProgram(fixture.canonical.domainContract, fixture.canonical.domainContractFingerprint, fixture.canonical.domainContract.requiredCapabilities,
                                                                    rejectedProgramBytes) == mt::MaterialInputResult::Success;
    if (rejectedProgramBytes.Size() >= 6)
    {
        rejectedProgramBytes[4] = 2;
        rejectedProgramBytes[5] = 0;
    }
    Check(encodedCurrentProgram && rejectedProgramBytes.Size() >= 6 && rejectedProgramBuild("materials/tests/old_version.mpgi", "materials/tests/old_version.vshader", rejectedProgramBytes),
          "the clean MPGI format cut rejects the previous input version");

    const resources::ResourceReference requiredTexture = Reference("materials/tests/required.vtex", textures::TextureResourceType);
    const resources::ResourceReference optionalTexture = Reference("materials/tests/optional.vtex", textures::TextureResourceType);
    const resources::ResourceReference softTexture = Reference("materials/tests/soft.vtex", textures::TextureResourceType);
    const materials::TechniqueBuildRecord dependencyTechnique[]{{0x434f4d505554455full, fixture.pipeline, nullptr}};
    const materials::ResourceValueBuildRecord dependencyResources[]{
        {0x1001, 0, requiredTexture, resources::DependencyKind::Required}, {0x1002, 0, optionalTexture, resources::DependencyKind::Optional}, {0x1003, 0, softTexture, resources::DependencyKind::Soft}};
    const mt::MaterialValueInput dependencyInput{0x444550454e44454eull, fixture.shader, dependencyTechnique, {}, dependencyResources};
    containers::DynamicArray<u8> dependencyInputBytes(memory::pools::Assets::GetInstance());
    const resources::ResourceReference dependencyInputResource = Reference("materials/tests/dependency_kinds.mvli", mt::MaterialValueInputResourceType);
    const resources::ResourceReference dependencyOutput = Reference("materials/tests/dependency_kinds.vmat", materials::MaterialResourceType);
    assets::BuildPlan dependencyPlan;
    const bool dependencyInputEncoded = mt::EncodeMaterialValueInput(dependencyInput, dependencyInputBytes) == mt::MaterialInputResult::Success;
    const assets::BuildRequest dependencyRequest{{dependencyInputResource, dependencyInputBytes, {}}, dependencyOutput, assets::TargetPlatform::WindowsD3D12, {}};
    const bool dependencyPrepared = dependencyInputEncoded && buildSystem.Prepare(dependencyRequest, dependencyPlan) == assets::Result::Success;
    bool foundRequiredDependency = false;
    bool foundOptionalDependency = false;
    bool foundSoftDependency = false;
    for (const assets::BuildDependency& dependency : dependencyPlan.GetDependencies())
    {
        foundRequiredDependency = foundRequiredDependency || (dependency.identity == requiredTexture && dependency.requirement == assets::DependencyRequirement::Required && dependency.content.IsEmpty());
        foundOptionalDependency = foundOptionalDependency || (dependency.identity == optionalTexture && dependency.requirement == assets::DependencyRequirement::Optional && dependency.content.IsEmpty());
        foundSoftDependency = foundSoftDependency || (dependency.identity == softTexture && dependency.requirement == assets::DependencyRequirement::Soft && dependency.content.IsEmpty());
    }
    Check(dependencyPrepared && foundRequiredDependency && foundOptionalDependency && foundSoftDependency, "MVLI discovery preserves Required, Optional, and Soft generated dependencies exactly");

    containers::DynamicArray<u8> staleValueInputBytes(memory::pools::Assets::GetInstance());
    const bool copiedStaleValueInput = CopyBytes(dependencyInputBytes, staleValueInputBytes);
    if (staleValueInputBytes.Size() >= 6)
    {
        staleValueInputBytes[4] = 2;
        staleValueInputBytes[5] = 0;
    }
    assets::BuildPlan staleValuePlan;
    const resources::ResourceReference staleValueInputResource = Reference("materials/tests/old_version.mvli", mt::MaterialValueInputResourceType);
    const resources::ResourceReference staleValueOutput = Reference("materials/tests/old_version.vmat", materials::MaterialResourceType);
    const assets::BuildRequest staleValueRequest{{staleValueInputResource, staleValueInputBytes, {}}, staleValueOutput, assets::TargetPlatform::WindowsD3D12, {}};
    Check(copiedStaleValueInput && staleValueInputBytes.Size() >= 6 && buildSystem.Prepare(staleValueRequest, staleValuePlan) != assets::Result::Success,
          "the canonical MVLI v3 reader rejects the previous value-input format");

    BeginSuite("indexed DDC and VPAK closure");
    assets::DependencyIndexConfig materialIndexConfig;
    materialIndexConfig.root = looseProofRoot.AsChar();
    materialIndexConfig.settingsFingerprint = crypto::Sha256("material.package-contract", 25);
    assets::DependencyIndex materialIndex;
    Check(materialIndex.Initialize(materialIndexConfig) == assets::IndexResult::Success, "material dependency-index initialization");

    assets::BuildGraph graph;
    Check(graph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture, {}, &materialIndex), "indexed material artifact graph initialization");
    assets::GraphRequest shaderRequest = graph.Request(fixture.programRequest, assets::BuildPriority::High);
    assets::GraphRequest pipelineRequest = graph.Request(fixture.pipelineRequest, assets::BuildPriority::High);
    assets::GraphRequest materialRequest = graph.Request(fixture.materialRequest, assets::BuildPriority::High);
    shaderRequest.Wait();
    pipelineRequest.Wait();
    materialRequest.Wait();
    if (!shaderRequest.HasSucceeded() || !pipelineRequest.HasSucceeded() || !materialRequest.HasSucceeded())
    {
        std::fprintf(stderr, "[materialToolsTests] graph status shader=%u/%u/%s pipeline=%u/%u/%s material=%u/%u/%s\n", static_cast<u32>(shaderRequest.GetStatus()), static_cast<u32>(shaderRequest.GetError()),
                     assets::ToString(shaderRequest.BuildError()), static_cast<u32>(pipelineRequest.GetStatus()), static_cast<u32>(pipelineRequest.GetError()), assets::ToString(pipelineRequest.BuildError()),
                     static_cast<u32>(materialRequest.GetStatus()), static_cast<u32>(materialRequest.GetError()), assets::ToString(materialRequest.BuildError()));
        assets::BuildReport report;
        if (materialRequest.CopyReport(report))
            for (const assets::BuildDiagnostic& diagnostic : report.GetDiagnostics())
                std::fprintf(stderr, "[materialToolsTests] material report %u: %.*s\n", diagnostic.code, static_cast<int>(report.GetMessage(diagnostic).Length()), report.GetMessage(diagnostic).Data());
    }
    assets::BuildOutput shaderOutput;
    assets::BuildOutput pipelineOutput;
    assets::BuildOutput materialOutput;
    Check(shaderRequest.HasSucceeded() && pipelineRequest.HasSucceeded() && materialRequest.HasSucceeded() && shaderRequest.CopyOutput(shaderOutput) && pipelineRequest.CopyOutput(pipelineOutput) &&
              materialRequest.CopyOutput(materialOutput),
          "generated VSHADER, VPPL, and VMAT graph completes");
    if (!shaderOutput.artifacts.Empty() && !pipelineOutput.artifacts.Empty() && !materialOutput.artifacts.Empty())
    {
        filesystem::MemoryFileReader shaderReader(shaderOutput.artifacts[0].bytes, 0);
        shaders::ShaderFile shaderFile;
        filesystem::MemoryFileReader pipelineReader(pipelineOutput.artifacts[0].bytes, 0);
        pipelines::PipelineFile pipelineFile;
        filesystem::MemoryFileReader materialReader(materialOutput.artifacts[0].bytes, 0);
        materials::MaterialFile materialFile;
        Check(shaderFile.Open(shaderReader) == shaders::Result::Success && shaderFile.HasMaterialContract() && pipelineFile.Open(pipelineReader) == pipelines::Result::Success &&
                  pipelines::ValidateShaderCompatibility(pipelineFile, shaderFile) == pipelines::Result::Success && materialFile.Open(materialReader) == materials::Result::Success &&
                  materialFile.GetShader() == fixture.shader && materialFile.GetTechniques().Count() == 1 && materialFile.GetTechniques()[0].pipeline == fixture.pipeline,
              "all material artifacts reopen and mutually validate");
    }
    shaderRequest.Reset();
    pipelineRequest.Reset();
    materialRequest.Reset();
    Check(graph.Shutdown(), "material artifact graph shutdown");
    Check(materialIndex.Save() == assets::IndexResult::Success, "material artifact baseline is committed before transactional recooking");

    BeginSuite("incremental recooking and reproducibility");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::DependencyRecord baselineShaderRecord;
    assets::DependencyRecord baselinePipelineRecord;
    assets::DependencyRecord baselineMaterialRecord;
    Check(materialIndex.Find(fixture.shader, baselineShaderRecord) == assets::IndexResult::Success && materialIndex.Find(fixture.pipeline, baselinePipelineRecord) == assets::IndexResult::Success &&
              materialIndex.Find(fixture.material, baselineMaterialRecord) == assets::IndexResult::Success,
          "recook baseline records are indexed");

    assets::BuildGraph recookGraph;
    Check(recookGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture, {}, &materialIndex), "material recook graph initialization");
    assets::IncrementalRecooker recooker;
    Check(recooker.Initialize(buildSystem, recookGraph, materialIndex, ArtifactFixture::ResolveOutput, &fixture), "material incremental recooker initialization");

    const auto findCurrentRecords = [&](assets::DependencyRecord& shader, assets::DependencyRecord& pipeline, assets::DependencyRecord& material) noexcept
    {
        return materialIndex.Find(fixture.shader, shader) == assets::IndexResult::Success && materialIndex.Find(fixture.pipeline, pipeline) == assets::IndexResult::Success &&
               materialIndex.Find(fixture.material, material) == assets::IndexResult::Success;
    };
    const auto recookOne = [&](const resources::ResourceReference source, assets::RecookStats& stats) noexcept
    {
        const assets::AssetChange change{source};
        assets::RecookBatch batch = recooker.Request({&change, 1}, assets::BuildPriority::High);
        if (!batch)
            return false;
        batch.Wait();
        stats = batch.GetStats();
        const bool succeeded = batch.GetStatus() == assets::RecookState::Succeeded && batch.GetError() == assets::RecookFailure::None;
        batch.Reset();
        return succeeded;
    };
    const auto baselineRestored = [&]() noexcept
    {
        assets::DependencyRecord shader;
        assets::DependencyRecord pipeline;
        assets::DependencyRecord material;
        return findCurrentRecords(shader, pipeline, material) && shader.buildFingerprint == baselineShaderRecord.buildFingerprint && shader.contentFingerprint == baselineShaderRecord.contentFingerprint &&
               pipeline.buildFingerprint == baselinePipelineRecord.buildFingerprint && pipeline.contentFingerprint == baselinePipelineRecord.contentFingerprint &&
               material.buildFingerprint == baselineMaterialRecord.buildFingerprint && material.contentFingerprint == baselineMaterialRecord.contentFingerprint;
    };

    fixture.RefreshRequests(fixture.changedMaterialBytes);
    assets::RecookStats dynamicStats;
    Check(recookOne(fixture.materialSource, dynamicStats) && dynamicStats.dirtySeeds == 1 && dynamicStats.affectedOutputs == 1 && dynamicStats.rootRequests == 1,
          "MVLI dynamic-value edit transaction rebuilds only VMAT");
    assets::DependencyRecord dynamicShaderRecord;
    assets::DependencyRecord dynamicPipelineRecord;
    assets::DependencyRecord dynamicMaterialRecord;
    Check(findCurrentRecords(dynamicShaderRecord, dynamicPipelineRecord, dynamicMaterialRecord) && dynamicShaderRecord.buildFingerprint == baselineShaderRecord.buildFingerprint &&
              dynamicPipelineRecord.buildFingerprint == baselinePipelineRecord.buildFingerprint && dynamicMaterialRecord.buildFingerprint != baselineMaterialRecord.buildFingerprint,
          "dynamic recook preserves VSHADER/VPPL identities and replaces VMAT");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats dynamicRevertStats;
    Check(recookOne(fixture.materialSource, dynamicRevertStats) && baselineRestored(), "reverting a dynamic value reproduces the byte-identical baseline records");

    fixture.RefreshRequests(fixture.programBytes, fixture.changedPipelineBytes, fixture.materialBytes);
    assets::RecookStats pipelineStats;
    Check(recookOne(fixture.pipelineSource, pipelineStats) && pipelineStats.dirtySeeds == 1 && pipelineStats.affectedOutputs == 2 && pipelineStats.rootRequests == 1,
          "MPLI pipeline-only edit transaction invalidates VPPL and dependent VMAT");
    assets::DependencyRecord pipelineEditShaderRecord;
    assets::DependencyRecord pipelineEditPipelineRecord;
    assets::DependencyRecord pipelineEditMaterialRecord;
    Check(findCurrentRecords(pipelineEditShaderRecord, pipelineEditPipelineRecord, pipelineEditMaterialRecord) && pipelineEditShaderRecord.buildFingerprint == baselineShaderRecord.buildFingerprint &&
              pipelineEditPipelineRecord.buildFingerprint != baselinePipelineRecord.buildFingerprint && pipelineEditMaterialRecord.buildFingerprint != baselineMaterialRecord.buildFingerprint,
          "pipeline-only recook preserves VSHADER and replaces VPPL/VMAT dependency state");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats pipelineRevertStats;
    Check(recookOne(fixture.pipelineSource, pipelineRevertStats) && baselineRestored(), "reverting an MPLI edit reproduces the baseline closure");

    fixture.RefreshRequests(fixture.structuralProgramBytes, fixture.pipelineBytes, fixture.materialBytes);
    assets::RecookStats structuralStats;
    Check(recookOne(fixture.programSource, structuralStats) && structuralStats.dirtySeeds == 1 && structuralStats.affectedOutputs == 3 && structuralStats.rootRequests == 1,
          "MPGI structural edit transaction invalidates VSHADER, VPPL, and VMAT");
    assets::DependencyRecord structuralShaderRecord;
    assets::DependencyRecord structuralPipelineRecord;
    assets::DependencyRecord structuralMaterialRecord;
    Check(findCurrentRecords(structuralShaderRecord, structuralPipelineRecord, structuralMaterialRecord) && structuralShaderRecord.buildFingerprint != baselineShaderRecord.buildFingerprint &&
              structuralPipelineRecord.buildFingerprint != baselinePipelineRecord.buildFingerprint && structuralMaterialRecord.buildFingerprint != baselineMaterialRecord.buildFingerprint,
          "structural recook replaces the complete material artifact closure");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats structuralRevertStats;
    Check(recookOne(fixture.programSource, structuralRevertStats) && baselineRestored(), "reverting an MPGI edit reproduces all baseline build and content fingerprints");

    const u8 changedBuildSetting = 0x5a;
    fixture.programRequest.settings = {&changedBuildSetting, 1};
    fixture.pipelineRequest.settings = {&changedBuildSetting, 1};
    fixture.materialRequest.settings = {&changedBuildSetting, 1};
    const assets::AssetChange settingChanges[]{{fixture.programSource}, {fixture.pipelineSource}, {fixture.materialSource}};
    assets::RecookBatch settingsBatch = recooker.Request(settingChanges, assets::BuildPriority::High);
    settingsBatch.Wait();
    const assets::RecookStats settingsStats = settingsBatch.GetStats();
    Check(settingsBatch.GetStatus() == assets::RecookState::Succeeded && settingsStats.dirtySeeds == 3 && settingsStats.affectedOutputs == 3 && settingsStats.rootRequests == 1,
          "shared compiler-setting edit transaction invalidates the complete closure and minimizes to one VMAT root");
    settingsBatch.Reset();
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats settingsRevertStats;
    Check(recookOne(fixture.programSource, settingsRevertStats), "compiler-setting revert recooks the closure");
    assets::RecookStats pipelineSettingRevertStats;
    assets::RecookStats materialSettingRevertStats;
    Check(recookOne(fixture.pipelineSource, pipelineSettingRevertStats) && recookOne(fixture.materialSource, materialSettingRevertStats) && baselineRestored(),
          "compiler-setting revert reproduces the baseline records");

    fixture.materialRequest.target = assets::TargetPlatform::WindowsVulkan;
    assets::RecookStats targetStats;
    Check(recookOne(fixture.materialSource, targetStats) && targetStats.affectedOutputs == 1, "target identity change invalidates and transactionally recooks VMAT");
    assets::DependencyRecord targetShaderRecord;
    assets::DependencyRecord targetPipelineRecord;
    assets::DependencyRecord targetMaterialRecord;
    Check(findCurrentRecords(targetShaderRecord, targetPipelineRecord, targetMaterialRecord) && targetMaterialRecord.target == assets::TargetPlatform::WindowsVulkan &&
              targetMaterialRecord.buildFingerprint != baselineMaterialRecord.buildFingerprint && targetShaderRecord.buildFingerprint == baselineShaderRecord.buildFingerprint &&
              targetPipelineRecord.buildFingerprint == baselinePipelineRecord.buildFingerprint,
          "VMAT target participates in indexed build identity without perturbing unchanged dependencies");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats targetRevertStats;
    Check(recookOne(fixture.materialSource, targetRevertStats) && baselineRestored(), "target revert reproduces the baseline VMAT record");

    fixture.RefreshRequests(fixture.invalidMaterialBytes);
    const assets::AssetChange failingChange{fixture.materialSource};
    assets::RecookBatch failingBatch = recooker.Request({&failingChange, 1}, assets::BuildPriority::High);
    failingBatch.Wait();
    Check(failingBatch.GetStatus() == assets::RecookState::Failed && failingBatch.GetError() == assets::RecookFailure::BuildFailed && baselineRestored() && !materialIndex.HasActiveTransaction() &&
              !materialIndex.HasChanges(),
          "failed VMAT recook rolls back the transaction and preserves the complete baseline");
    failingBatch.Reset();
    fixture.RefreshRequests(fixture.materialBytes);

    Check(recooker.Shutdown(), "material incremental recooker shutdown before restart");
    Check(recookGraph.Shutdown(), "material recook graph shutdown before restart");
    Check(materialIndex.Save() == assets::IndexResult::Success && materialIndex.Shutdown(), "material recook index persists before restart");
    Check(materialIndex.Initialize(materialIndexConfig) == assets::IndexResult::Success && baselineRestored(), "material recook index restart preserves the last committed deterministic baseline");
    Check(recookGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture, {}, &materialIndex) && recooker.Initialize(buildSystem, recookGraph, materialIndex, ArtifactFixture::ResolveOutput, &fixture),
          "material recooker restarts against the persistent dependency index");
    fixture.RefreshRequests(fixture.changedMaterialBytes);
    assets::RecookStats restartedDynamicStats;
    Check(recookOne(fixture.materialSource, restartedDynamicStats) && restartedDynamicStats.affectedOutputs == 1, "post-restart MVLI change retains exact incremental behavior");
    fixture.RefreshRequests(fixture.materialBytes);
    assets::RecookStats restartedRevertStats;
    Check(recookOne(fixture.materialSource, restartedRevertStats) && baselineRestored(), "post-restart revert reproduces the original VMAT fingerprint");
    Check(recooker.Shutdown(), "restarted material recooker shutdown");
    Check(recookGraph.Shutdown(), "restarted material recook graph shutdown");

    Check(artifactCompilers.Unregister() == assets::Result::Success && artifactCompilers.Shutdown(), "baseline material compiler unregisters for tool-fingerprint proof");
    mt::MaterialArtifactCompilerConfig changedArtifactCompilerConfig = artifactCompilerConfig;
    changedArtifactCompilerConfig.offline.targetCapabilities[static_cast<u32>(assets::TargetPlatform::LinuxVulkan)] &= ~shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Numeric16Bit);
    Check(artifactCompilers.Initialize(changedArtifactCompilerConfig) && artifactCompilers.Register(buildSystem) == assets::Result::Success, "changed material tool contract registers");
    Check(recookGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture, {}, &materialIndex) && recooker.Initialize(buildSystem, recookGraph, materialIndex, ArtifactFixture::ResolveOutput, &fixture),
          "tool-fingerprint recooker initialization");
    const assets::AssetChange toolChange{Reference("tools/vanguard-material-compiler", mt::MaterialCompilerToolResourceType)};
    assets::RecookStats toolStats;
    Check(recookOne(toolChange.identity, toolStats) && toolStats.dirtySeeds == 1 && toolStats.affectedOutputs == 3 && toolStats.rootRequests == 1,
          "material tool-fingerprint edit invalidates the complete VSHADER/VPPL/VMAT closure");
    assets::DependencyRecord toolShaderRecord;
    assets::DependencyRecord toolPipelineRecord;
    assets::DependencyRecord toolMaterialRecord;
    Check(findCurrentRecords(toolShaderRecord, toolPipelineRecord, toolMaterialRecord) && toolShaderRecord.buildFingerprint != baselineShaderRecord.buildFingerprint &&
              toolPipelineRecord.buildFingerprint != baselinePipelineRecord.buildFingerprint && toolMaterialRecord.buildFingerprint != baselineMaterialRecord.buildFingerprint,
          "tool recook replaces every material artifact dependency identity");
    Check(recooker.Shutdown(), "changed-tool recooker shutdown");
    Check(recookGraph.Shutdown(), "changed-tool recook graph shutdown");

    Check(artifactCompilers.Unregister() == assets::Result::Success && artifactCompilers.Shutdown() && artifactCompilers.Initialize(artifactCompilerConfig) &&
              artifactCompilers.Register(buildSystem) == assets::Result::Success,
          "baseline material tool contract restores");
    Check(recookGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture, {}, &materialIndex) && recooker.Initialize(buildSystem, recookGraph, materialIndex, ArtifactFixture::ResolveOutput, &fixture),
          "restored-tool recooker initialization");
    assets::RecookStats toolRevertStats;
    Check(recookOne(toolChange.identity, toolRevertStats) && baselineRestored(), "restoring the material tool fingerprint reproduces the baseline closure");
    Check(recooker.Shutdown(), "restored-tool recooker shutdown");
    Check(recookGraph.Shutdown(), "restored-tool recook graph shutdown");

    assets::BuildGraph declaredGraph;
    Check(declaredGraph.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture, {}, &materialIndex), "declared-surface indexed artifact graph initialization");
    assets::GraphRequest declaredShaderRequest = declaredGraph.Request(declaredFixture.programRequest, assets::BuildPriority::High);
    std::array<assets::GraphRequest, 2> declaredPipelineRequests{declaredGraph.Request(declaredFixture.pipelineRequests[0], assets::BuildPriority::High),
                                                                 declaredGraph.Request(declaredFixture.pipelineRequests[1], assets::BuildPriority::High)};
    assets::GraphRequest declaredMaterialRequest = declaredGraph.Request(declaredFixture.materialRequest, assets::BuildPriority::High);
    declaredShaderRequest.Wait();
    for (assets::GraphRequest& request : declaredPipelineRequests)
        request.Wait();
    declaredMaterialRequest.Wait();
    assets::BuildOutput declaredShaderOutput;
    std::array<assets::BuildOutput, 2> declaredPipelineOutputs;
    assets::BuildOutput declaredMaterialOutput;
    const bool declaredGraphBuilt = declaredShaderRequest.HasSucceeded() && declaredPipelineRequests[0].HasSucceeded() && declaredPipelineRequests[1].HasSucceeded() && declaredMaterialRequest.HasSucceeded() &&
                                    declaredShaderRequest.CopyOutput(declaredShaderOutput) && declaredPipelineRequests[0].CopyOutput(declaredPipelineOutputs[0]) &&
                                    declaredPipelineRequests[1].CopyOutput(declaredPipelineOutputs[1]) && declaredMaterialRequest.CopyOutput(declaredMaterialOutput);
    if (!declaredGraphBuilt)
    {
        const auto printReport = [](const char* const label, const assets::GraphRequest& request) noexcept
        {
            assets::BuildReport report;
            if (request.CopyReport(report))
                for (const assets::BuildDiagnostic& diagnostic : report.GetDiagnostics())
                    std::fprintf(stderr, "[materialToolsTests] declared %s report %u: %.*s\n", label, diagnostic.code, static_cast<int>(report.GetMessage(diagnostic).Length()),
                                 report.GetMessage(diagnostic).Data());
        };
        printReport("shader", declaredShaderRequest);
        printReport("pipeline-a", declaredPipelineRequests[0]);
        printReport("pipeline-b", declaredPipelineRequests[1]);
        printReport("material", declaredMaterialRequest);
    }
    Check(declaredGraphBuilt, "full declared surface cooks VSHADER, two VPPL techniques, and VMAT through the indexed graph");
    Check(declaredGraphBuilt && ValidateDeclaredSurfaceArtifacts(declaredFixture, declaredShaderOutput, declaredPipelineOutputs, declaredMaterialOutput),
          "production readers validate every declared scalar/resource family, arrays, matrices, aggregates, defaults, and both techniques");
    declaredShaderRequest.Reset();
    for (assets::GraphRequest& request : declaredPipelineRequests)
        request.Reset();
    declaredMaterialRequest.Reset();
    Check(declaredGraph.Shutdown(), "declared-surface artifact graph shutdown");
    Check(materialIndex.Save() == assets::IndexResult::Success, "declared-surface resource baseline is committed before recooking");

    assets::DependencyRecord committedTextureRecord;
    assets::DependencyRecord committedDeclaredMaterialRecord;
    Check(materialIndex.Find(recookTexture.output, committedTextureRecord) == assets::IndexResult::Success &&
              materialIndex.Find(declaredFixture.material, committedDeclaredMaterialRecord) == assets::IndexResult::Success,
          "referenced VTEX and VMAT records are indexed");
    assets::BuildGraph resourceRecookGraph;
    Check(resourceRecookGraph.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture, {}, &materialIndex), "referenced-resource recook graph initialization");
    assets::IncrementalRecooker resourceRecooker;
    Check(resourceRecooker.Initialize(buildSystem, resourceRecookGraph, materialIndex, DeclaredSurfaceArtifactFixture::ResolveOutput, &declaredFixture),
          "referenced-resource incremental recooker initialization");

    assets::GraphRequest coalescedMaterialA = resourceRecookGraph.Request(declaredFixture.materialRequest, assets::BuildPriority::Background);
    assets::GraphRequest coalescedMaterialB = resourceRecookGraph.Request(declaredFixture.materialRequest, assets::BuildPriority::High);
    Check(coalescedMaterialA && coalescedMaterialB && coalescedMaterialA.IsSameOperation(coalescedMaterialB), "identical material roots coalesce to one build-graph operation");
    coalescedMaterialA.Wait();
    coalescedMaterialB.Wait();
    coalescedMaterialA.Reset();
    coalescedMaterialB.Reset();
    Check(materialIndex.Save() == assets::IndexResult::Success, "coalesced material proof leaves a committed index before recooking");

    recookTexture.value = 2;
    const assets::AssetChange textureChange{recookTexture.source};
    assets::RecookBatch textureBatch = resourceRecooker.Request({&textureChange, 1}, assets::BuildPriority::High);
    textureBatch.Wait();
    const assets::RecookStats textureStats = textureBatch.GetStats();
    assets::DependencyRecord changedTextureRecord;
    assets::DependencyRecord changedDeclaredMaterialRecord;
    Check(textureBatch.GetStatus() == assets::RecookState::Succeeded && textureStats.dirtySeeds == 1 && textureStats.affectedOutputs == 2 && textureStats.rootRequests == 1 &&
              materialIndex.Find(recookTexture.output, changedTextureRecord) == assets::IndexResult::Success &&
              materialIndex.Find(declaredFixture.material, changedDeclaredMaterialRecord) == assets::IndexResult::Success && changedTextureRecord.buildFingerprint != committedTextureRecord.buildFingerprint &&
              changedDeclaredMaterialRecord.buildFingerprint != committedDeclaredMaterialRecord.buildFingerprint,
          "referenced-resource source edit transaction invalidates VTEX and dependent VMAT");
    textureBatch.Reset();
    committedTextureRecord = static_cast<assets::DependencyRecord&&>(changedTextureRecord);
    committedDeclaredMaterialRecord = static_cast<assets::DependencyRecord&&>(changedDeclaredMaterialRecord);

    recookTexture.value = 3;
    recookTexture.block = true;
    recookTexture.started.SetValue(false);
    assets::RecookBatch cancelledTextureBatch = resourceRecooker.Request({&textureChange, 1}, assets::BuildPriority::Background);
    while (!recookTexture.started.GetValue() && !cancelledTextureBatch.HasFinished())
        concurrency::YieldCurrentThread();
    const bool cancellationAccepted = cancelledTextureBatch.Cancel();
    cancelledTextureBatch.Wait();
    assets::DependencyRecord textureAfterCancellation;
    assets::DependencyRecord materialAfterCancellation;
    Check(cancellationAccepted && cancelledTextureBatch.GetStatus() == assets::RecookState::Cancelled && materialIndex.Find(recookTexture.output, textureAfterCancellation) == assets::IndexResult::Success &&
              materialIndex.Find(declaredFixture.material, materialAfterCancellation) == assets::IndexResult::Success && textureAfterCancellation.buildFingerprint == committedTextureRecord.buildFingerprint &&
              materialAfterCancellation.buildFingerprint == committedDeclaredMaterialRecord.buildFingerprint && !materialIndex.HasActiveTransaction() && !materialIndex.HasChanges(),
          "cancelled referenced-resource recook rolls back staged VTEX/VMAT publications");
    cancelledTextureBatch.Reset();
    recookTexture.block = false;

    recookTexture.value = 4;
    recookTexture.available = false;
    assets::RecookBatch unavailableTextureBatch = resourceRecooker.Request({&textureChange, 1}, assets::BuildPriority::High);
    unavailableTextureBatch.Wait();
    assets::DependencyRecord unavailableMaterialRecord;
    bool unavailableDependencyRecorded = false;
    if (materialIndex.Find(declaredFixture.material, unavailableMaterialRecord) == assets::IndexResult::Success)
        for (const assets::BuildDependency& dependency : unavailableMaterialRecord.dependencies)
            unavailableDependencyRecorded =
                unavailableDependencyRecorded || (dependency.identity == recookTexture.output && dependency.content.IsEmpty() && dependency.requirement == assets::DependencyRequirement::Optional);
    Check(unavailableTextureBatch.GetStatus() == assets::RecookState::Succeeded && unavailableDependencyRecorded, "unavailable Optional VTEX is removed from VMAT content closure without failing recook");
    unavailableTextureBatch.Reset();
    Check(resourceRecooker.Shutdown(), "referenced-resource incremental recooker shutdown");
    Check(resourceRecookGraph.Shutdown(), "referenced-resource recook graph shutdown");
    Check(materialIndex.Save() == assets::IndexResult::Success && materialIndex.Shutdown(), "compiler-produced material dependency index persists atomically");

    assets::DependencyIndex packagedIndex;
    Check(packagedIndex.Initialize(materialIndexConfig) == assets::IndexResult::Success, "production dependency index reopens compiler-produced material records");

    assets::DependencyRecord looseShaderRecord;
    assets::DependencyRecord loosePipelineRecord;
    assets::DependencyRecord looseMaterialRecord;
    const bool looseRecordsReady = packagedIndex.Find(fixture.shader, looseShaderRecord) == assets::IndexResult::Success &&
                                   packagedIndex.Find(fixture.pipeline, loosePipelineRecord) == assets::IndexResult::Success &&
                                   packagedIndex.Find(fixture.material, looseMaterialRecord) == assets::IndexResult::Success && shaderOutput.artifacts.Size() == 1 && pipelineOutput.artifacts.Size() == 1 &&
                                   materialOutput.artifacts.Size() == 1;
    Check(looseRecordsReady && looseShaderRecord.buildFingerprint == shaderOutput.buildFingerprint && loosePipelineRecord.buildFingerprint == pipelineOutput.buildFingerprint &&
              looseMaterialRecord.buildFingerprint == materialOutput.buildFingerprint && looseShaderRecord.contentFingerprint == shaderOutput.contentFingerprint &&
              loosePipelineRecord.contentFingerprint == pipelineOutput.contentFingerprint && looseMaterialRecord.contentFingerprint == materialOutput.contentFingerprint,
          "reopened index records identify the exact compiler-produced material artifacts");

    assets::DerivedDataArtifactSource looseSource;
    Check(looseSource.Initialize({looseProofRoot.AsChar(), {}}), "open material persistent DDC for loose reconstruction");
    const filesystem::AbsolutePath looseShaderPath = looseProofRoot.AddFilePath("program.vshader");
    const filesystem::AbsolutePath looseShaderTemporary = looseProofRoot.AddFilePath("program.vshader.tmp");
    const filesystem::AbsolutePath loosePipelinePath = looseProofRoot.AddFilePath("program.vppl");
    const filesystem::AbsolutePath loosePipelineTemporary = looseProofRoot.AddFilePath("program.vppl.tmp");
    const filesystem::AbsolutePath looseMaterialPath = looseProofRoot.AddFilePath("program.vmat");
    const filesystem::AbsolutePath looseMaterialTemporary = looseProofRoot.AddFilePath("program.vmat.tmp");
    assets::LooseResourceMaterializer looseMaterializer;
    const bool looseMaterialized = looseRecordsReady &&
                                   looseMaterializer.Materialize(looseShaderRecord, fixture.shader, looseSource, looseShaderPath, looseShaderTemporary) == assets::LooseMaterializationResult::Success &&
                                   looseMaterializer.Materialize(loosePipelineRecord, fixture.pipeline, looseSource, loosePipelinePath, loosePipelineTemporary) == assets::LooseMaterializationResult::Success &&
                                   looseMaterializer.Materialize(looseMaterialRecord, fixture.material, looseSource, looseMaterialPath, looseMaterialTemporary) == assets::LooseMaterializationResult::Success;
    Check(looseMaterialized && !fileManager.FileExist(looseShaderTemporary) && !fileManager.FileExist(loosePipelineTemporary) && !fileManager.FileExist(looseMaterialTemporary),
          "persistent DDC reconstructs VSHADER, VPPL, and VMAT as atomically published loose files");
    Check(looseMaterialized && FileEquals(looseShaderPath, shaderOutput.artifacts[0].bytes) && FileEquals(loosePipelinePath, pipelineOutput.artifacts[0].bytes) &&
              FileEquals(looseMaterialPath, materialOutput.artifacts[0].bytes),
          "loose material artifact bytes exactly match their independently cached outputs");

    auto looseShaderReader = fileManager.CreateFileReader(looseShaderPath, filesystem::FOF_Buffered);
    auto loosePipelineReader = fileManager.CreateFileReader(loosePipelinePath, filesystem::FOF_Buffered);
    auto looseMaterialReader = fileManager.CreateFileReader(looseMaterialPath, filesystem::FOF_Buffered);
    shaders::ShaderFile looseShader;
    pipelines::PipelineFile loosePipeline;
    materials::MaterialFile looseMaterial;
    const bool looseFilesOpen = looseShaderReader && loosePipelineReader && looseMaterialReader && looseShader.Open(*looseShaderReader) == shaders::Result::Success && looseShader.HasMaterialContract() &&
                                loosePipeline.Open(*loosePipelineReader) == pipelines::Result::Success && pipelines::ValidateShaderCompatibility(loosePipeline, looseShader) == pipelines::Result::Success &&
                                looseMaterial.Open(*looseMaterialReader) == materials::Result::Success;
    const shaders::MaterialContract* const looseContract = looseShader.GetMaterialContract();
    Check(looseFilesOpen && looseContract != nullptr && looseMaterial.GetShader() == fixture.shader && looseMaterial.GetMaterialDomainFingerprint() == looseContract->domainFingerprint &&
              looseMaterial.GetMaterialLayoutFingerprint() == looseContract->layoutFingerprint && looseMaterial.GetTechniques().Count() == 1 && looseMaterial.GetTechniques()[0].pipeline == fixture.pipeline,
          "production readers reopen loose artifacts and preserve shader, pipeline, domain, layout, and technique identities");
    looseShaderReader.Reset();
    loosePipelineReader.Reset();
    looseMaterialReader.Reset();

    assets::DependencyRecord mismatchedMaterialRecord = looseMaterialRecord;
    const bool mismatchReady = looseRecordsReady;
    if (mismatchReady && !mismatchedMaterialRecord.artifacts.Empty())
        ++mismatchedMaterialRecord.artifacts[0].byteCount;
    Check(mismatchReady &&
              looseMaterializer.Materialize(mismatchedMaterialRecord, fixture.material, looseSource, looseMaterialPath, looseMaterialTemporary) == assets::LooseMaterializationResult::DescriptorMismatch &&
              FileEquals(looseMaterialPath, materialOutput.artifacts[0].bytes) && !fileManager.FileExist(looseMaterialTemporary),
          "invalid material artifact metadata cannot replace the last validated loose VMAT");

    assets::PackageManifest materialManifest;
    materialManifest.packageId = 0x4d4154455249414cull;
    materialManifest.codec = packages::Codec::Lz4;
    const bool materialRootAdded = materialManifest.AddRoot({fixture.material, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success;
    assets::PackagePlanner materialPackagePlanner;
    assets::PackageBuildPlan materialPackagePlan;
    const bool materialPackagePlanned = materialRootAdded && materialPackagePlanner.Prepare(materialManifest, packagedIndex, materialPackagePlan) == assets::PackagingResult::Success;
    const assets::PlannedPackageResource* plannedShader = nullptr;
    const assets::PlannedPackageResource* plannedPipeline = nullptr;
    const assets::PlannedPackageResource* plannedMaterial = nullptr;
    for (const assets::PlannedPackageResource& resource : materialPackagePlan.resources)
    {
        if (resource.resource == fixture.shader)
            plannedShader = &resource;
        else if (resource.resource == fixture.pipeline)
            plannedPipeline = &resource;
        else if (resource.resource == fixture.material)
            plannedMaterial = &resource;
    }
    bool materialDependsOnShader = false;
    bool materialDependsOnPipeline = false;
    bool pipelineDependsOnShader = false;
    if (plannedMaterial != nullptr)
        for (const assets::PlannedPackageDependency& dependency : plannedMaterial->dependencies)
        {
            materialDependsOnShader = materialDependsOnShader || (dependency.resource == fixture.shader && dependency.kind == resources::DependencyKind::Required);
            materialDependsOnPipeline = materialDependsOnPipeline || (dependency.resource == fixture.pipeline && dependency.kind == resources::DependencyKind::Required);
        }
    if (plannedPipeline != nullptr)
        for (const assets::PlannedPackageDependency& dependency : plannedPipeline->dependencies)
            pipelineDependsOnShader = pipelineDependsOnShader || (dependency.resource == fixture.shader && dependency.kind == resources::DependencyKind::Required);
    Check(materialPackagePlanned && materialPackagePlan.resources.Size() == 3 && plannedShader != nullptr && plannedPipeline != nullptr && plannedMaterial != nullptr && plannedShader->dependencies.Empty() &&
              materialDependsOnShader && materialDependsOnPipeline && pipelineDependsOnShader &&
              plannedShader->origin == assets::ArtifactSetKey{looseShaderRecord.buildFingerprint, looseShaderRecord.contentFingerprint} &&
              plannedPipeline->origin == assets::ArtifactSetKey{loosePipelineRecord.buildFingerprint, loosePipelineRecord.contentFingerprint} &&
              plannedMaterial->origin == assets::ArtifactSetKey{looseMaterialRecord.buildFingerprint, looseMaterialRecord.contentFingerprint},
          "VMAT root closes over exact required VPPL and VSHADER index records");

    assets::DerivedDataPackageArtifactReader packageArtifactReader(looseSource);
    PackageProofFixture packageFixture{&fixture, &packageArtifactReader};
    const assets::PackageAssemblyCallbacks packageCallbacks{&PackageProofFixture::ResolvePath, &packageFixture, &PackageProofFixture::ReadArtifact, &packageFixture};
    assets::PackageAssembler materialPackageAssembler;
    const filesystem::AbsolutePath materialPackagePath = looseProofRoot.AddFilePath("material-closure.vpak");
    const filesystem::AbsolutePath materialPackageTemporary = looseProofRoot.AddFilePath("material-closure.vpak.tmp");
    const bool materialPackagePublished =
        materialPackagePlanned && materialPackageAssembler.Publish(materialPackagePlan, materialPackagePath, materialPackageTemporary, packageCallbacks) == assets::PackagingResult::Success;
    Check(materialPackagePublished && fileManager.FileExist(materialPackagePath) && !fileManager.FileExist(materialPackageTemporary) &&
              packageArtifactReader.GetOpenCount() == materialPackagePlan.resources.Size(),
          "persistent DDC atomically publishes the real indexed material closure as VPAK");

    auto materialPackagePhysical = fileManager.CreateFileReader(materialPackagePath, filesystem::FOF_Buffered);
    packages::PackageReader materialPackage;
    const bool materialPackageOpened = materialPackagePhysical && materialPackage.Open(*materialPackagePhysical) == packages::Result::Success &&
                                       materialPackage.GetHeader().packageId == materialPackagePlan.packageId && materialPackage.GetHeader().buildId == materialPackagePlan.buildId &&
                                       materialPackage.GetResources().Count() == 3;
    const packages::Resource* packagedShader = materialPackageOpened ? materialPackage.Find("materials/tests/program.vshader") : nullptr;
    const packages::Resource* packagedPipeline = materialPackageOpened ? materialPackage.Find("materials/tests/program.vppl") : nullptr;
    const packages::Resource* packagedMaterial = materialPackageOpened ? materialPackage.Find("materials/tests/program.vmat") : nullptr;
    bool packagedMaterialDependsOnShader = false;
    bool packagedMaterialDependsOnPipeline = false;
    bool packagedPipelineDependsOnShader = false;
    if (packagedMaterial != nullptr)
        for (const packages::Dependency& dependency : materialPackage.GetDependencies(*packagedMaterial))
        {
            packagedMaterialDependsOnShader =
                packagedMaterialDependsOnShader || (dependency.id == fixture.shader.GetPath().Id() && dependency.type == fixture.shader.ExpectedType() && dependency.kind == resources::DependencyKind::Required);
            packagedMaterialDependsOnPipeline = packagedMaterialDependsOnPipeline || (dependency.id == fixture.pipeline.GetPath().Id() && dependency.type == fixture.pipeline.ExpectedType() &&
                                                                                      dependency.kind == resources::DependencyKind::Required);
        }
    if (packagedPipeline != nullptr)
        for (const packages::Dependency& dependency : materialPackage.GetDependencies(*packagedPipeline))
            packagedPipelineDependsOnShader =
                packagedPipelineDependsOnShader || (dependency.id == fixture.shader.GetPath().Id() && dependency.type == fixture.shader.ExpectedType() && dependency.kind == resources::DependencyKind::Required);
    Check(materialPackageOpened && packagedShader != nullptr && packagedPipeline != nullptr && packagedMaterial != nullptr && materialPackage.GetDependencies(*packagedShader).Empty() &&
              packagedMaterialDependsOnShader && packagedMaterialDependsOnPipeline && packagedPipelineDependsOnShader,
          "decoded VPAK index preserves the real material dependency closure and concrete resource types");

    containers::DynamicArray<u8> packagedShaderBytes(memory::pools::Assets::GetInstance());
    containers::DynamicArray<u8> packagedPipelineBytes(memory::pools::Assets::GetInstance());
    containers::DynamicArray<u8> packagedMaterialBytes(memory::pools::Assets::GetInstance());
    const bool packageBytesDecoded = materialPackagePhysical && ReadPackageResource(materialPackage, packagedShader, *materialPackagePhysical, packagedShaderBytes) &&
                                     ReadPackageResource(materialPackage, packagedPipeline, *materialPackagePhysical, packagedPipelineBytes) &&
                                     ReadPackageResource(materialPackage, packagedMaterial, *materialPackagePhysical, packagedMaterialBytes);
    Check(packageBytesDecoded && EqualBytes(packagedShaderBytes, shaderOutput.artifacts[0].bytes) && EqualBytes(packagedPipelineBytes, pipelineOutput.artifacts[0].bytes) &&
              EqualBytes(packagedMaterialBytes, materialOutput.artifacts[0].bytes) && FileEquals(looseShaderPath, packagedShaderBytes) && FileEquals(loosePipelinePath, packagedPipelineBytes) &&
              FileEquals(looseMaterialPath, packagedMaterialBytes),
          "decoded VPAK, standalone loose files, and VDDC artifacts have identical logical resource bytes");

    shaders::ShaderFile packagedShaderFile;
    pipelines::PipelineFile packagedPipelineFile;
    materials::MaterialFile packagedMaterialFile;
    bool packagedShaderOpened = false;
    bool packagedPipelineOpened = false;
    bool packagedMaterialOpened = false;
    if (materialPackagePhysical && packagedShader != nullptr)
    {
        packages::ResourceFileReader reader;
        packagedShaderOpened = reader.Open(materialPackage, *packagedShader, *materialPackagePhysical) == packages::Result::Success && packagedShaderFile.Open(reader) == shaders::Result::Success;
        reader.Close();
    }
    if (materialPackagePhysical && packagedPipeline != nullptr)
    {
        packages::ResourceFileReader reader;
        packagedPipelineOpened = reader.Open(materialPackage, *packagedPipeline, *materialPackagePhysical) == packages::Result::Success && packagedPipelineFile.Open(reader) == pipelines::Result::Success;
        reader.Close();
    }
    if (materialPackagePhysical && packagedMaterial != nullptr)
    {
        packages::ResourceFileReader reader;
        packagedMaterialOpened = reader.Open(materialPackage, *packagedMaterial, *materialPackagePhysical) == packages::Result::Success && packagedMaterialFile.Open(reader) == materials::Result::Success;
        reader.Close();
    }
    const shaders::MaterialContract* const packagedContract = packagedShaderFile.GetMaterialContract();
    Check(packagedShaderOpened && packagedPipelineOpened && packagedMaterialOpened && packagedContract != nullptr &&
              pipelines::ValidateShaderCompatibility(packagedPipelineFile, packagedShaderFile) == pipelines::Result::Success && packagedMaterialFile.GetShader() == fixture.shader &&
              packagedMaterialFile.GetMaterialDomainFingerprint() == packagedContract->domainFingerprint && packagedMaterialFile.GetMaterialLayoutFingerprint() == packagedContract->layoutFingerprint &&
              packagedMaterialFile.GetTechniques().Count() == 1 && packagedMaterialFile.GetTechniques()[0].pipeline == fixture.pipeline,
          "production readers reopen and cross-validate VSHADER, VPPL, and VMAT directly from VPAK");
    materialPackage.Close();
    materialPackagePhysical.Reset();

    containers::DynamicArray<u8> validatedPackageBytes(memory::pools::Assets::GetInstance());
    const bool capturedValidatedPackage = ReadFileBytes(materialPackagePath, validatedPackageBytes);
    packageFixture.corruptArtifactSize = true;
    Check(capturedValidatedPackage && materialPackageAssembler.Publish(materialPackagePlan, materialPackagePath, materialPackageTemporary, packageCallbacks) == assets::PackagingResult::InvalidArtifactData &&
              FileEquals(materialPackagePath, validatedPackageBytes) && !fileManager.FileExist(materialPackageTemporary),
          "invalid indexed artifact data cannot replace the last validated material VPAK");
    packageFixture.corruptArtifactSize = false;

    assets::DependencyRecord declaredShaderRecord;
    std::array<assets::DependencyRecord, 2> declaredPipelineRecords;
    assets::DependencyRecord declaredMaterialRecord;
    const bool declaredRecordsReady = declaredGraphBuilt && packagedIndex.Find(declaredFixture.shader, declaredShaderRecord) == assets::IndexResult::Success &&
                                      packagedIndex.Find(declaredFixture.pipelines[0], declaredPipelineRecords[0]) == assets::IndexResult::Success &&
                                      packagedIndex.Find(declaredFixture.pipelines[1], declaredPipelineRecords[1]) == assets::IndexResult::Success &&
                                      packagedIndex.Find(declaredFixture.material, declaredMaterialRecord) == assets::IndexResult::Success;
    const filesystem::AbsolutePath declaredShaderPath = looseProofRoot.AddFilePath("declared_surface.vshader");
    const filesystem::AbsolutePath declaredShaderTemporary = looseProofRoot.AddFilePath("declared_surface.vshader.tmp");
    const filesystem::AbsolutePath declaredPipelineAPath = looseProofRoot.AddFilePath("declared_a.vppl");
    const filesystem::AbsolutePath declaredPipelineATemporary = looseProofRoot.AddFilePath("declared_a.vppl.tmp");
    const filesystem::AbsolutePath declaredPipelineBPath = looseProofRoot.AddFilePath("declared_b.vppl");
    const filesystem::AbsolutePath declaredPipelineBTemporary = looseProofRoot.AddFilePath("declared_b.vppl.tmp");
    const filesystem::AbsolutePath declaredMaterialPath = looseProofRoot.AddFilePath("declared_surface.vmat");
    const filesystem::AbsolutePath declaredMaterialTemporary = looseProofRoot.AddFilePath("declared_surface.vmat.tmp");
    const bool declaredLooseMaterialized =
        declaredRecordsReady &&
        looseMaterializer.Materialize(declaredShaderRecord, declaredFixture.shader, looseSource, declaredShaderPath, declaredShaderTemporary) == assets::LooseMaterializationResult::Success &&
        looseMaterializer.Materialize(declaredPipelineRecords[0], declaredFixture.pipelines[0], looseSource, declaredPipelineAPath, declaredPipelineATemporary) == assets::LooseMaterializationResult::Success &&
        looseMaterializer.Materialize(declaredPipelineRecords[1], declaredFixture.pipelines[1], looseSource, declaredPipelineBPath, declaredPipelineBTemporary) == assets::LooseMaterializationResult::Success &&
        looseMaterializer.Materialize(declaredMaterialRecord, declaredFixture.material, looseSource, declaredMaterialPath, declaredMaterialTemporary) == assets::LooseMaterializationResult::Success;
    Check(declaredLooseMaterialized && FileEquals(declaredShaderPath, declaredShaderOutput.artifacts[0].bytes) && FileEquals(declaredPipelineAPath, declaredPipelineOutputs[0].artifacts[0].bytes) &&
              FileEquals(declaredPipelineBPath, declaredPipelineOutputs[1].artifacts[0].bytes) && FileEquals(declaredMaterialPath, declaredMaterialOutput.artifacts[0].bytes) &&
              !fileManager.FileExist(declaredShaderTemporary) && !fileManager.FileExist(declaredPipelineATemporary) && !fileManager.FileExist(declaredPipelineBTemporary) &&
              !fileManager.FileExist(declaredMaterialTemporary),
          "persistent DDC reconstructs the complete declared-surface artifact set byte-exactly and atomically");

    assets::PackageManifest declaredManifest;
    declaredManifest.packageId = 0x4445434c41524544ull;
    declaredManifest.codec = packages::Codec::Lz4;
    assets::PackageBuildPlan declaredPackagePlan;
    const bool declaredPackagePlanned = declaredManifest.AddRoot({declaredFixture.material, assets::PackageRootFlags::Startup}) == assets::PackagingResult::Success &&
                                        materialPackagePlanner.Prepare(declaredManifest, packagedIndex, declaredPackagePlan) == assets::PackagingResult::Success;
    bool planHasDeclaredShader = false;
    bool planHasDeclaredMaterial = false;
    u32 planDeclaredPipelines = 0;
    bool planContainsExternalTexture = false;
    for (const assets::PlannedPackageResource& resource : declaredPackagePlan.resources)
    {
        planHasDeclaredShader = planHasDeclaredShader || resource.resource == declaredFixture.shader;
        planHasDeclaredMaterial = planHasDeclaredMaterial || resource.resource == declaredFixture.material;
        planDeclaredPipelines += resource.resource == declaredFixture.pipelines[0] || resource.resource == declaredFixture.pipelines[1] ? 1u : 0u;
        planContainsExternalTexture = planContainsExternalTexture || resource.resource.ExpectedType() == textures::TextureResourceType;
    }
    Check(declaredPackagePlanned && declaredPackagePlan.resources.Size() == 4 && planHasDeclaredShader && planHasDeclaredMaterial && planDeclaredPipelines == 2 && !planContainsExternalTexture,
          "declared-surface VMAT root closes over exactly two required VPPLs and one VSHADER without absent Optional/Soft textures");

    assets::DerivedDataPackageArtifactReader declaredPackageArtifactReader(looseSource);
    DeclaredSurfacePackageProofFixture declaredPackageFixture{&declaredFixture, &declaredPackageArtifactReader};
    const assets::PackageAssemblyCallbacks declaredPackageCallbacks{&DeclaredSurfacePackageProofFixture::ResolvePath, &declaredPackageFixture, &DeclaredSurfacePackageProofFixture::ReadArtifact,
                                                                    &declaredPackageFixture};
    const filesystem::AbsolutePath declaredPackagePath = looseProofRoot.AddFilePath("declared-surface-closure.vpak");
    const filesystem::AbsolutePath declaredPackageTemporary = looseProofRoot.AddFilePath("declared-surface-closure.vpak.tmp");
    const bool declaredPackagePublished =
        declaredPackagePlanned && materialPackageAssembler.Publish(declaredPackagePlan, declaredPackagePath, declaredPackageTemporary, declaredPackageCallbacks) == assets::PackagingResult::Success;
    auto declaredPackagePhysical = fileManager.CreateFileReader(declaredPackagePath, filesystem::FOF_Buffered);
    packages::PackageReader declaredPackage;
    const bool declaredPackageOpened =
        declaredPackagePublished && declaredPackagePhysical && declaredPackage.Open(*declaredPackagePhysical) == packages::Result::Success && declaredPackage.GetResources().Size() == 4;
    const packages::Resource* const packagedDeclaredShader = declaredPackageOpened ? declaredPackage.Find("materials/tests/declared_surface.vshader") : nullptr;
    const packages::Resource* const packagedDeclaredPipelineA = declaredPackageOpened ? declaredPackage.Find("materials/tests/declared_a.vppl") : nullptr;
    const packages::Resource* const packagedDeclaredPipelineB = declaredPackageOpened ? declaredPackage.Find("materials/tests/declared_b.vppl") : nullptr;
    const packages::Resource* const packagedDeclaredMaterial = declaredPackageOpened ? declaredPackage.Find("materials/tests/declared_surface.vmat") : nullptr;
    containers::DynamicArray<u8> packagedDeclaredShaderBytes(memory::pools::Assets::GetInstance());
    std::array<containers::DynamicArray<u8>, 2> packagedDeclaredPipelineBytes{containers::DynamicArray<u8>(memory::pools::Assets::GetInstance()),
                                                                              containers::DynamicArray<u8>(memory::pools::Assets::GetInstance())};
    containers::DynamicArray<u8> packagedDeclaredMaterialBytes(memory::pools::Assets::GetInstance());
    const bool declaredPackageDecoded = declaredPackagePhysical && ReadPackageResource(declaredPackage, packagedDeclaredShader, *declaredPackagePhysical, packagedDeclaredShaderBytes) &&
                                        ReadPackageResource(declaredPackage, packagedDeclaredPipelineA, *declaredPackagePhysical, packagedDeclaredPipelineBytes[0]) &&
                                        ReadPackageResource(declaredPackage, packagedDeclaredPipelineB, *declaredPackagePhysical, packagedDeclaredPipelineBytes[1]) &&
                                        ReadPackageResource(declaredPackage, packagedDeclaredMaterial, *declaredPackagePhysical, packagedDeclaredMaterialBytes);
    Check(declaredPackageOpened && packagedDeclaredShader != nullptr && packagedDeclaredPipelineA != nullptr && packagedDeclaredPipelineB != nullptr && packagedDeclaredMaterial != nullptr &&
              declaredPackageDecoded && EqualBytes(packagedDeclaredShaderBytes, declaredShaderOutput.artifacts[0].bytes) &&
              EqualBytes(packagedDeclaredPipelineBytes[0], declaredPipelineOutputs[0].artifacts[0].bytes) && EqualBytes(packagedDeclaredPipelineBytes[1], declaredPipelineOutputs[1].artifacts[0].bytes) &&
              EqualBytes(packagedDeclaredMaterialBytes, declaredMaterialOutput.artifacts[0].bytes) && declaredPackageArtifactReader.GetOpenCount() == declaredPackagePlan.resources.Size() &&
              !fileManager.FileExist(declaredPackageTemporary),
          "indexed VPAK decodes the complete declared surface with byte identity to DDC and loose artifacts");
    if (declaredPackageDecoded)
    {
        filesystem::MemoryFileReader packagedShaderReader(packagedDeclaredShaderBytes, 0);
        filesystem::MemoryFileReader packagedPipelineAReader(packagedDeclaredPipelineBytes[0], 0);
        filesystem::MemoryFileReader packagedPipelineBReader(packagedDeclaredPipelineBytes[1], 0);
        filesystem::MemoryFileReader packagedMaterialReader(packagedDeclaredMaterialBytes, 0);
        shaders::ShaderFile packagedSurfaceShader;
        pipelines::PipelineFile packagedSurfacePipelineA;
        pipelines::PipelineFile packagedSurfacePipelineB;
        materials::MaterialFile packagedSurfaceMaterial;
        Check(packagedSurfaceShader.Open(packagedShaderReader) == shaders::Result::Success && packagedSurfacePipelineA.Open(packagedPipelineAReader) == pipelines::Result::Success &&
                  packagedSurfacePipelineB.Open(packagedPipelineBReader) == pipelines::Result::Success && packagedSurfaceMaterial.Open(packagedMaterialReader) == materials::Result::Success &&
                  pipelines::ValidateShaderCompatibility(packagedSurfacePipelineA, packagedSurfaceShader) == pipelines::Result::Success &&
                  pipelines::ValidateShaderCompatibility(packagedSurfacePipelineB, packagedSurfaceShader) == pipelines::Result::Success && packagedSurfaceMaterial.GetShader() == declaredFixture.shader &&
                  packagedSurfaceMaterial.GetTechniques().Size() == 2,
              "production readers reopen and mutually validate the full declared surface decoded from VPAK");
    }
    declaredPackage.Close();
    declaredPackagePhysical.Reset();
    declaredPackageArtifactReader.Reset();

    Check(packagedIndex.Shutdown(), "material packaging dependency index shutdown");
    packageArtifactReader.Reset();
    Check(looseSource.Shutdown(), "material loose artifact source shutdown");

    assets::BuildGraph diagnosticGraph;
    Check(diagnosticGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture), "attributed-diagnostic graph initialization");
    assets::GraphRequest invalidProgramRequest = diagnosticGraph.Request(fixture.invalidProgramRequest, assets::BuildPriority::High);
    invalidProgramRequest.Wait();
    assets::BuildReport invalidProgramReport;
    bool attributedDiagnostic = false;
    if (invalidProgramRequest.CopyReport(invalidProgramReport))
    {
        for (const assets::BuildDiagnostic& diagnostic : invalidProgramReport.GetDiagnostics())
        {
            if (diagnostic.code != 0x4d500002u)
                continue;
            for (const assets::BuildDiagnosticLocation& location : invalidProgramReport.GetLocations(diagnostic))
                attributedDiagnostic =
                    attributedDiagnostic || (location.resource == fixture.invalidProgramSource && location.subject == AuthoredInputNode && location.detail == AuthoredInputPin && location.line != 0);
        }
    }
    Check(!invalidProgramRequest.HasSucceeded() && attributedDiagnostic, "Slang diagnostics map generated source lines back to authored material nodes and pins");
    invalidProgramRequest.Reset();
    Check(diagnosticGraph.Shutdown(), "attributed-diagnostic graph shutdown");

    fixture.RefreshRequests(fixture.changedMaterialBytes);
    assets::BuildGraph changedGraph;
    Check(changedGraph.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture), "changed-value graph initialization");
    assets::GraphRequest changedShader = changedGraph.Request(fixture.programRequest);
    assets::GraphRequest changedPipeline = changedGraph.Request(fixture.pipelineRequest);
    assets::GraphRequest changedMaterial = changedGraph.Request(fixture.materialRequest);
    changedShader.Wait();
    changedPipeline.Wait();
    changedMaterial.Wait();
    assets::BuildOutput changedShaderOutput;
    assets::BuildOutput changedPipelineOutput;
    assets::BuildOutput changedMaterialOutput;
    Check(changedShader.CopyOutput(changedShaderOutput) && changedPipeline.CopyOutput(changedPipelineOutput) && changedMaterial.CopyOutput(changedMaterialOutput) &&
              changedShaderOutput.disposition == assets::BuildDisposition::CacheHit && changedPipelineOutput.disposition == assets::BuildDisposition::CacheHit &&
              changedMaterialOutput.disposition == assets::BuildDisposition::CacheHit,
          "a repeated dynamic-value request reuses the independently cached VSHADER, VPPL, and recooked VMAT");
    changedShader.Reset();
    changedPipeline.Reset();
    changedMaterial.Reset();
    Check(changedGraph.Shutdown(), "changed-value graph shutdown");

    BeginSuite("adversarial canonical inputs");
    const auto prepares = [&buildSystem](const assets::BuildRequest& prototype, const containers::ArraySpan<const u8> bytes) noexcept
    {
        assets::BuildRequest request = prototype;
        request.source.content = bytes;
        assets::BuildPlan plan;
        return buildSystem.Prepare(request, plan) == assets::Result::Success && plan.IsPrepared();
    };
    const auto rejectsMalformedMatrix = [&prepares](const assets::BuildRequest& prototype) noexcept
    {
        containers::DynamicArray<u8> mutation(memory::pools::Assets::GetInstance());
        if (!CopyBytes(prototype.source.content, mutation) || mutation.Empty())
            return false;
        mutation[0] ^= 0xffu;
        const bool badMagicRejected = !prepares(prototype, mutation);
        if (!CopyBytes(prototype.source.content, mutation) || mutation.Empty())
            return false;
        mutation.Resize(mutation.Size() - 1u);
        const bool truncationRejected = !prepares(prototype, mutation);
        if (!CopyBytes(prototype.source.content, mutation))
            return false;
        const u32 previous = mutation.Size();
        mutation.PushBack(0xa5u);
        const bool trailingByteRejected = mutation.Size() == previous + 1u && !prepares(prototype, mutation);
        return badMagicRejected && truncationRejected && trailingByteRejected;
    };
    Check(rejectsMalformedMatrix(fixture.programRequest) && rejectsMalformedMatrix(fixture.pipelineRequest) && rejectsMalformedMatrix(fixture.materialRequest),
          "MPGI, MPLI, and MVLI reject bad magic, truncation, and trailing bytes without preparing a build");

    std::array<shader_tools::EntryPoint, 33> boundedEntries;
    for (shader_tools::EntryPoint& entry : boundedEntries)
        entry = {"MaterialCompute", shaders::ShaderStage::Compute};
    mt::MaterialProgramInput boundedProgram;
    boundedProgram.sourceName = "materials/tests/bounded_program.slang";
    boundedProgram.moduleName = "bounded_program";
    boundedProgram.program = 0x424f554e444d5047ull;
    boundedProgram.permutation = crypto::Sha256("bounded-program", sizeof("bounded-program") - 1u);
    boundedProgram.expectedDomain = fixture.canonical.domainContract;
    boundedProgram.expectedDomainFingerprint = fixture.canonical.domainContractFingerprint;
    boundedProgram.requiredCapabilities = fixture.canonical.domainContract.requiredCapabilities;
    boundedProgram.source = {reinterpret_cast<const u8*>(ComputeMaterialContractSource), sizeof(ComputeMaterialContractSource) - 1u};
    boundedProgram.settings.target = shader_tools::Target::D3D12Dxil;
    containers::DynamicArray<u8> boundedProgramBytes(memory::pools::Assets::GetInstance());
    containers::DynamicArray<u8> excessiveProgramBytes(memory::pools::Assets::GetInstance());
    boundedProgram.entryPoints = {boundedEntries.data(), 32};
    const bool boundedProgramEncoded = mt::EncodeMaterialProgramInput(boundedProgram, boundedProgramBytes) == mt::MaterialInputResult::Success;
    boundedProgram.entryPoints = {boundedEntries.data(), static_cast<u32>(boundedEntries.size())};
    const bool excessiveProgramEncoded = mt::EncodeMaterialProgramInput(boundedProgram, excessiveProgramBytes) == mt::MaterialInputResult::Success;
    const assets::BuildRequest boundedProgramRequest{{Reference("materials/tests/bounded_program.mpgi", mt::MaterialProgramInputResourceType), boundedProgramBytes, {}},
                                                     Reference("materials/tests/bounded_program.vshader", shaders::ShaderResourceType),
                                                     assets::TargetPlatform::WindowsD3D12,
                                                     {}};
    Check(boundedProgramEncoded && excessiveProgramEncoded && prepares(boundedProgramRequest, boundedProgramBytes) && !prepares(boundedProgramRequest, excessiveProgramBytes),
          "MPGI accepts exactly 32 entry points and rejects the 33rd before allocation");

    std::array<pipelines::VertexStream, 65> boundedStreams;
    for (u32 index = 0; index < boundedStreams.size(); ++index)
    {
        boundedStreams[index].binding = index;
        boundedStreams[index].stride = 4;
    }
    pipelines::BuildDescription boundedPipelineRecipe;
    boundedPipelineRecipe.kind = pipelines::PipelineKind::Compute;
    boundedPipelineRecipe.name = 0x424f554e444d504cull;
    mt::MaterialPipelineInput boundedPipeline{fixture.shader, boundedPipelineRecipe};
    containers::DynamicArray<u8> boundedPipelineBytes(memory::pools::Assets::GetInstance());
    containers::DynamicArray<u8> excessivePipelineBytes(memory::pools::Assets::GetInstance());
    boundedPipeline.recipe.vertexStreams = {boundedStreams.data(), 64};
    const bool boundedPipelineEncoded = mt::EncodeMaterialPipelineInput(boundedPipeline, boundedPipelineBytes) == mt::MaterialInputResult::Success;
    boundedPipeline.recipe.vertexStreams = {boundedStreams.data(), static_cast<u32>(boundedStreams.size())};
    const bool excessivePipelineEncoded = mt::EncodeMaterialPipelineInput(boundedPipeline, excessivePipelineBytes) == mt::MaterialInputResult::Success;
    const assets::BuildRequest boundedPipelineRequest{{Reference("materials/tests/bounded_pipeline.mpli", mt::MaterialPipelineInputResourceType), boundedPipelineBytes, {}},
                                                      Reference("materials/tests/bounded_pipeline.vppl", pipelines::PipelineResourceType),
                                                      assets::TargetPlatform::WindowsD3D12,
                                                      {}};
    Check(boundedPipelineEncoded && excessivePipelineEncoded && prepares(boundedPipelineRequest, boundedPipelineBytes) && !prepares(boundedPipelineRequest, excessivePipelineBytes),
          "MPLI accepts exactly 64 vertex streams and rejects the 65th before allocation");

    containers::DynamicArray<materials::TechniqueBuildRecord> boundedTechniques(memory::pools::Assets::GetInstance());
    boundedTechniques.Resize(257);
    for (u32 index = 0; index < boundedTechniques.Size(); ++index)
        boundedTechniques[index] = {0x424f550000000000ull + index + 1u, fixture.pipeline};
    mt::MaterialValueInput boundedMaterial{0x424f554e444d564cull, fixture.shader, {boundedTechniques.TypedData(), 256}, {}, {}};
    containers::DynamicArray<u8> boundedMaterialBytes(memory::pools::Assets::GetInstance());
    containers::DynamicArray<u8> excessiveMaterialBytes(memory::pools::Assets::GetInstance());
    const bool boundedMaterialEncoded = mt::EncodeMaterialValueInput(boundedMaterial, boundedMaterialBytes) == mt::MaterialInputResult::Success;
    boundedMaterial.techniques = boundedTechniques;
    const bool excessiveMaterialEncoded = mt::EncodeMaterialValueInput(boundedMaterial, excessiveMaterialBytes) == mt::MaterialInputResult::Success;
    const assets::BuildRequest boundedMaterialRequest{{Reference("materials/tests/bounded_material.mvli", mt::MaterialValueInputResourceType), boundedMaterialBytes, {}},
                                                      Reference("materials/tests/bounded_material.vmat", materials::MaterialResourceType),
                                                      assets::TargetPlatform::WindowsD3D12,
                                                      {}};
    Check(boundedMaterialEncoded && excessiveMaterialEncoded && prepares(boundedMaterialRequest, boundedMaterialBytes) && !prepares(boundedMaterialRequest, excessiveMaterialBytes),
          "MVLI accepts exactly 256 techniques and rejects the 257th before allocation");

    BeginSuite("preview lifecycle");
    fixture.RefreshRequests(fixture.changedMaterialBytes);
    mt::MaterialPreviewService preview;
    Check(preview.Initialize(buildSystem, ArtifactFixture::Resolve, &fixture), "headless material preview initialization");
    const assets::BuildRequest previewPipelines[]{fixture.pipelineRequest};
    const mt::MaterialPreviewBuildSet previewSet{fixture.programRequest, previewPipelines, fixture.materialRequest};
    const u64 validRevision = preview.Submit(previewSet);
    preview.Wait();
    assets::BuildOutput lastValidMaterial;
    Check(validRevision != 0 && preview.State() == mt::MaterialPreviewState::Valid && preview.LastValidRevision() == validRevision && preview.CopyLastValidMaterial(lastValidMaterial),
          "preview accepts only a fully validated material artifact set");
    u64 appliedRevision = 0;
    Check(preview.Apply(AppliedRevision, &appliedRevision) && appliedRevision == validRevision, "preview Apply commits the authored revision rather than transient bytes");
    u64 rejectedAppliedRevision = 0;
    Check(!preview.Apply(RejectAppliedRevision, &rejectedAppliedRevision) && rejectedAppliedRevision == validRevision && preview.LastValidRevision() == validRevision,
          "a rejected Apply leaves the accepted preview revision intact");
    fixture.RefreshRequests(fixture.invalidMaterialBytes);
    const assets::BuildRequest invalidPreviewPipelines[]{fixture.pipelineRequest};
    const mt::MaterialPreviewBuildSet invalidPreviewSet{fixture.programRequest, invalidPreviewPipelines, fixture.materialRequest};
    const u64 invalidRevision = preview.Submit(invalidPreviewSet);
    preview.Wait();
    Check(invalidRevision > validRevision && preview.State() == mt::MaterialPreviewState::Failed && preview.LastValidRevision() == validRevision && preview.CopyLastValidMaterial(lastValidMaterial),
          "failed preview revisions preserve the bounded last-valid result");
    appliedRevision = 0;
    Check(preview.Apply(AppliedRevision, &appliedRevision) && appliedRevision == validRevision, "Apply after a failed revision still commits the last fully valid authored revision");
    Check(preview.Shutdown(), "headless material preview shutdown");

    fixture.RefreshRequests(fixture.changedMaterialBytes);
    const assets::BuildRequest declaredPreviewPipelines[]{declaredFixture.pipelineRequests[0], declaredFixture.pipelineRequests[1]};
    const mt::MaterialPreviewBuildSet declaredPreviewSet{declaredFixture.programRequest, declaredPreviewPipelines, declaredFixture.materialRequest};
    mt::MaterialPreviewLimits onePipelineLimit;
    onePipelineLimit.maximumPipelines = 1;
    mt::MaterialPreviewService pipelineLimitedPreview;
    const bool pipelineLimitInitialized = pipelineLimitedPreview.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture, {}, onePipelineLimit);
    const u64 pipelineLimitRevision = pipelineLimitInitialized ? pipelineLimitedPreview.Submit(declaredPreviewSet) : ~u64{0};
    const u64 pipelineLimitCurrent = pipelineLimitedPreview.CurrentRevision();
    const mt::MaterialPreviewState pipelineLimitState = pipelineLimitedPreview.State();
    const bool pipelineLimitShutdown = pipelineLimitedPreview.Shutdown();
    Check(pipelineLimitInitialized && pipelineLimitRevision == 0 && pipelineLimitCurrent == 0 && pipelineLimitState == mt::MaterialPreviewState::Idle && pipelineLimitShutdown,
          "preview rejects an over-limit pipeline set without disturbing lifecycle state");

    const u64 simpleArtifactBytes = ArtifactBytes(shaderOutput) + ArtifactBytes(pipelineOutput) + ArtifactBytes(materialOutput);
    const u64 declaredArtifactBytes = ArtifactBytes(declaredShaderOutput) + ArtifactBytes(declaredPipelineOutputs[0]) + ArtifactBytes(declaredPipelineOutputs[1]) + ArtifactBytes(declaredMaterialOutput);
    mt::MaterialPreviewLimits retentionLimit;
    retentionLimit.maximumRetainedArtifactBytes = simpleArtifactBytes;
    mt::MaterialPreviewService budgetPreview;
    const bool budgetPreviewInitialized =
        simpleArtifactBytes != 0 && declaredArtifactBytes > simpleArtifactBytes && budgetPreview.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture, {}, retentionLimit);
    const u64 budgetValidRevision = budgetPreviewInitialized ? budgetPreview.Submit(previewSet) : 0;
    budgetPreview.Wait();
    const u64 budgetRejectedRevision = budgetPreviewInitialized ? budgetPreview.Submit(declaredPreviewSet) : 0;
    budgetPreview.Wait();
    appliedRevision = 0;
    const bool budgetPreserved = budgetPreview.State() == mt::MaterialPreviewState::Failed && budgetPreview.LastValidRevision() == budgetValidRevision &&
                                 budgetPreview.CopyLastValidMaterial(lastValidMaterial) && budgetPreview.Apply(AppliedRevision, &appliedRevision) && appliedRevision == budgetValidRevision;
    const bool budgetShutdown = budgetPreview.Shutdown();
    Check(budgetPreviewInitialized && budgetValidRevision != 0 && budgetRejectedRevision > budgetValidRevision && budgetPreserved && budgetShutdown,
          "retention-budget rejection is transactional and preserves the prior valid material and Apply revision");

    const auto waitForTextureCompile = [&recookTexture]() noexcept
    {
        for (u32 attempt = 0; attempt < 1000000u && !recookTexture.started.GetValue(); ++attempt)
            concurrency::YieldCurrentThread();
        return recookTexture.started.GetValue();
    };

    recookTexture.available = true;
    recookTexture.value = 0xe1u;
    recookTexture.block = true;
    recookTexture.started.SetValue(false);
    mt::MaterialPreviewService replacementPreview;
    const bool replacementInitialized = replacementPreview.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture);
    const u64 staleRevision = replacementInitialized ? replacementPreview.Submit(declaredPreviewSet) : 0;
    const bool staleRevisionStarted = staleRevision != 0 && waitForTextureCompile();
    const u64 replacementRevision = staleRevisionStarted ? replacementPreview.Submit(previewSet) : 0;
    replacementPreview.Wait();
    const mt::MaterialPreviewState replacementState = replacementPreview.State();
    const u64 acceptedReplacementRevision = replacementPreview.LastValidRevision();
    const bool replacementShutdown = replacementPreview.Shutdown();
    Check(replacementInitialized && staleRevisionStarted && replacementRevision > staleRevision && replacementState == mt::MaterialPreviewState::Valid && acceptedReplacementRevision == replacementRevision &&
              replacementShutdown,
          "active preview replacement cancels obsolete work and excludes its stale completion");
    recookTexture.block = false;

    recookTexture.value = 0xe2u;
    recookTexture.block = true;
    recookTexture.started.SetValue(false);
    mt::MaterialPreviewService cancellationPreview;
    const bool cancellationInitialized = cancellationPreview.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture);
    const u64 cancellationBaseline = cancellationInitialized ? cancellationPreview.Submit(previewSet) : 0;
    cancellationPreview.Wait();
    const u64 cancelledRevision = cancellationInitialized ? cancellationPreview.Submit(declaredPreviewSet) : 0;
    const bool cancelledRevisionStarted = cancelledRevision != 0 && waitForTextureCompile();
    const bool previewCancellationAccepted = cancelledRevisionStarted && cancellationPreview.Cancel();
    appliedRevision = 0;
    const bool cancellationPreserved = cancellationPreview.State() == mt::MaterialPreviewState::Cancelled && cancellationPreview.LastValidRevision() == cancellationBaseline &&
                                       cancellationPreview.CopyLastValidMaterial(lastValidMaterial) && cancellationPreview.Apply(AppliedRevision, &appliedRevision) && appliedRevision == cancellationBaseline;
    const bool cancellationShutdown = cancellationPreview.Shutdown();
    Check(cancellationInitialized && cancellationBaseline != 0 && cancelledRevision > cancellationBaseline && previewCancellationAccepted && cancellationPreserved && cancellationShutdown,
          "explicit preview cancellation releases active work and preserves the last-valid Apply revision");
    recookTexture.block = false;

    recookTexture.value = 0xe3u;
    recookTexture.block = true;
    recookTexture.started.SetValue(false);
    mt::MaterialPreviewService shutdownPreview;
    const bool shutdownInitialized = shutdownPreview.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &declaredFixture);
    const u64 shutdownRevision = shutdownInitialized ? shutdownPreview.Submit(declaredPreviewSet) : 0;
    const bool shutdownRevisionStarted = shutdownRevision != 0 && waitForTextureCompile();
    const bool activeShutdown = shutdownPreview.Shutdown();
    Check(shutdownInitialized && shutdownRevisionStarted && activeShutdown && !shutdownPreview.IsInitialized() && shutdownPreview.State() == mt::MaterialPreviewState::Idle &&
              shutdownPreview.Submit(declaredPreviewSet) == 0,
          "preview shutdown cancels and joins an active generated-resource build and returns to inert state");
    recookTexture.block = false;
    recookTexture.available = false;

    BeginSuite("cross-target final closure");
    Check(CompileRuntimeMaterialAccessorContract(working), "the actual paged GPU material, resource-role, and parameter-word accessors compile for DXIL and SPIR-V");
    Check(mt::tests::RunStaticSurfaceProof(working), "static surface vertex/GBuffer/depth wrappers compile generated parameter-only and sampled graphs for DXIL and SPIR-V");
    DeclaredSurfaceArtifactFixture windowsVulkanFixture(assets::TargetPlatform::WindowsVulkan, shader_tools::Target::VulkanSpirV);
    DeclaredSurfaceArtifactFixture linuxVulkanFixture(assets::TargetPlatform::LinuxVulkan, shader_tools::Target::VulkanSpirV);
    const auto targetRequestsMatch = [](const DeclaredSurfaceArtifactFixture& targetFixture, const assets::TargetPlatform target) noexcept
    {
        return targetFixture.programRequest.target == target && targetFixture.pipelineRequests[0].target == target && targetFixture.pipelineRequests[1].target == target &&
               targetFixture.materialRequest.target == target;
    };
    Check(windowsVulkanFixture.valid && linuxVulkanFixture.valid && targetRequestsMatch(windowsVulkanFixture, assets::TargetPlatform::WindowsVulkan) &&
              targetRequestsMatch(linuxVulkanFixture, assets::TargetPlatform::LinuxVulkan),
          "the full declared material surface produces canonical requests for every advertised Vulkan platform");

    const auto cookTarget = [&buildSystem](DeclaredSurfaceArtifactFixture& targetFixture, const shaders::NativeFormat expectedFormat, assets::BuildOutput& targetShaderOutput,
                                           std::array<assets::BuildOutput, 2>& targetPipelineOutputs, assets::BuildOutput& targetMaterialOutput) noexcept
    {
        assets::BuildGraph targetGraph;
        if (!targetGraph.Initialize(buildSystem, DeclaredSurfaceArtifactFixture::Resolve, &targetFixture))
            return false;
        assets::GraphRequest targetShader = targetGraph.Request(targetFixture.programRequest, assets::BuildPriority::High);
        std::array<assets::GraphRequest, 2> targetPipelines{targetGraph.Request(targetFixture.pipelineRequests[0], assets::BuildPriority::High),
                                                            targetGraph.Request(targetFixture.pipelineRequests[1], assets::BuildPriority::High)};
        assets::GraphRequest targetMaterial = targetGraph.Request(targetFixture.materialRequest, assets::BuildPriority::High);
        targetShader.Wait();
        for (assets::GraphRequest& targetPipeline : targetPipelines)
            targetPipeline.Wait();
        targetMaterial.Wait();
        const auto printFailure = [](const char* const label, const assets::GraphRequest& request) noexcept
        {
            if (request.HasSucceeded())
                return;
            std::fprintf(stderr, "[materialToolsTests][cross-target] %s state=%u failure=%u build=%s\n", label, static_cast<u32>(request.GetStatus()), static_cast<u32>(request.GetError()),
                         assets::ToString(request.BuildError()));
            assets::BuildReport report;
            if (!request.CopyReport(report))
                return;
            for (const assets::BuildDiagnostic& diagnostic : report.GetDiagnostics())
            {
                const containers::StringView message = report.GetMessage(diagnostic);
                std::fprintf(stderr, "[materialToolsTests][cross-target]   0x%08x %.*s\n", diagnostic.code, static_cast<int>(message.Length()), message.Data());
            }
        };
        printFailure("VSHADER", targetShader);
        printFailure("VPPL-A", targetPipelines[0]);
        printFailure("VPPL-B", targetPipelines[1]);
        printFailure("VMAT", targetMaterial);
        const bool copied = targetShader.CopyOutput(targetShaderOutput) && targetPipelines[0].CopyOutput(targetPipelineOutputs[0]) && targetPipelines[1].CopyOutput(targetPipelineOutputs[1]) &&
                            targetMaterial.CopyOutput(targetMaterialOutput);
        const bool succeeded = copied && ValidateDeclaredSurfaceArtifacts(targetFixture, targetShaderOutput, targetPipelineOutputs, targetMaterialOutput, expectedFormat);
        targetShader.Reset();
        for (assets::GraphRequest& targetPipeline : targetPipelines)
            targetPipeline.Reset();
        targetMaterial.Reset();
        return targetGraph.Shutdown() && succeeded;
    };

    assets::BuildOutput windowsVulkanShaderOutput;
    std::array<assets::BuildOutput, 2> windowsVulkanPipelineOutputs;
    assets::BuildOutput windowsVulkanMaterialOutput;
    assets::BuildOutput linuxVulkanShaderOutput;
    std::array<assets::BuildOutput, 2> linuxVulkanPipelineOutputs;
    assets::BuildOutput linuxVulkanMaterialOutput;
    Check(cookTarget(windowsVulkanFixture, shaders::NativeFormat::SpirV, windowsVulkanShaderOutput, windowsVulkanPipelineOutputs, windowsVulkanMaterialOutput) &&
              cookTarget(linuxVulkanFixture, shaders::NativeFormat::SpirV, linuxVulkanShaderOutput, linuxVulkanPipelineOutputs, linuxVulkanMaterialOutput),
          "Windows/Vulkan and Linux/Vulkan cook the full declared VSHADER, two VPPL techniques, and VMAT path with SPIR-V payloads");

    bool vulkanPlatformIdentity =
        windowsVulkanShaderOutput.buildFingerprint != linuxVulkanShaderOutput.buildFingerprint && windowsVulkanShaderOutput.contentFingerprint == linuxVulkanShaderOutput.contentFingerprint &&
        windowsVulkanMaterialOutput.buildFingerprint != linuxVulkanMaterialOutput.buildFingerprint && windowsVulkanMaterialOutput.contentFingerprint == linuxVulkanMaterialOutput.contentFingerprint;
    for (u32 index = 0; index < 2; ++index)
        vulkanPlatformIdentity = vulkanPlatformIdentity && windowsVulkanPipelineOutputs[index].buildFingerprint != linuxVulkanPipelineOutputs[index].buildFingerprint &&
                                 windowsVulkanPipelineOutputs[index].contentFingerprint == linuxVulkanPipelineOutputs[index].contentFingerprint;
    Check(vulkanPlatformIdentity, "Windows and Linux Vulkan retain distinct DDC identities while producing identical SPIR-V material artifacts");

    bool crossBackendDomainIdentity = false;
    if (declaredShaderOutput.artifacts.Size() == 1 && windowsVulkanShaderOutput.artifacts.Size() == 1)
    {
        filesystem::MemoryFileReader dxilReader(declaredShaderOutput.artifacts[0].bytes, 0);
        filesystem::MemoryFileReader spirVReader(windowsVulkanShaderOutput.artifacts[0].bytes, 0);
        shaders::ShaderFile dxilShader;
        shaders::ShaderFile spirVShader;
        if (dxilShader.Open(dxilReader) == shaders::Result::Success && spirVShader.Open(spirVReader) == shaders::Result::Success)
        {
            const shaders::MaterialContract* const dxilContract = dxilShader.GetMaterialContract();
            const shaders::MaterialContract* const spirVContract = spirVShader.GetMaterialContract();
            crossBackendDomainIdentity = dxilContract != nullptr && spirVContract != nullptr && shaders::MaterialDomainContractsEqual(dxilContract->domain, spirVContract->domain) &&
                                         dxilContract->domainFingerprint == spirVContract->domainFingerprint && dxilContract->accessorAbiVersion == spirVContract->accessorAbiVersion;
        }
    }
    Check(crossBackendDomainIdentity, "DXIL and SPIR-V preserve one frozen material-domain and accessor ABI identity");

    assets::BuildRequest mismatchedDxilRequest = declaredFixture.programRequest;
    mismatchedDxilRequest.target = assets::TargetPlatform::WindowsVulkan;
    assets::BuildPlan mismatchedDxilPlan;
    assets::BuildRequest mismatchedSpirVRequest = windowsVulkanFixture.programRequest;
    mismatchedSpirVRequest.target = assets::TargetPlatform::WindowsD3D12;
    assets::BuildPlan mismatchedSpirVPlan;
    Check(buildSystem.Prepare(mismatchedDxilRequest, mismatchedDxilPlan) != assets::Result::Success && buildSystem.Prepare(mismatchedSpirVRequest, mismatchedSpirVPlan) != assets::Result::Success,
          "canonical MPGI target encoding rejects platform/backend mismatches in both directions");

    Check(buildSystem.UnregisterCompiler(recookTextureCompiler.id) == assets::Result::Success, "material recook texture proof compiler unregisters");
    Check(artifactCompilers.Unregister() == assets::Result::Success && artifactCompilers.Shutdown(), "material artifact compilers shutdown");
    Check(buildSystem.Shutdown(), "material build-system shutdown");

    Check(jobs::Shutdown(), "jobs shutdown");
    DeleteLooseProofFiles(fileManager, looseProofRoot);
    filesystem::Shutdown();
    vanguard::io::Shutdown();
    diagnostics::Shutdown();
    if (failures == 0)
        std::puts("[materialToolsTests] Phase 2 IR, frontend, artifact graph, and preview checks passed");
    return failures == 0 ? 0 : 1;
}
