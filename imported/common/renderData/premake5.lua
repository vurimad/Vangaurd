---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals renderData

renderData_location = '../../../temp/' .. _ACTION .. '/common/renderData'

-- files renderData

renderData_files = {
	[""] = {
		"src/renderData.natvis",
		"src/build.cpp",
		"src/renderDataInit.cpp",
		"src/renderDataPublic.cpp",
		"src/renderMaterialSetupParam.cpp",
		"include/renderDataPublic.h",
		"src/build.h",
		"src/renderDataInternal.h",
		"include/renderStats.h",
		"src/renderStats.cpp",
		"include/renderMaterialSetupParam.h",
		"include/materialLayerParams.h",
		"include/renderDataApi.h",
		"include/particleDrawerData.h",
		"src/renderParticleEmitterLOD.cpp",
		"include/renderParticleEmitterLOD.h",
		"include/renderParticleUpdaterData.h",
		"src/renderParticleUpdaterData.cpp",
		"include/renderUtils.h",
		"include/resolutionParams.h",
		"src/resolutionParams.cpp",
		"include/dynamicDecal.h",
		"src/dynamicDecal.cpp",
		"include/decalParams.h",
		"src/decalParams.cpp",
		"include/captureHighResolutionScreenshot.h",
		"src/captureHighResolutionScreenshot.cpp",
		"include/deviceNotifier.h",
		"src/rendererDevConfig.cpp",
		"include/rendererDevConfig.h",
		"src/renderLightBase.cpp",
		"include/renderLightBase.h",
		"src/renderFeatureFlags.cpp",
		"include/renderFeatureFlags.h",
		"include/renderWorldTransform.h",
		},
	["Memory"] = {
		"include/gpuApiMemory.h",
		"src/gpuApiMemory.cpp",
		"include/renderingMemory.h",
		"src/renderingMemory.cpp",
		},
	["Drawing"] = {
		"include/renderDrawCommon.h",
		"include/renderDrawBuffer.h",
		"src/renderDrawBuffer.cpp",
		"include/drawer.h",
		"src/drawer.cpp",
		},
	["Drawing/Fonts"] = {
		"include/font.h",
		"include/fontStyle.h",
		"src/font.cpp",
		},
	["gameplayEffects"] = {
		"include/gameplayEffects.h",
		"src/gameplayEffects.cpp",
		"include/renderPostFx.h",
		"src/renderPostFx.cpp",
		},
	["RenderResource"] = {
		"include/renderDynamicTextureDesc.h",
		"src/renderDynamicTextureDesc.cpp",
		"include/renderResource.h",
		"src/renderResource.cpp",
		"include/renderResourceBlob.h",
		"src/renderResourceBlob.cpp",
		"include/renderSkinData.h",
		"src/renderSkinData.cpp",
		"include/renderHairData.h",
		"src/renderHairData.cpp",
		"include/renderGradientEntryData.h",
		"src/renderGradientEntryData.cpp",
		"include/renderGradientData.h",
		"include/renderFoliageData.h",
		"src/renderFoliageData.cpp",
		"include/renderMultilayer.h",
		"src/renderMultilayer.cpp",
		"include/distantLightResource.h",
		"src/distantLightResource.cpp",
		"include/iesResource.h",
		"src/iesResource.cpp",
		"include/timeOfDayVisibilityResource.h",
		"src/timeOfDayVisibilityResource.cpp",
		"include/renderIndexBuffer.h",
		"include/renderTerrainSetupData.h",
		},
	["RenderResource/Blobs"] = {
		"include/renderMeshBlob.h",
		"src/renderMeshBlob.cpp",
		"include/renderMorphTargetMeshBlob.h",
		"src/renderMorphTargetMeshBlob.cpp",
		"include/renderTextureBlob.h",
		"src/renderTextureBlob.cpp",
		"include/renderParticleBlob.h",
		"src/renderParticleBlob.cpp",
		"include/renderMultilayerMaskBlob.h",
		"src/renderMultilayerMaskBlob.cpp",
		},
	["shaderParsing"] = {
		"include/shaderCompilerLogger.h",
		"src/shaderCompilerLogger.cpp",
		},
	["fromEngine"] = {
		"src/renderObject.cpp",
		"src/renderProxy.cpp",
		"src/renderSettingsVars.cpp",
		"src/renderInGameSettings.cpp",
		"src/effectContextUpdateData.cpp",
		"src/textureEnums.cpp",
		"src/textureGroup.cpp",
		"src/meshEnum.cpp",
		"src/renderCamera.cpp",
		"src/renderWeaponPlaneParams.cpp",
		"src/renderSelectable.cpp",
		"src/renderSelectionResult.cpp",
		"src/viewport.cpp",
		"src/renderFrameInfo.cpp",
		"src/renderer.cpp",
		"src/particleEmitterEnum.cpp",
		"src/speedTreeWind.cpp",
		"src/speedTreeV8Sdk.cpp",
		"src/materialStructures.cpp",
		"src/materialFeatureFlags.cpp",
		"include/speedTreeWind.h",
		"include/speedTreeV8Sdk.h",
		"include/particleEmitterEnum.h",
		"include/effectContextUpdateData.h",
		"include/renderObject.h",
		"include/renderObjectPtr.h",
		"include/renderProxy.h",
		"include/renderProxyIsolation.h",
		"include/renderEnvProbeProperties.h",
		"include/renderSettingsVars.h",
		"include/renderInGameSettings.h",
		"include/renderQualityLevels.h",
		"include/textureEnums.h",
		"include/textureGroup.h",
		"include/meshEnum.h",
		"include/morphTargetCommon.h",
		"src/morphTargetCommon.cpp",
		"include/imageRegionDetector.h",
		"src/imageRegionDetector.cpp",
		"include/rectanglePacker.h",
		"src/rectanglePacker.cpp",
		"include/miniVec.h",
		"include/renderCamera.h",
		"include/renderWeaponPlaneParams.h",
		"include/terrainTypes.h",
		"include/terrainUtils.h",
		"include/renderSkinningData.h",
		"include/renderVertices.h",
		"include/renderCommandHandler.h",
		"include/renderSelectable.h",
		"include/renderSelectionResult.h",
		"include/viewport.h",
		"include/viewportCreator.h",
		"include/renderFrameInfo.h",
		"include/renderFrame.h",
		"include/renderer.h",
		"include/materialStructures.h",
		"include/materialFeatureFlags.h",
		"include/materialModifier.h",
		"src/materialModifier.cpp",
		"include/renderEngineHandler.h",
		"src/renderScene.cpp",
		"include/renderScene.h",
		"include/renderObjectPtr.inl",
		"include/materialShaderConstants.h",
		"include/renderVideo.h",
		"include/renderDismemberment.h",
		"src/renderDismemberment.cpp",
		"include/interiorMapRenderData.h",
		"src/interiorMapRenderData.cpp"
		},
	["fromGpuApi"] = {
		"src/renderSimpleTypes.cpp",
		"src/gpuWrapApiVertexPacking.cpp",
		"src/gpuWrapApiVertexFormats.cpp",
		"src/imageProcessing.cpp",
		"src/vertexLayoutDesc.cpp",
		"include/debugStats.h",
		"include/renderSimpleTypes.h",
		"include/gpuWrapApiVertexPacking.h",
		"include/bufferChunkType.h",
		"include/gpuWrapApiVertexDeclarations.h",
		"include/gpuWrapApiVertexFormats.h",
		"include/imageProcessing.h",
		"include/vertexLayoutDesc.h" },
	["parameters"] = {
		"src/cameraDofParams.cpp",
		"src/envParameters.cpp",
		"src/windParameters.cpp",
		"src/environmentGameParams.cpp",
		"src/worldShadowConfig.cpp",
		"src/screenshotParameters.cpp",
		"src/dynamicDecal.cpp",
		"src/vertexShaderCustomSlotParams.cpp",
		"src/tonemappingModes.cpp",
		"include/cameraDofParams.h",		
		"include/envParameters.h",
		"include/windParameters.h",
		"include/environmentGameParams.h",
		"include/worldShadowConfig.h",
		"include/foliageRenderSettings.h",
		"include/screenshotParameters.h",
		"include/dynamicDecal.h",
		"include/vertexShaderCustomSlotParams.h",
		"include/tonemappingModes.h" },
	["grouped"] = {
		"src/renderPublicEnums.cpp",
		"src/renderProxyInitData.cpp",
		"src/renderPublicFunctions.cpp",
		"src/envProbeResource.cpp",
		"include/renderPublicEnums.h",
		"include/renderPublicTypes.h",
		"include/renderGlobalBindings.h",
		"include/renderProxyInitData.h",
		"include/renderPublicFunctions.h",
		"include/envProbeResource.h" },
	["fromRenderer"] = {
		"src/renderViewportWindowInterface.cpp",
		"include/particleLayouts.h",
		"include/speedTreeDefines.h",
		"include/renderViewportWindowInterface.h",
		"include/renderMaterialHelpers.h",
		"include/gpuDataProcessor.h",
		"include/renderWindImpulseShape.h",
		"src/renderWindImpulseShape.cpp"		
		},
	["Filters"] = {
		"src/debugFilter.cpp",
		"include/debugFilter.h",
		"src/debugFilterMask.cpp",
		"include/debugFilterMask.h",
		"include/renderFiltersDeclaration.h",
		"include/renderFilters.h",
		"src/renderFilters.cpp",
		},
	["materials"] = {
		"include/materialDataProvider.h",
		"include/pipelineStateObject.h",
		"src/pipelineStateObject.cpp",
		"include/renderStageContext.h",
 		},
	["visibility"] = {
		"include/visibilityOccluderInstance.h",
		"include/visibilityOccluderMeshResource.h",
		"include/visibilityOccluderResource.h",
		"src/visibilityOccluderMeshResource.cpp",
		"src/visibilityOccluderResource.cpp",
		"src/visibilityOccluderInstance.cpp",
		"include/visibilityOccluderVisualizer.h",
		"src/visibilityOccluderVisualizer.cpp",
		"include/visibilityWorldCommon.h",
		"src/visibilityWorldCommon.cpp",
		},
	["capture"] = {
		"include/captureParameters.h",
		"src/captureParameters.cpp"
		}
	}

-- configs renderData

project "renderData"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(renderData_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs {
		"include",
		"src",
		"../../../external/freetype/include",
		"../../../external/speedtree_v8/cpp_source",
		"../../../external/speedtree_v8/include/wind"
	}

	uses {
		"directXTex",
		"freetype",
		"redImage",
		"imgui",
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(renderData_files)

	files(seq(renderData_files))
