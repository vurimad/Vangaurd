local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMathRoot = path.join(importedRoot, "redMath")
local adaptedMathRoot = "adapted"

local function applyRedCompatibilitySettings()
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"
    targetdir(output_root)
    objdir(object_root)

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
end

group "Engine/Compatibility"

project "redMathCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redMath", "RED_EXPORT_redMath" }
    pchheader "build.h"
    pchsource(path.join(redMathRoot, "src/build.cpp"))
    includedirs {
        redMathRoot,
        path.join(redMathRoot, "include"),
        path.join(redMathRoot, "src"),
        path.join(redSystemRoot, "include")
    }
    files {
        path.join(redMathRoot, "include/**.h"),
        path.join(redMathRoot, "include/**.hpp"),
        path.join(redMathRoot, "include/**.inl"),
        path.join(redMathRoot, "src/**.h"),
        path.join(redMathRoot, "src/**.cpp"),
        path.join(redMathRoot, "src/**.natvis")
    }
    excludes {
        path.join(redMathRoot, "src/redScalar_simd.cpp"),
        path.join(redMathRoot, "src/vectorFunctions_float.cpp")
    }
    vpaths {
        ["Imported/RED Math/Public/*"] = {
            path.join(redMathRoot, "include/**.h"),
            path.join(redMathRoot, "include/**.hpp"),
            path.join(redMathRoot, "include/**.inl")
        },
        ["Imported/RED Math/Private/*"] = {
            path.join(redMathRoot, "src/**.h"),
            path.join(redMathRoot, "src/**.cpp")
        },
        ["Imported/RED Math/Debugger/*"] = {
            path.join(redMathRoot, "src/**.natvis")
        }
    }
    links { "redSystemCompat" }

group "Engine/Runtime"

project "math"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4100", "4189", "4201", "4458" }
    targetdir(output_root)
    objdir(object_root)
    defines {
        "RED_COMPILER_MSC",
        "RED_VANGUARD_DISABLE_VTUNE",
        "RED_MODULE_redMath",
        "RED_EXPORT_redMath",
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

    pchheader "build.h"
    pchsource(path.join(adaptedMathRoot, "src/build.cpp"))
    files {
        "include/**.hpp",
        path.join(adaptedMathRoot, "include/**.h"),
        path.join(adaptedMathRoot, "include/**.hpp"),
        path.join(adaptedMathRoot, "include/**.inl"),
        path.join(adaptedMathRoot, "src/**.h"),
        path.join(adaptedMathRoot, "src/**.cpp"),
        path.join(adaptedMathRoot, "src/**.natvis")
    }
    excludes {
        path.join(adaptedMathRoot, "src/redScalar_simd.cpp"),
        path.join(adaptedMathRoot, "src/vectorFunctions_float.cpp")
    }
    includedirs {
        "include",
        adaptedMathRoot,
        path.join(adaptedMathRoot, "include"),
        path.join(adaptedMathRoot, "src"),
        "../system/include",
        path.join(redSystemRoot, "include")
    }
    links {
        "redSystemCompat",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Adapted/RED Math/Public/*"] = {
            path.join(adaptedMathRoot, "include/**.h"),
            path.join(adaptedMathRoot, "include/**.hpp"),
            path.join(adaptedMathRoot, "include/**.inl")
        },
        ["Adapted/RED Math/Private/*"] = {
            path.join(adaptedMathRoot, "src/**.h"),
            path.join(adaptedMathRoot, "src/**.cpp")
        },
        ["Adapted/RED Math/Debugger/*"] = {
            path.join(adaptedMathRoot, "src/**.natvis")
        }
    }

group "Tests/Math"

project "mathTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4189", "4201", "4458" }
    targetdir(output_root)
    objdir(object_root)
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

    files { "tests/math_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        adaptedMathRoot,
        path.join(adaptedMathRoot, "include"),
        path.join(redSystemRoot, "include")
    }
    links {
        "math",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/**.cpp" }
    }
