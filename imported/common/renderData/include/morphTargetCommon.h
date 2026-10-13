/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "renderSimpleTypes.h"

//---------------------------------------------------------------

namespace MorphTargets
{
	typedef Uint8 TMorphTargetTargetIndex;
	static constexpr TMorphTargetTargetIndex MORPH_TARGET_INVALID_TARGET = std::numeric_limits<TMorphTargetTargetIndex>::max();

	struct MorphTargetWeight
	{
		TMorphTargetTargetIndex	m_targetIndex;
		Float					m_weight;
	};

	struct MorphTargetID
	{
		CName	m_name;
		CName	m_regionName;
	};

	typedef red::DynArray< MorphTargetID > MorphTargetIDSet;

	enum BlendedTextureType
	{
		BTT_Normal		= 0,
		BTT_Roughness	= 1,
		BTT_USED_MAX	= BTT_Roughness,
		BTT_Diffuse		= 2,
		BTT_MAX			= 3,
	};

	enum DiffTextureSize : Uint8
	{
		TEXTURE_SIZE_1024x1024 = 0,
		TEXTURE_SIZE_512x512 = 1,
		TEXTURE_SIZE_256x256 = 2,
	};

	static Uint32 GetMaxDiffWidth( const DiffTextureSize textureSize )
	{
		switch ( textureSize )
		{
		case TEXTURE_SIZE_1024x1024:	return 1024;
		case TEXTURE_SIZE_512x512:		return 512;
		case TEXTURE_SIZE_256x256:		return 256;
		default:
			RED_ASSERT( false, "Invalid textureSize" );
			return 0;
		}
	}

	// MUST MATCH FacialSetup_FaceRegion in facialSetup.h
	enum FaceRegion : Uint8
	{
		FACE_REGION_EYES = 0,
		FACE_REGION_NOSE = 1,
		FACE_REGION_MOUTH = 2,
		FACE_REGION_JAW = 3,
		FACE_REGION_EARS = 4,
		FACE_REGION_NONE = 255
	};
	constexpr Uint32 c_FaceRegionsCount = static_cast< Uint32 >( FACE_REGION_EARS ) + 2;

	typedef red::DynArray< CName > JointNames;

	class RENDER_DATA_API JointsForFaceRegions
	{
	public:
		JointsForFaceRegions( const red::memory::Pool& pool );
		~JointsForFaceRegions();

		Bool Exist( Uint8 faceRegion ) const
		{
			return GetIndex( faceRegion ) < m_joints.Size();
		}

		const JointNames& GetRef( Uint8 faceRegion ) const
		{
			return m_joints[ GetIndex( faceRegion ) ];
		}

		JointNames& GetRef( Uint8 faceRegion )
		{
			return m_joints[ GetIndex( faceRegion ) ];
		}

		void Reserve( Uint32 nameCapacity );

	private:
		Uint32 GetIndex( Uint8 faceRegion ) const
		{
			return static_cast< Uint32 >( static_cast< Int8 >( faceRegion ) + 1 );
		}

		red::StaticArray< red::DynArray< CName >, c_FaceRegionsCount > m_joints;
	};

	struct TargetVertexDiffsData
	{
		DataBuffer&					m_vertexDiffs;
		DataBuffer&					m_vertexDiffsMapping;

		red::DynArray< Uint32 >		m_numVertexDiffsInEachChunk{ rend::PoolMesh() };
		red::DynArray< Uint32 >		m_numVertexDiffsMappingInEachChunk{ rend::PoolMesh() };
		Vector4						m_quantizationScale;
		Vector4						m_quantizationOffset;

		TargetVertexDiffsData( DataBuffer& vertexDiffs, DataBuffer& vertexDiffsMapping )
			: m_vertexDiffs( vertexDiffs )
			, m_vertexDiffsMapping( vertexDiffsMapping )
		{}
	};

	struct TextureBlendInfo
	{
		RTTI_DECLARE_TYPE( TextureBlendInfo );

		Bool			m_blend;
		DiffTextureSize	m_diffSize;
		CName			m_name;
	};

	static CName GetTextureName( const BlendedTextureType textureType )
	{
		switch ( textureType )
		{
		case BTT_Normal:	return RED_NAME_CONSTEXPR( "Normal" );
		case BTT_Roughness: return RED_NAME_CONSTEXPR( "Roughness" );
		case BTT_Diffuse:	return RED_NAME_CONSTEXPR( "Albedo" );
		default:
			RED_ASSERT( false, "Invalid texture type: %d", textureType );
			return CName::NONE();
		}
	}

	static Bool ShouldProcess( const BlendedTextureType textureType )
	{
		// currently we are only processing normalmap blends
		return textureType == BTT_Normal;
	}

	static Bool ShouldProcess( const BlendedTextureType textureType, const GpuWrapApi::eTextureFormat srcFormat )
	{
		// currently we are only processing normalmap blends
		return textureType == BTT_Normal && GpuWrapApi::IsSupportedCompressedFormat( srcFormat );
	}
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( DiffTextureSize, MorphTargets );
RTTI_DECLARE_ENUM_IN_NAMESPACE( FaceRegion, MorphTargets );