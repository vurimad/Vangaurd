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
            "/MP"
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

output_root = path.getabsolute("bin/%{cfg.buildcfg}")
object_root = path.getabsolute("build/obj/%{prj.name}/%{cfg.buildcfg}")

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
    prebuildcommands {
        engine_code_audit_command
    }
end

include "external/meshoptimizer"
include "external/bcCodecs"
include "external/compressonatorCore"
include "external/imageCodecs"
include "external/flecs"
include "external/sdl3"
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
include "source/gameInputTools"
include "source/shaders"
include "source/textures"
include "source/textureTools"
include "source/meshes"
include "source/meshTools"
include "source/pipelines"
include "source/pipelineCache"
include "source/materials"
include "source/prefabs"
include "source/ecs"
include "source/gameWorld"
include "source/world"
include "source/entities"
