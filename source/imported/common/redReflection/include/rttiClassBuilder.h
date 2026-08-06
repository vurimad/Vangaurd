/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassInternalBuilder.h"

//////////////////////////////////////////////////////////////////////////
// Set info about type alignment
#define RTTI_ALIGN_TYPE( alignment ) \
	registeredClass->SetAlignment( alignment );

//////////////////////////////////////////////////////////////////////////
// Set parent type for the currently defined type
// Static assert is not perfect, it should check serialization operator, but trait for that case is hard to create for all platforms.
#define RTTI_PARENT_TYPE( typeName ) \
	static_assert( std::is_polymorphic< typeName >::value, "Base class in rtti hierarchy must use RTTI_DECLARE_POLYMORPHIC_TYPE macro!" ); \
	static_assert( std::is_base_of< typeName, tCurrClassType >::value && !std::is_same<typeName, tCurrClassType>::value, "Class " #typeName " is not a base class!" ); \
	registeredClass->AddParentClass< tCurrClassType, typeName >();

#define RTTI_SCRIPT_ALIAS( name ) RTTIRegisterScriptAlias( registeredClass, name );

//////////////////////////////////////////////////////////////////////////
// Set custom backend for the type ( it is exatcly the same as customEditor() modifier 
// but not only for one property but for every property of that type )
#ifndef RED_CONFIGURATION_FINAL 
	#define RTTI_CUSTOM_EDITOR( customEditorName ) \
		registeredClass->SetCustomEditorName( RED_NAME_CONSTEXPR( customEditorName ) );
#else 
	#define RTTI_CUSTOM_EDITOR( customEditorName ) 
#endif

//////////////////////////////////////////////////////////////////////////
// Set category name for all properties which will be defined after this macro
#define RTTI_PROPERTY_CATEGORY( categoryName ) \
	currCategoryMetadataBuilder.ChangeContext( RED_NAME_CONSTEXPR( categoryName ) ); \
	currCategoryMetadataBuilder

#define RTTI_PROPERTY_CATEGORY_VAR( categoryName ) \
	currCategoryMetadataBuilder.ChangeContext( RED_NAME( categoryName ) ); \
	currCategoryMetadataBuilder

//////////////////////////////////////////////////////////////////////////
// Set import only flag for class
#define RTTI_IMPORT_ONLY() registeredClass->SetFlag( CF_ImportOnly, true )

//////////////////////////////////////////////////////////////////////////
// Set test only flag for class
#define RTTI_TEST_ONLY() \
	registeredClass->SetFlag( CF_TestOnly, true )

//////////////////////////////////////////////////////////////////////////
// Set visibility flag for class
#define RTTI_SET_VISIBILITY( visibility ) \
	static_assert( visibility == EClassFlags::CF_Private || visibility == EClassFlags::CF_Protected, "Invalid visibility flag" ); \
	registeredClass->SetFlag( visibility, true )

//////////////////////////////////////////////////////////////////////////
// Macro for a human-readable class name
#ifndef RED_CONFIGURATION_FINAL
	#define RTTI_DESCRIPTIVE_NAME( humanReadableName )			\
		registeredClass->SetDescriptiveName( #humanReadableName );
#else
	#define RTTI_DESCRIPTIVE_NAME( humanReadableName )
#endif

//////////////////////////////////////////////////////////////////////////
// Start definition of common type ( version for type in the global namespace )
#define RTTI_BEGIN_TYPE( typeName ) _INTERNAL_RTTI_BEGIN_TYPE( typeName );

//////////////////////////////////////////////////////////////////////////
// Start definition of type without default instance ( version for type in the global namespace )
#define RTTI_BEGIN_NODEFAULT_TYPE( typeName ) _INTERNAL_RTTI_BEGIN_NODEFAULT_TYPE( typeName );

//////////////////////////////////////////////////////////////////////////
// Start definition of type which cannot be instantiated, for example has pure virtual function ( version for type in the global namespace )
#define RTTI_BEGIN_ABSTRACT_TYPE( typeName ) _INTERNAL_RTTI_BEGIN_ABSTRACT_TYPE( typeName );

//////////////////////////////////////////////////////////////////////////
// Start definition of common type ( version for type in named namespaces )
#define RTTI_BEGIN_TYPE_IN_NAMESPACE( typeName, ... ) _INTERNAL_RTTI_BEGIN_TYPE_IN_NAMESPACE( typeName, BUILD_NAMESPACE( __VA_ARGS__ ), JOIN_TOKENS_MACRO( touchType, typeName, __VA_ARGS__ ), TO_STRING_MACRO( __VA_ARGS__, typeName ) );

//////////////////////////////////////////////////////////////////////////
// Start definition of type without default instance ( version for type in named namespaces )
#define RTTI_BEGIN_NODEFAULT_TYPE_IN_NAMESPACE( typeName, ... ) _INTERNAL_RTTI_BEGIN_NODEFAULT_TYPE_IN_NAMESPACE( typeName, BUILD_NAMESPACE( __VA_ARGS__ ), JOIN_TOKENS_MACRO( touchType, typeName, __VA_ARGS__ ), TO_STRING_MACRO( __VA_ARGS__, typeName ) );

//////////////////////////////////////////////////////////////////////////
// Start definition of type which cannot be instantiated, for example has pure virtual function ( version for type in named namespaces )
#define RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( typeName, ... ) _INTERNAL_RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( typeName, BUILD_NAMESPACE( __VA_ARGS__ ), JOIN_TOKENS_MACRO( touchType, typeName, __VA_ARGS__ ), TO_STRING_MACRO( __VA_ARGS__, typeName ) );

//////////////////////////////////////////////////////////////////////////
// General macro for property
#define RTTI_PROPERTY( field ) _INTERNAL_RTTI_PROPERTY( field )

//////////////////////////////////////////////////////////////////////////
// General macro for property override
#define RTTI_PROPERTY_OVERRIDE( field ) _INTERNAL_RTTI_PROPERTY_OVERRIDE( field )

//////////////////////////////////////////////////////////////////////////
// Macro for bitfield declared as pro
#define RTTI_PROPERTY_BITFIELD( field, type ) _INTERNAL_RTTI_PROPERTY_BITFIELD( field, type )

//////////////////////////////////////////////////////////////////////////
// Macro which will give you property name from field name
#define RTTI_PROPERTY_TXT( field ) _INTERNAL_RTTI_PROPERTY_TXT( field )

//////////////////////////////////////////////////////////////////////////
// Register an Event listener.
#define RED_EVENT_CONNECTOR( function ) rtti::RegisterEventConnector( RED_NAME_CONSTEXPR( "function" ),  registeredClass, &tCurrClassType::function )

//////////////////////////////////////////////////////////////////////////
// End type definition
#define RTTI_END_TYPE() \
	rtti::PropertyBinder::AddPropertyToClass( registeredClass, currPropertyBuilder ); \
	rtti::PropertyBinder::AddPropertyOverrideToClass( registeredClass, currPropertyOverrideBuilder ); \
}

//////////////////////////////////////////////////////////////////////////
//
#define RTTI_REPLICATED_CLASS_HANDLER( ReplicationHandlerClass )						\
	static ReplicationHandlerClass replicationHandler;								\
	rep::IRTTIService::GetInstance().SetReplicationHandler( registeredClass, &replicationHandler );


//////////////////////////////////////////////////////////////////////////
//
#define RTTI_REPLICATED_CLASS()								\
	rep::IRTTIService::GetInstance().MakeReplicable( registeredClass );

//////////////////////////////////////////////////////////////////////////
//
#define RTTI_REPLICATE_SUBCLASSES_AND_PROPERTIES()								\
	rep::IRTTIService::GetInstance().MakeReplicableSubclassesAndProperties( registeredClass );


//////////////////////////////////////////////////////////////////////////
//
#define RTTI_DECLARE_BASE_REPLICATED_CLASS()															\
	private:																							\
		rep::ObjectPtr m_replicatedObjectPtr;															\
	public:																								\
		RED_INLINE rep::ObjectPtr GetReplicatedObjectPtr() const { return m_replicatedObjectPtr; }		\
		RED_INLINE void SetReplicatedObjectPtr( const rep::ObjectPtr ptr ) { m_replicatedObjectPtr = ptr; }

//////////////////////////////////////////////////////////////////////////
//
#define RTTI_DEFINE_BASE_REPLICATED_CLASS()																		\
	rep::IRTTIService::GetInstance().RegisterRepPtrOffset( registeredClass, offsetof( tCurrClassType, m_replicatedObjectPtr ) );

//////////////////////////////////////////////////////////////////////////
//
#define RTTI_DISALLOW_TYPE_IN_NAMESPACE( typeName, ...  ) \
	_INTERNAL_RTTI_DISALLOW_TYPE_IN_NAMESPACE( typeName, JOIN_TOKENS_MACRO( touchType, typeName, __VA_ARGS__ ) )