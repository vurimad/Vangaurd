/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptExpressionParserToken.h"

#include "scriptDebuggerLocalsBase.h"

namespace script
{
	namespace expression
	{
		struct Element : public red::NonCopyable
		{
			Element();
			Element( Element&& );
			Element( const expression::ReadOnlyToken& token );
			~Element();

			ReadOnlyToken token;
			script::debug::IVariablePtr variable;
		};
	}
}