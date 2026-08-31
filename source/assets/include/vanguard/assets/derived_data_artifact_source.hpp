#pragma once

#include <vanguard/assets/assets.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::assets
{
    enum class DerivedDataArtifactResult : u8
    {
        Success,
        NotFound,
        InvalidArgument,
        InvalidState,
        ContentMismatch,
        DescriptorMismatch,
        Corrupt,
        IoFailure,
        OutOfMemory,
        LimitExceeded
    };

    [[nodiscard]] const char* ToString(DerivedDataArtifactResult result) noexcept;

    struct ArtifactSetKey
    {
        BuildFingerprint build;
        BuildFingerprint content;

        [[nodiscard]] friend bool operator==(const ArtifactSetKey&, const ArtifactSetKey&) noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !build.IsEmpty();
        }
    };

    struct DerivedDataArtifactLimits
    {
        u32 maximumArtifacts = 4096;
        u64 maximumArtifactBytes = 2ull * 1024ull * 1024ull * 1024ull;
        u32 validationScratchBytes = 1024u * 1024u;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return maximumArtifacts != 0 && maximumArtifactBytes != 0 && validationScratchBytes != 0;
        }
    };

    struct CachedArtifactDescriptor
    {
        resources::ResourceReference resource;
        u32 segment = 0;
        ArtifactFlags flags = ArtifactFlags::None;
        u8 alignmentLog2 = 4;
        u64 byteCount = 0;
    };

    class ArtifactSetReader final
    {
    public:
        struct Impl;

        ArtifactSetReader() noexcept = default;
        ~ArtifactSetReader();

        ArtifactSetReader(const ArtifactSetReader&) = delete;
        ArtifactSetReader& operator=(const ArtifactSetReader&) = delete;

        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] ArtifactSetKey Key() const noexcept;
        [[nodiscard]] containers::ArraySpan<const CachedArtifactDescriptor> Artifacts() const noexcept;
        [[nodiscard]] const CachedArtifactDescriptor* Find(resources::ResourceReference resource, u32 segment) const noexcept;

        [[nodiscard]] DerivedDataArtifactResult Read(const CachedArtifactDescriptor& expected,
                                                     containers::DynamicArray<u8>& bytes) noexcept;
        [[nodiscard]] DerivedDataArtifactResult CopyTo(const CachedArtifactDescriptor& expected,
                                                       filesystem::IFile& output,
                                                       containers::ArraySpan<u8> scratch,
                                                       crypto::Sha256Builder* contentHash = nullptr) noexcept;
        [[nodiscard]] DerivedDataArtifactResult ReadAll(BuildOutput& output) noexcept;

    private:
        Impl* m_impl = nullptr;

        friend class DerivedDataArtifactSource;
    };

    struct DerivedDataArtifactSourceConfig
    {
        const char* root = nullptr;
        DerivedDataArtifactLimits limits;
    };

    class DerivedDataArtifactSource final
    {
    public:
        struct Impl;

        DerivedDataArtifactSource() noexcept = default;
        ~DerivedDataArtifactSource();

        DerivedDataArtifactSource(const DerivedDataArtifactSource&) = delete;
        DerivedDataArtifactSource& operator=(const DerivedDataArtifactSource&) = delete;

        [[nodiscard]] bool Initialize(const DerivedDataArtifactSourceConfig& config) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] DerivedDataArtifactResult Open(ArtifactSetKey key, ArtifactSetReader& reader) const noexcept;

    private:
        Impl* m_impl = nullptr;
    };
} // namespace vanguard::assets
