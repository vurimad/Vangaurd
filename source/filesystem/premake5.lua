local importedRoot = "../imported/common"
local redSystemRoot = path.join(importedRoot, "redSystem")
local redMemoryRoot = path.join(importedRoot, "redMemory")
local redMathRoot = path.join(importedRoot, "redMath")
local redContainersRoot = path.join(importedRoot, "redContainers")
local redIORoot = path.join(importedRoot, "redIO")
local redCoreRoot = path.join(importedRoot, "redCore")
local redCompressionRoot = path.join(importedRoot, "redCompression")
local redFileSystemRoot = path.join(importedRoot, "redFileSystem")
local fileSyncRoot = path.getabsolute("../imported/internal/FileSync")
local udtRoot = path.getabsolute("../imported/external/udt")
local oodleRoot = path.getabsolute("../imported/external/oodle")
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

local function importedIncludes()
    includedirs {
        path.join(redFileSystemRoot, "include"),
        path.join(redFileSystemRoot, "src"),
        path.join(redCompressionRoot, "include"),
        path.join(redCompressionRoot, "src"),
        path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"),
        path.join(redIORoot, "include"),
        path.join(redCoreRoot, "include"),
        path.join(oodleRoot, "include"),
        path.join(fileSyncRoot, "include")
    }
end

group "Engine/Compatibility"

project "redCompressionThirdPartyCompat"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    exceptionhandling "Off"
    warnings "Default"
    targetdir(output_root)
    objdir(object_root)
    files {
        path.join(redCompressionRoot, "src/doboz/**.h"),
        path.join(redCompressionRoot, "src/doboz/**.cpp"),
        path.join(redCompressionRoot, "src/lz4/**.h"),
        path.join(redCompressionRoot, "src/lz4/**.c"),
        path.join(redCompressionRoot, "src/snappy/**.h"),
        path.join(redCompressionRoot, "src/snappy/**.cc"),
        path.join(redCompressionRoot, "src/zlib/**.h"),
        path.join(redCompressionRoot, "src/zlib/**.c")
    }
    vpaths {
        ["Imported/RED Compression/Third Party/*"] = {
            path.join(redCompressionRoot, "src/doboz/**"),
            path.join(redCompressionRoot, "src/lz4/**"),
            path.join(redCompressionRoot, "src/snappy/**"),
            path.join(redCompressionRoot, "src/zlib/**")
        }
    }

project "redCompressionCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redCompression", "RED_EXPORT_redCompression" }
    pchheader "build.h"
    pchsource(path.join(redCompressionRoot, "src/build.cpp"))
    importedIncludes()
    files {
        path.join(redCompressionRoot, "include/**.h"),
        path.join(redCompressionRoot, "src/**.h"),
        path.join(redCompressionRoot, "src/**.cpp")
    }
    excludes {
        path.join(redCompressionRoot, "src/doboz/**"),
        path.join(redCompressionRoot, "src/lz4/**"),
        path.join(redCompressionRoot, "src/snappy/**"),
        path.join(redCompressionRoot, "src/zlib/**")
    }
    links {
        "redCompressionThirdPartyCompat",
        "memory",
        "redSystemCompat"
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(oodleRoot, "lib/x64.Debug/oo2ext_7_win64_debug.dll") .. '" "%{cfg.targetdir}/oo2ext_7_win64_debug.dll"'
        }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(oodleRoot, "lib/x64.Release/oo2ext_7_win64.dll") .. '" "%{cfg.targetdir}/oo2ext_7_win64.dll"'
        }
    filter {}
    vpaths {
        ["Imported/RED Compression/Public/*"] = { path.join(redCompressionRoot, "include/**.h") },
        ["Imported/RED Compression/Private/*"] = {
            path.join(redCompressionRoot, "src/**.h"),
            path.join(redCompressionRoot, "src/**.c"),
            path.join(redCompressionRoot, "src/**.cc"),
            path.join(redCompressionRoot, "src/**.cpp")
        }
    }

project "redFileSystemCompat"
    kind "StaticLib"
    applyRedCompatibilitySettings()
    defines { "RED_MODULE_redFileSystem", "RED_EXPORT_redFileSystem" }
    pchheader "build.h"
    pchsource(path.join(redFileSystemRoot, "src/build.cpp"))
    importedIncludes()
    files {
        path.join(redFileSystemRoot, "include/**.h"),
        path.join(redFileSystemRoot, "include/**.inl"),
        path.join(redFileSystemRoot, "src/**.h"),
        path.join(redFileSystemRoot, "src/**.cpp")
    }
    excludes {
        path.join(redFileSystemRoot, "src/filePathsDurango.cpp"),
        path.join(redFileSystemRoot, "src/filePathsLinux.cpp"),
        path.join(redFileSystemRoot, "src/filePathsOrbis.cpp"),
        path.join(redFileSystemRoot, "src/systemLinux.cpp"),
        path.join(redFileSystemRoot, "src/systemOrbis.cpp")
    }
    links {
        "redCompressionCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "memory",
        "redSystemCompat"
    }
    filter "configurations:not Shipping"
        libdirs {
            path.join(fileSyncRoot, "lib/release_x64"),
            path.join(udtRoot, "lib/release_x64")
        }
        links { "FileSync", "udt" }
        linkoptions { "/IGNORE:4006" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(fileSyncRoot, "lib/release_x64/FileSync.dll") .. '" "%{cfg.targetdir}/FileSync.dll"',
            '{COPYFILE} "' .. path.join(udtRoot, "lib/release_x64/udt.dll") .. '" "%{cfg.targetdir}/udt.dll"'
        }
    filter {}
    vpaths {
        ["Imported/RED FileSystem/Public/*"] = {
            path.join(redFileSystemRoot, "include/**.h"),
            path.join(redFileSystemRoot, "include/**.inl")
        },
        ["Imported/RED FileSystem/Private/*"] = {
            path.join(redFileSystemRoot, "src/**.h"),
            path.join(redFileSystemRoot, "src/**.cpp")
        }
    }

project "filesystemCompat"
    enforceEngineCodePolicy()
    kind "StaticLib"
    applyRedCompatibilitySettings()
    files { "compat/**.cpp" }
    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../containers/include",
        "../io/include"
    }
    importedIncludes()
    links {
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "memory",
        "redSystemCompat"
    }
    vpaths { ["Compatibility/*"] = { "compat/**.cpp" } }

group "Engine/Runtime"

project "filesystem"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "private/**.hpp", "src/**.cpp" }
    includedirs {
        "include",
        "private",
        "../system/include",
        "../memory/include",
        "../diagnostics/include",
        "../containers/include",
        "../io/include"
    }
    links {
        "filesystemCompat",
        "io",
        "diagnostics",
        "containers",
        "memory",
        "system"
    }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Private API/*"] = { "private/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }
    }

group "Tests/Filesystem"

project "filesystemTests"
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
        "../containers/include",
        "../io/include"
    }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "filesystem",
        "filesystemCompat",
        "redFileSystemCompat",
        "redCompressionCompat",
        "redCoreCompat",
        "redIOCompat",
        "redContainersCompat",
        "redMathCompat",
        "io",
        "diagnostics",
        "diagnosticsCompat",
        "containers",
        "containersCompat",
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
