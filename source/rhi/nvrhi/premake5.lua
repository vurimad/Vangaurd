group "Engine/Rendering"

project "rhiNvrhi"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "private/**.hpp", "src/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "private", "../include", "../../system/include", "../../memory/include", "../../containers/include",
        "../../concurrency/include", "../../diagnostics/include", "../../jobs/include",
        path.join(directx_headers_root, "include"), path.join(nvrhi_root, "include"), path.join(nvrhi_root, "src")
    }
    links { "rhi", "nvrhiD3D12", "nvrhiCore", "directXGuids", "jobs", "diagnostics", "concurrency", "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Private API/*"] = { "private/**.hpp" },
        ["Source/Common/*"] = { "src/common_backend.cpp", "src/resource_lifetime.cpp" },
        ["Source/D3D12/*"] = { "src/d3d12_backend.cpp" },
        ["Source/*"] = { "src/**.hpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Rendering"

project "rhiNvrhiTests"
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
        "include", "private", "../include", "../../system/include", "../../memory/include", "../../containers/include",
        "../../concurrency/include", "../../diagnostics/include", "../../jobs/include", "../../rendering/include",
        "../../pipelineCache/include", "../../pipelines/include", "../../shaders/include", "../../crypto/include",
        "../../serialization/include", "../../resources/include", "../../filesystem/include", "../../io/include",
        "../../window/include"
    }
    links {
        "rendering", "pipelineCache", "pipelines", "shaders", "crypto", "serialization", "resources", "filesystem", "io", "window",
        "rhiNvrhi", "rhi", "nvrhiD3D12", "nvrhiCore", "directXGuids", "jobs", "diagnostics", "concurrency", "containers",
        "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
