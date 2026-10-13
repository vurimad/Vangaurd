/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redContainers/include/string/stringBuilder.h"

namespace rtti
{
	/// Helper class to build a RTTI value string
	class RED_REFLECTION_API ValueBuilder : public red::NonCopyable
	{
	public:
		typedef red::StringBuilder< String > TStringBuilder;

		// the value builder builds into a string builder
		ValueBuilder( TStringBuilder& builder );

		// emit start of the array
		RED_FORCE_INLINE void StartArray()
		{
			m_str->Append( '[' );
		}

		// emit end of the array
		RED_FORCE_INLINE void EndArray()
		{
			m_str->Append( ']' );
		}

		// emit start of the structure
		RED_FORCE_INLINE void StartStruct()
		{
			m_str->Append( '{' );
		}

		// emit end of the structure
		RED_FORCE_INLINE void EndStruct()
		{
			m_str->Append( '}' );
		}

		// emit start of the handle
		RED_FORCE_INLINE void StartHandle()
		{
			m_str->Append( '<' );
		}

		// emit end of the handle
		RED_FORCE_INLINE void EndHandle()
		{
			m_str->Append( '>' );
		}

		// emit '=' for key=value
		RED_FORCE_INLINE void Equals()
		{
			m_str->Append( '=' );
		}

		// emit value separator
		RED_FORCE_INLINE void Separator()
		{
			m_str->Append( ',' );
		}

		// emit identifier
		void Ident( const CName ident );

		// emit string - will put it in quotes if needed
		void Value( const AnsiChar* valueStr, const Bool forceQuotes = false );

	private:
		ValueBuilder();

		TStringBuilder*		m_str;

		// do we need special handling?
		static const Bool NeedQuotes( const AnsiChar* valueStr );
	};

} // rtti



