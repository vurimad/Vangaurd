/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "commandletConsole.h"
#include "../../../common/redIO/include/redIOFile.h"

namespace
{

HANDLE InitStdHandle( DWORD id )
{
	HANDLE result = ::GetStdHandle( id );
	if ( result == nullptr )
	{
		result = INVALID_HANDLE_VALUE;
	}
	return result;
}

}

namespace commandlet
{

struct InteractiveConsole::RedirectInfo
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

	io::NativeFileHandle handle;
	Uint32 bytesWritten;
};


InteractiveConsole::InteractiveConsole()
	: m_inputHandle( INVALID_HANDLE_VALUE )
	, m_outputHandle( INVALID_HANDLE_VALUE )
	, m_originalInputMode( 0 )
	, m_originalOutputMode( 0 )
{
}

InteractiveConsole::~InteractiveConsole() = default;

bool InteractiveConsole::Initialise()
{
	m_inputHandle = InitStdHandle( STD_INPUT_HANDLE );
	m_outputHandle = InitStdHandle( STD_OUTPUT_HANDLE );
	if ( m_inputHandle == INVALID_HANDLE_VALUE || m_outputHandle == INVALID_HANDLE_VALUE )
	{
		return false;
	}

	if ( ::GetConsoleMode( m_inputHandle, &m_originalInputMode ) == FALSE ||
		::GetConsoleMode( m_outputHandle, &m_originalOutputMode ) == FALSE )
	{
		return false;
	}

	::SetConsoleMode( m_inputHandle, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT );

	return true;
}

void InteractiveConsole::Shutdown()
{
	if ( m_inputHandle != INVALID_HANDLE_VALUE )
	{
		::SetConsoleMode( m_inputHandle, m_originalInputMode );
		m_originalInputMode = 0;
	}
	if ( m_outputHandle != INVALID_HANDLE_VALUE )
	{
		::SetConsoleMode( m_outputHandle, m_originalOutputMode );
		m_originalOutputMode = 0;
	}
}

red::StringView InteractiveConsole::GetInput()
{
	red::StringView result;

	DWORD charsRead = 0;
	if ( ::ReadConsoleA( m_inputHandle, m_inputBuffer, m_inputBufferSize, &charsRead, nullptr ) != FALSE )
	{
		result = red::StringView( m_inputBuffer, charsRead );
		if ( result.EndsWith( "\r\n" ) )
		{
			result.TrimBack( 2 );
		}
	}
	return result;
}

void InteractiveConsole::PrintOutput( red::StringView str )
{
	if ( m_redirect )
	{
		Uint32 bytesWritten = 0;
		m_redirect->handle.Write( str.Data(), str.Length(), bytesWritten );
		m_redirect->bytesWritten += bytesWritten;
	}
	else
	{
		DWORD charsWritten = 0;
		::WriteConsoleA( m_outputHandle, str.Data(), str.Length(), &charsWritten, nullptr );
	}
}

void InteractiveConsole::Clear()
{
	COORD coordScreen = { 0, 0 };
	DWORD unused;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if( !GetConsoleScreenBufferInfo( m_outputHandle, &csbi ) )
	{
		return;
	}
	
	const Uint32 consoleSize = csbi.dwSize.X * csbi.dwSize.Y;

	if( !FillConsoleOutputCharacterA( m_outputHandle, ' ', consoleSize, coordScreen, &unused ) )
	{
		return;
	}

	if( !GetConsoleScreenBufferInfo( m_outputHandle, &csbi ) )
	{
		return;
	}

	if( !FillConsoleOutputAttribute( m_outputHandle, csbi.wAttributes, consoleSize, coordScreen, &unused ) )
	{
		return;
	}

	SetConsoleCursorPosition( m_outputHandle, coordScreen );
}

void InteractiveConsole::StartRedirect( const red::StringView& redirectFilename )
{
	auto redirect = red::CreateUniquePtr< InteractiveConsole::RedirectInfo >();
	auto filename = redirectFilename.ToString();
	if ( redirect->handle.Open( filename.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		auto message = String::Printf( "Redirecting output to %s\n", filename.AsChar() );
		PrintOutput( message );
		redirect->bytesWritten = 0;
		m_redirect = std::move( redirect );
	}
}

void InteractiveConsole::EndRedirect()
{
	auto redirect = std::move( m_redirect );
	auto summary = String::Printf( "Wrote %u bytes\n", redirect->bytesWritten );
	PrintOutput( summary.AsChar() );
}


} // commandlet
