group "Engine/Tools"

project "shaderTools"
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
        "include", "../shaders/include", "../crypto/include", "../serialization/include", "../resources/include",
        "../filesystem/include", "../io/include", "../containers/include", "../concurrency/include", "../memory/include",
        "../system/include", path.join(slang_root, "include")
    }
    links { "shaders", "crypto", "serialization", "resources", "filesystem", "io", "containers", "concurrency", "memory", "system" }
    vpaths {
        ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" }
    }

group "Tests/Shader Tools"

project "shaderToolsTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(output_root)
    objdir(object_root)
    files { "tests/**.cpp", "tests/data/**.slang" }
    includedirs {
        "include", "../shaders/include", "../crypto/include", "../serialization/include", "../resources/include",
        "../filesystem/include", "../io/include", "../containers/include", "../concurrency/include", "../memory/include",
        "../diagnostics/include", "../system/include", path.join(slang_root, "include")
    }
    libdirs { path.join(slang_root, "lib") }
    links {
        "shaderTools", "shaders", "crypto", "serialization", "serializationCompat", "resources", "filesystem", "io",
        "containers", "containersCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "memory",
        "redSystemCompat", "system", "slang-compiler", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    vpaths { ["Tests/*"] = { "tests/**.cpp" }, ["Fixtures/*"] = { "tests/data/**.slang" } }
