/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "state.h"

namespace lexer { namespace native {

State::State( const char* source )
	: m_source( source )
	, m_scopeLevel( 0 )
{

}

State::State( const State& ) = default;

}} // namespace lexer { namespace native {
