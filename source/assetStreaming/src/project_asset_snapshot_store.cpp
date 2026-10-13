#include <vanguard/asset_streaming/project_asset_snapshot_store.hpp>

#include <algorithm>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace vanguard::asset_streaming
{
    namespace
    {
        constexpr u32 KnownSourceIssues = assets::SourceMissing | assets::MetadataMissing | assets::MetadataUnreadable |
                                          assets::MetadataInvalid | assets::AssetIdentityConflict | assets::OutputIdentityConflict |
                                          assets::SourceIdentityConflict;
        constexpr u32 KnownDiagnosticReasons = static_cast<u32>(ProjectAssetSourceDiagnosticReason::AmbiguousMove) |
                                               static_cast<u32>(ProjectAssetSourceDiagnosticReason::SourceContentUnavailable);
        constexpr u32 FileSizeOffset = 36u;
        constexpr u32 PayloadChecksumOffset = 52u;

        enum class SnapshotFilePresence : u8
        {
            Present,
            Missing,
            IoFailure
        };

        [[nodiscard]] SnapshotFilePresence QuerySnapshotFilePresence(const filesystem::AbsolutePath& path) noexcept
        {
#if defined(_WIN32)
            const DWORD attributes = ::GetFileAttributesW(path.ToUtf16String().AsChar());
            if (attributes != INVALID_FILE_ATTRIBUTES)
                return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ? SnapshotFilePresence::Present : SnapshotFilePresence::IoFailure;
            const DWORD error = ::GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? SnapshotFilePresence::Missing
                                                                                  : SnapshotFilePresence::IoFailure;
#else
            return filesystem::GetManager().FileExist(path) ? SnapshotFilePresence::Present : SnapshotFilePresence::Missing;
#endif
        }

        struct AssetSlice final
        {
            u32 outputOffset = 0;
            u32 outputCount = 0;
            u32 referenceOffset = 0;
            u32 referenceCount = 0;
        };

        class BufferWriter final
        {
        public:
            explicit BufferWriter(const u64 limit) noexcept : m_bytes(memory::pools::Assets::GetInstance()), m_limit(limit) {}

            [[nodiscard]] bool WriteU8(const u8 value) noexcept { return WriteBytes(&value, sizeof(value)); }
            [[nodiscard]] bool WriteU16(const u16 value) noexcept
            {
                const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u)};
                return WriteBytes(bytes, sizeof(bytes));
            }
            [[nodiscard]] bool WriteU32(const u32 value) noexcept
            {
                const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u),
                                    static_cast<u8>(value >> 24u)};
                return WriteBytes(bytes, sizeof(bytes));
            }
            [[nodiscard]] bool WriteU64(const u64 value) noexcept
            {
                const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u),
                                    static_cast<u8>(value >> 24u), static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u),
                                    static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
                return WriteBytes(bytes, sizeof(bytes));
            }
            [[nodiscard]] bool WriteBytes(const void* const data, const u32 size) noexcept
            {
                if ((size != 0u && data == nullptr) || static_cast<u64>(m_bytes.Size()) + size > m_limit ||
                    static_cast<u64>(m_bytes.Size()) + size > ~u32{0})
                    return false;
                const u32 before = m_bytes.Size();
                m_bytes.Resize(before + size);
                if (m_bytes.Size() != before + size)
                    return false;
                const auto* const source = static_cast<const u8*>(data);
                for (u32 index = 0; index < size; ++index)
                    m_bytes[before + index] = source[index];
                return true;
            }
            [[nodiscard]] bool WriteString(const containers::StringView value) noexcept
            {
                return WriteU32(value.Length()) && WriteBytes(value.Data(), value.Length());
            }
            [[nodiscard]] bool PatchBytes(const u32 offset, const void* const data, const u32 size) noexcept
            {
                if ((size != 0u && data == nullptr) || offset > m_bytes.Size() || size > m_bytes.Size() - offset)
                    return false;
                const auto* const source = static_cast<const u8*>(data);
                for (u32 index = 0; index < size; ++index)
                    m_bytes[offset + index] = source[index];
                return true;
            }
            [[nodiscard]] const containers::DynamicArray<u8>& Bytes() const noexcept { return m_bytes; }

        private:
            containers::DynamicArray<u8> m_bytes;
            u64 m_limit = 0;
        };

        class BufferReader final
        {
        public:
            BufferReader(const u8* const data, const u32 size) noexcept : m_data(data), m_size(size) {}

            [[nodiscard]] bool ReadU8(u8& value) noexcept { return ReadBytes(&value, sizeof(value)); }
            [[nodiscard]] bool ReadU16(u16& value) noexcept
            {
                u8 bytes[2];
                if (!ReadBytes(bytes, sizeof(bytes)))
                    return false;
                value = static_cast<u16>(bytes[0]) | (static_cast<u16>(bytes[1]) << 8u);
                return true;
            }
            [[nodiscard]] bool ReadU32(u32& value) noexcept
            {
                u8 bytes[4];
                if (!ReadBytes(bytes, sizeof(bytes)))
                    return false;
                value = static_cast<u32>(bytes[0]) | (static_cast<u32>(bytes[1]) << 8u) |
                        (static_cast<u32>(bytes[2]) << 16u) | (static_cast<u32>(bytes[3]) << 24u);
                return true;
            }
            [[nodiscard]] bool ReadU64(u64& value) noexcept
            {
                u8 bytes[8];
                if (!ReadBytes(bytes, sizeof(bytes)))
                    return false;
                value = static_cast<u64>(bytes[0]) | (static_cast<u64>(bytes[1]) << 8u) |
                        (static_cast<u64>(bytes[2]) << 16u) | (static_cast<u64>(bytes[3]) << 24u) |
                        (static_cast<u64>(bytes[4]) << 32u) | (static_cast<u64>(bytes[5]) << 40u) |
                        (static_cast<u64>(bytes[6]) << 48u) | (static_cast<u64>(bytes[7]) << 56u);
                return true;
            }
            [[nodiscard]] bool ReadBytes(void* const destination, const u32 size) noexcept
            {
                if ((size != 0u && destination == nullptr) || m_position > m_size || size > m_size - m_position)
                    return false;
                auto* const target = static_cast<u8*>(destination);
                for (u32 index = 0; index < size; ++index)
                    target[index] = m_data[m_position + index];
                m_position += size;
                return true;
            }
            [[nodiscard]] bool Skip(const u32 size) noexcept
            {
                if (m_position > m_size || size > m_size - m_position)
                    return false;
                m_position += size;
                return true;
            }
            [[nodiscard]] bool ReadString(containers::String& value) noexcept
            {
                u32 length = 0;
                if (!ReadU32(length) || m_position > m_size || length > m_size - m_position)
                    return false;
                containers::String parsed;
                if (!parsed.Resize(length))
                {
                    m_outOfMemory = true;
                    return false;
                }
                for (u32 index = 0; index < length; ++index)
                    parsed[index] = static_cast<char>(m_data[m_position + index]);
                m_position += length;
                value = static_cast<containers::String&&>(parsed);
                return true;
            }
            [[nodiscard]] u32 Position() const noexcept { return m_position; }
            [[nodiscard]] u32 Size() const noexcept { return m_size; }
            [[nodiscard]] bool OutOfMemory() const noexcept { return m_outOfMemory; }

        private:
            const u8* m_data = nullptr;
            u32 m_size = 0;
            u32 m_position = 0;
            bool m_outOfMemory = false;
        };

        [[nodiscard]] i32 CompareAssetId(const assets::AssetId& left, const assets::AssetId& right) noexcept
        {
            for (u32 index = 0; index < 16u; ++index)
            {
                if (left.bytes[index] < right.bytes[index])
                    return -1;
                if (left.bytes[index] > right.bytes[index])
                    return 1;
            }
            return 0;
        }

        [[nodiscard]] i32 CompareResource(const resources::ResourceReference left,
                                          const resources::ResourceReference right) noexcept
        {
            if (left.GetPath().Id() < right.GetPath().Id())
                return -1;
            if (left.GetPath().Id() > right.GetPath().Id())
                return 1;
            if (left.ExpectedType() < right.ExpectedType())
                return -1;
            if (left.ExpectedType() > right.ExpectedType())
                return 1;
            return 0;
        }

        [[nodiscard]] bool ReferenceDetailsLess(const assets::AssetReference& left,
                                                const assets::AssetReference& right) noexcept
        {
            if (left.location != right.location)
                return left.location < right.location;
            if (left.kind != right.kind)
                return left.kind < right.kind;
            return left.inPlace < right.inPlace;
        }

        [[nodiscard]] bool ReferenceToLess(const assets::AssetReference& left, const assets::AssetReference& right) noexcept
        {
            const i32 target = CompareResource(left.to, right.to);
            if (target != 0)
                return target < 0;
            const i32 source = CompareAssetId(left.from, right.from);
            return source != 0 ? source < 0 : ReferenceDetailsLess(left, right);
        }

        [[nodiscard]] bool OutputLess(const ProjectAssetOutputState& left, const ProjectAssetOutputState& right) noexcept
        {
            const containers::StringView leftKey(left.key);
            const containers::StringView rightKey(right.key);
            if (leftKey != rightKey)
                return leftKey < rightKey;
            return CompareResource(left.resource, right.resource) < 0;
        }

        [[nodiscard]] bool OutputKeyLess(const ProjectAssetOutputState& left, const ProjectAssetOutputState& right) noexcept
        {
            return containers::StringView(left.key) < containers::StringView(right.key);
        }

        [[nodiscard]] bool ReferenceLess(const assets::AssetReference& left, const assets::AssetReference& right) noexcept
        {
            const i32 target = CompareResource(left.to, right.to);
            return target != 0 ? target < 0 : ReferenceDetailsLess(left, right);
        }

        [[nodiscard]] bool DiagnosticLess(const ProjectAssetSourceDiagnostic& left,
                                          const ProjectAssetSourceDiagnostic& right) noexcept
        {
            if (left.sourceRoot != right.sourceRoot)
                return left.sourceRoot < right.sourceRoot;
            if (left.sourceIdentity != right.sourceIdentity)
                return left.sourceIdentity.Id() < right.sourceIdentity.Id();
            if (left.physicalPath != right.physicalPath)
                return left.physicalPath < right.physicalPath;
            const i32 asset = CompareAssetId(left.asset, right.asset);
            if (asset != 0)
                return asset < 0;
            if (left.sourceIssues != right.sourceIssues)
                return left.sourceIssues < right.sourceIssues;
            if (left.metadataResult != right.metadataResult)
                return left.metadataResult < right.metadataResult;
            if (left.importState != right.importState)
                return left.importState < right.importState;
            if (left.referenceScanState != right.referenceScanState)
                return left.referenceScanState < right.referenceScanState;
            if (left.reasons != right.reasons)
                return static_cast<u32>(left.reasons) < static_cast<u32>(right.reasons);
            return left.hasParsedAssetIdentity < right.hasParsedAssetIdentity;
        }

        [[nodiscard]] bool ValidSourceIssueBits(const u32 issues) noexcept { return (issues & ~KnownSourceIssues) == 0u; }
        [[nodiscard]] bool ValidDiagnosticReasonBits(const ProjectAssetSourceDiagnosticReason reasons) noexcept
        {
            return (static_cast<u32>(reasons) & ~KnownDiagnosticReasons) == 0u;
        }

        [[nodiscard]] bool ValidDirectoryPath(const filesystem::AbsolutePath& path) noexcept
        {
            return !path.Empty() && path.IsDirectoryPath() && filesystem::paths::IsAbsolutePath(path.AsStringView());
        }

        [[nodiscard]] bool ValidFilePath(const filesystem::AbsolutePath& path) noexcept
        {
            return !path.Empty() && path.IsFilePath() && filesystem::paths::IsAbsolutePath(path.AsStringView());
        }

        [[nodiscard]] bool ReadAbsolutePath(BufferReader& reader, const bool directory,
                                            filesystem::AbsolutePath& path) noexcept
        {
            containers::String text;
            if (!reader.ReadString(text))
                return false;
            const containers::StringView view(text);
            if (view.Empty() || !filesystem::AbsolutePath::IsValidPath(view) || !filesystem::paths::IsAbsolutePath(view) ||
                filesystem::paths::IsDirectoryPath(view) != directory)
                return false;
            path = directory ? filesystem::AbsolutePath::CreateDirPath(view) : filesystem::AbsolutePath::CreateFilePath(view);
            return directory ? ValidDirectoryPath(path) : ValidFilePath(path);
        }

        [[nodiscard]] bool ExpectedOutputPath(const assets::AssetId asset, const containers::StringView key,
                                              resources::ResourcePath& result) noexcept
        {
            if (!asset.IsValid() || key.Empty() || key.Length() > 64u)
                return false;
            for (const char character : key)
                if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-' ||
                      character == '_'))
                    return false;

            constexpr char hex[] = "0123456789abcdef";
            char path[7u + 32u + 1u + 64u];
            u32 position = 0u;
            constexpr char prefix[] = "assets/";
            for (u32 index = 0; index < sizeof(prefix) - 1u; ++index)
                path[position++] = prefix[index];
            for (const u8 byte : asset.bytes)
            {
                path[position++] = hex[byte >> 4u];
                path[position++] = hex[byte & 0x0fu];
            }
            path[position++] = '/';
            for (const char character : key)
                path[position++] = character;
            result = resources::ResourcePath::FromString({path, position});
            return result.IsValid();
        }

        [[nodiscard]] bool ValidOutput(const assets::AssetId asset, const ProjectAssetOutputState& output) noexcept
        {
            if (!output.resource.IsValid() || !output.resource.IsTyped())
                return false;
            resources::ResourcePath expected;
            return ExpectedOutputPath(asset, containers::StringView(output.key), expected) && expected == output.resource.GetPath();
        }

        [[nodiscard]] bool ValidReference(const assets::AssetId asset, const assets::AssetReference& reference) noexcept
        {
            return reference.from == asset && reference.to.IsValid() && reference.to.IsTyped() &&
                   reference.kind <= resources::DependencyKind::Soft && reference.location <= assets::AssetReferenceLocation::Inner;
        }

        [[nodiscard]] bool ValidState(const ProjectAssetAcceptedBaseline& snapshot, const ProjectAssetState& state) noexcept
        {
            return state.asset.IsValid() && ValidDirectoryPath(state.sourceRoot) && state.sourceIdentity.IsValid() &&
                   ValidFilePath(state.physicalPath) && ValidSourceIssueBits(state.sourceIssues) &&
                   state.importState <= assets::SourceImportState::InvalidMetadata &&
                   state.referenceScanState <= assets::SourceReferenceScanState::ScanFailed &&
                   state.presence <= ProjectAssetPresence::Conflicting && state.acceptedGeneration != 0u &&
                   state.acceptedGeneration <= snapshot.generation;
        }

        [[nodiscard]] bool ValidDiagnostic(const ProjectAssetSourceDiagnostic& diagnostic) noexcept
        {
            return ValidDirectoryPath(diagnostic.sourceRoot) && diagnostic.sourceIdentity.IsValid() &&
                   ValidFilePath(diagnostic.physicalPath) && ValidSourceIssueBits(diagnostic.sourceIssues) &&
                   diagnostic.metadataResult <= assets::MetadataResult::IdentityCollision &&
                   diagnostic.importState <= assets::SourceImportState::InvalidMetadata &&
                   diagnostic.referenceScanState <= assets::SourceReferenceScanState::ScanFailed &&
                   ValidDiagnosticReasonBits(diagnostic.reasons) &&
                   (!diagnostic.hasParsedAssetIdentity || diagnostic.asset.IsValid());
        }

        [[nodiscard]] bool EmptyCohortIntent(const ProjectAssetCohortIntent& intent) noexcept
        {
            return !intent.asset.IsValid() && !intent.sourceIdentity.IsValid() && intent.reviewedAcceptedGeneration == 0u &&
                   intent.targetMetadataFingerprint.IsEmpty() && intent.predecessorOutputs.Empty() && intent.successorOutputs.Empty();
        }

        [[nodiscard]] bool ValidCohortOutputSet(
            const containers::DynamicArray<resources::ResourceReference>& outputs) noexcept
        {
            if (outputs.Empty())
                return false;
            for (u32 index = 0u; index < outputs.Size(); ++index)
            {
                if (!outputs[index].IsValid() || !outputs[index].IsTyped() ||
                    (index != 0u && CompareResource(outputs[index - 1u], outputs[index]) >= 0))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidCohortIntent(const ProjectAssetAcceptedBaseline& snapshot) noexcept
        {
            const ProjectAssetCohortIntent& intent = snapshot.cohortIntent;
            if (EmptyCohortIntent(intent))
                return true;
            if (!intent.IsActive() || intent.reviewedAcceptedGeneration > snapshot.generation ||
                intent.predecessorOutputs.Size() > snapshot.limits.maximumOutputs ||
                intent.successorOutputs.Size() > snapshot.limits.maximumOutputs ||
                (!intent.predecessorOutputs.Empty() && !ValidCohortOutputSet(intent.predecessorOutputs)) ||
                !ValidCohortOutputSet(intent.successorOutputs))
                return false;
            for (const ProjectAssetState& state : snapshot.assets)
                if (state.asset == intent.asset)
                    return state.sourceIdentity == intent.sourceIdentity;
            // A first import has no accepted predecessor yet. The target VMETA
            // digest validates this write-ahead marker after transaction recovery;
            // a reimport must still bind to the exact accepted source owner.
            return intent.predecessorOutputs.Empty();
        }

        [[nodiscard]] ProjectAssetSnapshotResult ValidateSnapshotForWrite(const ProjectAssetAcceptedBaseline& snapshot) noexcept
        {
            if (!snapshot.limits.IsValid() || snapshot.assets.Size() > snapshot.limits.maximumAssets ||
                snapshot.tombstones > snapshot.limits.maximumTombstones || snapshot.outputs.Size() > snapshot.limits.maximumOutputs ||
                snapshot.references.Size() > snapshot.limits.maximumReferences ||
                snapshot.diagnostics.Size() > snapshot.limits.maximumDiagnostics)
                return ProjectAssetSnapshotResult::LimitExceeded;

            u32 outputs = 0;
            u32 references = 0;
            u32 tombstones = 0;
            for (u32 assetIndex = 0; assetIndex < snapshot.assets.Size(); ++assetIndex)
            {
                const ProjectAssetState& state = snapshot.assets[assetIndex];
                if (!ValidState(snapshot, state) ||
                    (assetIndex != 0u && CompareAssetId(snapshot.assets[assetIndex - 1u].asset, state.asset) >= 0))
                    return ProjectAssetSnapshotResult::InvalidState;
                if (state.outputs.Count() > snapshot.limits.maximumOutputs - outputs ||
                    state.references.Count() > snapshot.limits.maximumReferences - references)
                    return ProjectAssetSnapshotResult::LimitExceeded;
                if (state.outputs.Count() != 0u && state.outputs.Data() != snapshot.outputs.TypedData() + outputs)
                    return ProjectAssetSnapshotResult::InvalidState;
                if (state.references.Count() != 0u && state.references.Data() != snapshot.references.TypedData() + references)
                    return ProjectAssetSnapshotResult::InvalidState;
                for (u32 index = 0; index < state.outputs.Count(); ++index)
                {
                    if (!ValidOutput(state.asset, state.outputs[index]) ||
                        (index != 0u && (!OutputLess(state.outputs[index - 1u], state.outputs[index]) ||
                                         !OutputKeyLess(state.outputs[index - 1u], state.outputs[index]))))
                        return ProjectAssetSnapshotResult::InvalidState;
                }
                for (u32 index = 0; index < state.references.Count(); ++index)
                {
                    if (!ValidReference(state.asset, state.references[index]) ||
                        (index != 0u && !ReferenceLess(state.references[index - 1u], state.references[index])))
                        return ProjectAssetSnapshotResult::InvalidState;
                }
                outputs += state.outputs.Count();
                references += state.references.Count();
                if (state.presence == ProjectAssetPresence::Missing)
                    ++tombstones;
            }
            if (outputs != snapshot.outputs.Size() || references != snapshot.references.Size() || tombstones != snapshot.tombstones)
                return ProjectAssetSnapshotResult::InvalidState;
            for (u32 index = 0; index < snapshot.diagnostics.Size(); ++index)
                if (!ValidDiagnostic(snapshot.diagnostics[index]) ||
                    (index != 0u && !DiagnosticLess(snapshot.diagnostics[index - 1u], snapshot.diagnostics[index])))
                    return ProjectAssetSnapshotResult::InvalidState;
            if (!ValidCohortIntent(snapshot))
                return ProjectAssetSnapshotResult::InvalidState;
            return ProjectAssetSnapshotResult::Success;
        }

        [[nodiscard]] bool WriteOutput(BufferWriter& writer, const ProjectAssetOutputState& output) noexcept
        {
            return writer.WriteString(containers::StringView(output.key)) && writer.WriteU64(output.resource.GetPath().Id()) &&
                   writer.WriteU32(output.resource.ExpectedType());
        }

        [[nodiscard]] bool WriteReference(BufferWriter& writer, const assets::AssetReference& reference) noexcept
        {
            return writer.WriteBytes(reference.from.bytes, sizeof(reference.from.bytes)) && writer.WriteU64(reference.to.GetPath().Id()) &&
                   writer.WriteU32(reference.to.ExpectedType()) && writer.WriteU8(static_cast<u8>(reference.kind)) &&
                   writer.WriteU8(static_cast<u8>(reference.location)) && writer.WriteU8(reference.inPlace ? 1u : 0u) && writer.WriteU8(0u);
        }

        [[nodiscard]] bool WriteState(BufferWriter& writer, const ProjectAssetState& state) noexcept
        {
            return writer.WriteBytes(state.asset.bytes, sizeof(state.asset.bytes)) && writer.WriteString(state.sourceRoot.AsStringView()) &&
                   writer.WriteU64(state.sourceIdentity.Id()) && writer.WriteString(state.physicalPath.AsStringView()) &&
                   writer.WriteBytes(state.sourceContentFingerprint.bytes, crypto::Digest256::ByteCount) &&
                   writer.WriteBytes(state.settingsFingerprint.bytes, crypto::Digest256::ByteCount) && writer.WriteU64(state.importer) &&
                   writer.WriteU32(state.authoredImporterVersion) && writer.WriteU32(state.availableImporterVersion) &&
                   writer.WriteU32(state.settingsVersion) && writer.WriteU32(state.sourceType) && writer.WriteU32(state.sourceIssues) &&
                   writer.WriteU8(static_cast<u8>(state.importState)) && writer.WriteU8(static_cast<u8>(state.referenceScanState)) &&
                   writer.WriteU8(static_cast<u8>(state.presence)) && writer.WriteU8(state.hasSourceContentFingerprint ? 1u : 0u) &&
                   writer.WriteU64(state.acceptedGeneration) && writer.WriteU32(state.outputs.Count()) && writer.WriteU32(state.references.Count());
        }

        [[nodiscard]] bool WriteDiagnostic(BufferWriter& writer, const ProjectAssetSourceDiagnostic& diagnostic) noexcept
        {
            return writer.WriteString(diagnostic.sourceRoot.AsStringView()) && writer.WriteU64(diagnostic.sourceIdentity.Id()) &&
                   writer.WriteString(diagnostic.physicalPath.AsStringView()) &&
                   writer.WriteBytes(diagnostic.asset.bytes, sizeof(diagnostic.asset.bytes)) && writer.WriteU32(diagnostic.sourceIssues) &&
                   writer.WriteU8(static_cast<u8>(diagnostic.metadataResult)) && writer.WriteU8(static_cast<u8>(diagnostic.importState)) &&
                   writer.WriteU8(static_cast<u8>(diagnostic.referenceScanState)) && writer.WriteU8(diagnostic.hasParsedAssetIdentity ? 1u : 0u) &&
                   writer.WriteU32(static_cast<u32>(diagnostic.reasons));
        }

        [[nodiscard]] bool WriteResourceReference(BufferWriter& writer, const resources::ResourceReference output) noexcept
        {
            return writer.WriteU64(output.GetPath().Id()) && writer.WriteU32(output.ExpectedType());
        }

        [[nodiscard]] bool WriteCohortIntent(BufferWriter& writer, const ProjectAssetCohortIntent& intent) noexcept
        {
            if (!intent.IsActive())
                return true;
            bool written = writer.WriteBytes(intent.asset.bytes, sizeof(intent.asset.bytes)) &&
                           writer.WriteU64(intent.sourceIdentity.Id()) && writer.WriteU64(intent.reviewedAcceptedGeneration) &&
                           writer.WriteBytes(intent.targetMetadataFingerprint.bytes, crypto::Digest256::ByteCount) &&
                           writer.WriteU32(intent.predecessorOutputs.Size()) && writer.WriteU32(intent.successorOutputs.Size());
            for (const resources::ResourceReference output : intent.predecessorOutputs)
                written = written && WriteResourceReference(writer, output);
            for (const resources::ResourceReference output : intent.successorOutputs)
                written = written && WriteResourceReference(writer, output);
            return written;
        }

        [[nodiscard]] ProjectAssetSnapshotResult BuildSerializedSnapshot(const ProjectAssetAcceptedBaseline& snapshot,
                                                                         BufferWriter& writer) noexcept
        {
            const ProjectAssetSnapshotResult valid = ValidateSnapshotForWrite(snapshot);
            if (valid != ProjectAssetSnapshotResult::Success)
                return valid;

            crypto::Digest256 empty;
            bool written = writer.WriteU32(ProjectAssetSnapshotMagic) && writer.WriteU16(ProjectAssetSnapshotMajorVersion) &&
                           writer.WriteU16(ProjectAssetSnapshotMinorVersion) && writer.WriteU32(ProjectAssetSnapshotHeaderSize) &&
                           writer.WriteU32(snapshot.assets.Size()) && writer.WriteU32(snapshot.tombstones) &&
                           writer.WriteU32(snapshot.outputs.Size()) && writer.WriteU32(snapshot.references.Size()) &&
                           writer.WriteU32(snapshot.diagnostics.Size()) && writer.WriteU32(snapshot.cohortIntent.IsActive() ? 1u : 0u) &&
                           writer.WriteU64(0u) &&
                           writer.WriteU64(snapshot.generation) && writer.WriteBytes(empty.bytes, crypto::Digest256::ByteCount);
            const u8 reserved[ProjectAssetSnapshotHeaderSize - 84u]{};
            written = written && writer.WriteBytes(reserved, sizeof(reserved));

            for (const ProjectAssetState& state : snapshot.assets)
            {
                written = written && WriteState(writer, state);
                for (const ProjectAssetOutputState& output : state.outputs)
                    written = written && WriteOutput(writer, output);
                for (const assets::AssetReference& reference : state.references)
                    written = written && WriteReference(writer, reference);
            }
            for (const ProjectAssetSourceDiagnostic& diagnostic : snapshot.diagnostics)
                written = written && WriteDiagnostic(writer, diagnostic);
            written = written && WriteCohortIntent(writer, snapshot.cohortIntent);
            if (!written)
                return ProjectAssetSnapshotResult::LimitExceeded;

            const containers::DynamicArray<u8>& bytes = writer.Bytes();
            if (bytes.Size() < ProjectAssetSnapshotHeaderSize)
                return ProjectAssetSnapshotResult::InvalidState;
            const u64 fileSize = bytes.Size();
            const u8 sizeBytes[] = {static_cast<u8>(fileSize),        static_cast<u8>(fileSize >> 8u),
                                    static_cast<u8>(fileSize >> 16u), static_cast<u8>(fileSize >> 24u),
                                    static_cast<u8>(fileSize >> 32u), static_cast<u8>(fileSize >> 40u),
                                    static_cast<u8>(fileSize >> 48u), static_cast<u8>(fileSize >> 56u)};
            const crypto::Digest256 checksum =
                crypto::Sha256(bytes.TypedData() + ProjectAssetSnapshotHeaderSize, bytes.Size() - ProjectAssetSnapshotHeaderSize);
            return writer.PatchBytes(FileSizeOffset, sizeBytes, sizeof(sizeBytes)) &&
                           writer.PatchBytes(PayloadChecksumOffset, checksum.bytes, crypto::Digest256::ByteCount)
                       ? ProjectAssetSnapshotResult::Success
                       : ProjectAssetSnapshotResult::InvalidState;
        }

        [[nodiscard]] ProjectAssetSnapshotResult ReadSnapshotFile(filesystem::Manager& files, const filesystem::AbsolutePath& path,
                                                                  const u64 maximumBytes,
                                                                  containers::DynamicArray<u8>& bytes) noexcept
        {
            const SnapshotFilePresence presence = QuerySnapshotFilePresence(path);
            if (presence == SnapshotFilePresence::Missing)
                return ProjectAssetSnapshotResult::NotFound;
            if (presence == SnapshotFilePresence::IoFailure)
                return ProjectAssetSnapshotResult::IoFailure;

            auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!file)
                return ProjectAssetSnapshotResult::IoFailure;
            const u64 size = file->GetSize();
            if (size > maximumBytes || size > ~u32{0})
                return ProjectAssetSnapshotResult::LimitExceeded;
            if (size < ProjectAssetSnapshotHeaderSize)
                return ProjectAssetSnapshotResult::Corrupt;
            bytes.Resize(static_cast<u32>(size));
            if (bytes.Size() != size)
                return ProjectAssetSnapshotResult::OutOfMemory;
            file->Serialize(bytes.Data(), bytes.Size());
            const bool read = !file->HasErrors();
            file.Reset();
            if (!read)
            {
                bytes.Clear();
                return ProjectAssetSnapshotResult::IoFailure;
            }
            return ProjectAssetSnapshotResult::Success;
        }

        [[nodiscard]] bool ReadOutput(BufferReader& reader, const assets::AssetId asset, ProjectAssetOutputState& output) noexcept
        {
            containers::String key;
            u64 resourcePath = 0;
            u32 resourceType = 0;
            if (!reader.ReadString(key) || !reader.ReadU64(resourcePath) || !reader.ReadU32(resourceType))
                return false;
            output.key = static_cast<containers::String&&>(key);
            output.resource = resources::ResourceReference(resources::ResourcePath::FromId(resourcePath), resourceType);
            return ValidOutput(asset, output);
        }

        [[nodiscard]] bool ReadReference(BufferReader& reader, const assets::AssetId asset, assets::AssetReference& reference) noexcept
        {
            u64 targetPath = 0;
            u32 targetType = 0;
            u8 kind = 0;
            u8 location = 0;
            u8 inPlace = 0;
            u8 reserved = 0;
            if (!reader.ReadBytes(reference.from.bytes, sizeof(reference.from.bytes)) || !reader.ReadU64(targetPath) ||
                !reader.ReadU32(targetType) || !reader.ReadU8(kind) || !reader.ReadU8(location) || !reader.ReadU8(inPlace) ||
                !reader.ReadU8(reserved))
                return false;
            reference.to = resources::ResourceReference(resources::ResourcePath::FromId(targetPath), targetType);
            reference.kind = static_cast<resources::DependencyKind>(kind);
            reference.location = static_cast<assets::AssetReferenceLocation>(location);
            reference.inPlace = inPlace != 0u;
            return reserved == 0u && inPlace <= 1u && ValidReference(asset, reference);
        }

        [[nodiscard]] bool ReadState(BufferReader& reader, const u64 generation, ProjectAssetState& state, u32& outputCount,
                                     u32& referenceCount) noexcept
        {
            u64 sourceIdentity = 0;
            u8 importState = 0;
            u8 referenceScanState = 0;
            u8 presence = 0;
            u8 hasFingerprint = 0;
            if (!reader.ReadBytes(state.asset.bytes, sizeof(state.asset.bytes)) || !ReadAbsolutePath(reader, true, state.sourceRoot) ||
                !reader.ReadU64(sourceIdentity) || !ReadAbsolutePath(reader, false, state.physicalPath) ||
                !reader.ReadBytes(state.sourceContentFingerprint.bytes, crypto::Digest256::ByteCount) ||
                !reader.ReadBytes(state.settingsFingerprint.bytes, crypto::Digest256::ByteCount) || !reader.ReadU64(state.importer) ||
                !reader.ReadU32(state.authoredImporterVersion) || !reader.ReadU32(state.availableImporterVersion) ||
                !reader.ReadU32(state.settingsVersion) || !reader.ReadU32(state.sourceType) || !reader.ReadU32(state.sourceIssues) ||
                !reader.ReadU8(importState) || !reader.ReadU8(referenceScanState) || !reader.ReadU8(presence) ||
                !reader.ReadU8(hasFingerprint) || !reader.ReadU64(state.acceptedGeneration) || !reader.ReadU32(outputCount) ||
                !reader.ReadU32(referenceCount))
                return false;
            state.sourceIdentity = resources::ResourcePath::FromId(sourceIdentity);
            state.importState = static_cast<assets::SourceImportState>(importState);
            state.referenceScanState = static_cast<assets::SourceReferenceScanState>(referenceScanState);
            state.presence = static_cast<ProjectAssetPresence>(presence);
            state.hasSourceContentFingerprint = hasFingerprint != 0u;
            return hasFingerprint <= 1u && state.asset.IsValid() && ValidDirectoryPath(state.sourceRoot) && state.sourceIdentity.IsValid() &&
                   ValidFilePath(state.physicalPath) && ValidSourceIssueBits(state.sourceIssues) &&
                   state.importState <= assets::SourceImportState::InvalidMetadata &&
                   state.referenceScanState <= assets::SourceReferenceScanState::ScanFailed &&
                   state.presence <= ProjectAssetPresence::Conflicting && state.acceptedGeneration != 0u &&
                   state.acceptedGeneration <= generation;
        }

        [[nodiscard]] bool ReadDiagnostic(BufferReader& reader, ProjectAssetSourceDiagnostic& diagnostic) noexcept
        {
            u64 sourceIdentity = 0;
            u8 metadataResult = 0;
            u8 importState = 0;
            u8 referenceScanState = 0;
            u8 hasParsedAssetIdentity = 0;
            u32 reasons = 0;
            if (!ReadAbsolutePath(reader, true, diagnostic.sourceRoot) || !reader.ReadU64(sourceIdentity) ||
                !ReadAbsolutePath(reader, false, diagnostic.physicalPath) ||
                !reader.ReadBytes(diagnostic.asset.bytes, sizeof(diagnostic.asset.bytes)) || !reader.ReadU32(diagnostic.sourceIssues) ||
                !reader.ReadU8(metadataResult) || !reader.ReadU8(importState) || !reader.ReadU8(referenceScanState) ||
                !reader.ReadU8(hasParsedAssetIdentity) || !reader.ReadU32(reasons))
                return false;
            diagnostic.sourceIdentity = resources::ResourcePath::FromId(sourceIdentity);
            diagnostic.metadataResult = static_cast<assets::MetadataResult>(metadataResult);
            diagnostic.importState = static_cast<assets::SourceImportState>(importState);
            diagnostic.referenceScanState = static_cast<assets::SourceReferenceScanState>(referenceScanState);
            diagnostic.reasons = static_cast<ProjectAssetSourceDiagnosticReason>(reasons);
            diagnostic.hasParsedAssetIdentity = hasParsedAssetIdentity != 0u;
            return hasParsedAssetIdentity <= 1u && ValidDiagnostic(diagnostic);
        }

        [[nodiscard]] bool ReadResourceReference(BufferReader& reader, resources::ResourceReference& output) noexcept
        {
            u64 path = 0u;
            u32 type = resources::InvalidResourceTypeId;
            if (!reader.ReadU64(path) || !reader.ReadU32(type))
                return false;
            output = resources::ResourceReference(resources::ResourcePath::FromId(path), type);
            return output.IsValid() && output.IsTyped();
        }

        [[nodiscard]] bool ReadCohortIntent(BufferReader& reader, const ProjectAssetChangeStateLimits& limits,
                                            ProjectAssetCohortIntent& intent) noexcept
        {
            u64 sourceIdentity = 0u;
            u32 predecessorCount = 0u;
            u32 successorCount = 0u;
            if (!reader.ReadBytes(intent.asset.bytes, sizeof(intent.asset.bytes)) || !reader.ReadU64(sourceIdentity) ||
                !reader.ReadU64(intent.reviewedAcceptedGeneration) ||
                !reader.ReadBytes(intent.targetMetadataFingerprint.bytes, crypto::Digest256::ByteCount) ||
                !reader.ReadU32(predecessorCount) || !reader.ReadU32(successorCount))
                return false;
            intent.sourceIdentity = resources::ResourcePath::FromId(sourceIdentity);
            if (successorCount == 0u || predecessorCount > limits.maximumOutputs ||
                successorCount > limits.maximumOutputs)
                return false;
            intent.predecessorOutputs.Reserve(predecessorCount);
            intent.successorOutputs.Reserve(successorCount);
            if (intent.predecessorOutputs.Capacity() < predecessorCount || intent.successorOutputs.Capacity() < successorCount)
                return false;
            for (u32 index = 0u; index < predecessorCount; ++index)
            {
                resources::ResourceReference output;
                if (!ReadResourceReference(reader, output) ||
                    (index != 0u && CompareResource(intent.predecessorOutputs[index - 1u], output) >= 0))
                    return false;
                intent.predecessorOutputs.PushBack(output);
                if (intent.predecessorOutputs.Size() != index + 1u)
                    return false;
            }
            for (u32 index = 0u; index < successorCount; ++index)
            {
                resources::ResourceReference output;
                if (!ReadResourceReference(reader, output) ||
                    (index != 0u && CompareResource(intent.successorOutputs[index - 1u], output) >= 0))
                    return false;
                intent.successorOutputs.PushBack(output);
                if (intent.successorOutputs.Size() != index + 1u)
                    return false;
            }
            return intent.IsActive();
        }

        [[nodiscard]] ProjectAssetSnapshotResult ParseSnapshot(const containers::DynamicArray<u8>& bytes,
                                                               const ProjectAssetChangeStateLimits& limits,
                                                               ProjectAssetAcceptedBaseline& destination) noexcept
        {
            BufferReader reader(bytes.TypedData(), bytes.Size());
            u32 magic = 0;
            u16 major = 0;
            u16 minor = 0;
            u32 headerSize = 0;
            u32 assetCount = 0;
            u32 tombstoneCount = 0;
            u32 outputCount = 0;
            u32 referenceCount = 0;
            u32 diagnosticCount = 0;
            u32 reserved32 = 0;
            u64 fileSize = 0;
            u64 generation = 0;
            crypto::Digest256 checksum;
            u8 reservedHeader[ProjectAssetSnapshotHeaderSize - 84u]{};
            if (!reader.ReadU32(magic) || !reader.ReadU16(major) || !reader.ReadU16(minor) || !reader.ReadU32(headerSize) ||
                !reader.ReadU32(assetCount) || !reader.ReadU32(tombstoneCount) || !reader.ReadU32(outputCount) ||
                !reader.ReadU32(referenceCount) || !reader.ReadU32(diagnosticCount) || !reader.ReadU32(reserved32) ||
                !reader.ReadU64(fileSize) || !reader.ReadU64(generation) || !reader.ReadBytes(checksum.bytes, crypto::Digest256::ByteCount) ||
                !reader.ReadBytes(reservedHeader, sizeof(reservedHeader)))
                return ProjectAssetSnapshotResult::Corrupt;
            if (magic != ProjectAssetSnapshotMagic || major != ProjectAssetSnapshotMajorVersion ||
                minor > ProjectAssetSnapshotMinorVersion || headerSize != ProjectAssetSnapshotHeaderSize)
                return ProjectAssetSnapshotResult::Incompatible;
            bool reservedHeaderZero = true;
            for (const u8 value : reservedHeader)
                reservedHeaderZero = reservedHeaderZero && value == 0u;
            if ((minor == 0u ? reserved32 != 0u : reserved32 > 1u) || !reservedHeaderZero || fileSize != bytes.Size() ||
                crypto::Sha256(bytes.TypedData() + ProjectAssetSnapshotHeaderSize, bytes.Size() - ProjectAssetSnapshotHeaderSize) != checksum)
                return ProjectAssetSnapshotResult::Corrupt;
            if (assetCount > limits.maximumAssets || tombstoneCount > limits.maximumTombstones || tombstoneCount > assetCount ||
                outputCount > limits.maximumOutputs || referenceCount > limits.maximumReferences ||
                diagnosticCount > limits.maximumDiagnostics)
                return ProjectAssetSnapshotResult::LimitExceeded;
            if ((assetCount != 0u || tombstoneCount != 0u || outputCount != 0u || referenceCount != 0u || diagnosticCount != 0u) &&
                generation == 0u)
                return ProjectAssetSnapshotResult::Corrupt;

            ProjectAssetAcceptedBaseline parsed(limits);
            containers::DynamicArray<AssetSlice> slices(memory::pools::Assets::GetInstance());
            parsed.assets.Reserve(assetCount);
            parsed.outputs.Reserve(outputCount);
            parsed.references.Reserve(referenceCount);
            parsed.backwardReferences.Reserve(referenceCount);
            parsed.diagnostics.Reserve(diagnosticCount);
            parsed.assetLookup.Reserve(assetCount);
            slices.Reserve(assetCount);
            if (parsed.assets.Capacity() < assetCount || parsed.outputs.Capacity() < outputCount ||
                parsed.references.Capacity() < referenceCount || parsed.backwardReferences.Capacity() < referenceCount ||
                parsed.diagnostics.Capacity() < diagnosticCount || slices.Capacity() < assetCount)
                return ProjectAssetSnapshotResult::OutOfMemory;
            parsed.generation = generation;
            parsed.tombstones = tombstoneCount;

            u32 parsedOutputs = 0;
            u32 parsedReferences = 0;
            u32 actualTombstones = 0;
            for (u32 assetIndex = 0; assetIndex < assetCount; ++assetIndex)
            {
                ProjectAssetState state;
                u32 stateOutputCount = 0;
                u32 stateReferenceCount = 0;
                if (!ReadState(reader, generation, state, stateOutputCount, stateReferenceCount) ||
                    stateOutputCount > outputCount - parsedOutputs || stateReferenceCount > referenceCount - parsedReferences ||
                    (assetIndex != 0u && CompareAssetId(parsed.assets[assetIndex - 1u].asset, state.asset) >= 0))
                    return reader.OutOfMemory() ? ProjectAssetSnapshotResult::OutOfMemory : ProjectAssetSnapshotResult::Corrupt;

                AssetSlice slice;
                slice.outputOffset = parsed.outputs.Size();
                slice.outputCount = stateOutputCount;
                for (u32 index = 0; index < stateOutputCount; ++index)
                {
                    ProjectAssetOutputState output;
                    if (!ReadOutput(reader, state.asset, output) ||
                        (index != 0u && (!OutputLess(parsed.outputs[parsed.outputs.Size() - 1u], output) ||
                                         !OutputKeyLess(parsed.outputs[parsed.outputs.Size() - 1u], output))))
                        return reader.OutOfMemory() ? ProjectAssetSnapshotResult::OutOfMemory : ProjectAssetSnapshotResult::Corrupt;
                    parsed.outputs.PushBack(static_cast<ProjectAssetOutputState&&>(output));
                    if (parsed.outputs.Size() != slice.outputOffset + index + 1u)
                        return ProjectAssetSnapshotResult::OutOfMemory;
                }
                parsedOutputs += stateOutputCount;

                slice.referenceOffset = parsed.references.Size();
                slice.referenceCount = stateReferenceCount;
                for (u32 index = 0; index < stateReferenceCount; ++index)
                {
                    assets::AssetReference reference;
                    if (!ReadReference(reader, state.asset, reference) ||
                        (index != 0u && !ReferenceLess(parsed.references[parsed.references.Size() - 1u], reference)))
                        return reader.OutOfMemory() ? ProjectAssetSnapshotResult::OutOfMemory : ProjectAssetSnapshotResult::Corrupt;
                    parsed.references.PushBack(reference);
                    if (parsed.references.Size() != slice.referenceOffset + index + 1u)
                        return ProjectAssetSnapshotResult::OutOfMemory;
                }
                parsedReferences += stateReferenceCount;
                if (state.presence == ProjectAssetPresence::Missing)
                    ++actualTombstones;

                const u32 before = parsed.assets.Size();
                parsed.assets.PushBack(static_cast<ProjectAssetState&&>(state));
                slices.PushBack(slice);
                if (parsed.assets.Size() != before + 1u || slices.Size() != parsed.assets.Size())
                    return ProjectAssetSnapshotResult::OutOfMemory;
            }
            if (parsedOutputs != outputCount || parsedReferences != referenceCount || actualTombstones != tombstoneCount)
                return ProjectAssetSnapshotResult::Corrupt;

            for (u32 index = 0; index < diagnosticCount; ++index)
            {
                ProjectAssetSourceDiagnostic diagnostic;
                if (!ReadDiagnostic(reader, diagnostic) ||
                    (index != 0u && !DiagnosticLess(parsed.diagnostics[index - 1u], diagnostic)))
                    return reader.OutOfMemory() ? ProjectAssetSnapshotResult::OutOfMemory : ProjectAssetSnapshotResult::Corrupt;
                const u32 before = parsed.diagnostics.Size();
                parsed.diagnostics.PushBack(static_cast<ProjectAssetSourceDiagnostic&&>(diagnostic));
                if (parsed.diagnostics.Size() != before + 1u)
                    return ProjectAssetSnapshotResult::OutOfMemory;
            }
            if (minor >= 1u && reserved32 == 1u && !ReadCohortIntent(reader, limits, parsed.cohortIntent))
                return reader.OutOfMemory() ? ProjectAssetSnapshotResult::OutOfMemory : ProjectAssetSnapshotResult::Corrupt;
            if (reader.Position() != reader.Size())
                return ProjectAssetSnapshotResult::Corrupt;

            for (u32 index = 0; index < parsed.assets.Size(); ++index)
            {
                ProjectAssetState& state = parsed.assets[index];
                const AssetSlice& slice = slices[index];
                state.outputs = {parsed.outputs.TypedData() + slice.outputOffset, slice.outputCount};
                state.references = {parsed.references.TypedData() + slice.referenceOffset, slice.referenceCount};
                containers::String key;
                if (!assets::GetAssetOutputPath(state.asset, "source", key))
                    return ProjectAssetSnapshotResult::OutOfMemory;
                if (!parsed.assetLookup.Insert(static_cast<containers::String&&>(key), index).IsSuccessful())
                    return ProjectAssetSnapshotResult::OutOfMemory;
                if (state.presence == ProjectAssetPresence::Present)
                    for (const assets::AssetReference& reference : state.references)
                    {
                        const u32 before = parsed.backwardReferences.Size();
                        parsed.backwardReferences.PushBack(reference);
                        if (parsed.backwardReferences.Size() != before + 1u)
                            return ProjectAssetSnapshotResult::OutOfMemory;
                    }
            }
            if (parsed.backwardReferences.Size() > 1u)
                std::sort(parsed.backwardReferences.Begin(), parsed.backwardReferences.End(), ReferenceToLess);
            if (!ValidCohortIntent(parsed))
                return ProjectAssetSnapshotResult::Corrupt;

            // Durable images intentionally carry no current changes or
            // invalidation work. Those arrays remain empty in `parsed`.
            destination.Swap(parsed);
            return ProjectAssetSnapshotResult::Success;
        }
    } // namespace

    const char* ToString(const ProjectAssetSnapshotResult result) noexcept
    {
        switch (result)
        {
        case ProjectAssetSnapshotResult::NotChecked: return "NotChecked";
        case ProjectAssetSnapshotResult::Success: return "Success";
        case ProjectAssetSnapshotResult::NotFound: return "NotFound";
        case ProjectAssetSnapshotResult::InvalidState: return "InvalidState";
        case ProjectAssetSnapshotResult::Corrupt: return "Corrupt";
        case ProjectAssetSnapshotResult::Incompatible: return "Incompatible";
        case ProjectAssetSnapshotResult::IoFailure: return "IoFailure";
        case ProjectAssetSnapshotResult::OutOfMemory: return "OutOfMemory";
        case ProjectAssetSnapshotResult::LimitExceeded: return "LimitExceeded";
        }
        return "Unknown";
    }

    filesystem::AbsolutePath ProjectAssetSnapshotPath(const filesystem::AbsolutePath& root) noexcept
    {
        return root.AddFilePath(ProjectAssetSnapshotFileName);
    }

    filesystem::AbsolutePath ProjectAssetSnapshotStagePath(const filesystem::AbsolutePath& root) noexcept
    {
        return root.AddFilePath(ProjectAssetSnapshotStageFileName);
    }

    ProjectAssetSnapshotResult LoadProjectAssetAcceptedSnapshot(filesystem::Manager& files, const filesystem::AbsolutePath& root,
                                                                const ProjectAssetChangeStateLimits& limits,
                                                                ProjectAssetAcceptedBaseline& destination) noexcept
    {
        if (!limits.IsValid() || !ValidDirectoryPath(root))
            return ProjectAssetSnapshotResult::InvalidState;
        if (!files.CreatePath(root))
            return ProjectAssetSnapshotResult::IoFailure;

        const filesystem::AbsolutePath staged = ProjectAssetSnapshotStagePath(root);
        const SnapshotFilePresence stagedPresence = QuerySnapshotFilePresence(staged);
        if (stagedPresence == SnapshotFilePresence::IoFailure ||
            (stagedPresence == SnapshotFilePresence::Present && !files.DeleteFile(staged)))
            return ProjectAssetSnapshotResult::IoFailure;

        const filesystem::AbsolutePath path = ProjectAssetSnapshotPath(root);
        containers::DynamicArray<u8> bytes(memory::pools::Assets::GetInstance());
        ProjectAssetSnapshotResult loaded = ReadSnapshotFile(files, path, limits.maximumSerializedBytes, bytes);
        if (loaded == ProjectAssetSnapshotResult::Success)
            loaded = ParseSnapshot(bytes, limits, destination);
        // VPAS 1.1 carries the P5 write-ahead cohort intent that protects the
        // previous visible generation before a VADI retirement journal exists.
        // An unusable final image is therefore safety-critical evidence: preserve
        // it for diagnosis/recovery and let pipeline initialization fail closed.
        // Only an actually missing image is a valid bootstrap condition.
        return loaded;
    }

    ProjectAssetSnapshotResult StageProjectAssetAcceptedSnapshot(filesystem::Manager& files, const filesystem::AbsolutePath& root,
                                                                 const ProjectAssetAcceptedBaseline& snapshot) noexcept
    {
        if (!snapshot.limits.IsValid() || !ValidDirectoryPath(root))
            return ProjectAssetSnapshotResult::InvalidState;
        if (!files.CreatePath(root))
            return ProjectAssetSnapshotResult::IoFailure;

        const filesystem::AbsolutePath staged = ProjectAssetSnapshotStagePath(root);
        const SnapshotFilePresence stagedPresence = QuerySnapshotFilePresence(staged);
        if (stagedPresence == SnapshotFilePresence::IoFailure ||
            (stagedPresence == SnapshotFilePresence::Present && !files.DeleteFile(staged)))
            return ProjectAssetSnapshotResult::IoFailure;

        BufferWriter writer(snapshot.limits.maximumSerializedBytes);
        const ProjectAssetSnapshotResult serialized = BuildSerializedSnapshot(snapshot, writer);
        if (serialized != ProjectAssetSnapshotResult::Success)
            return serialized;

        auto file = files.CreateFileWriter(staged, filesystem::FOF_Buffered);
        if (!file)
            return ProjectAssetSnapshotResult::IoFailure;
        const containers::DynamicArray<u8>& bytes = writer.Bytes();
        file->Serialize(const_cast<u8*>(bytes.TypedData()), bytes.Size());
        file->Flush();
        const bool written = !file->HasErrors();
        file.Reset();
        if (!written)
        {
            static_cast<void>(files.DeleteFile(staged));
            return ProjectAssetSnapshotResult::IoFailure;
        }

        containers::DynamicArray<u8> validation(memory::pools::Assets::GetInstance());
        const ProjectAssetSnapshotResult readBack = ReadSnapshotFile(files, staged, snapshot.limits.maximumSerializedBytes, validation);
        if (readBack != ProjectAssetSnapshotResult::Success || validation.Size() != bytes.Size() ||
            crypto::Sha256(validation.TypedData(), validation.Size()) != crypto::Sha256(bytes.TypedData(), bytes.Size()))
        {
            static_cast<void>(files.DeleteFile(staged));
            return readBack == ProjectAssetSnapshotResult::OutOfMemory ? readBack : ProjectAssetSnapshotResult::IoFailure;
        }
        return ProjectAssetSnapshotResult::Success;
    }

    ProjectAssetSnapshotResult CommitStagedProjectAssetAcceptedSnapshot(filesystem::Manager& files,
                                                                        const filesystem::AbsolutePath& root) noexcept
    {
        if (!ValidDirectoryPath(root))
            return ProjectAssetSnapshotResult::InvalidState;
        const filesystem::AbsolutePath staged = ProjectAssetSnapshotStagePath(root);
        const SnapshotFilePresence stagedPresence = QuerySnapshotFilePresence(staged);
        if (stagedPresence == SnapshotFilePresence::Missing)
            return ProjectAssetSnapshotResult::InvalidState;
        if (stagedPresence == SnapshotFilePresence::IoFailure)
            return ProjectAssetSnapshotResult::IoFailure;
        if (!filesystem::ReplaceFile(files, staged, ProjectAssetSnapshotPath(root)))
            return ProjectAssetSnapshotResult::IoFailure;
        return ProjectAssetSnapshotResult::Success;
    }

    bool DiscardStagedProjectAssetAcceptedSnapshot(filesystem::Manager& files, const filesystem::AbsolutePath& root) noexcept
    {
        if (!ValidDirectoryPath(root))
            return false;
        const filesystem::AbsolutePath staged = ProjectAssetSnapshotStagePath(root);
        const SnapshotFilePresence stagedPresence = QuerySnapshotFilePresence(staged);
        return stagedPresence == SnapshotFilePresence::Missing ||
               (stagedPresence == SnapshotFilePresence::Present && files.DeleteFile(staged));
    }
} // namespace vanguard::asset_streaming
