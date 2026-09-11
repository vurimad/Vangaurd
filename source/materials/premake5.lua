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
        path.join(redMathRoot, "include"), path.join(redContainersRoot, "include"), path.join(redIORoot, "include"),
        path.join(redCoreRoot, "include")
    }
end

group "Engine/Runtime"

project "materials"
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
        "include", "../system/include", "../memory/include", "../containers/include", "../io/include",
        "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../shaders/include", "../pipelines/include"
    }
    links { "pipelines", "shaders", "serialization", "crypto", "resources", "filesystem", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }

group "Tests/Materials"

project "materialsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/materials_tests.cpp" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../shaders/include", "../pipelines/include", "../packages/include"
    }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "materials", "pipelines", "shaders", "packages", "resources", "crypto", "serialization", "serializationCompat",
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
    vpaths { ["Tests/*"] = { "tests/**.cpp" } }

project "materialResourceTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/material_resource_tests.cpp" }
    includedirs {
        "include", "../system/include", "../memory/include", "../diagnostics/include", "../concurrency/include", "../containers/include",
        "../io/include", "../filesystem/include", "../serialization/include", "../crypto/include", "../resources/include",
        "../streaming/include", "../packages/include", "../jobs/include", "../reflection/include", "../schemas/include",
        "../shaders/include", "../pipelines/include", "../rhi/include", "../rendering/include", "../rendering/private"
    }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "rendering", "materials", "pipelines", "shaders", "streaming", "schemas", "reflection", "reflectionCompat", "redReflectionCompat",
        "packages", "packagesCompat", "resources", "crypto", "serialization",
        "serializationCompat", "filesystem", "filesystemCompat", "redFileSystemCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "redCoreCompat", "redIOCompat", "redContainersCompat", "redMathCompat", "io", "jobs",
        "jobsCompat", "redJobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers",
        "containersCompat", "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi",
        "Shlwapi", "Shell32", "Version", "ws2_32"
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
    vpaths { ["Tests/*"] = { "tests/material_resource_tests.cpp" } }
