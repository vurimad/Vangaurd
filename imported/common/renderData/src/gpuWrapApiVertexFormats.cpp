/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gpuWrapApiVertexFormats.h"
#include "gpuWrapApiVertexDeclarations.h"
#include "bufferChunkType.h"

// Get vertex packing format for current platform
namespace GpuWrapApi
{	
	const GpuWrapApi::VertexPacking::PackingElement* GetVertexPackingForFormat( GpuWrapApi::eBufferChunkType vertexFormat )
	{
		static_assert( BCT_Max == 48, "Value of eBufferChunkType enum changed, make sure to update this if needed" );
		switch ( vertexFormat )
		{
			// Runtime formats
		case GpuWrapApi::BCT_VertexSystemPos:								return RuntimeVertices::DebugBase;
		case GpuWrapApi::BCT_VertexSystemPosNormal:							return RuntimeVertices::DebugNormal;
		case GpuWrapApi::BCT_VertexSystemPosColor:							return RuntimeVertices::DebugColor;
		case GpuWrapApi::BCT_VertexSystemPosColorUV:						return RuntimeVertices::DebugColorUV;

		//case GpuWrapApi::BCT_VertexSystemPos2:								return RuntimeVertices::SystemPos2;
		//case GpuWrapApi::BCT_VertexSystemPos2Color:							return RuntimeVertices::SystemPos2Color;
		//case GpuWrapApi::BCT_VertexSystemPos2UVColor:						return RuntimeVertices::SystemPos2UVColor;
		//case GpuWrapApi::BCT_VertexSystemPos2ColorColor:					return RuntimeVertices::SystemPos2ColorColor;
		case GpuWrapApi::BCT_VertexWater:									return RuntimeVertices_DEPRECATED::Water;		
		case GpuWrapApi::BCT_VertexTerrain:									return RuntimeVertices_DEPRECATED::Terrain;
		case GpuWrapApi::BCT_VertexTerrainSkirt:							return RuntimeVertices_DEPRECATED::TerrainSkirt;

			// Particle formats
		case GpuWrapApi::BCT_VertexBillboard:								return ParticleVertices::ParticleBillboard;
		case GpuWrapApi::BCT_VertexSystemParticleStandard:					return ParticleVertices::ParticleStandard;
		case GpuWrapApi::BCT_VertexSystemParticleMotionBlur:				return ParticleVertices::ParticleMotionBlur;
		case GpuWrapApi::BCT_VertexSystemParticleTrail:						return ParticleVertices::ParticleTrail;
		case GpuWrapApi::BCT_VertexSystemParticleFacingTrail_Beam:			return ParticleVertices::ParticleFacingTrail_Beam;

			// Scaleform
		case GpuWrapApi::BCT_VertexScaleformAColorAFactorPos:
		case GpuWrapApi::BCT_VertexScaleformAColorATCPos:
		case GpuWrapApi::BCT_VertexScaleformAColorATCPosVBatch:
		case GpuWrapApi::BCT_VertexScaleformAColorPosVBatch:
		case GpuWrapApi::BCT_VertexScaleformAFactorPos:
		case GpuWrapApi::BCT_VertexScaleformATCPos:
		case GpuWrapApi::BCT_VertexScaleformATCPosVBatch:
		case GpuWrapApi::BCT_VertexScaleformPos:
		case GpuWrapApi::BCT_VertexScaleformPosVBatch:
		{
			RED_ASSERT( false, "Unsupported Vertex Type!" );
			return nullptr;
		}

			// Cooked formats
		case GpuWrapApi::BCT_VertexMeshStaticSimpleInstanced:				return CookedVertices_DEPRECATED::StaticMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshStaticExtendedInstanced:				return CookedVertices_DEPRECATED::StaticMeshExtendedInstanced;
		case GpuWrapApi::BCT_VertexMeshStaticPositionOnlyInstanced:			return CookedVertices_DEPRECATED::StaticMeshPositionOnlyInstanced;
		case GpuWrapApi::BCT_VertexMeshSkinnedSimpleInstanced:				return CookedVertices_DEPRECATED::SkinnedMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshSkinnedExtendedInstanced:			return CookedVertices_DEPRECATED::SkinnedMeshExtendedInstanced;
		case GpuWrapApi::BCT_VertexMeshExtSkinnedSimpleInstanced:			return CookedVertices_DEPRECATED::ExtSkinnedMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshExtSkinnedExtendedInstanced:			return CookedVertices_DEPRECATED::ExtSkinnedMeshExtendedInstanced;
		case GpuWrapApi::BCT_VertexMeshGarmentSkinnedSimpleInstanced:		return CookedVertices_DEPRECATED::GarmentSkinnedMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshGarmentSkinnedExtendedInstanced:		return CookedVertices_DEPRECATED::GarmentSkinnedMeshExtendedInstanced;
		case GpuWrapApi::BCT_VertexMeshExtGarmentSkinnedSimpleInstanced:	return CookedVertices_DEPRECATED::ExtGarmentSkinnedMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshExtGarmentSkinnedExtendedInstanced:	return CookedVertices_DEPRECATED::ExtGarmentSkinnedMeshExtendedInstanced;
		case GpuWrapApi::BCT_VertexMeshSpeedTreeSimpleInstanced:			return CookedVertices_DEPRECATED::SpeedTreeMeshSimpleInstanced;
		case GpuWrapApi::BCT_VertexMeshSpeedTreeExtendedInstanced:			return CookedVertices_DEPRECATED::SpeedTreeMeshExtendedInstanced;

		case GpuWrapApi::BCT_VertexMeshStaticMorphGenIn:					return MorphGenVertices::InStaticMesh;
		case GpuWrapApi::BCT_VertexMeshSkinnedMorphGenIn:					return MorphGenVertices::InSkinnedMesh;
		case GpuWrapApi::BCT_VertexMeshStaticControlMorphGenIn:				return MorphGenVertices::InStaticMeshControl;
		case GpuWrapApi::BCT_VertexMeshSkinnedControlMorphGenIn:			return MorphGenVertices::InSkinnedMeshControl;
		case GpuWrapApi::BCT_VertexMeshStaticMorphGenOut:					return MorphGenVertices::OutStaticMesh;
		case GpuWrapApi::BCT_VertexMeshSkinnedMorphGenOut:					return MorphGenVertices::OutSkinnedMesh;

			// Not handled
		case GpuWrapApi::BCT_IndexUInt:
		case GpuWrapApi::BCT_IndexUShort:
			RED_ASSERT( !"index type buffers don't have vertex packing" );
			break;

		default:
			RED_ASSERT( !"unsupported buffer chunk type" );
		}

		// No packing
		return nullptr;
	}
}
