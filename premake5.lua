workspace "REDVanguard"
    architecture "x86_64"
    configurations {
        "Debug",
        "Development",
        "Profile",
        "Shipping"
    }
    startproject "runtime"
    location "build/projects/%{_ACTION}"
    staticruntime "On"
    exceptionhandling "Off"
    warnings "Extra"

    filter "system:windows"
        systemversion "latest"
        buildoptions {
            "/MP",
            "/FS",
            "/volatile:ms"
        }
        defines {
            "NOMINMAX",
            "WIN32_LEAN_AND_MEAN"
        }

    filter "configurations:Debug"
        defines {
            "VG_BUILD_DEBUG=1",
            "VG_ENABLE_ASSERTS=1"
        }
        runtime "Debug"
        runtimechecks "FastChecks"
        symbols "On"
        optimize "Off"

    filter "configurations:Development"
        defines {
            "VG_BUILD_DEVELOPMENT=1",
            "VG_ENABLE_ASSERTS=1"
        }
        runtime "Release"
        runtimechecks "Off"
        symbols "On"
        optimize "Speed"

    filter "configurations:Profile"
        defines {
            "VG_BUILD_PROFILE=1",
            "VG_ENABLE_ASSERTS=1"
        }
        runtime "Release"
        runtimechecks "Off"
        symbols "On"
        optimize "Full"

    filter "configurations:Shipping"
        defines {
            "VG_BUILD_SHIPPING=1",
            "VG_ENABLE_ASSERTS=0"
        }
        runtime "Release"
        runtimechecks "Off"
        symbols "Off"
        optimize "Full"

    filter {}

output_root = path.getabsolute("build/output/%{cfg.buildcfg}")
object_root = path.getabsolute("build/obj/%{prj.name}/%{cfg.buildcfg}")
runtime_output_root = path.getabsolute("bin/Runtime/%{cfg.buildcfg}")
editor_output_root = path.getabsolute("bin/Editor/%{cfg.buildcfg}")
tools_output_root = path.getabsolute("bin/Tools/%{prj.name}/%{cfg.buildcfg}")
external_root = path.getabsolute("external")
imgui_root = path.join(external_root, "imgui/upstream")
nvrhi_root = path.join(external_root, "nvrhi/upstream")
directx_headers_root = path.join(external_root, "directXHeaders/upstream")
flecs_root = path.join(external_root, "flecs/upstream")
sdl_root = path.join(external_root, "sdl3/upstream")
slang_root = path.join(external_root, "slang/upstream")
dxc_root = path.join(external_root, "dxc/upstream")

function deployVanguardRuntimeDependencies(includeSdl)
    if includeSdl then
        postbuildcommands {
            '{COPYFILE} "' .. path.join(output_root, "SDL3.dll") .. '" "%{cfg.targetdir}/SDL3.dll"'
        }
    end
    postbuildcommands {
        '{COPYFILE} "' .. path.join(output_root, "nvToolsExt64_1.dll") .. '" "%{cfg.targetdir}/nvToolsExt64_1.dll"'
    }
    filter "configurations:Debug"
        postbuildcommands {
            '{COPYFILE} "' .. path.join(output_root, "oo2ext_7_win64_debug.dll") ..
                '" "%{cfg.targetdir}/oo2ext_7_win64_debug.dll"'
        }
    filter "configurations:not Debug"
        postbuildcommands {
            '{COPYFILE} "' .. path.join(output_root, "oo2ext_7_win64.dll") ..
                '" "%{cfg.targetdir}/oo2ext_7_win64.dll"'
        }
    filter {}
end

local repository_root = path.getabsolute(".")
local engine_code_audit_script =
    path.join(repository_root, "scripts/audit-engine-code.ps1")
local engine_code_audit_command =
    'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' ..
    engine_code_audit_script ..
    '" -RepositoryRoot "' ..
    repository_root ..
    '"'

local audit_result = os.execute(engine_code_audit_command)
if audit_result ~= 0 and audit_result ~= true then
    error("Vanguard engine-service source audit failed.")
end

function enforceEngineCodePolicy()
    -- The repository-wide audit is the hard gate above during project generation. It must
    -- not be a per-project build event: Visual Studio treats such events as launch-time work
    -- and loses its fast up-to-date F5 path across the entire static-library dependency graph.
end

include "external/meshoptimizer"
include "external/assimp"
include "external/bcCodecs"
include "external/compressonatorCore"
include "external/imageCodecs"
include "external/flecs"
include "external/imgui"
include "external/sdl3"
include "external/nvrhi"
include "tools/bootstrapImage"
include "tools/nanovanguard"
include "source/system"
include "source/memory"
include "source/diagnostics"
include "source/concurrency"
include "source/math"
include "source/containers"
include "source/input"
include "source/window"
include "source/window/sdl"
include "source/gameInput"
include "source/io"
include "source/filesystem"
include "source/serialization"
include "source/crypto"
include "source/resources"
include "source/packages"
include "source/jobs"
include "source/application"
include "source/engine"
include "source/platform/windows"
include "runtime"
include "editor"
include "source/reflection"
include "source/schemas"
include "source/streaming"
include "source/assets"
include "source/projects"
include "source/gameInputTools"
include "source/rhi"
include "source/rhi/nvrhi"
include "source/shaders"
include "source/shaderTools"
include "source/rendering"
include "source/textures"
include "source/textureTools"
include "source/meshes"
include "source/meshTools"
include "source/pipelines"
include "source/pipelineCache"
include "source/materials"
include "source/materialTools"
include "source/prefabs"
include "source/ecs"
include "source/gameWorld"
include "source/world"
include "source/entities"
