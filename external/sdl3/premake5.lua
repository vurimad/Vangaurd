local sdlRoot = path.getabsolute("../../../vendors/SDL-release-3.4.14")
local sdlProject = path.join(sdlRoot, "VisualC/SDL/SDL.vcxproj")
local msbuild = '"$(MSBuildToolsPath)\\MSBuild.exe"'

group "External"

project "SDL3"
    kind "Makefile"
    location(path.join(path.getabsolute("../.."), "build/projects/%{_ACTION}"))
    files { "README.md" }

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
