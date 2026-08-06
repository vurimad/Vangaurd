group "Engine/Runtime"

project "crypto"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.hpp", "src/**.cpp" }
    includedirs {
        "include",
        "src",
        "../system/include"
    }
    links { "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" }
    }

group "Tests/Crypto"

project "cryptoTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../diagnostics/include"
    }
    links {
        "crypto",
        "diagnostics",
        "diagnosticsCompat",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
