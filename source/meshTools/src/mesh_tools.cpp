#include <vanguard/mesh_tools/mesh_tools.hpp>

#include <vanguard/memory/memory.hpp>
#include <meshoptimizer.h>

#include <cmath>

namespace
{
    namespace containers = vanguard::containers;
    namespace memory = vanguard::memory;
    namespace mesh = vanguard::meshes;
    namespace tools = vanguard::mesh_tools;
    using vanguard::f32;
    using vanguard::i16;
    using vanguard::i8;
    using vanguard::u16;
    using vanguard::u32;
    using vanguard::u64;
    using vanguard::u8;
    using vanguard::usize;

    bool g_initialized = false;
    bool g_profileRegistrySealed = false;
    constexpr u32 MaximumRegisteredProfiles = 32;
    constexpr u32 MaximumRulesPerProfile = 32;

    struct RegisteredProfile
    {
        tools::MeshCookingProfile profile;
        tools::VertexPackingRule rules[MaximumRulesPerProfile]{};
    };

    RegisteredProfile g_profiles[MaximumRegisteredProfiles]{};
    u32 g_profileCount = 0;

    bool IsValidVertexSemantic(const mesh::VertexSemantic semantic)
    {
        return semantic <= mesh::VertexSemantic::Custom;
    }

    bool IsValidVertexFormat(const mesh::VertexFormat format)
    {
        return format <= mesh::VertexFormat::R10G10B10A2UNorm && mesh::GetVertexFormatByteSize(format) != 0;
    }

    bool RulesOverlap(const tools::VertexPackingRule& left, const tools::VertexPackingRule& right)
    {
        if (left.semantic != right.semantic || left.sourceFormat != right.sourceFormat)
        {
            return false;
        }
        return left.semanticIndex == right.semanticIndex || tools::HasFlag(left.flags, tools::VertexPackingRuleFlags::MatchAnySemanticIndex) ||
               tools::HasFlag(right.flags, tools::VertexPackingRuleFlags::MatchAnySemanticIndex);
    }

    const tools::MeshCookingProfile* FindProfileInternal(tools::MeshCookingProfileId id);

    tools::ProfileRegistrationResult RegisterProfileInternal(const tools::MeshCookingProfile& profile)
    {
        if (g_profileRegistrySealed)
        {
            return tools::ProfileRegistrationResult::RegistrySealed;
        }
        if (profile.id == 0 || profile.version == 0 || profile.id == profile.baseProfile || profile.meshKind > mesh::MeshKind::Skinned ||
            profile.unmatchedStreams > tools::UnmatchedVertexStreamPolicy::PreserveInDedicatedBinding ||
            (static_cast<u8>(profile.flags) & ~static_cast<u8>(tools::MeshCookingProfileFlags::QuantizePositions)) != 0)
        {
            return tools::ProfileRegistrationResult::InvalidArgument;
        }
        for (u32 registeredIndex = 0; registeredIndex < g_profileCount; ++registeredIndex)
        {
            if (g_profiles[registeredIndex].profile.id == profile.id)
            {
                return tools::ProfileRegistrationResult::DuplicateIdentifier;
            }
        }
        if (g_profileCount == MaximumRegisteredProfiles)
        {
            return tools::ProfileRegistrationResult::CapacityExceeded;
        }
        const tools::MeshCookingProfile* baseProfile = nullptr;
        if (profile.baseProfile != 0)
        {
            baseProfile = FindProfileInternal(profile.baseProfile);
            if (baseProfile == nullptr)
            {
                return tools::ProfileRegistrationResult::InvalidArgument;
            }
        }
        const u32 baseRuleCount = baseProfile != nullptr ? baseProfile->rules.Size() : 0;
        if (profile.rules.Size() > MaximumRulesPerProfile - baseRuleCount)
        {
            return tools::ProfileRegistrationResult::CapacityExceeded;
        }
        for (u32 ruleIndex = 0; ruleIndex < profile.rules.Size(); ++ruleIndex)
        {
            const tools::VertexPackingRule& rule = profile.rules[ruleIndex];
            if (!IsValidVertexSemantic(rule.semantic) || !IsValidVertexFormat(rule.sourceFormat) || !IsValidVertexFormat(rule.storedFormat) ||
                rule.bindingGroup >= MaximumRulesPerProfile ||
                (static_cast<u8>(rule.flags) &
                 ~(static_cast<u8>(tools::VertexPackingRuleFlags::Required) | static_cast<u8>(tools::VertexPackingRuleFlags::MatchAnySemanticIndex))) != 0 ||
                (rule.pack == nullptr && rule.sourceFormat != rule.storedFormat))
            {
                return tools::ProfileRegistrationResult::InvalidArgument;
            }
            for (u32 previousRuleIndex = 0; previousRuleIndex < ruleIndex; ++previousRuleIndex)
            {
                if (RulesOverlap(rule, profile.rules[previousRuleIndex]))
                {
                    return tools::ProfileRegistrationResult::InvalidArgument;
                }
            }
            if (baseProfile != nullptr)
            {
                for (const tools::VertexPackingRule& baseRule : baseProfile->rules)
                {
                    if (RulesOverlap(rule, baseRule))
                    {
                        return tools::ProfileRegistrationResult::InvalidArgument;
                    }
                }
            }
        }

        RegisteredProfile& registered = g_profiles[g_profileCount++];
        registered.profile = profile;
        for (u32 ruleIndex = 0; ruleIndex < baseRuleCount; ++ruleIndex)
        {
            registered.rules[ruleIndex] = baseProfile->rules[ruleIndex];
        }
        for (u32 ruleIndex = 0; ruleIndex < profile.rules.Size(); ++ruleIndex)
        {
            registered.rules[baseRuleCount + ruleIndex] = profile.rules[ruleIndex];
        }
        registered.profile.rules = {registered.rules, baseRuleCount + profile.rules.Size()};
        return tools::ProfileRegistrationResult::Success;
    }

    const tools::MeshCookingProfile* FindProfileInternal(const tools::MeshCookingProfileId id)
    {
        for (u32 profileIndex = 0; profileIndex < g_profileCount; ++profileIndex)
        {
            if (g_profiles[profileIndex].profile.id == id)
            {
                return &g_profiles[profileIndex].profile;
            }
        }
        return nullptr;
    }

    struct MeshoptimizerAllocation
    {
        memory::MemoryBlock block;
    };

    void* MESHOPTIMIZER_ALLOC_CALLCONV MeshoptimizerAllocate(const size_t requestedSize)
    {
        constexpr usize Alignment = 16;
        const usize size = static_cast<usize>(requestedSize);
        if (size > static_cast<usize>(0xffffffffu) - sizeof(MeshoptimizerAllocation) - Alignment)
        {
            return nullptr;
        }
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Assets, size + sizeof(MeshoptimizerAllocation) + Alignment - 1, Alignment);
        if (!block)
        {
            return nullptr;
        }
        const usize first = reinterpret_cast<usize>(block.address) + sizeof(MeshoptimizerAllocation);
        const usize aligned = (first + Alignment - 1) & ~(Alignment - 1);
        auto* allocation = reinterpret_cast<MeshoptimizerAllocation*>(aligned - sizeof(MeshoptimizerAllocation));
        allocation->block = block;
        return reinterpret_cast<void*>(aligned);
    }

    void MESHOPTIMIZER_ALLOC_CALLCONV MeshoptimizerFree(void* const address)
    {
        if (address == nullptr)
        {
            return;
        }
        auto* allocation = reinterpret_cast<MeshoptimizerAllocation*>(reinterpret_cast<usize>(address) - sizeof(MeshoptimizerAllocation));
        memory::MemoryBlock block = allocation->block;
        memory::Free(block);
    }

    template <typename T> containers::ArraySpan<const T> Span(const containers::DynamicArray<T>& values)
    {
        return {values.TypedData(), values.Size()};
    }

    struct Payload
    {
        Payload() noexcept : bytes(memory::pools::Assets::GetInstance()) {}

        u32 id = 0;
        mesh::BufferKind kind = mesh::BufferKind::Custom;
        u32 stride = 0;
        bool requiredForLowestLod = false;
        containers::DynamicArray<u8> bytes;
    };

    struct BuildStorage
    {
        BuildStorage() noexcept
            : payloads(memory::pools::Assets::GetInstance()), buffers(memory::pools::Assets::GetInstance()), pages(memory::pools::Assets::GetInstance()),
              layouts(memory::pools::Assets::GetInstance()), streams(memory::pools::Assets::GetInstance()), materials(memory::pools::Assets::GetInstance()),
              lods(memory::pools::Assets::GetInstance()), submeshes(memory::pools::Assets::GetInstance())
        {
        }

        containers::DynamicArray<Payload> payloads;
        containers::DynamicArray<mesh::BufferBuildRecord> buffers;
        containers::DynamicArray<mesh::PageBuildRecord> pages;
        containers::DynamicArray<mesh::VertexLayoutBuildRecord> layouts;
        containers::DynamicArray<mesh::VertexStreamBuildRecord> streams;
        containers::DynamicArray<mesh::MaterialSlotBuildRecord> materials;
        containers::DynamicArray<mesh::LodBuildRecord> lods;
        containers::DynamicArray<mesh::SubmeshBuildRecord> submeshes;
    };

    const tools::SourceVertexStream* FindPosition(const tools::SourceSubmesh& source)
    {
        for (const tools::SourceVertexStream& stream : source.vertexStreams)
        {
            if (stream.semantic == mesh::VertexSemantic::Position && stream.semanticIndex == 0)
            {
                return &stream;
            }
        }
        return nullptr;
    }

    bool CopyRemappedStream(Payload& payload, const tools::SourceVertexStream& source, const u32 sourceVertexCount, const u32 cookedVertexCount,
                            const u32* const remap)
    {
        const u32 elementSize = mesh::GetVertexFormatByteSize(source.format);
        if (elementSize == 0 || cookedVertexCount > 0xffffffffu / elementSize)
        {
            return false;
        }
        payload.stride = elementSize;
        payload.bytes.Resize(cookedVertexCount * elementSize);
        meshopt_remapVertexBuffer(payload.bytes.TypedData(), source.data, sourceVertexCount, elementSize, remap);
        return true;
    }

    u32 SimplificationComponentCount(const mesh::VertexFormat format)
    {
        switch (format)
        {
        case mesh::VertexFormat::R32Float:
            return 1;
        case mesh::VertexFormat::R32G32Float:
            return 2;
        case mesh::VertexFormat::R32G32B32Float:
            return 3;
        case mesh::VertexFormat::R32G32B32A32Float:
            return 4;
        case mesh::VertexFormat::R16G16Float:
        case mesh::VertexFormat::R16G16SNorm:
        case mesh::VertexFormat::R16G16UNorm:
            return 2;
        case mesh::VertexFormat::R16G16B16A16Float:
        case mesh::VertexFormat::R16G16B16A16SNorm:
        case mesh::VertexFormat::R16G16B16A16UNorm:
        case mesh::VertexFormat::R8G8B8A8UNorm:
        case mesh::VertexFormat::R8G8B8A8SNorm:
        case mesh::VertexFormat::R10G10B10A2UNorm:
            return 4;
        default:
            return 0;
        }
    }

    u32 SimplificationComponentCount(const tools::SourceVertexStream& stream)
    {
        if (stream.format == mesh::VertexFormat::R10G10B10A2UNorm && stream.semantic == mesh::VertexSemantic::Normal)
        {
            return 3;
        }
        return SimplificationComponentCount(stream.format);
    }

    f32 DecodeSNorm(const i16 value)
    {
        return value <= static_cast<i16>(-32767) ? -1.0f : static_cast<f32>(value) / 32767.0f;
    }

    f32 DecodeSNorm(const i8 value)
    {
        return value <= static_cast<i8>(-127) ? -1.0f : static_cast<f32>(value) / 127.0f;
    }

    bool DecodeSimplificationAttribute(const tools::SourceVertexStream& stream, const u8* const source, f32* const destination)
    {
        switch (stream.format)
        {
        case mesh::VertexFormat::R32Float:
        case mesh::VertexFormat::R32G32Float:
        case mesh::VertexFormat::R32G32B32Float:
        case mesh::VertexFormat::R32G32B32A32Float:
        {
            const u32 componentCount = SimplificationComponentCount(stream);
            const f32* const values = reinterpret_cast<const f32*>(source);
            for (u32 component = 0; component < componentCount; ++component)
            {
                destination[component] = values[component];
            }
            return true;
        }
        case mesh::VertexFormat::R16G16Float:
        case mesh::VertexFormat::R16G16B16A16Float:
        {
            const u32 componentCount = SimplificationComponentCount(stream);
            const u16* const values = reinterpret_cast<const u16*>(source);
            for (u32 component = 0; component < componentCount; ++component)
            {
                destination[component] = meshopt_dequantizeHalf(values[component]);
            }
            return true;
        }
        case mesh::VertexFormat::R16G16SNorm:
        case mesh::VertexFormat::R16G16B16A16SNorm:
        {
            const u32 componentCount = SimplificationComponentCount(stream);
            const i16* const values = reinterpret_cast<const i16*>(source);
            for (u32 component = 0; component < componentCount; ++component)
            {
                destination[component] = DecodeSNorm(values[component]);
            }
            return true;
        }
        case mesh::VertexFormat::R16G16UNorm:
        case mesh::VertexFormat::R16G16B16A16UNorm:
        {
            const u32 componentCount = SimplificationComponentCount(stream);
            const u16* const values = reinterpret_cast<const u16*>(source);
            for (u32 component = 0; component < componentCount; ++component)
            {
                destination[component] = static_cast<f32>(values[component]) / 65535.0f;
            }
            return true;
        }
        case mesh::VertexFormat::R8G8B8A8UNorm:
            for (u32 component = 0; component < 4; ++component)
            {
                destination[component] = static_cast<f32>(source[component]) / 255.0f;
            }
            return true;
        case mesh::VertexFormat::R8G8B8A8SNorm:
        {
            const i8* const values = reinterpret_cast<const i8*>(source);
            for (u32 component = 0; component < 4; ++component)
            {
                destination[component] = DecodeSNorm(values[component]);
            }
            return true;
        }
        case mesh::VertexFormat::R10G10B10A2UNorm:
        {
            const u32 value = *reinterpret_cast<const u32*>(source);
            const f32 x = static_cast<f32>(value & 0x3ffu) / 1023.0f;
            const f32 y = static_cast<f32>((value >> 10u) & 0x3ffu) / 1023.0f;
            const f32 z = static_cast<f32>((value >> 20u) & 0x3ffu) / 1023.0f;
            const f32 w = static_cast<f32>((value >> 30u) & 0x3u) / 3.0f;
            if (stream.semantic == mesh::VertexSemantic::Normal || stream.semantic == mesh::VertexSemantic::Tangent)
            {
                destination[0] = x * 2.0f - 1.0f;
                destination[1] = y * 2.0f - 1.0f;
                destination[2] = z * 2.0f - 1.0f;
                if (stream.semantic == mesh::VertexSemantic::Tangent)
                {
                    destination[3] = w * 2.0f - 1.0f;
                }
            }
            else
            {
                destination[0] = x;
                destination[1] = y;
                destination[2] = z;
                destination[3] = w;
            }
            return true;
        }
        default:
            return false;
        }
    }

    f32 AttributeWeight(const tools::LodAttributeWeights& weights, const mesh::VertexSemantic semantic)
    {
        switch (semantic)
        {
        case mesh::VertexSemantic::Normal:
            return weights.normal;
        case mesh::VertexSemantic::Tangent:
            return weights.tangent;
        case mesh::VertexSemantic::TexCoord:
            return weights.texCoord;
        case mesh::VertexSemantic::Color:
            return weights.color;
        case mesh::VertexSemantic::JointWeights:
            return weights.jointWeights;
        case mesh::VertexSemantic::MorphPosition:
            return weights.morphPosition;
        default:
            return 0.0f;
        }
    }

    enum class SimplificationAttributeBuildResult : u8
    {
        Success,
        UnsupportedFormat,
        ComponentLimitExceeded
    };

    SimplificationAttributeBuildResult BuildSimplificationAttributes(const tools::SourceSubmesh& source, const containers::DynamicArray<Payload>& payloads,
                                                                     const u32 vertexCount, const tools::LodAttributeWeights& configuredWeights,
                                                                     containers::DynamicArray<f32>& attributes, containers::DynamicArray<f32>& weights)
    {
        u32 componentCount = 0;
        for (u32 streamIndex = 0; streamIndex < source.vertexStreams.Size(); ++streamIndex)
        {
            const tools::SourceVertexStream& stream = source.vertexStreams[streamIndex];
            const f32 weight = AttributeWeight(configuredWeights, stream.semantic);
            if (weight > 0.0f)
            {
                const u32 components = SimplificationComponentCount(stream);
                if (components == 0)
                {
                    return SimplificationAttributeBuildResult::UnsupportedFormat;
                }
                if (componentCount > 32 - components)
                {
                    return SimplificationAttributeBuildResult::ComponentLimitExceeded;
                }
                componentCount += components;
                for (u32 component = 0; component < components; ++component)
                {
                    weights.PushBack(weight);
                }
            }
        }
        if (componentCount == 0)
        {
            return SimplificationAttributeBuildResult::Success;
        }

        attributes.Resize(vertexCount * componentCount);
        for (u32 vertex = 0; vertex < vertexCount; ++vertex)
        {
            u32 destinationComponent = 0;
            for (u32 streamIndex = 0; streamIndex < source.vertexStreams.Size(); ++streamIndex)
            {
                const tools::SourceVertexStream& stream = source.vertexStreams[streamIndex];
                if (AttributeWeight(configuredWeights, stream.semantic) <= 0.0f)
                {
                    continue;
                }
                const u32 components = SimplificationComponentCount(stream);
                const Payload& payload = payloads[streamIndex];
                f32* const destination = attributes.TypedData() + vertex * componentCount + destinationComponent;
                if (!DecodeSimplificationAttribute(stream, payload.bytes.TypedData() + static_cast<usize>(vertex) * payload.stride, destination))
                {
                    return SimplificationAttributeBuildResult::UnsupportedFormat;
                }
                for (u32 component = 0; component < components; ++component)
                {
                    if (!std::isfinite(destination[component]))
                    {
                        return SimplificationAttributeBuildResult::UnsupportedFormat;
                    }
                }
                destinationComponent += components;
            }
        }
        return SimplificationAttributeBuildResult::Success;
    }

    bool CompactLodStreams(const containers::DynamicArray<Payload>& basePayloads, containers::DynamicArray<u32>& indices, const u32 baseVertexCount,
                           const bool optimizeVertexFetch, containers::DynamicArray<Payload>& outputPayloads, u32& outputVertexCount)
    {
        containers::DynamicArray<u32> fetchRemap(memory::pools::Assets::GetInstance());
        fetchRemap.Resize(baseVertexCount);
        if (optimizeVertexFetch)
        {
            outputVertexCount =
                static_cast<u32>(meshopt_optimizeVertexFetchRemap(fetchRemap.TypedData(), indices.TypedData(), indices.Size(), baseVertexCount));
        }
        else
        {
            containers::DynamicArray<u8> referenced(memory::pools::Assets::GetInstance());
            referenced.Resize(baseVertexCount);
            for (const u32 index : indices)
            {
                referenced[index] = 1;
            }
            outputVertexCount = 0;
            for (u32 vertex = 0; vertex < baseVertexCount; ++vertex)
            {
                fetchRemap[vertex] = referenced[vertex] != 0 ? outputVertexCount++ : 0xffffffffu;
            }
        }
        if (outputVertexCount == 0)
        {
            return false;
        }
        meshopt_remapIndexBuffer(indices.TypedData(), indices.TypedData(), indices.Size(), fetchRemap.TypedData());
        outputPayloads.Resize(basePayloads.Size());
        for (u32 streamIndex = 0; streamIndex < basePayloads.Size(); ++streamIndex)
        {
            const Payload& source = basePayloads[streamIndex];
            Payload& output = outputPayloads[streamIndex];
            output.kind = mesh::BufferKind::Vertex;
            output.stride = source.stride;
            output.bytes.Resize(baseVertexCount * source.stride);
            meshopt_remapVertexBuffer(output.bytes.TypedData(), source.bytes.TypedData(), baseVertexCount, source.stride, fetchRemap.TypedData());
            output.bytes.Resize(outputVertexCount * source.stride);
        }
        return true;
    }

    void CopyBytes(containers::DynamicArray<u8>& destination, const void* const source, const usize size)
    {
        destination.Resize(static_cast<u32>(size));
        const u8* bytes = static_cast<const u8*>(source);
        for (u32 index = 0; index < destination.Size(); ++index)
        {
            destination[index] = bytes[index];
        }
    }

    mesh::Bounds CalculateBounds(const f32* const positions, const u32 vertexCount, const u32 stride)
    {
        mesh::Bounds bounds;
        if (vertexCount == 0)
        {
            return bounds;
        }
        const u8* bytes = reinterpret_cast<const u8*>(positions);
        const f32* first = reinterpret_cast<const f32*>(bytes);
        for (u32 axis = 0; axis < 3; ++axis)
        {
            bounds.minimum[axis] = first[axis];
            bounds.maximum[axis] = first[axis];
        }
        for (u32 vertex = 1; vertex < vertexCount; ++vertex)
        {
            const f32* position = reinterpret_cast<const f32*>(bytes + static_cast<usize>(vertex) * stride);
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (position[axis] < bounds.minimum[axis])
                {
                    bounds.minimum[axis] = position[axis];
                }
                if (position[axis] > bounds.maximum[axis])
                {
                    bounds.maximum[axis] = position[axis];
                }
            }
        }
        f32 radiusSquared = 0.0f;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            bounds.sphereCenter[axis] = (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5f;
        }
        for (u32 vertex = 0; vertex < vertexCount; ++vertex)
        {
            const f32* position = reinterpret_cast<const f32*>(bytes + static_cast<usize>(vertex) * stride);
            f32 distanceSquared = 0.0f;
            for (u32 axis = 0; axis < 3; ++axis)
            {
                const f32 distance = position[axis] - bounds.sphereCenter[axis];
                distanceSquared += distance * distance;
            }
            if (distanceSquared > radiusSquared)
            {
                radiusSquared = distanceSquared;
            }
        }
        bounds.sphereRadius = ::sqrtf(radiusSquared);
        return bounds;
    }

    struct PackedStreamPlan
    {
        u32 sourceIndex = 0;
        mesh::VertexFormat format = mesh::VertexFormat::R32Float;
        u32 elementSize = 0;
        u32 group = 0;
        u32 byteOffset = 0;
        tools::VertexPackFunction pack = nullptr;
    };

    bool IsCanonicalStreamBefore(const tools::SourceVertexStream& left, const tools::SourceVertexStream& right)
    {
        if (left.semantic != right.semantic)
        {
            return left.semantic < right.semantic;
        }
        return left.semanticIndex < right.semanticIndex;
    }

    bool IsPackedPlanBefore(const PackedStreamPlan& left, const PackedStreamPlan& right, const tools::SourceSubmesh& source)
    {
        if (left.group != right.group)
        {
            return left.group < right.group;
        }
        return IsCanonicalStreamBefore(source.vertexStreams[left.sourceIndex], source.vertexStreams[right.sourceIndex]);
    }

    bool DoesRuleMatch(const tools::VertexPackingRule& rule, const tools::SourceVertexStream& stream)
    {
        return rule.semantic == stream.semantic && rule.sourceFormat == stream.format &&
               (rule.semanticIndex == stream.semanticIndex || tools::HasFlag(rule.flags, tools::VertexPackingRuleFlags::MatchAnySemanticIndex));
    }

    bool ValidateSourceStreamsAgainstProfile(const tools::SourceSubmesh& source, const tools::MeshCookingProfile& profile)
    {
        for (const tools::SourceVertexStream& stream : source.vertexStreams)
        {
            bool matched = false;
            for (const tools::VertexPackingRule& rule : profile.rules)
            {
                matched |= DoesRuleMatch(rule, stream);
            }
            if (!matched && profile.unmatchedStreams == tools::UnmatchedVertexStreamPolicy::Reject)
            {
                return false;
            }
        }
        for (const tools::VertexPackingRule& rule : profile.rules)
        {
            if (!tools::HasFlag(rule.flags, tools::VertexPackingRuleFlags::Required))
            {
                continue;
            }
            bool found = false;
            for (const tools::SourceVertexStream& stream : source.vertexStreams)
            {
                found |= DoesRuleMatch(rule, stream);
            }
            if (!found)
            {
                return false;
            }
        }
        return true;
    }

    bool BuildPackedStreamPlans(const tools::SourceSubmesh& source, const tools::MeshCookingProfile& profile, containers::DynamicArray<PackedStreamPlan>& plans)
    {
        if (!ValidateSourceStreamsAgainstProfile(source, profile))
        {
            return false;
        }
        containers::DynamicArray<u32> order(memory::pools::Assets::GetInstance());
        order.Resize(source.vertexStreams.Size());
        for (u32 index = 0; index < order.Size(); ++index)
        {
            order[index] = index;
        }
        for (u32 index = 1; index < order.Size(); ++index)
        {
            const u32 value = order[index];
            u32 insertion = index;
            while (insertion > 0 && IsCanonicalStreamBefore(source.vertexStreams[value], source.vertexStreams[order[insertion - 1]]))
            {
                order[insertion] = order[insertion - 1];
                --insertion;
            }
            order[insertion] = value;
        }

        plans.Reserve(order.Size());
        u32 separateGroup = 0;
        for (const tools::VertexPackingRule& rule : profile.rules)
        {
            const u32 nextGroup = static_cast<u32>(rule.bindingGroup) + 1;
            if (nextGroup > separateGroup)
            {
                separateGroup = nextGroup;
            }
        }
        for (const u32 sourceIndex : order)
        {
            const tools::SourceVertexStream& stream = source.vertexStreams[sourceIndex];
            PackedStreamPlan plan;
            plan.sourceIndex = sourceIndex;
            plan.format = stream.format;
            const tools::VertexPackingRule* matchedRule = nullptr;
            for (const tools::VertexPackingRule& rule : profile.rules)
            {
                if (DoesRuleMatch(rule, stream))
                {
                    matchedRule = &rule;
                    break;
                }
            }
            if (matchedRule != nullptr)
            {
                plan.format = matchedRule->storedFormat;
                plan.group = matchedRule->bindingGroup;
                plan.pack = matchedRule->pack;
            }
            else
            {
                if (profile.unmatchedStreams == tools::UnmatchedVertexStreamPolicy::Reject)
                {
                    return false;
                }
                plan.group = separateGroup++;
            }
            plan.elementSize = mesh::GetVertexFormatByteSize(plan.format);
            if (plan.elementSize == 0)
            {
                return false;
            }
            plans.PushBack(plan);
        }
        for (u32 index = 1; index < plans.Size(); ++index)
        {
            const PackedStreamPlan value = plans[index];
            u32 insertion = index;
            while (insertion > 0 && IsPackedPlanBefore(value, plans[insertion - 1], source))
            {
                plans[insertion] = plans[insertion - 1];
                --insertion;
            }
            plans[insertion] = value;
        }
        return true;
    }

    bool PackRuntimeVertexElement(u8* const destination, const tools::SourceVertexStream& source, const u8* const sourceBytes,
                                  const mesh::PositionQuantization& quantization) noexcept
    {
        const f32* const values = reinterpret_cast<const f32*>(sourceBytes);
        if (source.semantic == mesh::VertexSemantic::Position && source.semanticIndex == 0 && source.format == mesh::VertexFormat::R32G32B32Float)
        {
            i16* const packed = reinterpret_cast<i16*>(destination);
            for (u32 axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(values[axis]))
                {
                    return false;
                }
                const f32 normalized = (values[axis] - quantization.bias[axis]) / quantization.scale[axis];
                packed[axis] = static_cast<i16>(meshopt_quantizeSnorm(normalized, 16));
            }
            packed[3] = static_cast<i16>(32767);
            return true;
        }
        if ((source.semantic == mesh::VertexSemantic::Normal && source.format == mesh::VertexFormat::R32G32B32Float) ||
            (source.semantic == mesh::VertexSemantic::Tangent &&
             (source.format == mesh::VertexFormat::R32G32B32Float || source.format == mesh::VertexFormat::R32G32B32A32Float)))
        {
            for (u32 component = 0; component < 3; ++component)
            {
                if (!std::isfinite(values[component]))
                {
                    return false;
                }
            }
            const u32 x = static_cast<u32>(meshopt_quantizeUnorm(values[0] * 0.5f + 0.5f, 10));
            const u32 y = static_cast<u32>(meshopt_quantizeUnorm(values[1] * 0.5f + 0.5f, 10));
            const u32 z = static_cast<u32>(meshopt_quantizeUnorm(values[2] * 0.5f + 0.5f, 10));
            u32 w = 3;
            if (source.semantic == mesh::VertexSemantic::Tangent && source.format == mesh::VertexFormat::R32G32B32A32Float)
            {
                if (!std::isfinite(values[3]))
                {
                    return false;
                }
                w = values[3] < 0.0f ? 0u : 3u;
            }
            *reinterpret_cast<u32*>(destination) = x | (y << 10u) | (z << 20u) | (w << 30u);
            return true;
        }
        if (source.semantic == mesh::VertexSemantic::TexCoord && source.format == mesh::VertexFormat::R32G32Float)
        {
            if (!std::isfinite(values[0]) || !std::isfinite(values[1]))
            {
                return false;
            }
            u16* const packed = reinterpret_cast<u16*>(destination);
            packed[0] = meshopt_quantizeHalf(values[0]);
            packed[1] = meshopt_quantizeHalf(values[1]);
            return true;
        }
        if ((source.semantic == mesh::VertexSemantic::Color || source.semantic == mesh::VertexSemantic::JointWeights) &&
            source.format == mesh::VertexFormat::R32G32B32A32Float)
        {
            for (u32 component = 0; component < 4; ++component)
            {
                if (!std::isfinite(values[component]))
                {
                    return false;
                }
                destination[component] = static_cast<u8>(meshopt_quantizeUnorm(values[component], 8));
            }
            return true;
        }

        const u32 byteCount = mesh::GetVertexFormatByteSize(source.format);
        for (u32 byte = 0; byte < byteCount; ++byte)
        {
            destination[byte] = sourceBytes[byte];
        }
        return true;
    }

    bool RegisterBuiltInProfiles()
    {
        constexpr tools::VertexPackingRuleFlags AnyIndex = tools::VertexPackingRuleFlags::MatchAnySemanticIndex;
        constexpr tools::VertexPackingRuleFlags Required = tools::VertexPackingRuleFlags::Required;

        static const tools::VertexPackingRule RuntimeStaticRules[] = {
            {mesh::VertexSemantic::Position, 0, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R16G16B16A16SNorm, 0, Required,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Normal, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Tangent, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Tangent, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::TexCoord, tools::AnySemanticIndex, mesh::VertexFormat::R32G32Float, mesh::VertexFormat::R16G16Float, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Color, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R8G8B8A8UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::JointWeights, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R8G8B8A8UNorm, 2, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::JointIndices, tools::AnySemanticIndex, mesh::VertexFormat::R8G8B8A8UInt, mesh::VertexFormat::R8G8B8A8UInt, 2, AnyIndex,
             nullptr},
            {mesh::VertexSemantic::JointIndices, tools::AnySemanticIndex, mesh::VertexFormat::R16G16B16A16UInt, mesh::VertexFormat::R16G16B16A16UInt, 2,
             AnyIndex, nullptr},
            {mesh::VertexSemantic::JointIndices, tools::AnySemanticIndex, mesh::VertexFormat::R32UInt, mesh::VertexFormat::R32UInt, 2, AnyIndex, nullptr}};

        static const tools::VertexPackingRule RuntimeSkinned4Rules[] = {
            {mesh::VertexSemantic::Position, 0, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R16G16B16A16SNorm, 0, Required,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Normal, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Tangent, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Tangent, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R10G10B10A2UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::TexCoord, tools::AnySemanticIndex, mesh::VertexFormat::R32G32Float, mesh::VertexFormat::R16G16Float, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::Color, tools::AnySemanticIndex, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R8G8B8A8UNorm, 1, AnyIndex,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::JointWeights, 0, mesh::VertexFormat::R32G32B32A32Float, mesh::VertexFormat::R8G8B8A8UNorm, 2, Required,
             &PackRuntimeVertexElement},
            {mesh::VertexSemantic::JointIndices, 0, mesh::VertexFormat::R8G8B8A8UInt, mesh::VertexFormat::R8G8B8A8UInt, 2, Required, nullptr}};

        const tools::MeshCookingProfile preserveSource{tools::profiles::PreserveSource,
                                                       1,
                                                       0,
                                                       mesh::MeshKind::Static,
                                                       tools::MeshCookingProfileFlags::None,
                                                       tools::UnmatchedVertexStreamPolicy::PreserveInDedicatedBinding,
                                                       {}};
        const tools::MeshCookingProfile runtimeStatic{tools::profiles::RuntimeStatic,
                                                      1,
                                                      0,
                                                      mesh::MeshKind::Static,
                                                      tools::MeshCookingProfileFlags::QuantizePositions,
                                                      tools::UnmatchedVertexStreamPolicy::PreserveInDedicatedBinding,
                                                      {RuntimeStaticRules, static_cast<u32>(sizeof(RuntimeStaticRules) / sizeof(RuntimeStaticRules[0]))}};
        const tools::MeshCookingProfile runtimeSkinned4{
            tools::profiles::RuntimeSkinned4,
            1,
            0,
            mesh::MeshKind::Skinned,
            tools::MeshCookingProfileFlags::QuantizePositions,
            tools::UnmatchedVertexStreamPolicy::PreserveInDedicatedBinding,
            {RuntimeSkinned4Rules, static_cast<u32>(sizeof(RuntimeSkinned4Rules) / sizeof(RuntimeSkinned4Rules[0]))}};

        return RegisterProfileInternal(preserveSource) == tools::ProfileRegistrationResult::Success &&
               RegisterProfileInternal(runtimeStatic) == tools::ProfileRegistrationResult::Success &&
               RegisterProfileInternal(runtimeSkinned4) == tools::ProfileRegistrationResult::Success;
    }

    bool EmitProfilePackedStreams(BuildStorage& storage, const tools::SourceSubmesh& source, const tools::MeshCookingProfile& profile,
                                  const containers::DynamicArray<Payload>& sourcePayloads, const u32 vertexCount, const u32 layoutId,
                                  const bool requiredForLowestLod, const mesh::PositionQuantization& quantization, u32& nextBufferId)
    {
        containers::DynamicArray<PackedStreamPlan> plans(memory::pools::Assets::GetInstance());
        if (!BuildPackedStreamPlans(source, profile, plans))
        {
            return false;
        }

        u8 binding = 0;
        u32 first = 0;
        while (first < plans.Size())
        {
            const u32 group = plans[first].group;
            u32 end = first;
            u32 stride = 0;
            while (end < plans.Size() && plans[end].group == group)
            {
                plans[end].byteOffset = stride;
                stride += plans[end].elementSize;
                ++end;
            }
            if (stride == 0 || vertexCount > 0xffffffffu / stride)
            {
                return false;
            }

            Payload packedPayload;
            packedPayload.id = nextBufferId++;
            packedPayload.kind = mesh::BufferKind::Vertex;
            packedPayload.stride = stride;
            packedPayload.requiredForLowestLod = requiredForLowestLod;
            packedPayload.bytes.Resize(vertexCount * stride);
            for (u32 vertex = 0; vertex < vertexCount; ++vertex)
            {
                for (u32 planIndex = first; planIndex < end; ++planIndex)
                {
                    const PackedStreamPlan& plan = plans[planIndex];
                    const Payload& sourcePayload = sourcePayloads[plan.sourceIndex];
                    const u8* const sourceBytes = sourcePayload.bytes.TypedData() + static_cast<usize>(vertex) * sourcePayload.stride;
                    u8* const destination = packedPayload.bytes.TypedData() + static_cast<usize>(vertex) * stride + plan.byteOffset;
                    if (plan.pack != nullptr)
                    {
                        if (!plan.pack(destination, source.vertexStreams[plan.sourceIndex], sourceBytes, quantization))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        for (u32 byte = 0; byte < plan.elementSize; ++byte)
                        {
                            destination[byte] = sourceBytes[byte];
                        }
                    }
                }
            }
            for (u32 planIndex = first; planIndex < end; ++planIndex)
            {
                const PackedStreamPlan& plan = plans[planIndex];
                const tools::SourceVertexStream& sourceStream = source.vertexStreams[plan.sourceIndex];
                storage.streams.PushBack(
                    {layoutId, sourceStream.semantic, sourceStream.semanticIndex, plan.format, binding, packedPayload.id, plan.byteOffset, stride});
            }
            storage.payloads.PushBack(static_cast<Payload&&>(packedPayload));
            ++binding;
            first = end;
        }
        return true;
    }
} // namespace

namespace vanguard::mesh_tools
{
    const char* ToString(const Result result) noexcept
    {
        switch (result)
        {
        case Result::Success:
            return "Success";
        case Result::InvalidArgument:
            return "InvalidArgument";
        case Result::InvalidState:
            return "InvalidState";
        case Result::UnknownCookingProfile:
            return "UnknownCookingProfile";
        case Result::LimitExceeded:
            return "LimitExceeded";
        case Result::MissingPositionStream:
            return "MissingPositionStream";
        case Result::InvalidVertexStream:
            return "InvalidVertexStream";
        case Result::InvalidIndex:
            return "InvalidIndex";
        case Result::OptimizationFailure:
            return "OptimizationFailure";
        case Result::MeshWriteFailure:
            return "MeshWriteFailure";
        }
        return "Unknown";
    }

    bool Initialize() noexcept
    {
        if (g_initialized)
        {
            return true;
        }
        if (!memory::IsInitialized() || !containers::IsInitialized())
        {
            return false;
        }
        meshopt_setAllocator(&MeshoptimizerAllocate, &MeshoptimizerFree);
        if (!RegisterBuiltInProfiles())
        {
            return false;
        }
        g_initialized = true;
        return true;
    }

    bool IsInitialized() noexcept
    {
        return g_initialized;
    }

    ProfileRegistrationResult RegisterMeshCookingProfile(const MeshCookingProfile& profile) noexcept
    {
        return g_initialized ? RegisterProfileInternal(profile) : ProfileRegistrationResult::InvalidArgument;
    }

    const MeshCookingProfile* FindMeshCookingProfile(const MeshCookingProfileId id) noexcept
    {
        return g_initialized ? FindProfileInternal(id) : nullptr;
    }

    Result CookMesh(const SourceMesh& source, filesystem::IFile& output, const CookSettings& settings, CookReport* const report) noexcept
    {
        if (!g_initialized)
        {
            return Result::InvalidState;
        }
        const MeshCookingProfile* const cookingProfile = FindProfileInternal(settings.meshCookingProfile);
        if (cookingProfile == nullptr)
        {
            return Result::UnknownCookingProfile;
        }
        if ((cookingProfile->meshKind == mesh::MeshKind::Skinned) != source.skeleton.IsValid())
        {
            return Result::InvalidArgument;
        }
        g_profileRegistrySealed = true;
        if (source.submeshes.Empty() || source.submeshes.Size() > settings.maximumSubmeshes || settings.overdrawThreshold < 1.0f ||
            settings.lodLevels.Size() > settings.maximumLodLevels || settings.lodAttributeWeights.normal < 0.0f ||
            settings.lodAttributeWeights.tangent < 0.0f || settings.lodAttributeWeights.texCoord < 0.0f || settings.lodAttributeWeights.color < 0.0f ||
            settings.lodAttributeWeights.jointWeights < 0.0f || settings.lodAttributeWeights.morphPosition < 0.0f)
        {
            return Result::InvalidArgument;
        }
        f32 previousTriangleRatio = 1.0f;
        f32 previousScreenCoverage = 1.0f;
        f32 previousMaximumError = 0.0f;
        for (const LodLevelSettings& lod : settings.lodLevels)
        {
            if (lod.triangleRatio <= 0.0f || lod.triangleRatio >= previousTriangleRatio || lod.maximumNormalizedError <= 0.0f ||
                lod.maximumNormalizedError < previousMaximumError || lod.minimumScreenCoverage <= 0.0f || lod.minimumScreenCoverage >= previousScreenCoverage)
            {
                return Result::InvalidArgument;
            }
            previousTriangleRatio = lod.triangleRatio;
            previousScreenCoverage = lod.minimumScreenCoverage;
            previousMaximumError = lod.maximumNormalizedError;
        }

        BuildStorage storage;
        const u32 lodCount = settings.lodLevels.Size() + 1;
        storage.layouts.Reserve(source.submeshes.Size() * lodCount);
        storage.materials.Reserve(source.submeshes.Size());
        storage.submeshes.Reserve(source.submeshes.Size() * lodCount);
        storage.lods.Reserve(lodCount);
        storage.lods.PushBack({0, 1.0f});
        for (u32 lodIndex = 0; lodIndex < settings.lodLevels.Size(); ++lodIndex)
        {
            storage.lods.PushBack({static_cast<u16>(lodIndex + 1), settings.lodLevels[lodIndex].minimumScreenCoverage});
        }
        if (report != nullptr)
        {
            report->submeshes.Clear();
            report->submeshes.Reserve(source.submeshes.Size());
            report->lods.Clear();
            report->lods.Reserve(source.submeshes.Size() * lodCount);
            report->meshCookingProfile = cookingProfile->id;
            report->meshCookingProfileVersion = cookingProfile->version;
        }

        mesh::Bounds meshBounds;
        bool hasMeshBounds = false;
        for (const SourceSubmesh& input : source.submeshes)
        {
            const SourceVertexStream* const position = FindPosition(input);
            if (position == nullptr || position->format != mesh::VertexFormat::R32G32B32Float)
            {
                return Result::MissingPositionStream;
            }
            if (position->data == nullptr || position->vertexCount == 0 || position->vertexCount > settings.maximumVerticesPerSubmesh ||
                position->stride != sizeof(f32) * 3)
            {
                return Result::InvalidVertexStream;
            }
            const f32* const values = static_cast<const f32*>(position->data);
            for (u32 component = 0; component < position->vertexCount * 3; ++component)
            {
                if (!std::isfinite(values[component]))
                {
                    return Result::InvalidVertexStream;
                }
            }
            const mesh::Bounds sourceBounds = CalculateBounds(values, position->vertexCount, position->stride);
            if (!hasMeshBounds)
            {
                meshBounds = sourceBounds;
                hasMeshBounds = true;
            }
            else
            {
                for (u32 axis = 0; axis < 3; ++axis)
                {
                    if (sourceBounds.minimum[axis] < meshBounds.minimum[axis])
                        meshBounds.minimum[axis] = sourceBounds.minimum[axis];
                    if (sourceBounds.maximum[axis] > meshBounds.maximum[axis])
                        meshBounds.maximum[axis] = sourceBounds.maximum[axis];
                }
            }
        }
        mesh::PositionQuantization positionQuantization;
        if (HasFlag(cookingProfile->flags, MeshCookingProfileFlags::QuantizePositions))
        {
            for (u32 axis = 0; axis < 3; ++axis)
            {
                positionQuantization.bias[axis] = (meshBounds.minimum[axis] + meshBounds.maximum[axis]) * 0.5f;
                const f32 halfExtent = (meshBounds.maximum[axis] - meshBounds.minimum[axis]) * 0.5f;
                positionQuantization.scale[axis] = halfExtent > 0.0f ? halfExtent : 1.0f;
            }
        }
        u32 nextBufferId = 1;
        for (u32 submeshIndex = 0; submeshIndex < source.submeshes.Size(); ++submeshIndex)
        {
            const SourceSubmesh& input = source.submeshes[submeshIndex];
            if (input.stableId == 0 || input.vertexStreams.Empty() || input.vertexStreams.Size() > settings.maximumVertexStreamsPerSubmesh ||
                input.indices.Empty() || input.indices.Size() > settings.maximumIndicesPerSubmesh || input.indices.Size() % 3 != 0)
            {
                return Result::InvalidArgument;
            }
            const SourceVertexStream* const sourcePosition = FindPosition(input);
            if (sourcePosition == nullptr || sourcePosition->format != mesh::VertexFormat::R32G32B32Float)
            {
                return Result::MissingPositionStream;
            }
            const u32 sourceVertexCount = sourcePosition->vertexCount;
            if (sourceVertexCount == 0 || sourceVertexCount > settings.maximumVerticesPerSubmesh)
            {
                return Result::LimitExceeded;
            }

            meshopt_Stream optimizerStreams[32]{};
            for (u32 streamIndex = 0; streamIndex < input.vertexStreams.Size(); ++streamIndex)
            {
                const SourceVertexStream& stream = input.vertexStreams[streamIndex];
                const u32 elementSize = mesh::GetVertexFormatByteSize(stream.format);
                if (stream.data == nullptr || stream.vertexCount != sourceVertexCount || stream.stride != elementSize || elementSize == 0 || elementSize > 256)
                {
                    return Result::InvalidVertexStream;
                }
                optimizerStreams[streamIndex] = {stream.data, elementSize, elementSize};
            }
            if (!ValidateSourceStreamsAgainstProfile(input, *cookingProfile))
            {
                return Result::InvalidVertexStream;
            }
            for (const u32 index : input.indices)
            {
                if (index >= sourceVertexCount)
                {
                    return Result::InvalidIndex;
                }
            }

            containers::DynamicArray<u32> remap(memory::pools::Assets::GetInstance());
            remap.Resize(sourceVertexCount);
            const usize uniqueVertexCountRaw = meshopt_generateVertexRemapMulti(remap.TypedData(), input.indices.Data(), input.indices.Size(),
                                                                                sourceVertexCount, optimizerStreams, input.vertexStreams.Size());
            if (uniqueVertexCountRaw == 0 || uniqueVertexCountRaw > 0xffffffffu)
            {
                return Result::OptimizationFailure;
            }
            const u32 cookedVertexCount = static_cast<u32>(uniqueVertexCountRaw);
            containers::DynamicArray<u32> indices(memory::pools::Assets::GetInstance());
            indices.Resize(input.indices.Size());
            meshopt_remapIndexBuffer(indices.TypedData(), input.indices.Data(), input.indices.Size(), remap.TypedData());

            containers::DynamicArray<Payload> baseVertexPayloads(memory::pools::Assets::GetInstance());
            baseVertexPayloads.Resize(input.vertexStreams.Size());
            for (u32 streamIndex = 0; streamIndex < input.vertexStreams.Size(); ++streamIndex)
            {
                Payload& payload = baseVertexPayloads[streamIndex];
                if (!CopyRemappedStream(payload, input.vertexStreams[streamIndex], sourceVertexCount, cookedVertexCount, remap.TypedData()))
                {
                    return Result::LimitExceeded;
                }
            }

            const u32 positionStreamIndex = static_cast<u32>(sourcePosition - input.vertexStreams.Data());
            const f32* const basePositions = reinterpret_cast<const f32*>(baseVertexPayloads[positionStreamIndex].bytes.TypedData());
            const u32 basePositionStride = baseVertexPayloads[positionStreamIndex].stride;
            containers::DynamicArray<f32> simplificationAttributes(memory::pools::Assets::GetInstance());
            containers::DynamicArray<f32> simplificationWeights(memory::pools::Assets::GetInstance());
            const SimplificationAttributeBuildResult attributeBuildResult = BuildSimplificationAttributes(
                input, baseVertexPayloads, cookedVertexCount, settings.lodAttributeWeights, simplificationAttributes, simplificationWeights);
            if (attributeBuildResult == SimplificationAttributeBuildResult::UnsupportedFormat)
            {
                return Result::InvalidVertexStream;
            }
            if (attributeBuildResult == SimplificationAttributeBuildResult::ComponentLimitExceeded)
            {
                return Result::LimitExceeded;
            }

            const u32 materialId = submeshIndex + 1;
            storage.materials.PushBack({materialId, input.materialName, input.material});
            for (u32 lodIndex = 0; lodIndex < lodCount; ++lodIndex)
            {
                containers::DynamicArray<u32> lodIndices(memory::pools::Assets::GetInstance());
                u32 targetIndexCount = indices.Size();
                bool reachedTriangleTarget = true;
                f32 normalizedError = 0.0f;
                if (lodIndex > 0)
                {
                    const LodLevelSettings& lodSettings = settings.lodLevels[lodIndex - 1];
                    targetIndexCount = static_cast<u32>(static_cast<f32>(input.indices.Size()) * lodSettings.triangleRatio);
                    targetIndexCount -= targetIndexCount % 3;
                    if (targetIndexCount < 3)
                    {
                        targetIndexCount = 3;
                    }
                    if (targetIndexCount < indices.Size())
                    {
                        lodIndices.Resize(indices.Size());
                        u32 options = settings.lockLodBorders ? meshopt_SimplifyLockBorder : 0;
                        if (settings.regularizeLodTriangles)
                        {
                            options |= meshopt_SimplifyRegularize;
                        }
                        usize simplifiedCount = 0;
                        if (!simplificationWeights.Empty())
                        {
                            simplifiedCount = meshopt_simplifyWithAttributes(lodIndices.TypedData(), indices.TypedData(), indices.Size(), basePositions,
                                                                             cookedVertexCount, basePositionStride, simplificationAttributes.TypedData(),
                                                                             static_cast<usize>(simplificationWeights.Size()) * sizeof(f32),
                                                                             simplificationWeights.TypedData(), simplificationWeights.Size(), nullptr,
                                                                             targetIndexCount, lodSettings.maximumNormalizedError, options, &normalizedError);
                        }
                        else
                        {
                            simplifiedCount =
                                meshopt_simplify(lodIndices.TypedData(), indices.TypedData(), indices.Size(), basePositions, cookedVertexCount,
                                                 basePositionStride, targetIndexCount, lodSettings.maximumNormalizedError, options, &normalizedError);
                        }
                        if (simplifiedCount == 0 || simplifiedCount > 0xffffffffu || simplifiedCount % 3 != 0)
                        {
                            return Result::OptimizationFailure;
                        }
                        lodIndices.Resize(static_cast<u32>(simplifiedCount));
                    }
                    else
                    {
                        lodIndices = indices;
                    }
                    reachedTriangleTarget = lodIndices.Size() <= targetIndexCount;
                }
                else
                {
                    lodIndices = indices;
                }
                if (settings.optimizeVertexCache)
                {
                    meshopt_optimizeVertexCache(lodIndices.TypedData(), lodIndices.TypedData(), lodIndices.Size(), cookedVertexCount);
                }
                if (settings.optimizeOverdraw)
                {
                    meshopt_optimizeOverdraw(lodIndices.TypedData(), lodIndices.TypedData(), lodIndices.Size(), basePositions, cookedVertexCount,
                                             basePositionStride, settings.overdrawThreshold);
                }

                containers::DynamicArray<Payload> lodVertexPayloads(memory::pools::Assets::GetInstance());
                u32 lodVertexCount = 0;
                if (!CompactLodStreams(baseVertexPayloads, lodIndices, cookedVertexCount, settings.optimizeVertexFetch, lodVertexPayloads, lodVertexCount))
                {
                    return Result::OptimizationFailure;
                }

                const Payload& cookedPositionPayload = lodVertexPayloads[positionStreamIndex];
                const f32* const cookedPositions = reinterpret_cast<const f32*>(cookedPositionPayload.bytes.TypedData());
                const mesh::Bounds bounds = CalculateBounds(cookedPositions, lodVertexCount, cookedPositionPayload.stride);
                if (report != nullptr)
                {
                    report->lods.PushBack({input.stableId, static_cast<u16>(lodIndex), input.indices.Size(), lodIndices.Size(), simplificationWeights.Size(),
                                           normalizedError, reachedTriangleTarget});
                    if (lodIndex == 0)
                    {
                        SubmeshCookStatistics statistics;
                        statistics.stableId = input.stableId;
                        statistics.sourceVertexCount = sourceVertexCount;
                        statistics.cookedVertexCount = lodVertexCount;
                        statistics.indexCount = lodIndices.Size();
                        const meshopt_VertexCacheStatistics sourceCache =
                            meshopt_analyzeVertexCache(input.indices.Data(), input.indices.Size(), sourceVertexCount, 16, 0, 0);
                        const meshopt_VertexCacheStatistics cookedCache =
                            meshopt_analyzeVertexCache(lodIndices.TypedData(), lodIndices.Size(), lodVertexCount, 16, 0, 0);
                        const meshopt_VertexFetchStatistics sourceFetch =
                            meshopt_analyzeVertexFetch(input.indices.Data(), input.indices.Size(), sourceVertexCount, sourcePosition->stride);
                        const meshopt_VertexFetchStatistics cookedFetch =
                            meshopt_analyzeVertexFetch(lodIndices.TypedData(), lodIndices.Size(), lodVertexCount, cookedPositionPayload.stride);
                        const meshopt_OverdrawStatistics sourceOverdraw =
                            meshopt_analyzeOverdraw(input.indices.Data(), input.indices.Size(), static_cast<const f32*>(sourcePosition->data),
                                                    sourceVertexCount, sourcePosition->stride);
                        const meshopt_OverdrawStatistics cookedOverdraw =
                            meshopt_analyzeOverdraw(lodIndices.TypedData(), lodIndices.Size(), cookedPositions, lodVertexCount, cookedPositionPayload.stride);
                        statistics.sourceVertexCacheMissRatio = sourceCache.acmr;
                        statistics.cookedVertexCacheMissRatio = cookedCache.acmr;
                        statistics.sourceVertexFetchOverfetch = sourceFetch.overfetch;
                        statistics.cookedVertexFetchOverfetch = cookedFetch.overfetch;
                        statistics.sourceOverdraw = sourceOverdraw.overdraw;
                        statistics.cookedOverdraw = cookedOverdraw.overdraw;
                        report->submeshes.PushBack(statistics);
                    }
                }

                const u32 layoutId = submeshIndex * lodCount + lodIndex + 1;
                storage.layouts.PushBack({layoutId});
                const bool requiredForLowestLod = lodIndex + 1 == lodCount;
                if (!EmitProfilePackedStreams(storage, input, *cookingProfile, lodVertexPayloads, lodVertexCount, layoutId, requiredForLowestLod,
                                              positionQuantization, nextBufferId))
                {
                    return Result::InvalidVertexStream;
                }

                Payload indexPayload;
                indexPayload.id = nextBufferId++;
                indexPayload.kind = mesh::BufferKind::Index;
                indexPayload.requiredForLowestLod = requiredForLowestLod;
                const bool use16BitIndices = lodVertexCount <= 65536;
                if (use16BitIndices)
                {
                    containers::DynamicArray<u16> packedIndices(memory::pools::Assets::GetInstance());
                    packedIndices.Resize(lodIndices.Size());
                    for (u32 index = 0; index < lodIndices.Size(); ++index)
                    {
                        packedIndices[index] = static_cast<u16>(lodIndices[index]);
                    }
                    indexPayload.stride = sizeof(u16);
                    CopyBytes(indexPayload.bytes, packedIndices.TypedData(), static_cast<usize>(packedIndices.Size()) * sizeof(u16));
                }
                else
                {
                    indexPayload.stride = sizeof(u32);
                    CopyBytes(indexPayload.bytes, lodIndices.TypedData(), static_cast<usize>(lodIndices.Size()) * sizeof(u32));
                }
                const u32 indexBufferId = indexPayload.id;
                storage.payloads.PushBack(static_cast<Payload&&>(indexPayload));

                storage.submeshes.PushBack({input.stableId, input.name, static_cast<u16>(lodIndex), materialId, layoutId, indexBufferId,
                                            use16BitIndices ? mesh::IndexFormat::UInt16 : mesh::IndexFormat::UInt32, mesh::PrimitiveTopology::TriangleList,
                                            input.flags, 0, lodVertexCount, 0, lodIndices.Size(), bounds});
            }
        }

        for (Payload& payload : storage.payloads)
        {
            storage.buffers.PushBack({payload.id, payload.kind, payload.stride, payload.bytes.Size()});
            const mesh::PageFlags flags =
                payload.requiredForLowestLod ? mesh::PageFlags::RequiredForLowestLod | mesh::PageFlags::DirectGpuUpload : mesh::PageFlags::DirectGpuUpload;
            storage.pages.PushBack({payload.id, 0, payload.bytes.TypedData(), payload.bytes.Size(), 4, flags});
        }

        for (u32 axis = 0; axis < 3; ++axis)
        {
            meshBounds.sphereCenter[axis] = (meshBounds.minimum[axis] + meshBounds.maximum[axis]) * 0.5f;
        }
        f32 meshRadiusSquared = 0.0f;
        for (u32 axis = 0; axis < 3; ++axis)
        {
            const f32 extent = meshBounds.maximum[axis] - meshBounds.sphereCenter[axis];
            meshRadiusSquared += extent * extent;
        }
        meshBounds.sphereRadius = ::sqrtf(meshRadiusSquared);

        mesh::BuildDescription description;
        description.kind = cookingProfile->meshKind;
        description.name = source.name;
        description.bounds = meshBounds;
        description.positionQuantization = positionQuantization;
        description.sourceFingerprint = source.sourceFingerprint;
        description.skeleton = source.skeleton;
        description.buffers = Span(storage.buffers);
        description.pages = Span(storage.pages);
        description.vertexLayouts = Span(storage.layouts);
        description.vertexStreams = Span(storage.streams);
        description.materialSlots = Span(storage.materials);
        description.lods = Span(storage.lods);
        description.submeshes = Span(storage.submeshes);
        return mesh::WriteMesh(output, description) == mesh::Result::Success ? Result::Success : Result::MeshWriteFailure;
    }

} // namespace vanguard::mesh_tools
