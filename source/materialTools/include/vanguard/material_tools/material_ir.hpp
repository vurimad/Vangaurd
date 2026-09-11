#pragma once

#include <vanguard/shaders/shaders.hpp>

namespace vanguard::material_tools
{
    using MaterialIrValueId = u32;
    inline constexpr MaterialIrValueId InvalidMaterialIrValue = 0xffffffffu;
    using MaterialIrTypeId = u32;
    inline constexpr MaterialIrTypeId InvalidMaterialIrType = 0xffffffffu;

    enum class MaterialIrResult : u8
    {
        Success,
        InvalidArgument,
        InvalidType,
        InvalidOperand,
        CycleDetected,
        DuplicateParameter,
        InvalidStage,
        Poisoned,
        LimitExceeded
    };

    enum class MaterialIrTypeKind : u8
    {
        Poison,
        Void,
        Numeric,
        Aggregate,
        Texture,
        Sampler,
        Buffer,
        AccelerationStructure
    };

    enum class MaterialIrMatrixOrder : u8
    {
        None,
        RowMajor,
        ColumnMajor
    };

    enum class MaterialIrTextureDimension : u8
    {
        None,
        D1,
        D2,
        D3,
        Cube
    };

    enum class MaterialIrResourceAccess : u8
    {
        Read,
        Write,
        ReadWrite
    };

    enum class MaterialIrBufferKind : u8
    {
        None,
        Typed,
        Structured,
        ByteAddress
    };

    enum class MaterialIrSamplerKind : u8
    {
        Filtering,
        Comparison
    };

    struct MaterialIrType
    {
        MaterialIrTypeKind kind = MaterialIrTypeKind::Poison;
        shaders::ScalarType scalarType = shaders::ScalarType::F32;
        u8 rows = 1;
        u8 columns = 1;
        u32 arrayCount = 1;
        MaterialIrMatrixOrder matrixOrder = MaterialIrMatrixOrder::None;
        MaterialIrTextureDimension textureDimension = MaterialIrTextureDimension::None;
        MaterialIrResourceAccess resourceAccess = MaterialIrResourceAccess::Read;
        MaterialIrBufferKind bufferKind = MaterialIrBufferKind::None;
        MaterialIrSamplerKind samplerKind = MaterialIrSamplerKind::Filtering;
        bool textureArrayed = false;
        bool textureMultisampled = false;
        crypto::Digest256 aggregate;

        [[nodiscard]] friend bool operator==(const MaterialIrType& left, const MaterialIrType& right) noexcept;
    };

    struct MaterialIrAggregateFieldDescription
    {
        u64 name = 0;
        MaterialIrType type;
    };

    struct MaterialIrAggregateDescription
    {
        u64 name = 0;
        containers::ArraySpan<const MaterialIrAggregateFieldDescription> fields;
    };

    struct MaterialIrAggregateView
    {
        u64 name = 0;
        crypto::Digest256 fingerprint;
        containers::ArraySpan<const MaterialIrAggregateFieldDescription> fields;
    };

    /// Frozen, compiler-owned definitions for named aggregate types. Values keep
    /// only the full aggregate fingerprint; the registry supplies the field tree
    /// required to validate and later generate source code.
    class MaterialIrTypeRegistry final
    {
    public:
        MaterialIrTypeRegistry() noexcept;
        [[nodiscard]] bool RegisterAggregate(const MaterialIrAggregateDescription& description, MaterialIrType& type) noexcept;
        [[nodiscard]] bool Freeze() noexcept;
        [[nodiscard]] bool IsFrozen() const noexcept;
        [[nodiscard]] bool FindAggregate(const crypto::Digest256& fingerprint, MaterialIrAggregateView& view) const noexcept;
        [[nodiscard]] const crypto::Digest256& Fingerprint() const noexcept;

    private:
        struct Record
        {
            u64 name = 0;
            crypto::Digest256 fingerprint;
            u32 firstField = 0;
            u32 fieldCount = 0;
        };
        containers::DynamicArray<Record> m_records;
        containers::DynamicArray<MaterialIrAggregateFieldDescription> m_fields;
        crypto::Digest256 m_fingerprint;
        bool m_frozen = false;
    };

    struct MaterialIrTypeField
    {
        u64 name = 0;
        MaterialIrTypeId type = InvalidMaterialIrType;
    };

    /// One canonical type in a finalized module. Aggregate records address an
    /// ordered range in MaterialIrModule::GetTypeFields().
    struct MaterialIrTypeRecord
    {
        MaterialIrType type;
        crypto::Digest256 fingerprint;
        u64 name = 0;
        u32 firstField = 0;
        u32 fieldCount = 0;
    };

    /// Canonical little-endian scalar bytes used by source graphs and the IR.
    /// Booleans are exactly one byte (0 or 1). Floating-point signed zero is
    /// preserved, while all NaN payloads are canonicalized so equivalent
    /// authoring values cannot create different build identities.
    struct MaterialIrScalarConstant
    {
        u8 bytes[8]{};
        u8 size = 0;
    };

    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialBool(bool value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialI32(i32 value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialU32(u32 value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialI64(i64 value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialU64(u64 value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialF32(float value) noexcept;
    [[nodiscard]] MaterialIrScalarConstant EncodeMaterialF64(double value) noexcept;

    enum class MaterialIrValueKind : u8
    {
        Constant,
        DynamicParameter,
        StaticParameter,
        Instruction,
        DomainInput
    };

    enum class MaterialIrOpcode : u8
    {
        None,
        Add,
        Subtract,
        Multiply,
        Divide,
        Compare,
        Select,
        Cast,
        Construct,
        Extract,
        TextureSample
    };

    enum class MaterialIrComparePredicate : u8
    {
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };

    struct MaterialIrValueBuildDescription
    {
        MaterialIrValueKind kind = MaterialIrValueKind::Instruction;
        MaterialIrOpcode opcode = MaterialIrOpcode::None;
        MaterialIrType type;
        shaders::StageMask legalStages = 0;
        u64 semantic = 0;
        u64 sourceNode = 0;
        u32 sourcePin = 0;
        containers::ArraySpan<const MaterialIrValueId> operands;
        containers::ArraySpan<const u8> data;
    };

    struct MaterialIrOutputBuildDescription
    {
        u64 name = 0;
        MaterialIrValueId value = InvalidMaterialIrValue;
        shaders::StageMask stages = 0;
    };

    struct MaterialIrValue
    {
        MaterialIrValueKind kind = MaterialIrValueKind::Instruction;
        MaterialIrOpcode opcode = MaterialIrOpcode::None;
        MaterialIrType type;
        MaterialIrTypeId typeId = InvalidMaterialIrType;
        shaders::StageMask legalStages = 0;
        shaders::StageMask requiredStages = 0;
        u64 semantic = 0;
        u64 sourceNode = 0;
        u32 sourcePin = 0;
        u32 firstOperand = 0;
        u32 operandCount = 0;
        u32 dataOffset = 0;
        u32 dataSize = 0;
        crypto::Digest256 structuralFingerprint;
    };

    struct MaterialIrOutput
    {
        u64 name = 0;
        MaterialIrValueId value = InvalidMaterialIrValue;
        shaders::StageMask stages = 0;
    };

    struct MaterialIrAttribution
    {
        MaterialIrValueId value = InvalidMaterialIrValue;
        u64 sourceNode = 0;
        u32 sourcePin = 0;
    };

    struct MaterialIrDiagnostic
    {
        MaterialIrResult code = MaterialIrResult::Success;
        u64 sourceNode = 0;
        u32 sourcePin = 0;
        MaterialIrValueId value = InvalidMaterialIrValue;
    };

    struct MaterialIrLimits
    {
        u32 maximumValues = 65536;
        u32 maximumOperands = 262144;
        u32 maximumDataBytes = 16u * 1024u * 1024u;
        u32 maximumOutputs = 256;
        u32 maximumTypes = 16384;
        u32 maximumTypeFields = 262144;
    };

    class MaterialIrModule final
    {
    public:
        MaterialIrModule() noexcept;
        void Reset() noexcept;

        [[nodiscard]] const crypto::Digest256& GetDomainFingerprint() const noexcept;
        [[nodiscard]] const crypto::Digest256& GetContentFingerprint() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrTypeRecord> GetTypes() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrTypeField> GetTypeFields() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrValue> GetValues() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrValueId> GetOperands() const noexcept;
        [[nodiscard]] containers::ArraySpan<const u8> GetData() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrOutput> GetOutputs() const noexcept;
        [[nodiscard]] containers::ArraySpan<const MaterialIrAttribution> GetAttributions() const noexcept;

    private:
        crypto::Digest256 m_domainFingerprint;
        crypto::Digest256 m_contentFingerprint;
        containers::DynamicArray<MaterialIrTypeRecord> m_types;
        containers::DynamicArray<MaterialIrTypeField> m_typeFields;
        containers::DynamicArray<MaterialIrValue> m_values;
        containers::DynamicArray<MaterialIrValueId> m_operands;
        containers::DynamicArray<u8> m_data;
        containers::DynamicArray<MaterialIrOutput> m_outputs;
        containers::DynamicArray<MaterialIrAttribution> m_attributions;

        friend class MaterialIrBuilder;
    };

    class MaterialIrBuilder final
    {
    public:
        MaterialIrBuilder() noexcept;
        void Reset(const crypto::Digest256& domainFingerprint, const MaterialIrTypeRegistry* types = nullptr) noexcept;

        [[nodiscard]] MaterialIrResult AddValue(const MaterialIrValueBuildDescription& description, MaterialIrValueId& value) noexcept;
        [[nodiscard]] MaterialIrResult AddOutput(const MaterialIrOutputBuildDescription& description) noexcept;
        [[nodiscard]] MaterialIrResult Finalize(MaterialIrModule& module, containers::DynamicArray<MaterialIrDiagnostic>& diagnostics,
                                                const MaterialIrLimits& limits = {},
                                                containers::DynamicArray<MaterialIrValueId>* sourceValueRemap = nullptr) noexcept;

    private:
        crypto::Digest256 m_domainFingerprint;
        const MaterialIrTypeRegistry* m_types = nullptr;
        containers::DynamicArray<MaterialIrValue> m_values;
        containers::DynamicArray<MaterialIrValueId> m_operands;
        containers::DynamicArray<u8> m_data;
        containers::DynamicArray<MaterialIrOutput> m_outputs;
    };

    [[nodiscard]] const char* ToString(MaterialIrResult result) noexcept;
} // namespace vanguard::material_tools
