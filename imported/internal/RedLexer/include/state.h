/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "api.h"

#include "context.h"

namespace lexer
{
	namespace native
	{
		// Used to manage the state of the lexer internally
		class NATIVE_LEXER_API State
		{
		public:
			// The raw input
			const char* m_source;

			// The current context of the lexer
			// Usually this will be synonymous
			// with the "end" of the token emitted
			Context m_currentContext;

			// The start of the token emitted
			Context m_tokenStart;

			// The start of the sequence emitted
			// (a sequence being a token enclosed in a header/footer, such as a "string" or 'name')
			Context m_sequenceStart;

			// The context of the current line, at column 0
			Context m_lineStart;

			// The context of the line at the start of the section, at column 0
			Context m_sectionLineStart;

			// The number of opening braces '{' encountered up till now
			unsigned int m_scopeLevel;

			State( const char* source );
			State( const State& );
		};
	}
}
