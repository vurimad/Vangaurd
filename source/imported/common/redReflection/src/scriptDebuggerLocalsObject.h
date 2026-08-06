/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

class ISerializable;

namespace rtti
{
	class ClassType;
}

namespace script
{
	namespace debug
	{
		class Object : public IVariable
		{
			RED_BASE_CLASS( IVariable );
		public:
			Object( const void* object, const rtti::ClassType* classType );
			Object( const ISerializable* object );

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override final;
			virtual IVariablePtr FindChild( const red::StringView& name ) override final;
			virtual const rtti::IType* GetType() const override final;
			virtual String GetValue() const override final;
			virtual Uint32 GetAttributes() const override final;
			virtual const void* GetRaw() const override final;
			virtual String GetTypeName() const override final;

		private:
			const rtti::ClassType* GetBaseClass() const;
			void AddBaseClass( red::DynArray< IVariablePtr >& children ) const;
			void EnumProperties( red::DynArray< IVariablePtr >& children ) const;

			const void* m_object;
			const rtti::ClassType* m_class;
			const char* m_nativeTypeName = nullptr;
		};

		class AliasedObject : public Object
		{
		public:
			AliasedObject( const ISerializable* object, const AnsiChar* alias );
			AliasedObject( const ISerializable* object, const String& alias );
			AliasedObject( const void* object, const rtti::ClassType* classType, const String& alias );
			AliasedObject( const void* object, const rtti::ClassType* classType, const AnsiChar* alias );
			virtual String GetName() const override;

		private:
			String m_alias;
		};

		class NativeClassView : public IVariable
		{
		public:
			NativeClassView( const void* const object, const String& nativeTypeName );

			virtual red::DynArray< IVariablePtr > EnumerateChildren() override;
			virtual IVariablePtr FindChild( const red::StringView& name ) override;
			virtual String GetName() const override;
			virtual const rtti::IType* GetType() const override;
			virtual String GetValue() const override;
			virtual Uint32 GetAttributes() const override;
			virtual const void* GetRaw() const override;
			virtual String GetTypeName() const override;

		private:
			IVariablePtr CreateBaseClass( const Uint32 index, const dbgutils::ClassInfo::BaseClassInfo& baseClass );
			IVariablePtr CreateMember( const dbgutils::ClassInfo::MemberInfo& member );

		protected:
			const void* const m_object = nullptr;
			const String m_nativeTypeName;
		};
	}
}