local importedRoot = "../../imported/common"
local nvToolsRoot = path.getabsolute("../../imported/external/nvToolsExt")
local oodleRoot = path.getabsolute("../../imported/external/oodle")
local fileSyncRoot = path.getabsolute("../../imported/internal/FileSync")
local udtRoot = path.getabsolute("../../imported/external/udt")
local renderingShaderRoot = path.getabsolute("../../source/rendering/shaders")

local function configureProjectAssetClosure()
    disablewarnings { "4324" }
    defines { "PNG_STATIC", "Z_PREFIX" }
    includedirs {
        "../../source/assetStreaming/include",
        "../../source/streaming/include",
        "../../source/meshTools/include",
        "../../source/modelTools/include",
        "../../source/meshes/include",
        "../../source/entities/include",
        "../../source/prefabs/include",
        "../../source/materialTools/include",
        "../../source/materials/include",
        "../../source/pipelines/include",
        "../../source/shaderTools/include",
        "../../source/shaders/include",
        "../../source/textureTools/include",
        "../../source/textures/include",
        "../../source/assets/include",
        "../../source/packages/include",
        "../../source/schemas/include",
        "../../source/reflection/include",
        "../../source/resources/include",
        "../../source/crypto/include",
        "../../source/serialization/include",
        "../../source/filesystem/include",
        "../../source/io/include",
        "../../source/jobs/include",
        "../../source/diagnostics/include",
        "../../source/concurrency/include",
        "../../source/containers/include",
        "../../source/memory/include",
        "../../source/system/include",
        "../../source/projects/include",
        "../../external/bcCodecs/src",
        "../../external/compressonatorCore/cmp_core/source",
        "../../external/imageCodecs/libpng",
        "../../external/imageCodecs/zlib",
        "../../external/imageCodecs/libjpeg-turbo",
        "../../external/imageCodecs/libtiff",
        "../../external/imageCodecs/openexr",
        "../../external/imageCodecs/openexr/Imath",
        "../../external/imageCodecs/openexr/OpenEXRCore",
        path.join(importedRoot, "redFileSystem/include"),
        path.join(importedRoot, "redFileSystem/src"),
        path.join(importedRoot, "redCompression/include"),
        path.join(importedRoot, "redCompression/src"),
        path.join(importedRoot, "redSystem/include"),
        path.join(importedRoot, "redMemory/include"),
        path.join(importedRoot, "redMemory/src"),
        path.join(importedRoot, "redMath/include"),
        path.join(importedRoot, "redContainers/include"),
        path.join(importedRoot, "redIO/include"),
        path.join(importedRoot, "redCore/include")
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release"), path.join(slang_root, "lib") }
    links {
        "assetStreaming",
        "streaming",
        "meshTools",
        "meshes",
        "prefabs",
        "meshoptimizer",
        "assimp",
        "materialTools",
        "shaderTools",
        "shaders",
        "pipelines",
        "materials",
        "textureTools",
        "assets",
        "textures",
        "packages",
        "schemas",
        "reflection",
        "resources",
        "crypto",
        "serialization",
        "filesystem",
        "io",
        "jobs",
        "diagnostics",
        "concurrency",
        "containers",
        "memory",
        "system",
        "projects",
        "packagesCompat",
        "serializationCompat",
        "filesystemCompat",
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCompressionThirdPartyCompat",
        "redCoreCompat",
        "redIOCompat",
        "diagnosticsCompat",
        "containersCompat",
        "concurrencyCompat",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "redSystemCompat",
        "bcCodecs",
        "compressonatorCore",
        "libtiff",
        "openexrCore",
        "openjph",
        "libjpegTurbo",
        "libpng",
        "zlib",
        "nvToolsExt64_1",
        "Bcrypt",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Shell32",
        "Version",
        "ws2_32",
        "slang-compiler"
    }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"',
        '{MKDIR} "%{cfg.targetdir}/shaders"',
        '{COPYFILE} "' .. path.join(renderingShaderRoot, "static_surface_prefix.vsl") .. '" "%{cfg.targetdir}/shaders/static_surface_prefix.vsl"',
        '{COPYFILE} "' .. path.join(renderingShaderRoot, "static_surface_suffix.vsl") .. '" "%{cfg.targetdir}/shaders/static_surface_suffix.vsl"',
        '{COPYFILE} "' .. path.join(renderingShaderRoot, "gpu_scene_types.hlsli") .. '" "%{cfg.targetdir}/shaders/gpu_scene_types.hlsli"'
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs { path.join(fileSyncRoot, "lib/release_x64"), path.join(udtRoot, "lib/release_x64") }
        links { "FileSync", "udt" }
    filter {}
end

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
    deployVanguardRuntimeDependencies(false)
    includedirs { "../../source/assets/include", "../../source/resources/include", "../../source/crypto/include", "../../source/serialization/include", "../../source/concurrency/include", "../../source/jobs/include" }
    links { "assets", "resources", "crypto", "serialization", "serializationCompat", "diagnostics", "diagnosticsCompat", "concurrency", "concurrencyCompat", "jobs", "jobsCompat", "redJobsCompat" }
    files { "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs { "private", "../../source/projects/include", "../../source/filesystem/include", "../../source/io/include",
                  "../../source/containers/include", "../../source/memory/include", "../../source/system/include" }
    links { "projects", "filesystem", "io", "containers", "memory", "redSystemCompat", "Bcrypt", "Shell32", "Advapi32",
            "Dbghelp", "Psapi", "Shlwapi", "Version", "system" }
    configureProjectAssetClosure()
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
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "private/**.hpp", "src/**.cpp", "platform/windows/project_platform_windows.cpp", "tests/**.cpp" }
    removefiles { "src/project_asset_context.cpp", "src/project_asset_commands.cpp" }
    includedirs {
        "private",
        "../../source/assets/include",
        "../../source/resources/include",
        "../../source/crypto/include",
        "../../source/serialization/include",
        "../../source/concurrency/include",
        "../../source/jobs/include",
        "../../source/diagnostics/include",
        "../../source/projects/include",
        "../../source/filesystem/include",
        "../../source/io/include",
                  "../../source/containers/include", "../../source/memory/include", "../../source/system/include" }
    links {
        "assets", "resources", "crypto", "serialization", "serializationCompat",
        "diagnostics", "diagnosticsCompat", "concurrency", "concurrencyCompat",
        "jobs", "jobsCompat", "redJobsCompat",
        "projects", "filesystem", "io", "containers", "memory", "Bcrypt", "system"
    }
    vpaths {
        ["CLI/Private API/*"] = { "private/**.hpp" }, ["CLI/Source/*"] = { "src/**.cpp" },
        ["Tests/*"] = { "tests/**.cpp" }
    }

project "nanovanguardProjectAssetTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files {
        "private/vanguard/nanovanguard/command_line.hpp",
        "private/vanguard/nanovanguard/nanovanguard.hpp",
        "private/vanguard/nanovanguard/project_commands.hpp",
        "private/vanguard/nanovanguard/project_platform.hpp",
        "private/vanguard/nanovanguard/command_cancellation.hpp",
        "private/vanguard/nanovanguard/project_asset_commands.hpp",
        "private/vanguard/nanovanguard/project_asset_context.hpp",
        "src/command_line.cpp",
        "src/nanovanguard.cpp",
        "src/project_commands.cpp",
        "src/project_asset_commands.cpp",
        "src/project_asset_context.cpp",
        "platform/windows/project_platform_windows.cpp",
        "platform/windows/command_cancellation_windows.cpp",
        "integration/project_asset_context_tests.cpp"
    }
    includedirs { "private" }
    configureProjectAssetClosure()
    vpaths {
        ["CLI/Private API/*"] = { "private/vanguard/nanovanguard/*.hpp" },
        ["CLI/Source/*"] = { "src/command_line.cpp", "src/nanovanguard.cpp", "src/project_commands.cpp", "src/project_asset_commands.cpp", "src/project_asset_context.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/project_platform_windows.cpp", "platform/windows/command_cancellation_windows.cpp" },
        ["Tests/*"] = { "integration/project_asset_context_tests.cpp" }
    }
