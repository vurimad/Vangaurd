#pragma once

#include <vanguard/crypto/crypto.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/system/cancellation.hpp>

namespace vanguard::assets
{
    using BuildFingerprint = crypto::Digest256;
    using CompilerId = u64;

    inline constexpr CompilerId InvalidCompilerId = 0;

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
        Optional
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

    struct BuildDependency
    {
        resources::ResourceReference identity;
        BuildFingerprint content;
        DependencyRole role = DependencyRole::Source;
        DependencyRequirement requirement = DependencyRequirement::Required;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return identity.IsValid() && identity.IsTyped() && role <= DependencyRole::Tool && requirement <= DependencyRequirement::Optional &&
                   (requirement == DependencyRequirement::Optional || role == DependencyRole::Generated || !content.IsEmpty());
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

    struct Artifact
    {
        Artifact() noexcept;

        resources::ResourceReference resource;
        u32 segment = 0;
        ArtifactFlags flags = ArtifactFlags::None;
        u8 alignmentLog2 = 4;
        containers::DynamicArray<u8> bytes;
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
        BuildFingerprint buildFingerprint;
        IsCancellationRequestedFunction cancellation = nullptr;
        void* cancellationUserData = nullptr;

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return cancellation != nullptr && cancellation(cancellationUserData);
        }
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

    class BuildOutput final
    {
    public:
        BuildOutput() noexcept;

        void Reset() noexcept;

        BuildDisposition disposition = BuildDisposition::Built;
        BuildFingerprint buildFingerprint;
        BuildFingerprint contentFingerprint;
        containers::DynamicArray<Artifact> artifacts;
    };

    struct Config
    {
        u32 maximumCompilers = 256;
        u32 maximumDependenciesPerBuild = 4096;
        u32 maximumArtifactsPerBuild = 4096;
        u64 maximumArtifactBytesPerBuild = 2ull * 1024ull * 1024ull * 1024ull;
        u32 maximumCacheEntries = 1024;
        u64 maximumCacheBytes = 4ull * 1024ull * 1024ull * 1024ull;

        // Optional absolute directory for the persistent derived-data cache.
        // The string is copied during Initialize and may be released after it
        // returns. A null pointer keeps the cache memory-only.
        const char* persistentCacheRoot = nullptr;
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

        [[nodiscard]] Result Build(const BuildRequest& request, BuildOutput& output) noexcept;

        // Two-stage cooking. Prepare performs lightweight
        // dependency discovery. Execute verifies the compiler generation,
        // requires all generated dependencies to be resolved, computes the
        // final fingerprint, then performs cache lookup or compilation.
        [[nodiscard]] Result Prepare(const BuildRequest& request, BuildPlan& plan) noexcept;
        [[nodiscard]] Result Execute(const BuildRequest& request, const BuildPlan& plan, BuildOutput& output,
                                     IsCancellationRequestedFunction cancellation = nullptr, void* cancellationUserData = nullptr) noexcept;

        void ClearCache() noexcept;
        [[nodiscard]] Stats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
