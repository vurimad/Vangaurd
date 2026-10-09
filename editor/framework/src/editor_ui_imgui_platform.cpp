#include <vanguard/editor/editor_ui_imgui_platform.hpp>
#include <vanguard/editor/editor_ui_imgui.hpp>
#include <vanguard/editor/editor_ui_render_data.hpp>
#include <vanguard/editor/editor_resources.hpp>
#include <vanguard/editor/editor_svg_icon.hpp>
#include <vanguard/editor/editor_toolbar_icons.hpp>
#include <vanguard/editor/editor_visual_style.hpp>

#include <vanguard/engine/input_service.hpp>
#include <vanguard/engine/window_service.hpp>
#include <vanguard/concurrency/thread.hpp>
#include <vanguard/containers/containers.hpp>
#include <vanguard/diagnostics/diagnostics.hpp>
#include <vanguard/memory/memory.hpp>
#include <vanguard/memory/pool.hpp>
#include <vanguard/system/time.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

namespace
{
    namespace editor = vanguard::editor;
    namespace engine = vanguard::engine;
    namespace input = vanguard::input;
    namespace window = vanguard::window;

    constexpr vanguard::u32 MaximumBrandWordmarkSegments = 2048;
    constexpr float BrandWordmarkVanguardMaximumX = 885.0f;
    constexpr int BrandWordmarkTextureHeight = 64;
    constexpr int BrandWordmarkTexturePadding = 3;
    constexpr int BrandWordmarkCoverageSamples = 4;
    constexpr int MaximumBrandWordmarkTextureWidth = 1024;

    struct BrandWordmarkSegment final
    {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
    };

    struct BrandWordmarkGeometry final
    {
        BrandWordmarkSegment segments[MaximumBrandWordmarkSegments]{};
        vanguard::u32 count = 0;
        float minimumX = 0.0f;
        float minimumY = 0.0f;
        float maximumX = 0.0f;
        float maximumY = 0.0f;
        bool hasBounds = false;
    };

    void SkipSvgSeparators(const char*& cursor, const char* const end) noexcept
    {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ','))
            ++cursor;
    }

    [[nodiscard]] bool ReadSvgFloat(const char*& cursor, const char* const end, float& value) noexcept
    {
        SkipSvgSeparators(cursor, end);
        if (cursor >= end)
            return false;
        char* parsedEnd = nullptr;
        value = std::strtof(cursor, &parsedEnd);
        if (parsedEnd == cursor || parsedEnd > end || !std::isfinite(value))
            return false;
        cursor = parsedEnd;
        return true;
    }

    void IncludeBrandWordmarkPoint(BrandWordmarkGeometry& geometry, const float x, const float y) noexcept
    {
        if (!geometry.hasBounds)
        {
            geometry.minimumX = geometry.maximumX = x;
            geometry.minimumY = geometry.maximumY = y;
            geometry.hasBounds = true;
            return;
        }
        geometry.minimumX = x < geometry.minimumX ? x : geometry.minimumX;
        geometry.minimumY = y < geometry.minimumY ? y : geometry.minimumY;
        geometry.maximumX = x > geometry.maximumX ? x : geometry.maximumX;
        geometry.maximumY = y > geometry.maximumY ? y : geometry.maximumY;
    }

    [[nodiscard]] bool AppendBrandWordmarkSegment(BrandWordmarkGeometry& geometry, const float x0, const float y0,
                                                  const float x1, const float y1) noexcept
    {
        const bool firstVanguard = x0 <= BrandWordmarkVanguardMaximumX;
        const bool secondVanguard = x1 <= BrandWordmarkVanguardMaximumX;
        if (!firstVanguard && !secondVanguard)
            return true;
        if (firstVanguard != secondVanguard)
            return false;
        if (geometry.count >= MaximumBrandWordmarkSegments)
            return false;
        geometry.segments[geometry.count++] = {x0, y0, x1, y1};
        IncludeBrandWordmarkPoint(geometry, x0, y0);
        IncludeBrandWordmarkPoint(geometry, x1, y1);
        return true;
    }

    [[nodiscard]] bool ParseBrandWordmarkSvg(const char* const document, BrandWordmarkGeometry& geometry) noexcept
    {
        geometry = {};
        if (document == nullptr)
            return false;
        const char* const path = std::strstr(document, "<path");
        if (path == nullptr)
            return false;
        const char* const dataAttribute = std::strstr(path, "d=\"");
        if (dataAttribute == nullptr)
            return false;
        const char* cursor = dataAttribute + 3;
        const char* const end = std::strchr(cursor, '"');
        if (end == nullptr || cursor == end)
            return false;

        char command = 0;
        float currentX = 0.0f;
        float currentY = 0.0f;
        float contourX = 0.0f;
        float contourY = 0.0f;
        bool contourOpen = false;
        while (cursor < end)
        {
            SkipSvgSeparators(cursor, end);
            if (cursor >= end)
                break;
            if (*cursor == 'M' || *cursor == 'L' || *cursor == 'Z')
                command = *cursor++;
            else if (command == 0)
                return false;

            if (command == 'M')
            {
                float x = 0.0f;
                float y = 0.0f;
                if (!ReadSvgFloat(cursor, end, x) || !ReadSvgFloat(cursor, end, y))
                    return false;
                currentX = contourX = x;
                currentY = contourY = y;
                contourOpen = true;
                command = 'L';
                continue;
            }
            if (command == 'L')
            {
                if (!contourOpen)
                    return false;
                float x = 0.0f;
                float y = 0.0f;
                if (!ReadSvgFloat(cursor, end, x) || !ReadSvgFloat(cursor, end, y) ||
                    !AppendBrandWordmarkSegment(geometry, currentX, currentY, x, y))
                    return false;
                currentX = x;
                currentY = y;
                continue;
            }
            if (command == 'Z')
            {
                if (!contourOpen || !AppendBrandWordmarkSegment(geometry, currentX, currentY, contourX, contourY))
                    return false;
                currentX = contourX;
                currentY = contourY;
                contourOpen = false;
                command = 0;
                continue;
            }
            return false;
        }
        return geometry.count != 0 && geometry.hasBounds && geometry.maximumX > geometry.minimumX && geometry.maximumY > geometry.minimumY;
    }

    [[nodiscard]] bool RasterizeBrandWordmark(const BrandWordmarkGeometry& geometry, ImTextureData& texture,
                                              float& aspectRatio) noexcept
    {
        const float sourceWidth = geometry.maximumX - geometry.minimumX;
        const float sourceHeight = geometry.maximumY - geometry.minimumY;
        constexpr int contentHeight = BrandWordmarkTextureHeight - BrandWordmarkTexturePadding * 2;
        if (!(sourceWidth > 0.0f) || !(sourceHeight > 0.0f) || contentHeight <= 0)
            return false;
        const float scale = static_cast<float>(contentHeight) / sourceHeight;
        const int width = static_cast<int>(std::ceil(sourceWidth * scale)) + BrandWordmarkTexturePadding * 2;
        if (width <= 0 || width > MaximumBrandWordmarkTextureWidth)
            return false;

        texture.Create(ImTextureFormat_RGBA32, width, BrandWordmarkTextureHeight);
        auto* const pixels = static_cast<unsigned char*>(texture.GetPixels());
        const int pixelCount = width * BrandWordmarkTextureHeight;
        for (int pixel = 0; pixel < pixelCount; ++pixel)
        {
            pixels[pixel * 4 + 0] = 255;
            pixels[pixel * 4 + 1] = 255;
            pixels[pixel * 4 + 2] = 255;
            pixels[pixel * 4 + 3] = 0;
        }

        float intersections[MaximumBrandWordmarkSegments]{};
        float coverage[MaximumBrandWordmarkTextureWidth]{};
        for (int y = BrandWordmarkTexturePadding; y < BrandWordmarkTextureHeight - BrandWordmarkTexturePadding; ++y)
        {
            for (int x = 0; x < width; ++x)
                coverage[x] = 0.0f;
            for (int sample = 0; sample < BrandWordmarkCoverageSamples; ++sample)
            {
                const float subpixelY = (static_cast<float>(sample) + 0.5f) / static_cast<float>(BrandWordmarkCoverageSamples);
                const float sourceY = geometry.minimumY +
                                      (static_cast<float>(y - BrandWordmarkTexturePadding) + subpixelY) / scale;
                vanguard::u32 intersectionCount = 0;
                for (vanguard::u32 index = 0; index < geometry.count; ++index)
                {
                    const BrandWordmarkSegment& segment = geometry.segments[index];
                    const bool crosses = (segment.y0 <= sourceY && segment.y1 > sourceY) ||
                                         (segment.y1 <= sourceY && segment.y0 > sourceY);
                    if (!crosses)
                        continue;
                    if (intersectionCount >= MaximumBrandWordmarkSegments)
                        return false;
                    const float amount = (sourceY - segment.y0) / (segment.y1 - segment.y0);
                    const float sourceX = segment.x0 + (segment.x1 - segment.x0) * amount;
                    intersections[intersectionCount++] = static_cast<float>(BrandWordmarkTexturePadding) +
                                                         (sourceX - geometry.minimumX) * scale;
                }
                if ((intersectionCount & 1u) != 0u)
                    return false;
                std::sort(intersections, intersections + intersectionCount);
                for (vanguard::u32 index = 0; index < intersectionCount; index += 2u)
                {
                    const float left = intersections[index] > 0.0f ? intersections[index] : 0.0f;
                    const float right = intersections[index + 1u] < static_cast<float>(width)
                                            ? intersections[index + 1u]
                                            : static_cast<float>(width);
                    if (!(right > left))
                        continue;
                    int first = static_cast<int>(std::floor(left));
                    int last = static_cast<int>(std::ceil(right)) - 1;
                    first = first < 0 ? 0 : first;
                    last = last >= width ? width - 1 : last;
                    for (int x = first; x <= last; ++x)
                    {
                        const float overlapLeft = left > static_cast<float>(x) ? left : static_cast<float>(x);
                        const float overlapRight = right < static_cast<float>(x + 1) ? right : static_cast<float>(x + 1);
                        if (overlapRight > overlapLeft)
                            coverage[x] += (overlapRight - overlapLeft) / static_cast<float>(BrandWordmarkCoverageSamples);
                    }
                }
            }
            for (int x = 0; x < width; ++x)
            {
                const float alpha = coverage[x] < 0.0f ? 0.0f : (coverage[x] > 1.0f ? 1.0f : coverage[x]);
                pixels[(y * width + x) * 4 + 3] = static_cast<unsigned char>(alpha * 255.0f + 0.5f);
            }
        }
        aspectRatio = static_cast<float>(width) / static_cast<float>(BrandWordmarkTextureHeight);
        return aspectRatio > 0.0f && std::isfinite(aspectRatio);
    }

    void ClearFailure(editor::EditorUiFailure* const failure) noexcept
    {
        if (failure != nullptr)
            *failure = {};
    }

    [[nodiscard]] bool Fail(editor::EditorUiFailure* const failure, const char* const message) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = editor::EditorUiFailureCode::PlatformFailure;
            failure->message = message;
        }
        return false;
    }

    bool FailCode(editor::EditorUiFailure* const failure, const editor::EditorUiFailureCode code,
                  const char* const message) noexcept
    {
        if (failure != nullptr)
        {
            failure->code = code;
            failure->message = message;
        }
        return false;
    }

    void ClearManualIniDirtyState() noexcept
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return;
        ImGui::GetIO().WantSaveIniSettings = false;
        ImGui::GetCurrentContext()->SettingsDirtyTimer = 0.0f;
    }

    [[nodiscard]] ImGuiDockNode* FindPersistedCentralDockNode(ImGuiDockNode* const node) noexcept
    {
        if (node == nullptr)
            return nullptr;
        if (node->IsCentralNode())
            return node;
        if (ImGuiDockNode* const left = FindPersistedCentralDockNode(node->ChildNodes[0]))
            return left;
        return FindPersistedCentralDockNode(node->ChildNodes[1]);
    }

    [[nodiscard]] bool SavedDisplayLess(const editor::detail::EditorUiSavedDisplay& left,
                                        const editor::detail::EditorUiSavedDisplay& right) noexcept
    {
        if (left.primary != right.primary)
            return left.primary && !right.primary;
        if (left.boundsX != right.boundsX) return left.boundsX < right.boundsX;
        if (left.boundsY != right.boundsY) return left.boundsY < right.boundsY;
        if (left.boundsWidth != right.boundsWidth) return left.boundsWidth < right.boundsWidth;
        if (left.boundsHeight != right.boundsHeight) return left.boundsHeight < right.boundsHeight;
        if (left.workX != right.workX) return left.workX < right.workX;
        if (left.workY != right.workY) return left.workY < right.workY;
        if (left.workWidth != right.workWidth) return left.workWidth < right.workWidth;
        if (left.workHeight != right.workHeight) return left.workHeight < right.workHeight;
        if (left.desktopPixelWidth != right.desktopPixelWidth) return left.desktopPixelWidth < right.desktopPixelWidth;
        if (left.desktopPixelHeight != right.desktopPixelHeight) return left.desktopPixelHeight < right.desktopPixelHeight;
        return left.scaleQ16_16 < right.scaleQ16_16;
    }

    void* ImGuiAllocate(const size_t size, void*) noexcept
    {
        return VANGUARD_ALLOCATE(vanguard::memory::pools::Editor, size);
    }

    void ImGuiFree(void* const allocation, void*) noexcept
    {
        if (allocation != nullptr)
            VANGUARD_FREE(vanguard::memory::pools::Editor, allocation);
    }

    [[nodiscard]] void* WindowToken(const window::WindowHandle handle) noexcept
    {
        const vanguard::u64 value = (static_cast<vanguard::u64>(handle.generation) << 32u) | (static_cast<vanguard::u64>(handle.index) + 1u);
        return reinterpret_cast<void*>(static_cast<uintptr_t>(value));
    }

    [[nodiscard]] ImGuiKey TranslateKey(const input::Key key) noexcept
    {
        using enum input::Key;
        switch (key)
        {
        case Tab: return ImGuiKey_Tab;
        case Left: return ImGuiKey_LeftArrow;
        case Right: return ImGuiKey_RightArrow;
        case Up: return ImGuiKey_UpArrow;
        case Down: return ImGuiKey_DownArrow;
        case PageUp: return ImGuiKey_PageUp;
        case PageDown: return ImGuiKey_PageDown;
        case Home: return ImGuiKey_Home;
        case End: return ImGuiKey_End;
        case Insert: return ImGuiKey_Insert;
        case Delete: return ImGuiKey_Delete;
        case Backspace: return ImGuiKey_Backspace;
        case Space: return ImGuiKey_Space;
        case Enter: return ImGuiKey_Enter;
        case Escape: return ImGuiKey_Escape;
        case LeftControl: return ImGuiKey_LeftCtrl;
        case LeftShift: return ImGuiKey_LeftShift;
        case LeftAlt: return ImGuiKey_LeftAlt;
        case LeftGui: return ImGuiKey_LeftSuper;
        case RightControl: return ImGuiKey_RightCtrl;
        case RightShift: return ImGuiKey_RightShift;
        case RightAlt: return ImGuiKey_RightAlt;
        case RightGui: return ImGuiKey_RightSuper;
        case Application: return ImGuiKey_Menu;
        case Digit0: return ImGuiKey_0;
        case Digit1: return ImGuiKey_1;
        case Digit2: return ImGuiKey_2;
        case Digit3: return ImGuiKey_3;
        case Digit4: return ImGuiKey_4;
        case Digit5: return ImGuiKey_5;
        case Digit6: return ImGuiKey_6;
        case Digit7: return ImGuiKey_7;
        case Digit8: return ImGuiKey_8;
        case Digit9: return ImGuiKey_9;
        case A: return ImGuiKey_A;
        case B: return ImGuiKey_B;
        case C: return ImGuiKey_C;
        case D: return ImGuiKey_D;
        case E: return ImGuiKey_E;
        case F: return ImGuiKey_F;
        case G: return ImGuiKey_G;
        case H: return ImGuiKey_H;
        case I: return ImGuiKey_I;
        case J: return ImGuiKey_J;
        case K: return ImGuiKey_K;
        case L: return ImGuiKey_L;
        case M: return ImGuiKey_M;
        case N: return ImGuiKey_N;
        case O: return ImGuiKey_O;
        case P: return ImGuiKey_P;
        case Q: return ImGuiKey_Q;
        case R: return ImGuiKey_R;
        case S: return ImGuiKey_S;
        case T: return ImGuiKey_T;
        case U: return ImGuiKey_U;
        case V: return ImGuiKey_V;
        case W: return ImGuiKey_W;
        case X: return ImGuiKey_X;
        case Y: return ImGuiKey_Y;
        case Z: return ImGuiKey_Z;
        case F1: return ImGuiKey_F1;
        case F2: return ImGuiKey_F2;
        case F3: return ImGuiKey_F3;
        case F4: return ImGuiKey_F4;
        case F5: return ImGuiKey_F5;
        case F6: return ImGuiKey_F6;
        case F7: return ImGuiKey_F7;
        case F8: return ImGuiKey_F8;
        case F9: return ImGuiKey_F9;
        case F10: return ImGuiKey_F10;
        case F11: return ImGuiKey_F11;
        case F12: return ImGuiKey_F12;
        case F13: return ImGuiKey_F13;
        case F14: return ImGuiKey_F14;
        case F15: return ImGuiKey_F15;
        case F16: return ImGuiKey_F16;
        case F17: return ImGuiKey_F17;
        case F18: return ImGuiKey_F18;
        case F19: return ImGuiKey_F19;
        case F20: return ImGuiKey_F20;
        case F21: return ImGuiKey_F21;
        case F22: return ImGuiKey_F22;
        case F23: return ImGuiKey_F23;
        case F24: return ImGuiKey_F24;
        case Apostrophe: return ImGuiKey_Apostrophe;
        case Comma: return ImGuiKey_Comma;
        case Minus: return ImGuiKey_Minus;
        case Period: return ImGuiKey_Period;
        case Slash: return ImGuiKey_Slash;
        case Semicolon: return ImGuiKey_Semicolon;
        case Equals: return ImGuiKey_Equal;
        case LeftBracket: return ImGuiKey_LeftBracket;
        case Backslash: return ImGuiKey_Backslash;
        case RightBracket: return ImGuiKey_RightBracket;
        case Grave: return ImGuiKey_GraveAccent;
        case CapsLock: return ImGuiKey_CapsLock;
        case ScrollLock: return ImGuiKey_ScrollLock;
        case NumLock: return ImGuiKey_NumLock;
        case PrintScreen: return ImGuiKey_PrintScreen;
        case Pause: return ImGuiKey_Pause;
        case Keypad0: return ImGuiKey_Keypad0;
        case Keypad1: return ImGuiKey_Keypad1;
        case Keypad2: return ImGuiKey_Keypad2;
        case Keypad3: return ImGuiKey_Keypad3;
        case Keypad4: return ImGuiKey_Keypad4;
        case Keypad5: return ImGuiKey_Keypad5;
        case Keypad6: return ImGuiKey_Keypad6;
        case Keypad7: return ImGuiKey_Keypad7;
        case Keypad8: return ImGuiKey_Keypad8;
        case Keypad9: return ImGuiKey_Keypad9;
        case KeypadPeriod: return ImGuiKey_KeypadDecimal;
        case KeypadDivide: return ImGuiKey_KeypadDivide;
        case KeypadMultiply: return ImGuiKey_KeypadMultiply;
        case KeypadMinus: return ImGuiKey_KeypadSubtract;
        case KeypadPlus: return ImGuiKey_KeypadAdd;
        case KeypadEnter: return ImGuiKey_KeypadEnter;
        case KeypadEquals: return ImGuiKey_KeypadEqual;
        case NonUsBackslash: return ImGuiKey_Oem102;
        case ApplicationControlBack: return ImGuiKey_AppBack;
        case ApplicationControlForward: return ImGuiKey_AppForward;
        default: return ImGuiKey_None;
        }
    }

    [[nodiscard]] int TranslateMouseButton(const input::MouseButton button) noexcept
    {
        switch (button)
        {
        case input::MouseButton::Left: return 0;
        case input::MouseButton::Right: return 1;
        case input::MouseButton::Middle: return 2;
        case input::MouseButton::Extra1: return 3;
        case input::MouseButton::Extra2: return 4;
        default: return -1;
        }
    }
} // namespace

namespace vanguard::editor::detail
{
    struct EditorUiImGuiPlatform::Impl
    {
        Impl() noexcept : clipboard(memory::pools::Editor::GetInstance()), managedTextures(memory::pools::Editor::GetInstance())
        {
            clipboard.Reserve(256);
            managedTextures.Reserve(8);
        }

        struct ViewportRecord
        {
            ImGuiViewport* viewport = nullptr;
            window::WindowHandle window;
            EditorHostWindowHandle host;
            bool owned = false;
            bool active = false;
        };

        struct ManagedTextureRecord
        {
            ImTextureData* source = nullptr;
            EditorTextureHandle identity;
        };

        struct SvgIconRecord final
        {
            EditorResourceId resource = InvalidEditorResourceId;
            EditorSvgIcon icon;
            bool active = false;
            bool registered = false;
        };

        EditorUiService* ui = nullptr;
        engine::WindowService* windowService = nullptr;
        engine::InputService* inputService = nullptr;
        ImGuiContext* context = nullptr;
        window::WindowEventCursor eventCursor;
        window::WindowHandle mouseWindow;
        ViewportRecord viewports[window::MaximumWindows]{};
        containers::DynamicArray<char> clipboard;
        containers::DynamicArray<ManagedTextureRecord> managedTextures;
        rhi::SamplerState textureSampler;
        SvgIconRecord svgIcons[EditorToolbarIconCount]{};
        ImTextureData vanguardWordmarkTexture;
        float vanguardWordmarkAspectRatio = 0.0f;
        bool vanguardWordmarkRegistered = false;
        const char* deferredFailure = nullptr;
        ImGuiMouseCursor appliedCursor = ImGuiMouseCursor_COUNT;
        bool appliedCursorVisible = true;
        bool frameActive = false;
        bool frameEverBegun = false;
        bool layoutLoadAttempted = false;
        u64 inputCaptureFrame = 0;
        u64 diagnosticsHeartbeatTick = 0;
        bool frameDiagnosticsEnabled = false;
        bool preWantCaptureKeyboard = false;
        bool preWantTextInput = false;
        bool postImeWantTextInput = false;
        float userUiScale = 1.0f;
        float appliedUserUiScale = 1.0f;

        void ResetInputCapture() noexcept
        {
            inputCaptureFrame = 0;
            preWantCaptureKeyboard = false;
            preWantTextInput = false;
            postImeWantTextInput = false;
        }

        [[nodiscard]] window::WindowManager& Windows() const noexcept { return windowService->GetManager(); }

        [[nodiscard]] bool InitializeToolbarIcons(EditorResourceService& resources) noexcept
        {
            const containers::ArraySpan<const EditorToolbarIconDescriptor> descriptors =
                GetEditorToolbarIconDescriptors();
            if (descriptors.Size() != EditorToolbarIconCount)
                return false;
            for (u32 index = 0; index < descriptors.Size(); ++index)
            {
                EditorResourceLocation location;
                if (!resources.Resolve(descriptors[index].resource.id, location) ||
                    location.kind != EditorResourceKind::SvgIcon ||
                    !LoadEditorSvgIcon(location.path, svgIcons[index].icon))
                    return false;
                svgIcons[index].resource = descriptors[index].resource.id;
                ImGui::RegisterUserTexture(&svgIcons[index].icon.texture);
                svgIcons[index].registered = true;
                svgIcons[index].active = true;
            }
            return true;
        }

        void ReleaseToolbarIcons() noexcept
        {
            for (SvgIconRecord& record : svgIcons)
            {
                if (record.registered)
                    ImGui::UnregisterUserTexture(&record.icon.texture);
                record.registered = false;
                record.active = false;
                record.resource = InvalidEditorResourceId;
            }
        }

        [[nodiscard]] EditorSvgIcon* FindSvgIcon(const EditorResourceId resource) noexcept
        {
            if (resource == InvalidEditorResourceId)
                return nullptr;
            for (SvgIconRecord& record : svgIcons)
                if (record.active && record.resource == resource)
                    return &record.icon;
            return nullptr;
        }

        void InitializeVanguardWordmark(EditorResourceService& resources) noexcept
        {
            EditorResourceLocation location;
            if (!resources.Resolve(EditorVanguardEngineWordmarkResourceId, location) ||
                location.kind != EditorResourceKind::SvgImage)
                return;
            containers::String document;
            if (!filesystem::LoadFileToString(location.path, document))
                return;
            BrandWordmarkGeometry geometry;
            if (!ParseBrandWordmarkSvg(document.AsChar(), geometry) ||
                !RasterizeBrandWordmark(geometry, vanguardWordmarkTexture, vanguardWordmarkAspectRatio))
                return;
            ImGui::RegisterUserTexture(&vanguardWordmarkTexture);
            vanguardWordmarkRegistered = true;
        }

        void ReleaseVanguardWordmark() noexcept
        {
            if (!vanguardWordmarkRegistered)
                return;
            ImGui::UnregisterUserTexture(&vanguardWordmarkTexture);
            vanguardWordmarkRegistered = false;
            vanguardWordmarkAspectRatio = 0.0f;
        }

        static void ResetRenderState(const ImDrawList*, const ImDrawCmd*) noexcept {}

        static void OnChangedViewport(ImGuiViewport* const viewport) noexcept
        {
            Impl* const impl = Get();
            if (impl == nullptr || viewport == nullptr)
                return;
            editor::detail::ApplyEditorVisualStyle(impl->appliedUserUiScale, viewport->DpiScale);
        }

        [[nodiscard]] ViewportRecord* AllocateViewport() noexcept
        {
            for (ViewportRecord& record : viewports)
                if (!record.active)
                {
                    record = {};
                    record.active = true;
                    return &record;
                }
            deferredFailure = "editor native viewport capacity is exhausted";
            return nullptr;
        }

        [[nodiscard]] ViewportRecord* FindViewport(const window::WindowHandle handle) noexcept
        {
            for (ViewportRecord& record : viewports)
                if (record.active && record.window == handle)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] window::DisplayHandle DisplayFor(const ImVec2 position) noexcept
        {
            window::DisplaySnapshot displays[window::MaximumDisplays]{};
            const u32 count = Windows().VisitDisplays(displays, window::MaximumDisplays);
            for (u32 index = 0; index < count; ++index)
            {
                const window::WindowRect& bounds = displays[index].bounds;
                if (position.x >= static_cast<f32>(bounds.origin.x) && position.y >= static_cast<f32>(bounds.origin.y) &&
                    position.x < static_cast<f32>(bounds.origin.x + static_cast<i32>(bounds.extent.width)) &&
                    position.y < static_cast<f32>(bounds.origin.y + static_cast<i32>(bounds.extent.height)))
                    return displays[index].handle;
            }
            return Windows().GetPrimaryDisplay();
        }

        void RecordFailure(const char* const message) noexcept
        {
            if (deferredFailure == nullptr)
                deferredFailure = message;
        }

        static Impl* Get() noexcept
        {
            return ImGui::GetCurrentContext() != nullptr ? static_cast<Impl*>(ImGui::GetIO().BackendPlatformUserData) : nullptr;
        }

        static ViewportRecord* GetRecord(ImGuiViewport* const viewport) noexcept
        {
            return viewport != nullptr ? static_cast<ViewportRecord*>(viewport->PlatformUserData) : nullptr;
        }

        static void CreatePlatformWindow(ImGuiViewport* const viewport) noexcept
        {
            Impl* const impl = Get();
            if (impl == nullptr || viewport == nullptr)
                return;
            ViewportRecord* const record = impl->AllocateViewport();
            if (record == nullptr)
                return;
            record->viewport = viewport;
            record->owned = true;
            viewport->PlatformUserData = record;

            window::WindowDescriptor descriptor;
            descriptor.title = "Vanguard Editor";
            descriptor.role = window::WindowRole::EditorViewport;
            descriptor.relationship = window::WindowRelationship::EditorPlatformViewport;
            descriptor.parent = impl->windowService->GetPrimaryWindow();
            descriptor.placement.position = {static_cast<i32>(viewport->Pos.x), static_cast<i32>(viewport->Pos.y)};
            descriptor.placement.logicalExtent = {viewport->Size.x > 1.0f ? static_cast<u32>(viewport->Size.x) : 1u,
                                                  viewport->Size.y > 1.0f ? static_cast<u32>(viewport->Size.y) : 1u};
            descriptor.placement.display = impl->DisplayFor(viewport->Pos);
            descriptor.placement.visible = false;
            descriptor.initialPlacement = window::InitialWindowPlacement::Explicit;
            descriptor.flags = window::WindowFlag::Resizable | window::WindowFlag::HighPixelDensity |
                               window::WindowFlag::AcceptFileDrop;
            if ((viewport->Flags & ImGuiViewportFlags_NoDecoration) != 0)
                descriptor.flags = descriptor.flags | window::WindowFlag::Borderless;
            if ((viewport->Flags & ImGuiViewportFlags_TopMost) != 0)
                descriptor.flags = descriptor.flags | window::WindowFlag::AlwaysOnTop;
            if ((viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) != 0)
                descriptor.flags = descriptor.flags | window::WindowFlag::Utility | window::WindowFlag::SkipTaskbar;

            window::Failure windowFailure;
            if (!impl->Windows().Create(descriptor, record->window, &windowFailure))
            {
                impl->RecordFailure(windowFailure.message != nullptr ? windowFailure.message : "editor native viewport creation failed");
                return;
            }
            EditorUiFailure editorFailure;
            if (!impl->ui->RegisterHost({record->window, {}, {}, false}, record->host, &editorFailure))
            {
                static_cast<void>(impl->Windows().DestroyWindow(record->window, &windowFailure));
                record->window = {};
                impl->RecordFailure(editorFailure.message != nullptr ? editorFailure.message : "editor native viewport registration failed");
                return;
            }
            viewport->PlatformHandle = WindowToken(record->window);
        }

        static void DestroyWindow(ImGuiViewport* const viewport) noexcept
        {
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr)
                return;
            if (record->owned)
            {
                EditorUiFailure editorFailure;
                if (record->host.IsValid() && !impl->ui->UnregisterHost(record->host, &editorFailure))
                    impl->RecordFailure(editorFailure.message != nullptr ? editorFailure.message : "editor native viewport unregistration failed");
                window::WindowSnapshot state;
                window::Failure windowFailure;
                if (record->window.IsValid() && impl->Windows().GetSnapshot(record->window, state))
                {
                    const bool destroyed = state.lifecycle == window::WindowLifecycleState::CloseRequested
                                               ? impl->Windows().ResolveCloseRequest(record->window, state.closeRequestSerial, window::CloseDecision::Accept, &windowFailure)
                                               : impl->Windows().DestroyWindow(record->window, &windowFailure);
                    if (!destroyed)
                        impl->RecordFailure(windowFailure.message != nullptr ? windowFailure.message : "editor native viewport destruction failed");
                }
            }
            if (impl->mouseWindow == record->window)
                impl->mouseWindow = {};
            *record = {};
            viewport->PlatformUserData = nullptr;
            viewport->PlatformHandle = nullptr;
            viewport->PlatformHandleRaw = nullptr;
        }

        static void ShowWindow(ImGuiViewport* const viewport) noexcept { RequestPlacement(viewport, window::WindowStateField::Visibility); }
        static void SetWindowPosition(ImGuiViewport* const viewport, const ImVec2 position) noexcept
        {
            RequestPlacement(viewport, window::WindowStateField::Position, &position, nullptr);
        }
        static void SetWindowSize(ImGuiViewport* const viewport, const ImVec2 size) noexcept
        {
            RequestPlacement(viewport, window::WindowStateField::LogicalExtent, nullptr, &size);
        }

        static void RequestPlacement(ImGuiViewport* const viewport, const window::WindowStateField field, const ImVec2* const position = nullptr,
                                     const ImVec2* const extent = nullptr) noexcept
        {
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || !record->window.IsValid())
                return;
            window::WindowSnapshot current;
            if (!impl->Windows().GetSnapshot(record->window, current))
            {
                impl->RecordFailure("editor native viewport state is unavailable");
                return;
            }
            window::WindowStateRequest request;
            request.fields = field;
            request.placement = current.requested;
            if (position != nullptr)
                request.placement.position = {static_cast<i32>(position->x), static_cast<i32>(position->y)};
            if (extent != nullptr)
                request.placement.logicalExtent = {extent->x > 1.0f ? static_cast<u32>(extent->x) : 1u,
                                                   extent->y > 1.0f ? static_cast<u32>(extent->y) : 1u};
            if (field == window::WindowStateField::Visibility)
            {
                request.placement.visible = true;
                request.activateWhenShown = (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) == 0;
            }
            window::Failure failure;
            if (!impl->Windows().RequestState(record->window, request, &failure))
                impl->RecordFailure(failure.message != nullptr ? failure.message : "editor native viewport state change failed");
        }

        static ImVec2 GetWindowPosition(ImGuiViewport* const viewport) noexcept
        {
            const window::WindowSnapshot state = ReadWindow(viewport);
            return {static_cast<f32>(state.nativeState.placement.position.x), static_cast<f32>(state.nativeState.placement.position.y)};
        }

        static ImVec2 GetWindowSize(ImGuiViewport* const viewport) noexcept
        {
            const window::WindowSnapshot state = ReadWindow(viewport);
            return {static_cast<f32>(state.nativeState.placement.logicalExtent.width), static_cast<f32>(state.nativeState.placement.logicalExtent.height)};
        }

        static ImVec2 GetFramebufferScale(ImGuiViewport* const viewport) noexcept
        {
            const window::WindowSnapshot state = ReadWindow(viewport);
            const window::WindowExtent logical = state.nativeState.placement.logicalExtent;
            return {logical.width != 0 ? static_cast<f32>(state.nativeState.pixelExtent.width) / static_cast<f32>(logical.width) : 1.0f,
                    logical.height != 0 ? static_cast<f32>(state.nativeState.pixelExtent.height) / static_cast<f32>(logical.height) : 1.0f};
        }

        static void SetWindowFocus(ImGuiViewport* const viewport) noexcept
        {
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || !record->window.IsValid())
                return;
            window::Failure failure;
            static_cast<void>(impl->Windows().RequestFocus(record->window, &failure));
        }

        static void SetWindowAlpha(ImGuiViewport* const viewport, const float alpha) noexcept
        {
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || !record->window.IsValid())
                return;
            window::Failure failure;
            if (!impl->Windows().SetOpacity(record->window, alpha, &failure))
                impl->RecordFailure(failure.message != nullptr ? failure.message : "editor native viewport opacity change failed");
        }

        static bool GetWindowFocus(ImGuiViewport* const viewport) noexcept { return ReadWindow(viewport).nativeState.focused; }
        static bool GetWindowMinimized(ImGuiViewport* const viewport) noexcept { return ReadWindow(viewport).nativeState.minimized; }

        static void SetWindowTitle(ImGuiViewport* const viewport, const char* const title) noexcept
        {
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || !record->window.IsValid())
                return;
            // ImGui derives platform titles from the rendered portion of its window name. Internal
            // docking host windows are named with a leading "##" (for example ##DockNode_x), so
            // FindRenderedTextEnd() legitimately produces an empty platform title while a detached
            // viewport is transitioning/resizing. WindowManager intentionally rejects empty titles;
            // preserve the last valid native title instead of turning this transient ImGui state
            // into a fatal editor-frame failure.
            if (title == nullptr || title[0] == '\0')
                return;
            window::Failure failure;
            if (!impl->Windows().SetTitle(record->window, title, &failure))
                impl->RecordFailure(failure.message != nullptr ? failure.message : "editor native viewport title change failed");
        }

        static f32 GetWindowDpiScale(ImGuiViewport* const viewport) noexcept { return ReadWindow(viewport).nativeState.contentScale; }

        static const char* GetClipboardText(ImGuiContext*) noexcept
        {
            Impl* const impl = Get();
            if (impl == nullptr)
                return nullptr;
            u32 requiredCapacity = 0;
            window::Failure failure;
            if (!impl->Windows().ReadClipboardText(nullptr, 0, requiredCapacity, &failure) || requiredCapacity == 0)
                return nullptr;
            impl->clipboard.Resize(requiredCapacity);
            if (!impl->Windows().ReadClipboardText(impl->clipboard.TypedData(), impl->clipboard.Size(), requiredCapacity, &failure))
                return nullptr;
            return impl->clipboard.TypedData();
        }

        static void SetClipboardText(ImGuiContext*, const char* const text) noexcept
        {
            Impl* const impl = Get();
            if (impl == nullptr || text == nullptr)
                return;
            window::Failure failure;
            static_cast<void>(impl->Windows().WriteClipboardText(text, &failure));
        }

        static void SetImeData(ImGuiContext*, ImGuiViewport* const viewport, ImGuiPlatformImeData* const data) noexcept
        {
            Impl* const impl = Get();
            if (impl != nullptr && data != nullptr)
                impl->postImeWantTextInput = impl->postImeWantTextInput || data->WantTextInput;
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || data == nullptr || !record->window.IsValid())
                return;
            window::TextInputRequest request;
            request.enabled = data->WantVisible || data->WantTextInput;
            request.showIme = data->WantVisible;
            request.position = {static_cast<i32>(data->InputPos.x - viewport->Pos.x), static_cast<i32>(data->InputPos.y - viewport->Pos.y)};
            request.lineHeight = data->InputLineHeight > 1.0f ? static_cast<u32>(data->InputLineHeight) : 1u;
            window::Failure failure;
            static_cast<void>(impl->Windows().SetTextInput(record->window, request, &failure));
        }

        [[nodiscard]] static window::CursorShape TranslateCursor(const ImGuiMouseCursor cursor) noexcept
        {
            switch (cursor)
            {
            case ImGuiMouseCursor_TextInput: return window::CursorShape::TextInput;
            case ImGuiMouseCursor_ResizeAll: return window::CursorShape::Move;
            case ImGuiMouseCursor_ResizeNS: return window::CursorShape::ResizeNorthSouth;
            case ImGuiMouseCursor_ResizeEW: return window::CursorShape::ResizeEastWest;
            case ImGuiMouseCursor_ResizeNESW: return window::CursorShape::ResizeNorthEastSouthWest;
            case ImGuiMouseCursor_ResizeNWSE: return window::CursorShape::ResizeNorthWestSouthEast;
            case ImGuiMouseCursor_Hand: return window::CursorShape::Pointer;
            case ImGuiMouseCursor_Wait: return window::CursorShape::Wait;
            case ImGuiMouseCursor_Progress: return window::CursorShape::Progress;
            case ImGuiMouseCursor_NotAllowed: return window::CursorShape::NotAllowed;
            default: return window::CursorShape::Arrow;
            }
        }

        void UpdateCursor() noexcept
        {
            ImGuiIO& io = ImGui::GetIO();
            if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0)
                return;
            const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
            const bool visible = !io.MouseDrawCursor && cursor != ImGuiMouseCursor_None;
            if (cursor == appliedCursor && visible == appliedCursorVisible)
                return;
            window::Failure failure;
            if (Windows().SetCursor(TranslateCursor(cursor), visible, &failure))
            {
                appliedCursor = cursor;
                appliedCursorVisible = visible;
            }
        }

        [[nodiscard]] ManagedTextureRecord* FindManagedTexture(ImTextureData* const source) noexcept
        {
            for (ManagedTextureRecord& record : managedTextures)
                if (record.source == source)
                    return &record;
            return nullptr;
        }

        [[nodiscard]] bool CreateManagedTexture(ImTextureData& source, ManagedTextureRecord*& record, EditorUiFailure* const failure) noexcept
        {
            record = FindManagedTexture(&source);
            if (record != nullptr)
            {
                source.SetTexID(ToImGuiTextureId(record->identity));
                return true;
            }
            if (source.Format != ImTextureFormat_RGBA32 || source.Width <= 0 || source.Height <= 0 || source.Pixels == nullptr ||
                managedTextures.Size() >= MaximumEditorTextures || !textureSampler.IsValid())
                return Fail(failure, "editor UI texture creation request is invalid or unsupported");

            rhi::TextureDesc desc;
            desc.extent = {static_cast<u32>(source.Width), static_cast<u32>(source.Height), 1};
            desc.format = rhi::Format::R8G8B8A8UNorm;
            desc.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
            desc.initialState = rhi::ResourceState::ShaderResourceGraphics;
            rhi::Failure rhiFailure;
            rhi::Texture texture(rhi::AdoptReference, rhi::CreateTexture(desc, {}, &rhiFailure));
            if (!texture.IsValid())
                return Fail(failure, rhiFailure.message != nullptr ? rhiFailure.message : "editor UI texture creation failed");
            rhi::SetResourceDebugName(texture.GetRef(), "EditorUiTexture");

            EditorTextureHandle identity;
            if (!ui->RegisterTexture({texture.GetRef(), textureSampler.GetRef(), EditorTextureColorSpace::DisplaySrgb}, identity, failure))
                return false;
            managedTextures.PushBack({&source, identity});
            record = &managedTextures[managedTextures.Size() - 1u];
            source.SetTexID(ToImGuiTextureId(identity));
            return true;
        }

        [[nodiscard]] bool AppendTextureUpload(EditorUiTextureUpdateData& updateData, ImTextureData& source,
                                               const ManagedTextureRecord& record, EditorUiFailure* const failure) noexcept
        {
            if (source.Format != ImTextureFormat_RGBA32 || source.Width <= 0 || source.Height <= 0 || source.BytesPerPixel != 4 || source.Pixels == nullptr)
                return Fail(failure, "editor UI texture update request is invalid or unsupported");
            const u64 rowPitch = static_cast<u64>(source.Width) * static_cast<u64>(source.BytesPerPixel);
            const u64 dataSize = rowPitch * static_cast<u64>(source.Height);
            const u64 dataOffset = updateData.pixels.Size();
            if (rowPitch > ~u32{0} || dataSize == 0 || dataSize > MaximumEditorUiTextureUploadBytesPerFrame ||
                dataOffset > MaximumEditorUiTextureUploadBytesPerFrame - dataSize || updateData.uploads.Size() >= MaximumEditorTextures)
                return Fail(failure, "editor UI texture upload budget is exhausted");
            ResolvedEditorTexture resolved;
            if (!ui->ResolveTexture(record.identity, resolved))
                return Fail(failure, "editor UI managed texture identity became stale");

            const u64 nextSize = dataOffset + dataSize;
            updateData.pixels.Resize(static_cast<u32>(nextSize));
            std::memcpy(updateData.pixels.TypedData() + static_cast<u32>(dataOffset), source.Pixels, static_cast<size_t>(dataSize));
            EditorUiTextureUpload upload;
            upload.identity = record.identity;
            upload.texture.Reset(resolved.texture);
            upload.width = static_cast<u32>(source.Width);
            upload.height = static_cast<u32>(source.Height);
            upload.rowPitch = static_cast<u32>(rowPitch);
            upload.dataOffset = dataOffset;
            upload.dataSize = dataSize;
            updateData.uploads.PushBack(static_cast<EditorUiTextureUpload&&>(upload));
            return true;
        }

        void RollBackManagedTextures(const u32 firstCreated) noexcept
        {
            while (managedTextures.Size() > firstCreated)
            {
                ManagedTextureRecord& record = managedTextures[managedTextures.Size() - 1u];
                EditorUiFailure ignored;
                static_cast<void>(ui->UnregisterTexture(record.identity, &ignored));
                if (record.source != nullptr)
                    record.source->SetTexID(ImTextureID_Invalid);
                static_cast<void>(managedTextures.RemoveAt(managedTextures.Size() - 1u));
            }
        }

        [[nodiscard]] bool CaptureTextureUpdates(EditorUiTextureFrameData& frame, EditorUiFailure* const failure) noexcept
        {
            frame.Reset();
            ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
            bool needsUpload = false;
            for (ImTextureData* const source : platform.Textures)
                needsUpload = needsUpload || (source != nullptr && (source->Status == ImTextureStatus_WantCreate || source->Status == ImTextureStatus_WantUpdates));

            EditorUiTextureUpdateData* const updateData = needsUpload ? AllocateEditorUiTextureUpdateData() : nullptr;
            if (needsUpload && updateData == nullptr)
                return Fail(failure, "editor UI texture update data allocation failed");
            const u32 firstCreated = managedTextures.Size();
            for (ImTextureData* const source : platform.Textures)
            {
                if (source == nullptr)
                {
                    RollBackManagedTextures(firstCreated);
                    ReleaseEditorUiTextureUpdateData(updateData);
                    return Fail(failure, "editor UI texture request list contains a null entry");
                }
                if (source->Status != ImTextureStatus_WantCreate && source->Status != ImTextureStatus_WantUpdates)
                    continue;
                ManagedTextureRecord* record = FindManagedTexture(source);
                if (source->Status == ImTextureStatus_WantCreate && !CreateManagedTexture(*source, record, failure))
                {
                    RollBackManagedTextures(firstCreated);
                    ReleaseEditorUiTextureUpdateData(updateData);
                    return false;
                }
                if (record == nullptr || !AppendTextureUpload(*updateData, *source, *record, failure))
                {
                    RollBackManagedTextures(firstCreated);
                    ReleaseEditorUiTextureUpdateData(updateData);
                    return record != nullptr ? false : Fail(failure, "editor UI texture update has no managed texture");
                }
            }

            for (ImTextureData* const source : platform.Textures)
                if (source->Status == ImTextureStatus_WantCreate || source->Status == ImTextureStatus_WantUpdates)
                    source->SetStatus(ImTextureStatus_OK);
            for (u32 index = managedTextures.Size(); index > 0; --index)
            {
                ManagedTextureRecord& record = managedTextures[index - 1u];
                if (record.source == nullptr || record.source->Status != ImTextureStatus_WantDestroy)
                    continue;
                EditorUiFailure ignored;
                static_cast<void>(ui->UnregisterTexture(record.identity, &ignored));
                record.source->SetTexID(ImTextureID_Invalid);
                record.source->BackendUserData = nullptr;
                record.source->QueueUserData = nullptr;
                record.source->SetStatus(ImTextureStatus_Destroyed);
                static_cast<void>(managedTextures.RemoveAt(index - 1u));
            }

            if (updateData != nullptr && updateData->uploads.Size() != 0)
            {
                const EditorUiFramePayload payload{updateData, RetainEditorUiTextureUpdateData, ReleaseEditorUiTextureUpdateData};
                frame = EditorUiTextureFrameData(ui->GetFrame(), payload);
            }
            ReleaseEditorUiTextureUpdateData(updateData);
            return updateData == nullptr || frame.IsValid() ? true : Fail(failure, "editor UI retained texture data could not be created");
        }

        void ReleaseManagedTextures() noexcept
        {
            for (ManagedTextureRecord& record : managedTextures)
            {
                EditorUiFailure ignored;
                static_cast<void>(ui->UnregisterTexture(record.identity, &ignored));
                if (record.source != nullptr)
                {
                    record.source->SetTexID(ImTextureID_Invalid);
                    record.source->BackendUserData = nullptr;
                    record.source->QueueUserData = nullptr;
                    record.source->SetStatus(ImTextureStatus_Destroyed);
                }
            }
            managedTextures.Clear();
            textureSampler.Reset();
        }

        void RequeueTextureUpdates(EditorUiTextureFrameData& frame) noexcept
        {
            if (!frame.IsValid())
                return;
            auto* const updateData = static_cast<EditorUiTextureUpdateData*>(frame.GetPayload().data);
            if (updateData != nullptr)
                for (const EditorUiTextureUpload& upload : updateData->uploads)
                    for (ManagedTextureRecord& record : managedTextures)
                        if (record.identity == upload.identity && record.source != nullptr)
                        {
                            record.source->SetStatus(ImTextureStatus_WantUpdates);
                            break;
                        }
            frame.Reset();
        }

        [[nodiscard]] bool ResolveRenderTexture(EditorUiRenderData& renderData, const ImTextureID texture, u32& index,
                                                EditorUiFailure* const failure) noexcept
        {
            const EditorTextureHandle identity = FromImGuiTextureId(texture);
            if (!identity.IsValid())
                return Fail(failure, "editor UI draw command references an invalid texture identity");
            for (u32 candidate = 0; candidate < renderData.textures.Size(); ++candidate)
                if (renderData.textures[candidate].identity == identity)
                {
                    index = candidate;
                    return true;
                }
            ResolvedEditorTexture resolved;
            if (!ui->ResolveTexture(identity, resolved))
                return Fail(failure, "editor UI draw command references a stale texture identity");
            if (renderData.textures.Size() >= MaximumEditorTextures)
                return Fail(failure, "editor UI frame references too many textures");
            EditorUiRenderTexture retained;
            retained.identity = identity;
            retained.texture.Reset(resolved.texture);
            retained.sampler.Reset(resolved.sampler);
            retained.colorSpace = resolved.colorSpace;
            retained.publication = resolved.publication;
            renderData.textures.PushBack(static_cast<EditorUiRenderTexture&&>(retained));
            index = renderData.textures.Size() - 1u;
            return true;
        }

        [[nodiscard]] bool CaptureViewport(const ViewportRecord& viewportRecord, const ImDrawData& source, EditorUiFrameData& frame,
                                           EditorUiFailure* const failure) noexcept
        {
            if (!source.Valid || source.TotalVtxCount < 0 || source.TotalIdxCount < 0 || source.DisplaySize.x < 0.0f || source.DisplaySize.y < 0.0f ||
                source.FramebufferScale.x <= 0.0f || source.FramebufferScale.y <= 0.0f ||
                static_cast<u32>(source.TotalVtxCount) > MaximumEditorUiVerticesPerHost ||
                static_cast<u32>(source.TotalIdxCount) > MaximumEditorUiIndicesPerHost)
                return Fail(failure, "editor UI viewport draw data exceeds its retained-frame limits");
            u32 vertexCountTotal = 0;
            u32 indexCountTotal = 0;
            u32 commandCount = 0;
            for (const ImDrawList* const list : source.CmdLists)
            {
                if (list == nullptr || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0 || list->CmdBuffer.Size < 0 ||
                    static_cast<u32>(list->VtxBuffer.Size) > MaximumEditorUiVerticesPerHost - vertexCountTotal ||
                    static_cast<u32>(list->IdxBuffer.Size) > MaximumEditorUiIndicesPerHost - indexCountTotal ||
                    static_cast<u32>(list->CmdBuffer.Size) > MaximumEditorUiCommandsPerHost - commandCount)
                    return Fail(failure, "editor UI viewport contains invalid draw-list data");
                vertexCountTotal += static_cast<u32>(list->VtxBuffer.Size);
                indexCountTotal += static_cast<u32>(list->IdxBuffer.Size);
                commandCount += static_cast<u32>(list->CmdBuffer.Size);
            }
            if (vertexCountTotal != static_cast<u32>(source.TotalVtxCount) || indexCountTotal != static_cast<u32>(source.TotalIdxCount))
                return Fail(failure, "editor UI viewport draw-list totals are inconsistent");

            EditorUiRenderData* const renderData = AllocateEditorUiRenderData();
            if (renderData == nullptr)
                return Fail(failure, "editor UI retained render-data allocation failed");
            renderData->displayPosition[0] = source.DisplayPos.x;
            renderData->displayPosition[1] = source.DisplayPos.y;
            renderData->displayExtent[0] = source.DisplaySize.x;
            renderData->displayExtent[1] = source.DisplaySize.y;
            renderData->framebufferScale[0] = source.FramebufferScale.x;
            renderData->framebufferScale[1] = source.FramebufferScale.y;
            renderData->vertices.Reserve(static_cast<u32>(source.TotalVtxCount));
            renderData->indices.Reserve(static_cast<u32>(source.TotalIdxCount));
            renderData->commands.Reserve(commandCount);

            static_assert(sizeof(EditorUiVertex) == sizeof(ImDrawVert));
            static_assert(offsetof(EditorUiVertex, position) == offsetof(ImDrawVert, pos));
            static_assert(offsetof(EditorUiVertex, uv) == offsetof(ImDrawVert, uv));
            static_assert(offsetof(EditorUiVertex, color) == offsetof(ImDrawVert, col));
            static_assert(sizeof(ImDrawIdx) == sizeof(u16));

            const ImDrawCallback resetState = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
            for (const ImDrawList* const list : source.CmdLists)
            {
                const u32 vertexBase = renderData->vertices.Size();
                const u32 indexBase = renderData->indices.Size();
                const u32 vertexCount = static_cast<u32>(list->VtxBuffer.Size);
                const u32 indexCount = static_cast<u32>(list->IdxBuffer.Size);
                renderData->vertices.Resize(vertexBase + vertexCount);
                renderData->indices.Resize(indexBase + indexCount);
                if (vertexCount != 0)
                    std::memcpy(renderData->vertices.TypedData() + vertexBase, list->VtxBuffer.Data, static_cast<size_t>(vertexCount) * sizeof(EditorUiVertex));
                if (indexCount != 0)
                    std::memcpy(renderData->indices.TypedData() + indexBase, list->IdxBuffer.Data, static_cast<size_t>(indexCount) * sizeof(u16));

                for (const ImDrawCmd& sourceCommand : list->CmdBuffer)
                {
                    if (sourceCommand.UserCallback != nullptr)
                    {
                        if (sourceCommand.UserCallback != resetState && sourceCommand.UserCallback != ImDrawCallback_ResetRenderState)
                        {
                            ReleaseEditorUiRenderData(renderData);
                            return Fail(failure, "editor UI custom draw callbacks cannot cross the retained render-data boundary");
                        }
                        EditorUiRenderCommand command;
                        command.kind = EditorUiRenderCommandKind::ResetState;
                        renderData->commands.PushBack(command);
                        continue;
                    }
                    if (sourceCommand.ElemCount == 0)
                        continue;
                    if (sourceCommand.IdxOffset > indexCount || sourceCommand.ElemCount > indexCount - sourceCommand.IdxOffset ||
                        sourceCommand.VtxOffset >= vertexCount)
                    {
                        ReleaseEditorUiRenderData(renderData);
                        return Fail(failure, "editor UI draw command addresses data outside its draw list");
                    }
                    const f32 clipMinimumX = (sourceCommand.ClipRect.x - source.DisplayPos.x) * source.FramebufferScale.x;
                    const f32 clipMinimumY = (sourceCommand.ClipRect.y - source.DisplayPos.y) * source.FramebufferScale.y;
                    const f32 clipMaximumX = (sourceCommand.ClipRect.z - source.DisplayPos.x) * source.FramebufferScale.x;
                    const f32 clipMaximumY = (sourceCommand.ClipRect.w - source.DisplayPos.y) * source.FramebufferScale.y;
                    if (clipMaximumX <= clipMinimumX || clipMaximumY <= clipMinimumY)
                        continue;
                    u32 textureIndex = 0;
                    const ImTextureID texture = sourceCommand.TexRef._TexData != nullptr ? sourceCommand.TexRef._TexData->GetTexID() : sourceCommand.TexRef._TexID;
                    if (!ResolveRenderTexture(*renderData, texture, textureIndex, failure))
                    {
                        ReleaseEditorUiRenderData(renderData);
                        return false;
                    }
                    EditorUiRenderCommand command;
                    command.elementCount = sourceCommand.ElemCount;
                    command.indexOffset = indexBase + sourceCommand.IdxOffset;
                    command.vertexOffset = vertexBase + sourceCommand.VtxOffset;
                    command.texture = textureIndex;
                    command.clipMinimum[0] = clipMinimumX;
                    command.clipMinimum[1] = clipMinimumY;
                    command.clipMaximum[0] = clipMaximumX;
                    command.clipMaximum[1] = clipMaximumY;
                    renderData->commands.PushBack(command);
                }
            }

            const EditorUiFramePayload payload{renderData, RetainEditorUiRenderData, ReleaseEditorUiRenderData};
            frame = EditorUiFrameData(viewportRecord.host, ui->GetFrame(), payload);
            ReleaseEditorUiRenderData(renderData);
            return frame.IsValid() ? true : Fail(failure, "editor UI retained frame could not be created");
        }

        [[nodiscard]] bool CaptureFrames(EditorUiFrameData* const frames, const u32 capacity, u32& count,
                                         EditorUiFailure* const failure) noexcept
        {
            count = 0;
            if ((frames == nullptr) != (capacity == 0))
                return Fail(failure, "editor UI retained-frame destination is invalid");
            for (const ViewportRecord& record : viewports)
            {
                if (!record.active || record.viewport == nullptr || record.viewport->DrawData == nullptr || !record.viewport->DrawData->Valid)
                    continue;
                if (!record.host.IsValid() || count >= capacity)
                    return Fail(failure, "editor UI retained-frame destination capacity is exhausted");
                if (!CaptureViewport(record, *record.viewport->DrawData, frames[count], failure))
                    return false;
                ++count;
            }
            return true;
        }

        static window::WindowSnapshot ReadWindow(ImGuiViewport* const viewport) noexcept
        {
            window::WindowSnapshot state;
            Impl* const impl = Get();
            ViewportRecord* const record = GetRecord(viewport);
            if (impl == nullptr || record == nullptr || !record->window.IsValid() || !impl->Windows().GetSnapshot(record->window, state))
            {
                if (impl != nullptr)
                    impl->RecordFailure("editor native viewport state is unavailable");
            }
            return state;
        }

        [[nodiscard]] bool ProcessWindowEvents(EditorUiFailure* const failure) noexcept
        {
            window::WindowEvent events[64]{};
            for (;;)
            {
                const window::WindowEventReadResult result = Windows().ReadEvents(eventCursor, {events, 64});
                if (result.invalidCursor || result.lostEvents != 0)
                    return Fail(failure, "editor UI lost authoritative native window events");
                for (u32 index = 0; index < result.count; ++index)
                {
                    ViewportRecord* const record = FindViewport(events[index].window);
                    if (record == nullptr || record->viewport == nullptr)
                        continue;
                    switch (events[index].type)
                    {
                    case window::WindowEventType::CloseRequested: record->viewport->PlatformRequestClose = true; break;
                    // Requests also publish acknowledgement events. Only a differing native placement should override ImGui's next drag update.
                    case window::WindowEventType::Moved:
                        record->viewport->PlatformRequestMove = events[index].position.x != static_cast<i32>(record->viewport->Pos.x) || events[index].position.y != static_cast<i32>(record->viewport->Pos.y);
                        break;
                    case window::WindowEventType::Resized:
                        record->viewport->PlatformRequestResize = events[index].logicalExtent.width != static_cast<u32>(record->viewport->Size.x) || events[index].logicalExtent.height != static_cast<u32>(record->viewport->Size.y);
                        break;
                    case window::WindowEventType::MouseEntered: mouseWindow = record->window; break;
                    case window::WindowEventType::MouseLeft:
                        if (mouseWindow == record->window)
                            mouseWindow = {};
                        break;
                    default: break;
                    }
                }
                if (result.count < 64)
                    return true;
            }
        }

        void UpdateMonitors() noexcept
        {
            window::DisplaySnapshot displays[window::MaximumDisplays]{};
            const u32 count = Windows().VisitDisplays(displays, window::MaximumDisplays);
            ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
            platform.Monitors.resize(0);
            for (u32 index = 0; index < count; ++index)
            {
                ImGuiPlatformMonitor monitor;
                monitor.MainPos = {static_cast<f32>(displays[index].bounds.origin.x), static_cast<f32>(displays[index].bounds.origin.y)};
                monitor.MainSize = {static_cast<f32>(displays[index].bounds.extent.width), static_cast<f32>(displays[index].bounds.extent.height)};
                monitor.WorkPos = {static_cast<f32>(displays[index].workArea.origin.x), static_cast<f32>(displays[index].workArea.origin.y)};
                monitor.WorkSize = {static_cast<f32>(displays[index].workArea.extent.width), static_cast<f32>(displays[index].workArea.extent.height)};
                monitor.DpiScale = displays[index].contentScale;
                monitor.PlatformHandle = WindowToken({displays[index].handle.index, displays[index].handle.generation});
                platform.Monitors.push_back(monitor);
            }
        }

        void ForwardInput() noexcept
        {
            ImGuiIO& io = ImGui::GetIO();
            for (const input::RawEvent& event : inputService->GetEvents())
            {
                switch (event.type)
                {
                case input::EventType::KeyChanged:
                {
                    const ImGuiKey key = TranslateKey(event.data.key.key);
                    if (key != ImGuiKey_None)
                        io.AddKeyEvent(key, event.data.key.pressed);
                    break;
                }
                case input::EventType::MouseButtonChanged:
                {
                    const int button = TranslateMouseButton(event.data.mouseButton.button);
                    if (button >= 0)
                        io.AddMouseButtonEvent(button, event.data.mouseButton.pressed);
                    break;
                }
                case input::EventType::MouseMoved:
                {
                    // Do not rebase queued local coordinates using a window that may have moved since input translation.
                    if (event.data.mouseMotion.hasDesktopPosition && FindViewport(event.window) != nullptr)
                        io.AddMousePosEvent(event.data.mouseMotion.desktopX, event.data.mouseMotion.desktopY);
                    break;
                }
                case input::EventType::MouseWheel: io.AddMouseWheelEvent(event.data.wheel.x, event.data.wheel.y); break;
                case input::EventType::TextInput: io.AddInputCharactersUTF8(event.data.text.utf8); break;
                case input::EventType::FocusGained: io.AddFocusEvent(true); break;
                case input::EventType::FocusLost: io.AddFocusEvent(false); break;
                default: break;
                }
            }
            // Capture routes motion to its owner, not necessarily to the window beneath the pointer.
            ViewportRecord* const hovered = FindViewport(mouseWindow);
            io.AddMouseViewportEvent(hovered != nullptr && hovered->viewport != nullptr && (hovered->viewport->Flags & ImGuiViewportFlags_NoInputs) == 0 ? hovered->viewport->ID : 0);
            const input::KeyboardState& keyboard = inputService->GetSnapshot().keyboard;
            io.AddKeyEvent(ImGuiMod_Ctrl, keyboard.IsDown(input::Key::LeftControl) || keyboard.IsDown(input::Key::RightControl));
            io.AddKeyEvent(ImGuiMod_Shift, keyboard.IsDown(input::Key::LeftShift) || keyboard.IsDown(input::Key::RightShift));
            io.AddKeyEvent(ImGuiMod_Alt, keyboard.IsDown(input::Key::LeftAlt) || keyboard.IsDown(input::Key::RightAlt));
            io.AddKeyEvent(ImGuiMod_Super, keyboard.IsDown(input::Key::LeftGui) || keyboard.IsDown(input::Key::RightGui));
        }
    };

    EditorUiImGuiPlatform::~EditorUiImGuiPlatform()
    {
        Shutdown();
    }

    bool EditorUiImGuiPlatform::Initialize(EditorUiService& ui, EditorResourceService& resources,
                                           engine::WindowService& windows, engine::InputService& input,
                                           const bool frameDiagnosticsEnabled,
                                           EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl != nullptr)
            return Fail(failure, "editor UI platform is already initialized");
        memory::MemoryBlock block = memory::Allocate(memory::PoolId::Editor, sizeof(Impl), alignof(Impl));
        if (!block)
            return Fail(failure, "editor UI platform allocation failed");
        m_impl = ::new (block.address) Impl();
        m_impl->ui = &ui;
        m_impl->windowService = &windows;
        m_impl->inputService = &input;
        m_impl->frameDiagnosticsEnabled = frameDiagnosticsEnabled;

        ImGui::SetAllocatorFunctions(ImGuiAllocate, ImGuiFree);
        m_impl->context = ImGui::CreateContext();
        if (m_impl->context == nullptr)
        {
            Shutdown();
            return Fail(failure, "Dear ImGui context creation failed");
        }
        ImGui::SetCurrentContext(m_impl->context);
        rhi::SamplerStateDesc samplerDesc;
        samplerDesc.addressU = rhi::SamplerAddressMode::Clamp;
        samplerDesc.addressV = rhi::SamplerAddressMode::Clamp;
        samplerDesc.addressW = rhi::SamplerAddressMode::Clamp;
        rhi::Failure rhiFailure;
        m_impl->textureSampler = rhi::SamplerState(rhi::AdoptReference, rhi::RequestSamplerState(samplerDesc, &rhiFailure));
        if (!m_impl->textureSampler.IsValid())
        {
            const char* const message = rhiFailure.message != nullptr ? rhiFailure.message : "editor UI sampler creation failed";
            Shutdown();
            return Fail(failure, message);
        }
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
        io.ConfigDockingTransparentPayload = true;
        io.ConfigDpiScaleFonts = true;
        io.ConfigDpiScaleViewports = true;

        EditorResourceLocation regularFont;
        EditorResourceLocation semiboldFont;
        if (!resources.Resolve(EditorUiRegularFontResourceId, regularFont) ||
            !resources.Resolve(EditorUiSemiboldFontResourceId, semiboldFont))
        {
            Shutdown();
            return Fail(failure, "editor UI font resources could not be resolved");
        }

        ImFontConfig regularConfig;
        regularConfig.SizePixels = 13.0f;
        regularConfig.OversampleH = 2;
        regularConfig.OversampleV = 2;
        std::snprintf(regularConfig.Name, sizeof(regularConfig.Name), "%s", "Source Sans 3 Regular");
        ImFont* const regular = io.Fonts->AddFontFromFileTTF(regularFont.path.AsChar(), 13.0f, &regularConfig);

        ImFontConfig semiboldConfig;
        semiboldConfig.SizePixels = 13.0f;
        semiboldConfig.OversampleH = 2;
        semiboldConfig.OversampleV = 2;
        std::snprintf(semiboldConfig.Name, sizeof(semiboldConfig.Name), "%s", "Source Sans 3 Semibold");
        ImFont* const semibold = io.Fonts->AddFontFromFileTTF(semiboldFont.path.AsChar(), 13.0f, &semiboldConfig);
        if (regular == nullptr || semibold == nullptr)
        {
            Shutdown();
            return Fail(failure, "editor UI fonts could not be loaded from the editor resource image");
        }
        io.FontDefault = regular;
        editor::detail::SetEditorVisualFonts(regular, semibold);
        editor::detail::ApplyEditorVisualStyle(m_impl->userUiScale, 1.0f);
        if (!m_impl->InitializeToolbarIcons(resources))
        {
            Shutdown();
            return Fail(failure, "editor toolbar SVG icon resources could not be loaded");
        }
        m_impl->InitializeVanguardWordmark(resources);
        io.IniFilename = nullptr;
        io.BackendPlatformUserData = m_impl;
        io.BackendPlatformName = "vanguard_editor_platform";
        io.BackendRendererName = "vanguard_editor_graph";
        io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseHoveredViewport | ImGuiBackendFlags_HasMouseCursors |
                           ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports;

        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Platform_CreateWindow = Impl::CreatePlatformWindow;
        platform.Platform_DestroyWindow = Impl::DestroyWindow;
        platform.Platform_ShowWindow = Impl::ShowWindow;
        platform.Platform_SetWindowPos = Impl::SetWindowPosition;
        platform.Platform_GetWindowPos = Impl::GetWindowPosition;
        platform.Platform_SetWindowSize = Impl::SetWindowSize;
        platform.Platform_GetWindowSize = Impl::GetWindowSize;
        platform.Platform_GetWindowFramebufferScale = Impl::GetFramebufferScale;
        platform.Platform_SetWindowFocus = Impl::SetWindowFocus;
        platform.Platform_SetWindowAlpha = Impl::SetWindowAlpha;
        platform.Platform_GetWindowFocus = Impl::GetWindowFocus;
        platform.Platform_GetWindowMinimized = Impl::GetWindowMinimized;
        platform.Platform_SetWindowTitle = Impl::SetWindowTitle;
        platform.Platform_GetWindowDpiScale = Impl::GetWindowDpiScale;
        platform.Platform_OnChangedViewport = Impl::OnChangedViewport;
        platform.Platform_GetClipboardTextFn = Impl::GetClipboardText;
        platform.Platform_SetClipboardTextFn = Impl::SetClipboardText;
        platform.Platform_SetImeDataFn = Impl::SetImeData;
        platform.DrawCallback_ResetRenderState = Impl::ResetRenderState;

        Impl::ViewportRecord* const primary = m_impl->AllocateViewport();
        const window::WindowHandle primaryWindow = windows.GetPrimaryWindow();
        if (primary == nullptr || !primaryWindow.IsValid() || !windows.GetManager().CreateEventCursor(window::EventCursorOrigin::NextEvent, m_impl->eventCursor))
        {
            Shutdown();
            return Fail(failure, "editor UI primary native window is unavailable");
        }
        primary->viewport = ImGui::GetMainViewport();
        primary->window = primaryWindow;
        primary->owned = false;
        if (!ui.RegisterHost({primaryWindow, {}, {}, true}, primary->host, failure))
        {
            Shutdown();
            return false;
        }
        primary->viewport->PlatformUserData = primary;
        primary->viewport->PlatformHandle = WindowToken(primaryWindow);
        window::WindowSnapshot primaryState;
        if (m_impl->Windows().GetSnapshot(primaryWindow, primaryState) && primaryState.nativeState.mouseFocus)
            m_impl->mouseWindow = primaryWindow;
        m_impl->UpdateMonitors();
        return true;
    }

    void EditorUiImGuiPlatform::Shutdown() noexcept
    {
        if (m_impl == nullptr)
            return;
        m_impl->ResetInputCapture();
        if (m_impl->context != nullptr)
        {
            ImGui::SetCurrentContext(m_impl->context);
            if (m_impl->frameActive)
                ImGui::EndFrame();
            const window::WindowHandle primaryWindow = m_impl->windowService != nullptr ? m_impl->windowService->GetPrimaryWindow() : window::WindowHandle{};
            if (primaryWindow.IsValid())
            {
                window::Failure failure;
                static_cast<void>(m_impl->Windows().SetTextInput(primaryWindow, {}, &failure));
                static_cast<void>(m_impl->Windows().SetHitTest(primaryWindow, {}, &failure));
            }
            ImGui::DestroyPlatformWindows();
            m_impl->ReleaseToolbarIcons();
            m_impl->ReleaseVanguardWordmark();
            m_impl->ReleaseManagedTextures();
            for (Impl::ViewportRecord& record : m_impl->viewports)
            {
                if (!record.active)
                    continue;
                if (record.host.IsValid() && m_impl->ui != nullptr)
                    static_cast<void>(m_impl->ui->UnregisterHost(record.host));
                if (record.viewport != nullptr)
                {
                    record.viewport->PlatformUserData = nullptr;
                    record.viewport->PlatformHandle = nullptr;
                    record.viewport->PlatformHandleRaw = nullptr;
                }
                record = {};
            }
            ImGui::GetPlatformIO().ClearPlatformHandlers();
            ImGui::GetPlatformIO().ClearRendererHandlers();
            ImGui::GetIO().BackendPlatformUserData = nullptr;
            ImGui::GetIO().BackendPlatformName = nullptr;
            editor::detail::ResetEditorVisualFonts();
            ImGui::DestroyContext(m_impl->context);
        }
        m_impl->ResetInputCapture();
        m_impl->~Impl();
        memory::MemoryBlock block{m_impl, sizeof(Impl), memory::PoolId::Editor};
        memory::Free(block);
        m_impl = nullptr;
    }

    bool EditorUiImGuiPlatform::BeginFrame(const engine::FrameContext& frame, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (m_impl == nullptr || m_impl->context == nullptr || m_impl->frameActive)
            return Fail(failure, "editor UI platform is not ready to begin a frame");
        ImGui::SetCurrentContext(m_impl->context);
        m_impl->deferredFailure = nullptr;
        const vanguard::u64 frequency = vanguard::system::GetMonotonicFrequency();
        const vanguard::u64 beginTick = vanguard::system::GetMonotonicTicks();
        if (!m_impl->ProcessWindowEvents(failure))
            return false;
        const vanguard::u64 afterEventsTick = vanguard::system::GetMonotonicTicks();
        m_impl->UpdateMonitors();
        const vanguard::u64 afterMonitorsTick = vanguard::system::GetMonotonicTicks();
        ImGuiIO& io = ImGui::GetIO();
        m_impl->ResetInputCapture();
        m_impl->inputCaptureFrame = frame.frame;
        m_impl->preWantCaptureKeyboard = io.WantCaptureKeyboard;
        m_impl->preWantTextInput = io.WantTextInput;
        u32 mouseMotionEvents = 0;
        u32 mouseButtonEvents = 0;
        u32 focusEvents = 0;
        if (m_impl->frameDiagnosticsEnabled)
        {
            for (const input::RawEvent& event : m_impl->inputService->GetEvents())
            {
                if (event.type == input::EventType::MouseMoved)
                    ++mouseMotionEvents;
                else if (event.type == input::EventType::MouseButtonChanged)
                    ++mouseButtonEvents;
                else if (event.type == input::EventType::FocusGained || event.type == input::EventType::FocusLost)
                    ++focusEvents;
            }
        }
        m_impl->ForwardInput();
        window::WindowSnapshot primary;
        if (!m_impl->Windows().GetSnapshot(m_impl->windowService->GetPrimaryWindow(), primary))
        {
            m_impl->ResetInputCapture();
            return Fail(failure, "editor UI primary native window state is unavailable");
        }
        m_impl->appliedUserUiScale = m_impl->userUiScale;
        editor::detail::ApplyEditorVisualStyle(m_impl->appliedUserUiScale, primary.nativeState.contentScale);
        io.DisplaySize = {static_cast<f32>(primary.nativeState.placement.logicalExtent.width), static_cast<f32>(primary.nativeState.placement.logicalExtent.height)};
        io.DisplayFramebufferScale = Impl::GetFramebufferScale(ImGui::GetMainViewport());
        io.DeltaTime = frame.realDeltaSeconds > 0.0f ? frame.realDeltaSeconds : 1.0f / 60.0f;
        if (m_impl->frameDiagnosticsEnabled && frequency != 0 && !primary.nativeState.focused &&
            (m_impl->diagnosticsHeartbeatTick == 0 || beginTick - m_impl->diagnosticsHeartbeatTick >= frequency / 2u))
        {
            const input::FrameSnapshot& inputSnapshot = m_impl->inputService->GetSnapshot();
            const input::InputStats inputStats = m_impl->inputService->GetStats();
            VG_LOG_INFO(vanguard::diagnostics::Category::Engine,
                        "editor input state: frame=%llu mouseFocus=%u mouseWindow=%u inputFocused=%u inputEvents=%u mouseMotionEvents=%u mouseButtonEvents=%u focusEvents=%u mouseX=%.1f mouseY=%.1f deltaX=%.1f deltaY=%.1f buttons=%u imguiMouseX=%.1f imguiMouseY=%.1f hoveredViewport=%u",
                        static_cast<unsigned long long>(frame.frame), u32(primary.nativeState.mouseFocus), u32(m_impl->mouseWindow.IsValid()),
                        u32(inputSnapshot.focused), inputStats.lastFrameEvents, mouseMotionEvents, mouseButtonEvents, focusEvents,
                        inputSnapshot.mouse.x, inputSnapshot.mouse.y, inputSnapshot.mouse.deltaX, inputSnapshot.mouse.deltaY, u32(inputSnapshot.mouse.down),
                        io.MousePos.x, io.MousePos.y, io.MouseHoveredViewport);
            m_impl->diagnosticsHeartbeatTick = beginTick;
        }
        // Enter/leave hover reporting is not reliable under capture, including the first drag frame.
        if (m_impl->inputService->GetSnapshot().mouse.down == 0 && ImGui::GetDragDropPayload() == nullptr)
            io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
        else
            io.BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        ImGui::NewFrame();
        const vanguard::u64 finishedTick = vanguard::system::GetMonotonicTicks();
        if (frequency != 0)
        {
            const auto milliseconds = [frequency](const vanguard::u64 from, const vanguard::u64 to) noexcept
            {
                return to >= from ? 1000.0 * static_cast<double>(to - from) / static_cast<double>(frequency) : 0.0;
            };
            const double eventsMs = milliseconds(beginTick, afterEventsTick);
            const double monitorsMs = milliseconds(afterEventsTick, afterMonitorsTick);
            const double newFrameMs = milliseconds(afterMonitorsTick, finishedTick);
            const double totalMs = milliseconds(beginTick, finishedTick);
            if (totalMs >= 40.0)
                VG_LOG_WARNING(vanguard::diagnostics::Category::Engine,
                               "editor ImGui begin stall: frame=%llu totalMs=%.3f windowEventsMs=%.3f monitorsMs=%.3f newFrameMs=%.3f",
                               static_cast<unsigned long long>(frame.frame), totalMs, eventsMs, monitorsMs, newFrameMs);
        }
        m_impl->frameActive = true;
        m_impl->frameEverBegun = true;
        return true;
    }

    bool EditorUiImGuiPlatform::EndFrame(EditorUiFrameData* const frames, const u32 capacity, u32& count, EditorUiTextureFrameData& textureFrame,
                                         EditorUiInputCapture& inputCapture, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        count = 0;
        textureFrame.Reset();
        inputCapture = {};
        if (m_impl == nullptr || !m_impl->frameActive)
            return Fail(failure, "editor UI platform has no active frame");
        ImGui::SetCurrentContext(m_impl->context);
        const vanguard::u64 frequency = vanguard::system::GetMonotonicFrequency();
        const vanguard::u64 beginTick = vanguard::system::GetMonotonicTicks();
        m_impl->UpdateCursor();
        ImGui::Render();
        const vanguard::u64 afterRenderTick = vanguard::system::GetMonotonicTicks();
        m_impl->frameActive = false;
        const ImGuiIO& io = ImGui::GetIO();
        const bool postAnyItemActive = ImGui::IsAnyItemActive();
        const bool postAnyPopup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        const bool postNavigationCapture = io.NavActive && io.ConfigNavCaptureKeyboard;
        {
            ImGui::UpdatePlatformWindows();
        }
        const vanguard::u64 afterPlatformWindowsTick = vanguard::system::GetMonotonicTicks();
        if (m_impl->deferredFailure != nullptr)
        {
            m_impl->ResetInputCapture();
            return Fail(failure, m_impl->deferredFailure);
        }
        const bool textInputCaptured = m_impl->preWantTextInput || m_impl->postImeWantTextInput;
        const bool keyboardCaptured = m_impl->preWantCaptureKeyboard || textInputCaptured || postAnyItemActive || postAnyPopup || postNavigationCapture;
        if (!m_impl->CaptureTextureUpdates(textureFrame, failure))
        {
            m_impl->ResetInputCapture();
            return false;
        }
        const vanguard::u64 afterTexturesTick = vanguard::system::GetMonotonicTicks();
        if (!m_impl->CaptureFrames(frames, capacity, count, failure))
        {
            m_impl->RequeueTextureUpdates(textureFrame);
            m_impl->ResetInputCapture();
            return false;
        }
        const vanguard::u64 finishedTick = vanguard::system::GetMonotonicTicks();
        if (frequency != 0)
        {
            const auto milliseconds = [frequency](const vanguard::u64 from, const vanguard::u64 to) noexcept
            {
                return to >= from ? 1000.0 * static_cast<double>(to - from) / static_cast<double>(frequency) : 0.0;
            };
            const double renderMs = milliseconds(beginTick, afterRenderTick);
            const double platformWindowsMs = milliseconds(afterRenderTick, afterPlatformWindowsTick);
            const double texturesMs = milliseconds(afterPlatformWindowsTick, afterTexturesTick);
            const double framesMs = milliseconds(afterTexturesTick, finishedTick);
            const double totalMs = milliseconds(beginTick, finishedTick);
            if (totalMs >= 40.0)
                VG_LOG_WARNING(vanguard::diagnostics::Category::Engine,
                               "editor ImGui end stall: totalMs=%.3f renderMs=%.3f platformWindowsMs=%.3f texturesMs=%.3f framesMs=%.3f",
                               totalMs, renderMs, platformWindowsMs, texturesMs, framesMs);
        }
        inputCapture = {m_impl->inputCaptureFrame, textInputCaptured, keyboardCaptured};
        m_impl->ResetInputCapture();
        return true;
    }

    void EditorUiImGuiPlatform::AbortFrame() noexcept
    {
        if (m_impl == nullptr)
            return;
        if (m_impl->frameActive)
        {
            ImGui::SetCurrentContext(m_impl->context);
            ImGui::EndFrame();
            m_impl->frameActive = false;
        }
        m_impl->ResetInputCapture();
    }

    bool EditorUiImGuiPlatform::GetCurrentViewportHost(EditorHostWindowHandle& host) const noexcept
    {
        host = {};
        if (m_impl == nullptr || m_impl->context == nullptr || ImGui::GetCurrentContext() != m_impl->context)
            return false;
        ImGuiViewport* const viewport = ImGui::GetWindowViewport();
        Impl::ViewportRecord* const record = Impl::GetRecord(viewport);
        if (record == nullptr || !record->active || !record->host.IsValid())
            return false;
        host = record->host;
        return true;
    }

    bool EditorUiImGuiPlatform::GetVanguardWordmarkTexture(ImTextureData*& texture, float& aspectRatio) const noexcept
    {
        texture = nullptr;
        aspectRatio = 0.0f;
        if (m_impl == nullptr || m_impl->context == nullptr || !m_impl->vanguardWordmarkRegistered ||
            !(m_impl->vanguardWordmarkAspectRatio > 0.0f))
            return false;
        texture = &m_impl->vanguardWordmarkTexture;
        aspectRatio = m_impl->vanguardWordmarkAspectRatio;
        return true;
    }

    EditorSvgIcon* EditorUiImGuiPlatform::FindSvgIcon(const EditorResourceId resource) const noexcept
    {
        return m_impl != nullptr ? m_impl->FindSvgIcon(resource) : nullptr;
    }

    f32 EditorUiImGuiPlatform::GetUserScale() const noexcept
    {
        return m_impl != nullptr ? m_impl->userUiScale : 1.0f;
    }

    bool EditorUiImGuiPlatform::SetUserScale(const f32 scale, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!vanguard::concurrency::IsMainThread())
            return FailCode(failure, EditorUiFailureCode::WrongThread,
                            "editor UI scale changes require the main thread");
        if (m_impl == nullptr || m_impl->context == nullptr)
            return FailCode(failure, EditorUiFailureCode::NotReady,
                            "editor UI scale changes require an initialized platform");
        if (!(scale >= 0.50f && scale <= 3.00f) || !std::isfinite(scale))
            return FailCode(failure, EditorUiFailureCode::InvalidDescriptor,
                            "editor UI scale must be finite and between 0.50 and 3.00");
        if (std::fabs(m_impl->userUiScale - scale) <= 0.0001f)
            return true;

        m_impl->userUiScale = scale;
        // Preference edits may occur while the UI is authoring a frame. Do not mutate global style
        // metrics underneath widgets already laid out in that frame; BeginFrame reapplies the new
        // scale before the next ImGui::NewFrame(). Startup restore reaches this path before framing and
        // can apply immediately.
        if (m_impl->frameActive)
            return true;

        ImGui::SetCurrentContext(m_impl->context);
        m_impl->appliedUserUiScale = m_impl->userUiScale;
        ImGuiContext* const context = ImGui::GetCurrentContext();
        const ImGuiViewport* const viewport = context != nullptr && context->CurrentViewport != nullptr
                                                  ? context->CurrentViewport
                                                  : ImGui::GetMainViewport();
        const f32 dpiScale = viewport != nullptr && viewport->DpiScale > 0.0f ? viewport->DpiScale : 1.0f;
        editor::detail::ApplyEditorVisualStyle(m_impl->appliedUserUiScale, dpiScale);
        return true;
    }

    EditorUiUserStateLayoutLoadResult EditorUiImGuiPlatform::LoadUserStateLayout(
        const containers::ArraySpan<const u8> bytes,
        const containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
        u32& repairedPlacementCount, EditorUiFailure* const failure) noexcept
    {
        return ApplyUserStateLayoutInternal(bytes, savedDisplays, true, repairedPlacementCount, failure);
    }

    EditorUiUserStateLayoutLoadResult EditorUiImGuiPlatform::ApplyUserStateLayoutLive(
        const containers::ArraySpan<const u8> bytes,
        const containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
        u32& repairedPlacementCount, EditorUiFailure* const failure) noexcept
    {
        return ApplyUserStateLayoutInternal(bytes, savedDisplays, false, repairedPlacementCount, failure);
    }

    bool EditorUiImGuiPlatform::ResetUserStateLayoutLive(EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
            return FailCode(failure, EditorUiFailureCode::WrongThread,
                            "editor UI live layout reset requires the main thread");
        if (m_impl == nullptr || m_impl->context == nullptr)
            return FailCode(failure, EditorUiFailureCode::NotReady,
                            "editor UI live layout reset requires an initialized platform");
        if (m_impl->frameActive)
            return FailCode(failure, EditorUiFailureCode::WrongPhase,
                            "editor UI live layout reset requires no active UI frame");
        ImGui::SetCurrentContext(m_impl->context);
        ImGui::ClearIniSettings();
        // ClearIniSettings() removes the live docking tree and persisted window records, but
        // existing ImGuiWindow instances keep the condition masks consumed by their first Begin().
        // Vanguard's default placement path intentionally uses ImGuiCond_FirstUseEver, so without
        // re-arming that condition a live reset leaves already-created panels undocked instead of
        // placing them into the freshly rebuilt default dock tree. Hidden windows keep this bit
        // until they are authored again, which also gives them their normal initial placement when
        // reopened after the reset.
        ImGuiContext& context = *ImGui::GetCurrentContext();
        for (ImGuiWindow* const window : context.Windows)
        {
            if (window == nullptr || (window->Flags & ImGuiWindowFlags_NoSavedSettings) != 0)
                continue;
            window->SetWindowPosAllowFlags = static_cast<ImGuiCond>(
                window->SetWindowPosAllowFlags | ImGuiCond_FirstUseEver);
            window->SetWindowSizeAllowFlags = static_cast<ImGuiCond>(
                window->SetWindowSizeAllowFlags | ImGuiCond_FirstUseEver);
            window->SetWindowCollapsedAllowFlags = static_cast<ImGuiCond>(
                window->SetWindowCollapsedAllowFlags | ImGuiCond_FirstUseEver);
            window->SetWindowDockAllowFlags = static_cast<ImGuiCond>(
                window->SetWindowDockAllowFlags | ImGuiCond_FirstUseEver);
        }
        ClearManualIniDirtyState();
        return true;
    }

    EditorUiUserStateLayoutLoadResult EditorUiImGuiPlatform::ApplyUserStateLayoutInternal(
        const containers::ArraySpan<const u8> bytes,
        const containers::ArraySpan<const EditorUiSavedDisplay> savedDisplays,
        const bool startup, u32& repairedPlacementCount, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        repairedPlacementCount = 0;
        if (!concurrency::IsMainThread())
        {
            FailCode(failure, EditorUiFailureCode::WrongThread, "editor UI user-state load requires the main thread");
            return EditorUiUserStateLayoutLoadResult::InvalidArgument;
        }
        if (m_impl == nullptr || m_impl->context == nullptr)
        {
            FailCode(failure, EditorUiFailureCode::NotReady, "editor UI user-state load requires an initialized platform");
            return EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
        }
        if (m_impl->frameActive || (startup && m_impl->frameEverBegun))
        {
            FailCode(failure, EditorUiFailureCode::WrongPhase,
                     startup ? "editor UI user-state load must precede the first UI frame"
                             : "editor UI live layout apply requires no active UI frame");
            return EditorUiUserStateLayoutLoadResult::InvalidArgument;
        }
        if (startup && m_impl->layoutLoadAttempted)
        {
            FailCode(failure, EditorUiFailureCode::WrongPhase, "editor UI user-state layout load is one-shot per platform lifetime");
            return EditorUiUserStateLayoutLoadResult::InvalidArgument;
        }
        if (startup)
            m_impl->layoutLoadAttempted = true;
        if (bytes.Size() > MaximumEditorUiPersistenceBytes || savedDisplays.Empty() ||
            savedDisplays.Size() > window::MaximumDisplays || (bytes.Size() != 0 && bytes.Data() == nullptr))
        {
            FailCode(failure, EditorUiFailureCode::InvalidDescriptor, "editor UI user-state layout input is invalid");
            return EditorUiUserStateLayoutLoadResult::InvalidArgument;
        }

        u32 savedPrimaryCount = 0;
        for (const EditorUiSavedDisplay& display : savedDisplays)
        {
            if (display.boundsWidth == 0 || display.boundsHeight == 0 || display.workWidth == 0 || display.workHeight == 0 ||
                display.desktopPixelWidth == 0 || display.desktopPixelHeight == 0 || display.scaleQ16_16 == 0 ||
                display.scaleQ16_16 > 0x00100000u)
            {
                FailCode(failure, EditorUiFailureCode::InvalidDescriptor, "editor UI saved display topology is invalid");
                return EditorUiUserStateLayoutLoadResult::InvalidArgument;
            }
            savedPrimaryCount += display.primary ? 1u : 0u;
        }
        if (savedPrimaryCount != 1)
        {
            FailCode(failure, EditorUiFailureCode::InvalidDescriptor, "editor UI saved display topology requires one primary display");
            return EditorUiUserStateLayoutLoadResult::InvalidArgument;
        }

        window::DisplaySnapshot currentDisplays[window::MaximumDisplays]{};
        const u32 currentCount = m_impl->Windows().VisitDisplays(currentDisplays, window::MaximumDisplays);
        u32 currentPrimaryCount = 0;
        bool currentValid = currentCount != 0 && currentCount <= window::MaximumDisplays;
        for (u32 index = 0; currentValid && index < currentCount; ++index)
        {
            EditorUiSavedDisplay converted;
            currentValid = editor::detail::CaptureEditorUiSavedDisplay(currentDisplays[index], converted);
            currentPrimaryCount += currentDisplays[index].primary ? 1u : 0u;
            for (u32 previous = 0; currentValid && previous < index; ++previous)
                if (currentDisplays[previous].handle == currentDisplays[index].handle)
                    currentValid = false;
        }
        if (!currentValid || currentPrimaryCount != 1)
        {
            FailCode(failure, EditorUiFailureCode::PlatformFailure, "editor UI current display topology is unavailable for restore");
            return EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
        }

        ImGui::SetCurrentContext(m_impl->context);
        const ImGuiID defaultViewportId = ImGui::GetMainViewport()->ID;
        if (bytes.Empty())
        {
            if (startup)
                ImGui::ClearIniSettings();
            ClearManualIniDirtyState();
            return EditorUiUserStateLayoutLoadResult::DiscardedUnusable;
        }

        containers::String liveBackup;
        if (!startup)
        {
            size_t backupBytes = 0;
            const char* const backup = ImGui::SaveIniSettingsToMemory(&backupBytes);
            if (backupBytes > MaximumEditorUiPersistenceBytes ||
                (backupBytes != 0 && backup == nullptr))
            {
                FailCode(failure, EditorUiFailureCode::CapacityExceeded,
                         "editor UI live layout rollback snapshot exceeds the persistence bound");
                return EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
            }
            if (backupBytes != 0)
                liveBackup.Append(backup, static_cast<u32>(backupBytes));
        }
        const auto rollbackLiveLayout = [&]() noexcept {
            if (startup)
                ImGui::ClearIniSettings();
            else
            {
                ImGui::ClearIniSettings();
                if (!liveBackup.Empty())
                    ImGui::LoadIniSettingsFromMemory(liveBackup.AsChar(), liveBackup.Length());
            }
            ClearManualIniDirtyState();
        };
        ImGui::LoadIniSettingsFromMemory(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        ImGuiContext& context = *ImGui::GetCurrentContext();
        const containers::ArraySpan<const window::DisplaySnapshot> currentSpan{currentDisplays, currentCount};

        for (ImGuiWindowSettings* settings = context.SettingsWindows.begin(); settings != nullptr;
             settings = context.SettingsWindows.next_chunk(settings))
        {
            if (settings->WantDelete || settings->IsChild || settings->ViewportId == 0 ||
                settings->ViewportId == defaultViewportId)
                continue;
            const EditorUiSavedRect savedRect = DecodeEditorUiPrivateWindowPlacement({
                settings->ViewportPos.x, settings->ViewportPos.y, settings->Pos.x, settings->Pos.y,
                settings->Size.x, settings->Size.y});
            EditorUiRepairedPlacement repaired;
            const EditorUiPlacementResolveResult result = ResolveEditorUiSavedPlacement(savedDisplays, currentSpan, savedRect, repaired);
            if (result != EditorUiPlacementResolveResult::Resolved)
            {
                rollbackLiveLayout();
                FailCode(failure, result == EditorUiPlacementResolveResult::InvalidArgument
                                      ? EditorUiFailureCode::InvalidDescriptor
                                      : EditorUiFailureCode::PlatformFailure,
                         "editor UI detached placement cannot be restored safely");
                return result == EditorUiPlacementResolveResult::InvalidArgument
                    ? EditorUiUserStateLayoutLoadResult::InvalidArgument
                    : EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
            }
        }

        for (ImGuiWindowSettings* settings = context.SettingsWindows.begin(); settings != nullptr;
             settings = context.SettingsWindows.next_chunk(settings))
        {
            if (settings->WantDelete || settings->IsChild || settings->ViewportId == 0 ||
                settings->ViewportId == defaultViewportId)
                continue;
            const EditorUiSavedRect savedRect = DecodeEditorUiPrivateWindowPlacement({
                settings->ViewportPos.x, settings->ViewportPos.y, settings->Pos.x, settings->Pos.y,
                settings->Size.x, settings->Size.y});
            EditorUiRepairedPlacement repaired;
            if (ResolveEditorUiSavedPlacement(savedDisplays, currentSpan, savedRect, repaired) !=
                EditorUiPlacementResolveResult::Resolved)
            {
                rollbackLiveLayout();
                FailCode(failure, EditorUiFailureCode::PlatformFailure, "editor UI detached placement changed during restore");
                return EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
            }
            EditorUiPrivateWindowPlacementFields writeback;
            if (!EncodeEditorUiPrivateWindowPlacement(repaired.rect, writeback))
            {
                rollbackLiveLayout();
                FailCode(failure, EditorUiFailureCode::PlatformFailure,
                         "editor UI detached placement cannot be represented by private toolkit settings");
                return EditorUiUserStateLayoutLoadResult::EnvironmentUnavailable;
            }
            settings->ViewportPos = ImVec2ih(writeback.viewportX, writeback.viewportY);
            settings->Pos = ImVec2ih(writeback.relativeX, writeback.relativeY);
            settings->Size = ImVec2ih(writeback.width, writeback.height);
            repairedPlacementCount += repaired.changed ? 1u : 0u;
        }

        ImGuiDockNode* const root = ImGui::DockBuilderGetNode(EditorPrimaryDockspaceId);
        // LoadIniSettingsFromMemory() has rebuilt the persisted docking nodes at this point, but
        // ImGui does not populate the root's derived CentralNode pointer until DockSpace() runs in
        // the first frame. Validate the persisted central-node flag directly from the loaded tree
        // instead of rejecting every valid pre-first-frame dock layout as unusable.
        ImGuiDockNode* const central = FindPersistedCentralDockNode(root);
        if (!IsPersistedEditorDockTreeUsable(
                root != nullptr, root != nullptr && root->IsDockSpace(),
                root != nullptr && root->IsRootNode(), central != nullptr,
                root != nullptr && central != nullptr && ImGui::DockNodeGetRootNode(central) == root))
        {
            rollbackLiveLayout();
            repairedPlacementCount = 0;
            return EditorUiUserStateLayoutLoadResult::DiscardedUnusable;
        }
        ClearManualIniDirtyState();
        return EditorUiUserStateLayoutLoadResult::AppliedUsable;
    }

    bool EditorUiImGuiPlatform::CaptureUserStateLayout(void* const destination, const u32 capacity, u32& byteCount,
                                                       EditorUiSavedDisplay* const savedDisplays, const u32 displayCapacity,
                                                       u32& displayCount, EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        byteCount = 0;
        displayCount = 0;
        if (!concurrency::IsMainThread())
            return FailCode(failure, EditorUiFailureCode::WrongThread, "editor UI user-state capture requires the main thread");
        if (m_impl == nullptr || m_impl->context == nullptr)
            return FailCode(failure, EditorUiFailureCode::NotReady, "editor UI user-state capture requires an initialized platform");
        if (m_impl->frameActive)
            return FailCode(failure, EditorUiFailureCode::WrongPhase, "editor UI user-state capture requires no active authoring frame");

        window::DisplaySnapshot currentDisplays[window::MaximumDisplays]{};
        displayCount = m_impl->Windows().VisitDisplays(currentDisplays, window::MaximumDisplays);
        EditorUiSavedDisplay captured[window::MaximumDisplays]{};
        u32 primaryCount = 0;
        bool topologyValid = displayCount != 0 && displayCount <= window::MaximumDisplays;
        for (u32 index = 0; topologyValid && index < displayCount; ++index)
        {
            topologyValid = editor::detail::CaptureEditorUiSavedDisplay(currentDisplays[index], captured[index]);
            primaryCount += currentDisplays[index].primary ? 1u : 0u;
            for (u32 previous = 0; topologyValid && previous < index; ++previous)
                if (currentDisplays[previous].handle == currentDisplays[index].handle)
                    topologyValid = false;
        }
        if (!topologyValid || primaryCount != 1)
            return FailCode(failure, EditorUiFailureCode::PlatformFailure, "editor UI current display topology cannot be persisted");

        for (u32 index = 1; index < displayCount; ++index)
        {
            const EditorUiSavedDisplay value = captured[index];
            u32 insertion = index;
            while (insertion != 0 && SavedDisplayLess(value, captured[insertion - 1u]))
            {
                captured[insertion] = captured[insertion - 1u];
                --insertion;
            }
            captured[insertion] = value;
        }

        ImGui::SetCurrentContext(m_impl->context);
        ImGuiContext& context = *ImGui::GetCurrentContext();
        const float priorTimer = context.SettingsDirtyTimer;
        const bool priorWantSave = ImGui::GetIO().WantSaveIniSettings;
        size_t requiredBytes = 0;
        const char* const data = ImGui::SaveIniSettingsToMemory(&requiredBytes);
        context.SettingsDirtyTimer = priorTimer;
        ImGui::GetIO().WantSaveIniSettings = priorWantSave;
        byteCount = requiredBytes <= ~u32{0} ? static_cast<u32>(requiredBytes) : ~u32{0};
        if (requiredBytes > MaximumEditorUiPersistenceBytes || requiredBytes > capacity || displayCount > displayCapacity ||
            (requiredBytes != 0 && (destination == nullptr || data == nullptr)) ||
            (displayCount != 0 && savedDisplays == nullptr))
        {
            return FailCode(failure, EditorUiFailureCode::CapacityExceeded, "editor UI user-state capture destination is insufficient");
        }
        if (requiredBytes != 0)
            std::memcpy(destination, data, requiredBytes);
        for (u32 index = 0; index < displayCount; ++index)
            savedDisplays[index] = captured[index];
        return true;
    }

    bool EditorUiImGuiPlatform::ConsumeUserStateLayoutDirty(EditorUiFailure* const failure) noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
        {
            FailCode(failure, EditorUiFailureCode::WrongThread, "editor UI layout dirty observation requires the main thread");
            return false;
        }
        if (m_impl == nullptr || m_impl->context == nullptr || m_impl->frameActive)
        {
            FailCode(failure, m_impl == nullptr ? EditorUiFailureCode::NotReady : EditorUiFailureCode::WrongPhase,
                     "editor UI layout dirty observation requires an idle live UI");
            return false;
        }
        ImGui::SetCurrentContext(m_impl->context);
        const bool dirty = ImGui::GetIO().WantSaveIniSettings;
        ImGui::GetIO().WantSaveIniSettings = false;
        return dirty;
    }

    bool EditorUiImGuiPlatform::IsUserStateLayoutSettling(EditorUiFailure* const failure) const noexcept
    {
        ClearFailure(failure);
        if (!concurrency::IsMainThread())
        {
            FailCode(failure, EditorUiFailureCode::WrongThread, "editor UI layout settling observation requires the main thread");
            return false;
        }
        if (m_impl == nullptr || m_impl->context == nullptr || m_impl->frameActive)
        {
            FailCode(failure, m_impl == nullptr ? EditorUiFailureCode::NotReady : EditorUiFailureCode::WrongPhase,
                     "editor UI layout settling observation requires an idle live UI");
            return false;
        }
        ImGui::SetCurrentContext(m_impl->context);
        return ImGui::GetCurrentContext()->SettingsDirtyTimer > 0.0f;
    }

    bool EditorUiImGuiPlatform::IsInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->context != nullptr;
    }
} // namespace vanguard::editor::detail
