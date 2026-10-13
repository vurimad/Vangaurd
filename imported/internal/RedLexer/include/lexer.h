/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "api.h"

namespace lexer
{
	namespace native
	{
		class NATIVE_LEXER_API IListener;

		class NATIVE_LEXER_API Lexer
		{
		public:
			Lexer() = default;
			~Lexer() = default;

			// Start the lexer!
			// str = null terminated script file
			// listener = object that will recieve all emitted tokens
			void Tokenize( const char* str, IListener* listener ) const;
		};
	} // namespace native
} // namespace lexer
