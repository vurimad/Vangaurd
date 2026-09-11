#include <vanguard/editor/editor_ui_render_data.hpp>

#include <vanguard/memory/memory.hpp>

#include <new>

namespace vanguard::editor::detail
{
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
