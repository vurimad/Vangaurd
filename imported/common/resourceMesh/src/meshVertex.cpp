/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshVertex.h"

void operator<<( IFile& file, SMeshVertex& v )
{
	// TODO bulk serialisation
	// this is only called if we are serializing one by one

	static_assert( sizeof( v.m_position ) == 12, "" );
	static_assert( sizeof( v.m_indices ) == 4, "" );
	static_assert( sizeof( v.m_weights ) == 16, "" );
	static_assert( sizeof( v.m_normal ) == 12, "" );
	static_assert( sizeof( v.m_uv0 ) == 8, "" );
	static_assert( sizeof( v.m_uv1 ) == 8, "" );
	static_assert( sizeof( v.m_tangent ) == 12, "" );
	static_assert( sizeof( v.m_binormal ) == 12, "" );
	static_assert( sizeof( v.m_extraData0 ) == 16, "" );
	static_assert( sizeof( v.m_extraData1 ) == 16, "" );
	static_assert( sizeof( v.m_extraData2 ) == 16, "" );
	static_assert( sizeof( v.m_extraData3 ) == 16, "" );

	file.Serialize( &v.m_position, sizeof( v.m_position ) );
	file.Serialize( &v.m_indices, sizeof( v.m_indices ) );
	file.Serialize( &v.m_weights, sizeof( v.m_weights ) );
	file.Serialize( &v.m_normal, sizeof( v.m_normal ) );
	file << v.m_color;
	file.Serialize( &v.m_uv0, sizeof( v.m_uv0 ) );
	file.Serialize( &v.m_uv1, sizeof( v.m_uv1 ) );
	file.Serialize( &v.m_tangent, sizeof( v.m_tangent ) );
	file.Serialize( &v.m_binormal, sizeof( v.m_binormal ) );

	file.Serialize( &v.m_extraData0, sizeof( v.m_extraData0 ) );
	file.Serialize( &v.m_extraData1, sizeof( v.m_extraData1 ) );
	file.Serialize( &v.m_extraData2, sizeof( v.m_extraData2 ) );
	file.Serialize( &v.m_extraData3, sizeof( v.m_extraData3 ) );
}
