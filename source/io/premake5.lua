local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")
local redMathRoot = path.join(importedRoot, "redMath")
local redContainersRoot = path.join(importedRoot, "redContainers")
local redIORoot = path.join(importedRoot, "redIO")
local redCoreRoot = path.join(importedRoot, "redCore")

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

project "redIOCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redIO", "RED_EXPORT_redIO" }
    pchheader "build.h"
    pchsource(path.join(redIORoot, "src/build.cpp"))

    includedirs {
        redIORoot,
        path.join(redIORoot, "include"),
        path.join(redIORoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redCoreRoot, "include")
    }

    files {
        path.join(redIORoot, "include/**.h"),
        path.join(redIORoot, "include/**.hpp"),
        path.join(redIORoot, "include/**.inl"),
        path.join(redIORoot, "src/**.h"),
        path.join(redIORoot, "src/**.hpp"),
        path.join(redIORoot, "src/**.inl"),
        path.join(redIORoot, "src/**.c"),
        path.join(redIORoot, "src/**.cc"),
        path.join(redIORoot, "src/**.cpp"),
        path.join(redIORoot, "src/**.natvis"),
        path.join(redIORoot, "src/**.natstepfilter")
    }

    excludes {
        path.join(redIORoot, "src/redIOProfilerOrbis.cpp"),
        path.join(redIORoot, "src/redIOSystemFileLinuxAPI.cpp"),
        path.join(redIORoot, "src/redIOSystemFileOrbisAPI.cpp"),
        path.join(redIORoot, "src/redIOTraceWriterOrbis.cpp"),
        path.join(redIORoot, "src/redIOWorkerOrbis.cpp")
    }

    links { "redContainersCompat", "memory", "redSystemCompat" }

    vpaths {
        ["Imported/RED IO/Public/*"] = {
            path.join(redIORoot, "include/**.h"),
            path.join(redIORoot, "include/**.hpp"),
            path.join(redIORoot, "include/**.inl")
        },
        ["Imported/RED IO/Private/*"] = {
            path.join(redIORoot, "src/**.h"),
            path.join(redIORoot, "src/**.hpp"),
            path.join(redIORoot, "src/**.inl"),
            path.join(redIORoot, "src/**.c"),
            path.join(redIORoot, "src/**.cc"),
            path.join(redIORoot, "src/**.cpp")
        },
        ["Imported/RED IO/Debugger/*"] = {
            path.join(redIORoot, "src/**.natvis"),
            path.join(redIORoot, "src/**.natstepfilter")
        }
    }

project "ioCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    applyRedCompatibilitySettings()

    files { "compat/**.cpp" }

    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../containers/include",
        redIORoot,
        path.join(redIORoot, "include"),
        path.join(redIORoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redCoreRoot, "include")
    }

    links {
        "redIOCompat",
        "redContainersCompat",
        "memory",
        "redSystemCompat"
    }

    vpaths {
        ["Compatibility/*"] = { "compat/**.cpp" }
    }

group "Engine/Runtime"

project "io"
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
        "../memory/include",
        "../diagnostics/include",
        "../containers/include"
    }

    links {
        "ioCompat",
        "diagnostics",
        "containers",
        "memory",
        "system"
    }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }
    }

group "Tests/IO"

project "ioTests"
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
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include"
    }

    links {
        "io",
        "ioCompat",
        "redIOCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "concurrency",
        "concurrencyCompat",
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
