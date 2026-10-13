/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderSimpleTypes.h"

//////////////////////////////////////////////////////////////////////

enum EDissolveMode : Uint8
{
	DISSOLVEMODE_Opaque,
	DISSOLVEMODE_FadeIn,
	DISSOLVEMODE_FadeOut,
	DISSOLVEMODE_Invisible,

	DISSOLVEMODE_MAX,
};

//////////////////////////////////////////////////////////////////////////

/// Type of material vertex factory
/// WARNING: RenderMeshBlob is serializing the values of this enum casted to Uint8. When changing the enum values be sure to update RenderMeshBlob::UpgradeVersion()!
enum EMaterialVertexFactory : Uint8
{
	MVF_Invalid=0,							//!< Invalid vertex factory, do not draw
	MVF_Terrain,							//!< Terrain mesh ( height map sampling )
	MVF_MeshStatic,							//!< Generic static mesh vertex factory
	MVF_MeshSkinned,						//!< Generic skinned mesh vertex factory
	MVF_MeshExtSkinned,						//!< Skinned mesh with up to 8 bone influences per vertex
	MVF_GarmentMeshSkinned,					//!< Generic skinned mesh vertex factory with morph_offset vectors for garment layering.
	MVF_GarmentMeshExtSkinned,				//!< Skinned mesh with up to 8 bone influences per vertex and with morph_offset vectors for garment layering.
	MVF_MeshSpeedTree,						//!< Generic speed tree mesh vertex factory
	MVF_ParticleBilboard,					//!< Simple bilboarded CPU particles
	MVF_ParticleParallel,					//!< Particles parallel to emitter
	MVF_ParticleMotionBlur,					//!< Particles oriented in movement direction with stretching ability
	MVF_ParticleSphereAligned,				//!< Particles billboarded along sphere surface
	MVF_ParticleVerticalFixed,				//!< Particles billboarded along sphere surface, aligned to Z axis
	MVF_ParticleTrail,						//!< Particles producing motion trail effect
	MVF_ParticleFacingTrail,				//!< Particles producing camera facing motion trail effect
	MVF_ParticleScreen,						//!< Particles produced in screen space
	MVF_ParticleBeam,						//!< Particles composing beam effects
	MVF_ParticleFacingBeam,					//!< Particles composing facing beam effects
	MVF_Decal,								//!< Screen space decals
	MVF_Debug,								//!< Debug fragments
	MVF_DrawBuffer,							//!< User interface
	MVF_Fullscreen,							//!< Fullscreen pass, no geometry bound, things generated in VS
	MVF_MeshSkinnedVehicle,                 //!< Vehicle (extra channels for post-damage position and normals)
	MVF_MeshStaticVehicle,					//!< Vehicle (extra channels for post-damage position and normals)
	MVF_MeshProcedural,						//!< Mesh with procedurally-generated instance buffer
	MVF_MeshDestructible,					//!< Static meshes with destruction streams (i.e. pre-fractured)
	MVF_MeshDestructibleSkinned,			//!< Static mesh with destruction streams (i.e. pre-fractured). Uses skinning data to reduce transform texture size.
	MVF_MeshSkinnedLightBlockers,			//!< Generic skinned mesh vertex factory with light blocker intensity per vertex
	MVF_MeshExtSkinnedLightBlockers,		//!< Skinned mesh with up to 8 bone influences per vertex with light blocker intensity per vertex
	MVF_GarmentMeshSkinnedLightBlockers,	//!< Generic skinned mesh vertex factory with morph_offset vectors for garment layering with light blocker intensity per vertex
	MVF_GarmentMeshExtSkinnedLightBlockers,	//!< Skinned mesh with up to 8 bone influences per vertex with morph_offset vectors for garment layering with light blocker intensity per vertex
	MVF_MeshSkinnedSingleBone,				//!< Skinned mesh with a single bone skinning (i.e. physical destruction)
	MVF_MeshProxy,							//!< Super simple mesh for proxy, no tangents, no normal maps, no color, no extra streams.
	MVF_MeshWindowProxy,					//!< Super simple mesh for window proxy, no tangents, no normal maps, no color, no extra streams.
	MVF_Max
};

RTTI_DECLARE_ENUM( EMaterialVertexFactory );

using VertexFactoryMask = red::BitSet64<MVF_Max>;

RED_INLINE Bool DoesMaterialVertexFactorySupportDiscards( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_MeshStatic:
	case MVF_MeshDestructible:
	case MVF_MeshDestructibleSkinned:
	case MVF_MeshSkinned:
	case MVF_MeshExtSkinned:
	case MVF_MeshStaticVehicle:
	case MVF_MeshSkinnedVehicle:
	case MVF_GarmentMeshSkinned:
	case MVF_GarmentMeshExtSkinned:
	case MVF_MeshSpeedTree:
	case MVF_Terrain:
	case MVF_MeshSkinnedLightBlockers:
	case MVF_MeshExtSkinnedLightBlockers:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
	case MVF_MeshProcedural:
	case MVF_MeshSkinnedSingleBone:
	case MVF_MeshProxy:
	case MVF_MeshWindowProxy:
	case MVF_Decal:
		return true;

	default:
		return false;
	}
}

RED_INLINE Bool DoesMaterialVertexFactorySupportPositionOnly( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_MeshStatic:
	case MVF_MeshDestructible:
	case MVF_MeshDestructibleSkinned:
	case MVF_MeshSkinned:
	case MVF_MeshExtSkinned:
	case MVF_MeshStaticVehicle:
	case MVF_MeshSkinnedVehicle:
	case MVF_GarmentMeshSkinned:
	case MVF_GarmentMeshExtSkinned:
	case MVF_Terrain:
	case MVF_Debug:
	case MVF_Decal:
	case MVF_MeshSpeedTree:
	case MVF_MeshProcedural:
	case MVF_MeshSkinnedLightBlockers:
	case MVF_MeshExtSkinnedLightBlockers:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
	case MVF_MeshProxy:
	case MVF_MeshSkinnedSingleBone:
		return true;

	default:
		return false;
	}
}

RED_INLINE Bool DoesMaterialVertexFactorySupportGarmentStreams( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_GarmentMeshSkinned:					/// @todo DP: this should not be here (no tex coord 1 nor vertex color in regular skinned mesh), but at the end of 2018.01 it turned out this is needed for unknown (yet) reasons
	case MVF_GarmentMeshExtSkinned:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
		return true;
	default:
		return false;
	}
}

RED_INLINE Bool DoesMaterialVertexFactorySupportSkinning( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_MeshSkinned:
	case MVF_MeshExtSkinned:
	case MVF_MeshSkinnedVehicle:
	case MVF_GarmentMeshSkinned:
	case MVF_GarmentMeshExtSkinned:
	case MVF_MeshSkinnedLightBlockers:
	case MVF_MeshExtSkinnedLightBlockers:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
	case MVF_MeshSkinnedSingleBone:
		return true;

	default:
		return false;
	}
}


RED_INLINE Bool DoesMaterialVertexFactorySupportInstancing( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_MeshStatic:
	case MVF_MeshDestructible:
	case MVF_MeshDestructibleSkinned:
	case MVF_MeshSkinned:
	case MVF_MeshExtSkinned:
	case MVF_MeshStaticVehicle:
	case MVF_MeshSkinnedVehicle:
	case MVF_GarmentMeshSkinned:
	case MVF_GarmentMeshExtSkinned:
	case MVF_Debug:
	case MVF_Terrain:
	case MVF_MeshSpeedTree:
	case MVF_MeshSkinnedLightBlockers:
	case MVF_MeshExtSkinnedLightBlockers:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
	case MVF_Decal:
	case MVF_MeshProxy:
	case MVF_MeshWindowProxy:
	case MVF_MeshSkinnedSingleBone:
		return true;

	default:
		return false;
	}
}


RED_INLINE Bool DoesMaterialVertexFactorySupportExtraSteams( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{	
	case MVF_Terrain:
	case MVF_MeshProxy:
	case MVF_MeshWindowProxy:
		return false;

	default:
		return true;
	}
}


RED_INLINE Bool DoesMaterialVertexFactorySupportDismemberment( EMaterialVertexFactory factory )
{
	static_assert( MVF_Max == 34, "EMaterialVertexFactory has changed" );
	switch ( factory )
	{
	case MVF_MeshSkinned:
	case MVF_MeshExtSkinned:
	case MVF_GarmentMeshSkinned:
	case MVF_GarmentMeshExtSkinned:
	case MVF_MeshSkinnedLightBlockers:
	case MVF_MeshExtSkinnedLightBlockers:
	case MVF_GarmentMeshSkinnedLightBlockers:
	case MVF_GarmentMeshExtSkinnedLightBlockers:
	case MVF_MeshSkinnedSingleBone:
		return true;

	default:
		return false;
	}
}

//////////////////////////////////////////////////////////////////////////

/// Type of texture filtering
enum ETextureFilteringMin : Uint8
{
	TFMin_Point = 0,			// Point filtering used aka NEAREST
	TFMin_Linear,			// Bilinear interpolation filtering  
	TFMin_Anisotropic,		// Anisotropic texture filtering 
	TFMin_AnisotropicLow,	// Anisotropic texture low filtering 
};

RTTI_DECLARE_ENUM( ETextureFilteringMin );

/// Type of texture filtering
enum ETextureFilteringMag : Uint8
{
	TFMag_Point = 0,			// Point filtering used aka NEAREST
	TFMag_Linear,			// Bilinear interpolation filtering  
};

RTTI_DECLARE_ENUM( ETextureFilteringMag );

/// Type of texture filtering
enum ETextureFilteringMip : Uint8
{
	TFMip_None = 0,				// No filtering
	TFMip_Point,			// Point filtering used aka NEAREST
	TFMip_Linear,			// Bilinear interpolation filtering  
};

RTTI_DECLARE_ENUM( ETextureFilteringMip );

/// Texture addressing mode
enum ETextureAddressing : Uint8
{
	TA_Wrap = 0,		// Tile the texture at every integer junction
	TA_Mirror,			// Similar to wrap, except that the texture is flipped at every integer junction
	TA_Clamp,			// Texture coordinates outside the range [0.0, 1.0] are set to the texture color at 0.0 or 1.0, respectively
	TA_MirrorOnce,		// Takes the absolute value of the texture coordinate (thus, mirroring around 0), and then clamps to the maximum value
	TA_Border,
};

RTTI_DECLARE_ENUM( ETextureAddressing );

enum ETextureComparisonFunction : Uint8
{
	TCF_None = 0,
	TCF_Less, //todo: add more if you need
	TCF_Equal,
	TCF_LessEqual,
	TCF_Greater,
	TCF_NotEqual,
	TCF_GreaterEqual,
	TCF_Always,
};

RTTI_DECLARE_ENUM( ETextureComparisonFunction );

/// Material shader target
enum EMaterialShaderTarget : Uint8
{
	MSH_Invalid			= 0,
	MSH_First			= 1,
	MSH_VertexShader	= MSH_First,
	MSH_PixelShader		= 2,
	MSH_MAX				= 3,
};

RTTI_DECLARE_ENUM( EMaterialShaderTarget );

enum EMaterialPriority : Uint8
{
	EMP_Normal	= 0,
	EMP_Front	= 1,
	EMP_MAX		= 2,
};

RTTI_DECLARE_ENUM( EMaterialPriority );

static EMaterialShaderTarget MapShaderTarget( GpuWrapApi::eShaderType shaderType )
{
	switch ( shaderType )
	{
		case GpuWrapApi::VertexShader:		return MSH_VertexShader;
		case GpuWrapApi::PixelShader:		return MSH_PixelShader;
		case GpuWrapApi::ComputeShader:
		default:
			RED_ASSERT( false, "Invalid ShaderType!" );
			return MSH_MAX;
	}
}

static red::String GetShaderTargetName( EMaterialShaderTarget shaderTarget )
{
	switch ( shaderTarget )
	{
		case MSH_VertexShader:		return "VertexShader";
		case MSH_PixelShader:		return "PixelShader";
		default:
			RED_ASSERT( false, "Invalid ShaderTarget!" );
			return String::EMPTY();
	}
}


//////////////////////////////////////////////////////////////////////////

struct SamplerStateInfo
{
	RTTI_DECLARE_TYPE( SamplerStateInfo );
	RED_USE_MEMORY_POOL( rend::PoolShaderCacheData );

	ETextureFilteringMin		m_filteringMin;
	ETextureFilteringMag		m_filteringMag;
	ETextureFilteringMip		m_filteringMip;
	ETextureAddressing			m_addressU;
	ETextureAddressing			m_addressV;
	ETextureAddressing			m_addressW;
	ETextureComparisonFunction	m_comparisonFunc;
	Uint8						m_register;

	RED_INLINE Bool operator==( const SamplerStateInfo& rhs ) const
	{
		return	m_filteringMin	== rhs.m_filteringMin	&&
			m_filteringMag		== rhs.m_filteringMag	&&
			m_filteringMip		== rhs.m_filteringMip	&&
			m_addressU			== rhs.m_addressU		&&
			m_addressV			== rhs.m_addressV		&&
			m_addressW			== rhs.m_addressW		&&
			m_comparisonFunc	== rhs.m_comparisonFunc;
	}

	void Serialize( IFile& file )
	{
		if ( file.IsReader() || file.IsWriter() )
		{
			file.Serialize( &m_filteringMin, sizeof( m_filteringMin ) );
			file.Serialize( &m_filteringMag, sizeof( m_filteringMag ) );
			file.Serialize( &m_filteringMip, sizeof( m_filteringMip ) );
			file.Serialize( &m_addressU, sizeof( m_addressU ) );
			file.Serialize( &m_addressV, sizeof( m_addressV ) );
			file.Serialize( &m_addressW, sizeof( m_addressW ) );
			file.Serialize( &m_comparisonFunc, sizeof( m_comparisonFunc ) );
			file.Serialize( &m_register, sizeof( m_register ) );
		}
	}
};
typedef red::DynArray< SamplerStateInfo > TSamplerStateInfoArray;

/// Information about material parameter used by rendering technique
struct MaterialUsedParameter
{
	RTTI_DECLARE_TYPE( MaterialUsedParameter );

	CName	m_name;				//!< Name of parameter
	Uint8	m_register;			//!< Target registers

public:
	RED_INLINE MaterialUsedParameter()
	{};

	// Constructor
	RED_INLINE MaterialUsedParameter( const CName& name, Uint8 reg )
		: m_name( name )
		, m_register( reg )
	{}

	// Compare
	RED_INLINE Bool operator==( const MaterialUsedParameter& param ) const
	{
		return ( m_name == param.m_name ) && ( m_register == param.m_register );
	}
};

typedef red::DynArray< MaterialUsedParameter > TMaterialUsedParameterArray;
