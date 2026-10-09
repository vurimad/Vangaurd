#pragma once

#include <vanguard/editor/editor_resources.hpp>

#include <vanguard/containers/containers.hpp>

namespace vanguard::editor::detail
{
    struct EditorToolbarIconDescriptor final
    {
        EditorResourceDescriptor resource;
        const char* label = nullptr;
        u8 group = 0;
    };

    inline constexpr EditorToolbarIconDescriptor EditorToolbarIcons[]{
        {{0x656469636f6e0001ull, EditorResourceKind::SvgIcon, "Images/Icons/save.svg", true}, "Save", 0},
        {{0x656469636f6e0002ull, EditorResourceKind::SvgIcon, "Images/Icons/undo.svg", true}, "Undo", 0},
        {{0x656469636f6e0003ull, EditorResourceKind::SvgIcon, "Images/Icons/redo.svg", true}, "Redo", 0},

        {{0x656469636f6e0004ull, EditorResourceKind::SvgIcon, "Images/Icons/select.svg", true}, "Select", 1},
        {{0x656469636f6e0005ull, EditorResourceKind::SvgIcon, "Images/Icons/select-box.svg", true}, "Box Select", 1},

        {{0x656469636f6e0006ull, EditorResourceKind::SvgIcon, "Images/Icons/move.svg", true}, "Move", 2},
        {{0x656469636f6e0007ull, EditorResourceKind::SvgIcon, "Images/Icons/rotate.svg", true}, "Rotate", 2},
        {{0x656469636f6e0008ull, EditorResourceKind::SvgIcon, "Images/Icons/scale.svg", true}, "Scale", 2},
        {{0x656469636f6e0009ull, EditorResourceKind::SvgIcon, "Images/Icons/world-coords.svg", true}, "World Coordinates", 2},

        {{0x656469636f6e000aull, EditorResourceKind::SvgIcon, "Images/Icons/snap-grid.svg", true}, "Grid Snap", 3},
        {{0x656469636f6e000bull, EditorResourceKind::SvgIcon, "Images/Icons/snap-angle.svg", true}, "Angle Snap", 3},
        {{0x656469636f6e000cull, EditorResourceKind::SvgIcon, "Images/Icons/snap-scale.svg", true}, "Scale Snap", 3},
        {{0x656469636f6e000dull, EditorResourceKind::SvgIcon, "Images/Icons/snap-terrain.svg", true}, "Terrain Snap", 3},
        {{0x656469636f6e000eull, EditorResourceKind::SvgIcon, "Images/Icons/align-to-surface.svg", true}, "Align to Surface", 3},

        {{0x656469636f6e000full, EditorResourceKind::SvgIcon, "Images/Icons/focus-selection.svg", true}, "Focus Selection", 4},
        {{0x656469636f6e0010ull, EditorResourceKind::SvgIcon, "Images/Icons/zoom.svg", true}, "Zoom", 4},
        {{0x656469636f6e0011ull, EditorResourceKind::SvgIcon, "Images/Icons/eye.svg", true}, "Visibility", 4},
        {{0x656469636f6e0012ull, EditorResourceKind::SvgIcon, "Images/Icons/camera.svg", true}, "Camera", 4},

        {{0x656469636f6e0013ull, EditorResourceKind::SvgIcon, "Images/Icons/group.svg", true}, "Group", 5},
        {{0x656469636f6e0014ull, EditorResourceKind::SvgIcon, "Images/Icons/ungroup.svg", true}, "Ungroup", 5},
        {{0x656469636f6e0015ull, EditorResourceKind::SvgIcon, "Images/Icons/link.svg", true}, "Link", 5},
        {{0x656469636f6e0016ull, EditorResourceKind::SvgIcon, "Images/Icons/unlink.svg", true}, "Unlink", 5},
        {{0x656469636f6e0017ull, EditorResourceKind::SvgIcon, "Images/Icons/lock.svg", true}, "Lock", 5},
        {{0x656469636f6e0018ull, EditorResourceKind::SvgIcon, "Images/Icons/unlock.svg", true}, "Unlock", 5},

        {{0x656469636f6e0019ull, EditorResourceKind::SvgIcon, "Images/Icons/terrain.svg", true}, "Terrain", 6},
        {{0x656469636f6e001aull, EditorResourceKind::SvgIcon, "Images/Icons/character.svg", true}, "Character", 6},

        {{0x656469636f6e001bull, EditorResourceKind::SvgIcon, "Images/Icons/play.svg", true}, "Play", 7},
        {{0x656469636f6e001cull, EditorResourceKind::SvgIcon, "Images/Icons/pause.svg", true}, "Pause", 7},
        {{0x656469636f6e001dull, EditorResourceKind::SvgIcon, "Images/Icons/stop.svg", true}, "Stop", 7},
        {{0x656469636f6e001eull, EditorResourceKind::SvgIcon, "Images/Icons/step-forward.svg", true}, "Step Forward", 7},
        {{0x656469636f6e001full, EditorResourceKind::SvgIcon, "Images/Icons/fast-forward.svg", true}, "Fast Forward", 7}};

    inline constexpr u32 EditorToolbarIconCount =
        static_cast<u32>(sizeof(EditorToolbarIcons) / sizeof(EditorToolbarIcons[0]));

    [[nodiscard]] inline containers::ArraySpan<const EditorToolbarIconDescriptor> GetEditorToolbarIconDescriptors() noexcept
    {
        return {EditorToolbarIcons, EditorToolbarIconCount};
    }
} // namespace vanguard::editor::detail
