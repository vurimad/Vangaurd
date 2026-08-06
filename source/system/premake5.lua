group "Engine/Runtime"

project "system"
    enforceEngineCodePolicy()
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "include/**.hpp",
        "src/**.cpp"
    }

    includedirs {
        "include"
    }

    vpaths {
        ["Public API/*"] = { "include/**.hpp" },
        ["Source/*"] = { "src/**.cpp" }
    }

group "Tests/System"

project "systemTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "tests/system_tests.cpp"
    }

    includedirs {
        "include"
    }

    links {
        "system"
    }

    vpaths {
        ["Tests/*"] = { "tests/**.cpp" }
    }

project "systemFatalTest"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    fatalwarnings "All"
    targetdir(output_root)
    objdir(object_root)

    files {
        "tests/fatal_test.cpp"
    }

    includedirs {
        "include"
    }

    links {
        "system"
    }

    vpaths {
        ["Tests/*"] = { "tests/**.cpp" }
    }
