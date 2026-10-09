local editor_resource_root = path.getabsolute("resources")

function deployVanguardEditorResources()
    postbuildcommands {
        '{COPYDIR} "' .. editor_resource_root .. '" "%{cfg.targetdir}/Editor"'
    }
end

group "Applications/Editor"

project "editor"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "VanguardEditor"
    targetdir(editor_output_root)
    objdir(object_root)
    debugdir(editor_output_root)
    deployVanguardRuntimeDependencies(true)
    enableVanguardD3D12Agility()
    deployVanguardEditorResources()
    files { "framework/include/**.hpp", "framework/src/**.cpp", "private/**.hpp", "commands/**.cpp", "extensions/**.cpp", "panels/**.cpp", "surfaces/**.cpp", "src/**.cpp", "platform/windows/**.cpp", "README.md" }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/jobs/include",
        "../source/shaderTools/include", "../source/shaders/include", "../source/assets/include", "../source/textureTools/include",
        "../source/assetStreaming/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/io/include", "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/streaming/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/meshes/include", "../source/prefabs/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/crypto/include", "../source/math/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr"), imgui_root, nanosvg_root
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "shaderTools", "shaders", "textureTools", "assetStreaming", "assets", "crypto", "serializationCompat", "slang-compiler",
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "imgui", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    libdirs { path.join(slang_root, "lib") }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    vpaths {
        ["Framework/Public API/*"] = { "framework/include/**.hpp" },
        ["Framework/Source/*"] = { "framework/src/**.cpp" },
        ["Application/Private API/*"] = { "private/**.hpp" },
        ["Commands/*"] = { "commands/**.cpp" },
        ["Extensions/*"] = { "extensions/**.cpp" },
        ["Panels/*"] = { "panels/**.cpp" },
        ["Surfaces/*"] = { "surfaces/**.cpp" },
        ["Application/Source/*"] = { "src/**.cpp" },
        ["Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Documentation"] = { "README.md" }
    }

group "Tests/Editor"

local sceneOutputFixtureFileSyncRoot = path.getabsolute("../source/imported/internal/FileSync")
local sceneOutputFixtureUdtRoot = path.getabsolute("../source/imported/external/udt")
local sceneOutputFixtureOodleRoot = path.getabsolute("../source/imported/external/oodle")
local sceneOutputFixtureNvToolsRoot = path.getabsolute("../source/imported/external/nvToolsExt")

project "editorSceneOutputPackageFixture"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4324" }
    targetdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    objdir(object_root)
    debugdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    deployVanguardRuntimeDependencies(false)
    files { "tests/support/editor_scene_output_package_fixture.cpp" }
    includedirs {
        "../source/system/include", "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/io/include", "../source/filesystem/include", "../source/serialization/include", "../source/crypto/include",
        "../source/resources/include", "../source/reflection/include", "../source/schemas/include", "../source/prefabs/include",
        "../source/world/include", "../source/meshes/include", "../source/packages/include", "../source/rendering/include"
    }
    includedirs {
        "../source/imported/common/redFileSystem/include", "../source/imported/common/redFileSystem/src",
        "../source/imported/common/redCompression/include", "../source/imported/common/redCompression/src",
        "../source/imported/common/redSystem/include", "../source/imported/common/redMemory/include", "../source/imported/common/redMemory/src",
        "../source/imported/common/redMath/include", "../source/imported/common/redContainers/include",
        "../source/imported/common/redIO/include", "../source/imported/common/redCore/include"
    }
    libdirs { path.join(sceneOutputFixtureNvToolsRoot, "lib/x64.Release") }
    links {
        "world", "prefabs", "packages", "packagesCompat", "schemas", "reflection", "serialization", "serializationCompat",
        "crypto", "resources", "filesystem", "filesystemCompat", "redFileSystemCompat", "redCompressionCompat",
        "redCompressionThirdPartyCompat", "redCoreCompat", "redIOCompat", "redContainersCompat", "redMathCompat",
        "io", "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Shell32", "Version", "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.join(sceneOutputFixtureOodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(sceneOutputFixtureOodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs { path.join(sceneOutputFixtureFileSyncRoot, "lib/release_x64"), path.join(sceneOutputFixtureUdtRoot, "lib/release_x64") }
        links { "FileSync", "udt" }
        linkoptions { "/IGNORE:4006" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(sceneOutputFixtureFileSyncRoot, "lib/release_x64/FileSync.dll") .. '" "%{cfg.targetdir}/FileSync.dll"',
            '{COPYFILE} "' .. path.join(sceneOutputFixtureUdtRoot, "lib/release_x64/udt.dll") .. '" "%{cfg.targetdir}/udt.dll"'
        }
    filter {}
    vpaths { ["Tests/Scene Output Package Fixture/*"] = { "tests/support/editor_scene_output_package_fixture.cpp" } }

project "editorApplicationLifecycleIntegrationTest"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "VanguardEditorApplicationLifecycleIntegrationTest"
    targetdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    objdir(object_root)
    debugdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    deployVanguardRuntimeDependencies(true)
    enableVanguardD3D12Agility()
    deployVanguardEditorResources()
    files {
        "framework/include/**.hpp", "framework/src/**.cpp", "private/**.hpp", "commands/**.cpp", "extensions/**.cpp", "panels/**.cpp", "surfaces/**.cpp",
        "src/**.cpp", "platform/windows/**.cpp",
        -- Explicit test support. Never include tests/support through a production wildcard.
        "tests/support/editor_application_lifecycle_integration_test_application.cpp",
        "tests/support/editor_application_lifecycle_integration_test_application.hpp",
        "tests/support/editor_application_lifecycle_integration_test_main_windows.cpp",
        "tests/support/native_ui_integration_test_extension.hpp",
        "tests/editor-application-lifecycle-integration-test.vproject",
        "tests/editor-demonstration-integration-test.vproject",
        "tests/editor-scene-output-lifecycle-integration-test.vproject"
    }
    removefiles {
        -- The dedicated test executable owns only its process entry point and
        -- otherwise delegates the normal production editor composition unchanged.
        "platform/windows/main_windows.cpp"
    }
    includedirs {
        "framework/include", "private", "tests/support", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/jobs/include",
        "../source/shaderTools/include", "../source/shaders/include", "../source/assets/include", "../source/textureTools/include",
        "../source/assetStreaming/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/io/include", "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/streaming/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/meshes/include", "../source/prefabs/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/crypto/include", "../source/math/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr"), imgui_root, nanosvg_root
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "shaderTools", "shaders", "textureTools", "assetStreaming", "assets", "crypto", "serializationCompat", "slang-compiler",
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "imgui", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    libdirs { path.join(slang_root, "lib") }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    vpaths {
        ["Production/Framework/Public API/*"] = { "framework/include/**.hpp" },
        ["Production/Framework/Source/*"] = { "framework/src/**.cpp" },
        ["Production/Application/Private API/*"] = { "private/**.hpp" },
        ["Production/Commands/*"] = { "commands/**.cpp" },
        ["Production/Extensions/*"] = { "extensions/**.cpp" },
        ["Production/Panels/*"] = { "panels/**.cpp" },
        ["Production/Surfaces/*"] = { "surfaces/**.cpp" },
        ["Production/Application/Source/*"] = { "src/**.cpp" },
        ["Production/Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Tests/Application Lifecycle Integration Support/*"] = { "tests/support/editor_application_lifecycle_integration_test_*.*" },
        ["Tests/Shared Persistence Support/*"] = { "tests/support/native_ui_integration_test_extension.hpp" },
        ["Tests/Fixtures/*"] = { "tests/editor-application-lifecycle-integration-test.vproject" }
    }

project "editorNativeUiIntegrationTest"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetname "VanguardEditorNativeUiIntegrationTest"
    targetdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    objdir(object_root)
    debugdir(path.getabsolute("bin/EditorTests/%{cfg.buildcfg}"))
    deployVanguardRuntimeDependencies(true)
    enableVanguardD3D12Agility()
    deployVanguardEditorResources()
    files {
        "framework/include/**.hpp", "framework/src/**.cpp", "private/**.hpp", "commands/**.cpp", "extensions/**.cpp", "panels/**.cpp", "surfaces/**.cpp",
        "src/**.cpp", "platform/windows/**.cpp",
        -- Explicit test support. Never include tests/support through a production wildcard.
        "tests/support/native_ui_integration_test_harness.cpp",
        "tests/support/native_ui_integration_test_harness.hpp",
        "tests/support/native_ui_integration_test_extension.cpp",
        "tests/support/native_ui_integration_test_extension.hpp",
        "tests/support/native_ui_integration_test_application.cpp",
        "tests/support/native_ui_integration_test_application.hpp",
        "tests/support/native_ui_integration_test_catalog.cpp",
        "tests/support/native_ui_integration_test_main_windows.cpp",
        "tests/native-ui-integration-test.vproject",
        "tests/run-native-ui-integration-test.ps1"
    }
    removefiles {
        -- The dedicated test executable owns its process entry point and compiled
        -- extension catalog while delegating all normal editor composition to
        -- the production EditorApplication.
        "platform/windows/main_windows.cpp",
        "extensions/editor_static_extensions.cpp"
    }
    includedirs {
        "framework/include", "private", "tests/support", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/jobs/include",
        "../source/shaderTools/include", "../source/shaders/include", "../source/assets/include", "../source/textureTools/include",
        "../source/assetStreaming/include",
        "../source/input/include", "../source/gameInput/include",
        "../source/pipelines/include", "../source/pipelineCache/include", "../source/textures/include", "../source/materials/include",
        "../source/platform/windows/include", "../source/system/include",
        "../source/memory/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/concurrency/include", "../source/filesystem/include", "../source/io/include", "../source/window/include",
        "../source/resources/include", "../source/world/include", "../source/streaming/include",
        "../source/entities/include", "../source/gameWorld/include", "../source/ecs/include",
        "../source/meshes/include", "../source/prefabs/include", "../source/packages/include",
        "../source/serialization/include", "../source/schemas/include", "../source/reflection/include",
        "../source/crypto/include", "../source/math/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr"), imgui_root, nanosvg_root
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "shaderTools", "shaders", "textureTools", "assetStreaming", "assets", "crypto", "serializationCompat", "slang-compiler",
        "platformWindows", "engine", "rendering", "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "imgui", "world", "streaming", "resources", "packages", "schemas", "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi",
        "Shell32", "Shlwapi", "User32", "Version", "ws2_32"
    }
    libdirs { path.join(slang_root, "lib") }
    postbuildcommands {
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-compiler.dll") .. '" "%{cfg.targetdir}/slang-compiler.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glslang.dll") .. '" "%{cfg.targetdir}/slang-glslang.dll"',
        '{COPYFILE} "' .. path.join(slang_root, "bin/slang-glsl-module.dll") .. '" "%{cfg.targetdir}/slang-glsl-module.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxcompiler.dll") .. '" "%{cfg.targetdir}/dxcompiler.dll"',
        '{COPYFILE} "' .. path.join(dxc_root, "bin/dxil.dll") .. '" "%{cfg.targetdir}/dxil.dll"'
    }
    vpaths {
        ["Production/Framework/Public API/*"] = { "framework/include/**.hpp" },
        ["Production/Framework/Source/*"] = { "framework/src/**.cpp" },
        ["Production/Application/Private API/*"] = { "private/**.hpp" },
        ["Production/Commands/*"] = { "commands/**.cpp" },
        ["Production/Extensions/*"] = { "extensions/**.cpp" },
        ["Production/Panels/*"] = { "panels/**.cpp" },
        ["Production/Surfaces/*"] = { "surfaces/**.cpp" },
        ["Production/Application/Source/*"] = { "src/**.cpp" },
        ["Production/Platform/Windows/*"] = { "platform/windows/**.cpp" },
        ["Tests/Native UI Integration Support/*"] = { "tests/support/native_ui_integration_test_*.*" },
        ["Tests/Fixtures/*"] = { "tests/native-ui-integration-test.vproject", "tests/run-native-ui-integration-test.ps1" }
    }

project "editorPanelTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_panel_tests.cpp",
        "tests/support/editor_extension_registration_test_adapter.hpp",
        "panels/editor_panel_registry.cpp",
        "private/vanguard/editor/editor_panels.hpp",
        "framework/include/vanguard/editor/editor_ui.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "tests/support", "../source/application/include", "../source/engine/include",
        "../source/rendering/include", "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/window/include",
        "../source/resources/include", "../source/math/include", "../source/jobs/include", "../source/input/include",
        "../source/concurrency/include", "../source/containers/include", "../source/memory/include",
        "../source/diagnostics/include", "../source/system/include"
    }
    links {
        "concurrency", "concurrencyCompat", "containers", "containersCompat", "memory",
        "diagnostics", "diagnosticsCompat", "redSystemCompat", "system",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_panel_tests.cpp" },
        ["Tests/Support/*"] = { "tests/support/editor_extension_registration_test_adapter.hpp" },
        ["Production/*"] = { "panels/editor_panel_registry.cpp", "private/vanguard/editor/editor_panels.hpp",
                             "framework/include/vanguard/editor/editor_ui.hpp",
                             "framework/include/vanguard/editor/editor_service_ids.hpp" }
    }

project "editorSurfaceTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_surface_tests.cpp",
        "surfaces/editor_surface_registry.cpp",
        "private/vanguard/editor/editor_surfaces.hpp",
        "private/vanguard/editor/editor_extension.hpp",
        "private/vanguard/editor/editor_commands.hpp",
        "framework/include/vanguard/editor/editor_ui.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include",
        "../source/rendering/include", "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/window/include",
        "../source/resources/include", "../source/math/include", "../source/jobs/include", "../source/input/include",
        "../source/concurrency/include", "../source/containers/include", "../source/memory/include",
        "../source/diagnostics/include", "../source/system/include"
    }
    links {
        "concurrency", "concurrencyCompat", "containers", "containersCompat", "memory",
        "diagnostics", "diagnosticsCompat", "redSystemCompat", "system",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_surface_tests.cpp" },
        ["Production/*"] = {
            "surfaces/editor_surface_registry.cpp",
            "private/vanguard/editor/editor_surfaces.hpp",
            "private/vanguard/editor/editor_extension.hpp",
            "private/vanguard/editor/editor_commands.hpp",
            "framework/include/vanguard/editor/editor_ui.hpp",
            "framework/include/vanguard/editor/editor_service_ids.hpp"
        }
    }

project "editorCommandTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_command_tests.cpp",
        "tests/support/editor_extension_registration_test_adapter.hpp",
        "commands/editor_command_service.cpp",
        "private/vanguard/editor/editor_commands.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "tests/support", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/input/include",
        "../source/resources/include", "../source/math/include",
        "../source/jobs/include", "../source/window/include", "../source/concurrency/include", "../source/diagnostics/include",
        "../source/containers/include", "../source/memory/include", "../source/system/include"
    }
    links {
        "engine", "input", "jobs", "jobsCompat", "application", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_command_tests.cpp" },
        ["Tests/Support/*"] = { "tests/support/editor_extension_registration_test_adapter.hpp" },
        ["Production/*"] = { "commands/editor_command_service.cpp", "private/vanguard/editor/editor_commands.hpp",
                             "framework/include/vanguard/editor/editor_service_ids.hpp" }
    }

project "editorShellTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_shell_tests.cpp",
        "framework/src/editor_file_dialog.cpp",
        "commands/editor_command_service.cpp",
        "panels/editor_panel_registry.cpp",
        "surfaces/editor_surface_registry.cpp",
        "extensions/editor_extension_catalog.cpp",
        "extensions/editor_shell_service.cpp",
        "extensions/editor_demonstration.cpp",
        "tests/support/editor_demonstration_test_link_stubs.cpp",
        "tests/support/editor_test_extensions.cpp",
        "tests/support/editor_test_extensions.hpp",
        "tests/support/editor_extension_registration_test_adapter.hpp",
        "private/vanguard/editor/editor_extension.hpp",
        "private/vanguard/editor/editor_asset_browser_persistence.hpp",
        "private/vanguard/editor/editor_asset_browser_policy.hpp",
        "private/vanguard/editor/editor_asset_browser_tree.hpp",
        "private/vanguard/editor/editor_console_persistence.hpp",
        "private/vanguard/editor/editor_commands.hpp",
        "private/vanguard/editor/editor_panels.hpp",
        "private/vanguard/editor/editor_surfaces.hpp",
        "framework/include/vanguard/editor/editor_activity.hpp",
        "framework/include/vanguard/editor/editor_file_dialog.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp",
        "private/vanguard/editor/editor_activity_service.hpp",
        "private/vanguard/editor/editor_shell_service.hpp"
    }
    includedirs {
        "framework/include", "private", "tests/support", "../tests/support", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/input/include", "../source/window/include",
        "../source/resources/include", "../source/math/include", "../source/jobs/include", "../source/projects/include", "../source/assets/include",
        "../source/assetStreaming/include", "../source/streaming/include", "../source/textures/include",
        "../source/packages/include", "../source/serialization/include", "../source/reflection/include", "../source/schemas/include",
        "../source/crypto/include", "../source/resources/include", "../source/filesystem/include", "../source/io/include",
        "../source/containers/include", "../source/concurrency/include", "../source/diagnostics/include",
        "../source/memory/include", "../source/system/include", imgui_root
    }
    links {
        "application", "assetStreaming", "assets", "jobs", "projects", "filesystem", "io", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system", "imgui", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_shell_tests.cpp" },
        ["Production/*"] = {
            "commands/editor_command_service.cpp", "panels/editor_panel_registry.cpp", "surfaces/editor_surface_registry.cpp",
            "extensions/editor_extension_catalog.cpp", "extensions/editor_shell_service.cpp", "extensions/editor_demonstration.cpp",
            "private/vanguard/editor/editor_extension.hpp", "private/vanguard/editor/editor_asset_browser_persistence.hpp",
            "private/vanguard/editor/editor_asset_browser_policy.hpp",
            "private/vanguard/editor/editor_asset_browser_tree.hpp",
            "private/vanguard/editor/editor_console_persistence.hpp",
            "private/vanguard/editor/editor_commands.hpp", "private/vanguard/editor/editor_panels.hpp",
            "private/vanguard/editor/editor_surfaces.hpp", "framework/include/vanguard/editor/editor_activity.hpp",
            "framework/src/editor_file_dialog.cpp", "framework/include/vanguard/editor/editor_file_dialog.hpp",
            "framework/include/vanguard/editor/editor_service_ids.hpp", "private/vanguard/editor/editor_activity_service.hpp",
            "private/vanguard/editor/editor_shell_service.hpp"
        },
        ["Tests/Support/*"] = { "tests/support/editor_demonstration_test_link_stubs.cpp",
                                "tests/support/editor_test_extensions.cpp", "tests/support/editor_test_extensions.hpp",
                                "tests/support/editor_extension_registration_test_adapter.hpp" }
    }

project "editorUserStateTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_user_state_tests.cpp",
        "src/editor_user_state_service.cpp",
        "src/editor_user_state_text_codec.cpp",
        "src/editor_state_artifact_store.cpp",
        "framework/src/editor_ui_persistence.cpp",
        "panels/editor_panel_registry.cpp",
        "private/vanguard/editor/editor_user_state_service.hpp",
        "private/vanguard/editor/editor_ui_persistence.hpp",
        "private/vanguard/editor/editor_panels.hpp",
        "private/vanguard/editor/editor_commands.hpp",
        "private/vanguard/editor/editor_extension.hpp",
        "private/vanguard/editor/editor_project_service.hpp",
        "framework/include/vanguard/editor/editor_ui.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include",
        "../source/projects/include", "../source/serialization/include", "../source/crypto/include",
        "../source/filesystem/include", "../source/io/include", "../source/input/include",
        "../source/rendering/include", "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/window/include",
        "../source/resources/include", "../source/assets/include", "../source/assetStreaming/include", "../source/streaming/include",
        "../source/textures/include", "../source/packages/include", "../source/reflection/include", "../source/schemas/include",
        "../source/jobs/include", "../source/concurrency/include",
        "../source/containers/include", "../source/memory/include", "../source/diagnostics/include", "../source/system/include"
    }
    libdirs { path.join(sceneOutputFixtureNvToolsRoot, "lib/x64.Release") }
    links {
        "projects", "crypto", "serialization", "serializationCompat", "application", "window", "filesystem", "filesystemCompat",
        "redFileSystemCompat", "redCompressionCompat", "redCoreCompat", "redIOCompat", "redContainersCompat", "redMathCompat",
        "io", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat", "containers", "containersCompat",
        "memory", "redSystemCompat", "system", "nvToolsExt64_1", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Shell32",
        "Version", "ws2_32"
    }
    filter "configurations:Debug"
        libdirs { path.join(sceneOutputFixtureOodleRoot, "lib/x64.Debug") }
        links { "oo2ext_win64_debug" }
    filter "configurations:not Debug"
        libdirs { path.join(sceneOutputFixtureOodleRoot, "lib/x64.Release") }
        links { "oo2ext_win64" }
    filter "configurations:not Shipping"
        libdirs { path.join(sceneOutputFixtureFileSyncRoot, "lib/release_x64"), path.join(sceneOutputFixtureUdtRoot, "lib/release_x64") }
        links { "FileSync", "udt" }
    filter {}
    vpaths {
        ["Tests/*"] = { "tests/editor_user_state_tests.cpp" },
        ["Production/*"] = {
            "src/editor_user_state_service.cpp", "framework/src/editor_ui_persistence.cpp", "panels/editor_panel_registry.cpp",
            "private/vanguard/editor/editor_user_state_service.hpp", "private/vanguard/editor/editor_ui_persistence.hpp",
            "private/vanguard/editor/editor_panels.hpp",
            "private/vanguard/editor/editor_commands.hpp", "private/vanguard/editor/editor_extension.hpp",
            "private/vanguard/editor/editor_project_service.hpp", "framework/include/vanguard/editor/editor_ui.hpp",
            "framework/include/vanguard/editor/editor_service_ids.hpp"
        }
    }

project "editorActivityTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324", "4389" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_activity_tests.cpp",
        "framework/src/editor_activity_service.cpp",
        "framework/include/vanguard/editor/editor_activity.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp",
        "private/vanguard/editor/editor_activity_service.hpp"
    }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include", "../source/input/include", "../source/window/include",
        "../source/concurrency/include", "../source/containers/include", "../source/memory/include",
        "../source/diagnostics/include", "../source/system/include"
    }
    links {
        "application", "concurrency", "concurrencyCompat", "diagnostics", "diagnosticsCompat",
        "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_activity_tests.cpp" },
        ["Production/*"] = {
            "framework/src/editor_activity_service.cpp",
            "framework/include/vanguard/editor/editor_activity.hpp",
            "framework/include/vanguard/editor/editor_service_ids.hpp",
            "private/vanguard/editor/editor_activity_service.hpp"
        }
    }

project "editorAssetRetirementTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    enableVanguardD3D12Agility()
    files {
        "tests/editor_asset_retirement_tests.cpp",
        "src/editor_project_service.cpp",
        "private/vanguard/editor/editor_project_service.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/assets/include",
        "../source/textureTools/include", "../source/shaderTools/include", "../source/shaders/include", "../source/assetStreaming/include",
        "../source/textures/include", "../source/pipelines/include", "../source/pipelineCache/include", "../source/resources/include",
        "../source/streaming/include", "../source/packages/include", "../source/schemas/include", "../source/reflection/include",
        "../source/serialization/include", "../source/crypto/include", "../source/filesystem/include", "../source/io/include",
        "../source/jobs/include", "../source/concurrency/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/memory/include", "../source/system/include", "../source/math/include", "../source/input/include",
        "../source/gameInput/include", "../source/window/include", "../source/platform/windows/include", "../source/entities/include",
        "../source/world/include", "../source/meshes/include", "../source/materials/include", "../source/prefabs/include",
        "../source/gameWorld/include", "../source/ecs/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr")
    }
    links {
        "textureTools", "shaderTools", "shaders", "assetStreaming", "assets", "crypto", "serializationCompat", "engine", "rendering",
        "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "textures",
        "meshes", "materials", "prefabs", "pipelines", "pipelineCache", "window", "input", "gameInput", "packages", "schemas",
        "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat",
        "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_asset_retirement_tests.cpp" },
        ["Production/*"] = { "src/editor_project_service.cpp", "private/vanguard/editor/editor_project_service.hpp",
                             "framework/include/vanguard/editor/editor_service_ids.hpp" }
    }

project "editorSceneOutputTests"
    enforceEngineCodePolicy()
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    exceptionhandling "Off"
    fatalwarnings "All"
    disablewarnings { "4127", "4324" }
    targetdir(output_root)
    objdir(object_root)
    files {
        "tests/editor_scene_output_tests.cpp",
        "src/editor_scene_output_service.cpp",
        "framework/src/editor_ui_render_data.cpp",
        "private/vanguard/editor/editor_scene_output_service.hpp",
        "private/vanguard/editor/editor_ui_render_data.hpp",
        "framework/include/vanguard/editor/editor_ui.hpp",
        "framework/include/vanguard/editor/editor_service_ids.hpp"
    }
    includedirs {
        "framework/include", "private", "../source/application/include", "../source/engine/include", "../source/rendering/include",
        "../source/rhi/include", "../source/rhi/nvrhi/include", "../source/projects/include", "../source/assets/include",
        "../source/textureTools/include", "../source/shaderTools/include", "../source/shaders/include", "../source/assetStreaming/include",
        "../source/textures/include", "../source/pipelines/include", "../source/pipelineCache/include", "../source/resources/include",
        "../source/streaming/include", "../source/packages/include", "../source/schemas/include", "../source/reflection/include",
        "../source/serialization/include", "../source/crypto/include", "../source/filesystem/include", "../source/io/include",
        "../source/jobs/include", "../source/concurrency/include", "../source/diagnostics/include", "../source/containers/include",
        "../source/memory/include", "../source/system/include", "../source/math/include", "../source/input/include",
        "../source/gameInput/include", "../source/window/include", "../source/platform/windows/include", "../source/entities/include",
        "../source/world/include", "../source/meshes/include", "../source/materials/include", "../source/prefabs/include",
        "../source/gameWorld/include", "../source/ecs/include", "../source/imported/common/redMath/include", path.join(flecs_root, "distr")
    }
    defines { "FLECS_CUSTOM_BUILD", "FLECS_CPP", "FLECS_MODULE", "FLECS_SYSTEM", "FLECS_PIPELINE", "FLECS_TIMER" }
    links {
        "textureTools", "shaderTools", "shaders", "assetStreaming", "assets", "crypto", "serializationCompat", "engine", "rendering",
        "rhiNvrhi", "rhi", "projects", "entities", "gameWorld", "ecs", "flecs", "world", "streaming", "resources", "textures",
        "meshes", "materials", "prefabs", "pipelines", "pipelineCache", "window", "input", "gameInput", "packages", "schemas",
        "reflection", "serialization", "filesystem", "io", "application", "jobs", "jobsCompat", "concurrency", "concurrencyCompat",
        "diagnostics", "diagnosticsCompat", "containers", "containersCompat", "memory", "redSystemCompat", "system",
        "d3d12", "dxgi", "Advapi32", "Dbghelp", "Psapi", "Shlwapi", "Version", "ws2_32"
    }
    vpaths {
        ["Tests/*"] = { "tests/editor_scene_output_tests.cpp" },
        ["Production/Scene Output/*"] = { "src/editor_scene_output_service.cpp", "private/vanguard/editor/editor_scene_output_service.hpp" },
        ["Production/UI Handoff/*"] = { "framework/src/editor_ui_render_data.cpp", "private/vanguard/editor/editor_ui_render_data.hpp",
                                         "framework/include/vanguard/editor/editor_ui.hpp", "framework/include/vanguard/editor/editor_service_ids.hpp" }
    }
