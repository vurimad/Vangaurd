group "Tools/Nanovanguard"

project "nanovanguard"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(tools_output_root)
    objdir(object_root)
    debugdir(tools_output_root)
    includedirs { "../../source/assets/include", "../../source/resources/include", "../../source/crypto/include", "../../source/serialization/include", "../../source/concurrency/include", "../../source/jobs/include" }
    links { "assets", "resources", "crypto", "serialization", "serializationCompat", "diagnostics", "diagnosticsCompat", "concurrency", "concurrencyCompat", "jobs", "jobsCompat", "redJobsCompat" }
    files { "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs { "private", "../../source/projects/include", "../../source/filesystem/include", "../../source/io/include",
                  "../../source/containers/include", "../../source/memory/include", "../../source/system/include" }
    links { "projects", "filesystem", "io", "containers", "memory", "redSystemCompat", "Bcrypt", "Shell32", "Advapi32",
            "Dbghelp", "Psapi", "Shlwapi", "Version", "system" }
    vpaths {
        ["CLI/Private API/*"] = { "private/**.hpp" },
        ["CLI/Source/*"] = { "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

group "Tests/Nanovanguard"

project "nanovanguardTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "private/**.hpp", "src/**.cpp", "platform/windows/project_platform_windows.cpp", "tests/**.cpp" }
    includedirs { "private", "../../source/projects/include", "../../source/filesystem/include", "../../source/io/include",
                  "../../source/containers/include", "../../source/memory/include", "../../source/system/include" }
    links { "projects", "filesystem", "io", "containers", "memory", "Bcrypt", "system" }
    vpaths {
        ["CLI/Private API/*"] = { "private/**.hpp" }, ["CLI/Source/*"] = { "src/**.cpp" },
        ["Tests/*"] = { "tests/**.cpp" }
    }
