local sdlProject = path.join(sdl_root, "VisualC/SDL/SDL.vcxproj")
local msbuild = '"$(MSBuildToolsPath)\\MSBuild.exe"'

group "External"

project "SDL3"
    kind "Makefile"
    location(path.join(path.getabsolute("../.."), "build/projects/%{_ACTION}"))
    files { "README.md", "UPSTREAM.md", path.join(sdl_root, "LICENSE.txt") }

    filter "configurations:Debug"
        buildcommands {
            msbuild .. ' "' .. sdlProject .. '" /m /nologo /verbosity:minimal /p:Configuration=Debug /p:Platform=x64 /p:OutDir=' ..
                output_root .. '\\ /p:IntDir=' .. object_root .. '\\'
        }
        buildoutputs { path.join(output_root, "SDL3.lib"), path.join(output_root, "SDL3.dll") }

    filter "configurations:Development or configurations:Profile or configurations:Shipping"
        buildcommands {
            msbuild .. ' "' .. sdlProject .. '" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=x64 /p:OutDir=' ..
                output_root .. '\\ /p:IntDir=' .. object_root .. '\\'
        }
        buildoutputs { path.join(output_root, "SDL3.lib"), path.join(output_root, "SDL3.dll") }

    filter {}
