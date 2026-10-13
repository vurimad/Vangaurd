---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals resourceMaterial

resourceMaterial_location = '../../../temp/' .. _ACTION .. '/common/resourceMaterial/'

-- files resourceMaterial

resourceMaterial_files = {
	[""] = {
		"include/resourceMaterialApi.h",
		"include/resourceMaterialPublic.h",
		"src/resourceMaterialInit.cpp",
		"src/resourceMaterialInternal.h",
		"src/build.cpp",
		"src/build.h",
		},
	["Multilayer"] = {
		"include/materialLayerDefinition.h",
		"src/materialLayerDefinition.cpp",
		"include/materialLayerLibrary.h",
		"src/materialLayerLibrary.cpp",
		"include/multilayerLayerTemplate.h",
		"src/multilayerLayerTemplate.cpp",
		"include/multilayerMask.h",
		"src/multilayerMask.cpp",
		"include/multilayerSetup.h",
		"src/multilayerSetup.cpp",
		"src/multilayerResourceLoader.cpp",
		"src/multilayerResourceSaver.cpp",
		},
	["Parameter"] = {
		"include/materialParameter.h",
		"src/materialParameter.cpp",
		"include/materialParameterInstance.h",
		"src/materialParameterInstance.cpp",
		"include/skinProfile.h",
		"src/skinProfile.cpp",
		"include/hairProfile.h",
		"src/hairProfile.cpp",
		"include/gradient.h",
		"src/gradient.cpp",
		"include/terrainSetup.h",
		"src/terrainSetup.cpp",
		"include/foliageProfile.h",
		"src/foliageProfile.cpp",
		"include/textureRegionSet.h",
		"src/textureRegionSet.cpp",
		},
	["Material"] = {
		"include/material.h",
		"src/material.cpp",
		"include/materialDefinition.h",
		"src/materialDefinition.cpp",
		"include/materialInstance.h",
		"src/materialInstance.cpp",
		"include/materialMetaDefinition.h",
		"src/materialMetaDefinition.cpp",
		"include/materialTemplate.h",
		"src/materialTemplate.cpp",
		"src/materialOverrideProvider.h",
		"src/materialOverrideProvider.cpp",
		},
		
   ["Texture"] = {
		"include/bitmapDataBuffer.h",
		"include/bitmapTexture.h",
		"include/cubeTexture.h",
		"include/dynamicTexture.h",
		"include/sourceTextureData.h",
		"include/texture.h",
		"include/textureArray.h",
		"src/bitmapDataBuffer.cpp",
		"src/bitmapTexture.cpp",
		"src/build.cpp",
		"src/build.h",
		"src/cubeTexture.cpp",
		"src/dynamicTexture.cpp",
		"src/sourceTextureData.cpp",
		"src/texture.cpp",
		"src/textureArray.cpp",
		"include/mipMapGenParams.h"
		},
		
	["Compilation"] = {
		"include/materialCompilationManager.h",
		"src/materialCompilationManager.cpp",
		},
}

-- configs resourceMaterial

project "resourceMaterial"
    filter "platforms:not x64_DLL"
        kind "StaticLib"
    filter "platforms:x64_DLL"
        kind "SharedLib"
    filter {}

    language "C++"

    location(resourceMaterial_location)

    pchheader "build.h"
    pchsource "src/build.cpp"

    includedirs "include"

    uses { 
		"shaderCache"
		}

	prebuildcommands {
		generateRtti()
	}
	
    vpaths(resourceMaterial_files)

    files(seq(resourceMaterial_files))
