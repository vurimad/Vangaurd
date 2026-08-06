local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")

local function applyRedCompatibilitySettings()
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"

    defines {
        "RED_COMPILER_MSC",
        "RED_VANGUARD_DISABLE_VTUNE",
        "_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING"
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

project "redSystemCompat"
    kind "StaticLib"
    targetdir(output_root)
    objdir(object_root)
    applyRedCompatibilitySettings()

    pchheader "build.h"
    pchsource(path.join(redSystemRoot, "src/build.cpp"))

    includedirs {
        path.join(redSystemRoot, "include"),
        path.join(redSystemRoot, "src"),
        "../../external/ittnotify/include"
    }

    files {
        path.join(redSystemRoot, "include/**.h"),
        path.join(redSystemRoot, "include/**.hpp"),
        path.join(redSystemRoot, "include/**.inl"),
        path.join(redSystemRoot, "src/**.h"),
        path.join(redSystemRoot, "src/**.hpp"),
        path.join(redSystemRoot, "src/**.cpp")
    }

    excludes {
        path.join(redSystemRoot, "src/dbgUtilsLinux.cpp"),
        path.join(redSystemRoot, "src/dbgUtilsPS4.cpp"),
        path.join(redSystemRoot, "src/dbgUtilsXboxOne.cpp"),
        path.join(redSystemRoot, "src/errorHandlerImplErrorHooksLinux.cpp"),
        path.join(redSystemRoot, "src/errorHandlerImplErrorHooksOrbis.cpp"),
        path.join(redSystemRoot, "src/errorReporterIPCPS4.cpp"),
        path.join(redSystemRoot, "src/redThreadsThreadLinuxAPI.cpp"),
        path.join(redSystemRoot, "src/redThreadsThreadOrbisAPI.cpp"),
        path.join(redSystemRoot, "src/sce_callstack.cpp"),
        path.join(redSystemRoot, "src/timerLinux.cpp"),
        path.join(redSystemRoot, "src/timerOrbis.cpp")
    }

    vpaths {
        ["Imported/RED System/Public/*"] = {
            path.join(redSystemRoot, "include/**.h"),
            path.join(redSystemRoot, "include/**.hpp"),
            path.join(redSystemRoot, "include/**.inl")
        },
        ["Imported/RED System/Private/*"] = {
            path.join(redSystemRoot, "src/**.h"),
            path.join(redSystemRoot, "src/**.hpp"),
            path.join(redSystemRoot, "src/**.cpp")
        }
    }

group "Engine/Runtime"

project "memory"
    enforceEngineCodePolicy()
    kind "StaticLib"
    targetdir(output_root)
    objdir(object_root)
    applyRedCompatibilitySettings()

    pchheader "build.h"
    pchsource(path.join(redMemoryRoot, "src/build.cpp"))

    includedirs {
        redMemoryRoot,
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redSystemRoot, "include"),
        "../system/include",
        "include"
    }

    defines {
        "RED_MODULE_redMemory",
        "RED_EXPORT_redMemory"
    }

    files {
        path.join(redMemoryRoot, "include/**.h"),
        path.join(redMemoryRoot, "include/**.hpp"),
        path.join(redMemoryRoot, "include/**.inl"),
        path.join(redMemoryRoot, "src/**.h"),
        path.join(redMemoryRoot, "src/**.hpp"),
        path.join(redMemoryRoot, "src/**.cpp"),
        path.join(redMemoryRoot, "src/**.natvis"),
        path.join(redMemoryRoot, "src/**.natstepfilter"),
        "include/**.hpp",
        "src/**.hpp",
        "src/vanguard_memory.cpp",
        "compat/**.cpp"
    }

    excludes {
        path.join(redMemoryRoot, "src/DLL/**"),
        path.join(redMemoryRoot, "src/operatorsLegacy.cpp"),
        path.join(redMemoryRoot, "src/hooks.cpp"),
        path.join(redMemoryRoot, "src/flexibleSystemAllocator.cpp"),
        path.join(redMemoryRoot, "src/systemPageAllocatorDurango.cpp"),
        path.join(redMemoryRoot, "src/systemPageAllocatorOrbis.cpp"),
        path.join(redMemoryRoot, "src/systemPageAllocatorLinux.cpp"),
        path.join(redMemoryRoot, "src/systemAllocatorDurango.cpp"),
        path.join(redMemoryRoot, "src/systemAllocatorLinux.cpp"),
        path.join(redMemoryRoot, "src/systemAllocatorOrbis.cpp"),
        path.join(redMemoryRoot, "src/systemAllocatorOrbisHelper.cpp"),
        path.join(redMemoryRoot, "src/callstackCollectorOrbis.cpp"),
        path.join(redMemoryRoot, "src/callstackCollectorLinux.cpp"),
        path.join(redMemoryRoot, "src/metricsCaptureDurango.cpp"),
        path.join(redMemoryRoot, "src/metricsCaptureLinux.cpp"),
        path.join(redMemoryRoot, "src/metricsCaptureOrbis.cpp"),
        path.join(redMemoryRoot, "src/memoryAnalyzerOrbis.cpp")
    }

    links {
        "redSystemCompat",
        "system"
    }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = {
            "src/**.hpp",
            "src/vanguard_memory.cpp"
        },
        ["Compatibility/*"] = { "compat/**.cpp" },
        ["Imported/RED Memory/Public/*"] = {
            path.join(redMemoryRoot, "include/**.h"),
            path.join(redMemoryRoot, "include/**.hpp"),
            path.join(redMemoryRoot, "include/**.inl")
        },
        ["Imported/RED Memory/Private/*"] = {
            path.join(redMemoryRoot, "src/**.h"),
            path.join(redMemoryRoot, "src/**.hpp"),
            path.join(redMemoryRoot, "src/**.cpp")
        },
        ["Imported/RED Memory/Debugger/*"] = {
            path.join(redMemoryRoot, "src/**.natvis"),
            path.join(redMemoryRoot, "src/**.natstepfilter")
        }
    }

group "Tests/Memory"

project "memorySmoke"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "tests/memory_smoke.cpp"
    }

    includedirs {
        "include",
        "../system/include"
    }

    links {
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

project "memoryPoolTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "tests/memory_pool_tests.cpp"
    }

    includedirs {
        "include",
        "../system/include"
    }

    links {
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
