/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace script
{
	namespace debug
	{
		class Simple : public IVariable
		{
		public:
			Simple( const void* data, const rtti::IType* type, const String& name );

			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;
			virtual const void* GetRaw() const override final;

		private:
			const rtti::IType* m_type;
			const void* m_data;
			String m_name;
		};
	}
}
