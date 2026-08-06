/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "configVar.h"
#include "configVarStorage.h"
#include "configVarRegistry.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::AnsiChar;
using red::String;
using red::DynArray;


namespace Config
{

CConfigVarRegistry::CConfigVarRegistry()
{
}

CConfigVarRegistry::~CConfigVarRegistry()
{
}


void CConfigVarRegistry::Refresh( const class CConfigVarStorage& storage )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// set all values we found a value for
	for ( auto it = m_vars.Begin(); it != m_vars.End(); ++it )
	{
		IConfigVar* var = it.Value();

		// do we have a value for this property in storage ?
		String configValue;
		if ( storage.GetEntry( var->GetGroup(), var->GetName(), configValue ) )
		{
			// set only if different
			String currentValue;
			if ( !var->GetText( currentValue ) || (currentValue != configValue) )
			{
				var->SetText( configValue );
			}
		}
	}
}

void CConfigVarRegistry::Capture( class CConfigVarStorage& storage ) const
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// we should care only about saveable values
	for ( auto it = m_vars.Begin(); it != m_vars.End(); ++it )
	{
		const IConfigVar* var = it.Value();

		if ( var->HasFlag( eConsoleVarFlag_Save ) )
		{
			// get current value
			String currentValue;
			if ( var->GetText( currentValue ) )
			{
				// store in the storage
				storage.SetEntry( var->GetGroup(), var->GetName(), currentValue );
			}
		}
	}
}

CConfigVarRegistry::TNameHash CConfigVarRegistry::CalcNameHash( const AnsiChar* name, const AnsiChar* groupName )
{
	TNameHash hash = red::CalculateHash32( name );
	hash = red::CalculateHash32( groupName, hash );
	return hash;
}

void CConfigVarRegistry::Register( IConfigVar& var )
{
	red::ScopedLock< red::Mutex > lock( m_lock );
	
	// calculate name hash
	const TNameHash hash = CalcNameHash( var.GetName(), var.GetGroup() );
	IConfigVar* existingVar = nullptr;
	if ( m_vars.Find( hash, existingVar ) )
	{
		if ( existingVar != &var )
		{
			if ( 0 == red::Strcmp(var.GetName(), existingVar->GetName()) )
			{
				RED_FATAL( "Console variable '%hs', group '%hs' is duplicated", var.GetName(), var.GetGroup() );
			}
			else
			{
				RED_FATAL( "Console variable '%hs', group '%hs' has hash collision!", var.GetName(), var.GetGroup() );
			}
		}

		// do not add twice
		return;
	}

	// add to list
	m_vars.Insert( hash, &var );
}

void CConfigVarRegistry::Unregister( IConfigVar& var )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// calculate name hash
	const TNameHash hash = CalcNameHash( var.GetName(), var.GetGroup() );
	IConfigVar* existingVal = nullptr;
	if ( m_vars.Find( hash, existingVal ) )
	{
		RED_FATAL_ASSERT( existingVal == &var, "Different console variable is registered" );
		m_vars.Remove( hash );
	}
}

IConfigVar* CConfigVarRegistry::Find( const AnsiChar* groupName, const AnsiChar* name ) const
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	const TNameHash hash = CalcNameHash( name, groupName );
	IConfigVar* existingVal = nullptr;
	if ( m_vars.Find( hash, existingVal ) )
	{
		if ( 0 == red::Strcmp( groupName, existingVal->GetGroup() ) )
		{
			if ( 0 == red::Strcmp( name, existingVal->GetName() ) )
			{
				return existingVal;
			}
		}
	}

	// not found
	return nullptr;
}

void CConfigVarRegistry::EnumVars( DynArray< IConfigVar* >& outVars, const AnsiChar* groupMatch /*= ""*/, const AnsiChar* nameMatch /*= ""*/, const Uint32 includeFlags /*= 0*/, const Uint32 excludeFlags /*= 0*/ ) const
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// linear scan
	for ( auto it = m_vars.Begin(); it != m_vars.End(); ++it )
	{
		IConfigVar* var = it.Value();

		// check group match
		if ( groupMatch && *groupMatch )
		{
			if ( nullptr == red::Strstr( var->GetGroup(), groupMatch ) )
				continue;
		}

		// check name match
		if ( nameMatch && *nameMatch )
		{
			if ( nullptr ==  red::Strstr( var->GetName(), nameMatch ) )
				continue;
		}

		// check inclusion flags
		if ( includeFlags && !var->HasFlag( (EConfigVarFlags)includeFlags ) )
		{
			continue;
		}

		// check exclusion flags
		if ( var->HasFlag( (EConfigVarFlags) excludeFlags ) )
		{
			continue;
		}

		// add to list
		outVars.PushBack( var );
	}
}

} // Console