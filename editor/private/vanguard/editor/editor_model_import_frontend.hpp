#pragma once

#include <vanguard/assets/asset_metadata.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/filesystem/filesystem.hpp>

namespace vanguard::editor
{
    // Must remain equivalent to the exact-current ModelAssetCompiler source
    // extension set. Asset Browser uses this before its ordinary immediate
    // ImportExternalSource path so case variants cannot bypass model review.
    [[nodiscard]] inline bool IsEditorModelImportSourcePath(const containers::StringView sourcePath) noexcept
    {
        const containers::StringView extension = filesystem::paths::GetExtension(sourcePath);
        return (extension.Length() == 4u && extension.StartsWithIgnoreCase("gltf")) ||
               (extension.Length() == 3u && extension.StartsWithIgnoreCase("glb"));
    }

    enum class EditorModelImportOfferResult : u8
    {
        Accepted,
        Busy,
        Unavailable,
        InvalidArgument
    };

    // Main-thread presentation handoff for Asset Browser's existing bounded
    // external-file capture. The bytes are copied into one independent per-source
    // session before this returns; no source/VMETA/project mutation occurs here.
    [[nodiscard]] EditorModelImportOfferResult OfferEditorModelImportExternalSnapshot(
        const filesystem::AbsolutePath& source,
        containers::ArraySpan<const u8> sourceSnapshot,
        u32 destinationRoot,
        containers::StringView rootRelativeFolder) noexcept;

    // Opens a reviewed Refresh/Reimport-from-Source session for one already
    // accepted model. Unlike RequestAssetReimport this re-inspects current source
    // bytes and may reconcile the model-owned output schema, but still commits
    // nothing until the user explicitly applies the reviewed plan.
    [[nodiscard]] EditorModelImportOfferResult OfferEditorModelReimportFromSource(
        assets::AssetId asset,
        u64 expectedAcceptedGeneration,
        u64 expectedSourceGeneration) noexcept;
} // namespace vanguard::editor
