/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{

//-----------------------------------------------------------------------------

class IndexIterator final
{
public:
	constexpr explicit IndexIterator( Uint32 index )
		: m_index( index )
	{}

	RED_INLINE IndexIterator operator++()
	{
		m_index++;
		return *this;
	}

	constexpr Uint32 operator*() const
	{
		return m_index;
	}

	constexpr bool operator==( IndexIterator other ) const
	{
		return m_index == other.m_index;
	}

	constexpr bool operator!=( IndexIterator other ) const
	{
		return m_index != other.m_index;
	}

private:
	Uint32 m_index;
};

//-----------------------------------------------------------------------------

class ReverseIndexIterator final
{
public:
	constexpr explicit ReverseIndexIterator( Uint32 index )
		: m_index( index )
	{}

	RED_INLINE ReverseIndexIterator operator++()
	{
		m_index--;
		return *this;
	}

	constexpr Uint32 operator*() const
	{
		return m_index - 1;
	}

	constexpr bool operator==( ReverseIndexIterator other ) const
	{
		return m_index == other.m_index;
	}

	constexpr bool operator!=( ReverseIndexIterator other ) const
	{
		return m_index != other.m_index;
	}

private:
	Uint32 m_index;
};

//-----------------------------------------------------------------------------

// Integer range denoting indexes from beginIndex (inclusive) increasing to endIndex (exclusive)
// Provides iterator support and simple operations
class IndexRange final
{
public:
	constexpr IndexRange()
		: m_beginIndex( 0 )
		, m_endIndex( 0 )
	{}

	constexpr IndexRange( Uint32 beginIndex, Uint32 endIndex )
		: m_beginIndex( beginIndex )
		, m_endIndex( endIndex )
	{}

	constexpr Uint32 StartIndex() const
	{
		return m_beginIndex;
	}

	constexpr Uint32 EndIndex() const
	{
		return m_endIndex;
	}

	constexpr Uint32 Size() const
	{
		return m_endIndex - m_beginIndex;
	}

	constexpr bool Empty() const
	{
		return m_beginIndex == m_endIndex;
	}

	constexpr bool Contains( Uint32 index ) const
	{
		return m_beginIndex <= index && index < m_endIndex;
	}

	constexpr bool Contains( const IndexRange& range ) const
	{
		return m_beginIndex <= range.m_beginIndex && range.m_endIndex <= m_endIndex;
	}

private:
	Uint32 m_beginIndex;
	Uint32 m_endIndex;
};

// Reversed integer range denoting indexes from beginIndex (exclusive) decreasing to endIndex (inclusive)
// Provides iterator support and simple functions
class ReverseIndexRange final
{
public:
	constexpr ReverseIndexRange()
		: m_beginIndex( 0 )
		, m_endIndex( 0 )
	{}

	constexpr ReverseIndexRange( Uint32 beginIndex, Uint32 endIndex )
		: m_beginIndex( beginIndex )
		, m_endIndex( endIndex )
	{}

	constexpr Uint32 StartIndex() const
	{
		return m_beginIndex;
	}

	constexpr Uint32 EndIndex() const
	{
		return m_endIndex;
	}

	constexpr Uint32 Size() const
	{
		return m_beginIndex - m_endIndex;
	}

	constexpr bool Empty() const
	{
		return m_beginIndex == m_endIndex;
	}

	constexpr bool Contains( Uint32 index ) const
	{
		return m_endIndex <= index && index < m_beginIndex;
	}

	constexpr bool Contains( const ReverseIndexRange& range ) const
	{
		return m_endIndex <= range.m_endIndex && range.m_beginIndex <= m_beginIndex;
	}

private:
	Uint32 m_beginIndex;
	Uint32 m_endIndex;
};

//-----------------------------------------------------------------------------

RED_INLINE IndexIterator begin( const IndexRange& ir )
{
	return IndexIterator( ir.StartIndex() );
}

RED_INLINE IndexIterator end( const IndexRange& ir )
{
	return IndexIterator( ir.EndIndex() );
}

RED_INLINE ReverseIndexIterator begin( const ReverseIndexRange& ir )
{
	return ReverseIndexIterator( ir.StartIndex() );
}

RED_INLINE ReverseIndexIterator end( const ReverseIndexRange& ir )
{
	return ReverseIndexIterator( ir.EndIndex() );
}

} // red
