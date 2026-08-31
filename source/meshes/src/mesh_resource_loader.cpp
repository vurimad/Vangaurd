#include <vanguard/meshes/mesh_resource.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstring>
#include <limits>
#include <new>

namespace vanguard::meshes
{
    namespace
    {
        constexpr u32 MetadataSection = serialization::MakeFourCC('M', 'E', 'T', 'A');

        template <typename T, typename... Args> [[nodiscard]] T* AllocateLoaderObject(memory::PoolId pool, Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(pool, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteLoaderObject(T* object, memory::PoolId pool) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), pool};
            memory::Free(block);
        }

        [[nodiscard]] resources::DependencyRequirement ToRequirement(resources::DependencyKind kind) noexcept
        {
            return kind == resources::DependencyKind::Optional ? resources::DependencyRequirement::Optional : resources::DependencyRequirement::Required;
        }

        [[nodiscard]] resources::Failure ConvertMeshFailure(Result result) noexcept
        {
            switch (result)
            {
            case Result::Success:
                return resources::Failure::None;
            case Result::IntegrityFailure:
            case Result::InvalidMagic:
            case Result::InvalidLayout:
            case Result::InvalidBuffer:
            case Result::InvalidPage:
            case Result::InvalidLod:
            case Result::InvalidSubmesh:
            case Result::InvalidMaterial:
            case Result::DuplicateIdentifier:
            case Result::DuplicateStream:
            case Result::MissingPositionStream:
            case Result::DependencyMismatch:
                return resources::Failure::IntegrityFailure;
            case Result::UnsupportedVersion:
                return resources::Failure::UnsupportedVersion;
            case Result::LimitExceeded:
            case Result::BufferTooSmall:
                return resources::Failure::OutOfMemory;
            case Result::Cancelled:
                return resources::Failure::Cancelled;
            case Result::IoFailure:
                return resources::Failure::IoFailure;
            default:
                return resources::Failure::DeserializationFailure;
            }
        }

        struct MetadataLoad;
        void MetadataReadCompleted(streaming::ResourceSourceResult result, const streaming::ResourceReadStats&, void* userData);
    } // namespace

    struct MeshResourceLoader::Impl
    {
        Impl(streaming::ResourceStreamer& resourceStreamer, resources::ResourcePipeline& resourcePipeline, const ReadLimits& readLimits) noexcept
            : streamer(&resourceStreamer), pipeline(&resourcePipeline), limits(readLimits)
        {
        }

        streaming::ResourceStreamer* streamer = nullptr;
        resources::ResourcePipeline* pipeline = nullptr;
        ReadLimits limits;
        concurrency::Atomic<u32> activeStates{0};

        [[nodiscard]] resources::Failure Create(resources::ResourceReference reference, MetadataLoad*& output) noexcept;
        [[nodiscard]] bool IssueRead(MetadataLoad& load, u64 prefixBytes) noexcept;
        void ProcessRead(MetadataLoad& load, streaming::ResourceSourceResult result) noexcept;

        static resources::Failure Discover(resources::ResourceReference reference, resources::DependencyBuilder& dependencies, void* userData) noexcept;
        static bool Begin(const resources::PreparationRequest& request, void* userData) noexcept;
        static void Cancel(const resources::PreparationRequest& request, void* userData) noexcept;
        static resources::ResourceObject* Construct(const resources::LoadContext& context, resources::Failure& failure, void* userData) noexcept;
        static void Destroy(resources::ResourceObject* resource, void*) noexcept;
    };

    namespace
    {
        struct MetadataLoad
        {
            MetadataLoad() noexcept : dependencies(memory::pools::Streaming::GetInstance()), prefix(memory::pools::Streaming::GetInstance()) {}

            MeshResourceLoader::Impl* owner = nullptr;
            resources::ResourceReference reference;
            streaming::ResourceSource source;
            containers::DynamicArray<streaming::DependencyDescriptor> dependencies;
            streaming::StagingReservation staging;
            containers::DynamicArray<u8> prefix;
            MeshFile metadata;
            resources::PreparationRequest preparation;
            streaming::ResourceReadRequest read;
            concurrency::Mutex readLock;
            concurrency::Atomic<u32> references{1}; // pipeline operation state
            concurrency::Atomic<bool> preparationCompleted{false};
        };

        void Retain(MetadataLoad& load) noexcept
        {
            static_cast<void>(load.references.Increment());
        }

        void Release(MetadataLoad* load) noexcept
        {
            if (load != nullptr && load->references.Decrement() == 0)
            {
                static_cast<void>(load->owner->activeStates.Decrement());
                DeleteLoaderObject(load, memory::PoolId::Streaming);
            }
        }

        void Complete(MetadataLoad& load, const resources::Failure failure) noexcept
        {
            if (!load.preparationCompleted.CompareExchange(true, false))
            {
                MetadataLoad* const failedState = failure != resources::Failure::None ? static_cast<MetadataLoad*>(load.preparation.TakeLoaderState()) : nullptr;
                static_cast<void>(load.preparation.Complete(failure));
                Release(failedState);
            }
        }

        void MetadataReadCompleted(const streaming::ResourceSourceResult result, const streaming::ResourceReadStats&, void* const userData)
        {
            auto& load = *static_cast<MetadataLoad*>(userData);
            load.readLock.Acquire();
            load.read.Reset();
            load.owner->ProcessRead(load, result);
            load.readLock.Release();
            Release(&load);
        }
    } // namespace

    resources::Failure MeshResourceLoader::Impl::Create(const resources::ResourceReference reference, MetadataLoad*& output) noexcept
    {
        output = nullptr;
        MetadataLoad* const load = AllocateLoaderObject<MetadataLoad>(memory::PoolId::Streaming);
        if (load == nullptr)
            return resources::Failure::OutOfMemory;
        load->owner = this;
        load->reference = reference;
        const resources::Failure opened = streamer->OpenSource(reference, load->source, load->dependencies);
        if (opened != resources::Failure::None)
        {
            Release(load);
            return opened;
        }
        static_cast<void>(activeStates.Increment());
        output = load;
        return resources::Failure::None;
    }

    bool MeshResourceLoader::Impl::IssueRead(MetadataLoad& load, const u64 prefixBytes) noexcept
    {
        if (prefixBytes > load.source.GetLogicalSize() || prefixBytes > limits.maximumFileSize)
        {
            Complete(load, resources::Failure::IntegrityFailure);
            return false;
        }
        if (prefixBytes > std::numeric_limits<u32>::max())
        {
            Complete(load, resources::Failure::OutOfMemory);
            return false;
        }
        if (!streamer->ReserveStaging(prefixBytes, load.staging))
        {
            Complete(load, resources::Failure::OutOfMemory);
            return false;
        }
        load.prefix.Resize(static_cast<u32>(prefixBytes));
        if (load.prefix.Size() != prefixBytes)
        {
            Complete(load, resources::Failure::OutOfMemory);
            return false;
        }
        Retain(load); // asynchronous callback
        const streaming::ResourceRangeRead range{0, prefixBytes, load.prefix.TypedData(), load.prefix.Size(), streaming::ToIoPriority(load.preparation.Priority())};
        const streaming::ResourceSourceResult result = load.source.ReadAsync(range, load.read, MetadataReadCompleted, &load);
        if (result != streaming::ResourceSourceResult::Success)
        {
            Release(&load);
            Complete(load, streaming::ToFailure(result));
            return false;
        }
        return true;
    }

    void MeshResourceLoader::Impl::ProcessRead(MetadataLoad& load, const streaming::ResourceSourceResult result) noexcept
    {
        if (result != streaming::ResourceSourceResult::Success || load.preparation.IsCancellationRequested())
        {
            Complete(load, load.preparation.IsCancellationRequested() ? resources::Failure::Cancelled : streaming::ToFailure(result));
            return;
        }

        streaming::ResourcePrefixReader reader({load.prefix.TypedData(), load.prefix.Size()}, load.source.GetLogicalSize(), "VMSH metadata prefix");
        serialization::BinaryReader binary(reader);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 2;
        const serialization::Result headerResult = serialization::ReadDocumentHeader(binary, MeshMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            Complete(load, headerResult == serialization::Result::UnsupportedVersion ? resources::Failure::UnsupportedVersion : resources::Failure::IntegrityFailure);
            return;
        }

        const u64 tableBytes = static_cast<u64>(header.sectionCount) * serialization::SectionDescriptor::WireSize;
        if (header.sectionTableOffset > ~u64{0} - tableBytes)
        {
            Complete(load, resources::Failure::IntegrityFailure);
            return;
        }
        const u64 tableEnd = header.sectionTableOffset + tableBytes;
        if (load.prefix.Size() < tableEnd)
        {
            static_cast<void>(IssueRead(load, tableEnd));
            return;
        }

        containers::DynamicArray<serialization::SectionDescriptor> sections{memory::pools::Serialization::GetInstance()};
        if (serialization::ReadSectionTable(binary, header, documentLimits, sections) != serialization::Result::Success)
        {
            Complete(load, resources::Failure::IntegrityFailure);
            return;
        }
        const serialization::SectionDescriptor* metadata = nullptr;
        for (const serialization::SectionDescriptor& section : sections)
        {
            if (section.id == MetadataSection)
                metadata = &section;
        }
        if (metadata == nullptr || metadata->storedSize > limits.maximumMetadataBytes || metadata->offset > ~u64{0} - metadata->storedSize)
        {
            Complete(load, resources::Failure::IntegrityFailure);
            return;
        }
        const u64 metadataEnd = metadata->offset + metadata->storedSize;
        if (load.prefix.Size() < metadataEnd)
        {
            static_cast<void>(IssueRead(load, metadataEnd));
            return;
        }

        streaming::ResourcePrefixReader meshReader({load.prefix.TypedData(), load.prefix.Size()}, load.source.GetLogicalSize(), "VMSH metadata prefix");
        Complete(load, ConvertMeshFailure(load.metadata.Open(meshReader, limits)));
    }

    resources::Failure MeshResourceLoader::Impl::Discover(const resources::ResourceReference reference, resources::DependencyBuilder& dependencies, void* const userData) noexcept
    {
        auto& self = *static_cast<Impl*>(userData);
        MetadataLoad* load = nullptr;
        const resources::Failure created = self.Create(reference, load);
        if (created != resources::Failure::None)
            return created;
        if (!dependencies.SetLoaderState(load))
        {
            Release(load);
            return resources::Failure::InternalError;
        }
        for (const streaming::DependencyDescriptor& dependency : load->dependencies)
        {
            if (!dependencies.Add(dependency.reference, ToRequirement(dependency.kind)))
            {
                Release(static_cast<MetadataLoad*>(dependencies.TakeLoaderState()));
                return resources::Failure::DependencyLimit;
            }
        }
        return resources::Failure::None;
    }

    bool MeshResourceLoader::Impl::Begin(const resources::PreparationRequest& request, void* const userData) noexcept
    {
        auto& self = *static_cast<Impl*>(userData);
        auto* const load = static_cast<MetadataLoad*>(request.GetLoaderState());
        if (load == nullptr)
            return false;
        Retain(*load);
        load->preparation = request;

        u64 initialBytes = serialization::DocumentHeader::WireSize;
        streaming::ResourceReadPlan plan;
        if (load->source.PlanRead(0, initialBytes, plan) == streaming::ResourceSourceResult::Success && load->source.GetKind() == streaming::ResourceSourceKind::Package &&
            plan.decodedRangeOffset == 0 && plan.decodedRangeBytes > initialBytes)
        {
            initialBytes = plan.decodedRangeBytes;
        }
        load->readLock.Acquire();
        const bool started = self.IssueRead(*load, initialBytes);
        load->readLock.Release();
        Release(load);
        return started;
    }

    void MeshResourceLoader::Impl::Cancel(const resources::PreparationRequest& request, void* const userData) noexcept
    {
        static_cast<void>(userData);
        auto* const load = static_cast<MetadataLoad*>(request.TakeLoaderState());
        if (load == nullptr)
            return;
        load->readLock.Acquire();
        if (load->read.IsValid() && !load->read.HasFinished())
            static_cast<void>(load->read.Cancel());
        else
            Complete(*load, resources::Failure::Cancelled);
        load->readLock.Release();
        Release(load);
    }

    resources::ResourceObject* MeshResourceLoader::Impl::Construct(const resources::LoadContext& context, resources::Failure& failure, void* const userData) noexcept
    {
        static_cast<void>(userData);
        auto* const load = static_cast<MetadataLoad*>(context.TakeLoaderState());
        if (load == nullptr || !load->metadata.IsOpen())
        {
            failure = resources::Failure::InternalError;
            Release(load);
            return nullptr;
        }

        containers::DynamicArray<resources::ResourceHandle> dependencies{memory::pools::Resources::GetInstance()};
        dependencies.Reserve(context.GetDependencyCount());
        for (u32 index = 0; index < context.GetDependencyCount(); ++index)
            dependencies.PushBack(context.GetDependency(index));

        MeshPageSource source;
        Result opened = source.Open(static_cast<streaming::ResourceSource&&>(load->source));
        MeshResourceObject* resource = nullptr;
        if (opened == Result::Success)
        {
            resource = AllocateLoaderObject<MeshResourceObject>(memory::PoolId::Resources);
            if (resource == nullptr)
                opened = Result::LimitExceeded;
        }
        if (opened == Result::Success)
            opened = resource->OpenPrepared(static_cast<MeshPageSource&&>(source), static_cast<MeshFile&&>(load->metadata), {dependencies.TypedData(), dependencies.Size()});
        if (opened != Result::Success)
        {
            DeleteLoaderObject(resource, memory::PoolId::Resources);
            resource = nullptr;
            failure = ConvertMeshFailure(opened);
        }
        Release(load);
        return resource;
    }

    void MeshResourceLoader::Impl::Destroy(resources::ResourceObject* const resource, void*) noexcept
    {
        DeleteLoaderObject(static_cast<MeshResourceObject*>(resource), memory::PoolId::Resources);
    }

    MeshResourceLoader::~MeshResourceLoader()
    {
        static_cast<void>(Shutdown());
    }

    bool MeshResourceLoader::Initialize(streaming::ResourceStreamer& streamer, resources::ResourcePipeline& pipeline, const ReadLimits& limits) noexcept
    {
        if (m_impl != nullptr || !streamer.IsInitialized() || !pipeline.IsInitialized())
            return false;
        Impl* const impl = AllocateLoaderObject<Impl>(memory::PoolId::Streaming, streamer, pipeline, limits);
        if (impl == nullptr)
            return false;
        const resources::AsyncLoaderDescriptor descriptor{MeshResourceType, "Vanguard mesh metadata", &Impl::Discover, &Impl::Construct, &Impl::Destroy, impl, &Impl::Begin, &Impl::Cancel};
        if (!pipeline.RegisterLoader(descriptor))
        {
            DeleteLoaderObject(impl, memory::PoolId::Streaming);
            return false;
        }
        m_impl = impl;
        return true;
    }

    bool MeshResourceLoader::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        const bool busy = m_impl->activeStates.GetValue() != 0;
        if (busy || !m_impl->pipeline->UnregisterLoader(MeshResourceType))
            return false;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteLoaderObject(impl, memory::PoolId::Streaming);
        return true;
    }

    bool MeshResourceLoader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }
} // namespace vanguard::meshes
