#pragma once

#include <vanguard/containers/containers.hpp>

namespace vanguard::resources
{
    using ResourceId = u64;
    using ResourceTypeId = u32;

    inline constexpr ResourceId InvalidResourceId = 0;
    inline constexpr ResourceTypeId InvalidResourceTypeId = 0;
    inline constexpr usize MaximumResourcePathBytes = 1024;

    enum class DependencyKind : u8
    {
        Required,
        Optional,
        Soft
    };

    enum class Result : u8
    {
        Success,
        InvalidArgument,
        InvalidPath,
        BufferTooSmall
    };

    [[nodiscard]] const char* ToString(Result result) noexcept;

    // Vanguard paths are logical, relative UTF-8 byte sequences. Canonical
    // paths use '/', lowercase ASCII, contain no empty, "." or ".." segments,
    // and never contain platform drive/depot syntax.
    [[nodiscard]] Result CanonicalizePath(containers::StringView path, char* destination, usize capacity, usize& written) noexcept;

    [[nodiscard]] ResourceId HashPath(containers::StringView path) noexcept;

    // Type names are stable lowercase dotted identifiers such as
    // "vanguard.mesh". Their IDs are metadata identities, not RTTI addresses.
    [[nodiscard]] ResourceTypeId HashTypeName(containers::StringView typeName) noexcept;

    class ResourcePath final
    {
    public:
        constexpr ResourcePath() noexcept = default;

        [[nodiscard]] static constexpr ResourcePath FromId(const ResourceId id) noexcept
        {
            return ResourcePath(id);
        }

        [[nodiscard]] static ResourcePath FromString(containers::StringView path) noexcept
        {
            return ResourcePath(HashPath(path));
        }

        [[nodiscard]] constexpr ResourceId Id() const noexcept
        {
            return m_id;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_id != InvalidResourceId;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return IsValid();
        }

        [[nodiscard]] friend constexpr bool operator==(const ResourcePath&, const ResourcePath&) noexcept = default;

        [[nodiscard]] friend constexpr bool operator<(const ResourcePath left, const ResourcePath right) noexcept
        {
            return left.m_id < right.m_id;
        }

    private:
        explicit constexpr ResourcePath(const ResourceId id) noexcept : m_id(id) {}

        ResourceId m_id = InvalidResourceId;
    };

    static_assert(sizeof(ResourcePath) == sizeof(ResourceId));

    struct ResourceKey
    {
        ResourcePath path;
        ResourceTypeId type = InvalidResourceTypeId;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return path.IsValid();
        }

        [[nodiscard]] constexpr bool IsTyped() const noexcept
        {
            return type != InvalidResourceTypeId;
        }

        [[nodiscard]] friend constexpr bool operator==(const ResourceKey&, const ResourceKey&) noexcept = default;
    };

    class ResourceReference final
    {
    public:
        constexpr ResourceReference() noexcept = default;

        explicit constexpr ResourceReference(const ResourcePath path, const ResourceTypeId expectedType = InvalidResourceTypeId) noexcept
            : m_key{path, expectedType}
        {
        }

        [[nodiscard]] constexpr const ResourceKey& Key() const noexcept
        {
            return m_key;
        }

        [[nodiscard]] constexpr ResourcePath Path() const noexcept
        {
            return m_key.path;
        }

        [[nodiscard]] constexpr ResourceTypeId ExpectedType() const noexcept
        {
            return m_key.type;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return m_key.IsValid();
        }

        [[nodiscard]] constexpr bool IsTyped() const noexcept
        {
            return m_key.IsTyped();
        }

        [[nodiscard]] friend constexpr bool operator==(const ResourceReference&, const ResourceReference&) noexcept = default;

    private:
        ResourceKey m_key;
    };

    enum class State : u8
    {
        Unloaded,
        Queued,
        Loading,
        Loaded,
        Reloading,
        Evicting,
        Failed,
        Cancelled
    };

    enum class Failure : u8
    {
        None,
        InvalidPath,
        UnknownType,
        NotFound,
        DependencyFailure,
        DependencyCycle,
        DependencyLimit,
        IntegrityFailure,
        UnsupportedVersion,
        OutOfMemory,
        IoFailure,
        DeserializationFailure,
        Cancelled,
        InternalError
    };

    [[nodiscard]] constexpr bool IsTerminal(const State state) noexcept
    {
        return state == State::Loaded || state == State::Failed || state == State::Cancelled;
    }

    [[nodiscard]] constexpr bool CanTransition(const State from, const State to) noexcept
    {
        switch (from)
        {
        case State::Unloaded:
            return to == State::Queued;
        case State::Queued:
            return to == State::Loading || to == State::Failed || to == State::Cancelled;
        case State::Loading:
            return to == State::Loaded || to == State::Failed || to == State::Cancelled;
        case State::Loaded:
            return to == State::Reloading || to == State::Evicting;
        case State::Reloading:
            return to == State::Loaded || to == State::Failed || to == State::Cancelled;
        case State::Evicting:
            return to == State::Unloaded || to == State::Loaded;
        case State::Failed:
        case State::Cancelled:
            return to == State::Queued;
        }
        return false;
    }

    class ResourceRegistry;
    class WeakResourceHandle;

    class ResourceObject
    {
    public:
        ResourceObject() = default;
        virtual ~ResourceObject() = default;

        ResourceObject(const ResourceObject&) = delete;
        ResourceObject& operator=(const ResourceObject&) = delete;
        ResourceObject(ResourceObject&&) = delete;
        ResourceObject& operator=(ResourceObject&&) = delete;

        [[nodiscard]] virtual ResourceTypeId Type() const noexcept = 0;
    };

    class ResourceHandle final
    {
    public:
        ResourceHandle() noexcept = default;
        ResourceHandle(const ResourceHandle& other) noexcept;
        ResourceHandle(ResourceHandle&& other) noexcept;
        ~ResourceHandle();

        ResourceHandle& operator=(const ResourceHandle& other) noexcept;
        ResourceHandle& operator=(ResourceHandle&& other) noexcept;

        [[nodiscard]] ResourceObject* Get() const noexcept;
        [[nodiscard]] ResourcePath Path() const noexcept;
        [[nodiscard]] ResourceTypeId Type() const noexcept;
        [[nodiscard]] u32 Generation() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        void Reset() noexcept;
        [[nodiscard]] WeakResourceHandle ToWeak() const noexcept;

    private:
        struct AdoptTag
        {
        };

        ResourceHandle(ResourceRegistry* registry, u32 slot, u32 generation, ResourceKey key, AdoptTag) noexcept;

        ResourceRegistry* m_registry = nullptr;
        u32 m_slot = 0;
        u32 m_generation = 0;
        ResourceKey m_key;

        friend class ResourceRegistry;
        friend class WeakResourceHandle;
    };

    class WeakResourceHandle final
    {
    public:
        WeakResourceHandle() noexcept = default;
        WeakResourceHandle(const WeakResourceHandle& other) noexcept;
        WeakResourceHandle(WeakResourceHandle&& other) noexcept;
        ~WeakResourceHandle();

        WeakResourceHandle& operator=(const WeakResourceHandle& other) noexcept;
        WeakResourceHandle& operator=(WeakResourceHandle&& other) noexcept;

        [[nodiscard]] ResourceHandle Lock() const noexcept;
        [[nodiscard]] ResourcePath Path() const noexcept;
        [[nodiscard]] ResourceTypeId Type() const noexcept;
        [[nodiscard]] u32 Generation() const noexcept;
        [[nodiscard]] bool IsStale() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        void Reset() noexcept;

    private:
        struct AdoptTag
        {
        };

        WeakResourceHandle(ResourceRegistry* registry, u32 slot, u32 generation, ResourceKey key, AdoptTag) noexcept;

        ResourceRegistry* m_registry = nullptr;
        u32 m_slot = 0;
        u32 m_generation = 0;
        ResourceKey m_key;

        friend class ResourceHandle;
        friend class ResourceRegistry;
    };

    class ResourceRequest final
    {
    public:
        // Opaque shared operation state. Publicly nameable so the registry
        // implementation can store it without exposing its representation.
        struct Control;

        ResourceRequest() noexcept = default;
        ResourceRequest(const ResourceRequest& other) noexcept;
        ResourceRequest(ResourceRequest&& other) noexcept;
        ~ResourceRequest();

        ResourceRequest& operator=(const ResourceRequest& other) noexcept;
        ResourceRequest& operator=(ResourceRequest&& other) noexcept;

        [[nodiscard]] ResourceReference Reference() const noexcept;
        [[nodiscard]] State Status() const noexcept;
        [[nodiscard]] Failure Error() const noexcept;
        [[nodiscard]] bool HasFinished() const noexcept;
        [[nodiscard]] bool HasLoaded() const noexcept;
        [[nodiscard]] bool HasFailed() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] bool IsSameOperation(const ResourceRequest& other) const noexcept;

        void Wait() const noexcept;
        [[nodiscard]] bool TryWait(u32 timeoutMilliseconds = 0) const noexcept;
        [[nodiscard]] ResourceHandle Acquire() const noexcept;
        void Reset() noexcept;

    private:
        struct AdoptTag
        {
        };

        explicit ResourceRequest(Control* control) noexcept;
        ResourceRequest(Control* control, AdoptTag) noexcept;

        Control* m_control = nullptr;

        friend class ResourceRegistry;
    };

    using BeginLoadFunction = void (*)(ResourceRegistry& registry, const ResourceRequest& request, void* userData) noexcept;
    using DestroyResourceFunction = void (*)(ResourceObject* resource, void* userData) noexcept;

    struct LoaderDescriptor
    {
        ResourceTypeId type = InvalidResourceTypeId;
        const char* name = nullptr;
        BeginLoadFunction beginLoad = nullptr;
        DestroyResourceFunction destroyResource = nullptr;
        void* userData = nullptr;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return type != InvalidResourceTypeId && name != nullptr && name[0] != '\0' && beginLoad != nullptr &&
                   destroyResource != nullptr;
        }
    };

    struct RegistryStats
    {
        u32 registeredLoaders = 0;
        u32 knownResources = 0;
        u32 queuedResources = 0;
        u32 loadingResources = 0;
        u32 loadedResources = 0;
        u32 failedResources = 0;
        u32 cancelledResources = 0;
        u32 strongHandles = 0;
        u32 weakHandles = 0;
        u64 issuedRequests = 0;
        u64 coalescedRequests = 0;
    };

    class ResourceRegistry final
    {
    public:
        // Opaque registry storage; its representation remains source-private.
        struct Impl;

        ResourceRegistry() noexcept = default;
        ~ResourceRegistry();

        ResourceRegistry(const ResourceRegistry&) = delete;
        ResourceRegistry& operator=(const ResourceRegistry&) = delete;

        [[nodiscard]] bool Initialize() noexcept;
        // Startup and shutdown are composition-root operations and must be
        // externally serialized. Shutdown refuses live requests or handles.
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool RegisterLoader(const LoaderDescriptor& loader) noexcept;
        [[nodiscard]] bool UnregisterLoader(ResourceTypeId type) noexcept;
        [[nodiscard]] bool HasLoader(ResourceTypeId type) const noexcept;

        // The first request invokes the registered loader outside the registry
        // lock. Concurrent requests for the same key share one operation.
        [[nodiscard]] ResourceRequest Request(ResourceReference reference) noexcept;

        // Loader-facing completion contract. A loader explicitly transitions
        // Queued -> Loading, then publishes one matching object or failure.
        [[nodiscard]] bool BeginLoading(const ResourceRequest& request) noexcept;
        [[nodiscard]] bool Publish(const ResourceRequest& request, ResourceObject* resource) noexcept;
        [[nodiscard]] bool Fail(const ResourceRequest& request, Failure failure) noexcept;
        [[nodiscard]] bool Cancel(const ResourceRequest& request) noexcept;

        [[nodiscard]] ResourceHandle TryAcquire(ResourceReference reference) noexcept;
        [[nodiscard]] bool Evict(ResourcePath path) noexcept;
        [[nodiscard]] State GetState(ResourcePath path) const noexcept;
        [[nodiscard]] RegistryStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;

        [[nodiscard]] ResourceRequest MakeFailedRequest(ResourceReference reference, Failure failure) noexcept;

        void AddStrong(u32 slot, u32 generation) noexcept;
        void ReleaseStrong(u32 slot, u32 generation) noexcept;
        void AddWeak(u32 slot, u32 generation) noexcept;
        void ReleaseWeak(u32 slot, u32 generation) noexcept;
        [[nodiscard]] ResourceObject* Resolve(u32 slot, u32 generation) const noexcept;
        [[nodiscard]] ResourceHandle LockWeak(u32 slot, u32 generation, ResourceKey key) noexcept;
        [[nodiscard]] bool IsGenerationCurrent(u32 slot, u32 generation) const noexcept;
        [[nodiscard]] ResourceHandle AcquireRequest(const ResourceRequest& request) noexcept;

        friend class ResourceHandle;
        friend class WeakResourceHandle;
        friend class ResourceRequest;
    };
} // namespace vanguard::resources
