/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"



//////////////////////////////////////////////////////////////////////////
// Below you can find all files which contain macros for reflection system ( RTTI system )
// These files are included in public header so probably you shouldn't include them directly
// You shouldn't use macros which start from prefix _INTERNAL_
#include "rttiSystem.h"			// contains few miscellaneous macros
#include "enumBuilder.h"		// contains macros related with enumeration types
#include "bitFieldBuilder.h"	// contains macros related with bitfield types
#include "rttiClassBuilder.h"	// contains macros related with classes ( reflection system does not distinguish classes and structures )
#include "rttiFunctionMacros.h"	// contains macros related with functions
#include "scriptStackFrame.h"	// contains macros related with scripts
#include "scriptable.h"


//////////////////////////////////////////////////////////////////////////
//
// TABLE OF CONTENT
//
// 1.0 Classes
// 1.1 Declare and define class which is not prepared for inheritance
// 1.2 Declare and define classes in inheritance hierarchy
// 1.3 Declare and define classes in inherited directly or indirectly from ISerializable
// 1.4 Declare and define class in namespace, which is not prepared for inheritance
// 1.5 Declare and define classes in inheritance hierarchy ( in one namespace )
// 1.6 Declare and define classes in inheritance hierarchy ( in different namespaces )
// 1.7 Declare and define classes in inherited directly or indirectly from ISerializable
// 1.8 Declare and define non copyable class
// 1.9 Declare and define abstract class
// 1.10 Declare and define abstract class in namespace
// 1.11 Declare and define class without default object
// 1.12 Declare and define class without default object in namespace
// 1.13 Set descriptive name for class type
// 1.14 Set custom alignment for class type
// 1.15 Set class as prepared only for import in scripts
// 1.16 Set class visibility
//
// 2.0 Properties
// 2.1 General case
// 2.2 Special case for bitfield property
// 2.3 Read only
// 2.4 Inlined
// 2.5 Editable
// 2.6 NotCooked
// 2.7 NotSerialized
// 2.8 Resizable
// 2.9 NoMask
// 2.10 SetName
// 2.11 Range
// 2.12 CustomEditor
// 2.13 Replicated
// 2.14 neverChanges
// 2.15 Property category
// 2.16 Modifiers chain
//
// 3.0 Functions
// 3.1 Register native function
// 3.2 Register native static function
// 3.3 Register native function with parameters
// 3.4 Register native static function with parameters
// 3.5 Register native global function
// 3.6 Register native global function with parameters
// 3.7 Mark function as replicable
// 3.8 Set different replication targets
// 3.9 Mark function as reliable
//
// 4.0 Enumerations
// 4.1 Declaration and definition enumeration type in global space
// 4.2 Declaration and definition enumeration type in one namespace
// 4.3 Declaration and definition enumeration type in few namespaces
// 4.4 Get all enumeration options from enumeration type description
// 4.5 Get all enumeration values from enumeration type description
// 4.6 Legacy enumeration options
//
// 5.0 Bitfield
// 5.1 Declaration and definition bitfield in global space
// 5.2 Declaration and definition bitfield in one namespace
// 5.3 Declaration and definition bitfield in few namespaces
//
// 6.0 Helper functions
// 6.1 Get RTTI object for native type
// 6.2 Get RTTI object for object
// 6.3 Find type in RTTI by type name
// 6.4 Find class in RTTI by type name
// 6.5 Find enum in RTTI by enum name
// 6.6 Find bitfield in RTTI by bitfield name
// 6.7 How to construct object from rtti type description
// 6.8 How to delete object which is created from rtti type description
// 6.9 Register type wrapper



//////////////////////////////////////////////////////////////////////////
// 1.0 Classes
// Classes and structures are the same from rtti system point of view

//////////////////////////////////////////////////////////////////////////
// 1.1 Declare and define class which is not prepared for inheritance
class Sample_Class_1_1
{
	RTTI_DECLARE_TYPE( Sample_Class_1_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_1 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.2 Declare and define classes in inheritance hierarchy
class Sample_Class_1_2_0
{
public:
	virtual ~Sample_Class_1_2_0() {}

	RTTI_DECLARE_POLYMORPHIC_TYPE( Sample_Class_1_2_0 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_2_0 );
RTTI_END_TYPE();

class Sample_Class_1_2_1 : public Sample_Class_1_2_0
{
	RTTI_DECLARE_TYPE( Sample_Class_1_2_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_2_1 );
	RTTI_PARENT_TYPE( Sample_Class_1_2_0 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.3 Declare and define classes in inherited directly or indirectly from ISerializable
class Sample_Class_1_3_0 : public ISerializable
{
	RTTI_DECLARE_TYPE( Sample_Class_1_3_0 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_3_0 );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

class Sample_Class_1_3_1 : public Sample_Class_1_3_0
{
	RTTI_DECLARE_TYPE( Sample_Class_1_3_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_3_1 );
	RTTI_PARENT_TYPE( Sample_Class_1_3_0 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.4 Declare and define class in namespace, which is not prepared for inheritance
namespace Sample_Namespace_1_4
{
	class Sample_Class_1_4
	{
		RTTI_DECLARE_TYPE( Sample_Class_1_4 );
	};
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_4, Sample_Namespace_1_4 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.5 Declare and define classes in inheritance hierarchy ( in one namespace )
namespace Sample_Namespace_1_5
{
	class Sample_Class_1_5_0
	{
	public:
		virtual ~Sample_Class_1_5_0() {}

		RTTI_DECLARE_POLYMORPHIC_TYPE( Sample_Class_1_5_0 );
	};

	class Sample_Class_1_5_1 : public Sample_Class_1_5_0
	{
		RTTI_DECLARE_TYPE( Sample_Class_1_5_1 );
	};
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_5_0, Sample_Namespace_1_5 );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_5_1, Sample_Namespace_1_5 );
	RTTI_PARENT_TYPE( Sample_Namespace_1_5::Sample_Class_1_5_0 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.6 Declare and define classes in inheritance hierarchy ( in different namespaces )
namespace Sample_Namespace_1_6_0
{
	class Sample_Class_1_6_0
	{
	public:
		virtual ~Sample_Class_1_6_0() {}

		RTTI_DECLARE_POLYMORPHIC_TYPE( Sample_Class_1_6_0 );
	};
}

namespace Sample_Namespace_1_6_1
{
	class Sample_Class_1_6_1 : public Sample_Namespace_1_6_0::Sample_Class_1_6_0
	{
		RTTI_DECLARE_TYPE( Sample_Class_1_6_1 );
	};
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_6_0, Sample_Namespace_1_6_0 );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_6_1, Sample_Namespace_1_6_1 );
	RTTI_PARENT_TYPE( Sample_Namespace_1_6_0::Sample_Class_1_6_0 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.7 Declare and define classes in inherited directly or indirectly from ISerializable
class Sample_Class_1_7_0 : public ISerializable
{
	RTTI_DECLARE_TYPE( Sample_Class_1_7_0 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_7_0 );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

class Sample_Class_1_7_1 : public Sample_Class_1_7_0
{
	RTTI_DECLARE_TYPE( Sample_Class_1_7_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_7_1 );
	RTTI_PARENT_TYPE( Sample_Class_1_7_0 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.8 Declare and define non copyable class
// If class type doesn't have copy constructor and copy operator then 
// rtti system automatically prepares non copyable description for that class type in rtti system
class Sample_Non_Copyable_Class_1_8
{
	RTTI_DECLARE_TYPE( Sample_Non_Copyable_Class_1_8 );

	Sample_Non_Copyable_Class_1_8( const Sample_Non_Copyable_Class_1_8& ) = delete;
	const Sample_Non_Copyable_Class_1_8& operator=( const Sample_Non_Copyable_Class_1_8& ) = delete;

public:
	Sample_Non_Copyable_Class_1_8() = default;
};

RTTI_BEGIN_TYPE( Sample_Non_Copyable_Class_1_8 );
RTTI_END_TYPE();

void Sample_Helper_Func_1_8()
{
	static_assert( !std::is_copy_assignable<Sample_Non_Copyable_Class_1_8>::value
		&& !std::is_copy_constructible<Sample_Non_Copyable_Class_1_8>::value,
		"Class Sample_Non_Copyable_Class_1_8 cannot be copied!");
}

//////////////////////////////////////////////////////////////////////////
// 1.9 Declare and define abstract class
class Sample_Abstract_Class_1_9
{
	RTTI_DECLARE_TYPE( Sample_Abstract_Class_1_9 );

	virtual void AbstractFunc() = 0;
};

RTTI_BEGIN_ABSTRACT_TYPE( Sample_Abstract_Class_1_9 );
RTTI_END_TYPE();

void Sample_Helper_Func_1_9()
{
	static_assert( std::is_abstract<Sample_Abstract_Class_1_9>::value, "Class Sample_Abstract_Class_1_9 must be abstract!" );
}

//////////////////////////////////////////////////////////////////////////
// 1.10 Declare and define abstract class in namespace
namespace Sample_Namespace_1_10
{
	class Sample_Abstract_Class_1_10
	{
		RTTI_DECLARE_TYPE( Sample_Abstract_Class_1_10 );

		virtual void AbstractFunc() = 0;
	};
}

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( Sample_Abstract_Class_1_10, Sample_Namespace_1_10 );
RTTI_END_TYPE();

void Sample_Helper_Func_1_10()
{
	static_assert(std::is_abstract<Sample_Namespace_1_10::Sample_Abstract_Class_1_10>::value, "Class Sample_Abstract_Class_1_10 must be abstract!" );
}

//////////////////////////////////////////////////////////////////////////
// 1.11 Declare and define class without default object
// Default object is used for serialization process to prepare diff between serialized object and default object
// If type doesn't have default object then is serializes in full
class Sample_Class_Without_Default_Object_1_11
{
	RTTI_DECLARE_TYPE( Sample_Class_Without_Default_Object_1_11 );
};

RTTI_BEGIN_NODEFAULT_TYPE( Sample_Class_Without_Default_Object_1_11 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.12 Declare and define class without default object in namespace
// Default object is used for serialization process to prepare diff between serialized object and default object
// If type doesn't have default object then is serializes in full
namespace Sample_Namespace_1_12
{
	class Sample_Class_Without_Default_Object_1_12
	{
		RTTI_DECLARE_TYPE( Sample_Class_Without_Default_Object_1_12 );
	};
}

RTTI_BEGIN_NODEFAULT_TYPE_IN_NAMESPACE( Sample_Class_Without_Default_Object_1_12, Sample_Namespace_1_12 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.13 Set descriptive name for class type
// Descriptive name:
//	- is useful when full type name is very long
//	- in most cases is using by editor
//	- is stripped in final build
namespace Sample_Namespace_1_13_0
{
	namespace Sample_Namespace_1_13_1
	{
		namespace Sample_Namespace_1_13_2
		{
			class Sample_Class_1_13
			{
				RTTI_DECLARE_TYPE( Sample_Class_1_13 );
			};
		}
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( Sample_Class_1_13, Sample_Namespace_1_13_0, Sample_Namespace_1_13_1, Sample_Namespace_1_13_2 );
	RTTI_DESCRIPTIVE_NAME( CustomSimpleClassName );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.14 Set custom alignment for class type
class Sample_Class_1_14
{
	RTTI_DECLARE_TYPE( Sample_Class_1_14 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_14 );
	RTTI_ALIGN_TYPE( 16 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.15 Set class as prepared only for import in scripts
class Sample_Class_1_15
{
	RTTI_DECLARE_TYPE( Sample_Class_1_15 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_15 );
	RTTI_IMPORT_ONLY();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 1.16 Set class visibility
class Sample_Class_1_16
{
	RTTI_DECLARE_TYPE( Sample_Class_1_16 );
};

RTTI_BEGIN_TYPE( Sample_Class_1_16 );
	RTTI_SET_VISIBILITY( EClassFlags::CF_Private );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.0 Properties

//////////////////////////////////////////////////////////////////////////
// 2.1 General case
class Sample_Class_2_1
{
	RTTI_DECLARE_TYPE( Sample_Class_2_1 );

private:
	Int32 m_var0;
	Float m_var1;
	String m_var2;
	CName m_var3;
};

RTTI_BEGIN_TYPE( Sample_Class_2_1 );
	RTTI_PROPERTY( m_var0 );
	RTTI_PROPERTY( m_var1 );
	RTTI_PROPERTY( m_var2 );
	RTTI_PROPERTY( m_var3 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.2 Special case for bitfield property
enum Sample_Enum_As_Bitfield_2_2
{
	Sample_Bitfield_Option_2_2_0
};

RTTI_DECLARE_BITFIELD( Sample_Enum_As_Bitfield_2_2 );

RTTI_BEGIN_BITFIELD( Sample_Enum_As_Bitfield_2_2, 4 );
	RTTI_BITFIELD_OPTION( Sample_Bitfield_Option_2_2_0 );
RTTI_END_BITFIELD();

class Sample_Class_2_2
{
	RTTI_DECLARE_TYPE( Sample_Class_2_2 );

private:
	Sample_Enum_As_Bitfield_2_2 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_2 );
	RTTI_PROPERTY_BITFIELD( m_var0, Sample_Enum_As_Bitfield_2_2 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.3 Read only
// This modifier:
//	- gives possibility to read value from variable in editor
//	- doesn't allow to write value to variable in editor
class Sample_Class_2_3
{
	RTTI_DECLARE_TYPE( Sample_Class_2_3 );

private:
	Int32 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_3 );
	RTTI_PROPERTY( m_var0 ).readOnly();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.4 Inlined
// This modifier:
//	- has sense only for THandles
//	- give possibility to create new object directly from editor
class Sample_Class_2_4_0 : public ISerializable
{
	RTTI_DECLARE_TYPE(Sample_Class_2_4_0);
};

class Sample_Class_2_4_1
{
	RTTI_DECLARE_TYPE(Sample_Class_2_4_1);

private:
	THandle<Sample_Class_2_4_0> m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_4_0 );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( Sample_Class_2_4_1 );
	RTTI_PROPERTY( m_var0 ).inlined();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.5 Editable
// This modifier:
//	- gives possibility to read value from variable in editor
//	- gives possibility to write value to variable in editor
class Sample_Class_2_5
{
	RTTI_DECLARE_TYPE( Sample_Class_2_5 );

private:
	Int32 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_5 );
	RTTI_PROPERTY(m_var0).editable();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.6 NotCooked
// This modifier:
//	- 
class Sample_Class_2_6
{
	RTTI_DECLARE_TYPE( Sample_Class_2_6 );

private:
	Int32 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_6 );
	RTTI_PROPERTY( m_var0 ).notCooked();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.7 NotSerialized
// This modifier:
//	- marks property as not saved and value of this property won't be saved to file/memory
class Sample_Class_2_7
{
	RTTI_DECLARE_TYPE( Sample_Class_2_7 );

private:
	Int32 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_7 );
	RTTI_PROPERTY( m_var0 ).notSerialized();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.8 Resizable
// This modifier:
//	- is useful only for DynArray
//	- gives possibility to change array size directly from editor
class Sample_Class_2_8
{
	RTTI_DECLARE_TYPE( Sample_Class_2_8 );

private:
	red::DynArray<Int32> m_array{ red::PoolRTTI() };
};

RTTI_BEGIN_TYPE( Sample_Class_2_8 );
	RTTI_PROPERTY( m_array ).resizable();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.9 NoMask
// This modifier:
//	- is only for bitfield properties
//	- gives possibility to set property as normal enumeration type ( only one value per property )
//	- if is not set then editor gives possibility to set particular bits in one property 
enum Sample_Enum_As_Bitfield_2_9
{
	Sample_Bitfield_Option_2_9_0
};

RTTI_DECLARE_BITFIELD( Sample_Enum_As_Bitfield_2_9 );

RTTI_BEGIN_BITFIELD( Sample_Enum_As_Bitfield_2_9, 4 );
	RTTI_BITFIELD_OPTION( Sample_Bitfield_Option_2_9_0 );
RTTI_END_BITFIELD();

class Sample_Class_2_9
{
	RTTI_DECLARE_TYPE( Sample_Class_2_9 );

private:
	Sample_Enum_As_Bitfield_2_9 m_bitField;
};

RTTI_BEGIN_TYPE( Sample_Class_2_9 );
	RTTI_PROPERTY_BITFIELD( m_bitField, Sample_Enum_As_Bitfield_2_9 ).noMask();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.10 SetName
// This modifier:
//	- change original property name to new one
class Sample_Class_2_10
{
	RTTI_DECLARE_TYPE( Sample_Class_2_10 );
private:
	Float m_var0;
};

RTTI_BEGIN_TYPE(Sample_Class_2_10);
	RTTI_PROPERTY(m_var0).setName( "MyCustomNameForProperty" );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.11 Range
// This modifier:
//	- gives possibility to set value only between range
//	- other values will be clamped to that range
class Sample_Class_2_11
{
	RTTI_DECLARE_TYPE( Sample_Class_2_11 );
private:
	Float m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_11 );
	RTTI_PROPERTY( m_var0 ).range( 0.0f, 1.0f );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.12 CustomEditor
// This modifier:
//	- set text key which is used by editor to prepare custom editor frontend and backend for that property
class Sample_Class_2_12_0
{
	RTTI_DECLARE_TYPE( Sample_Class_2_12_0 );
};

class Sample_Class_2_12_1
{
	RTTI_DECLARE_TYPE( Sample_Class_2_12_1 );
private:
	Sample_Class_2_12_0 m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_12_0 );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( Sample_Class_2_12_1 );
	RTTI_PROPERTY( m_var0 ).customEditor( "CustomEditorForMyClass" );
RTTI_END_TYPE();


//////////////////////////////////////////////////////////////////////////
// 2.13 Replicated
// This modifier:
//	- set property as replicated by network system
class Sample_Class_2_13
{
	RTTI_DECLARE_TYPE( Sample_Class_2_13 );
private:
	Float m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_13 );
	RTTI_PROPERTY( m_var0 ).replicated();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.14 Replicated + neverChanges
// This modifier:
//	- set property as replicated by network system and marks property as 'never changes' which is used to optimize network bandwidth
class Sample_Class_2_14
{
	RTTI_DECLARE_TYPE( Sample_Class_2_14 );
private:
	Float m_var0;
};

RTTI_BEGIN_TYPE( Sample_Class_2_14 );
	RTTI_PROPERTY( m_var0 ).replicated().neverChanges();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.15 Property category
// This modifier:
//	- is used only by editor
//	- groups few properties in one section in editor
class Sample_Class_2_15
{
	RTTI_DECLARE_TYPE( Sample_Class_2_15 );
private:
	Float m_var0;
	Float m_var1;
	Int32 m_var2;
	Int32 m_var3;
};

RTTI_BEGIN_TYPE( Sample_Class_2_15 );
	RTTI_PROPERTY_CATEGORY( "Floats" );
		RTTI_PROPERTY( m_var0 ).editable();
		RTTI_PROPERTY (m_var1 ).editable();
	RTTI_PROPERTY_CATEGORY( "Ints" );
		RTTI_PROPERTY( m_var2 ).editable();
		RTTI_PROPERTY( m_var3 ).editable();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 2.16 Modifiers chain
class Sample_Class_2_16_0 : public ISerializable
{
	RTTI_DECLARE_TYPE( Sample_Class_2_16_0 );
};

class Sample_Class_2_16_1
{
	RTTI_DECLARE_TYPE( Sample_Class_2_16_1 );

private:
	THandle<Sample_Class_2_16_0> m_var0;
	red::DynArray<Int32> m_var1{ red::PoolRTTI() };
};

RTTI_BEGIN_TYPE( Sample_Class_2_16_0 );
	RTTI_PARENT_TYPE( ISerializable );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( Sample_Class_2_16_1 );
	RTTI_PROPERTY( m_var0 ).inlined().editable().setName("MyNameForVariable").customEditor("MyCustomEditor");
	RTTI_PROPERTY( m_var1 ).editable().resizable().setName("ArrayOfInts");
RTTI_END_TYPE();



//////////////////////////////////////////////////////////////////////////
// 3.0 Functions

//////////////////////////////////////////////////////////////////////////
// 3.1 Register native function
class Sample_Class_3_1 : public IScriptable
{
	RTTI_DECLARE_TYPE( Sample_Class_3_1 );

	void funcToString( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		FINISH_PARAMETERS;
		RETURN_STRING( "Our Sample Class 3 1" );
	}
};

RTTI_BEGIN_TYPE( Sample_Class_3_1 );
	RTTI_NATIVE_FUNCTION( "ToString", funcToString );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 3.2 Register native static function
class Sample_Class_3_2
{
	RTTI_DECLARE_TYPE( Sample_Class_3_2 );

	static void Sample_Func_3_2( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		FINISH_PARAMETERS;

		const auto randomNumber = 2; // chosen by fair dice roll ;)
		RETURN_INT(randomNumber);
	}
};

RTTI_BEGIN_TYPE( Sample_Class_3_2 );
	RTTI_NATIVE_STATIC_FUNCTION( "SampleFunc32", Sample_Func_3_2 );
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 3.3 Register native function with parameters
class Sample_Class_3_3 : public IScriptable
{
	RTTI_DECLARE_TYPE( Sample_Class_3_3 );

	void Sample_Func_3_3( CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		GET_PARAMETER( Int32, myVar, 0 );
		FINISH_PARAMETERS;

		m_internalVar0 = myVar;
	}

private:
	Int32 m_internalVar0;
};

RTTI_BEGIN_TYPE( Sample_Class_3_3 );
	RTTI_BEGIN_NATIVE_FUNCTION( "SampleFunc33", Sample_Func_3_3 );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_FUNCTION();
RTTI_END_TYPE();


//////////////////////////////////////////////////////////////////////////
// 3.4 Register native static function with parameters
class Sample_Class_3_4
{
	RTTI_DECLARE_TYPE(Sample_Class_3_4);

	static void Sample_Func_3_4(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
	{
		GET_PARAMETER(Int32, myVar, 0);
		FINISH_PARAMETERS;

		static Int32 internalVar0 = myVar;
	}
};

RTTI_BEGIN_TYPE( Sample_Class_3_4 );
	RTTI_BEGIN_NATIVE_STATIC_FUNCTION( "SampleFunc34", Sample_Func_3_4 );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_STATIC_FUNCTION();
RTTI_END_TYPE();

//////////////////////////////////////////////////////////////////////////
// 3.5 Register native global function
void Sample_Func_3_5_0( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	FINISH_PARAMETERS;
	RETURN_VOID();
}

void Sample_Func_3_5_1()
{
	RTTI_DEFINE_NATIVE_GLOBAL_FUNCTION( "SampleFunc350", Sample_Func_3_5_0 );
}

//////////////////////////////////////////////////////////////////////////
// 3.6 Register native global function with parameters
void Sample_Func_3_6_0( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER(Int32, myVar, 0);
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_6_1()
{
	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc360", Sample_Func_3_6_0 );
	RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();
}

//////////////////////////////////////////////////////////////////////////
// 3.7 Mark function as replicable
void Sample_Func_3_7_0( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_7_1()
{
	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc370", Sample_Func_3_7_0 );
		RTTI_REPLICATED_FUNCTION();
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();
}

//////////////////////////////////////////////////////////////////////////
// 3.8 Set different replication targets
void Sample_Func_3_8_0( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_8_1( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_8_2( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_8_3( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_8_4()
{
	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc380", Sample_Func_3_8_0 );
		RTTI_REPLICATED_FUNCTION();
		RTTI_EXECUTION_TARGET( FET_Host );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();

	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc381", Sample_Func_3_8_1 );
		RTTI_REPLICATED_FUNCTION();
		RTTI_EXECUTION_TARGET( FET_Client );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();

	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc382", Sample_Func_3_8_2 );
		RTTI_REPLICATED_FUNCTION();
		RTTI_EXECUTION_TARGET( FET_Multicast );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();

	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc383", Sample_Func_3_8_3 );
		RTTI_REPLICATED_FUNCTION();
		RTTI_EXECUTION_TARGET( FET_Custom );
		RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();
}

//////////////////////////////////////////////////////////////////////////
// 3.9 Mark function as reliable
void Sample_Func_3_9_0( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Int32, myVar, 0 );
	FINISH_PARAMETERS;

	static Int32 internalVar0 = myVar;
}

void Sample_Func_3_9_1()
{
	RTTI_BEGIN_NATIVE_GLOBAL_FUNCTION( "SampleFunc390", Sample_Func_3_9_0 );
	RTTI_REPLICATED_FUNCTION();
	RTTI_RELIABLE_FUNCTION();
	RTTI_FUNC_PARAM( "myVar", Int32 );
	RTTI_END_NATIVE_GLOBAL_FUNCTION();
}


//////////////////////////////////////////////////////////////////////////
// 4.0 Enumerations
// - Enumeration types and enumeration class types are from rtti system point of view

//////////////////////////////////////////////////////////////////////////
// 4.1 Declaration and definition enumeration type in global space
enum Sample_Enum_4_1
{
	Sample_Enum_Option_4_1_0,
	Sample_Enum_Option_4_1_1 = 7,
	Sample_Enum_Option_4_1_2,
	Sample_Enum_Option_4_1_3,
};

RTTI_DECLARE_ENUM( Sample_Enum_4_1 );

RTTI_BEGIN_ENUM( Sample_Enum_4_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_1_0 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_1_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_1_2 );
	RTTI_ENUM_OPTION_ALIAS( "MyCustomEnumOptionName413", Sample_Enum_Option_4_1_3 );
RTTI_END_ENUM();

//////////////////////////////////////////////////////////////////////////
// 4.2 Declaration and definition enumeration type in one namespace
namespace Sample_Namespace_4_2
{
	enum Sample_Enum_4_2
	{
		Sample_Enum_Option_4_2_0,
		Sample_Enum_Option_4_2_1 = 7,
		Sample_Enum_Option_4_2_2,
		Sample_Enum_Option_4_2_3,
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( Sample_Enum_4_2, Sample_Namespace_4_2 );

RTTI_BEGIN_ENUM_IN_NAMESPACE( Sample_Enum_4_2, Sample_Namespace_4_2 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_2_0 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_2_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_2_2 );
	RTTI_ENUM_OPTION_ALIAS( "MyCustomEnumOptionName423", Sample_Enum_Option_4_2_3 );
RTTI_END_ENUM();

//////////////////////////////////////////////////////////////////////////
// 4.3 Declaration and definition enumeration type in few namespaces
namespace Sample_Namespace_4_3_0
{
	namespace Sample_Namespace_4_3_1
	{
		enum Sample_Enum_4_3
		{
			Sample_Enum_Option_4_3_0,
			Sample_Enum_Option_4_3_1 = 7,
			Sample_Enum_Option_4_3_2,
			Sample_Enum_Option_4_3_3,
		};
	}
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( Sample_Enum_4_3, Sample_Namespace_4_3_0, Sample_Namespace_4_3_1 );

RTTI_BEGIN_ENUM_IN_NAMESPACE( Sample_Enum_4_3, Sample_Namespace_4_3_0, Sample_Namespace_4_3_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_3_0 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_3_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_3_2 );
	RTTI_ENUM_OPTION_ALIAS( "MyCustomEnumOptionName433", Sample_Enum_Option_4_3_3 );
RTTI_END_ENUM();

//////////////////////////////////////////////////////////////////////////
// 4.4 Get all enumeration options from enumeration type description
enum Sample_Enum_4_4
{
	Sample_Enum_Option_4_4_0,
	Sample_Enum_Option_4_4_1,
	Sample_Enum_Option_4_4_2,
};

RTTI_DECLARE_ENUM( Sample_Enum_4_4 );

RTTI_BEGIN_ENUM( Sample_Enum_4_4 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_4_0 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_4_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_4_2 );
RTTI_END_ENUM();

void Sample_Helper_Func_4_4()
{
	const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR( "Sample_Enum_4_4" ) );
	if (enumDesc)
	{
		const red::DynArray< CName >& names = enumDesc->GetOptions();
		for ( CName name : names )
		{
			RED_LOG( "%s", name.AsChar() );
		}
	}
	else
	{
		RED_LOG( "Sample_Enum_4_4 type not exist in RTTI system." );
		RED_LOG( "Rtti system could have type with that name but it is not enum type." );
	}
}

//////////////////////////////////////////////////////////////////////////
// 4.5 Get all enumeration values from enumeration type description
enum Sample_Enum_4_5
{
	Sample_Enum_Option_4_5_0 = 10,
	Sample_Enum_Option_4_5_1 = 27,
	Sample_Enum_Option_4_5_2 = 108,
};

RTTI_DECLARE_ENUM( Sample_Enum_4_5 );

RTTI_BEGIN_ENUM( Sample_Enum_4_5 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_5_0 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_5_1 );
	RTTI_ENUM_OPTION( Sample_Enum_Option_4_5_2 );
RTTI_END_ENUM();

void Sample_Helper_Func_4_5()
{
	const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR( "Sample_Enum_4_5" ) );
	if (enumDesc)
	{
		const red::DynArray< Int64 >& values = enumDesc->GetValues();
		for ( Int64 value : values )
		{
			RED_LOG( "%d", value );
		}
	}
	else
	{
		RED_LOG("Sample_Enum_4_5 type not exist in RTTI system.");
		RED_LOG("Rtti system could have type with that name but it is not enum type.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 4.6 Legacy enumeration options
// Useful when renaming an enum option. Allows the value to be found using the old name (such as when
// loading a resource). Legacy name will not be used in saving, and will not be listed by GetOptions().
enum Sample_Enum_4_6
{
	Sample_Enum_Option_4_6_0,
	Sample_Enum_Option_4_6_1_with_a_new_name,
};

RTTI_DECLARE_ENUM( Sample_Enum_4_6 );

RTTI_BEGIN_ENUM( Sample_Enum_4_6 );
RTTI_ENUM_OPTION( Sample_Enum_Option_4_6_0 );
RTTI_ENUM_OPTION( Sample_Enum_Option_4_6_1_with_a_new_name ).legacyName( RED_NAME_CONSTEXPR("Sample_Enum_Option_4_6_1") );
RTTI_END_ENUM();

void Sample_Helper_Func_4_6()
{
	const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR( "Sample_Enum_4_6" ) );
	if (enumDesc)
	{
		Int64 valueForNewName, valueForOldName;
		if ( !enumDesc->FindValue( RED_NAME_CONSTEXPR("Sample_Enum_Option_4_6_1_with_a_new_name"), valueForNewName ) )
		{
			RED_LOG("Could not find value for Sample_Enum_Option_4_6_1_with_a_new_name");
		}
		if ( !enumDesc->FindValue( RED_NAME_CONSTEXPR("Sample_Enum_Option_4_6_1"), valueForOldName ) )
		{
			RED_LOG("Could not find value for Sample_Enum_Option_4_6_1");
		}

		if ( valueForNewName != valueForOldName )
		{
			RED_LOG("Old and new names do not have the same value");
		}

		const red::DynArray< CName >& names = enumDesc->GetOptions();
		for ( CName name : names )
		{
			RED_LOG( "%hs", name.AsChar() );
		}
	}
	else
	{
		RED_LOG("Sample_Enum_4_6 type not exist in RTTI system.");
		RED_LOG("Rtti system could have type with that name but it is not enum type.");
	}
}


//////////////////////////////////////////////////////////////////////////
// 5.0 Bitfields

//////////////////////////////////////////////////////////////////////////
// 5.1 Declaration and definition bitfield in global space
enum Sample_Enum_As_Bitfield_5_1
{
	Sample_Bitfield_Option_5_1_0 = RED_FLAG(0),
	Sample_Bitfield_Option_5_1_1 = RED_FLAG(1),
	Sample_Bitfield_Option_5_1_2 = RED_FLAG(2),
};

RTTI_DECLARE_BITFIELD( Sample_Enum_As_Bitfield_5_1 );

RTTI_BEGIN_BITFIELD( Sample_Enum_As_Bitfield_5_1, 4 );
	RTTI_BITFIELD_OPTION( Sample_Bitfield_Option_5_1_0 );
	RTTI_BITFIELD_OPTION( Sample_Bitfield_Option_5_1_1 );
	RTTI_BITFIELD_OPTION_ALIAS( "MyCustomBitfieldOptionName512", Sample_Bitfield_Option_5_1_2 );
RTTI_END_BITFIELD();

//////////////////////////////////////////////////////////////////////////
// 5.2 Declaration and definition bitfield in one namespace
namespace Sample_Namespace_5_2
{
	enum Sample_Enum_As_Bitfield_5_2
	{
		Sample_Option_5_2_0 = RED_FLAG(0),
		Sample_Option_5_2_1 = RED_FLAG(1),
		Sample_Option_5_2_2 = RED_FLAG(2),
	};
}

RTTI_DECLARE_BITFIELD_IN_NAMESPACE( Sample_Enum_As_Bitfield_5_2, Sample_Namespace_5_2 );

RTTI_BEGIN_BITFIELD_IN_NAMESPACE( Sample_Enum_As_Bitfield_5_2, Sample_Namespace_5_2 );
	RTTI_BITFIELD_OPTION( Sample_Option_5_2_0 );
	RTTI_BITFIELD_OPTION( Sample_Option_5_2_1 );
	RTTI_BITFIELD_OPTION_ALIAS( "MyCustomBitfieldOptionName522", Sample_Option_5_2_2 );
RTTI_END_BITFIELD();

//////////////////////////////////////////////////////////////////////////
// 5.3 Declaration and definition bitfield in few namespaces
namespace Sample_Namespace_5_3_0
{
	namespace Sample_Namespace_5_3_1
	{
		enum Sample_Enum_As_Bitfield_5_3
		{
			Sample_Option_5_3_0 = RED_FLAG(0),
			Sample_Option_5_3_1 = RED_FLAG(1),
			Sample_Option_5_3_2 = RED_FLAG(2),
		};
	}
}

RTTI_DECLARE_BITFIELD_IN_NAMESPACE( Sample_Enum_As_Bitfield_5_3, Sample_Namespace_5_3_0, Sample_Namespace_5_3_1 );

RTTI_BEGIN_BITFIELD_IN_NAMESPACE( Sample_Enum_As_Bitfield_5_3, Sample_Namespace_5_3_0, Sample_Namespace_5_3_1 );
	RTTI_BITFIELD_OPTION( Sample_Option_5_3_0 );
	RTTI_BITFIELD_OPTION( Sample_Option_5_3_1 );
	RTTI_BITFIELD_OPTION_ALIAS( "MyCustomBitfieldOptionName532", Sample_Option_5_3_2 );
RTTI_END_BITFIELD();



//////////////////////////////////////////////////////////////////////////
// 6.0 Helper functions

//////////////////////////////////////////////////////////////////////////
// 6.1 Get RTTI object for native type
class Sample_Class_6_1
{
	RTTI_DECLARE_TYPE( Sample_Class_6_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_1 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_1()
{
	const rtti::IType* typeDesc = GetTypeObject<Sample_Class_6_1>();
	if (typeDesc)
	{
		RED_LOG("Sample_Class_6_1 exists in rtti system");
	}
	else
	{
		RED_LOG("Sample_Class_6_1 not exist in rtti system");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.2 Get RTTI object for object
class Sample_Class_6_2
{
	RTTI_DECLARE_TYPE( Sample_Class_6_2 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_2 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_2()
{
	Sample_Class_6_2 object;

	const rtti::IType* typeDesc = GetTypeObject( object );
	if (typeDesc)
	{
		RED_LOG("Sample_Class_6_2 exists in rtti system");
	}
	else
	{
		RED_LOG("Sample_Class_6_2 not exist in rtti system");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.3 Find type in RTTI by type name
class Sample_Class_6_6
{
	RTTI_DECLARE_TYPE( Sample_Class_6_6 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_6 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_6()
{
	const rtti::IType* typeDesc = GetRttiSystem().FindType( RED_NAME_CONSTEXPR("Sample_Class_6_6") );
	if ( typeDesc )
	{
		RED_LOG("Sample_Class_6_6 type exists in RTTI system.");
	}
	else
	{
		RED_LOG("Sample_Class_6_6 type not exist in RTTI system.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.4 Find class in RTTI by type name
class Sample_Class_6_7
{
	RTTI_DECLARE_TYPE( Sample_Class_6_7 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_7 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_7()
{
	const rtti::ClassType* classDesc = GetRttiSystem().FindClass( RED_NAME_CONSTEXPR("Sample_Helper_Func_6_7") );
	if ( classDesc )
	{
		RED_LOG("Sample_Helper_Func_6_7 type exists in RTTI system.");
	}
	else
	{
		RED_LOG("Sample_Helper_Func_6_7 type not exist in RTTI system.");
		RED_LOG("Rtti system could have type with that name but it is not class type.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.5 Find enum in RTTI by enum name
enum Sample_Enum_6_8
{
	Sample_Enum_Option
};

RTTI_DECLARE_ENUM( Sample_Enum_6_8 );

RTTI_BEGIN_ENUM( Sample_Enum_6_8 );
RTTI_END_ENUM();

void Sample_Helper_Func_6_8()
{
	const rtti::EnumType* enumDesc = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR("Sample_Enum_6_8") );
	if ( enumDesc )
	{
		RED_LOG("Sample_Enum_6_8 type exists in RTTI system.");
	}
	else
	{
		RED_LOG("Sample_Enum_6_8 type not exist in RTTI system.");
		RED_LOG("Rtti system could have type with that name but it is not enum type.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.6 Find bitfield in RTTI by bitfield name
enum Sample_Enum_As_Bitfield_6_9
{
	Sample_Bitfield_Option_6_9_0
};

RTTI_DECLARE_BITFIELD( Sample_Enum_As_Bitfield_6_9 );

RTTI_BEGIN_BITFIELD( Sample_Enum_As_Bitfield_6_9, 4 );
	RTTI_BITFIELD_OPTION( Sample_Bitfield_Option_6_9_0 );
RTTI_END_BITFIELD();

void Sample_Helper_Func_6_9()
{
	const rtti::BitFieldType* bitfieldDesc = GetRttiSystem().FindBitField( RED_NAME_CONSTEXPR("Sample_Enum_As_Bitfield_6_9") );
	if ( bitfieldDesc )
	{
		RED_LOG("Sample_Enum_As_Bitfield_6_9 type exists in RTTI system.");
	}
	else
	{
		RED_LOG("Sample_Enum_As_Bitfield_6_9 type not exist in RTTI system.");
		RED_LOG("Rtti system could have type with that name but it is not bitfield type.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.7 How to construct object from rtti type description
class Sample_Class_6_10 : public ISerializable
{
	RTTI_DECLARE_TYPE( Sample_Class_6_10 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_10 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_10()
{
	const rtti::ClassType* classDesc = GetRttiSystem().FindClass( RED_NAME_CONSTEXPR("Sample_Class_6_10") );
	if (classDesc)
	{
		Sample_Class_6_10* newObject = classDesc->CreateObject<Sample_Class_6_10>();
		THandle<Sample_Class_6_10> newObjectInHandle = classDesc->CreateHandle<Sample_Class_6_10>();
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.8 How to delete object which is created from rtti type description
class Sample_Class_6_11 : public ISerializable
{
	RTTI_DECLARE_TYPE( Sample_Class_6_11 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_11 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_11()
{
	const rtti::ClassType* classDesc = GetRttiSystem().FindClass( RED_NAME_CONSTEXPR("Sample_Class_6_11") );
	if (classDesc)
	{
		Sample_Class_6_11* newObject = classDesc->CreateObject<Sample_Class_6_11>();
		classDesc->DestroyObject(newObject);

		THandle<Sample_Class_6_11> newObjectInHandle = classDesc->CreateHandle<Sample_Class_6_11>();
		RED_LOG("Inner object in THandle will be destroyed when last THandle object, which contais that inner object, will be destroyed.");
	}
}

//////////////////////////////////////////////////////////////////////////
// 6.9 Register type wrapper
class Sample_Class_6_15_0
{
};

class Sample_Class_6_15_1 : public Sample_Class_6_15_0
{
	RTTI_DECLARE_TYPE( Sample_Class_6_15_1 );
};

RTTI_BEGIN_TYPE( Sample_Class_6_15_1 );
RTTI_END_TYPE();

void Sample_Helper_Func_6_15()
{
	RTTI_REGISTER_TYPE_WRAPPER(Sample_Class_6_15_0, Sample_Class_6_15_1);

	const rtti::IType* typeDesc1 = GetTypeObject<Sample_Class_6_15_0>();
	const rtti::IType* typeDesc2 = GetTypeObject<Sample_Class_6_15_1>();

	if (typeDesc1 == typeDesc2)
	{
		RED_LOG("Sample_Class_6_15_0 and Sample_Class_6_15_1 are recognized in rtti system as the same type");
	}
}
