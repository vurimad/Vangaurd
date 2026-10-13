/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "worldNodeNameUtils.h"

#include "../../redContainers/include/string/stringUtils.h"

namespace red
{
	Bool ValidateGlobalNodeName( StringView name )
	{
		if ( name.Empty() )
			return false;

		Uint32 nameSize = name.Length();
		const AnsiChar* txt = name.Data();
		const AnsiChar* txtEnd = txt + nameSize;

		if ( *txt == '#' )
		{
			++txt;
			if ( txt == txtEnd )
				return false;                  // Sole '#' is not valid name.
		}
		else if ( *txt == '{' )                // Make sure '#' is either first, or right after '{'.
		{
			++txt;
			if ( txt == txtEnd )
				return false;
			if ( *txt == '#' )
			{
				++txt;
				if ( txt == txtEnd )
					return false;              // Only "{#" is not allowed.
			}
		}

		while ( txt != txtEnd )
		{
			const auto ch = *txt++;
			if ( (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || (ch == '_') || (ch == '-') || (ch == '{') || (ch == '}') )
				continue;

			return false;
		}

		return true;
	}

	void FixNodeName( String& name )
	{
		for ( Uint32 i = 0; i < name.Length(); )
		{
			const char c = name[i];
			if ( isalnum( c ) || c == '_' || c == '-' )
				i++;
			else
				name.RemoveAt( i );
		}
	}

	void FixGlobalNodeName( String& name )
	{
		for ( Uint32 i = 0; i < name.Length(); )
		{
			const char c = name[i];
			// same as "Name", but also allows '#' as the first character for flat names
			if ( isalnum( c ) || c == '_' || c == '-' || (c == '#' && i == 0) )
				i++;
			else
				name.RemoveAt( i );
		}
	}

	String MarkNodeNameAsManual( const red::StringView& initialName )
	{
		red::StringView coreName = red::GetCoreNameString( initialName );								/// @todo DP: make this code more like ExtractNameParts, maybe use it internally. For now I'm just moving code, not changing.
		red::StringView numericalSuffixString = initialName.SubView( coreName.Length() );

		while ( coreName.EndsWith( "0" ) )
		{
			coreName.TrimBack( 1 );
			numericalSuffixString = initialName.SubView( coreName.Length() );
		}

		if ( !numericalSuffixString.Empty() )
		{
			return String::Printf( "{%hs}%hs", coreName.ToString().AsChar(), numericalSuffixString.ToString().AsChar() );
		}
		else
		{
			return String::Printf( "{%hs}", coreName.ToString().AsChar() );
		}
	}

	String UnmarkNodeNameAsManual( const red::StringView& initialName )
	{
																		/// @todo DP: this code was copied from valueBoxName.cpp and worldTree_Node.cpp.
																		///           For now I just leave it this way to minimize changes.
																		///           At some point it would be good to use ExtractNameParts here...
		if ( initialName.StartsWith( '{' ) )
		{
			Int32 lastNonDigit = -1;
			for ( Int32 i = initialName.Length() - 1; i >= 0; --i )
			{
				if ( !isdigit( initialName[i] ) && initialName[i] != '_' )
				{
					lastNonDigit = i;
					break;
				}
			}

			String result = initialName.SubView(0, lastNonDigit + 1 ).ToString();
			result.TrimLeft( '{' );
			result.TrimRight( '}' );

			result += initialName.SubView( lastNonDigit + 1 ).ToString();
			return result;
		}

		return initialName.ToString();
	}

	Bool IsNodeNameMarkedAsManual( const red::StringView& initialName )
	{
		return initialName.StartsWith( '{' );														/// @todo DP: what about '#' ?
	}

} // red
