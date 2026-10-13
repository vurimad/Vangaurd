group "Applications/Runtime"

project "runtime"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "Vanguard"
    targetdir(runtime_output_root)
    objdir(object_root)
    debugdir(runtime_output_root)
    deployVanguardRuntimeDependencies(true)
    enableVanguardD3D12Agility()
    files { "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs {
        "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/jobs/include",
        "../source/shaders/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/streaming/include",
        "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/meshes/include", "../source/prefabs/include",
        "../source/crypto/include", "../source/math/include", "../source/io/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include", "../imported/common/redMath/include",
        path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    vpaths {
        ["Application/Private API/*"] = { "private/**.hpp" },
        ["Application/Source/*"] = { "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

project "runtimeViewportTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/runtime_viewport_tests.cpp",
        "src/runtime_viewport_service.cpp",
        "private/vanguard/runtime/runtime_viewport_service.hpp"
    }
    includedirs {
        "private", "../source/application/include", "../source/engine/include", "../source/rendering/include", "../source/rhi/include",
        "../source/window/include", "../source/pipelines/include", "../source/shaders/include", "../source/resources/include",
        "../source/packages/include", "../source/schemas/include", "../source/reflection/include", "../source/serialization/include",
        "../source/filesystem/include", "../source/io/include", "../source/jobs/include", "../source/concurrency/include",
        "../source/containers/include", "../source/memory/include", "../source/diagnostics/include", "../source/system/include",
        "../source/world/include", "../source/meshes/include", "../source/textures/include", "../source/streaming/include", "../source/prefabs/include", "../source/materials/include",
        "../source/pipelineCache/include", "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/input/include", "../source/gameInput/include", "../source/crypto/include", "../source/math/include",
        "../imported/common/redMath/include", "../source/math/adapted/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "rendering", "pipelineCache", "rhi", "entities", "gameWorld",
        "ecs", "flecs", "world", "meshes", "textures", "materials", "pipelines", "shaders", "streaming", "resources", "packages",
        "schemas", "reflection", "serialization", "crypto", "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat",
        "ioCompat", "redIOCompat", "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "packagesCompat", "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat",
        "redConfigCompat", "commChannelCompat", "commProtocolCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/runtime_viewport_tests.cpp" },
        ["Production/*"] = { "src/runtime_viewport_service.cpp", "private/vanguard/runtime/runtime_viewport_service.hpp" }
    }

group "Tests/Runtime"

project "runtimeWorldFrameIntegrationTest"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "VanguardRuntimeWorldFrameIntegrationTest"
    targetdir(path.getabsolute("bin/RuntimeTests/%{cfg.buildcfg}"))
    objdir(object_root)
    debugdir(path.getabsolute("bin/RuntimeTests/%{cfg.buildcfg}"))
    deployVanguardRuntimeDependencies(true)
    enableVanguardD3D12Agility()
    files {
        "private/**.hpp", "src/**.cpp", "platform/windows/**.cpp", "README.md",
        "tests/support/runtime_world_frame_integration_test_application.hpp",
        "tests/support/runtime_world_frame_integration_test_application.cpp",
        "tests/support/runtime_world_frame_integration_test_main_windows.cpp"
    }
    removefiles { "platform/windows/main_windows.cpp" }
    includedirs {
        "private", "tests/support", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/jobs/include",
        "../source/shaders/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/streaming/include",
        "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/meshes/include", "../source/prefabs/include",
        "../source/crypto/include", "../source/math/include", "../source/io/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include", "../imported/common/redMath/include",
        path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    vpaths {
        ["Production/Application/Private API/*"] = { "private/**.hpp" },
        ["Production/Application/Source/*"] = { "src/**.cpp" },
        ["Production/Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Tests/World Frame Integration Support/*"] = { "tests/support/runtime_world_frame_integration_test_*.*" },
        ["Documentation"] = { "README.md" }
    }
