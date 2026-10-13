---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals backendMaterial

backendMaterial_location = '../../../temp/' .. _ACTION .. '/backend/backendMaterial'

-- files backendMaterial

backendMaterial_files = {
	[""] = {
		"src/build.h",
		"src/build.cpp",
		"src/backendMaterialInit.cpp",
		"src/backendMaterialInternal.h",
		"include/backendMaterialApi.h",
		"include/backendMaterialPublic.h"
	},
	["editors"] = {
		"include/materialViewer.h",
		"src/materialViewer.cpp",
		"src/materialLibraryEditor.h",
		"src/materialLibraryEditor.cpp",
		"include/materialPreviewPanel.h",
		"src/materialPreviewPanel.cpp",
		"src/multilayerLayerTemplateViewer.cpp",
		"src/baseMaterialCustomEditor.cpp",
		"src/terrainSetupCustomEditor.cpp",
		"include/mlmaskViewer.h",
		"src/mlmaskViewer.cpp",
		"include/mlmaskPreviewPanel.h",
		"src/mlmaskPreviewPanel.cpp",
		"include/textureViewer.h",
		"src/textureViewer.cpp",
		"include/texturePreviewPanel.h",
		"src/texturePreviewPanel.cpp",
		"include/textureArrayEditor.h",
		"src/textureArrayEditor.cpp",
		"src/materialValidator.h",
		"src/materialValidator.cpp"
	},
	["import"] = {
		"include/multilayerMaskBuilder.h",
		"src/multilayerMaskBuilder.cpp",
		"src/importMultilayerMask.cpp",
		"src/importMultilayerSetup.cpp",
		"src/importBitmapTexture.cpp",
		"src/importBitmapTexture.h",
		"src/importCubeTexture.cpp",
		"src/importIES.cpp",
	},
	["commandlets"] = {
		"src/multilayerCreateMaskCommandlet.cpp",
		"src/multilayerDumpLegacyCommandlet.cpp",
		"src/reimportMaterialsCommandlet.cpp",
		"src/textureResolutionCommandlet.cpp"
	},
	["data"] = {
		"include/materialController.h",
		"src/materialController.cpp",
		"include/materialInterop.h",
		"src/materialInterop.cpp",
		"include/materialLibraryDataProvider.h",
		"src/materialLibraryDataProvider.cpp",
		"include/materialLayersTree.h",
		"src/materialLayersTree.cpp",
		"include/materialLayerNode.h",
		"src/materialLayerNode.cpp",
		"include/mlmaskView.h",
		"src/mlmaskView.cpp",
		"include/textureInterface.h",
		"src/textureInterface.cpp",
		"include/textureView.h",
		"src/textureView.cpp",
	},
	["metadata"] = {
		"include/multilayerMaskMetadata.h",
		"src/multilayerMaskMetadata.cpp",
	},
	["export"] = {
		"src/exportBitmap.cpp",
	},
	["cook"] = {
		"include/bitmapTextureCompilationSource.h",
		"include/bitmapTextureCooker.h",
		"include/cubeTextureCooker.h",
		"include/textureArrayCooker.h",
		"src/bitmapTextureCompilationSource.cpp",
		"src/bitmapTextureCooker.cpp",	
		"src/cubeTextureCompilationSource.cpp",
		"src/cubeTextureCompilationSource.h",
		"src/cubeTextureCooker.cpp",
		"src/particleCooker.cpp",
		"src/textureArrayCompilationSource.cpp",
		"src/textureArrayCompilationSource.h",
		"src/textureArrayCooker.cpp",
		"src/textureCooking.cpp",
		"src/cookedMultilayerSetupGenerator.cpp",
		"src/materialCooking.cpp",
	},
}
-- configs backendMaterial

project "backendMaterial"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}
	
	language "C++"
	location(backendMaterial_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs "include"

	uses { 
		"backendEngine",
		"renderBackend",
		"worldEntities",
		"backendData",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(backendMaterial_files)

	filter "platforms:Durango or ORBIS"
		flags { "ExcludeFromBuild" }
	filter "system:Windows"
		files(seq(backendMaterial_files))