/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "frustum.h"

#include "rttiClassBuilder.h"

RTTI_BEGIN_NODEFAULT_TYPE( CFrustum );
	RTTI_SCRIPT_ALIAS( "Frustum" );
RTTI_END_TYPE();

void ComputeFrustumPlane( const EFrustumPlane plane, const Matrix& matrix, __m128& outPlane, __m128& outMask )
{
	Float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
	switch ( plane )
	{
	case FP_Near:
		{
			x = matrix[3][0] + matrix[2][0];
			y = matrix[3][1] + matrix[2][1];
			z = matrix[3][2] + matrix[2][2];
			w = matrix[3][3] + matrix[2][3];
			break;
		}
	case FP_Right:
		{
			x = matrix[3][0] - matrix[0][0];
			y = matrix[3][1] - matrix[0][1];
			z = matrix[3][2] - matrix[0][2];
			w = matrix[3][3] - matrix[0][3];
			break;
		}
	case FP_Left:
		{
			x = matrix[3][0] + matrix[0][0];
			y = matrix[3][1] + matrix[0][1];
			z = matrix[3][2] + matrix[0][2];
			w = matrix[3][3] + matrix[0][3];
			break;
		}
	case FP_Bottom:
		{
			x = matrix[3][0] + matrix[1][0];
			y = matrix[3][1] + matrix[1][1];
			z = matrix[3][2] + matrix[1][2];
			w = matrix[3][3] + matrix[1][3];
			break;
		}
	case FP_Top:
		{
			x = matrix[3][0] - matrix[1][0];
			y = matrix[3][1] - matrix[1][1];
			z = matrix[3][2] - matrix[1][2];
			w = matrix[3][3] - matrix[1][3];
			break;
		}
	case FP_Far:
		{
			x = matrix[3][0] - matrix[2][0];
			y = matrix[3][1] - matrix[2][1];
			z = matrix[3][2] - matrix[2][2];
			w = matrix[3][3] - matrix[2][3];
			break;
		}
	default:
		RED_FATAL_ASSERT( false, "Invalid FrustumPlane to calculate" );
	}

	// Normalize plane
	Float len = sqrt( x * x + y * y + z * z );
	outPlane = _mm_setr_ps( x / len, y / len, z / len, w / len );
	outMask = _mm_cmplt_ps( outPlane, _mm_setzero_ps() );
}

void CFrustum::InitFromCamera( const Matrix& matrix )
{
	Matrix w2sTransposed = matrix.Transposed();

	ComputeFrustumPlane( FP_Near,	w2sTransposed,	m_planes[ FP_Near ],	m_masks[ FP_Near ] );
	ComputeFrustumPlane( FP_Right,	w2sTransposed,	m_planes[ FP_Right ],	m_masks[ FP_Right ] );
	ComputeFrustumPlane( FP_Left,	w2sTransposed,	m_planes[ FP_Left ],	m_masks[ FP_Left ] );
	ComputeFrustumPlane( FP_Bottom,	w2sTransposed,	m_planes[ FP_Bottom ],	m_masks[ FP_Bottom ] );
	ComputeFrustumPlane( FP_Top,	w2sTransposed,	m_planes[ FP_Top ],		m_masks[ FP_Top ] );
	ComputeFrustumPlane( FP_Far,	w2sTransposed,	m_planes[ FP_Far ],		m_masks[ FP_Far ] );
}

Float CFrustum::GetPointMinDistance( const Vector4 &pos ) const
{
	__m128 testPos = _mm_set_ps( 1.0f, pos.Z, pos.Y, pos.X );
	__m128 tempDist;
	DOT_PRODUCT( tempDist, m_planes[0], testPos );
	Float minDist = _mm_cvtss_f32( tempDist );
	for ( Uint32 i = 1; i < FP_Max; ++i )
	{
		DOT_PRODUCT( tempDist, m_planes[ i ], testPos );
		minDist = Min( minDist, _mm_cvtss_f32( tempDist ) );
	}
	return minDist;
}

Float CFrustum::GetPointMinDistance( const Vector4 & pos, EFrustumPlane frustumPlane ) const
{
	__m128 testPos = _mm_set_ps( 1.0f, pos.Z, pos.Y, pos.X );
	__m128 tempDist;
	DOT_PRODUCT( tempDist, m_planes[ frustumPlane ], testPos );
	return _mm_cvtss_f32( tempDist );
}

Vector4 CFrustum::GetPlane( EFrustumPlane plane ) const
{
	Vector4 out;
	_mm_store_ps( &out.X, m_planes[plane] );

	return out; //  Vector4( out.W, out.Z, out.Y, out.X );
}

void CFrustum::SetPlane( EFrustumPlane plane, const Vector4& in )
{
	m_planes[ plane ] = _mm_load_ps( &in.X );
}

void CFrustum::ComputeFrustum( CFrustum* out, const Matrix& localToWorld, Float fov, Float aspect, Float znear, Float zfar )
{
	static thread_local const Matrix axesConversion( Vector4( 1, 0, 0, 0 ), Vector4( 0, 0, 1, 0 ), Vector4( 0, 1, 0, 0 ), Vector4( 0, 0, 0, 1 ) );
	const Matrix worldToView = localToWorld.OrthonormInverted() * axesConversion;

	Matrix viewToScreen;
	viewToScreen.BuildPerspectiveLH( DEG2RAD( fov ), aspect, znear, zfar );

	const Matrix worldToScreen = worldToView * viewToScreen;
	out->InitFromCamera( worldToScreen );
}
