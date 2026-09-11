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
        path.join(redFileSystemRoot, "include"), path.join(redFileSystemRoot, "src"), path.join(redCompressionRoot, "include"),
        path.join(redCompressionRoot, "src"), path.join(redSystemRoot, "include"), path.join(redMemoryRoot, "include"),
        path.join(redMemoryRoot, "src"), path.join(redMathRoot, "include"), path.join(redContainersRoot, "include"),
        path.join(redIORoot, "include"), path.join(redCoreRoot, "include")
    }
end

group "Engine/Tools"

project "materialTools"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "include/**.hpp", "src/**.cpp" }
    includedirs { "include", "../assets/include", "../shaderTools/include", "../shaders/include", "../pipelines/include", "../materials/include", "../textures/include",
                  "../crypto/include", "../serialization/include", "../resources/include",
                  "../filesystem/include", "../io/include", "../containers/include", "../memory/include", "../system/include" }
    links { "assets", "shaderTools", "shaders", "pipelines", "materials", "crypto", "serialization", "resources", "filesystem", "io", "containers", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" } }

group "Tests/Material Tools"

project "materialToolsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp" }
    includedirs { "include", "../assets/include", "../packages/include", "../shaderTools/include", "../shaders/include", "../pipelines/include", "../materials/include", "../textures/include",
                  "../jobs/include", "../concurrency/include", "../shaders/include", "../crypto/include", "../serialization/include", "../resources/include",
                  "../filesystem/include", "../io/include", "../containers/include", "../memory/include", "../diagnostics/include", "../system/include",
                  path.join(slang_root, "include") }
    importedIncludes()
    libdirs { path.join(nvToolsRoot, "lib/x64.Release"), path.join(slang_root, "lib") }
    links { "materialTools", "assets", "shaderTools", "shaders", "pipelines", "materials", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "packages",
            "crypto", "serialization", "serializationCompat", "resources", "filesystem", "filesystemCompat", "io",
            "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat", "redContainersCompat", "redMathCompat", "diagnostics",
            "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp",
            "Psapi", "Shlwapi", "Shell32", "Version", "ws2_32", "slang-compiler" }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
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
