/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "rttiValueBuilder.h"

namespace rtti
{

	ValueBuilder::ValueBuilder( TStringBuilder& builder )
		: m_str( &builder )
	{
	}

	void ValueBuilder::Ident( const CName ident )
	{
		RED_ASSERT( ident, "Trying to emit empty identifier, generated value string will be totally broken" );
		m_str->Append( ident.AsChar() );
	}

	const Bool ValueBuilder::NeedQuotes( const AnsiChar* valueStr )
	{
		while ( *valueStr )
		{
			const auto ch = *valueStr++;

			// whitespaces
			if ( ch <= ' ' )
				return true;

			// reserved charactdrs
			if ( ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '<' || ch == '>' || ch == ',' || ch == '=' || ch == '\"' || ch == '\'' )
				return true;
		}

		return false;
	}

	void ValueBuilder::Value( const AnsiChar* valueStr, const Bool forceQuotes /*= false*/ )
	{
		// empty strings are supported directly
		if ( !valueStr || !*valueStr )
			return;

		// do we need to put string in quotes ?
		if ( forceQuotes || NeedQuotes( valueStr ) )
		{
			m_str->Append( '\"' );

			// put characters one by one
			while ( *valueStr )
			{
				const auto ch = *valueStr++;

				if ( ch == '\n' )
				{
					m_str->Append( "\\n" );
				}
				else if ( ch == '\t' )
				{
					m_str->Append( "\\t" );
				}
				else if ( ch == '\"' )
				{
					m_str->Append( "\\\"" );
				}
				else if ( ch == '\'' )
				{
					m_str->Append( "\\\'" );
				}
				else if ( ch == '\\' )
				{
					m_str->Append( "\\\\" );
				}
				else if ( ch >= 0 && ch < ' ' )
				{
					// do not preserve other white space characters
				}
				else
				{
					m_str->Append( ch );
				}
			}

			m_str->Append( '\"' );
		}
		else
		{
			// string is simple enough to just work
			m_str->Append( valueStr );
		}
	}

} // rtii