local flecsRoot = path.getabsolute("../../../vendors/flecs")

group "Engine/Runtime"

project "engine"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../prefabs/include", "../crypto/include", "../math/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include",
        "../gameInput/include", path.join(flecsRoot, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links { "application", "gameInput", "input", "window", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "jobs", "memory", "system" }
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
    disablewarnings { "4127" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/engine_services_tests.cpp" }
    includedirs {
        "include", "../application/include", "../filesystem/include", "../io/include", "../jobs/include", "../resources/include",
        "../streaming/include", "../packages/include", "../schemas/include", "../reflection/include", "../serialization/include",
        "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../concurrency/include",
        "../world/include", "../meshes/include", "../prefabs/include", "../crypto/include", "../math/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include", "../gameInput/include",
        path.join(flecsRoot, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "gameInput", "input", "window", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization",
        "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat",
        "redIOCompat", "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "packagesCompat", "reflectionCompat", "redReflectionCompat",
        "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/engine_services_tests.cpp" } }

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
        "../world/include", "../meshes/include", "../prefabs/include", "../crypto/include", "../math/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../input/include", "../window/include", path.join(flecsRoot, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "application", "input", "window", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization",
        "filesystem", "io", "jobs", "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat",
        "redIOCompat", "redFileSystemCompat", "filesystemCompat", "serializationCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "packagesCompat", "reflectionCompat", "redReflectionCompat",
        "redLexerCompat", "redNetworkCompat", "redConfigCompat", "commChannelCompat", "commProtocolCompat",
        "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/frame_pipeline_cycle_tests.cpp" } }

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
        "../world/include", "../meshes/include", "../prefabs/include", "../crypto/include", "../math/include",
        "../entities/include", "../gameWorld/include", "../ecs/include", "../gameInput/include", path.join(flecsRoot, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "engine", "gameInput", "input", "window", "application", "entities", "gameWorld", "ecs", "flecs", "world", "streaming",
        "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "jobs",
        "jobsCompat", "redJobsCompat", "redCoreCompat", "ioCompat", "redIOCompat", "redFileSystemCompat",
        "filesystemCompat", "serializationCompat", "redCompressionCompat", "redCompressionThirdPartyCompat",
        "packagesCompat", "reflectionCompat", "redReflectionCompat", "redLexerCompat", "redNetworkCompat",
        "redConfigCompat", "commChannelCompat", "commProtocolCompat", "concurrency", "concurrencyCompat",
        "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat",
        "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/input_service_tests.cpp" } }
