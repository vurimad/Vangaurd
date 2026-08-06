/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptExpressionParserToken.h"

class CScriptStackFrame;

namespace script
{
	namespace debug
	{
		class IVariable;
		typedef red::UniquePtr< IVariable > IVariablePtr;
	}

	namespace expression
	{
		class RED_REFLECTION_API Path : red::NonCopyable
		{
		public:
			Path();
			~Path();

			void Add( const red::StringView& text, Uint32 id );

			Bool Build( const String& data );
			Bool Resolve( const CScriptStackFrame* in, script::debug::IVariablePtr& out );

			void Clear();

			red::DynArray< expression::ReadOnlyToken >::const_iterator begin() const { return m_tokens.Begin(); }
			red::DynArray< expression::ReadOnlyToken >::const_iterator end() const { return m_tokens.End(); }

		protected:
			red::DynArray< ReadOnlyToken > m_tokens{ red::PoolScript() };
		};
	}
}
