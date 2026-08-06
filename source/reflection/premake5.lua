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
local nvToolsRoot = path.getabsolute("../imported/external/nvToolsExt")

local function applyRedCompatibilitySettings()
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"
    targetdir(output_root)
    objdir(object_root)
    defines {
        "RED_COMPILER_MSC",
        "RED_VANGUARD_DISABLE_VTUNE",
        "_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING",
        "_SILENCE_CXX20_IS_POD_DEPRECATION_WARNING"
    }
    filter "configurations:Debug"
        defines { "RED_CONFIGURATION_DEBUG" }
    filter "configurations:Development"
        defines { "RED_CONFIGURATION_RELEASE" }
    filter "configurations:Profile"
        defines { "RED_CONFIGURATION_RELEASE" }
    filter "configurations:Shipping"
        defines { "RED_CONFIGURATION_FINAL" }
    filter {}
end

local function addAllRedIncludes()
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

local function addModuleFiles(root)
    files {
        path.join(root, "include/**.h"),
        path.join(root, "include/**.hpp"),
        path.join(root, "include/**.inl"),
        path.join(root, "src/**.h"),
        path.join(root, "src/**.hpp"),
        path.join(root, "src/**.inl"),
        path.join(root, "src/**.c"),
        path.join(root, "src/**.cc"),
        path.join(root, "src/**.cpp")
    }
end

group "Engine/Compatibility"

project "redNetworkCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redNetwork", "RED_EXPORT_redNetwork" }
    pchheader "build.h"
    pchsource(path.join(redNetworkRoot, "src/build.cpp"))
    includedirs {
        path.join(redNetworkRoot, "include"),
        path.join(redNetworkRoot, "src")
    }
    addModuleFiles(redNetworkRoot)
    excludes {
        path.join(redNetworkRoot, "src/platformLinux.cpp")
    }
    links {
        "redContainersCompat",
        "memory",
        "redSystemCompat"
    }

project "redConfigCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redConfig", "RED_EXPORT_redConfig" }
    pchheader "build.h"
    pchsource(path.join(redConfigRoot, "src/build.cpp"))
    includedirs {
        redConfigRoot,
        path.join(redConfigRoot, "include"),
        path.join(redConfigRoot, "src")
    }
    forceincludes { "cassert" }
    addModuleFiles(redConfigRoot)
    links {
        "redFileSystemCompat",
        "redCoreCompat",
        "redCompressionCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }

project "commProtocolCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_commProtocol", "RED_EXPORT_commProtocol" }
    pchheader "build.h"
    pchsource(path.join(commProtocolRoot, "src/build.cpp"))
    includedirs {
        commProtocolRoot,
        path.join(commProtocolRoot, "include"),
        path.join(commProtocolRoot, "src"),
        path.join(commProtocolRoot, "gen")
    }
    addModuleFiles(commProtocolRoot)
    files { path.join(commProtocolRoot, "gen/protocol.h") }
    links {
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }

project "commChannelCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_commChannel", "RED_EXPORT_commChannel" }
    pchheader "build.h"
    pchsource(path.join(commChannelRoot, "src/build.cpp"))
    includedirs {
        commChannelRoot,
        path.join(commChannelRoot, "include"),
        path.join(commChannelRoot, "src"),
        path.join(commProtocolRoot, "gen")
    }
    addModuleFiles(commChannelRoot)
    links {
        "commProtocolCompat",
        "redNetworkCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }

project "redLexerCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "_WINDOWS" }
    includedirs {
        path.join(redLexerRoot, "include"),
        path.join(redLexerRoot, "src"),
        path.join(redLexerRoot, "gen")
    }
    files {
        path.join(redLexerRoot, "include/**.h"),
        path.join(redLexerRoot, "src/build.h"),
        path.join(redLexerRoot, "src/flexSupplimentary.h"),
        path.join(redLexerRoot, "src/lexer.cpp"),
        path.join(redLexerRoot, "src/state.cpp"),
        path.join(redLexerRoot, "gen/bison_tokens.h")
    }

project "redReflectionCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redReflection", "RED_EXPORT_redReflection" }
    pchheader "build.h"
    pchsource(path.join(redReflectionRoot, "src/build.cpp"))
    includedirs {
        redReflectionRoot,
        path.join(redReflectionRoot, "include"),
        path.join(redReflectionRoot, "src"),
        path.join(generatedRoot, "redReflection"),
        path.join(generatedRoot, "rtti/redReflection")
    }
    addAllRedIncludes()
    addModuleFiles(redReflectionRoot)
    files {
        path.join(generatedRoot, "redReflection/**.h"),
        path.join(generatedRoot, "rtti/redReflection/**.h")
    }
    links {
        "redJobsCompat",
        "redConfigCompat",
        "commChannelCompat",
        "redLexerCompat",
        "redFileSystemCompat",
        "redCoreCompat",
        "redCompressionCompat",
        "redIOCompat",
        "redNetworkCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }
    filter "configurations:not Debug"
        buildoptions { "/d2SSAOptimizer-" }
    filter {}

project "reflectionCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    applyRedCompatibilitySettings()
    files { "compat/**.cpp" }
    includedirs {
        "include",
        "src",
        "../system/include",
        "../memory/include"
    }
    addAllRedIncludes()
    links {
        "redReflectionCompat",
        "redConfigCompat",
        "commProtocolCompat",
        "commChannelCompat",
        "redLexerCompat"
    }

group "Engine/Runtime"

project "reflection"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files {
        "include/**.hpp",
        "src/**.hpp",
        "src/**.cpp"
    }
    includedirs {
        "include",
        "src",
        "../system/include",
        "../memory/include",
        "../concurrency/include",
        "../containers/include"
    }
    links {
        "reflectionCompat",
        "containers",
        "concurrency",
        "memory",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" }
    }

group "Tests/Reflection"

project "reflectionTests"
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
        "../concurrency/include",
        "../containers/include"
    }
    libdirs {
        path.join(nvToolsRoot, "lib/x64.Release")
    }
    linkoptions {
        "/WHOLEARCHIVE:redReflectionCompat.lib"
    }
    links {
        "reflection",
        "reflectionCompat",
        "redReflectionCompat",
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
        "Version",
        "ws2_32"
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
