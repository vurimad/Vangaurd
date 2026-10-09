#include <vanguard/rendering/material_resource_resolver.hpp>

#include <vanguard/rendering/material_materializer.hpp>
#include <vanguard/rendering/texture_residency_runtime.hpp>

#include <vanguard/rhi/rhi.hpp>
#include <vanguard/serialization/serialization.hpp>

namespace vanguard::rendering::tests
{
    [[nodiscard]] bool RunMaterialResidencyRuntimeProof(MaterialMaterializer& materializer, GpuSceneRuntime& runtime, RenderCommandSystem& commands, RenderSceneManager& scenes,
                                                        const resources::ResourceHandle& mesh) noexcept;

    namespace
    {
        namespace gpu = vanguard::rhi;
        namespace rendering = vanguard::rendering;
        namespace resources = vanguard::resources;
        namespace shaders = vanguard::shaders;

        inline constexpr resources::ResourceTypeId TestMaterialBufferType = vanguard::serialization::MakeFourCC('T', 'B', 'U', 'F');
        inline constexpr resources::ResourceTypeId TestMaterialSamplerType = vanguard::serialization::MakeFourCC('T', 'S', 'M', 'P');
        inline constexpr resources::ResourceTypeId TestMaterialAccelerationStructureType = vanguard::serialization::MakeFourCC('T', 'A', 'S', 'R');
        inline constexpr resources::ResourceTypeId TestMaterialTextureType = vanguard::serialization::MakeFourCC('T', 'T', 'E', 'X');

        struct MaterialProviderHarness
        {
            rendering::MaterialResourceProviderResult result;
            vanguard::u64 nextToken = 1;
            vanguard::u32 begins = 0;
            vanguard::u32 polls = 0;
            vanguard::u32 cancellations = 0;
            vanguard::u32 abandonments = 0;
            vanguard::u32 releases = 0;
            gpu::ResidencyFenceSet lastReleaseFences;
        };

        [[nodiscard]] bool BeginMaterialProvider(void* const userData, const rendering::MaterialResourceProviderRequest&, rendering::MaterialResourceProviderToken& token, const char*&) noexcept
        {
            auto& provider = *static_cast<MaterialProviderHarness*>(userData);
            token.value = provider.nextToken++;
            ++provider.begins;
            return true;
        }

        [[nodiscard]] bool PollMaterialProvider(void* const userData, const rendering::MaterialResourceProviderToken, rendering::MaterialResourceProviderResult& result, const char*&) noexcept
        {
            auto& provider = *static_cast<MaterialProviderHarness*>(userData);
            ++provider.polls;
            result = provider.result;
            return true;
        }

        void CancelMaterialProvider(void* const userData, const rendering::MaterialResourceProviderToken) noexcept
        {
            ++static_cast<MaterialProviderHarness*>(userData)->cancellations;
        }

        void AbandonMaterialProvider(void* const userData, const rendering::MaterialResourceProviderToken) noexcept
        {
            ++static_cast<MaterialProviderHarness*>(userData)->abandonments;
        }

        void ReleaseMaterialProvider(void* const userData, const rendering::MaterialResourceProviderToken, const gpu::ResidencyFenceSet& safeAfter) noexcept
        {
            auto& provider = *static_cast<MaterialProviderHarness*>(userData);
            ++provider.releases;
            provider.lastReleaseFences = safeAfter;
        }

        template <typename T> [[nodiscard]] bool ReadGpuSceneElement(GpuSceneTables& tables, const u32 index, T& output, gpu::Failure& failure, const u64 commandName) noexcept
        {
            GpuSceneElementAddress address;
            if (!tables.Resolve<T>(index, address))
                return false;
            gpu::BufferDesc desc;
            desc.size = sizeof(T);
            desc.usage = gpu::BufferUsage::CopyDestination;
            desc.initialState = gpu::ResourceState::CopyDestination;
            desc.memoryType = gpu::MemoryType::Readback;
            gpu::Buffer readback(gpu::AdoptReference, gpu::CreateBuffer(desc, {}, &failure));
            const gpu::CommandListRef commandList = gpu::CreateCommandList(gpu::CommandListType::CopySync, commandName, &failure);
            const gpu::ResourceState shaderRead = gpu::ResourceState::ShaderResourceGraphics | gpu::ResourceState::ShaderResourceCompute;
            if (!readback || !commandList || !gpu::BindCommandList(commandList, &failure) || !gpu::TransitionBuffer(address.buffer, gpu::ResourceState::Unknown, gpu::ResourceState::CopySource, &failure) ||
                !gpu::CopyBuffer(readback, 0, address.buffer, address.byteOffset, sizeof(T), &failure) || !gpu::TransitionBuffer(address.buffer, gpu::ResourceState::Unknown, shaderRead, &failure))
                return false;
            gpu::UnbindCommandList();
            const gpu::CommandListRef submissions[]{commandList};
            gpu::GpuFence completion;
            if (!gpu::CloseAndSubmitCommandLists("material GPU Scene readback", submissions, gpu::CommandListSyncType::None, completion, &failure) ||
                !gpu::WaitForGpuFence(completion, 5'000'000'000ull, &failure))
                return false;
            const T* const mapped = static_cast<const T*>(gpu::LockBuffer(readback, 0, sizeof(T), &failure));
            if (mapped == nullptr)
                return false;
            output = *mapped;
            gpu::UnlockBuffer(readback);
            return true;
        }

        [[nodiscard]] bool SubmitMaterialRetirementFences(gpu::ResidencyFenceSet& fences, gpu::Failure& failure) noexcept
        {
            fences = {};
            const gpu::CommandListType types[]{gpu::CommandListType::Default, gpu::CommandListType::Compute, gpu::CommandListType::CopyAsync};
            for (u32 index = 0; index < 3; ++index)
            {
                const gpu::CommandListRef commandList = gpu::CreateCommandList(types[index], 0x4d41545245544952ull + index, &failure);
                if (!commandList || !gpu::BindCommandList(commandList, &failure))
                    return false;
                gpu::UnbindCommandList();
                const gpu::CommandListRef submissions[]{commandList};
                gpu::GpuFence completion;
                if (!gpu::CloseAndSubmitCommandLists("material retirement cutover", submissions, gpu::CommandListSyncType::None, completion, &failure))
                    return false;
                fences.Include(completion);
            }
            return true;
        }
    } // namespace

    bool RunMaterialResourceResolverProof(MaterialResourceResolver& materialResources, TextureResidencyRuntime& textures, GpuSceneRuntime& runtime, RenderCommandSystem& commands, RenderSceneManager& scenes,
                                          const resources::ResourceHandle& mesh, const rhi::DescriptorDomainRef resourceDescriptors, const rhi::DescriptorDomainRef samplerDescriptors) noexcept
    {
        MaterialResourceResolverFailure materialResourceFailure;
        gpu::Failure failure;
        gpu::BufferDesc materialBufferDesc;
        materialBufferDesc.size = 256;
        materialBufferDesc.structureStride = 16;
        materialBufferDesc.usage = gpu::BufferUsage::Structured | gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        gpu::Buffer materialBuffer(gpu::AdoptReference, gpu::CreateBuffer(materialBufferDesc, {}, &failure));
        gpu::BufferDesc typedMaterialBufferDesc;
        typedMaterialBufferDesc.size = 256;
        typedMaterialBufferDesc.usage = gpu::BufferUsage::ShaderResource | gpu::BufferUsage::UnorderedAccess;
        gpu::Buffer typedMaterialBuffer(gpu::AdoptReference, gpu::CreateBuffer(typedMaterialBufferDesc, {}, &failure));
        gpu::BufferDesc rawMaterialBufferDesc = typedMaterialBufferDesc;
        rawMaterialBufferDesc.usage = rawMaterialBufferDesc.usage | gpu::BufferUsage::Raw;
        gpu::Buffer rawMaterialBuffer(gpu::AdoptReference, gpu::CreateBuffer(rawMaterialBufferDesc, {}, &failure));
        gpu::SamplerStateDesc comparisonSamplerDesc;
        comparisonSamplerDesc.comparison = gpu::ComparisonFunction::LessEqual;
        gpu::SamplerState comparisonSampler(gpu::AdoptReference, gpu::RequestSamplerState(comparisonSamplerDesc, &failure));
        gpu::SamplerStateDesc filteringSamplerDesc;
        gpu::SamplerState filteringSampler(gpu::AdoptReference, gpu::RequestSamplerState(filteringSamplerDesc, &failure));
        MaterialProviderHarness bufferProvider;
        bufferProvider.result.state = rendering::MaterialResourceProviderState::Ready;
        bufferProvider.result.buffer = materialBuffer;
        bufferProvider.result.identity = 0x425546464552ull;
        bufferProvider.result.generation = 1;
        MaterialProviderHarness samplerProvider;
        samplerProvider.result.state = rendering::MaterialResourceProviderState::Ready;
        samplerProvider.result.sampler = comparisonSampler;
        samplerProvider.result.samplerDesc = comparisonSamplerDesc;
        samplerProvider.result.identity = 0x53414d504c4552ull;
        samplerProvider.result.generation = 1;
        MaterialProviderHarness accelerationStructureProvider;
        accelerationStructureProvider.result.state = rendering::MaterialResourceProviderState::Ready;
        accelerationStructureProvider.result.identity = 0x414343454cull;
        accelerationStructureProvider.result.generation = 1;
        MaterialProviderHarness alternateTextureProvider;
        alternateTextureProvider.result.state = rendering::MaterialResourceProviderState::Ready;
        alternateTextureProvider.result.texture = {123, 9};
        const rendering::MaterialResourceProviderDesc bufferProviderDesc{shaders::MaterialResourceKind::Buffer,
                                                                         TestMaterialBufferType,
                                                                         &BeginMaterialProvider,
                                                                         &PollMaterialProvider,
                                                                         &CancelMaterialProvider,
                                                                         &ReleaseMaterialProvider,
                                                                         &AbandonMaterialProvider,
                                                                         &bufferProvider};
        const rendering::MaterialResourceProviderDesc samplerProviderDesc{shaders::MaterialResourceKind::Sampler,
                                                                          TestMaterialSamplerType,
                                                                          &BeginMaterialProvider,
                                                                          &PollMaterialProvider,
                                                                          &CancelMaterialProvider,
                                                                          &ReleaseMaterialProvider,
                                                                          &AbandonMaterialProvider,
                                                                          &samplerProvider};
        const rendering::MaterialResourceProviderDesc accelerationStructureProviderDesc{shaders::MaterialResourceKind::AccelerationStructure,
                                                                                        TestMaterialAccelerationStructureType,
                                                                                        &BeginMaterialProvider,
                                                                                        &PollMaterialProvider,
                                                                                        &CancelMaterialProvider,
                                                                                        &ReleaseMaterialProvider,
                                                                                        &AbandonMaterialProvider,
                                                                                        &accelerationStructureProvider};
        const rendering::MaterialResourceProviderDesc alternateTextureProviderDesc{shaders::MaterialResourceKind::Texture,
                                                                                   TestMaterialTextureType,
                                                                                   &BeginMaterialProvider,
                                                                                   &PollMaterialProvider,
                                                                                   &CancelMaterialProvider,
                                                                                   &ReleaseMaterialProvider,
                                                                                   &AbandonMaterialProvider,
                                                                                   &alternateTextureProvider};
        shaders::MaterialResourceShape readBufferShape;
        readBufferShape.access = shaders::MaterialResourceAccess::Read;
        readBufferShape.bufferKind = shaders::MaterialBufferKind::Structured;
        readBufferShape.elementStride = 16;
        shaders::MaterialResourceShape writeBufferShape = readBufferShape;
        writeBufferShape.access = shaders::MaterialResourceAccess::ReadWrite;
        shaders::MaterialResourceShape typedBufferShape;
        typedBufferShape.access = shaders::MaterialResourceAccess::Read;
        typedBufferShape.bufferKind = shaders::MaterialBufferKind::Typed;
        typedBufferShape.scalarType = shaders::ScalarType::F32;
        typedBufferShape.componentCount = 4;
        shaders::MaterialResourceShape rawBufferShape;
        rawBufferShape.access = shaders::MaterialResourceAccess::Read;
        rawBufferShape.bufferKind = shaders::MaterialBufferKind::ByteAddress;
        shaders::MaterialResourceShape samplerShape;
        samplerShape.access = shaders::MaterialResourceAccess::Read;
        samplerShape.samplerKind = shaders::MaterialSamplerKind::Comparison;
        shaders::MaterialResourceShape filteringSamplerShape = samplerShape;
        filteringSamplerShape.samplerKind = shaders::MaterialSamplerKind::Filtering;
        shaders::MaterialResourceShape accelerationStructureShape;
        accelerationStructureShape.access = shaders::MaterialResourceAccess::Read;
        shaders::MaterialResourceShape alternateTextureShape;
        alternateTextureShape.access = shaders::MaterialResourceAccess::Read;
        alternateTextureShape.textureDimension = shaders::MaterialTextureDimension::D3;
        alternateTextureShape.componentCount = 1;
        vanguard::crypto::Digest256 bufferFingerprint;
        bufferFingerprint.bytes[0] = 2;
        vanguard::crypto::Digest256 samplerFingerprint;
        samplerFingerprint.bytes[0] = 3;
        vanguard::crypto::Digest256 accelerationStructureFingerprint;
        accelerationStructureFingerprint.bytes[0] = 4;
        vanguard::crypto::Digest256 alternateTextureFingerprint;
        alternateTextureFingerprint.bytes[0] = 5;
        const rendering::MaterialResourceFallbackDesc readBufferFallback{shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, readBufferShape, {}};
        const rendering::MaterialResourceFallbackDesc writeBufferFallback{shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, writeBufferShape, {}};
        const rendering::MaterialResourceFallbackDesc typedBufferFallback{shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, typedBufferShape, {}};
        const rendering::MaterialResourceFallbackDesc rawBufferFallback{shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, rawBufferShape, {}};
        const rendering::MaterialResourceFallbackDesc samplerFallback{shaders::MaterialResourceKind::Sampler, TestMaterialSamplerType, samplerFingerprint, samplerShape, {}};
        const rendering::MaterialResourceFallbackDesc filteringSamplerFallback{shaders::MaterialResourceKind::Sampler, TestMaterialSamplerType, samplerFingerprint, filteringSamplerShape, {}};
        const rendering::MaterialResourceFallbackDesc accelerationStructureFallback{
            shaders::MaterialResourceKind::AccelerationStructure, TestMaterialAccelerationStructureType, accelerationStructureFingerprint, accelerationStructureShape, {}};
        const rendering::MaterialResourceFallbackDesc alternateTextureFallback{shaders::MaterialResourceKind::Texture, TestMaterialTextureType, alternateTextureFingerprint, alternateTextureShape, {}};
        if (!materialBuffer || !typedMaterialBuffer || !rawMaterialBuffer || !comparisonSampler || !filteringSampler || !materialResources.RegisterProvider(bufferProviderDesc, &materialResourceFailure) ||
            !materialResources.RegisterProvider(samplerProviderDesc, &materialResourceFailure) || !materialResources.RegisterProvider(accelerationStructureProviderDesc, &materialResourceFailure) ||
            !materialResources.RegisterProvider(alternateTextureProviderDesc, &materialResourceFailure) || !materialResources.RegisterFallback(readBufferFallback, &materialResourceFailure) ||
            !materialResources.RegisterFallback(writeBufferFallback, &materialResourceFailure) || !materialResources.RegisterFallback(typedBufferFallback, &materialResourceFailure) ||
            !materialResources.RegisterFallback(rawBufferFallback, &materialResourceFailure) || !materialResources.RegisterFallback(samplerFallback, &materialResourceFailure) ||
            !materialResources.RegisterFallback(filteringSamplerFallback, &materialResourceFailure) || !materialResources.RegisterFallback(accelerationStructureFallback, &materialResourceFailure))
            return false;
        if (!materialResources.RegisterFallback(alternateTextureFallback, &materialResourceFailure))
            return false;

        const auto MakeFallbackRequest =
            [](const shaders::MaterialResourceKind kind, const resources::ResourceTypeId type, const vanguard::crypto::Digest256& fingerprint, const shaders::MaterialResourceShape& shape) noexcept
        {
            rendering::MaterialResourceResolveRequest request;
            request.role.name = 0x66616c6c6261636bull;
            request.role.kind = kind;
            request.role.typeFingerprint = fingerprint;
            request.role.shape = shape;
            request.expectedAssetType = type;
            request.dependency = resources::DependencyKind::Optional;
            return request;
        };
        const rendering::MaterialResourceResolveRequest readBufferRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, readBufferShape);
        const rendering::MaterialResourceResolveRequest samplerRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Sampler, TestMaterialSamplerType, samplerFingerprint, samplerShape);
        const rendering::MaterialResourceResolveRequest writeBufferRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, writeBufferShape);
        const rendering::MaterialResourceResolveRequest typedBufferRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, typedBufferShape);
        const rendering::MaterialResourceResolveRequest rawBufferRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Buffer, TestMaterialBufferType, bufferFingerprint, rawBufferShape);
        const rendering::MaterialResourceResolveRequest accelerationStructureRequest =
            MakeFallbackRequest(shaders::MaterialResourceKind::AccelerationStructure, TestMaterialAccelerationStructureType, accelerationStructureFingerprint, accelerationStructureShape);
        const rendering::MaterialResourceResolveRequest alternateTextureRequest =
            MakeFallbackRequest(shaders::MaterialResourceKind::Texture, TestMaterialTextureType, alternateTextureFingerprint, alternateTextureShape);
        rendering::MaterialResourceResolveTicket firstBufferTicket;
        rendering::MaterialResourceResolveTicket secondBufferTicket;
        rendering::MaterialResourceResolveTicket samplerTicket;
        rendering::MaterialResourceReference firstBufferReference;
        rendering::MaterialResourceReference secondBufferReference;
        rendering::MaterialResourceReference samplerReference;
        if (!materialResources.Begin(readBufferRequest, firstBufferTicket, &materialResourceFailure) || !materialResources.Begin(readBufferRequest, secondBufferTicket, &materialResourceFailure) ||
            materialResources.Poll(firstBufferTicket, firstBufferReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            materialResources.Poll(secondBufferTicket, secondBufferReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            firstBufferReference.GetIdentity() != secondBufferReference.GetIdentity() || !materialResources.Begin(samplerRequest, samplerTicket, &materialResourceFailure) ||
            materialResources.Poll(samplerTicket, samplerReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            samplerReference.GetGpuResource().samplerDescriptor == rendering::InvalidGpuDescriptorIndex || samplerReference.GetIdentity().descriptorDomain != gpu::DescriptorDomainKind::Samplers)
            return false;

        bufferProvider.result.identity = 0x425546464553ull;
        rendering::MaterialResourceResolveTicket exhaustedTicket;
        rendering::MaterialResourceReference exhaustedReference;
        if (!materialResources.Begin(readBufferRequest, exhaustedTicket, &materialResourceFailure) ||
            materialResources.Poll(exhaustedTicket, exhaustedReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Failed ||
            materialResourceFailure.code != rendering::MaterialResourceResolverFailureCode::CapacityExceeded || exhaustedReference.IsValid())
            return false;
        bufferProvider.result.identity = 0x425546464552ull;

        samplerReference.Reset();
        rendering::MaterialResourceResolveTicket sharedWritableTicket;
        rendering::MaterialResourceReference writableReference;
        if (!materialResources.Begin(writeBufferRequest, sharedWritableTicket, &materialResourceFailure) ||
            materialResources.Poll(sharedWritableTicket, writableReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Failed ||
            materialResourceFailure.code != rendering::MaterialResourceResolverFailureCode::UnsupportedShape || writableReference.IsValid())
            return false;
        bufferProvider.result.exclusive = true;
        rendering::MaterialResourceResolveTicket privateWritableTicket;
        if (!materialResources.Begin(writeBufferRequest, privateWritableTicket, &materialResourceFailure) ||
            materialResources.Poll(privateWritableTicket, writableReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready || writableReference.GetGpuResource().flags != 0)
            return false;
        bufferProvider.result.exclusive = false;

        rendering::MaterialResourceResolveTicket accelerationStructureTicket;
        rendering::MaterialResourceReference accelerationStructureReference;
        if (!materialResources.Begin(accelerationStructureRequest, accelerationStructureTicket, &materialResourceFailure) ||
            materialResources.Poll(accelerationStructureTicket, accelerationStructureReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Failed ||
            materialResourceFailure.code != rendering::MaterialResourceResolverFailureCode::UnsupportedShape || accelerationStructureReference.IsValid())
            return false;
        rendering::MaterialResourceResolveTicket alternateTextureTicket;
        rendering::MaterialResourceReference alternateTextureReference;
        alternateTextureProvider.result.state = rendering::MaterialResourceProviderState::Pending;
        if (!materialResources.Begin(alternateTextureRequest, alternateTextureTicket, &materialResourceFailure) ||
            materialResources.Poll(alternateTextureTicket, alternateTextureReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Pending || !alternateTextureTicket.IsValid() ||
            alternateTextureReference.IsValid())
            return false;
        const vanguard::u32 cancellationsBeforeExplicitCancel = alternateTextureProvider.cancellations;
        if (!materialResources.Cancel(alternateTextureTicket, &materialResourceFailure) || alternateTextureTicket.IsValid() || alternateTextureProvider.cancellations != cancellationsBeforeExplicitCancel + 1u ||
            !materialResources.Begin(alternateTextureRequest, alternateTextureTicket, &materialResourceFailure) ||
            materialResources.Poll(alternateTextureTicket, alternateTextureReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Pending)
            return false;
        alternateTextureProvider.result.state = rendering::MaterialResourceProviderState::Ready;
        if (materialResources.Poll(alternateTextureTicket, alternateTextureReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            alternateTextureReference.GetIdentity().index != 123 || alternateTextureReference.GetIdentity().generation != 9 || alternateTextureReference.GetGpuResource().resource != 123)
            return false;
        alternateTextureReference.Reset();
        rendering::MaterialResourceResolveRequest softBufferRequest = readBufferRequest;
        softBufferRequest.dependency = resources::DependencyKind::Soft;
        rendering::MaterialResourceResolveTicket softBufferTicket;
        rendering::MaterialResourceReference softBufferReference;
        if (!materialResources.Begin(softBufferRequest, softBufferTicket, &materialResourceFailure) ||
            materialResources.Poll(softBufferTicket, softBufferReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready)
            return false;
        softBufferReference.Reset();
        rendering::MaterialResourceResolveRequest missingRequired = readBufferRequest;
        missingRequired.dependency = resources::DependencyKind::Required;
        missingRequired.role.flags = shaders::MaterialResourceFlags::Required;
        rendering::MaterialResourceResolveTicket missingRequiredTicket;
        if (materialResources.Begin(missingRequired, missingRequiredTicket, &materialResourceFailure) || materialResourceFailure.code != rendering::MaterialResourceResolverFailureCode::DependencyUnavailable)
            return false;
        if (firstBufferReference.Retire({}, &materialResourceFailure) || materialResourceFailure.code != rendering::MaterialResourceResolverFailureCode::MissingRetirementFence ||
            !firstBufferReference.IsValid())
            return false;

        const rendering::MaterialResourceResolvedIdentity firstDescriptorIdentity = firstBufferReference.GetIdentity();
        gpu::ResidencyFenceSet materialReferenceFences;
        materialReferenceFences.graphics = 1;
        materialReferenceFences.compute = 1;
        materialReferenceFences.copy = 1;
        if (!firstBufferReference.Retire(materialReferenceFences, &materialResourceFailure) || firstBufferReference.IsValid() || bufferProvider.lastReleaseFences.graphics != 1 ||
            bufferProvider.lastReleaseFences.compute != 1 || bufferProvider.lastReleaseFences.copy != 1)
            return false;
        secondBufferReference.Reset();
        writableReference.Reset();
        bufferProvider.result.identity = 0x425546464554ull;
        bufferProvider.result.generation = 2;
        rendering::MaterialResourceResolveTicket recycledTicket;
        rendering::MaterialResourceReference recycledReference;
        if (!materialResources.Begin(readBufferRequest, recycledTicket, &materialResourceFailure) ||
            materialResources.Poll(recycledTicket, recycledReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            (recycledReference.GetIdentity().index == firstDescriptorIdentity.index && recycledReference.GetIdentity().generation == firstDescriptorIdentity.generation))
            return false;
        recycledReference.Reset();
        bufferProvider.result.buffer = typedMaterialBuffer;
        bufferProvider.result.identity = 0x5459504544425546ull;
        rendering::MaterialResourceResolveTicket typedBufferTicket;
        rendering::MaterialResourceReference typedBufferReference;
        if (!materialResources.Begin(typedBufferRequest, typedBufferTicket, &materialResourceFailure) ||
            materialResources.Poll(typedBufferTicket, typedBufferReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            typedBufferReference.GetGpuResource().resource == rendering::InvalidGpuSceneIndex)
            return false;
        typedBufferReference.Reset();
        bufferProvider.result.buffer = rawMaterialBuffer;
        bufferProvider.result.identity = 0x5241574255464645ull;
        rendering::MaterialResourceResolveTicket rawBufferTicket;
        rendering::MaterialResourceReference rawBufferReference;
        if (!materialResources.Begin(rawBufferRequest, rawBufferTicket, &materialResourceFailure) ||
            materialResources.Poll(rawBufferTicket, rawBufferReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            rawBufferReference.GetGpuResource().resource == rendering::InvalidGpuSceneIndex)
            return false;
        rawBufferReference.Reset();
        samplerProvider.result.sampler = filteringSampler;
        samplerProvider.result.samplerDesc = filteringSamplerDesc;
        samplerProvider.result.identity = 0x46494c544552494eull;
        rendering::MaterialResourceResolveTicket filteringSamplerTicket;
        rendering::MaterialResourceReference filteringSamplerReference;
        const rendering::MaterialResourceResolveRequest filteringSamplerRequest = MakeFallbackRequest(shaders::MaterialResourceKind::Sampler, TestMaterialSamplerType, samplerFingerprint, filteringSamplerShape);
        if (!materialResources.Begin(filteringSamplerRequest, filteringSamplerTicket, &materialResourceFailure) ||
            materialResources.Poll(filteringSamplerTicket, filteringSamplerReference, &materialResourceFailure) != rendering::MaterialResourceResolveStatus::Ready ||
            filteringSamplerReference.GetGpuResource().samplerDescriptor == rendering::InvalidGpuDescriptorIndex)
            return false;
        filteringSamplerReference.Reset();
        bufferProvider.result.buffer = materialBuffer;
        bufferProvider.result.identity = 0x425546464552ull;
        bufferProvider.result.generation = 1;
        samplerProvider.result.sampler = comparisonSampler;
        samplerProvider.result.samplerDesc = comparisonSamplerDesc;
        samplerProvider.result.identity = 0x53414d504c4552ull;

        MaterialProgramLayoutRegistry materializerLayouts;
        MaterialProgramLayoutFailure materializerLayoutFailure;
        MaterialMaterializer materializer;
        MaterialMaterializerFailure materializerFailure;
        MaterialMaterializerConfig materializerConfig;
        materializerConfig.maximumOperations = 10;
        materializerConfig.maximumRolesPerMaterial = 16;
        materializerConfig.maximumMaterialsPerBatch = 6;
        if (!materializerLayouts.Initialize({gpu::BackendKind::D3D12, 2}, &materializerLayoutFailure) ||
            !materializer.Initialize(materializerLayouts, materialResources, runtime, materializerConfig, &materializerFailure) ||
            !RunMaterialResidencyRuntimeProof(materializer, runtime, commands, scenes, mesh) || !materializer.Shutdown(&materializerFailure))
            return false;

        MaterialResourceResolver abandonmentResolver;
        MaterialResourceResolverConfig abandonmentConfig;
        abandonmentConfig.maximumProviders = 2;
        abandonmentConfig.maximumFallbacks = 1;
        abandonmentConfig.maximumOperations = 1;
        abandonmentConfig.maximumDescriptorCacheEntries = 1;
        MaterialResourceResolveTicket abandonedTicket;
        MaterialResourceReference abandonedReference;
        if (!abandonmentResolver.Initialize(textures, resourceDescriptors, samplerDescriptors, abandonmentConfig, &materialResourceFailure) ||
            !abandonmentResolver.RegisterProvider(bufferProviderDesc, &materialResourceFailure) || !abandonmentResolver.RegisterFallback(readBufferFallback, &materialResourceFailure) ||
            !abandonmentResolver.Begin(readBufferRequest, abandonedTicket, &materialResourceFailure) ||
            abandonmentResolver.Poll(abandonedTicket, abandonedReference, &materialResourceFailure) != MaterialResourceResolveStatus::Ready || !abandonedReference.IsValid() ||
            !abandonmentResolver.AbandonDevice(&materialResourceFailure) || abandonedReference.IsValid() || bufferProvider.abandonments != 1 || !abandonmentResolver.Shutdown(&materialResourceFailure))
            return false;
        abandonedReference.Reset();
        if (!materializerLayouts.Shutdown(&materializerLayoutFailure))
            return false;

        const rendering::MaterialResourceResolverStats materialResourceStats = materialResources.GetStats();
        if (materialResourceStats.descriptorReuses == 0 || materialResourceStats.fallbacksSelected < 7 || materialResourceStats.cachedDescriptors != 0 || materialResourceStats.liveReferences != 1 ||
            bufferProvider.releases < 4 || bufferProvider.cancellations < 2)
            return false;
        return true;
    }
} // namespace vanguard::rendering::tests
