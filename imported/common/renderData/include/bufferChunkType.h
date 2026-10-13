/**
* Copyright (c) 2016-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "gpuWrapApiVertexPacking.h"
#if defined( VANGUARD_ASSET_HOST )
#include "renderDataApi.h"
#else
#include "renderDataPublic.h"
#endif

namespace GpuWrapApi // acseparate temporary namespace name
{
	enum eIndexBufferChunkType : Uint8
	{
		IBCT_IndexUInt,
		IBCT_IndexUShort,

		IBCT_Max,
	};

	// WARNING: RenderMeshBlob is serializing the values of this enum casted to Uint8. When changing the enum values be sure to update RenderMeshBlob::UpgradeVersion()!
	enum eBufferChunkType : Uint32
	{
		// Vertex buffer types
		BCT_VertexBillboard,

		// Water instanced patches
		BCT_VertexWater,

		// New terrain
		BCT_VertexTerrain,
		BCT_VertexTerrainSkirt,

		// Instanced types		
		BCT_VertexMeshStaticPositionOnlyInstanced,
		BCT_VertexMeshStaticSimpleInstanced,
		BCT_VertexMeshStaticExtendedInstanced,
		BCT_VertexMeshSkinnedSimpleInstanced,
		BCT_VertexMeshSkinnedExtendedInstanced,
		BCT_VertexMeshExtSkinnedSimpleInstanced,
		BCT_VertexMeshExtSkinnedExtendedInstanced,
		BCT_VertexMeshGarmentSkinnedSimpleInstanced,
		BCT_VertexMeshGarmentSkinnedExtendedInstanced,
		BCT_VertexMeshExtGarmentSkinnedSimpleInstanced,
		BCT_VertexMeshExtGarmentSkinnedExtendedInstanced,
		BCT_VertexMeshSpeedTreeSimpleInstanced,
		BCT_VertexMeshSpeedTreeExtendedInstanced,

		// Runtime formats ( not cooked or anything )
		BCT_VertexSystemPos,
		BCT_VertexSystemPosNormal,
		BCT_VertexSystemPosColor,
		BCT_VertexSystemPosColorUV,
		BCT_VertexSystemPosColorFUV,
		BCT_VertexSystemSkinnedPosColorUV,
		BCT_VertexSystemPos2,
		BCT_VertexSystemPos2Color,
		BCT_VertexSystemPos2UVColor,
		BCT_VertexSystemPos2ColorColor,
		BCT_VertexSystemParticleStandard,
		BCT_VertexSystemParticleMotionBlur,
		BCT_VertexSystemParticleTrail,
		BCT_VertexSystemParticleFacingTrail_Beam,

		// Index buffer types
		BCT_IndexUInt,
		BCT_IndexUShort,

		// Scaleform
		BCT_VertexScaleformAColorAFactorPos,
		BCT_VertexScaleformAColorATCPos,
		BCT_VertexScaleformAColorATCPosVBatch,
		BCT_VertexScaleformAColorPosVBatch,
		BCT_VertexScaleformAFactorPos,
		BCT_VertexScaleformATCPos,
		BCT_VertexScaleformATCPosVBatch,
		BCT_VertexScaleformPos,
		BCT_VertexScaleformPosVBatch,

		// For generating Morphed Mesh data
		BCT_VertexMeshStaticMorphGenIn,
		BCT_VertexMeshSkinnedMorphGenIn,
		BCT_VertexMeshStaticControlMorphGenIn,
		BCT_VertexMeshSkinnedControlMorphGenIn,
		BCT_VertexMeshStaticMorphGenOut,
		BCT_VertexMeshSkinnedMorphGenOut,

		// Summary
		BCT_Max,
	};

} // end namespace GpuWrapApi

RTTI_DECLARE_ENUM_IN_NAMESPACE( eIndexBufferChunkType, GpuWrapApi );
