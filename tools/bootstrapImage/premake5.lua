local importedRoot = "../../source/imported/common"
local nvToolsRoot = path.getabsolute("../../source/imported/external/nvToolsExt")
local oodleRoot = path.getabsolute("../../source/imported/external/oodle")

group "Tools/Bootstrap"

project "bootstrapImage"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(tools_output_root)
    objdir(object_root)
    debugdir(tools_output_root)
    debugargs { '"' .. runtime_output_root .. '"' }
    deployVanguardRuntimeDependencies(false)
    files { "src/**.cpp", "README.md" }
    includedirs {
        "../../source/assets/include", "../../source/world/include", "../../source/prefabs/include",
        "../../source/gameInput/include", "../../source/input/include", "../../source/window/include",
        "../../source/meshes/include", "../../source/schemas/include", "../../source/reflection/include",
        "../../source/resources/include", "../../source/packages/include", "../../source/serialization/include",
        "../../source/crypto/include", "../../source/filesystem/include", "../../source/io/include",
        "../../source/jobs/include", "../../source/concurrency/include", "../../source/containers/include",
        "../../source/diagnostics/include", "../../source/memory/include", "../../source/system/include",
        path.join(importedRoot, "redSystem/include"), path.join(importedRoot, "redMemory/include"),
        path.join(importedRoot, "redMath/include"), path.join(importedRoot, "redContainers/include"),
        path.join(importedRoot, "redIO/include"), path.join(importedRoot, "redCore/include"),
        path.join(importedRoot, "redCompression/include"), path.join(importedRoot, "redFileSystem/include")
    }
    libdirs { path.join(nvToolsRoot, "lib/x64.Release") }
    links {
        "assets", "world", "meshes", "prefabs", "schemas", "reflection", "gameInput", "input", "window",
        "packages", "resources", "crypto", "serialization", "serializationCompat", "filesystem",
        "filesystemCompat", "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat",
        "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "concurrency",
        "concurrencyCompat", "jobs", "jobsCompat", "redJobsCompat", "redContainersCompat", "redMathCompat",
        "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi",
        "Shell32", "Version", "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(oodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter {}
    vpaths { ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }
