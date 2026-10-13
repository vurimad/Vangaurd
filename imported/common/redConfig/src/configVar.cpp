/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "configVar.h"
#include "configVarSystem.h"
#include "configVarRegistry.h"

//---

/* This is an example usage for refrerence
namespace Config
{
	TConfigVar<Int32 > cvExampleProp1( "Example", "TestInt", 30 );
	TConfigVar<Int32 > cvExampleProp2( "Example", "TestIntClamped", 30, -5, 5, eConsoleVarFlag_Save );
	TConfigVar<Float > cvExampleProp3( "Example", "TestFloat", 15.0f );
	TConfigVar<Float > cvExampleProp4( "Example", "TestTestFloatClamped", 1.1f, -5.0f, 5.0f, eConsoleVarFlag_Save );
	TConfigVar<Bool> cvExampleProp5( "Example", "TestBool", false );
	TConfigVar<Bool> cvExampleProp6( "Example", "TestBool2", true );
	TConfigVar<Int32> cvExampleProp7( "Example", "TestReadOnly", 666, eConsoleVarFlag_ReadOnly );
	TConfigVar<String> cvExampleProp8( "Example", "TestString", "FooBar" );
	TConfigVar<Int32> cvExampleProp9( "Example/SubGroup", "TestChild1", 1 );
	TConfigVar<Int32> cvExampleProp10( "Example/SubGroup", "TestChild2", 2 );
	TConfigVar<Int32> cvExampleProp11( "Example/SubGroup", "TestChild3", 3 );
	TConfigVar<Int32> cvExampleProp12( "Example/SubGroup2", "TestChild1", 4 );
	TConfigVar<Int32> cvExampleProp13( "Example/SubGroup2", "TestChild2", 5 );
	TConfigVar<Int32> cvExampleProp14( "Example/SubGroup2", "TestChild3", 6 );
	TConfigVar<Int32> cvExampleProp15( "Example/SubGroup2/Nested", "TestNestedChild1", -100 );
	TConfigVar<Int32> cvExampleProp16( "Example/SubGroup2/Nested", "TestNestedChild2", 0 );
	TConfigVar<Int32> cvExampleProp17( "Example/SubGroup2/Nested", "TestNestedChild3", 100 );
}
*/

//---


namespace Config
{

	TGlobalNotifiers IConfigVar::st_globalNotifiers = {};

#ifndef RED_CONFIGURATION_FINAL
	namespace Helpers
	{

		static Bool IsAlphaNum( const AnsiChar ch )
		{
			if ( ( ch >= '0' && ch <= '9' ) || ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) )
				return true;

			return false;
		}

		static void ValidateConfigVarName( const AnsiChar* name )
		{
			RED_FATAL_ASSERT( name != nullptr, "Variable name should be specified" );

			while ( *name )
			{
				RED_FATAL_ASSERT( *name > ' ', "Variable name should not contain white spaces" );
				RED_FATAL_ASSERT( IsAlphaNum(*name) || *name == '_', "Variable name should only contain alphanumeric characters or _" );
				++name;
			}
		}

		static void ValidateConfigVarGroup( const AnsiChar* name )
		{
			RED_FATAL_ASSERT( name != nullptr, "Variable group should be specified" );

			while ( *name )
			{
				RED_FATAL_ASSERT( *name > ' ', "Variable group should not contain white spaces" );
				RED_FATAL_ASSERT( IsAlphaNum(*name) || *name == '_' || *name == '/', "Variable group should only contain alphanumeric characters or '_' or '/'" );
				++name;
			}
		}

	}
#endif

	IConfigVar::IConfigVar( const AnsiChar* group, const AnsiChar* name, const Uint32 flags /*= 0*/ )
		: m_name( name )
		, m_group( group )
		, m_help( nullptr )
		, m_onValueChanged( nullptr )
		, m_flags( flags )
	{
#ifndef RED_CONFIGURATION_FINAL
		Helpers::ValidateConfigVarName( name );
		Helpers::ValidateConfigVarGroup( group );
#endif
	}

	IConfigVar::~IConfigVar()
	{
		Config::GetConfigSystem().GetRegistry().Unregister( *this );
	}

	void IConfigVar::Register()
	{
		// modify the value to whatever we have in the config files
		String value;
		if ( Config::GetConfigSystem().GetValue( GetGroup(), GetName(), value ) )
		{
			if ( !SetText( value ) )
			{
				RED_LOG_WARNING( "Core: Unable to set initial value of config var '%hs' in '%hs'", GetName(), GetGroup() );
			}
		}

		// add to the system
		Config::GetConfigSystem().GetRegistry().Register( *this );
	}

	void IConfigVar::NotifyChanged()
	{
		auto& lock = st_globalNotifiers.m_spinLock;
		RED_SCOPE_SHARED_LOCK( lock );

		for ( IConfigVarGlobalNotifier* notifier : st_globalNotifiers.m_notifiers )
		{
			notifier->NotifyCvarChanged( this );
		}
	}

	void IConfigVar::RegisterGlobalNotifier( IConfigVarGlobalNotifier* notifier )
	{
		auto& lock = st_globalNotifiers.m_spinLock;
		RED_SCOPE_LOCK( lock );

		RED_FATAL_ASSERT( st_globalNotifiers.m_notifiers.FindPtr(notifier) == nullptr, "Error: Adding Global Interop Notifier multipe times" );
		st_globalNotifiers.m_notifiers.PushBack(notifier);
	}

	void IConfigVar::UnRegisterGlobalNotifier( IConfigVarGlobalNotifier* notifier)
	{
		auto& lock = st_globalNotifiers.m_spinLock;
		RED_SCOPE_LOCK( lock );

		st_globalNotifiers.m_notifiers.Remove(notifier);
	}

	//////////////////////////////////////////////////////////////////////////

	void IConfigVarGlobalNotifier::NotifyCvarChanged( IConfigVar* changedVar )
	{
		RED_SCOPE_LOCK( m_configVarGlobalNotifierLock );
		OnChanged( changedVar );
	}

} // Console
