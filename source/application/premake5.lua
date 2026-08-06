group "Engine/Runtime"

project "application"
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
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../concurrency/include"
    }
    links { "concurrency", "diagnostics", "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Application"

project "applicationTests"
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
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../concurrency/include"
    }
    links {
        "application", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers",
        "containersCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi",
        "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
