#include <vanguard/asset_streaming/project_loose_publisher.hpp>

#include <vanguard/memory/memory.hpp>

namespace vanguard::asset_streaming
{
    namespace
    {
        [[nodiscard]] char HexDigit(const u8 value) noexcept
        {
            constexpr char digits[] = "0123456789abcdef";
            return digits[value & 0x0fu];
        }

        void WriteHex64(char* const output, const u64 value) noexcept
        {
            for (u32 index = 0; index < 16u; ++index)
                output[index] = HexDigit(static_cast<u8>(value >> ((15u - index) * 4u)));
        }

        void WriteFingerprint(char* const output, const assets::BuildFingerprint& fingerprint) noexcept
        {
            for (u32 index = 0; index < assets::BuildFingerprint::ByteCount; ++index)
            {
                output[index * 2u] = HexDigit(fingerprint.bytes[index] >> 4u);
                output[index * 2u + 1u] = HexDigit(fingerprint.bytes[index]);
            }
        }

        [[nodiscard]] resources::DependencyKind RuntimeKind(const assets::DependencyRequirement requirement) noexcept
        {
            if (requirement == assets::DependencyRequirement::Required)
                return resources::DependencyKind::Required;
            return requirement == assets::DependencyRequirement::Optional ? resources::DependencyKind::Optional
                                                                          : resources::DependencyKind::Soft;
        }

        [[nodiscard]] ProjectLoosePublicationResult MapMaterialization(const assets::LooseMaterializationResult result) noexcept
        {
            if (result == assets::LooseMaterializationResult::OutOfMemory)
                return ProjectLoosePublicationResult::OutOfMemory;
            if (result == assets::LooseMaterializationResult::LimitExceeded)
                return ProjectLoosePublicationResult::LimitExceeded;
            return ProjectLoosePublicationResult::MaterializationFailure;
        }

        [[nodiscard]] assets::LooseMaterializationResult MapArtifactValidation(const assets::DerivedDataArtifactResult result) noexcept
        {
            switch (result)
            {
            case assets::DerivedDataArtifactResult::Success: return assets::LooseMaterializationResult::Success;
            case assets::DerivedDataArtifactResult::NotFound: return assets::LooseMaterializationResult::ArtifactNotFound;
            case assets::DerivedDataArtifactResult::ContentMismatch:
            case assets::DerivedDataArtifactResult::Corrupt: return assets::LooseMaterializationResult::CorruptArtifactSet;
            case assets::DerivedDataArtifactResult::DescriptorMismatch: return assets::LooseMaterializationResult::DescriptorMismatch;
            case assets::DerivedDataArtifactResult::IoFailure: return assets::LooseMaterializationResult::IoFailure;
            case assets::DerivedDataArtifactResult::OutOfMemory: return assets::LooseMaterializationResult::OutOfMemory;
            case assets::DerivedDataArtifactResult::LimitExceeded: return assets::LooseMaterializationResult::LimitExceeded;
            case assets::DerivedDataArtifactResult::InvalidArgument: return assets::LooseMaterializationResult::InvalidArgument;
            case assets::DerivedDataArtifactResult::InvalidState: return assets::LooseMaterializationResult::InvalidState;
            }
            return assets::LooseMaterializationResult::ValidationFailed;
        }

        [[nodiscard]] bool SameRecord(const assets::DependencyRecord& left, const assets::DependencyRecord& right) noexcept
        {
            if (left.source != right.source || left.output != right.output || left.target != right.target || left.compiler != right.compiler ||
                left.compilerVersion != right.compilerVersion || left.sourceInputFingerprint != right.sourceInputFingerprint ||
                left.buildFingerprint != right.buildFingerprint || left.contentFingerprint != right.contentFingerprint ||
                left.dependencies.Size() != right.dependencies.Size() || left.artifacts.Size() != right.artifacts.Size())
                return false;
            for (u32 index = 0; index < left.dependencies.Size(); ++index)
            {
                const assets::BuildDependency& a = left.dependencies[index];
                const assets::BuildDependency& b = right.dependencies[index];
                if (a.identity != b.identity || a.content != b.content || a.role != b.role || a.requirement != b.requirement)
                    return false;
            }
            for (u32 index = 0; index < left.artifacts.Size(); ++index)
            {
                const assets::IndexedArtifact& a = left.artifacts[index];
                const assets::IndexedArtifact& b = right.artifacts[index];
                if (a.resource != b.resource || a.segment != b.segment || a.flags != b.flags || a.alignmentLog2 != b.alignmentLog2 ||
                    a.byteCount != b.byteCount)
                    return false;
            }
            return true;
        }
    } // namespace

    struct ProjectLoosePublisher::Impl final
    {
        Impl(assets::DependencyIndex& dependencyIndex, streaming::ResourceStreamer& resourceStreamer,
             const ProjectLoosePublisherConfig& publisherConfig) noexcept
            : index(&dependencyIndex), streamer(&resourceStreamer), config(publisherConfig)
        {
        }

        assets::DependencyIndex* index = nullptr;
        streaming::ResourceStreamer* streamer = nullptr;
        ProjectLoosePublisherConfig config;
        assets::DerivedDataArtifactSource artifacts;
        assets::LooseResourceMaterializer materializer;
    };

    const char* ToString(const ProjectLoosePublicationResult result) noexcept
    {
        switch (result)
        {
        case ProjectLoosePublicationResult::Success: return "Success";
        case ProjectLoosePublicationResult::InvalidArgument: return "InvalidArgument";
        case ProjectLoosePublicationResult::InvalidState: return "InvalidState";
        case ProjectLoosePublicationResult::RecordNotFound: return "RecordNotFound";
        case ProjectLoosePublicationResult::IndexFailure: return "IndexFailure";
        case ProjectLoosePublicationResult::TargetMismatch: return "TargetMismatch";
        case ProjectLoosePublicationResult::DirectoryFailure: return "DirectoryFailure";
        case ProjectLoosePublicationResult::MaterializationFailure: return "MaterializationFailure";
        case ProjectLoosePublicationResult::StreamingPublicationFailure: return "StreamingPublicationFailure";
        case ProjectLoosePublicationResult::OutOfMemory: return "OutOfMemory";
        case ProjectLoosePublicationResult::LimitExceeded: return "LimitExceeded";
        }
        return "Unknown";
    }

    ProjectLoosePublisher::~ProjectLoosePublisher()
    {
        static_cast<void>(Shutdown());
    }

    bool ProjectLoosePublisher::Initialize(assets::DependencyIndex& index, streaming::ResourceStreamer& streamer,
                                           const ProjectLoosePublisherConfig& config) noexcept
    {
        if (m_impl != nullptr || !index.IsInitialized() || !streamer.IsInitialized() || !config.IsValid())
            return false;
        Impl* const implementation = VANGUARD_NEW(Impl, memory::pools::Assets)(index, streamer, config);
        if (implementation == nullptr)
            return false;
        if (!implementation->artifacts.Initialize({config.artifactCacheRoot.AsChar(), config.artifactLimits, config.cacheCoordination}))
        {
            VANGUARD_DELETE(implementation);
            return false;
        }
        m_impl = implementation;
        return true;
    }

    bool ProjectLoosePublisher::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return true;
        if (!m_impl->artifacts.Shutdown())
            return false;
        VANGUARD_DELETE(m_impl);
        m_impl = nullptr;
        return true;
    }

    bool ProjectLoosePublisher::IsInitialized() const noexcept
    {
        return m_impl != nullptr;
    }

    bool ProjectLoosePublisher::IsCommittedExact(const assets::DependencyRecord& expected) const noexcept
    {
        if (m_impl == nullptr || !expected.output.IsValid() || !expected.output.IsTyped())
            return false;
        assets::DependencyRecord current;
        return m_impl->index->Find(expected.output, current) == assets::IndexResult::Success && SameRecord(current, expected);
    }

    ProjectLoosePublicationResult ProjectLoosePublisher::ValidateExact(
        const assets::DependencyRecord& expected, ProjectLoosePublicationFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        const auto fail = [failure](const ProjectLoosePublicationResult result,
                                    const assets::IndexResult index = assets::IndexResult::Success,
                                    const assets::LooseMaterializationResult materialization = assets::LooseMaterializationResult::Success) noexcept
        {
            if (failure != nullptr)
                *failure = {result, index, materialization};
            return result;
        };
        if (m_impl == nullptr)
            return fail(ProjectLoosePublicationResult::InvalidState);
        if (!expected.output.IsValid() || !expected.output.IsTyped() || expected.buildFingerprint.IsEmpty() ||
            expected.contentFingerprint.IsEmpty() || expected.artifacts.Empty())
            return fail(ProjectLoosePublicationResult::InvalidArgument);

        assets::DependencyRecord current;
        const assets::IndexResult found = m_impl->index->Find(expected.output, current);
        if (found != assets::IndexResult::Success)
            return fail(found == assets::IndexResult::NotFound ? ProjectLoosePublicationResult::RecordNotFound
                                                               : ProjectLoosePublicationResult::IndexFailure,
                        found);
        if (!SameRecord(current, expected) || expected.target != m_impl->config.target)
            return fail(ProjectLoosePublicationResult::TargetMismatch);

        assets::ArtifactSetReader reader;
        const assets::DerivedDataArtifactResult opened =
            m_impl->artifacts.Open({expected.buildFingerprint, expected.contentFingerprint}, reader);
        if (opened != assets::DerivedDataArtifactResult::Success)
        {
            const assets::LooseMaterializationResult materialization = MapArtifactValidation(opened);
            return fail(MapMaterialization(materialization), assets::IndexResult::Success, materialization);
        }

        const containers::ArraySpan<const assets::CachedArtifactDescriptor> cached = reader.Artifacts();
        if (cached.Count() != expected.artifacts.Size())
            return fail(ProjectLoosePublicationResult::MaterializationFailure, assets::IndexResult::Success,
                        assets::LooseMaterializationResult::DescriptorMismatch);
        for (u32 index = 0; index < cached.Count(); ++index)
        {
            const assets::CachedArtifactDescriptor& left = cached[index];
            const assets::IndexedArtifact& right = expected.artifacts[index];
            if (left.resource != right.resource || left.segment != right.segment || left.flags != right.flags ||
                left.alignmentLog2 != right.alignmentLog2 || left.byteCount != right.byteCount)
                return fail(ProjectLoosePublicationResult::MaterializationFailure, assets::IndexResult::Success,
                            assets::LooseMaterializationResult::DescriptorMismatch);
        }
        return ProjectLoosePublicationResult::Success;
    }

    ProjectLoosePublicationResult ProjectLoosePublisher::PrepareExact(
        const assets::DependencyRecord& expected, containers::DynamicArray<streaming::DependencyDescriptor>& dependencies,
        u64& expectedContentCrc64, ProjectLoosePublicationFailure* const failure,
        ProjectLoosePublicationInfo* const publication) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (publication != nullptr)
            *publication = {};
        dependencies.Clear();
        expectedContentCrc64 = 0u;
        const auto fail = [failure](const ProjectLoosePublicationResult result, const assets::IndexResult index = assets::IndexResult::Success,
                                    const assets::LooseMaterializationResult materialization = assets::LooseMaterializationResult::Success) noexcept
        {
            if (failure != nullptr)
                *failure = {result, index, materialization};
            return result;
        };
        if (m_impl == nullptr)
            return fail(ProjectLoosePublicationResult::InvalidState);
        if (!expected.output.IsValid() || !expected.output.IsTyped())
            return fail(ProjectLoosePublicationResult::InvalidArgument);

        assets::DependencyRecord current;
        const assets::IndexResult found = m_impl->index->Find(expected.output, current);
        if (found != assets::IndexResult::Success)
            return fail(found == assets::IndexResult::NotFound ? ProjectLoosePublicationResult::RecordNotFound
                                                               : ProjectLoosePublicationResult::IndexFailure,
                        found);
        if (!SameRecord(current, expected) || expected.target != m_impl->config.target)
            return fail(ProjectLoosePublicationResult::TargetMismatch);

        if (publication != nullptr)
        {
            publication->buildFingerprint = expected.buildFingerprint;
            publication->contentFingerprint = expected.contentFingerprint;
            publication->priority = m_impl->config.priority;
        }
        dependencies.Reserve(expected.dependencies.Size());
        if (dependencies.Capacity() < expected.dependencies.Size())
            return fail(ProjectLoosePublicationResult::OutOfMemory);
        for (const assets::BuildDependency& dependency : expected.dependencies)
        {
            if (dependency.role != assets::DependencyRole::Generated)
                continue;
            if (dependencies.Size() >= m_impl->config.maximumRuntimeDependencies)
                return fail(ProjectLoosePublicationResult::LimitExceeded);
            const u32 previous = dependencies.Size();
            dependencies.PushBack({dependency.identity, RuntimeKind(dependency.requirement)});
            if (dependencies.Size() != previous + 1u)
                return fail(ProjectLoosePublicationResult::OutOfMemory);
        }

        char first[3]{HexDigit(expected.buildFingerprint.bytes[0] >> 4u), HexDigit(expected.buildFingerprint.bytes[0]), '\0'};
        char second[3]{HexDigit(expected.buildFingerprint.bytes[1] >> 4u), HexDigit(expected.buildFingerprint.bytes[1]), '\0'};
        const filesystem::AbsolutePath directory = m_impl->config.looseRoot.AddDirPath(first).AddDirPath(second);
        if (directory.Empty() || !filesystem::GetManager().CreatePath(directory))
            return fail(ProjectLoosePublicationResult::DirectoryFailure);

        char name[87]{};
        WriteHex64(name, expected.output.GetPath().Id());
        name[16] = '-';
        WriteFingerprint(name + 17, expected.buildFingerprint);
        constexpr char suffix[] = ".vres";
        for (u32 index = 0; index < sizeof(suffix); ++index)
            name[81u + index] = suffix[index];
        const filesystem::AbsolutePath target = directory.AddFilePath(name);
        containers::String temporaryName(name);
        temporaryName.Append(".tmp", 4);
        const filesystem::AbsolutePath temporary = directory.AddFilePath(temporaryName);
        if (target.Empty() || temporary.Empty())
            return fail(ProjectLoosePublicationResult::InvalidArgument);

        assets::LooseMaterializationInfo materialized;
        const assets::LooseMaterializationResult materialization =
            m_impl->materializer.Materialize(expected, expected.output, m_impl->artifacts, target, temporary,
                                             m_impl->config.materializationLimits, &materialized);
        if (materialization != assets::LooseMaterializationResult::Success)
            return fail(MapMaterialization(materialization), assets::IndexResult::Success, materialization);

        expectedContentCrc64 = materialized.contentCrc64;
        if (publication != nullptr)
            publication->physicalPath = target;
        return ProjectLoosePublicationResult::Success;
    }

    bool ProjectLoosePublisher::PublishPrepared(const containers::ArraySpan<const streaming::LooseResourceDescriptor> resources) noexcept
    {
        return m_impl != nullptr && m_impl->streamer->PublishLooseBatch(resources);
    }

    bool ProjectLoosePublisher::PrepareVisibility(const containers::ArraySpan<const streaming::LooseResourceDescriptor> resources,
                                                  streaming::PreparedLooseBatch& batch) noexcept
    {
        return m_impl != nullptr && m_impl->streamer->PrepareLooseBatch(resources, batch);
    }

    bool ProjectLoosePublisher::CommitPreparedVisibility(streaming::PreparedLooseBatch& batch) noexcept
    {
        return m_impl != nullptr && m_impl->streamer->CommitPreparedLooseBatch(batch);
    }

    ProjectLoosePublicationResult ProjectLoosePublisher::Publish(const resources::ResourceReference resource,
                                                                  ProjectLoosePublicationFailure* const failure,
                                                                  ProjectLoosePublicationInfo* const publication) noexcept
    {
        if (failure != nullptr)
            *failure = {};
        if (publication != nullptr)
            *publication = {};
        const auto fail = [failure](const ProjectLoosePublicationResult result, const assets::IndexResult index = assets::IndexResult::Success,
                                    const assets::LooseMaterializationResult materialization = assets::LooseMaterializationResult::Success) noexcept
        {
            if (failure != nullptr)
                *failure = {result, index, materialization};
            return result;
        };
        if (m_impl == nullptr)
            return fail(ProjectLoosePublicationResult::InvalidState);
        if (!resource.IsValid() || !resource.IsTyped())
            return fail(ProjectLoosePublicationResult::InvalidArgument);

        assets::DependencyRecord record;
        const assets::IndexResult found = m_impl->index->Find(resource, record);
        if (found != assets::IndexResult::Success)
            return fail(found == assets::IndexResult::NotFound ? ProjectLoosePublicationResult::RecordNotFound
                                                               : ProjectLoosePublicationResult::IndexFailure,
                        found);
        if (record.output != resource || record.target != m_impl->config.target)
            return fail(ProjectLoosePublicationResult::TargetMismatch);
        if (publication != nullptr)
        {
            // These fields describe committed VADI authority and remain useful
            // even when materialization or streamer installation later fails.
            publication->buildFingerprint = record.buildFingerprint;
            publication->contentFingerprint = record.contentFingerprint;
        }

        const ProjectLoosePublicationResult validation = ValidateExact(record, failure);
        if (validation != ProjectLoosePublicationResult::Success)
            return validation;

        containers::DynamicArray<streaming::DependencyDescriptor> dependencies(memory::pools::Assets::GetInstance());
        u64 expectedContentCrc64 = 0u;
        ProjectLoosePublicationInfo prepared;
        const ProjectLoosePublicationResult preparedResult =
            PrepareExact(record, dependencies, expectedContentCrc64, failure, &prepared);
        if (preparedResult != ProjectLoosePublicationResult::Success)
        {
            if (publication != nullptr)
            {
                publication->buildFingerprint = prepared.buildFingerprint;
                publication->contentFingerprint = prepared.contentFingerprint;
            }
            return preparedResult;
        }

        const streaming::LooseResourceDescriptor descriptor{resource, prepared.physicalPath,
                                                             {dependencies.TypedData(), dependencies.Size()},
                                                             expectedContentCrc64, m_impl->config.priority};
        if (!PublishPrepared({&descriptor, 1u}))
            return fail(ProjectLoosePublicationResult::StreamingPublicationFailure);
        if (publication != nullptr)
            *publication = prepared;
        return ProjectLoosePublicationResult::Success;
    }
} // namespace vanguard::asset_streaming
