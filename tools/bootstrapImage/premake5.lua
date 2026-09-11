local importedRoot = "../../source/imported/common"
local nvToolsRoot = path.getabsolute("../../source/imported/external/nvToolsExt")
local oodleRoot = path.getabsolute("../../source/imported/external/oodle")

group "Tools/Bootstrap"

project "bootstrapImage"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(tools_output_root)
    objdir(object_root)
    debugdir(tools_output_root)
    debugargs { '"' .. runtime_output_root .. '"', '"' .. path.getabsolute("../../source/rendering/shaders") .. '"' }
    deployVanguardRuntimeDependencies(false)
    files { "src/**.cpp", "README.md" }
    includedirs {
        "../../source/assets/include", "../../source/world/include", "../../source/prefabs/include",
        "../../source/gameInput/include", "../../source/input/include", "../../source/window/include",
        "../../source/meshes/include", "../../source/entities/include", "../../source/ecs/include", "../../source/gameWorld/include",
        "../../source/math/include", "../../source/rendering/include", "../../source/rhi/include",
        "../../source/streaming/include", "../../source/textures/include", "../../source/pipelineCache/include",
        "../../source/materialTools/include", "../../source/materials/include", "../../source/pipelines/include",
        "../../source/shaderTools/include", "../../source/shaders/include",
        "../../source/schemas/include", "../../source/reflection/include",
        "../../source/resources/include", "../../source/packages/include", "../../source/serialization/include",
        "../../source/crypto/include", "../../source/filesystem/include", "../../source/io/include",
        "../../source/jobs/include", "../../source/concurrency/include", "../../source/containers/include",
        "../../source/diagnostics/include", "../../source/memory/include", "../../source/system/include",
        path.join(importedRoot, "redSystem/include"), path.join(importedRoot, "redMemory/include"),
        path.join(importedRoot, "redMath/include"), path.join(importedRoot, "redContainers/include"), path.join(flecs_root, "distr"),
        path.join(importedRoot, "redIO/include"), path.join(importedRoot, "redCore/include"),
        path.join(importedRoot, "redCompression/include"), path.join(importedRoot, "redFileSystem/include")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "materialTools", "shaderTools", "entities", "rendering", "materials", "pipelines", "shaders",
        "assets", "world", "meshes", "prefabs", "schemas", "reflection", "gameInput", "input", "window",
        "packages", "resources", "crypto", "serialization", "serializationCompat", "filesystem",
        "filesystemCompat", "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat",
        "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "concurrency",
        "concurrencyCompat", "jobs", "jobsCompat", "redJobsCompat", "redContainersCompat", "redMathCompat",
        "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi",
        "Shell32", "Version", "ws2_32", "slang-compiler"
    }
    libdirs { path.join(slang_root, "lib") }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter {}
    vpaths { ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }
