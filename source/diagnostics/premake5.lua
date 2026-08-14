group "Engine/Compatibility"

project "diagnosticsCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"
    targetdir(output_root)
    objdir(object_root)

    files {
        "compat/**.cpp"
    }

    includedirs {
        "include",
        "private",
        "../system/include",
        "../imported/common/redSystem/include",
        "../imported/common/redSystem/src"
    }

    defines {
        "RED_COMPILER_MSC",
        "RED_VANGUARD_DISABLE_VTUNE",
        "_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING",
        "_SILENCE_CXX20_IS_POD_DEPRECATION_WARNING"
    }

    filter "configurations:Debug"
        defines { "RED_CONFIGURATION_DEBUG" }

    filter "configurations:Development"
        defines { "RED_CONFIGURATION_RELEASE" }

    filter "configurations:Profile"
        defines { "RED_CONFIGURATION_RELEASE" }

    filter "configurations:Shipping"
        defines { "RED_CONFIGURATION_FINAL" }

    filter {}

    links {
        "redSystemCompat",
        "system"
    }

    vpaths {
        ["Compatibility/*"] = { "compat/**.cpp" }
    }

group "Engine/Runtime"

project "diagnostics"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "include/**.hpp",
        "private/**.hpp",
        "src/**.cpp"
    }

    includedirs {
        "include",
        "private",
        "../system/include"
    }

    links {
        "diagnosticsCompat",
        "system"
    }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = {
            "src/**.cpp"
        }
    }

group "Tests/Diagnostics"

project "diagnosticsSmoke"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "tests/diagnostics_smoke.cpp"
    }

    includedirs {
        "include",
        "../system/include"
    }

    links {
        "diagnostics",
        "diagnosticsCompat",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version"
    }

    vpaths {
        ["Tests/*"] = { "tests/**.cpp" }
    }
