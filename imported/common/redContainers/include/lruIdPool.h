/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////
// Least recently used id generator
//////////////////////////////////////////////////////////////////////////

template< Uint32 COUNT >
class LRUIdPool
{
public:

	RED_INLINE LRUIdPool();
	RED_INLINE void Reset();
	RED_INLINE Uint32 GenerateId();
	RED_INLINE void ReleaseId( const Uint32 id );

private:

	Uint32 m_ids[ COUNT ];
	Uint32 m_firstFree;
	Uint32 m_numFree;
};

//////////////////////////////////////////////////////////////////////////

template< Uint32 COUNT >
RED_INLINE LRUIdPool< COUNT >::LRUIdPool()
{
	Reset();
}

template< Uint32 COUNT >
RED_INLINE void LRUIdPool< COUNT >::Reset()
{
	m_firstFree = 0;
	m_numFree = COUNT;
	for ( Uint32 i = 0; i < COUNT; ++i )
	{
		m_ids[ i ] = i;
	}
}

template< Uint32 COUNT >
RED_INLINE Uint32 LRUIdPool< COUNT >::GenerateId()
{
	RED_ASSERT( m_numFree > 0 );
	const Uint32 newId = m_ids[ m_firstFree ];
	m_firstFree = ( m_firstFree + 1 ) % COUNT;
	--m_numFree;
	return newId;
}

template< Uint32 COUNT >
RED_INLINE void LRUIdPool< COUNT >::ReleaseId( const Uint32 id )
{
	RED_ASSERT( m_numFree <= COUNT );
	m_ids[ ( m_firstFree + m_numFree ) % COUNT ] = id;
	++m_numFree;
}

} // red
