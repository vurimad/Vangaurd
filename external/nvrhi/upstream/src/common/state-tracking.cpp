/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include "state-tracking.h"
#include "../../../../../source/profiling/include/vanguard/profiling/profiling.hpp"

#include <nvrhi/utils.h>

#include <cstdint>
#include <sstream>

namespace nvrhi
{
    bool verifyPermanentResourceState(ResourceStates permanentState, ResourceStates requiredState, bool isTexture, const std::string& debugName, IMessageCallback* messageCallback)
    {
        if ((permanentState & requiredState) != requiredState)
        {
            std::stringstream ss;
            ss << "Permanent " << (isTexture ? "texture " : "buffer ") << utils::DebugNameToString(debugName)
                << " doesn't have the right state bits. Required: 0x" << std::hex << uint32_t(requiredState)
                << ", present: 0x" << std::hex << uint32_t(permanentState);
            messageCallback->message(MessageSeverity::Error, ss.str().c_str());
            return false;
        }

        return true;
    }

    static uint32_t calcSubresource(MipLevel mipLevel, ArraySlice arraySlice, const TextureDesc& desc)
    {
        return mipLevel + arraySlice * desc.mipLevels;
    }

    static size_t hashStateExtensionPointer(const void* pointer)
    {
        size_t hash = static_cast<size_t>(reinterpret_cast<std::uintptr_t>(pointer) >> 4u);
        hash ^= hash >> 16u;
        hash *= size_t(0x9e3779b1u);
        hash ^= hash >> 13u;
        return hash;
    }

    void CommandListResourceStateTracker::resetTextureState(TextureState& tracking)
    {
        if (tracking.subresourceStates.capacity() > RetainedSubresourceStateCapacity)
            std::vector<ResourceStates>().swap(tracking.subresourceStates);
        else
            tracking.subresourceStates.clear();
        tracking.state = ResourceStates::Unknown;
        tracking.enableUavBarriers = true;
        tracking.firstUavBarrierPlaced = false;
        tracking.permanentTransition = false;
        tracking.oversizedRetention = false;
    }

    void CommandListResourceStateTracker::resetBufferState(BufferState& tracking)
    {
        tracking.state = ResourceStates::Unknown;
        tracking.enableUavBarriers = true;
        tracking.firstUavBarrierPlaced = false;
        tracking.permanentTransition = false;
    }

    void CommandListResourceStateTracker::advanceRecordingEpoch()
    {
        ++m_RecordingEpoch;
        if (m_RecordingEpoch != 0)
            return;

        // A wrapped epoch is practically unreachable, but stale slots must never
        // alias a future recording if it does happen.
        for (RetainedTextureStateSlot& slot : m_RetainedTextureStates)
            slot.epoch = 0;
        for (RetainedBufferStateSlot& slot : m_RetainedBufferStates)
            slot.epoch = 0;
        m_RecordingEpoch = 1;
    }

    void CommandListResourceStateTracker::releaseOversizedTextureStateStorage()
    {
        for (TextureState* tracking : m_OversizedTextureStates)
        {
            if (tracking == nullptr || !tracking->oversizedRetention)
                continue;
            std::vector<ResourceStates>().swap(tracking->subresourceStates);
            tracking->oversizedRetention = false;
        }
        m_OversizedTextureStates.clear();
        if (m_OversizedTextureStates.capacity() > RetainedStateSlotCapacity)
            std::vector<TextureState*>().swap(m_OversizedTextureStates);
    }

    void CommandListResourceStateTracker::trimTransientOverflowStorage()
    {
        constexpr size_t retainedBucketLimit = RetainedStateSlotCapacity * 2u;
        if (m_OverflowTextureStates.bucket_count() > retainedBucketLimit)
            decltype(m_OverflowTextureStates)().swap(m_OverflowTextureStates);
        if (m_OverflowBufferStates.bucket_count() > retainedBucketLimit)
            decltype(m_OverflowBufferStates)().swap(m_OverflowBufferStates);
        if (m_InitialTextureStates.capacity() > RetainedStateSlotCapacity)
            std::vector<TextureStateExtension*>().swap(m_InitialTextureStates);
        if (m_PermanentTextureStates.capacity() > RetainedStateSlotCapacity)
            decltype(m_PermanentTextureStates)().swap(m_PermanentTextureStates);
        if (m_PermanentBufferStates.capacity() > RetainedStateSlotCapacity)
            decltype(m_PermanentBufferStates)().swap(m_PermanentBufferStates);
    }

    void CommandListResourceStateTracker::noteTextureStateStorage(TextureState* tracking)
    {
        if (tracking == nullptr || tracking->oversizedRetention ||
            tracking->subresourceStates.capacity() <= RetainedSubresourceStateCapacity)
            return;
        tracking->oversizedRetention = true;
        m_OversizedTextureStates.push_back(tracking);
    }

    void CommandListResourceStateTracker::abandonRecordingState()
    {
        releaseOversizedTextureStateStorage();
        m_OverflowTextureStates.clear();
        m_OverflowBufferStates.clear();
        m_ActiveTextureStateSlots.clear();
        m_ActiveBufferStateSlots.clear();
        m_PermanentTextureStates.clear();
        m_PermanentBufferStates.clear();
        m_InitialTextureStates.clear();
        m_TextureBarriers.clear();
        m_BufferBarriers.clear();
        trimTransientOverflowStorage();
        advanceRecordingEpoch();
    }

    void CommandListResourceStateTracker::beginRecording()
    {
        abandonRecordingState();
    }

    void CommandListResourceStateTracker::setEnableUavBarriersForTexture(TextureStateExtension* texture, bool enableBarriers)
    {
        TextureState* tracking = getTextureStateTracking(texture, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandListResourceStateTracker::setEnableUavBarriersForBuffer(BufferStateExtension* buffer, bool enableBarriers)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandListResourceStateTracker::beginTrackingTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        const TextureDesc& desc = texture->descRef;

        TextureState* tracking = getTextureStateTracking(texture, true);
        
        subresources = subresources.resolve(desc, false);

        if (subresources.isEntireTexture(desc))
        {
            tracking->state = stateBits;
            tracking->subresourceStates.clear();
        }
        else
        {
            tracking->subresourceStates.resize(desc.mipLevels * desc.arraySize, tracking->state);
            noteTextureStateStorage(tracking);
            tracking->state = ResourceStates::Unknown;

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
                {
                    uint32_t subresource = calcSubresource(mipLevel, arraySlice, desc);
                    tracking->subresourceStates[subresource] = stateBits;
                }
            }
        }
    }

    void CommandListResourceStateTracker::beginTrackingBufferState(BufferStateExtension* buffer, ResourceStates stateBits)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        tracking->state = stateBits;
    }

    void CommandListResourceStateTracker::setPermanentTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates stateBits)
    {
        const TextureDesc& desc = texture->descRef;

        subresources = subresources.resolve(desc, false);

        bool permanent = true;
        if (!subresources.isEntireTexture(desc))
        {
            std::stringstream ss;
            ss << "Attempted to perform a permanent state transition on a subset of subresources of texture "
                << utils::DebugNameToString(desc.debugName);
            m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
            permanent = false;
        }

        requireTextureState(texture, subresources, stateBits);

        if (permanent)
        {
            m_PermanentTextureStates.push_back(std::make_pair(texture, stateBits));
            getTextureStateTracking(texture, true)->permanentTransition = true;
        }
    }

    void CommandListResourceStateTracker::setPermanentBufferState(BufferStateExtension* buffer, ResourceStates stateBits)
    {
        requireBufferState(buffer, stateBits);

        m_PermanentBufferStates.push_back(std::make_pair(buffer, stateBits));
    }

    ResourceStates CommandListResourceStateTracker::getTextureSubresourceState(TextureStateExtension* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        TextureState* tracking = getTextureStateTracking(texture, false);
        if (!tracking)
        {
            return texture->descRef.keepInitialState ? 
                (texture->stateInitialized ? texture->descRef.initialState : ResourceStates::Common) :
                ResourceStates::Unknown;
        }

        // whole resource
        if (tracking->subresourceStates.empty())
            return tracking->state;

        uint32_t subresource = calcSubresource(mipLevel, arraySlice, texture->descRef);
        return tracking->subresourceStates[subresource];
    }

    ResourceStates CommandListResourceStateTracker::getBufferState(BufferStateExtension* buffer)
    {
        BufferState* tracking = getBufferStateTracking(buffer, false);

        if (!tracking)
            return ResourceStates::Unknown;

        return tracking->state;
    }
    
    void CommandListResourceStateTracker::requireTextureState(TextureStateExtension* texture, TextureSubresourceSet subresources, ResourceStates state)
    {
        if (texture->permanentState != 0)
        {
            verifyPermanentResourceState(texture->permanentState, state, true, texture->descRef.debugName, m_MessageCallback);
            return;
        }

        subresources = subresources.resolve(texture->descRef, false);

        TextureState* tracking = getTextureStateTracking(texture, true);

        if (tracking->subresourceStates.empty() && tracking->state == ResourceStates::Unknown)
        {
            std::stringstream ss;
            ss << "Unknown prior state of texture " << utils::DebugNameToString(texture->descRef.debugName) << ". "
                "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                "keepInitialState and initialState members of TextureDesc.";
            m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
        }
        
        if (subresources.isEntireTexture(texture->descRef) && tracking->subresourceStates.empty())
        {
            // We're requiring state for the entire texture, and it's been tracked as entire texture too

            bool transitionNecessary = tracking->state != state;
            bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
                && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

            if (transitionNecessary || uavNecessary)
            {
                TextureBarrier barrier;
                barrier.texture = texture;
                barrier.entireTexture = true;
                barrier.stateBefore = tracking->state;
                barrier.stateAfter = state;
                m_TextureBarriers.push_back(barrier);
            }

            tracking->state = state;

            if (uavNecessary && !transitionNecessary)
            {
                tracking->firstUavBarrierPlaced = true;
            }
        }
        else
        {
            // Transition individual subresources

            // Make sure that we're tracking the texture on subresource level
            bool stateExpanded = false;
            if (tracking->subresourceStates.empty())
            {
                tracking->subresourceStates.resize(texture->descRef.mipLevels * texture->descRef.arraySize, tracking->state);
                noteTextureStateStorage(tracking);
                tracking->state = ResourceStates::Unknown;
                stateExpanded = true;
            }
            
            bool anyUavBarrier = false;

            for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
            {
                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    uint32_t subresourceIndex = calcSubresource(mipLevel, arraySlice, texture->descRef);

                    auto priorState = tracking->subresourceStates[subresourceIndex];

                    if (priorState == ResourceStates::Unknown && !stateExpanded)
                    {
                        std::stringstream ss;
                        ss << "Unknown prior state of texture " << utils::DebugNameToString(texture->descRef.debugName)
                            << " subresource (MipLevel = " << mipLevel << ", ArraySlice = " << arraySlice << "). "
                            "Call CommandList::beginTrackingTextureState(...) before using the texture or use the "
                            "keepInitialState and initialState members of TextureDesc.";
                        m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                    }
                    
                    bool transitionNecessary = priorState != state;
                    bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
                        && !anyUavBarrier && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

                    if (transitionNecessary || uavNecessary)
                    {
                        TextureBarrier barrier;
                        barrier.texture = texture;
                        barrier.entireTexture = false;
                        barrier.mipLevel = mipLevel;
                        barrier.arraySlice = arraySlice;
                        barrier.stateBefore = priorState;
                        barrier.stateAfter = state;
                        m_TextureBarriers.push_back(barrier);
                    }

                    tracking->subresourceStates[subresourceIndex] = state;

                    if (uavNecessary && !transitionNecessary)
                    {
                        anyUavBarrier = true;
                        tracking->firstUavBarrierPlaced = true;
                    }
                }
            }
        }
    }

    void CommandListResourceStateTracker::requireBufferState(BufferStateExtension* buffer, ResourceStates state)
    {
        if (buffer->descRef.isVolatile)
            return;

        if (buffer->permanentState != 0)
        {
            verifyPermanentResourceState(buffer->permanentState, state, false, buffer->descRef.debugName, m_MessageCallback);

            return;
        }

        if (buffer->descRef.cpuAccess != CpuAccessMode::None)
        {
            // CPU-visible buffers can't change state
            return;
        }

        BufferState* tracking = getBufferStateTracking(buffer, true);

        if (tracking->state == ResourceStates::Unknown)
        {
            std::stringstream ss;
            ss << "Unknown prior state of buffer " << utils::DebugNameToString(buffer->descRef.debugName) << ". "
                "Call CommandList::beginTrackingBufferState(...) before using the buffer or use the "
                "keepInitialState and initialState members of BufferDesc.";
            m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
        }

        bool transitionNecessary = tracking->state != state;
        bool uavNecessary = ((state & ResourceStates::UnorderedAccess) != 0)
            && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced);

        if (transitionNecessary)
        {
            // See if this buffer is already used for a different purpose in this batch.
            // If it is, combine the state bits.
            // Example: same buffer used as index and vertex buffer, or as SRV and indirect arguments.
            for (BufferBarrier& barrier : m_BufferBarriers)
            {
                if (barrier.buffer == buffer)
                {
                    barrier.stateAfter = ResourceStates(barrier.stateAfter | state);
                    tracking->state = barrier.stateAfter;
                    return;
                }
            }
        }

        if (transitionNecessary || uavNecessary)
        {
            BufferBarrier barrier;
            barrier.buffer = buffer;
            barrier.stateBefore = tracking->state;
            barrier.stateAfter = state;
            m_BufferBarriers.push_back(barrier);
        }

        if (uavNecessary && !transitionNecessary)
        {
            tracking->firstUavBarrierPlaced = true;
        }
    
        tracking->state = state;
    }

    void CommandListResourceStateTracker::keepBufferInitialStates()
    {
        for (uint32_t slotIndex : m_ActiveBufferStateSlots)
        {
            RetainedBufferStateSlot& slot = m_RetainedBufferStates[slotIndex];
            if (slot.epoch != m_RecordingEpoch)
                continue;
            BufferStateExtension* const buffer = slot.buffer;
            BufferState* const tracking = &slot.tracking;
            if (buffer->descRef.keepInitialState && 
                !buffer->permanentState &&
                !buffer->descRef.isVolatile &&
                !tracking->permanentTransition)
            {
                requireBufferState(buffer, buffer->descRef.initialState);
            }
        }
        for (auto& [buffer, tracking] : m_OverflowBufferStates)
        {
            if (buffer->descRef.keepInitialState &&
                !buffer->permanentState &&
                !buffer->descRef.isVolatile &&
                !tracking->permanentTransition)
            {
                requireBufferState(buffer, buffer->descRef.initialState);
            }
        }
    }

    void CommandListResourceStateTracker::keepTextureInitialStates()
    {
        for (uint32_t slotIndex : m_ActiveTextureStateSlots)
        {
            RetainedTextureStateSlot& slot = m_RetainedTextureStates[slotIndex];
            if (slot.epoch != m_RecordingEpoch)
                continue;
            TextureStateExtension* const texture = slot.texture;
            TextureState* const tracking = &slot.tracking;
            if (texture->descRef.keepInitialState && 
                !texture->permanentState && 
                !tracking->permanentTransition)
            {
                requireTextureState(texture, AllSubresources, texture->descRef.initialState);
            }
        }
        for (auto& [texture, tracking] : m_OverflowTextureStates)
        {
            if (texture->descRef.keepInitialState &&
                !texture->permanentState &&
                !tracking->permanentTransition)
            {
                requireTextureState(texture, AllSubresources, texture->descRef.initialState);
            }
        }
    }

    void CommandListResourceStateTracker::commandListSubmitted()
    {
        VG_PROFILE_ZONE_NAMED("RFCPU/NVRHI/TrackerSubmit");
        {
        VG_PROFILE_ZONE_NAMED("RFCPU/NVRHI/PermanentPublication");
        for (auto [texture, state] : m_PermanentTextureStates)
        {
            if (texture->permanentState != 0 && texture->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of texture " << utils::DebugNameToString(texture->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(texture->permanentState) << " to 0x" << std::hex << uint32_t(state);
                m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                continue;
            }

            texture->permanentState = state;
        }
        m_PermanentTextureStates.clear();

        for (auto [buffer, state] : m_PermanentBufferStates)
        {
            if (buffer->permanentState != 0 && buffer->permanentState != state)
            {
                std::stringstream ss;
                ss << "Attempted to switch permanent state of buffer " << utils::DebugNameToString(buffer->descRef.debugName)
                    << " from 0x" << std::hex << uint32_t(buffer->permanentState) << " to 0x" << std::hex << uint32_t(state);
                m_MessageCallback->message(MessageSeverity::Error, ss.str().c_str());
                continue;
            }

            buffer->permanentState = state;
        }
        m_PermanentBufferStates.clear();
        }

        {
        VG_PROFILE_ZONE_NAMED("RFCPU/NVRHI/InitialStatePublication");
        for (TextureStateExtension* texture : m_InitialTextureStates)
        {
            if (texture->descRef.keepInitialState && !texture->stateInitialized)
                texture->stateInitialized = true;
        }
        m_InitialTextureStates.clear();
        }
        {
            VG_PROFILE_ZONE_NAMED("RFCPU/NVRHI/TrackerMapDestruction");
            releaseOversizedTextureStateStorage();
            m_OverflowTextureStates.clear();
            m_OverflowBufferStates.clear();
            m_ActiveTextureStateSlots.clear();
            m_ActiveBufferStateSlots.clear();
            m_TextureBarriers.clear();
            m_BufferBarriers.clear();
            trimTransientOverflowStorage();
            advanceRecordingEpoch();
        }
    }

    TextureState* CommandListResourceStateTracker::getTextureStateTracking(TextureStateExtension* texture, bool allowCreate)
    {
        if (!m_RetainedTextureStates.empty())
        {
            const size_t first = hashStateExtensionPointer(texture) & (RetainedStateSlotCapacity - 1u);
            for (uint32_t probe = 0; probe < RetainedStateSlotCapacity; ++probe)
            {
                const uint32_t slotIndex = uint32_t((first + probe) & (RetainedStateSlotCapacity - 1u));
                RetainedTextureStateSlot& slot = m_RetainedTextureStates[slotIndex];
                if (slot.epoch != m_RecordingEpoch)
                {
                    if (!allowCreate)
                        break;

                    slot.texture = texture;
                    resetTextureState(slot.tracking);
                    slot.epoch = m_RecordingEpoch;
                    m_ActiveTextureStateSlots.push_back(slotIndex);
                    if (texture->descRef.keepInitialState)
                    {
                        slot.tracking.state = texture->stateInitialized ? texture->descRef.initialState : ResourceStates::Common;
                        if (!texture->stateInitialized)
                            m_InitialTextureStates.push_back(texture);
                    }
                    return &slot.tracking;
                }

                // Never compare a pointer carried by a stale epoch: a resource may
                // have been destroyed and a later resource may reuse its address.
                if (slot.texture == texture)
                    return &slot.tracking;
            }
        }

        auto overflow = m_OverflowTextureStates.find(texture);
        if (overflow != m_OverflowTextureStates.end())
            return overflow->second.get();

        if (!allowCreate)
            return nullptr;

        if (m_RetainedTextureStates.empty())
        {
            m_RetainedTextureStates.resize(RetainedStateSlotCapacity);
            m_ActiveTextureStateSlots.reserve(RetainedStateSlotCapacity);
            m_InitialTextureStates.reserve(RetainedStateSlotCapacity);
            return getTextureStateTracking(texture, true);
        }

        std::unique_ptr<TextureState> trackingRef = std::make_unique<TextureState>();
        TextureState* const tracking = trackingRef.get();
        if (texture->descRef.keepInitialState)
        {
            tracking->state = texture->stateInitialized ? texture->descRef.initialState : ResourceStates::Common;
            if (!texture->stateInitialized)
                m_InitialTextureStates.push_back(texture);
        }
        m_OverflowTextureStates.insert(std::make_pair(texture, std::move(trackingRef)));
        return tracking;
    }

    BufferState* CommandListResourceStateTracker::getBufferStateTracking(BufferStateExtension* buffer, bool allowCreate)
    {
        if (!m_RetainedBufferStates.empty())
        {
            const size_t first = hashStateExtensionPointer(buffer) & (RetainedStateSlotCapacity - 1u);
            for (uint32_t probe = 0; probe < RetainedStateSlotCapacity; ++probe)
            {
                const uint32_t slotIndex = uint32_t((first + probe) & (RetainedStateSlotCapacity - 1u));
                RetainedBufferStateSlot& slot = m_RetainedBufferStates[slotIndex];
                if (slot.epoch != m_RecordingEpoch)
                {
                    if (!allowCreate)
                        break;

                    slot.buffer = buffer;
                    resetBufferState(slot.tracking);
                    slot.epoch = m_RecordingEpoch;
                    m_ActiveBufferStateSlots.push_back(slotIndex);
                    if (buffer->descRef.keepInitialState)
                        slot.tracking.state = buffer->descRef.initialState;
                    return &slot.tracking;
                }

                if (slot.buffer == buffer)
                    return &slot.tracking;
            }
        }

        auto overflow = m_OverflowBufferStates.find(buffer);
        if (overflow != m_OverflowBufferStates.end())
            return overflow->second.get();

        if (!allowCreate)
            return nullptr;

        if (m_RetainedBufferStates.empty())
        {
            m_RetainedBufferStates.resize(RetainedStateSlotCapacity);
            m_ActiveBufferStateSlots.reserve(RetainedStateSlotCapacity);
            return getBufferStateTracking(buffer, true);
        }

        std::unique_ptr<BufferState> trackingRef = std::make_unique<BufferState>();
        BufferState* const tracking = trackingRef.get();
        if (buffer->descRef.keepInitialState)
        {
            tracking->state = buffer->descRef.initialState;
        }
        m_OverflowBufferStates.insert(std::make_pair(buffer, std::move(trackingRef)));
        return tracking;
    }

    ResourceStates getShaderResourceStateForBindingLayout(IBindingLayout* bindingLayout)
    {
        ShaderType const visibility = bindingLayout->getDesc()->visibility;
        
        bool const hasPixelShader = (visibility & ShaderType::Pixel) != 0;

        return hasPixelShader
            ? ResourceStates::ShaderResource
            : ResourceStates::NonPixelShaderResource;
    }

} // namespace nvrhi
