#include <vanguard/material_tools/material_canonical_builder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

    [[nodiscard]] mt::MaterialIrType ImportedSurfaceFloatType(const u8 rows) noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Numeric;
        type.scalarType = shaders::ScalarType::F32;
        type.rows = rows;
        return type;
    }

    [[nodiscard]] mt::MaterialIrType ImportedSurfaceTextureType() noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Texture;
        type.scalarType = shaders::ScalarType::F32;
        type.rows = 4;
        type.textureDimension = mt::MaterialIrTextureDimension::D2;
        type.resourceAccess = mt::MaterialIrResourceAccess::Read;
        return type;
    }

    [[nodiscard]] mt::MaterialIrType ImportedSurfaceSamplerType() noexcept
    {
        mt::MaterialIrType type;
        type.kind = mt::MaterialIrTypeKind::Sampler;
        type.samplerKind = mt::MaterialIrSamplerKind::Filtering;
        return type;
    }

    [[nodiscard]] constexpr u64 MakeImportedSurfaceTechniqueId(const char* const name) noexcept
    {
        if (name == nullptr || name[0] == '\0')
            return 0;
        u64 hash = 1469598103934665603ull;
        for (const char* character = name; *character != '\0'; ++character)
        {
            hash ^= static_cast<u8>(*character);
            hash *= 1099511628211ull;
        }
        for (u32 byteIndex = 0; byteIndex < sizeof(u32); ++byteIndex)
        {
            hash ^= 0u;
            hash *= 1099511628211ull;
        }
        return hash != 0 ? hash : 1ull;
    }

    static_assert(MakeImportedSurfaceTechniqueId("vanguard.render.depth_prepass") == mt::ImportedSurfaceDepthPrepassTechnique);
    static_assert(MakeImportedSurfaceTechniqueId("vanguard.render.opaque") == mt::ImportedSurfaceOpaqueTechnique);
    static_assert(MakeImportedSurfaceTechniqueId("vanguard.render.transparent") == mt::ImportedSurfaceTransparentTechnique);

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
    MaterialDomainId GetImportedSurfaceDomainId() noexcept
    {
        return shaders::HashInterfaceName("VanguardStaticSurface");
    }

    MaterialSlangDomain MakeImportedSurfaceSlangDomain(const containers::ArraySpan<const u8> prefix,
                                                       const containers::ArraySpan<const u8> suffix) noexcept
    {
        static const MaterialSlangSymbol inputs[]{{ImportedSurfaceUvInputSemantic, "uv"},
                                                  {ImportedSurfaceWorldNormalInputSemantic, "worldNormal"},
                                                  {ImportedSurfaceViewRelativePositionInputSemantic, "viewRelativePosition"}};
        static const MaterialSlangSymbol outputs[]{{ImportedSurfaceBaseColorOutputSemantic, "baseColor"},
                                                   {ImportedSurfaceNormalOutputSemantic, "normalTS"},
                                                   {ImportedSurfaceRoughnessOutputSemantic, "roughness"},
                                                   {ImportedSurfaceMetallicOutputSemantic, "metallic"},
                                                   {ImportedSurfaceEmissiveOutputSemantic, "emissive"},
                                                   {ImportedSurfaceOpacityCutoffOutputSemantic, "opacityCutoff"},
                                                   {ImportedSurfaceSpecularColorOutputSemantic, "specularColor"},
                                                   {ImportedSurfaceOcclusionOutputSemantic, "occlusion"}};
        MaterialSlangDomain domain;
        domain.stableName = "VanguardStaticSurface";
        domain.schemaVersion = ImportedSurfaceMaterialContractVersion;
        domain.legalStages = shaders::StageBit(shaders::ShaderStage::Fragment);
        domain.requiredCapabilities = 0;
        domain.inputTypeName = "StaticSurfaceInput";
        domain.outputTypeName = "StaticSurfaceOutput";
        domain.parameterTypeName = "StaticSurfaceParameters";
        domain.resourceTypeName = "StaticSurfaceResources";
        domain.evaluationFunctionName = "EvaluateStaticSurface";
        domain.inputs = inputs;
        domain.outputs = outputs;
        domain.prefix = prefix;
        domain.suffix = suffix;
        return domain;
    }

    containers::ArraySpan<const ImportedSurfaceTechniqueDescriptor> GetImportedSurfaceTechniques() noexcept
    {
        static const resources::ResourceReference depthTemplate(
            resources::ResourcePath::FromString("tools/vanguard-material/imported-surface-depth.mpli"),
            MaterialPipelineInputResourceType);
        static const resources::ResourceReference opaqueTemplate(
            resources::ResourcePath::FromString("tools/vanguard-material/imported-surface-opaque.mpli"),
            MaterialPipelineInputResourceType);
        static const resources::ResourceReference transparentTemplate(
            resources::ResourcePath::FromString("tools/vanguard-material/imported-surface-transparent.mpli"),
            MaterialPipelineInputResourceType);
        static const ImportedSurfaceTechniqueDescriptor techniques[]{
            {ImportedSurfaceDepthPrepassTechnique, depthTemplate, "StaticSurfaceVertexMain", "StaticSurfaceDepthMain",
             "StaticSurfaceMaskedDepthMain"},
            {ImportedSurfaceOpaqueTechnique, opaqueTemplate, "StaticSurfaceVertexMain", "StaticSurfaceGBufferMain",
             "StaticSurfaceMaskedGBufferMain"},
            {ImportedSurfaceTransparentTechnique, transparentTemplate, "StaticSurfaceVertexMain", "StaticSurfaceTransparentMain",
             "StaticSurfaceTransparentMain"}};
        return techniques;
    }

    bool RegisterImportedSurfaceMaterialSchema(const shaders::MaterialDomainContract& reflectedDomain,
                                               MaterialNodeRegistry& nodes,
                                               MaterialDomainRegistry& domains) noexcept
    {
        crypto::Digest256 domainFingerprint;
        const shaders::StageMask fragment = shaders::StageBit(shaders::ShaderStage::Fragment);
        if (reflectedDomain.name != GetImportedSurfaceDomainId() ||
            reflectedDomain.schemaVersion != ImportedSurfaceMaterialContractVersion ||
            reflectedDomain.legalStages != fragment || reflectedDomain.requiredCapabilities != 0 ||
            shaders::CalculateMaterialDomainFingerprint(reflectedDomain, domainFingerprint) != shaders::Result::Success)
            return false;

        const MaterialIrType float1 = ImportedSurfaceFloatType(1);
        const MaterialIrType float2 = ImportedSurfaceFloatType(2);
        const MaterialIrType float3 = ImportedSurfaceFloatType(3);
        const MaterialIrType float4 = ImportedSurfaceFloatType(4);
        const MaterialIrType texture = ImportedSurfaceTextureType();
        const MaterialIrType sampler = ImportedSurfaceSamplerType();
        const MaterialPinSchema float1Output{1, float1, true};
        const MaterialPinSchema float3Output{1, float3, true};
        const MaterialPinSchema float4Output{1, float4, true};
        const MaterialPinSchema textureOutput{1, texture, true};
        const MaterialPinSchema samplerOutput{1, sampler, true};
        const MaterialPinSchema uvOutput{1, float2, true};
        const MaterialPinSchema sampleInputs[]{{1, texture, true}, {2, sampler, true}, {3, float2, true}};
        const MaterialPinSchema extractInputs[]{{1, float4, true}};
        const MaterialPinSchema constructFloat3Inputs[]{{1, float1, true}, {2, float1, true}, {3, float1, true}};
        const MaterialPinSchema constructFloat4Inputs[]{{1, float1, true}, {2, float1, true}, {3, float1, true}, {4, float1, true}};
        const MaterialPinSchema multiplyFloatInputs[]{{1, float1, true}, {2, float1, true}};
        const MaterialPinSchema subtractFloatInputs[]{{1, float1, true}, {2, float1, true}};
        const MaterialPinSchema multiplyFloat3Inputs[]{{1, float3, true}, {2, float3, true}};
        const MaterialPinSchema subtractFloat3Inputs[]{{1, float3, true}, {2, float3, true}};
        const MaterialPinSchema multiplyInputs[]{{1, float4, true}, {2, float4, true}};
        constexpr char NodePolicy[] = "vanguard.material.imported-surface-nodes.v2";
        const crypto::Digest256 nodeFingerprint = crypto::Sha256(NodePolicy, sizeof(NodePolicy) - 1u);
        if (!nodes.Register({ImportedSurfaceFloatNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&float1Output, 1}}) ||
            !nodes.Register({ImportedSurfaceFloat3NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&float3Output, 1}}) ||
            !nodes.Register({ImportedSurfaceFloat4NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&float4Output, 1}}) ||
            !nodes.Register({ImportedSurfaceTexture2DNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&textureOutput, 1}}) ||
            !nodes.Register({ImportedSurfaceSamplerNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&samplerOutput, 1}}) ||
            !nodes.Register({ImportedSurfaceUvNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {}, {&uvOutput, 1}}) ||
            !nodes.Register({ImportedSurfaceSampleNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {sampleInputs, 3}, {&float4Output, 1}}) ||
            !nodes.Register({ImportedSurfaceExtractFloatNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {extractInputs, 1}, {&float1Output, 1}}) ||
            !nodes.Register({ImportedSurfaceConstructFloat3NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {constructFloat3Inputs, 3}, {&float3Output, 1}}) ||
            !nodes.Register({ImportedSurfaceConstructFloat4NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {constructFloat4Inputs, 4}, {&float4Output, 1}}) ||
            !nodes.Register({ImportedSurfaceMultiplyFloatNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {multiplyFloatInputs, 2}, {&float1Output, 1}}) ||
            !nodes.Register({ImportedSurfaceSubtractFloatNodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {subtractFloatInputs, 2}, {&float1Output, 1}}) ||
            !nodes.Register({ImportedSurfaceMultiplyFloat3NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {multiplyFloat3Inputs, 2}, {&float3Output, 1}}) ||
            !nodes.Register({ImportedSurfaceSubtractFloat3NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {subtractFloat3Inputs, 2}, {&float3Output, 1}}) ||
            !nodes.Register({ImportedSurfaceMultiplyFloat4NodeType, ImportedSurfaceMaterialContractVersion, nodeFingerprint, {multiplyInputs, 2}, {&float4Output, 1}}))
            return false;

        const MaterialDomainInputSchema inputs[]{{ImportedSurfaceUvInputSemantic, float2, fragment},
                                                 {ImportedSurfaceWorldNormalInputSemantic, float3, fragment},
                                                 {ImportedSurfaceViewRelativePositionInputSemantic, float3, fragment}};
        const MaterialDomainOutputSchema outputs[]{{ImportedSurfaceBaseColorOutputSemantic, float4, fragment, {}},
                                                   {ImportedSurfaceNormalOutputSemantic, float3, fragment, {}},
                                                   {ImportedSurfaceRoughnessOutputSemantic, float1, fragment, {}},
                                                   {ImportedSurfaceMetallicOutputSemantic, float1, fragment, {}},
                                                   {ImportedSurfaceEmissiveOutputSemantic, float3, fragment, {}},
                                                   {ImportedSurfaceOpacityCutoffOutputSemantic, float1, fragment, {}},
                                                   {ImportedSurfaceSpecularColorOutputSemantic, float3, fragment, {}},
                                                   {ImportedSurfaceOcclusionOutputSemantic, float1, fragment, {}}};
        const containers::ArraySpan<const ImportedSurfaceTechniqueDescriptor> techniqueDescriptors = GetImportedSurfaceTechniques();
        if (techniqueDescriptors.Count() != 3)
            return false;
        const MaterialTechniqueRequirement techniques[]{{techniqueDescriptors[0].name, techniqueDescriptors[0].pipelineTemplate},
                                                        {techniqueDescriptors[1].name, techniqueDescriptors[1].pipelineTemplate},
                                                        {techniqueDescriptors[2].name, techniqueDescriptors[2].pipelineTemplate}};
        constexpr char DomainPolicy[] = "vanguard.material.imported-surface-domain.v3";
        const crypto::Digest256 implementationFingerprint = crypto::Sha256(DomainPolicy, sizeof(DomainPolicy) - 1u);
        return domains.Register({reflectedDomain.name, reflectedDomain, implementationFingerprint, {inputs, 3}, {outputs, 8}, {techniques, 3}});
    }

    void ImportedSurfaceMaterialMapping::Reset() noexcept
    {
        m_graphIdentity = {};
        for (MaterialSourceValue& value : m_values)
            value = {};
        for (MaterialSourceOutput& value : m_outputs)
            value = {};
        for (MaterialCanonicalResourceValue& value : m_resources)
            value = {};
        for (MaterialSourceOperand& value : m_operands)
            value = {};
        std::memset(m_extractData, 0, sizeof(m_extractData));
        std::memset(m_baseColor, 0, sizeof(m_baseColor));
        std::memset(m_normal, 0, sizeof(m_normal));
        std::memset(m_emissive, 0, sizeof(m_emissive));
        std::memset(m_specular, 0, sizeof(m_specular));
        m_roughness = 1.0f;
        m_metallic = 0.0f;
        m_opacityCutoff = 0.5f;
        m_normalScale[0] = m_normalScale[1] = m_normalScale[2] = 2.0f;
        m_normalBias[0] = m_normalBias[1] = m_normalBias[2] = 1.0f;
        m_one = 1.0f;
        m_occlusionStrength = 1.0f;
        m_valueCount = 0;
        m_resourceCount = 0;
        m_operandCount = 0;
        m_extractCount = 0;
        m_warnings = ImportedSurfaceMappingWarning::None;
        m_coverage = ImportedSurfaceCoverageMode::Opaque;
        m_twoSided = false;
        m_valid = false;
    }

    bool ImportedSurfaceMaterialMapping::IsValid() const noexcept { return m_valid; }

    MaterialSourceGraph ImportedSurfaceMaterialMapping::GetGraph() const noexcept
    {
        return m_valid ? MaterialSourceGraph{m_graphIdentity, GetImportedSurfaceDomainId(), {m_values, m_valueCount}, {m_outputs, 8}}
                       : MaterialSourceGraph{};
    }

    containers::ArraySpan<const MaterialCanonicalResourceValue> ImportedSurfaceMaterialMapping::GetResources() const noexcept
    {
        return m_valid ? containers::ArraySpan<const MaterialCanonicalResourceValue>{m_resources, m_resourceCount}
                       : containers::ArraySpan<const MaterialCanonicalResourceValue>{};
    }

    ImportedSurfaceMappingWarning ImportedSurfaceMaterialMapping::GetWarnings() const noexcept { return m_warnings; }
    ImportedSurfaceCoverageMode ImportedSurfaceMaterialMapping::GetCoverageMode() const noexcept { return m_coverage; }
    bool ImportedSurfaceMaterialMapping::IsTwoSided() const noexcept { return m_twoSided; }

    ImportedSurfaceMappingResult BuildImportedSurfaceMaterialMapping(const ImportedSurfaceMaterialInput& input,
                                                                     const resources::ResourceReference graphIdentity,
                                                                     ImportedSurfaceMaterialMapping& output) noexcept
    {
        output.Reset();
        const auto finite = [](const f32 value) noexcept { return std::isfinite(value); };
        if (!graphIdentity.IsValid() || input.coverage > ImportedSurfaceCoverageMode::Blended ||
            (!input.textures.Empty() && input.textures.Data() == nullptr) || !finite(input.metallic) || !finite(input.roughness) ||
            !finite(input.opacity) || !finite(input.opacityCutoff) ||
            !finite(input.occlusionStrength) || input.occlusionStrength < 0.0f || input.occlusionStrength > 1.0f ||
            !finite(input.specularFactor) || input.specularFactor < 0.0f || input.specularFactor > 1.0f ||
            !finite(input.ior) || input.ior < 1.0f)
            return ImportedSurfaceMappingResult::InvalidArgument;
        for (const f32 value : input.baseColor)
            if (!finite(value)) return ImportedSurfaceMappingResult::InvalidArgument;
        for (const f32 value : input.emissive)
            if (!finite(value)) return ImportedSurfaceMappingResult::InvalidArgument;
        for (const f32 value : input.specular)
            if (!finite(value)) return ImportedSurfaceMappingResult::InvalidArgument;

        constexpr u32 TextureSemanticCount = static_cast<u32>(ImportedSurfaceTextureSemantic::SpecularStrength) + 1u;
        const ImportedSurfaceTextureBinding* bindings[TextureSemanticCount]{};
        for (const ImportedSurfaceTextureBinding& binding : input.textures)
        {
            if (binding.semantic > ImportedSurfaceTextureSemantic::SpecularStrength ||
                !TypedAs(binding.resource, textures::TextureResourceType) || binding.dependency > resources::DependencyKind::Soft)
                return ImportedSurfaceMappingResult::InvalidArgument;
            const u32 semantic = static_cast<u32>(binding.semantic);
            if (bindings[semantic] != nullptr)
                return ImportedSurfaceMappingResult::InvalidArgument;
            bindings[semantic] = &binding;
            if (binding.uvSet != 0)
                output.m_warnings = output.m_warnings | ImportedSurfaceMappingWarning::UvSetUnsupported;
        }

        const auto usable = [&bindings](const ImportedSurfaceTextureSemantic semantic) noexcept -> const ImportedSurfaceTextureBinding*
        {
            const ImportedSurfaceTextureBinding* const binding = bindings[static_cast<u32>(semantic)];
            return binding != nullptr && binding->uvSet == 0 ? binding : nullptr;
        };
        const ImportedSurfaceTextureBinding* const baseColorTexture = usable(ImportedSurfaceTextureSemantic::BaseColor);
        const ImportedSurfaceTextureBinding* const normalTexture = usable(ImportedSurfaceTextureSemantic::Normal);
        const ImportedSurfaceTextureBinding* const metallicTexture = usable(ImportedSurfaceTextureSemantic::Metallic);
        const ImportedSurfaceTextureBinding* const roughnessTexture = usable(ImportedSurfaceTextureSemantic::Roughness);
        const ImportedSurfaceTextureBinding* const metallicRoughnessTexture = usable(ImportedSurfaceTextureSemantic::MetallicRoughness);
        const ImportedSurfaceTextureBinding* const emissiveTexture = usable(ImportedSurfaceTextureSemantic::Emissive);
        const ImportedSurfaceTextureBinding* const opacityTexture = usable(ImportedSurfaceTextureSemantic::Opacity);
        const ImportedSurfaceTextureBinding* const specularTexture = usable(ImportedSurfaceTextureSemantic::Specular);
        const ImportedSurfaceTextureBinding* const specularStrengthTexture = usable(ImportedSurfaceTextureSemantic::SpecularStrength);
        const ImportedSurfaceTextureBinding* const occlusionTexture = usable(ImportedSurfaceTextureSemantic::Occlusion);

        if (input.coverage == ImportedSurfaceCoverageMode::Opaque && (input.opacity != 1.0f || opacityTexture != nullptr))
            output.m_warnings = output.m_warnings | ImportedSurfaceMappingWarning::OpaqueOpacityIgnored;

        output.m_graphIdentity = graphIdentity;
        output.m_coverage = input.coverage;
        output.m_twoSided = input.twoSided;
        std::memcpy(output.m_baseColor, input.baseColor, sizeof(output.m_baseColor));
        output.m_baseColor[3] *= input.opacity;
        output.m_normal[0] = 0.0f;
        output.m_normal[1] = 0.0f;
        output.m_normal[2] = 1.0f;
        output.m_roughness = input.roughness;
        output.m_metallic = input.metallic;
        std::memcpy(output.m_emissive, input.emissive, sizeof(output.m_emissive));
        // KHR_materials_ior changes dielectric Fresnel F0. KHR_materials_specular
        // modulates its strength/color independently, rather than providing an
        // RGB highlight that can be substituted for glTF metallic reflectance.
        const f32 fresnel = (input.ior - 1.0f) / (input.ior + 1.0f);
        const f32 dielectricF0 = fresnel * fresnel * input.specularFactor;
        for (u32 channel = 0; channel < 3; ++channel)
            output.m_specular[channel] = dielectricF0 * input.specular[channel];
        output.m_occlusionStrength = input.occlusionStrength;
        output.m_opacityCutoff = input.opacityCutoff;

        const shaders::StageMask fragment = shaders::StageBit(shaders::ShaderStage::Fragment);
        const MaterialIrType float1 = ImportedSurfaceFloatType(1);
        const MaterialIrType float2 = ImportedSurfaceFloatType(2);
        const MaterialIrType float3 = ImportedSurfaceFloatType(3);
        const MaterialIrType float4 = ImportedSurfaceFloatType(4);
        const MaterialIrType textureType = ImportedSurfaceTextureType();
        const MaterialIrType samplerType = ImportedSurfaceSamplerType();
        const auto storeOperands = [&](const MaterialSourceOperand* const operands, const u32 count) noexcept
            -> containers::ArraySpan<const MaterialSourceOperand>
        {
            if (count == 0)
                return {};
            constexpr u32 Capacity = sizeof(output.m_operands) / sizeof(output.m_operands[0]);
            if (operands == nullptr || output.m_operandCount > Capacity || count > Capacity - output.m_operandCount)
                return {};
            MaterialSourceOperand* const first = output.m_operands + output.m_operandCount;
            for (u32 index = 0; index < count; ++index)
                first[index] = operands[index];
            output.m_operandCount += count;
            return {first, count};
        };
        const auto addValue = [&](const MaterialNodeTypeId nodeType, const MaterialIrValueKind kind, const MaterialIrOpcode opcode,
                                  const MaterialIrType& type, const u64 semantic, const containers::ArraySpan<const u8> data,
                                  const MaterialSourceOperand* const operands = nullptr, const u32 operandCount = 0) noexcept
        {
            constexpr u32 Capacity = sizeof(output.m_values) / sizeof(output.m_values[0]);
            if (output.m_valueCount >= Capacity)
                return InvalidMaterialSourceValue;
            const containers::ArraySpan<const MaterialSourceOperand> storedOperands = storeOperands(operands, operandCount);
            if (operandCount != 0 && storedOperands.Empty())
                return InvalidMaterialSourceValue;
            const MaterialSourceValueId id = output.m_valueCount + 1u;
            MaterialSourceValue& value = output.m_values[output.m_valueCount++];
            value.id = id;
            value.node = 0x4953460000000000ull | id;
            value.nodeType = nodeType;
            value.nodeSchemaVersion = ImportedSurfaceMaterialContractVersion;
            value.outputPin = 1;
            value.kind = kind;
            value.opcode = opcode;
            value.type = type;
            value.legalStages = fragment;
            value.semantic = semantic;
            value.operands = storedOperands;
            value.data = data;
            return id;
        };

        const auto addBinary = [&](const MaterialNodeTypeId nodeType, const MaterialIrOpcode opcode, const MaterialIrType& type,
                                   const MaterialSourceValueId left, const MaterialSourceValueId right) noexcept
        {
            const MaterialSourceOperand operands[]{{1, left}, {2, right}};
            return addValue(nodeType, MaterialIrValueKind::Instruction, opcode, type, 0, {}, operands, 2);
        };
        const auto addExtract = [&](const MaterialSourceValueId source, const u32 component) noexcept
        {
            constexpr u32 Capacity = sizeof(output.m_extractData) / sizeof(output.m_extractData[0]);
            if (source == InvalidMaterialSourceValue || component > 3 || output.m_extractCount >= Capacity)
                return InvalidMaterialSourceValue;
            u8* const data = output.m_extractData[output.m_extractCount++];
            data[0] = static_cast<u8>(component);
            data[1] = static_cast<u8>(component >> 8u);
            data[2] = static_cast<u8>(component >> 16u);
            data[3] = static_cast<u8>(component >> 24u);
            const MaterialSourceOperand operand{1, source};
            return addValue(ImportedSurfaceExtractFloatNodeType, MaterialIrValueKind::Instruction, MaterialIrOpcode::Extract,
                            float1, 0, {data, 4}, &operand, 1);
        };
        const auto addConstruct3 = [&](const MaterialSourceValueId x, const MaterialSourceValueId y,
                                       const MaterialSourceValueId z) noexcept
        {
            const MaterialSourceOperand operands[]{{1, x}, {2, y}, {3, z}};
            return addValue(ImportedSurfaceConstructFloat3NodeType, MaterialIrValueKind::Instruction, MaterialIrOpcode::Construct,
                            float3, 0, {}, operands, 3);
        };
        const auto addConstruct4 = [&](const MaterialSourceValueId x, const MaterialSourceValueId y,
                                       const MaterialSourceValueId z, const MaterialSourceValueId w) noexcept
        {
            const MaterialSourceOperand operands[]{{1, x}, {2, y}, {3, z}, {4, w}};
            return addValue(ImportedSurfaceConstructFloat4NodeType, MaterialIrValueKind::Instruction, MaterialIrOpcode::Construct,
                            float4, 0, {}, operands, 4);
        };

        const MaterialSourceValueId baseColor = addValue(ImportedSurfaceFloat4NodeType, MaterialIrValueKind::DynamicParameter,
                                                         MaterialIrOpcode::None, float4, ImportedSurfaceBaseColorParameterSemantic,
                                                         {reinterpret_cast<const u8*>(output.m_baseColor), sizeof(output.m_baseColor)});
        const MaterialSourceValueId normal = addValue(ImportedSurfaceFloat3NodeType, MaterialIrValueKind::DynamicParameter,
                                                      MaterialIrOpcode::None, float3, ImportedSurfaceNormalParameterSemantic,
                                                      {reinterpret_cast<const u8*>(output.m_normal), sizeof(output.m_normal)});
        const MaterialSourceValueId roughness = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::DynamicParameter,
                                                         MaterialIrOpcode::None, float1, ImportedSurfaceRoughnessParameterSemantic,
                                                         {reinterpret_cast<const u8*>(&output.m_roughness), sizeof(output.m_roughness)});
        const MaterialSourceValueId metallic = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::DynamicParameter,
                                                        MaterialIrOpcode::None, float1, ImportedSurfaceMetallicParameterSemantic,
                                                        {reinterpret_cast<const u8*>(&output.m_metallic), sizeof(output.m_metallic)});
        const MaterialSourceValueId emissive = addValue(ImportedSurfaceFloat3NodeType, MaterialIrValueKind::DynamicParameter,
                                                        MaterialIrOpcode::None, float3, ImportedSurfaceEmissiveParameterSemantic,
                                                        {reinterpret_cast<const u8*>(output.m_emissive), sizeof(output.m_emissive)});
        const MaterialSourceValueId specular = addValue(ImportedSurfaceFloat3NodeType, MaterialIrValueKind::DynamicParameter,
                                                        MaterialIrOpcode::None, float3, ImportedSurfaceSpecularColorParameterSemantic,
                                                        {reinterpret_cast<const u8*>(output.m_specular), sizeof(output.m_specular)});
        const MaterialSourceValueId occlusionStrength = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::DynamicParameter,
                                                                MaterialIrOpcode::None, float1, ImportedSurfaceOcclusionStrengthParameterSemantic,
                                                                {reinterpret_cast<const u8*>(&output.m_occlusionStrength), sizeof(output.m_occlusionStrength)});
        const MaterialSourceValueId opacityCutoff = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::DynamicParameter,
                                                             MaterialIrOpcode::None, float1, ImportedSurfaceOpacityCutoffParameterSemantic,
                                                             {reinterpret_cast<const u8*>(&output.m_opacityCutoff), sizeof(output.m_opacityCutoff)});
        if (baseColor == InvalidMaterialSourceValue || normal == InvalidMaterialSourceValue || roughness == InvalidMaterialSourceValue ||
            metallic == InvalidMaterialSourceValue || emissive == InvalidMaterialSourceValue ||
            specular == InvalidMaterialSourceValue || opacityCutoff == InvalidMaterialSourceValue ||
            occlusionStrength == InvalidMaterialSourceValue)
            return ImportedSurfaceMappingResult::InvalidArgument;

        MaterialSourceValueId sampler = InvalidMaterialSourceValue;
        MaterialSourceValueId uv = InvalidMaterialSourceValue;
        const auto addSample = [&](const ImportedSurfaceTextureBinding* const binding, const u64 semantic) noexcept
        {
            if (binding == nullptr)
                return InvalidMaterialSourceValue;
            if (sampler == InvalidMaterialSourceValue)
            {
                sampler = addValue(ImportedSurfaceSamplerNodeType, MaterialIrValueKind::DynamicParameter,
                                   MaterialIrOpcode::None, samplerType, ImportedSurfaceSamplerSemantic, {});
                uv = addValue(ImportedSurfaceUvNodeType, MaterialIrValueKind::DomainInput,
                              MaterialIrOpcode::None, float2, ImportedSurfaceUvInputSemantic, {});
                if (sampler == InvalidMaterialSourceValue || uv == InvalidMaterialSourceValue)
                    return InvalidMaterialSourceValue;
            }
            constexpr u32 ResourceCapacity = sizeof(output.m_resources) / sizeof(output.m_resources[0]);
            if (output.m_resourceCount >= ResourceCapacity)
                return InvalidMaterialSourceValue;
            const MaterialSourceValueId texture = addValue(ImportedSurfaceTexture2DNodeType, MaterialIrValueKind::DynamicParameter,
                                                           MaterialIrOpcode::None, textureType, semantic, {});
            if (texture == InvalidMaterialSourceValue)
                return InvalidMaterialSourceValue;
            const MaterialSourceOperand operands[]{{1, texture}, {2, sampler}, {3, uv}};
            const MaterialSourceValueId sampled = addValue(ImportedSurfaceSampleNodeType, MaterialIrValueKind::Instruction,
                                                           MaterialIrOpcode::TextureSample, float4, 0, {}, operands, 3);
            if (sampled == InvalidMaterialSourceValue)
                return InvalidMaterialSourceValue;
            output.m_resources[output.m_resourceCount++] = {semantic, 0, binding->resource, binding->dependency};
            return sampled;
        };

        MaterialSourceValueId baseColorOutput = baseColor;
        if (baseColorTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(baseColorTexture, ImportedSurfaceBaseColorTextureSemantic);
            baseColorOutput = addBinary(ImportedSurfaceMultiplyFloat4NodeType, MaterialIrOpcode::Multiply, float4, sampled, baseColor);
            if (baseColorOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        MaterialSourceValueId normalOutput = normal;
        if (normalTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(normalTexture, ImportedSurfaceNormalTextureSemantic);
            const MaterialSourceValueId x = addExtract(sampled, 0);
            const MaterialSourceValueId y = addExtract(sampled, 1);
            const MaterialSourceValueId z = addExtract(sampled, 2);
            const MaterialSourceValueId rgb = addConstruct3(x, y, z);
            const MaterialSourceValueId scale = addValue(ImportedSurfaceFloat3NodeType, MaterialIrValueKind::Constant,
                                                         MaterialIrOpcode::None, float3, 0,
                                                         {reinterpret_cast<const u8*>(output.m_normalScale), sizeof(output.m_normalScale)});
            const MaterialSourceValueId bias = addValue(ImportedSurfaceFloat3NodeType, MaterialIrValueKind::Constant,
                                                        MaterialIrOpcode::None, float3, 0,
                                                        {reinterpret_cast<const u8*>(output.m_normalBias), sizeof(output.m_normalBias)});
            const MaterialSourceValueId scaled = addBinary(ImportedSurfaceMultiplyFloat3NodeType, MaterialIrOpcode::Multiply,
                                                           float3, rgb, scale);
            normalOutput = addBinary(ImportedSurfaceSubtractFloat3NodeType, MaterialIrOpcode::Subtract, float3, scaled, bias);
            if (normalOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        MaterialSourceValueId metallicOutput = metallic;
        const ImportedSurfaceTextureBinding* const metallicSource = metallicTexture != nullptr ? metallicTexture : metallicRoughnessTexture;
        MaterialSourceValueId metallicSample = InvalidMaterialSourceValue;
        MaterialSourceValueId roughnessSample = InvalidMaterialSourceValue;
        if (metallicTexture != nullptr)
            metallicSample = addSample(metallicTexture, ImportedSurfaceMetallicTextureSemantic);
        if (roughnessTexture != nullptr)
            roughnessSample = addSample(roughnessTexture, ImportedSurfaceRoughnessTextureSemantic);
        if (metallicRoughnessTexture != nullptr && (metallicTexture == nullptr || roughnessTexture == nullptr))
        {
            const MaterialSourceValueId packed = addSample(metallicRoughnessTexture, ImportedSurfaceMetallicRoughnessTextureSemantic);
            if (metallicTexture == nullptr)
                metallicSample = packed;
            if (roughnessTexture == nullptr)
                roughnessSample = packed;
        }
        if (metallicSource != nullptr)
        {
            const MaterialSourceValueId value = addExtract(metallicSample, metallicTexture != nullptr ? 0u : 2u);
            metallicOutput = addBinary(ImportedSurfaceMultiplyFloatNodeType, MaterialIrOpcode::Multiply, float1, value, metallic);
            if (metallicOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        MaterialSourceValueId roughnessOutput = roughness;
        const ImportedSurfaceTextureBinding* const roughnessSource = roughnessTexture != nullptr ? roughnessTexture : metallicRoughnessTexture;
        if (roughnessSource != nullptr)
        {
            const MaterialSourceValueId value = addExtract(roughnessSample, roughnessTexture != nullptr ? 0u : 1u);
            roughnessOutput = addBinary(ImportedSurfaceMultiplyFloatNodeType, MaterialIrOpcode::Multiply, float1, value, roughness);
            if (roughnessOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        MaterialSourceValueId emissiveOutput = emissive;
        if (emissiveTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(emissiveTexture, ImportedSurfaceEmissiveTextureSemantic);
            const MaterialSourceValueId r = addExtract(sampled, 0);
            const MaterialSourceValueId g = addExtract(sampled, 1);
            const MaterialSourceValueId b = addExtract(sampled, 2);
            const MaterialSourceValueId rgb = addConstruct3(r, g, b);
            emissiveOutput = addBinary(ImportedSurfaceMultiplyFloat3NodeType, MaterialIrOpcode::Multiply, float3, rgb, emissive);
            if (emissiveOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        if (opacityTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(opacityTexture, ImportedSurfaceOpacityTextureSemantic);
            const MaterialSourceValueId opacityValue = addExtract(sampled, 0);
            const MaterialSourceValueId one = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::Constant,
                                                       MaterialIrOpcode::None, float1, 0,
                                                       {reinterpret_cast<const u8*>(&output.m_one), sizeof(output.m_one)});
            const MaterialSourceValueId opacityVector = addConstruct4(one, one, one, opacityValue);
            baseColorOutput = addBinary(ImportedSurfaceMultiplyFloat4NodeType, MaterialIrOpcode::Multiply,
                                        float4, baseColorOutput, opacityVector);
            if (baseColorOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        // Preserve RGB specular extension data as an authored independent
        // material value. The lighting/transparent shader consumes this output;
        // it must never be silently folded into baseColor or ignored.
        MaterialSourceValueId specularOutput = specular;
        if (specularTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(specularTexture, ImportedSurfaceSpecularTextureSemantic);
            const MaterialSourceValueId red = addExtract(sampled, 0);
            const MaterialSourceValueId green = addExtract(sampled, 1);
            const MaterialSourceValueId blue = addExtract(sampled, 2);
            const MaterialSourceValueId rgb = addConstruct3(red, green, blue);
            specularOutput = addBinary(ImportedSurfaceMultiplyFloat3NodeType, MaterialIrOpcode::Multiply, float3, rgb, specular);
            if (specularOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }
        if (specularStrengthTexture != nullptr)
        {
            // The scalar KHR_materials_specular texture uses its alpha channel;
            // specularColorTexture independently uses RGB in the path above.
            const MaterialSourceValueId sampled = addSample(specularStrengthTexture, ImportedSurfaceSpecularStrengthTextureSemantic);
            const MaterialSourceValueId strength = addExtract(sampled, 3);
            const MaterialSourceValueId strengthRGB = addConstruct3(strength, strength, strength);
            specularOutput = addBinary(ImportedSurfaceMultiplyFloat3NodeType, MaterialIrOpcode::Multiply,
                                      float3, specularOutput, strengthRGB);
            if (specularOutput == InvalidMaterialSourceValue)
                return ImportedSurfaceMappingResult::InvalidArgument;
        }

        // glTF occlusionTexture uses the R channel. Strength blends the
        // authored sample with one: 1 - strength * (1 - texture.r).
        MaterialSourceValueId occlusionOutput = addValue(ImportedSurfaceFloatNodeType, MaterialIrValueKind::Constant,
                                                        MaterialIrOpcode::None, float1, 0,
                                                        {reinterpret_cast<const u8*>(&output.m_one), sizeof(output.m_one)});
        if (occlusionTexture != nullptr)
        {
            const MaterialSourceValueId sampled = addSample(occlusionTexture, ImportedSurfaceOcclusionTextureSemantic);
            const MaterialSourceValueId red = addExtract(sampled, 0);
            const MaterialSourceValueId deviation = addBinary(ImportedSurfaceSubtractFloatNodeType, MaterialIrOpcode::Subtract,
                                                              float1, occlusionOutput, red);
            const MaterialSourceValueId weighted = addBinary(ImportedSurfaceMultiplyFloatNodeType, MaterialIrOpcode::Multiply,
                                                             float1, occlusionStrength, deviation);
            occlusionOutput = addBinary(ImportedSurfaceSubtractFloatNodeType, MaterialIrOpcode::Subtract,
                                        float1, occlusionOutput, weighted);
        }
        if (occlusionOutput == InvalidMaterialSourceValue)
            return ImportedSurfaceMappingResult::InvalidArgument;

        output.m_outputs[0] = {ImportedSurfaceBaseColorOutputSemantic, baseColorOutput, fragment};
        output.m_outputs[1] = {ImportedSurfaceNormalOutputSemantic, normalOutput, fragment};
        output.m_outputs[2] = {ImportedSurfaceRoughnessOutputSemantic, roughnessOutput, fragment};
        output.m_outputs[3] = {ImportedSurfaceMetallicOutputSemantic, metallicOutput, fragment};
        output.m_outputs[4] = {ImportedSurfaceEmissiveOutputSemantic, emissiveOutput, fragment};
        output.m_outputs[5] = {ImportedSurfaceOpacityCutoffOutputSemantic, opacityCutoff, fragment};
        output.m_outputs[6] = {ImportedSurfaceSpecularColorOutputSemantic, specularOutput, fragment};
        output.m_outputs[7] = {ImportedSurfaceOcclusionOutputSemantic, occlusionOutput, fragment};
        output.m_valid = true;
        return ImportedSurfaceMappingResult::Success;
    }

    const char* ToString(const ImportedSurfaceMappingResult result) noexcept
    {
        switch (result)
        {
        case ImportedSurfaceMappingResult::Success: return "Success";
        case ImportedSurfaceMappingResult::InvalidArgument: return "InvalidArgument";
        }
        return "Unknown";
    }

    struct ImportedSurfaceMaterialContext::Impl final
    {
        Impl() noexcept
            : prefix(memory::pools::Tools::GetInstance()), suffix(memory::pools::Tools::GetInstance()),
              gpuSceneTypes(memory::pools::Tools::GetInstance())
        {
        }

        containers::DynamicArray<u8> prefix;
        containers::DynamicArray<u8> suffix;
        containers::DynamicArray<u8> gpuSceneTypes;
        MaterialNodeRegistry nodes;
        MaterialDomainRegistry domains;
        assets::TargetPlatform target = assets::TargetPlatform::WindowsD3D12;
        shader_tools::CompileSettings compileSettings;
        MaterialOfflineContract offline;
        crypto::Digest256 sourceProviderFingerprint;
    };

    namespace
    {
        [[nodiscard]] bool CopyImportedSurfaceSource(const containers::ArraySpan<const u8> source,
                                                     containers::DynamicArray<u8>& destination) noexcept
        {
            if (source.Empty() || source.Data() == nullptr)
                return false;
            destination.Resize(source.Count());
            if (destination.Size() != source.Count())
                return false;
            std::memcpy(destination.TypedData(), source.Data(), source.SizeInBytes());
            return true;
        }

        [[nodiscard]] bool BuildImportedSurfaceReferenceIr(MaterialIrModule& module) noexcept
        {
            constexpr u8 widths[]{4, 3, 1, 1, 3, 1, 3, 1};
            constexpr u64 outputs[]{ImportedSurfaceBaseColorOutputSemantic, ImportedSurfaceNormalOutputSemantic,
                                    ImportedSurfaceRoughnessOutputSemantic, ImportedSurfaceMetallicOutputSemantic,
                                    ImportedSurfaceEmissiveOutputSemantic, ImportedSurfaceOpacityCutoffOutputSemantic,
                                    ImportedSurfaceSpecularColorOutputSemantic, ImportedSurfaceOcclusionOutputSemantic};
            const shaders::StageMask fragment = shaders::StageBit(shaders::ShaderStage::Fragment);
            MaterialIrBuilder builder;
            constexpr char Policy[] = "vanguard.material.imported-surface-bootstrap.v3";
            builder.Reset(crypto::Sha256(Policy, sizeof(Policy) - 1u));
            for (u32 index = 0; index < 8; ++index)
            {
                MaterialIrValueBuildDescription value;
                value.kind = MaterialIrValueKind::DynamicParameter;
                value.type = ImportedSurfaceFloatType(widths[index]);
                value.legalStages = fragment;
                value.semantic = ImportedSurfaceBaseColorParameterSemantic + index;
                MaterialIrValueId parameter = InvalidMaterialIrValue;
                if (builder.AddValue(value, parameter) != MaterialIrResult::Success ||
                    builder.AddOutput({outputs[index], parameter, fragment}) != MaterialIrResult::Success)
                    return false;
            }
            containers::DynamicArray<MaterialIrDiagnostic> diagnostics{memory::pools::Tools::GetInstance()};
            return builder.Finalize(module, diagnostics) == MaterialIrResult::Success;
        }

        [[nodiscard]] resources::ResourceReference DeriveImportedSurfaceReference(const resources::ResourceReference materialOutput,
                                                                                  const char* const role,
                                                                                  const resources::ResourceTypeId type) noexcept
        {
            if (!TypedAs(materialOutput, materials::MaterialResourceType) || role == nullptr || role[0] == '\0' ||
                type == resources::InvalidResourceTypeId)
                return {};
            crypto::Sha256Builder hash;
            constexpr char Domain[] = "vanguard.material.imported-surface-intermediate.v1";
            const u64 parent = materialOutput.GetPath().Id();
            const u32 contractVersion = ImportedSurfaceMaterialContractVersion;
            const u32 mapperVersion = ImportedSurfaceMaterialMapperVersion;
            if (!hash.Update(Domain, sizeof(Domain) - 1u) || !hash.Update(&parent, sizeof(parent)) ||
                !hash.Update(&contractVersion, sizeof(contractVersion)) || !hash.Update(&mapperVersion, sizeof(mapperVersion)) ||
                !hash.Update(role, std::strlen(role)))
                return {};
            crypto::Digest256 digest;
            if (!hash.Finalize(digest))
                return {};
            u64 id = 0;
            for (u32 index = 0; index < sizeof(id); ++index)
                id |= static_cast<u64>(digest.bytes[index]) << (index * 8u);
            if (id == resources::InvalidResourceId)
                id = 1;
            return resources::ResourceReference(resources::ResourcePath::FromId(id), type);
        }
    } // namespace

    ImportedSurfaceMaterialContext::~ImportedSurfaceMaterialContext() { Reset(); }

    bool ImportedSurfaceMaterialContext::Initialize(const ImportedSurfaceMaterialContextDescription& description) noexcept
    {
        if (m_impl != nullptr || description.target >= assets::TargetPlatform::Count ||
            !TargetMatches(description.target, description.compileSettings.target) || !description.offline.IsValid() ||
            description.prefix.Empty() || description.suffix.Empty() || description.gpuSceneTypes.Empty() ||
            description.prefix.Data() == nullptr || description.suffix.Data() == nullptr || description.gpuSceneTypes.Data() == nullptr)
            return false;

        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Tools);
        if (implementation == nullptr)
            return false;
        m_impl = implementation;
        implementation->target = description.target;
        implementation->compileSettings = description.compileSettings;
        implementation->offline = description.offline;
        if (!CopyImportedSurfaceSource(description.prefix, implementation->prefix) ||
            !CopyImportedSurfaceSource(description.suffix, implementation->suffix) ||
            !CopyImportedSurfaceSource(description.gpuSceneTypes, implementation->gpuSceneTypes))
        {
            Reset();
            return false;
        }

        crypto::Sha256Builder providerHash;
        constexpr char ProviderPolicy[] = "vanguard.material.imported-surface-gpu-scene-provider.v1";
        constexpr char IncludeName[] = "gpu_scene_types.hlsli";
        if (!providerHash.Update(ProviderPolicy, sizeof(ProviderPolicy) - 1u) ||
            !providerHash.Update(IncludeName, sizeof(IncludeName) - 1u) ||
            !providerHash.Update(implementation->gpuSceneTypes.TypedData(), implementation->gpuSceneTypes.Size()) ||
            !providerHash.Finalize(implementation->sourceProviderFingerprint) || implementation->sourceProviderFingerprint.IsEmpty())
        {
            Reset();
            return false;
        }

        MaterialIrModule referenceModule;
        containers::DynamicArray<u8> probe{memory::pools::Tools::GetInstance()};
        const MaterialSlangDomain domain = GetSlangDomain();
        if (!BuildImportedSurfaceReferenceIr(referenceModule) ||
            GenerateMaterialSlangProbe(referenceModule, domain, probe) != MaterialSlangResult::Success)
        {
            Reset();
            return false;
        }
        const shader_tools::EntryPoint entries[]{{"StaticSurfaceVertexMain", shaders::ShaderStage::Vertex},
                                                 {"StaticSurfaceGBufferMain", shaders::ShaderStage::Fragment}};
        shader_tools::CompileRequest request;
        request.sourceName = "generated/imported_surface_contract.slang";
        request.moduleName = "vanguard_imported_surface_contract";
        request.source = probe;
        request.entryPoints = entries;
        request.loadSource = &ImportedSurfaceMaterialContext::LoadShaderSource;
        request.loadSourceUserData = this;
        request.settings = implementation->compileSettings;
        shader_tools::ShaderCompiler compiler;
        if (compiler.Initialize() != shader_tools::Result::Success)
        {
            Reset();
            return false;
        }
        shader_tools::CompileOutput reflection;
        const shader_tools::Result reflected = compiler.Reflect(request, reflection);
        compiler.Shutdown();
        if (reflected != shader_tools::Result::Success || !reflection.HasMaterialContract() ||
            !RegisterImportedSurfaceMaterialSchema(reflection.GetMaterialDomain(), implementation->nodes, implementation->domains) ||
            !implementation->nodes.Freeze() || !implementation->domains.Freeze())
        {
            Reset();
            return false;
        }
        return true;
    }

    void ImportedSurfaceMaterialContext::Reset() noexcept
    {
        if (m_impl != nullptr)
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
        }
    }

    bool ImportedSurfaceMaterialContext::IsInitialized() const noexcept { return m_impl != nullptr; }

    MaterialFrontendConfig ImportedSurfaceMaterialContext::GetFrontendConfig() const noexcept
    {
        return m_impl != nullptr ? MaterialFrontendConfig{&m_impl->nodes, &m_impl->domains} : MaterialFrontendConfig{};
    }

    MaterialSlangDomain ImportedSurfaceMaterialContext::GetSlangDomain() const noexcept
    {
        return m_impl != nullptr ? MakeImportedSurfaceSlangDomain(m_impl->prefix, m_impl->suffix) : MaterialSlangDomain{};
    }

    assets::TargetPlatform ImportedSurfaceMaterialContext::GetTarget() const noexcept
    {
        return m_impl != nullptr ? m_impl->target : assets::TargetPlatform::WindowsD3D12;
    }

    const shader_tools::CompileSettings& ImportedSurfaceMaterialContext::GetCompileSettings() const noexcept
    {
        static const shader_tools::CompileSettings empty;
        return m_impl != nullptr ? m_impl->compileSettings : empty;
    }

    const MaterialOfflineContract& ImportedSurfaceMaterialContext::GetOfflineContract() const noexcept
    {
        static const MaterialOfflineContract empty;
        return m_impl != nullptr ? m_impl->offline : empty;
    }

    const crypto::Digest256& ImportedSurfaceMaterialContext::GetSourceProviderFingerprint() const noexcept
    {
        static const crypto::Digest256 empty;
        return m_impl != nullptr ? m_impl->sourceProviderFingerprint : empty;
    }

    bool ImportedSurfaceMaterialContext::ConfigureArtifactCompiler(MaterialArtifactCompilerConfig& config) const noexcept
    {
        if (m_impl == nullptr)
            return false;
        config.offline = m_impl->offline;
        config.loadSource = &ImportedSurfaceMaterialContext::LoadShaderSource;
        config.loadSourceUserData = const_cast<ImportedSurfaceMaterialContext*>(this);
        config.sourceProviderFingerprint = m_impl->sourceProviderFingerprint;
        return true;
    }

    bool ImportedSurfaceMaterialContext::LoadShaderSource(const char* const canonicalPath, shader_tools::LoadedSource& source,
                                                          void* const userData) noexcept
    {
        source = {};
        const auto* const context = static_cast<const ImportedSurfaceMaterialContext*>(userData);
        if (context == nullptr || context->m_impl == nullptr || canonicalPath == nullptr)
            return false;
        const char* fileName = canonicalPath;
        for (const char* character = canonicalPath; *character != '\0'; ++character)
            if (*character == '/' || *character == '\\')
                fileName = character + 1;
        if (std::strcmp(fileName, "gpu_scene_types.hlsli") != 0)
            return false;
        source.content = context->m_impl->gpuSceneTypes;
        return true;
    }

    bool ImportedSurfaceCanonicalIntermediates::IsValid() const noexcept
    {
        return TypedAs(programSource, MaterialProgramInputResourceType) && TypedAs(shader, shaders::ShaderResourceType) &&
               TypedAs(depthProgramSource, MaterialProgramInputResourceType) && TypedAs(depthShader, shaders::ShaderResourceType) &&
               TypedAs(materialSource, MaterialValueInputResourceType) && TypedAs(depthPipeline, pipelines::PipelineResourceType) &&
               TypedAs(opaquePipeline, pipelines::PipelineResourceType) &&
               TypedAs(transparentPipeline, pipelines::PipelineResourceType);
    }

    bool DeriveImportedSurfaceCanonicalIntermediates(const resources::ResourceReference materialOutput,
                                                     ImportedSurfaceCanonicalIntermediates& output) noexcept
    {
        output = {};
        if (!TypedAs(materialOutput, materials::MaterialResourceType))
            return false;
        output.programSource = DeriveImportedSurfaceReference(materialOutput, "program-input", MaterialProgramInputResourceType);
        output.shader = DeriveImportedSurfaceReference(materialOutput, "program", shaders::ShaderResourceType);
        output.depthProgramSource = DeriveImportedSurfaceReference(materialOutput, "depth-program-input", MaterialProgramInputResourceType);
        output.depthShader = DeriveImportedSurfaceReference(materialOutput, "depth-program", shaders::ShaderResourceType);
        output.materialSource = DeriveImportedSurfaceReference(materialOutput, "material-input", MaterialValueInputResourceType);
        output.depthPipeline = DeriveImportedSurfaceReference(materialOutput, "depth-pipeline", pipelines::PipelineResourceType);
        output.opaquePipeline = DeriveImportedSurfaceReference(materialOutput, "opaque-pipeline", pipelines::PipelineResourceType);
        output.transparentPipeline = DeriveImportedSurfaceReference(materialOutput, "transparent-pipeline", pipelines::PipelineResourceType);
        if (!output.IsValid())
        {
            output = {};
            return false;
        }
        const resources::ResourceId ids[]{output.programSource.GetPath().Id(), output.shader.GetPath().Id(), output.depthProgramSource.GetPath().Id(),
                                         output.depthShader.GetPath().Id(), output.materialSource.GetPath().Id(), output.depthPipeline.GetPath().Id(),
                                         output.opaquePipeline.GetPath().Id(), output.transparentPipeline.GetPath().Id()};
        for (u32 index = 0; index < sizeof(ids) / sizeof(ids[0]); ++index)
            for (u32 previous = 0; previous < index; ++previous)
                if (ids[index] == ids[previous] || ids[index] == materialOutput.GetPath().Id())
                {
                    output = {};
                    return false;
                }
        return true;
    }

    MaterialCanonicalResult BuildImportedSurfaceCanonicalInputs(const ImportedSurfaceMaterialContext& context,
                                                                const ImportedSurfaceMaterialInput& material,
                                                                const resources::ResourceReference materialOutput,
                                                                const pipelines::BuildDescription& depthPrepassRecipe,
                                                                const pipelines::BuildDescription& opaqueRecipe,
                                                                MaterialCanonicalBuildSet& build,
                                                                ImportedSurfaceMappingWarning* const warnings,
                                                                MaterialCanonicalDiagnostic* const diagnostic,
                                                                const containers::ArraySpan<const u8> buildSettings,
                                                                const MaterialCanonicalLimits& limits,
                                                                const pipelines::BuildDescription* const transparentRecipe) noexcept
    {
        if (warnings != nullptr)
            *warnings = ImportedSurfaceMappingWarning::None;
        if (!context.IsInitialized() || !TypedAs(materialOutput, materials::MaterialResourceType) ||
            depthPrepassRecipe.kind != pipelines::PipelineKind::Graphics || opaqueRecipe.kind != pipelines::PipelineKind::Graphics ||
            !depthPrepassRecipe.shaders.Empty() || !opaqueRecipe.shaders.Empty())
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);

        ImportedSurfaceCanonicalIntermediates intermediates;
        ImportedSurfaceMaterialMapping mapping;
        if (!DeriveImportedSurfaceCanonicalIntermediates(materialOutput, intermediates) ||
            BuildImportedSurfaceMaterialMapping(material, intermediates.materialSource, mapping) != ImportedSurfaceMappingResult::Success)
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        if (warnings != nullptr)
            *warnings = mapping.GetWarnings();

        const containers::ArraySpan<const ImportedSurfaceTechniqueDescriptor> descriptors = GetImportedSurfaceTechniques();
        if (descriptors.Count() != 3 || descriptors[0].name != ImportedSurfaceDepthPrepassTechnique ||
            descriptors[1].name != ImportedSurfaceOpaqueTechnique ||
            descriptors[2].name != ImportedSurfaceTransparentTechnique)
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        const bool blended = mapping.GetCoverageMode() == ImportedSurfaceCoverageMode::Blended;
        if (blended && (transparentRecipe == nullptr || transparentRecipe->kind != pipelines::PipelineKind::Graphics ||
                        !transparentRecipe->shaders.Empty()))
            return Fail(diagnostic, MaterialCanonicalResult::InvalidArgument);
        pipelines::BuildDescription depthRecipe = depthPrepassRecipe;
        pipelines::BuildDescription opaque = opaqueRecipe;
        depthRecipe.name = descriptors[0].name;
        opaque.name = descriptors[1].name;
        const char* const opaqueFragment = mapping.GetCoverageMode() == ImportedSurfaceCoverageMode::Masked
                                               ? descriptors[1].maskedFragmentEntryPoint
                                               : descriptors[1].opaqueFragmentEntryPoint;
        const char* const depthFragment = mapping.GetCoverageMode() == ImportedSurfaceCoverageMode::Masked
                                              ? descriptors[0].maskedFragmentEntryPoint
                                              : descriptors[0].opaqueFragmentEntryPoint;
        const shader_tools::EntryPoint primaryEntries[]{{descriptors[1].vertexEntryPoint, shaders::ShaderStage::Vertex},
                                                        {opaqueFragment, shaders::ShaderStage::Fragment}};
        const shader_tools::EntryPoint depthEntries[]{{descriptors[0].vertexEntryPoint, shaders::ShaderStage::Vertex},
                                                      {depthFragment, shaders::ShaderStage::Fragment}};
        const shader_tools::EntryPoint transparentEntries[]{{descriptors[2].vertexEntryPoint, shaders::ShaderStage::Vertex},
                                                            {descriptors[2].opaqueFragmentEntryPoint, shaders::ShaderStage::Fragment}};
        const MaterialCanonicalTechnique techniques[]{
            {descriptors[0].name, descriptors[0].pipelineTemplate, intermediates.depthPipeline, depthRecipe,
             intermediates.depthProgramSource, intermediates.depthShader, depthEntries},
            {descriptors[1].name, descriptors[1].pipelineTemplate, intermediates.opaquePipeline, opaque}};
        pipelines::BuildDescription transparent;
        MaterialCanonicalTechnique blendedTechnique{};
        if (blended)
        {
            transparent = *transparentRecipe;
            transparent.name = descriptors[2].name;
            blendedTechnique = {descriptors[2].name, descriptors[2].pipelineTemplate, intermediates.transparentPipeline, transparent};
        }

        MaterialCanonicalDescription description;
        description.graph = mapping.GetGraph();
        description.frontend = context.GetFrontendConfig();
        description.slang = context.GetSlangDomain();
        description.sourceName = "generated/imported_surface.slang";
        description.moduleName = "vanguard_imported_surface";
        description.program = intermediates.shader.GetPath().Id();
        description.material = materialOutput.GetPath().Id();
        description.entryPoints = blended ? containers::ArraySpan<const shader_tools::EntryPoint>{transparentEntries, 2u}
                                          : containers::ArraySpan<const shader_tools::EntryPoint>{primaryEntries, 2u};
        description.compileSettings = context.GetCompileSettings();
        description.offline = context.GetOfflineContract();
        description.programSource = intermediates.programSource;
        description.shader = intermediates.shader;
        description.materialSource = intermediates.materialSource;
        description.materialOutput = materialOutput;
        description.techniques = blended ? containers::ArraySpan<const MaterialCanonicalTechnique>{&blendedTechnique, 1u}
                                         : containers::ArraySpan<const MaterialCanonicalTechnique>{techniques, 2u};
        description.resources = mapping.GetResources();
        description.target = context.GetTarget();
        description.buildSettings = buildSettings;
        return BuildMaterialCanonicalInputs(description, build, diagnostic, limits);
    }

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
        // Standard imported surfaces select either the two opaque/masked
        // techniques or the single transparent technique. In other domains
        // all registered technique requirements remain mandatory.
        bool matchesImportedVariant = false;
        if (domain.contract.name == GetImportedSurfaceDomainId() && domain.techniques.Count() == 3u)
        {
            const bool hasDepth = techniqueOrder.Size() == 2u &&
                                  (description.techniques[techniqueOrder[0]].name == ImportedSurfaceDepthPrepassTechnique ||
                                   description.techniques[techniqueOrder[1]].name == ImportedSurfaceDepthPrepassTechnique);
            const bool hasOpaque = techniqueOrder.Size() == 2u &&
                                   (description.techniques[techniqueOrder[0]].name == ImportedSurfaceOpaqueTechnique ||
                                    description.techniques[techniqueOrder[1]].name == ImportedSurfaceOpaqueTechnique);
            matchesImportedVariant = (hasDepth && hasOpaque) ||
                                     (techniqueOrder.Size() == 1u &&
                                      description.techniques[techniqueOrder[0]].name == ImportedSurfaceTransparentTechnique);
        }
        else
            matchesImportedVariant = techniqueOrder.Size() == domain.techniques.Count();
        if (!matchesImportedVariant)
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
