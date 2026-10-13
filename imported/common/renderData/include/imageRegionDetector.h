/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "morphTargetCommon.h"

// Generate list of regions in an input image.
// Uses a very simple algorithm, which may not give perfect results in some situations:
//    1) Find overall bounds of "important" areas
//    2) Find a row or column within the bounds that has no important pixels
//    3) If row or column was found, split the area into two
//    4) Repeat from 1 on each new area
//    5) If no empty row or column, add area as a region
class ImageRegionDetector
{
public:
	static const Uint32	DEFAULT_SIZE_LIMIT	= 8;
	static const Uint32	DEFAULT_AREA_LIMIT	= 64;

private:
	red::DynArray< Rect >		m_regions;
	red::DynArray< Uint8 >		m_regionIDs;

	red::DynArray< Uint32 >		m_imageMask;
	Uint32						m_imageWidth;
	Uint32						m_imageHeight;

	Uint32						m_regionSizeLimit;
	Uint32						m_regionAreaLimit;

	Uint8						m_maxID;

public:
	ImageRegionDetector();

	// Set the input image. Pixels that should be detected should have the corresponding bit set to true.
	//void SetInputImage( const red::BitSet64& mask, Uint32 imageWidth, Uint32 imageHeight );

	// Set the input image. Individual bits in the provided image indicate different regions of interest. This way
	// overlapping sections can be detected and separated, by setting different bits for each.
	void SetInputImage( const red::DynArray< Uint32 >& mask, Uint32 imageWidth, Uint32 imageHeight );

	// Set minimum size for a region to be considered. Any regions with width or height less than this will be ignored.
	void SetRegionSizeLimit( Uint32 sizeLimit );

	// Set minimum area for a region to be considered. Any regions with area (counting on the "important" pixels) less
	// that this will be ignored.
	void SetRegionAreaLimit( Uint32 areaLimit);


	void FindRegions();


	// Get the regions that were detected during FindRegions. If FindRegion hasn't been called, or input image has changed,
	// this will be empty.
	const red::DynArray< Rect >& GetRegions() const { return m_regions; }

	// Get an array of "IDs" for each rectangle returned by GetRegions. When image was given with the "bitfield" version
	// of SetInputImage this will simply be 1 for each region. When the Uint32 mask version was used, then this will be
	// the bit position that was set for the region (e.g. if a region was marked in bit 3 of the input image, then the
	// rectangle(s) for that region will have ID 3).
	const red::DynArray< Uint8 >& GetRegionIDs() const { return m_regionIDs; }

	// Get mask for the given region. Returned bitfield is sized for the region rectangle (not the full image), with
	// a bit set where the region is important.
	//LongBitField GetRegionMask( Uint32 index );


private:

	// Given a rectangular area of the original image, find a tight-fitting region bound.
	Rect FindRegionInSubimage( Uint8 regionID, const Rect& subimage, Uint32& outArea ) const;

	// Try to split the given region into two subimages. Return true if split was successful, false if the region could
	// not be split.
	Bool SplitRegion( Uint8 regionID, const Rect& region, Rect& outSplit1, Rect& outSplit2 ) const;

	// Given a rectangular area of the original image, split it into smaller regions until it cannot be split anymore.
	// Add all leaf regions to the regions list.
	void ProcessSubimage( Uint8 regionID, const Rect& subimage );
};
