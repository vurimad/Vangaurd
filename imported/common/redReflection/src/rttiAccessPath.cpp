/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiAccessPath.h"

namespace rtti
{
	// TODO: Use dedicated string utilities when this path builder becomes hot.

	AccessPath AccessPath::operator[]( const Int32 index ) const
	{
		RED_ASSERT( index >= 0, "array index should be non negative" );

		AccessPath ret;
		ret.m_path = String::Printf( "%hs[%d]", m_path.AsChar(), index );
		return ret;
	}

	AccessPath AccessPath::operator[]( const Uint32 index ) const
	{
		AccessPath ret;
		ret.m_path = String::Printf( "%hs[%u]", m_path.AsChar(), index );
		return ret;
	}

	AccessPath AccessPath::operator[]( const AnsiChar* name ) const
	{
		RED_ASSERT( name && *name, "Empty child name" );

		if ( !name || !*name )
			return AccessPath(*this); // invalid element access

		AccessPath ret;

		ret.m_path = m_path;

		if ( !ret.m_path.Empty() )
			ret.m_path += ".";

		ret.m_path += name;
		return ret;
	}

	AccessPath AccessPath::operator[]( const String& name ) const
	{
		RED_ASSERT( !name.Empty(), "Empty child name" );

		if ( name.Empty() )
			return AccessPath(*this); // invalid element access

		AccessPath ret;

		ret.m_path = m_path;

		if ( !ret.m_path.Empty() )
			ret.m_path += ".";

		ret.m_path += name;
		return ret;
	}

	AccessPath AccessPath::operator[]( const CName name ) const
	{
		RED_ASSERT( !name.Empty(), "Empty child name" );

		if ( name.Empty() )
			return AccessPath(*this); // invalid element access

		AccessPath ret;

		ret.m_path = m_path;

		if ( !ret.m_path.Empty() )
			ret.m_path += ".";

		ret.m_path += name.AsChar();
		return ret;
	}

} // rtti

