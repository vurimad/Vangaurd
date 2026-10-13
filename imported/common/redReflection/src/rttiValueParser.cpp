/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiValueParser.h"
#include "../../redContainers/include/string/stringBuilder.h"

namespace rtti
{

	namespace helper
	{
		RED_FORCE_INLINE const Bool IsIdentChar( const Bool isFirst, const AnsiChar ch )
		{
			if ( ch == '_' || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') )
				return true;

			if ( !isFirst && (ch >= '0' && ch <= '9' ) )
				return true;

			return false;
		}

		RED_FORCE_INLINE const Bool IsValidStringChar( const AnsiChar ch )
		{
			if ( ch <= ' ' || ch == ',' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '<' || ch == '>' || ch == '\"' || ch == '=' )
				return false;

			return true;
		}
	}

	ValueParser::ValueParser( const AnsiChar* txt )
		: m_start( txt ? txt : "" )
	{
		m_pos = m_start;
		m_end = m_start + red::Strlen( m_start );
	}

	const CName ValueParser::EatName()
	{
		SkipWhitespaces();

		AnsiChar buf[ 256 ];

		AnsiChar* writeStart = buf;
		AnsiChar* writePtr = buf;
		AnsiChar* writeEnd = buf + RED_ARRAY_COUNT(buf) - 1;
		
		while ( m_pos < m_end )
		{
			const auto isFirst = (writeStart == writePtr);
			if ( !helper::IsIdentChar( isFirst, *m_pos ) )
				break;

			RED_ASSERT( writePtr < writeEnd, "Identifier is to long" );
			if ( writePtr < writeEnd )
				*writePtr++ = *m_pos;

			++m_pos;
		}

		*writePtr = 0;

		return RED_NAME( buf );
	}

	const String ValueParser::EatValue()
	{
		SkipWhitespaces();

		red::StringBuilder< String > ret;

		// we have two modes: string in quotes (that can have special characters like \n\r, etc) or a simple string with no quotes that is not allowed to have anything like that
		if ( *m_pos == '\"' )
		{
			++m_pos; // skip the quotes

			while ( m_pos < m_end )
			{
				// end of string
				if ( *m_pos == '\"' )
				{
					++m_pos;
					break;
				}

				// escapement
				if ( *m_pos == '\\' )
				{
					++m_pos;
					if ( *m_pos == 'n' )
					{
						ret.Append( "\n" );
						++m_pos;
					}
					else if ( *m_pos == 'r' )
					{
						++m_pos;
					}
					else if ( *m_pos == 't' )
					{
						ret.Append( "\t" );
						++m_pos;
					}
					else if ( *m_pos == '\\' )
					{
						ret.Append( "\\" );
						++m_pos;
					}
					else if ( *m_pos == '\'' )
					{
						ret.Append( "\'" );
						++m_pos;
					}
					else if ( *m_pos == '\"' )
					{
						ret.Append( "\"" );
						++m_pos;
					}
					else
					{
						if ( *m_pos >= ' ' )
							ret.Append( *m_pos++ );
						else
							++m_pos;
					}
				}
				else
				{
					if ( *m_pos >= 0 && *m_pos < ' ' )
						++m_pos;
					else
						ret.Append( *m_pos++ );
				}
			}
		}
		else
		{
			while ( m_pos < m_end )
			{
				if ( !helper::IsValidStringChar( *m_pos ) )
					break;

				ret.Append( *m_pos++ );
			}
		}

		return ret.ToString();
	}

	void ValueParser::SkipWhitespaces()
	{
		while ( (m_pos < m_end) && (*m_pos <= ' ') )
			++m_pos;
	}

} // rtti
