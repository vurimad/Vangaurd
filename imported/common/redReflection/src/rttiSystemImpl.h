/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiSystem.h"

namespace rtti
{
	typedef Uint64 TypeHash;

	/// RTTI SYSTEM implementation (hidden)
	class TypeSystemImpl : public ITypeSystem, red::NonCopyable
	{
	public:
		TypeSystemImpl();
		virtual ~TypeSystemImpl();

		static TypeSystemImpl& GetInstance();

		/// ITypeSystem interface
		virtual const rtti::IType* FindType( CName name ) override final;
		virtual const rtti::IType* FindTypeByHash( TypeHash typeHashCode ) override final;
		virtual const rtti::ClassType* FindClass( const CName name ) override final;
		virtual const rtti::EnumType* FindEnum( const CName name ) override final;
		virtual const rtti::BitFieldType* FindBitField( const CName name ) override final;
		virtual const rtti::Function* FindGlobalFunction( const CName name ) override final;
		virtual const Function* FindGlobalFunction( Uint64 hash ) override final;
		virtual const rtti::IType* FindDuplicatedType( const IType* type ) override final;

		virtual void EnumNativeTypes( red::DynArray< const rtti::IType* >& types ) override final;
		virtual void EnumGlobalFunctions( red::DynArray< const rtti::Function* >& functions ) override final;
		virtual void EnumGlobalFunctionsFromFamily( rtti::IFunctionCollector& collector ) override final;
		virtual void EnumFunctions( red::DynArray< const rtti::Function* >& functions ) override final;
		virtual void EnumEnums( red::DynArray< const rtti::EnumType* >& enums, Bool onlyScripted = false ) override final;
		virtual void EnumBitFields( red::DynArray< const rtti::BitFieldType* >& bitFields, Bool onlyScripted = false ) override final;
		virtual void EnumClasses( const rtti::ClassType* baseClass, red::DynArray< const rtti::ClassType* > &classes, Bool classFilter( const rtti::ClassType * ) = NULL, Bool allowAbstract = false ) override final;
		virtual void EnumDerivedClasses( const rtti::ClassType* baseClass, red::DynArray< const rtti::ClassType* > &classes ) override final;

		virtual void RegisterPendingTypes() override final;
		virtual void RegisterType( rtti::IType *type, TypeHash hash ) override final;
		virtual void UnregisterType( const rtti::IType *type ) override final;
		virtual void RegisterGlobalFunction( rtti::Function* function ) override final;
		virtual void UnregisterGlobalFunction( const rtti::Function* function ) override final;
		virtual void ClearScriptedGlobalFunctions() override final;
		virtual void RegisterTypeWrapper( TypeHash existingTypeHash, TypeHash wrappedTypeHash ) override final;
		virtual void AddPreRegistrationFunc( red::FixedSizeFunction<void()> preRegisterFunc ) override final;
		virtual void AddRegistrationFunc( red::FixedSizeFunction<void()> registerFunc ) override final;
		virtual void RegisterDynamicTypeCreator( red::StringView keyword, DynamicTypeCreationFunction creatorFunction ) override final;
		virtual void RegisterScriptAlias( CName nativeName, CName scriptAlias ) override final;
		virtual const ClassType* FindScriptClass( CName scriptAlias ) override final;
		virtual const EnumType* FindScriptEnum( CName scriptAlias ) override final;
		virtual CName NativeNameToScriptAlias( CName nativeName ) const override final;
		virtual CName ScriptAliasToNativeName( CName scriptAlias ) const override final;

		virtual const rtti::ClassType* CreateScriptedClass( CName className, Uint32 flags, const ClassType* baseClass ) override final;
		virtual const rtti::EnumType* CreateScriptedEnum( CName enumName, Uint32 enumSize, const red::DynArray<std::pair<CName, Int64>>& options ) override final;
		virtual const rtti::BitFieldType* CreateScriptedBitfield( CName bitfieldName, const red::DynArray<std::pair<CName, Uint64>>& options ) override final;

		virtual void RebuildRuntimeData() override final;

	private:

		using Types = red::HashMap< CName, rtti::IType* >;
		using TNativeTypes = red::HashMap< TypeHash, rtti::IType* >;
		using THashesMap = red::HashMap< CName, TypeHash >;
		using TFunctions = red::HashMap< CName, rtti::Function* >;
		using TRemapTable = red::HashMap< CName, CName >;
		using TRemapFunction = red::DynArray< red::FixedSizeFunction< CName( CName ) > >;
		using TRegisterFunc = red::DynArray< red::FixedSizeFunction< void() > >;
		using TDynamicTypeCreators = red::DynArray< std::pair< const red::StringView, DynamicTypeCreationFunction > >;

		void Init();
		void CheckPropertiesCorrectness();
		void LoadClassRemapTable();
		void RegisterDefaultDynamicTypeCreator();

		// Fallback method that handles naming conversion for old type names (@Int, #CEntity, etc)
		const rtti::IType* HandleOldWrappedTypes( const CName typeName );

		// Create a dynamic type that is not yet known (arrays, native arrays, handles, etc)
		const rtti::IType* CreateDynamicType( const CName typeName );

		const rtti::IType* TryFindType( CName name );
		const rtti::IType* TryRegisterType( red::UniquePtr< rtti::IType > type, TypeHash hash );

		
		mutable red::Atomic<Bool>	m_newClassAdded;

		Types						m_types;
		TNativeTypes				m_nativeTypes;			 // ctremblay: Is this still needed. Isn't <name,type> enough ?
		THashesMap					m_nameHashToTypeHashMap; // ctremblay: Is this still needed. Isn't <name,type> enough ?
		TFunctions					m_globalFunctions;
		red::HashMap< Uint64, rtti::Function* > m_hashGlobalFunctions;
		TRemapTable					m_classRemapTable;
		TRemapFunction				m_classRemapFunction;
		TDynamicTypeCreators		m_dynamicTypeCreator;
		TRemapTable					m_scriptAliasToNativeName;
		TRemapTable					m_nativeNameToScriptAlias;

		mutable TRegisterFunc m_registerFunctions;
		mutable TRegisterFunc m_preRegisterFunctions;

		red::Mutex m_dynamicTypeLock;
		red::RWSpinLock m_typesLock;
		red::Mutex m_registerFunctionsLock; // TODO should be reentrant RW lock
	};

} // rtti
