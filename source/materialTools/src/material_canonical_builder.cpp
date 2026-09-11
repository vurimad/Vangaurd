#include <vanguard/material_tools/material_canonical_builder.hpp>

#include <algorithm>
#include <cstdio>

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;

    [[nodiscard]] bool TypedAs(const resources::ResourceReference value, const resources::ResourceTypeId type) noexcept
    {
        return value.IsValid() && value.IsTyped() && value.ExpectedType() == type;
    }

    [[nodiscard]] bool IsResourceType(const mt::MaterialIrType& type) noexcept
    {
        return type.kind == mt::MaterialIrTypeKind::Texture || type.kind == mt::MaterialIrTypeKind::Sampler ||
               type.kind == mt::MaterialIrTypeKind::Buffer || type.kind == mt::MaterialIrTypeKind::AccelerationStructure;
    }

    [[nodiscard]] materials::ResourceParameterKind ResourceKind(const mt::MaterialIrTypeKind kind) noexcept
    {
        switch (kind)
        {
        case mt::MaterialIrTypeKind::Texture: return materials::ResourceParameterKind::Texture;
        case mt::MaterialIrTypeKind::Buffer: return materials::ResourceParameterKind::Buffer;
        case mt::MaterialIrTypeKind::Sampler: return materials::ResourceParameterKind::Sampler;
        case mt::MaterialIrTypeKind::AccelerationStructure: return materials::ResourceParameterKind::AccelerationStructure;
        default: return static_cast<materials::ResourceParameterKind>(0xffu);
        }
    }

    [[nodiscard]] shaders::MaterialShaderCapabilityMask TypeCapabilities(const mt::MaterialIrType& type) noexcept
    {
        shaders::MaterialShaderCapabilityMask capabilities = 0;
        if (type.kind == mt::MaterialIrTypeKind::Numeric)
        {
            if (type.scalarType == shaders::ScalarType::I16 || type.scalarType == shaders::ScalarType::U16 ||
                type.scalarType == shaders::ScalarType::F16)
                capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Numeric16Bit);
            else if (type.scalarType == shaders::ScalarType::I64 || type.scalarType == shaders::ScalarType::U64)
                capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::Integer64Bit);
            else if (type.scalarType == shaders::ScalarType::F64)
                capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::FloatingPoint64Bit);
        }
        if ((type.kind == mt::MaterialIrTypeKind::Texture || type.kind == mt::MaterialIrTypeKind::Buffer) &&
            type.resourceAccess != mt::MaterialIrResourceAccess::Read)
            capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::WritableResources);
        if (type.kind == mt::MaterialIrTypeKind::Texture && type.textureMultisampled)
            capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::MultisampledTextures);
        if (type.kind == mt::MaterialIrTypeKind::Sampler && type.samplerKind == mt::MaterialIrSamplerKind::Comparison)
            capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::ComparisonSampling);
        if (type.kind == mt::MaterialIrTypeKind::AccelerationStructure)
            capabilities |= shaders::MaterialShaderCapabilityBit(shaders::MaterialShaderCapability::AccelerationStructure);
        return capabilities;
    }

    template <typename Schema>
    [[nodiscard]] bool SymbolsMatch(const containers::ArraySpan<const mt::MaterialSlangSymbol> symbols,
                                    const containers::ArraySpan<const Schema> schemas) noexcept
    {
        if (symbols.Count() != schemas.Count())
            return false;
        for (u32 index = 0; index < symbols.Count(); ++index)
        {
            if (symbols[index].semantic == 0 || symbols[index].name == nullptr || symbols[index].name[0] == '\0')
                return false;
            bool found = false;
            for (const Schema& schema : schemas)
                found = found || schema.name == symbols[index].semantic;
            if (!found)
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (symbols[previous].semantic == symbols[index].semantic)
                    return false;
        }
        return true;
    }

    [[nodiscard]] u64 InterfaceChild(const char* const typeName, const char prefix, const u64 semantic) noexcept
    {
        char field[32]{};
        std::snprintf(field, sizeof(field), "%c_%016llx", prefix, static_cast<unsigned long long>(semantic));
        return shaders::HashInterfaceChildName(shaders::HashInterfaceName(typeName), field);
    }

    [[nodiscard]] u64 ResourceField(const u64 semantic) noexcept
    {
        char field[32]{};
        std::snprintf(field, sizeof(field), "r_%016llx", static_cast<unsigned long long>(semantic));
        return shaders::HashInterfaceName(field);
    }

    [[nodiscard]] u32 LogicalScalarByteSize(const shaders::ScalarType type) noexcept
    {
        switch (type)
        {
        case shaders::ScalarType::Bool: return 1;
        case shaders::ScalarType::I16:
        case shaders::ScalarType::U16:
        case shaders::ScalarType::F16: return 2;
        case shaders::ScalarType::I32:
        case shaders::ScalarType::U32:
        case shaders::ScalarType::F32: return 4;
        case shaders::ScalarType::I64:
        case shaders::ScalarType::U64:
        case shaders::ScalarType::F64: return 8;
        }
        return 0;
    }

    [[nodiscard]] bool TightTypeSize(const mt::MaterialIrModule& module, const mt::MaterialIrTypeId typeId, u32& size) noexcept
    {
        if (typeId >= module.GetTypes().Size())
            return false;
        const mt::MaterialIrTypeRecord& record = module.GetTypes()[typeId];
        if (record.type.kind == mt::MaterialIrTypeKind::Numeric)
        {
            const u64 value = static_cast<u64>(LogicalScalarByteSize(record.type.scalarType)) * record.type.rows * record.type.columns *
                              record.type.arrayCount;
            if (value == 0 || value > ~u32{0})
                return false;
            size = static_cast<u32>(value);
            return true;
        }
        if (record.type.kind != mt::MaterialIrTypeKind::Aggregate ||
            record.firstField > module.GetTypeFields().Size() || record.fieldCount > module.GetTypeFields().Size() - record.firstField)
            return false;
        u64 elementSize = 0;
        for (u32 index = 0; index < record.fieldCount; ++index)
        {
            u32 fieldSize = 0;
            if (!TightTypeSize(module, module.GetTypeFields()[record.firstField + index].type, fieldSize))
                return false;
            elementSize += fieldSize;
            if (elementSize > ~u32{0})
                return false;
        }
        const u64 total = elementSize * record.type.arrayCount;
        if (elementSize == 0 || total > ~u32{0})
            return false;
        size = static_cast<u32>(total);
        return true;
    }

    [[nodiscard]] bool FindArrayElementType(const mt::MaterialIrModule& module, mt::MaterialIrType type,
                                            mt::MaterialIrTypeId& typeId) noexcept
    {
        type.arrayCount = 1;
        const containers::ArraySpan<const mt::MaterialIrTypeRecord> types = module.GetTypes();
        for (u32 index = 0; index < types.Size(); ++index)
            if (types[index].type == type)
            {
                typeId = index;
                return true;
            }
        return false;
    }

    [[nodiscard]] bool StoreLogicalConstant(const mt::MaterialIrType& type, const u64 name,
                                            const u32 arrayCount, const containers::ArraySpan<const u8> data,
                                            containers::DynamicArray<mt::MaterialLogicalConstantValue>& constants,
                                            containers::DynamicArray<u8>& storage) noexcept
    {
        if (name == 0 || arrayCount == 0 || data.Empty() || storage.Size() > 0xffffffffu - data.Count())
            return false;
        const u32 offset = storage.Size();
        for (const u8 byte : data)
            storage.PushBack(byte);
        if (storage.Size() != offset + data.Count())
            return false;
        constants.PushBack({name, type.scalarType, type.rows, type.columns,
                            type.matrixOrder == mt::MaterialIrMatrixOrder::RowMajor, arrayCount,
                            {storage.TypedData() + offset, data.Count()}});
        return true;
    }

    [[nodiscard]] bool AppendAggregateArrayConstants(const mt::MaterialIrModule& module,
                                                      const mt::MaterialIrTypeId elementTypeId, const u64 name,
                                                      const containers::ArraySpan<const u8> data, const u32 arrayCount,
                                                      const u32 elementStride, const u32 fieldOffset,
                                                      containers::DynamicArray<mt::MaterialLogicalConstantValue>& constants,
                                                      containers::DynamicArray<u8>& storage) noexcept
    {
        if (elementTypeId >= module.GetTypes().Size() || arrayCount == 0 || elementStride == 0 ||
            data.Count() != static_cast<u64>(elementStride) * arrayCount)
            return false;
        const mt::MaterialIrTypeRecord& record = module.GetTypes()[elementTypeId];
        if (record.type.arrayCount != 1)
            return false;
        if (record.type.kind == mt::MaterialIrTypeKind::Numeric)
        {
            u32 leafSize = 0;
            if (!TightTypeSize(module, elementTypeId, leafSize) || fieldOffset > elementStride || leafSize > elementStride - fieldOffset ||
                static_cast<u64>(leafSize) * arrayCount > 0xffffffffu ||
                storage.Size() > 0xffffffffu - static_cast<u32>(static_cast<u64>(leafSize) * arrayCount))
                return false;
            const u32 leafArraySize = static_cast<u32>(static_cast<u64>(leafSize) * arrayCount);
            const u32 offset = storage.Size();
            for (u32 arrayIndex = 0; arrayIndex < arrayCount; ++arrayIndex)
                for (u32 byte = 0; byte < leafSize; ++byte)
                    storage.PushBack(data[arrayIndex * elementStride + fieldOffset + byte]);
            if (storage.Size() != offset + leafArraySize)
                return false;
            constants.PushBack({name, record.type.scalarType, record.type.rows, record.type.columns,
                                record.type.matrixOrder == mt::MaterialIrMatrixOrder::RowMajor, arrayCount,
                                {storage.TypedData() + offset, leafArraySize}});
            return true;
        }
        if (record.type.kind != mt::MaterialIrTypeKind::Aggregate ||
            record.firstField > module.GetTypeFields().Size() || record.fieldCount > module.GetTypeFields().Size() - record.firstField)
            return false;
        u32 offset = fieldOffset;
        for (u32 index = 0; index < record.fieldCount; ++index)
        {
            const mt::MaterialIrTypeField& field = module.GetTypeFields()[record.firstField + index];
            u32 fieldSize = 0;
            char fieldName[32]{};
            std::snprintf(fieldName, sizeof(fieldName), "f_%016llx", static_cast<unsigned long long>(field.name));
            if (!TightTypeSize(module, field.type, fieldSize) || offset > elementStride || fieldSize > elementStride - offset ||
                !AppendAggregateArrayConstants(module, field.type, shaders::HashInterfaceChildName(name, fieldName), data,
                                               arrayCount, elementStride, offset, constants, storage))
                return false;
            offset += fieldSize;
        }
        return offset <= elementStride;
    }

    [[nodiscard]] bool AppendLogicalConstants(const mt::MaterialIrModule& module, const mt::MaterialIrTypeId typeId,
                                              const u64 name, const containers::ArraySpan<const u8> data,
                                              containers::DynamicArray<mt::MaterialLogicalConstantValue>& constants,
                                              containers::DynamicArray<u8>& storage) noexcept
    {
        if (name == 0 || typeId >= module.GetTypes().Size())
            return false;
        const mt::MaterialIrTypeRecord& record = module.GetTypes()[typeId];
        u32 expectedSize = 0;
        if (!TightTypeSize(module, typeId, expectedSize) || data.Count() != expectedSize)
            return false;
        if (record.type.kind == mt::MaterialIrTypeKind::Numeric)
        {
            const u64 reflectedName = record.type.arrayCount > 1 ? shaders::HashInterfaceChildName(name, "values") : name;
            return StoreLogicalConstant(record.type, reflectedName, record.type.arrayCount, data, constants, storage);
        }
        if (record.type.kind != mt::MaterialIrTypeKind::Aggregate)
            return false;
        if (record.type.arrayCount > 1)
        {
            mt::MaterialIrTypeId elementTypeId = 0;
            u32 elementStride = 0;
            if (!FindArrayElementType(module, record.type, elementTypeId) ||
                !TightTypeSize(module, elementTypeId, elementStride))
                return false;
            return AppendAggregateArrayConstants(module, elementTypeId, shaders::HashInterfaceChildName(name, "values"), data,
                                                 record.type.arrayCount, elementStride, 0, constants, storage);
        }
        u32 offset = 0;
        for (u32 index = 0; index < record.fieldCount; ++index)
        {
            const mt::MaterialIrTypeField& field = module.GetTypeFields()[record.firstField + index];
            u32 fieldSize = 0;
            char fieldName[32]{};
            std::snprintf(fieldName, sizeof(fieldName), "f_%016llx", static_cast<unsigned long long>(field.name));
            if (!TightTypeSize(module, field.type, fieldSize) || fieldSize > data.Count() - offset ||
                !AppendLogicalConstants(module, field.type, shaders::HashInterfaceChildName(name, fieldName),
                                        {data.Data() + offset, fieldSize}, constants, storage))
                return false;
            offset += fieldSize;
        }
        return offset == data.Count();
    }

    [[nodiscard]] mt::MaterialCanonicalResult Fail(mt::MaterialCanonicalDiagnostic* const diagnostic,
                                                   const mt::MaterialCanonicalResult result) noexcept
    {
        if (diagnostic != nullptr && diagnostic->code == mt::MaterialCanonicalResult::Success)
            diagnostic->code = result;
        return result;
    }

    [[nodiscard]] bool TargetMatches(const assets::TargetPlatform platform, const shader_tools::Target target) noexcept
    {
        return (platform == assets::TargetPlatform::WindowsD3D12 && target == shader_tools::Target::D3D12Dxil) ||
               ((platform == assets::TargetPlatform::WindowsVulkan || platform == assets::TargetPlatform::LinuxVulkan) &&
                target == shader_tools::Target::VulkanSpirV);
    }
} // namespace

namespace vanguard::material_tools
{
    MaterialCanonicalBuildSet::MaterialCanonicalBuildSet() noexcept
        : m_programBytes(memory::pools::Assets::GetInstance()), m_pipelineBytes(memory::pools::Assets::GetInstance()),
          m_materialBytes(memory::pools::Assets::GetInstance()), m_buildSettings(memory::pools::Assets::GetInstance()),
          m_pipelineRanges(memory::pools::Tools::GetInstance()), m_pipelineRequests(memory::pools::Tools::GetInstance()),
          m_programRanges(memory::pools::Tools::GetInstance()), m_programRequests(memory::pools::Tools::GetInstance())
    {
    }

    void MaterialCanonicalBuildSet::Reset() noexcept
    {
        m_valid = false;
        m_programRequest = {};
        m_materialRequest = {};
        m_pipelineRequests.Clear();
        m_programRequests.Clear();
        m_programRanges.Clear();
        m_pipelineRanges.Clear();
        m_programBytes.Clear();
        m_pipelineBytes.Clear();
        m_materialBytes.Clear();
        m_buildSettings.Clear();
    }

    bool MaterialCanonicalBuildSet::IsValid() const noexcept { return m_valid; }
    const assets::BuildRequest& MaterialCanonicalBuildSet::Program() const noexcept { return m_programRequest; }
    containers::ArraySpan<const assets::BuildRequest> MaterialCanonicalBuildSet::Programs() const noexcept { return m_programRequests; }
    containers::ArraySpan<const assets::BuildRequest> MaterialCanonicalBuildSet::Pipelines() const noexcept { return m_pipelineRequests; }
    const assets::BuildRequest& MaterialCanonicalBuildSet::Material() const noexcept { return m_materialRequest; }

    MaterialCanonicalResult BuildMaterialCanonicalInputs(const MaterialCanonicalDescription& description, MaterialCanonicalBuildSet& build,
                                                          MaterialCanonicalDiagnostic* const diagnostic,
                                                          const MaterialCanonicalLimits& limits) noexcept
    {
        build.Reset();
        if (diagnostic != nullptr)
        {
            *diagnostic = {};
            diagnostic->resource = description.graph.identity;
        }
        if (description.frontend.nodes == nullptr || description.frontend.domains == nullptr || description.sourceName == nullptr ||
            description.moduleName == nullptr || description.program == 0 || description.material == 0 || description.entryPoints.Empty() ||
            !description.offline.IsValid() ||
            description.target >= assets::TargetPlatform::Count || !TargetMatches(description.target, description.compileSettings.target) ||
            !TypedAs(description.programSource, MaterialProgramInputResourceType) ||
            !TypedAs(description.shader, shaders::ShaderResourceType) ||
            !TypedAs(description.materialSource, MaterialValueInputResourceType) ||
            !TypedAs(description.materialOutput, materials::MaterialResourceType) || limits.maximumTechniques == 0 ||
            limits.maximumResourceValues == 0 || limits.maximumProgramInputBytes == 0 || limits.maximumPipelineInputBytes == 0 ||
            limits.maximumMaterialInputBytes == 0 || limits.maximumBuildSettingsBytes == 0 || limits.slang.maximumSourceBytes == 0 ||
            description.techniques.Count() > limits.maximumTechniques || description.resources.Count() > limits.maximumResourceValues ||
            description.buildSettings.Count() > limits.maximumBuildSettingsBytes)
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);

        MaterialDomainView domain;
        if (!description.frontend.domains->IsFrozen() || !description.frontend.domains->Find(description.graph.domain, domain) ||
            shaders::HashInterfaceName(description.slang.stableName) != domain.contract.name ||
            description.slang.schemaVersion != domain.contract.schemaVersion ||
            description.slang.legalStages != domain.contract.legalStages ||
            description.slang.requiredCapabilities != domain.contract.requiredCapabilities ||
            !SymbolsMatch(description.slang.inputs, domain.inputs) || !SymbolsMatch(description.slang.outputs, domain.outputs))
            return Fail(diagnostic, MaterialCanonicalResult::DomainMismatch);

        MaterialIrModule module;
        MaterialSourceMap sourceMap;
        containers::DynamicArray<MaterialIrDiagnostic> irDiagnostics{memory::pools::Tools::GetInstance()};
        MaterialFrontendDiagnostic frontendDiagnostic;
        MaterialFrontend frontend;
        const MaterialFrontendResult frontendResult = frontend.Lower(description.graph, description.frontend, module, sourceMap,
                                                                     irDiagnostics, &frontendDiagnostic);
        if (frontendResult != MaterialFrontendResult::Success)
        {
            if (diagnostic != nullptr)
            {
                diagnostic->code = MaterialCanonicalResult::FrontendFailure;
                diagnostic->frontend = frontendResult;
                diagnostic->resource = frontendDiagnostic.resource;
                diagnostic->node = frontendDiagnostic.node;
                diagnostic->pin = frontendDiagnostic.pin;
                if (!irDiagnostics.Empty())
                    diagnostic->ir = irDiagnostics[0].code;
            }
            return MaterialCanonicalResult::FrontendFailure;
        }
        if (module.GetDomainFingerprint() != domain.implementationFingerprint)
            return Fail(diagnostic, MaterialCanonicalResult::DomainMismatch);

        shaders::MaterialShaderCapabilityMask requiredCapabilities = domain.contract.requiredCapabilities;
        for (const MaterialIrTypeRecord& type : module.GetTypes())
            requiredCapabilities |= TypeCapabilities(type.type);
        if (!shaders::IsValidMaterialShaderCapabilityMask(requiredCapabilities) ||
            (requiredCapabilities & ~description.offline.Capabilities(description.target)) != 0)
            return Fail(diagnostic, MaterialCanonicalResult::UnsupportedTargetCapability);

        containers::DynamicArray<u8> generatedSource{memory::pools::Assets::GetInstance()};
        containers::DynamicArray<MaterialGeneratedSourceRange> sourceRanges{memory::pools::Tools::GetInstance()};
        const MaterialSlangResult slangResult = GenerateMaterialSlangProbe(module, description.slang, generatedSource, limits.slang, &sourceRanges);
        if (slangResult != MaterialSlangResult::Success)
        {
            if (diagnostic != nullptr)
                diagnostic->slang = slangResult;
            return Fail(diagnostic, MaterialCanonicalResult::SlangFailure);
        }

        MaterialProgramInput program;
        program.sourceName = description.sourceName;
        program.moduleName = description.moduleName;
        program.program = description.program;
        program.permutation = module.GetContentFingerprint();
        program.expectedDomain = domain.contract;
        program.expectedDomainFingerprint = domain.contractFingerprint;
        program.requiredCapabilities = requiredCapabilities;
        program.source = generatedSource;
        program.entryPoints = description.entryPoints;
        program.defines = description.defines;
        program.sourceRanges = sourceRanges;
        program.settings = description.compileSettings;
        const MaterialInputResult programInputResult = EncodeMaterialProgramInput(program, build.m_programBytes);
        if (programInputResult != MaterialInputResult::Success)
        {
            if (diagnostic != nullptr)
                diagnostic->input = programInputResult;
            return Fail(diagnostic, MaterialCanonicalResult::InputEncodingFailure);
        }
        if (build.m_programBytes.Size() > limits.maximumProgramInputBytes)
            return Fail(diagnostic, MaterialCanonicalResult::LimitExceeded);
        build.m_programRanges.PushBack({0, build.m_programBytes.Size(), description.programSource, description.shader});

        containers::DynamicArray<u32> techniqueOrder{memory::pools::Tools::GetInstance()};
        techniqueOrder.Reserve(description.techniques.Count());
        for (u32 index = 0; index < description.techniques.Count(); ++index)
            techniqueOrder.PushBack(index);
        std::sort(techniqueOrder.Begin(), techniqueOrder.End(), [&description](const u32 left, const u32 right)
                  { return description.techniques[left].name < description.techniques[right].name; });
        for (u32 index = 1; index < techniqueOrder.Size(); ++index)
            if (description.techniques[techniqueOrder[index - 1u]].name == description.techniques[techniqueOrder[index]].name)
                return Fail(diagnostic, MaterialCanonicalResult::DuplicateTechnique);
        if (techniqueOrder.Size() != domain.techniques.Count())
            return Fail(diagnostic, MaterialCanonicalResult::MissingTechnique);

        containers::DynamicArray<materials::TechniqueBuildRecord> materialTechniques{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<u8> encodedPipeline{memory::pools::Assets::GetInstance()};
        containers::DynamicArray<u8> encodedProgram{memory::pools::Assets::GetInstance()};
        materialTechniques.Reserve(techniqueOrder.Size());
        build.m_pipelineRanges.Reserve(techniqueOrder.Size());
        for (const u32 techniqueIndex : techniqueOrder)
        {
            const MaterialCanonicalTechnique& technique = description.techniques[techniqueIndex];
            const MaterialTechniqueRequirement* requirement = nullptr;
            for (const MaterialTechniqueRequirement& candidate : domain.techniques)
                if (candidate.name == technique.name)
                    requirement = &candidate;
            if (requirement == nullptr || technique.source != requirement->pipelineTemplate || technique.recipe.name != technique.name ||
                !TypedAs(technique.source, MaterialPipelineInputResourceType) || !TypedAs(technique.output, pipelines::PipelineResourceType))
                return Fail(diagnostic, MaterialCanonicalResult::MissingTechnique);
            encodedPipeline.Clear();
            resources::ResourceReference techniqueShader = description.shader;
            if (technique.programSource.IsValid() || technique.shader.IsValid() || !technique.entryPoints.Empty())
            {
                if (!TypedAs(technique.programSource, MaterialProgramInputResourceType) || !TypedAs(technique.shader, shaders::ShaderResourceType) ||
                    technique.entryPoints.Empty() || technique.entryPoints.Data() == nullptr)
                    return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
                for (const auto& previous : build.m_programRanges)
                {
                    if (previous.source == technique.programSource || previous.output == technique.shader)
                        return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
                }
                // Reuse the finalized graph and compile policy. Only the entry
                // selection and output resource identity differ between passes.
                program.entryPoints = technique.entryPoints;
                encodedProgram.Clear();
                const MaterialInputResult variantResult = EncodeMaterialProgramInput(program, encodedProgram);
                if (variantResult != MaterialInputResult::Success)
                {
                    if (diagnostic != nullptr)
                        diagnostic->input = variantResult;
                    return Fail(diagnostic, MaterialCanonicalResult::InputEncodingFailure);
                }
                if (encodedProgram.Size() > limits.maximumProgramInputBytes ||
                    build.m_programBytes.Size() > limits.maximumProgramInputBytes - encodedProgram.Size())
                    return Fail(diagnostic, MaterialCanonicalResult::LimitExceeded);
                const u32 programOffset = build.m_programBytes.Size();
                for (const u8 byte : encodedProgram)
                    build.m_programBytes.PushBack(byte);
                build.m_programRanges.PushBack({programOffset, encodedProgram.Size(), technique.programSource, technique.shader});
                techniqueShader = technique.shader;
            }
            const MaterialPipelineInput pipeline{techniqueShader, technique.recipe};
            const MaterialInputResult pipelineInputResult = EncodeMaterialPipelineInput(pipeline, encodedPipeline);
            if (pipelineInputResult != MaterialInputResult::Success)
            {
                if (diagnostic != nullptr)
                    diagnostic->input = pipelineInputResult;
                return Fail(diagnostic, MaterialCanonicalResult::InputEncodingFailure);
            }
            if (encodedPipeline.Size() > limits.maximumPipelineInputBytes ||
                build.m_pipelineBytes.Size() > 0xffffffffu - encodedPipeline.Size())
                return Fail(diagnostic, MaterialCanonicalResult::LimitExceeded);
            const u32 offset = build.m_pipelineBytes.Size();
            for (const u8 byte : encodedPipeline)
                build.m_pipelineBytes.PushBack(byte);
            build.m_pipelineRanges.PushBack({offset, encodedPipeline.Size()});
            materialTechniques.PushBack({technique.name, technique.output, nullptr});
        }

        struct ResourceDeclaration
        {
            u64 semantic = 0;
            u64 name = 0;
            u32 arrayCount = 1;
            materials::ResourceParameterKind kind = materials::ResourceParameterKind::Texture;
        };
        containers::DynamicArray<ResourceDeclaration> resourceDeclarations{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<MaterialLogicalConstantValue> constants{memory::pools::Tools::GetInstance()};
        containers::DynamicArray<u8> constantStorage{memory::pools::Tools::GetInstance()};
        const containers::ArraySpan<const u8> moduleData = module.GetData();
        constantStorage.Reserve(moduleData.Count());
        for (const MaterialIrValue& value : module.GetValues())
        {
            if (value.kind != MaterialIrValueKind::DynamicParameter)
                continue;
            if (IsResourceType(value.type))
            {
                if (value.dataSize != 0)
                    return Fail(diagnostic, MaterialCanonicalResult::InvalidDynamicValue);
                resourceDeclarations.PushBack({value.semantic, ResourceField(value.semantic),
                                               value.type.arrayCount, ResourceKind(value.type.kind)});
                continue;
            }
            if (value.dataSize == 0 || value.dataOffset > moduleData.Count() || value.dataSize > moduleData.Count() - value.dataOffset)
                return Fail(diagnostic, MaterialCanonicalResult::InvalidDynamicValue);
            if (!AppendLogicalConstants(module, value.typeId, InterfaceChild(description.slang.parameterTypeName, 'p', value.semantic),
                                        {moduleData.Data() + value.dataOffset, value.dataSize}, constants, constantStorage))
                return Fail(diagnostic, MaterialCanonicalResult::InvalidDynamicValue);
        }
        std::sort(constants.Begin(), constants.End(), [](const MaterialLogicalConstantValue& left,
                                                         const MaterialLogicalConstantValue& right) { return left.name < right.name; });
        std::sort(resourceDeclarations.Begin(), resourceDeclarations.End(), [](const ResourceDeclaration& left, const ResourceDeclaration& right)
                  { return left.semantic < right.semantic; });

        containers::DynamicArray<materials::ResourceValueBuildRecord> resourceValues{memory::pools::Tools::GetInstance()};
        resourceValues.Reserve(description.resources.Count());
        for (const MaterialCanonicalResourceValue& source : description.resources)
        {
            const ResourceDeclaration* declaration = nullptr;
            for (const ResourceDeclaration& candidate : resourceDeclarations)
                if (candidate.semantic == source.semantic)
                    declaration = &candidate;
            const resources::ResourceTypeId expectedType = declaration != nullptr
                                                               ? description.offline.ResourceType(declaration->kind)
                                                               : resources::InvalidResourceTypeId;
            if (declaration == nullptr || source.arrayIndex >= declaration->arrayCount || !source.resource.IsValid() || !source.resource.IsTyped() ||
                expectedType == resources::InvalidResourceTypeId || source.resource.ExpectedType() != expectedType ||
                source.dependency > resources::DependencyKind::Soft)
                return Fail(diagnostic, MaterialCanonicalResult::InvalidResourceValue);
            for (const materials::ResourceValueBuildRecord& existing : resourceValues)
                if (existing.name == declaration->name && existing.arrayIndex == source.arrayIndex)
                    return Fail(diagnostic, MaterialCanonicalResult::InvalidResourceValue);
            resourceValues.PushBack({declaration->name, source.arrayIndex, source.resource, source.dependency});
        }
        std::sort(resourceValues.Begin(), resourceValues.End(), [](const materials::ResourceValueBuildRecord& left,
                                                                  const materials::ResourceValueBuildRecord& right)
                  { return left.name != right.name ? left.name < right.name : left.arrayIndex < right.arrayIndex; });

        const MaterialValueInput material{description.material, description.shader, materialTechniques, constants, resourceValues};
        const MaterialInputResult materialInputResult = EncodeMaterialValueInput(material, build.m_materialBytes);
        if (materialInputResult != MaterialInputResult::Success)
        {
            if (diagnostic != nullptr)
                diagnostic->input = materialInputResult;
            return Fail(diagnostic, MaterialCanonicalResult::InputEncodingFailure);
        }
        if (build.m_materialBytes.Size() > limits.maximumMaterialInputBytes)
            return Fail(diagnostic, MaterialCanonicalResult::LimitExceeded);

        build.m_buildSettings.Resize(description.buildSettings.Count());
        if (build.m_buildSettings.Size() != description.buildSettings.Count())
            return Fail(diagnostic, MaterialCanonicalResult::LimitExceeded);
        for (u32 index = 0; index < description.buildSettings.Count(); ++index)
            build.m_buildSettings[index] = description.buildSettings[index];
        const containers::ArraySpan<const u8> settings = build.m_buildSettings;
        // Construct spans only after all program bytes have stopped growing.
        build.m_programRequests.Reserve(build.m_programRanges.Size());
        for (const auto& range : build.m_programRanges)
        {
            const containers::ArraySpan<const u8> bytes{build.m_programBytes.TypedData() + range.offset, range.size};
            build.m_programRequests.PushBack({{range.source, bytes, {}}, range.output, description.target, settings});
        }
        build.m_programRequest = build.m_programRequests[0];
        build.m_pipelineRequests.Reserve(build.m_pipelineRanges.Size());
        for (u32 index = 0; index < build.m_pipelineRanges.Size(); ++index)
        {
            const MaterialCanonicalBuildSet::PipelineRange& range = build.m_pipelineRanges[index];
            const MaterialCanonicalTechnique& technique = description.techniques[techniqueOrder[index]];
            const containers::ArraySpan<const u8> bytes{build.m_pipelineBytes.TypedData() + range.offset, range.size};
            build.m_pipelineRequests.PushBack({{technique.source, bytes, {}}, technique.output, description.target, settings});
        }
        build.m_materialRequest = {{description.materialSource, build.m_materialBytes, {}}, description.materialOutput, description.target, settings};
        if (!build.m_programRequest.IsValid() || !build.m_materialRequest.IsValid() || build.m_pipelineRequests.Size() != techniqueOrder.Size())
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        for (const assets::BuildRequest& request : build.m_programRequests)
        {
            if (!request.IsValid())
                return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        }
        for (const assets::BuildRequest& request : build.m_pipelineRequests)
            if (!request.IsValid())
                return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        build.m_valid = true;
        return MaterialCanonicalResult::Success;
    }

    const char* ToString(const MaterialCanonicalResult result) noexcept
    {
        switch (result)
        {
        case MaterialCanonicalResult::Success: return "Success";
        case MaterialCanonicalResult::InvalidArgument: return "InvalidArgument";
        case MaterialCanonicalResult::DomainMismatch: return "DomainMismatch";
        case MaterialCanonicalResult::FrontendFailure: return "FrontendFailure";
        case MaterialCanonicalResult::SlangFailure: return "SlangFailure";
        case MaterialCanonicalResult::MissingTechnique: return "MissingTechnique";
        case MaterialCanonicalResult::DuplicateTechnique: return "DuplicateTechnique";
        case MaterialCanonicalResult::InvalidDynamicValue: return "InvalidDynamicValue";
        case MaterialCanonicalResult::InvalidResourceValue: return "InvalidResourceValue";
        case MaterialCanonicalResult::UnsupportedTargetCapability: return "UnsupportedTargetCapability";
        case MaterialCanonicalResult::LimitExceeded: return "LimitExceeded";
        case MaterialCanonicalResult::InputEncodingFailure: return "InputEncodingFailure";
        }
        return "Unknown";
    }
} // namespace vanguard::material_tools
