/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{

	RED_FORCE_INLINE ImageIterator::ImageIterator()
		: m_x( 0 ), m_y( 0 ), m_z( 0 ) 
		, m_minX( 0 ), m_minY( 0 ), m_minZ( 0 )
		, m_maxX( 0 ), m_maxY( 0 ), m_maxZ( 0 )
	{}

	RED_FORCE_INLINE ImageIterator::ImageIterator( const ImageIterator& other )
		: m_x( other.m_x ), m_y( other.m_y ), m_z( other.m_z )
		, m_minX( other.m_minX ), m_minY( other.m_minY ), m_minZ( other.m_minZ )
		, m_maxX( other.m_maxX ), m_maxY( other.m_maxY ), m_maxZ( other.m_maxZ )
	{
	}

	RED_FORCE_INLINE ImageIterator::ImageIterator(const Int32 x, const Int32 y, const Int32 z)
		: m_x( x ), m_y( y ), m_z( z )
		, m_minX( x ), m_minY( y ), m_minZ( z )
		, m_maxX( x+1 ), m_maxY( y+1 ), m_maxZ( z+1 )
	{
	}

	RED_FORCE_INLINE ImageIterator::ImageIterator( const Int32 minX, const Int32 minY, const Int32 minZ, const Int32 maxX, const Int32 maxY, const Int32 maxZ )
		: m_minX( minX ), m_minY( minY ), m_minZ( minZ )
		, m_maxX( maxX ), m_maxY( maxY ), m_maxZ( maxZ )
	{
		RED_ASSERT( m_maxX >= m_minX, "Swapped X range in image iterator" );
		RED_ASSERT( m_maxY >= m_minY, "Swapped Y range in image iterator" );
		RED_ASSERT( m_maxZ >= m_minZ, "Swapped Z range in image iterator" );
		Reset();
	}


	RED_FORCE_INLINE const Int32 ImageIterator::GetX() const
	{
		//RLPARANOID( m_x >= m_minX && m_x < m_maxX, "X is out of range" );
		return m_x;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetY() const
	{
		//RLPARANOID( m_y >= m_minY && m_y < m_maxY, "Y is out of range" );
		return m_y;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetZ() const
	{
		//RLPARANOID( m_z >= m_minZ && m_z < m_maxZ, "Z is out of range" );
		return m_z;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMaxX() const
	{
		return m_maxX;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMaxY() const
	{
		return m_maxY;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMaxZ() const
	{
		return m_maxZ;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMinX() const
	{
		return m_minX;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMinY() const
	{
		return m_minY;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetMinZ() const
	{
		return m_minZ;
	}

	RED_FORCE_INLINE const Bool ImageIterator::Next()
	{	
		RED_ASSERT( !IsEmpty(), "Cannot iterate over empty image" );
		RED_ASSERT( (m_x >= m_minX) && (m_x < m_maxX), "Initial state of iterator is not valid" );
		RED_ASSERT( (m_y >= m_minY) && (m_y < m_maxY), "Initial state of iterator is not valid" );
		RED_ASSERT( (m_z >= m_minZ) && (m_z < m_maxZ), "Initial state of iterator is not valid" );

		if ( ++m_x == m_maxX )
		{
			m_x = m_minX;
			if ( ++m_y == m_maxY )
			{
				m_y = m_minY;
				if ( ++m_z == m_maxZ )
				{
					// revert to last pixel
					m_x = m_maxX-1;
					m_y = m_maxY-1;
					m_z = m_maxZ-1;
					return false;
				}
			}
		}

		// valid pixel
		return true;
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetSizeX() const
	{
		RED_ASSERT( m_maxX >= m_minX, "Swapped X range in image iterator" );
		return (m_maxX - m_minX);
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetSizeY() const
	{
		RED_ASSERT( m_maxY >= m_minY, "Swapped Y range in image iterator" );
		return (m_maxY - m_minY);
	}

	RED_FORCE_INLINE const Int32 ImageIterator::GetSizeZ() const
	{
		RED_ASSERT( m_maxZ >= m_minZ, "Swapped Z range in image iterator" );
		return (m_maxZ - m_minZ);
	}

	RED_FORCE_INLINE const Bool ImageIterator::IsEmpty() const
	{
		RED_ASSERT( m_maxX >= m_minX, "Swapped X range in image iterator" );
		RED_ASSERT( m_maxY >= m_minY, "Swapped Y range in image iterator" );
		RED_ASSERT( m_maxZ >= m_minZ, "Swapped Z range in image iterator" );
		return (m_maxX == m_minX) || (m_maxY == m_minY) || (m_maxZ == m_minZ);
	}

	RED_FORCE_INLINE void ImageIterator::Reset()
	{
		m_x = m_minX;
		m_y = m_minY;
		m_z = m_minZ;
	}

} // img