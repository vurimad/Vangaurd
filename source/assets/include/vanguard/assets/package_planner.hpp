#pragma once

#include <vanguard/assets/asset_index.hpp>
#include <vanguard/packages/packages.hpp>

namespace vanguard::assets
{
    inline constexpr u32 PackageManifestMagic = serialization::MakeFourCC('V', 'P', 'M', 'F');

    enum class PackagingResult : u8
    {
        Success,
        InvalidArgument,
        InvalidState,
        InvalidMagic,
        UnsupportedVersion,
        CorruptManifest,
        DuplicateRoot,
        MissingResource,
        TargetMismatch,
        MissingArtifact,
        DuplicateArtifact,
        ArtifactReadFailed,
        InvalidArtifactData,
        PackageWriteFailed,
        ValidationFailed,
        PublicationFailed,
        RebalanceRequired,
        IndexFailure,
        OutOfMemory,
        LimitExceeded,
        IoFailure
    };

    [[nodiscard]] const char* ToString(PackagingResult result) noexcept;

    enum class PackageManifestFlags : u32
    {
        None = 0,
        IncludeGeneratedDependencies = 1u << 0u,
        IncludeOptionalDependencies = 1u << 1u,
        IncludeEditorArtifacts = 1u << 2u,
        HasDebugPaths = 1u << 3u
    };

    enum class PackageRootFlags : u8
    {
        None = 0,
        Startup = 1u << 0u,
        Optional = 1u << 1u
    };

    [[nodiscard]] constexpr PackageManifestFlags operator|(const PackageManifestFlags left, const PackageManifestFlags right) noexcept
    {
        return static_cast<PackageManifestFlags>(static_cast<u32>(left) | static_cast<u32>(right));
    }

    [[nodiscard]] constexpr PackageRootFlags operator|(const PackageRootFlags left, const PackageRootFlags right) noexcept
    {
        return static_cast<PackageRootFlags>(static_cast<u8>(left) | static_cast<u8>(right));
    }

    [[nodiscard]] constexpr bool HasFlag(const PackageManifestFlags value, const PackageManifestFlags flag) noexcept
    {
        return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool HasFlag(const PackageRootFlags value, const PackageRootFlags flag) noexcept
    {
        return (static_cast<u8>(value) & static_cast<u8>(flag)) != 0;
    }

    struct PackageRoot
    {
        resources::ResourceReference resource;
        PackageRootFlags flags = PackageRootFlags::None;
    };

    struct PackageManifestLimits
    {
        u32 maximumRoots = 65536;
        u64 maximumBytes = 16ull * 1024ull * 1024ull;
    };

    class PackageManifest final
    {
    public:
        static constexpr serialization::Version CurrentVersion{1, 0};
        static constexpr u16 HeaderWireSize = 64;
        static constexpr u16 RootWireSize = 16;

        PackageManifest() noexcept;

        serialization::Version version = CurrentVersion;
        u64 packageId = 0;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        packages::Codec codec = packages::Codec::Lz4;
        u8 dataAlignmentLog2 = 12;
        PackageManifestFlags flags = PackageManifestFlags::IncludeGeneratedDependencies;
        containers::DynamicArray<PackageRoot> roots;

        [[nodiscard]] PackagingResult AddRoot(const PackageRoot& root, u32 maximumRoots = 65536) noexcept;
        [[nodiscard]] bool IsValid(const PackageManifestLimits& limits = {}) const noexcept;
    };

    [[nodiscard]] PackagingResult WritePackageManifest(filesystem::IFile& file, const PackageManifest& manifest,
                                                       const PackageManifestLimits& limits = {}) noexcept;
    [[nodiscard]] PackagingResult ReadPackageManifest(filesystem::IFile& file, PackageManifest& manifest,
                                                      const PackageManifestLimits& limits = {}) noexcept;

    struct PackagePlanLimits
    {
        u32 maximumResources = 4u * 1024u * 1024u;
        u32 maximumSegments = 16u * 1024u * 1024u;
        u32 maximumDependencies = 64u * 1024u * 1024u;
    };

    struct PlannedPackageSegment
    {
        IndexedArtifact artifact;
        packages::Codec codec = packages::Codec::None;
        packages::SegmentFlags flags = packages::SegmentFlags::None;
    };

    struct PlannedPackageDependency
    {
        resources::ResourceReference resource;
        resources::DependencyKind kind = resources::DependencyKind::Required;
    };

    class PlannedPackageResource final
    {
    public:
        PlannedPackageResource() noexcept;

        resources::ResourceReference resource;
        packages::ResourceFlags flags = packages::ResourceFlags::None;
        BuildFingerprint contentFingerprint;
        containers::DynamicArray<PlannedPackageSegment> segments;
        containers::DynamicArray<PlannedPackageDependency> dependencies;
    };

    class PackageBuildPlan final
    {
    public:
        PackageBuildPlan() noexcept;

        u64 packageId = 0;
        u64 buildId = 0;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        packages::BuildOptions options;
        containers::DynamicArray<PlannedPackageResource> resources;

        void Clear() noexcept;
        [[nodiscard]] bool IsPrepared() const noexcept;
    };

    class PackagePlanner final
    {
    public:
        [[nodiscard]] PackagingResult Prepare(const PackageManifest& manifest, const DependencyIndex& index, PackageBuildPlan& plan,
                                              const PackagePlanLimits& limits = {}) const noexcept;
    };

    using ResolvePackagePathFunction = bool (*)(resources::ResourceReference resource, char* destination, usize capacity, usize& written,
                                                void* userData) noexcept;
    using ReadPackageArtifactFunction = bool (*)(const IndexedArtifact& artifact, containers::DynamicArray<u8>& bytes,
                                                 void* userData) noexcept;

    struct PackageAssemblyCallbacks
    {
        ResolvePackagePathFunction resolvePath = nullptr;
        ReadPackageArtifactFunction readArtifact = nullptr;
        void* userData = nullptr;
    };

    struct PackageAssemblyLimits
    {
        u64 maximumResourceBytes = 2ull * 1024ull * 1024ull * 1024ull;
        packages::ReadLimits validation;
    };

    class PackageAssembler final
    {
    public:
        [[nodiscard]] PackagingResult Assemble(const PackageBuildPlan& plan, filesystem::IFile& output,
                                               const PackageAssemblyCallbacks& callbacks,
                                               const PackageAssemblyLimits& limits = {}) const noexcept;

        [[nodiscard]] PackagingResult Assemble(const PackageBuildPlan& plan, filesystem::IFile& output,
                                               const PackageAssemblyCallbacks& callbacks,
                                               const packages::PackageSetBuild& packageSet,
                                               const PackageAssemblyLimits& limits = {}) const noexcept;

        // The caller supplies an explicit sibling temporary path. Publication
        // validates the completed VPAK before atomically replacing the target.
        [[nodiscard]] PackagingResult Publish(const PackageBuildPlan& plan, const filesystem::AbsolutePath& target,
                                              const filesystem::AbsolutePath& temporary, const PackageAssemblyCallbacks& callbacks,
                                              const PackageAssemblyLimits& limits = {}) const noexcept;

    private:
        [[nodiscard]] PackagingResult AssembleInternal(const PackageBuildPlan& plan, filesystem::IFile& output,
                                                       const PackageAssemblyCallbacks& callbacks,
                                                       const packages::PackageSetBuild* packageSet,
                                                       const PackageAssemblyLimits& limits) const noexcept;
    };
} // namespace vanguard::assets
