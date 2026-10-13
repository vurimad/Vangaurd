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
		class GeneratedVariable : public debug::IVariable
		{
			RED_BASE_CLASS( IVariable );

		public:
			GeneratedVariable( const String& value, const rtti::IType* type = nullptr );
			GeneratedVariable( const String& value, const String& typeName );

			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual String GetTypeName() const override final;

		private:
			String m_value;
			String m_typeName;
			const rtti::IType* m_type;
		};

		// Generated Value
		// Values must not clash with those output by bison
		const Uint32 CUSTOM_TOKEN_GENVAR = 1000;
	}
}
