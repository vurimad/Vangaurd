#include <vanguard/editor/editor_ui_render_data.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::editor::detail
{
    bool ResolveEditorTexturePublicationImport(const EditorTextureRenderOutputPublication& publication,
                                               rendering::RetainedTextureImportDesc& import) noexcept
    {
        if (!publication.IsValid())
            return false;
        rendering::RenderOutputReceipt receipt;
        if (!publication.ResolveReceipt(receipt) || !receipt.IsReady() || receipt.viewport != publication.viewport ||
            receipt.outputRevision != publication.outputRevision)
            return false;

        import.initialState = receipt.terminalState;
        import.terminalState = receipt.terminalState;
        import.initialQueue = receipt.terminalQueue;
        import.terminalQueue = receipt.terminalQueue;
        switch (receipt.readiness)
        {
        case rendering::RenderOutputPublicationReadinessKind::SameQueueContinuation:
            import.readiness = rendering::ImportReadinessKind::SameQueueContinuation;
            import.incomingWait = {};
            return true;
        case rendering::RenderOutputPublicationReadinessKind::ExplicitFence:
            import.readiness = rendering::ImportReadinessKind::ExplicitFenceWait;
            import.incomingWait = receipt.readyFence;
            return import.incomingWait.IsValid();
        default:
            return false;
        }
    }

    bool ResolveEditorUiTextureImport(const EditorUiRenderTexture& texture,
                                      rendering::RetainedTextureImportDesc& import) noexcept
    {
        import = {};
        import.texture = texture.texture.GetRef();
        import.token = {rhi::ResourceRef(import.texture).value};
        if (!import.texture.IsValid() || !rhi::GetTextureDesc(import.texture, import.expected))
            return false;

        if (!texture.publication.IsSpecified())
        {
            import.initialState = import.terminalState = import.expected.initialState;
            return true;
        }

        return ResolveEditorTexturePublicationImport(texture.publication, import);
    }

    EditorUiRenderData* AllocateEditorUiRenderData() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor, sizeof(EditorUiRenderData), alignof(EditorUiRenderData));
        return block ? ::new (block.address) EditorUiRenderData() : nullptr;
    }

    void RetainEditorUiRenderData(void* const data) noexcept
    {
        auto* const renderData = static_cast<EditorUiRenderData*>(data);
        if (renderData != nullptr)
            static_cast<void>(renderData->references.Increment());
    }

    void ReleaseEditorUiRenderData(void* const data) noexcept
    {
        auto* const renderData = static_cast<EditorUiRenderData*>(data);
        if (renderData == nullptr || renderData->references.Decrement() != 0)
            return;
        renderData->~EditorUiRenderData();
        memory::MemoryBlock block{renderData, sizeof(EditorUiRenderData), memory::PoolId::Editor};
        memory::Free(block);
    }

    EditorUiTextureUpdateData* AllocateEditorUiTextureUpdateData() noexcept
    {
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor, sizeof(EditorUiTextureUpdateData), alignof(EditorUiTextureUpdateData));
        return block ? ::new (block.address) EditorUiTextureUpdateData() : nullptr;
    }

    void RetainEditorUiTextureUpdateData(void* const data) noexcept
    {
        auto* const updateData = static_cast<EditorUiTextureUpdateData*>(data);
        if (updateData != nullptr)
            static_cast<void>(updateData->references.Increment());
    }

    void ReleaseEditorUiTextureUpdateData(void* const data) noexcept
    {
        auto* const updateData = static_cast<EditorUiTextureUpdateData*>(data);
        if (updateData == nullptr || updateData->references.Decrement() != 0)
            return;
        updateData->~EditorUiTextureUpdateData();
        memory::MemoryBlock block{updateData, sizeof(EditorUiTextureUpdateData), memory::PoolId::Editor};
        memory::Free(block);
    }
} // namespace vanguard::editor::detail
