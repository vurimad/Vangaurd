/**
* Copyright (c) 2014 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "commandline.h"
#include "../../redSystem/include/crt.h"

namespace red
{

namespace prv
{

// Note: Maximum command line string size is 2047 characters (with 1 presumably for null character).
// We over allocate here because we're converting to utf-8 which can have 4 byte code points
constexpr Uint32 c_MaxCommandLineBufferSize = 8192;

//------------------------------------------------------------------------------

RED_INLINE bool IsWhitespace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

RED_INLINE bool IsDoubleQuote( char c )
{
	return c == '\"';
}

RED_INLINE bool IsOptionStart( char c )
{
	return c == '-' || c == '/';
}

RED_INLINE bool IsParamStart( char c )
{
	return c == '=' || c == ':';
}

RED_INLINE bool IsValidOptionChar( char c )
{
	return
		( c >= 'A' && c <= 'Z' ) ||
		( c >= 'a' && c <= 'z' ) ||
		( c >= '0' && c <= '9' ) ||
		( c == '-' ) || 
		( c == '_' );
}

template < typename CharFunction >
red::StringView ReadWhile( CharFunction testFunc, red::StringView& str )
{
	Uint32 i = 0;
	while ( i < str.Length() && testFunc( str[i] ) )
	{
		++i;
	}

	red::StringView result = str.Slice( 0, i );
	str.TrimFront( i );
	return result;
}

template < typename CharFunction >
red::StringView ReadUntil( CharFunction testFunc, red::StringView& str )
{
	Uint32 i = 0;
	while ( i < str.Length() && !testFunc( str[i] ) )
	{
		++i;
	}

	red::StringView result = str.Slice( 0, i );
	str.TrimFront( i );
	return result;
}

red::StringView ReadOptionName( red::StringView& str )
{
	RED_FATAL_ASSERT( IsOptionStart( str.Front() ), "Invalid option name" );
	str.TrimFront( 1 );

	return ReadWhile( IsValidOptionChar, str );
}

void SkipWhitespace( red::StringView& str )
{
	ReadWhile( IsWhitespace, str );
}

//------------------------------------------------------------------------------

class BaseCommandLineParser
{
protected:
	explicit BaseCommandLineParser( red::HashMap< red::String, CommandLine::Params >& options );

	CommandLine::Params& GetCurrentParams();
	void SetCurrentParams( const red::String& optionName );

	red::HashMap< red::String, CommandLine::Params >& m_options;

	// State
	CommandLine::Params* m_currentParams;
};

BaseCommandLineParser::BaseCommandLineParser( red::HashMap< red::String, CommandLine::Params >& options )
	: m_options( options )
	, m_currentParams( nullptr )
{
}

CommandLine::Params& BaseCommandLineParser::GetCurrentParams()
{
	if ( m_currentParams == nullptr )
	{
		SetCurrentParams( "" );
	}
	return *m_currentParams;
}

void BaseCommandLineParser::SetCurrentParams( const red::String& optionName )
{
	m_currentParams = &m_options.GetRef( optionName, red::PoolEngine() );
	if( !m_currentParams->Capacity() )
	{
		m_currentParams->SetPool( red::PoolEngine() );
	}
}

//------------------------------------------------------------------------------

class CommandLineParser : public BaseCommandLineParser
{
public:
	explicit CommandLineParser( red::HashMap< red::String, CommandLine::Params >& options );

	void Parse( red::StringView str );

private:
	red::String ReadParameter( red::StringView& str );
};

CommandLineParser::CommandLineParser( red::HashMap< red::String, CommandLine::Params >& options )
	: BaseCommandLineParser( options )
{
}

void CommandLineParser::Parse( red::StringView str )
{
	while ( !str.Empty() )
	{
		char c = str.Front();
		if ( IsWhitespace( c ) )
		{
			SkipWhitespace( str );
		}
		else if ( IsOptionStart( c ) )
		{
			str.TrimFront( 1 );

			red::String optionName = ReadWhile( IsValidOptionChar, str ).ToString();
			SetCurrentParams( optionName );
		}
		else if ( IsParamStart( c ) )
		{
			str.TrimFront( 1 );
		}
		else
		{
			GetCurrentParams().PushBack( ReadParameter( str ) );
		}
	}
}

red::String CommandLineParser::ReadParameter( red::StringView& str )
{
	red::String result;

	char buffer[c_MaxCommandLineBufferSize];
	Uint32 inIndex = 0, outIndex = 0;

	bool insideQuotes = false;

	while ( inIndex < str.Length() )
	{
		char c = str[inIndex];
		++inIndex;

		if ( c == '\"' )
		{
			insideQuotes = !insideQuotes;
		}
		else
		{
			if ( !insideQuotes && IsWhitespace( c ) )
			{
				break;
			}
			buffer[outIndex++] = c;

			if ( outIndex == c_MaxCommandLineBufferSize )
			{
				result.Append( buffer, outIndex );
				outIndex = 0;
			}
		}
	}

	if ( outIndex > 0 )
	{
		result.Append( buffer, outIndex );
	}

	str.TrimFront( inIndex );

	return result;

}

//------------------------------------------------------------------------------

class CommandLineArgvParser : public BaseCommandLineParser
{
public:
	explicit CommandLineArgvParser( red::HashMap< red::String, CommandLine::Params >& options );

	void ParseArg( red::StringView str );

private:
	void ParseParameter( red::StringView str );

	// State
	bool m_expectsParam;
};

CommandLineArgvParser::CommandLineArgvParser( red::HashMap< red::String, CommandLine::Params >& options )
	: BaseCommandLineParser( options )
	, m_expectsParam( false )
{
}

void CommandLineArgvParser::ParseArg( red::StringView str )
{
	if ( m_expectsParam )
	{
		// Treat the remainder of the string as a parameter to the last option specified
		// The string could be empty when someone passed in ""
		ParseParameter( str );
		return;
	}

	if ( !str.Empty() )
	{
		// ReadOption
		char c = str.Front();
		if ( IsOptionStart( c ) ) // Looks for - or /
		{
			red::String optionName = ReadOptionName( str ).ToString();
			SetCurrentParams( optionName );

			SkipWhitespace( str );
		}
	}

	if ( !str.Empty() )
	{
		char c = str.Front();
		if ( IsParamStart( c ) ) // Looks for = or :
		{
			str.TrimFront( 1 );
			m_expectsParam = true;
		}
	}

	if ( !str.Empty() )
	{
		ParseParameter( str );
	}
}

void CommandLineArgvParser::ParseParameter( red::StringView str )
{
	GetCurrentParams().PushBack( str.ToString() );
	m_expectsParam = false;
}

} // prv

//------------------------------------------------------------------------------

namespace prv
{
static CommandLine GCommandLine;
static String GRawCommandLineString;

const char* SkipExecutableName( const char* str )
{
#ifdef RED_PLATFORM_WINPC
	if ( str == ::GetCommandLineA() )
	{
		while ( *str != '\0' && !red::prv::IsWhitespace( *str ) )
		{
			++str;
		}
	}
#endif
	return str;
}

const wchar_t* SkipExecutableName( const wchar_t* str )
{
#ifdef RED_PLATFORM_WINPC
	if ( str == ::GetCommandLineW() )
	{
		while ( *str != '\0' && !red::IsWhiteSpace( *str ) )
		{
			++str;
		}
	}
#endif
	return str;
}

red::String CombineArgv( int argc, const char* const argv[] )
{
	size_t size = 0;
	for ( int i = 0; i < argc; ++i )
	{
		size += red::Strlen( argv[i] );
	}
	size += argc;

	red::String result;
	result.Reserve( static_cast< Uint32 >( size ) );

	result.Set( argv[0] );
	for ( int i = 1; i < argc; ++i )
	{
		result.Append( ' ' );
		result += argv[i];
	}

	return result;
}

red::String CombineArgv( int argc, const wchar_t* const argv[] )
{
	size_t size = 0;
	for ( int i = 0; i < argc; ++i )
	{
		size += red::Strlen( argv[i] );
	}
	size += argc;

	red::String result;
	result.Reserve( static_cast< Uint32 >( size ) );

	result.Set( UNICODE_TO_ANSI( argv[0] ) );
	for ( int i = 1; i < argc; ++i )
	{
		result.Append( ' ' );
		result += UNICODE_TO_ANSI( argv[i] );
	}

	return result;
}

} // prv

void CommandLine::Init( const char* str )
{
	prv::GRawCommandLineString = str;
	prv::GCommandLine = Parse( prv::SkipExecutableName( str ) );
}

void CommandLine::Init( const wchar_t* str )
{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	char buffer[ prv::c_MaxCommandLineBufferSize ];

	// Convert once the whole path to store in the command line string
	// NOTE: this could be using UNICODE_TO_ANSI but deciding to do the same conversion so the log can have the same bytes as we actually use
	RED_VERIFY( red::FileSystemStringToEngineString( str, buffer, prv::c_MaxCommandLineBufferSize ) );
	prv::GRawCommandLineString = buffer;

	// Convert the command line again without the executable name, this is because we compare the pointer versus the one we get from Windows
	RED_VERIFY( red::FileSystemStringToEngineString( prv::SkipExecutableName( str ), buffer, prv::c_MaxCommandLineBufferSize ) );
	prv::GCommandLine = Parse( buffer );
#else
	prv::GRawCommandLineString = UNICODE_TO_ANSI( str );
	prv::GCommandLine = Parse( prv::SkipExecutableName( str ) );
#endif
}

void CommandLine::Init( int argc, const char* const argv[] )
{
	prv::GRawCommandLineString = prv::CombineArgv( argc, argv );
	prv::GCommandLine = Parse( argc, argv );
}

void CommandLine::Init( int argc, const wchar_t* const argv[] )
{
	prv::GRawCommandLineString = prv::CombineArgv( argc, argv );
	prv::GCommandLine = Parse( argc, argv );
}

const CommandLine& CommandLine::Get()
{
	return prv::GCommandLine;
}

const String& CommandLine::Internal_GetRawCommandLine()
{
	return prv::GRawCommandLineString;
}

CommandLine CommandLine::Parse( const char* str )
{
	CommandLine result;
	prv::CommandLineParser parser( result.m_options );
	parser.Parse( str );
	return result;
}

CommandLine CommandLine::ParseInnerString( const String& str )
{
	return Parse( str.AsChar() );
}

CommandLine CommandLine::Parse( const wchar_t* str )
{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	char buffer[ prv::c_MaxCommandLineBufferSize ];
	RED_VERIFY( red::FileSystemStringToEngineString( str, buffer, prv::c_MaxCommandLineBufferSize ) );
	return Parse( buffer );
#else
	return Parse( UNICODE_TO_ANSI( str ) );
#endif
}

CommandLine CommandLine::Parse( int argc, const char* const argv[] )
{
	CommandLine result;
	prv::CommandLineArgvParser parser( result.m_options );
	// skip executable name stored in argv[0]
	for ( int i = 1; i < argc; ++i )
	{
		parser.ParseArg( argv[i] );
	}
	return result;
}

CommandLine CommandLine::Parse( int argc, const wchar_t* const argv[] )
{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
	char buffer[ prv::c_MaxCommandLineBufferSize ];
#endif

	CommandLine result;
	prv::CommandLineArgvParser parser( result.m_options );
	// skip executable name stored in argv[0]
	for ( int i = 1; i < argc; ++i )
	{
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		RED_VERIFY( red::FileSystemStringToEngineString( argv[i], buffer, prv::c_MaxCommandLineBufferSize ) );
		parser.ParseArg( buffer );
#else
		parser.ParseArg( UNICODE_TO_ANSI( argv[i] ) );
#endif
	}
	return result;
}

//------------------------------------------------------------------------------

CommandLine::CommandLine()
{
}

} // red
