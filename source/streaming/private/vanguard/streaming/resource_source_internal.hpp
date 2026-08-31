#pragma once

#include <vanguard/streaming/resource_source.hpp>

namespace vanguard::streaming::detail
{
    struct ResourceSourceAccounting;
    struct ResourceSourcePackageGeneration;

    struct ResourceSourceAccountingStats
    {
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
} // namespace vanguard::streaming::detail
