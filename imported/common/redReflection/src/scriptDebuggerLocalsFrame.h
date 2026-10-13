/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

class CScriptStackFrame;

namespace script
{
	namespace debug
	{
		class Frame : public IVariable
		{
		public:
			Frame( const CScriptStackFrame* frame );
			virtual ~Frame();

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual String GetName() const override final;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;

		private:
			const CScriptStackFrame* m_frame;
		};
	}
}
