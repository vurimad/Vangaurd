group "ThirdParty/Editor"

project "imgui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(imgui_root, "imconfig.h"),
        path.join(imgui_root, "imgui.h"),
        path.join(imgui_root, "imgui_internal.h"),
        path.join(imgui_root, "imstb_rectpack.h"),
        path.join(imgui_root, "imstb_textedit.h"),
        path.join(imgui_root, "imstb_truetype.h"),
        path.join(imgui_root, "imgui.cpp"),
        path.join(imgui_root, "imgui_draw.cpp"),
        path.join(imgui_root, "imgui_tables.cpp"),
        path.join(imgui_root, "imgui_widgets.cpp"),
        path.join(imgui_root, "LICENSE.txt"),
        "UPSTREAM.md"
    }
    includedirs { imgui_root }
    vpaths {
        ["Dear ImGui/Headers/*"] = {
            path.join(imgui_root, "imconfig.h"),
            path.join(imgui_root, "imgui.h"),
            path.join(imgui_root, "imgui_internal.h"),
            path.join(imgui_root, "imstb_*.h")
        },
        ["Dear ImGui/Source/*"] = {
            path.join(imgui_root, "imgui.cpp"),
            path.join(imgui_root, "imgui_draw.cpp"),
            path.join(imgui_root, "imgui_tables.cpp"),
            path.join(imgui_root, "imgui_widgets.cpp")
        },
        ["Documentation"] = { path.join(imgui_root, "LICENSE.txt"), "UPSTREAM.md" }
    }
