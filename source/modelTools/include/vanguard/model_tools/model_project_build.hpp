#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/assets/source_database.hpp>
#include <vanguard/concurrency/synchronization.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/crypto/crypto.hpp>
#include <vanguard/entities/component_schemas.hpp>
#include <vanguard/filesystem/filesystem.hpp>
#include <vanguard/material_tools/material_canonical_builder.hpp>
#include <vanguard/materials/materials.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/mesh_tools/model_import.hpp>
#include <vanguard/meshes/meshes.hpp>
#include <vanguard/pipelines/pipelines.hpp>
#include <vanguard/prefabs/prefabs.hpp>
#include <vanguard/resources/resources.hpp>
#include <vanguard/serialization/serialization.hpp>
#include <vanguard/shaders/shaders.hpp>
#include <vanguard/texture_tools/texture_asset_compiler.hpp>
#include <vanguard/texture_tools/texture_tools.hpp>
#include <vanguard/textures/textures.hpp>

#include <algorithm>
#include <cstring>

namespace vanguard::model_tools
{
    namespace detail
    {
        inline constexpr u32 MaximumGeneratedRequests = 262144;
        inline constexpr u32 GeneratedRequestBucketCount = 524288;
        inline constexpr u64 MaximumGeneratedRequestBytes = 512ull * 1024ull * 1024ull;
        inline constexpr resources::ResourceTypeId PrefabBuildInputResourceType = serialization::MakeFourCC('V', 'P', 'B', 'I');
        inline constexpr u32 PrefabCompilerVersion = 1u;
        inline constexpr char PrefabCompilerName[] = "vanguard.prefab.compiler";
        inline constexpr u64 CompositionEntityStableId = 1u;

        struct GeneratedRequestEntry final
        {
            resources::ResourceReference identity;
            resources::ResourceReference publisher;
            assets::OwnedBuildRequest request;
            u32 next = 0;
        };

        [[nodiscard]] inline bool EqualBytes(const containers::DynamicArray<u8>& left,
                                      const containers::DynamicArray<u8>& right) noexcept
        {
            return left.Size() == right.Size() &&
                   (left.Empty() || std::memcmp(left.TypedData(), right.TypedData(), left.Size()) == 0);
        }

        [[nodiscard]] inline bool EquivalentGeneratedRequest(const assets::OwnedBuildRequest& left,
                                                      const assets::OwnedBuildRequest& right) noexcept
        {
            return left.sourceReference == right.sourceReference && left.outputReference == right.outputReference &&
                   left.target == right.target && EqualBytes(left.source, right.source) &&
                   EqualBytes(left.metadata, right.metadata) && EqualBytes(left.settings, right.settings);
        }

        struct GeneratedRequestStore final
        {
            GeneratedRequestStore() noexcept : entries(memory::pools::Tools::GetInstance()) {}

            ~GeneratedRequestStore()
            {
                Clear();
            }

            GeneratedRequestStore(const GeneratedRequestStore&) = delete;
            GeneratedRequestStore& operator=(const GeneratedRequestStore&) = delete;

            [[nodiscard]] u32 Bucket(const resources::ResourceReference output) const noexcept
            {
                return static_cast<u32>(output.GetPath().Id()) & (GeneratedRequestBucketCount - 1u);
            }

            [[nodiscard]] bool Publish(const resources::ResourceReference publisher, const assets::BuildRequest& request) noexcept
            {
                return Publish(publisher, containers::ArraySpan<const assets::BuildRequest>(&request, 1u));
            }

            [[nodiscard]] bool Publish(const resources::ResourceReference publisher,
                                       const containers::ArraySpan<const assets::BuildRequest> requests) noexcept
            {
                if (!publisher.IsValid() || !publisher.IsTyped())
                    return false;
                if (requests.Empty())
                    return true;
                if (requests.Count() > MaximumGeneratedRequests)
                    return false;

                containers::DynamicArray<GeneratedRequestEntry*> fresh(memory::pools::Tools::GetInstance());
                fresh.Reserve(requests.Count());
                if (fresh.Capacity() < requests.Count())
                    return false;
                for (const assets::BuildRequest& request : requests)
                {
                    GeneratedRequestEntry* const entry = VANGUARD_NEW(GeneratedRequestEntry, memory::pools::Tools);
                    if (entry == nullptr || !entry->request.CopyFrom(request) ||
                        entry->request.ByteCount() > MaximumGeneratedRequestBytes)
                    {
                        if (entry != nullptr)
                            VANGUARD_DELETE(entry);
                        for (GeneratedRequestEntry* const pending : fresh)
                            VANGUARD_DELETE(pending);
                        return false;
                    }
                    entry->identity = request.output;
                    entry->publisher = publisher;
                    fresh.PushBack(entry);
                }

                bool success = true;
                {
                    vanguard::concurrency::ScopedLock lock(mutex);
                    u32 finalEntries = entries.Size();
                    u64 finalBytes = totalBytes;
                    for (u32 freshIndex = 0u; freshIndex < fresh.Size() && success; ++freshIndex)
                    {
                        const GeneratedRequestEntry& candidate = *fresh[freshIndex];
                        bool duplicateInBatch = false;
                        for (u32 earlierIndex = 0u; earlierIndex < freshIndex; ++earlierIndex)
                        {
                            const GeneratedRequestEntry& earlier = *fresh[earlierIndex];
                            if (earlier.identity != candidate.identity)
                                continue;
                            duplicateInBatch = true;
                            success = EquivalentGeneratedRequest(earlier.request, candidate.request);
                            break;
                        }
                        if (!success || duplicateInBatch)
                            continue;

                        const GeneratedRequestEntry* existing = nullptr;
                        u32 current = buckets[Bucket(candidate.identity)];
                        while (current != 0u)
                        {
                            const GeneratedRequestEntry& value = *entries[current - 1u];
                            if (value.identity == candidate.identity)
                            {
                                existing = &value;
                                break;
                            }
                            current = value.next;
                        }
                        if (existing != nullptr)
                        {
                            if (existing->publisher != publisher || finalBytes < existing->request.ByteCount())
                            {
                                success = false;
                                continue;
                            }
                            finalBytes -= existing->request.ByteCount();
                        }
                        else
                        {
                            if (finalEntries >= MaximumGeneratedRequests)
                            {
                                success = false;
                                continue;
                            }
                            ++finalEntries;
                        }
                        const u64 bytes = candidate.request.ByteCount();
                        if (finalBytes > MaximumGeneratedRequestBytes - bytes)
                            success = false;
                        else
                            finalBytes += bytes;
                    }

                    if (success)
                    {
                        entries.Reserve(finalEntries);
                        success = entries.Capacity() >= finalEntries;
                    }

                    if (success)
                    {
                        for (u32 freshIndex = 0u; freshIndex < fresh.Size(); ++freshIndex)
                        {
                            GeneratedRequestEntry* const candidate = fresh[freshIndex];
                            bool duplicateInBatch = false;
                            for (u32 earlierIndex = 0u; earlierIndex < freshIndex; ++earlierIndex)
                            {
                                const GeneratedRequestEntry* const earlier = fresh[earlierIndex];
                                if (earlier != nullptr && earlier->identity == candidate->identity)
                                {
                                    duplicateInBatch = true;
                                    break;
                                }
                            }
                            if (duplicateInBatch)
                                continue;

                            u32 existingIndex = ~u32{0};
                            u32 current = buckets[Bucket(candidate->identity)];
                            while (current != 0u)
                            {
                                const GeneratedRequestEntry& existing = *entries[current - 1u];
                                if (existing.identity == candidate->identity)
                                {
                                    existingIndex = current - 1u;
                                    break;
                                }
                                current = existing.next;
                            }
                            if (existingIndex != ~u32{0})
                            {
                                GeneratedRequestEntry* const existing = entries[existingIndex];
                                if (!existing->request.View().IsValid() || !EquivalentGeneratedRequest(existing->request, candidate->request))
                                {
                                    candidate->next = existing->next;
                                    totalBytes = totalBytes - existing->request.ByteCount() + candidate->request.ByteCount();
                                    entries[existingIndex] = candidate;
                                    fresh[freshIndex] = nullptr;
                                    VANGUARD_DELETE(existing);
                                }
                                continue;
                            }

                            const u32 bucket = Bucket(candidate->identity);
                            const u32 previous = entries.Size();
                            candidate->next = buckets[bucket];
                            entries.PushBack(candidate);
                            buckets[bucket] = previous + 1u;
                            totalBytes += candidate->request.ByteCount();
                            fresh[freshIndex] = nullptr;
                        }
                    }
                }

                for (GeneratedRequestEntry* const pending : fresh)
                    if (pending != nullptr)
                        VANGUARD_DELETE(pending);
                return success;
            }

            [[nodiscard]] bool Resolve(const assets::BuildDependency& dependency, assets::OwnedBuildRequest& request) noexcept
            {
                if (!dependency.IsValid())
                    return false;
                vanguard::concurrency::ScopedLock lock(mutex);
                u32 current = buckets[Bucket(dependency.identity)];
                while (current != 0u)
                {
                    GeneratedRequestEntry* const entry = entries[current - 1u];
                    if (entry->identity == dependency.identity)
                        return request.CopyFrom(entry->request.View());
                    current = entry->next;
                }
                return false;
            }

            [[nodiscard]] bool PruneRetiredPublishers(const assets::SourceDatabase& sources) noexcept
            {
                vanguard::concurrency::ScopedLock lock(mutex);
                u32 index = 0u;
                while (index < entries.Size())
                {
                    GeneratedRequestEntry* const retired = entries[index];
                    if (sources.LookupOutput(retired->publisher) != nullptr)
                    {
                        ++index;
                        continue;
                    }

                    u32* link = &buckets[Bucket(retired->identity)];
                    while (*link != 0u && *link != index + 1u)
                        link = &entries[*link - 1u]->next;
                    if (*link != index + 1u || totalBytes < retired->request.ByteCount())
                        return false;
                    *link = retired->next;
                    totalBytes -= retired->request.ByteCount();

                    const u32 lastIndex = entries.Size() - 1u;
                    if (index != lastIndex)
                    {
                        GeneratedRequestEntry* const moved = entries[lastIndex];
                        entries[index] = moved;
                        u32* movedLink = &buckets[Bucket(moved->identity)];
                        while (*movedLink != 0u && *movedLink != lastIndex + 1u)
                            movedLink = &entries[*movedLink - 1u]->next;
                        if (*movedLink != lastIndex + 1u)
                            return false;
                        *movedLink = index + 1u;
                    }
                    static_cast<void>(entries.PopBack());
                    VANGUARD_DELETE(retired);
                }
                return true;
            }

            void Clear() noexcept
            {
                vanguard::concurrency::ScopedLock lock(mutex);
                for (GeneratedRequestEntry* const entry : entries)
                    VANGUARD_DELETE(entry);
                entries.Clear();
                totalBytes = 0;
                for (u32& bucket : buckets)
                    bucket = 0;
            }

            vanguard::concurrency::Mutex mutex;
            containers::DynamicArray<GeneratedRequestEntry*> entries;
            u32 buckets[GeneratedRequestBucketCount]{};
            u64 totalBytes = 0;
        };

        [[nodiscard]] inline bool ResolveGeneratedFallback(const assets::BuildDependency& dependency, assets::OwnedBuildRequest& request,
                                                    void* userData) noexcept
        {
            auto* const store = static_cast<GeneratedRequestStore*>(userData);
            return store != nullptr && store->Resolve(dependency, request);
        }

        [[nodiscard]] inline bool IsPreservedDds(const mesh_tools::ModelImportEmbeddedTexture& texture) noexcept
        {
            return texture.bytes.Size() >= 4u && texture.bytes[0] == 'D' && texture.bytes[1] == 'D' &&
                   texture.bytes[2] == 'S' && texture.bytes[3] == ' ';
        }

        struct ModelBuildAdapter final
        {
            assets::SourceDatabase* sources = nullptr;
            material_tools::ImportedSurfaceMaterialContext* materialContext = nullptr;
            GeneratedRequestStore* generated = nullptr;
        };

        [[nodiscard]] inline assets::DependencyRequirement ToBuildRequirement(const resources::DependencyKind kind) noexcept
        {
            switch (kind)
            {
            case resources::DependencyKind::Required:
                return assets::DependencyRequirement::Required;
            case resources::DependencyKind::Optional:
                return assets::DependencyRequirement::Optional;
            case resources::DependencyKind::Soft:
                return assets::DependencyRequirement::Soft;
            }
            return assets::DependencyRequirement::Required;
        }

        [[nodiscard]] inline bool OpenPrefabBuildInput(const containers::ArraySpan<const u8> bytes, prefabs::PrefabFile& prefab) noexcept
        {
            if (bytes.Empty() || bytes.Data() == nullptr)
                return false;
            filesystem::MemoryFileReader reader(bytes.Data(), bytes.Size(), 0);
            return prefab.Open(reader) == prefabs::Result::Success;
        }

        [[nodiscard]] inline bool DiscoverPrefabDependencies(const assets::BuildRequest& request, assets::DependencyCollector& dependencies, void*) noexcept
        {
            if (request.source.identity.ExpectedType() != PrefabBuildInputResourceType ||
                request.output.ExpectedType() != prefabs::PrefabResourceType)
                return false;
            prefabs::PrefabFile prefab;
            if (!OpenPrefabBuildInput(request.source.content, prefab))
                return false;
            for (const prefabs::DependencyRecord& dependency : prefab.GetDependencies())
                if (dependencies.Add({dependency.resource, {}, assets::DependencyRole::Generated, ToBuildRequirement(dependency.kind)}) !=
                    assets::Result::Success)
                    return false;
            return true;
        }

        [[nodiscard]] inline bool EstimatePrefabResources(const assets::BuildRequest& request,
                                                   containers::ArraySpan<const assets::BuildDependency>,
                                                   assets::BuildResourceEstimate& estimate, void*) noexcept
        {
            prefabs::PrefabFile prefab;
            if (request.output.ExpectedType() != prefabs::PrefabResourceType || !OpenPrefabBuildInput(request.source.content, prefab))
                return false;
            estimate = {};
            estimate.compilerTransientBytes = 0;
            estimate.artifactBytes = request.source.content.SizeInBytes();
            return estimate.IsValid();
        }

        [[nodiscard]] inline bool CompilePrefab(const assets::CompileContext& context, assets::ArtifactWriter& artifacts, void*) noexcept
        {
            if (context.IsCancellationRequested() || context.request.output.ExpectedType() != prefabs::PrefabResourceType ||
                artifacts.GetRemainingArtifactCount() == 0 || artifacts.GetRemainingByteCount() < context.request.source.content.SizeInBytes())
                return false;
            prefabs::PrefabFile prefab;
            if (!OpenPrefabBuildInput(context.request.source.content, prefab))
                return false;
            return artifacts.Add(context.request.output, 0, assets::ArtifactFlags::Primary | assets::ArtifactFlags::MemoryResident, 4,
                                 context.request.source.content.Data(), context.request.source.content.SizeInBytes()) == assets::Result::Success;
        }

        [[nodiscard]] inline assets::CompilerDescriptor PrefabCompilerDescriptor() noexcept
        {
            return {assets::HashCompilerName(PrefabCompilerName), PrefabCompilerName, PrefabCompilerVersion, PrefabBuildInputResourceType,
                    prefabs::PrefabResourceType, DiscoverPrefabDependencies, CompilePrefab, nullptr, EstimatePrefabResources};
        }

        [[nodiscard]] inline bool ComposeComposition(const mesh_tools::ModelOutputBuildRequestContext& context,
                                              assets::OwnedBuildRequest& request, void*) noexcept
        {
            request.Reset();
            if (context.manifest == nullptr || context.settings == nullptr || context.correspondence == nullptr || context.source == nullptr ||
                context.correspondence->kind != mesh_tools::ModelImportElementKind::Composition ||
                context.correspondence->persistentId == 0u || !context.settings->includeComposition ||
                !context.requestedOutput.IsTyped() || context.requestedOutput.ExpectedType() != prefabs::PrefabResourceType ||
                context.settings->compositionNodes.Size() != context.manifest->nodes.Size())
                return false;

            const auto& manifest = *context.manifest;
            const auto& settings = *context.settings;
            const u32 nodeCount = manifest.nodes.Size();
            const u32 geometryCount = manifest.geometryDefinitions.Size();
            const u32 instanceCount = manifest.geometryInstances.Size();

            struct GeometryOutput final
            {
                u64 persistentId = 0u;
                resources::ResourceReference resource;
            };
            containers::DynamicArray<GeometryOutput> persistedGeometryOutputs(memory::pools::Tools::GetInstance());
            persistedGeometryOutputs.Reserve(settings.outputs.Size());
            for (const mesh_tools::ModelImportOutputCorrespondence& correspondence : settings.outputs)
            {
                if (correspondence.kind != mesh_tools::ModelImportElementKind::Geometry)
                    continue;
                const resources::ResourceReference resource = assets::GetAssetOutputReference(context.source->metadata.id, correspondence.output);
                if (correspondence.persistentId == 0u || !resource.IsValid() || !resource.IsTyped() ||
                    resource.ExpectedType() != meshes::MeshResourceType)
                    return false;
                persistedGeometryOutputs.PushBack({correspondence.persistentId, resource});
            }
            if (persistedGeometryOutputs.Size() > 1u)
                std::sort(persistedGeometryOutputs.TypedData(), persistedGeometryOutputs.TypedData() + persistedGeometryOutputs.Size(),
                          [](const GeometryOutput& left, const GeometryOutput& right) noexcept
                {
                    return left.persistentId < right.persistentId;
                });
            for (u32 index = 1u; index < persistedGeometryOutputs.Size(); ++index)
                if (persistedGeometryOutputs[index - 1u].persistentId == persistedGeometryOutputs[index].persistentId)
                    return false;
            const auto findGeometryOutput = [&persistedGeometryOutputs](const u64 persistentId) noexcept -> const GeometryOutput*
            {
                u32 begin = 0u;
                u32 end = persistedGeometryOutputs.Size();
                while (begin < end)
                {
                    const u32 middle = begin + (end - begin) / 2u;
                    if (persistedGeometryOutputs[middle].persistentId < persistentId)
                        begin = middle + 1u;
                    else
                        end = middle;
                }
                return begin < persistedGeometryOutputs.Size() && persistedGeometryOutputs[begin].persistentId == persistentId
                           ? &persistedGeometryOutputs[begin]
                           : nullptr;
            };

            containers::DynamicArray<resources::ResourceReference> geometryResources(memory::pools::Tools::GetInstance());
            containers::DynamicArray<u64> geometryPersistentIds(memory::pools::Tools::GetInstance());
            geometryResources.Resize(geometryCount);
            geometryPersistentIds.Resize(geometryCount);
            if (geometryResources.Size() != geometryCount || geometryPersistentIds.Size() != geometryCount)
                return false;
            for (u32 geometry = 0; geometry < geometryCount; ++geometry)
                geometryPersistentIds[geometry] = 0u;

            containers::DynamicArray<u64> stableNodeIds(memory::pools::Tools::GetInstance());
            containers::DynamicArray<u64> parentStableNodeIds(memory::pools::Tools::GetInstance());
            containers::DynamicArray<u64> effectiveComponentIds(memory::pools::Tools::GetInstance());
            containers::DynamicArray<mesh_tools::ModelImportRigidTransform> localTransforms(memory::pools::Tools::GetInstance());
            stableNodeIds.Resize(nodeCount);
            parentStableNodeIds.Resize(nodeCount);
            effectiveComponentIds.Resize(nodeCount);
            localTransforms.Resize(nodeCount);
            if (stableNodeIds.Size() != nodeCount || parentStableNodeIds.Size() != nodeCount || effectiveComponentIds.Size() != nodeCount ||
                localTransforms.Size() != nodeCount)
                return false;
            for (u32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                stableNodeIds[nodeIndex] = 0u;
                parentStableNodeIds[nodeIndex] = 0u;
                effectiveComponentIds[nodeIndex] = 0u;
            }
            for (const mesh_tools::ModelImportCompositionNodeCorrespondence& correspondence : settings.compositionNodes)
            {
                if (correspondence.sourceNodeIndex >= nodeCount || correspondence.stableNodeId == 0u ||
                    stableNodeIds[correspondence.sourceNodeIndex] != 0u)
                    return false;
                stableNodeIds[correspondence.sourceNodeIndex] = correspondence.stableNodeId;
                parentStableNodeIds[correspondence.sourceNodeIndex] = correspondence.parentStableNodeId;
            }

            mesh_tools::ModelGeometryExtractionSettings transformSettings;
            transformSettings.uniformScale = settings.mesh.uniformScale;
            transformSettings.sourceUpAxis = settings.mesh.sourceUpAxis;
            for (u32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const mesh_tools::ModelImportNode& node = manifest.nodes[nodeIndex];
                if (stableNodeIds[nodeIndex] == 0u ||
                    !mesh_tools::ConvertModelImportRigidLocalTransform(node, transformSettings, localTransforms[nodeIndex]))
                    return false;
                if (node.parentIndex == mesh_tools::InvalidModelImportIndex)
                {
                    if (parentStableNodeIds[nodeIndex] != 0u)
                        return false;
                }
                else if (node.parentIndex >= nodeCount || parentStableNodeIds[nodeIndex] != stableNodeIds[node.parentIndex])
                    return false;
            }

            containers::DynamicArray<u64> meshComponentIds(memory::pools::Tools::GetInstance());
            meshComponentIds.Resize(instanceCount);
            if (meshComponentIds.Size() != instanceCount || settings.compositionMeshes.Size() != instanceCount)
                return false;
            for (u32 instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
                meshComponentIds[instanceIndex] = 0u;
            for (const mesh_tools::ModelImportCompositionMeshCorrespondence& correspondence : settings.compositionMeshes)
            {
                if (correspondence.sourceInstanceIndex >= instanceCount || correspondence.stableComponentId == 0u ||
                    meshComponentIds[correspondence.sourceInstanceIndex] != 0u)
                    return false;
                const mesh_tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[correspondence.sourceInstanceIndex];
                if (instance.nodeIndex >= nodeCount || instance.geometryDefinitionIndex >= geometryCount ||
                    correspondence.stableNodeId != stableNodeIds[instance.nodeIndex] ||
                    correspondence.geometryPersistentId == 0u)
                    return false;
                const GeometryOutput* const geometryOutput = findGeometryOutput(correspondence.geometryPersistentId);
                if (geometryOutput == nullptr)
                    return false;
                u64& geometryPersistentId = geometryPersistentIds[instance.geometryDefinitionIndex];
                resources::ResourceReference& geometryResource = geometryResources[instance.geometryDefinitionIndex];
                if (geometryPersistentId != 0u &&
                    (geometryPersistentId != correspondence.geometryPersistentId || geometryResource != geometryOutput->resource))
                    return false;
                geometryPersistentId = correspondence.geometryPersistentId;
                geometryResource = geometryOutput->resource;
                meshComponentIds[correspondence.sourceInstanceIndex] = correspondence.stableComponentId;
            }

            for (u32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const mesh_tools::ModelImportNode& node = manifest.nodes[nodeIndex];
                effectiveComponentIds[nodeIndex] = stableNodeIds[nodeIndex];
                if (node.geometryInstanceIndices.Size() == 1u)
                {
                    const u32 instanceIndex = node.geometryInstanceIndices[0];
                    if (instanceIndex >= instanceCount || meshComponentIds[instanceIndex] == 0u)
                        return false;
                    effectiveComponentIds[nodeIndex] = meshComponentIds[instanceIndex];
                }
                else if (node.geometryInstanceIndices.Size() > 1u)
                {
                    for (const u32 instanceIndex : node.geometryInstanceIndices)
                        if (instanceIndex >= meshComponentIds.Size() || meshComponentIds[instanceIndex] == 0u)
                            return false;
                }
            }

            u64 componentCount64 = 0u;
            u64 transformCount64 = 0u;
            for (const mesh_tools::ModelImportNode& node : manifest.nodes)
            {
                const u64 geometryInstances = node.geometryInstanceIndices.Size();
                componentCount64 += geometryInstances == 0u ? 1u : geometryInstances + (geometryInstances > 1u ? 1u : 0u);
                transformCount64 += geometryInstances == 1u ? 0u : 1u;
                if (componentCount64 > ~u32{0} || transformCount64 > ~u32{0})
                    return false;
            }
            const u32 componentCount = static_cast<u32>(componentCount64);
            containers::DynamicArray<entities::TransformComponentData> transformData(memory::pools::Tools::GetInstance());
            containers::DynamicArray<entities::StaticMeshComponentData> meshData(memory::pools::Tools::GetInstance());
            containers::DynamicArray<prefabs::ComponentBuildRecord> components(memory::pools::Tools::GetInstance());
            containers::DynamicArray<prefabs::PlacedComponentBuildRecord> placements(memory::pools::Tools::GetInstance());
            containers::DynamicArray<u64> componentIds(memory::pools::Tools::GetInstance());
            transformData.Reserve(static_cast<u32>(transformCount64));
            meshData.Reserve(instanceCount);
            components.Reserve(componentCount);
            placements.Reserve(componentCount);
            componentIds.Reserve(componentCount);
            if (transformData.Capacity() < transformCount64 || meshData.Capacity() < instanceCount || components.Capacity() < componentCount ||
                placements.Capacity() < componentCount || componentIds.Capacity() < componentCount)
                return false;

            const u64 entityStableId = CompositionEntityStableId;
            const prefabs::EntityBuildRecord entity{entityStableId, prefabs::InvalidStableId, 0u, prefabs::EntityFlags::None};
            for (u32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const mesh_tools::ModelImportNode& node = manifest.nodes[nodeIndex];
                const u64 parentComponentId = node.parentIndex == mesh_tools::InvalidModelImportIndex
                                                  ? prefabs::InvalidStableId
                                                  : effectiveComponentIds[node.parentIndex];
                const mesh_tools::ModelImportRigidTransform& transform = localTransforms[nodeIndex];

                if (node.geometryInstanceIndices.Size() != 1u)
                {
                    entities::TransformComponentData& data = transformData.EmplaceBack();
                    const u64 componentId = effectiveComponentIds[nodeIndex];
                    components.PushBack({componentId, entityStableId, &entities::GetTransformComponentSchema(), &data,
                                         prefabs::ComponentFlags::None});
                    prefabs::PlacedComponentBuildRecord& placement = placements.EmplaceBack();
                    placement.componentStableId = componentId;
                    placement.parentComponentStableId = parentComponentId;
                    std::memcpy(placement.translation, transform.translation, sizeof(placement.translation));
                    std::memcpy(placement.rotation, transform.rotation, sizeof(placement.rotation));
                    componentIds.PushBack(componentId);
                }

                for (const u32 instanceIndex : node.geometryInstanceIndices)
                {
                    if (instanceIndex >= instanceCount)
                        return false;
                    const mesh_tools::ModelImportGeometryInstance& instance = manifest.geometryInstances[instanceIndex];
                    if (instance.geometryDefinitionIndex >= geometryCount)
                        return false;
                    const u64 meshComponentId = meshComponentIds[instanceIndex];
                    if (meshComponentId == prefabs::InvalidStableId)
                        return false;
                    entities::StaticMeshComponentData& data = meshData.EmplaceBack();
                    data.mesh = geometryResources[instance.geometryDefinitionIndex];
                    components.PushBack({meshComponentId, entityStableId, &entities::GetStaticMeshComponentSchema(), &data,
                                         prefabs::ComponentFlags::None});
                    prefabs::PlacedComponentBuildRecord& placement = placements.EmplaceBack();
                    placement.componentStableId = meshComponentId;
                    if (node.geometryInstanceIndices.Size() == 1u)
                    {
                        placement.parentComponentStableId = parentComponentId;
                        std::memcpy(placement.translation, transform.translation, sizeof(placement.translation));
                        std::memcpy(placement.rotation, transform.rotation, sizeof(placement.rotation));
                    }
                    else
                        placement.parentComponentStableId = effectiveComponentIds[nodeIndex];
                    componentIds.PushBack(meshComponentId);
                }
            }
            if (components.Size() != componentCount || placements.Size() != componentCount || componentIds.Size() != componentCount)
                return false;
            if (componentIds.Size() > 1u)
                std::sort(componentIds.TypedData(), componentIds.TypedData() + componentIds.Size());
            for (u32 index = 1; index < componentIds.Size(); ++index)
                if (componentIds[index - 1u] == componentIds[index])
                    return false;

            prefabs::CookDescription description;
            description.name = context.correspondence->persistentId;
            description.entities = {&entity, 1u};
            description.components = {components.TypedData(), components.Size()};
            description.placedComponents = {placements.TypedData(), placements.Size()};
            description.sourceFingerprint = settings.sourceFingerprint.IsEmpty()
                                                ? crypto::Sha256(context.data.content.Data(), context.data.content.SizeInBytes())
                                                : settings.sourceFingerprint;
            containers::DynamicArray<u8> bytes(memory::pools::Tools::GetInstance());
            filesystem::MemoryFileWriter writer(bytes);
            if (prefabs::CookPrefab(description, writer) != prefabs::Result::Success)
                return false;

            const resources::ResourceReference source(context.requestedOutput.GetPath(), PrefabBuildInputResourceType);
            const assets::BuildRequest borrowed{{source, {bytes.TypedData(), bytes.Size()}, {}}, context.requestedOutput, context.data.target, {}};
            return borrowed.IsValid() && request.CopyFrom(borrowed);
        }

        enum class EmbeddedTextureClass : u8
        {
            Unset,
            Color,
            Normal,
            Masks,
            Data
        };

        [[nodiscard]] inline bool MergeEmbeddedTextureClass(EmbeddedTextureClass& accumulated,
                                                     const mesh_tools::ImportedTextureSemantic semantic) noexcept
        {
            EmbeddedTextureClass current = EmbeddedTextureClass::Data;
            switch (semantic)
            {
            case mesh_tools::ImportedTextureSemantic::BaseColor:
            case mesh_tools::ImportedTextureSemantic::Emissive:
            case mesh_tools::ImportedTextureSemantic::Specular:
                current = EmbeddedTextureClass::Color;
                break;
            case mesh_tools::ImportedTextureSemantic::Normal:
                current = EmbeddedTextureClass::Normal;
                break;
            case mesh_tools::ImportedTextureSemantic::Metalness:
            case mesh_tools::ImportedTextureSemantic::Roughness:
            case mesh_tools::ImportedTextureSemantic::MetallicRoughness:
            case mesh_tools::ImportedTextureSemantic::Occlusion:
            case mesh_tools::ImportedTextureSemantic::Opacity:
                current = EmbeddedTextureClass::Masks;
                break;
            case mesh_tools::ImportedTextureSemantic::Unknown:
                current = EmbeddedTextureClass::Data;
                break;
            }
            if (accumulated == EmbeddedTextureClass::Unset)
            {
                accumulated = current;
                return true;
            }
            return accumulated == current;
        }

        [[nodiscard]] inline bool ComposeEmbeddedTexture(const mesh_tools::ModelOutputBuildRequestContext& context,
                                                  assets::OwnedBuildRequest& request, void*) noexcept
        {
            if (context.manifest == nullptr || context.sourceIndex >= context.manifest->embeddedTextures.Size() ||
                !context.requestedOutput.IsValid() || context.requestedOutput.ExpectedType() != textures::TextureResourceType)
                return false;
            const mesh_tools::ModelImportEmbeddedTexture& embedded = context.manifest->embeddedTextures[context.sourceIndex];
            if (!embedded.compressed || embedded.bytes.Empty())
                return false;

            EmbeddedTextureClass textureClass = EmbeddedTextureClass::Unset;
            bool hasSpecularStrengthAlpha = false;
            for (const mesh_tools::ModelImportTextureReference& reference : context.manifest->textureReferences)
            {
                if (reference.storage != mesh_tools::ImportedTextureStorage::Embedded ||
                    reference.embeddedTextureIndex != context.sourceIndex)
                    continue;
                if (reference.semantic == mesh_tools::ImportedTextureSemantic::Specular &&
                    reference.specularTextureRole == mesh_tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha)
                {
                    // glTF KHR_materials_specular strength uses alpha, which
                    // remains linear even when this source's RGB must decode
                    // as sRGB for a specular-color or base-color consumer.
                    hasSpecularStrengthAlpha = true;
                    continue;
                }
                if (!MergeEmbeddedTextureClass(textureClass, reference.semantic))
                    return false;
            }
            if (hasSpecularStrengthAlpha &&
                textureClass != EmbeddedTextureClass::Unset && textureClass != EmbeddedTextureClass::Color &&
                textureClass != EmbeddedTextureClass::Masks)
                return false;
            if (textureClass == EmbeddedTextureClass::Unset)
                textureClass = hasSpecularStrengthAlpha ? EmbeddedTextureClass::Masks : EmbeddedTextureClass::Data;

            texture_tools::TextureBuildDescription description;
            const bool preservedDds = IsPreservedDds(embedded);
            if (preservedDds)
            {
                description.sourceMode = texture_tools::TextureBuildSourceMode::PreservedDds;
                description.colorSpace = texture_tools::ImportedColorSpace::Automatic;
                description.channels = {};
                description.routeFlags = texture_tools::TextureBuildRouteFlags::None;
                description.ddsMipTailCount = 1u;
                description.profile = 0;
            }
            else
            {
                description.sourceMode = texture_tools::TextureBuildSourceMode::Image2D;
                switch (textureClass)
                {
                case EmbeddedTextureClass::Color:
                    description.colorSpace = texture_tools::ImportedColorSpace::SRgb;
                    description.profile = texture_tools::profiles::ColorAlpha;
                    break;
                case EmbeddedTextureClass::Normal:
                    description.colorSpace = texture_tools::ImportedColorSpace::Linear;
                    description.profile = texture_tools::profiles::Normal;
                    break;
                case EmbeddedTextureClass::Masks:
                    description.colorSpace = texture_tools::ImportedColorSpace::Linear;
                    description.profile = texture_tools::profiles::Masks;
                    break;
                case EmbeddedTextureClass::Data:
                case EmbeddedTextureClass::Unset:
                    description.colorSpace = texture_tools::ImportedColorSpace::Linear;
                    description.profile = texture_tools::profiles::Data;
                    break;
                }
            }
            containers::DynamicArray<u8> settings(memory::pools::Assets::GetInstance());
            if (texture_tools::EncodeTextureBuildSettings(description, settings) != texture_tools::TextureBuildSettingsResult::Success)
                return false;
            const containers::String sourceName =
                containers::String::Printf("generated/model/embedded/%016llx", static_cast<unsigned long long>(context.requestedOutput.GetPath().Id()));
            const resources::ResourceReference source(resources::ResourcePath::FromString(sourceName.AsChar()),
                                                      texture_tools::TextureSourceResourceType);
            const assets::BuildRequest borrowed{{source, {embedded.bytes.TypedData(), embedded.bytes.Size()}, {}},
                                                context.requestedOutput,
                                                context.data.target,
                                                {settings.TypedData(), settings.Size()}};
            return request.CopyFrom(borrowed);
        }

        [[nodiscard]] inline const mesh_tools::ModelImportTextureResolution* FindTextureResolution(
            const mesh_tools::ModelImportSettings& settings, const containers::String& authoredPath) noexcept
        {
            for (const mesh_tools::ModelImportTextureResolution& resolution : settings.textureResolutions)
                if (resolution.authoredPath == authoredPath)
                    return &resolution;
            return nullptr;
        }

        [[nodiscard]] inline bool MapImportedTextureSemantic(const mesh_tools::ImportedTextureSemantic semantic,
                                                      material_tools::ImportedSurfaceTextureSemantic& output) noexcept
        {
            switch (semantic)
            {
            case mesh_tools::ImportedTextureSemantic::BaseColor:
                output = material_tools::ImportedSurfaceTextureSemantic::BaseColor;
                return true;
            case mesh_tools::ImportedTextureSemantic::Normal:
                output = material_tools::ImportedSurfaceTextureSemantic::Normal;
                return true;
            case mesh_tools::ImportedTextureSemantic::Metalness:
                output = material_tools::ImportedSurfaceTextureSemantic::Metallic;
                return true;
            case mesh_tools::ImportedTextureSemantic::Roughness:
                output = material_tools::ImportedSurfaceTextureSemantic::Roughness;
                return true;
            case mesh_tools::ImportedTextureSemantic::MetallicRoughness:
                output = material_tools::ImportedSurfaceTextureSemantic::MetallicRoughness;
                return true;
            case mesh_tools::ImportedTextureSemantic::Specular:
                output = material_tools::ImportedSurfaceTextureSemantic::Specular;
                return true;
            case mesh_tools::ImportedTextureSemantic::Emissive:
                output = material_tools::ImportedSurfaceTextureSemantic::Emissive;
                return true;
            case mesh_tools::ImportedTextureSemantic::Occlusion:
                output = material_tools::ImportedSurfaceTextureSemantic::Occlusion;
                return true;
            case mesh_tools::ImportedTextureSemantic::Opacity:
                output = material_tools::ImportedSurfaceTextureSemantic::Opacity;
                return true;
            case mesh_tools::ImportedTextureSemantic::Unknown:
                return false;
            }
            return false;
        }

        [[nodiscard]] inline bool ResolveImportedMaterialTexture(const mesh_tools::ModelOutputBuildRequestContext& context,
                                                         const mesh_tools::ModelImportTextureReference& texture,
                                                         ModelBuildAdapter& adapter,
                                                         resources::ResourceReference& output,
                                                         bool& ignored) noexcept
        {
            output = {};
            ignored = false;
            if (context.settings == nullptr || context.manifest == nullptr || adapter.sources == nullptr)
                return false;

            if (texture.storage == mesh_tools::ImportedTextureStorage::Embedded)
                return texture.embeddedTextureIndex != mesh_tools::InvalidModelImportIndex &&
                       mesh_tools::ResolveModelEmbeddedTextureOutput(context, texture.embeddedTextureIndex, output) &&
                       output.IsTyped() && output.ExpectedType() == textures::TextureResourceType;

            if (texture.storage != mesh_tools::ImportedTextureStorage::ExternalSource || texture.authoredPath.Empty())
                return false;
            const mesh_tools::ModelImportTextureResolution* const resolution =
                FindTextureResolution(*context.settings, texture.authoredPath);
            if (resolution == nullptr)
                return false;

            switch (resolution->resolution)
            {
            case mesh_tools::ModelImportTextureResolutionKind::ReuseManagedResource:
                output = resolution->managedResource;
                return output.IsTyped() && output.ExpectedType() == textures::TextureResourceType &&
                       adapter.sources->LookupOutput(output) != nullptr;
            case mesh_tools::ModelImportTextureResolutionKind::AdoptProjectSource:
            {
                if (!resolution->projectSourceIdentity.IsValid())
                    return false;
                const assets::SourceRecord* const source = adapter.sources->LookupSource(resolution->projectSourceIdentity);
                if (source == nullptr || source->metadataResult != assets::MetadataResult::Success ||
                    source->importState != assets::SourceImportState::MetadataSelected)
                    return false;
                bool ambiguous = false;
                return assets::ResolveUniqueAssetOutputOfType(source->metadata, textures::TextureResourceType, output, ambiguous) &&
                       !ambiguous && output.IsTyped() && output.ExpectedType() == textures::TextureResourceType;
            }
            case mesh_tools::ModelImportTextureResolutionKind::IgnoreMissing:
                ignored = true;
                return texture.missing;
            case mesh_tools::ModelImportTextureResolutionKind::Unresolved:
                return false;
            }
            return false;
        }

        struct StaticSurfacePipelineRecipes final
        {
            pipelines::VertexStream streams[3]{};
            pipelines::VertexAttribute attributes[6]{};
            pipelines::BuildDescription depth;
            pipelines::BuildDescription opaque;
            pipelines::BuildDescription transparent;
        };

        [[nodiscard]] inline bool MakeStaticSurfacePipelineRecipes(const bool twoSided, StaticSurfacePipelineRecipes& output) noexcept
        {
            // Must stay byte-for-byte compatible with RuntimeStatic packing and
            // rendering/static_surface_prefix.vsl's StaticSurfaceVertex ABI.
            output = {};
            output.streams[0] = {0u, 8u, pipelines::InputRate::PerVertex, 1u};
            output.streams[1] = {1u, 12u, pipelines::InputRate::PerVertex, 1u};
            output.streams[2] = {15u, 16u, pipelines::InputRate::PerInstance, 1u};
            output.attributes[0] = {shaders::HashInterfaceName("POSITION"), 0u, 0u, 0u, 0u, shaders::NumericClass::FloatingPoint,
                                    4u, 16u, pipelines::Format::R16G16B16A16SNorm, "POSITION"};
            output.attributes[1] = {shaders::HashInterfaceName("NORMAL"), 0u, 1u, 1u, 0u, shaders::NumericClass::FloatingPoint,
                                    4u, 10u, pipelines::Format::R10G10B10A2UNorm, "NORMAL"};
            output.attributes[2] = {shaders::HashInterfaceName("TANGENT"), 0u, 2u, 1u, 4u, shaders::NumericClass::FloatingPoint,
                                    4u, 10u, pipelines::Format::R10G10B10A2UNorm, "TANGENT"};
            output.attributes[3] = {shaders::HashInterfaceName("TEXCOORD"), 0u, 3u, 1u, 8u, shaders::NumericClass::FloatingPoint,
                                    2u, 16u, pipelines::Format::R16G16Float, "TEXCOORD"};
            output.attributes[4] = {shaders::HashInterfaceName("VG_DRAW"), 0u, 4u, 15u, 0u, shaders::NumericClass::UnsignedInteger,
                                    2u, 32u, pipelines::Format::R32G32UInt, "VG_DRAW"};
            output.attributes[5] = {shaders::HashInterfaceName("VG_DRAW"), 1u, 5u, 15u, 8u, shaders::NumericClass::UnsignedInteger,
                                    2u, 32u, pipelines::Format::R32G32UInt, "VG_DRAW"};

            const auto initialize = [&](pipelines::BuildDescription& recipe, const u32 colorAttachments) noexcept
            {
                recipe.kind = pipelines::PipelineKind::Graphics;
                recipe.vertexStreams = {output.streams, 3u};
                recipe.vertexAttributes = {output.attributes, 6u};
                recipe.graphics.topology = pipelines::PrimitiveTopology::TriangleList;
                recipe.graphics.rasterizer.cull = twoSided ? pipelines::CullMode::None : pipelines::CullMode::Back;
                recipe.graphics.rasterizer.frontFace = pipelines::FrontFace::CounterClockwise;
                recipe.graphics.depthStencil.depthTest = true;
                recipe.graphics.depthStencil.depthWrite = true;
                recipe.graphics.depthStencil.depthCompare = pipelines::CompareOperation::GreaterEqual;
                recipe.graphics.multisample.sampleCount = 1u;
                recipe.graphics.blend.attachmentCount = colorAttachments;
                recipe.graphics.attachmentPolicy = pipelines::AttachmentPolicy::Deferred;
            };
            initialize(output.depth, 0u);
            // Opaque GGX consumes the full five-attachment GBuffer: base/AO,
            // normal/roughness, emissive/metallic, dielectric F0, view direction.
            initialize(output.opaque, 5u);
            initialize(output.transparent, 1u);
            output.transparent.graphics.depthStencil.depthWrite = false;
            auto& attachment = output.transparent.graphics.blend.attachments[0];
            attachment.blendEnable = true;
            attachment.sourceColor = pipelines::BlendFactor::SourceAlpha;
            attachment.destinationColor = pipelines::BlendFactor::OneMinusSourceAlpha;
            attachment.colorOperation = pipelines::BlendOperation::Add;
            attachment.sourceAlpha = pipelines::BlendFactor::One;
            attachment.destinationAlpha = pipelines::BlendFactor::OneMinusSourceAlpha;
            attachment.alphaOperation = pipelines::BlendOperation::Add;
            attachment.writeMask = 0x0fu;
            return true;
        }

        [[nodiscard]] inline bool ComposeMaterial(const mesh_tools::ModelOutputBuildRequestContext& context,
                                           assets::OwnedBuildRequest& request, void* const userData) noexcept
        {
            auto* const adapter = static_cast<ModelBuildAdapter*>(userData);
            if (adapter == nullptr || adapter->sources == nullptr || adapter->materialContext == nullptr || adapter->generated == nullptr ||
                context.manifest == nullptr || context.settings == nullptr || context.sourceIndex >= context.manifest->materials.Size() ||
                !context.requestedOutput.IsTyped() || context.requestedOutput.ExpectedType() != materials::MaterialResourceType ||
                !adapter->materialContext->IsInitialized() ||
                // Version 2 metadata may already have a durable reviewed source commit.
                // It had no successfully admitted specular or blend VMATs; compile its
                // authored inputs using the current versioned canonical intermediate IDs,
                // without silently rewriting reviewed VMETA or output identities.
                (context.settings->materialMapperVersion != 2u &&
                 context.settings->materialMapperVersion != material_tools::ImportedSurfaceMaterialMapperVersion))
                return false;

            const mesh_tools::ModelImportMaterial& source = context.manifest->materials[context.sourceIndex];

            containers::DynamicArray<material_tools::ImportedSurfaceTextureBinding> textures(memory::pools::Tools::GetInstance());
            textures.Reserve(source.textureReferenceIndices.Size());
            if (textures.Capacity() < source.textureReferenceIndices.Size())
                return false;
            for (const u32 textureIndex : source.textureReferenceIndices)
            {
                if (textureIndex >= context.manifest->textureReferences.Size())
                    return false;
                const mesh_tools::ModelImportTextureReference& texture = context.manifest->textureReferences[textureIndex];
                material_tools::ImportedSurfaceTextureSemantic semantic;
                if (!MapImportedTextureSemantic(texture.semantic, semantic))
                    return false;
                if (texture.semantic == mesh_tools::ImportedTextureSemantic::Specular &&
                    texture.specularTextureRole == mesh_tools::ModelImportTextureReference::SpecularTextureRole::FactorAlpha)
                    semantic = material_tools::ImportedSurfaceTextureSemantic::SpecularStrength;
                resources::ResourceReference resource;
                bool ignored = false;
                if (!ResolveImportedMaterialTexture(context, texture, *adapter, resource, ignored))
                    return false;
                if (ignored)
                    continue;
                material_tools::ImportedSurfaceTextureBinding& binding = textures.EmplaceBack();
                binding.semantic = semantic;
                binding.uvSet = texture.uvSet;
                binding.resource = resource;
                binding.dependency = resources::DependencyKind::Required;
            }

            material_tools::ImportedSurfaceMaterialInput material;
            std::memcpy(material.baseColor, source.baseColor, sizeof(material.baseColor));
            std::memcpy(material.emissive, source.emissiveColor, sizeof(material.emissive));
            std::memcpy(material.specular, source.specularColor, sizeof(material.specular));
            material.specularFactor = source.specularFactor;
            material.ior = source.indexOfRefraction;
            material.occlusionStrength = source.occlusionStrength;
            material.metallic = source.metallic;
            material.roughness = source.roughness;
            material.opacity = source.opacity;
            material.opacityCutoff = source.alphaCutoff;
            material.twoSided = source.twoSided;
            material.coverage = source.alphaMode == mesh_tools::ModelImportAlphaMode::Mask
                                    ? material_tools::ImportedSurfaceCoverageMode::Masked
                                    : source.alphaMode == mesh_tools::ModelImportAlphaMode::Blend
                                        ? material_tools::ImportedSurfaceCoverageMode::Blended
                                        : material_tools::ImportedSurfaceCoverageMode::Opaque;
            material.textures = {textures.TypedData(), textures.Size()};

            StaticSurfacePipelineRecipes recipes;
            if (!MakeStaticSurfacePipelineRecipes(source.twoSided, recipes))
                return false;
            material_tools::MaterialCanonicalBuildSet build;
            material_tools::ImportedSurfaceMappingWarning warnings = material_tools::ImportedSurfaceMappingWarning::None;
            material_tools::MaterialCanonicalDiagnostic diagnostic;
            if (material_tools::BuildImportedSurfaceCanonicalInputs(*adapter->materialContext, material, context.requestedOutput,
                                                                     recipes.depth, recipes.opaque, build, &warnings, &diagnostic,
                                                                     {}, {}, &recipes.transparent) !=
                    material_tools::MaterialCanonicalResult::Success ||
                !build.IsValid())
                return false;
            const material_tools::ImportedSurfaceMappingWarning unsupported =
                material_tools::ImportedSurfaceMappingWarning::SpecularUnsupported |
                material_tools::ImportedSurfaceMappingWarning::TextureSemanticUnsupported |
                material_tools::ImportedSurfaceMappingWarning::UvSetUnsupported;
            if ((static_cast<u32>(warnings) & static_cast<u32>(unsupported)) != 0u)
                return false;

            const containers::ArraySpan<const assets::BuildRequest> programs = build.Programs();
            const containers::ArraySpan<const assets::BuildRequest> pipelines = build.Pipelines();
            if (programs.Count() > MaximumGeneratedRequests || pipelines.Count() > MaximumGeneratedRequests - programs.Count())
                return false;
            containers::DynamicArray<assets::BuildRequest> generated(memory::pools::Tools::GetInstance());
            generated.Reserve(programs.Count() + pipelines.Count());
            if (generated.Capacity() < programs.Count() + pipelines.Count())
                return false;
            for (const assets::BuildRequest& program : programs)
                generated.PushBack(program);
            for (const assets::BuildRequest& pipeline : pipelines)
                generated.PushBack(pipeline);
            return generated.Size() == programs.Count() + pipelines.Count() &&
                   adapter->generated->PruneRetiredPublishers(*adapter->sources) &&
                   adapter->generated->Publish(context.requestedOutput, {generated.TypedData(), generated.Size()}) &&
                   request.CopyFrom(build.Material());
        }
    } // namespace detail

    using GeneratedRequestStore = detail::GeneratedRequestStore;
    using ModelBuildAdapter = detail::ModelBuildAdapter;

    // Pure capability check for reviewed authoring. The real build callbacks
    // may publish generated dependencies, so they cannot be used as speculative
    // pre-commit validation. Mirror only known, deterministic adapter limits;
    // ordinary build admission remains authoritative for all other failures.
    enum class ModelImportBuildability : u8
    {
        Supported,
        CompositionNeedsRigidUnitScale,
        DefaultMaterialReferenceRequired,
        UnsupportedBlendMaterial,
        UnsupportedSpecularMaterial,
        UnsupportedMaterialTextureSemantic,
        UnsupportedTextureUvSet
    };

    struct ModelImportBuildabilityFailure final
    {
        ModelImportBuildability reason = ModelImportBuildability::Supported;
        u32 sourceIndex = 0u;
    };

    [[nodiscard]] inline ModelImportBuildabilityFailure CheckReviewedModelBuildability(
        const mesh_tools::ModelImportManifest& manifest, const mesh_tools::ModelImportSettings& settings) noexcept
    {
        if (settings.includeComposition)
        {
            mesh_tools::ModelGeometryExtractionSettings conversion;
            conversion.uniformScale = settings.mesh.uniformScale;
            conversion.sourceUpAxis = settings.mesh.sourceUpAxis;
            for (u32 index = 0u; index < manifest.nodes.Size(); ++index)
            {
                mesh_tools::ModelImportRigidTransform transform;
                if (!mesh_tools::ConvertModelImportRigidLocalTransform(manifest.nodes[index], conversion, transform))
                    return {ModelImportBuildability::CompositionNeedsRigidUnitScale, index};
            }
        }
        if (settings.materialPolicy == mesh_tools::ModelImportMaterialPolicy::UseDefaultMaterial)
        {
            if (!settings.mesh.defaultMaterial.IsValid() || !settings.mesh.defaultMaterial.IsTyped() ||
                settings.mesh.defaultMaterial.ExpectedType() != materials::MaterialResourceType)
                return {ModelImportBuildability::DefaultMaterialReferenceRequired, 0u};
            return {};
        }
        for (u32 materialIndex = 0u; materialIndex < manifest.materials.Size(); ++materialIndex)
        {
            const mesh_tools::ModelImportMaterial& material = manifest.materials[materialIndex];
            for (const u32 textureIndex : material.textureReferenceIndices)
            {
                if (textureIndex >= manifest.textureReferences.Size())
                    return {ModelImportBuildability::UnsupportedMaterialTextureSemantic, materialIndex};
                const mesh_tools::ModelImportTextureReference& texture = manifest.textureReferences[textureIndex];
                if (texture.semantic == mesh_tools::ImportedTextureSemantic::Unknown)
                    return {ModelImportBuildability::UnsupportedMaterialTextureSemantic, materialIndex};
                if (texture.uvSet != 0u)
                    return {ModelImportBuildability::UnsupportedTextureUvSet, materialIndex};
            }
        }
        return {};
    }

    [[nodiscard]] inline bool ResolveGeneratedFallback(const assets::BuildDependency& dependency,
                                                       assets::OwnedBuildRequest& request, void* userData) noexcept
    {
        return detail::ResolveGeneratedFallback(dependency, request, userData);
    }

    [[nodiscard]] inline assets::CompilerDescriptor PrefabCompilerDescriptor() noexcept
    {
        return detail::PrefabCompilerDescriptor();
    }

    [[nodiscard]] inline bool ComposeComposition(const mesh_tools::ModelOutputBuildRequestContext& context,
                                                 assets::OwnedBuildRequest& request, void* userData) noexcept
    {
        return detail::ComposeComposition(context, request, userData);
    }

    [[nodiscard]] inline bool ComposeEmbeddedTexture(const mesh_tools::ModelOutputBuildRequestContext& context,
                                                     assets::OwnedBuildRequest& request, void* userData) noexcept
    {
        return detail::ComposeEmbeddedTexture(context, request, userData);
    }

    [[nodiscard]] inline bool ComposeMaterial(const mesh_tools::ModelOutputBuildRequestContext& context,
                                              assets::OwnedBuildRequest& request, void* userData) noexcept
    {
        return detail::ComposeMaterial(context, request, userData);
    }

    [[nodiscard]] inline bool ComposeModelCompositionBuildRequest(
        const mesh_tools::ModelOutputBuildRequestContext& context, assets::OwnedBuildRequest& request) noexcept
    {
        return detail::ComposeComposition(context, request, nullptr);
    }
} // namespace vanguard::model_tools
