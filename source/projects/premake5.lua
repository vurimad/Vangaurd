group "Engine/Tools"
project "projects"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md" }
    includedirs { "include", "../filesystem/include", "../io/include", "../containers/include", "../memory/include", "../system/include" }
    links { "filesystem", "io", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }

group "Tests/Projects"
project "projectsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs { "include", "../filesystem/include", "../io/include", "../containers/include", "../memory/include", "../system/include" }
    links { "projects", "filesystem", "io", "containers", "memory", "system" }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
