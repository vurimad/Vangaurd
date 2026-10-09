#pragma once

#include <vanguard/streaming/resource_source.hpp>

namespace vanguard::streaming::detail
{
    struct ResourceSourceAccounting;
    struct ResourceSourcePackageGeneration;
    struct ManagedLooseRoot;
    struct ManagedLooseGeneration;

    enum class ManagedLooseRetireResult : u8
    {
        Removed,
        NotManaged,
        Current,
        Busy,
        InvalidPath,
        LimitExceeded,
        IoFailure
    };

    struct ManagedLooseRootStats
    {
        u32 generations = 0;
        u32 descriptorLeases = 0;
        u32 sourceLeases = 0;
        u32 objectLeases = 0;
        bool retiring = false;
    };

    struct ResourceSourceAccountingStats
    {
        u32 liveSources = 0;
        u32 activeReads = 0;
        u64 stagingBudgetBytes = 0;
        u64 stagingBytesInUse = 0;
        u64 peakStagingBytes = 0;
        u64 bytesRead = 0;
        u64 budgetRejections = 0;
    };

    [[nodiscard]] ResourceSourceAccounting* CreateResourceSourceAccounting(u64 stagingBudgetBytes) noexcept;
    void RetainResourceSourceAccounting(ResourceSourceAccounting* accounting) noexcept;
    void ReleaseResourceSourceAccounting(ResourceSourceAccounting* accounting) noexcept;
    [[nodiscard]] bool ReserveResourceSourceStaging(ResourceSourceAccounting* accounting, u64 bytes) noexcept;
    void ReleaseResourceSourceStaging(ResourceSourceAccounting* accounting, u64 bytes) noexcept;
    [[nodiscard]] ResourceSourceAccountingStats GetResourceSourceAccountingStats(const ResourceSourceAccounting* accounting) noexcept;

    [[nodiscard]] ResourceSourcePackageGeneration* CreateResourceSourcePackageGeneration(const filesystem::AbsolutePath& physicalPath) noexcept;
    void RetainResourceSourcePackageGeneration(ResourceSourcePackageGeneration* generation) noexcept;
    void ReleaseResourceSourcePackageGeneration(ResourceSourcePackageGeneration* generation) noexcept;
    [[nodiscard]] const packages::PackageReader* GetResourceSourcePackageReader(const ResourceSourcePackageGeneration* generation) noexcept;

    [[nodiscard]] bool RegisterManagedLooseRoot(const filesystem::AbsolutePath& root, u32 maximumGenerations,
                                                ManagedLooseRoot*& authority) noexcept;
    [[nodiscard]] bool CanReleaseManagedLooseRoot(const ManagedLooseRoot* authority, bool allowDescriptorLeases) noexcept;
    [[nodiscard]] bool ReleaseManagedLooseRoot(ManagedLooseRoot*& authority) noexcept;
    [[nodiscard]] bool ManagedLooseRootContains(const ManagedLooseRoot* authority, const filesystem::AbsolutePath& path) noexcept;
    [[nodiscard]] ManagedLooseRootStats GetManagedLooseRootStats(const ManagedLooseRoot* authority) noexcept;

    // These acquisitions are exact-path leases. A null generation on success
    // means the path is outside every registered managed root and therefore
    // uses direct-source behavior without managed-generation governance.
    [[nodiscard]] bool AcquireManagedLooseDescriptor(const filesystem::AbsolutePath& path,
                                                     ManagedLooseGeneration*& generation) noexcept;
    void ReleaseManagedLooseDescriptor(ManagedLooseGeneration*& generation) noexcept;
    [[nodiscard]] bool AcquireManagedLooseSource(const filesystem::AbsolutePath& path,
                                                 ManagedLooseGeneration*& generation) noexcept;
    void ReleaseManagedLooseSource(ManagedLooseGeneration*& generation) noexcept;
    [[nodiscard]] bool AcquireManagedLooseObject(ManagedLooseGeneration* generation) noexcept;
    void ReleaseManagedLooseObject(ManagedLooseGeneration* generation) noexcept;
    [[nodiscard]] ManagedLooseGeneration* GetManagedLooseGeneration(const ResourceSource& source) noexcept;
    [[nodiscard]] ManagedLooseRetireResult RetireManagedLooseGeneration(ManagedLooseRoot* authority,
                                                                       const filesystem::AbsolutePath& path) noexcept;
} // namespace vanguard::streaming::detail
