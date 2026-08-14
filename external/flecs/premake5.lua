group "External"

project "flecs"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(flecs_root, "distr/flecs.c"),
        path.join(flecs_root, "distr/flecs.h"),
        path.join(flecs_root, "LICENSE"),
        "UPSTREAM.md"
    }
    includedirs { path.join(flecs_root, "distr") }
    defines {
        "FLECS_CUSTOM_BUILD",
        "FLECS_CPP",
        "FLECS_MODULE",
        "FLECS_SYSTEM",
        "FLECS_PIPELINE",
        "FLECS_TIMER"
    }
    vpaths {
        ["Flecs/*"] = { path.join(flecs_root, "distr/**") },
        ["Documentation"] = { path.join(flecs_root, "LICENSE"), "UPSTREAM.md" }
    }
