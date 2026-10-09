group "ThirdParty/Diagnostics"

project "tracyClient"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    warnings "Off"
    targetdir(output_root)
    objdir(object_root)

    files {
        path.join(tracy_root, "public/TracyClient.cpp"),
        path.join(tracy_root, "public/**.h"),
        path.join(tracy_root, "public/**.hpp"),
        path.join(tracy_root, "LICENSE"),
        "UPSTREAM.md"
    }

    includedirs { path.join(tracy_root, "public") }

    filter "configurations:Profile"
        defines { "TRACY_ENABLE" }

    filter {}

    vpaths {
        ["Tracy/Public/*"] = { path.join(tracy_root, "public/**") },
        ["Documentation"] = { path.join(tracy_root, "LICENSE"), "UPSTREAM.md" }
    }
