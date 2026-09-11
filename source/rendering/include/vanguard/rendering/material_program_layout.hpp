#pragma once

#include <vanguard/containers/containers.hpp>
#include <vanguard/rhi/rhi_types.hpp>
#include <vanguard/shaders/shaders.hpp>

namespace vanguard::rendering
{
    inline constexpr u32 MaximumMaterialProgramLayouts = 65536;
    inline constexpr u32 InvalidMaterialProgramLayoutIndex = 0xffffffffu;

    struct MaterialProgramLayoutId
    {
        u32 index = InvalidMaterialProgramLayoutIndex;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != InvalidMaterialProgramLayoutIndex;
        }
        [[nodiscard]] friend constexpr bool operator==(const MaterialProgramLayoutId&, const MaterialProgramLayoutId&) noexcept = default;
    };

    struct MaterialProgramLayoutView
    {
        MaterialProgramLayoutId id;
        crypto::Digest256 layoutFingerprint;
        crypto::Digest256 domainFingerprint;
        shaders::MaterialDomainContract domain;
        u32 accessorAbiVersion = 0;
        u32 parameterByteSize = 0;
        containers::ArraySpan<const shaders::ConstantMember> parameters;
        containers::ArraySpan<const shaders::MaterialResourceRole> resources;
    };

    enum class MaterialProgramLayoutFailureCode : u8
    {
        None,
        NotInitialized,
        AlreadyInitialized,
        WrongThread,
        InvalidConfiguration,
        InvalidShader,
        UnsupportedBackendFormat,
        CapacityExceeded,
        FingerprintCollision,
        InvalidLayout
    };

    struct MaterialProgramLayoutFailure
    {
        MaterialProgramLayoutFailureCode code = MaterialProgramLayoutFailureCode::None;
        crypto::Digest256 fingerprint;
        MaterialProgramLayoutId layout;
        const char* message = nullptr;
    };

    struct MaterialProgramLayoutRegistryConfig
    {
        rhi::BackendKind backend = rhi::BackendKind::Unknown;
        u32 maximumLayouts = MaximumMaterialProgramLayouts;
    };

    struct MaterialProgramLayoutRegistryStats
    {
        u32 registeredLayouts = 0;
        u32 capacity = 0;
        u64 repeatedRegistrations = 0;
        u64 rejectedRegistrations = 0;
        u64 fingerprintCollisions = 0;
    };

    class MaterialProgramLayoutRegistry;

    namespace detail
    {
        struct MaterialProgramLayoutRegistryAccess;
    } // namespace detail

    /// Renderer/device-lifetime registry for shader-reflected material layouts.
    /// Registration, lookup, and shutdown are renderer-owning-thread operations.
    /// IDs are never recycled before shutdown and may be copied directly into
    /// GpuMaterial::materialLayout through MaterialProgramLayoutId::index.
    class MaterialProgramLayoutRegistry final
    {
    public:
        struct Impl;

        MaterialProgramLayoutRegistry() noexcept = default;
        ~MaterialProgramLayoutRegistry();

        MaterialProgramLayoutRegistry(const MaterialProgramLayoutRegistry&) = delete;
        MaterialProgramLayoutRegistry& operator=(const MaterialProgramLayoutRegistry&) = delete;

        [[nodiscard]] bool Initialize(const MaterialProgramLayoutRegistryConfig& config, MaterialProgramLayoutFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Shutdown(MaterialProgramLayoutFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        [[nodiscard]] bool Register(const shaders::ShaderResourceObject& shader, MaterialProgramLayoutId& layout, MaterialProgramLayoutFailure* failure = nullptr) noexcept;
        [[nodiscard]] bool Get(MaterialProgramLayoutId layout, MaterialProgramLayoutView& view) const noexcept;
        [[nodiscard]] MaterialProgramLayoutRegistryStats GetStats() const noexcept;

    private:
        Impl* m_impl = nullptr;

        friend struct detail::MaterialProgramLayoutRegistryAccess;
    };
} // namespace vanguard::rendering
