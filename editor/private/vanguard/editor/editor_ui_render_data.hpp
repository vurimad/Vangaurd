#pragma once

#include <vanguard/concurrency/atomic.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/editor/editor_ui.hpp>
#include <vanguard/memory/pool.hpp>

namespace vanguard::editor::detail
{
    inline constexpr u32 MaximumEditorUiVerticesPerHost = 4u * 1024u * 1024u;
    inline constexpr u32 MaximumEditorUiIndicesPerHost = 8u * 1024u * 1024u;
    inline constexpr u32 MaximumEditorUiCommandsPerHost = 256u * 1024u;
    inline constexpr u64 MaximumEditorUiTextureUploadBytesPerFrame = 64ull * 1024ull * 1024ull;

    struct EditorUiVertex
    {
        f32 position[2]{};
        f32 uv[2]{};
        u32 color = 0;
    };

    enum class EditorUiRenderCommandKind : u8
    {
        Draw,
        ResetState
    };

    struct EditorUiRenderCommand
    {
        EditorUiRenderCommandKind kind = EditorUiRenderCommandKind::Draw;
        u32 elementCount = 0;
        u32 indexOffset = 0;
        u32 vertexOffset = 0;
        u32 texture = ~u32{0};
        // Host-local framebuffer pixels, already adjusted for display origin and DPI.
        f32 clipMinimum[2]{};
        f32 clipMaximum[2]{};
    };

    struct EditorUiRenderTexture
    {
        EditorTextureHandle identity;
        rhi::Texture texture;
        rhi::SamplerState sampler;
        EditorTextureColorSpace colorSpace = EditorTextureColorSpace::DisplaySrgb;
    };

    struct EditorUiRenderData
    {
        EditorUiRenderData() noexcept
            : vertices(memory::pools::Editor::GetInstance()), indices(memory::pools::Editor::GetInstance()),
              commands(memory::pools::Editor::GetInstance()), textures(memory::pools::Editor::GetInstance())
        {
        }

        concurrency::Atomic<u32> references{1};
        f32 displayPosition[2]{};
        f32 displayExtent[2]{};
        f32 framebufferScale[2]{1.0f, 1.0f};
        containers::DynamicArray<EditorUiVertex> vertices;
        containers::DynamicArray<u16> indices;
        containers::DynamicArray<EditorUiRenderCommand> commands;
        containers::DynamicArray<EditorUiRenderTexture> textures;
    };

    struct EditorUiTextureUpload
    {
        EditorTextureHandle identity;
        rhi::Texture texture;
        u32 width = 0;
        u32 height = 0;
        u32 rowPitch = 0;
        u64 dataOffset = 0;
        u64 dataSize = 0;
    };

    struct EditorUiTextureUpdateData
    {
        EditorUiTextureUpdateData() noexcept
            : uploads(memory::pools::Editor::GetInstance()), pixels(memory::pools::Editor::GetInstance())
        {
        }

        concurrency::Atomic<u32> references{1};
        containers::DynamicArray<EditorUiTextureUpload> uploads;
        containers::DynamicArray<u8> pixels;
    };

    [[nodiscard]] EditorUiRenderData* AllocateEditorUiRenderData() noexcept;
    void RetainEditorUiRenderData(void* data) noexcept;
    void ReleaseEditorUiRenderData(void* data) noexcept;
    [[nodiscard]] EditorUiTextureUpdateData* AllocateEditorUiTextureUpdateData() noexcept;
    void RetainEditorUiTextureUpdateData(void* data) noexcept;
    void ReleaseEditorUiTextureUpdateData(void* data) noexcept;
} // namespace vanguard::editor::detail
