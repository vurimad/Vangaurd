#include <vanguard/rendering/material_program_layout.hpp>

#include <vanguard/concurrency/thread.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/rendering/material_program_layout_internal.hpp>

#include <new>

namespace vanguard::rendering
{
    namespace
    {
        void ClearFailure(MaterialProgramLayoutFailure* const failure) noexcept
        {
            if (failure != nullptr)
                *failure = {};
        }

        [[nodiscard]] bool Fail(MaterialProgramLayoutRegistry::Impl* impl, MaterialProgramLayoutFailure* failure, MaterialProgramLayoutFailureCode code, const char* message,
                                const crypto::Digest256& fingerprint = {}, MaterialProgramLayoutId layout = {}) noexcept;

        [[nodiscard]] bool NativeFormatCompatible(const shaders::NativeFormat format, const rhi::BackendKind backend) noexcept
        {
            if (backend == rhi::BackendKind::D3D12)
                return format == shaders::NativeFormat::Dxil;
            if (backend == rhi::BackendKind::Vulkan)
                return format == shaders::NativeFormat::SpirV;
            return false;
        }

        [[nodiscard]] bool SameParameter(const shaders::ConstantMember& left, const shaders::ConstantMember& right) noexcept
        {
            return left.name == right.name && left.byteOffset == right.byteOffset && left.byteSize == right.byteSize && left.arrayStride == right.arrayStride && left.matrixStride == right.matrixStride &&
                   left.scalarType == right.scalarType && left.rows == right.rows && left.columns == right.columns && left.rowMajor == right.rowMajor;
        }

        [[nodiscard]] bool SameResource(const shaders::MaterialResourceRole& left, const shaders::MaterialResourceRole& right) noexcept
        {
            return left.name == right.name && left.arrayIndex == right.arrayIndex && left.slot == right.slot && left.kind == right.kind && left.flags == right.flags && left.reserved == right.reserved &&
                   left.typeFingerprint == right.typeFingerprint && left.shape == right.shape;
        }

        [[nodiscard]] u64 FingerprintHash(const crypto::Digest256& fingerprint) noexcept
        {
            u64 value = 0xcbf29ce484222325ull;
            for (u32 index = 0; index < crypto::Digest256::ByteCount; ++index)
                value = (value ^ fingerprint.bytes[index]) * 0x100000001b3ull;
            return value;
        }
    } // namespace

    struct MaterialProgramLayoutRegistry::Impl
    {
        struct Record
        {
            Record() noexcept : parameters(memory::pools::Rendering::GetInstance()), resources(memory::pools::Rendering::GetInstance()) {}

            MaterialProgramLayoutId id;
            crypto::Digest256 layoutFingerprint;
            crypto::Digest256 domainFingerprint;
            shaders::MaterialDomainContract domain;
            u32 accessorAbiVersion = 0;
            u32 parameterByteSize = 0;
            containers::DynamicArray<shaders::ConstantMember> parameters;
            containers::DynamicArray<shaders::MaterialResourceRole> resources;
            u32 nextFingerprintCollision = InvalidMaterialProgramLayoutIndex;
        };

        Impl() noexcept : records(memory::pools::Rendering::GetInstance()), fingerprintHeads(memory::pools::Rendering::GetInstance()) {}

        containers::DynamicArray<Record> records;
        containers::HashMap<u64, u32> fingerprintHeads;
        MaterialProgramLayoutRegistryStats stats;
        rhi::BackendKind backend = rhi::BackendKind::Unknown;
        u32 maximumLayouts = 0;
    };

    namespace
    {
        [[nodiscard]] bool Fail(MaterialProgramLayoutRegistry::Impl* const impl, MaterialProgramLayoutFailure* const failure, const MaterialProgramLayoutFailureCode code, const char* const message,
                                const crypto::Digest256& fingerprint, const MaterialProgramLayoutId layout) noexcept
        {
            if (impl != nullptr)
            {
                ++impl->stats.rejectedRegistrations;
                if (code == MaterialProgramLayoutFailureCode::FingerprintCollision)
                    ++impl->stats.fingerprintCollisions;
            }
            if (failure != nullptr)
                *failure = {code, fingerprint, layout, message};
            return false;
        }

        [[nodiscard]] bool SameCanonical(const MaterialProgramLayoutRegistry::Impl::Record& record, const detail::MaterialProgramLayoutCanonical& canonical) noexcept
        {
            if (record.domainFingerprint != canonical.domainFingerprint || !shaders::MaterialDomainContractsEqual(record.domain, canonical.domain) || record.accessorAbiVersion != canonical.accessorAbiVersion ||
                record.parameterByteSize != canonical.parameterByteSize || record.parameters.Size() != canonical.parameters.Size() || record.resources.Size() != canonical.resources.Size())
                return false;
            for (u32 index = 0; index < record.parameters.Size(); ++index)
                if (!SameParameter(record.parameters[index], canonical.parameters[index]))
                    return false;
            for (u32 index = 0; index < record.resources.Size(); ++index)
                if (!SameResource(record.resources[index], canonical.resources[index]))
                    return false;
            return true;
        }

        [[nodiscard]] bool ValidCanonical(const detail::MaterialProgramLayoutCanonical& canonical) noexcept
        {
            return !canonical.layoutFingerprint.IsEmpty() && !canonical.domainFingerprint.IsEmpty() && canonical.domain.name != 0 && canonical.domain.schemaVersion != 0 && canonical.domain.legalStages != 0 &&
                   shaders::IsValidMaterialShaderCapabilityMask(canonical.domain.requiredCapabilities) && !canonical.domain.inputType.IsEmpty() && !canonical.domain.outputType.IsEmpty() &&
                   canonical.accessorAbiVersion != 0 && (canonical.parameterByteSize != 0 || canonical.parameters.Empty());
        }
    } // namespace

    MaterialProgramLayoutRegistry::~MaterialProgramLayoutRegistry()
    {
        if (m_impl != nullptr)
            static_cast<void>(Shutdown());
    }

    bool MaterialProgramLayoutRegistry::Initialize(const MaterialProgramLayoutRegistryConfig& config, MaterialProgramLayoutFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::AlreadyInitialized, "material program-layout registry is already initialized");
        if (!concurrency::IsMainThread())
            return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::WrongThread, "material program-layout registry must initialize on the main thread");
        if ((config.backend != rhi::BackendKind::D3D12 && config.backend != rhi::BackendKind::Vulkan) || config.maximumLayouts == 0 || config.maximumLayouts > MaximumMaterialProgramLayouts)
            return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::InvalidConfiguration, "material program-layout registry configuration is invalid");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Rendering, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::CapacityExceeded, "material program-layout registry allocation failed");
        m_impl = ::new (block.address) Impl();
        m_impl->backend = config.backend;
        m_impl->maximumLayouts = config.maximumLayouts;
        m_impl->stats.capacity = config.maximumLayouts;
        return true;
    }

    bool MaterialProgramLayoutRegistry::Shutdown(MaterialProgramLayoutFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr)
            return true;
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialProgramLayoutFailureCode::WrongThread, "material program-layout registry must shutdown on the main thread");
        Impl* const impl = m_impl;
        m_impl = nullptr;
        impl->~Impl();
        memory::MemoryBlock block{impl, sizeof(Impl), memory::PoolId::Rendering};
        memory::Free(block);
        return true;
    }

    bool MaterialProgramLayoutRegistry::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool MaterialProgramLayoutRegistry::Register(const shaders::ShaderResourceObject& shaderObject, MaterialProgramLayoutId& layout, MaterialProgramLayoutFailure* const failure) noexcept
    {
        ClearFailure(failure);
        layout = {};
        if (m_impl == nullptr)
            return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::NotInitialized, "material program-layout registry is not initialized");
        if (!concurrency::IsMainThread())
            return Fail(m_impl, failure, MaterialProgramLayoutFailureCode::WrongThread, "material program layouts must be registered on the main thread");
        if (!shaderObject.IsOpen())
            return Fail(m_impl, failure, MaterialProgramLayoutFailureCode::InvalidShader, "material program layout requires an open shader resource object");
        const shaders::ShaderFile& shader = shaderObject.GetFile();
        const shaders::MaterialContract* const contract = shader.GetMaterialContract();
        if (contract == nullptr)
            return Fail(m_impl, failure, MaterialProgramLayoutFailureCode::InvalidShader, "shader resource has no material contract");
        for (const shaders::StageRecord& stage : shader.GetStages())
            if (!NativeFormatCompatible(stage.format, m_impl->backend))
                return Fail(m_impl, failure, MaterialProgramLayoutFailureCode::UnsupportedBackendFormat, "shader resource native format is incompatible with the registry backend", contract->layoutFingerprint);

        const detail::MaterialProgramLayoutCanonical canonical{contract->layoutFingerprint,    contract->domainFingerprint,  contract->domain, contract->accessorAbiVersion, contract->parameterByteSize,
                                                               shader.GetMaterialParameters(), shader.GetMaterialResources()};
        return detail::MaterialProgramLayoutRegistryAccess::RegisterCanonical(*this, canonical, layout, failure);
    }

    bool MaterialProgramLayoutRegistry::Get(const MaterialProgramLayoutId layout, MaterialProgramLayoutView& view) const noexcept
    {
        view = {};
        if (m_impl == nullptr || !layout.IsValid() || layout.index >= m_impl->records.Size() || !concurrency::IsMainThread())
            return false;
        const Impl::Record& record = m_impl->records[layout.index];
        view = {record.id, record.layoutFingerprint, record.domainFingerprint, record.domain, record.accessorAbiVersion, record.parameterByteSize, record.parameters, record.resources};
        return true;
    }

    MaterialProgramLayoutRegistryStats MaterialProgramLayoutRegistry::GetStats() const noexcept
    {
        return m_impl != nullptr ? m_impl->stats : MaterialProgramLayoutRegistryStats{};
    }

    namespace detail
    {
        bool MaterialProgramLayoutRegistryAccess::RegisterCanonical(MaterialProgramLayoutRegistry& registry, const MaterialProgramLayoutCanonical& canonical, MaterialProgramLayoutId& layout,
                                                                    MaterialProgramLayoutFailure* const failure) noexcept
        {
            ClearFailure(failure);
            layout = {};
            MaterialProgramLayoutRegistry::Impl* const impl = registry.m_impl;
            if (impl == nullptr)
                return Fail(nullptr, failure, MaterialProgramLayoutFailureCode::NotInitialized, "material program-layout registry is not initialized");
            if (!concurrency::IsMainThread())
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::WrongThread, "material program layouts must be registered on the main thread");
            if (!ValidCanonical(canonical))
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::InvalidLayout, "canonical material program layout is invalid", canonical.layoutFingerprint);
            const u64 fingerprintHash = FingerprintHash(canonical.layoutFingerprint);
            u32 candidate = InvalidMaterialProgramLayoutIndex;
            static_cast<void>(impl->fingerprintHeads.Find(fingerprintHash, candidate));
            u32 collisionTail = InvalidMaterialProgramLayoutIndex;
            while (candidate != InvalidMaterialProgramLayoutIndex)
            {
                const MaterialProgramLayoutRegistry::Impl::Record& record = impl->records[candidate];
                if (record.layoutFingerprint != canonical.layoutFingerprint)
                {
                    collisionTail = candidate;
                    candidate = record.nextFingerprintCollision;
                    continue;
                }
                if (SameCanonical(record, canonical))
                {
                    ++impl->stats.repeatedRegistrations;
                    layout = record.id;
                    return true;
                }
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::FingerprintCollision, "material layout fingerprint aliases an incompatible canonical payload", canonical.layoutFingerprint,
                            record.id);
            }
            if (impl->records.Size() >= impl->maximumLayouts)
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::CapacityExceeded, "material program-layout registry capacity exceeded", canonical.layoutFingerprint);

            MaterialProgramLayoutRegistry::Impl::Record record;
            record.id = {impl->records.Size()};
            record.layoutFingerprint = canonical.layoutFingerprint;
            record.domainFingerprint = canonical.domainFingerprint;
            record.domain = canonical.domain;
            record.accessorAbiVersion = canonical.accessorAbiVersion;
            record.parameterByteSize = canonical.parameterByteSize;
            record.parameters.Reserve(canonical.parameters.Size());
            for (const shaders::ConstantMember& parameter : canonical.parameters)
                record.parameters.PushBack(parameter);
            record.resources.Reserve(canonical.resources.Size());
            for (const shaders::MaterialResourceRole& resource : canonical.resources)
                record.resources.PushBack(resource);
            if (record.parameters.Size() != canonical.parameters.Size() || record.resources.Size() != canonical.resources.Size())
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::CapacityExceeded, "material program-layout payload allocation failed", canonical.layoutFingerprint);
            const u32 expectedSize = impl->records.Size() + 1u;
            layout = record.id;
            impl->records.PushBack(static_cast<MaterialProgramLayoutRegistry::Impl::Record&&>(record));
            if (impl->records.Size() != expectedSize)
            {
                layout = {};
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::CapacityExceeded, "material program-layout record allocation failed", canonical.layoutFingerprint);
            }
            const u32 recordIndex = expectedSize - 1u;
            if (collisionTail != InvalidMaterialProgramLayoutIndex)
                impl->records[collisionTail].nextFingerprintCollision = recordIndex;
            else if (!impl->fingerprintHeads.Insert(fingerprintHash, recordIndex).IsSuccessful())
            {
                static_cast<void>(impl->records.RemoveAt(recordIndex));
                layout = {};
                return Fail(impl, failure, MaterialProgramLayoutFailureCode::CapacityExceeded, "material program-layout fingerprint index allocation failed", canonical.layoutFingerprint);
            }
            impl->stats.registeredLayouts = impl->records.Size();
            return true;
        }
    } // namespace detail
} // namespace vanguard::rendering
