---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals backendResources

backendResources_location = '../../../temp/' .. _ACTION .. '/backend/backendResources'

-- files backendResources

backendResources_files = {
	[""] = {
		"include/backendResourcesApi.h",
		"include/backendResourcesPublic.h",
		"src/backendResourcesInternal.h",
		"src/build.h",
		"src/build.cpp",
		"src/backendResourcesInit.cpp",
	},
	["FBX"] = {
		"src/fbxCommon.h",
		"src/fbxMeshBuilder.h",
		"src/fbxCommon.cpp",
		"src/fbxMeshBuilder.cpp",
		"src/fbxMeshExporter.cpp",
		"src/fbxMeshImporter.cpp",
	},
	["Export"] = {
		"src/exportSVG.cpp",
		"src/exportPhysicsFromMesh.cpp"
	},
	["Import"] = {
		"src/importFont.h",
		"src/importFont.cpp",
		"src/importMeshSpeedTreeXML.cpp",
		"src/importSVGResource.cpp",
		"src/importTextureRegionSet.cpp",
		"src/importCurveSet.cpp",
		"include/importCurveSet.h",
		"include/importMeshSpeedTree.h",
	},
	["Import/SMD"] = {
		"src/importMeshSMD.cpp",
		"src/smdParser.cpp",
		"src/smdParser.h",
	},
	["Import/Re"] = {
		"src/importMeshREX.cpp",
		"src/importMeshREX_Chunks.h",
		"src/importMeshREX_Chunks.cpp",
		"src/importMeshREX_Cloth.h",
		"src/importMeshREX_Cloth.cpp",
		"src/importMeshREX_Common.h",
		"src/importMeshREX_Common.cpp",
		"src/importMeshREX_Materials.h",
		"src/importMeshREX_Materials.cpp",
		"src/importMeshREX_Physics.h",
		"src/importMeshREX_Physics.cpp",
		"src/importMeshREX_Skinning.h",
		"src/importMeshREX_Skinning.cpp",
		"src/importMeshREX_Utils.h",
		"src/importMeshREX_Utils.cpp",
	},
	["Factory"] = {
		"src/factoryFont.cpp",
		"src/factoryMesh.cpp",
		"src/factoryCurveSet.cpp",
		"src/factoryCurveResourceSet.cpp",
		"src/factory2dArray.cpp",
		"src/factoryCubeTexture.cpp",
		"src/factoryEnvDefinition.cpp",
		"src/factoryEnvParameters.cpp",
		"src/factoryGenericMaterial.cpp",
		"src/factorySkinProfile.cpp",
		"src/factoryHairProfile.cpp",
		"src/factoryGradient.cpp",
		"src/factoryFoliageProfile.cpp",
		"src/factoryTextureRegionSet.cpp",
		"src/factoryMaterialInstance.cpp",
		"src/factoryPrefab.cpp",
		"src/factoryTextureArray.cpp",
		"src/factoryWorld.cpp",
		"src/factoryBlockout.cpp",
		"src/factoryBitmapTexture.cpp",
		"src/factoryDynamicTexture.cpp",
		"src/factoryMaterialLayerLibrary.cpp",
		"src/factoryAppearanceResource.cpp",
		"src/factoryPhysicsMaterial.cpp",
		"src/factoryPhysicsDecoration.cpp",
		"src/factoryHotSpotLayerGroup.cpp",
		"src/factoryTweakDB.cpp",
		"src/factoryVoiceTags.cpp",
		"src/factoryDlcManifest.cpp"
	},
	["Commandlets"] = {
		"include/findResourceCommandlet.h",
		"include/resaveCommandlet.h",
		"src/animSetGenerateFootIKDataCommandlet.cpp",
		"src/animSetSimplePropsCommandlet.cpp",
		"src/animSetUserDataCopyCommandlet.cpp",
		"src/animSetPackAnimDataCommandlet.cpp",
		"src/checkOrphansCommandlet.cpp",
		"src/communityTemplateResaveCommandlet.cpp",
		"src/compileTweakDBCommandlet.cpp",
		"src/createCommandlet.cpp",
		"src/convertHeatmapCommandlet.cpp",
		"src/dumpNonproductionAssetsCommandlet.cpp",
		"src/exportCommandlet.cpp",
		"src/fileVerCommandlet.cpp",
		"src/findDuplicatedComponentsCommandlet.cpp",
		"src/findMeshesWithMaterialCommandlet.cpp",
		"src/findMeshesWithoutSnapPointsCommandlet.cpp",
		"src/findResourceCommandlet.cpp",
		"src/generateAnimationTagsCommandlet.cpp",
		"src/generateCollisionShapesCommandlet.cpp",
		"src/generateLiteOnePropsCommandlet.cpp",
		"src/generateStaticEntityCommandlet.cpp",
		"src/imageMetadataScanCommandlet.cpp",
		"src/importCommandlet.cpp",
		"src/instancedDestructibleMeshListCommandlet.cpp",
		"src/localizationStringExtractorCommandlet.cpp",
		"src/materialLibraryExportCommandlet.cpp",
		"src/meshAppearanceCensorshipCommandlet.cpp",
		"src/meshConsoleBiasResaveCommandlet.cpp",
		"src/meshPhysicsResaveCommandlet.cpp",
		"src/meshSkinningLODCommandlet.cpp",
		"src/moveTextureCommandlet.cpp",
		"src/multilayerMaskResaveCommandlet.cpp",
		"src/nameLookupCommandlet.cpp",
		"src/playtestCommandlet.cpp",
		"src/populateStreamingCacheCommandlet.cpp",
		"src/prefabLockerCommandlet.cpp",
		"src/prefabVariantListFixerCommandlet.cpp",
		"src/recomputeVertexEpsilonsCommandlet.cpp",
		"src/resaveCommandlet.cpp",
		"src/resourceDumpMemoryMetricsCommandlet.cpp",
		"src/resourceDumpMetadataCommandlet.cpp",
		"src/resourceHashMapperCommandlet.cpp",
		"src/roadResourcesCommandlet.cpp",
		"src/shadowFlagsCommandlet.cpp",
		"src/textureResaveCommandlet.cpp",
		"src/textureCommandlet.cpp",
		"src/validateCommandlet.cpp",
		"src/validateScriptsCommandlet.cpp",
		"src/validateTweakDBCommandlet.cpp",
		"src/vehicleAnimLodSetterCommandlet.cpp",
		"src/vehicleMorphResaveCommandlet.cpp",
		"src/visualTagsCommandlet.cpp",
		"src/widgetResourceResaveCommandlet.cpp",
		"src/workspotEntriesUpdaterCommandlet.cpp",
		"src/workspotResaveCommandlet.cpp",
		"src/workspotTagCommandlet.cpp",
		"src/workspotTagUpdateData.cpp",
		"src/proxyDependencyCommandlet.cpp",
	},
	["Commandlets/Archive"] = {
		"src/archiveDumpCommandlet.cpp",
		"src/archiveExtractCommandlet.cpp",
		"src/archiveVerifyCommandlet.cpp",
	},
	["Commandlets/Thumbnails"] = {
		"src/getThumbnailCommandlet.cpp",
		"src/updateThumbnailCommandlet.cpp",
	},
	["Commandlets/Depends"] = {
		"src/dependencyMap.cpp",
		"src/dependencyMap.h",
		"src/dependsCommandlet.cpp",
	},
	["Commandlets/ResourcesInfo"] = {
		"src/resourceCommandlet.cpp",
		"src/resourcesCommands.cpp",
		"src/resourcesCommands.h",
	},
	["Commandlets/Links"] = {
		"src/redundantLinksCommandlet.cpp",
		"src/relinkCommandlet.cpp",
		"src/showLinksCommandlet.cpp",
	},
	["Editors/Json"] = {
		"src/backendEditorJson.h",
		"src/backendEditorJson.cpp",
	},
	["Editors/Font"] = {
		"src/fontEditor.h",
		"src/fontEditor.cpp",
		"src/fontEditorPreviewPanel.h",
		"src/fontEditorPreviewPanel.cpp",
		"src/fontEditorPreviewPanelSharedData.h",
		"src/fontEditorPreviewPanelSharedData.cpp",
	},
	["Commands"] = {
		"include/addColorSetToProxyMeshCommand.h",
		"include/calculateGarmentMeshStreamsCommand.h",
		"include/convertSkinnedMeshComponentsToGarmentsCommand.h",
		"include/createResourceCommand.h",
		"include/generateBodyPartCommand.h",
		"include/generateComponentsIdsCommand.h",
		"include/generatePrefabsCommand.h",
		"include/generateVehiclePartCommand.h",
		"include/rebuildMorphTargetsCommand.h",
		"include/createOnePropEntityCommand.h",
		"include/removeLodCommand.h",
		"include/optimizeRoadMeshCommand.h",
		"include/clearDefaultInstancedParamsCommand.h",
		"include/exportPhysicsCommand.h",
		"src/addColorSetToProxyMeshCommand.cpp",
		"src/calculateGarmentMeshStreamsCommand.cpp",
		"src/convertSkinnedMeshComponentsToGarmentsCommand.cpp",
		"src/createResourceCommand.cpp",
		"src/generateBodyPartCommand.cpp",
		"src/generateComponentsIdsCommand.cpp",
		"src/generatePrefabsCommand.cpp",
		"src/generateVehiclePartCommand.cpp",
		"src/rebuildMorphTargetsCommand.cpp",
		"src/createOnePropEntityCommand.cpp",
		"src/removeLodCommand.cpp",
		"src/optimizeRoadMeshCommand.cpp",
		"src/clearDefaultInstancedParamsCommand.cpp",
		"src/exportPhysicsCommand.cpp"
	},
	["validation"] = {
		"src/resourceImportValidator.cpp",
		"src/binkValidator.cpp",
		"include/binkValidator.h",
	},
	["batchTool"] = {
		"src/meshLODsCustomDescriptor.cpp",
		"src/meshAppearancesCustomDescriptor.cpp",
		"src/textureCustomDescriptor.cpp",
		"src/entityBatchCustomDescriptors.cpp",
		"src/mlMaskCustomDescriptor.cpp",
	},
}

-- configs backendResources

project "backendResources"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(backendResources_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs { 
		"include", 
		"../../../external/fbxsdk/include/", 
		"../../../external/freetype/include",
		"../../../external/speedtree_v8/cpp_source",
		"../../../external/speedtree_v8/include/wind"
	}

	links { 
		"archive",	
	}

	uses {
		"backendAudio",
		"backendLocalization",
		"backendCharacter",
		"backendEntities",
		"backendMesh",
		"backendMaterial",
		"backendResources",
		"backendWorkspots",
		"backendData",
		"freetype",
		"fbxsdk",
		"houdini"
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(backendResources_files)

	filter "platforms:Durango or ORBIS"
		flags { "ExcludeFromBuild" }
	filter "system:Windows"
		files(seq(backendResources_files))


usage "backendResources" 	
	filter { "system:Windows", "kind:ConsoleApp or WindowedApp" }
		if hasDurangoSDKInstalled() then
			postbuildcommands '{COPY} "$(XboxOneXDKLatest)/xdk/FXC/amd64/D3DCompiler_47_xdk.dll" "%{cfg.targetdir}"'
			postbuildcommands '{COPY} "$(XboxOneXDKLatest)/xdk/FXC/amd64/SC_DLL.dll" "%{cfg.targetdir}"'
		end
		if hasOrbisSDKInstalled() then
			postbuildcommands {
				'{COPY} "$(SCE_ORBIS_SDK_DIR)/host_tools/bin/libSceGnm.dll" "%{cfg.targetdir}"',
				'{COPY} "$(SCE_ORBIS_SDK_DIR)/host_tools/bin/libSceGnmx.dll" "%{cfg.targetdir}"',
				'{COPY} "$(SCE_ORBIS_SDK_DIR)/host_tools/bin/libSceGpuAddress.dll" "%{cfg.targetdir}"'
			}
		end
