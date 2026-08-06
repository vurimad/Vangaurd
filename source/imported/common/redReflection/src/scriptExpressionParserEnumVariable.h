/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace rtti
{
	class EnumType;
}

namespace script
{
	namespace expression
	{
		// Can represent both an enum type, or an enum option
		class EnumVariable : public debug::IVariable
		{
		public:
			EnumVariable( const rtti::EnumType* type, const red::StringView& option );
			EnumVariable( const rtti::EnumType* type, const CName& option );
			EnumVariable( const rtti::EnumType* type );

			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;

			virtual red::DynArray<debug::IVariablePtr> EnumerateChildren() override final;
			virtual debug::IVariablePtr FindChild( const red::StringView& name ) override final;

			virtual Uint32 GetAttributes() const override;

		private:
			CName m_option;
			const rtti::EnumType* m_type;
		};

		// Generated Value
		// Values must not clash with those output by bison
		const Uint32 CUSTOM_TOKEN_ENUMTYPE = 1001;
	}
}
