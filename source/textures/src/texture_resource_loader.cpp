#include <vanguard/textures/texture_resource.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstring>
#include <limits>
#include <new>

namespace vanguard::textures
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
        [[nodiscard]] resources::Failure ConvertTextureFailure(const Result result) noexcept
        {
            switch (result)
            {
            case Result::Success:
                return resources::Failure::None;
            case Result::UnsupportedVersion:
                return resources::Failure::UnsupportedVersion;
            case Result::LimitExceeded:
            case Result::BufferTooSmall:
                return resources::Failure::OutOfMemory;
            case Result::Cancelled:
                return resources::Failure::Cancelled;
            case Result::IoFailure:
                return resources::Failure::IoFailure;
            case Result::InvalidMagic:
            case Result::InvalidLayout:
            case Result::IntegrityFailure:
            case Result::InvalidDimension:
            case Result::InvalidFormat:
            case Result::InvalidMipChain:
            case Result::InvalidSubresource:
            case Result::DuplicateSubresource:
            case Result::MissingSubresource:
                return resources::Failure::IntegrityFailure;
            default:
                return resources::Failure::DeserializationFailure;
            }
        }

        struct TextureMetadataLoad;
        void MetadataReadCompleted(streaming::ResourceSourceResult result, const streaming::ResourceReadStats&, void* userData);
    } // namespace

    struct TextureResourceLoader::Impl
    {
        Impl(streaming::ResourceStreamer& resourceStreamer, resources::ResourcePipeline& resourcePipeline, const ReadLimits& readLimits) noexcept
            : streamer(&resourceStreamer), pipeline(&resourcePipeline), limits(readLimits)
        {
        }
        streaming::ResourceStreamer* streamer = nullptr;
        resources::ResourcePipeline* pipeline = nullptr;
        ReadLimits limits;
        concurrency::Atomic<u32> activeStates{0};
        [[nodiscard]] resources::Failure Create(resources::ResourceReference reference, TextureMetadataLoad*& output) noexcept;
        [[nodiscard]] bool IssueRead(TextureMetadataLoad& load, u64 prefixBytes) noexcept;
        void ProcessRead(TextureMetadataLoad& load, streaming::ResourceSourceResult result) noexcept;
        static resources::Failure Discover(resources::ResourceReference, resources::DependencyBuilder&, void*) noexcept;
        static bool Begin(const resources::PreparationRequest&, void*) noexcept;
        static void Cancel(const resources::PreparationRequest&, void*) noexcept;
        static resources::ResourceObject* Construct(const resources::LoadContext&, resources::Failure&, void*) noexcept;
        static void Destroy(resources::ResourceObject*, void*) noexcept;
    };

    namespace
    {
        struct TextureMetadataLoad
        {
            TextureMetadataLoad() noexcept : dependencies(memory::pools::Streaming::GetInstance()), prefix(memory::pools::Streaming::GetInstance()) {}
            TextureResourceLoader::Impl* owner = nullptr;
            streaming::ResourceSource source;
            containers::DynamicArray<streaming::DependencyDescriptor> dependencies;
            streaming::StagingReservation staging;
            containers::DynamicArray<u8> prefix;
            TextureFile metadata;
            resources::PreparationRequest preparation;
            streaming::ResourceReadRequest read;
            concurrency::Mutex readLock;
            concurrency::Atomic<u32> references{1};
            concurrency::Atomic<bool> preparationCompleted{false};
        };
        void Retain(TextureMetadataLoad& load) noexcept
        {
            static_cast<void>(load.references.Increment());
        }
        void Release(TextureMetadataLoad* load) noexcept
        {
            if (load != nullptr && load->references.Decrement() == 0)
            {
                static_cast<void>(load->owner->activeStates.Decrement());
                DeleteLoaderObject(load, memory::PoolId::Streaming);
            }
        }
        void Complete(TextureMetadataLoad& load, const resources::Failure failure) noexcept
        {
            if (!load.preparationCompleted.CompareExchange(true, false))
            {
                TextureMetadataLoad* const failed = failure != resources::Failure::None ? static_cast<TextureMetadataLoad*>(load.preparation.TakeLoaderState()) : nullptr;
                static_cast<void>(load.preparation.Complete(failure));
                Release(failed);
            }
        }
        void MetadataReadCompleted(const streaming::ResourceSourceResult result, const streaming::ResourceReadStats&, void* const userData)
        {
            auto& load = *static_cast<TextureMetadataLoad*>(userData);
            load.readLock.Acquire();
            load.read.Reset();
            load.owner->ProcessRead(load, result);
            load.readLock.Release();
            Release(&load);
        }
    } // namespace

    resources::Failure TextureResourceLoader::Impl::Create(const resources::ResourceReference reference, TextureMetadataLoad*& output) noexcept
    {
        output = nullptr;
        TextureMetadataLoad* const load = AllocateLoaderObject<TextureMetadataLoad>(memory::PoolId::Streaming);
        if (load == nullptr)
            return resources::Failure::OutOfMemory;
        load->owner = this;
        const resources::Failure opened = streamer->OpenSource(reference, load->source, load->dependencies);
        if (opened != resources::Failure::None)
        {
            DeleteLoaderObject(load, memory::PoolId::Streaming);
            return opened;
        }
        static_cast<void>(activeStates.Increment());
        output = load;
        return resources::Failure::None;
    }
    bool TextureResourceLoader::Impl::IssueRead(TextureMetadataLoad& load, const u64 prefixBytes) noexcept
    {
        if (prefixBytes > load.source.GetLogicalSize() || prefixBytes > limits.maximumFileSize)
        {
            Complete(load, resources::Failure::IntegrityFailure);
            return false;
        }
        if (prefixBytes > std::numeric_limits<u32>::max() || !streamer->ReserveStaging(prefixBytes, load.staging))
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
        Retain(load);
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
    void TextureResourceLoader::Impl::ProcessRead(TextureMetadataLoad& load, const streaming::ResourceSourceResult sourceResult) noexcept
    {
        if (sourceResult != streaming::ResourceSourceResult::Success || load.preparation.IsCancellationRequested())
        {
            Complete(load, load.preparation.IsCancellationRequested() ? resources::Failure::Cancelled : streaming::ToFailure(sourceResult));
            return;
        }
        streaming::ResourcePrefixReader reader({load.prefix.TypedData(), load.prefix.Size()}, load.source.GetLogicalSize(), "VTEX metadata prefix");
        serialization::BinaryReader binary(reader);
        serialization::DocumentHeader header;
        serialization::ReadLimits documentLimits;
        documentLimits.maximumFileSize = limits.maximumFileSize;
        documentLimits.maximumSections = 2;
        const serialization::Result headerResult = serialization::ReadDocumentHeader(binary, TextureMagic, {1, 0, 0}, documentLimits, header);
        if (headerResult != serialization::Result::Success)
        {
            Complete(load, headerResult == serialization::Result::UnsupportedVersion ? resources::Failure::UnsupportedVersion : resources::Failure::IntegrityFailure);
            return;
        }
        const u64 tableBytes = static_cast<u64>(header.sectionCount) * serialization::SectionDescriptor::WireSize;
        if (header.sectionTableOffset > std::numeric_limits<u64>::max() - tableBytes)
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
        if (metadata == nullptr || metadata->storedSize > limits.maximumMetadataBytes || metadata->offset > std::numeric_limits<u64>::max() - metadata->storedSize)
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
        streaming::ResourcePrefixReader textureReader({load.prefix.TypedData(), load.prefix.Size()}, load.source.GetLogicalSize(), "VTEX metadata prefix");
        Complete(load, ConvertTextureFailure(load.metadata.Open(textureReader, limits)));
    }
    resources::Failure TextureResourceLoader::Impl::Discover(const resources::ResourceReference reference, resources::DependencyBuilder& dependencies, void* const userData) noexcept
    {
        auto& self = *static_cast<Impl*>(userData);
        TextureMetadataLoad* load = nullptr;
        const resources::Failure result = self.Create(reference, load);
        if (result != resources::Failure::None)
            return result;
        if (load->dependencies.Size() != 0 || !dependencies.SetLoaderState(load))
        {
            const bool unexpectedDependencies = load->dependencies.Size() != 0;
            Release(load);
            return unexpectedDependencies ? resources::Failure::IntegrityFailure : resources::Failure::InternalError;
        }
        return resources::Failure::None;
    }
    bool TextureResourceLoader::Impl::Begin(const resources::PreparationRequest& request, void* const userData) noexcept
    {
        auto& self = *static_cast<Impl*>(userData);
        auto* const load = static_cast<TextureMetadataLoad*>(request.GetLoaderState());
        if (load == nullptr)
            return false;
        Retain(*load);
        load->preparation = request;
        u64 initialBytes = serialization::DocumentHeader::WireSize;
        streaming::ResourceReadPlan plan;
        if (load->source.PlanRead(0, initialBytes, plan) == streaming::ResourceSourceResult::Success && load->source.GetKind() == streaming::ResourceSourceKind::Package &&
            plan.decodedRangeOffset == 0 && plan.decodedRangeBytes > initialBytes)
            initialBytes = plan.decodedRangeBytes;
        load->readLock.Acquire();
        const bool started = self.IssueRead(*load, initialBytes);
        load->readLock.Release();
        Release(load);
        return started;
    }
    void TextureResourceLoader::Impl::Cancel(const resources::PreparationRequest& request, void*) noexcept
    {
        auto* const load = static_cast<TextureMetadataLoad*>(request.TakeLoaderState());
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
    resources::ResourceObject* TextureResourceLoader::Impl::Construct(const resources::LoadContext& context, resources::Failure& failure, void*) noexcept
    {
        auto* const load = static_cast<TextureMetadataLoad*>(context.TakeLoaderState());
        if (load == nullptr || !load->metadata.IsOpen())
        {
            failure = resources::Failure::InternalError;
            Release(load);
            return nullptr;
        }
        TextureSubresourceSource source;
        Result result = source.Open(static_cast<streaming::ResourceSource&&>(load->source));
        TextureResourceObject* resource = nullptr;
        if (result == Result::Success)
        {
            resource = AllocateLoaderObject<TextureResourceObject>(memory::PoolId::Resources);
            if (resource == nullptr)
                result = Result::LimitExceeded;
        }
        if (result == Result::Success)
            result = resource->OpenPrepared(static_cast<TextureSubresourceSource&&>(source), static_cast<TextureFile&&>(load->metadata));
        if (result != Result::Success)
        {
            DeleteLoaderObject(resource, memory::PoolId::Resources);
            resource = nullptr;
            failure = ConvertTextureFailure(result);
        }
        Release(load);
        return resource;
    }
    void TextureResourceLoader::Impl::Destroy(resources::ResourceObject* resource, void*) noexcept
    {
        DeleteLoaderObject(static_cast<TextureResourceObject*>(resource), memory::PoolId::Resources);
    }
    TextureResourceLoader::~TextureResourceLoader()
    {
        static_cast<void>(Shutdown());
    }
    bool TextureResourceLoader::Initialize(streaming::ResourceStreamer& streamer, resources::ResourcePipeline& pipeline, const ReadLimits& limits) noexcept
    {
        if (m_impl != nullptr || !streamer.IsInitialized() || !pipeline.IsInitialized())
            return false;
        Impl* const impl = AllocateLoaderObject<Impl>(memory::PoolId::Streaming, streamer, pipeline, limits);
        if (impl == nullptr)
            return false;
        const resources::AsyncLoaderDescriptor descriptor{TextureResourceType, "Vanguard texture metadata", &Impl::Discover, &Impl::Construct, &Impl::Destroy, impl, &Impl::Begin,
                                                          &Impl::Cancel};
        if (!pipeline.RegisterLoader(descriptor))
        {
            DeleteLoaderObject(impl, memory::PoolId::Streaming);
            return false;
        }
        m_impl = impl;
        return true;
    }
    bool TextureResourceLoader::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (m_impl->activeStates.GetValue() != 0 || !m_impl->pipeline->UnregisterLoader(TextureResourceType))
            return false;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteLoaderObject(impl, memory::PoolId::Streaming);
        return true;
    }
    bool TextureResourceLoader::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }
} // namespace vanguard::textures
