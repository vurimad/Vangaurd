---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals backendMesh

backendMesh_location = '../../../temp/' .. _ACTION .. '/backend/backendMesh'

-- files backendMesh

backendMesh_files = {
	[""] = {
		"src/build.cpp",
		"src/backendMeshInit.cpp",
		"include/backendMeshApi.h",
		"include/backendMeshPublic.h",
		"src/build.h" },
	["editor"] = {
		"include/backendMeshEditor.h",
		"include/backendMeshPreviewPanel.h",
		"include/meshStructure.h",
		"include/meshReport.h",
		"include/backendMeshPanelSharedData.h",
		"include/meshEditModes.h",
		"include/physicsEditModeSimulation.h",
		"include/meshStructureNodes.h",
		"include/backendMeshPreviewPanelInputListener.h",
        "include/lodGenerationInterop.h",
		"src/backendMeshEditor.cpp",
		"src/backendMeshPreviewPanel.cpp",
		"src/meshStructure.cpp",
		"src/meshReport.cpp",
		"src/meshStructureNodes.cpp",
		"src/backendMeshPreviewPanelInputListener.cpp",
		"src/meshStructureVisualization.cpp",
		"src/meshStructureVisualization.h",
		"src/backendMeshPanelSharedData.cpp",
		"src/meshEditModes.cpp",
		"src/physicsEditModeSimulation.cpp",
        "src/lodGenerationInterop.cpp",
		"src/meshValidator.h",
		"src/meshValidator.cpp"
		},
	["editor/customControls"] = {
		"src/physicsBoneSelector.cpp", 
		"src/transformMatrixCustomEditor.cpp", 
		"include/physicsBoneSelector.h",
		"include/transformMatrixCustomEditor.h"
		 },
	["utils"] = {
		"include/meshPhysicsUtils.h",
		"include/meshOccluderCompilationSource.h",
		"include/worldMeshBuilder.h",
		"include/meshGluer.h",
		"include/meshBackendUtils.h",
		"include/backendMeshBrush.h",
		"include/lodGenerationUtils.h",
		"include/meshCookingArtifact.h",
		"src/meshCooking.cpp",
		"src/meshPhysicsUtils.cpp",
		"src/meshOccluderCompilationSource.cpp",
		"src/worldMeshBuilder.cpp",
		"src/meshGluer.cpp",
		"src/meshBackendUtils.cpp",
		"src/removeUnusedBonesCommandlet.cpp",
		"src/backendMeshBrush.cpp",
		"src/lodGenerationUtils.cpp",
		"include/meshOptimizer.h",
		"src/meshOptimizer.cpp",
		"src/meshCookingArtifact.cpp",
	},
	["view"] = {
		"include/meshDataView.h",
		"src/meshDataView.cpp",
		"include/meshDataBuilder.h",
		"src/meshDataBuilder.cpp",
	},
	["visualization"] = {
		"include/meshVisualization.h",
		"src/meshVisualization.cpp",
	},
	["materials"] = {
		"src/meshMaterialList.cpp",
		"include/meshMaterialList.h",
		"include/materialsListInterop.h",
		"include/appearancesListInterop.h",
		"include/chunkListInterop.h",
		"include/meshMultilayerMaterialProxy.h",
		"include/materialLayersLoader.h",
		"src/materialsListInterop.cpp",
		"src/appearancesListInterop.cpp",
		"src/chunkListInterop.cpp",
		"src/meshMultilayerMaterialProxy.cpp",
		"src/materialLayersLoader.cpp",
	},
	["packing"] = {
		"include/meshCompilationSource.h",
		"include/meshPacking.h",
		"include/meshRenderBlobUtils.h",
		"src/meshCompilationSource.cpp",
		"src/meshPacking.cpp",
		"src/meshRenderBlobUtils.cpp",
	},
	["physics"] = {
		"include/physicsEditorScene.h",
		"src/physicsEditorScene.cpp",
		"include/physicsBackendHelper.h",
		"src/physicsBackendHelper.cpp",
		"include/physicsShapeRenderer.h",
		"src/physicsShapeRenderer.cpp"
	},
	["physics/physicsWrappers"] = {
		"include/physicsActor.h",
		"include/physicsBase.h",
		"include/physicsShape.h",
		"include/physicsJoint.h",
		"include/physicsMesh.h",
		"src/physicsActor.cpp",
		"src/physicsBase.cpp",
		"src/physicsShape.cpp",
		"src/physicsJoint.cpp",
		"src/physicsMesh.cpp"
	},
	["physics/helpers"] = {
		"include/physicsEditableProxy.h",
		"include/physicsObjectResolver.h",
		"include/physicsObjectConstructor.h",
		"include/physicsObjectActionsHelper.h",
		"include/physicsObjectCreationInfo.h",
		"src/physicsEditableProxy.cpp",
		"src/physicsObjectActionsHelper.cpp",
		"src/physicsObjectResolver.cpp",
		"src/physicsObjectConstructor.cpp",
		"src/physicsObjectCreationInfo.cpp",
	},
	["cloth"] = {
		"src/backendMeshPreviewEditMode.h",
		"src/backendMeshPreviewEditMode_ClothPaint.h",
		"src/backendMeshPreviewEditMode_ClothPaint.cpp",
		"src/backendMeshPreviewEditMode_Simulation.h",
		"src/backendMeshPreviewEditMode_Simulation.cpp"
	},
	["morph"] = {
		"include/backendMorphTargetEditor.h",
		"src/backendMorphTargetEditor.cpp",
		"include/backendMorphTargetPreviewPanel.h",
		"src/backendMorphTargetPreviewPanel.cpp"
	},
	["commandlets"] = {
		"include/gatherResourceDataCommandletUtils.h",
		"src/generateLodsCommandlet.cpp",
		"src/gatherMeshDataCommandlet.cpp",
		"src/gatherMLMaskDataCommandlet.cpp",
		"src/gatherResourceDataCommandletUtils.cpp"
	},
	["shadowMesh"] = {
		"src/backendMeshPreviewEditMode_ShadowMesh.h",
		"src/backendMeshPreviewEditMode_ShadowMesh.cpp"
	},
	["destruction"] = {
		"src/backendMeshPreviewEditMode_Destruction.h",
		"src/backendMeshPreviewEditMode_Destruction.cpp"
	}
}

-- configs backendMesh

project "backendMesh"
	filter "platforms:not x64_DLL"
		kind "StaticLib"
	filter "platforms:x64_DLL"
		kind "SharedLib"
	filter {}

	language "C++"
	location(backendMesh_location)

	pchheader "build.h"
	pchsource "src/build.cpp"

	includedirs {
    	"include",
        "../../../external/PhysX342/PxShared/include",
        "../../../external/PhysX342/PhysX_3.4/Include",
		"../../../external/speedtree_v8/cpp_source",
		"../../../external/speedtree_v8/include/wind"
    }
	uses {
		"animation",
		"backendEngine",
		"renderBackend",
		"backendVisibility",
        "backendREA"
	}

	links { 
		"backendData",
		"backendMaterial"
	}

	prebuildcommands {
		generateRtti()
	}

	vpaths(backendMesh_files)

	filter "platforms:Durango or ORBIS"
		flags { "ExcludeFromBuild" }
	filter "system:Windows"
		files(seq(backendMesh_files))
	filter { "system:Windows", "configurations:Release"}
		buildoptions "/openmp"