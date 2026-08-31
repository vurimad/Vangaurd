#pragma once

namespace vanguard::system
{
    using IsCancellationRequestedFunction = bool (*)(void* userData) noexcept;

    /// Read-only cancellation signal shared by long-running tool and runtime operations.
    /// It does not own cancellation state and is safe to copy into synchronous call chains.
    struct CancellationView
    {
        IsCancellationRequestedFunction function = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsCancellationRequested() const noexcept
        {
            return function != nullptr && function(userData);
        }
    };
} // namespace vanguard::system
