group "Engine/Foundation"

project "input"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs { "include", "../window/include", "../containers/include", "../system/include", "../memory/include" }
    links { "window", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
             ["Documentation"] = { "README.md", "UPSTREAM.md" } }
