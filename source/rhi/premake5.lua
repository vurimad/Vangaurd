group "Engine/Rendering"

project "rhi"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs { "include", "../system/include", "../memory/include", "../containers/include" }
    links { "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Rendering"

project "rhiTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs { "include", "../system/include", "../memory/include", "../containers/include" }
    links {
        "rhi", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
