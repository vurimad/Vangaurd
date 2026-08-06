group "Engine/Runtime"

project "resources"
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
        "src/**.hpp",
        "src/**.cpp"
    }
    includedirs {
        "include",
        "src",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../jobs/include",
        "../containers/include",
        "../diagnostics/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include"
    }
    links {
        "serialization",
        "containers",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "memory",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" }
    }

group "Tests/Resources"

project "resourcesTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/resources_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../jobs/include",
        "../containers/include",
        "../diagnostics/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include"
    }
    links {
        "resources",
        "serialization",
        "serializationCompat",
        "filesystem",
        "io",
        "diagnostics",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
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
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }

project "resourcePipelineTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/resource_pipeline_tests.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../jobs/include",
        "../containers/include",
        "../diagnostics/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include"
    }
    links {
        "resources",
        "serialization",
        "serializationCompat",
        "filesystem",
        "io",
        "diagnostics",
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
    vpaths { ["Tests/*"] = { "tests/resource_pipeline_tests.cpp" } }
