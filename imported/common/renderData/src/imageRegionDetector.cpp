/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "imageRegionDetector.h"

ImageRegionDetector::ImageRegionDetector()
	: m_imageWidth( 0 )
	, m_imageHeight( 0 )
	, m_regionSizeLimit( DEFAULT_SIZE_LIMIT )
	, m_regionAreaLimit( DEFAULT_AREA_LIMIT )
	, m_regions( rend::PoolMesh() )
	, m_regionIDs( rend::PoolMesh() )
	, m_imageMask( rend::PoolMesh() )
{
}

/*
void ImageRegionDetector::SetInputImage( const red::BitSet64& mask, Uint32 imageWidth, Uint32 imageHeight )
{
	m_regions.Clear();
	m_regionIDs.Clear();

	m_imageWidth = imageWidth;
	m_imageHeight = imageHeight;

	const Uint32 numElements = m_imageWidth * m_imageHeight;

#ifdef RED_ASSERTS_ENABLED
	{
		// LongBitField rounds the number of elements up to a multiple of the bit-size of it's internal type.
		// So we can't just compare numElements to mask.Size. Instead, need to round numElements up.
		//const Uint32 bitsPerMaskElement = sizeof( LongBitField::TBaseType)*8;
		//RED_ASSERT( red::AlignUp( numElements, bitsPerMaskElement ) == mask.Size(), TXT("Image mask has inconsistent size. Has %u elements, for %ux%u image."), m_imageMask.Size(), m_imageWidth, m_imageHeight );
	}
#endif

	Bool hasAnythingSet = false;
	m_imageMask.Resize( numElements );
	for ( Uint32 i = 0; i < numElements; ++i )
	{
		const Bool isSet = mask.Get( i );
		m_imageMask[i] = isSet ? 1 : 0;
		hasAnythingSet |= isSet;
	}

	m_maxID = hasAnythingSet ? 1 : 0;
}
*/

void ImageRegionDetector::SetInputImage( const red::DynArray< Uint32 >& mask, Uint32 imageWidth, Uint32 imageHeight )
{
	m_regions.Clear();
	m_regionIDs.Clear();

	m_imageMask = mask;
	m_imageWidth = imageWidth;
	m_imageHeight = imageHeight;
	RED_ASSERT( m_imageWidth * m_imageHeight == m_imageMask.Size(), "Image mask has inconsistent size. Has %u elements, for %ux%u image.", m_imageMask.Size(), m_imageWidth, m_imageHeight );

	Uint32 maxValue = 0;
	for ( Uint32 index : m_imageMask )
	{
		maxValue = Max( maxValue, index );
	}

	const Uint32 highestBitSet = red::BitUtils::BitScanReverse( maxValue );
	RED_ASSERT( highestBitSet < 256 );
	m_maxID = static_cast<Uint8>( highestBitSet );
}


void ImageRegionDetector::SetRegionSizeLimit( Uint32 sizeLimit )
{
	m_regionSizeLimit = sizeLimit;
}

void ImageRegionDetector::SetRegionAreaLimit( Uint32 areaLimit)
{
	// Clamp to 1, because 0 doesn't make sense (discard anything less than the limit).
	m_regionAreaLimit = Max( areaLimit, 1u );
}


void ImageRegionDetector::FindRegions()
{
	m_regions.Clear();
	m_regionIDs.Clear();
	for ( Uint32 i = 0; i <= m_maxID; ++i )
	{
		ProcessSubimage( i, Rect( 0, m_imageWidth, 0, m_imageHeight ) );
	}
}


Rect ImageRegionDetector::FindRegionInSubimage( Uint8 regionID, const Rect& subimage, Uint32& outArea ) const
{
	RED_ASSERT( subimage.m_left >= 0 && subimage.m_top >= 0 && subimage.m_right <= (Int32)m_imageWidth && subimage.m_bottom <= (Int32)m_imageHeight,
		"Subimage (%d,%d)-(%d,%d) doesn't fit image %ux%u", subimage.m_left, subimage.m_top, subimage.m_right, subimage.m_bottom, m_imageWidth, m_imageHeight );

	const Uint32 regionMask = 1 << regionID;

	Uint32 area = 0;

	Rect r( Rect::RESET_STATE );
	for ( Int32 y = subimage.m_top; y < subimage.m_bottom; ++y )
	{
		for ( Int32 x = subimage.m_left; x < subimage.m_right; ++x )
		{
			if ( ( m_imageMask[ x + y*m_imageWidth ] & regionMask ) != 0 )
			{
				r.m_left	= Min( r.m_left, x );
				r.m_right	= Max( r.m_right, x+1 );
				r.m_top		= Min( r.m_top, y );
				r.m_bottom	= Max( r.m_bottom, y+1 );
				++area;
			}
		}
	}

	outArea = area;

	return r;
}


Bool ImageRegionDetector::SplitRegion( Uint8 regionID, const Rect& region, Rect& outSplit1, Rect& outSplit2 ) const
{
	const Uint32 regionMask = 1 << regionID;

	// Check if there's an empty row...
	for ( Int32 y = region.m_top; y < region.m_bottom; ++y )
	{
		Bool isEmpty = true;
		for ( Int32 x = region.m_left; x < region.m_right; ++x )
		{
			if ( ( m_imageMask[ x + y*m_imageWidth ] & regionMask ) != 0 )
			{
				isEmpty = false;
				break;
			}
		}

		if ( isEmpty )
		{
			outSplit1 = Rect( region.m_left, region.m_right, region.m_top, y );
			outSplit2 = Rect( region.m_left, region.m_right, y, region.m_bottom );
			return true;
		}
	}

	// Check if there's an empty column...
	for ( Int32 x = region.m_left; x < region.m_right; ++x )
	{
		Bool isEmpty = true;
		for ( Int32 y = region.m_top; y < region.m_bottom; ++y )
		{
			if ( ( m_imageMask[ x + y*m_imageWidth ] & regionMask ) != 0 )
			{
				isEmpty = false;
				break;
			}
		}

		if ( isEmpty )
		{
			outSplit1 = Rect( region.m_left, x, region.m_top, region.m_bottom );
			outSplit2 = Rect( x, region.m_right, region.m_top, region.m_bottom );
			return true;
		}
	}

	return false;
}


void ImageRegionDetector::ProcessSubimage( Uint8 regionID, const Rect& subimage )
{
	Uint32 regionArea = 0;
	Rect region = FindRegionInSubimage( regionID, subimage, regionArea );
	const Int32 minRegionDimension = Min( region.Width(), region.Height() );
	if ( regionArea >= m_regionAreaLimit && minRegionDimension >= (Int32)m_regionSizeLimit )
	{
		RED_ASSERT( !region.IsEmpty(), "Found region that satisfies the area limit, but rectangle is empty?" );

		Rect split1, split2;
		if ( SplitRegion( regionID, region, split1, split2 ) )
		{
			if ( !split1.IsEmpty() )
			{
				ProcessSubimage( regionID, split1 );
			}
			if ( !split2.IsEmpty() )
			{
				ProcessSubimage( regionID, split2 );
			}
		}
		else
		{
			m_regions.PushBack( region );
			m_regionIDs.PushBack( regionID );
		}
	}
}


/*
LongBitField ImageRegionDetector::GetRegionMask( Uint32 index )
{
	RED_FATAL_ASSERT( index < m_regions.Size(), "Index out of range (%u >= %u)! Caller is responsible for making sure to provide a valid region.", index, m_regions.Size() );

	const Rect region = m_regions[index];
	const Uint32 regionMask = 1 << m_regionIDs[index];

	const Uint32 regionWidth = region.Width();
	const Uint32 regionHeight = region.Height();

	LongBitField mask( regionWidth * regionHeight );

	for ( Int32 y = region.m_top; y < region.m_bottom; ++y )
	{
		for ( Int32 x = region.m_left; x < region.m_right; ++x )
		{
			if ( ( m_imageMask[x + m_imageWidth * y] & regionMask ) != 0 )
			{
				const Int32 mx = x - region.m_left;
				const Int32 my = y - region.m_top;
				mask.SetBit( mx + regionWidth * my, true );
			}
		}
	}

	return mask;
}
*/
