/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsBase.h"

#include "rttiType.h"

namespace script
{
	namespace debug
	{
		IVariable::IVariable() = default;
		IVariable::~IVariable() = default;

		red::DynArray< IVariablePtr > IVariable::EnumerateChildren()
		{
			return red::DynArray< IVariablePtr >(red::PoolDebug());
		}

		IVariablePtr IVariable::FindChild( const red::StringView& name )
		{
			return nullptr;
		}

		Uint32 IVariable::GetAttributes() const
		{
			return RED_FLAG( Attributes::ReadOnly );
		}

		const void* IVariable::GetRaw() const
		{
			return nullptr;
		}

		String IVariable::GetTypeName() const
		{
			const rtti::IType* type = GetType();

			if( type )
				return type->GetName().AsChar();

			return String::EMPTY();
		}
	}
}
