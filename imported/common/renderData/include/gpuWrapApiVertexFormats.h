/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "gpuWrapApiVertexPacking.h"
#include "vertexLayoutDesc.h"
#include "gpuWrapApiVertexDeclarations.h"

namespace GpuWrapApi
{
	/////////////////////////////////////////////////////////////////////////////////////////////////////////

	// Get vertex packing format for current platform
	RENDER_DATA_API const GpuWrapApi::VertexPacking::PackingElement* GetVertexPackingForFormat( GpuWrapApi::eBufferChunkType vertexFormat );

	/////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct SystemVertex_Pos
	{
		Float x, y, z;

		SystemVertex_Pos ()
		{}

		SystemVertex_Pos ( Float newX, Float newY, Float newZ )
			: x( newX ), y( newY ), z( newZ )
		{}

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemPos.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_PosColor : public SystemVertex_Pos
	{
		Uint32	color;

		SystemVertex_PosColor ()
		{}

		SystemVertex_PosColor ( Float newX, Float newY, Float newZ, Uint32 newColor )
			: SystemVertex_Pos ( newX, newY, newZ )
			, color( newColor )
		{}		

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemPosColor.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_PosColorFUV : public SystemVertex_Pos
	{
		Float	color[4];
		Float	u,v;

		SystemVertex_PosColorFUV()
		{}

		SystemVertex_PosColorFUV( Float newX, Float newY, Float newZ, const Float* newColor, Float newU, Float newV )
			: SystemVertex_Pos( newX, newY, newZ )
			, u( newU ), v( newV )
		{
			color[0] = newColor[0];
			color[1] = newColor[1];
			color[2] = newColor[2];
			color[3] = newColor[3];
		}

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemPosColorFUV.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_PosColorUV : public SystemVertex_PosColor
	{
		Float u,v;

		SystemVertex_PosColorUV ()
		{}

		SystemVertex_PosColorUV ( Float newX, Float newY, Float newZ, Uint32 newColor, Float newU, Float newV )
			: SystemVertex_PosColor( newX, newY, newZ, newColor )
			, u( newU ), v( newV )
		{}

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemPosColorUV.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_Terrain
	{
		Float m_patchCorner[2];
		/*Float m_patchSize;
		Float m_patchSizeNorm;
		Float m_patchUV[2];*/

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexTerrain.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_Water
	{
		Float m_position[3];

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexWater.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_ParticleStandard
	{		
		Float	m_position[3];
		Uint32	m_color;
		Float	m_rotation[2];
		Float	m_frame;
		Float	m_size[2];
		Float	m_uv[2];

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemParticleStandard.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_ParticleMotionBlur
	{		
		Float	m_position[3];
		Uint32	m_color;
		Float	m_rotation[2];
		Float	m_frame;
		Float	m_size[2];
		Float	m_uv[2];
		Float	m_direction[3];
		Float	m_stretch;
		Float	m_motionBlend;

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemParticleMotionBlur.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_ParticleTrail
	{		
		Float	m_position[3];
		Uint32	m_color;
		Float	m_tangent[3];
		Float	m_frame;
		Float	m_uv[2];

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemParticleTrail.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_ParticleFacingTrail_Beam
	{
		Float	m_position[3];
		Uint32	m_color;
		Float	m_prevPosition[3];
		Float	m_uv[2];
		Float	m_stretch;
		Float	m_frame;

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemParticleFacingTrail_Beam.GetVertexLayoutDesc();
		}
	};

	struct SystemVertex_ParticleTrail_Beam : public SystemVertex_ParticleFacingTrail_Beam
	{		
		Float	m_rotation;

		static const GpuWrapApi::VertexLayoutDesc& GetSystemVertexLayoutDesc()
		{
			return GpuWrapApi::VertexSystemParticleTrail_Beam.GetVertexLayoutDesc();
		}
	};

};
