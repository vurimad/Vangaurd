/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptExpressionParserPathElement.h"

namespace script { namespace expression
{
	Element::Element() = default;
	Element::Element( Element&& ) = default;
	Element::~Element() = default;

	Element::Element( const expression::ReadOnlyToken& token )
		: token( token )
	{
	}
} } // namespace script { namespace expression
