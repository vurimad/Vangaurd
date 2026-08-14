group "Engine/Platform"

project "platformWindows"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "../../application/include", "../../input/include", "../../system/include", "../../memory/include",
        "../../containers/include", "../../concurrency/include", "../../window/include", "../../window/sdl/include",
        path.join(sdl_root, "include")
    }
    dependson { "SDL3" }
    links { path.join(output_root, "SDL3.lib"), "windowSdl", "window", "application", "input", "concurrency", "memory" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Application"

project "platformWindowsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../../application/include", "../../input/include", "../../system/include", "../../memory/include",
        "../../diagnostics/include", "../../containers/include", "../../concurrency/include", "../../window/include",
        "../../window/sdl/include", path.join(sdl_root, "include")
    }
    links {
        "platformWindows", "windowSdl", "window", "application", "input", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
