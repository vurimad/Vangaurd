group "Engine/Runtime"

project "entities"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324", "4201", "4458" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md", "UPSTREAM.md" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include",
        "../resources/include", "../reflection/include", "../schemas/include", "../prefabs/include",
        "../world/include", "../concurrency/include", "../ecs/include", "../gameWorld/include",
        "../streaming/include", "../packages/include", "../jobs/include",
        "../meshes/include", "../rendering/include", "../imported/common/redMath/include", "../math/adapted/include",
        path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links { "rendering", "gameWorld", "ecs", "flecs", "world", "prefabs", "schemas", "reflection", "serialization", "resources", "math",
            "filesystem", "jobs", "containers", "concurrency", "diagnostics", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md", "UPSTREAM.md" }
    }

group "Tests/Entities"

project "entitiesTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324", "4201", "4458" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include",
        "../resources/include", "../reflection/include", "../schemas/include", "../prefabs/include",
        "../world/include", "../concurrency/include", "../ecs/include", "../gameWorld/include",
        "../streaming/include", "../packages/include", "../jobs/include",
        "../meshes/include", "../rendering/include", "../imported/common/redMath/include", "../math/adapted/include", "../rhi/include", "../pipelines/include", "../shaders/include",
        path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "entities", "rendering", "gameWorld", "ecs", "flecs", "world", "prefabs", "schemas", "reflection", "serialization",
        "resources", "filesystem", "jobs", "containers", "concurrency", "diagnostics", "memory", "math", "system"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
