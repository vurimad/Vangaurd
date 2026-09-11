group "Applications/Editor"

project "editor"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "VanguardEditor"
    targetdir(editor_output_root)
    objdir(object_root)
    debugdir(editor_output_root)
    deployVanguardRuntimeDependencies(true)
    files { "framework/include/**.hpp", "framework/src/**.cpp", "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/jobs/include",
        "../source/shaderTools/include", "../source/shaders/include", "../source/assets/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/io/include", "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/streaming/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/meshes/include", "../source/prefabs/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/crypto/include", "../source/math/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr"), imgui_root
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "shaderTools", "shaders", "assets", "crypto", "serializationCompat", "slang-compiler",
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "imgui", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    libdirs { path.join(slang_root, "lib") }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    vpaths {
        ["Framework/Public API/*"] = { "framework/include/**.hpp" },
        ["Framework/Source/*"] = { "framework/src/**.cpp" },
        ["Application/Private API/*"] = { "private/**.hpp" },
        ["Application/Source/*"] = { "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }
