/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace rtti
{
	class ArrayType;
}

namespace script
{
	namespace debug
	{
		class Array : public IVariable
		{
		public:
			Array( const void* data, const rtti::ArrayType* type, const String& name );
			virtual ~Array() override final;

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;

		private:
			const void* m_data;
			const rtti::ArrayType* m_type;
			String m_name;
		};
	}
}
