/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "envProbeResource.h"
#include "miniVec.h"
#include "renderSimpleTypes.h"
#include "renderCamera.h"

RTTI_BEGIN_TYPE( CReflectionProbeDataResource );
{
	RTTI_PARENT_TYPE( res::StreamedResource );

	RTTI_PROPERTY( m_data ).readOnly();
	RTTI_PROPERTY( m_textureData );
	RTTI_PROPERTY( m_dataHash ).readOnly();
	RTTI_PROPERTY( m_haveSkyData ).readOnly();
	RTTI_PROPERTY( m_faceDepth ).readOnly();
}
RTTI_END_TYPE();

RTTI_BEGIN_ENUM_IN_NAMESPACE( NeighborMode, envUtils );
{
	RTTI_ENUM_OPTION( eCLOSEST );
	RTTI_ENUM_OPTION( eONLY_GLOBAL );
	RTTI_ENUM_OPTION( eONLY_SELF );
	RTTI_ENUM_OPTION( eFILL_SURROUNDING );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( ReflectionProbeAmbientContributionMode, envUtils );
{
	RTTI_ENUM_OPTION( eNO_AMBIENT_CONTRIBUTION );
	RTTI_ENUM_OPTION( eALLOW_AMBIENT_CONTRIBUTION );
	RTTI_ENUM_OPTION( eOVERRIDE_GI_AMBIENT );
}
RTTI_END_ENUM();

namespace constants
{
	static constexpr Float		MAX_VIEW_DISTANCE	= 4000.0f;

	static const Quaternion cameraRotations[6] =
	{
		EulerAngles( -90.0f, 0.0f, 90.0f ).ToQuat(),
		EulerAngles( 90.0f, 0.0f, -90.0f ).ToQuat(),
		EulerAngles( 0.0f, 0.0f, 180.0f ).ToQuat(),
		EulerAngles( 0.0f, 180.0f, 180.0f ).ToQuat(),
		EulerAngles( 0.0f, -90.0f, 180.0f ).ToQuat(),
		EulerAngles( 0.0f, 90.0f, 0.0f ).ToQuat(),
	};

}

namespace external
{
	Bool (*EnvProbeResaver)(CReflectionProbeDataResource*,ECookingPlatform) = nullptr;
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

CReflectionProbeDataResource::CReflectionProbeDataResource()
	: m_haveSkyData( true )
{
	for( Float& d : m_faceDepth ) d = 32.0f;
}

CReflectionProbeDataResource::~CReflectionProbeDataResource()
{}

void CReflectionProbeDataResource::RecreateInternalRenderResources( Uint32 flag )
{
	res::StreamedResource::RecreateInternalRenderResources( flag );
	(void)flag;
}

void CReflectionProbeDataResource::OnPreSave( const PreSaveContext& context )
{
#ifndef RED_CONFIGURATION_FINAL
	if( !m_data.Empty() && external::EnvProbeResaver )
	{
		if( external::EnvProbeResaver( this, PLATFORM_All ) )
		{
			m_data.Clear();
		}
	}
#endif // RED_CONFIGURATION_FINAL
}

const DataBuffer& CReflectionProbeDataResource::GetData_LEGACY() const
{
	return m_data;
}

const THandle< IRenderResourceBlob > CReflectionProbeDataResource::GetTextureDataBlob() const
{
	return m_textureData.GetRenderResourceBlob();
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

#ifndef NO_TEXTURE_EDITING
void CReflectionProbeDataResource::SetEditorData( Uint64 dataHash, Bool haveSkyData, Float faceDepth[6], rend::RenderTextureResource&& data )
{
	m_dataHash		= dataHash;
	m_haveSkyData	= haveSkyData;

	for( Uint32 i =0; i < 6; ++i ) m_faceDepth[i] = faceDepth[i];

	m_textureData	= std::move( data );
}

void CReflectionProbeDataResource::SetResourceResaver( Bool( *callback )(CReflectionProbeDataResource*, ECookingPlatform) )
{
	external::EnvProbeResaver = callback;
}

bool CReflectionProbeDataResource::Cooking_OptimiseRenderResourceBlob( ECookingPlatform platform )
{
	if( !m_data.Empty() && external::EnvProbeResaver )
	{
		if( !external::EnvProbeResaver( this, platform ) )
			return false;

		m_data.Clear();
	}

	const auto savedBlob = Cast< rend::IRenderTextureBlob >( m_textureData.GetCookingPlatfromResourceBlob( platform ) );
	if( savedBlob == nullptr )
	{
		RED_LOG_ERROR( "Cooking: Error getting platform specific render texture blob, cooked data will contain data for ALL PLATFORMS!" );
		return false;
	}

	m_textureData.Clear();
	m_textureData.InitRenderResourceBlob( platform, savedBlob );

	return true;
}

#endif

Quaternion	CReflectionProbeDataResource::BuildEnvProbeRotation( Uint32 faceIndex )
{
	RED_ASSERT( faceIndex < 6 );

	return constants::cameraRotations[faceIndex];
}

Float		CReflectionProbeDataResource::BuildEnvProbeFOV( Uint32 faceIndex, const Vector3& boxSize )
{
	RED_ASSERT( faceIndex < 6 );

	const Vector2 fovTangent[3] =
	{
		Vector2( boxSize.X , boxSize.Y ),
		Vector2( boxSize.Y , boxSize.Z ),
		Vector2( boxSize.Z , boxSize.Y )
	};

	const Uint32 axis	= faceIndex / 2;
	const Float angle	= std::atan2( fovTangent[axis].Y, fovTangent[axis].X );

	return 2.0f * RAD2DEG( angle );
}

Float		CReflectionProbeDataResource::BuildEnvProbeAspectRatio( Uint32 faceIndex, const Vector3& boxSize )
{
	switch( faceIndex / 2 )
	{
		case 0: return boxSize.Z / boxSize.Y;
		case 1: return boxSize.X / boxSize.Z;
		case 2: return boxSize.X / boxSize.Y;
	}

	RED_FATAL( "Invalid face index" );

	return 1.0f;
}

Vector2		CReflectionProbeDataResource::BuildEnvProbeViewRatio( Uint32 faceIndex, const Vector3& boxSize )
{
	switch( faceIndex / 2 )
	{
		case 0 : return { boxSize.Z / boxSize.X , boxSize.Y / boxSize.X };
		case 1 : return { boxSize.X / boxSize.Y , boxSize.Z / boxSize.Y };
		case 2 : return { boxSize.X / boxSize.Z , boxSize.Y / boxSize.Z };
	};

	RED_FATAL( "Invalid face index" );

	return {1.0f, 1.0f};
}



void CReflectionProbeDataResource::BuildEnvProbeCamera( CRenderCamera &camera, const Vector3& origin, const Quaternion& rotation, Uint32 faceIndex, const Vector3& boxSize, Float nearClipDistance, Float farClipDistance )
{
	RED_ASSERT( faceIndex < 6 );

	CRenderCamera::CreationContext context;
	{
		context.position	= WorldPosition( origin );
		context.rotation	= rotation * BuildEnvProbeRotation(faceIndex);
		context.fov			= BuildEnvProbeFOV( faceIndex, boxSize );
		context.aspect		= BuildEnvProbeAspectRatio( faceIndex, boxSize );
		context.nearPlane	= math::Max( nearClipDistance, 0.05f );	// Do not allow small near plane
		context.farPlane	= farClipDistance < 0.0f ? constants::MAX_VIEW_DISTANCE : farClipDistance;
	}
	camera.Set( context );
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

