#include <vanguard/rendering/material_resource_resolver.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rhi/rhi.hpp>
#include <vanguard/textures/texture_resource.hpp>

#include <new>

namespace vanguard::rendering
{
    static_assert(static_cast<u32>(shaders::MaterialResourceKind::Texture) == static_cast<u32>(GpuMaterialResourceType::Texture));
    static_assert(static_cast<u32>(shaders::MaterialResourceKind::Buffer) == static_cast<u32>(GpuMaterialResourceType::Buffer));
    static_assert(static_cast<u32>(shaders::MaterialResourceKind::Sampler) == static_cast<u32>(GpuMaterialResourceType::Sampler));
    static_assert(static_cast<u32>(shaders::MaterialResourceKind::AccelerationStructure) == static_cast<u32>(GpuMaterialResourceType::AccelerationStructure));

    namespace
    {
        enum class OperationState : u8
        {
            Free,
            Pending,
            Leased
        };

        [[nodiscard]] constexpr bool IsWritable(const shaders::MaterialResourceShape& shape) noexcept
        {
            return shape.access == shaders::MaterialResourceAccess::Write || shape.access == shaders::MaterialResourceAccess::ReadWrite;
        }

        [[nodiscard]] constexpr bool CompleteCutover(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return fences.Covers(rhi::QueueType::Graphics) && fences.Covers(rhi::QueueType::Compute) && fences.Covers(rhi::QueueType::Copy);
        }

        [[nodiscard]] constexpr rhi::DescriptorRetirement DescriptorRetirement(const rhi::ResidencyFenceSet& fences) noexcept
        {
            return {fences.graphics, fences.compute, fences.copy};
        }

        void IncludeFences(rhi::ResidencyFenceSet& destination, const rhi::ResidencyFenceSet& source) noexcept
        {
            if (source.graphics > destination.graphics)
                destination.graphics = source.graphics;
            if (source.compute > destination.compute)
                destination.compute = source.compute;
            if (source.copy > destination.copy)
                destination.copy = source.copy;
        }

        [[nodiscard]] GpuMaterialResourceType GpuKind(const shaders::MaterialResourceKind kind) noexcept
        {
            return static_cast<GpuMaterialResourceType>(kind);
        }

        [[nodiscard]] rhi::Format TypedBufferFormat(const shaders::MaterialResourceShape& shape) noexcept
        {
            using Scalar = shaders::ScalarType;
            if (shape.scalarType == Scalar::F16)
            {
                if (shape.componentCount == 1)
                    return rhi::Format::R16Float;
                if (shape.componentCount == 2)
                    return rhi::Format::R16G16Float;
                if (shape.componentCount == 4)
                    return rhi::Format::R16G16B16A16Float;
            }
            if (shape.scalarType == Scalar::U16)
            {
                if (shape.componentCount == 1)
                    return rhi::Format::R16UInt;
                if (shape.componentCount == 2)
                    return rhi::Format::R16G16UInt;
                if (shape.componentCount == 4)
                    return rhi::Format::R16G16B16A16UInt;
            }
            if (shape.scalarType == Scalar::F32)
            {
                if (shape.componentCount == 1)
                    return rhi::Format::R32Float;
                if (shape.componentCount == 2)
                    return rhi::Format::R32G32Float;
                if (shape.componentCount == 3)
                    return rhi::Format::R32G32B32Float;
                if (shape.componentCount == 4)
                    return rhi::Format::R32G32B32A32Float;
            }
            if (shape.scalarType == Scalar::U32)
            {
                if (shape.componentCount == 1)
                    return rhi::Format::R32UInt;
                if (shape.componentCount == 2)
                    return rhi::Format::R32G32UInt;
            }
            return rhi::Format::Unknown;
        }

        [[nodiscard]] bool SameShape(const shaders::MaterialResourceShape& left, const shaders::MaterialResourceShape& right) noexcept
        {
            return left == right;
        }

        [[nodiscard]] bool TextureMetadataMatches(const textures::TextureFile& metadata, const shaders::MaterialResourceShape& shape) noexcept
        {
            using Dimension = shaders::MaterialTextureDimension;
            if (shape.access != shaders::MaterialResourceAccess::Read || shaders::HasFlag(shape.flags, shaders::MaterialResourceShapeFlags::Multisampled))
                return false;
            const bool dimensionMatches = (shape.textureDimension == Dimension::D1 && metadata.GetDimension() == textures::TextureDimension::Texture1D) ||
                                          (shape.textureDimension == Dimension::D2 && metadata.GetDimension() == textures::TextureDimension::Texture2D) ||
                                          (shape.textureDimension == Dimension::D3 && metadata.GetDimension() == textures::TextureDimension::Texture3D) ||
                                          (shape.textureDimension == Dimension::Cube && metadata.GetDimension() == textures::TextureDimension::Cube);
            if (!dimensionMatches)
                return false;
            const bool arrayed = metadata.GetArrayLayers() > 1;
            if (arrayed != shaders::HasFlag(shape.flags, shaders::MaterialResourceShapeFlags::Arrayed))
                return false;

            shaders::ScalarType scalar = shaders::ScalarType::F32;
            u8 components = 4;
            switch (metadata.Format())
            {
            case textures::PixelFormat::R8UInt:
                scalar = shaders::ScalarType::U32;
                components = 1;
                break;
            case textures::PixelFormat::R8G8UInt:
                scalar = shaders::ScalarType::U32;
                components = 2;
                break;
            case textures::PixelFormat::R8G8B8A8UInt:
                scalar = shaders::ScalarType::U32;
                components = 4;
                break;
            case textures::PixelFormat::R8UNorm:
            case textures::PixelFormat::R8SNorm:
            case textures::PixelFormat::R16UNorm:
            case textures::PixelFormat::R16SNorm:
            case textures::PixelFormat::R16Float:
            case textures::PixelFormat::R32Float:
            case textures::PixelFormat::BC4UNorm:
            case textures::PixelFormat::BC4SNorm:
                components = 1;
                break;
            case textures::PixelFormat::R8G8UNorm:
            case textures::PixelFormat::R8G8SNorm:
            case textures::PixelFormat::R16G16UNorm:
            case textures::PixelFormat::R16G16SNorm:
            case textures::PixelFormat::R16G16Float:
            case textures::PixelFormat::R32G32Float:
            case textures::PixelFormat::BC5UNorm:
            case textures::PixelFormat::BC5SNorm:
                components = 2;
                break;
            case textures::PixelFormat::R11G11B10Float:
            case textures::PixelFormat::R9G9B9E5SharedExponent:
            case textures::PixelFormat::BC6HUFloat:
            case textures::PixelFormat::BC6HSFloat:
                components = 3;
                break;
            default:
                components = 4;
                break;
            }
            return shape.scalarType == scalar && shape.componentCount == components;
        }
    } // namespace

    struct MaterialResourceResolver::Impl
    {
        struct Provider
        {
            MaterialResourceProviderDesc desc;
            bool builtinTexture = false;
            u32 liveOperations = 0;
        };

        struct Fallback
        {
            MaterialResourceFallbackDesc desc;
        };

        struct DescriptorRecord
        {
            bool active = false;
            shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Buffer;
            shaders::MaterialResourceShape shape;
            u64 providerIdentity = 0;
            u32 providerGeneration = 0;
            rhi::BufferViewDesc bufferView;
            rhi::DescriptorHandle descriptor;
            rhi::Buffer buffer;
            rhi::SamplerState sampler;
            rhi::AccelerationStructure accelerationStructure;
            rhi::ResidencyFenceSet retirementFloor;
            u32 references = 0;
            u64 cacheHash = 0;
            u32 nextHashCollision = InvalidMaterialResourceResolveIndex;
        };

        struct Operation
        {
            OperationState state = OperationState::Free;
            u32 generation = 0;
            u32 provider = InvalidMaterialResourceResolveIndex;
            MaterialResourceProviderToken providerToken;
            MaterialResourceResolveRequest request;
            TextureDemandHandle textureDemand;
            u32 descriptor = InvalidMaterialResourceResolveIndex;
            GpuMaterialResource gpuResource;
            MaterialResourceResolvedIdentity identity;
            bool fallback = false;
        };

        Impl() noexcept
            : providers(memory::pools::Rendering::GetInstance()), fallbacks(memory::pools::Rendering::GetInstance()), operations(memory::pools::Rendering::GetInstance()),
              recycledOperations(memory::pools::Rendering::GetInstance()), descriptors(memory::pools::Rendering::GetInstance()), recycledDescriptors(memory::pools::Rendering::GetInstance()),
              descriptorHeads(memory::pools::Rendering::GetInstance())
        {
        }

        containers::DynamicArray<Provider> providers;
        containers::DynamicArray<Fallback> fallbacks;
        containers::DynamicArray<Operation> operations;
        containers::DynamicArray<u32> recycledOperations;
        containers::DynamicArray<DescriptorRecord> descriptors;
        containers::DynamicArray<u32> recycledDescriptors;
        containers::HashMap<u64, u32> descriptorHeads;
        TextureResidencyRuntime* textures = nullptr;
        rhi::DescriptorDomain resourceDescriptors;
        rhi::DescriptorDomain samplerDescriptors;
        MaterialResourceResolverConfig config;
        MaterialResourceResolverStats stats;
    };

    namespace
    {
        void ClearFailure(MaterialResourceResolverFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MaterialResourceResolver::Impl* const impl, MaterialResourceResolverFailure* const failure, const MaterialResourceResolverFailureCode code, const char* const message,
                                const shaders::MaterialResourceKind kind = shaders::MaterialResourceKind::Texture, const resources::ResourceTypeId assetType = resources::InvalidResourceTypeId,
                                const rhi::Failure& rhiFailure = {}) noexcept
        {
            if (impl != nullptr)
                ++impl->stats.rejectedOperations;
            if (failure != nullptr)
                *failure = {code, message, kind, assetType, rhiFailure};
            return false;
        }

        [[nodiscard]] u32 FindProvider(const MaterialResourceResolver::Impl& impl, const shaders::MaterialResourceKind kind, const resources::ResourceTypeId assetType) noexcept
        {
            for (u32 index = 0; index < impl.providers.Size(); ++index)
                if (impl.providers[index].desc.kind == kind && impl.providers[index].desc.assetType == assetType)
                    return index;
            return InvalidMaterialResourceResolveIndex;
        }

        [[nodiscard]] const MaterialResourceResolver::Impl::Fallback* FindFallback(const MaterialResourceResolver::Impl& impl, const MaterialResourceResolveRequest& request) noexcept
        {
            for (const MaterialResourceResolver::Impl::Fallback& fallback : impl.fallbacks)
                if (fallback.desc.kind == request.role.kind && fallback.desc.assetType == request.expectedAssetType && fallback.desc.typeFingerprint == request.role.typeFingerprint &&
                    SameShape(fallback.desc.shape, request.role.shape))
                    return &fallback;
            return nullptr;
        }

        [[nodiscard]] MaterialResourceResolver::Impl::Operation* FindOperation(MaterialResourceResolver::Impl& impl, const MaterialResourceResolveTicket ticket) noexcept
        {
            if (!ticket.IsValid() || ticket.index >= impl.operations.Size())
                return nullptr;
            MaterialResourceResolver::Impl::Operation& operation = impl.operations[ticket.index];
            return operation.state != OperationState::Free && operation.generation == ticket.generation ? &operation : nullptr;
        }

        void RecycleOperation(MaterialResourceResolver::Impl& impl, const u32 index) noexcept
        {
            MaterialResourceResolver::Impl::Operation& operation = impl.operations[index];
            const u32 generation = operation.generation;
            operation = {};
            operation.generation = generation;
            impl.recycledOperations.PushBack(index);
            --impl.stats.activeOperations;
        }

        [[nodiscard]] bool StartOperation(MaterialResourceResolver::Impl& impl, MaterialResourceResolver::Impl::Operation& operation, const resources::ResourceHandle& resource, const bool fallback,
                                          MaterialResourceResolverFailure* const failure) noexcept
        {
            operation.fallback = fallback;
            operation.provider = FindProvider(impl, operation.request.role.kind, operation.request.expectedAssetType);
            if (operation.provider == InvalidMaterialResourceResolveIndex)
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderNotFound, "no material resource provider is registered for the typed role", operation.request.role.kind,
                            operation.request.expectedAssetType);
            MaterialResourceResolver::Impl::Provider& provider = impl.providers[operation.provider];
            ++provider.liveOperations;
            if (provider.builtinTexture)
            {
                if (!resource.IsValid() || resource.GetType() != textures::TextureResourceType)
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::DependencyUnavailable, "VTEX material role requires an exact loaded texture generation", operation.request.role.kind,
                                operation.request.expectedAssetType);
                const auto* const object = static_cast<const textures::TextureResourceObject*>(resource.Get());
                if (object == nullptr || !object->IsOpen() || !TextureMetadataMatches(object->GetMetadata(), operation.request.role.shape))
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ShapeMismatch, "VTEX metadata does not match the reflected texture role shape", operation.request.role.kind,
                                operation.request.expectedAssetType);
                TextureResidencyRuntimeFailure textureFailure;
                if (!impl.textures->RequestTexture(resource, operation.textureDemand, &textureFailure))
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, textureFailure.message != nullptr ? textureFailure.message : "texture residency request failed",
                                operation.request.role.kind, operation.request.expectedAssetType);
                return true;
            }
            MaterialResourceProviderRequest providerRequest{resource, operation.request.role, fallback};
            const char* message = nullptr;
            if (!provider.desc.begin(provider.desc.userData, providerRequest, operation.providerToken, message) || !operation.providerToken.IsValid())
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, message != nullptr ? message : "material resource provider begin failed", operation.request.role.kind,
                            operation.request.expectedAssetType);
            return true;
        }

        void CancelProvider(MaterialResourceResolver::Impl& impl, MaterialResourceResolver::Impl::Operation& operation) noexcept
        {
            if (operation.provider == InvalidMaterialResourceResolveIndex || operation.provider >= impl.providers.Size())
                return;
            MaterialResourceResolver::Impl::Provider& provider = impl.providers[operation.provider];
            if (provider.builtinTexture)
                operation.textureDemand.Reset();
            else if (operation.providerToken.IsValid())
                provider.desc.cancel(provider.desc.userData, operation.providerToken);
            operation.providerToken = {};
            --provider.liveOperations;
        }

        void AbandonProvider(MaterialResourceResolver::Impl& impl, MaterialResourceResolver::Impl::Operation& operation) noexcept
        {
            if (operation.provider == InvalidMaterialResourceResolveIndex || operation.provider >= impl.providers.Size())
                return;
            MaterialResourceResolver::Impl::Provider& provider = impl.providers[operation.provider];
            if (provider.builtinTexture)
                operation.textureDemand.Reset();
            else if (operation.providerToken.IsValid())
                provider.desc.abandon(provider.desc.userData, operation.providerToken);
            operation.providerToken = {};
            --provider.liveOperations;
        }

        [[nodiscard]] bool TryFallback(MaterialResourceResolver::Impl& impl, MaterialResourceResolver::Impl::Operation& operation, MaterialResourceResolverFailure* const failure) noexcept
        {
            if (operation.fallback || operation.request.dependency == resources::DependencyKind::Required)
                return false;
            const MaterialResourceResolver::Impl::Fallback* const fallback = FindFallback(impl, operation.request);
            if (fallback == nullptr)
                return false;
            CancelProvider(impl, operation);
            operation.provider = InvalidMaterialResourceResolveIndex;
            ++impl.stats.fallbacksSelected;
            ClearFailure(failure);
            return StartOperation(impl, operation, fallback->desc.resource, true, failure);
        }

        [[nodiscard]] bool SameDescriptor(const MaterialResourceResolver::Impl::DescriptorRecord& record, const MaterialResourceProviderResult& result, const shaders::MaterialResourceKind kind,
                                          const shaders::MaterialResourceShape& shape, const rhi::BufferViewDesc& view) noexcept
        {
            return record.active && record.kind == kind && record.shape == shape && record.providerIdentity == result.identity && record.providerGeneration == result.generation &&
                   record.bufferView.format == view.format && record.bufferView.offset == view.offset && record.bufferView.size == view.size && record.bufferView.structureStride == view.structureStride &&
                   record.buffer.GetRef() == result.buffer && record.sampler.GetRef() == result.sampler && record.accelerationStructure.GetRef() == result.accelerationStructure;
        }

        void HashU64(u64& hash, const u64 value) noexcept
        {
            for (u32 byte = 0; byte < sizeof(value); ++byte)
                hash = (hash ^ static_cast<u8>(value >> (byte * 8u))) * 0x100000001b3ull;
        }

        [[nodiscard]] u64 DescriptorHash(const MaterialResourceProviderResult& result, const shaders::MaterialResourceKind kind, const shaders::MaterialResourceShape& shape,
                                         const rhi::BufferViewDesc& view) noexcept
        {
            u64 hash = 0xcbf29ce484222325ull;
            HashU64(hash, static_cast<u8>(kind));
            HashU64(hash, result.identity);
            HashU64(hash, result.generation);
            HashU64(hash, static_cast<u8>(shape.access));
            HashU64(hash, static_cast<u8>(shape.textureDimension));
            HashU64(hash, static_cast<u8>(shape.bufferKind));
            HashU64(hash, static_cast<u8>(shape.samplerKind));
            HashU64(hash, static_cast<u8>(shape.scalarType));
            HashU64(hash, shape.componentCount);
            HashU64(hash, static_cast<u8>(shape.flags));
            HashU64(hash, shape.elementStride);
            HashU64(hash, static_cast<u64>(view.format));
            HashU64(hash, view.offset);
            HashU64(hash, view.size);
            HashU64(hash, view.structureStride);
            return hash;
        }

        void RemoveDescriptorIndex(MaterialResourceResolver::Impl& impl, const u32 index) noexcept
        {
            MaterialResourceResolver::Impl::DescriptorRecord& removed = impl.descriptors[index];
            u32* const head = impl.descriptorHeads.FindPtr(removed.cacheHash);
            if (head == nullptr)
                return;
            if (*head == index)
            {
                if (removed.nextHashCollision == InvalidMaterialResourceResolveIndex)
                    static_cast<void>(impl.descriptorHeads.Remove(removed.cacheHash));
                else
                    *head = removed.nextHashCollision;
                return;
            }
            u32 candidate = *head;
            while (candidate != InvalidMaterialResourceResolveIndex)
            {
                MaterialResourceResolver::Impl::DescriptorRecord& record = impl.descriptors[candidate];
                if (record.nextHashCollision == index)
                {
                    record.nextHashCollision = removed.nextHashCollision;
                    return;
                }
                candidate = record.nextHashCollision;
            }
        }

        [[nodiscard]] bool ValidateAndDescribe(MaterialResourceResolver::Impl& impl, const MaterialResourceResolver::Impl::Operation& operation, const MaterialResourceProviderResult& result,
                                               rhi::BindingType& binding, rhi::BufferViewDesc& view, MaterialResourceResolverFailure* const failure) noexcept
        {
            const shaders::MaterialResourceKind kind = operation.request.role.kind;
            const shaders::MaterialResourceShape& shape = operation.request.role.shape;
            const bool writable = IsWritable(shape);
            if (result.identity == 0 || result.generation == 0)
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, "ready material provider result has no stable identity", kind, operation.request.expectedAssetType);
            if (operation.fallback && writable && !result.exclusive)
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::UnsupportedShape, "writable fallback must be provider-private", kind, operation.request.expectedAssetType);
            if (kind == shaders::MaterialResourceKind::Buffer)
            {
                if (!result.buffer.IsValid() || result.texture.IsValid() || result.sampler.IsValid() || result.accelerationStructure.IsValid())
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, "buffer provider returned the wrong physical resource family", kind, operation.request.expectedAssetType);
                rhi::BufferDesc desc;
                rhi::Failure rhiFailure;
                if (!rhi::GetBufferDesc(result.buffer, desc, &rhiFailure))
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::DescriptorFailure, "buffer provider result description failed", kind, operation.request.expectedAssetType, rhiFailure);
                view = result.bufferView;
                if (view.offset > desc.size || view.size > desc.size - view.offset)
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ShapeMismatch, "buffer provider view is outside the physical buffer", kind, operation.request.expectedAssetType);
                if (view.size == 0)
                    view.size = desc.size - view.offset;
                if (shape.bufferKind == shaders::MaterialBufferKind::Typed)
                {
                    view.format = TypedBufferFormat(shape);
                    view.structureStride = 0;
                    if (view.format == rhi::Format::Unknown)
                        return Fail(&impl, failure, MaterialResourceResolverFailureCode::UnsupportedShape, "typed buffer role has no exact RHI view format", kind, operation.request.expectedAssetType);
                    binding = writable ? rhi::BindingType::TypedBufferUnorderedAccess : rhi::BindingType::TypedBufferShaderResource;
                }
                else if (shape.bufferKind == shaders::MaterialBufferKind::Structured)
                {
                    if ((desc.structureStride != 0 && desc.structureStride != shape.elementStride) || (view.structureStride != 0 && view.structureStride != shape.elementStride))
                        return Fail(&impl, failure, MaterialResourceResolverFailureCode::ShapeMismatch, "structured buffer stride disagrees with reflection", kind, operation.request.expectedAssetType);
                    view.format = rhi::Format::Unknown;
                    view.structureStride = shape.elementStride;
                    binding = writable ? rhi::BindingType::StructuredBufferUnorderedAccess : rhi::BindingType::StructuredBufferShaderResource;
                }
                else
                {
                    view.format = rhi::Format::Unknown;
                    view.structureStride = 0;
                    binding = writable ? rhi::BindingType::ByteAddressBufferUnorderedAccess : rhi::BindingType::ByteAddressBufferShaderResource;
                }
                return true;
            }
            if (kind == shaders::MaterialResourceKind::Sampler)
            {
                if (!result.sampler.IsValid() || result.texture.IsValid() || result.buffer.IsValid() || result.accelerationStructure.IsValid())
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, "sampler provider returned the wrong physical resource family", kind, operation.request.expectedAssetType);
                const bool comparison = result.samplerDesc.comparison != rhi::ComparisonFunction::Never;
                if (comparison != (shape.samplerKind == shaders::MaterialSamplerKind::Comparison))
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ShapeMismatch, "sampler comparison mode disagrees with reflection", kind, operation.request.expectedAssetType);
                binding = rhi::BindingType::Sampler;
                return true;
            }
            if (kind == shaders::MaterialResourceKind::AccelerationStructure)
            {
                if (!rhi::GetCapabilities().rayTracing)
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::UnsupportedShape, "active backend does not support acceleration structures", kind, operation.request.expectedAssetType);
                if (!result.accelerationStructure.IsValid() || result.texture.IsValid() || result.buffer.IsValid() || result.sampler.IsValid())
                    return Fail(&impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, "acceleration-structure provider returned the wrong physical resource family", kind,
                                operation.request.expectedAssetType);
                binding = rhi::BindingType::AccelerationStructure;
                return true;
            }
            return Fail(&impl, failure, MaterialResourceResolverFailureCode::InvalidArgument, "texture providers must return a stable texture residency identity", kind, operation.request.expectedAssetType);
        }

        [[nodiscard]] bool AcquireDescriptor(MaterialResourceResolver::Impl& impl, MaterialResourceResolver::Impl::Operation& operation, const MaterialResourceProviderResult& result,
                                             MaterialResourceResolverFailure* const failure) noexcept
        {
            rhi::BindingType binding = rhi::BindingType::TextureShaderResource;
            rhi::BufferViewDesc view;
            if (!ValidateAndDescribe(impl, operation, result, binding, view, failure))
                return false;
            const u64 cacheHash = DescriptorHash(result, operation.request.role.kind, operation.request.role.shape, view);
            u32 candidate = InvalidMaterialResourceResolveIndex;
            static_cast<void>(impl.descriptorHeads.Find(cacheHash, candidate));
            u32 collisionTail = InvalidMaterialResourceResolveIndex;
            while (candidate != InvalidMaterialResourceResolveIndex)
            {
                MaterialResourceResolver::Impl::DescriptorRecord& record = impl.descriptors[candidate];
                if (!SameDescriptor(record, result, operation.request.role.kind, operation.request.role.shape, view))
                {
                    collisionTail = candidate;
                    candidate = record.nextHashCollision;
                    continue;
                }
                ++record.references;
                ++impl.stats.descriptorReuses;
                operation.descriptor = candidate;
                operation.identity = {operation.request.role.kind,
                                      operation.request.role.kind == shaders::MaterialResourceKind::Sampler ? rhi::DescriptorDomainKind::Samplers : rhi::DescriptorDomainKind::Resources, record.descriptor.index,
                                      record.descriptor.generation};
                return true;
            }
            if (impl.stats.cachedDescriptors >= impl.config.maximumDescriptorCacheEntries)
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material descriptor cache capacity exceeded", operation.request.role.kind,
                            operation.request.expectedAssetType);

            const rhi::DescriptorDomainRef domain = operation.request.role.kind == shaders::MaterialResourceKind::Sampler ? impl.samplerDescriptors.GetRef() : impl.resourceDescriptors.GetRef();
            rhi::Failure rhiFailure;
            rhi::DescriptorHandle descriptor = rhi::AllocateDescriptor(domain, &rhiFailure);
            if (!descriptor.IsValid())
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::DescriptorFailure, "material descriptor allocation failed", operation.request.role.kind, operation.request.expectedAssetType,
                            rhiFailure);
            bool written = false;
            if (operation.request.role.kind == shaders::MaterialResourceKind::Buffer)
                written = rhi::WriteDescriptor(domain, descriptor, result.buffer, binding, view, &rhiFailure);
            else if (operation.request.role.kind == shaders::MaterialResourceKind::Sampler)
                written = rhi::WriteDescriptor(domain, descriptor, result.sampler, &rhiFailure);
            else
                written = rhi::WriteDescriptor(domain, descriptor, result.accelerationStructure, &rhiFailure);
            if (!written)
            {
                static_cast<void>(rhi::RetireDescriptor(domain, descriptor, {}));
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::DescriptorFailure, "material descriptor write failed", operation.request.role.kind, operation.request.expectedAssetType,
                            rhiFailure);
            }

            u32 index = InvalidMaterialResourceResolveIndex;
            if (!impl.recycledDescriptors.Empty())
            {
                index = impl.recycledDescriptors.Back();
                impl.recycledDescriptors.PopBack();
            }
            else
            {
                index = impl.descriptors.Size();
                impl.descriptors.PushBack({});
            }
            MaterialResourceResolver::Impl::DescriptorRecord& record = impl.descriptors[index];
            record.active = true;
            record.kind = operation.request.role.kind;
            record.shape = operation.request.role.shape;
            record.providerIdentity = result.identity;
            record.providerGeneration = result.generation;
            record.bufferView = view;
            record.descriptor = descriptor;
            record.references = 1;
            record.cacheHash = cacheHash;
            record.nextHashCollision = InvalidMaterialResourceResolveIndex;
            if (result.buffer.IsValid())
                record.buffer.Reset(result.buffer);
            if (result.sampler.IsValid())
                record.sampler.Reset(result.sampler);
            if (result.accelerationStructure.IsValid())
                record.accelerationStructure.Reset(result.accelerationStructure);
            if (collisionTail != InvalidMaterialResourceResolveIndex)
                impl.descriptors[collisionTail].nextHashCollision = index;
            else if (!impl.descriptorHeads.Insert(cacheHash, index).IsSuccessful())
            {
                static_cast<void>(rhi::RetireDescriptor(domain, descriptor, {}));
                record = {};
                impl.recycledDescriptors.PushBack(index);
                return Fail(&impl, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material descriptor index allocation failed", operation.request.role.kind,
                            operation.request.expectedAssetType);
            }
            ++impl.stats.cachedDescriptors;
            operation.descriptor = index;
            operation.identity = {operation.request.role.kind, operation.request.role.kind == shaders::MaterialResourceKind::Sampler ? rhi::DescriptorDomainKind::Samplers : rhi::DescriptorDomainKind::Resources,
                                  descriptor.index, descriptor.generation};
            return true;
        }
    } // namespace

    MaterialResourceReference::~MaterialResourceReference()
    {
        Reset();
    }

    MaterialResourceReference::MaterialResourceReference(MaterialResourceReference&& other) noexcept
        : m_owner(other.m_owner), m_index(other.m_index), m_generation(other.m_generation), m_gpuResource(other.m_gpuResource), m_identity(other.m_identity)
    {
        other.m_owner = nullptr;
        other.m_index = InvalidMaterialResourceResolveIndex;
        other.m_generation = 0;
    }

    MaterialResourceReference& MaterialResourceReference::operator=(MaterialResourceReference&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_owner = other.m_owner;
        m_index = other.m_index;
        m_generation = other.m_generation;
        m_gpuResource = other.m_gpuResource;
        m_identity = other.m_identity;
        other.m_owner = nullptr;
        other.m_index = InvalidMaterialResourceResolveIndex;
        other.m_generation = 0;
        return *this;
    }

    bool MaterialResourceReference::IsValid() const noexcept
    {
        return m_owner != nullptr && m_owner->IsReferenceValid(m_index, m_generation);
    }

    void MaterialResourceReference::Abandon() noexcept
    {
        if (m_owner != nullptr)
            m_owner->AbandonReference(m_index, m_generation);
        m_owner = nullptr;
        m_index = InvalidMaterialResourceResolveIndex;
        m_generation = 0;
        m_gpuResource = {};
        m_identity = {};
    }

    const GpuMaterialResource& MaterialResourceReference::GetGpuResource() const noexcept
    {
        return m_gpuResource;
    }

    MaterialResourceResolvedIdentity MaterialResourceReference::GetIdentity() const noexcept
    {
        return m_identity;
    }

    void MaterialResourceReference::Reset() noexcept
    {
        if (m_owner != nullptr)
            static_cast<void>(m_owner->ReleaseReference(m_index, m_generation, {}, false));
        m_owner = nullptr;
        m_index = InvalidMaterialResourceResolveIndex;
        m_generation = 0;
        m_gpuResource = {};
        m_identity = {};
    }

    bool MaterialResourceReference::Retire(const rhi::ResidencyFenceSet& safeAfter, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!IsValid())
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::InvalidArgument, "material resource reference is invalid");
        if (!m_owner->ReleaseReference(m_index, m_generation, safeAfter, true, failure))
            return false;
        m_owner = nullptr;
        m_index = InvalidMaterialResourceResolveIndex;
        m_generation = 0;
        m_gpuResource = {};
        m_identity = {};
        return true;
    }

    MaterialResourceResolver::~MaterialResourceResolver()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool MaterialResourceResolver::Initialize(TextureResidencyRuntime& textures, const rhi::DescriptorDomainRef resourceDescriptors, const rhi::DescriptorDomainRef samplerDescriptors,
                                              const MaterialResourceResolverConfig& config, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::AlreadyInitialized, "material resource resolver is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::WrongThread, "material resource resolver must initialize on the main thread");
        if (!textures.IsInitialized() || !resourceDescriptors.IsValid() || !samplerDescriptors.IsValid())
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::InvalidArgument, "material resource resolver dependencies are invalid");
        if (config.maximumProviders == 0 || config.maximumFallbacks == 0 || config.maximumOperations == 0 || config.maximumDescriptorCacheEntries == 0)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::InvalidConfiguration, "material resource resolver configuration is invalid");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material resource resolver allocation failed");
        m_impl = ::new (block.address) Impl();
        m_impl->textures = &textures;
        m_impl->resourceDescriptors.Reset(resourceDescriptors);
        m_impl->samplerDescriptors.Reset(samplerDescriptors);
        m_impl->config = config;
        m_impl->providers.Reserve(config.maximumProviders);
        m_impl->fallbacks.Reserve(config.maximumFallbacks);
        m_impl->operations.Reserve(config.maximumOperations);
        m_impl->descriptors.Reserve(config.maximumDescriptorCacheEntries);
        Impl::Provider textureProvider;
        textureProvider.desc.kind = shaders::MaterialResourceKind::Texture;
        textureProvider.desc.assetType = textures::TextureResourceType;
        textureProvider.builtinTexture = true;
        m_impl->providers.PushBack(textureProvider);
        m_impl->stats.registeredProviders = 1;
        return true;
    }

    bool MaterialResourceResolver::Shutdown(MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material resource resolver must shutdown on the main thread");
        if (m_impl->stats.activeOperations != 0 || m_impl->stats.liveReferences != 0 || m_impl->stats.cachedDescriptors != 0)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::LiveReferencesRemain, "material resource resolver shutdown requires zero operations, references, and cached descriptors");
        for (const Impl::Provider& provider : m_impl->providers)
            if (provider.liveOperations != 0)
                return Fail(m_impl, failure, MaterialResourceResolverFailureCode::LiveReferencesRemain, "material resource provider still owns live operations", provider.desc.kind, provider.desc.assetType);
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool MaterialResourceResolver::AbandonDevice(MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material resource abandonment must run on the main thread");
        for (u32 index = 0; index < m_impl->operations.Size(); ++index)
        {
            Impl::Operation& operation = m_impl->operations[index];
            if (operation.state == OperationState::Free)
                continue;
            if (operation.state == OperationState::Leased)
                --m_impl->stats.liveReferences;
            AbandonProvider(*m_impl, operation);
            RecycleOperation(*m_impl, index);
        }
        m_impl->descriptorHeads.Clear();
        m_impl->recycledDescriptors.Clear();
        for (u32 index = 0; index < m_impl->descriptors.Size(); ++index)
        {
            m_impl->descriptors[index] = {};
            m_impl->recycledDescriptors.PushBack(index);
        }
        m_impl->stats.cachedDescriptors = 0;
        return true;
    }

    bool MaterialResourceResolver::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MaterialResourceResolver::RegisterProvider(const MaterialResourceProviderDesc& provider, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::NotInitialized, "material resource resolver is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material providers must register on the main thread");
        if (!provider.IsValid())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::InvalidArgument, "material provider descriptor is invalid", provider.kind, provider.assetType);
        if (FindProvider(*m_impl, provider.kind, provider.assetType) != InvalidMaterialResourceResolveIndex)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::DuplicateProvider, "material provider key is already registered", provider.kind, provider.assetType);
        if (m_impl->providers.Size() >= m_impl->config.maximumProviders)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material provider registry capacity exceeded", provider.kind, provider.assetType);
        Impl::Provider record;
        record.desc = provider;
        m_impl->providers.PushBack(record);
        m_impl->stats.registeredProviders = m_impl->providers.Size();
        return true;
    }

    bool MaterialResourceResolver::RegisterFallback(const MaterialResourceFallbackDesc& fallback, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::NotInitialized, "material resource resolver is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material fallbacks must register on the main thread");
        if (fallback.assetType == resources::InvalidResourceTypeId || fallback.typeFingerprint.IsEmpty() || !shaders::IsValidMaterialResourceShape(fallback.kind, fallback.shape) ||
            FindProvider(*m_impl, fallback.kind, fallback.assetType) == InvalidMaterialResourceResolveIndex || (fallback.resource.IsValid() && fallback.resource.GetType() != fallback.assetType))
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::InvalidArgument, "material fallback descriptor is invalid", fallback.kind, fallback.assetType);
        MaterialResourceResolveRequest request;
        request.role.kind = fallback.kind;
        request.role.typeFingerprint = fallback.typeFingerprint;
        request.role.shape = fallback.shape;
        request.expectedAssetType = fallback.assetType;
        if (FindFallback(*m_impl, request) != nullptr)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::DuplicateFallback, "exact material fallback is already registered", fallback.kind, fallback.assetType);
        if (m_impl->fallbacks.Size() >= m_impl->config.maximumFallbacks)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material fallback registry capacity exceeded", fallback.kind, fallback.assetType);
        Impl::Fallback record;
        record.desc = fallback;
        m_impl->fallbacks.PushBack(static_cast<Impl::Fallback&&>(record));
        m_impl->stats.registeredFallbacks = m_impl->fallbacks.Size();
        return true;
    }

    bool MaterialResourceResolver::ClearFallbacks(MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::NotInitialized, "material resource resolver is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material fallbacks must clear on the main thread");
        if (m_impl->stats.activeOperations != 0 || m_impl->stats.liveReferences != 0)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::LiveReferencesRemain, "material fallbacks cannot clear while resolution work is live");
        m_impl->fallbacks.Clear();
        m_impl->stats.registeredFallbacks = 0;
        return true;
    }

    bool MaterialResourceResolver::Begin(const MaterialResourceResolveRequest& request, MaterialResourceResolveTicket& ticket, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        ticket = {};
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::NotInitialized, "material resource resolver is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material role resolution must begin on the main thread");
        const bool roleRequired = shaders::HasFlag(request.role.flags, shaders::MaterialResourceFlags::Required);
        if (request.expectedAssetType == resources::InvalidResourceTypeId || request.role.typeFingerprint.IsEmpty() || !shaders::IsValidMaterialResourceShape(request.role.kind, request.role.shape) ||
            (request.resource.IsValid() && request.resource.GetType() != request.expectedAssetType) || (request.dependency == resources::DependencyKind::Soft && request.resource.IsValid()) ||
            roleRequired != (request.dependency == resources::DependencyKind::Required))
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::InvalidArgument, "material role resolution request is invalid", request.role.kind, request.expectedAssetType);
        if (request.dependency == resources::DependencyKind::Required && !request.resource.IsValid())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::DependencyUnavailable, "required material role has no exact loaded dependency generation", request.role.kind,
                        request.expectedAssetType);
        if (m_impl->stats.activeOperations >= m_impl->config.maximumOperations)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::CapacityExceeded, "material role operation capacity exceeded", request.role.kind, request.expectedAssetType);

        u32 index = InvalidMaterialResourceResolveIndex;
        if (!m_impl->recycledOperations.Empty())
        {
            index = m_impl->recycledOperations.Back();
            m_impl->recycledOperations.PopBack();
        }
        else
        {
            index = m_impl->operations.Size();
            m_impl->operations.PushBack({});
        }
        Impl::Operation& operation = m_impl->operations[index];
        ++operation.generation;
        if (operation.generation == 0)
            ++operation.generation;
        operation.state = OperationState::Pending;
        operation.request = request;
        operation.provider = InvalidMaterialResourceResolveIndex;
        ++m_impl->stats.activeOperations;
        ++m_impl->stats.resolutionsBegun;
        ticket = {index, operation.generation};

        const bool exact = request.dependency != resources::DependencyKind::Soft && request.resource.IsValid();
        if (exact && StartOperation(*m_impl, operation, request.resource, false, failure))
            return true;
        if (exact && operation.provider != InvalidMaterialResourceResolveIndex)
            CancelProvider(*m_impl, operation);
        const Impl::Fallback* const fallback = FindFallback(*m_impl, request);
        if (request.dependency != resources::DependencyKind::Required && fallback != nullptr)
        {
            ++m_impl->stats.fallbacksSelected;
            ClearFailure(failure);
            if (StartOperation(*m_impl, operation, fallback->desc.resource, true, failure))
                return true;
            if (operation.provider != InvalidMaterialResourceResolveIndex)
                CancelProvider(*m_impl, operation);
        }
        RecycleOperation(*m_impl, index);
        ticket = {};
        if (failure != nullptr && failure->code != MaterialResourceResolverFailureCode::None)
            return false;
        return Fail(m_impl, failure, fallback == nullptr ? MaterialResourceResolverFailureCode::FallbackNotFound : MaterialResourceResolverFailureCode::ProviderFailure,
                    fallback == nullptr ? "material role has no exact typed fallback" : "material fallback provider failed", request.role.kind, request.expectedAssetType);
    }

    MaterialResourceResolveStatus MaterialResourceResolver::Poll(MaterialResourceResolveTicket& ticket, MaterialResourceReference& reference, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread() || reference.IsValid())
        {
            static_cast<void>(Fail(m_impl, failure,
                                   m_impl == nullptr              ? MaterialResourceResolverFailureCode::NotInitialized
                                   : !concurrency::IsMainThread() ? MaterialResourceResolverFailureCode::WrongThread
                                                                  : MaterialResourceResolverFailureCode::InvalidArgument,
                                   "material role poll is invalid"));
            return MaterialResourceResolveStatus::Failed;
        }
        Impl::Operation* const operation = FindOperation(*m_impl, ticket);
        if (operation == nullptr || operation->state != OperationState::Pending)
        {
            static_cast<void>(Fail(m_impl, failure, MaterialResourceResolverFailureCode::StaleTicket, "material role ticket is stale"));
            return MaterialResourceResolveStatus::Failed;
        }
        const u32 operationIndex = ticket.index;
        Impl::Provider& provider = m_impl->providers[operation->provider];
        if (provider.builtinTexture)
        {
            TextureRuntimeInfo info;
            TextureResidencyRuntimeFailure textureFailure;
            if (!m_impl->textures->GetInfo(operation->textureDemand.GetResidency(), info, &textureFailure))
            {
                if (TryFallback(*m_impl, *operation, failure))
                    return MaterialResourceResolveStatus::Pending;
                const shaders::MaterialResourceKind kind = operation->request.role.kind;
                const resources::ResourceTypeId assetType = operation->request.expectedAssetType;
                CancelProvider(*m_impl, *operation);
                RecycleOperation(*m_impl, operationIndex);
                ticket = {};
                static_cast<void>(
                    Fail(m_impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, textureFailure.message != nullptr ? textureFailure.message : "texture residency query failed", kind, assetType));
                return MaterialResourceResolveStatus::Failed;
            }
            if (info.state != TextureRuntimeState::BindlessReady)
            {
                if (info.state == TextureRuntimeState::Failed || info.state == TextureRuntimeState::Cancelling || info.state == TextureRuntimeState::Retiring)
                {
                    if (TryFallback(*m_impl, *operation, failure))
                        return MaterialResourceResolveStatus::Pending;
                    const shaders::MaterialResourceKind kind = operation->request.role.kind;
                    const resources::ResourceTypeId assetType = operation->request.expectedAssetType;
                    CancelProvider(*m_impl, *operation);
                    RecycleOperation(*m_impl, operationIndex);
                    ticket = {};
                    static_cast<void>(Fail(m_impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, "texture residency failed before becoming bindless-ready", kind, assetType));
                    return MaterialResourceResolveStatus::Failed;
                }
                return MaterialResourceResolveStatus::Pending;
            }
            operation->identity = {shaders::MaterialResourceKind::Texture, rhi::DescriptorDomainKind::Resources, info.residency.index, info.residency.generation};
            operation->gpuResource.resource = info.residency.index;
            operation->gpuResource.type = GpuMaterialResourceType::Texture;
        }
        else
        {
            MaterialResourceProviderResult result;
            const char* message = nullptr;
            if (!provider.desc.poll(provider.desc.userData, operation->providerToken, result, message))
                result.state = MaterialResourceProviderState::Failed;
            if (result.state == MaterialResourceProviderState::Pending)
                return MaterialResourceResolveStatus::Pending;
            bool resolved = result.state == MaterialResourceProviderState::Ready;
            if (resolved && operation->request.role.kind == shaders::MaterialResourceKind::Texture)
            {
                resolved = result.texture.IsValid() && !result.buffer.IsValid() && !result.sampler.IsValid() && !result.accelerationStructure.IsValid() &&
                           (!operation->fallback || !IsWritable(operation->request.role.shape) || result.exclusive);
                if (resolved)
                {
                    operation->identity = {shaders::MaterialResourceKind::Texture, rhi::DescriptorDomainKind::Resources, result.texture.index, result.texture.generation};
                    operation->gpuResource.resource = result.texture.index;
                }
                else
                    static_cast<void>(Fail(m_impl, failure,
                                           operation->fallback && IsWritable(operation->request.role.shape) && !result.exclusive ? MaterialResourceResolverFailureCode::UnsupportedShape
                                                                                                                                 : MaterialResourceResolverFailureCode::ProviderFailure,
                                           operation->fallback && IsWritable(operation->request.role.shape) && !result.exclusive ? "writable texture fallback must be provider-private"
                                                                                                                                 : "texture provider did not return a bindless-ready stable residency identity",
                                           operation->request.role.kind, operation->request.expectedAssetType));
            }
            else if (resolved)
                resolved = AcquireDescriptor(*m_impl, *operation, result, failure);
            if (!resolved)
            {
                if (TryFallback(*m_impl, *operation, failure))
                    return MaterialResourceResolveStatus::Pending;
                const shaders::MaterialResourceKind kind = operation->request.role.kind;
                const resources::ResourceTypeId assetType = operation->request.expectedAssetType;
                CancelProvider(*m_impl, *operation);
                RecycleOperation(*m_impl, operationIndex);
                ticket = {};
                if (failure != nullptr && failure->code == MaterialResourceResolverFailureCode::None)
                    static_cast<void>(Fail(m_impl, failure, MaterialResourceResolverFailureCode::ProviderFailure, message != nullptr ? message : "material resource provider failed", kind, assetType));
                return MaterialResourceResolveStatus::Failed;
            }
            operation->gpuResource.type = GpuKind(operation->request.role.kind);
            if (operation->request.role.kind == shaders::MaterialResourceKind::Sampler)
                operation->gpuResource.samplerDescriptor = operation->identity.index;
            else if (operation->request.role.kind != shaders::MaterialResourceKind::Texture)
                operation->gpuResource.resource = operation->identity.index;
        }
        operation->gpuResource.flags = 0;
        operation->state = OperationState::Leased;
        reference.m_owner = this;
        reference.m_index = operationIndex;
        reference.m_generation = operation->generation;
        reference.m_gpuResource = operation->gpuResource;
        reference.m_identity = operation->identity;
        ticket = {};
        ++m_impl->stats.liveReferences;
        ++m_impl->stats.resolutionsReady;
        return MaterialResourceResolveStatus::Ready;
    }

    bool MaterialResourceResolver::Cancel(MaterialResourceResolveTicket& ticket, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialResourceResolverFailureCode::NotInitialized, "material resource resolver is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::WrongThread, "material role cancellation must run on the main thread");
        Impl::Operation* const operation = FindOperation(*m_impl, ticket);
        if (operation == nullptr || operation->state != OperationState::Pending)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::StaleTicket, "material role ticket is stale");
        const u32 index = ticket.index;
        CancelProvider(*m_impl, *operation);
        RecycleOperation(*m_impl, index);
        ticket = {};
        return true;
    }

    bool MaterialResourceResolver::ReleaseReference(const u32 index, const u32 generation, const rhi::ResidencyFenceSet& safeAfter, const bool retired, MaterialResourceResolverFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || !concurrency::IsMainThread())
            return Fail(m_impl, failure, m_impl == nullptr ? MaterialResourceResolverFailureCode::NotInitialized : MaterialResourceResolverFailureCode::WrongThread,
                        "material resource reference release is invalid");
        if (index >= m_impl->operations.Size())
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::StaleTicket, "material resource reference is stale");
        Impl::Operation& operation = m_impl->operations[index];
        if (operation.state != OperationState::Leased || operation.generation != generation)
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::StaleTicket, "material resource reference is stale");
        if (retired && !CompleteCutover(safeAfter))
            return Fail(m_impl, failure, MaterialResourceResolverFailureCode::MissingRetirementFence, "published material resource reference requires graphics, compute, and copy cutover fences",
                        operation.request.role.kind, operation.request.expectedAssetType);

        if (operation.descriptor != InvalidMaterialResourceResolveIndex)
        {
            Impl::DescriptorRecord& descriptor = m_impl->descriptors[operation.descriptor];
            if (descriptor.active && descriptor.references != 0)
            {
                if (retired)
                    IncludeFences(descriptor.retirementFloor, safeAfter);
                if (descriptor.references == 1)
                {
                    const rhi::DescriptorDomainRef domain = descriptor.kind == shaders::MaterialResourceKind::Sampler ? m_impl->samplerDescriptors.GetRef() : m_impl->resourceDescriptors.GetRef();
                    rhi::Failure rhiFailure;
                    if (!rhi::RetireDescriptor(domain, descriptor.descriptor, DescriptorRetirement(descriptor.retirementFloor), &rhiFailure))
                        return Fail(m_impl, failure, MaterialResourceResolverFailureCode::DescriptorFailure, "material descriptor retirement failed", operation.request.role.kind,
                                    operation.request.expectedAssetType, rhiFailure);
                    RemoveDescriptorIndex(*m_impl, operation.descriptor);
                    descriptor = {};
                    m_impl->recycledDescriptors.PushBack(operation.descriptor);
                    --m_impl->stats.cachedDescriptors;
                }
                else
                    --descriptor.references;
            }
        }

        Impl::Provider& provider = m_impl->providers[operation.provider];
        if (provider.builtinTexture)
            operation.textureDemand.Reset();
        else
            provider.desc.release(provider.desc.userData, operation.providerToken, retired ? safeAfter : rhi::ResidencyFenceSet{});
        --provider.liveOperations;
        --m_impl->stats.liveReferences;
        RecycleOperation(*m_impl, index);
        return true;
    }

    MaterialResourceResolverStats MaterialResourceResolver::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : MaterialResourceResolverStats{};
    }

    void MaterialResourceResolver::AbandonReference(const u32 index, const u32 generation) noexcept
    {
        if (m_impl == nullptr || !concurrency::IsMainThread() || index >= m_impl->operations.Size())
            return;
        Impl::Operation& operation = m_impl->operations[index];
        if (operation.state != OperationState::Leased || operation.generation != generation)
            return;
        AbandonProvider(*m_impl, operation);
        --m_impl->stats.liveReferences;
        RecycleOperation(*m_impl, index);
    }

    bool MaterialResourceResolver::IsReferenceValid(const u32 index, const u32 generation) const noexcept
    {
        return m_impl != nullptr && index < m_impl->operations.Size() && m_impl->operations[index].state == OperationState::Leased && m_impl->operations[index].generation == generation;
    }
} // namespace vanguard::rendering
