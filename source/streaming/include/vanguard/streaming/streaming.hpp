#pragma once

#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/packages/packages.hpp>
#include <vanguard/resources/resource_pipeline.hpp>
#include <vanguard/schemas/schemas.hpp>
#include <vanguard/streaming/resource_source.hpp>

namespace vanguard::streaming
{
    enum class SourceKind : u8
    {
        LooseFile,
        Package
    };

    [[nodiscard]] io::AsyncPriority ToIoPriority(resources::LoadPriority priority) noexcept;
    [[nodiscard]] resources::Failure ToFailure(ResourceSourceResult result) noexcept;

    struct Config
    {
        u64 stagingBudgetBytes = 512ull * 1024ull * 1024ull;
        u64 maximumResourceBytes = 2ull * 1024ull * 1024ull * 1024ull;
        u32 maximumDependenciesPerResource = 4096;
        u32 maximumSegmentsPerResource = 16384;
    };

    struct DependencyDescriptor
    {
        resources::ResourceReference reference;
        resources::DependencyKind kind = resources::DependencyKind::Required;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return reference.IsValid() && reference.IsTyped();
        }
    };

    struct LooseResourceDescriptor
    {
        resources::ResourceReference reference;
        filesystem::AbsolutePath physicalPath;
        containers::ArraySpan<const DependencyDescriptor> dependencies;
        u64 expectedContentCrc64 = 0;
        i32 priority = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return reference.IsValid() && reference.IsTyped() && physicalPath.IsFilePath();
        }
    };

    using DecodeResourceFunction = resources::ResourceObject* (*)(resources::ResourceReference reference, const void* data, usize size,
                                                                  const resources::LoadContext& context, resources::Failure& failure, void* userData) noexcept;

    struct DecoderDescriptor
    {
        resources::ResourceTypeId type = resources::InvalidResourceTypeId;
        const char* name = nullptr;
        DecodeResourceFunction decode = nullptr;
        resources::DestroyResourceFunction destroy = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return type != resources::InvalidResourceTypeId && name != nullptr && name[0] != '\0' && decode != nullptr && destroy != nullptr;
        }
    };

    using ResolveSchemaFunction = const reflection::Schema* (*)(const resources::LoadContext& context, void* userData) noexcept;
    using CreateSchemaResourceFunction = resources::ResourceObject* (*)(const reflection::Schema& schema, const resources::LoadContext& context,
                                                                        void* userData) noexcept;
    using SchemaObjectFunction = void* (*)(resources::ResourceObject& resource, void* userData) noexcept;
    using BindSchemaDependenciesFunction = bool (*)(resources::ResourceObject& resource, const resources::LoadContext& context, void* userData) noexcept;

    // The descriptor, resolved schema, and callback state must remain alive
    // until the decoder is unregistered. A fixed schema or a dependency-driven
    // resolver may be supplied. Resolution happens after dependency fan-in, so
    // formats such as materials can derive their layout from a loaded shader.
    // Dependencies declared in VPAK/loose metadata are checked against the
    // reflected object before publication.
    struct SchemaDecoderDescriptor
    {
        resources::ResourceTypeId type = resources::InvalidResourceTypeId;
        const char* name = nullptr;
        const reflection::Schema* schema = nullptr;
        ResolveSchemaFunction resolveSchema = nullptr;
        CreateSchemaResourceFunction create = nullptr;
        SchemaObjectFunction object = nullptr;
        BindSchemaDependenciesFunction bindDependencies = nullptr;
        resources::DestroyResourceFunction destroy = nullptr;
        schemas::ReadLimits limits;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return type != resources::InvalidResourceTypeId && name != nullptr && name[0] != '\0' && ((schema != nullptr) != (resolveSchema != nullptr)) &&
                   create != nullptr && object != nullptr && destroy != nullptr;
        }
    };

    struct Stats
    {
        u32 registeredDecoders = 0;
        u32 looseResources = 0;
        u32 mountedPackages = 0;
        u32 activeLoads = 0;
        u32 activeReads = 0;
        u64 stagingBudgetBytes = 0;
        u64 stagingBytesInUse = 0;
        u64 peakStagingBytes = 0;
        u64 bytesRead = 0;
        u64 completedLoads = 0;
        u64 failedLoads = 0;
        u64 cancelledLoads = 0;
        u64 integrityFailures = 0;
        u64 budgetRejections = 0;
    };

    struct PackageMountDescriptor
    {
        const packages::PackageReader* reader = nullptr;
        filesystem::AbsolutePath physicalPath;
        i32 priority = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return reader != nullptr && reader->IsOpen() && physicalPath.IsFilePath();
        }
    };

    class StagingReservation final
    {
    public:
        StagingReservation() noexcept = default;
        ~StagingReservation();
        StagingReservation(const StagingReservation&) = delete;
        StagingReservation& operator=(const StagingReservation&) = delete;
        StagingReservation(StagingReservation&& other) noexcept;
        StagingReservation& operator=(StagingReservation&& other) noexcept;

        void Reset() noexcept;
        [[nodiscard]] u64 GetBytes() const noexcept;

    private:
        detail::ResourceSourceAccounting* m_accounting = nullptr;
        u64 m_bytes = 0;

        friend class ResourceStreamer;
    };

    class ResourceStreamer final
    {
    public:
        struct Impl;

        ResourceStreamer() noexcept = default;
        ~ResourceStreamer();

        ResourceStreamer(const ResourceStreamer&) = delete;
        ResourceStreamer& operator=(const ResourceStreamer&) = delete;

        [[nodiscard]] bool Initialize(resources::ResourcePipeline& pipeline, const Config& config = {}) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterDecoder(const DecoderDescriptor& decoder) noexcept;
        [[nodiscard]] bool RegisterSchemaDecoder(const SchemaDecoderDescriptor& decoder) noexcept;
        [[nodiscard]] bool UnregisterDecoder(resources::ResourceTypeId type) noexcept;

        [[nodiscard]] bool RegisterLoose(const LooseResourceDescriptor& resource) noexcept;
        [[nodiscard]] bool UnregisterLoose(resources::ResourcePath path) noexcept;

        // Reader and package file must remain valid and unchanged until
        // unmounted. Higher priority wins; later mount wins at equal priority.
        [[nodiscard]] bool MountPackage(const packages::PackageReader& reader, const filesystem::AbsolutePath& physicalPath, i32 priority) noexcept;
        [[nodiscard]] bool UnmountPackage(const packages::PackageReader& reader) noexcept;

        // Batch mutation is all-or-nothing and becomes visible under one streamer lock. It is the required path for package-set startup
        // and shutdown so another thread can never observe only part of a mounted runtime image.
        [[nodiscard]] bool MountPackages(containers::ArraySpan<const PackageMountDescriptor> packages) noexcept;
        [[nodiscard]] bool UnmountPackages(containers::ArraySpan<const PackageMountDescriptor> packages) noexcept;

        // Resolves the same winning loose/package generation used by ordinary
        // decoders, pins it as an independently owned range source, and copies
        // its non-soft dependency table. Specialized formats use this instead
        // of building a second source registry or staging the whole resource.
        [[nodiscard]] resources::Failure OpenSource(resources::ResourceReference reference, ResourceSource& source,
                                                    containers::DynamicArray<DependencyDescriptor>& dependencies) noexcept;
        [[nodiscard]] bool ReserveStaging(u64 bytes, StagingReservation& reservation) noexcept;

        [[nodiscard]] resources::PipelineRequest Request(resources::ResourceReference reference,
                                                         resources::LoadPriority priority = resources::LoadPriority::Normal) noexcept;
        [[nodiscard]] Stats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;
    };

    enum class PackageSetMountResult : u8
    {
        Success,
        InvalidArgument,
        AlreadyMounted,
        MissingRootPackage,
        RootOpenFailed,
        MissingPackageSet,
        GameMismatch,
        BuildMismatch,
        TargetMismatch,
        MissingRequiredPackage,
        PackageOpenFailed,
        PackageMetadataMismatch,
        PackageDigestMismatch,
        MountFailed,
        Busy,
        OutOfMemory,
        IoFailure
    };

    [[nodiscard]] const char* ToString(PackageSetMountResult result) noexcept;

    struct PackageSetMountConfig
    {
        u64 expectedGameId = 0;
        u64 expectedBuildId = 0;
        u32 expectedTargetPlatformId = 0;
        i32 rootPriority = (-2147483647 - 1);
        bool mountOptionalPackages = true;
        packages::CatalogVerification verification = packages::CatalogVerification::IndexAndIdentity;
        packages::ReadLimits packageLimits;
        packages::PackageSetReadLimits packageSetLimits;
        u32 hashBufferBytes = 1024u * 1024u;
    };

    struct MountedPackageInfo
    {
        const packages::PackageReader* reader = nullptr;
        filesystem::AbsolutePath physicalPath;
        u32 packageNumber = 0;
        packages::PackageSetEntryFlags flags = packages::PackageSetEntryFlags::Required;
        i32 priority = 0;
    };

    // Owns all PackageReader metadata required by ResourceStreamer. Mount is transactional: DATA000 and every selected catalog entry
    // are opened and validated before the complete set becomes visible. The owner must outlive requests that can resolve from the set.
    class PackageSetMount final
    {
    public:
        struct Impl;

        PackageSetMount() noexcept = default;
        ~PackageSetMount();

        PackageSetMount(const PackageSetMount&) = delete;
        PackageSetMount& operator=(const PackageSetMount&) = delete;

        [[nodiscard]] PackageSetMountResult Mount(ResourceStreamer& streamer, const filesystem::AbsolutePath& gameDirectory,
                                                  const PackageSetMountConfig& config = {}) noexcept;
        [[nodiscard]] PackageSetMountResult Unmount() noexcept;
        [[nodiscard]] bool IsMounted() const noexcept;

        [[nodiscard]] u64 GetGameId() const noexcept;
        [[nodiscard]] u64 BuildId() const noexcept;
        [[nodiscard]] u32 GetTargetPlatformId() const noexcept;
        [[nodiscard]] resources::ResourceReference StartupWorld() const noexcept;
        [[nodiscard]] resources::ResourceReference GetDefaultInput() const noexcept;
        [[nodiscard]] u32 PackageCount() const noexcept;
        [[nodiscard]] const MountedPackageInfo* FindPackage(u32 packageNumber) const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::streaming
