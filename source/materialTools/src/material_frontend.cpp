#include <vanguard/material_tools/material_frontend.hpp>

#include <algorithm>

namespace
{
    using namespace vanguard;
    namespace mt = vanguard::material_tools;

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[4]{static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[8]{static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u),
                          static_cast<u8>(value >> 24u), static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                          static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashType(crypto::Sha256Builder& hash, const mt::MaterialIrType& type) noexcept
    {
        const u8 scalar[]{static_cast<u8>(type.kind), static_cast<u8>(type.scalarType), type.rows, type.columns,
                          static_cast<u8>(type.matrixOrder), static_cast<u8>(type.textureDimension),
                          static_cast<u8>(type.resourceAccess), static_cast<u8>(type.bufferKind),
                          static_cast<u8>(type.samplerKind), static_cast<u8>(type.textureArrayed ? 1u : 0u),
                          static_cast<u8>(type.textureMultisampled ? 1u : 0u)};
        static_cast<void>(hash.Update(scalar, sizeof(scalar)));
        HashU32(hash, type.arrayCount);
        static_cast<void>(hash.Update(type.aggregate.bytes, crypto::Digest256::ByteCount));
    }

    [[nodiscard]] bool ValidPinList(const containers::ArraySpan<const mt::MaterialPinSchema> pins) noexcept
    {
        for (u32 index = 0; index < pins.Count(); ++index)
        {
            if (pins[index].id == 0)
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (pins[previous].id == pins[index].id)
                    return false;
        }
        return true;
    }

    struct BindingContext;

    struct GraphFrame
    {
        explicit GraphFrame(const mt::MaterialSourceGraph& source) noexcept
            : graph(&source), values(memory::pools::Tools::GetInstance()), lowered(memory::pools::Tools::GetInstance())
        {
        }
        const mt::MaterialSourceGraph* graph = nullptr;
        containers::HashMap<mt::MaterialSourceValueId, u32> values;
        containers::HashMap<mt::MaterialSourceValueId, mt::MaterialIrValueId> lowered;
        const BindingContext* binding = nullptr;
    };

    struct BindingContext
    {
        GraphFrame* caller = nullptr;
        const mt::MaterialSourceValue* call = nullptr;
    };

    struct ActiveValue
    {
        resources::ResourceId graph = resources::InvalidResourceId;
        mt::MaterialSourceValueId value = mt::InvalidMaterialSourceValue;
        u64 callSite = 0;
    };

    class Lowering final
    {
    public:
        Lowering(const mt::MaterialFrontendConfig& input, mt::MaterialIrBuilder& output, mt::MaterialSourceMap& map,
                 mt::MaterialFrontendDiagnostic* const diagnostic) noexcept
            : config(input), builder(output), sourceMap(map), frontendDiagnostic(diagnostic), active(memory::pools::Tools::GetInstance()),
              functions(memory::pools::Tools::GetInstance()), callSites(memory::pools::Tools::GetInstance())
        {
        }

        mt::MaterialFrontendResult InitializeFrame(GraphFrame& frame) noexcept
        {
            if (frame.graph == nullptr || !frame.graph->identity.IsValid() || frame.graph->values.Count() > config.limits.maximumSourceValues)
                return mt::MaterialFrontendResult::InvalidArgument;
            for (u32 index = 0; index < frame.graph->values.Count(); ++index)
            {
                const mt::MaterialSourceValue& value = frame.graph->values[index];
                if (value.id == mt::InvalidMaterialSourceValue || frame.values.Find(value.id, indexScratch) ||
                    !frame.values.Insert(value.id, index).IsSuccessful())
                {
                    SetFailure(mt::MaterialFrontendResult::InvalidArgument, *frame.graph, &value);
                    return mt::MaterialFrontendResult::InvalidArgument;
                }
            }
            return mt::MaterialFrontendResult::Success;
        }

        mt::MaterialFrontendResult LowerValue(GraphFrame& frame, const mt::MaterialSourceValueId sourceId, mt::MaterialIrValueId& result) noexcept
        {
            if (frame.lowered.Find(sourceId, result))
                return mt::MaterialFrontendResult::Success;
            u32 sourceIndex = 0;
            if (!frame.values.Find(sourceId, sourceIndex) || sourceIndex >= frame.graph->values.Count())
            {
                SetFailure(mt::MaterialFrontendResult::InvalidConnection, *frame.graph, nullptr);
                return mt::MaterialFrontendResult::InvalidConnection;
            }
            const mt::MaterialSourceValue& source = frame.graph->values[sourceIndex];

            if (source.kind == mt::MaterialIrValueKind::DomainInput && frame.binding != nullptr)
            {
                const mt::MaterialSourceOperand* supplied = nullptr;
                for (const mt::MaterialSourceOperand& operand : frame.binding->call->operands)
                    if (operand.input == source.semantic)
                        supplied = &operand;
                if (supplied == nullptr)
                {
                    SetFailure(mt::MaterialFrontendResult::InvalidConnection, *frame.graph, &source);
                    return mt::MaterialFrontendResult::InvalidConnection;
                }
                return LowerValue(*frame.binding->caller, supplied->value, result);
            }

            const ActiveValue key{frame.graph->identity.GetPath().Id(), sourceId, callSites.Empty() ? 0 : callSites.Back()};
            for (const ActiveValue& existing : active)
                if (existing.graph == key.graph && existing.value == key.value && existing.callSite == key.callSite)
                {
                    SetFailure(mt::MaterialFrontendResult::InvalidConnection, *frame.graph, &source);
                    return mt::MaterialFrontendResult::InvalidConnection;
                }
            if (expandedValues >= config.limits.maximumExpandedValues)
            {
                SetFailure(mt::MaterialFrontendResult::LimitExceeded, *frame.graph, &source);
                return mt::MaterialFrontendResult::LimitExceeded;
            }
            ++expandedValues;
            active.PushBack(key);

            mt::MaterialFrontendResult loweredResult = mt::MaterialFrontendResult::Success;
            if (source.nodeType == mt::MaterialFunctionCallNodeType || source.function.IsValid())
                loweredResult = LowerFunction(frame, source, result);
            else if (source.nodeType == mt::MaterialStaticSelectNodeType)
                loweredResult = LowerStaticSelect(frame, source, result);
            else
                loweredResult = LowerOrdinary(frame, source, result);
            active.PopBack();
            if (loweredResult != mt::MaterialFrontendResult::Success)
            {
                SetFailure(loweredResult, *frame.graph, &source);
                return loweredResult;
            }
            if (!frame.lowered.Insert(sourceId, result).IsSuccessful())
            {
                SetFailure(mt::MaterialFrontendResult::LimitExceeded, *frame.graph, &source);
                return mt::MaterialFrontendResult::LimitExceeded;
            }
            const mt::MaterialFrontendResult mapped = AddSourceMap(result, *frame.graph, source);
            if (mapped != mt::MaterialFrontendResult::Success)
                SetFailure(mapped, *frame.graph, &source);
            return mapped;
        }

    private:
        void SetFailure(const mt::MaterialFrontendResult code, const mt::MaterialSourceGraph& graph,
                        const mt::MaterialSourceValue* const source) noexcept
        {
            if (frontendDiagnostic == nullptr || frontendDiagnostic->code != mt::MaterialFrontendResult::Success)
                return;
            frontendDiagnostic->code = code;
            frontendDiagnostic->resource = graph.identity;
            if (source != nullptr)
            {
                frontendDiagnostic->node = source->node;
                frontendDiagnostic->pin = source->outputPin;
            }
        }

        mt::MaterialFrontendResult LowerOrdinary(GraphFrame& frame, const mt::MaterialSourceValue& source, mt::MaterialIrValueId& result) noexcept
        {
            mt::MaterialNodeView node;
            if (!config.nodes->Find(source.nodeType, node))
                return mt::MaterialFrontendResult::UnknownNode;
            if (source.nodeSchemaVersion != node.schemaVersion)
                return mt::MaterialFrontendResult::UnknownNode;
            const mt::MaterialPinSchema* output = nullptr;
            for (const mt::MaterialPinSchema& pin : node.outputs)
                if (pin.id == source.outputPin)
                    output = &pin;
            if (output == nullptr || !(output->type == source.type) || source.legalStages == 0)
                return mt::MaterialFrontendResult::InvalidPin;

            containers::DynamicArray<mt::MaterialIrValueId> operands{memory::pools::Tools::GetInstance()};
            operands.Reserve(node.inputs.Count());
            for (const mt::MaterialPinSchema& input : node.inputs)
            {
                const mt::MaterialSourceOperand* connection = nullptr;
                for (const mt::MaterialSourceOperand& operand : source.operands)
                {
                    if (operand.input == input.id)
                    {
                        if (connection != nullptr)
                            return mt::MaterialFrontendResult::InvalidConnection;
                        connection = &operand;
                    }
                }
                if (connection == nullptr)
                {
                    if (input.required)
                        return mt::MaterialFrontendResult::InvalidConnection;
                    continue;
                }
                mt::MaterialIrValueId operand = mt::InvalidMaterialIrValue;
                const mt::MaterialFrontendResult lowered = LowerValue(frame, connection->value, operand);
                if (lowered != mt::MaterialFrontendResult::Success)
                    return lowered;
                operands.PushBack(operand);
            }
            mt::MaterialIrValueBuildDescription description;
            description.kind = source.kind;
            description.opcode = source.opcode;
            description.type = source.type;
            description.legalStages = source.legalStages;
            description.semantic = source.semantic;
            description.sourceNode = source.node;
            description.sourcePin = source.outputPin;
            description.operands = operands;
            description.data = source.data;
            return builder.AddValue(description, result) == mt::MaterialIrResult::Success ? mt::MaterialFrontendResult::Success
                                                                                           : mt::MaterialFrontendResult::IrFailure;
        }

        mt::MaterialFrontendResult LowerStaticSelect(GraphFrame& frame, const mt::MaterialSourceValue& source,
                                                      mt::MaterialIrValueId& result) noexcept
        {
            const mt::MaterialSourceOperand* conditionConnection = nullptr;
            const mt::MaterialSourceOperand* trueConnection = nullptr;
            const mt::MaterialSourceOperand* falseConnection = nullptr;
            for (const mt::MaterialSourceOperand& operand : source.operands)
            {
                const mt::MaterialSourceOperand** destination = nullptr;
                if (operand.input == mt::MaterialStaticSelectConditionPin) destination = &conditionConnection;
                else if (operand.input == mt::MaterialStaticSelectTruePin) destination = &trueConnection;
                else if (operand.input == mt::MaterialStaticSelectFalsePin) destination = &falseConnection;
                else return mt::MaterialFrontendResult::InvalidPin;
                if (*destination != nullptr)
                    return mt::MaterialFrontendResult::InvalidConnection;
                *destination = &operand;
            }
            if (conditionConnection == nullptr || trueConnection == nullptr || falseConnection == nullptr)
                return mt::MaterialFrontendResult::InvalidConnection;

            u32 conditionIndex = 0;
            u32 selectedIndex = 0;
            if (!frame.values.Find(conditionConnection->value, conditionIndex) || conditionIndex >= frame.graph->values.Count())
                return mt::MaterialFrontendResult::InvalidConnection;
            const mt::MaterialSourceValue& condition = frame.graph->values[conditionIndex];
            if ((condition.kind != mt::MaterialIrValueKind::StaticParameter && condition.kind != mt::MaterialIrValueKind::Constant) ||
                condition.type.kind != mt::MaterialIrTypeKind::Numeric || condition.type.scalarType != shaders::ScalarType::Bool ||
                condition.type.rows != 1 || condition.type.columns != 1 || condition.type.arrayCount != 1 || condition.data.Count() != 1 ||
                condition.data[0] > 1)
                return mt::MaterialFrontendResult::InvalidConnection;
            const mt::MaterialSourceValueId selectedId = condition.data[0] != 0 ? trueConnection->value : falseConnection->value;
            if (!frame.values.Find(selectedId, selectedIndex) || selectedIndex >= frame.graph->values.Count() ||
                !(frame.graph->values[selectedIndex].type == source.type))
                return mt::MaterialFrontendResult::InvalidConnection;
            return LowerValue(frame, selectedId, result);
        }

        mt::MaterialFrontendResult LowerFunction(GraphFrame& caller, const mt::MaterialSourceValue& call, mt::MaterialIrValueId& result) noexcept
        {
            if (config.loadFunction == nullptr || !call.function.IsValid() || call.functionOutput == 0)
                return mt::MaterialFrontendResult::FunctionUnavailable;
            if (functions.Size() >= config.limits.maximumFunctionDepth || callSites.Size() >= config.limits.maximumCallSites)
                return mt::MaterialFrontendResult::LimitExceeded;
            for (const resources::ResourceId function : functions)
                if (function == call.function.GetPath().Id())
                    return mt::MaterialFrontendResult::FunctionCycle;
            mt::MaterialSourceGraph graph;
            if (!config.loadFunction(call.function, graph, config.loadFunctionUserData) || graph.identity != call.function)
                return mt::MaterialFrontendResult::FunctionUnavailable;
            GraphFrame functionFrame(graph);
            BindingContext binding{&caller, &call};
            functionFrame.binding = &binding;
            const mt::MaterialFrontendResult initialized = InitializeFrame(functionFrame);
            if (initialized != mt::MaterialFrontendResult::Success)
                return initialized;
            const mt::MaterialSourceOutput* selected = nullptr;
            for (const mt::MaterialSourceOutput& output : graph.outputs)
                if (output.name == call.functionOutput)
                    selected = &output;
            if (selected == nullptr)
                return mt::MaterialFrontendResult::InvalidConnection;
            functions.PushBack(call.function.GetPath().Id());
            callSites.PushBack(call.node);
            const mt::MaterialFrontendResult lowered = LowerValue(functionFrame, selected->value, result);
            callSites.PopBack();
            functions.PopBack();
            return lowered;
        }

        mt::MaterialFrontendResult AddSourceMap(const mt::MaterialIrValueId value, const mt::MaterialSourceGraph& graph,
                                                const mt::MaterialSourceValue& source) noexcept
        {
            return sourceMap.Add(value, graph.identity, source.node, source.outputPin, callSites, config.limits.maximumSourceMapEntries)
                       ? mt::MaterialFrontendResult::Success
                       : mt::MaterialFrontendResult::LimitExceeded;
        }

        const mt::MaterialFrontendConfig& config;
        mt::MaterialIrBuilder& builder;
        mt::MaterialSourceMap& sourceMap;
        mt::MaterialFrontendDiagnostic* frontendDiagnostic = nullptr;
        containers::DynamicArray<ActiveValue> active;
        containers::DynamicArray<resources::ResourceId> functions;
        containers::DynamicArray<u64> callSites;
        u32 expandedValues = 0;
        u32 indexScratch = 0;
    };
} // namespace

namespace vanguard::material_tools
{
    MaterialNodeRegistry::MaterialNodeRegistry() noexcept
        : m_records(memory::pools::Tools::GetInstance()), m_pins(memory::pools::Tools::GetInstance())
    {
    }

    bool MaterialNodeRegistry::Register(const MaterialNodeDescriptor& descriptor) noexcept
    {
        if (m_frozen || descriptor.type == InvalidMaterialNodeType || descriptor.type == MaterialFunctionCallNodeType ||
            descriptor.type == MaterialStaticSelectNodeType || descriptor.schemaVersion == 0 ||
            descriptor.implementationFingerprint.IsEmpty() || descriptor.outputs.Empty() || !ValidPinList(descriptor.inputs) ||
            !ValidPinList(descriptor.outputs))
            return false;
        for (const Record& record : m_records)
            if (record.type == descriptor.type)
                return false;
        Record record;
        record.type = descriptor.type;
        record.schemaVersion = descriptor.schemaVersion;
        record.implementationFingerprint = descriptor.implementationFingerprint;
        record.firstInput = m_pins.Size();
        record.inputCount = descriptor.inputs.Count();
        for (const MaterialPinSchema& pin : descriptor.inputs)
            m_pins.PushBack(pin);
        if (record.inputCount > 1)
            std::sort(m_pins.Begin() + record.firstInput, m_pins.Begin() + record.firstInput + record.inputCount,
                      [](const MaterialPinSchema& left, const MaterialPinSchema& right) { return left.id < right.id; });
        record.firstOutput = m_pins.Size();
        record.outputCount = descriptor.outputs.Count();
        for (const MaterialPinSchema& pin : descriptor.outputs)
            m_pins.PushBack(pin);
        if (record.outputCount > 1)
            std::sort(m_pins.Begin() + record.firstOutput, m_pins.Begin() + record.firstOutput + record.outputCount,
                      [](const MaterialPinSchema& left, const MaterialPinSchema& right) { return left.id < right.id; });
        m_records.PushBack(record);
        return true;
    }

    bool MaterialNodeRegistry::Freeze() noexcept
    {
        if (m_frozen)
            return true;
        if (m_records.Empty())
            return false;
        std::sort(m_records.Begin(), m_records.End(), [](const Record& left, const Record& right) { return left.type < right.type; });
        crypto::Sha256Builder hash;
        HashU32(hash, 1);
        for (const Record& record : m_records)
        {
            HashU64(hash, record.type);
            HashU32(hash, record.schemaVersion);
            static_cast<void>(hash.Update(record.implementationFingerprint.bytes, crypto::Digest256::ByteCount));
            HashU32(hash, record.inputCount);
            HashU32(hash, record.outputCount);
            for (u32 index = 0; index < record.inputCount; ++index)
            {
                const MaterialPinSchema& pin = m_pins[record.firstInput + index];
                HashU32(hash, pin.id);
                HashType(hash, pin.type);
                HashU32(hash, pin.required ? 1u : 0u);
            }
            for (u32 index = 0; index < record.outputCount; ++index)
            {
                const MaterialPinSchema& pin = m_pins[record.firstOutput + index];
                HashU32(hash, pin.id);
                HashType(hash, pin.type);
            }
        }
        m_frozen = hash.Finalize(m_fingerprint);
        return m_frozen;
    }

    bool MaterialNodeRegistry::IsFrozen() const noexcept { return m_frozen; }
    const crypto::Digest256& MaterialNodeRegistry::Fingerprint() const noexcept { return m_fingerprint; }

    bool MaterialNodeRegistry::Find(const MaterialNodeTypeId type, MaterialNodeView& view) const noexcept
    {
        view = {};
        if (!m_frozen)
            return false;
        const auto record = std::lower_bound(m_records.Begin(), m_records.End(), type,
                                             [](const Record& candidate, const MaterialNodeTypeId key) { return candidate.type < key; });
        if (record == m_records.End() || record->type != type)
            return false;
        view = {record->type, record->schemaVersion, record->implementationFingerprint,
                {m_pins.TypedData() + record->firstInput, record->inputCount},
                {m_pins.TypedData() + record->firstOutput, record->outputCount}};
        return true;
    }

    MaterialDomainRegistry::MaterialDomainRegistry() noexcept
        : m_records(memory::pools::Tools::GetInstance()), m_inputs(memory::pools::Tools::GetInstance()),
          m_outputs(memory::pools::Tools::GetInstance()),
          m_defaultRanges(memory::pools::Tools::GetInstance()), m_defaults(memory::pools::Tools::GetInstance()),
          m_techniques(memory::pools::Tools::GetInstance())
    {
    }

    bool MaterialDomainRegistry::Register(const MaterialDomainDescriptor& descriptor) noexcept
    {
        crypto::Digest256 contractFingerprint;
        if (m_frozen || descriptor.domain == InvalidMaterialDomain || descriptor.domain != descriptor.contract.name ||
            descriptor.implementationFingerprint.IsEmpty() || descriptor.outputs.Empty() ||
            shaders::CalculateMaterialDomainFingerprint(descriptor.contract, contractFingerprint) != shaders::Result::Success)
            return false;
        for (const Record& record : m_records)
            if (record.domain == descriptor.domain)
                return false;
        for (u32 index = 0; index < descriptor.inputs.Count(); ++index)
        {
            const MaterialDomainInputSchema& input = descriptor.inputs[index];
            if (input.name == 0 || input.stages == 0 || (input.stages & ~descriptor.contract.legalStages) != 0)
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (descriptor.inputs[previous].name == input.name)
                    return false;
        }
        for (u32 index = 0; index < descriptor.outputs.Count(); ++index)
        {
            const MaterialDomainOutputSchema& output = descriptor.outputs[index];
            if (output.name == 0 || output.stages == 0 || (output.stages & ~descriptor.contract.legalStages) != 0)
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (descriptor.outputs[previous].name == output.name)
                    return false;
        }
        for (u32 index = 0; index < descriptor.techniques.Count(); ++index)
        {
            if (descriptor.techniques[index].name == 0 || !descriptor.techniques[index].pipelineTemplate.IsValid())
                return false;
            for (u32 previous = 0; previous < index; ++previous)
                if (descriptor.techniques[previous].name == descriptor.techniques[index].name)
                    return false;
        }
        Record record;
        record.domain = descriptor.domain;
        record.contract = descriptor.contract;
        record.contractFingerprint = contractFingerprint;
        record.implementationFingerprint = descriptor.implementationFingerprint;
        record.firstInput = m_inputs.Size();
        record.inputCount = descriptor.inputs.Count();
        for (const MaterialDomainInputSchema& input : descriptor.inputs)
            m_inputs.PushBack(input);
        if (record.inputCount > 1)
            std::sort(m_inputs.Begin() + record.firstInput, m_inputs.Begin() + record.firstInput + record.inputCount,
                      [](const MaterialDomainInputSchema& left, const MaterialDomainInputSchema& right) { return left.name < right.name; });
        record.firstOutput = m_outputs.Size();
        record.outputCount = descriptor.outputs.Count();
        for (u32 index = 0; index < descriptor.outputs.Count(); ++index)
        {
            const MaterialDomainOutputSchema& output = descriptor.outputs[index];
            const u32 offset = m_defaults.Size();
            for (const u8 byte : output.defaultValue)
                m_defaults.PushBack(byte);
            MaterialDomainOutputSchema stored = output;
            stored.defaultValue = {};
            m_outputs.PushBack(stored);
            m_defaultRanges.PushBack({offset, output.defaultValue.Count()});
        }
        // Keep the copied output/default pairs together while canonicalizing
        // by stable output identity.
        for (u32 left = 0; left < record.outputCount; ++left)
        {
            for (u32 right = left + 1u; right < record.outputCount; ++right)
            {
                const u32 leftIndex = record.firstOutput + left;
                const u32 rightIndex = record.firstOutput + right;
                if (m_outputs[rightIndex].name < m_outputs[leftIndex].name)
                {
                    std::swap(m_outputs[leftIndex], m_outputs[rightIndex]);
                    std::swap(m_defaultRanges[leftIndex], m_defaultRanges[rightIndex]);
                }
            }
        }
        record.firstTechnique = m_techniques.Size();
        record.techniqueCount = descriptor.techniques.Count();
        for (u32 index = 0; index < descriptor.techniques.Count(); ++index)
            m_techniques.PushBack(descriptor.techniques[index]);
        if (record.techniqueCount > 1)
            std::sort(m_techniques.Begin() + record.firstTechnique, m_techniques.Begin() + record.firstTechnique + record.techniqueCount,
                      [](const MaterialTechniqueRequirement& left, const MaterialTechniqueRequirement& right) { return left.name < right.name; });
        m_records.PushBack(record);
        return true;
    }

    bool MaterialDomainRegistry::Freeze() noexcept
    {
        if (m_frozen)
            return true;
        if (m_records.Empty())
            return false;
        for (u32 index = 0; index < m_outputs.Size(); ++index)
        {
            const DefaultRange range = m_defaultRanges[index];
            m_outputs[index].defaultValue = range.size != 0 ? containers::ArraySpan<const u8>{m_defaults.TypedData() + range.offset, range.size}
                                                            : containers::ArraySpan<const u8>{};
        }
        std::sort(m_records.Begin(), m_records.End(), [](const Record& left, const Record& right) { return left.domain < right.domain; });
        crypto::Sha256Builder hash;
        HashU32(hash, 2);
        for (const Record& record : m_records)
        {
            HashU64(hash, record.domain);
            static_cast<void>(hash.Update(record.contractFingerprint.bytes, crypto::Digest256::ByteCount));
            static_cast<void>(hash.Update(record.implementationFingerprint.bytes, crypto::Digest256::ByteCount));
            for (u32 index = 0; index < record.inputCount; ++index)
            {
                const MaterialDomainInputSchema& input = m_inputs[record.firstInput + index];
                HashU64(hash, input.name);
                HashType(hash, input.type);
                HashU32(hash, input.stages);
            }
            for (u32 index = 0; index < record.outputCount; ++index)
            {
                const MaterialDomainOutputSchema& output = m_outputs[record.firstOutput + index];
                HashU64(hash, output.name);
                HashType(hash, output.type);
                HashU32(hash, output.stages);
                HashU32(hash, output.defaultValue.Count());
                if (!output.defaultValue.Empty())
                    static_cast<void>(hash.Update(output.defaultValue.Data(), output.defaultValue.Count()));
            }
            for (u32 index = 0; index < record.techniqueCount; ++index)
            {
                const MaterialTechniqueRequirement& technique = m_techniques[record.firstTechnique + index];
                HashU64(hash, technique.name);
                HashU64(hash, technique.pipelineTemplate.GetPath().Id());
            }
        }
        m_frozen = hash.Finalize(m_fingerprint);
        return m_frozen;
    }

    bool MaterialDomainRegistry::IsFrozen() const noexcept { return m_frozen; }
    const crypto::Digest256& MaterialDomainRegistry::Fingerprint() const noexcept { return m_fingerprint; }

    bool MaterialDomainRegistry::Find(const MaterialDomainId domain, MaterialDomainView& view) const noexcept
    {
        view = {};
        if (!m_frozen)
            return false;
        const auto record = std::lower_bound(m_records.Begin(), m_records.End(), domain,
                                             [](const Record& candidate, const MaterialDomainId key) { return candidate.domain < key; });
        if (record == m_records.End() || record->domain != domain)
            return false;
        view = {record->domain, record->contract, record->contractFingerprint, record->implementationFingerprint,
                {m_inputs.TypedData() + record->firstInput, record->inputCount},
                {m_outputs.TypedData() + record->firstOutput, record->outputCount},
                {m_techniques.TypedData() + record->firstTechnique, record->techniqueCount}};
        return true;
    }

    MaterialSourceMap::MaterialSourceMap() noexcept
        : m_entries(memory::pools::Tools::GetInstance()), m_callSites(memory::pools::Tools::GetInstance())
    {
    }
    void MaterialSourceMap::Reset() noexcept { m_entries.Clear(); m_callSites.Clear(); }
    containers::ArraySpan<const MaterialSourceMapEntry> MaterialSourceMap::Entries() const noexcept { return m_entries; }
    containers::ArraySpan<const u64> MaterialSourceMap::CallSites(const MaterialSourceMapEntry& entry) const noexcept
    {
        if (entry.callSiteCount == 0 || entry.firstCallSite > m_callSites.Size() || entry.callSiteCount > m_callSites.Size() - entry.firstCallSite)
            return {};
        return {m_callSites.TypedData() + entry.firstCallSite, entry.callSiteCount};
    }

    bool MaterialSourceMap::Add(const MaterialIrValueId irValue, const resources::ResourceReference resource, const u64 node, const u32 pin,
                                const containers::ArraySpan<const u64> callSites, const u32 maximumEntries) noexcept
    {
        if (irValue == InvalidMaterialIrValue || !resource.IsValid() || node == 0 || m_entries.Size() >= maximumEntries ||
            m_callSites.Size() > ~u32{0} - callSites.Count())
            return false;
        const u32 first = m_callSites.Size();
        for (const u64 callSite : callSites)
            m_callSites.PushBack(callSite);
        m_entries.PushBack({irValue, resource, node, pin, first, callSites.Count()});
        return true;
    }

    MaterialFrontendResult MaterialFrontend::Lower(const MaterialSourceGraph& graph, const MaterialFrontendConfig& config, MaterialIrModule& module,
                                                   MaterialSourceMap& sourceMap,
                                                   containers::DynamicArray<MaterialIrDiagnostic>& diagnostics,
                                                   MaterialFrontendDiagnostic* const frontendDiagnostic) noexcept
    {
        module.Reset();
        sourceMap.Reset();
        diagnostics.Clear();
        if (frontendDiagnostic != nullptr)
            *frontendDiagnostic = {};
        const auto fail = [&](const MaterialFrontendResult result) noexcept
        {
            if (frontendDiagnostic != nullptr && frontendDiagnostic->code == MaterialFrontendResult::Success)
            {
                frontendDiagnostic->code = result;
                frontendDiagnostic->resource = graph.identity;
            }
            return result;
        };
        if (config.nodes == nullptr || config.domains == nullptr)
            return fail(MaterialFrontendResult::InvalidArgument);
        if (!config.nodes->IsFrozen() || !config.domains->IsFrozen())
            return fail(MaterialFrontendResult::RegistryNotFrozen);
        MaterialDomainView domain;
        if (!config.domains->Find(graph.domain, domain))
            return fail(MaterialFrontendResult::UnknownDomain);
        MaterialIrBuilder builder;
        builder.Reset(domain.implementationFingerprint, config.types);
        GraphFrame frame(graph);
        Lowering lowering(config, builder, sourceMap, frontendDiagnostic);
        MaterialFrontendResult result = lowering.InitializeFrame(frame);
        if (result != MaterialFrontendResult::Success)
            return fail(result);
        for (const MaterialDomainOutputSchema& required : domain.outputs)
        {
            const MaterialSourceOutput* sourceOutput = nullptr;
            for (const MaterialSourceOutput& candidate : graph.outputs)
                if (candidate.name == required.name)
                    sourceOutput = &candidate;
            MaterialIrValueId value = InvalidMaterialIrValue;
            if (sourceOutput != nullptr)
            {
                if (sourceOutput->stages != required.stages)
                    return fail(MaterialFrontendResult::InvalidPin);
                result = lowering.LowerValue(frame, sourceOutput->value, value);
                if (result != MaterialFrontendResult::Success)
                    return fail(result);
            }
            else
            {
                if (required.defaultValue.Empty())
                    return fail(MaterialFrontendResult::InvalidConnection);
                MaterialIrValueBuildDescription defaultValue;
                defaultValue.kind = MaterialIrValueKind::Constant;
                defaultValue.type = required.type;
                defaultValue.legalStages = required.stages;
                defaultValue.data = required.defaultValue;
                if (builder.AddValue(defaultValue, value) != MaterialIrResult::Success)
                    return fail(MaterialFrontendResult::IrFailure);
            }
            if (builder.AddOutput({required.name, value, required.stages}) != MaterialIrResult::Success)
                return fail(MaterialFrontendResult::IrFailure);
        }
        containers::DynamicArray<MaterialIrValueId> remap{memory::pools::Tools::GetInstance()};
        if (builder.Finalize(module, diagnostics, config.limits.ir, &remap) != MaterialIrResult::Success)
        {
            if (frontendDiagnostic != nullptr && frontendDiagnostic->code == MaterialFrontendResult::Success && !diagnostics.Empty())
            {
                frontendDiagnostic->code = MaterialFrontendResult::IrFailure;
                frontendDiagnostic->resource = graph.identity;
                frontendDiagnostic->node = diagnostics[0].sourceNode;
                frontendDiagnostic->pin = diagnostics[0].sourcePin;
            }
            return fail(MaterialFrontendResult::IrFailure);
        }
        for (MaterialSourceMapEntry& entry : sourceMap.m_entries)
        {
            if (entry.irValue >= remap.Size() || remap[entry.irValue] == InvalidMaterialIrValue)
                return fail(MaterialFrontendResult::IrFailure);
            entry.irValue = remap[entry.irValue];
        }
        for (const MaterialIrValue& value : module.GetValues())
        {
            if (value.kind != MaterialIrValueKind::DomainInput)
                continue;
            const MaterialDomainInputSchema* schema = nullptr;
            for (const MaterialDomainInputSchema& candidate : domain.inputs)
                if (candidate.name == value.semantic)
                    schema = &candidate;
            if (schema == nullptr || !(value.type == schema->type) || value.legalStages != schema->stages)
                return fail(MaterialFrontendResult::InvalidPin);
        }
        for (const MaterialIrOutput& output : module.GetOutputs())
        {
            const MaterialDomainOutputSchema* schema = nullptr;
            for (const MaterialDomainOutputSchema& candidate : domain.outputs)
                if (candidate.name == output.name)
                    schema = &candidate;
            if (schema == nullptr || output.value >= module.GetValues().Count() || !(module.GetValues()[output.value].type == schema->type))
                return fail(MaterialFrontendResult::InvalidPin);
        }
        return MaterialFrontendResult::Success;
    }

    const char* ToString(const MaterialFrontendResult result) noexcept
    {
        switch (result)
        {
        case MaterialFrontendResult::Success: return "Success";
        case MaterialFrontendResult::InvalidArgument: return "InvalidArgument";
        case MaterialFrontendResult::RegistryNotFrozen: return "RegistryNotFrozen";
        case MaterialFrontendResult::UnknownDomain: return "UnknownDomain";
        case MaterialFrontendResult::UnknownNode: return "UnknownNode";
        case MaterialFrontendResult::InvalidPin: return "InvalidPin";
        case MaterialFrontendResult::InvalidConnection: return "InvalidConnection";
        case MaterialFrontendResult::FunctionUnavailable: return "FunctionUnavailable";
        case MaterialFrontendResult::FunctionCycle: return "FunctionCycle";
        case MaterialFrontendResult::LimitExceeded: return "LimitExceeded";
        case MaterialFrontendResult::IrFailure: return "IrFailure";
        }
        return "Unknown";
    }
} // namespace vanguard::material_tools
