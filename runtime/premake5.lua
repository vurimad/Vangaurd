group "Applications/Runtime"

project "runtime"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetname "Vanguard"
    targetdir(output_root)
    objdir(object_root)
    files { "src/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs {
        "src", "../source/application/include", "../source/engine/include", "../source/jobs/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/streaming/include",
        "../source/resources/include", "../source/world/include", "../source/meshes/include", "../source/prefabs/include",
        "../source/crypto/include", "../source/math/include", "../source/io/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        path.join(path.getabsolute("../../../vendors/flecs"), "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "platformWindows", "engine", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    vpaths {
        ["Application/*"] = { "src/**.hpp", "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }
