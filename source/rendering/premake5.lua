group "Engine/Rendering"

local nvToolsRoot = path.getabsolute("../imported/external/nvToolsExt")

project "rendering"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "private/**.hpp", "src/**.cpp", "shaders/**.hlsli", "README.md", "docs/**.md" }
    includedirs {
        "include", "private", "../rhi/include", "../pipelineCache/include", "../pipelines/include", "../shaders/include",
        "../crypto/include", "../serialization/include", "../resources/include", "../filesystem/include", "../io/include",
        "../jobs/include", "../window/include", "../containers/include", "../concurrency/include", "../memory/include",
        "../system/include"
    }
    links { "pipelineCache", "pipelines", "shaders", "rhi", "crypto", "serialization", "resources", "filesystem", "io",
            "jobs", "window", "containers", "concurrency", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }, ["Shaders/*"] = { "shaders/**.hlsli" },
        ["Documentation"] = { "README.md", "docs/**.md" }
    }

group "Tests/Rendering"

project "renderingTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../rhi/include", "../resources/include", "../filesystem/include", "../io/include", "../jobs/include",
        "../window/include", "../system/include", "../memory/include", "../containers/include", "../concurrency/include"
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "rendering", "rhi", "resources", "filesystem", "io", "serialization", "crypto", "jobs", "jobsCompat",
        "redJobsCompat", "redCoreCompat", "redIOCompat", "redFileSystemCompat", "filesystemCompat",
        "ioCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat",
        "redContainersCompat", "redMathCompat", "window", "containers", "containersCompat", "memory", "concurrency",
        "concurrencyCompat", "diagnostics", "diagnosticsCompat", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi",
        "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
