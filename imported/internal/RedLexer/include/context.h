/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "api.h"

namespace lexer
{
	namespace native
	{
		class NATIVE_LEXER_API Context
		{
		public:
			// The Line that this context refers to
			unsigned int m_line;

			// The associated position in the raw input buffer
			unsigned int m_byte;

			// The associated position in the input buffer resolved as a utf-8 string
			unsigned int m_character;

			Context()
				: m_line( 0 )
				, m_byte( 0 )
				, m_character( 0 )
			{
			}

			Context( const Context& ) = default;
		};

	} // namespace native {
} // namespace lexer
