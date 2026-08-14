local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")
local redMathRoot = path.join(importedRoot, "redMath")
local redContainersRoot = path.join(importedRoot, "redContainers")
local redIORoot = path.join(importedRoot, "redIO")
local redCoreRoot = path.join(importedRoot, "redCore")
local redJobsRoot = path.join(importedRoot, "redJobs2")
local nvToolsRoot = path.getabsolute("../imported/external/nvToolsExt")

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

local function addImportedFiles(root)
    files {
        path.join(root, "include/**.h"),
        path.join(root, "include/**.hpp"),
        path.join(root, "include/**.inl"),
        path.join(root, "src/**.h"),
        path.join(root, "src/**.hpp"),
        path.join(root, "src/**.inl"),
        path.join(root, "src/**.c"),
        path.join(root, "src/**.cc"),
        path.join(root, "src/**.cpp"),
        path.join(root, "src/**.natvis"),
        path.join(root, "src/**.natstepfilter")
    }
end

local function addImportedVPaths(label, root)
    vpaths {
        ["Imported/" .. label .. "/Public/*"] = {
            path.join(root, "include/**.h"),
            path.join(root, "include/**.hpp"),
            path.join(root, "include/**.inl")
        },
        ["Imported/" .. label .. "/Private/*"] = {
            path.join(root, "src/**.h"),
            path.join(root, "src/**.hpp"),
            path.join(root, "src/**.inl"),
            path.join(root, "src/**.c"),
            path.join(root, "src/**.cc"),
            path.join(root, "src/**.cpp")
        },
        ["Imported/" .. label .. "/Debugger/*"] = {
            path.join(root, "src/**.natvis"),
            path.join(root, "src/**.natstepfilter")
        }
    }
end

group "Engine/Compatibility"

project "redCoreCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redCore", "RED_EXPORT_redCore" }
    pchheader "build.h"
    pchsource(path.join(redCoreRoot, "src/build.cpp"))
    includedirs {
        redCoreRoot,
        path.join(redCoreRoot, "include"),
        path.join(redCoreRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redIORoot, "include")
    }
    addImportedFiles(redCoreRoot)
    addImportedVPaths("RED Core", redCoreRoot)
    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }
    links {
        "nvToolsExt64_1",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }
    postbuildcommands {
        '{COPYFILE} "' ..
            path.join(
                nvToolsRoot,
                "lib/x64.Release/nvToolsExt64_1.dll") ..
            '" "%{cfg.targetdir}/nvToolsExt64_1.dll"'
    }

project "redJobsCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redJobs2", "RED_EXPORT_redJobs2" }
    pchheader "build.h"
    pchsource(path.join(redJobsRoot, "src/build.cpp"))
    includedirs {
        redJobsRoot,
        path.join(redJobsRoot, "include"),
        path.join(redJobsRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redIORoot, "include"),
        path.join(redCoreRoot, "include")
    }
    addImportedFiles(redJobsRoot)
    addImportedVPaths("RED Jobs", redJobsRoot)
    links {
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }

project "jobsCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    applyRedCompatibilitySettings()
    files { "compat/**.cpp" }
    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        redJobsRoot,
        path.join(redJobsRoot, "include"),
        path.join(redJobsRoot, "src"),
        path.join(redCoreRoot, "include"),
        path.join(redIORoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redMathRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redSystemRoot, "include")
    }
    vpaths {
        ["Compatibility/*"] = { "compat/**.cpp" }
    }
    links {
        "redJobsCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }

group "Engine/Runtime"

project "jobs"
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
        "src/**.cpp",
        "README.md",
        "UPSTREAM.md",
        "docs/**.md"
    }

    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        "../concurrency/include"
    }

    links {
        "jobsCompat",
        "memory",
        "concurrency",
        "system"
    }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md", "docs/**.md" }
    }

group "Tests/Jobs"

project "jobsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files { "tests/jobs_tests.cpp" }

    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include"
    }

    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }

    links {
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "concurrency",
        "concurrencyCompat",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
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

project "jobsStress"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files { "tests/jobs_stress.cpp" }

    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include"
    }

    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }

    links {
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "concurrency",
        "concurrencyCompat",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
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

project "jobsSoak"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files { "tests/jobs_soak.cpp" }

    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include"
    }

    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }

    links {
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "concurrency",
        "concurrencyCompat",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
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

group "Benchmarks/Jobs"

project "jobsBenchmarks"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files { "benchmarks/jobs_benchmarks.cpp" }

    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include"
    }

    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }

    links {
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "concurrency",
        "concurrencyCompat",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }

    vpaths {
        ["Benchmarks/*"] = { "benchmarks/**.cpp" }
    }
