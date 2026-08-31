#include <vanguard/rendering/pipeline_cache.hpp>

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::rendering
{
    namespace
    {
        inline constexpr u32 MaximumExportNameLength = 128;

        struct OwnedExportName
        {
            char exportName[MaximumExportNameLength]{};
        };

        struct CreationPayload
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);

            CreationPayload() noexcept
                : bindingLayouts(memory::pools::Rendering::GetInstance()), descriptorDomains(memory::pools::Rendering::GetInstance()),
                  rayTracingShaders(memory::pools::Rendering::GetInstance()), rayTracingShaderNames(memory::pools::Rendering::GetInstance()),
                  rayTracingHitGroups(memory::pools::Rendering::GetInstance()), rayTracingHitGroupNames(memory::pools::Rendering::GetInstance())
            {
            }

            concurrency::Atomic<u32> references{1};
            pipelines::PipelineKind kind = pipelines::PipelineKind::Graphics;
            rhi::GraphicsPipelineDesc graphics;
            rhi::ComputePipelineDesc compute;
            rhi::RayTracingPipelineDesc rayTracing;
            containers::DynamicArray<rhi::BindingLayoutRef> bindingLayouts;
            containers::DynamicArray<rhi::DescriptorDomainRef> descriptorDomains;
            containers::DynamicArray<rhi::RayTracingShaderDesc> rayTracingShaders;
            containers::DynamicArray<OwnedExportName> rayTracingShaderNames;
            containers::DynamicArray<rhi::RayTracingHitGroupDesc> rayTracingHitGroups;
            containers::DynamicArray<OwnedExportName> rayTracingHitGroupNames;

            [[nodiscard]] bool RetainReferences() noexcept
            {
                const auto retainShader = [](const rhi::ShaderRef shader) noexcept
                {
                    if (shader)
                        rhi::AddRef(shader);
                };
                for (const rhi::BindingLayoutRef layout : bindingLayouts)
                    rhi::AddRef(layout);
                for (const rhi::DescriptorDomainRef domain : descriptorDomains)
                    rhi::AddRef(domain);
                if (kind == pipelines::PipelineKind::Graphics)
                {
                    retainShader(graphics.vertexShader);
                    retainShader(graphics.hullShader);
                    retainShader(graphics.domainShader);
                    retainShader(graphics.geometryShader);
                    retainShader(graphics.pixelShader);
                }
                else if (kind == pipelines::PipelineKind::Compute)
                {
                    retainShader(compute.computeShader);
                }
                else
                {
                    for (const rhi::RayTracingShaderDesc& shader : rayTracingShaders)
                    {
                        retainShader(shader.shader);
                        if (shader.localBindingLayout)
                            rhi::AddRef(shader.localBindingLayout);
                    }
                    for (const rhi::RayTracingHitGroupDesc& group : rayTracingHitGroups)
                    {
                        retainShader(group.closestHitShader);
                        retainShader(group.anyHitShader);
                        retainShader(group.intersectionShader);
                        if (group.localBindingLayout)
                            rhi::AddRef(group.localBindingLayout);
                    }
                }
                return true;
            }

            void ReleaseReferences() noexcept
            {
                const auto releaseShader = [](rhi::ShaderRef& shader) noexcept { static_cast<void>(rhi::SafeRelease(shader)); };
                for (rhi::BindingLayoutRef& layout : bindingLayouts)
                    static_cast<void>(rhi::SafeRelease(layout));
                for (rhi::DescriptorDomainRef& domain : descriptorDomains)
                    static_cast<void>(rhi::SafeRelease(domain));
                if (kind == pipelines::PipelineKind::Graphics)
                {
                    releaseShader(graphics.vertexShader);
                    releaseShader(graphics.hullShader);
                    releaseShader(graphics.domainShader);
                    releaseShader(graphics.geometryShader);
                    releaseShader(graphics.pixelShader);
                }
                else if (kind == pipelines::PipelineKind::Compute)
                {
                    releaseShader(compute.computeShader);
                }
                else
                {
                    for (rhi::RayTracingShaderDesc& shader : rayTracingShaders)
                    {
                        releaseShader(shader.shader);
                        static_cast<void>(rhi::SafeRelease(shader.localBindingLayout));
                    }
                    for (rhi::RayTracingHitGroupDesc& group : rayTracingHitGroups)
                    {
                        releaseShader(group.closestHitShader);
                        releaseShader(group.anyHitShader);
                        releaseShader(group.intersectionShader);
                        static_cast<void>(rhi::SafeRelease(group.localBindingLayout));
                    }
                }
            }
        };

        struct CachedPipeline
        {
            VANGUARD_USE_MEMORY_POOL(memory::pools::Rendering);
            rhi::PipelineRef pipeline;
        };

        [[nodiscard]] bool CopyName(char* destination, const char* source) noexcept
        {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 index = 0;
            while (index + 1u < MaximumExportNameLength && source[index] != '\0')
            {
                destination[index] = source[index];
                ++index;
            }
            if (source[index] != '\0')
                return false;
            destination[index] = '\0';
            return true;
        }

        [[nodiscard]] bool CopyLayouts(CreationPayload& payload, const rhi::BindingLayoutRef* source, const u32 count) noexcept
        {
            if ((source == nullptr) != (count == 0) || count > rhi::MaximumBindingLayoutsPerPipeline)
                return false;
            payload.bindingLayouts.Resize(count);
            if (payload.bindingLayouts.Size() != count)
                return false;
            for (u32 index = 0; index < count; ++index)
            {
                if (!rhi::IsResourceReferenceValid(rhi::ResourceRef(source[index])))
                    return false;
                payload.bindingLayouts[index] = source[index];
            }
            return true;
        }

        [[nodiscard]] bool CopyDomains(CreationPayload& payload, const rhi::DescriptorDomainRef* source, const u32 count) noexcept
        {
            if ((source == nullptr) != (count == 0) || count > rhi::MaximumDescriptorDomainsPerPipeline)
                return false;
            payload.descriptorDomains.Resize(count);
            if (payload.descriptorDomains.Size() != count)
                return false;
            for (u32 index = 0; index < count; ++index)
            {
                if (!rhi::IsResourceReferenceValid(rhi::ResourceRef(source[index])))
                    return false;
                payload.descriptorDomains[index] = source[index];
            }
            return true;
        }

        [[nodiscard]] CreationPayload* CopyGraphics(const rhi::GraphicsPipelineDesc& source) noexcept
        {
            const rhi::ShaderRef shaders[] = {source.vertexShader, source.hullShader, source.domainShader, source.geometryShader, source.pixelShader};
            if (!source.vertexShader || (source.vertexLayout && !rhi::IsResourceReferenceValid(rhi::ResourceRef(source.vertexLayout))))
                return nullptr;
            for (const rhi::ShaderRef shader : shaders)
                if (shader && !rhi::IsResourceReferenceValid(rhi::ResourceRef(shader)))
                    return nullptr;
            CreationPayload* const payload = VANGUARD_NEW(CreationPayload);
            if (payload == nullptr)
                return nullptr;
            payload->kind = pipelines::PipelineKind::Graphics;
            payload->graphics = source;
            if (!CopyLayouts(*payload, source.bindingLayouts, source.bindingLayoutCount) ||
                !CopyDomains(*payload, source.descriptorDomains, source.descriptorDomainCount))
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            payload->graphics.bindingLayouts = payload->bindingLayouts.Empty() ? nullptr : payload->bindingLayouts.TypedData();
            payload->graphics.descriptorDomains = payload->descriptorDomains.Empty() ? nullptr : payload->descriptorDomains.TypedData();
            if (!payload->RetainReferences())
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            return payload;
        }

        [[nodiscard]] CreationPayload* CopyCompute(const rhi::ComputePipelineDesc& source) noexcept
        {
            if (!source.computeShader || !rhi::IsResourceReferenceValid(rhi::ResourceRef(source.computeShader)))
                return nullptr;
            CreationPayload* const payload = VANGUARD_NEW(CreationPayload);
            if (payload == nullptr)
                return nullptr;
            payload->kind = pipelines::PipelineKind::Compute;
            payload->compute = source;
            if (!CopyLayouts(*payload, source.bindingLayouts, source.bindingLayoutCount) ||
                !CopyDomains(*payload, source.descriptorDomains, source.descriptorDomainCount))
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            payload->compute.bindingLayouts = payload->bindingLayouts.Empty() ? nullptr : payload->bindingLayouts.TypedData();
            payload->compute.descriptorDomains = payload->descriptorDomains.Empty() ? nullptr : payload->descriptorDomains.TypedData();
            if (!payload->RetainReferences())
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            return payload;
        }

        [[nodiscard]] CreationPayload* CopyRayTracing(const rhi::RayTracingPipelineDesc& source) noexcept
        {
            if (source.shaders == nullptr || source.shaderCount == 0 || source.shaderCount > rhi::MaximumRayTracingShaders ||
                (source.hitGroups == nullptr) != (source.hitGroupCount == 0) || source.hitGroupCount > rhi::MaximumRayTracingHitGroups)
                return nullptr;
            for (u32 index = 0; index < source.shaderCount; ++index)
            {
                const rhi::RayTracingShaderDesc& shader = source.shaders[index];
                if (!rhi::IsResourceReferenceValid(rhi::ResourceRef(shader.shader)) ||
                    (shader.localBindingLayout && !rhi::IsResourceReferenceValid(rhi::ResourceRef(shader.localBindingLayout))))
                    return nullptr;
            }
            for (u32 index = 0; index < source.hitGroupCount; ++index)
            {
                const rhi::RayTracingHitGroupDesc& group = source.hitGroups[index];
                const rhi::ShaderRef shaders[] = {group.closestHitShader, group.anyHitShader, group.intersectionShader};
                for (const rhi::ShaderRef shader : shaders)
                    if (shader && !rhi::IsResourceReferenceValid(rhi::ResourceRef(shader)))
                        return nullptr;
                if (group.localBindingLayout && !rhi::IsResourceReferenceValid(rhi::ResourceRef(group.localBindingLayout)))
                    return nullptr;
            }
            CreationPayload* const payload = VANGUARD_NEW(CreationPayload);
            if (payload == nullptr)
                return nullptr;
            payload->kind = pipelines::PipelineKind::RayTracing;
            payload->rayTracing = source;
            if (!CopyLayouts(*payload, source.globalBindingLayouts, source.globalBindingLayoutCount) ||
                !CopyDomains(*payload, source.descriptorDomains, source.descriptorDomainCount))
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            payload->rayTracingShaders.Resize(source.shaderCount);
            payload->rayTracingShaderNames.Resize(source.shaderCount);
            payload->rayTracingHitGroups.Resize(source.hitGroupCount);
            payload->rayTracingHitGroupNames.Resize(source.hitGroupCount);
            if (payload->rayTracingShaders.Size() != source.shaderCount || payload->rayTracingShaderNames.Size() != source.shaderCount ||
                payload->rayTracingHitGroups.Size() != source.hitGroupCount || payload->rayTracingHitGroupNames.Size() != source.hitGroupCount)
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            for (u32 index = 0; index < source.shaderCount; ++index)
            {
                rhi::RayTracingShaderDesc& destination = payload->rayTracingShaders[index];
                destination = source.shaders[index];
                if (!CopyName(payload->rayTracingShaderNames[index].exportName, source.shaders[index].exportName))
                {
                    VANGUARD_DELETE(payload);
                    return nullptr;
                }
                destination.exportName = payload->rayTracingShaderNames[index].exportName;
            }
            for (u32 index = 0; index < source.hitGroupCount; ++index)
            {
                rhi::RayTracingHitGroupDesc& destination = payload->rayTracingHitGroups[index];
                destination = source.hitGroups[index];
                if (!CopyName(payload->rayTracingHitGroupNames[index].exportName, source.hitGroups[index].exportName))
                {
                    VANGUARD_DELETE(payload);
                    return nullptr;
                }
                destination.exportName = payload->rayTracingHitGroupNames[index].exportName;
            }
            payload->rayTracing.shaders = payload->rayTracingShaders.TypedData();
            payload->rayTracing.hitGroups = payload->rayTracingHitGroups.TypedData();
            payload->rayTracing.globalBindingLayouts = payload->bindingLayouts.Empty() ? nullptr : payload->bindingLayouts.TypedData();
            payload->rayTracing.descriptorDomains = payload->descriptorDomains.Empty() ? nullptr : payload->descriptorDomains.TypedData();
            if (!payload->RetainReferences())
            {
                VANGUARD_DELETE(payload);
                return nullptr;
            }
            return payload;
        }

        [[nodiscard]] bool RetainPayload(void* const address) noexcept
        {
            auto* const payload = static_cast<CreationPayload*>(address);
            if (payload == nullptr)
                return false;
            static_cast<void>(payload->references.Increment());
            return true;
        }

        void ReleasePayload(void* const address) noexcept
        {
            auto* const payload = static_cast<CreationPayload*>(address);
            if (payload == nullptr || payload->references.Decrement() != 0)
                return;
            payload->ReleaseReferences();
            VANGUARD_DELETE(payload);
        }

        [[nodiscard]] bool CreateNativePipeline(const pipelines::PipelineKind kind, const crypto::Digest256&, void* const address,
                                                pipeline_cache::NativePipeline& output, pipeline_cache::FailureEvidence& evidence, void*) noexcept
        {
            auto* const payload = static_cast<CreationPayload*>(address);
            if (payload == nullptr || payload->kind != kind)
            {
                evidence.failure = pipeline_cache::Failure::InvalidRequest;
                return false;
            }
            rhi::Failure failure;
            rhi::PipelineRef pipeline;
            if (kind == pipelines::PipelineKind::Graphics)
                pipeline = rhi::CreateGraphicsPipeline(payload->graphics, &failure);
            else if (kind == pipelines::PipelineKind::Compute)
                pipeline = rhi::CreateComputePipeline(payload->compute, &failure);
            else
                pipeline = rhi::CreateRayTracingPipeline(payload->rayTracing, &failure);
            if (!pipeline)
            {
                evidence.failure = pipeline_cache::Failure::BackendRejected;
                evidence.backendCode = failure.backendCode;
                static_cast<void>(CopyName(evidence.message, failure.message[0] != '\0' ? failure.message : "RHI rejected pipeline creation"));
                return false;
            }
            CachedPipeline* const cached = VANGUARD_NEW(CachedPipeline);
            if (cached == nullptr)
            {
                static_cast<void>(rhi::SafeRelease(pipeline));
                evidence.failure = pipeline_cache::Failure::InvalidNativeObject;
                return false;
            }
            cached->pipeline = pipeline;
            output.object = cached;
            output.backendType = static_cast<u64>(kind) + 1u;
            return true;
        }

        void DestroyNativePipeline(const pipeline_cache::NativePipeline pipeline, void*) noexcept
        {
            auto* const cached = static_cast<CachedPipeline*>(pipeline.object);
            if (cached == nullptr)
                return;
            static_cast<void>(rhi::SafeRelease(cached->pipeline));
            VANGUARD_DELETE(cached);
        }

        [[nodiscard]] pipeline_cache::CreationPayload MakePayload(CreationPayload& payload) noexcept
        {
            return {&payload, &RetainPayload, &ReleasePayload};
        }
    } // namespace

    bool PipelineRequest::IsValid() const noexcept
    {
        return m_request.IsValid();
    }
    PipelineRequest::operator bool() const noexcept
    {
        return IsValid();
    }
    pipeline_cache::State PipelineRequest::GetStatus() const noexcept
    {
        return m_request.GetStatus();
    }
    bool PipelineRequest::HasFinished() const noexcept
    {
        return m_request.HasFinished();
    }
    bool PipelineRequest::HasSucceeded() const noexcept
    {
        return m_request.HasSucceeded();
    }
    void PipelineRequest::Wait() const noexcept
    {
        m_request.Wait();
    }
    bool PipelineRequest::TryWait(const u32 timeoutMilliseconds) const noexcept
    {
        return m_request.TryWait(timeoutMilliseconds);
    }
    rhi::PipelineRef PipelineRequest::GetPipeline() const noexcept
    {
        const pipeline_cache::NativePipeline native = m_request.GetNativeObject();
        const auto* const cached = static_cast<const CachedPipeline*>(native.object);
        return cached != nullptr ? cached->pipeline : rhi::PipelineRef{};
    }
    pipeline_cache::FailureEvidence PipelineRequest::GetError() const noexcept
    {
        return m_request.GetError();
    }
    u64 PipelineRequest::GetGeneration() const noexcept
    {
        return m_request.GetGeneration();
    }
    void PipelineRequest::Reset() noexcept
    {
        m_request.Reset();
    }

    bool PipelineCache::Initialize(const pipeline_cache::Config& config) noexcept
    {
        if (!rhi::IsInitialized())
            return false;
        pipeline_cache::Backend backend;
        backend.create = &CreateNativePipeline;
        backend.destroy = &DestroyNativePipeline;
        backend.identity = 0x564752484950534full;
        return m_cache.Initialize(backend, config);
    }
    bool PipelineCache::Shutdown() noexcept
    {
        return m_cache.Shutdown();
    }
    bool PipelineCache::IsInitialized() const noexcept
    {
        return m_cache.IsInitialized();
    }

    pipeline_cache::Result PipelineCache::RequestGraphics(const crypto::Digest256& key, const rhi::GraphicsPipelineDesc& desc, PipelineRequest& output,
                                                          const pipeline_cache::Priority priority) noexcept
    {
        output.Reset();
        CreationPayload* const payload = CopyGraphics(desc);
        if (payload == nullptr)
            return pipeline_cache::Result::InvalidArgument;
        const pipeline_cache::Result result = m_cache.Request(key, pipelines::PipelineKind::Graphics, MakePayload(*payload), output.m_request, priority);
        ReleasePayload(payload);
        return result;
    }
    pipeline_cache::Result PipelineCache::RequestCompute(const crypto::Digest256& key, const rhi::ComputePipelineDesc& desc, PipelineRequest& output,
                                                         const pipeline_cache::Priority priority) noexcept
    {
        output.Reset();
        CreationPayload* const payload = CopyCompute(desc);
        if (payload == nullptr)
            return pipeline_cache::Result::InvalidArgument;
        const pipeline_cache::Result result = m_cache.Request(key, pipelines::PipelineKind::Compute, MakePayload(*payload), output.m_request, priority);
        ReleasePayload(payload);
        return result;
    }
    pipeline_cache::Result PipelineCache::RequestRayTracing(const crypto::Digest256& key, const rhi::RayTracingPipelineDesc& desc, PipelineRequest& output,
                                                            const pipeline_cache::Priority priority) noexcept
    {
        output.Reset();
        CreationPayload* const payload = CopyRayTracing(desc);
        if (payload == nullptr)
            return pipeline_cache::Result::InvalidArgument;
        const pipeline_cache::Result result = m_cache.Request(key, pipelines::PipelineKind::RayTracing, MakePayload(*payload), output.m_request, priority);
        ReleasePayload(payload);
        return result;
    }
    bool PipelineCache::Invalidate(const crypto::Digest256& key) noexcept
    {
        return m_cache.Invalidate(key);
    }
    u32 PipelineCache::InvalidateAll() noexcept
    {
        return m_cache.InvalidateAll();
    }
    void PipelineCache::WaitIdle() const noexcept
    {
        m_cache.WaitIdle();
    }
    bool PipelineCache::TryWaitIdle(const u32 timeoutMilliseconds) const noexcept
    {
        return m_cache.TryWaitIdle(timeoutMilliseconds);
    }
    pipeline_cache::Stats PipelineCache::GetStats() const noexcept
    {
        return m_cache.GetStats();
    }
} // namespace vanguard::rendering
