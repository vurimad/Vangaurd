group "Engine/Tools"

project "assets"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.hpp", "src/**.cpp" }
    includedirs {
        "include",
        "src",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include",
        "../packages/include"
    }
    links {
        "packages",
        "resources",
        "crypto",
        "serialization",
        "filesystem",
        "io",
        "containers",
        "concurrency",
        "memory",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" }
    }

group "Tests/Assets"

project "assetsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/assets_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include"
    }
    links {
        "assets",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "io",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/assets_tests.cpp" } }

project "assetsPersistentCacheTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/assets_persistent_cache_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include"
    }
    links {
        "assets",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "io",
        "ioCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/assets_persistent_cache_tests.cpp" } }

project "assetGraphTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/asset_graph_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include"
    }
    links {
        "assets",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "io",
        "ioCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/asset_graph_tests.cpp" } }

project "assetIndexTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/asset_index_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include"
    }
    links {
        "assets",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "io",
        "ioCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/asset_index_tests.cpp" } }

project "assetRecookerTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/asset_recooker_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include"
    }
    links {
        "assets",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "io",
        "ioCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/asset_recooker_tests.cpp" } }

project "packagePlannerTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/package_planner_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../jobs/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include",
        "../packages/include"
    }
    links {
        "assets",
        "packages",
        "resources",
        "crypto",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "io",
        "ioCompat",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/package_planner_tests.cpp" } }
