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

local function importedIncludes()
    includedirs {
        path.join(redFileSystemRoot, "include"), path.join(redFileSystemRoot, "src"),
        path.join(redCompressionRoot, "include"), path.join(redCompressionRoot, "src"),
        path.join(redSystemRoot, "include"), path.join(redMemoryRoot, "include"), path.join(redMemoryRoot, "src"),
        path.join(redMathRoot, "include"), path.join(redContainersRoot, "include"),
        path.join(redIORoot, "include"), path.join(redCoreRoot, "include")
    }
end

group "Engine/Runtime"

project "textures"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.hpp", "src/**.cpp", "README.md" }
    includedirs {
        "include", "src", "../system/include", "../memory/include", "../containers/include", "../io/include",
        "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../streaming/include", "../packages/include", "../jobs/include", "../concurrency/include", "../reflection/include", "../schemas/include"
    }
    links { "serialization", "crypto", "resources", "streaming", "packages", "schemas", "reflection", "filesystem", "concurrency", "containers", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.hpp", "src/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

group "Tests/Textures"

project "texturesTests"
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
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../streaming/include", "../packages/include", "../jobs/include", "../concurrency/include", "../reflection/include", "../schemas/include"
    }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "textures", "streaming", "packages", "schemas", "reflection", "resources", "crypto", "serialization", "serializationCompat", "filesystem", "filesystemCompat",
        "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat", "redContainersCompat",
        "redMathCompat", "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory",
        "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Shell32",
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
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }
