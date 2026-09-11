#include <vanguard/assets/assets.hpp>
#include <vanguard/assets/derived_data_artifact_source.hpp>

#include <vanguard/assets/derived_data_artifact_source_internal.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    using namespace vanguard::assets;

    constexpr u16 KnownArtifactFlags = static_cast<u16>(ArtifactFlags::Primary) | static_cast<u16>(ArtifactFlags::Streamable) |
                                       static_cast<u16>(ArtifactFlags::MemoryResident) | static_cast<u16>(ArtifactFlags::EditorOnly);

    void HashU8(crypto::Sha256Builder& hash, const u8 value) noexcept
    {
        static_cast<void>(hash.Update(&value, sizeof(value)));
    }

    void HashU32(crypto::Sha256Builder& hash, const u32 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value), static_cast<u8>(value >> 8u), static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashU64(crypto::Sha256Builder& hash, const u64 value) noexcept
    {
        const u8 bytes[] = {static_cast<u8>(value),        static_cast<u8>(value >> 8u),  static_cast<u8>(value >> 16u), static_cast<u8>(value >> 24u),
                            static_cast<u8>(value >> 32u), static_cast<u8>(value >> 40u), static_cast<u8>(value >> 48u), static_cast<u8>(value >> 56u)};
        static_cast<void>(hash.Update(bytes, sizeof(bytes)));
    }

    void HashDigest(crypto::Sha256Builder& hash, const BuildFingerprint& digest) noexcept
    {
        static_cast<void>(hash.Update(digest.bytes, BuildFingerprint::ByteCount));
    }

    [[nodiscard]] bool DependencyLess(const BuildDependency& left, const BuildDependency& right) noexcept
    {
        if (left.role != right.role)
        {
            return left.role < right.role;
        }
        if (left.identity.GetPath() != right.identity.GetPath())
        {
            return left.identity.GetPath() < right.identity.GetPath();
        }
        if (left.identity.ExpectedType() != right.identity.ExpectedType())
        {
            return left.identity.ExpectedType() < right.identity.ExpectedType();
        }
        return left.requirement < right.requirement;
    }

    void SortDependencies(containers::DynamicArray<BuildDependency>& dependencies) noexcept
    {
        for (u32 index = 1; index < dependencies.Size(); ++index)
        {
            const BuildDependency value = dependencies[index];
            u32 insertion = index;
            while (insertion != 0 && DependencyLess(value, dependencies[insertion - 1u]))
            {
                dependencies[insertion] = dependencies[insertion - 1u];
                --insertion;
            }
            dependencies[insertion] = value;
        }
    }

    [[nodiscard]] bool CopyArtifacts(const containers::ArraySpan<const Artifact> source, containers::DynamicArray<Artifact>& destination) noexcept
    {
        destination.Clear();
        destination.Reserve(source.Count());
        if (destination.Capacity() < source.Count())
        {
            return false;
        }
        for (const Artifact& artifact : source)
        {
            const u32 previous = destination.Size();
            destination.PushBack(artifact);
            if (destination.Size() != previous + 1u)
            {
                destination.Clear();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] u64 ArtifactByteCount(const containers::ArraySpan<const Artifact> artifacts) noexcept
    {
        u64 bytes = 0;
        for (const Artifact& artifact : artifacts)
        {
            bytes += artifact.bytes.Size();
        }
        return bytes;
    }

    [[nodiscard]] BuildFingerprint ComputeBuildFingerprint(const BuildRequest& request, const CompilerDescriptor& compiler,
                                                           const containers::ArraySpan<const BuildDependency> dependencies) noexcept
    {
        constexpr char Domain[] = "vanguard.asset-build.v1";
        crypto::Sha256Builder hash;
        static_cast<void>(hash.Update(Domain, sizeof(Domain) - 1u));
        HashU64(hash, request.source.identity.GetPath().Id());
        HashU32(hash, request.source.identity.ExpectedType());
        HashU64(hash, request.output.GetPath().Id());
        HashU32(hash, request.output.ExpectedType());
        HashU64(hash, compiler.id);
        HashU32(hash, compiler.version);
        HashU8(hash, static_cast<u8>(request.target));
        HashDigest(hash, crypto::Sha256(request.source.content.Data(), request.source.content.SizeInBytes()));
        HashDigest(hash, crypto::Sha256(request.source.metadata.Data(), request.source.metadata.SizeInBytes()));
        HashDigest(hash, crypto::Sha256(request.settings.Data(), request.settings.SizeInBytes()));
        HashU32(hash, dependencies.Count());
        for (const BuildDependency& dependency : dependencies)
        {
            HashU8(hash, static_cast<u8>(dependency.role));
            HashU8(hash, static_cast<u8>(dependency.requirement));
            HashU64(hash, dependency.identity.GetPath().Id());
            HashU32(hash, dependency.identity.ExpectedType());
            HashDigest(hash, dependency.content);
        }
        BuildFingerprint result;
        static_cast<void>(hash.Finalize(result));
        return result;
    }

    [[nodiscard]] BuildFingerprint ComputeArtifactFingerprint(const containers::ArraySpan<const Artifact> artifacts) noexcept
    {
        detail::ArtifactSetFingerprintBuilder fingerprint(artifacts.Count());
        for (const Artifact& artifact : artifacts)
        {
            static_cast<void>(fingerprint.AddDescriptor(artifact.resource, artifact.segment, artifact.flags,
                                                        artifact.alignmentLog2, artifact.bytes.Size()));
            static_cast<void>(fingerprint.AddBytes(artifact.bytes.Data(), artifact.bytes.Size()));
        }
        BuildFingerprint result;
        static_cast<void>(fingerprint.Finalize(result));
        return result;
    }

    [[nodiscard]] bool WritePersistentRecord(const filesystem::AbsolutePath& path, const BuildOutput& output) noexcept
    {
        u64 payloadBytes = 0;
        for (const Artifact& artifact : output.artifacts)
        {
            if (artifact.bytes.Size() > ~u64{0} - payloadBytes)
            {
                return false;
            }
            payloadBytes += artifact.bytes.Size();
        }
        const u64 descriptorBytes = static_cast<u64>(output.artifacts.Size()) * detail::PersistentArtifactDescriptorSize;
        if (payloadBytes > ~u64{0} - detail::PersistentCacheHeaderSize - descriptorBytes)
        {
            return false;
        }
        const u64 fileSize = detail::PersistentCacheHeaderSize + descriptorBytes + payloadBytes;

        auto file = filesystem::GetManager().CreateFileWriter(path, filesystem::FOF_Buffered);
        if (!file)
        {
            return false;
        }
        vanguard::serialization::BinaryWriter writer(*file);
        bool written = writer.WriteU32(detail::PersistentCacheMagic) && writer.WriteU16(detail::PersistentCacheMajorVersion) &&
                       writer.WriteU16(detail::PersistentCacheMinorVersion) && writer.WriteU32(detail::PersistentCacheHeaderSize) &&
                       writer.WriteU32(output.artifacts.Size()) && writer.WriteU64(fileSize) &&
                       writer.WriteBytes(output.buildFingerprint.bytes, BuildFingerprint::ByteCount) &&
                       writer.WriteBytes(output.contentFingerprint.bytes, BuildFingerprint::ByteCount) && writer.WriteU64(payloadBytes);

        for (const Artifact& artifact : output.artifacts)
        {
            written = written && writer.WriteU64(artifact.resource.GetPath().Id()) && writer.WriteU32(artifact.resource.ExpectedType()) &&
                      writer.WriteU32(artifact.segment) && writer.WriteU16(static_cast<u16>(artifact.flags)) && writer.WriteU8(artifact.alignmentLog2) &&
                      writer.WriteU8(0) && writer.WriteU64(artifact.bytes.Size()) && writer.WriteU32(0);
        }
        for (const Artifact& artifact : output.artifacts)
        {
            written = written && writer.WriteBytes(artifact.bytes.Data(), artifact.bytes.Size());
        }
        written = written && writer.Position() == fileSize && writer.Flush();
        file.Reset();
        return written;
    }

} // namespace

namespace vanguard::assets
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::CompilerNotFound:
            return "CompilerNotFound";
        case Result::CompilerAlreadyRegistered:
            return "CompilerAlreadyRegistered";
        case Result::CompilerBusy:
            return "CompilerBusy";
        case Result::DependencyDiscoveryFailed:
            return "DependencyDiscoveryFailed";
        case Result::ResourceEstimationFailed:
            return "ResourceEstimationFailed";
        case Result::DuplicateDependency:
            return "DuplicateDependency";
        case Result::CompileFailed:
            return "CompileFailed";
        case Result::InvalidArtifact:
            return "InvalidArtifact";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::OutOfMemory:
            return "OutOfMemory";
        case Result::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    BuildPlan::BuildPlan() noexcept : m_dependencies(memory::pools::Assets::GetInstance()) {}

    void BuildPlan::Reset() noexcept
    {
        m_compiler = InvalidCompilerId;
        m_compilerVersion = 0;
        m_sourceType = resources::InvalidResourceTypeId;
        m_outputType = resources::InvalidResourceTypeId;
        m_resourceEstimate = {};
        m_hasResourceEstimate = false;
        m_dependencies.Clear();
    }

    bool BuildPlan::IsPrepared() const noexcept
    {
        return m_compiler != InvalidCompilerId && m_compilerVersion != 0 && m_sourceType != resources::InvalidResourceTypeId &&
               m_outputType != resources::InvalidResourceTypeId;
    }

    CompilerId BuildPlan::Compiler() const noexcept
    {
        return m_compiler;
    }

    u32 BuildPlan::CompilerVersion() const noexcept
    {
        return m_compilerVersion;
    }

    resources::ResourceTypeId BuildPlan::GetSourceType() const noexcept
    {
        return m_sourceType;
    }

    resources::ResourceTypeId BuildPlan::GetOutputType() const noexcept
    {
        return m_outputType;
    }

    containers::ArraySpan<const BuildDependency> BuildPlan::GetDependencies() const noexcept
    {
        return {m_dependencies.TypedData(), m_dependencies.Size()};
    }

    bool BuildPlan::HasResourceEstimate() const noexcept
    {
        return m_hasResourceEstimate;
    }

    const BuildResourceEstimate& BuildPlan::GetResourceEstimate() const noexcept
    {
        return m_resourceEstimate;
    }

    Result BuildPlan::SetGeneratedDependencyContent(const resources::ResourceReference dependency, const BuildFingerprint& content) noexcept
    {
        if (!dependency.IsValid() || !dependency.IsTyped() || content.IsEmpty())
        {
            return Result::InvalidArgument;
        }
        for (BuildDependency& existing : m_dependencies)
        {
            if (existing.role == DependencyRole::Generated && existing.identity == dependency)
            {
                if (existing.requirement == DependencyRequirement::Soft)
                {
                    return Result::InvalidArgument;
                }
                existing.content = content;
                return Result::Success;
            }
        }
        return Result::InvalidArgument;
    }

    DependencyCollector::DependencyCollector() noexcept : m_dependencies(memory::pools::Assets::GetInstance()) {}

    Result DependencyCollector::Add(const BuildDependency& dependency) noexcept
    {
        if (m_status != Result::Success)
        {
            return m_status;
        }
        if (!dependency.IsValid())
        {
            m_status = Result::InvalidArgument;
            return m_status;
        }
        for (const BuildDependency& existing : m_dependencies)
        {
            if (existing.role == dependency.role && existing.identity == dependency.identity)
            {
                m_status = Result::DuplicateDependency;
                return m_status;
            }
        }
        const u32 previous = m_dependencies.Size();
        m_dependencies.PushBack(dependency);
        if (m_dependencies.Size() != previous + 1u)
        {
            m_status = Result::OutOfMemory;
        }
        return m_status;
    }

    u32 DependencyCollector::Count() const noexcept
    {
        return m_dependencies.Size();
    }

    containers::ArraySpan<const BuildDependency> DependencyCollector::GetDependencies() const noexcept
    {
        return {m_dependencies.TypedData(), m_dependencies.Size()};
    }

    Result DependencyCollector::GetStatus() const noexcept
    {
        return m_status;
    }

    Artifact::Artifact() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

    const ArtifactView* GeneratedDependencyView::Find(const resources::ResourceReference resource, const u32 segment) const noexcept
    {
        for (const ArtifactView& artifact : artifacts)
        {
            if (artifact.resource == resource && artifact.segment == segment)
            {
                return &artifact;
            }
        }
        return nullptr;
    }

    BuildReport::BuildReport() noexcept
        : m_diagnostics(memory::pools::Assets::GetInstance()), m_locations(memory::pools::Assets::GetInstance()),
          m_messages(memory::pools::Assets::GetInstance())
    {
    }

    void BuildReport::Reset() noexcept
    {
        m_diagnostics.Clear();
        m_locations.Clear();
        m_messages.Clear();
    }

    bool BuildReport::CopyFrom(const BuildReport& report) noexcept
    {
        if (this == &report)
        {
            return true;
        }
        Reset();
        m_diagnostics.Resize(report.m_diagnostics.Size());
        m_locations.Resize(report.m_locations.Size());
        m_messages.Resize(report.m_messages.Size());
        if (m_diagnostics.Size() != report.m_diagnostics.Size() || m_locations.Size() != report.m_locations.Size() ||
            m_messages.Size() != report.m_messages.Size())
        {
            Reset();
            return false;
        }
        for (u32 index = 0; index < m_diagnostics.Size(); ++index)
        {
            m_diagnostics[index] = report.m_diagnostics[index];
        }
        for (u32 index = 0; index < m_locations.Size(); ++index)
        {
            m_locations[index] = report.m_locations[index];
        }
        for (u32 index = 0; index < m_messages.Size(); ++index)
        {
            m_messages[index] = report.m_messages[index];
        }
        return true;
    }

    containers::ArraySpan<const BuildDiagnostic> BuildReport::GetDiagnostics() const noexcept
    {
        return {m_diagnostics.TypedData(), m_diagnostics.Size()};
    }

    containers::ArraySpan<const BuildDiagnosticLocation> BuildReport::GetLocations(const BuildDiagnostic& diagnostic) const noexcept
    {
        if (diagnostic.firstLocation > m_locations.Size() || diagnostic.locationCount > m_locations.Size() - diagnostic.firstLocation)
        {
            return {};
        }
        if (diagnostic.locationCount == 0)
        {
            return {};
        }
        return {m_locations.TypedData() + diagnostic.firstLocation, diagnostic.locationCount};
    }

    containers::StringView BuildReport::GetMessage(const BuildDiagnostic& diagnostic) const noexcept
    {
        if (diagnostic.messageOffset > m_messages.Size() || diagnostic.messageLength > m_messages.Size() - diagnostic.messageOffset)
        {
            return {};
        }
        if (diagnostic.messageLength == 0)
        {
            return {};
        }
        return {m_messages.TypedData() + diagnostic.messageOffset, diagnostic.messageLength};
    }

    BuildReportWriter::BuildReportWriter(BuildReport& report, const BuildReportLimits& limits) noexcept : m_report(&report), m_limits(limits)
    {
        report.Reset();
        if (!limits.IsValid())
        {
            m_status = Result::InvalidArgument;
        }
    }

    Result BuildReportWriter::Add(const BuildDiagnosticDescription& diagnostic) noexcept
    {
        if (m_status != Result::Success)
        {
            return m_status;
        }
        if (diagnostic.severity > BuildDiagnosticSeverity::Error || diagnostic.code == 0 || diagnostic.message.Empty())
        {
            m_status = Result::InvalidArgument;
            return m_status;
        }
        const u32 messageLength = diagnostic.message.Length();
        if (m_report->m_diagnostics.Size() >= m_limits.maximumDiagnostics ||
            diagnostic.locations.Count() > m_limits.maximumLocations - m_report->m_locations.Size() ||
            messageLength > m_limits.maximumMessageBytes - m_report->m_messages.Size())
        {
            m_status = Result::LimitExceeded;
            return m_status;
        }

        const u32 firstLocation = m_report->m_locations.Size();
        const u32 firstMessage = m_report->m_messages.Size();
        m_report->m_locations.Resize(firstLocation + diagnostic.locations.Count());
        m_report->m_messages.Resize(firstMessage + messageLength);
        const u32 diagnosticIndex = m_report->m_diagnostics.Size();
        m_report->m_diagnostics.Resize(diagnosticIndex + 1u);
        if (m_report->m_locations.Size() != firstLocation + diagnostic.locations.Count() ||
            m_report->m_messages.Size() != firstMessage + messageLength || m_report->m_diagnostics.Size() != diagnosticIndex + 1u)
        {
            m_report->m_locations.Resize(firstLocation);
            m_report->m_messages.Resize(firstMessage);
            m_report->m_diagnostics.Resize(diagnosticIndex);
            m_status = Result::OutOfMemory;
            return m_status;
        }
        for (u32 index = 0; index < diagnostic.locations.Count(); ++index)
        {
            m_report->m_locations[firstLocation + index] = diagnostic.locations[index];
        }
        for (u32 index = 0; index < messageLength; ++index)
        {
            m_report->m_messages[firstMessage + index] = diagnostic.message.Data()[index];
        }
        m_report->m_diagnostics[diagnosticIndex] =
            {diagnostic.severity, diagnostic.code, firstLocation, diagnostic.locations.Count(), firstMessage, messageLength};
        return Result::Success;
    }

    Result BuildReportWriter::GetStatus() const noexcept
    {
        return m_status;
    }

    const GeneratedDependencyView* CompileContext::FindGeneratedDependency(const resources::ResourceReference dependency) const noexcept
    {
        for (const GeneratedDependencyView& resolved : generatedDependencies)
        {
            if (resolved.dependency.identity == dependency)
            {
                return &resolved;
            }
        }
        return nullptr;
    }

    ArtifactWriter::ArtifactWriter(const resources::ResourceReference primaryOutput, const u32 maximumArtifacts, const u64 maximumBytes) noexcept
        : m_primaryOutput(primaryOutput), m_artifacts(memory::pools::Assets::GetInstance()), m_maximumArtifacts(maximumArtifacts), m_maximumBytes(maximumBytes)
    {
    }

    Result ArtifactWriter::Add(const resources::ResourceReference resource, const u32 segment, const ArtifactFlags flags, const u8 alignmentLog2,
                               const void* const data, const usize size) noexcept
    {
        if (m_status != Result::Success)
        {
            return m_status;
        }
        const bool primary = HasFlag(flags, ArtifactFlags::Primary);
        if (!resource.IsValid() || !resource.IsTyped() || size == 0 || data == nullptr || size > static_cast<usize>(~u32{0}) || alignmentLog2 > 20 ||
            (static_cast<u16>(flags) & ~KnownArtifactFlags) != 0 || (primary && resource != m_primaryOutput))
        {
            m_status = Result::InvalidArtifact;
            return m_status;
        }
        if (m_artifacts.Size() >= m_maximumArtifacts || size > m_maximumBytes - m_bytes)
        {
            m_status = Result::LimitExceeded;
            return m_status;
        }
        for (const Artifact& existing : m_artifacts)
        {
            if (existing.resource == resource && existing.segment == segment)
            {
                m_status = Result::InvalidArtifact;
                return m_status;
            }
        }

        const u32 previous = m_artifacts.Size();
        m_artifacts.Resize(previous + 1u);
        if (m_artifacts.Size() != previous + 1u)
        {
            m_status = Result::OutOfMemory;
            return m_status;
        }
        Artifact& artifact = m_artifacts.Back();
        artifact.resource = resource;
        artifact.segment = segment;
        artifact.flags = flags;
        artifact.alignmentLog2 = alignmentLog2;
        artifact.bytes.Resize(static_cast<u32>(size));
        if (artifact.bytes.Size() != size)
        {
            static_cast<void>(m_artifacts.RemoveAt(previous));
            m_status = Result::OutOfMemory;
            return m_status;
        }
        const auto* const source = static_cast<const u8*>(data);
        for (u32 index = 0; index < artifact.bytes.Size(); ++index)
        {
            artifact.bytes[index] = source[index];
        }
        m_bytes += size;
        m_hasPrimary |= primary;
        return Result::Success;
    }

    u32 ArtifactWriter::Count() const noexcept
    {
        return m_artifacts.Size();
    }

    u64 ArtifactWriter::GetByteCount() const noexcept
    {
        return m_bytes;
    }

    u32 ArtifactWriter::GetMaximumArtifactCount() const noexcept
    {
        return m_maximumArtifacts;
    }

    u32 ArtifactWriter::GetRemainingArtifactCount() const noexcept
    {
        return m_artifacts.Size() < m_maximumArtifacts ? m_maximumArtifacts - m_artifacts.Size() : 0;
    }

    u64 ArtifactWriter::GetMaximumByteCount() const noexcept
    {
        return m_maximumBytes;
    }

    u64 ArtifactWriter::GetRemainingByteCount() const noexcept
    {
        return m_bytes < m_maximumBytes ? m_maximumBytes - m_bytes : 0;
    }

    containers::ArraySpan<const Artifact> ArtifactWriter::GetArtifacts() const noexcept
    {
        return {m_artifacts.TypedData(), m_artifacts.Size()};
    }

    Result ArtifactWriter::GetStatus() const noexcept
    {
        return m_status;
    }

    bool ArtifactWriter::HasPrimaryOutput() const noexcept
    {
        return m_hasPrimary;
    }

    CompilerId HashCompilerName(const containers::StringView name) noexcept
    {
        if (name.Empty())
        {
            return InvalidCompilerId;
        }
        constexpr u64 OffsetBasis = 14695981039346656037ull;
        constexpr u64 Prime = 1099511628211ull;
        u64 hash = OffsetBasis;
        bool previousDot = false;
        for (const char byte : name)
        {
            const bool valid = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '.';
            if (!valid || (byte == '.' && previousDot))
            {
                return InvalidCompilerId;
            }
            hash ^= static_cast<u8>(byte);
            hash *= Prime;
            previousDot = byte == '.';
        }
        return previousDot || hash == InvalidCompilerId ? InvalidCompilerId : hash;
    }

    BuildOutput::BuildOutput() noexcept : artifacts(memory::pools::Assets::GetInstance()) {}

    void BuildOutput::Reset() noexcept
    {
        disposition = BuildDisposition::Built;
        buildFingerprint = {};
        contentFingerprint = {};
        artifacts.Clear();
    }

    struct BuildSystem::Impl
    {
        VANGUARD_USE_MEMORY_POOL(memory::pools::Assets);

        struct CompilerRecord
        {
            CompilerDescriptor descriptor;
            u32 activeBuilds = 0;
        };

        struct CacheEntry
        {
            CacheEntry() noexcept : artifacts(memory::pools::Assets::GetInstance()) {}

            BuildFingerprint key;
            BuildFingerprint content;
            containers::DynamicArray<Artifact> artifacts;
            u64 bytes = 0;
            u64 sequence = 0;
        };

        explicit Impl(const Config& value) noexcept
            : config(value), compilers(memory::pools::Assets::GetInstance()), cache(memory::pools::Assets::GetInstance())
        {
        }

        Config config;
        filesystem::AbsolutePath persistentRoot;
        DerivedDataArtifactSource persistentArtifacts;
        mutable concurrency::RWSpinLock lock;
        containers::DynamicArray<CompilerRecord> compilers;
        containers::DynamicArray<CacheEntry> cache;
        Stats stats;
        u64 nextSequence = 1;

        [[nodiscard]] bool InitializePersistentCache() noexcept
        {
            if (config.persistentCacheRoot == nullptr)
            {
                return true;
            }
            persistentRoot = filesystem::AbsolutePath::ParseDirPath(config.persistentCacheRoot);
            config.persistentCacheRoot = nullptr;
            filesystem::Manager& manager = filesystem::GetManager();
            if (persistentRoot.Empty() || !manager.CreatePath(persistentRoot))
            {
                return false;
            }
            DerivedDataArtifactSourceConfig sourceConfig;
            sourceConfig.root = persistentRoot.AsChar();
            sourceConfig.limits.maximumArtifacts = config.maximumArtifactsPerBuild;
            sourceConfig.limits.maximumArtifactBytes = config.maximumArtifactBytesPerBuild;
            if (!persistentArtifacts.Initialize(sourceConfig))
            {
                return false;
            }

            containers::DynamicArray<filesystem::AbsolutePath> staleFiles(memory::pools::Assets::GetInstance());
            manager.FindFiles(persistentRoot, containers::String("*.tmp"), staleFiles, true);
            u64 recovered = 0;
            for (const filesystem::AbsolutePath& stale : staleFiles)
            {
                if (manager.DeleteFile(stale))
                {
                    ++recovered;
                }
            }
            stats.persistentCacheRecoveries += recovered;
            return true;
        }

        [[nodiscard]] bool LookupPersistentCache(const BuildFingerprint& key, BuildOutput& output) noexcept
        {
            if (persistentRoot.Empty())
            {
                return false;
            }
            const BuildFingerprint requestedKey = key;
            const filesystem::AbsolutePath path = detail::PersistentRecordPath(persistentRoot, requestedKey);
            ArtifactSetReader reader;
            DerivedDataArtifactResult result = persistentArtifacts.Open({requestedKey, {}}, reader);
            if (result == DerivedDataArtifactResult::Success)
                result = reader.ReadAll(output);
            if (result != DerivedDataArtifactResult::Success)
            {
                output.Reset();
                output.buildFingerprint = requestedKey;
            }

            lock.Acquire();
            switch (result)
            {
            case DerivedDataArtifactResult::Success:
                ++stats.persistentCacheHits;
                break;
            case DerivedDataArtifactResult::NotFound:
                ++stats.persistentCacheMisses;
                break;
            case DerivedDataArtifactResult::Corrupt:
            case DerivedDataArtifactResult::ContentMismatch:
                ++stats.persistentCacheCorruptions;
                break;
            case DerivedDataArtifactResult::InvalidArgument:
            case DerivedDataArtifactResult::InvalidState:
            case DerivedDataArtifactResult::DescriptorMismatch:
            case DerivedDataArtifactResult::IoFailure:
            case DerivedDataArtifactResult::OutOfMemory:
            case DerivedDataArtifactResult::LimitExceeded:
                ++stats.persistentCacheIoFailures;
                break;
            }
            lock.Release();

            if (result == DerivedDataArtifactResult::Corrupt || result == DerivedDataArtifactResult::ContentMismatch)
            {
                static_cast<void>(filesystem::GetManager().DeleteFile(path));
            }
            return result == DerivedDataArtifactResult::Success;
        }

        void StorePersistentCache(const BuildOutput& output) noexcept
        {
            if (persistentRoot.Empty())
            {
                return;
            }
            filesystem::Manager& manager = filesystem::GetManager();
            const filesystem::AbsolutePath directory = detail::PersistentRecordDirectory(persistentRoot, output.buildFingerprint);
            const filesystem::AbsolutePath target = detail::PersistentRecordPath(persistentRoot, output.buildFingerprint);
            if (!manager.CreatePath(directory))
            {
                RecordPersistentStore(false);
                return;
            }

            if (manager.FileExist(target))
            {
                ArtifactSetReader existing;
                const DerivedDataArtifactResult existingResult =
                    persistentArtifacts.Open({output.buildFingerprint, output.contentFingerprint}, existing);
                if (existingResult == DerivedDataArtifactResult::Success)
                {
                    return;
                }
                if (existingResult != DerivedDataArtifactResult::Corrupt &&
                    existingResult != DerivedDataArtifactResult::ContentMismatch)
                {
                    RecordPersistentStore(false);
                    return;
                }
                if (!manager.DeleteFile(target))
                {
                    RecordPersistentStore(false);
                    return;
                }
            }

            const filesystem::AbsolutePath generated = manager.GenerateTemporaryFilePath();
            const filesystem::AbsolutePath temporary = directory.AddFilePath(filesystem::paths::GetFileName(generated));
            if (!WritePersistentRecord(temporary, output))
            {
                static_cast<void>(manager.DeleteFile(temporary));
                RecordPersistentStore(false);
                return;
            }

            DerivedDataArtifactLimits limits;
            limits.maximumArtifacts = config.maximumArtifactsPerBuild;
            limits.maximumArtifactBytes = config.maximumArtifactBytesPerBuild;
            const DerivedDataArtifactResult validationResult = detail::ValidatePersistentRecord(
                temporary, {output.buildFingerprint, output.contentFingerprint}, limits);
            if (validationResult != DerivedDataArtifactResult::Success)
            {
                static_cast<void>(manager.DeleteFile(temporary));
                RecordPersistentStore(false);
                return;
            }

            const bool moved = filesystem::SystemIO::MoveFile(temporary.AsChar(), target.AsChar());
            bool published = moved;
            if (!moved && manager.FileExist(target))
            {
                ArtifactSetReader raced;
                published = persistentArtifacts.Open({output.buildFingerprint, output.contentFingerprint}, raced) ==
                            DerivedDataArtifactResult::Success;
            }
            if (!moved)
            {
                static_cast<void>(manager.DeleteFile(temporary));
            }
            RecordPersistentStore(published);
        }

        void RecordPersistentStore(const bool success) noexcept
        {
            lock.Acquire();
            if (success)
            {
                ++stats.persistentCacheStores;
            }
            else
            {
                ++stats.persistentCacheIoFailures;
            }
            lock.Release();
        }

        [[nodiscard]] Result AcquireCompiler(const BuildRequest& request, CompilerDescriptor& descriptor) noexcept
        {
            lock.Acquire();
            for (CompilerRecord& compiler : compilers)
            {
                if (compiler.descriptor.sourceType == request.source.identity.ExpectedType() && compiler.descriptor.outputType == request.output.ExpectedType())
                {
                    ++compiler.activeBuilds;
                    ++stats.activeBuilds;
                    descriptor = compiler.descriptor;
                    lock.Release();
                    return Result::Success;
                }
            }
            lock.Release();
            return Result::CompilerNotFound;
        }

        void ReleaseCompiler(const CompilerId id) noexcept
        {
            lock.Acquire();
            for (CompilerRecord& compiler : compilers)
            {
                if (compiler.descriptor.id == id)
                {
                    if (compiler.activeBuilds != 0)
                    {
                        --compiler.activeBuilds;
                    }
                    break;
                }
            }
            if (stats.activeBuilds != 0)
            {
                --stats.activeBuilds;
            }
            lock.Release();
        }

        [[nodiscard]] bool LookupCache(const BuildFingerprint& key, BuildOutput& output) noexcept
        {
            lock.AcquireShared();
            for (const CacheEntry& entry : cache)
            {
                if (entry.key == key)
                {
                    output.buildFingerprint = entry.key;
                    output.contentFingerprint = entry.content;
                    output.disposition = BuildDisposition::CacheHit;
                    const bool copied = CopyArtifacts({entry.artifacts.TypedData(), entry.artifacts.Size()}, output.artifacts);
                    lock.ReleaseShared();
                    return copied;
                }
            }
            lock.ReleaseShared();
            return false;
        }

        void StoreCache(const BuildOutput& output) noexcept
        {
            const u64 bytes = ArtifactByteCount({output.artifacts.TypedData(), output.artifacts.Size()});
            if (config.maximumCacheEntries == 0 || bytes > config.maximumCacheBytes)
            {
                return;
            }

            lock.Acquire();
            for (CacheEntry& entry : cache)
            {
                if (entry.key == output.buildFingerprint)
                {
                    stats.cacheBytes -= entry.bytes;
                    entry.content = output.contentFingerprint;
                    entry.bytes = bytes;
                    static_cast<void>(CopyArtifacts({output.artifacts.TypedData(), output.artifacts.Size()}, entry.artifacts));
                    stats.cacheBytes += entry.bytes;
                    lock.Release();
                    return;
                }
            }

            while (!cache.Empty() && (cache.Size() >= config.maximumCacheEntries || bytes > config.maximumCacheBytes - stats.cacheBytes))
            {
                u32 oldest = 0;
                for (u32 index = 1; index < cache.Size(); ++index)
                {
                    if (cache[index].sequence < cache[oldest].sequence)
                    {
                        oldest = index;
                    }
                }
                stats.cacheBytes -= cache[oldest].bytes;
                static_cast<void>(cache.RemoveAt(oldest));
                ++stats.cacheEvictions;
            }

            const u32 previous = cache.Size();
            cache.Resize(previous + 1u);
            if (cache.Size() == previous + 1u)
            {
                CacheEntry& entry = cache.Back();
                entry.key = output.buildFingerprint;
                entry.content = output.contentFingerprint;
                entry.bytes = bytes;
                entry.sequence = nextSequence++;
                if (CopyArtifacts({output.artifacts.TypedData(), output.artifacts.Size()}, entry.artifacts))
                {
                    stats.cacheBytes += bytes;
                    ++stats.cacheStores;
                }
                else
                {
                    static_cast<void>(cache.RemoveAt(previous));
                }
            }
            stats.cacheEntries = cache.Size();
            lock.Release();
        }

        void RecordResult(const bool cacheHit, const bool success) noexcept
        {
            lock.Acquire();
            ++stats.buildRequests;
            if (success)
            {
                if (cacheHit)
                {
                    ++stats.cacheHits;
                }
                else
                {
                    ++stats.cacheMisses;
                    ++stats.localBuilds;
                }
            }
            else
            {
                ++stats.failedBuilds;
            }
            stats.cacheEntries = cache.Size();
            lock.Release();
        }
    };

    namespace
    {
        class CompilerLease final
        {
        public:
            CompilerLease(BuildSystem::Impl& implementation, const CompilerId compiler) noexcept : m_implementation(&implementation), m_compiler(compiler) {}

            ~CompilerLease()
            {
                m_implementation->ReleaseCompiler(m_compiler);
            }

            CompilerLease(const CompilerLease&) = delete;
            CompilerLease& operator=(const CompilerLease&) = delete;

        private:
            BuildSystem::Impl* m_implementation;
            CompilerId m_compiler;
        };
    } // namespace

    BuildSystem::~BuildSystem()
    {
        static_cast<void>(Shutdown());
    }

    bool BuildSystem::Initialize(const Config& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!memory::IsInitialized() || config.maximumCompilers == 0 || config.maximumDependenciesPerBuild == 0 || config.maximumArtifactsPerBuild == 0 ||
            config.maximumArtifactBytesPerBuild == 0 || !config.reportLimits.IsValid() || (config.maximumCacheEntries != 0 && config.maximumCacheBytes == 0) ||
            (config.persistentCacheRoot != nullptr &&
             (!filesystem::IsInitialized() || config.persistentCacheRoot[0] == '\0' || !filesystem::AbsolutePath::IsValidPath(config.persistentCacheRoot) ||
              !filesystem::paths::IsAbsolutePath(config.persistentCacheRoot))))
        {
            return false;
        }
        m_impl = VANGUARD_NEW(Impl)(config);
        if (m_impl == nullptr)
        {
            return false;
        }
        if (!m_impl->InitializePersistentCache())
        {
            VANGUARD_DELETE(m_impl);
            m_impl = nullptr;
            return false;
        }
        return true;
    }

    bool BuildSystem::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.AcquireShared();
        const bool busy = m_impl->stats.activeBuilds != 0;
        m_impl->lock.ReleaseShared();
        if (busy)
        {
            return false;
        }
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool BuildSystem::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    Result BuildSystem::RegisterCompiler(const CompilerDescriptor& compiler) noexcept
    {
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!compiler.IsValid() || HashCompilerName(compiler.name) != compiler.id)
        {
            return Result::InvalidArgument;
        }

        m_impl->lock.Acquire();
        if (m_impl->compilers.Size() >= m_impl->config.maximumCompilers)
        {
            m_impl->lock.Release();
            return Result::LimitExceeded;
        }
        for (const Impl::CompilerRecord& existing : m_impl->compilers)
        {
            if (existing.descriptor.id == compiler.id ||
                (existing.descriptor.sourceType == compiler.sourceType && existing.descriptor.outputType == compiler.outputType))
            {
                m_impl->lock.Release();
                return Result::CompilerAlreadyRegistered;
            }
        }
        const u32 previous = m_impl->compilers.Size();
        m_impl->compilers.PushBack({compiler, 0});
        if (m_impl->compilers.Size() != previous + 1u)
        {
            m_impl->lock.Release();
            return Result::OutOfMemory;
        }
        m_impl->stats.registeredCompilers = m_impl->compilers.Size();
        m_impl->lock.Release();
        return Result::Success;
    }

    Result BuildSystem::UnregisterCompiler(const CompilerId compiler) noexcept
    {
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        m_impl->lock.Acquire();
        for (u32 index = 0; index < m_impl->compilers.Size(); ++index)
        {
            const Impl::CompilerRecord& record = m_impl->compilers[index];
            if (record.descriptor.id == compiler)
            {
                if (record.activeBuilds != 0)
                {
                    m_impl->lock.Release();
                    return Result::CompilerBusy;
                }
                static_cast<void>(m_impl->compilers.RemoveAt(index));
                m_impl->stats.registeredCompilers = m_impl->compilers.Size();
                m_impl->lock.Release();
                return Result::Success;
            }
        }
        m_impl->lock.Release();
        return Result::CompilerNotFound;
    }

    Result BuildSystem::Build(const BuildRequest& request, BuildOutput& output, BuildReport* const report) noexcept
    {
        output.Reset();
        if (report != nullptr)
        {
            report->Reset();
        }
        BuildPlan plan;
        const Result prepared = Prepare(request, plan);
        if (prepared != Result::Success)
        {
            if (m_impl != nullptr)
            {
                m_impl->RecordResult(false, false);
            }
            return prepared;
        }
        return Execute(request, plan, output, nullptr, nullptr, {}, report);
    }

    Result BuildSystem::Prepare(const BuildRequest& request, BuildPlan& plan) noexcept
    {
        plan.Reset();
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!request.IsValid())
        {
            return Result::InvalidArgument;
        }

        CompilerDescriptor compiler;
        Result result = m_impl->AcquireCompiler(request, compiler);
        if (result != Result::Success)
        {
            return result;
        }
        CompilerLease lease(*m_impl, compiler.id);

        DependencyCollector collector;
        if (!compiler.discoverDependencies(request, collector, compiler.userData))
        {
            return Result::DependencyDiscoveryFailed;
        }
        if (collector.GetStatus() != Result::Success)
        {
            return collector.GetStatus();
        }
        if (collector.Count() > m_impl->config.maximumDependenciesPerBuild)
        {
            return Result::LimitExceeded;
        }

        plan.m_dependencies.Reserve(collector.Count());
        if (plan.m_dependencies.Capacity() < collector.Count())
        {
            return Result::OutOfMemory;
        }
        for (const BuildDependency& dependency : collector.GetDependencies())
        {
            plan.m_dependencies.PushBack(dependency);
        }
        SortDependencies(plan.m_dependencies);
        const containers::ArraySpan<const BuildDependency> sortedDependencies{plan.m_dependencies.TypedData(), plan.m_dependencies.Size()};
        if (compiler.estimateResources != nullptr)
        {
            BuildResourceEstimate estimate;
            if (!compiler.estimateResources(request, sortedDependencies, estimate, compiler.userData) || !estimate.IsValid())
            {
                plan.Reset();
                return Result::ResourceEstimationFailed;
            }
            plan.m_resourceEstimate = estimate;
            plan.m_hasResourceEstimate = true;
        }
        plan.m_compiler = compiler.id;
        plan.m_compilerVersion = compiler.version;
        plan.m_sourceType = compiler.sourceType;
        plan.m_outputType = compiler.outputType;
        return Result::Success;
    }

    Result BuildSystem::Execute(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output, const IsCancellationRequestedFunction cancellation,
                                void* const cancellationUserData, const containers::ArraySpan<const GeneratedDependencyView> generatedDependencies,
                                BuildReport* const report) noexcept
    {
        output.Reset();
        if (report != nullptr)
        {
            report->Reset();
        }
        if (m_impl == nullptr)
        {
            return Result::InvalidState;
        }
        if (!request.IsValid() || !plan.IsPrepared() || plan.m_sourceType != request.source.identity.ExpectedType() ||
            plan.m_outputType != request.output.ExpectedType())
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidArgument;
        }
        for (const BuildDependency& dependency : plan.m_dependencies)
        {
            if (!dependency.IsValid() ||
                (dependency.role == DependencyRole::Generated && dependency.requirement == DependencyRequirement::Required && dependency.content.IsEmpty()))
            {
                m_impl->RecordResult(false, false);
                return Result::InvalidState;
            }
        }

        for (u32 viewIndex = 0; viewIndex < generatedDependencies.Size(); ++viewIndex)
        {
            const GeneratedDependencyView& view = generatedDependencies[viewIndex];
            if (!view.dependency.IsValid() || view.dependency.role != DependencyRole::Generated || view.buildFingerprint.IsEmpty() ||
                view.contentFingerprint.IsEmpty() || view.artifacts.Empty())
            {
                m_impl->RecordResult(false, false);
                return Result::InvalidArgument;
            }
            bool matchesPlan = false;
            for (const BuildDependency& dependency : plan.m_dependencies)
            {
                if (dependency.role == DependencyRole::Generated && dependency.identity == view.dependency.identity)
                {
                    matchesPlan = dependency.content == view.contentFingerprint;
                    break;
                }
            }
            if (!matchesPlan)
            {
                m_impl->RecordResult(false, false);
                return Result::InvalidState;
            }
            for (u32 previous = 0; previous < viewIndex; ++previous)
            {
                if (generatedDependencies[previous].dependency.identity == view.dependency.identity)
                {
                    m_impl->RecordResult(false, false);
                    return Result::InvalidArgument;
                }
            }
            for (const ArtifactView& artifact : view.artifacts)
            {
                if (!artifact.resource.IsValid() || !artifact.resource.IsTyped() || artifact.bytes.Empty() || artifact.alignmentLog2 > 20 ||
                    (static_cast<u16>(artifact.flags) & ~KnownArtifactFlags) != 0)
                {
                    m_impl->RecordResult(false, false);
                    return Result::InvalidArtifact;
                }
            }
        }

        CompilerDescriptor compiler;
        Result result = m_impl->AcquireCompiler(request, compiler);
        if (result != Result::Success)
        {
            m_impl->RecordResult(false, false);
            return result;
        }
        CompilerLease lease(*m_impl, compiler.id);
        if (compiler.id != plan.m_compiler || compiler.version != plan.m_compilerVersion)
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidState;
        }
        if (cancellation != nullptr && cancellation(cancellationUserData))
        {
            m_impl->RecordResult(false, false);
            return Result::Cancelled;
        }

        const containers::ArraySpan<const BuildDependency> sortedDependencies{plan.m_dependencies.TypedData(), plan.m_dependencies.Size()};
        output.buildFingerprint = ComputeBuildFingerprint(request, compiler, sortedDependencies);

        if (m_impl->LookupCache(output.buildFingerprint, output))
        {
            m_impl->RecordResult(true, true);
            return Result::Success;
        }
        if (m_impl->LookupPersistentCache(output.buildFingerprint, output))
        {
            m_impl->StoreCache(output);
            m_impl->RecordResult(true, true);
            return Result::Success;
        }

        ArtifactWriter writer(request.output, m_impl->config.maximumArtifactsPerBuild, m_impl->config.maximumArtifactBytesPerBuild);
        BuildReport discardedReport;
        BuildReportWriter reportWriterStorage{report != nullptr ? *report : discardedReport, m_impl->config.reportLimits};
        BuildReportWriter* const reportWriter = report != nullptr ? &reportWriterStorage : nullptr;
        const CompileContext context{request, sortedDependencies, generatedDependencies, output.buildFingerprint, reportWriter, cancellation,
                                     cancellationUserData};
        if (!compiler.compile(context, writer, compiler.userData))
        {
            m_impl->RecordResult(false, false);
            if (writer.GetStatus() != Result::Success)
            {
                return writer.GetStatus();
            }
            if (reportWriter != nullptr && reportWriter->GetStatus() != Result::Success)
            {
                return reportWriter->GetStatus();
            }
            return cancellation != nullptr && cancellation(cancellationUserData) ? Result::Cancelled : Result::CompileFailed;
        }
        if (cancellation != nullptr && cancellation(cancellationUserData))
        {
            m_impl->RecordResult(false, false);
            return Result::Cancelled;
        }
        if (writer.GetStatus() != Result::Success)
        {
            m_impl->RecordResult(false, false);
            return writer.GetStatus();
        }
        if (reportWriter != nullptr && reportWriter->GetStatus() != Result::Success)
        {
            m_impl->RecordResult(false, false);
            return reportWriter->GetStatus();
        }
        if (writer.Count() == 0 || !writer.HasPrimaryOutput())
        {
            m_impl->RecordResult(false, false);
            return Result::InvalidArtifact;
        }
        if (!CopyArtifacts(writer.GetArtifacts(), output.artifacts))
        {
            m_impl->RecordResult(false, false);
            return Result::OutOfMemory;
        }
        output.disposition = BuildDisposition::Built;
        output.contentFingerprint = ComputeArtifactFingerprint({output.artifacts.TypedData(), output.artifacts.Size()});
        m_impl->StorePersistentCache(output);
        m_impl->StoreCache(output);
        m_impl->RecordResult(false, true);
        return Result::Success;
    }

    void BuildSystem::ClearCache() noexcept
    {
        if (m_impl == nullptr)
        {
            return;
        }
        m_impl->lock.Acquire();
        m_impl->cache.Clear();
        m_impl->stats.cacheEntries = 0;
        m_impl->stats.cacheBytes = 0;
        m_impl->lock.Release();
    }

    Stats BuildSystem::GetStats() const noexcept
    {
        if (m_impl == nullptr)
        {
            return {};
        }
        m_impl->lock.AcquireShared();
        const Stats stats = m_impl->stats;
        m_impl->lock.ReleaseShared();
        return stats;
    }
} // namespace vanguard::assets
