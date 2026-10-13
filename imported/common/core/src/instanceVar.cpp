/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "instanceVar.h"
#include "../../redReflection/include/rttiSystem.h"


Uint32 InstanceVar::MAX_OFFSET = 0xFFFFFFFF;

InstanceVar::InstanceVar()
	: m_offset( MAX_OFFSET )
	, m_numElem( 1 )
#ifdef RED_ASSERTS_ENABLED
	, m_index( 0xFFFF )
#endif
	, m_type( nullptr )
{

}


InstanceVar::InstanceVar( CName typeName )
	: m_offset( MAX_OFFSET )
	, m_numElem( 1 )
#ifdef RED_ASSERTS_ENABLED
	, m_index( 0xFFFF )
#endif
{
	// Find the RTTI type by name
	m_type = GetRttiSystem().FindType( typeName );
	if ( !m_type )
	{
		HALT( "Instance var uses unregistered type '%hs'. Please debug.", typeName.AsChar() );
	}
}

InstanceVar::InstanceVar( const rtti::IType* type )
	: m_offset( MAX_OFFSET )
	, m_numElem( 1 )
#ifdef RED_ASSERTS_ENABLED
	, m_index( 0xffff )
#endif
	, m_type( type )
{
}

void InstanceVar::Initialize( const rtti::IType* type )
{
	RED_ASSERT( m_offset == MAX_OFFSET, "InstanceVar (too) late initialization." );
	m_type = type;
}
