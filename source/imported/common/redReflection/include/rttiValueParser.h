/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace rtti
{
	/// Simple token walker
	class RED_REFLECTION_API ValueParser
	{
	public:
		ValueParser( const AnsiChar* txt );

		/// returns true if we have reached the end
		RED_FORCE_INLINE const Bool End()
		{
			SkipWhitespaces();
			return m_pos >= m_end;
		}

		/// if the next token is 'struct start' it eats it and returns true
		RED_FORCE_INLINE const Bool EatStructStart()
		{
			SkipWhitespaces();
			if ( *m_pos == '{' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is 'struct end' it eats it and returns true
		RED_FORCE_INLINE const Bool EatStructEnd()
		{
			SkipWhitespaces();
			if ( *m_pos == '}' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is 'array start' it eats it and returns true
		RED_FORCE_INLINE const Bool EatArrayStart()
		{
			SkipWhitespaces();
			if ( *m_pos == '[' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is 'struct end' it eats it and returns true
		RED_FORCE_INLINE const Bool EatArrayEnd()
		{
			SkipWhitespaces();
			if ( *m_pos == ']' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		RED_FORCE_INLINE const Bool EatEmptyArrayElement()
		{
			SkipWhitespaces();
			if ( *m_pos == '"' && *(m_pos+1) == '<' && *(m_pos+2) == '>' && *(m_pos+3) == '"' )
			{
				m_pos += 4;
				return true;
			}
			return false;
		}

		/// if the next token is '=' it eats it and returns true
		RED_FORCE_INLINE const Bool EatEquals()
		{
			SkipWhitespaces();
			if ( *m_pos == '=' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is ',' it eats it and returns true
		RED_FORCE_INLINE const Bool EatSeparator()
		{
			SkipWhitespaces();
			if ( *m_pos == ',' ) 
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is '<' it eats it and returns true
		RED_FORCE_INLINE const Bool EatHandleStart()
		{
			SkipWhitespaces();
			if ( *m_pos == '<' )
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// if the next token is '>' it eats it and returns true
		RED_FORCE_INLINE const Bool EatHandleEnd()
		{
			SkipWhitespaces();
			if ( *m_pos == '>' )
			{
				++m_pos;
				return true;
			}

			return false;
		}

		/// eat identifier (name), returns empty name if invalid
		const CName EatName();

		/// eat simple value - can be stored in quotes with escaped characters, removes that and restores original value
		const String EatValue();

	public:
		// skip to next non whitespace character
		void SkipWhitespaces();

		// parsing state
		const AnsiChar*		m_start;
		const AnsiChar*		m_pos;
		const AnsiChar*		m_end;
	};

} // rtti
