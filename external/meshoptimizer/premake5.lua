group "ThirdParty"

project "meshoptimizer"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "src/**.h",
        "src/**.cpp",
        "LICENSE.md",
        "README.md",
        "UPSTREAM.md"
    }
    excludes {
        "src/clusterizer.cpp",
        "src/meshletcodec.cpp",
        "src/meshletutils.cpp"
    }
    includedirs { "src" }
    vpaths {
        ["Source/*"] = { "src/**.h", "src/**.cpp" },
        ["Documentation"] = { "LICENSE.md", "README.md", "UPSTREAM.md" }
    }
