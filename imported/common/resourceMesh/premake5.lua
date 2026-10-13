---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals resourceMesh

resourceMesh_location = '../../../temp/' .. _ACTION .. '/common/resourceMesh/'

-- files resourceMesh

resourceMesh_files = {
   ["include"] = {
		"include/mesh.h",
		"include/morphTargetMesh.h",
		"include/meshAppearance.h",
		"include/meshChunk.h",
		"include/meshDataViewRO.h",
		"include/meshMaterialBank.h",
		"include/meshMaterialBuffer.h",
		"include/meshPacker.h",
		"include/meshParamPhysics.h",
		"include/meshParamCompiledPhysics.h",
		"include/meshParamDestructionStepsData.h",
		"include/meshParamCloth.h",
		"include/meshParameter.h",
		"include/meshParamGpuBuffer.h",
		"include/meshParamImportedSnapPoint.h",
		"include/meshParamOccluderData.h",
		"include/meshParamWaterPatchData.h",
		"include/meshParamSpeedTreeWind.h",
		"include/meshParamWorkspotOffsets.h",
		"include/meshParamUICollisionData.h",
		"include/meshParamGarmentSupport.h",
		"include/meshParamTerrain.h",
		"include/meshUtils.h",
		"include/meshVertex.h",
		"include/mergedMesh.h",
		"include/navigationSetting.h",
		"include/physicsClothMeshUtils.h",
		"include/meshParamShadowMeshCreationData.h",
		"include/meshParamBendedRoad.h"
		},
		
	["src"] = {
		"src/materialSetupParamsProvider.h",
		"src/materialSetupParamsProvider.cpp",
		"src/mesh.cpp",
		"src/morphTargetMesh.cpp",
		"src/morphTargetMeshDiffBufferWriter.h",
		"src/meshAppearance.cpp",
		"src/meshChunk.cpp",
		"src/meshDataViewRO.cpp",
		"src/meshMaterialBank.cpp",
		"src/meshMaterialBuffer.cpp",
		"src/meshPacker.cpp",
		"src/meshParamPhysics.cpp",
		"src/meshParamCompiledPhysics.cpp",
		"src/meshParamDestructionStepsData.cpp",
		"src/meshParamCloth.cpp",
		"src/meshParameter.cpp",
		"src/meshParamGpuBuffer.cpp",
		"src/meshParamImportedSnapPoint.cpp",
		"src/meshParamOccluderData.cpp",
		"src/meshParamWaterPatchData.cpp",
		"src/meshParamSpeedTreeWind.cpp",
		"src/meshParamWorkspotOffsets.cpp",
		"src/meshParamUICollisionData.cpp",
		"src/meshParamGarmentSupport.cpp",
		"src/meshParamTerrain.cpp",
		"src/meshUtils.cpp",
		"src/meshVertex.cpp",
		"src/mergedMesh.cpp",
		"src/navigationSetting.cpp",
		"src/physicsClothMeshUtils.cpp",
		"src/meshParamShadowMeshCreationData.cpp",
		"src/meshParamBendedRoad.cpp"
		},

	[""] = {
		"src/build.h",
		"src/build.cpp",
		"src/resourceMeshInternal.h",
		"src/resourceMeshInit.cpp",
		"include/resourceMeshApi.h",
		"include/resourceMeshPublic.h"
	}
}

-- configs resourceMesh

project "resourceMesh"
    filter "platforms:not x64_DLL"
        kind "StaticLib"
    filter "platforms:x64_DLL"
        kind "SharedLib"
    filter {}

    language "C++"

    location(resourceMesh_location)

    pchheader "build.h"
    pchsource "src/build.cpp"

    includedirs { 
		"include",
		"../../../external/speedtree_v8/cpp_source",
		"../../../external/speedtree_v8/include/wind"
	}

    uses "resourceMaterial"
    links "physics"
	
	prebuildcommands {
		generateRtti()
	}

    vpaths(resourceMesh_files)

    files(seq(resourceMesh_files))
