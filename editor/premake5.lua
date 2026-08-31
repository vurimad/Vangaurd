group "Applications/Editor"

project "editor"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetname "VanguardEditor"
    targetdir(editor_output_root)
    objdir(object_root)
    debugdir(editor_output_root)
    deployVanguardRuntimeDependencies(true)
    files { "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs {
        "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/jobs/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/io/include", "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/streaming/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/meshes/include", "../source/prefabs/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/crypto/include", "../source/math/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    vpaths {
        ["Application/Private API/*"] = { "private/**.hpp" },
        ["Application/Source/*"] = { "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }
