local nvrhiRoot = path.getabsolute("../../../vendors/NVRHI")
local directXHeadersRoot = path.getabsolute("../../../vendors/DirectX-Headers")

group "ThirdParty/Rendering"

project "directXGuids"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files { path.join(directXHeadersRoot, "src/dxguids.cpp") }
    includedirs { path.join(directXHeadersRoot, "include") }

project "nvrhiCore"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(nvrhiRoot, "include/nvrhi/**.h"),
        path.join(nvrhiRoot, "src/common/format-info.cpp"),
        path.join(nvrhiRoot, "src/common/misc.cpp"),
        path.join(nvrhiRoot, "src/common/state-tracking.cpp"),
        path.join(nvrhiRoot, "src/common/state-tracking.h"),
        path.join(nvrhiRoot, "src/common/utils.cpp"),
        path.join(nvrhiRoot, "src/common/aftermath.cpp"),
        path.join(nvrhiRoot, "tools/nvrhi.natvis"),
        "LICENSE.txt", "README.md", "UPSTREAM.md"
    }
    includedirs { path.join(nvrhiRoot, "include") }
    defines { "NVRHI_WITH_AFTERMATH=0" }
    vpaths {
        ["NVRHI/Headers/*"] = { path.join(nvrhiRoot, "include/nvrhi/**.h") },
        ["NVRHI/Common/*"] = { path.join(nvrhiRoot, "src/common/**") },
        ["Documentation"] = { "LICENSE.txt", "README.md", "UPSTREAM.md" }
    }

project "nvrhiD3D12"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(nvrhiRoot, "include/nvrhi/d3d12.h"),
        path.join(nvrhiRoot, "src/common/dxgi-format.h"),
        path.join(nvrhiRoot, "src/common/dxgi-format.cpp"),
        path.join(nvrhiRoot, "src/common/versioning.h"),
        path.join(nvrhiRoot, "src/d3d12/**.h"),
        path.join(nvrhiRoot, "src/d3d12/**.cpp")
    }
    includedirs { path.join(directXHeadersRoot, "include"), path.join(nvrhiRoot, "include") }
    links { "nvrhiCore", "directXGuids" }
    defines {
        "NVRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP=0",
        "NVRHI_D3D12_WITH_NVAPI=0",
        "NVRHI_WITH_AFTERMATH=0"
    }
    vpaths {
        ["NVRHI/D3D12/*"] = { path.join(nvrhiRoot, "src/d3d12/**") },
        ["NVRHI/Common/*"] = { path.join(nvrhiRoot, "src/common/dxgi-format.*"), path.join(nvrhiRoot, "src/common/versioning.h") }
    }
