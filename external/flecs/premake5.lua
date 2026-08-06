local flecsRoot = path.getabsolute("../../../vendors/flecs")

group "External"

project "flecs"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(flecsRoot, "distr/flecs.c"),
        path.join(flecsRoot, "distr/flecs.h")
    }
    includedirs { path.join(flecsRoot, "distr") }
    defines {
        "FLECS_CUSTOM_BUILD",
        "FLECS_CPP",
        "FLECS_MODULE",
        "FLECS_SYSTEM",
        "FLECS_PIPELINE",
        "FLECS_TIMER"
    }
    vpaths { ["Flecs/*"] = { path.join(flecsRoot, "distr/**") } }
