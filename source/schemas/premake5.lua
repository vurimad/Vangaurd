local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")
local redMathRoot = path.join(importedRoot, "redMath")
local redContainersRoot = path.join(importedRoot, "redContainers")
local redIORoot = path.join(importedRoot, "redIO")
local redCoreRoot = path.join(importedRoot, "redCore")
local redCompressionRoot = path.join(importedRoot, "redCompression")
local redFileSystemRoot = path.join(importedRoot, "redFileSystem")
local redJobsRoot = path.join(importedRoot, "redJobs2")
local redJobsLegacyRoot = path.join(importedRoot, "redJobs")
local redNetworkRoot = path.join(importedRoot, "redNetwork")
local redConfigRoot = path.join(importedRoot, "redConfig")
local commProtocolRoot = path.join(importedRoot, "commProtocol")
local commChannelRoot = path.join(importedRoot, "commChannel")
local redReflectionRoot = path.join(importedRoot, "redReflection")
local redLexerRoot = path.getabsolute("../imported/internal/RedLexer")
local generatedRoot = path.getabsolute("../imported/generated")
local fileSyncRoot = path.getabsolute("../imported/internal/FileSync")
local udtRoot = path.getabsolute("../imported/external/udt")
local oodleRoot = path.getabsolute("../imported/external/oodle")
local nvToolsRoot = path.getabsolute("../imported/external/nvToolsExt")

local function importedIncludes()
    includedirs {
        redReflectionRoot,
        path.join(redReflectionRoot, "include"),
        path.join(redReflectionRoot, "src"),
        path.join(redConfigRoot, "include"),
        path.join(redConfigRoot, "src"),
        path.join(commProtocolRoot, "include"),
        path.join(commProtocolRoot, "gen"),
        path.join(commChannelRoot, "include"),
        path.join(commChannelRoot, "src"),
        path.join(redNetworkRoot, "include"),
        path.join(redNetworkRoot, "src"),
        path.join(redJobsRoot, "include"),
        path.join(redJobsRoot, "src"),
        path.join(redJobsLegacyRoot, "include"),
        path.join(redFileSystemRoot, "include"),
        path.join(redFileSystemRoot, "src"),
        path.join(redCompressionRoot, "include"),
        path.join(redCompressionRoot, "src"),
        path.join(redCoreRoot, "include"),
        path.join(redIORoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redMathRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redLexerRoot, "include"),
        path.join(redLexerRoot, "gen"),
        path.join(generatedRoot, "redReflection"),
        path.join(generatedRoot, "rtti/redReflection")
    }
end

group "Engine/Runtime"

project "schemas"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.hpp", "src/**.cpp" }
    includedirs {
        "include",
        "src",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../containers/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../resources/include",
        "../reflection/include"
    }
    links {
        "reflection",
        "resources",
        "serialization",
        "filesystem",
        "containers",
        "concurrency",
        "memory",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" }
    }

group "Tests/Schemas"

project "schemasTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs {
        "include",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../concurrency/include",
        "../containers/include",
        "../io/include",
        "../filesystem/include",
        "../serialization/include",
        "../resources/include",
        "../reflection/include"
    }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    linkoptions { "/WHOLEARCHIVE:redReflectionCompat.lib" }
    links {
        "schemas",
        "reflection",
        "reflectionCompat",
        "redReflectionCompat",
        "resources",
        "serialization",
        "serializationCompat",
        "filesystem",
        "filesystemCompat",
        "redConfigCompat",
        "commChannelCompat",
        "commProtocolCompat",
        "redLexerCompat",
        "redJobsCompat",
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCompressionThirdPartyCompat",
        "redCoreCompat",
        "redNetworkCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "io",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
        "concurrency",
        "concurrencyCompat",
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
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs {
            path.join(fileSyncRoot, "lib/release_x64"),
            path.join(udtRoot, "lib/release_x64")
        }
        links { "FileSync", "udt" }
    filter {}
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
