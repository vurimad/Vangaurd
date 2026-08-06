group "Engine/Foundation"

project "window"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "../system/include", "../memory/include", "../containers/include", "../concurrency/include"
    }
    links { "concurrency", "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Foundation"

project "windowTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../system/include", "../memory/include", "../containers/include", "../concurrency/include"
    }
    links {
        "window", "concurrency", "concurrencyCompat", "containers", "containersCompat", "memory",
        "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
