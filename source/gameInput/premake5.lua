group "Engine/Gameplay"

project "gameInput"
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
        "include", "../input/include", "../window/include", "../containers/include", "../memory/include", "../system/include",
        "../filesystem/include", "../io/include", "../serialization/include", "../resources/include"
    }
    links { "resources", "serialization", "filesystem", "io", "input", "window", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
             ["Documentation"] = { "README.md", "UPSTREAM.md" } }

group "Tests/Gameplay"

project "gameInputTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/game_input_tests.cpp" }
    includedirs {
        "include", "../input/include", "../window/include", "../containers/include", "../memory/include", "../system/include",
        "../filesystem/include", "../io/include", "../serialization/include", "../resources/include"
    }
    links {
        "gameInput", "resources", "serialization", "filesystem", "io", "input", "window", "containers", "memory",
        "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/game_input_tests.cpp" } }
