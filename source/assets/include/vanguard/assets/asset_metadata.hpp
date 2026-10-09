#pragma once

#include <vanguard/assets/assets.hpp>

namespace vanguard::assets
{
    struct AssetId
    {
        u8 bytes[16]{};
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] u32 CalcHash() const noexcept
        {
            u32 hash = 2166136261u;
            for (const u8 byte : bytes)
                hash = (hash ^ byte) * 16777619u;
            return hash;
        }
        [[nodiscard]] friend bool operator==(const AssetId&, const AssetId&) noexcept = default;
    };

    // Generates a UUID-v4-shaped opaque identity from the platform secure
    // random source. Catalog collision policy remains with the project owner.
    [[nodiscard]] bool GenerateAssetId(AssetId& id) noexcept;

    struct AssetOutput
    {
        containers::String key;
        resources::ResourceTypeId type = resources::InvalidResourceTypeId;
    };

    struct AssetMetadata
    {
        AssetMetadata() noexcept : settings(memory::pools::Assets::GetInstance()), outputs(memory::pools::Assets::GetInstance()) {}
        AssetId id;
        SourceImporterId importer = InvalidSourceImporterId;
        u32 importerVersion = 0;
        u32 settingsVersion = 0;
        containers::DynamicArray<u8> settings;
        containers::DynamicArray<AssetOutput> outputs;
    };

    enum class MetadataResult : u8
    {
        Success,
        InvalidData,
        UnsupportedVersion,
        LimitExceeded,
        DuplicateOutput,
        IdentityCollision
    };

    inline constexpr u32 MaximumAssetMetadataBytes = 1024u * 1024u;
    inline constexpr u32 MaximumAssetSettingsBytes = 256u * 1024u;
    inline constexpr u32 MaximumAssetOutputs = 4096;

    // Callers must check catalog-wide identity collisions before publication.
    [[nodiscard]] bool IsValidAssetOutputKey(containers::StringView key) noexcept;
    // Non-allocating canonical resource-path derivation for hot admission/query
    // paths. The string form below remains available to callers that need text.
    [[nodiscard]] resources::ResourcePath GetAssetOutputResourcePath(const AssetId& id, containers::StringView key) noexcept;
    [[nodiscard]] bool GetAssetOutputPath(const AssetId& id, containers::StringView key, containers::String& path) noexcept;
    [[nodiscard]] resources::ResourceReference GetAssetOutputReference(const AssetId& id, const AssetOutput& output) noexcept;
    [[nodiscard]] MetadataResult ValidateMetadata(const AssetMetadata& metadata) noexcept;
    // Pure codecs: neither assigns identities nor reads/writes source files.
    // Destination remains unchanged on failure. Only schema 1 is accepted.
    [[nodiscard]] MetadataResult ParseMetadata(containers::StringView text, AssetMetadata& metadata) noexcept;
    [[nodiscard]] MetadataResult EncodeMetadata(const AssetMetadata& metadata, containers::String& text) noexcept;
} // namespace vanguard::assets
