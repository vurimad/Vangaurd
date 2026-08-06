group "Engine/Tools"

project "gameInputTools"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp", "README.md" }
    includedirs {
        "include", "../gameInput/include", "../assets/include", "../resources/include", "../serialization/include",
        "../filesystem/include", "../io/include", "../crypto/include", "../containers/include", "../memory/include",
        "../input/include", "../window/include", "../system/include"
    }
    links { "gameInput", "assets", "resources", "serialization", "filesystem", "io", "crypto", "containers", "memory", "input", "window", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }

group "Tests/Gameplay"

project "gameInputToolsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include", "../gameInput/include", "../assets/include", "../resources/include", "../serialization/include",
        "../filesystem/include", "../io/include", "../crypto/include", "../containers/include", "../memory/include",
        "../diagnostics/include", "../input/include", "../window/include", "../system/include"
    }
    links {
        "gameInputTools", "gameInput", "assets", "resources", "crypto", "serialization", "serializationCompat",
        "filesystem", "filesystemCompat", "redFileSystemCompat", "io", "ioCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "concurrency", "concurrencyCompat", "jobs", "jobsCompat", "redJobsCompat", "window",
        "redCompressionCompat", "redCompressionThirdPartyCompat", "redCoreCompat", "redIOCompat", "redContainersCompat",
        "redMathCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
