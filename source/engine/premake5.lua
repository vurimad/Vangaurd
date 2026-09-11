group "Engine/Runtime"

project "engine"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include",
        "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include",
        "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include",
        "../rendering/include", "../rhi/include",
        "../gameInput/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links { "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "jobs", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Engine"

project "engineServicesTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/engine_services_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include",
        "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include",
        "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include",
        "../rendering/include", "../rhi/include", "../gameInput/include",
        path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection", "serialization",
        "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat",
        "redIOCompat", "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "packagesCompat", "reflectionCompat", "redReflectionCompat",
        "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/engine_services_tests.cpp" } }

project "resourceStreamingServiceTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/resource_streaming_service_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include",
        "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include", "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include",
        "../rendering/include", "../rhi/include", "../gameInput/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection", "serialization",
        "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat", "redIOCompat", "redFileSystemCompat",
        "filesystemCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat", "packagesCompat",
        "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat",
        "commProtocolCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/resource_streaming_service_tests.cpp" } }

project "framePipelineCycleTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/frame_pipeline_cycle_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include", "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include",
        "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include",
        "../rendering/include", "../rhi/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "input", "window", "rendering", "pipelineCache", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection", "serialization",
        "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat",
        "redIOCompat", "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "packagesCompat", "reflectionCompat", "redReflectionCompat",
        "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/frame_pipeline_cycle_tests.cpp" } }

project "textureResidencyServiceTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/texture_residency_service_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../assets/include", "../textureTools/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include", "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include",
        "../imported/common/redMath/include", "../math/adapted/include", "../entities/include", "../gameWorld/include", "../ecs/include",
        "../input/include", "../window/include", "../rendering/include", "../rhi/include", "../rhi/nvrhi/include", "../rhi/nvrhi/private",
        "../gameInput/include", path.join(flecs_root, "distr"), path.join(directx_headers_root, "include"),
        path.join(nvrhi_root, "include"), path.join(nvrhi_root, "src")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhiNvrhi", "rhi", "nvrhiD3D12", "nvrhiCore", "directXGuids",
        "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textureTools", "assets", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection",
        "bcCodecs", "compressonatorCore", "libtiff", "openexrCore", "openjph", "libjpegTurbo", "libpng", "zlib",
        "serialization", "crypto", "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat", "redIOCompat",
        "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat", "packagesCompat",
        "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/texture_residency_service_tests.cpp" } }

project "materialRuntimeServiceTests"
    includedirs { "../shaderTools/include", "../materialTools/include", "../materialTools/tests" }
    files { "../materialTools/tests/material_static_surface_tests.cpp", "tests/static_surface_native_tests.cpp" }
    libdirs { path.join(slang_root, "lib") }
    links { "materialTools", "shaderTools", "slang-compiler" }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/material_runtime_service_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include", "../shaders/include",
        "../pipelines/include", "../materials/include", "../pipelineCache/include", "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include", "../rendering/include",
        "../rhi/include", "../rhi/nvrhi/include", "../rhi/nvrhi/private", "../gameInput/include", path.join(flecs_root, "distr"),
        path.join(directx_headers_root, "include"), path.join(nvrhi_root, "include"), path.join(nvrhi_root, "src")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhiNvrhi", "rhi", "nvrhiD3D12", "nvrhiCore", "directXGuids",
        "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages", "schemas", "reflection",
        "serialization", "crypto", "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat", "redIOCompat",
        "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat", "packagesCompat",
        "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "d3d12", "dxgi", "Advapi32", "D3DCompiler", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/material_runtime_service_tests.cpp" } }

project "inputServiceTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/input_service_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include",
        "../input/include", "../window/include", "../system/include", "../memory/include", "../diagnostics/include",
        "../containers/include", "../concurrency/include", "../resources/include", "../streaming/include",
        "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../world/include", "../meshes/include", "../textures/include", "../prefabs/include", "../crypto/include", "../shaders/include", "../pipelines/include", "../materials/include", "../pipelineCache/include",
        "../imported/common/redMath/include", "../math/adapted/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../rendering/include", "../rhi/include",
        "../gameInput/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "gameInput", "input", "window", "rendering", "pipelineCache", "rhi", "application", "entities", "gameWorld", "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming",
        "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "jobs",
        "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat", "redIOCompat", "redFileSystemCompat",
        "filesystemCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat",
        "packagesCompat", "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat",
        "redConfigCompat", "commChannelCompat", "commProtocolCompat", "concurrency", "concurrencyCompat",
        "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat",
        "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/input_service_tests.cpp" } }
