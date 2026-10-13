////////////////////////////////////////////////////////////////////////////////////
// This file contains vertex declarations for all system vertex formats           //
////////////////////////////////////////////////////////////////////////////////////

// Do not include this file. Should only be needed by gpuWrapApiVertexFormats.cpp.
// Including it elsewhere results in compiler errors for these variables already
// being defined.

#pragma once

namespace GpuWrapApi
{
	// The slot numbers in these layouts should match the streams provided by CRenderMesh.
	// Except for the instance slot, which should match what CRenderGeometryBatcher sets.

	using namespace VertexPacking;

	struct RENDER_DATA_API VertexLayout
	{
		PackingElement*		m_packingElements;
		VertexLayoutDesc	m_vertexLayoutDesc;

		VertexLayout( PackingElement* elements )
		{
			m_packingElements = elements;
			m_vertexLayoutDesc = VertexLayoutDesc( m_packingElements );
		}

		RED_FORCE_INLINE const VertexLayoutDesc& GetVertexLayoutDesc() const { return m_vertexLayoutDesc; }
	};

	namespace RuntimeVertices
	{
		//////////////////////////////////////////////////////////////////////////
		// Debug

		static PackingElement DebugBase[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement DebugNormal[] =
		{
			{   PT_Float3,      PS_Position,    0,			0,  ST_PerVertex    },
			{   PT_Float3,      PS_Normal,      0,			0,  ST_PerVertex    },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement DebugColor[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement DebugColorUV[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement DebugColorFUV[] = 
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Float4,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SystemStaticMeshPositionOnlyInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};
	}

	static VertexLayout	VertexSystemPos( RuntimeVertices::DebugBase );
	static VertexLayout VertexSystemPosNormal( RuntimeVertices::DebugNormal );
	static VertexLayout VertexSystemPosColor( RuntimeVertices::DebugColor );
	static VertexLayout VertexSystemPosColorUV( RuntimeVertices::DebugColorUV );
	static VertexLayout VertexSystemPosColorFUV( RuntimeVertices::DebugColorFUV );
	static VertexLayout VertexSystemStaticMeshPositionOnlyInstanced( RuntimeVertices::SystemStaticMeshPositionOnlyInstanced );
	
	namespace ParticleVertices
	{
		static PackingElement ParticleStandard[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleMotionBlur[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	3,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	4,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	5,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleSphereAlign[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	3,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleTrail[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleTrail_Beam[] =
		{
			{ PT_Float3,		PS_Position,	0,			0,	ST_PerVertex },
			{ PT_Color,			PS_Color,		0,			0,	ST_PerVertex },
			{ PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex },
			{ PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex },
			{ PT_Float1,		PS_TexCoord,	2,			0,	ST_PerVertex },
			{ PT_Float1,		PS_TexCoord,	3,			0,	ST_PerVertex },
			{ PT_Float1,		PS_TexCoord,	4,			0,	ST_PerVertex },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleFacingTrail_Beam[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float3,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_TexCoord,	3,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ParticleBillboard[] =
		{
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Color,		PS_Color,		0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	1,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	2,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	3,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_TexCoord,	4,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};
	}

	static VertexLayout VertexSystemParticleBillboard( ParticleVertices::ParticleBillboard );
	static VertexLayout VertexSystemParticleStandard( ParticleVertices::ParticleStandard );
	static VertexLayout VertexSystemParticleMotionBlur( ParticleVertices::ParticleMotionBlur );
	static VertexLayout VertexSystemParticleTrail( ParticleVertices::ParticleTrail );
	static VertexLayout VertexSystemParticleTrail_Beam( ParticleVertices::ParticleTrail_Beam );
	static VertexLayout VertexSystemParticleFacingTrail_Beam( ParticleVertices::ParticleFacingTrail_Beam );

	namespace RuntimeVertices_DEPRECATED
	{
		static PackingElement Terrain[] =
		{
			//	Type			Usage			UsageIndex	Slot	SlotType
			{	PT_Float2,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Float1,		PS_PatchSize,	0,			1,	ST_PerInstance	},
			{	PT_Float3,		PS_PatchBias,	0,			1,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement Water[] =
		{
			//	Type			Usage			UsageIndex	Slot	SlotType
			{	PT_Float3,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Float2,		PS_PatchOffset,	0,			1,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement TerrainSkirt[] =
		{
			{	PT_Float2,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Float4,		PS_PatchBias,	0,			1,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};
	}

	static VertexLayout VertexWater( RuntimeVertices_DEPRECATED::Water );
	static VertexLayout VertexTerrain( RuntimeVertices_DEPRECATED::Terrain );
	static VertexLayout VertexTerrainSkirt( RuntimeVertices_DEPRECATED::TerrainSkirt );

	namespace CookedVertices_DEPRECATED
	{
		static PackingElement StaticMeshPositionOnlyInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement StaticMeshSimpleInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement StaticMeshExtendedInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Color,		PS_Color,				0,	3,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SkinnedMeshSimpleInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			0,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			{	PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SkinnedMeshExtendedInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			0,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Color,		PS_Color,				0,	3,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			{	PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ExtSkinnedMeshSimpleInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			1,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			0,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			1,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			{	PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ExtSkinnedMeshExtendedInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			0,	0,	ST_PerVertex	},
			{	PT_UByte4,		PS_SkinIndices,			1,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			0,	0,	ST_PerVertex	},
			{	PT_UByte4N,		PS_SkinWeights,			1,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Color,		PS_Color,				0,	3,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex	},
			{	PT_UShort2,		PS_DestructionIndices,	0,	4,	ST_PerVertex	},
			{	PT_Float1,		PS_MultilayerPaint,		0,	4,	ST_PerVertex	}, // obsolete, unused
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			{	PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement GarmentSkinnedMeshSimpleInstanced[] =
		{
			{ PT_Short4N,	PS_Position,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			0,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			0,	0,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex },
			{ PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex },
			{ PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex },
			{ PT_UShort2,	PS_DestructionIndices,	0,	4,	ST_PerVertex },
			{ PT_Float1,	PS_MultilayerPaint,		0,	4,	ST_PerVertex }, // obsolete, unused
			{ PT_Float16_4,	PS_ExtraData,			0,	0,	ST_PerVertex },
			{ PT_Float4,	PS_InstanceTransform,	0,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	1,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	2,	7,	ST_PerInstance },
			{ PT_UInt4,	PS_InstanceSkinningData,0,	7,	ST_PerInstance },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement GarmentSkinnedMeshExtendedInstanced[] =
		{
			{ PT_Short4N,	PS_Position,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			0,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			0,	0,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex },
			{ PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex },
			{ PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex },
			{ PT_Color,		PS_Color,				0,	3,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex },
			{ PT_UShort2,	PS_DestructionIndices,	0,	4,	ST_PerVertex },
			{ PT_Float1,	PS_MultilayerPaint,		0,	4,	ST_PerVertex }, // obsolete, unused
			{ PT_Float16_4,	PS_ExtraData,			0,	0,	ST_PerVertex },
			{ PT_Float4,	PS_InstanceTransform,	0,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	1,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	2,	7,	ST_PerInstance },
			{ PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ExtGarmentSkinnedMeshSimpleInstanced[] =
		{
			{ PT_Short4N,	PS_Position,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			1,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			0,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			1,	0,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex },
			{ PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex },
			{ PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex },
			{ PT_UShort2,	PS_DestructionIndices,	0,	4,	ST_PerVertex },
			{ PT_Float1,	PS_MultilayerPaint,		0,	4,	ST_PerVertex }, // obsolete, unused
			{ PT_Float16_4,	PS_ExtraData,			0,	0,	ST_PerVertex },
			{ PT_Float4,	PS_InstanceTransform,	0,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	1,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	2,	7,	ST_PerInstance },
			{ PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement ExtGarmentSkinnedMeshExtendedInstanced[] =
		{
			{ PT_Short4N,	PS_Position,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			0,	0,	ST_PerVertex },
			{ PT_UByte4,	PS_SkinIndices,			1,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			0,	0,	ST_PerVertex },
			{ PT_UByte4N,	PS_SkinWeights,			1,	0,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex },
			{ PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex },
			{ PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex },
			{ PT_Color,		PS_Color,				0,	3,	ST_PerVertex },
			{ PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex },
			{ PT_UShort2,	PS_DestructionIndices,	0,	4,	ST_PerVertex },
			{ PT_Float1,	PS_MultilayerPaint,		0,	4,	ST_PerVertex }, // obsolete, unused
			{ PT_Float16_4,	PS_ExtraData,			0,	0,	ST_PerVertex },
			{ PT_Float4,	PS_InstanceTransform,	0,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	1,	7,	ST_PerInstance },
			{ PT_Float4,	PS_InstanceTransform,	2,	7,	ST_PerInstance },
			{ PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance },
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SpeedTreeMeshSimpleInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SpeedTreeMeshExtendedInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Color,		PS_Color,				0,	3,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			1,	3,	ST_PerVertex	},
			{	PT_Float16_4,	PS_ExtraData,			0,  4,  ST_PerVertex	},
			{	PT_Float16_4,	PS_ExtraData,			1,  4,  ST_PerVertex	},
			{	PT_Float16_4,	PS_ExtraData,			2,  4,  ST_PerVertex	},
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement SkinnedMeshSingleBoneInstanced[] =
		{
			{	PT_Short4N,		PS_Position,			0,	0,	ST_PerVertex	},
			{	PT_UInt1,		PS_BoneIndex,			0,	0,	ST_PerVertex	},
			{	PT_Float16_2,	PS_TexCoord,			0,	1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,				0,	2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,				0,	2,	ST_PerVertex	},
			{	PT_Float4,		PS_InstanceTransform,	0,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	1,	7,	ST_PerInstance	},
			{	PT_Float4,		PS_InstanceTransform,	2,	7,	ST_PerInstance	},
			{	PT_UInt4,		PS_InstanceSkinningData,0,	7,	ST_PerInstance	},
			PackingElement::END_OF_ELEMENTS
		};
	}

	static VertexLayout VertexMeshStaticSimpleInstanced( CookedVertices_DEPRECATED::StaticMeshSimpleInstanced );
	static VertexLayout VertexMeshStaticExtendedInstanced( CookedVertices_DEPRECATED::StaticMeshExtendedInstanced );
	static VertexLayout VertexMeshStaticPositionOnlyInstanced( CookedVertices_DEPRECATED::StaticMeshPositionOnlyInstanced );
	static VertexLayout VertexMeshSkinnedSimpleInstanced( CookedVertices_DEPRECATED::SkinnedMeshSimpleInstanced );
	static VertexLayout VertexMeshSkinnedExtendedInstanced( CookedVertices_DEPRECATED::SkinnedMeshExtendedInstanced );
	static VertexLayout VertexMeshExtSkinnedSimpleInstanced( CookedVertices_DEPRECATED::ExtSkinnedMeshSimpleInstanced );
	static VertexLayout VertexMeshExtSkinnedExtendedInstanced( CookedVertices_DEPRECATED::ExtSkinnedMeshExtendedInstanced );
	static VertexLayout VertexMeshGarmentSkinnedSimpleInstanced( CookedVertices_DEPRECATED::GarmentSkinnedMeshSimpleInstanced );
	static VertexLayout VertexMeshGarmentSkinnedExtendedInstanced( CookedVertices_DEPRECATED::GarmentSkinnedMeshExtendedInstanced );
	static VertexLayout VertexMeshExtGarmentSkinnedSimpleInstanced( CookedVertices_DEPRECATED::ExtGarmentSkinnedMeshSimpleInstanced );
	static VertexLayout VertexMeshExtGarmentSkinnedExtendedInstanced( CookedVertices_DEPRECATED::ExtGarmentSkinnedMeshExtendedInstanced );
	static VertexLayout VertexMeshSpeedTreeSimpleInstanced( CookedVertices_DEPRECATED::SpeedTreeMeshSimpleInstanced );
	static VertexLayout VertexMeshSpeedTreeExtendedInstanced( CookedVertices_DEPRECATED::SpeedTreeMeshExtendedInstanced );
	static VertexLayout VertexMeshSkinnedSingleBoneInstanced( CookedVertices_DEPRECATED::SkinnedMeshSingleBoneInstanced );

	namespace MorphTargetVertices
	{
		using namespace GpuWrapApi::VertexPacking;

		static PackingElement MorphTargetDifference[] =
		{
			{	PT_Dec4,	PS_PositionDelta,	0,			0,	ST_PerVertex	},
			{	PT_Dec4,	PS_Normal,			0,			0,	ST_PerVertex	},
			{	PT_Dec4,	PS_Tangent,			0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};
	}

	static VertexLayout VertexMorphTargetDifference( MorphTargetVertices::MorphTargetDifference );

	namespace MorphGenVertices
	{
		using namespace GpuWrapApi::VertexPacking;

		// Input layouts must match regular mesh layouts above.
		// Output layouts must be compatible with the mesh layouts as well (same size, data written from SO shader must be readable
		// by a mesh layout).
		
		static PackingElement InStaticMesh[] =
		{
			{	PT_Short4N,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		0,			1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		0,			1,	ST_PerVertex	},

			{	PT_Short4N,		PS_Position,	1,			2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		1,			3,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		1,			3,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement InStaticMeshControl[] =
		{
			{	PT_Short4N,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		0,			1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		0,			1,	ST_PerVertex	},

			{	PT_Short4N,		PS_Position,	1,			2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		1,			3,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		1,			3,	ST_PerVertex	},

			{	PT_Float16_2,	PS_TexCoord,	0,			4,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement OutStaticMesh[] =
		{
			// HACK : HLSL Doesn't seem to like outputting other than floats for Position, so use TexCoord instead...
			{	PT_UInt2,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			// Normal and tangent packed together into one element. In theory we should be able to output one element to each output
			// buffer, so this would let us do that... doesn't seem to work on xbox...
			{	PT_UInt2,		PS_Normal,		0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement InSkinnedMesh[] =
		{
			{	PT_Short4N,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinIndices,	0,			0,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinWeights,	0,			0,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		0,			1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		0,			1,	ST_PerVertex	},

			{	PT_Short4N,		PS_Position,	1,			2,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinIndices,	1,			2,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinWeights,	1,			2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		1,			3,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		1,			3,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement InSkinnedMeshControl[] =
		{
			{	PT_Short4N,		PS_Position,	0,			0,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinIndices,	0,			0,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinWeights,	0,			0,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		0,			1,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		0,			1,	ST_PerVertex	},

			{	PT_Short4N,		PS_Position,	1,			2,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinIndices,	1,			2,	ST_PerVertex	},
			{	PT_UInt1,		PS_SkinWeights,	1,			2,	ST_PerVertex	},
			{	PT_Dec4,		PS_Normal,		1,			3,	ST_PerVertex	},
			{	PT_Dec4,		PS_Tangent,		1,			3,	ST_PerVertex	},
			
			{	PT_Float16_2,	PS_TexCoord,	0,			4,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};

		static PackingElement OutSkinnedMesh[] =
		{
			// HACK : HLSL Doesn't seem to like outputting other than floats for Position, so use TexCoord instead...
			// Position packed together with skinning data, rather than splitting it apart.
			{	PT_UInt4,		PS_TexCoord,	0,			0,	ST_PerVertex	},
			// Again, normal and tangent together.
			{	PT_UInt2,		PS_Normal,		0,			0,	ST_PerVertex	},
			PackingElement::END_OF_ELEMENTS
		};
	}

};
