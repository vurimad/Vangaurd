#pragma once

#include <vanguard/material_tools/material_ir.hpp>

namespace vanguard::material_tools
{
    using MaterialNodeTypeId = u64;
    using MaterialDomainId = u64;
    using MaterialPinId = u32;
    using MaterialSourceValueId = u64;

    inline constexpr MaterialNodeTypeId InvalidMaterialNodeType = 0;
    inline constexpr MaterialDomainId InvalidMaterialDomain = 0;
    inline constexpr MaterialSourceValueId InvalidMaterialSourceValue = 0;
    inline constexpr MaterialNodeTypeId MaterialFunctionCallNodeType = 0xfffffffffffffff0ull;
    /// Reserved compile-time selection node. Operand pins 1, 2, and 3 are the
    /// boolean condition, true value, and false value respectively.
    inline constexpr MaterialNodeTypeId MaterialStaticSelectNodeType = 0xfffffffffffffff1ull;
    inline constexpr u64 MaterialStaticSelectConditionPin = 1;
    inline constexpr u64 MaterialStaticSelectTruePin = 2;
    inline constexpr u64 MaterialStaticSelectFalsePin = 3;

    struct MaterialPinSchema
    {
        MaterialPinId id = 0;
        MaterialIrType type;
        bool required = true;
    };

    struct MaterialNodeDescriptor
    {
        MaterialNodeTypeId type = InvalidMaterialNodeType;
        u32 schemaVersion = 0;
        crypto::Digest256 implementationFingerprint;
        containers::ArraySpan<const MaterialPinSchema> inputs;
        containers::ArraySpan<const MaterialPinSchema> outputs;
    };

    struct MaterialNodeView
    {
        MaterialNodeTypeId type = InvalidMaterialNodeType;
        u32 schemaVersion = 0;
        crypto::Digest256 implementationFingerprint;
        containers::ArraySpan<const MaterialPinSchema> inputs;
        containers::ArraySpan<const MaterialPinSchema> outputs;
    };

    class MaterialNodeRegistry final
    {
    public:
        MaterialNodeRegistry() noexcept;
        [[nodiscard]] bool Register(const MaterialNodeDescriptor& descriptor) noexcept;
        [[nodiscard]] bool Freeze() noexcept;
        [[nodiscard]] bool IsFrozen() const noexcept;
        [[nodiscard]] bool Find(MaterialNodeTypeId type, MaterialNodeView& view) const noexcept;
        [[nodiscard]] const crypto::Digest256& Fingerprint() const noexcept;

    private:
        struct Record
        {
            MaterialNodeTypeId type = InvalidMaterialNodeType;
            u32 schemaVersion = 0;
            crypto::Digest256 implementationFingerprint;
            u32 firstInput = 0;
            u32 inputCount = 0;
            u32 firstOutput = 0;
            u32 outputCount = 0;
        };
        containers::DynamicArray<Record> m_records;
        containers::DynamicArray<MaterialPinSchema> m_pins;
        crypto::Digest256 m_fingerprint;
        bool m_frozen = false;
    };

    struct MaterialDomainInputSchema
    {
        u64 name = 0;
        MaterialIrType type;
        shaders::StageMask stages = 0;
    };

    struct MaterialDomainOutputSchema
    {
        u64 name = 0;
        MaterialIrType type;
        shaders::StageMask stages = 0;
        containers::ArraySpan<const u8> defaultValue;
    };

    struct MaterialTechniqueRequirement
    {
        u64 name = 0;
        resources::ResourceReference pipelineTemplate;
    };

    struct MaterialDomainDescriptor
    {
        MaterialDomainId domain = InvalidMaterialDomain;
        shaders::MaterialDomainContract contract;
        crypto::Digest256 implementationFingerprint;
        containers::ArraySpan<const MaterialDomainInputSchema> inputs;
        containers::ArraySpan<const MaterialDomainOutputSchema> outputs;
        containers::ArraySpan<const MaterialTechniqueRequirement> techniques;
    };

    struct MaterialDomainView
    {
        MaterialDomainId domain = InvalidMaterialDomain;
        shaders::MaterialDomainContract contract;
        crypto::Digest256 contractFingerprint;
        crypto::Digest256 implementationFingerprint;
        containers::ArraySpan<const MaterialDomainInputSchema> inputs;
        containers::ArraySpan<const MaterialDomainOutputSchema> outputs;
        containers::ArraySpan<const MaterialTechniqueRequirement> techniques;
    };

    class MaterialDomainRegistry final
    {
    public:
        MaterialDomainRegistry() noexcept;
        [[nodiscard]] bool Register(const MaterialDomainDescriptor& descriptor) noexcept;
        [[nodiscard]] bool Freeze() noexcept;
        [[nodiscard]] bool IsFrozen() const noexcept;
        [[nodiscard]] bool Find(MaterialDomainId domain, MaterialDomainView& view) const noexcept;
        [[nodiscard]] const crypto::Digest256& Fingerprint() const noexcept;

    private:
        struct Record
        {
            MaterialDomainId domain = InvalidMaterialDomain;
            shaders::MaterialDomainContract contract;
            crypto::Digest256 contractFingerprint;
            crypto::Digest256 implementationFingerprint;
            u32 firstInput = 0;
            u32 inputCount = 0;
            u32 firstOutput = 0;
            u32 outputCount = 0;
            u32 firstTechnique = 0;
            u32 techniqueCount = 0;
        };
        containers::DynamicArray<Record> m_records;
        containers::DynamicArray<MaterialDomainInputSchema> m_inputs;
        containers::DynamicArray<MaterialDomainOutputSchema> m_outputs;
        struct DefaultRange { u32 offset = 0; u32 size = 0; };
        containers::DynamicArray<DefaultRange> m_defaultRanges;
        containers::DynamicArray<u8> m_defaults;
        containers::DynamicArray<MaterialTechniqueRequirement> m_techniques;
        crypto::Digest256 m_fingerprint;
        bool m_frozen = false;
    };

    struct MaterialSourceOperand
    {
        u64 input = 0;
        MaterialSourceValueId value = InvalidMaterialSourceValue;
    };

    struct MaterialSourceValue
    {
        MaterialSourceValueId id = InvalidMaterialSourceValue;
        u64 node = 0;
        MaterialNodeTypeId nodeType = InvalidMaterialNodeType;
        u32 nodeSchemaVersion = 0;
        MaterialPinId outputPin = 0;
        MaterialIrValueKind kind = MaterialIrValueKind::Instruction;
        MaterialIrOpcode opcode = MaterialIrOpcode::None;
        MaterialIrType type;
        shaders::StageMask legalStages = 0;
        u64 semantic = 0;
        containers::ArraySpan<const MaterialSourceOperand> operands;
        containers::ArraySpan<const u8> data;
        resources::ResourceReference function;
        u64 functionOutput = 0;
    };

    struct MaterialSourceOutput
    {
        u64 name = 0;
        MaterialSourceValueId value = InvalidMaterialSourceValue;
        shaders::StageMask stages = 0;
    };

    struct MaterialSourceGraph
    {
        resources::ResourceReference identity;
        MaterialDomainId domain = InvalidMaterialDomain;
        containers::ArraySpan<const MaterialSourceValue> values;
        containers::ArraySpan<const MaterialSourceOutput> outputs;
    };

    using LoadMaterialFunction = bool (*)(resources::ResourceReference function, MaterialSourceGraph& graph, void* userData) noexcept;

    struct MaterialFrontendLimits
    {
        u32 maximumSourceValues = 65536;
        u32 maximumExpandedValues = 262144;
        u32 maximumFunctionDepth = 64;
        u32 maximumCallSites = 16384;
        u32 maximumSourceMapEntries = 262144;
        MaterialIrLimits ir;
    };

    enum class MaterialFrontendResult : u8
    {
        Success,
        InvalidArgument,
        RegistryNotFrozen,
        UnknownDomain,
        UnknownNode,
        InvalidPin,
        InvalidConnection,
        FunctionUnavailable,
        FunctionCycle,
        LimitExceeded,
        IrFailure
    };

    struct MaterialFrontendDiagnostic
    {
        MaterialFrontendResult code = MaterialFrontendResult::Success;
        resources::ResourceReference resource;
        u64 node = 0;
        u32 pin = 0;
    };

    struct MaterialSourceMapEntry
    {
        MaterialIrValueId irValue = InvalidMaterialIrValue;
        resources::ResourceReference resource;
        u64 node = 0;
        u32 pin = 0;
        u32 firstCallSite = 0;
        u32 callSiteCount = 0;
    };

    class MaterialSourceMap final
    {
    public:
        MaterialSourceMap() noexcept;
        void Reset() noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialSourceMapEntry> Entries() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u64> CallSites(const MaterialSourceMapEntry& entry) const noexcept;
        [[nodiscard]] bool Add(MaterialIrValueId irValue, resources::ResourceReference resource, u64 node, u32 pin,
                               containers::ArraySpan<const u64> callSites,
                               u32 maximumEntries) noexcept;

    private:
        containers::DynamicArray<MaterialSourceMapEntry> m_entries;
        containers::DynamicArray<u64> m_callSites;
        friend class MaterialFrontend;
    };

    struct MaterialFrontendConfig
    {
        const MaterialNodeRegistry* nodes = nullptr;
        const MaterialDomainRegistry* domains = nullptr;
        const MaterialIrTypeRegistry* types = nullptr;
        LoadMaterialFunction loadFunction = nullptr;
        void* loadFunctionUserData = nullptr;
        MaterialFrontendLimits limits;
    };

    class MaterialFrontend final
    {
    public:
        [[nodiscard]] MaterialFrontendResult Lower(const MaterialSourceGraph& graph, const MaterialFrontendConfig& config, MaterialIrModule& module,
                                                   MaterialSourceMap& sourceMap,
                                                   containers::DynamicArray<MaterialIrDiagnostic>& diagnostics,
                                                   MaterialFrontendDiagnostic* frontendDiagnostic = nullptr) noexcept;
    };

    [[nodiscard]] const char* ToString(MaterialFrontendResult result) noexcept;
} // namespace vanguard::material_tools
