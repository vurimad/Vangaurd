#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/system/cancellation.hpp>

namespace vanguard::assets
{
    using BuildFingerprint = crypto::Digest256;
    using CompilerId = u64;
    using SourceImporterId = u64;

    inline constexpr CompilerId InvalidCompilerId = 0;
    inline constexpr SourceImporterId InvalidSourceImporterId = 0;

    struct ArtifactSetKey
    {
        BuildFingerprint build;
        BuildFingerprint content;

        [[nodiscard]] friend bool operator==(const ArtifactSetKey&, const ArtifactSetKey&) noexcept = default;
        [[nodiscard]] bool IsValid() const noexcept { return !build.IsEmpty(); }
    };

    struct PersistentCacheCoordinationLimits
    {
        u32 maximumRegisteredRoots = 32;
        u32 maximumLiveLeasesPerRoot = 65536;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumRegisteredRoots != 0u && maximumLiveLeasesPerRoot != 0u;
        }

        [[nodiscard]] friend bool operator==(const PersistentCacheCoordinationLimits&,
                                             const PersistentCacheCoordinationLimits&) noexcept = default;
    };

    enum class PersistentCacheMaintenanceResult : u8
    {
        Success,
        Deferred,
        InvalidArgument,
        InvalidState,
        ScanFailed,
        LimitExceeded,
        OutOfMemory,
        ProtectionChanged,
        Cancelled
    };

    struct PersistentCacheMaintenanceLimits
    {
        u64 maximumCacheBytes = 4ull * 1024ull * 1024ull * 1024ull;
        u32 maximumProtectionRecords = 262144;
        u64 maximumProtectionBytes = 64ull * 1024ull * 1024ull;
        u32 maximumScanEntries = 262144;
        u64 maximumScanBytes = 64ull * 1024ull * 1024ull * 1024ull;
        u32 maximumDeleteRecords = 1024;
        u32 maximumDiagnosticRows = 64;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumCacheBytes != 0u && maximumProtectionRecords != 0u && maximumProtectionBytes != 0u &&
                   maximumScanEntries != 0u && maximumScanBytes >= maximumCacheBytes && maximumDeleteRecords != 0u &&
                   maximumDiagnosticRows != 0u;
        }
    };

    enum class PersistentCacheMaintenanceDiagnosticReason : u8
    {
        LeaseBusy,
        InvalidCandidate,
        IoFailure
    };

    struct PersistentCacheMaintenanceDiagnostic
    {
        BuildFingerprint build;
        PersistentCacheMaintenanceDiagnosticReason reason = PersistentCacheMaintenanceDiagnosticReason::LeaseBusy;
    };

    struct PersistentCacheMaintenanceReport
    {
        u32 scannedRecords = 0;
        u64 scannedBytes = 0;
        u32 protectedRecords = 0;
        u64 protectedBytes = 0;
        u32 eligibleRecords = 0;
        u64 eligibleBytes = 0;
        u32 removedRecords = 0;
        u64 removedBytes = 0;
        u32 deferredDeletes = 0;
        u32 failedDeletes = 0;
        u32 foreignFiles = 0;
        u32 stageFiles = 0;
        bool protectedOverBudget = false;
        bool deleteBatchLimitReached = false;
        bool budgetSatisfied = false;
        bool diagnosticsTruncated = false;
    };

    namespace detail
    {
        class PersistentCacheAuthorityAccess;
        struct OwnedBuildRequestSourceBacking;
    }

    class BuildGraph;
    class DependencyIndex;
    class ProjectAssetBuildSystem;

    enum class TargetPlatform : u8
    {
        WindowsD3D12,
        WindowsVulkan,
        LinuxVulkan,
        Count
    };

    enum class DependencyRole : u8
    {
        Source,
        Generated,
        Tool
    };

    enum class DependencyRequirement : u8
    {
        Required,
        Optional,
        /// Records a logical cooked-resource reference without resolving or
        /// building it as part of the requesting operation.
        Soft
    };

    enum class ArtifactFlags : u16
    {
        None = 0,
        Primary = 1u << 0u,
        Streamable = 1u << 1u,
        MemoryResident = 1u << 2u,
        EditorOnly = 1u << 3u
    };

    [[nodiscard]] constexpr ArtifactFlags operator|(const ArtifactFlags left, const ArtifactFlags right) noexcept
    {
        return static_cast<ArtifactFlags>(static_cast<u16>(left) | static_cast<u16>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const ArtifactFlags value, const ArtifactFlags flag) noexcept
    {
        return (static_cast<u16>(value) & static_cast<u16>(flag)) != 0;
    }

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        CompilerNotFound,
        CompilerAlreadyRegistered,
        CompilerBusy,
        DependencyDiscoveryFailed,
        ResourceEstimationFailed,
        DuplicateDependency,
        CompileFailed,
        InvalidArtifact,
        PersistentCacheFailure,
        LimitExceeded,
        OutOfMemory,
        Cancelled
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    struct SourceAsset
    {
        resources::ResourceReference identity;
        containers::ArraySpan<const u8> content;
        containers::ArraySpan<const u8> metadata;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return identity.IsValid() && identity.IsTyped() && (content.Empty() || content.Data() != nullptr) &&
                   (metadata.Empty() || metadata.Data() != nullptr);
        }
    };

    struct BuildRequest
    {
        SourceAsset source;
        resources::ResourceReference output;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        containers::ArraySpan<const u8> settings;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return source.IsValid() && output.IsValid() && output.IsTyped() && target < TargetPlatform::Count &&
                   (settings.Empty() || settings.Data() != nullptr);
        }
    };

    // Ownership-safe form of BuildRequest for resolver boundaries. BuildRequest
    // intentionally remains a lightweight borrowed view; CopyFrom deep-copies
    // every byte span so callers can retain the request after importer/source
    // temporaries have expired. Recook may explicitly promote only the immutable
    // source content to shared backing after ownership has been established.
    struct OwnedBuildRequest
    {
        OwnedBuildRequest() noexcept;

        [[nodiscard]] bool CopyFrom(const BuildRequest& request) noexcept;
        // Recook-only retention optimization. Ordinary resolver boundaries keep
        // CopyFrom's deep-copy semantics; callers opt in only after ownership is
        // already established. Sharing requires exact source identity and bytes.
        [[nodiscard]] bool MakeSourceShared() noexcept;
        [[nodiscard]] bool CopyFromSharingSource(const OwnedBuildRequest& request) noexcept;
        [[nodiscard]] bool CopyFromSharingSource(const BuildRequest& request, const OwnedBuildRequest& sourceOwner) noexcept;
        [[nodiscard]] bool SourceContentEquals(const BuildRequest& request) const noexcept;
        [[nodiscard]] bool SharesSourceStorage(const OwnedBuildRequest& other) const noexcept;
        [[nodiscard]] BuildRequest View() const noexcept;
        [[nodiscard]] u64 ByteCount() const noexcept;
        [[nodiscard]] u64 SourceByteCount() const noexcept;
        [[nodiscard]] u64 PrivateByteCount() const noexcept;
        [[nodiscard]] u64 SharedSourceStorageByteCount() const noexcept;
        [[nodiscard]] static u64 SharedSourceStorageOverhead() noexcept;
        void Reset() noexcept;
        void ReleasePayload() noexcept;

        resources::ResourceReference sourceReference;
        resources::ResourceReference outputReference;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        containers::DynamicArray<u8> source;
        containers::DynamicArray<u8> metadata;
        containers::DynamicArray<u8> settings;

    private:
        struct SharedSourceHandle final
        {
            SharedSourceHandle() noexcept = default;
            SharedSourceHandle(const SharedSourceHandle& other) noexcept;
            SharedSourceHandle& operator=(const SharedSourceHandle& other) noexcept;
            SharedSourceHandle(SharedSourceHandle&& other) noexcept;
            SharedSourceHandle& operator=(SharedSourceHandle&& other) noexcept;
            ~SharedSourceHandle() noexcept;

            void Adopt(detail::OwnedBuildRequestSourceBacking* value) noexcept;
            void Reset() noexcept;

            detail::OwnedBuildRequestSourceBacking* value = nullptr;
        };

        SharedSourceHandle m_sharedSource;
    };

    struct BuildDependency
    {
        resources::ResourceReference identity;
        BuildFingerprint content;
        DependencyRole role = DependencyRole::Source;
        DependencyRequirement requirement = DependencyRequirement::Required;

        [[nodiscard]] bool IsValid() const noexcept
        {
            if (!identity.IsValid() || !identity.IsTyped() || role > DependencyRole::Tool || requirement > DependencyRequirement::Soft)
            {
                return false;
            }
            if (requirement == DependencyRequirement::Soft)
            {
                return role == DependencyRole::Generated && content.IsEmpty();
            }
            return requirement == DependencyRequirement::Optional || role == DependencyRole::Generated || !content.IsEmpty();
        }
    };

    class DependencyCollector final
    {
    public:
        DependencyCollector() noexcept;

        [[nodiscard]] Result Add(const BuildDependency& dependency) noexcept;
        [[nodiscard]] u32 Count() const noexcept;
        [[nodiscard]] containers::ArraySpan<const BuildDependency> GetDependencies() const noexcept;
        [[nodiscard]] Result GetStatus() const noexcept;

    private:
        containers::DynamicArray<BuildDependency> m_dependencies;
        Result m_status = Result::Success;
    };

    enum class AssetReferenceLocation : u8
    {
        Primary,
        Inner
    };

    struct ScannedAssetReference
    {
        resources::ResourceReference target;
        resources::DependencyKind kind = resources::DependencyKind::Required;
        AssetReferenceLocation location = AssetReferenceLocation::Primary;
        bool inPlace = false;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return target.IsValid() && target.IsTyped() && kind <= resources::DependencyKind::Soft && location <= AssetReferenceLocation::Inner;
        }
    };

    class AssetReferenceCollector final
    {
    public:
        AssetReferenceCollector() noexcept;

        [[nodiscard]] Result Add(const ScannedAssetReference& reference) noexcept;
        [[nodiscard]] u32 Count() const noexcept;
        [[nodiscard]] containers::ArraySpan<const ScannedAssetReference> GetReferences() const noexcept;
        [[nodiscard]] Result GetStatus() const noexcept;

    private:
        containers::DynamicArray<ScannedAssetReference> m_references;
        Result m_status = Result::Success;
    };

    struct Artifact
    {
        Artifact() noexcept;

        resources::ResourceReference resource;
        u32 segment = 0;
        ArtifactFlags flags = ArtifactFlags::None;
        u8 alignmentLog2 = 4;
        containers::DynamicArray<u8> bytes;
    };

    struct ArtifactView
    {
        resources::ResourceReference resource;
        u32 segment = 0;
        ArtifactFlags flags = ArtifactFlags::None;
        u8 alignmentLog2 = 4;
        containers::ArraySpan<const u8> bytes;
    };

    struct GeneratedDependencyView
    {
        BuildDependency dependency;
        BuildFingerprint buildFingerprint;
        BuildFingerprint contentFingerprint;
        containers::ArraySpan<const ArtifactView> artifacts;

        [[nodiscard]] const ArtifactView* Find(resources::ResourceReference resource, u32 segment = 0) const noexcept;
    };

    enum class BuildDiagnosticSeverity : u8
    {
        Information,
        Warning,
        Error
    };

    struct BuildDiagnosticLocation
    {
        resources::ResourceReference resource;
        u64 subject = 0;
        u32 detail = 0;
        u32 line = 0;
        u32 column = 0;
        u32 byteOffset = 0;
        u32 byteLength = 0;
    };

    struct BuildDiagnosticDescription
    {
        BuildDiagnosticSeverity severity = BuildDiagnosticSeverity::Error;
        u32 code = 0;
        containers::ArraySpan<const BuildDiagnosticLocation> locations;
        containers::StringView message;
    };

    struct BuildDiagnostic
    {
        BuildDiagnosticSeverity severity = BuildDiagnosticSeverity::Error;
        u32 code = 0;
        u32 firstLocation = 0;
        u32 locationCount = 0;
        u32 messageOffset = 0;
        u32 messageLength = 0;
    };

    struct BuildReportLimits
    {
        u32 maximumDiagnostics = 1024;
        u32 maximumLocations = 8192;
        u32 maximumMessageBytes = 1024u * 1024u;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumDiagnostics != 0 && maximumLocations != 0 && maximumMessageBytes != 0;
        }
    };

    class BuildReport final
    {
    public:
        BuildReport() noexcept;

        void Reset() noexcept;
        [[nodiscard]] bool CopyFrom(const BuildReport& report) noexcept;
        [[nodiscard]] containers::ArraySpan<const BuildDiagnostic> GetDiagnostics() const noexcept;
        [[nodiscard]] containers::ArraySpan<const BuildDiagnosticLocation> GetLocations(const BuildDiagnostic& diagnostic) const noexcept;
        [[nodiscard]] containers::StringView GetMessage(const BuildDiagnostic& diagnostic) const noexcept;

    private:
        containers::DynamicArray<BuildDiagnostic> m_diagnostics;
        containers::DynamicArray<BuildDiagnosticLocation> m_locations;
        containers::DynamicArray<char> m_messages;

        friend class BuildReportWriter;
        friend class BuildGraph;
    };

    class BuildReportWriter final
    {
    public:
        BuildReportWriter(BuildReport& report, const BuildReportLimits& limits) noexcept;

        [[nodiscard]] Result Add(const BuildDiagnosticDescription& diagnostic) noexcept;
        [[nodiscard]] Result GetStatus() const noexcept;

    private:
        BuildReport* m_report = nullptr;
        BuildReportLimits m_limits;
        Result m_status = Result::Success;
    };

    class ArtifactWriter final
    {
    public:
        ArtifactWriter(resources::ResourceReference primaryOutput, u32 maximumArtifacts, u64 maximumBytes) noexcept;

        [[nodiscard]] Result Add(resources::ResourceReference resource, u32 segment, ArtifactFlags flags, u8 alignmentLog2, const void* data,
                                 usize size) noexcept;
        [[nodiscard]] u32 Count() const noexcept;
        [[nodiscard]] u64 GetByteCount() const noexcept;
        [[nodiscard]] u32 GetMaximumArtifactCount() const noexcept;
        [[nodiscard]] u32 GetRemainingArtifactCount() const noexcept;
        [[nodiscard]] u64 GetMaximumByteCount() const noexcept;
        [[nodiscard]] u64 GetRemainingByteCount() const noexcept;
        [[nodiscard]] containers::ArraySpan<const Artifact> GetArtifacts() const noexcept;
        [[nodiscard]] Result GetStatus() const noexcept;
        [[nodiscard]] bool HasPrimaryOutput() const noexcept;

    private:
        resources::ResourceReference m_primaryOutput;
        containers::DynamicArray<Artifact> m_artifacts;
        u32 m_maximumArtifacts = 0;
        u64 m_maximumBytes = 0;
        u64 m_bytes = 0;
        Result m_status = Result::Success;
        bool m_hasPrimary = false;

        friend class BuildSystem;
    };

    using IsCancellationRequestedFunction = system::IsCancellationRequestedFunction;

    struct CompileContext
    {
        const BuildRequest& request;
        containers::ArraySpan<const BuildDependency> dependencies;
        containers::ArraySpan<const GeneratedDependencyView> generatedDependencies;
        BuildFingerprint buildFingerprint;
        BuildReportWriter* report = nullptr;
        IsCancellationRequestedFunction cancellation = nullptr;
        void* cancellationUserData = nullptr;

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return cancellation != nullptr && cancellation(cancellationUserData);
        }

        [[nodiscard]] const GeneratedDependencyView* FindGeneratedDependency(resources::ResourceReference dependency) const noexcept;
    };

    struct BuildResourceEstimate
    {
        /// Peak compiler-owned working memory. This excludes the graph-owned request and
        /// generic ArtifactWriter, BuildOutput, and cache copies.
        u64 compilerTransientBytes = 0;
        /// Total bytes expected to be emitted through ArtifactWriter.
        u64 artifactBytes = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return artifactBytes != 0;
        }
    };

    class BuildPlan final
    {
    public:
        BuildPlan() noexcept;

        void Reset() noexcept;
        [[nodiscard]] bool IsPrepared() const noexcept;
        [[nodiscard]] CompilerId Compiler() const noexcept;
        [[nodiscard]] u32 CompilerVersion() const noexcept;
        [[nodiscard]] resources::ResourceTypeId GetSourceType() const noexcept;
        [[nodiscard]] resources::ResourceTypeId GetOutputType() const noexcept;
        [[nodiscard]] containers::ArraySpan<const BuildDependency> GetDependencies() const noexcept;
        [[nodiscard]] bool HasResourceEstimate() const noexcept;
        [[nodiscard]] const BuildResourceEstimate& GetResourceEstimate() const noexcept;
        [[nodiscard]] Result SetGeneratedDependencyContent(resources::ResourceReference dependency, const BuildFingerprint& content) noexcept;

    private:
        CompilerId m_compiler = InvalidCompilerId;
        u32 m_compilerVersion = 0;
        resources::ResourceTypeId m_sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId m_outputType = resources::InvalidResourceTypeId;
        BuildResourceEstimate m_resourceEstimate;
        bool m_hasResourceEstimate = false;
        containers::DynamicArray<BuildDependency> m_dependencies;

        friend class BuildSystem;
    };

    using DiscoverDependenciesFunction = bool (*)(const BuildRequest& request, DependencyCollector& dependencies, void* userData) noexcept;
    using EstimateBuildResourcesFunction = bool (*)(const BuildRequest& request, containers::ArraySpan<const BuildDependency> dependencies,
                                                    BuildResourceEstimate& estimate, void* userData) noexcept;
    using CompileFunction = bool (*)(const CompileContext& context, ArtifactWriter& artifacts, void* userData) noexcept;
    struct CompilerDescriptor
    {
        CompilerId id = InvalidCompilerId;
        const char* name = nullptr;
        u32 version = 0;
        resources::ResourceTypeId sourceType = resources::InvalidResourceTypeId;
        resources::ResourceTypeId outputType = resources::InvalidResourceTypeId;
        DiscoverDependenciesFunction discoverDependencies = nullptr;
        CompileFunction compile = nullptr;
        void* userData = nullptr;
        /// Optional for direct synchronous BuildSystem use. BuildGraph rejects plans without
        /// an estimate so asynchronous work can never bypass its byte admission limit.
        EstimateBuildResourcesFunction estimateResources = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return id != InvalidCompilerId && name != nullptr && name[0] != '\0' && version != 0 && sourceType != resources::InvalidResourceTypeId &&
                   outputType != resources::InvalidResourceTypeId && discoverDependencies != nullptr && compile != nullptr;
        }
    };

    [[nodiscard]] CompilerId HashCompilerName(containers::StringView name) noexcept;

    enum class BuildDisposition : u8
    {
        Built,
        CacheHit
    };

    enum class BuildOrigin : u8
    {
        Unknown,
        MemoryCacheHit,
        PersistentCacheHit,
        CompilerBuilt
    };

    class BuildOutputProtection final
    {
    public:
        BuildOutputProtection() noexcept = default;
        BuildOutputProtection(const BuildOutputProtection& other) noexcept;
        BuildOutputProtection& operator=(const BuildOutputProtection& other) noexcept;
        BuildOutputProtection(BuildOutputProtection&& other) noexcept;
        BuildOutputProtection& operator=(BuildOutputProtection&& other) noexcept;
        ~BuildOutputProtection();

        [[nodiscard]] bool IsValid() const noexcept { return m_impl != nullptr; }
        void Reset() noexcept;

    private:
        struct Impl;
        Impl* m_impl = nullptr;

        friend class detail::PersistentCacheAuthorityAccess;
    };

    class BuildOutput final
    {
    public:
        BuildOutput() noexcept;

        void Reset() noexcept;

        BuildDisposition disposition = BuildDisposition::Built;
        BuildOrigin origin = BuildOrigin::Unknown;
        BuildFingerprint buildFingerprint;
        BuildFingerprint contentFingerprint;
        containers::DynamicArray<Artifact> artifacts;

    private:
        BuildOutputProtection m_publicationProtection;

        friend class BuildGraph;
        friend class BuildSystem;
        friend class detail::PersistentCacheAuthorityAccess;
    };

    struct Config
    {
        u32 maximumCompilers = 256;
        u32 maximumDependenciesPerBuild = 4096;
        u32 maximumArtifactsPerBuild = 4096;
        u64 maximumArtifactBytesPerBuild = 2ull * 1024ull * 1024ull * 1024ull;
        BuildReportLimits reportLimits;
        u32 maximumCacheEntries = 1024;
        u64 maximumCacheBytes = 4ull * 1024ull * 1024ull * 1024ull;

        // Optional absolute directory for the persistent derived-data cache.
        // The string is copied during Initialize and may be released after it
        // returns. A null pointer keeps the cache memory-only.
        const char* persistentCacheRoot = nullptr;
        PersistentCacheCoordinationLimits persistentCacheCoordination;
    };

    struct Stats
    {
        u32 registeredCompilers = 0;
        u32 activeBuilds = 0;
        u32 cacheEntries = 0;
        u64 cacheBytes = 0;
        u64 buildRequests = 0;
        u64 localBuilds = 0;
        u64 cacheHits = 0;
        u64 cacheMisses = 0;
        u64 cacheStores = 0;
        u64 cacheEvictions = 0;
        u64 memoryCacheHits = 0;
        u64 persistentCacheHits = 0;
        u64 persistentCacheMisses = 0;
        u64 persistentCacheStores = 0;
        u64 persistentCacheCorruptions = 0;
        u64 persistentCacheIoFailures = 0;
        u64 persistentCacheRecoveries = 0;
        u64 failedBuilds = 0;
    };

    class BuildSystem final
    {
    public:
        struct Impl;

        BuildSystem() noexcept = default;
        ~BuildSystem();

        BuildSystem(const BuildSystem&) = delete;
        BuildSystem& operator=(const BuildSystem&) = delete;

        [[nodiscard]] bool Initialize(const Config& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] Result RegisterCompiler(const CompilerDescriptor& compiler) noexcept;
        [[nodiscard]] Result UnregisterCompiler(CompilerId compiler) noexcept;

        [[nodiscard]] Result Build(const BuildRequest& request, BuildOutput& output, BuildReport* report = nullptr) noexcept;

        // Two-stage cooking. Prepare performs lightweight
        // dependency discovery. Execute verifies the compiler generation,
        // requires all generated dependencies to be resolved, computes the
        // final fingerprint, then performs cache lookup or compilation.
        [[nodiscard]] Result Prepare(const BuildRequest& request, BuildPlan& plan) noexcept;
        [[nodiscard]] Result Execute(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output,
                                     IsCancellationRequestedFunction cancellation = nullptr, void* cancellationUserData = nullptr,
                                     containers::ArraySpan<const GeneratedDependencyView> generatedDependencies = {}, BuildReport* report = nullptr) noexcept;

        void ClearCache() noexcept;
        [[nodiscard]] Stats GetStats() const noexcept;

    private:
        [[nodiscard]] bool RegisterPersistentProtectionDomain(const void* owner) noexcept;
        [[nodiscard]] bool UnregisterPersistentProtectionDomain(const void* owner) noexcept;
        [[nodiscard]] bool OwnsPersistentPublication(const void* owner, const BuildOutput& output) const noexcept;
        void AdoptPersistentPublication(const void* owner, BuildOutput& output) noexcept;
        [[nodiscard]] u64 PersistentProtectionEpoch(const void* owner) const noexcept;
        [[nodiscard]] PersistentCacheMaintenanceResult MaintainPersistentCache(
            const void* owner, containers::ArraySpan<const ArtifactSetKey> protectedKeys, u64 protectionEpoch,
            const PersistentCacheMaintenanceLimits& limits, PersistentCacheMaintenanceReport& report,
            containers::DynamicArray<PersistentCacheMaintenanceDiagnostic>& diagnostics,
            IsCancellationRequestedFunction cancellation = nullptr, void* cancellationUserData = nullptr) noexcept;

        Impl* m_impl = nullptr;

        friend class BuildGraph;
        friend class DependencyIndex;
        friend class ProjectAssetBuildSystem;
    };
} // namespace vanguard::assets
