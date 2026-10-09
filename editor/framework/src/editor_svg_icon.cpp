#include <vanguard/editor/editor_svg_icon.hpp>

#include <vanguard/containers/containers.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4127 4244 4267 4456 4457 4458 4702 4706 4996)
#endif
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace
{
    namespace containers = vanguard::containers;
    namespace editor = vanguard::editor;

    [[nodiscard]] bool PrepareSvgDocument(containers::String& document) noexcept
    {
        if (document.Empty() || document.Length() > 256u * 1024u)
            return false;
        const char* const source = document.AsChar();
        if (source == nullptr || std::strstr(source, "<svg") == nullptr ||
            std::strstr(source, "<image") != nullptr || std::strstr(source, "<text") != nullptr ||
            std::strstr(source, "href=") != nullptr)
            return false;

        // NanoSVG does not implement CSS currentColor. Editor SvgIcon resources use currentColor
        // deliberately so one canonical asset can be tinted by UI state. Normalize only the parser's
        // temporary mutable copy; the resource on disk remains byte-identical to the authored SVG.
        const containers::String currentColor("currentColor");
        const containers::String white("#ffffff");
        static_cast<void>(document.ReplaceAll(currentColor, white));
        return true;
    }
} // namespace

namespace vanguard::editor::detail
{
    bool LoadEditorSvgIcon(const filesystem::AbsolutePath& path, EditorSvgIcon& output) noexcept
    {
        if (path.Empty() || !path.IsFilePath() || output.texture.Status != ImTextureStatus_Destroyed)
            return false;

        containers::String document;
        if (!filesystem::LoadFileToString(path, document) || !PrepareSvgDocument(document))
            return false;

        NSVGimage* const image = nsvgParse(document.AsChar(), "px", 96.0f);
        if (image == nullptr || !std::isfinite(image->width) || !std::isfinite(image->height) ||
            !(image->width > 0.0f) || !(image->height > 0.0f) || image->width > 4096.0f || image->height > 4096.0f)
        {
            if (image != nullptr)
                nsvgDelete(image);
            return false;
        }

        NSVGrasterizer* const rasterizer = nsvgCreateRasterizer();
        if (rasterizer == nullptr)
        {
            nsvgDelete(image);
            return false;
        }

        output.texture.Create(ImTextureFormat_RGBA32, EditorSvgIconRasterExtent, EditorSvgIconRasterExtent);
        auto* const pixels = static_cast<unsigned char*>(output.texture.GetPixels());
        const float scale = std::min(static_cast<float>(EditorSvgIconRasterExtent) / image->width,
                                     static_cast<float>(EditorSvgIconRasterExtent) / image->height);
        const float translationX = (static_cast<float>(EditorSvgIconRasterExtent) - image->width * scale) * 0.5f;
        const float translationY = (static_cast<float>(EditorSvgIconRasterExtent) - image->height * scale) * 0.5f;
        nsvgRasterize(rasterizer, image, translationX, translationY, scale, pixels,
                      EditorSvgIconRasterExtent, EditorSvgIconRasterExtent, EditorSvgIconRasterExtent * 4);

        // The toolbar tints the canonical currentColor artwork at draw time. Keep the raster as
        // white-alpha coverage so hover/active/disabled colors cannot inherit parser RGB state.
        const int pixelCount = EditorSvgIconRasterExtent * EditorSvgIconRasterExtent;
        for (int pixel = 0; pixel < pixelCount; ++pixel)
        {
            pixels[pixel * 4 + 0] = 255;
            pixels[pixel * 4 + 1] = 255;
            pixels[pixel * 4 + 2] = 255;
        }
        output.texture.UseColors = false;

        nsvgDeleteRasterizer(rasterizer);
        nsvgDelete(image);
        return output.IsValid();
    }

    void DrawEditorSvgIcon(ImDrawList& drawList, EditorSvgIcon& icon,
                           const ImVec2 minimum, const float extent, const ImU32 color) noexcept
    {
        if (!icon.IsValid() || !(extent > 0.0f) || color == 0)
            return;
        drawList.AddImage(icon.texture.GetTexRef(), minimum,
                          {minimum.x + extent, minimum.y + extent},
                          {0.0f, 0.0f}, {1.0f, 1.0f}, color);
    }
} // namespace vanguard::editor::detail
