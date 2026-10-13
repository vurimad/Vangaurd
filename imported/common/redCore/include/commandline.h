/*
 * Copyright (c) 2017 CDProjekt Red, Inc. All Rights Reserved.
 */

#pragma once

#include "../../redContainers/include/fundamentalStringConversion.h"

namespace red
{

// Holds parsed data from command line like strings, provides access to a single
// global CommandLine object. For valid command line styles see the comment below.
class REDCORE_API CommandLine
{
public:
	typedef red::DynArray< red::String > Params;

	// Initialise the global command line object (the one returned from Get())
	// using the string obtained from the main function.
	// Note that this string does not contain the executable name inside of it
	static void Init( const char* str );
	static void Init( const wchar_t* str );
	static void Init( int argc, const char* const argv[] );
	static void Init( int argc, const wchar_t* const argv[] );

	// Returns the global command line object for this application
	static const CommandLine& Get();

	// Returns a combined command line string
	// DO NOT USE for normal command line argument parsing
	static const String& Internal_GetRawCommandLine();

	// Returns a new CommandLine object that is parsed from the given string
	// This is intended to be used to parse an extra string from some other
	// input source and does not affect the global command line.
	// NOTE: These functions should mainly be used for testing
	static CommandLine Parse( const char* str );
	static CommandLine Parse( const wchar_t* str );
	static CommandLine Parse( int argc, const char* const argv[] );
	static CommandLine Parse( int argc, const wchar_t* const argv[] );

	// Parses command line arguments that are not from the OS
	// but instead ones that are hand crafted or specified in some other way.
	static CommandLine ParseInnerString( const red::String& str );

	CommandLine();

	// Gets the number of command line options
	Uint32 GetNumberOfOptions() const;

	// Checks if the specified option exists
	Bool HasOption( const red::String& option ) const;

	// Gets the values for a specified option
	const Params& GetValues( const red::String& option ) const;

	// Utility function to get a specific parameter value in a specific type
	template< typename T >
	Bool GetFirstParam( const red::String& option, T& outParam ) const;

	// Utility function to get all specific parameter values in a specific type
	template< typename T >
	Bool GetAllParams( const red::String& option, red::DynArray< T >& outParams ) const;

	// Utility function to get the parameter value
	Bool GetFirstOption( const red::String& option, red::String& outParam ) const;

private:
	red::HashMap< red::String, Params > m_options{ red::PoolEngine() };
};


/*
Supported Command Line formats:

Can replace:
	- with / 
	= with :

param
"param"
	HasOption("") -> true
	GetValues("") -> [ "param" ]

-opt
	HasOption("opt") -> true
	GetValues("opt") -> []

-opt param1
-opt "param1"
-opt=param1
-opt= param1
-opt =param1
-opt = param1
-opt="param1"
-opt= "param1"
-opt ="param1"
-opt = "param1"
"-opt param1"
"-opt=param1"
"-opt =param1"
	HasOption("opt") -> true
	GetValues("opt") -> [ "param1" ]

-opt " param1"
-opt=" param1"
"-opt= param1"
"-opt = param1"
	HasOption("opt") -> true
	GetValues("opt") -> [ " param1" ]

-opt param1 param2
-opt=param1 param2
-opt= param1 param2
-opt =param1 param2
-opt = param1 param2
-opt "param1" param2
-opt param1 "param2"
	HasOption("opt") -> true
	GetValues("opt") -> [ "param1", "param2" ]

-opt "param1 param2"
-opt="param1 param2"
-opt= "param1 param2"
-opt ="param1 param2"
-opt = "param1 param2"
	HasOption("opt") -> true
	GetValues("opt") -> [ "param1 param2" ]

-opt1 param1 param2 -opt2 param3 param4
-opt1 param1 -opt2 param3 param4 -opt1 param2
-opt1 param1 -opt2 param3 -opt1 param2 -opt2 param4
-opt1=param1 -opt2=param3 -opt1=param2 -opt2=param4
	HasOption("opt1") -> true
	GetValues("opt1") -> [ "param1", "param2" ]
	HasOption("opt2") -> true
	GetValues("opt2") -> [ "param3", "param4" ]

*/

RED_INLINE Uint32 CommandLine::GetNumberOfOptions() const
{
	return m_options.Size();
}

RED_INLINE Bool CommandLine::HasOption( const red::String& option ) const
{
	return m_options.KeyExist( option );
}

RED_INLINE const CommandLine::Params& CommandLine::GetValues( const red::String& option ) const
{
	return m_options[ option ];
}

template< typename T >
RED_INLINE Bool CommandLine::GetFirstParam( const red::String& option, T& outParam ) const
{
	const Params* params = m_options.FindPtr( option );

	if ( params && !params->Empty() )
	{
		return FromString( params->Front(), outParam );
	}

	return false;
}

// Utility function to get all specific parameter values in a specific type
template< typename T >
RED_INLINE Bool CommandLine::GetAllParams( const red::String& option, red::DynArray< T >& outParams ) const
{
	const Params* params = m_options.FindPtr( option );
	if ( params )
	{
		for ( const String& paramString : *params )
		{
			T param;
			if ( !FromString( paramString, param ) )
			{
				return false;
			}
			outParams.PushBack( std::move( param ) );
		}
		return true;	
	}
	return false;
}

RED_INLINE Bool CommandLine::GetFirstOption( const red::String& option, red::String& outParam ) const
{
	const Params* params = m_options.FindPtr( option );

	if ( params && !params->Empty() )
	{
		outParam = params->Front();
		return true;
	}

	return false;
}

} // red
