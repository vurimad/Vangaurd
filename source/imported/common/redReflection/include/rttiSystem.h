/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiCommon.h"
#include "rttiTypeResolvingUtils.h"
#include "rttiComplexTypeCreator.h"

namespace rtti
{
	class IType;
	class EnumType;
	class ClassType;
	class BitFieldType;
	class Property;
	class Function;
	class IFunctionCollector;

	/// RTTI SYSTEM interface
	class RED_REFLECTION_API ITypeSystem
	{
	public:
		static ITypeSystem& GetInstance();

		/// Find type by name (any type)
		virtual const rtti::IType* FindType( CName name ) = 0;

		virtual const rtti::IType* FindTypeByHash( TypeHash typeHashCode ) = 0;

		/// Find class type definition (a little bit faster than general FindType)
		virtual const ClassType* FindClass( const CName name ) = 0;

		/// Find enum type definition (a little bit faster than general FindType)
		virtual const EnumType* FindEnum( const CName name ) = 0;

		/// Find bitfield type definition (a little bit faster than general FindType)
		virtual const BitFieldType* FindBitField( const CName name ) = 0;

		/// (DEPRECATED) Find global script function
		virtual const Function* FindGlobalFunction( const CName name ) = 0;
		virtual const Function* FindGlobalFunction( Uint64 hash ) = 0;

		virtual const IType* FindDuplicatedType( const IType* type ) = 0;

		/// Get all registered native types
		virtual void EnumNativeTypes( red::DynArray< const rtti::IType* >& types ) = 0;

		/// Get all registered global functions
		virtual void EnumGlobalFunctions( red::DynArray< const rtti::Function* >& functions ) = 0;

		/// Get all registered global functions from specified family
		virtual void EnumGlobalFunctionsFromFamily( rtti::IFunctionCollector& collector ) = 0;

		/// Enumerate all registered class functions
		virtual void EnumFunctions( red::DynArray< const rtti::Function* >& functions ) = 0;

		/// Enumerate all registered enums
		virtual void EnumEnums( red::DynArray< const rtti::EnumType* >& enums, Bool onlyScripted = false ) = 0;

		/// Enumerate all registered bit fields
		virtual void EnumBitFields( red::DynArray< const rtti::BitFieldType* >& bitFields, Bool onlyScripted = false ) = 0;

		/// Enumerate classes derived from given base class
		virtual void EnumClasses( const rtti::ClassType* baseClass, red::DynArray< const rtti::ClassType* > &classes, Bool classFilter( const rtti::ClassType * ) = NULL, Bool allowAbstract = false ) = 0;

		/// Enumerate classes directly derived from given class
		virtual void EnumDerivedClasses( const rtti::ClassType* baseClass, red::DynArray< const rtti::ClassType* > &classes ) = 0;

		/// Register type in the RTTI system
		virtual void RegisterType( rtti::IType *type, TypeHash hash ) = 0;

		/// Register wrapper type in the RTTI system ( the same type could be find by more than one hash )
		virtual void RegisterTypeWrapper( TypeHash existingTypeHash, TypeHash wrappedTypeHash ) = 0;

		/// Unregister type from RTTI system (on DLL unloading)
		virtual void UnregisterType( const rtti::IType *type ) = 0;

		/// Register global function in the RTTI system (TODO: move to scripting system)
		virtual void RegisterGlobalFunction( rtti::Function* function ) = 0;

		/// Unregister global function in the RTTI system (TODO: move to scripting system)
		virtual void UnregisterGlobalFunction( const rtti::Function* function ) = 0;

		/// Remove all registered global functions which are defined in scripts
		virtual void ClearScriptedGlobalFunctions() = 0;

		/// Add new type pre-registration function for later calling
		virtual void AddPreRegistrationFunc( red::FixedSizeFunction<void()> preRegisterFunc ) = 0;

		/// Add new type registration function for later calling ( register == add type to known types container )
		virtual void AddRegistrationFunc( red::FixedSizeFunction<void()> registerFunc ) = 0;

		/// Register new types requested from last registration process
		virtual void RegisterPendingTypes() = 0;

		/// Register a Dynamic Type Creator. Dynamic type are usually a type that composition of multiple type. Like DynArray or THandle.
		using DynamicTypeCreationFunction = red::UniquePtr< rtti::IType > ( * )( red::StringView typeName, ITypeSystem* typeSystem ); 
		virtual void RegisterDynamicTypeCreator( red::StringView keyword, DynamicTypeCreationFunction creatorFunction ) = 0;

		/// Create RTTI type for scripted class
		virtual const rtti::ClassType* CreateScriptedClass( CName className, Uint32 flags, const ClassType* baseClass ) = 0;

		/// Create RTTI type for scripted enum
		virtual const rtti::EnumType* CreateScriptedEnum( CName enumName, Uint32 enumSize, const red::DynArray<std::pair<CName, Int64>>& options ) = 0;

		/// Create RTTI type for scripted bitfield
		virtual const rtti::BitFieldType* CreateScriptedBitfield( CName bitfieldName, const red::DynArray<std::pair<CName, Uint64>>& options ) = 0;

		/// Refresh after script reloading
		virtual void RebuildRuntimeData() = 0;

		/// Create script alias for native name
		virtual void RegisterScriptAlias( CName nativeName, CName scriptAlias ) = 0;

		/// Find scripted class using its alias
		virtual const ClassType* FindScriptClass( CName scriptAlias ) = 0;

		/// Find scripted enum using its alias
		virtual const rtti::EnumType* FindScriptEnum( CName scriptAlias ) = 0;

		/// Get script alias for native type
		virtual CName NativeNameToScriptAlias( CName nativeName ) const = 0;

		/// Get script alias for native type
		virtual CName ScriptAliasToNativeName( CName scriptAlias ) const = 0;

	protected:
		ITypeSystem();
		virtual ~ITypeSystem();
	};

} // rtti

//////////////////////////////////////////////////////////////////////////
// Direct access to rtti system
RED_FORCE_INLINE rtti::ITypeSystem& GetRttiSystem()
{
	return rtti::ITypeSystem::GetInstance();
}

//////////////////////////////////////////////////////////////////////////
// Resolving rtti type function
template< typename T >
const rtti::IType* ResolveRttiType()
{
	const TypeHash typeHashCode = GetNativeTypeHash< T >();
	const rtti::IType* foundType = GetRttiSystem().FindTypeByHash( typeHashCode );
	if( foundType != nullptr )
	{
		return foundType;
	}

	// stack overflow guard for RttiTypeExtractor
	if( std::is_same< T, typename rtti::extractor::RttiTypeExtractor< T >::InnerType >::value )
	{
		return nullptr;
	}

	// get inner type from template
	const rtti::IType* innerType = ResolveRttiType< typename rtti::extractor::RttiTypeExtractor< T >::InnerType >();
	if ( !innerType )
	{
		return nullptr;
	}

	// create new complex type
	red::UniquePtr< rtti::IType > newType = rtti::ComplexTypeCreator< T >::Create( innerType );
	if ( !newType )
	{
		return nullptr;
	}

	// check duplicated type which could be created from scripts
	const rtti::IType* existingType = GetRttiSystem().FindDuplicatedType( newType.Get() );
	if( existingType != nullptr )
	{
		return existingType;
	}

	GetRttiSystem().RegisterType( newType.Get(), typeHashCode );

	return newType.ReleaseOwnership();
}


/// Get the RTTI type object representing given C++ type _Type
/// NOTE: this will only fail at runtime
template< class _Type >
RED_FORCE_INLINE const rtti::IType* GetTypeObject()
{
	static const rtti::IType* rttiType = ResolveRttiType< typename std::remove_const< typename std::remove_reference< _Type >::type >::type >();
	return rttiType;
}

template< class _Type >
RED_FORCE_INLINE const rtti::IType* GetTypeObject( const _Type& )
{
	static const rtti::IType* rttiType = ResolveRttiType< _Type >();
	return rttiType;
}
