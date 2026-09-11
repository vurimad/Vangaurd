#pragma once

#include <vanguard/assets/package_planner.hpp>

namespace vanguard::assets
{
    inline constexpr u32 PackagePlacementMagic = serialization::MakeFourCC('V', 'P', 'L', 'S');

    struct PackagePlacement
    {
        resources::ResourceReference resource;
        u32 packageNumber = 0;
    };

    class PackagePlacementState final
    {
    public:
        static constexpr serialization::Version CurrentVersion{1, 0};
        static constexpr u16 HeaderWireSize = 64;
        static constexpr u16 EntryWireSize = 16;

        PackagePlacementState() noexcept;

        void Clear() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

        serialization::Version version = CurrentVersion;
        u64 gameId = 0;
        TargetPlatform target = TargetPlatform::WindowsD3D12;
        containers::DynamicArray<PackagePlacement> placements;
    };

    struct PackagePlacementLimits
    {
        u32 maximumResources = 4u * 1024u * 1024u;
        u64 maximumBytes = 128ull * 1024ull * 1024ull;
    };

    [[nodiscard]] PackagingResult WritePackagePlacementState(filesystem::IFile& file, const PackagePlacementState& state,
                                                             const PackagePlacementLimits& limits = {}) noexcept;
    [[nodiscard]] PackagingResult ReadPackagePlacementState(filesystem::IFile& file, PackagePlacementState& state,
                                                            const PackagePlacementLimits& limits = {}) noexcept;

    struct PackageSetPlanOptions
    {
        u64 gameId = 0;
        resources::ResourceReference startupWorld;
        resources::ResourceReference defaultInput;
        resources::ResourceReference rendererBootstrap;
        u64 targetPackageBytes = 8ull * 1024ull * 1024ull * 1024ull;
        u64 maximumPackageBytes = 16ull * 1024ull * 1024ull * 1024ull;
        u64 maximumBootstrapBytes = 1024ull * 1024ull * 1024ull;
        u32 maximumPackages = packages::MaximumPackageNumber + 1u;
        bool rebalance = false;
    };

    class PlannedDataPackage final
    {
    public:
        PlannedDataPackage() noexcept;

        u32 packageNumber = 0;
        u64 packageId = 0;
        u64 buildId = 0;
        u64 estimatedBytes = 0;
        containers::DynamicArray<u32> resourceIndices;
    };

    class PackageSetBuildPlan final
    {
    public:
        PackageSetBuildPlan() noexcept;

        void Clear() noexcept;
        [[nodiscard]] bool IsPrepared() const noexcept;
        [[nodiscard]] const PlannedDataPackage* FindPackage(u32 packageNumber) const noexcept;

        const PackageBuildPlan* source = nullptr;
        u64 gameId = 0;
        u64 buildId = 0;
        u32 targetPlatformId = 0;
        resources::ResourceReference startupWorld;
        resources::ResourceReference defaultInput;
        resources::ResourceReference rendererBootstrap;
        u64 maximumPackageBytes = 0;
        containers::DynamicArray<PlannedDataPackage> packages;
    };

    class PackageSetPlanner final
    {
    public:
        [[nodiscard]] PackagingResult Prepare(const PackageBuildPlan& source, const PackageSetPlanOptions& options,
                                              const PackagePlacementState* previousPlacement, PackageSetBuildPlan& plan,
                                              PackagePlacementState& nextPlacement) const noexcept;
    };

    struct PackageSetPublicationLimits
    {
        PackageAssemblyLimits package;
        u32 hashBufferBytes = 1024u * 1024u;
    };

    class PackageSetAssembler final
    {
    public:
        // The output directory must not already contain any target DATA files. All packages are staged and validated first;
        // DATA000 is renamed last and acts as the commit marker for the complete runtime image.
        [[nodiscard]] PackagingResult Publish(const PackageSetBuildPlan& plan, const filesystem::AbsolutePath& outputDirectory,
                                              const PackageAssemblyCallbacks& callbacks, const PackageSetPublicationLimits& limits = {}) const noexcept;
    };
} // namespace vanguard::assets
