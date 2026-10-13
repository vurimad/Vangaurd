/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "state.h"

namespace lexer
{
	namespace native
	{
		// You will need to create a subclass of IListener in
		// order to receive the tokenised output of the lexer
		class NATIVE_LEXER_API IListener
		{
		public:
			IListener() = default;
			virtual ~IListener() = default;

			// A Script comment has been encountered (Could be either single or multiline)
			virtual void Comment( const State& lexerState ) = 0;

			// A Standard token
			virtual void Token( const State& lexerState, unsigned int id ) = 0;

			// A Token denoted by a start and end series of chars (such as a string or name)
			virtual void Sequence( const State& lexerState, unsigned int id ) = 0;

			// A section of script that has failed to match any token rules
			virtual void Error( const State& lexerState ) = 0;
		};
	} // namespace native
} // namespace lexer
