group "ThirdParty/Rendering"

project "directXGuids"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(directx_headers_root, "src/dxguids.cpp"),
        path.join(directx_headers_root, "LICENSE"),
        path.join(external_root, "directXHeaders/UPSTREAM.md")
    }
    includedirs { path.join(directx_headers_root, "include") }
    vpaths {
        ["Source"] = { path.join(directx_headers_root, "src/dxguids.cpp") },
        ["Documentation"] = { path.join(directx_headers_root, "LICENSE"), path.join(external_root, "directXHeaders/UPSTREAM.md") }
    }

project "nvrhiCore"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(nvrhi_root, "include/nvrhi/**.h"),
        path.join(nvrhi_root, "src/common/format-info.cpp"),
        path.join(nvrhi_root, "src/common/misc.cpp"),
        path.join(nvrhi_root, "src/common/state-tracking.cpp"),
        path.join(nvrhi_root, "src/common/state-tracking.h"),
        path.join(nvrhi_root, "src/common/utils.cpp"),
        path.join(nvrhi_root, "src/common/aftermath.cpp"),
        path.join(nvrhi_root, "tools/nvrhi.natvis"),
        "LICENSE.txt", "README.md", "UPSTREAM.md", path.join(nvrhi_root, "LICENSE.txt")
    }
    includedirs { path.join(nvrhi_root, "include") }
    defines { "NVRHI_WITH_AFTERMATH=0" }
    vpaths {
        ["NVRHI/Headers/*"] = { path.join(nvrhi_root, "include/nvrhi/**.h") },
        ["NVRHI/Common/*"] = { path.join(nvrhi_root, "src/common/**") },
        ["Documentation"] = { "LICENSE.txt", "README.md", "UPSTREAM.md", path.join(nvrhi_root, "LICENSE.txt") }
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
        path.join(nvrhi_root, "include/nvrhi/d3d12.h"),
        path.join(nvrhi_root, "src/common/dxgi-format.h"),
        path.join(nvrhi_root, "src/common/dxgi-format.cpp"),
        path.join(nvrhi_root, "src/common/versioning.h"),
        path.join(nvrhi_root, "src/d3d12/**.h"),
        path.join(nvrhi_root, "src/d3d12/**.cpp")
    }
    includedirs { path.join(directx_headers_root, "include"), path.join(nvrhi_root, "include") }
    links { "nvrhiCore", "directXGuids" }
    defines {
        "NVRHI_D3D12_WITH_DXR12_OPACITY_MICROMAP=0",
        "NVRHI_D3D12_WITH_NVAPI=0",
        "NVRHI_WITH_AFTERMATH=0"
    }
    vpaths {
        ["NVRHI/D3D12/*"] = { path.join(nvrhi_root, "src/d3d12/**") },
        ["NVRHI/Common/*"] = { path.join(nvrhi_root, "src/common/dxgi-format.*"), path.join(nvrhi_root, "src/common/versioning.h") }
    }
