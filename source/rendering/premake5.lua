group "Engine/Rendering"

project "rendering"
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
        "include", "../rhi/include", "../pipelineCache/include", "../pipelines/include", "../shaders/include",
        "../crypto/include", "../serialization/include", "../resources/include", "../filesystem/include", "../io/include",
        "../containers/include", "../concurrency/include", "../memory/include", "../system/include"
    }
    links { "pipelineCache", "pipelines", "shaders", "rhi", "crypto", "serialization", "resources", "filesystem", "io", "containers", "concurrency", "memory", "system" }
    vpaths { ["Public API/*"] = { "include/**.hpp" }, ["Source/*"] = { "src/**.cpp" }, ["Documentation"] = { "README.md" } }
