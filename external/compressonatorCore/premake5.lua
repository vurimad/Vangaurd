group "ThirdParty"

project "compressonatorCore"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        "cmp_core/shaders/bc2_encode_kernel.cpp", "cmp_core/shaders/bc4_encode_kernel.cpp",
        "cmp_core/shaders/bc5_encode_kernel.cpp", "cmp_core/shaders/bc6_encode_kernel.cpp", "cmp_core/shaders/**.h",
        "cmp_core/source/**.h", "cmp_math/**.h",
        "license/corelicense.txt", "UPSTREAM.md"
    }
    includedirs { "cmp_core/shaders", "cmp_core/source", "cmp_math" }
    vpaths {
        ["Source/*"] = { "cmp_core/**.h", "cmp_core/**.cpp", "cmp_math/**.h", "cmp_math/**.cpp" },
        ["Documentation"] = { "license/corelicense.txt", "UPSTREAM.md" }
    }
