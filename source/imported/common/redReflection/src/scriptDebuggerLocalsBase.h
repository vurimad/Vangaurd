/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace rtti
{
	class IType;
}

namespace script
{
	namespace debug
	{
		class IVariable;
		typedef red::UniquePtr< IVariable > IVariablePtr;

		//TODO: Move to comm protocol (Currently it's duplicated across engine and debugger)
		enum Attributes
		{
			/// Has children
			IsExpandable = 0,

			// Cannot be edited by a debugger
			ReadOnly,

			Abstract,
			Constant,

			// Value is not valid
			Invalid,

			// Access type
			AccessPublic,
			AccessPrivate,
			AccessProtected,

			// Is a boolean value
			TypeBoolean,

			// Value can be represented by a string
			TypeString,

			// Some other type of value
			TypeProperty,

			TypeClass,
			TypeBaseClass,
			TypeInnerClass,
			TypeMostDerivedClass
		};

		class IVariable
		{
			RED_USE_MEMORY_POOL( red::PoolScriptDebugger );

		public:
			IVariable();
			virtual ~IVariable();

			virtual red::DynArray< IVariablePtr > EnumerateChildren();
			virtual IVariablePtr FindChild( const red::StringView& name );

			virtual String GetName() const = 0;
			virtual const rtti::IType* GetType() const = 0;
			virtual String GetValue() const = 0;
			virtual Uint32 GetAttributes() const;

			virtual const void* GetRaw() const;

			virtual String GetTypeName() const;
		};
	}
}
