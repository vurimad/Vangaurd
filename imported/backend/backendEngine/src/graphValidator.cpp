/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "graphValidator.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( GraphValidatorOutputEntryType, tools )
	RTTI_ENUM_OPTION( Warning );
	RTTI_ENUM_OPTION( Error );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( GraphValidatorOutputEntry, tools )
	RTTI_PROPERTY( m_type ).readOnly();
	RTTI_PROPERTY( m_message ).readOnly();
	RTTI_PROPERTY( m_graphIdChain ).readOnly();
	RTTI_PROPERTY( m_hasFixAction ).readOnly();
RTTI_END_TYPE()

namespace tools
{
	GraphValidatorOutputEntry::GraphValidatorOutputEntry()
		: m_type()
		, m_message( String::EMPTY() )
		, m_hasFixAction( false )
		, m_graphIdChain{ red::PoolBackend() }
	{
	}

	GraphValidatorOutputEntry::GraphValidatorOutputEntry( GraphValidatorOutputEntryType type, const String& message,
		const red::DynArray< GraphId >& graphIdChain, ValidatorEntryAction fixAction )
		: m_type( type )
		, m_message( message )
		, m_graphIdChain( graphIdChain )
		, m_fixAction( fixAction )
	{
		m_hasFixAction = fixAction ? true : false;
	}

	Bool GraphValidatorOutputEntry::Fix() const
	{
		if ( m_hasFixAction )
		{
			return m_fixAction();
		}
		return false;
	}

} // tools