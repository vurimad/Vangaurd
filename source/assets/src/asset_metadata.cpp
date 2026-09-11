#include <vanguard/assets/asset_metadata.hpp>

namespace vanguard::assets
{
    namespace
    {
        constexpr char HexDigits[] = "0123456789abcdef";

        bool ValidKey(const containers::StringView key) noexcept
        {
            if (key.Empty() || key.Length() > 64)
                return false;
            for (const char c : key)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
                    return false;
            return true;
        }

        bool ReadHex(const containers::StringView text, u64& value) noexcept
        {
            if (text.Empty() || text.Length() > 16)
                return false;
            value = 0;
            for (const char c : text)
            {
                const u32 digit = c >= '0' && c <= '9' ? static_cast<u32>(c - '0') : c >= 'a' && c <= 'f' ? static_cast<u32>(c - 'a' + 10) : 16u;
                if (digit == 16)
                    return false;
                value = (value << 4u) | digit;
            }
            return true;
        }

        void WriteHex(char*& out, const u64 value, const u32 digits) noexcept
        {
            for (u32 i = digits; i > 0; --i)
                *out++ = HexDigits[(value >> ((i - 1u) * 4u)) & 15u];
        }
    }

    bool AssetId::IsValid() const noexcept
    {
        for (const u8 byte : bytes)
            if (byte != 0)
                return true;
        return false;
    }

    bool GetAssetOutputPath(const AssetId& id, const containers::StringView key, containers::String& path) noexcept
    {
        if (!id.IsValid() || !ValidKey(key))
            return false;
        containers::String result;
        if (!result.Resize(7u + 32u + 1u + key.Length()))
            return false;
        char* out = result.AsChar();
        for (const char c : containers::StringView("assets/"))
            *out++ = c;
        for (const u8 byte : id.bytes)
            WriteHex(out, byte, 2);
        *out++ = '/';
        for (const char c : key)
            *out++ = c;
        path = static_cast<containers::String&&>(result);
        return true;
    }

    resources::ResourceReference GetAssetOutputReference(const AssetId& id, const AssetOutput& output) noexcept
    {
        containers::String path;
        if (output.type == resources::InvalidResourceTypeId || !GetAssetOutputPath(id, output.key, path))
            return {};
        return resources::ResourceReference(resources::ResourcePath::FromString(path), output.type);
    }

    MetadataResult ValidateMetadata(const AssetMetadata& metadata) noexcept
    {
        if (!metadata.id.IsValid() || metadata.importer == InvalidCompilerId || metadata.importerVersion == 0 || metadata.settingsVersion == 0)
            return MetadataResult::InvalidData;
        if (metadata.settings.Size() > MaximumAssetSettingsBytes || metadata.outputs.Size() > MaximumAssetOutputs)
            return MetadataResult::LimitExceeded;
        containers::HashMap<resources::ResourceId, u32> identities(memory::pools::Assets::GetInstance());
        for (u32 i = 0; i < metadata.outputs.Size(); ++i)
        {
            const AssetOutput& output = metadata.outputs[i];
            const resources::ResourceReference reference = GetAssetOutputReference(metadata.id, output);
            if (!reference.IsValid())
                return MetadataResult::InvalidData;
            u32 previous = 0;
            if (identities.Find(reference.GetPath().Id(), previous))
            {
                if (output.key == metadata.outputs[previous].key)
                    return MetadataResult::DuplicateOutput;
                return MetadataResult::IdentityCollision;
            }
            if (!identities.Insert(reference.GetPath().Id(), i).IsSuccessful())
                return MetadataResult::LimitExceeded;
        }
        return MetadataResult::Success;
    }

    MetadataResult ParseMetadata(const containers::StringView text, AssetMetadata& metadata) noexcept
    {
        if (text.Length() > MaximumAssetMetadataBytes)
            return MetadataResult::LimitExceeded;
        AssetMetadata parsed;
        u32 position = 0;
        auto line = [&]() noexcept
        {
            const u32 start = position;
            while (position < text.Length() && text[position] != '\n')
                ++position;
            u32 end = position;
            if (position < text.Length())
                ++position;
            if (end > start && text[end - 1u] == '\r')
                --end;
            return text.Slice(start, end);
        };
        const containers::StringView header = line();
        if (header != "vmeta 1")
            return header.StartsWith("vmeta ") ? MetadataResult::UnsupportedVersion : MetadataResult::InvalidData;
        const containers::StringView identity = line();
        if (identity.Length() != 35 || !identity.StartsWith("id "))
            return MetadataResult::InvalidData;
        u64 value = 0;
        for (u32 i = 0; i < 16; ++i)
        {
            if (!ReadHex(identity.Slice(3u + i * 2u, 5u + i * 2u), value))
                return MetadataResult::InvalidData;
            parsed.id.bytes[i] = static_cast<u8>(value);
        }
        const containers::StringView importer = line();
        if (importer.Length() != 34 || !importer.StartsWith("importer ") || importer[25] != ' ' || !ReadHex(importer.Slice(9, 25), value))
            return MetadataResult::InvalidData;
        parsed.importer = value;
        if (!ReadHex(importer.Slice(26, 34), value))
            return MetadataResult::InvalidData;
        parsed.importerVersion = static_cast<u32>(value);
        const containers::StringView settings = line();
        if (settings.Length() < 18 || !settings.StartsWith("settings ") || settings[17] != ' ' || !ReadHex(settings.Slice(9, 17), value))
            return MetadataResult::InvalidData;
        parsed.settingsVersion = static_cast<u32>(value);
        const u32 byteCount = (settings.Length() - 18u) / 2u;
        if ((settings.Length() - 18u) % 2u != 0)
            return MetadataResult::InvalidData;
        if (byteCount > MaximumAssetSettingsBytes)
            return MetadataResult::LimitExceeded;
        parsed.settings.Resize(byteCount);
        if (parsed.settings.Size() != byteCount)
            return MetadataResult::LimitExceeded;
        for (u32 i = 0; i < byteCount; ++i)
        {
            if (!ReadHex(settings.Slice(18u + i * 2u, 20u + i * 2u), value))
                return MetadataResult::InvalidData;
            parsed.settings[i] = static_cast<u8>(value);
        }
        while (position < text.Length())
        {
            const containers::StringView output = line();
            if (parsed.outputs.Size() == MaximumAssetOutputs)
                return MetadataResult::LimitExceeded;
            if (output.Length() < 17 || !output.StartsWith("output ") || output[output.Length() - 9u] != ' ' || !ReadHex(output.Slice(output.Length() - 8u, output.Length()), value))
                return MetadataResult::InvalidData;
            AssetOutput entry;
            const auto key = output.Slice(7, output.Length() - 9u);
            entry.key = containers::String(key.Data(), key.Length());
            if (entry.key.Length() != output.Length() - 16u)
                return MetadataResult::LimitExceeded;
            entry.type = static_cast<resources::ResourceTypeId>(value);
            const u32 previousCount = parsed.outputs.Size();
            parsed.outputs.PushBack(static_cast<AssetOutput&&>(entry));
            if (parsed.outputs.Size() != previousCount + 1u)
                return MetadataResult::LimitExceeded;
        }
        const MetadataResult result = ValidateMetadata(parsed);
        if (result == MetadataResult::Success)
            metadata = static_cast<AssetMetadata&&>(parsed);
        return result;
    }

    MetadataResult EncodeMetadata(const AssetMetadata& metadata, containers::String& text) noexcept
    {
        const MetadataResult result = ValidateMetadata(metadata);
        if (result != MetadataResult::Success)
            return result;
        u32 size = 8u + 36u + 35u + 19u + metadata.settings.Size() * 2u;
        for (const AssetOutput& output : metadata.outputs)
            size += 17u + output.key.Length();
        if (size > MaximumAssetMetadataBytes)
            return MetadataResult::LimitExceeded;
        containers::String encoded;
        if (!encoded.Resize(size))
            return MetadataResult::LimitExceeded;
        char* out = encoded.AsChar();
        auto write = [&](const containers::StringView part) noexcept { for (const char c : part) *out++ = c; };
        write("vmeta 1\nid ");
        for (const u8 byte : metadata.id.bytes)
            WriteHex(out, byte, 2);
        write("\nimporter ");
        WriteHex(out, metadata.importer, 16);
        *out++ = ' ';
        WriteHex(out, metadata.importerVersion, 8);
        write("\nsettings ");
        WriteHex(out, metadata.settingsVersion, 8);
        *out++ = ' ';
        for (const u8 byte : metadata.settings)
            WriteHex(out, byte, 2);
        *out++ = '\n';
        for (const AssetOutput& output : metadata.outputs)
        {
            write("output ");
            write(output.key);
            *out++ = ' ';
            WriteHex(out, output.type, 8);
            *out++ = '\n';
        }
        text = static_cast<containers::String&&>(encoded);
        return MetadataResult::Success;
    }
} // namespace vanguard::assets
