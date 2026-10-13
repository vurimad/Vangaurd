/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "viewport.h"
#include "renderFrameInfo.h"

IViewport::IViewport( Uint32 width, Uint32 height, EViewportWindowMode windowMode )
	: m_width( width )
	, m_height( height )
	, m_fullWidth( width )
	, m_fullHeight( height )
	, m_x( 0 )
	, m_y( 0 )
	, m_windowMode( windowMode )
	, m_mouseMode( MM_Normal )
	, m_cachetAspectRatio( EAspectRatio::FR_NONE )
{	
}

IViewport::~IViewport()
{
}

Bool IViewport::AdjustSize( Uint32 width, Uint32 height )
{
	m_fullWidth = m_width = width;
	m_fullHeight = m_height = height;

	if( m_cachetAspectRatio != EAspectRatio::FR_NONE )
	{
		Int32 outVertical = 0, outHorizontal = 0;

		Float viewAspect = (Float)m_fullWidth / (Float)m_fullHeight;

		Float aspect = 1.0f;

		switch ( m_cachetAspectRatio )
		{
		case EAspectRatio::FR_16_9:
			aspect = 1.777777777777778f;
			break;
		case EAspectRatio::FR_16_10:
			aspect = 1.6f;
			break;
		case EAspectRatio::FR_4_3:
			aspect = 1.333333333333333f;
			break;
		case EAspectRatio::FR_21_9:
			aspect = 2.333333333333333f;
			break;
		default:
			aspect = viewAspect;
			break;
		}

		if( aspect != 0.0f )
		{
			if ( viewAspect <= aspect )
			{
				Float cameraHeight = m_fullWidth / aspect;
				outVertical = (Int32) ceilf( ( m_fullHeight - cameraHeight ) / 2.0f );
			}
			else
			{
				Float cameraWidth = m_fullHeight * aspect;
				outHorizontal = (Int32) ceilf( ( m_fullWidth - cameraWidth ) / 2.0f );
			}
		}

		
		AdjustSize( outHorizontal, outVertical, m_fullWidth - 2*outHorizontal, m_fullHeight - 2*outVertical );
	}

	return true;
}

Bool IViewport::AdjustSize( Uint32 x, Uint32 y, Uint32 width, Uint32 height )
{

	m_x = x;
	m_y = y;
	m_width = width;
	m_height = height;

	return true;
}

void IViewport::AdjustSizeWithCachets( EAspectRatio cachetAspectRatio )
{
	m_cachetAspectRatio = cachetAspectRatio;
	AdjustSize( m_fullWidth, m_fullHeight );
}

void IViewport::RestoreSize()
{
	m_cachetAspectRatio = EAspectRatio::FR_NONE;
	m_x = 0;
	m_y = 0;
	AdjustSize( m_fullWidth, m_fullHeight );
}

void IViewport::GetSizeAdjustedToCachet( const EAspectRatio aspectRatio, const Uint32 inFullWidth, const Uint32 inFullHeight, Int32& outWidth, Int32& outHeight) const
{
	if( aspectRatio != EAspectRatio::FR_NONE )
	{
		Int32 outVertical = 0, outHorizontal = 0;
		Float viewAspect = (Float)inFullWidth / (Float)inFullHeight;
		Float aspect = 1.0f;

		switch ( aspectRatio )
		{
		case EAspectRatio::FR_16_9:
			aspect = 1.777777777777778f;
			break;
		case EAspectRatio::FR_16_10:
			aspect = 1.6f;
			break;
		case EAspectRatio::FR_4_3:
			aspect = 1.333333333333333f;
			break;
		case EAspectRatio::FR_21_9:
			aspect = 2.333333333333333f;
			break;
		default:
			aspect = viewAspect;
			break;
		}

		if( aspect != 0.0f )
		{
			if ( viewAspect <= aspect )
			{
				Float cameraHeight = inFullWidth / aspect;
				outVertical = (Int32) ceilf( ( inFullHeight - cameraHeight ) );
			}
			else
			{
				Float cameraWidth = inFullHeight * aspect;
				outHorizontal = (Int32) ceilf( ( inFullWidth - cameraWidth ) );
			}
		}

		outWidth = inFullWidth - outHorizontal;
		outHeight = inFullHeight - outVertical;
	}
	else
	{
		outWidth = inFullWidth;
		outHeight = inFullHeight;
	}
}
