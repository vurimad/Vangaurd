#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/schemas/schemas.hpp>

#include <vanguard/containers/containers.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{
    using namespace vanguard;

    int g_failures = 0;

    void Check(const bool condition, const char* const message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "[schemasTests] FAILED: %s\n", message);
            ++g_failures;
        }
    }

    enum class BlendMode : u32
    {
        Opaque,
        Masked,
        Transparent
    };

    struct UvTransform
    {
        f32 scaleU = 1.0f;
        f32 scaleV = 1.0f;
    };

    struct MaterialData
    {
        MaterialData() noexcept
            : layerWeights(memory::pools::Serialization::GetInstance()), detailTextures(memory::pools::Serialization::GetInstance()),
              uvVariants(memory::pools::Serialization::GetInstance())
        {
        }

        resources::ResourceReference baseColor;
        resources::ResourceReference normal;
        resources::ResourceReference shader;
        UvTransform uv;
        containers::String editorLabel;
        containers::DynamicArray<f32> layerWeights;
        containers::DynamicArray<resources::ResourceReference> detailTextures;
        containers::DynamicArray<UvTransform> uvVariants;
        f32 roughness = 0.5f;
        f32 metallic = 0.0f;
        BlendMode blend = BlendMode::Opaque;
        u32 runtimeCache = 0;
    };

    constexpr reflection::SchemaTypeId UvType = reflection::HashSchemaName("vanguard.test.uv_transform");
    constexpr reflection::SchemaTypeId MaterialType = reflection::HashSchemaName("vanguard.test.material");
    constexpr reflection::SchemaTypeId BlendType = reflection::HashSchemaName("vanguard.test.blend_mode");

    const reflection::SchemaField UvFields[] = {
        reflection::MakeField("scale_v", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(UvTransform, scaleV),
                              sizeof(UvTransform::scaleV), alignof(decltype(UvTransform::scaleV))),
        reflection::MakeField("scale_u", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(UvTransform, scaleU),
                              sizeof(UvTransform::scaleU), alignof(decltype(UvTransform::scaleU)))};

    const reflection::Schema UvSchema{UvType,   "vanguard.test.uv_transform",         sizeof(UvTransform), alignof(UvTransform), 1, 1,
                                      UvFields, static_cast<u32>(std::size(UvFields))};

    const reflection::SchemaField MaterialFields[] = {
        reflection::MakeField("roughness", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(MaterialData, roughness),
                              sizeof(MaterialData::roughness), alignof(decltype(MaterialData::roughness)), 1, 0,
                              reflection::FieldFlags::Required),
        reflection::MakeField("normal", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              offsetof(MaterialData, normal), sizeof(MaterialData::normal), alignof(decltype(MaterialData::normal)), 1, 0,
                              reflection::FieldFlags::OptionalDependency),
        reflection::MakeField("runtime_cache", reflection::builtin::U32, reflection::ValueKind::U32, offsetof(MaterialData, runtimeCache),
                              sizeof(MaterialData::runtimeCache), alignof(decltype(MaterialData::runtimeCache)), 1, 0,
                              reflection::FieldFlags::Transient),
        reflection::MakeField("base_color", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              offsetof(MaterialData, baseColor), sizeof(MaterialData::baseColor),
                              alignof(decltype(MaterialData::baseColor)), 1, 0, reflection::FieldFlags::Required),
        reflection::MakeField("editor_label", reflection::builtin::String, reflection::ValueKind::String,
                              offsetof(MaterialData, editorLabel), sizeof(MaterialData::editorLabel),
                              alignof(decltype(MaterialData::editorLabel)), 2, 0, reflection::FieldFlags::EditorOnly),
        reflection::MakeField("uv", UvType, reflection::ValueKind::Structure, offsetof(MaterialData, uv), sizeof(MaterialData::uv),
                              alignof(decltype(MaterialData::uv))),
        reflection::MakeField("metallic", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(MaterialData, metallic),
                              sizeof(MaterialData::metallic), alignof(decltype(MaterialData::metallic)), 2),
        reflection::MakeField("shader", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              offsetof(MaterialData, shader), sizeof(MaterialData::shader), alignof(decltype(MaterialData::shader)), 1, 0,
                              reflection::FieldFlags::SoftDependency),
        reflection::MakeField("blend", BlendType, reflection::ValueKind::Enumeration, offsetof(MaterialData, blend),
                              sizeof(MaterialData::blend), alignof(decltype(MaterialData::blend))),
        schemas::MakeDynamicArrayField<f32>("layer_weights", reflection::builtin::F32, reflection::ValueKind::F32,
                                            offsetof(MaterialData, layerWeights)),
        schemas::MakeDynamicArrayField<resources::ResourceReference>(
            "detail_textures", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
            offsetof(MaterialData, detailTextures), 1, 0, reflection::FieldFlags::OptionalDependency),
        schemas::MakeDynamicArrayField<UvTransform>("uv_variants", UvType, reflection::ValueKind::Structure,
                                                    offsetof(MaterialData, uvVariants))};

    const reflection::Schema MaterialSchema{MaterialType,
                                            "vanguard.test.material",
                                            sizeof(MaterialData),
                                            alignof(MaterialData),
                                            2,
                                            1,
                                            MaterialFields,
                                            static_cast<u32>(std::size(MaterialFields))};

    const reflection::SchemaField LegacyMaterialFields[] = {
        reflection::MakeField("retired_value", reflection::builtin::U32, reflection::ValueKind::U32, offsetof(MaterialData, runtimeCache),
                              sizeof(MaterialData::runtimeCache), alignof(decltype(MaterialData::runtimeCache))),
        reflection::MakeField("base_color", reflection::builtin::ResourceReference, reflection::ValueKind::ResourceReference,
                              offsetof(MaterialData, baseColor), sizeof(MaterialData::baseColor),
                              alignof(decltype(MaterialData::baseColor)), 1, 0, reflection::FieldFlags::Required),
        reflection::MakeField("roughness", reflection::builtin::F32, reflection::ValueKind::F32, offsetof(MaterialData, roughness),
                              sizeof(MaterialData::roughness), alignof(decltype(MaterialData::roughness)), 1, 0,
                              reflection::FieldFlags::Required)};

    const reflection::Schema LegacyMaterialSchema{MaterialType,
                                                  "vanguard.test.material",
                                                  sizeof(MaterialData),
                                                  alignof(MaterialData),
                                                  1,
                                                  1,
                                                  LegacyMaterialFields,
                                                  static_cast<u32>(std::size(LegacyMaterialFields))};

    const reflection::SchemaField MissingRequiredFields[] = {LegacyMaterialFields[0]};

    const reflection::Schema MissingRequiredWriterSchema{
        MaterialType, "vanguard.test.material", sizeof(MaterialData), alignof(MaterialData), 1, 1, MissingRequiredFields, 1};

    using ByteArray = containers::DynamicArray<u8>;

    schemas::Result WriteToMemory(const reflection::Schema& schema, const MaterialData& material, ByteArray& output,
                                  const bool editorData = false)
    {
        output.Clear();
        filesystem::MemoryFileWriter file(output);
        vanguard::serialization::BinaryWriter writer(file);
        schemas::WriteOptions options;
        options.includeEditorFields = editorData;
        const schemas::Result result = schemas::WriteObject(writer, schema, &material, options);
        if (result == schemas::Result::Success)
        {
            static_cast<void>(writer.Flush());
        }
        return result;
    }

    schemas::Result ReadFromMemory(const reflection::Schema& schema, ByteArray& bytes, MaterialData& material,
                                   schemas::ReadInfo* const info = nullptr, const schemas::ReadLimits& limits = {})
    {
        filesystem::MemoryFileReader file(bytes, 0);
        vanguard::serialization::BinaryReader reader(file);
        return schemas::ReadObject(reader, schema, &material, limits, info);
    }

    void StoreU64(ByteArray& bytes, const u32 offset, const u64 value)
    {
        for (u32 index = 0; index < 8; ++index)
        {
            bytes[offset + index] = static_cast<u8>(value >> (index * 8u));
        }
    }

    u32 LoadU32(const ByteArray& bytes, const u32 offset)
    {
        u32 value = 0;
        for (u32 index = 0; index < 4; ++index)
        {
            value |= static_cast<u32>(bytes[offset + index]) << (index * 8u);
        }
        return value;
    }

    u64 LoadU64(const ByteArray& bytes, const u32 offset)
    {
        u64 value = 0;
        for (u32 index = 0; index < 8; ++index)
        {
            value |= static_cast<u64>(bytes[offset + index]) << (index * 8u);
        }
        return value;
    }

    struct Dependencies
    {
        std::array<resources::ResourceReference, 8> references;
        std::array<schemas::DependencyKind, 8> kinds;
        u32 count = 0;
    };

    bool CollectDependency(const resources::ResourceReference reference, const schemas::DependencyKind kind, void* const userData) noexcept
    {
        auto& dependencies = *static_cast<Dependencies*>(userData);
        if (dependencies.count >= dependencies.references.size())
        {
            return false;
        }
        dependencies.references[dependencies.count] = reference;
        dependencies.kinds[dependencies.count] = kind;
        ++dependencies.count;
        return true;
    }
} // namespace

int main()
{
    using namespace vanguard;

    Check(memory::Initialize(), "memory initialization");
    Check(diagnostics::Initialize(diagnostics::Mode::Synchronous, "schemasTests"), "diagnostics initialization");
    Check(containers::Initialize(), "containers initialization");
    Check(reflection::Initialize(), "reflection initialization");

    Check(reflection::HashSchemaName("vanguard.test.material") == MaterialType &&
              reflection::HashSchemaName("Vanguard.Test.Material") == reflection::InvalidSchemaTypeId &&
              reflection::HashFieldName("base_color") != reflection::InvalidSchemaFieldId,
          "stable schema and field identity rules");
    Check(reflection::RegisterSchema(UvSchema), "nested schema registration");
    Check(reflection::RegisterSchema(MaterialSchema), "material schema registration");
    Check(!reflection::RegisterSchema(MaterialSchema), "duplicate schema rejection");
    Check(reflection::FindSchema(MaterialType) == &MaterialSchema && reflection::FindSchema("vanguard.test.material") == &MaterialSchema &&
              reflection::SchemaCount() == 2,
          "schema lookup and inspection");
    {
        reflection::SchemaField invalidField = MaterialFields[0];
        invalidField.offset = 1;
        const reflection::Schema invalidSchema{reflection::HashSchemaName("vanguard.test.invalid"),
                                               "vanguard.test.invalid",
                                               sizeof(MaterialData),
                                               alignof(MaterialData),
                                               1,
                                               1,
                                               &invalidField,
                                               1};
        Check(!reflection::RegisterSchema(invalidSchema), "misaligned reflected field rejection");
    }

    const resources::ResourceTypeId textureType = resources::HashTypeName("vanguard.texture");
    const resources::ResourceTypeId shaderType = resources::HashTypeName("vanguard.shader");
    MaterialData source;
    source.baseColor = resources::ResourceReference(resources::ResourcePath::FromString("textures/stone_base.vtex"), textureType);
    source.normal = resources::ResourceReference(resources::ResourcePath::FromString("textures/stone_normal.vtex"), textureType);
    source.shader = resources::ResourceReference(resources::ResourcePath::FromString("shaders/pbr.vshader"), shaderType);
    source.uv = {2.0f, 3.0f};
    source.editorLabel = "Stone cliff";
    source.roughness = 0.73f;
    source.metallic = 0.18f;
    source.blend = BlendMode::Masked;
    source.runtimeCache = 0xdeadbeefu;
    source.layerWeights.PushBack(0.25f);
    source.layerWeights.PushBack(0.75f);
    source.detailTextures.PushBack(
        resources::ResourceReference(resources::ResourcePath::FromString("textures/stone_detail_a.vtex"), textureType));
    source.detailTextures.PushBack(
        resources::ResourceReference(resources::ResourcePath::FromString("textures/stone_detail_b.vtex"), textureType));
    source.uvVariants.PushBack({4.0f, 5.0f});
    source.uvVariants.PushBack({6.0f, 7.0f});

    ByteArray first(memory::pools::Serialization::GetInstance());
    ByteArray second(memory::pools::Serialization::GetInstance());
    Check(WriteToMemory(MaterialSchema, source, first) == schemas::Result::Success &&
              WriteToMemory(MaterialSchema, source, second) == schemas::Result::Success && first == second,
          "deterministic stable-field-order serialization");
    {
        ByteArray prefixed(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter file(prefixed);
        vanguard::serialization::BinaryWriter writer(file);
        constexpr u8 prefix[3]{1, 2, 3};
        Check(writer.WriteBytes(prefix, sizeof(prefix)) &&
                  schemas::WriteObject(writer, MaterialSchema, &source) == schemas::Result::Success &&
                  prefixed.Size() == first.Size() + sizeof(prefix) &&
                  std::memcmp(prefixed.TypedData() + sizeof(prefix), first.TypedData(), first.Size()) == 0,
              "VOBJ bytes are independent of containing-file offset");
    }

    MaterialData loaded;
    loaded.editorLabel = "default label";
    loaded.runtimeCache = 77;
    schemas::ReadInfo info;
    Check(ReadFromMemory(MaterialSchema, first, loaded, &info) == schemas::Result::Success, "current material object deserialization");
    Check(loaded.baseColor == source.baseColor && loaded.normal == source.normal && loaded.shader == source.shader &&
              loaded.uv.scaleU == source.uv.scaleU && loaded.uv.scaleV == source.uv.scaleV && loaded.roughness == source.roughness &&
              loaded.metallic == source.metallic && loaded.blend == source.blend && loaded.layerWeights.Size() == 2 &&
              loaded.layerWeights[0] == 0.25f && loaded.layerWeights[1] == 0.75f && loaded.detailTextures.Size() == 2 &&
              loaded.detailTextures[0] == source.detailTextures[0] && loaded.detailTextures[1] == source.detailTextures[1] &&
              loaded.uvVariants.Size() == 2 && loaded.uvVariants[1].scaleU == 6.0f && loaded.uvVariants[1].scaleV == 7.0f,
          "primitive, enum, nested, array, and resource fields round trip");
    Check(loaded.editorLabel == "default label" && loaded.runtimeCache == 77, "excluded editor and transient fields retain defaults");
    Check(info.sourceSchemaVersion == 2 && info.unknownFieldsSkipped == 0, "reader reports source schema metadata");

    ByteArray editorBytes(memory::pools::Serialization::GetInstance());
    MaterialData editorLoaded;
    Check(WriteToMemory(MaterialSchema, source, editorBytes, true) == schemas::Result::Success &&
              ReadFromMemory(MaterialSchema, editorBytes, editorLoaded) == schemas::Result::Success &&
              editorLoaded.editorLabel == source.editorLabel,
          "editor fields use the same schema with an explicit flag");

    ByteArray legacy(memory::pools::Serialization::GetInstance());
    Check(WriteToMemory(LegacyMaterialSchema, source, legacy) == schemas::Result::Success, "legacy version-one object creation");
    MaterialData upgraded;
    upgraded.metallic = 0.42f;
    upgraded.editorLabel = "new default";
    upgraded.runtimeCache = 9;
    schemas::ReadInfo legacyInfo;
    Check(ReadFromMemory(MaterialSchema, legacy, upgraded, &legacyInfo) == schemas::Result::Success, "older schema version is accepted");
    Check(upgraded.baseColor == source.baseColor && upgraded.roughness == source.roughness && upgraded.metallic == 0.42f &&
              upgraded.editorLabel == "new default" && upgraded.runtimeCache == 9 && legacyInfo.sourceSchemaVersion == 1 &&
              legacyInfo.unknownFieldsSkipped == 1,
          "missing fields keep defaults and removed fields are skipped");

    Dependencies dependencies;
    Check(schemas::VisitDependencies(MaterialSchema, &source, &CollectDependency, &dependencies) == schemas::Result::Success &&
              dependencies.count == 5,
          "reflected resource dependencies are gathered");
    bool hasRequired = false;
    bool hasOptional = false;
    bool hasSoft = false;
    for (u32 index = 0; index < dependencies.count; ++index)
    {
        hasRequired |= dependencies.kinds[index] == schemas::DependencyKind::Required;
        hasOptional |= dependencies.kinds[index] == schemas::DependencyKind::Optional;
        hasSoft |= dependencies.kinds[index] == schemas::DependencyKind::Soft;
    }
    Check(hasRequired && hasOptional && hasSoft, "dependency strength follows reflected field flags");

    {
        MaterialData untyped;
        untyped.baseColor = resources::ResourceReference(resources::ResourcePath::FromString("textures/untyped.vtex"));
        ByteArray rejected(memory::pools::Serialization::GetInstance());
        Check(WriteToMemory(MaterialSchema, untyped, rejected) == schemas::Result::FieldMismatch &&
                  schemas::VisitDependencies(MaterialSchema, &untyped, &CollectDependency, &dependencies) == schemas::Result::FieldMismatch,
              "untyped nonempty resource reference rejection");
    }

    {
        ByteArray corrupt(first);
        corrupt[20] ^= 1u;
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, corrupt, output) == schemas::Result::IntegrityFailure, "object header corruption rejection");
    }
    {
        ByteArray malformed(first);
        StoreU64(malformed, schemas::ObjectHeader::WireSize + 16, 0);
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, malformed, output) == schemas::Result::InvalidLayout, "field range corruption rejection");
    }
    {
        ByteArray mismatch(first);
        StoreU64(mismatch, schemas::ObjectHeader::WireSize + 8, reflection::builtin::U64);
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, mismatch, output) == schemas::Result::FieldMismatch,
              "persisted field type mismatch rejection");
    }
    {
        ByteArray padding(first);
        const u32 fieldCount = LoadU32(padding, 36);
        bool changed = false;
        for (u32 index = 0; index + 1 < fieldCount; ++index)
        {
            const u32 record = schemas::ObjectHeader::WireSize + index * schemas::FieldRecord::WireSize;
            const u32 nextRecord = record + schemas::FieldRecord::WireSize;
            const u64 end = LoadU64(padding, record + 16) + LoadU64(padding, record + 24);
            const u64 next = LoadU64(padding, nextRecord + 16);
            if (next > end)
            {
                padding[static_cast<u32>(end)] = 1;
                changed = true;
                break;
            }
        }
        MaterialData output;
        Check(changed && ReadFromMemory(MaterialSchema, padding, output) == schemas::Result::InvalidLayout,
              "nonzero alignment padding rejection");
    }
    {
        ByteArray missing(memory::pools::Serialization::GetInstance());
        Check(WriteToMemory(MissingRequiredWriterSchema, source, missing) == schemas::Result::Success, "missing-required fixture creation");
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, missing, output) == schemas::Result::MissingRequiredField, "required field absence rejection");
    }
    {
        schemas::ReadLimits limits;
        limits.maximumFields = 1;
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, first, output, nullptr, limits) == schemas::Result::LimitExceeded, "caller field-count limit");
    }
    {
        ByteArray limited(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter file(limited);
        vanguard::serialization::BinaryWriter writer(file);
        schemas::WriteOptions options;
        options.maximumArrayElements = 1;
        Check(schemas::WriteObject(writer, MaterialSchema, &source, options) == schemas::Result::LimitExceeded,
              "writer array-element limit");
    }
    {
        ByteArray limited(memory::pools::Serialization::GetInstance());
        filesystem::MemoryFileWriter file(limited);
        vanguard::serialization::BinaryWriter writer(file);
        schemas::WriteOptions options;
        options.includeEditorFields = true;
        options.maximumStringBytes = 4;
        Check(schemas::WriteObject(writer, MaterialSchema, &source, options) == schemas::Result::LimitExceeded, "writer string-byte limit");
    }
    {
        schemas::ReadLimits limits;
        limits.maximumArrayElements = 1;
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, first, output, nullptr, limits) == schemas::Result::LimitExceeded,
              "reader array-element limit");
    }
    {
        schemas::ReadLimits limits;
        limits.maximumNestingDepth = 0;
        MaterialData output;
        Check(ReadFromMemory(MaterialSchema, first, output, nullptr, limits) == schemas::Result::LimitExceeded,
              "reader nesting-depth limit");
    }

    Check(reflection::UnregisterSchema(MaterialType) && reflection::UnregisterSchema(UvType) && reflection::SchemaCount() == 0,
          "schema registry teardown");

    diagnostics::Shutdown();
    if (g_failures == 0)
    {
        std::puts("[schemasTests] Vanguard reflected object schema "
                  "checks passed");
    }
    return g_failures == 0 ? 0 : 1;
}
