#include <vanguard/streaming/streaming.hpp>

#include <vanguard/streaming/resource_source_internal.hpp>

#include <vanguard/concurrency/concurrency.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/system/assert.hpp>

#include <cstring>
#include <limits>
#include <new>

namespace
{
    using namespace vanguard;

    template <typename T, typename... Args> [[nodiscard]] T* AllocateStreamingObject(Args&&... args) noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(T), alignof(T));
        if (!block)
        {
            return nullptr;
        }
        return ::new (block.address) T(static_cast<Args&&>(args)...);
    }

    template <typename T> void DeleteStreamingObject(T* const object) noexcept
    {
        if (object == nullptr)
        {
            return;
        }
        object->~T();
        memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Streaming};
        memory::Free(block);
    }

} // namespace

namespace vanguard::streaming
{
    io::AsyncPriority ToIoPriority(const resources::LoadPriority priority) noexcept
    {
        switch (priority)
        {
        case resources::LoadPriority::Background:
        case resources::LoadPriority::Low:
            return io::eAsyncPriority_Background;
        case resources::LoadPriority::Normal:
            return io::eAsyncPriority_Normal;
        case resources::LoadPriority::High:
            return io::eAsyncPriority_High;
        case resources::LoadPriority::Critical:
            return io::eAsyncPriority_GAMEPLAY_CRITICAL;
        }
        return io::eAsyncPriority_Normal;
    }

    resources::Failure ToFailure(const ResourceSourceResult result) noexcept
    {
        switch (result)
        {
        case ResourceSourceResult::Success:
            return resources::Failure::None;
        case ResourceSourceResult::NotFound:
            return resources::Failure::NotFound;
        case ResourceSourceResult::TypeMismatch:
        case ResourceSourceResult::IntegrityFailure:
            return resources::Failure::IntegrityFailure;
        case ResourceSourceResult::UnsupportedVersion:
            return resources::Failure::UnsupportedVersion;
        case ResourceSourceResult::LimitExceeded:
        case ResourceSourceResult::BufferTooSmall:
            return resources::Failure::OutOfMemory;
        case ResourceSourceResult::Cancelled:
            return resources::Failure::Cancelled;
        case ResourceSourceResult::IoFailure:
            return resources::Failure::IoFailure;
        default:
            return resources::Failure::InternalError;
        }
    }

    namespace
    {
        struct LooseEntry
        {
            LooseEntry() noexcept : dependencies(memory::pools::Streaming::GetInstance()) {}

            resources::ResourceReference reference;
            filesystem::AbsolutePath path;
            containers::DynamicArray<DependencyDescriptor> dependencies;
            u64 expectedCrc64 = 0;
            i32 priority = 0;
            u64 sequence = 0;
        };

        struct PackageMount
        {
            ~PackageMount()
            {
                detail::ReleaseResourceSourcePackageGeneration(generation);
            }

            const packages::PackageReader* reader = nullptr;
            detail::ResourceSourcePackageGeneration* generation = nullptr;
            filesystem::AbsolutePath path;
            i32 priority = 0;
            u64 sequence = 0;
        };

        struct ResolvedSource
        {
            SourceKind kind = SourceKind::LooseFile;
            LooseEntry* loose = nullptr;
            PackageMount* package = nullptr;
            const packages::Resource* packageResource = nullptr;
            i32 priority = 0;
            u64 sequence = 0;

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return loose != nullptr || (package != nullptr && packageResource != nullptr);
            }
        };

        struct StreamLoad
        {
            StreamLoad() noexcept
                : dependencies(memory::pools::Streaming::GetInstance())
            {
            }

            ResourceStreamer::Impl* owner = nullptr;
            resources::ResourceReference reference;
            DecoderDescriptor decoder;
            containers::DynamicArray<DependencyDescriptor> dependencies;
            ResourceSource source;
            ResourceReadRequest sourceRead;
            resources::PreparationRequest preparation;
            memory::MemoryBlock logicalData;
            u64 logicalSize = 0;
            u64 reservedBytes = 0;
            u64 expectedCrc64 = 0;
            bool preparationStarted = false;
            bool released = false;
        };

        [[nodiscard]] resources::Failure ConvertPackageFailure(const packages::Result result) noexcept
        {
            switch (result)
            {
            case packages::Result::Success:
                return resources::Failure::None;
            case packages::Result::IntegrityFailure:
                return resources::Failure::IntegrityFailure;
            case packages::Result::ResourceNotFound:
                return resources::Failure::NotFound;
            case packages::Result::UnsupportedVersion:
            case packages::Result::UnsupportedCodec:
                return resources::Failure::UnsupportedVersion;
            case packages::Result::BufferTooSmall:
            case packages::Result::LimitExceeded:
                return resources::Failure::OutOfMemory;
            case packages::Result::IoFailure:
                return resources::Failure::IoFailure;
            default:
                return resources::Failure::DeserializationFailure;
            }
        }

        [[nodiscard]] resources::Failure ConvertSchemaFailure(const schemas::Result result) noexcept
        {
            switch (result)
            {
            case schemas::Result::Success:
                return resources::Failure::None;
            case schemas::Result::IntegrityFailure:
            case schemas::Result::InvalidMagic:
            case schemas::Result::InvalidLayout:
                return resources::Failure::IntegrityFailure;
            case schemas::Result::UnsupportedVersion:
                return resources::Failure::UnsupportedVersion;
            case schemas::Result::OutOfMemory:
            case schemas::Result::LimitExceeded:
                return resources::Failure::OutOfMemory;
            case schemas::Result::IoFailure:
                return resources::Failure::IoFailure;
            default:
                return resources::Failure::DeserializationFailure;
            }
        }

        struct SchemaDependencyValidation
        {
            SchemaDependencyValidation(const resources::LoadContext& loadContext) noexcept
                : context(loadContext), matched(memory::pools::Streaming::GetInstance())
            {
                matched.Resize(context.GetDependencyCount());
                for (u32 index = 0; index < matched.Size(); ++index)
                {
                    matched[index] = 0;
                }
            }

            const resources::LoadContext& context;
            containers::DynamicArray<u8> matched;
            bool valid = true;
        };

        [[nodiscard]] bool ValidateSchemaDependency(const resources::ResourceReference reference, const resources::DependencyKind kind,
                                                    void* const userData) noexcept
        {
            auto& state = *static_cast<SchemaDependencyValidation*>(userData);
            if (kind == resources::DependencyKind::Soft)
            {
                return true;
            }
            const resources::DependencyRequirement required =
                kind == resources::DependencyKind::Optional ? resources::DependencyRequirement::Optional : resources::DependencyRequirement::Required;
            for (u32 index = 0; index < state.context.GetDependencyCount(); ++index)
            {
                if (state.matched[index] == 0 && state.context.GetDependencyReference(index) == reference &&
                    state.context.GetDependencyRequirementAt(index) == required)
                {
                    state.matched[index] = 1;
                    return true;
                }
            }
            state.valid = false;
            return false;
        }

        [[nodiscard]] resources::ResourceObject* DecodeSchemaResource(const resources::ResourceReference reference, const void* const data, const usize size,
                                                                      const resources::LoadContext& context, resources::Failure& failure,
                                                                      void* const userData) noexcept
        {
            const auto& descriptor = *static_cast<const SchemaDecoderDescriptor*>(userData);
            const reflection::Schema* const schema =
                descriptor.resolveSchema != nullptr ? descriptor.resolveSchema(context, descriptor.userData) : descriptor.schema;
            if (!descriptor.IsValid() || descriptor.type != reference.ExpectedType() || schema == nullptr || reflection::FindSchema(schema->id) != schema ||
                size > static_cast<usize>(~u32{0}) || context.IsCancellationRequested())
            {
                failure = context.IsCancellationRequested() ? resources::Failure::Cancelled : resources::Failure::InternalError;
                return nullptr;
            }

            resources::ResourceObject* const resource = descriptor.create(*schema, context, descriptor.userData);
            if (resource == nullptr)
            {
                failure = resources::Failure::OutOfMemory;
                return nullptr;
            }
            void* const object = descriptor.object(*resource, descriptor.userData);
            if (object == nullptr || resource->GetType() != descriptor.type)
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = resources::Failure::InternalError;
                return nullptr;
            }

            filesystem::MemoryFileReaderExternalBuffer file(static_cast<const u8*>(data), static_cast<u32>(size), nullptr);
            serialization::BinaryReader reader(file);
            const schemas::Result read = schemas::ReadObject(reader, *schema, object, descriptor.limits);
            if (read != schemas::Result::Success || reader.Position() != size)
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = read == schemas::Result::Success ? resources::Failure::IntegrityFailure : ConvertSchemaFailure(read);
                return nullptr;
            }

            SchemaDependencyValidation validation(context);
            if (validation.matched.Size() != context.GetDependencyCount())
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = resources::Failure::OutOfMemory;
                return nullptr;
            }
            const schemas::Result visited =
                schemas::VisitDependencies(*schema, object, &ValidateSchemaDependency, &validation, descriptor.limits.maximumNestingDepth);
            if (visited != schemas::Result::Success || !validation.valid)
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = resources::Failure::IntegrityFailure;
                return nullptr;
            }
            for (const u8 matched : validation.matched)
            {
                if (matched == 0)
                {
                    descriptor.destroy(resource, descriptor.userData);
                    failure = resources::Failure::IntegrityFailure;
                    return nullptr;
                }
            }
            if (descriptor.bindDependencies != nullptr && !descriptor.bindDependencies(*resource, context, descriptor.userData))
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = resources::Failure::DependencyFailure;
                return nullptr;
            }
            if (context.IsCancellationRequested())
            {
                descriptor.destroy(resource, descriptor.userData);
                failure = resources::Failure::Cancelled;
                return nullptr;
            }
            failure = resources::Failure::None;
            return resource;
        }

        void DestroySchemaResource(resources::ResourceObject* const resource, void* const userData) noexcept
        {
            const auto& descriptor = *static_cast<const SchemaDecoderDescriptor*>(userData);
            descriptor.destroy(resource, descriptor.userData);
        }
    } // namespace

    struct ResourceStreamer::Impl
    {
        Impl(resources::ResourcePipeline& resourcePipeline, const Config& streamerConfig) noexcept
            : pipeline(&resourcePipeline), config(streamerConfig), decoders(memory::pools::Streaming::GetInstance()),
              decoderTypes(memory::pools::Streaming::GetInstance()), looseEntries(memory::pools::Streaming::GetInstance()),
              packageMounts(memory::pools::Streaming::GetInstance()), activeLoads(memory::pools::Streaming::GetInstance())
        {
        }

        resources::ResourcePipeline* pipeline = nullptr;
        Config config;
        concurrency::RWLock lock;
        containers::HashMap<resources::ResourceTypeId, DecoderDescriptor> decoders;
        containers::DynamicArray<resources::ResourceTypeId> decoderTypes;
        containers::DynamicArray<LooseEntry*> looseEntries;
        containers::DynamicArray<PackageMount*> packageMounts;
        containers::HashMap<resources::ResourceId, StreamLoad*> activeLoads;
        u64 nextSequence = 1;
        u64 completedLoads = 0;
        u64 failedLoads = 0;
        u64 cancelledLoads = 0;
        u64 integrityFailures = 0;
        detail::ResourceSourceAccounting* sourceAccounting = nullptr;

        [[nodiscard]] ResolvedSource ResolveLocked(const resources::ResourceId id) noexcept
        {
            ResolvedSource best;
            bool found = false;
            for (LooseEntry* const loose : looseEntries)
            {
                if (loose->reference.GetPath().Id() != id)
                {
                    continue;
                }
                if (!found || loose->priority > best.priority || (loose->priority == best.priority && loose->sequence > best.sequence))
                {
                    best = {SourceKind::LooseFile, loose, nullptr, nullptr, loose->priority, loose->sequence};
                    found = true;
                }
            }
            for (PackageMount* const mount : packageMounts)
            {
                const packages::Resource* const resource = mount->reader->Find(id);
                if (resource == nullptr)
                {
                    continue;
                }
                if (!found || mount->priority > best.priority || (mount->priority == best.priority && mount->sequence > best.sequence))
                {
                    best = {SourceKind::Package, nullptr, mount, resource, mount->priority, mount->sequence};
                    found = true;
                }
            }
            return best;
        }

        void ReleaseBudgetLocked(StreamLoad& load) noexcept
        {
            if (load.reservedBytes != 0)
            {
                detail::ReleaseResourceSourceStaging(sourceAccounting, load.reservedBytes);
                load.reservedBytes = 0;
            }
        }

        void ReleaseLoad(StreamLoad& load) noexcept
        {
            lock.Acquire();
            if (load.released)
            {
                lock.Release();
                return;
            }
            load.released = true;
            StreamLoad* current = nullptr;
            if (activeLoads.Find(load.reference.GetPath().Id(), current) && current == &load)
            {
                static_cast<void>(activeLoads.Remove(load.reference.GetPath().Id()));
            }
            ReleaseBudgetLocked(load);
            lock.Release();

            load.sourceRead.Reset();
            memory::Free(load.logicalData);
            DeleteStreamingObject(&load);
        }

        [[nodiscard]] bool ReserveStaging(StreamLoad& load, const u64 requestedBytes) noexcept
        {
            if (!detail::ReserveResourceSourceStaging(sourceAccounting, requestedBytes))
                return false;
            load.reservedBytes = requestedBytes;
            return true;
        }

        [[nodiscard]] resources::Failure OpenResolvedLocked(const resources::ResourceReference reference, ResourceSource& output,
                                                            containers::DynamicArray<DependencyDescriptor>& dependencies,
                                                            u64* const expectedCrc64 = nullptr) noexcept
        {
            const ResolvedSource source = ResolveLocked(reference.GetPath().Id());
            if (!source)
                return resources::Failure::NotFound;
            const resources::ResourceTypeId sourceType =
                source.kind == SourceKind::LooseFile ? source.loose->reference.ExpectedType() : source.packageResource->type;
            if (sourceType != reference.ExpectedType())
                return resources::Failure::IntegrityFailure;

            ResourceSourceResult opened = ResourceSourceResult::InvalidState;
            if (source.kind == SourceKind::LooseFile)
            {
                if (expectedCrc64 != nullptr)
                    *expectedCrc64 = source.loose->expectedCrc64;
                for (const DependencyDescriptor& dependency : source.loose->dependencies)
                {
                    if (dependency.kind != resources::DependencyKind::Soft)
                        dependencies.PushBack(dependency);
                }
                opened = output.OpenLoose(source.loose->path, reference.GetPath().Id(), reference.ExpectedType());
            }
            else
            {
                if (expectedCrc64 != nullptr)
                    *expectedCrc64 = source.packageResource->contentCrc64;
                if (source.package->reader->GetSegments(*source.packageResource).Count() > config.maximumSegmentsPerResource)
                    return resources::Failure::DependencyLimit;
                const auto packagedDependencies = source.package->reader->GetDependencies(*source.packageResource);
                if (packagedDependencies.Count() > config.maximumDependenciesPerResource)
                    return resources::Failure::DependencyLimit;
                for (const packages::Dependency& dependency : packagedDependencies)
                {
                    if (dependency.kind != resources::DependencyKind::Soft)
                        dependencies.PushBack({resources::ResourceReference(resources::ResourcePath::FromId(dependency.id), dependency.type), dependency.kind});
                }
                opened = output.OpenPackage(source.package->generation, reference.GetPath().Id(), reference.ExpectedType());
            }
            if (opened != ResourceSourceResult::Success)
            {
                dependencies.Clear();
                return ToFailure(opened);
            }
            output.AttachAccounting(sourceAccounting);
            return resources::Failure::None;
        }

        [[nodiscard]] resources::Failure CreateLoad(const resources::ResourceReference reference, StreamLoad*& output) noexcept
        {
            output = nullptr;
            lock.Acquire();
            StreamLoad* existing = nullptr;
            if (activeLoads.Find(reference.GetPath().Id(), existing))
            {
                lock.Release();
                return resources::Failure::InternalError;
            }

            DecoderDescriptor decoder;
            if (!decoders.Find(reference.ExpectedType(), decoder))
            {
                lock.Release();
                return resources::Failure::UnknownType;
            }
            StreamLoad* const load = AllocateStreamingObject<StreamLoad>();
            if (load == nullptr)
            {
                lock.Release();
                return resources::Failure::OutOfMemory;
            }
            load->owner = this;
            load->reference = reference;
            load->decoder = decoder;
            const resources::Failure opened = OpenResolvedLocked(reference, load->source, load->dependencies, &load->expectedCrc64);
            if (opened != resources::Failure::None)
            {
                DeleteStreamingObject(load);
                lock.Release();
                return opened;
            }
            load->logicalSize = load->source.GetLogicalSize();

            if (!activeLoads.Insert(reference.GetPath().Id(), load).IsSuccessful())
            {
                DeleteStreamingObject(load);
                lock.Release();
                return resources::Failure::OutOfMemory;
            }
            lock.Release();
            output = load;
            return resources::Failure::None;
        }

        [[nodiscard]] bool StartPreparation(StreamLoad& load, const resources::PreparationRequest& preparation) noexcept
        {
            load.preparation = preparation;
            load.preparationStarted = true;
            if (preparation.IsCancellationRequested())
            {
                static_cast<void>(preparation.TakeLoaderState());
                static_cast<void>(preparation.Complete(resources::Failure::Cancelled));
                ReleaseLoad(load);
                return true;
            }

            if (load.logicalSize > config.maximumResourceBytes || load.logicalSize > static_cast<u64>(std::numeric_limits<usize>::max()))
            {
                static_cast<void>(preparation.TakeLoaderState());
                static_cast<void>(preparation.Complete(resources::Failure::OutOfMemory));
                ReleaseLoad(load);
                return true;
            }
            if (!ReserveStaging(load, load.logicalSize))
            {
                static_cast<void>(preparation.TakeLoaderState());
                static_cast<void>(preparation.Complete(resources::Failure::OutOfMemory));
                ReleaseLoad(load);
                return true;
            }

            if (load.logicalSize != 0)
            {
                load.logicalData = memory::Allocate(memory::PoolId::Streaming, static_cast<usize>(load.logicalSize), 16);
                if (!load.logicalData)
                {
                    static_cast<void>(preparation.TakeLoaderState());
                    static_cast<void>(preparation.Complete(resources::Failure::OutOfMemory));
                    ReleaseLoad(load);
                    return true;
                }
            }
            const ResourceRangeRead read{0, load.logicalSize, load.logicalData.address, static_cast<usize>(load.logicalSize), ToIoPriority(preparation.Priority())};
            const ResourceSourceResult started = load.source.ReadAsync(read, load.sourceRead, &SourceReadCompletedThunk, &load);
            if (started != ResourceSourceResult::Success)
            {
                static_cast<void>(preparation.TakeLoaderState());
static_cast<void>(preparation.Complete(ToFailure(started)));
                ReleaseLoad(load);
            }
            return true;
        }

        static void SourceReadCompletedThunk(const ResourceSourceResult result, const ResourceReadStats&, void* const userData)
        {
            auto& load = *static_cast<StreamLoad*>(userData);
            load.owner->OnSourceReadCompleted(load, result);
        }

        void OnSourceReadCompleted(StreamLoad& load, const ResourceSourceResult result) noexcept
        {
            load.sourceRead.Reset();
resources::Failure failure = ToFailure(result);
            if (load.preparation.IsCancellationRequested())
                failure = resources::Failure::Cancelled;
            static_cast<void>(load.preparation.Complete(failure));
            if (failure != resources::Failure::None)
            {
                static_cast<void>(load.preparation.TakeLoaderState());
                lock.Acquire();
                if (failure == resources::Failure::Cancelled)
                    ++cancelledLoads;
                else
                {
                    ++failedLoads;
                    if (failure == resources::Failure::IntegrityFailure)
                        ++integrityFailures;
                }
                lock.Release();
                ReleaseLoad(load);
            }
        }

        void CancelPreparation(const resources::PreparationRequest& request) noexcept
        {
            auto* const load = static_cast<StreamLoad*>(request.TakeLoaderState());
            if (load == nullptr)
                return;
            if (!load->sourceRead.IsValid() || load->sourceRead.HasFinished())
            {
                ReleaseLoad(*load);
                return;
            }
            static_cast<void>(load->sourceRead.Cancel());
        }

        [[nodiscard]] resources::ResourceObject* Decode(const resources::LoadContext& context, resources::Failure& failure) noexcept
        {
            auto* const load = static_cast<StreamLoad*>(context.TakeLoaderState());
            if (load == nullptr)
            {
                failure = resources::Failure::InternalError;
                return nullptr;
            }

            if (serialization::Crc64(load->logicalData.address, static_cast<usize>(load->logicalSize)) != load->expectedCrc64 && load->expectedCrc64 != 0)
            {
                failure = resources::Failure::IntegrityFailure;
                lock.Acquire();
                ++failedLoads;
                ++integrityFailures;
                lock.Release();
                ReleaseLoad(*load);
                return nullptr;
            }

            resources::ResourceObject* const resource = load->decoder.decode(load->reference, load->logicalData.address, static_cast<usize>(load->logicalSize),
                                                                             context, failure, load->decoder.userData);
            lock.Acquire();
            if (resource != nullptr)
            {
                ++completedLoads;
            }
            else
            {
                ++failedLoads;
            }
            lock.Release();
            ReleaseLoad(*load);
            return resource;
        }

        static resources::Failure DiscoverDependencies(const resources::ResourceReference reference, resources::DependencyBuilder& dependencies,
                                                       void* const userData) noexcept
        {
            auto& self = *static_cast<Impl*>(userData);
            StreamLoad* load = nullptr;
            const resources::Failure failure = self.CreateLoad(reference, load);
            if (failure != resources::Failure::None)
            {
                return failure;
            }
            if (!dependencies.SetLoaderState(load))
            {
                self.ReleaseLoad(*load);
                return resources::Failure::InternalError;
            }
            for (const DependencyDescriptor& dependency : load->dependencies)
            {
                if (dependency.kind == resources::DependencyKind::Soft)
                {
                    continue;
                }
                const resources::DependencyRequirement requirement = dependency.kind == resources::DependencyKind::Optional
                                                                         ? resources::DependencyRequirement::Optional
                                                                         : resources::DependencyRequirement::Required;
                if (!dependencies.Add(dependency.reference, requirement))
                {
                    auto* const owned = static_cast<StreamLoad*>(dependencies.TakeLoaderState());
                    if (owned != nullptr)
                        self.ReleaseLoad(*owned);
                    return resources::Failure::DependencyLimit;
                }
            }
            return resources::Failure::None;
        }

        static bool BeginPreparation(const resources::PreparationRequest& request, void* const userData) noexcept
        {
            auto& self = *static_cast<Impl*>(userData);
            auto* const load = static_cast<StreamLoad*>(request.GetLoaderState());
            return load != nullptr && self.StartPreparation(*load, request);
        }

        static void CancelPreparation(const resources::PreparationRequest& request, void* const userData) noexcept
        {
            static_cast<Impl*>(userData)->CancelPreparation(request);
        }

        static resources::ResourceObject* Construct(const resources::LoadContext& context, resources::Failure& failure, void* const userData) noexcept
        {
            return static_cast<Impl*>(userData)->Decode(context, failure);
        }

        static void Destroy(resources::ResourceObject* const resource, void* const userData) noexcept
        {
            auto& self = *static_cast<Impl*>(userData);
            DecoderDescriptor decoder;
            self.lock.AcquireShared();
            static_cast<void>(self.decoders.Find(resource->GetType(), decoder));
            self.lock.ReleaseShared();
            if (decoder.destroy != nullptr)
            {
                decoder.destroy(resource, decoder.userData);
            }
        }
    };

    ResourceStreamer::~ResourceStreamer()
    {
        static_cast<void>(Shutdown());
    }

    bool ResourceStreamer::Initialize(resources::ResourcePipeline& pipeline, const Config& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return true;
        }
        if (!pipeline.IsInitialized() || !filesystem::IsInitialized() || !io::IsInitialized() || config.stagingBudgetBytes == 0 ||
            config.maximumResourceBytes == 0 || config.maximumDependenciesPerResource == 0 || config.maximumSegmentsPerResource == 0)
        {
            return false;
        }
        m_impl = AllocateStreamingObject<Impl>(pipeline, config);
        if (m_impl == nullptr)
            return false;
        m_impl->sourceAccounting = detail::CreateResourceSourceAccounting(config.stagingBudgetBytes);
        if (m_impl->sourceAccounting == nullptr)
        {
            DeleteStreamingObject(m_impl);
            m_impl = nullptr;
            return false;
        }
        return true;
    }

    bool ResourceStreamer::Shutdown() noexcept
    {
        if (m_impl == nullptr)
        {
            return true;
        }
        m_impl->lock.AcquireShared();
        const detail::ResourceSourceAccountingStats sourceStats = detail::GetResourceSourceAccountingStats(m_impl->sourceAccounting);
        const bool busy = !m_impl->activeLoads.Empty() || sourceStats.activeReads != 0 || sourceStats.stagingBytesInUse != 0;
        m_impl->lock.ReleaseShared();
        if (busy)
        {
            return false;
        }

        while (!m_impl->decoderTypes.Empty())
        {
            const resources::ResourceTypeId type = m_impl->decoderTypes.Back();
            if (!UnregisterDecoder(type))
            {
                return false;
            }
        }
        for (LooseEntry* const loose : m_impl->looseEntries)
        {
            DeleteStreamingObject(loose);
        }
        for (PackageMount* const mount : m_impl->packageMounts)
        {
            DeleteStreamingObject(mount);
        }
        detail::ReleaseResourceSourceAccounting(m_impl->sourceAccounting);
        DeleteStreamingObject(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ResourceStreamer::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool ResourceStreamer::RegisterDecoder(const DecoderDescriptor& decoder) noexcept
    {
        if (m_impl == nullptr || !decoder.IsValid())
        {
            return false;
        }
        m_impl->lock.Acquire();
        if (!m_impl->decoders.Insert(decoder.type, decoder).IsSuccessful())
        {
            m_impl->lock.Release();
            return false;
        }
        m_impl->decoderTypes.PushBack(decoder.type);
        m_impl->lock.Release();

        const resources::AsyncLoaderDescriptor loader{decoder.type,   decoder.name, &Impl::DiscoverDependencies, &Impl::Construct,
                                                      &Impl::Destroy, m_impl,       &Impl::BeginPreparation,     &Impl::CancelPreparation};
        if (!m_impl->pipeline->RegisterLoader(loader))
        {
            m_impl->lock.Acquire();
            static_cast<void>(m_impl->decoders.Remove(decoder.type));
            m_impl->decoderTypes.PopBack();
            m_impl->lock.Release();
            return false;
        }
        return true;
    }

    bool ResourceStreamer::RegisterSchemaDecoder(const SchemaDecoderDescriptor& decoder) noexcept
    {
        if (!decoder.IsValid() || (decoder.schema != nullptr && reflection::FindSchema(decoder.schema->id) != decoder.schema))
        {
            return false;
        }
        return RegisterDecoder({decoder.type, decoder.name, &DecodeSchemaResource, &DestroySchemaResource, const_cast<SchemaDecoderDescriptor*>(&decoder)});
    }

    bool ResourceStreamer::UnregisterDecoder(const resources::ResourceTypeId type) noexcept
    {
        if (m_impl == nullptr || !m_impl->pipeline->UnregisterLoader(type))
        {
            return false;
        }
        m_impl->lock.Acquire();
        const bool removed = m_impl->decoders.Remove(type).IsSuccessful();
        for (u32 index = 0; index < m_impl->decoderTypes.Size(); ++index)
        {
            if (m_impl->decoderTypes[index] == type)
            {
                static_cast<void>(m_impl->decoderTypes.RemoveAt(index));
                break;
            }
        }
        m_impl->lock.Release();
        return removed;
    }

    bool ResourceStreamer::RegisterLoose(const LooseResourceDescriptor& resource) noexcept
    {
        if (m_impl == nullptr || !resource.IsValid() || resource.dependencies.Count() > m_impl->config.maximumDependenciesPerResource ||
            !filesystem::GetManager().FileExist(resource.physicalPath))
        {
            return false;
        }
        LooseEntry* const entry = AllocateStreamingObject<LooseEntry>();
        if (entry == nullptr)
        {
            return false;
        }
        entry->reference = resource.reference;
        entry->path = resource.physicalPath;
        entry->expectedCrc64 = resource.expectedContentCrc64;
        entry->priority = resource.priority;
        for (const DependencyDescriptor& dependency : resource.dependencies)
        {
            if (!dependency.IsValid())
            {
                DeleteStreamingObject(entry);
                return false;
            }
            entry->dependencies.PushBack(dependency);
        }

        m_impl->lock.Acquire();
        for (const LooseEntry* const existing : m_impl->looseEntries)
        {
            if (existing->reference.GetPath() == entry->reference.GetPath())
            {
                m_impl->lock.Release();
                DeleteStreamingObject(entry);
                return false;
            }
        }
        entry->sequence = m_impl->nextSequence++;
        m_impl->looseEntries.PushBack(entry);
        m_impl->lock.Release();
        return true;
    }

    bool ResourceStreamer::UnregisterLoose(const resources::ResourcePath path) noexcept
    {
        if (m_impl == nullptr || !path.IsValid())
        {
            return false;
        }
        m_impl->lock.Acquire();
        StreamLoad* active = nullptr;
        if (m_impl->activeLoads.Find(path.Id(), active))
        {
            m_impl->lock.Release();
            return false;
        }
        for (u32 index = 0; index < m_impl->looseEntries.Size(); ++index)
        {
            LooseEntry* const entry = m_impl->looseEntries[index];
            if (entry->reference.GetPath() == path)
            {
                static_cast<void>(m_impl->looseEntries.RemoveAt(index));
                m_impl->lock.Release();
                DeleteStreamingObject(entry);
                return true;
            }
        }
        m_impl->lock.Release();
        return false;
    }

    bool ResourceStreamer::MountPackage(const packages::PackageReader& reader, const filesystem::AbsolutePath& physicalPath, const i32 priority) noexcept
    {
        const PackageMountDescriptor mount{&reader, physicalPath, priority};
        return MountPackages({&mount, 1});
    }

    bool ResourceStreamer::UnmountPackage(const packages::PackageReader& reader) noexcept
    {
        const PackageMountDescriptor mount{&reader, {}, 0};
        return UnmountPackages({&mount, 1});
    }

    bool ResourceStreamer::MountPackages(const containers::ArraySpan<const PackageMountDescriptor> mounts) noexcept
    {
        if (m_impl == nullptr || mounts.Empty())
        {
            return false;
        }
        containers::DynamicArray<PackageMount*> pending(memory::pools::Streaming::GetInstance());
        pending.Reserve(mounts.Count());
        if (pending.Capacity() < mounts.Count())
        {
            return false;
        }
        for (u32 index = 0; index < mounts.Count(); ++index)
        {
            const PackageMountDescriptor& descriptor = mounts[index];
            if (!descriptor.IsValid() || !filesystem::GetManager().FileExist(descriptor.physicalPath))
            {
                for (PackageMount* const mount : pending)
                {
                    DeleteStreamingObject(mount);
                }
                return false;
            }
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (mounts[previous].reader == descriptor.reader)
                {
                    for (PackageMount* const mount : pending)
                    {
                        DeleteStreamingObject(mount);
                    }
                    return false;
                }
            }
            PackageMount* const mount = AllocateStreamingObject<PackageMount>();
            if (mount == nullptr)
            {
                for (PackageMount* const allocated : pending)
                {
                    DeleteStreamingObject(allocated);
                }
                return false;
            }
            mount->reader = descriptor.reader;
            mount->path = descriptor.physicalPath;
            mount->priority = descriptor.priority;
            mount->generation = detail::CreateResourceSourcePackageGeneration(descriptor.physicalPath);
            const packages::PackageReader* const ownedReader = detail::GetResourceSourcePackageReader(mount->generation);
            if (ownedReader == nullptr || ownedReader->GetHeader().packageId != descriptor.reader->GetHeader().packageId ||
                ownedReader->GetHeader().buildId != descriptor.reader->GetHeader().buildId ||
                ownedReader->GetHeader().indexCrc64 != descriptor.reader->GetHeader().indexCrc64 ||
                ownedReader->GetHeader().fileSize != descriptor.reader->GetHeader().fileSize)
            {
                DeleteStreamingObject(mount);
                for (PackageMount* const allocated : pending)
                    DeleteStreamingObject(allocated);
                return false;
            }
            pending.PushBack(mount);
        }

        m_impl->lock.Acquire();
        for (const PackageMount* const existing : m_impl->packageMounts)
        {
            for (const PackageMount* const mount : pending)
            {
                if (existing->reader == mount->reader)
                {
                    m_impl->lock.Release();
                    for (PackageMount* const allocated : pending)
                    {
                        DeleteStreamingObject(allocated);
                    }
                    return false;
                }
            }
        }
        m_impl->packageMounts.Reserve(m_impl->packageMounts.Size() + pending.Size());
        if (m_impl->packageMounts.Capacity() < m_impl->packageMounts.Size() + pending.Size())
        {
            m_impl->lock.Release();
            for (PackageMount* const mount : pending)
            {
                DeleteStreamingObject(mount);
            }
            return false;
        }
        for (PackageMount* const mount : pending)
        {
            mount->sequence = m_impl->nextSequence++;
            m_impl->packageMounts.PushBack(mount);
        }
        m_impl->lock.Release();
        return true;
    }

    bool ResourceStreamer::UnmountPackages(const containers::ArraySpan<const PackageMountDescriptor> mounts) noexcept
    {
        if (m_impl == nullptr || mounts.Empty())
        {
            return false;
        }
        containers::DynamicArray<PackageMount*> removed(memory::pools::Streaming::GetInstance());
        removed.Resize(mounts.Count());
        if (removed.Size() != mounts.Count())
        {
            return false;
        }
        for (u32 index = 0; index < mounts.Count(); ++index)
        {
            if (mounts[index].reader == nullptr)
            {
                return false;
            }
            for (u32 previous = 0; previous < index; ++previous)
            {
                if (mounts[previous].reader == mounts[index].reader)
                {
                    return false;
                }
            }
        }

        m_impl->lock.Acquire();
        for (u32 descriptorIndex = 0; descriptorIndex < mounts.Count(); ++descriptorIndex)
        {
            PackageMount* found = nullptr;
            for (PackageMount* const mount : m_impl->packageMounts)
            {
                if (mount->reader == mounts[descriptorIndex].reader)
                {
                    found = mount;
                    break;
                }
            }
            if (found == nullptr)
            {
                m_impl->lock.Release();
                return false;
            }
            removed[descriptorIndex] = found;
        }
        for (PackageMount* const mount : removed)
        {
            for (u32 index = 0; index < m_impl->packageMounts.Size(); ++index)
            {
                if (m_impl->packageMounts[index] == mount)
                {
                    static_cast<void>(m_impl->packageMounts.RemoveAt(index));
                    break;
                }
            }
        }
        m_impl->lock.Release();
        for (PackageMount* const mount : removed)
        {
            DeleteStreamingObject(mount);
        }
        return true;
    }

    resources::PipelineRequest ResourceStreamer::Request(const resources::ResourceReference reference, const resources::LoadPriority priority) noexcept
    {
        return m_impl != nullptr ? m_impl->pipeline->Request(reference, priority) : resources::PipelineRequest{};
    }

    resources::Failure ResourceStreamer::OpenSource(const resources::ResourceReference reference, ResourceSource& output,
                                                    containers::DynamicArray<DependencyDescriptor>& dependencies) noexcept
    {
        dependencies.Clear();
        if (m_impl == nullptr || output.IsOpen() || !reference.IsValid() || !reference.IsTyped())
            return resources::Failure::InternalError;

        m_impl->lock.AcquireShared();
        const resources::Failure opened = m_impl->OpenResolvedLocked(reference, output, dependencies);
        m_impl->lock.ReleaseShared();
        return opened;
    }

    Stats ResourceStreamer::GetStats() const noexcept
    {
        Stats stats;
        if (m_impl == nullptr)
        {
            return stats;
        }
        VG_SCOPE_SHARED_LOCK(m_impl->lock);
        const detail::ResourceSourceAccountingStats sourceStats = detail::GetResourceSourceAccountingStats(m_impl->sourceAccounting);
        stats.registeredDecoders = m_impl->decoderTypes.Size();
        stats.looseResources = m_impl->looseEntries.Size();
        stats.mountedPackages = m_impl->packageMounts.Size();
        stats.activeLoads = m_impl->activeLoads.Size();
        stats.activeReads = sourceStats.activeReads;
        stats.stagingBudgetBytes = m_impl->config.stagingBudgetBytes;
        stats.stagingBytesInUse = sourceStats.stagingBytesInUse;
        stats.peakStagingBytes = sourceStats.peakStagingBytes;
        stats.bytesRead = sourceStats.bytesRead;
        stats.completedLoads = m_impl->completedLoads;
        stats.failedLoads = m_impl->failedLoads;
        stats.cancelledLoads = m_impl->cancelledLoads;
        stats.integrityFailures = m_impl->integrityFailures;
        stats.budgetRejections = sourceStats.budgetRejections;
        return stats;
    }

    StagingReservation::~StagingReservation()
    {
        Reset();
    }

    StagingReservation::StagingReservation(StagingReservation&& other) noexcept : m_accounting(other.m_accounting), m_bytes(other.m_bytes)
    {
        other.m_accounting = nullptr;
        other.m_bytes = 0;
    }

    StagingReservation& StagingReservation::operator=(StagingReservation&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_accounting = other.m_accounting;
            m_bytes = other.m_bytes;
            other.m_accounting = nullptr;
            other.m_bytes = 0;
        }
        return *this;
    }

    void StagingReservation::Reset() noexcept
    {
        detail::ReleaseResourceSourceStaging(m_accounting, m_bytes);
        detail::ReleaseResourceSourceAccounting(m_accounting);
        m_accounting = nullptr;
        m_bytes = 0;
    }

    u64 StagingReservation::GetBytes() const noexcept
    {
        return m_bytes;
    }

    bool ResourceStreamer::ReserveStaging(const u64 bytes, StagingReservation& reservation) noexcept
    {
        if (m_impl == nullptr || (reservation.m_accounting != nullptr && reservation.m_accounting != m_impl->sourceAccounting))
            return false;
        if (bytes > reservation.m_bytes)
        {
            if (!detail::ReserveResourceSourceStaging(m_impl->sourceAccounting, bytes - reservation.m_bytes))
                return false;
        }
        else
        {
            detail::ReleaseResourceSourceStaging(m_impl->sourceAccounting, reservation.m_bytes - bytes);
        }
        if (reservation.m_accounting == nullptr)
        {
            detail::RetainResourceSourceAccounting(m_impl->sourceAccounting);
            reservation.m_accounting = m_impl->sourceAccounting;
        }
        reservation.m_bytes = bytes;
        return true;
    }

    namespace
    {
        struct OwnedSetPackage
        {
            packages::PackageReader reader;
            MountedPackageInfo info;
        };

        [[nodiscard]] bool BuildDataPath(const filesystem::AbsolutePath& directory, const u32 packageNumber, filesystem::AbsolutePath& path) noexcept
        {
            char name[13]{};
            usize written = 0;
            if (packages::FormatPackageFileName(packageNumber, name, sizeof(name), written) != packages::Result::Success)
            {
                return false;
            }
            path = directory.AddFilePath(name);
            return !path.Empty();
        }

        [[nodiscard]] bool IsOptional(const packages::PackageSetEntryFlags flags) noexcept
        {
            return (static_cast<u32>(flags) & static_cast<u32>(packages::PackageSetEntryFlags::Optional)) != 0;
        }

        [[nodiscard]] bool HasOverride(const packages::PackageSetEntryFlags flags) noexcept
        {
            return (static_cast<u32>(flags) & static_cast<u32>(packages::PackageSetEntryFlags::Override)) != 0;
        }

        [[nodiscard]] PackageSetMountResult ConvertCatalogOpenFailure(const packages::Result result, const bool digestVerification) noexcept
        {
            if (result == packages::Result::IntegrityFailure)
            {
                return digestVerification ? PackageSetMountResult::PackageDigestMismatch : PackageSetMountResult::PackageMetadataMismatch;
            }
            if (result == packages::Result::IoFailure)
            {
                return PackageSetMountResult::IoFailure;
            }
            if (result == packages::Result::LimitExceeded || result == packages::Result::BufferTooSmall)
            {
                return PackageSetMountResult::OutOfMemory;
            }
            return PackageSetMountResult::PackageOpenFailed;
        }
    } // namespace

    struct PackageSetMount::Impl
    {
        Impl() noexcept : packages(memory::pools::Streaming::GetInstance()) {}

        ResourceStreamer* streamer = nullptr;
        containers::DynamicArray<OwnedSetPackage*> packages;
        u64 gameId = 0;
        u64 buildId = 0;
        u32 targetPlatformId = 0;
        resources::ResourceReference startupWorld;
        resources::ResourceReference defaultInput;
        bool mounted = false;
    };

    namespace
    {
        void DeletePackageSetImpl(PackageSetMount::Impl* const impl) noexcept
        {
            if (impl == nullptr)
            {
                return;
            }
            for (OwnedSetPackage* const package : impl->packages)
            {
                package->reader.Close();
                DeleteStreamingObject(package);
            }
            DeleteStreamingObject(impl);
        }

        [[nodiscard]] bool AddOwnedPackage(PackageSetMount::Impl& impl, OwnedSetPackage* const package) noexcept
        {
            const u32 previous = impl.packages.Size();
            impl.packages.PushBack(package);
            return impl.packages.Size() == previous + 1u;
        }

        [[nodiscard]] bool HasAuthorizedOverrides(const PackageSetMount::Impl& impl, const OwnedSetPackage& candidate) noexcept
        {
            for (const OwnedSetPackage* const existing : impl.packages)
            {
                const bool candidateWins = candidate.info.priority >= existing->info.priority;
                if (!candidateWins)
                {
                    continue;
                }
                for (const packages::Resource& resource : candidate.reader.GetResources())
                {
                    if (existing->reader.Find(resource.id) != nullptr && !HasOverride(candidate.info.flags))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    const char* ToString(const PackageSetMountResult result) noexcept
    {
        switch (result)
        {
        case PackageSetMountResult::Success:
            return "Success";
        case PackageSetMountResult::InvalidArgument:
            return "InvalidArgument";
        case PackageSetMountResult::AlreadyMounted:
            return "AlreadyMounted";
        case PackageSetMountResult::MissingRootPackage:
            return "MissingRootPackage";
        case PackageSetMountResult::RootOpenFailed:
            return "RootOpenFailed";
        case PackageSetMountResult::MissingPackageSet:
            return "MissingPackageSet";
        case PackageSetMountResult::GameMismatch:
            return "GameMismatch";
        case PackageSetMountResult::BuildMismatch:
            return "BuildMismatch";
        case PackageSetMountResult::TargetMismatch:
            return "TargetMismatch";
        case PackageSetMountResult::MissingRequiredPackage:
            return "MissingRequiredPackage";
        case PackageSetMountResult::PackageOpenFailed:
            return "PackageOpenFailed";
        case PackageSetMountResult::PackageMetadataMismatch:
            return "PackageMetadataMismatch";
        case PackageSetMountResult::PackageDigestMismatch:
            return "PackageDigestMismatch";
        case PackageSetMountResult::MountFailed:
            return "MountFailed";
        case PackageSetMountResult::Busy:
            return "Busy";
        case PackageSetMountResult::OutOfMemory:
            return "OutOfMemory";
        case PackageSetMountResult::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    PackageSetMount::~PackageSetMount()
    {
        if (m_impl != nullptr && m_impl->mounted)
        {
            system::ReportFatalFailure("PackageSetMount requires explicit Unmount before destruction.");
        }
        if (m_impl != nullptr)
        {
            DeletePackageSetImpl(m_impl);
            m_impl = nullptr;
        }
    }

    PackageSetMountResult PackageSetMount::Mount(ResourceStreamer& streamer, const filesystem::AbsolutePath& gameDirectory,
                                                 const PackageSetMountConfig& config) noexcept
    {
        if (m_impl != nullptr)
        {
            return PackageSetMountResult::AlreadyMounted;
        }
        if (!streamer.IsInitialized() || gameDirectory.Empty() || !gameDirectory.IsDirectoryPath() || config.hashBufferBytes == 0 ||
            config.verification > packages::CatalogVerification::WholeFileDigest)
        {
            return PackageSetMountResult::InvalidArgument;
        }

        Impl* const impl = AllocateStreamingObject<Impl>();
        if (impl == nullptr)
        {
            return PackageSetMountResult::OutOfMemory;
        }
        impl->streamer = &streamer;
        filesystem::AbsolutePath rootPath;
        if (!BuildDataPath(gameDirectory, 0, rootPath))
        {
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::InvalidArgument;
        }
        filesystem::Manager& files = filesystem::GetManager();
        if (!files.FileExist(rootPath))
        {
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::MissingRootPackage;
        }

        OwnedSetPackage* const root = AllocateStreamingObject<OwnedSetPackage>();
        if (root == nullptr)
        {
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::OutOfMemory;
        }
        auto rootFile = files.CreateFileReader(rootPath, filesystem::FOF_Buffered);
        if (!rootFile)
        {
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::IoFailure;
        }
        const packages::Result rootOpened = root->reader.Open(*rootFile, config.packageLimits, config.packageSetLimits);
        rootFile.Reset();
        if (rootOpened != packages::Result::Success)
        {
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::RootOpenFailed;
        }
        const packages::PackageSet* const packageSet = root->reader.GetPackageSet();
        if (packageSet == nullptr || !packageSet->IsValid())
        {
            root->reader.Close();
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::MissingPackageSet;
        }
        if (config.expectedGameId != 0 && packageSet->gameId != config.expectedGameId)
        {
            root->reader.Close();
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::GameMismatch;
        }
        if (config.expectedBuildId != 0 && packageSet->buildId != config.expectedBuildId)
        {
            root->reader.Close();
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::BuildMismatch;
        }
        if (config.expectedTargetPlatformId != 0 && packageSet->targetPlatformId != config.expectedTargetPlatformId)
        {
            root->reader.Close();
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::TargetMismatch;
        }

        impl->gameId = packageSet->gameId;
        impl->buildId = packageSet->buildId;
        impl->targetPlatformId = packageSet->targetPlatformId;
        impl->startupWorld = resources::ResourceReference(resources::ResourcePath::FromId(packageSet->startupWorld), packageSet->startupWorldType);
        impl->defaultInput = resources::ResourceReference(resources::ResourcePath::FromId(packageSet->defaultInput), packageSet->defaultInputType);
        root->info = {&root->reader, rootPath, 0, packages::PackageSetEntryFlags::Required, config.rootPriority};
        if (!AddOwnedPackage(*impl, root))
        {
            root->reader.Close();
            DeleteStreamingObject(root);
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::OutOfMemory;
        }

        for (const packages::PackageSetEntry& entry : packageSet->packages)
        {
            const bool optional = IsOptional(entry.flags);
            if (optional && !config.mountOptionalPackages)
            {
                continue;
            }
            filesystem::AbsolutePath path;
            if (!BuildDataPath(gameDirectory, entry.packageNumber, path))
            {
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::InvalidArgument;
            }
            if (!files.FileExist(path))
            {
                if (optional)
                {
                    continue;
                }
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::MissingRequiredPackage;
            }
            auto file = files.CreateFileReader(path, filesystem::FOF_Buffered);
            if (!file)
            {
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::IoFailure;
            }
            if (file->GetSize() != entry.fileSize)
            {
                file.Reset();
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::PackageMetadataMismatch;
            }
            OwnedSetPackage* const owned = AllocateStreamingObject<OwnedSetPackage>();
            if (owned == nullptr)
            {
                file.Reset();
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::OutOfMemory;
            }
            packages::Result opened = packages::OpenCatalogPackage(*file, entry, owned->reader, packages::CatalogVerification::IndexAndIdentity,
                                                                   config.packageLimits, config.hashBufferBytes);
            if (opened == packages::Result::Success && config.verification == packages::CatalogVerification::WholeFileDigest)
            {
                opened = packages::OpenCatalogPackage(*file, entry, owned->reader, packages::CatalogVerification::WholeFileDigest, config.packageLimits,
                                                      config.hashBufferBytes);
            }
            file.Reset();
            if (opened != packages::Result::Success)
            {
                owned->reader.Close();
                DeleteStreamingObject(owned);
                DeletePackageSetImpl(impl);
                return ConvertCatalogOpenFailure(opened, config.verification == packages::CatalogVerification::WholeFileDigest);
            }
            owned->info = {&owned->reader, path, entry.packageNumber, entry.flags, entry.mountPriority};
            if (!HasAuthorizedOverrides(*impl, *owned))
            {
                owned->reader.Close();
                DeleteStreamingObject(owned);
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::PackageMetadataMismatch;
            }
            if (!AddOwnedPackage(*impl, owned))
            {
                owned->reader.Close();
                DeleteStreamingObject(owned);
                DeletePackageSetImpl(impl);
                return PackageSetMountResult::OutOfMemory;
            }
        }

        containers::DynamicArray<PackageMountDescriptor> descriptors(memory::pools::Streaming::GetInstance());
        descriptors.Resize(impl->packages.Size());
        if (descriptors.Size() != impl->packages.Size())
        {
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::OutOfMemory;
        }
        for (u32 index = 0; index < impl->packages.Size(); ++index)
        {
            const MountedPackageInfo& info = impl->packages[index]->info;
            descriptors[index] = {info.reader, info.physicalPath, info.priority};
        }
        if (!streamer.MountPackages(descriptors))
        {
            DeletePackageSetImpl(impl);
            return PackageSetMountResult::MountFailed;
        }
        impl->mounted = true;
        m_impl = impl;
        return PackageSetMountResult::Success;
    }

    PackageSetMountResult PackageSetMount::Unmount() noexcept
    {
        if (m_impl == nullptr || !m_impl->mounted || m_impl->streamer == nullptr)
        {
            return PackageSetMountResult::InvalidArgument;
        }
        containers::DynamicArray<PackageMountDescriptor> descriptors(memory::pools::Streaming::GetInstance());
        descriptors.Resize(m_impl->packages.Size());
        if (descriptors.Size() != m_impl->packages.Size())
        {
            return PackageSetMountResult::OutOfMemory;
        }
        for (u32 index = 0; index < m_impl->packages.Size(); ++index)
        {
            const MountedPackageInfo& info = m_impl->packages[index]->info;
            descriptors[index] = {info.reader, info.physicalPath, info.priority};
        }
        if (!m_impl->streamer->UnmountPackages(descriptors))
        {
            return PackageSetMountResult::Busy;
        }
        Impl* const released = m_impl;
        m_impl = nullptr;
        released->mounted = false;
        DeletePackageSetImpl(released);
        return PackageSetMountResult::Success;
    }

    bool PackageSetMount::IsMounted() const noexcept
    {
        return m_impl != nullptr && m_impl->mounted;
    }

    u64 PackageSetMount::GetGameId() const noexcept
    {
        return m_impl != nullptr ? m_impl->gameId : 0;
    }

    u64 PackageSetMount::BuildId() const noexcept
    {
        return m_impl != nullptr ? m_impl->buildId : 0;
    }

    u32 PackageSetMount::GetTargetPlatformId() const noexcept
    {
        return m_impl != nullptr ? m_impl->targetPlatformId : 0;
    }

    resources::ResourceReference PackageSetMount::StartupWorld() const noexcept
    {
        return m_impl != nullptr ? m_impl->startupWorld : resources::ResourceReference{};
    }

    resources::ResourceReference PackageSetMount::GetDefaultInput() const noexcept
    {
        return m_impl != nullptr ? m_impl->defaultInput : resources::ResourceReference{};
    }

    u32 PackageSetMount::PackageCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->packages.Size() : 0;
    }

    const MountedPackageInfo* PackageSetMount::FindPackage(const u32 packageNumber) const noexcept
    {
        if (m_impl == nullptr)
        {
            return nullptr;
        }
        for (const OwnedSetPackage* const package : m_impl->packages)
        {
            if (package->info.packageNumber == packageNumber)
            {
                return &package->info;
            }
        }
        return nullptr;
    }
} // namespace vanguard::streaming
