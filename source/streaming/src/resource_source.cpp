#include <vanguard/streaming/resource_source.hpp>

#include <vanguard/streaming/resource_source_internal.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/io/io.hpp>
#include <vanguard/memory/memory.hpp>

#include <cstring>
#include <limits>
#include <new>

namespace vanguard::streaming
{
    namespace detail
    {
        struct ResourceSourceAccounting
        {
            explicit ResourceSourceAccounting(const u64 budget) noexcept : stagingBudgetBytes(budget) {}

            concurrency::Atomic<u32> references{1};
            concurrency::RWLock lock;
            u32 activeReads = 0;
            u64 stagingBudgetBytes = 0;
            u64 stagingBytesInUse = 0;
            u64 peakStagingBytes = 0;
            u64 bytesRead = 0;
            u64 budgetRejections = 0;
        };

        struct ResourceSourcePackageGeneration
        {
            concurrency::Atomic<u32> references{1};
            filesystem::AbsolutePath physicalPath;
            packages::PackageReader package;
            io::FileHandle pinnedFile = io::InvalidFileHandle;
        };
    } // namespace detail

    namespace
    {
        template <typename T, typename... Args> [[nodiscard]] T* AllocateSourceObject(Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(memory::PoolId::Streaming, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteSourceObject(T* object) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), memory::PoolId::Streaming};
            memory::Free(block);
        }

        [[nodiscard]] ResourceSourceResult ConvertPackageResult(const packages::Result result) noexcept
        {
            switch (result)
            {
            case packages::Result::Success:
                return ResourceSourceResult::Success;
            case packages::Result::ResourceNotFound:
                return ResourceSourceResult::NotFound;
            case packages::Result::IntegrityFailure:
                return ResourceSourceResult::IntegrityFailure;
            case packages::Result::UnsupportedVersion:
            case packages::Result::UnsupportedCodec:
                return ResourceSourceResult::UnsupportedVersion;
            case packages::Result::LimitExceeded:
                return ResourceSourceResult::LimitExceeded;
            case packages::Result::BufferTooSmall:
                return ResourceSourceResult::BufferTooSmall;
            case packages::Result::IoFailure:
                return ResourceSourceResult::IoFailure;
            default:
                return ResourceSourceResult::InvalidArgument;
            }
        }

        struct ReadCompletion
        {
            concurrency::ManualResetEvent completed;
            io::AsyncResult result = io::eAsyncResult_Unknown;
            u32 bytesTransferred = 0;
        };

        void CompleteRead(const io::AsyncReadToken& token, const io::AsyncResult result, const u32 bytesTransferred, io::ShareableIOMemory, const u32, io::UniqueBuffer)
        {
            auto& completion = *static_cast<ReadCompletion*>(token.m_userData);
            completion.result = result;
            completion.bytesTransferred = bytesTransferred;
            completion.completed.Signal();
        }

        class PinnedPhysicalReader final : public filesystem::IFile
        {
        public:
            PinnedPhysicalReader(const io::FileHandle file, const char* const name) noexcept
                : filesystem::IFile(filesystem::FF_Reader | filesystem::FF_FileBased), m_file(file), m_name(name), m_size(io::GetSystem().GetFileSize(file))
            {
            }

            void Serialize(void* buffer, size_t size) override
            {
                auto* destination = static_cast<u8*>(buffer);
                while (!m_failed && size != 0)
                {
                    const u32 chunk = size > std::numeric_limits<u32>::max() ? std::numeric_limits<u32>::max() : static_cast<u32>(size);
                    if (m_offset > static_cast<u64>(std::numeric_limits<i64>::max()))
                    {
                        Fail();
                        return;
                    }
                    ReadCompletion completion;
                    io::AsyncReadToken token;
                    token.m_callback = CompleteRead;
                    token.m_userData = &completion;
                    token.m_buffer = destination;
                    token.m_debugLogicalFileName = m_name;
                    token.m_offset = static_cast<i64>(m_offset);
                    token.m_numberOfBytesToRead = chunk;
                    token.m_requestSource = io::RequestSource::ResourceSystem;
                    io::GetSystem().BeginRead(m_file, token, io::eAsyncPriority_Streaming);
                    completion.completed.Wait();
                    m_offset += completion.bytesTransferred;
                    m_bytesRead += completion.bytesTransferred;
                    if (completion.result != io::eAsyncResult_Success || completion.bytesTransferred != chunk)
                    {
                        Fail();
                        return;
                    }
                    destination += chunk;
                    size -= chunk;
                }
            }

            [[nodiscard]] Uint64 GetOffset() const override
            {
                return m_offset;
            }

            [[nodiscard]] Uint64 GetSize() const override
            {
                return m_size;
            }

            void Seek(const Int64 offset) override
            {
                if (offset < 0 || static_cast<u64>(offset) > m_size)
                {
                    Fail();
                    return;
                }
                m_offset = static_cast<u64>(offset);
            }

            void Flush() override {}

            [[nodiscard]] const char* GetFileNameForDebug() const override
            {
                return m_name;
            }

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return !m_failed;
            }

            [[nodiscard]] u64 GetBytesRead() const noexcept
            {
                return m_bytesRead;
            }

        private:
            void Fail() noexcept
            {
                m_failed = true;
                m_flags |= filesystem::FF_ErrorOccured;
            }

            io::FileHandle m_file = io::InvalidFileHandle;
            const char* m_name = nullptr;
            u64 m_offset = 0;
            u64 m_size = 0;
            u64 m_bytesRead = 0;
            bool m_failed = false;
        };
    } // namespace

    detail::ResourceSourceAccounting* detail::CreateResourceSourceAccounting(const u64 stagingBudgetBytes) noexcept
    {
        return stagingBudgetBytes != 0 ? AllocateSourceObject<ResourceSourceAccounting>(stagingBudgetBytes) : nullptr;
    }

    void detail::RetainResourceSourceAccounting(ResourceSourceAccounting* const accounting) noexcept
    {
        if (accounting != nullptr)
            static_cast<void>(accounting->references.Increment());
    }

    void detail::ReleaseResourceSourceAccounting(ResourceSourceAccounting* const accounting) noexcept
    {
        if (accounting != nullptr && accounting->references.Decrement() == 0)
            DeleteSourceObject(accounting);
    }

    bool detail::ReserveResourceSourceStaging(ResourceSourceAccounting* const accounting, const u64 bytes) noexcept
    {
        if (accounting == nullptr || bytes == 0)
            return true;
        accounting->lock.Acquire();
        if (bytes > accounting->stagingBudgetBytes || accounting->stagingBytesInUse > accounting->stagingBudgetBytes - bytes)
        {
            ++accounting->budgetRejections;
            accounting->lock.Release();
            return false;
        }
        accounting->stagingBytesInUse += bytes;
        if (accounting->stagingBytesInUse > accounting->peakStagingBytes)
            accounting->peakStagingBytes = accounting->stagingBytesInUse;
        accounting->lock.Release();
        return true;
    }

    void detail::ReleaseResourceSourceStaging(ResourceSourceAccounting* const accounting, const u64 bytes) noexcept
    {
        if (accounting == nullptr || bytes == 0)
            return;
        accounting->lock.Acquire();
        accounting->stagingBytesInUse -= bytes;
        accounting->lock.Release();
    }

    detail::ResourceSourceAccountingStats detail::GetResourceSourceAccountingStats(const ResourceSourceAccounting* const accounting) noexcept
    {
        ResourceSourceAccountingStats stats;
        if (accounting == nullptr)
            return stats;
        auto& mutableAccounting = *const_cast<ResourceSourceAccounting*>(accounting);
        VG_SCOPE_SHARED_LOCK(mutableAccounting.lock);
        stats.activeReads = accounting->activeReads;
        stats.stagingBudgetBytes = accounting->stagingBudgetBytes;
        stats.stagingBytesInUse = accounting->stagingBytesInUse;
        stats.peakStagingBytes = accounting->peakStagingBytes;
        stats.bytesRead = accounting->bytesRead;
        stats.budgetRejections = accounting->budgetRejections;
        return stats;
    }

    detail::ResourceSourcePackageGeneration* detail::CreateResourceSourcePackageGeneration(const filesystem::AbsolutePath& physicalPath) noexcept
    {
        if (!physicalPath.IsFilePath() || !io::IsInitialized())
            return nullptr;
        const io::FileHandle file = io::GetSystem().OpenFile(physicalPath.AsChar());
        if (file == io::InvalidFileHandle)
            return nullptr;
        auto* const generation = AllocateSourceObject<ResourceSourcePackageGeneration>();
        if (generation == nullptr)
        {
            io::GetSystem().ReleaseFile(file);
            return nullptr;
        }
        generation->physicalPath = physicalPath;
        generation->pinnedFile = file;
        PinnedPhysicalReader physical(file, physicalPath.AsChar());
        if (generation->package.Open(physical) != packages::Result::Success || !physical.Succeeded())
        {
            generation->package.Close();
            io::GetSystem().ReleaseFile(file);
            DeleteSourceObject(generation);
            return nullptr;
        }
        return generation;
    }

    void detail::RetainResourceSourcePackageGeneration(ResourceSourcePackageGeneration* const generation) noexcept
    {
        if (generation != nullptr)
            static_cast<void>(generation->references.Increment());
    }

    void detail::ReleaseResourceSourcePackageGeneration(ResourceSourcePackageGeneration* const generation) noexcept
    {
        if (generation == nullptr || generation->references.Decrement() != 0)
            return;
        generation->package.Close();
        if (generation->pinnedFile != io::InvalidFileHandle && io::IsInitialized())
            io::GetSystem().ReleaseFile(generation->pinnedFile);
        DeleteSourceObject(generation);
    }

    const packages::PackageReader* detail::GetResourceSourcePackageReader(const ResourceSourcePackageGeneration* const generation) noexcept
    {
        return generation != nullptr ? &generation->package : nullptr;
    }

    struct ResourceSource::Impl
    {
        concurrency::Atomic<u32> references{1};
        filesystem::AbsolutePath physicalPath;
        packages::PackageReader package;
        detail::ResourceSourceAccounting* accounting = nullptr;
        detail::ResourceSourcePackageGeneration* packageGeneration = nullptr;
        const packages::Resource* packagedResource = nullptr;
        io::FileHandle pinnedFile = io::InvalidFileHandle;
        resources::ResourceId resource = resources::InvalidResourceId;
        resources::ResourceTypeId type = resources::InvalidResourceTypeId;
        u64 logicalSize = 0;
        ResourceSourceKind kind = ResourceSourceKind::LooseFile;
    };

    namespace
    {
        [[nodiscard]] const packages::PackageReader& PackageOf(const ResourceSource::Impl& source) noexcept
        {
            return source.packageGeneration != nullptr ? source.packageGeneration->package : source.package;
        }

        [[nodiscard]] io::FileHandle PinnedFileOf(const ResourceSource::Impl& source) noexcept
        {
            return source.packageGeneration != nullptr ? source.packageGeneration->pinnedFile : source.pinnedFile;
        }

        void RetainSource(ResourceSource::Impl& source) noexcept
        {
            static_cast<void>(source.references.Increment());
        }

        void ReleaseSource(ResourceSource::Impl* source) noexcept
        {
            if (source == nullptr || source->references.Decrement() != 0)
                return;
            source->package.Close();
            detail::ReleaseResourceSourcePackageGeneration(source->packageGeneration);
            detail::ReleaseResourceSourceAccounting(source->accounting);
            if (source->pinnedFile != io::InvalidFileHandle && io::IsInitialized())
                io::GetSystem().ReleaseFile(source->pinnedFile);
            DeleteSourceObject(source);
        }
    } // namespace

    struct ResourceSourceReader::Impl
    {
        explicit Impl(const ResourceSource::Impl& source) noexcept : owner(&source), physical(PinnedFileOf(source), source.physicalPath.AsChar()), active(&physical)
        {
            RetainSource(*const_cast<ResourceSource::Impl*>(&source));
        }

        ~Impl()
        {
            ReleaseSource(const_cast<ResourceSource::Impl*>(owner));
        }

        const ResourceSource::Impl* owner = nullptr;
        PinnedPhysicalReader physical;
        packages::ResourceFileReader logical;
        filesystem::IFile* active = nullptr;
        ResourceSourceResult result = ResourceSourceResult::Success;
    };

    struct ResourceReadRequest::Impl;

    namespace
    {
        struct AsyncReadPiece
        {
            ResourceReadRequest::Impl* operation = nullptr;
            io::AsyncReadToken token;
            packages::Segment segment;
            u64 segmentLogicalOffset = 0;
            u64 storedBufferOffset = 0;
            u64 decodedBufferOffset = 0;
            bool packaged = false;
        };
    } // namespace

    struct ResourceReadRequest::Impl
    {
        Impl() noexcept : pieces(memory::pools::Streaming::GetInstance()) {}

        concurrency::Atomic<u32> references{2}; // public handle + in-flight operation
        concurrency::Atomic<u32> pendingReads{0};
        concurrency::Atomic<u32> result{static_cast<u32>(ResourceSourceResult::Success)};
        concurrency::Atomic<u64> storedBytesRead{0};
        concurrency::Atomic<bool> finished{false};
        concurrency::Atomic<bool> cancellationRequested{false};
        concurrency::ManualResetEvent completion;
        ResourceSource::Impl* source = nullptr;
        containers::DynamicArray<AsyncReadPiece*> pieces;
        memory::MemoryBlock storedData;
        memory::MemoryBlock decodedData;
        ResourceRangeRead read;
        ResourceReadPlan plan;
        ResourceReadCallback callback = nullptr;
        void* userData = nullptr;
        red::SharedPtr<io::IOContext> ioContext;
        u32 decodedSegments = 0;
        u64 reservedStagingBytes = 0;
        bool accountedActiveRead = false;
    };

    namespace
    {
        void ReleaseReadOperation(ResourceReadRequest::Impl* operation) noexcept
        {
            if (operation == nullptr || operation->references.Decrement() != 0)
                return;
            for (AsyncReadPiece* const piece : operation->pieces)
                DeleteSourceObject(piece);
            operation->pieces.Clear();
            memory::Free(operation->storedData);
            memory::Free(operation->decodedData);
            detail::ReleaseResourceSourceStaging(operation->source->accounting, operation->reservedStagingBytes);
            ReleaseSource(operation->source);
            DeleteSourceObject(operation);
        }

        void SetReadFailure(ResourceReadRequest::Impl& operation, const ResourceSourceResult result) noexcept
        {
            static_cast<void>(operation.result.CompareExchange(static_cast<u32>(result), static_cast<u32>(ResourceSourceResult::Success)));
        }

        [[nodiscard]] ResourceSourceResult BuildReadPlan(const ResourceSource::Impl& source, const u64 offset, const u64 size, ResourceReadPlan& plan) noexcept
        {
            plan = {};
            if (offset > source.logicalSize || size > source.logicalSize - offset)
                return ResourceSourceResult::InvalidArgument;
            plan.logicalBytes = size;
            plan.decodedRangeOffset = offset;
            plan.decodedRangeBytes = size;
            if (size == 0)
                return ResourceSourceResult::Success;
            if (source.kind == ResourceSourceKind::LooseFile)
            {
                plan.storedBytes = size;
                plan.decodedBytes = size;
                plan.touchedSegments = static_cast<u32>(((size - 1u) / std::numeric_limits<u32>::max()) + 1u);
                return ResourceSourceResult::Success;
            }

            const u64 end = offset + size;
            u64 logicalOffset = 0;
            bool foundFirstSegment = false;
            plan.decodedRangeBytes = 0;
            for (const packages::Segment& segment : PackageOf(source).GetSegments(*source.packagedResource))
            {
                const u64 segmentEnd = logicalOffset + segment.logicalSize;
                if (offset < segmentEnd && end > logicalOffset)
                {
                    if (plan.storedBytes > ~u64{0} - segment.storedSize || plan.decodedBytes > ~u64{0} - segment.logicalSize || plan.touchedSegments == ~u32{0})
                        return ResourceSourceResult::LimitExceeded;
                    plan.storedBytes += segment.storedSize;
                    plan.decodedBytes += segment.logicalSize;
                    if (!foundFirstSegment)
                    {
                        plan.decodedRangeOffset = logicalOffset;
                        foundFirstSegment = true;
                    }
                    plan.decodedRangeBytes += segment.logicalSize;
                    ++plan.touchedSegments;
                }
                logicalOffset = segmentEnd;
            }
            return plan.touchedSegments != 0 ? ResourceSourceResult::Success : ResourceSourceResult::InvalidArgument;
        }

        [[nodiscard]] ResourceSourceResult DecodePackagedRange(ResourceReadRequest::Impl& operation) noexcept
        {
            u8* const output = static_cast<u8*>(operation.read.destination);
            for (const AsyncReadPiece* const piece : operation.pieces)
            {
                void* const decoded = static_cast<u8*>(operation.decodedData.address) + piece->decodedBufferOffset;
                const void* const stored = static_cast<const u8*>(operation.storedData.address) + piece->storedBufferOffset;
                const packages::Result decodedResult =
                    PackageOf(*operation.source)
                        .DecodeSegment(piece->segment, stored, static_cast<usize>(piece->segment.storedSize), decoded, static_cast<usize>(piece->segment.logicalSize));
                if (decodedResult != packages::Result::Success)
                    return ConvertPackageResult(decodedResult);

                const u64 requestEnd = operation.read.offset + operation.read.size;
                const u64 segmentEnd = piece->segmentLogicalOffset + piece->segment.logicalSize;
                const u64 copyBegin = operation.read.offset > piece->segmentLogicalOffset ? operation.read.offset : piece->segmentLogicalOffset;
                const u64 copyEnd = requestEnd < segmentEnd ? requestEnd : segmentEnd;
                const u64 sourceOffset = copyBegin - piece->segmentLogicalOffset;
                const u64 destinationOffset = copyBegin - operation.read.offset;
                const u64 copySize = copyEnd - copyBegin;
                std::memcpy(output + destinationOffset, static_cast<const u8*>(decoded) + sourceOffset, static_cast<usize>(copySize));
                ++operation.decodedSegments;
            }
            return ResourceSourceResult::Success;
        }

        void FinishRead(ResourceReadRequest::Impl& operation) noexcept
        {
            ResourceSourceResult result = static_cast<ResourceSourceResult>(operation.result.GetValue());
            if (result == ResourceSourceResult::Success && operation.source->kind == ResourceSourceKind::Package)
            {
                result = DecodePackagedRange(operation);
                if (result != ResourceSourceResult::Success)
                    SetReadFailure(operation, result);
            }
            operation.finished.SetValue(true);
            if (operation.accountedActiveRead && operation.source->accounting != nullptr)
            {
                auto& accounting = *operation.source->accounting;
                accounting.lock.Acquire();
                --accounting.activeReads;
                accounting.bytesRead += operation.storedBytesRead.GetValue();
                accounting.lock.Release();
                operation.accountedActiveRead = false;
            }
            operation.completion.Signal();
            if (operation.callback != nullptr)
            {
                ResourceReadStats stats;
                stats.storedBytesRead = operation.storedBytesRead.GetValue();
                stats.decodedBytesProduced = result == ResourceSourceResult::Success ? operation.read.size : 0;
                stats.decodedSegments = operation.decodedSegments;
                operation.callback(result, stats, operation.userData);
            }
            ReleaseReadOperation(&operation);
        }

        void AsyncRangeReadCompleted(const io::AsyncReadToken& token, const io::AsyncResult result, const u32 bytesTransferred, io::ShareableIOMemory, const u32, io::UniqueBuffer)
        {
            auto& piece = *static_cast<AsyncReadPiece*>(token.m_userData);
            ResourceReadRequest::Impl& operation = *piece.operation;
            static_cast<void>(operation.storedBytesRead.ExchangeAdd(bytesTransferred));
            if (result == io::eAsyncResult_Canceled || operation.cancellationRequested.GetValue() || (operation.ioContext && operation.ioContext->IsCancelRequested()))
                SetReadFailure(operation, ResourceSourceResult::Cancelled);
            else if (result != io::eAsyncResult_Success || bytesTransferred != token.m_numberOfBytesToRead)
                SetReadFailure(operation, ResourceSourceResult::IoFailure);
            if (operation.pendingReads.Decrement() == 0)
                FinishRead(operation);
        }
    } // namespace

    const char* ToString(const ResourceSourceResult result) noexcept
    {
        switch (result)
        {
        case ResourceSourceResult::Success:
            return "Success";
        case ResourceSourceResult::InvalidArgument:
            return "InvalidArgument";
        case ResourceSourceResult::InvalidState:
            return "InvalidState";
        case ResourceSourceResult::NotFound:
            return "NotFound";
        case ResourceSourceResult::TypeMismatch:
            return "TypeMismatch";
        case ResourceSourceResult::IntegrityFailure:
            return "IntegrityFailure";
        case ResourceSourceResult::UnsupportedVersion:
            return "UnsupportedVersion";
        case ResourceSourceResult::LimitExceeded:
            return "LimitExceeded";
        case ResourceSourceResult::BufferTooSmall:
            return "BufferTooSmall";
        case ResourceSourceResult::Cancelled:
            return "Cancelled";
        case ResourceSourceResult::IoFailure:
            return "IoFailure";
        }
        return "Unknown";
    }

    ResourceFailureClass ClassifyFailure(const ResourceSourceResult result) noexcept
    {
        switch (result)
        {
        case ResourceSourceResult::Success:
            return ResourceFailureClass::None;
        case ResourceSourceResult::IoFailure:
            return ResourceFailureClass::Transient;
        case ResourceSourceResult::Cancelled:
            return ResourceFailureClass::Cancelled;
        default:
            return ResourceFailureClass::Permanent;
        }
    }

    ResourceSource::~ResourceSource()
    {
        Close();
    }

    ResourceSource::ResourceSource(ResourceSource&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    ResourceSource& ResourceSource::operator=(ResourceSource&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    ResourceSourceResult ResourceSource::OpenLoose(const filesystem::AbsolutePath& physicalPath, const resources::ResourceId resource, const resources::ResourceTypeId type) noexcept
    {
        if (m_impl != nullptr || !physicalPath.IsFilePath() || !io::IsInitialized())
            return m_impl != nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        const io::FileHandle file = io::GetSystem().OpenFile(physicalPath.AsChar());
        if (file == io::InvalidFileHandle)
            return ResourceSourceResult::NotFound;
        Impl* const impl = AllocateSourceObject<Impl>();
        if (impl == nullptr)
        {
            io::GetSystem().ReleaseFile(file);
            return ResourceSourceResult::LimitExceeded;
        }
        impl->physicalPath = physicalPath;
        impl->pinnedFile = file;
        impl->resource = resource;
        impl->type = type;
        impl->logicalSize = io::GetSystem().GetFileSize(file);
        impl->kind = ResourceSourceKind::LooseFile;
        m_impl = impl;
        return ResourceSourceResult::Success;
    }

    ResourceSourceResult ResourceSource::OpenPackage(const filesystem::AbsolutePath& physicalPath, const resources::ResourceId resource,
                                                     const resources::ResourceTypeId expectedType) noexcept
    {
        if (m_impl != nullptr || !physicalPath.IsFilePath() || resource == resources::InvalidResourceId || !io::IsInitialized())
            return m_impl != nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        detail::ResourceSourcePackageGeneration* const generation = detail::CreateResourceSourcePackageGeneration(physicalPath);
        if (generation == nullptr)
            return ResourceSourceResult::NotFound;
        const ResourceSourceResult opened = OpenPackage(generation, resource, expectedType);
        detail::ReleaseResourceSourcePackageGeneration(generation);
        return opened;
    }

    ResourceSourceResult ResourceSource::OpenPackage(detail::ResourceSourcePackageGeneration* const generation, const resources::ResourceId resource,
                                                     const resources::ResourceTypeId expectedType) noexcept
    {
        if (m_impl != nullptr || generation == nullptr || resource == resources::InvalidResourceId)
            return m_impl != nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        Impl* const impl = AllocateSourceObject<Impl>();
        if (impl == nullptr)
            return ResourceSourceResult::LimitExceeded;
        impl->physicalPath = generation->physicalPath;
        impl->packageGeneration = generation;
        detail::RetainResourceSourcePackageGeneration(generation);
        impl->packagedResource = generation->package.Find(resource);
        if (impl->packagedResource == nullptr)
        {
            detail::ReleaseResourceSourcePackageGeneration(generation);
            DeleteSourceObject(impl);
            return ResourceSourceResult::NotFound;
        }
        if (expectedType != resources::InvalidResourceTypeId && impl->packagedResource->type != expectedType)
        {
            detail::ReleaseResourceSourcePackageGeneration(generation);
            DeleteSourceObject(impl);
            return ResourceSourceResult::TypeMismatch;
        }
        impl->resource = resource;
        impl->type = impl->packagedResource->type;
        impl->logicalSize = impl->packagedResource->logicalSize;
        impl->kind = ResourceSourceKind::Package;
        m_impl = impl;
        return ResourceSourceResult::Success;
    }

    void ResourceSource::Close() noexcept
    {
        if (m_impl == nullptr)
            return;
        Impl* const impl = m_impl;
        m_impl = nullptr;
        ReleaseSource(impl);
    }

    bool ResourceSource::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }

    ResourceSourceKind ResourceSource::GetKind() const noexcept
    {
        return m_impl != nullptr ? m_impl->kind : ResourceSourceKind::LooseFile;
    }

    const filesystem::AbsolutePath& ResourceSource::GetPhysicalPath() const noexcept
    {
        static const filesystem::AbsolutePath empty;
        return m_impl != nullptr ? m_impl->physicalPath : empty;
    }

    u64 ResourceSource::GetLogicalSize() const noexcept
    {
        return m_impl != nullptr ? m_impl->logicalSize : 0;
    }

    resources::ResourceId ResourceSource::GetResourceId() const noexcept
    {
        return m_impl != nullptr ? m_impl->resource : resources::InvalidResourceId;
    }

    resources::ResourceTypeId ResourceSource::GetResourceType() const noexcept
    {
        return m_impl != nullptr ? m_impl->type : resources::InvalidResourceTypeId;
    }

    u64 ResourceSource::GetStagingBudgetBytes() const noexcept
    {
        return m_impl != nullptr ? detail::GetResourceSourceAccountingStats(m_impl->accounting).stagingBudgetBytes : 0;
    }

    ResourceSourceResult ResourceSource::PlanRead(const u64 offset, const u64 size, ResourceReadPlan& plan) const noexcept
    {
        if (m_impl == nullptr)
        {
            plan = {};
            return ResourceSourceResult::InvalidState;
        }
        return BuildReadPlan(*m_impl, offset, size, plan);
    }

    ResourceSourceResult ResourceSource::ReadAsync(const ResourceRangeRead& read, ResourceReadRequest& request, const ResourceReadCallback callback, void* const userData) const noexcept
    {
        return ReadAsyncInternal(read, request, callback, userData, false);
    }

    ResourceSourceResult ResourceSource::ReadAsyncInternal(const ResourceRangeRead& read, ResourceReadRequest& request, const ResourceReadCallback callback, void* const userData,
                                                           const bool stagingPreReserved) const noexcept
    {
        if (m_impl == nullptr || request.IsValid())
            return m_impl == nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        if (read.destinationSize < read.size || (read.size != 0 && read.destination == nullptr) || read.offset > static_cast<u64>(std::numeric_limits<i64>::max()))
            return read.destinationSize < read.size ? ResourceSourceResult::BufferTooSmall : ResourceSourceResult::InvalidArgument;

        ResourceReadPlan plan;
        ResourceSourceResult result = BuildReadPlan(*m_impl, read.offset, read.size, plan);
        if (result != ResourceSourceResult::Success)
            return result;
        if (plan.storedBytes > static_cast<u64>(std::numeric_limits<usize>::max()) || plan.decodedBytes > static_cast<u64>(std::numeric_limits<usize>::max()))
            return ResourceSourceResult::LimitExceeded;

        const u64 stagingBytes = m_impl->kind == ResourceSourceKind::Package ? plan.storedBytes + plan.decodedBytes : 0;
        if ((m_impl->kind == ResourceSourceKind::Package && stagingBytes < plan.storedBytes) ||
            (!stagingPreReserved && !detail::ReserveResourceSourceStaging(m_impl->accounting, stagingBytes)))
            return ResourceSourceResult::LimitExceeded;

        ResourceReadRequest::Impl* const operation = AllocateSourceObject<ResourceReadRequest::Impl>();
        if (operation == nullptr)
        {
            if (!stagingPreReserved)
                detail::ReleaseResourceSourceStaging(m_impl->accounting, stagingBytes);
            return ResourceSourceResult::LimitExceeded;
        }
        operation->source = m_impl;
        RetainSource(*m_impl);
        operation->reservedStagingBytes = stagingPreReserved ? 0 : stagingBytes;
        operation->read = read;
        operation->plan = plan;
        operation->callback = callback;
        operation->userData = userData;
        operation->ioContext = red::CreateSharedPtr<io::IOContext>();
        if (!operation->ioContext)
        {
            ReleaseReadOperation(operation);
            ReleaseReadOperation(operation);
            return ResourceSourceResult::LimitExceeded;
        }

        if (m_impl->kind == ResourceSourceKind::Package)
        {
            if (plan.storedBytes != 0)
                operation->storedData = memory::Allocate(memory::PoolId::Streaming, static_cast<usize>(plan.storedBytes), 16);
            if (plan.decodedBytes != 0)
                operation->decodedData = memory::Allocate(memory::PoolId::Streaming, static_cast<usize>(plan.decodedBytes), 16);
            if ((plan.storedBytes != 0 && !operation->storedData) || (plan.decodedBytes != 0 && !operation->decodedData))
            {
                ReleaseReadOperation(operation);
                ReleaseReadOperation(operation);
                return ResourceSourceResult::LimitExceeded;
            }

            const u64 requestEnd = read.offset + read.size;
            u64 logicalOffset = 0;
            u64 storedOffset = 0;
            u64 decodedOffset = 0;
            for (const packages::Segment& segment : PackageOf(*m_impl).GetSegments(*m_impl->packagedResource))
            {
                const u64 segmentEnd = logicalOffset + segment.logicalSize;
                if (read.offset < segmentEnd && requestEnd > logicalOffset)
                {
                    if (segment.offset > static_cast<u64>(std::numeric_limits<i64>::max()) || segment.storedSize > std::numeric_limits<u32>::max())
                    {
                        result = ResourceSourceResult::LimitExceeded;
                        break;
                    }
                    AsyncReadPiece* const piece = AllocateSourceObject<AsyncReadPiece>();
                    if (piece == nullptr)
                    {
                        result = ResourceSourceResult::LimitExceeded;
                        break;
                    }
                    piece->operation = operation;
                    piece->segment = segment;
                    piece->segmentLogicalOffset = logicalOffset;
                    piece->storedBufferOffset = storedOffset;
                    piece->decodedBufferOffset = decodedOffset;
                    piece->packaged = true;
                    piece->token.m_callback = AsyncRangeReadCompleted;
                    piece->token.m_userData = piece;
                    piece->token.m_buffer = static_cast<u8*>(operation->storedData.address) + storedOffset;
                    piece->token.m_debugLogicalFileName = m_impl->physicalPath.AsChar();
                    piece->token.m_offset = static_cast<i64>(segment.offset);
                    piece->token.m_numberOfBytesToRead = static_cast<u32>(segment.storedSize);
                    piece->token.m_ioContext = operation->ioContext;
                    piece->token.m_requestSource = io::RequestSource::ResourceSystem;
                    const u32 before = operation->pieces.Size();
                    operation->pieces.PushBack(piece);
                    if (operation->pieces.Size() != before + 1u)
                    {
                        DeleteSourceObject(piece);
                        result = ResourceSourceResult::LimitExceeded;
                        break;
                    }
                    storedOffset += segment.storedSize;
                    decodedOffset += segment.logicalSize;
                }
                logicalOffset = segmentEnd;
            }
        }
        else
        {
            u64 remaining = read.size;
            u64 rangeOffset = 0;
            while (remaining != 0)
            {
                const u32 chunk = remaining > std::numeric_limits<u32>::max() ? std::numeric_limits<u32>::max() : static_cast<u32>(remaining);
                AsyncReadPiece* const piece = AllocateSourceObject<AsyncReadPiece>();
                if (piece == nullptr)
                {
                    result = ResourceSourceResult::LimitExceeded;
                    break;
                }
                piece->operation = operation;
                piece->token.m_callback = AsyncRangeReadCompleted;
                piece->token.m_userData = piece;
                piece->token.m_buffer = static_cast<u8*>(read.destination) + rangeOffset;
                piece->token.m_debugLogicalFileName = m_impl->physicalPath.AsChar();
                piece->token.m_offset = static_cast<i64>(read.offset + rangeOffset);
                piece->token.m_numberOfBytesToRead = chunk;
                piece->token.m_ioContext = operation->ioContext;
                piece->token.m_requestSource = io::RequestSource::ResourceSystem;
                const u32 before = operation->pieces.Size();
                operation->pieces.PushBack(piece);
                if (operation->pieces.Size() != before + 1u)
                {
                    DeleteSourceObject(piece);
                    result = ResourceSourceResult::LimitExceeded;
                    break;
                }
                rangeOffset += chunk;
                remaining -= chunk;
            }
        }

        if (result != ResourceSourceResult::Success || operation->pieces.Size() != plan.touchedSegments)
        {
            ReleaseReadOperation(operation);
            ReleaseReadOperation(operation);
            return result != ResourceSourceResult::Success ? result : ResourceSourceResult::LimitExceeded;
        }

        request = ResourceReadRequest(operation);
        if (m_impl->accounting != nullptr)
        {
            m_impl->accounting->lock.Acquire();
            ++m_impl->accounting->activeReads;
            m_impl->accounting->lock.Release();
            operation->accountedActiveRead = true;
        }
        if (operation->pieces.Empty())
        {
            FinishRead(*operation);
            return ResourceSourceResult::Success;
        }
        operation->pendingReads.SetValue(operation->pieces.Size());
        for (AsyncReadPiece* const piece : operation->pieces)
            io::GetSystem().BeginRead(PinnedFileOf(*m_impl), piece->token, read.priority);
        return ResourceSourceResult::Success;
    }

    void ResourceSource::AttachAccounting(detail::ResourceSourceAccounting* const accounting) noexcept
    {
        if (m_impl == nullptr || m_impl->accounting == accounting)
            return;
        detail::RetainResourceSourceAccounting(accounting);
        detail::ReleaseResourceSourceAccounting(m_impl->accounting);
        m_impl->accounting = accounting;
    }

    ResourceReadRequest::ResourceReadRequest(Impl* const impl) noexcept : m_impl(impl) {}

    ResourceReadRequest::~ResourceReadRequest()
    {
        Reset();
    }

    ResourceReadRequest::ResourceReadRequest(ResourceReadRequest&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    ResourceReadRequest& ResourceReadRequest::operator=(ResourceReadRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    bool ResourceReadRequest::IsValid() const noexcept
    {
        return m_impl != nullptr;
    }

    bool ResourceReadRequest::HasFinished() const noexcept
    {
        return m_impl != nullptr && m_impl->finished.GetValue();
    }

    void ResourceReadRequest::Wait() const noexcept
    {
        if (m_impl != nullptr)
            m_impl->completion.Wait();
    }

    bool ResourceReadRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_impl != nullptr && m_impl->completion.TryWait(timeoutMilliseconds);
    }

    bool ResourceReadRequest::Cancel() noexcept
    {
        if (m_impl == nullptr || m_impl->finished.GetValue())
            return false;
        m_impl->cancellationRequested.SetValue(true);
        m_impl->ioContext->RequestCancel();
        return true;
    }

    ResourceSourceResult ResourceReadRequest::GetResult() const noexcept
    {
        return m_impl != nullptr && m_impl->finished.GetValue() ? static_cast<ResourceSourceResult>(m_impl->result.GetValue()) : ResourceSourceResult::InvalidState;
    }

    ResourceReadPlan ResourceReadRequest::GetPlan() const noexcept
    {
        return m_impl != nullptr ? m_impl->plan : ResourceReadPlan{};
    }

    ResourceReadStats ResourceReadRequest::GetStats() const noexcept
    {
        ResourceReadStats stats;
        if (m_impl != nullptr)
        {
            stats.storedBytesRead = m_impl->storedBytesRead.GetValue();
            stats.decodedBytesProduced = m_impl->finished.GetValue() && m_impl->result.GetValue() == static_cast<u32>(ResourceSourceResult::Success) ? m_impl->read.size : 0;
            stats.decodedSegments = m_impl->decodedSegments;
        }
        return stats;
    }

    void ResourceReadRequest::Reset() noexcept
    {
        if (m_impl == nullptr)
            return;
        if (!m_impl->finished.GetValue())
            static_cast<void>(Cancel());
        Impl* const impl = m_impl;
        m_impl = nullptr;
        ReleaseReadOperation(impl);
    }

    struct ResourceRangeReadQueue::Impl;

    struct CoalescedResourceReadRequest::Impl
    {
        Impl() noexcept : bytes(memory::pools::Streaming::GetInstance()) {}
        void Start() noexcept;

        concurrency::Atomic<u32> references{2}; // public interest + queue list
        concurrency::Atomic<u32> interests{1};
        concurrency::Atomic<u32> result{static_cast<u32>(ResourceSourceResult::Success)};
        concurrency::Atomic<bool> finished{false};
        concurrency::ManualResetEvent completion;
        ResourceRangeReadQueue::Impl* owner = nullptr;
        ResourceReadRequest sourceRequest;
        containers::DynamicArray<u8> bytes;
        ResourceReadStats stats;
        u64 offset = 0;
        u64 size = 0;
        u64 requiredBytes = 0;
        u64 admittedBytes = 0;
        crypto::Digest256 expectedDigest;
        io::AsyncPriority priority = io::eAsyncPriority_Streaming;
        u32 attemptCount = 0;
        bool verifyDigest = false;
        bool queued = true;
        bool listed = true;
    };

    struct ResourceRangeReadQueue::Impl
    {
        Impl() noexcept : operations(memory::pools::Streaming::GetInstance()) {}

        concurrency::Atomic<u32> references{1};
        concurrency::RWLock lock;
        ResourceSource source;
        detail::ResourceSourceAccounting* accounting = nullptr;
        containers::DynamicArray<CoalescedResourceReadRequest::Impl*> operations;
        u32 maximumTransientRetries = 2;
        bool accepting = true;
    };

    namespace
    {
        void RetainRangeQueue(ResourceRangeReadQueue::Impl& queue) noexcept
        {
            static_cast<void>(queue.references.Increment());
        }

        void ReleaseRangeQueue(ResourceRangeReadQueue::Impl* queue) noexcept
        {
            if (queue != nullptr && queue->references.Decrement() == 0)
                DeleteSourceObject(queue);
        }

        void PumpRangeQueue(ResourceRangeReadQueue::Impl& queue) noexcept;

        void ReleaseCoalescedRead(CoalescedResourceReadRequest::Impl* operation) noexcept
        {
            if (operation == nullptr || operation->references.Decrement() != 0)
                return;
            ResourceRangeReadQueue::Impl* const owner = operation->owner;
            const u64 admittedBytes = operation->admittedBytes;
            operation->admittedBytes = 0;
            DeleteSourceObject(operation);
            if (admittedBytes != 0)
            {
                detail::ReleaseResourceSourceStaging(owner->accounting, admittedBytes);
                PumpRangeQueue(*owner);
            }
            ReleaseRangeQueue(owner);
        }

        void RemoveCoalescedReadLocked(ResourceRangeReadQueue::Impl& queue, CoalescedResourceReadRequest::Impl& operation) noexcept
        {
            if (!operation.listed)
                return;
            for (u32 index = 0; index < queue.operations.Size(); ++index)
            {
                if (queue.operations[index] == &operation)
                {
                    static_cast<void>(queue.operations.RemoveAt(index));
                    operation.listed = false;
                    return;
                }
            }
        }

        void FinishCoalescedRead(CoalescedResourceReadRequest::Impl& operation, const ResourceSourceResult result, const ResourceReadStats& stats) noexcept
        {
            operation.stats = stats;
            operation.result.SetValue(static_cast<u32>(result));
            operation.finished.SetValue(true);
            auto& owner = *operation.owner;
            owner.lock.Acquire();
            RemoveCoalescedReadLocked(owner, operation);
            owner.lock.Release();
            operation.completion.Signal();
            ReleaseCoalescedRead(&operation); // queue-list reference
        }

        void CompleteCoalescedSourceRead(const ResourceSourceResult result, const ResourceReadStats& stats, void* const userData)
        {
            auto& operation = *static_cast<CoalescedResourceReadRequest::Impl*>(userData);
            operation.stats.storedBytesRead += stats.storedBytesRead;
            operation.stats.decodedBytesProduced += stats.decodedBytesProduced;
            operation.stats.decodedSegments += stats.decodedSegments;
            if (ClassifyFailure(result) == ResourceFailureClass::Transient && operation.interests.GetValue() != 0 && operation.attemptCount <= operation.owner->maximumTransientRetries)
            {
                operation.sourceRequest.Reset();
                operation.Start();
                return;
            }
            ResourceSourceResult finalResult = result;
            if (finalResult == ResourceSourceResult::Success && operation.verifyDigest && !(crypto::Sha256(operation.bytes.TypedData(), operation.bytes.Size()) == operation.expectedDigest))
                finalResult = ResourceSourceResult::IntegrityFailure;
            FinishCoalescedRead(operation, finalResult, operation.stats);
        }

        void StartCoalescedRead(CoalescedResourceReadRequest::Impl& operation) noexcept
        {
            operation.Start();
        }

        void PumpRangeQueue(ResourceRangeReadQueue::Impl& queue) noexcept
        {
            containers::DynamicArray<CoalescedResourceReadRequest::Impl*> ready(memory::pools::Streaming::GetInstance());
            queue.lock.Acquire();
            for (CoalescedResourceReadRequest::Impl* const operation : queue.operations)
            {
                if (!operation->queued)
                    continue;
                if (!detail::ReserveResourceSourceStaging(queue.accounting, operation->requiredBytes))
                    break;
                operation->admittedBytes = operation->requiredBytes;
                operation->queued = false;
                ready.PushBack(operation);
            }
            queue.lock.Release();
            for (CoalescedResourceReadRequest::Impl* const operation : ready)
                StartCoalescedRead(*operation);
        }
    } // namespace

    void CoalescedResourceReadRequest::Impl::Start() noexcept
    {
        if (interests.GetValue() == 0)
        {
            FinishCoalescedRead(*this, ResourceSourceResult::Cancelled, {});
            return;
        }
        bytes.Resize(static_cast<u32>(size));
        if (bytes.Size() != size)
        {
            FinishCoalescedRead(*this, ResourceSourceResult::LimitExceeded, {});
            return;
        }
        const ResourceRangeRead read{offset, size, bytes.TypedData(), bytes.Size(), priority};
        ++attemptCount;
        const ResourceSourceResult started = owner->source.ReadAsyncInternal(read, sourceRequest, CompleteCoalescedSourceRead, this, true);
        if (started != ResourceSourceResult::Success)
            FinishCoalescedRead(*this, started, {});
    }

    CoalescedResourceReadRequest::CoalescedResourceReadRequest(Impl* const impl) noexcept : m_impl(impl), m_hasInterest(impl != nullptr) {}

    CoalescedResourceReadRequest::~CoalescedResourceReadRequest()
    {
        Reset();
    }

    CoalescedResourceReadRequest::CoalescedResourceReadRequest(CoalescedResourceReadRequest&& other) noexcept : m_impl(other.m_impl), m_hasInterest(other.m_hasInterest)
    {
        other.m_impl = nullptr;
        other.m_hasInterest = false;
    }

    CoalescedResourceReadRequest& CoalescedResourceReadRequest::operator=(CoalescedResourceReadRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            m_hasInterest = other.m_hasInterest;
            other.m_impl = nullptr;
            other.m_hasInterest = false;
        }
        return *this;
    }

    bool CoalescedResourceReadRequest::IsValid() const noexcept
    {
        return m_impl != nullptr;
    }
    bool CoalescedResourceReadRequest::HasFinished() const noexcept
    {
        return m_impl != nullptr && m_impl->finished.GetValue();
    }
    void CoalescedResourceReadRequest::Wait() const noexcept
    {
        if (m_impl != nullptr)
            m_impl->completion.Wait();
    }
    bool CoalescedResourceReadRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_impl != nullptr && m_impl->completion.TryWait(timeoutMilliseconds);
    }

    bool CoalescedResourceReadRequest::Cancel() noexcept
    {
        if (m_impl == nullptr || !m_hasInterest || m_impl->finished.GetValue())
            return false;
        m_hasInterest = false;
        if (m_impl->interests.Decrement() != 0)
            return true;
        if (!m_impl->queued)
        {
            static_cast<void>(m_impl->sourceRequest.Cancel());
            return true;
        }
        auto& owner = *m_impl->owner;
        owner.lock.Acquire();
        RemoveCoalescedReadLocked(owner, *m_impl);
        owner.lock.Release();
        m_impl->result.SetValue(static_cast<u32>(ResourceSourceResult::Cancelled));
        m_impl->finished.SetValue(true);
        m_impl->completion.Signal();
        ReleaseCoalescedRead(m_impl);
        return true;
    }

    ResourceSourceResult CoalescedResourceReadRequest::GetResult() const noexcept
    {
        return HasFinished() ? static_cast<ResourceSourceResult>(m_impl->result.GetValue()) : ResourceSourceResult::InvalidState;
    }

    ResourceReadStats CoalescedResourceReadRequest::GetStats() const noexcept
    {
        return HasFinished() ? m_impl->stats : ResourceReadStats{};
    }
    u32 CoalescedResourceReadRequest::GetAttemptCount() const noexcept
    {
        return m_impl != nullptr ? m_impl->attemptCount : 0;
    }
    containers::ArraySpan<const u8> CoalescedResourceReadRequest::GetBytes() const noexcept
    {
        return GetResult() == ResourceSourceResult::Success ? containers::ArraySpan<const u8>(m_impl->bytes.TypedData(), m_impl->bytes.Size()) : containers::ArraySpan<const u8>();
    }

    bool CoalescedResourceReadRequest::IsSameOperation(const CoalescedResourceReadRequest& other) const noexcept
    {
        return m_impl != nullptr && m_impl == other.m_impl;
    }

    void CoalescedResourceReadRequest::Reset() noexcept
    {
        if (m_impl == nullptr)
            return;
        if (m_hasInterest)
            static_cast<void>(Cancel());
        Impl* const impl = m_impl;
        m_impl = nullptr;
        m_hasInterest = false;
        ReleaseCoalescedRead(impl);
    }

    ResourceRangeReadQueue::~ResourceRangeReadQueue()
    {
        Close();
    }

    ResourceSourceResult ResourceRangeReadQueue::Open(const ResourceSource& source, const u32 maximumTransientRetries) noexcept
    {
        if (m_impl != nullptr || !source.IsOpen())
            return m_impl != nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        Impl* const impl = AllocateSourceObject<Impl>();
        if (impl == nullptr)
            return ResourceSourceResult::LimitExceeded;
        impl->source.m_impl = source.m_impl;
        RetainSource(*source.m_impl);
        impl->accounting = source.m_impl->accounting;
        impl->maximumTransientRetries = maximumTransientRetries;
        m_impl = impl;
        return ResourceSourceResult::Success;
    }

    void ResourceRangeReadQueue::Close() noexcept
    {
        Impl* const impl = m_impl;
        m_impl = nullptr;
        if (impl != nullptr)
        {
            impl->accepting = false;
            ReleaseRangeQueue(impl);
        }
    }

    bool ResourceRangeReadQueue::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }

    ResourceSourceResult ResourceRangeReadQueue::Read(const u64 offset, const u64 size, CoalescedResourceReadRequest& request, const io::AsyncPriority priority) noexcept
    {
        return ReadInternal(offset, size, nullptr, request, priority);
    }

    ResourceSourceResult ResourceRangeReadQueue::ReadVerified(const u64 offset, const u64 size, const crypto::Digest256& expectedDigest, CoalescedResourceReadRequest& request,
                                                              const io::AsyncPriority priority) noexcept
    {
        return ReadInternal(offset, size, &expectedDigest, request, priority);
    }

    ResourceSourceResult ResourceRangeReadQueue::ReadInternal(const u64 offset, const u64 size, const crypto::Digest256* const expectedDigest, CoalescedResourceReadRequest& request,
                                                              const io::AsyncPriority priority) noexcept
    {
        if (m_impl == nullptr || request.IsValid() || size > ~u32{0})
            return m_impl == nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        ResourceReadPlan plan;
        const ResourceSourceResult planned = m_impl->source.PlanRead(offset, size, plan);
        if (planned != ResourceSourceResult::Success)
            return planned;
        u64 admission = size;
        if (m_impl->source.GetKind() == ResourceSourceKind::Package)
        {
            if (admission > ~u64{0} - plan.storedBytes || admission + plan.storedBytes > ~u64{0} - plan.decodedBytes)
                return ResourceSourceResult::LimitExceeded;
            admission += plan.storedBytes + plan.decodedBytes;
        }
        const detail::ResourceSourceAccountingStats accounting = detail::GetResourceSourceAccountingStats(m_impl->accounting);
        if (m_impl->accounting != nullptr && admission > accounting.stagingBudgetBytes)
            return ResourceSourceResult::LimitExceeded;

        m_impl->lock.Acquire();
        for (CoalescedResourceReadRequest::Impl* const existing : m_impl->operations)
        {
            const bool sameVerification = expectedDigest == nullptr ? !existing->verifyDigest : existing->verifyDigest && existing->expectedDigest == *expectedDigest;
            if (existing->offset == offset && existing->size == size && sameVerification && !existing->finished.GetValue())
            {
                static_cast<void>(existing->references.Increment());
                static_cast<void>(existing->interests.Increment());
                m_impl->lock.Release();
                request = CoalescedResourceReadRequest(existing);
                return ResourceSourceResult::Success;
            }
        }
        auto* const operation = AllocateSourceObject<CoalescedResourceReadRequest::Impl>();
        if (operation == nullptr)
        {
            m_impl->lock.Release();
            return ResourceSourceResult::LimitExceeded;
        }
        operation->owner = m_impl;
        operation->offset = offset;
        operation->size = size;
        operation->requiredBytes = admission;
        operation->priority = priority;
        operation->verifyDigest = expectedDigest != nullptr;
        if (expectedDigest != nullptr)
            operation->expectedDigest = *expectedDigest;
        RetainRangeQueue(*m_impl);
        m_impl->operations.PushBack(operation);
        m_impl->lock.Release();
        request = CoalescedResourceReadRequest(operation);
        PumpRangeQueue(*m_impl);
        return ResourceSourceResult::Success;
    }

    ResourceSourceReader::ResourceSourceReader() noexcept : filesystem::IFile(filesystem::FF_Reader | filesystem::FF_FileBased) {}

    ResourceSourceReader::~ResourceSourceReader()
    {
        Close();
    }

    ResourceSourceResult ResourceSourceReader::Open(const ResourceSource& source) noexcept
    {
        if (m_impl != nullptr || source.m_impl == nullptr)
            return m_impl != nullptr ? ResourceSourceResult::InvalidState : ResourceSourceResult::InvalidArgument;
        Impl* const impl = AllocateSourceObject<Impl>(*source.m_impl);
        if (impl == nullptr)
            return ResourceSourceResult::LimitExceeded;
        if (source.m_impl->kind == ResourceSourceKind::Package)
        {
            const packages::Result opened = impl->logical.Open(PackageOf(*source.m_impl), *source.m_impl->packagedResource, impl->physical);
            if (opened != packages::Result::Success)
            {
                const ResourceSourceResult result = ConvertPackageResult(opened);
                DeleteSourceObject(impl);
                return result;
            }
            impl->active = &impl->logical;
        }
        m_impl = impl;
        return ResourceSourceResult::Success;
    }

    void ResourceSourceReader::Close() noexcept
    {
        if (m_impl != nullptr)
            m_impl->logical.Close();
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteSourceObject(impl);
        m_flags &= ~filesystem::FF_ErrorOccured;
    }

    bool ResourceSourceReader::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }

    ResourceSourceResult ResourceSourceReader::GetLastResult() const noexcept
    {
        if (m_impl == nullptr)
            return ResourceSourceResult::InvalidState;
        if (m_impl->active == &m_impl->logical && m_impl->logical.GetLastResult() != packages::Result::Success)
            return ConvertPackageResult(m_impl->logical.GetLastResult());
        return m_impl->physical.Succeeded() ? m_impl->result : ResourceSourceResult::IoFailure;
    }

    ResourceReadStats ResourceSourceReader::GetStats() const noexcept
    {
        ResourceReadStats stats;
        if (m_impl != nullptr && m_impl->active == &m_impl->logical)
        {
            stats.storedBytesRead = m_impl->logical.GetStoredBytesRead();
            stats.decodedSegments = m_impl->logical.GetDecodedSegmentCount();
        }
        else if (m_impl != nullptr)
        {
            stats.storedBytesRead = m_impl->physical.GetBytesRead();
        }
        return stats;
    }

    void ResourceSourceReader::Serialize(void* const buffer, const size_t size)
    {
        if (m_impl == nullptr)
        {
            m_flags |= filesystem::FF_ErrorOccured;
            return;
        }
        m_impl->active->Serialize(buffer, size);
        if (GetLastResult() != ResourceSourceResult::Success)
            m_flags |= filesystem::FF_ErrorOccured;
    }

    Uint64 ResourceSourceReader::GetOffset() const
    {
        return m_impl != nullptr ? m_impl->active->GetOffset() : 0;
    }

    Uint64 ResourceSourceReader::GetSize() const
    {
        return m_impl != nullptr ? m_impl->active->GetSize() : 0;
    }

    void ResourceSourceReader::Seek(const Int64 offset)
    {
        if (m_impl == nullptr)
        {
            m_flags |= filesystem::FF_ErrorOccured;
            return;
        }
        m_impl->active->Seek(offset);
        if (GetLastResult() != ResourceSourceResult::Success)
            m_flags |= filesystem::FF_ErrorOccured;
    }

    void ResourceSourceReader::Flush() {}

    const char* ResourceSourceReader::GetFileNameForDebug() const
    {
        return m_impl != nullptr ? m_impl->owner->physicalPath.AsChar() : "";
    }

    ResourcePrefixReader::ResourcePrefixReader(const containers::ArraySpan<const u8> bytes, const u64 logicalSize, const char* const debugName) noexcept
        : filesystem::IFile(filesystem::FF_Reader | filesystem::FF_MemoryBased), m_bytes(bytes), m_debugName(debugName != nullptr ? debugName : "resource metadata prefix"),
          m_logicalSize(logicalSize)
    {
    }

    void ResourcePrefixReader::Serialize(void* const destination, const size_t size)
    {
        if (m_offset > m_bytes.Count() || size > m_bytes.Count() - m_offset)
        {
            m_flags |= filesystem::FF_ErrorOccured;
            return;
        }
        std::memcpy(destination, m_bytes.Data() + m_offset, size);
        m_offset += size;
    }

    Uint64 ResourcePrefixReader::GetOffset() const
    {
        return m_offset;
    }
    Uint64 ResourcePrefixReader::GetSize() const
    {
        return m_logicalSize;
    }
    void ResourcePrefixReader::Seek(const Int64 offset)
    {
        if (offset < 0 || static_cast<u64>(offset) > m_bytes.Count())
            m_flags |= filesystem::FF_ErrorOccured;
        else
            m_offset = static_cast<u64>(offset);
    }
    void ResourcePrefixReader::Flush() {}
    const char* ResourcePrefixReader::GetFileNameForDebug() const
    {
        return m_debugName;
    }
} // namespace vanguard::streaming
