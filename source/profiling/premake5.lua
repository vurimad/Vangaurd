local profilingIncludeRoot = path.getabsolute("include")
local tracyPublicRoot = path.join(tracy_root, "public")

function useVanguardProfiling()
    includedirs { profilingIncludeRoot }

    filter "configurations:Profile"
        links { "profiling" }

    filter {}
end

group "Engine/Runtime"

project "profiling"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files { "include/**.hpp", "src/**.cpp", "README.md" }
    includedirs { "include" }
    links { "tracyClient" }

    filter "configurations:Profile"
        includedirs { tracyPublicRoot }

    filter {}

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.cpp" },
        ["Documentation"] = { "README.md" }
    }
