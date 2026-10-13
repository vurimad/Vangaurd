/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"
#include "serializable.h"

namespace rtti
{
	class IType;
}

namespace script
{
	namespace debug
	{
		class Handle : public IVariable
		{
		public:
			Handle( const SerializableHandle* handle, const rtti::IType* type, const String& name );

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;
			virtual const void* GetRaw() const override final;

		private:
			const SerializableHandle* m_handle;
			const rtti::IType* m_type;
			String m_name;
		};
	}
}
