/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace script
{
	namespace debug
	{
		class Virtual : public IVariable
		{
		public:
			Virtual( const String& name, const String& value );
			virtual ~Virtual() override final;

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;

		private:
			const String m_name;
			const String m_value;
		};
	}
}
