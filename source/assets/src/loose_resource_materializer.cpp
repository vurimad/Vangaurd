#include <vanguard/assets/loose_resource_materializer.hpp>

#include <vanguard/serialization/serialization.hpp>

namespace
{
    using namespace vanguard;
    namespace assets = vanguard::assets;

    [[nodiscard]] assets::LooseMaterializationResult MapOpenResult(const assets::DerivedDataArtifactResult result) noexcept
    {
        switch (result)
        {
        case assets::DerivedDataArtifactResult::Success: return assets::LooseMaterializationResult::Success;
        case assets::DerivedDataArtifactResult::NotFound: return assets::LooseMaterializationResult::ArtifactNotFound;
        case assets::DerivedDataArtifactResult::InvalidArgument: return assets::LooseMaterializationResult::InvalidArgument;
        case assets::DerivedDataArtifactResult::InvalidState: return assets::LooseMaterializationResult::InvalidState;
        case assets::DerivedDataArtifactResult::ContentMismatch:
        case assets::DerivedDataArtifactResult::Corrupt: return assets::LooseMaterializationResult::CorruptArtifactSet;
        case assets::DerivedDataArtifactResult::DescriptorMismatch: return assets::LooseMaterializationResult::DescriptorMismatch;
        case assets::DerivedDataArtifactResult::IoFailure: return assets::LooseMaterializationResult::IoFailure;
        case assets::DerivedDataArtifactResult::OutOfMemory: return assets::LooseMaterializationResult::OutOfMemory;
        case assets::DerivedDataArtifactResult::LimitExceeded: return assets::LooseMaterializationResult::LimitExceeded;
        }
        return assets::LooseMaterializationResult::InvalidState;
    }

} // namespace

namespace vanguard::assets
{
    const char* ToString(const LooseMaterializationResult result) noexcept
    {
        switch (result)
        {
        case LooseMaterializationResult::Success: return "Success";
        case LooseMaterializationResult::InvalidArgument: return "InvalidArgument";
        case LooseMaterializationResult::InvalidState: return "InvalidState";
        case LooseMaterializationResult::ResourceNotFound: return "ResourceNotFound";
        case LooseMaterializationResult::MissingSegment: return "MissingSegment";
        case LooseMaterializationResult::DuplicateSegment: return "DuplicateSegment";
        case LooseMaterializationResult::DescriptorMismatch: return "DescriptorMismatch";
        case LooseMaterializationResult::ArtifactNotFound: return "ArtifactNotFound";
        case LooseMaterializationResult::CorruptArtifactSet: return "CorruptArtifactSet";
        case LooseMaterializationResult::ValidationFailed: return "ValidationFailed";
        case LooseMaterializationResult::PublicationFailed: return "PublicationFailed";
        case LooseMaterializationResult::IoFailure: return "IoFailure";
        case LooseMaterializationResult::OutOfMemory: return "OutOfMemory";
        case LooseMaterializationResult::LimitExceeded: return "LimitExceeded";
        }
        return "Unknown";
    }

    LooseMaterializationResult LooseResourceMaterializer::Materialize(const DependencyRecord& record,
                                                                      const resources::ResourceReference resource,
                                                                      const DerivedDataArtifactSource& source,
                                                                      const filesystem::AbsolutePath& target,
                                                                      const filesystem::AbsolutePath& temporary,
                                                                      const LooseMaterializationLimits& limits) const noexcept
    {
        if (!resource.IsValid() || !resource.IsTyped() || target.Empty() || temporary.Empty() || !target.IsFilePath() ||
            !temporary.IsFilePath() || target == temporary ||
            filesystem::paths::ParentAbsolutePath(target) != filesystem::paths::ParentAbsolutePath(temporary) || !limits.IsValid())
        {
            return LooseMaterializationResult::InvalidArgument;
        }
        if (!source.IsInitialized() || record.buildFingerprint.IsEmpty() || record.contentFingerprint.IsEmpty())
            return LooseMaterializationResult::InvalidState;

        filesystem::Manager& manager = filesystem::GetManager();
        if (manager.FileExist(temporary) && !manager.DeleteFile(temporary))
            return LooseMaterializationResult::PublicationFailed;

        u32 selectedCount = 0;
        for (const IndexedArtifact& artifact : record.artifacts)
        {
            if (artifact.resource == resource)
            {
                if (selectedCount >= limits.maximumSegments)
                    return LooseMaterializationResult::LimitExceeded;
                ++selectedCount;
            }
        }
        if (selectedCount == 0)
            return LooseMaterializationResult::ResourceNotFound;

        containers::DynamicArray<IndexedArtifact> selected(memory::pools::Assets::GetInstance());
        containers::DynamicArray<u8> present(memory::pools::Assets::GetInstance());
        selected.Resize(selectedCount);
        present.Resize(selectedCount);
        if (selected.Size() != selectedCount || present.Size() != selectedCount)
            return LooseMaterializationResult::OutOfMemory;
        for (u8& value : present)
            value = 0;
        for (const IndexedArtifact& artifact : record.artifacts)
        {
            if (artifact.resource != resource)
                continue;
            if (artifact.segment >= selectedCount)
                return LooseMaterializationResult::MissingSegment;
            if (present[artifact.segment] != 0)
                return LooseMaterializationResult::DuplicateSegment;
            selected[artifact.segment] = artifact;
            present[artifact.segment] = 1;
        }

        u64 totalBytes = 0;
        for (u32 index = 0; index < selected.Size(); ++index)
        {
            if (present[index] == 0)
                return LooseMaterializationResult::MissingSegment;
            const IndexedArtifact& artifact = selected[index];
            if (artifact.byteCount == 0 || artifact.byteCount > limits.maximumResourceBytes - totalBytes)
                return LooseMaterializationResult::LimitExceeded;
            totalBytes += artifact.byteCount;
        }

        ArtifactSetReader reader;
        const DerivedDataArtifactResult openResult =
            source.Open({record.buildFingerprint, record.contentFingerprint}, reader);
        if (openResult != DerivedDataArtifactResult::Success)
            return MapOpenResult(openResult);
        for (const IndexedArtifact& indexed : selected)
        {
            const CachedArtifactDescriptor* const stored = reader.Find(indexed.resource, indexed.segment);
            if (stored == nullptr)
                return LooseMaterializationResult::DescriptorMismatch;
            if (stored->flags != indexed.flags || stored->alignmentLog2 != indexed.alignmentLog2 ||
                stored->byteCount != indexed.byteCount)
            {
                return LooseMaterializationResult::DescriptorMismatch;
            }
        }

        auto output = manager.CreateFileWriter(temporary, filesystem::FOF_Buffered);
        if (!output)
            return LooseMaterializationResult::IoFailure;

        containers::DynamicArray<u8> scratch(memory::pools::Assets::GetInstance());
        scratch.Resize(limits.scratchBytes);
        if (scratch.Size() != limits.scratchBytes)
        {
            output.Reset();
            static_cast<void>(manager.DeleteFile(temporary));
            return LooseMaterializationResult::OutOfMemory;
        }

        crypto::Sha256Builder expectedHash;
        for (const IndexedArtifact& indexed : selected)
        {
            const CachedArtifactDescriptor expected{indexed.resource, indexed.segment, indexed.flags,
                                                    indexed.alignmentLog2, indexed.byteCount};
            const DerivedDataArtifactResult copied = reader.CopyTo(expected, *output, scratch, &expectedHash);
            if (copied != DerivedDataArtifactResult::Success)
            {
                output.Reset();
                static_cast<void>(manager.DeleteFile(temporary));
                return MapOpenResult(copied);
            }
        }
        output->Flush();
        const bool written = !output->HasErrors() && output->GetSize() == totalBytes;
        output.Reset();
        BuildFingerprint expectedDigest;
        if (!written || !expectedHash.Finalize(expectedDigest))
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return LooseMaterializationResult::IoFailure;
        }

        auto staged = manager.CreateFileReader(temporary, filesystem::FOF_Buffered);
        if (!staged || staged->GetSize() != totalBytes)
        {
            staged.Reset();
            static_cast<void>(manager.DeleteFile(temporary));
            return LooseMaterializationResult::ValidationFailed;
        }
        serialization::BinaryReader stagedReader(*staged);
        crypto::Sha256Builder actualHash;
        u64 remaining = totalBytes;
        while (remaining != 0)
        {
            const u32 batch = static_cast<u32>(remaining < scratch.Size() ? remaining : scratch.Size());
            if (!stagedReader.ReadBytes(scratch.Data(), batch) || !actualHash.Update(scratch.Data(), batch))
            {
                staged.Reset();
                static_cast<void>(manager.DeleteFile(temporary));
                return LooseMaterializationResult::ValidationFailed;
            }
            remaining -= batch;
        }
        BuildFingerprint actualDigest;
        const bool valid = stagedReader.Position() == totalBytes && !staged->HasErrors() && actualHash.Finalize(actualDigest) &&
                           actualDigest == expectedDigest;
        staged.Reset();
        if (!valid)
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return LooseMaterializationResult::ValidationFailed;
        }

        if (!filesystem::ReplaceFile(temporary, target))
        {
            static_cast<void>(manager.DeleteFile(temporary));
            return LooseMaterializationResult::PublicationFailed;
        }
        return LooseMaterializationResult::Success;
    }
} // namespace vanguard::assets
