group "ThirdParty"

project "bcCodecs"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files { "src/**.h", "src/**.cpp", "LICENSE", "README.upstream.md", "UPSTREAM.md" }
    includedirs { "src" }
    vpaths {
        ["Source/*"] = { "src/**.h", "src/**.cpp" },
        ["Documentation"] = { "LICENSE", "README.upstream.md", "UPSTREAM.md" }
    }

