group "Engine/Tools"

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

project "textureTools"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324", "4611" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.hpp", "src/**.cpp", "README.md" }
    includedirs {
        "include", "src", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include", "../io/include",
        "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include", "../jobs/include", "../concurrency/include",
        "../textures/include", "../assets/include", "../../external/bcCodecs/src", "../../external/compressonatorCore/cmp_core/source",
        "../../external/imageCodecs/libpng", "../../external/imageCodecs/zlib", "../../external/imageCodecs/libjpeg-turbo",
        "../../external/imageCodecs/libtiff", "../../external/imageCodecs/openexr", "../../external/imageCodecs/openexr/Imath",
        "../../external/imageCodecs/openexr/OpenEXRCore"
    }
    defines { "PNG_STATIC", "Z_PREFIX" }
    links { "assets", "textures", "bcCodecs", "compressonatorCore", "libpng", "zlib", "libjpegTurbo", "libtiff", "openexrCore", "openjph", "resources", "crypto", "serialization", "filesystem", "io", "jobs", "concurrency", "diagnostics", "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

group "Tests/Texture Tools"

project "textureToolsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp", "tests/data/**" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include", "../jobs/include", "../concurrency/include",
        "../textures/include", "../assets/include", "../../external/bcCodecs/src", "../../external/compressonatorCore/cmp_core/source",
        "../../external/imageCodecs/libtiff", "../../external/imageCodecs/openexr", "../../external/imageCodecs/openexr/OpenEXRCore",
        path.join(redFileSystemRoot, "include"), path.join(redFileSystemRoot, "src"),
        path.join(redCompressionRoot, "include"), path.join(redCompressionRoot, "src"), path.join(redSystemRoot, "include"),
        path.join(redMemoryRoot, "include"), path.join(redMemoryRoot, "src"), path.join(redMathRoot, "include"),
        path.join(redContainersRoot, "include"), path.join(redIORoot, "include"), path.join(redCoreRoot, "include")
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "textureTools", "assets", "textures", "bcCodecs", "compressonatorCore", "libtiff", "openexrCore", "openjph", "libjpegTurbo", "libpng", "zlib", "resources", "crypto", "serialization", "serializationCompat", "jobs", "jobsCompat", "redJobsCompat", "concurrency", "concurrencyCompat",
        "filesystem", "filesystemCompat", "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat",
        "redContainersCompat", "redMathCompat", "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Shell32",
        "Version", "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs { path.join(fileSyncRoot, "lib/release_x64"), path.join(udtRoot, "lib/release_x64") }
        links { "FileSync", "udt" }
    filter {}
    vpaths { ["Tests/*"] = { "tests/**.cpp" }, ["Fixtures/*"] = { "tests/data/**" } }

group "Benchmarks/Texture Tools"

project "textureToolsBenchmarks"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "benchmarks/**.cpp" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../jobs/include", "../concurrency/include", "../textures/include", "../assets/include", path.join(redFileSystemRoot, "include"),
        path.join(redFileSystemRoot, "src"), path.join(redCompressionRoot, "include"), path.join(redCompressionRoot, "src"),
        path.join(redSystemRoot, "include"), path.join(redMemoryRoot, "include"), path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"), path.join(redContainersRoot, "include"), path.join(redIORoot, "include"),
        path.join(redCoreRoot, "include")
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "textureTools", "assets", "textures", "bcCodecs", "compressonatorCore", "libtiff", "openexrCore", "openjph", "libjpegTurbo", "libpng", "zlib", "resources", "crypto", "serialization",
        "serializationCompat", "jobs", "jobsCompat", "redJobsCompat", "concurrency", "concurrencyCompat", "filesystem",
        "filesystemCompat", "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat",
        "redContainersCompat", "redMathCompat", "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Shell32",
        "Version", "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs { path.join(fileSyncRoot, "lib/release_x64"), path.join(udtRoot, "lib/release_x64") }
        links { "FileSync", "udt" }
    filter {}
    vpaths { ["Benchmarks/*"] = { "benchmarks/**.cpp" } }
