/**
* Copyright © 2013 CD Projekt Red. All Rights Reserved.
*/

namespace rend
{

	RED_FORCE_INLINE DebugFilterMask::DebugFilterMask()
	{
		m_bits.ClearAll();
	}

	RED_FORCE_INLINE DebugFilterMask& DebugFilterMask::operator|=( const Uint16 bitIndex )
	{
		m_bits.Set( bitIndex );
		return *this;
	}

	RED_FORCE_INLINE DebugFilterMask& DebugFilterMask::operator-=( const Uint16 bitIndex )
	{
		m_bits.Clear( bitIndex );
		return *this;	
	}

	RED_FORCE_INLINE DebugFilterMask& DebugFilterMask::operator^=( const Uint16 bitIndex )
	{
		m_bits.Toggle( bitIndex );
		return *this;
	}

} // rend
