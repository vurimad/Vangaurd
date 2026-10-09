#pragma once

namespace vanguard::nanovanguard
{
    // Process-local foreground-command cancellation only. The Windows console
    // callback publishes this flag; owner-thread command loops observe it and
    // call the already-owned ProjectAssetPipeline cancellation APIs.
    void ResetCommandCancellation() noexcept;
    void RequestCommandCancellation() noexcept;
    [[nodiscard]] bool IsCommandCancellationRequested() noexcept;

    [[nodiscard]] bool InstallCommandCancellationHandler() noexcept;
    void RemoveCommandCancellationHandler() noexcept;
} // namespace vanguard::nanovanguard
