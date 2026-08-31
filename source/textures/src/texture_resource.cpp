#include <vanguard/textures/texture_resource.hpp>

#include <vanguard/memory/memory.hpp>

#include <limits>
#include <new>

namespace vanguard::textures
{
    namespace
    {
        template <typename T, typename... Args> [[nodiscard]] T* AllocateTextureObject(memory::PoolId pool, Args&&... args) noexcept
        {
            memory::MemoryBlock block = memory::Allocate(pool, sizeof(T), alignof(T));
            return block ? ::new (block.address) T(static_cast<Args&&>(args)...) : nullptr;
        }

        template <typename T> void DeleteTextureObject(T* object, memory::PoolId pool) noexcept
        {
            if (object == nullptr)
                return;
            object->~T();
            memory::MemoryBlock block{object, sizeof(T), pool};
            memory::Free(block);
        }

        [[nodiscard]] Result ConvertSourceResult(const streaming::ResourceSourceResult result) noexcept
        {
            switch (result)
            {
            case streaming::ResourceSourceResult::Success:
                return Result::Success;
            case streaming::ResourceSourceResult::InvalidArgument:
            case streaming::ResourceSourceResult::TypeMismatch:
                return Result::InvalidArgument;
            case streaming::ResourceSourceResult::InvalidState:
                return Result::InvalidState;
            case streaming::ResourceSourceResult::IntegrityFailure:
                return Result::IntegrityFailure;
            case streaming::ResourceSourceResult::UnsupportedVersion:
                return Result::UnsupportedVersion;
            case streaming::ResourceSourceResult::LimitExceeded:
                return Result::LimitExceeded;
            case streaming::ResourceSourceResult::BufferTooSmall:
                return Result::BufferTooSmall;
            case streaming::ResourceSourceResult::Cancelled:
                return Result::Cancelled;
            case streaming::ResourceSourceResult::NotFound:
            case streaming::ResourceSourceResult::IoFailure:
                return Result::IoFailure;
            }
            return Result::IoFailure;
        }

        [[nodiscard]] bool AddChecked(u64& value, const u64 addition) noexcept
        {
            if (value > std::numeric_limits<u64>::max() - addition)
                return false;
            value += addition;
            return true;
        }
    } // namespace

    struct TextureSubresourceSource::Impl
    {
        streaming::ResourceSource source;
        streaming::ResourceRangeReadQueue reads;
    };

    struct TextureSubresourceReadRequest::Impl
    {
        streaming::CoalescedResourceReadRequest sourceRequest;
    };

    TextureSubresourceReadRequest::TextureSubresourceReadRequest(Impl* const impl) noexcept : m_impl(impl) {}
    TextureSubresourceReadRequest::~TextureSubresourceReadRequest()
    {
        Reset();
    }
    TextureSubresourceReadRequest::TextureSubresourceReadRequest(TextureSubresourceReadRequest&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }
    TextureSubresourceReadRequest& TextureSubresourceReadRequest::operator=(TextureSubresourceReadRequest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }
    bool TextureSubresourceReadRequest::IsValid() const noexcept
    {
        return m_impl != nullptr;
    }
    bool TextureSubresourceReadRequest::HasFinished() const noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.HasFinished();
    }
    void TextureSubresourceReadRequest::Wait() const noexcept
    {
        if (m_impl != nullptr)
            m_impl->sourceRequest.Wait();
    }
    bool TextureSubresourceReadRequest::TryWait(const u32 milliseconds) const noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.TryWait(milliseconds);
    }
    bool TextureSubresourceReadRequest::Cancel() noexcept
    {
        return m_impl != nullptr && m_impl->sourceRequest.Cancel();
    }
    Result TextureSubresourceReadRequest::GetResult() const noexcept
    {
        return HasFinished() ? ConvertSourceResult(m_impl->sourceRequest.GetResult()) : Result::InvalidState;
    }
    TextureSubresourceReadStats TextureSubresourceReadRequest::GetStats() const noexcept
    {
        if (!HasFinished())
            return {};
        const streaming::ResourceReadStats stats = m_impl->sourceRequest.GetStats();
        return {stats.storedBytesRead, stats.decodedBytesProduced, stats.decodedSegments};
    }
    containers::ArraySpan<const u8> TextureSubresourceReadRequest::GetBytes() const noexcept
    {
        return GetResult() == Result::Success ? m_impl->sourceRequest.GetBytes() : containers::ArraySpan<const u8>();
    }
    void TextureSubresourceReadRequest::Reset() noexcept
    {
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteTextureObject(impl, memory::PoolId::Streaming);
    }

    TextureSubresourceSource::~TextureSubresourceSource()
    {
        Close();
    }
    TextureSubresourceSource::TextureSubresourceSource(TextureSubresourceSource&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }
    TextureSubresourceSource& TextureSubresourceSource::operator=(TextureSubresourceSource&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }
    Result TextureSubresourceSource::OpenLoose(const filesystem::AbsolutePath& path) noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        Impl* const impl = AllocateTextureObject<Impl>(memory::PoolId::Streaming);
        if (impl == nullptr)
            return Result::LimitExceeded;
        Result result = ConvertSourceResult(impl->source.OpenLoose(path, resources::InvalidResourceId, TextureResourceType));
        if (result == Result::Success)
            result = ConvertSourceResult(impl->reads.Open(impl->source));
        if (result != Result::Success)
        {
            DeleteTextureObject(impl, memory::PoolId::Streaming);
            return result;
        }
        m_impl = impl;
        return Result::Success;
    }
    Result TextureSubresourceSource::OpenPackage(const filesystem::AbsolutePath& path, const resources::ResourceId resource) noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        Impl* const impl = AllocateTextureObject<Impl>(memory::PoolId::Streaming);
        if (impl == nullptr)
            return Result::LimitExceeded;
        Result result = ConvertSourceResult(impl->source.OpenPackage(path, resource, TextureResourceType));
        if (result == Result::Success)
            result = ConvertSourceResult(impl->reads.Open(impl->source));
        if (result != Result::Success)
        {
            DeleteTextureObject(impl, memory::PoolId::Streaming);
            return result;
        }
        m_impl = impl;
        return Result::Success;
    }
    Result TextureSubresourceSource::Open(streaming::ResourceSource&& source) noexcept
    {
        if (m_impl != nullptr || !source.IsOpen() || source.GetResourceType() != TextureResourceType)
            return m_impl != nullptr ? Result::InvalidState : Result::InvalidArgument;
        Impl* const impl = AllocateTextureObject<Impl>(memory::PoolId::Streaming);
        if (impl == nullptr)
            return Result::LimitExceeded;
        impl->source = static_cast<streaming::ResourceSource&&>(source);
        const Result result = ConvertSourceResult(impl->reads.Open(impl->source));
        if (result != Result::Success)
        {
            DeleteTextureObject(impl, memory::PoolId::Streaming);
            return result;
        }
        m_impl = impl;
        return Result::Success;
    }
    void TextureSubresourceSource::Close() noexcept
    {
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteTextureObject(impl, memory::PoolId::Streaming);
    }
    bool TextureSubresourceSource::IsOpen() const noexcept
    {
        return m_impl != nullptr;
    }
    u64 TextureSubresourceSource::GetLogicalSize() const noexcept
    {
        return m_impl != nullptr ? m_impl->source.GetLogicalSize() : 0;
    }
    u64 TextureSubresourceSource::GetStagingBudgetBytes() const noexcept
    {
        return m_impl != nullptr ? m_impl->source.GetStagingBudgetBytes() : 0;
    }
    Result TextureSubresourceSource::PlanSubresource(const TextureFile& texture, const u32 subresource, streaming::ResourceReadPlan& plan, u64& admissionBytes) const noexcept
    {
        plan = {};
        admissionBytes = 0;
        if (m_impl == nullptr || !texture.IsOpen())
            return Result::InvalidState;
        if (subresource >= texture.GetSubresources().Size())
            return Result::InvalidSubresource;
        const SubresourceRecord& record = texture.GetSubresources()[subresource];
        if (texture.GetTextureDataOffset() > std::numeric_limits<u64>::max() - record.dataOffset)
            return Result::InvalidLayout;
        const Result result = ConvertSourceResult(m_impl->source.PlanRead(texture.GetTextureDataOffset() + record.dataOffset, record.byteSize, plan));
        if (result != Result::Success)
            return result;
        admissionBytes = record.byteSize;
        if (m_impl->source.GetKind() == streaming::ResourceSourceKind::Package && (!AddChecked(admissionBytes, plan.storedBytes) || !AddChecked(admissionBytes, plan.decodedBytes)))
            return Result::LimitExceeded;
        return Result::Success;
    }
    Result TextureSubresourceSource::ReadSubresourceAsync(const TextureFile& texture, const u32 subresource, TextureSubresourceReadRequest& request,
                                                          const io::AsyncPriority priority) const noexcept
    {
        if (m_impl == nullptr || !texture.IsOpen() || request.IsValid())
            return Result::InvalidState;
        if (subresource >= texture.GetSubresources().Size())
            return Result::InvalidSubresource;
        const SubresourceRecord& record = texture.GetSubresources()[subresource];
        if (texture.GetTextureDataOffset() > std::numeric_limits<u64>::max() - record.dataOffset)
            return Result::InvalidLayout;
        TextureSubresourceReadRequest::Impl* const operation = AllocateTextureObject<TextureSubresourceReadRequest::Impl>(memory::PoolId::Streaming);
        if (operation == nullptr)
            return Result::LimitExceeded;
        request = TextureSubresourceReadRequest(operation);
        const Result result =
            ConvertSourceResult(m_impl->reads.ReadVerified(texture.GetTextureDataOffset() + record.dataOffset, record.byteSize, record.digest, operation->sourceRequest, priority));
        if (result != Result::Success)
            request.Reset();
        return result;
    }

    TextureResourceObject::~TextureResourceObject()
    {
        Close();
    }
    resources::ResourceTypeId TextureResourceObject::GetType() const noexcept
    {
        return TextureResourceType;
    }
    Result TextureResourceObject::OpenPrepared(TextureSubresourceSource&& source, TextureFile&& metadata) noexcept
    {
        if (m_open || !source.IsOpen() || !metadata.IsOpen())
            return Result::InvalidState;
        m_source = static_cast<TextureSubresourceSource&&>(source);
        m_metadata = static_cast<TextureFile&&>(metadata);
        m_open = true;
        return Result::Success;
    }
    void TextureResourceObject::Close() noexcept
    {
        m_metadata.Close();
        m_source.Close();
        m_open = false;
    }
    bool TextureResourceObject::IsOpen() const noexcept
    {
        return m_open;
    }
    const TextureFile& TextureResourceObject::GetMetadata() const noexcept
    {
        return m_metadata;
    }
    const TextureSubresourceSource& TextureResourceObject::GetSubresourceSource() const noexcept
    {
        return m_source;
    }

    struct TextureMipAcquisition::Impl
    {
        Impl() noexcept : requests(memory::pools::Streaming::GetInstance()), views(memory::pools::Streaming::GetInstance()) {}
        resources::ResourceHandle texture;
        TextureResourceObject* object = nullptr;
        TextureMipReadWindowLimits limits;
        containers::DynamicArray<TextureSubresourceReadRequest> requests;
        containers::DynamicArray<TextureSubresourceView> views;
        u32 firstSubresource = InvalidSubresourceIndex;
        u32 requiredCount = 0;
        u32 cursor = 0;
        u32 windowEnd = 0;
        TextureMipAcquisitionState state = TextureMipAcquisitionState::Invalid;
        Result result = Result::InvalidState;
    };

    TextureMipAcquisition::~TextureMipAcquisition()
    {
        Reset();
    }
    TextureMipAcquisition::TextureMipAcquisition(TextureMipAcquisition&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }
    TextureMipAcquisition& TextureMipAcquisition::operator=(TextureMipAcquisition&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }
    Result TextureMipAcquisition::Open(resources::ResourceHandle texture, const u8 firstMip, const u8 mipCount, const TextureMipReadWindowLimits& limits) noexcept
    {
        if (m_impl != nullptr)
            return Result::InvalidState;
        if (!texture.IsValid() || texture.GetType() != TextureResourceType || limits.maximumAdmissionBytes == 0 || limits.maximumSubresources == 0)
            return Result::InvalidArgument;
        auto* const object = static_cast<TextureResourceObject*>(texture.Get());
        if (object == nullptr || !object->IsOpen() || mipCount == 0 || firstMip >= object->GetMetadata().GetMipCount() || mipCount > object->GetMetadata().GetMipCount() - firstMip)
            return Result::InvalidArgument;
        Impl* const impl = AllocateTextureObject<Impl>(memory::PoolId::Streaming);
        if (impl == nullptr)
            return Result::LimitExceeded;
        impl->texture = static_cast<resources::ResourceHandle&&>(texture);
        impl->object = object;
        impl->limits = limits;
        const TextureFile& metadata = object->GetMetadata();
        const u32 faces = metadata.GetDimension() == TextureDimension::Cube ? 6u : 1u;
        const u64 subresourcesPerMip = static_cast<u64>(metadata.GetArrayLayers()) * faces;
        const u64 requiredCount64 = static_cast<u64>(mipCount) * subresourcesPerMip;
        const u32 firstSubresource = metadata.FindSubresource(firstMip, 0, 0);
        if (firstSubresource == InvalidSubresourceIndex || requiredCount64 == 0 || requiredCount64 > 0xffffffffu ||
            firstSubresource > metadata.GetSubresources().Size() ||
            requiredCount64 > metadata.GetSubresources().Size() - firstSubresource)
        {
            DeleteTextureObject(impl, memory::PoolId::Streaming);
            return Result::MissingSubresource;
        }
        const u32 requiredCount = static_cast<u32>(requiredCount64);
        const u32 lastSubresource = firstSubresource + requiredCount - 1u;
        if (metadata.GetSubresources()[firstSubresource].mipLevel != firstMip ||
            metadata.GetSubresources()[lastSubresource].mipLevel != static_cast<u32>(firstMip) + mipCount - 1u)
        {
            DeleteTextureObject(impl, memory::PoolId::Streaming);
            return Result::MissingSubresource;
        }
        impl->firstSubresource = firstSubresource;
        impl->requiredCount = requiredCount;
        impl->state = TextureMipAcquisitionState::Idle;
        impl->result = Result::Success;
        m_impl = impl;
        return Result::Success;
    }
    Result TextureMipAcquisition::OpenMipTail(resources::ResourceHandle texture, const TextureMipReadWindowLimits& limits) noexcept
    {
        if (!texture.IsValid() || texture.GetType() != TextureResourceType)
            return Result::InvalidArgument;
        const auto* const object = static_cast<const TextureResourceObject*>(texture.Get());
        if (object == nullptr || !object->IsOpen())
            return Result::InvalidArgument;
        const u8 first = object->GetMetadata().GetMipTailFirstLevel();
        return Open(static_cast<resources::ResourceHandle&&>(texture), first, static_cast<u8>(object->GetMetadata().GetMipCount() - first), limits);
    }
    Result TextureMipAcquisition::BeginNextWindow(const io::AsyncPriority priority) noexcept
    {
        if (m_impl == nullptr || m_impl->state != TextureMipAcquisitionState::Idle)
            return Result::InvalidState;
        if (m_impl->cursor == m_impl->requiredCount)
        {
            m_impl->state = TextureMipAcquisitionState::Complete;
            return Result::Success;
        }
        const u64 sourceBudget = m_impl->object->GetSubresourceSource().GetStagingBudgetBytes();
        const u64 windowBudget = sourceBudget != 0 && sourceBudget < m_impl->limits.maximumAdmissionBytes ? sourceBudget : m_impl->limits.maximumAdmissionBytes;
        u64 admitted = 0;
        m_impl->windowEnd = m_impl->cursor;
        while (m_impl->windowEnd < m_impl->requiredCount && m_impl->windowEnd - m_impl->cursor < m_impl->limits.maximumSubresources)
        {
            streaming::ResourceReadPlan plan;
            u64 cost = 0;
            const u32 subresource = m_impl->firstSubresource + m_impl->windowEnd;
            const Result planned = m_impl->object->GetSubresourceSource().PlanSubresource(m_impl->object->GetMetadata(), subresource, plan, cost);
            if (planned != Result::Success || cost > windowBudget)
            {
                m_impl->state = TextureMipAcquisitionState::Failed;
                m_impl->result = planned != Result::Success ? planned : Result::LimitExceeded;
                return m_impl->result;
            }
            if (admitted > windowBudget - cost)
                break;
            admitted += cost;
            ++m_impl->windowEnd;
        }
        if (m_impl->windowEnd == m_impl->cursor)
            return Result::LimitExceeded;
        m_impl->requests.Reserve(m_impl->windowEnd - m_impl->cursor);
        for (u32 index = m_impl->cursor; index < m_impl->windowEnd; ++index)
        {
            TextureSubresourceReadRequest request;
            const u32 subresource = m_impl->firstSubresource + index;
            const Result issued = m_impl->object->GetSubresourceSource().ReadSubresourceAsync(m_impl->object->GetMetadata(), subresource, request, priority);
            if (issued != Result::Success)
            {
                m_impl->requests.Clear();
                m_impl->state = TextureMipAcquisitionState::Failed;
                m_impl->result = issued;
                return issued;
            }
            m_impl->requests.PushBack(static_cast<TextureSubresourceReadRequest&&>(request));
        }
        m_impl->state = TextureMipAcquisitionState::Reading;
        return Result::Success;
    }
    TextureMipAcquisitionState TextureMipAcquisition::Poll() noexcept
    {
        if (m_impl == nullptr || m_impl->state != TextureMipAcquisitionState::Reading)
            return m_impl != nullptr ? m_impl->state : TextureMipAcquisitionState::Invalid;
        for (const TextureSubresourceReadRequest& request : m_impl->requests)
        {
            if (!request.HasFinished())
                return m_impl->state;
        }
        m_impl->views.Clear();
        m_impl->views.Reserve(m_impl->requests.Size());
        for (u32 index = 0; index < m_impl->requests.Size(); ++index)
        {
            const Result result = m_impl->requests[index].GetResult();
            if (result != Result::Success)
            {
                m_impl->result = result;
                m_impl->state = result == Result::Cancelled ? TextureMipAcquisitionState::Cancelled : TextureMipAcquisitionState::Failed;
                m_impl->requests.Clear();
                return m_impl->state;
            }
            const u32 subresource = m_impl->firstSubresource + m_impl->cursor + index;
            m_impl->views.PushBack({subresource, &m_impl->object->GetMetadata().GetSubresources()[subresource], m_impl->requests[index].GetBytes()});
        }
        m_impl->state = TextureMipAcquisitionState::Ready;
        m_impl->result = Result::Success;
        return m_impl->state;
    }
    void TextureMipAcquisition::Wait() noexcept
    {
        if (m_impl == nullptr || m_impl->state != TextureMipAcquisitionState::Reading)
            return;
        for (const TextureSubresourceReadRequest& request : m_impl->requests)
            request.Wait();
        static_cast<void>(Poll());
    }
    TextureMipAcquisitionState TextureMipAcquisition::GetState() const noexcept
    {
        return m_impl != nullptr ? m_impl->state : TextureMipAcquisitionState::Invalid;
    }
    Result TextureMipAcquisition::GetResult() const noexcept
    {
        return m_impl != nullptr ? m_impl->result : Result::InvalidState;
    }
    containers::ArraySpan<const TextureSubresourceView> TextureMipAcquisition::GetWindow() const noexcept
    {
        return m_impl != nullptr && m_impl->state == TextureMipAcquisitionState::Ready ? containers::ArraySpan<const TextureSubresourceView>{m_impl->views.TypedData(), m_impl->views.Size()}
                                                                                       : containers::ArraySpan<const TextureSubresourceView>();
    }
    Result TextureMipAcquisition::ReleaseWindow() noexcept
    {
        if (m_impl == nullptr || m_impl->state != TextureMipAcquisitionState::Ready)
            return Result::InvalidState;
        m_impl->views.Clear();
        m_impl->requests.Clear();
        m_impl->cursor = m_impl->windowEnd;
        m_impl->state = m_impl->cursor == m_impl->requiredCount ? TextureMipAcquisitionState::Complete : TextureMipAcquisitionState::Idle;
        return Result::Success;
    }
    bool TextureMipAcquisition::Cancel() noexcept
    {
        if (m_impl == nullptr ||
            (m_impl->state != TextureMipAcquisitionState::Idle && m_impl->state != TextureMipAcquisitionState::Reading && m_impl->state != TextureMipAcquisitionState::Ready))
            return false;
        const bool cancelled = true;
        for (TextureSubresourceReadRequest& request : m_impl->requests)
            static_cast<void>(request.Cancel());
        m_impl->views.Clear();
        m_impl->requests.Clear();
        m_impl->state = TextureMipAcquisitionState::Cancelled;
        m_impl->result = Result::Cancelled;
        return cancelled;
    }
    void TextureMipAcquisition::Reset() noexcept
    {
        Impl* const impl = m_impl;
        m_impl = nullptr;
        DeleteTextureObject(impl, memory::PoolId::Streaming);
    }
} // namespace vanguard::textures
