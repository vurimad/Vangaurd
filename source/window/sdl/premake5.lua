group "Engine/Platform"

project "windowSdl"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md" }
    includedirs {
        "include", "../include", "../../system/include", "../../memory/include", "../../containers/include",
        "../../concurrency/include", path.join(sdl_root, "include")
    }
    dependson { "SDL3" }
    links { path.join(output_root, "SDL3.lib"), "window", "memory" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

group "Tests/Platform"

project "sdlWindowBackendTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../include", "../../system/include", "../../memory/include", "../../containers/include",
        "../../concurrency/include", path.join(sdl_root, "include")
    }
    links {
        "windowSdl", "window", "concurrency", "concurrencyCompat", "containers", "containersCompat", "memory",
        "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
