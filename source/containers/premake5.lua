local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")
local redMathRoot = path.join(importedRoot, "redMath")
local redContainersRoot = path.join(importedRoot, "redContainers")

local function applyRedCompatibilitySettings()
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"
    targetdir(output_root)
    objdir(object_root)

    defines {
        "RED_COMPILER_MSC",
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

project "redContainersCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redContainers", "RED_EXPORT_redContainers" }
    pchheader "build.h"
    pchsource(path.join(redContainersRoot, "src/build.cpp"))

    includedirs {
        redContainersRoot,
        path.join(redContainersRoot, "include"),
        path.join(redContainersRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include")
    }

    files {
        path.join(redContainersRoot, "include/**.h"),
        path.join(redContainersRoot, "include/**.hpp"),
        path.join(redContainersRoot, "include/**.inl"),
        path.join(redContainersRoot, "src/**.h"),
        path.join(redContainersRoot, "src/**.hpp"),
        path.join(redContainersRoot, "src/**.inl"),
        path.join(redContainersRoot, "src/**.cpp"),
        path.join(redContainersRoot, "src/**.natvis"),
        path.join(redContainersRoot, "src/**.natstepfilter")
    }

    links { "memory", "redMathCompat", "redSystemCompat" }

    vpaths {
        ["Imported/RED Containers/Public/*"] = {
            path.join(redContainersRoot, "include/**.h"),
            path.join(redContainersRoot, "include/**.hpp"),
            path.join(redContainersRoot, "include/**.inl")
        },
        ["Imported/RED Containers/Private/*"] = {
            path.join(redContainersRoot, "src/**.h"),
            path.join(redContainersRoot, "src/**.hpp"),
            path.join(redContainersRoot, "src/**.inl"),
            path.join(redContainersRoot, "src/**.cpp")
        },
        ["Imported/RED Containers/Debugger/*"] = {
            path.join(redContainersRoot, "src/**.natvis"),
            path.join(redContainersRoot, "src/**.natstepfilter")
        }
    }

project "containersCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    applyRedCompatibilitySettings()

    files { "compat/**.cpp" }

    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        redContainersRoot,
        path.join(redContainersRoot, "include"),
        path.join(redContainersRoot, "src"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMathRoot, "include")
    }

    links { "redContainersCompat", "memory", "redMathCompat", "redSystemCompat" }

    vpaths {
        ["Compatibility/*"] = { "compat/**.cpp" }
    }

group "Engine/Runtime"

project "containers"
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
        "../system/include",
        "../memory/include"
    }

    links { "containersCompat", "memory", "system" }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }
    }

group "Tests/Containers"

project "containersTests"
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
        "../memory/include"
    }

    links {
        "containers",
        "containersCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
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
