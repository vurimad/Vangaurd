/**
* Copyright © 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "rectanglePacker.h"


// Algorithm is based on http://www.codeproject.com/Articles/210979/Fast-optimizing-rectangle-packing-algorithm-for-bu
// Axes are flipped (e.g. we start with all rectangles vertical and try to find top-most space to fit a rectangle instead
// of starting with them horizontal and finding left-most) because the grid cells are arranged in row-major order. So
// scanning a row is better than scanning a column.


// TODO : Try optimizations from article, to increase height by a smart amount in the case of rectangles not fitting.
// Right now just increases by the alignment, which can result in many iterations just increasing the height before
// something new fits. Didn't bother now, it seems to be working quick enough for now.

//////////////////////////////////////////////////////////////////////////

RectanglePacker::PackingGrid::PackingGrid( Int32 width, Int32 height )
{
	m_cells.PushBack( false );
	m_widths.PushBack( width );
	m_heights.PushBack( height );
}

Bool RectanglePacker::PackingGrid::IsFree( Uint32 col, Uint32 row, Int32 w, Int32 h )
{
	RED_ASSERT( w > 0 && h > 0 );

	const Uint32 numRows = GetRowCount();
	const Uint32 numCols = GetColumnCount();

	while ( h > 0 )
	{
		if ( row >= numRows )
		{
			return false;
		}

		Uint32 tcol = col;
		Int32 tw = w;
		while ( tw > 0 )
		{
			if (tcol >= numCols)
			{
				return false;
			}

			if ( !IsFree( tcol, row ) )
			{
				return false;
			}
			tw -= m_widths[tcol];
			++tcol;
		}

		h -= m_heights[row];
		++row;
	}

	return true;
}

Int32 RectanglePacker::PackingGrid::CellX( Uint32 col ) const
{
	Int32 x = 0;
	for ( Uint32 c = 0; c < col; ++c )
	{
		x += m_widths[c];
	}
	return x;
}
Int32 RectanglePacker::PackingGrid::CellY( Uint32 row ) const
{
	Int32 y = 0;
	for ( Uint32 r = 0; r < row; ++r )
	{
		y += m_heights[r];
	}
	return y;
}

void RectanglePacker::PackingGrid::Fill( Uint32 col, Uint32 row, Int32 w, Int32 h )
{
	RED_ASSERT( w > 0 && h > 0 );

	const Uint32 numRows = GetRowCount();
	const Uint32 numCols = GetColumnCount();

	while ( h > 0 && row < numRows )
	{
		Uint32 tcol = col;
		Int32 tw = w;
		while ( tw > 0 && tcol < numCols )
		{
			Fill( tcol, row );
			tw -= m_widths[tcol];
			++tcol;
		}

		h -= m_heights[row];
		++row;
	}
}

void RectanglePacker::PackingGrid::SplitColumn( Uint32 col, Int32 leftWidth )
{
	const Uint32 numRows = GetRowCount();
	const Uint32 numCols = GetColumnCount();

	// Find the actual column that needs to be split.
	while ( col < numCols && leftWidth > m_widths[col] )
	{
		leftWidth -= m_widths[col];
		++col;
	}

	if ( col >= numCols || leftWidth == m_widths[col] )
	{
		return;
	}

	red::DynArray<Bool> newCells( m_cells.Size() + numRows, red::PoolFrame() );

	for ( Uint32 i = 0; i < numRows; ++i )
	{
		for ( Uint32 j = 0; j <= col; ++j )
		{
			newCells[i*(numCols+1)+j] = m_cells[i*numCols+j];
		}
		newCells[i*(numCols+1)+col+1] = m_cells[i*numCols+col];
		for ( Uint32 j = col+1; j < numCols; ++j )
		{
			newCells[i*(numCols+1)+j+1] = m_cells[i*numCols+j];
		}
	}

	m_cells = std::move( newCells );

	const Int32 w = m_widths[col];
	m_widths[ col ] = leftWidth;
	m_widths.InsertAt( col + 1, w - leftWidth );
}

void RectanglePacker::PackingGrid::SplitRow( Uint32 row, Int32 topHeight )
{
	const Uint32 numRows = GetRowCount();
	const Uint32 numCols = GetColumnCount();

	// Find the actual column that needs to be split.
	while ( row < numRows && topHeight > m_heights[row] )
	{
		topHeight -= m_heights[row];
		++row;
	}

	if ( row >= numRows || topHeight == m_heights[row] )
	{
		return;
	}

	red::DynArray<Bool> newCells( m_cells.Size() + numCols, red::PoolFrame() );

	for ( Uint32 i = 0; i <= row; ++i )
	{
		for ( Uint32 j = 0; j < numCols; ++j )
		{
			newCells[i*numCols+j] = m_cells[i*numCols+j];
		}
	}
	for ( Uint32 j = 0; j < numCols; ++j )
	{
		newCells[(row+1)*numCols+j] = m_cells[row*numCols+j];
	}
	for ( Uint32 i = row+1; i < numRows; ++i )
	{
		for ( Uint32 j = 0; j < numCols; ++j )
		{
			newCells[(i+1)*numCols+j] = m_cells[i*numCols+j];
		}
	}

	m_cells = std::move( newCells );

	const Int32 h = m_heights[row];
	m_heights[row] = topHeight;
	m_heights.InsertAt( row+1, h - topHeight );
}


//////////////////////////////////////////////////////////////////////////


RectanglePacker::RectanglePacker()
	: m_packedWidth( 0 )
	, m_packedHeight( 0 )
	, m_spacing( DEFAULT_SPACING )
	, m_alignment( DEFAULT_ALIGNMENT )
{
}

void RectanglePacker::SetSpacing( Uint32 spacing )
{
	m_spacing = spacing;
}

void RectanglePacker::SetAlignment( Uint32 align )
{
	RED_ASSERT( red::IsPowerOf2( align ), "Alignment must be a power of 2: %u", align );

	// Clamp to 1. Alignment 0 doesn't make much sense.
	m_alignment = Max( align, 1u );
}

void RectanglePacker::SetInputRectangles( const red::DynArray<Rect>& rectangles )
{
	const Uint32 numRectangles = rectangles.Size();

	m_rectangleIndices.Resize( numRectangles );
	for ( Uint32 i = 0; i < numRectangles; ++i )
	{
		m_rectangleIndices[i] = i;
	}

	// Sort by decreasing width
	std::sort( m_rectangleIndices.Begin(), m_rectangleIndices.End(), [&]( Uint32 i0, Uint32 i1 )
	{
		return rectangles[i0].Width() > rectangles[i1].Width();
	} );


	m_totalRectArea = 0;

	m_minRectHeight = std::numeric_limits<Int32>::max();
	m_maxRectHeight = 0;

	m_sortedRectangles.Resize( numRectangles );
	for ( Uint32 i = 0; i < numRectangles; ++i )
	{
		const Uint32 srcIndex = m_rectangleIndices[i];

		const Int32 w = rectangles[srcIndex].Width();
		const Int32 h = rectangles[srcIndex].Height();

		m_sortedRectangles[i].width = w;
		m_sortedRectangles[i].height = h;

		m_minRectHeight = Min( m_minRectHeight, h );
		m_maxRectHeight = Max( m_maxRectHeight, h );
		m_totalRectArea += w*h;
	}

	m_packedRectangles = rectangles;
	m_packedWidth = 0;
	m_packedHeight = 0;
}

void RectanglePacker::Pack()
{
	RED_ASSERT( m_packedRectangles.Size() == m_rectangleIndices.Size() );
	RED_ASSERT( m_packedRectangles.Size() == m_sortedRectangles.Size() );

	const Uint32 numRectangles = m_sortedRectangles.Size();

	if ( numRectangles == 0 )
	{
		m_packedWidth = 0;
		m_packedHeight = 0;
		return;
	}


	const Int32 maxRectWidthAligned = AlignUp( m_sortedRectangles[0].width );
	const Int32 maxRectHeightAligned = AlignUp( m_maxRectHeight );


	RectangleSize currentRect( maxRectWidthAligned, std::numeric_limits< Int32 >::max() );
	RectangleSize bestRect = currentRect;

	red::DynArray< Rect >			bestPackedRectangles { red::PoolFrame() };
	Uint32						bestPackedWidth;
	Uint32						bestPackedHeight;


	while ( true )
	{
		PackingGrid grid( currentRect.width, currentRect.height );

		Bool didFit = TryPack( grid );

		if ( didFit )
		{
			// Fit current rect to the packed size
			currentRect.height = m_packedHeight;

			// Track best fit so far
			Uint32 thisArea = currentRect.width * currentRect.height;
			Uint32 bestArea = bestRect.width * bestRect.height;
			if ( thisArea < bestArea )
			{
				bestRect = currentRect;
				bestPackedRectangles = m_packedRectangles;
				bestPackedWidth = m_packedWidth;
				bestPackedHeight = m_packedHeight;
			}
			// If the areas are the same, pick the one that results in the squarest packing.
			else if ( thisArea == bestArea && Abs( currentRect.width - currentRect.height ) < Abs( bestRect.width - bestRect.height ) )
			{
				bestRect = currentRect;
				bestPackedRectangles = m_packedRectangles;
				bestPackedWidth = m_packedWidth;
				bestPackedHeight = m_packedHeight;
			}


			// Decrease bounding height. Since m_packedHeight is the minimum bounds around the current packing, we know that
			// We won't be fitting the same set of rectangles into a shorter bounds with the same width. So we'll bump up the
			// width too.
			// NOTE : Article suggests an optimization of increasing [width] by the maximum [width] of the rectangles touching
			// the edge. In practice, I've found that this can result in missed opportunities. So the width is only increased
			// by a small amount.
			currentRect.height -= m_alignment;
			currentRect.width += m_alignment;

			// If the new size with decreased height has less area than the total area of all rectangles, then
			// it's obviously not going to be able to be packed, so increase width.
			if ( currentRect.width * currentRect.height < m_totalRectArea )
			{
				currentRect.width = ( ( m_totalRectArea + currentRect.height - 1 ) / currentRect.height );
			}
		}

		if ( !didFit )
		{
			currentRect.width += m_alignment;
		}


		// If the enclosing rectangle can't fit the tallest rect, we're done.
		if ( currentRect.height < maxRectHeightAligned )
		{
			break;
		}
	}


	m_packedRectangles	= std::move( bestPackedRectangles );
	m_packedWidth		= AlignUp( bestPackedWidth );
	m_packedHeight		= AlignUp( bestPackedHeight );
}



Bool RectanglePacker::TryPack( PackingGrid& grid )
{
	const Uint32 numRectangles = m_sortedRectangles.Size();

	m_packedWidth = 0;
	m_packedHeight = 0;

	for ( Uint32 i = 0; i < numRectangles; ++i )
	{
		Rect packedRect;
		if ( !TryFitRectangle( grid, m_sortedRectangles[i], packedRect ) )
		{
			return false;
		}

		m_packedWidth = Max<Uint32>( packedRect.m_right, m_packedWidth );
		m_packedHeight = Max<Uint32>( packedRect.m_bottom, m_packedHeight );

		const Uint32 outI = m_rectangleIndices[i];
		m_packedRectangles[outI] = packedRect;
	}

	return true;
}


Bool RectanglePacker::TryFitRectangle( PackingGrid& grid, const RectangleSize& unpaddedRect, Rect& outPackedRect )
{
	// Pad the size of the rectangle when we insert it into the grid, and then things will be aligned nicely.
	const Int32 w = AlignUp( unpaddedRect.width + m_spacing );
	const Int32 h = AlignUp( unpaddedRect.height + m_spacing );

	const Int32 numCol = grid.GetColumnCount();
	const Int32 numRow = grid.GetRowCount();

	// Find top-most position that the rectangle can fit in.

	for ( Int32 row = 0; row < numRow; ++row )
	{
		for ( Int32 col = 0; col < numCol; ++col )
		{
			if ( grid.IsFree( col, row, w, h ) )
			{
				outPackedRect.m_left	= grid.CellX( col );
				outPackedRect.m_top		= grid.CellY( row );
				outPackedRect.m_right	= outPackedRect.m_left + unpaddedRect.width;
				outPackedRect.m_bottom	= outPackedRect.m_top + unpaddedRect.height;

				grid.SplitColumn( col, w );
				grid.SplitRow( row, h );

				grid.Fill( col, row, w, h );

				return true;
			}
		}
	}

	return false;
}
