/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IFile;

namespace red
{
	/// Helper class that allows you to easily parse the content of a ANSI string file
	class RED_FILESYSTEM_API CAnsiStringFileReader
	{
	public:
		CAnsiStringFileReader();
		CAnsiStringFileReader( IFile* file ); // read the file content
		CAnsiStringFileReader( const String& string ); // copy from string
		~CAnsiStringFileReader();

		// parse matching keyword, optionally part of the keyword can be matched
		Bool ParseKeyword( const AnsiChar* keyword, const Uint32 length = (Uint32)-1, const Bool allowLineBreak = true );

		// parse a valid identifier (alphanumeric string, not in quotes)
		Bool ParseIdent( String& outIdent, const Bool allowLineBreak = true );

		// parse a valid identifier (alphanumeric string, not in quotes) with custom compare function
		Bool ParseIdentCustom( String& outIdent, const red::FixedSizeFunction< Bool( const AnsiChar c ) >& cmpFunc, const Bool allowLineBreak = true );

		// parse a generic number string (supports +-, integer, float, decimal point and optional 'f')
		Bool ParseNumber( String& outNumber, const Bool allowLineBreak = true );

		// parse string - will respect quotes
		Bool ParseString( String& outString, const Bool allowLineBreak = true );

		// parse string surrounded by given symbols
		Bool ParseStringBetweenSymbols( String& outString, const AnsiChar startSymbol, const AnsiChar endSymbol, const Bool ignoreWhitespaces = true, const Bool allowLineBreak = true );

		// parse a token - token is a single character, identifier or a string
		Bool ParseToken( String& outToken, const Bool allowLineBreak = true );

		// skip to the end of the current line
		void SkipCurrentLine();

		// get current line (for error reporting)
		Uint32 GetLine() const;

		// have we reached end of file ?
		Bool EndOfFile() const;

		Bool SkipWhitespaces( const Bool allowLineBreak );

	protected:
		AnsiChar*		m_buffer;
		
		const AnsiChar*	m_end;
		const AnsiChar*	m_pos;
		Uint32			m_line;		// current line
	};

} // Red