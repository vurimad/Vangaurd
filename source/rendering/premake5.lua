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
    files { "include/**.hpp", "private/**.hpp", "src/**.cpp", "shaders/**.hlsli", "shaders/**.slang", "README.md", "docs/**.md" }
    includedirs {
        "include", "private", "../rhi/include", "../pipelineCache/include", "../pipelines/include", "../shaders/include",
        "../crypto/include", "../serialization/include", "../resources/include", "../filesystem/include", "../io/include",
        "../jobs/include", "../window/include", "../containers/include", "../concurrency/include", "../memory/include",
        "../system/include", "../meshes/include", "../textures/include", "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include"
    }
    links { "pipelineCache", "pipelines", "shaders", "rhi", "crypto", "serialization", "resources", "filesystem", "io",
            "jobs", "window", "containers", "concurrency", "memory", "system", "meshes", "textures", "streaming" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }, ["Shaders/*"] = { "shaders/**.hlsli", "shaders/**.slang" },
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
    removefiles { "tests/geometry_allocator_tests.cpp" }
    includedirs {
        "include", "../rhi/include", "../resources/include", "../filesystem/include", "../io/include", "../jobs/include",
        "../window/include", "../system/include", "../memory/include", "../containers/include", "../concurrency/include", "../crypto/include",
        "../serialization/include", "../packages/include", "../schemas/include", "../reflection/include", "../meshes/include", "../textures/include", "../streaming/include"
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "rendering", "meshes", "textures", "streaming", "rhi", "resources", "filesystem", "io", "serialization", "crypto", "jobs", "jobsCompat",
        "redJobsCompat", "redCoreCompat", "redIOCompat", "redFileSystemCompat", "filesystemCompat",
        "ioCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat",
        "redContainersCompat", "redMathCompat", "window", "containers", "containersCompat", "memory", "concurrency",
        "concurrencyCompat", "diagnostics", "diagnosticsCompat", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi",
        "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }

project "geometryAllocatorTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/geometry_allocator_tests.cpp" }
    includedirs {
        "include", "../rhi/include", "../rhi/nvrhi/include", "../rhi/nvrhi/private", "../jobs/include",
        "../memory/include", "../containers/include", "../concurrency/include", "../diagnostics/include", "../system/include", "../window/include",
        "../crypto/include", "../meshes/include", "../textures/include", "../serialization/include", "../resources/include", "../filesystem/include", "../io/include", "../streaming/include",
        "../packages/include", "../schemas/include", "../reflection/include",
        path.join(directx_headers_root, "include"), path.join(nvrhi_root, "include"), path.join(nvrhi_root, "src")
    }
    links {
        "rendering", "meshes", "textures", "streaming", "packages", "schemas", "reflection", "rhiNvrhi", "rhi", "nvrhiD3D12", "nvrhiCore", "directXGuids", "crypto", "serialization", "resources", "filesystem", "jobs", "diagnostics",
        "concurrency", "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/geometry_allocator_tests.cpp" } }
