/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "morphTargetCommon.h"

// Utility for packing a set of rectangles into a single atlas.
class RectanglePacker
{
public:
	static const Uint32 DEFAULT_SPACING = 0;
	static const Uint32 DEFAULT_ALIGNMENT = 1;

private:

	// Non-uniform grid used for packing algorithm. Supports splitting rows and columns, marking cells as filled, and
	// querying if regions are filled or not.
	class PackingGrid
	{
	private:
		red::DynArray< Bool > m_cells { rend::PoolMesh() };
		red::DynArray< Int32 > m_widths { rend::PoolMesh() };
		red::DynArray< Int32 > m_heights { rend::PoolMesh() };

	public:
		PackingGrid( Int32 width, Int32 height );


		Uint32 GetColumnCount() const { return m_widths.Size(); }
		Uint32 GetRowCount() const { return m_heights.Size(); }

		Int32 GetColumnWidth( Uint32 col ) const { return m_widths[col]; }
		Int32 GetRowHeight( Uint32 row ) const { return m_heights[row]; }

		Bool IsFree( Uint32 col, Uint32 row ) const { return !m_cells[col + row*m_widths.Size()]; }
		Bool IsFree( Uint32 col, Uint32 row, Int32 w, Int32 h );

		Int32 CellX( Uint32 col ) const;
		Int32 CellY( Uint32 row ) const;


		void Fill( Uint32 col, Uint32 row ) { m_cells[col + row*m_widths.Size()] = true; }
		void Fill( Uint32 col, Uint32 row, Int32 w, Int32 h );

		void SplitColumn( Uint32 col, Int32 leftWidth );
		void SplitRow( Uint32 row, Int32 topHeight );
	};


	struct RectangleSize
	{
		Int32 width;
		Int32 height;

		RectangleSize() {}
		RectangleSize( Int32 w, Int32 h )
			: width( w )
			, height( h )
		{}
	};

	red::DynArray< RectangleSize >	m_sortedRectangles { rend::PoolMesh() };		// Sorted rectangles, ready for packing.
	red::DynArray< Uint32 >			m_rectangleIndices { rend::PoolMesh() };		// Mapping from m_sortedRectangles back to the original list.

	red::DynArray< Rect >			m_packedRectangles { rend::PoolMesh() };
	Uint32						m_packedWidth;
	Uint32						m_packedHeight;

	Uint32						m_spacing;
	Uint32						m_alignment;

	Int32						m_minRectHeight;
	Int32						m_maxRectHeight;
	Int32						m_totalRectArea;

public:
	RectanglePacker();

	// Set the amount of space that should be left between rectangles. Spacing is not added to the edges of the
	// atlas, only internal. Changing this will not affect any previously calculated packing.
	void SetSpacing( Uint32 spacing );

	// Set alignment for packed rectangles. Each output rectangle will have its top/left coordinate aligned to this.
	// The size of the packed atlas (retrieved by GetPackedWidth/GetPackedHeight) will also be aligned to this, although
	// the individual rectangle sizes may not be. Must be a power of two. Changing this will not affect any previously
	// calculated packing.
	void SetAlignment( Uint32 align );

	// Provide the rectangles to pack. This will clear any previously calculated packing.
	void SetInputRectangles( const red::DynArray<Rect>& rectangles );

	void Pack();

	// Get the packed rectangles. These are in the same order as the ones provided to SetInputRectangles.
	const red::DynArray< Rect >& GetPackedRectangles() const { return m_packedRectangles; }
	Uint32 GetPackedWidth() const { return m_packedWidth; }
	Uint32 GetPackedHeight() const { return m_packedHeight; }


private:
	// Try packing the rectangles into the given grid. Return true and fill m_packedRectangles, m_packedWidth, m_packedHeight
	// if rectangles fit. If they didn't fit, return false and members are in undefined state (although m_packedRectangles
	// size will not be touched).
	Bool TryPack( PackingGrid& grid );

	Bool TryFitRectangle( PackingGrid& grid, const RectangleSize& unpaddedRect, Rect& outPackedRect );

	Int32 AlignUp( Int32 v ) const { return red::AlignUp( v, ( Int32 )m_alignment ); }
};
