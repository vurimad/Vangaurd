group "Engine/Tools"

project "assetStreaming"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "private/**.hpp", "src/**.cpp" }
    includedirs {
        "include", "private", "../assets/include", "../streaming/include", "../resources/include", "../packages/include", "../schemas/include",
        "../reflection/include", "../serialization/include", "../filesystem/include", "../io/include", "../jobs/include", "../crypto/include",
        "../containers/include", "../concurrency/include", "../memory/include", "../system/include"
    }
    links { "concurrency", "streaming", "assets", "resources", "serialization", "filesystem", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Private API/*"] = { "private/**.hpp" }, ["Source/*"] = { "src/**.cpp" } }

-- E1F.V Phase 2: production-backed behavioral proof for the shared project
-- asset operations and session-local undo/redo. Mirrors the streamingTests
-- link/include surface because ResourceStreamer remains the runtime owner.
group "Tests/Assets"

project "assetStreamingTests"
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
        "include",
        "private",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../crypto/include",
        "../resources/include",
        "../packages/include",
        "../jobs/include",
        "../reflection/include",
        "../schemas/include",
        "../assets/include",
        "../streaming/include",
        "../imported/common/redFileSystem/include",
        "../imported/common/redFileSystem/src",
        "../imported/common/redCompression/include",
        "../imported/common/redCompression/src",
        "../imported/common/redSystem/include",
        "../imported/common/redMemory/include",
        "../imported/common/redMemory/src",
        "../imported/common/redMath/include",
        "../imported/common/redContainers/include",
        "../imported/common/redIO/include",
        "../imported/common/redCore/include"
    }
    libdirs { path.getabsolute("../imported/external/nvToolsExt/lib/x64.Release") }
    links {
        "assetStreaming",
        "streaming",
        "assets",
        "schemas",
        "reflection",
        "packages",
        "packagesCompat",
        "crypto",
        "resources",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCompressionThirdPartyCompat",
        "redCoreCompat",
        "redIOCompat",
        "io",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
        "jobs",
        "jobsCompat",
        "redJobsCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat",
        "system",
        "nvToolsExt64_1",
        "Advapi32",
        "Dbghelp",
        "Psapi",
        "Shlwapi",
        "Shell32",
        "Version",
        "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.getabsolute("../imported/external/oodle/lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.getabsolute("../imported/external/oodle/lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs {
            path.getabsolute("../imported/internal/FileSync/lib/release_x64"),
            path.getabsolute("../imported/external/udt/lib/release_x64")
        }
        links { "FileSync", "udt" }
    filter {}
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
