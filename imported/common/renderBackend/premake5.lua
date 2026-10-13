---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals renderBackend

renderBackend_location = '../../../temp/' .. _ACTION .. '/common/renderBackend'

-- files renderBackend

renderBackend_files = {
	[""] = {
		"src/build.cpp",
		"src/meshPackingSource.cpp",
		"src/renderBackendInit.cpp",
		"src/renderMeshCompiler.cpp",
		"src/renderTextureCompiler.cpp",
		"include/meshPackingSource.h",
		"include/renderBackendApi.h",
		"include/renderBackendPublic.h",
		"include/renderMeshCompilationSource.h",
		"include/renderMeshCompiler.h",
		"include/renderTextureCompilationSource.h",
		"include/renderTextureCompiler.h",
		"include/renderTextureMipMapLevel.h",
		"include/materialLibrary.h",
		"include/materialMetaDefinitionLoader.h",
		"src/build.h",
		"src/renderBackendInternal.h",
		"src/renderTextureCompilerUtils.h",
		"include/renderParticleCompilationSource.h",
		"include/renderParticleCompiler.h",
		"src/renderParticleCompiler.cpp",
		"src/materialLibrary.cpp",
		"src/materialMetaDefinitionLoader.cpp",
		"include/shaderMetaLoader.h",
		"src/shaderMetaLoader.cpp",
	},
	["Cooking"] = {
		"src/textureCookDurango.cpp",
		"src/textureCookOrbis.cpp",
	}
}
renderBackend_durango_files = {
	["Cooking"] = {
		"src/textureCookDurango.cpp",
	}
}

renderBackend_orbis_files = {
	["Cooking"] = {
		"src/textureCookOrbis.cpp",
	}
}

-- configs renderBackend

project "renderBackend"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(renderBackend_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { 
		"src", 
		"include",
		"../../../external/speedtree_v8/cpp_source",
		"../../../external/speedtree_v8/include/wind"	
	}

	uses {
		"redImage",
		"app",
		"renderBackend",
		"renderData",
		"resourceMaterial",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(renderBackend_files)

	filter "platforms:Durango"
		files {
			"src/renderBackendInit.cpp",
			"src/build.cpp"
		}
	filter "platforms:ORBIS"
		files {
			"src/renderBackendInit.cpp",
			"src/build.cpp"
		}
	filter "system:Windows"
		files(seq(renderBackend_files))

	filter "system:Windows"
		if not hasDurangoSDKInstalled() then
			print( "Durango SDK not detected. Cooking for Durango will be disabled." )
			defines { "NO_CONSOLE_SDK" }
			removefiles(seq(renderBackend_durango_files))
		end

	filter "system:Windows"
		if not hasOrbisSDKInstalled() or not hasCorrectOrbisSDKInstalled() then
			if hasOrbisSDKInstalled() then
				print( "Incorrect Orbis SDK installed. Cooking for Orbis will be disabled!" )
			else
				print( "Orbis SDK not detected. Cooking for Orbis will be disabled!" )
			end
			defines { "NO_CONSOLE_SDK" }
			removefiles(seq(renderBackend_orbis_files))
		end
	
	filter {} -- reset filter

usage "renderBackend"
	-- include SDK specific files and add SDK include/lib paths
	linkToRenderBackend() -- use common function to include host SDK's

	filter { "system:Windows", "kind:ConsoleApp or WindowedApp" }
		if hasDurangoSDKInstalled() then
			postbuildcommands '{COPY} "$(DurangoXDK)/bin/xg.dll" "%{cfg.targetdir}"'
		end