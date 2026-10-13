/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace rtti
{
	class Property;
}

namespace script
{
	namespace debug
	{
		class Property : public IVariable
		{
		public:
			Property( const rtti::IType* type, const void* data, const String& name );
			Property( const rtti::Property* property, const void* data );
			virtual ~Property() override final;

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual String GetName() const override final;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;
			virtual const void* GetRaw() const override final;

		private:
			IVariablePtr m_resolvedProperty;
			Uint32 m_attributes;
		};
	}
}
