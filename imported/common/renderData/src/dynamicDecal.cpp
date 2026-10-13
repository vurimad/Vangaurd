/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "dynamicDecal.h"
#include "../../redMath/include/random.h"

RTTI_BEGIN_ENUM( ERenderDynamicDecalProjection )
	RTTI_ENUM_OPTION( RDDP_Ortho );
	RTTI_ENUM_OPTION( RDDP_Sphere );
RTTI_END_ENUM()

RTTI_BEGIN_ENUM( ERenderDynamicDecalAtlas )
	RTTI_ENUM_OPTION( RDDA_1x1 );
	RTTI_ENUM_OPTION( RDDA_2x1 );
	RTTI_ENUM_OPTION( RDDA_2x2 );
	RTTI_ENUM_OPTION( RDDA_4x2 );
	RTTI_ENUM_OPTION( RDDA_4x4 );
	RTTI_ENUM_OPTION( RDDA_8x4 );
RTTI_END_ENUM()

RTTI_BEGIN_ENUM( EDynamicDecalSpawnPriority )
	RTTI_ENUM_OPTION( RDDS_Normal );
	RTTI_ENUM_OPTION( RDDS_Highest );
RTTI_END_ENUM()

SDynamicDecalInitInfo::SDynamicDecalInitInfo()
	: m_origin( Vector4::ZERO_3D_POINT() )
	, m_dirFront( 0, 1, 0, 0 )
	, m_dirUp( 0, 0, 1, 0 )
	, m_width( 1 )
	, m_height( 1 )
	, m_nearZ( 0 )
	, m_farZ( 1 )
	, m_depthFadePower( 1.0f )
	, m_depthFadeOrigin( 1.0f )
	, m_normalFadeBias( 0.0f )
	, m_normalFadeScale( 1.0f )
	, m_roughnessScale( 1.f )
	, m_normalThreshold( 1.f )
	, m_doubleSided( false )
	, m_additiveNormals( true )
	, m_applyInLocalSpace( false )
	, m_isDismembermentDecal( false )
	, m_projectionMode( RDDP_Ortho )
	, m_timeToLive( std::numeric_limits< Float >::infinity() )
	, m_fadeTime( 0.0f )
	, m_fadeInTime( 0.0f )
	, m_startScale( 1.0f )
	, m_scaleTime( 1.0f )
	, m_autoHideDistance( -1.f )							// -1 means hide distance will be taken from SRenderSettings
	, m_atlasVector( .5f , .5f , .5f, .5f )
	, m_spawnPriority( EDynamicDecalSpawnPriority::RDDS_Normal )
{
}

Matrix SDynamicDecalInitInfo::GetWorldToDecalMatrix() const
{
	return GetDecalToWorldMatrix().FullInverted();
}

Matrix SDynamicDecalInitInfo::GetDecalToWorldMatrix() const
{
	// ( [-1,1], [0,1], [-1,1] ) local coordinates are inside the decal.
	// local Y axis is "forward" direction.
	Vector4 vecRight;
	Vector4 vecUp;
	Vector4 vecFront;
	Vector4 origin;
	GetDecalVectors( vecRight, vecUp, vecFront, origin );
	vecRight.W	= 0.f;
	vecUp.W		= 0.f;
	vecFront.W	= 0.f;
	origin.W	= 1.f;
	return Matrix( vecRight, vecFront, vecUp, origin );
}

Box SDynamicDecalInitInfo::GetWorldBounds() const
{
	Vector4 corner0;
	Vector4 corner1;
	Vector4 corner2;
	Vector4 corner3;
	Vector4 forward;
	GetDecalBoxCorners( corner0, corner1, corner2, corner3, forward );

	Box box( Box::RESET_STATE );
	box.AddPoint( corner0 );
	box.AddPoint( corner1 );
	box.AddPoint( corner2 );
	box.AddPoint( corner3 );
	box.AddPoint( corner0 + forward );
	box.AddPoint( corner1 + forward );
	box.AddPoint( corner2 + forward );
	box.AddPoint( corner3 + forward );
	return box;
}

void SDynamicDecalInitInfo::GetWorldBoundPoints( red::StaticArray< Vector4, 8 >& arr ) const
{
	Vector4 corner0;
	Vector4 corner1;
	Vector4 corner2;
	Vector4 corner3;
	Vector4 forward;
	GetDecalBoxCorners( corner0, corner1, corner2, corner3, forward );

	arr.PushBack( corner0 );
	arr.PushBack( corner1 );
	arr.PushBack( corner2 );
	arr.PushBack( corner3 );
	arr.PushBack( corner0 + forward );
	arr.PushBack( corner1 + forward );
	arr.PushBack( corner2 + forward );
	arr.PushBack( corner3 + forward );
}

void SDynamicDecalInitInfo::SetAtlasVector( ERenderDynamicDecalAtlas atlasType )
{
	Uint8 scaleS = 1;
	Uint8 scaleT = 1;

	switch( atlasType )
	{
	case RDDA_1x1 : 
		break;
	case RDDA_2x1 :
		scaleS = 2;
		break;
	case RDDA_2x2 :
		scaleS = 2;
		scaleT = 2;
		break;
	case RDDA_4x2 :
		scaleS = 4;
		scaleT = 2;
		break;
	case RDDA_4x4 :
		scaleS = 4;
		scaleT = 4;
		break;
	case RDDA_8x4 :
		scaleS = 8;
		scaleT = 4;
		break;
	default:
		RED_FATAL( "Undefined SubUV type in decal spawning" );
		break;
	}

	Uint8 randS = (Uint8) math::DefaultRandom().Get<Uint32>();
	Uint8 randT = (Uint8) math::DefaultRandom().Get<Uint32>();

	/*
		Scale-Bias from -1 .. +1 to atlas 0 .. +1
	*/

	m_atlasVector = Vector4( 0.5f / scaleS, 0.5f / scaleT , static_cast<Float>( randS % scaleS ) , static_cast<Float>( randT % scaleT ) );
	m_atlasVector.Z = ( 1.0f / scaleS ) * m_atlasVector.Z + m_atlasVector.X;
	m_atlasVector.W = ( 1.0f / scaleT ) * m_atlasVector.W + m_atlasVector.Y;
}


void SDynamicDecalInitInfo::SetAtlasVector( Uint32 scaleS, Uint32 scaleT, Uint32 index )
{
	RED_ASSERT( scaleS >= 1 );
	RED_ASSERT( scaleT >= 1 );

	Uint8 randS = index % scaleS;
	Uint8 randT = index / scaleS;

	/*
		Scale-Bias from -1 .. +1 to atlas 0 .. +1
	*/

	m_atlasVector = Vector4( 0.5f / scaleS, 0.5f / scaleT , static_cast<Float>( randS % scaleS ) , static_cast<Float>( randT % scaleT ) );
	m_atlasVector.Z = ( 1.0f / scaleS ) * m_atlasVector.Z + m_atlasVector.X;
	m_atlasVector.W = ( 1.0f / scaleT ) * m_atlasVector.W + m_atlasVector.Y;
}

SDynamicDecalMaterialInfo::SDynamicDecalMaterialInfo()
	: m_material( nullptr )
	, m_specularColor( 0, 0, 0, 0 )
	, m_specularScale( 0 )
	, m_specularBase( 1 )
	, m_additiveNormals( true )
	, m_diffuseRandomColor0( Color::WHITE() )
	, m_diffuseRandomColor1( Color::WHITE() )
	, m_subUVType( RDDA_1x1 )
	, m_specularity( -1.0f )
{

}

SDynamicDecalMaterialInfo::~SDynamicDecalMaterialInfo()
{

}
