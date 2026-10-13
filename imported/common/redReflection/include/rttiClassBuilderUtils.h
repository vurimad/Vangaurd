/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClass.h"
#include "rttiNativeClass.h"
#include "rttiPropertyBuilder.h"
#include "rttiCategoryMetadataBuilder.h"
#include "rttiPropertyOverrideBuilder.h"
#include "rttiFunctionParamBuilder.h"
#include "rttiAbstractClass.h"
#include "rttiTypeName.h"
#include "rttiSystem.h"
#include "serializationUtils.h"
#include "rttiReplicationBinding.h"
#include "rttiMacrosUtils.h"
#include "handle.h"

RED_DISABLE_WARNING_CLANG( "-Winvalid-offsetof" )

class IScriptable;

namespace ent
{
	class Entity;
	class IComponent;
}

namespace rtti
{
	class ClassType;
	class Property;
	class PropertyBuilder;
	class PropertyOverrideBuilder;
	class Function;
	class FunctionParamBuilder;

	struct RED_REFLECTION_API PropertyBinder
	{
		static void AddPropertyToClass( const rtti::ClassType* classType, rtti::PropertyBuilder*& builder );
		static void AddPropertyOverrideToClass( const rtti::ClassType* classType, rtti::PropertyOverrideBuilder*& builder );
	};

	struct RED_REFLECTION_API FunctionParamBinder
	{
		static void AddParamToFunction( const rtti::Function* function, rtti::FunctionParamBuilder*& builder );
	};

	// TODO ctremblay, replace with enable_if when moving to MSVC 2015
	// For example:
	// template< typename T, typename std::enable_if_t< !std::is_base_of< IScriptable, T >::value > >
	// RED_FORCE_INLINE const rtti::ClassType * ExtractClass( const T* obj ){ return T::GetStaticClass(); }
	struct ClassExtrator
	{
		template< typename T >
		RED_FORCE_INLINE static const rtti::ClassType * Extract( const T * ) { return T::GetStaticClass(); }
	};

	struct RED_REFLECTION_API ScriptableClassExtrator
	{
		static const rtti::ClassType * Extract( const IScriptable * scriptable );
	};

	template< typename T >
	RED_INLINE const rtti::ClassType * ExtractClass( const T* obj ) 
	{ 
		typedef typename std::conditional< std::is_base_of< IScriptable, T >::value, ScriptableClassExtrator, ClassExtrator >::type ExtratorType;
		return ExtratorType::Extract( obj ); 
	}

	template< typename T >
	class ClassDescSetter
	{
	public:
		RED_FORCE_INLINE ClassDescSetter( const rtti::ClassType* classDesc )
		{
			T::sm_classDesc = classDesc;
		}
	};

	template< typename T, typename EventType >
	void RegisterEventConnector( CName functionName, ClassType * classType, void(T::*func)(const EventType&) )
	{
		static_assert(std::is_base_of< red::Event, EventType >::value, "Provided Event type must inherit from red::Event class.");
		static_assert(std::is_base_of< ISerializable, T >::value, "Only ISerializable object can register an event connector.");

		auto connector = [func]( ISerializable& object, const THandle< red::Event>& event )
		{
			T& listener = static_cast<T&>(object);
			(listener.*func)(static_cast<const EventType&>(*event));
		};

		classType->Internal_RegisterEventConnector( functionName, EventType::GetStaticClass(), connector );
	}

} // rtti

//////////////////////////////////////////////////////////////////////////
// 
#define _INTERNAL_RTTI_OFFSETOF( Type, Member ) offsetof( Type, Member )

//////////////////////////////////////////////////////////////////////////
// Because WinRT projects would convert L"m_abc" + 2 into a Platform::String^
namespace red
{
	namespace prv
	{
		// Get the string after m_ prefix
		template< Uint32 ArrayCount >
		struct PropertyNameHelper
		{
			static_assert( ArrayCount != 0, "Invalid ArrayCount" );
			static constexpr Uint32 c_prefixOffset = 2;
		};

		// Create an empty string (to keep backwards compatible behavior), but without overreading the buffer
		template<>
		struct PropertyNameHelper< 2 >
		{
			static constexpr Uint32 c_prefixOffset = 1;
		};

		// Create an empty string (to keep backwards compatible behavior), but without overreading the buffer
		template<>
		struct PropertyNameHelper< 1 >
		{
			static constexpr Uint32 c_prefixOffset = 0;
		};
	}
}

#define _INTERNAL_RTTI_PROPERTY_TXT( x ) (&(( x )[ ::red::prv::PropertyNameHelper<RED_ARRAY_COUNT_U32(x)>::c_prefixOffset ]))

//////////////////////////////////////////////////////////////////////////
// Internal macros which help with properties registration
#define _INTERNAL_RTTI_REGISTER_PROPERTIES_IN_TYPE( typeNameWithNamespaces )												\
	[]()																												\
	{																													\
		rtti::ClassType* registeredClass = const_cast<rtti::ClassType*>( typeNameWithNamespaces::GetStaticClass() );	\
		typeNameWithNamespaces::RegisterProperties( registeredClass );													\
		registeredClass->MarkAsInitialized();																			\
	},																													\
	false

#define _INTERNAL_RTTI_REGISTER_PROPERTIES_BODY( typeNameWithNamespaces )					\
inline void typeNameWithNamespaces::RegisterProperties( rtti::ClassType* registeredClass )	\
{																							\
	typedef typeNameWithNamespaces tCurrClassType;											\
	rtti::PropertyBuilder* currPropertyBuilder = nullptr;									\
	rtti::PropertyOverrideBuilder* currPropertyOverrideBuilder = nullptr;					\
	rtti::CategoryMetadataBuilder currCategoryMetadataBuilder;

#define _INTERNAL_RTTI_IMPLEMENT_TYPE_REGISTRATOR( typeNameWithNamespaces )	\
	_INTERNAL_RTTI_REGISTER_PROPERTIES_IN_TYPE( typeNameWithNamespaces ) );	\
	_INTERNAL_RTTI_REGISTER_PROPERTIES_BODY( typeNameWithNamespaces );


//////////////////////////////////////////////////////////////////////////
// Internal macros for type implementation
#define _INTERNAL_RTTI_IMPLEMENT_TYPE( _type ) \
	const rtti::ClassType* touchType##_type() { return _type::GetStaticClass(); }

#define _INTERNAL_RTTI_IMPLEMENT_TYPE_WITH_NAMESPACE( _type, namespace_, touchNamespaceClassFunc ) \
	const rtti::ClassType* touchNamespaceClassFunc() { return namespace_::_type::GetStaticClass(); }


#ifdef RED_CONFIGURATION_FINAL
#	define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING( typeName )
#	define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING_WITH_NAMESPACE( typeName, namespace_ )
#else
#	define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING( typeName ) \
		const AnsiChar* typeName::GetNativeTypeName() const { return ( #typeName ); }

#	define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING_WITH_NAMESPACE( typeName, namespace_ ) \
		const AnsiChar* namespace_::typeName::GetNativeTypeName() const { return ( #namespace_ "::" #typeName ); }
#endif

//////////////////////////////////////////////////////////////////////////
// Internal macros for type name implementation
#define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME( typeName )														\
	CName typeName::GetTypeName() { return RED_NAME_CONSTEXPR( #typeName ); }								\
	const rtti::ClassType * typeName::GetClass() const { return rtti::ExtractClass( this ); }				\
	const rtti::ClassType * typeName::GetNativeClass() const { return sm_classDesc; }						\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING( typeName )											\
	const rtti::ClassType * typeName::GetStaticClass() { return sm_classDesc; }

#define _INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_WITH_NAMESPACE( typeName, namespace_, namespacesString )			\
	CName namespace_::typeName::GetTypeName() { return RED_NAME_CONSTEXPR( namespacesString ); }			\
	const rtti::ClassType * namespace_::typeName::GetClass() const { return rtti::ExtractClass( this ); }	\
	const rtti::ClassType * namespace_::typeName::GetNativeClass() const { return sm_classDesc; }			\
	_INTERNAL_RTTI_IMPLEMENT_TYPE_NAME_SIMPLE_STRING_WITH_NAMESPACE( typeName, namespace_ )					\
	const rtti::ClassType * namespace_::typeName::GetStaticClass() { return sm_classDesc; }