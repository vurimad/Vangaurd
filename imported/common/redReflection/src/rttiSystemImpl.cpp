/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "rttiSystemImpl.h"
#include "rttiEnum.h"
#include "rttiBitField.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypesImpl.h"
#include "rttiClassBuilder.h"
#include "rttiScriptedClass.h"
#include "rttiFunction.h"
#include "rttiUtils.h"
#include "resource.h"
#include "../../redSystem/include/stopWatch.h"
#include "../../redContainers/include/fundamentalStringParser.h"
#include "../../redContainers/include/dynArrayAccessor.h"
#include "../../redContainers/include/string/stringUtils.h"

using red::DynArray;
using red::DynArrayAccessor;

#define REMAP( oldName, newName ) RED_VERIFY( m_classRemapTable.Insert( RED_NAME(oldName), RED_NAME(newName) ).IsSuccessful() )
#define REMAP_CONSTEXPR( oldName, newName ) RED_VERIFY( m_classRemapTable.Insert( RED_NAME_CONSTEXPR(oldName), RED_NAME_CONSTEXPR(newName) ).IsSuccessful() )

namespace rtti
{
	extern void RegisterFundamentalTypes( ITypeSystem& rs );

	TypeSystemImpl::TypeSystemImpl()
		: m_registerFunctions( red::PoolEngine() )
		, m_preRegisterFunctions( red::PoolEngine() )
		, m_types( red::PoolRTTI() )
		, m_nativeTypes( red::PoolRTTI() )
		, m_nameHashToTypeHashMap( red::PoolRTTI() )
		, m_globalFunctions( red::PoolRTTI() )
		, m_hashGlobalFunctions( red::PoolRTTI() )
		, m_classRemapTable( red::PoolEngine() )
		, m_classRemapFunction( red::PoolEngine() )
		, m_dynamicTypeCreator( red::PoolEngine() )
		, m_scriptAliasToNativeName( red::PoolRTTI() )
		, m_nativeNameToScriptAlias( red::PoolRTTI() )
	{
	}

	TypeSystemImpl::~TypeSystemImpl()
	{
	}

	TypeSystemImpl& TypeSystemImpl::GetInstance()
	{
		static Bool isInitialized = false;
		static TypeSystemImpl theSystem;
		if( !isInitialized )
		{
			isInitialized = true;
			theSystem.Init();
		}
		return theSystem;
	}

	void TypeSystemImpl::Init()
	{
		// register global fundamental types
		RegisterFundamentalTypes( *this );

		// create a remap list
		LoadClassRemapTable();

		RegisterDefaultDynamicTypeCreator();
	}

	void TypeSystemImpl::RegisterPendingTypes()
	{
		if( !m_newClassAdded.GetValue() )
			return;

		// nothing new
		if( !m_newClassAdded.Exchange( false ) )
			return;

		TRegisterFunc preRegisterFunctions{ red::PoolRTTI() };
		TRegisterFunc registerFunctions{ red::PoolRTTI() };
		{
			RED_SCOPE_LOCK( m_registerFunctionsLock );
			preRegisterFunctions = std::move( m_preRegisterFunctions );
			registerFunctions = std::move( m_registerFunctions );
		}

		// run pre-register functions
		for( auto preRegisterFunc : preRegisterFunctions )
			preRegisterFunc();

		// run register functions
		for( const auto& registerFunc : registerFunctions )
			registerFunc();

		#ifndef RED_CONFIGURATION_FINAL
		CheckPropertiesCorrectness();	// TEMPSHIT - validate our new rtti assumptions
		#endif
	}

	void TypeSystemImpl::CheckPropertiesCorrectness()
	{
		RED_SCOPE_SHARED_LOCK( m_typesLock );

		// Check properties
		for( const auto& it : m_nativeTypes )
		{
			const auto currentType = it.Value()->GetType();
			if( currentType == RT_Class )
			{
				const auto* classType = static_cast<const ClassType*>( it.Value() );
				for( const auto& prop : classType->GetLocalProperties() )
				{
					if( prop->GetType()->GetType() == RT_Pointer || prop->GetType()->GetType() == RT_Handle )
					{
						const IBasePointerType* ptr = static_cast<const IBasePointerType*>( prop->GetType() );
						const auto* pointed = ptr->GetPointedType();

						RED_FATAL_ASSERT( pointed->IsSerializable(), "Pointers and handles in RTTI MUST be to at least ISerializable, property: '%hs' in class '%hs'",
							prop->GetName().AsChar(),
							classType->GetName().AsChar() );
					}
				}
			}
			else if ( currentType == RT_ResourceReference )
			{
				const auto* resRefType = static_cast<const ResourceReferenceType*>( it.Value() );
				RED_FATAL_ASSERT( resRefType->GetPointedType()->IsA< CResource >(), "Only CResource is allowed in ResourceReference type." );
			}
			else if ( currentType == RT_ResourceAsyncReference )
			{
				const auto* resRefType = static_cast<const ResourceAsyncReferenceType*>( it.Value() );
				RED_FATAL_ASSERT( resRefType->GetPointedType()->IsA< CResource >(), "Only CResource is allowed in ResourceAsyncReference type." );
			}
		}
	}

	void TypeSystemImpl::RegisterType( rtti::IType *type, TypeHash hash )
	{
		RED_FATAL_ASSERT( type != nullptr, "Expecting type to register" );

		const CName typeName = type->GetName();

		RED_SCOPE_LOCK( m_typesLock );

		RED_FATAL_ASSERT( !m_nativeTypes.KeyExist( hash ), "Type '%hs' is already registered", typeName.AsChar() );
		RED_FATAL_ASSERT( !m_nameHashToTypeHashMap.KeyExist( typeName ), "Type '%hs' is already registered", typeName.AsChar() );

		m_types[ typeName ] = type;
		m_nativeTypes[hash] = type;
		m_nameHashToTypeHashMap[typeName] = hash;

		rep::IRTTIService::GetInstance().OnCoreTypeRegistered( type );

		RED_LOG_SPAM( "Core: Registering type '%hs'", type->GetName().AsChar() );
	}

	const rtti::IType* TypeSystemImpl::TryRegisterType(red::UniquePtr< rtti::IType > type, TypeHash hash )
	{
		RED_FATAL_ASSERT( type != nullptr, "Expecting type to register" );

		const CName typeName = type->GetName();

		RED_SCOPE_LOCK( m_typesLock );

		auto iter = m_nativeTypes.Find( hash );
		if( iter == m_nativeTypes.End() )
		{
			m_types[ typeName ] = type.Get();
			m_nativeTypes[hash] = type.Get();
			m_nameHashToTypeHashMap[typeName] = hash;
			rep::IRTTIService::GetInstance().OnCoreTypeRegistered( type.Get() );

			RED_LOG_SPAM( "Core: Registering type '%hs'", typeName.AsChar() );
			return type.ReleaseOwnership();
		}

		return iter.Value();
	}

	void TypeSystemImpl::RegisterTypeWrapper( TypeHash existingTypeHash, TypeHash wrappedTypeHash )
	{
		RED_SCOPE_LOCK( m_typesLock );

		rtti::IType* existingType = m_nativeTypes[existingTypeHash];
		RED_FATAL_ASSERT( existingType != nullptr, "Wrapper not exist in rtti system" );
		m_nativeTypes[wrappedTypeHash] = existingType;
	}

	void TypeSystemImpl::UnregisterType( const rtti::IType *type )
	{
		RED_ASSERT( type );

		const CName typeName = type->GetName();

		RED_SCOPE_LOCK( m_typesLock );

		if( m_nameHashToTypeHashMap.KeyExist( typeName ) )
		{
			TypeHash typeHash = m_nameHashToTypeHashMap[typeName];
			auto it = m_nativeTypes.Find( typeHash );
			if( it != m_nativeTypes.End() )
			{
				rep::IRTTIService::GetInstance().OnCoreTypeUnregistered( type );
				m_nativeTypes.Remove( it );
				return;
			}
		}

		RED_LOG_WARNING( "Core: Type %hs not registered in RTTI system !", typeName.AsChar() );
	}

	void TypeSystemImpl::AddPreRegistrationFunc( red::FixedSizeFunction<void()> preRegisterFunc )
	{
		RED_SCOPE_LOCK( m_registerFunctionsLock );

		m_preRegisterFunctions.PushBack( preRegisterFunc );
		m_newClassAdded.Exchange( true );
	}

	void TypeSystemImpl::AddRegistrationFunc( red::FixedSizeFunction<void()> registerFunc )
	{
		RED_SCOPE_LOCK( m_registerFunctionsLock );

		m_registerFunctions.PushBack( registerFunc );
		m_newClassAdded.Exchange( true );
	}

	const rtti::ClassType* TypeSystemImpl::FindClass( const CName name )
	{
		const rtti::IType* type = FindType( name );
		if( !type || ( type->GetType() != RT_Class ) )
			return nullptr;

		return static_cast<const rtti::ClassType*>( type );
	}

	const EnumType* TypeSystemImpl::FindEnum( const CName name )
	{
		const rtti::IType* type = FindType( name );
		if( !type || ( type->GetType() != RT_Enum ) )
			return nullptr;

		return static_cast<const EnumType*>( type );
	}

	const BitFieldType* TypeSystemImpl::FindBitField( const CName name )
	{
		const rtti::IType* type = FindType( name );
		if( !type || ( type->GetType() != RT_BitField ) )
			return nullptr;

		return static_cast<const BitFieldType*>( type );
	}

	const rtti::IType* TypeSystemImpl::HandleOldWrappedTypes( const CName typeName )
	{
		// we still need a way to resolve CName -> Char* in order for RTTI to work
		auto typeNameStr = typeName.AsStringView();
		if( typeNameStr.Empty() )
		{
			return nullptr;
		}

		// old RTTI type naming was somehow cryptic
		// '@' - Dynamic array
		//  '*' - C pointer
		//  '#' - Handle
		//  '~' - Soft handle
		if( typeNameStr[0] == '@' )
		{
			// resolve the inner type
			typeNameStr.RemovePrefix( 1 );
			const CName innerTypeName = RED_NAME( typeNameStr );

			// get the proper type
			const CName fullTypeName = rtti::FormatDynArrayTypeName( innerTypeName );
			const rtti::IType* wrappedType = FindType( fullTypeName );
			if( nullptr != wrappedType )
			{
				// register with the old name
				REMAP( typeName, wrappedType->GetName() );
			}
			return wrappedType;
		}
		else if( typeNameStr[0] == '*' )
		{
			// resolve the inner type
			typeNameStr.RemovePrefix( 1 );
			const CName innerTypeName = RED_NAME( typeNameStr );

			// get the proper type
			const CName fullTypeName = rtti::FormatPointerTypeName( innerTypeName );
			const rtti::IType* wrappedType = FindType( fullTypeName );
			if( nullptr != wrappedType )
			{
				// register with the old name
				REMAP( typeName, wrappedType->GetName() );
			}
			return wrappedType;
		}
		else if( typeNameStr[0] == '#' )
		{
			// resolve the inner type
			typeNameStr.RemovePrefix( 1 );
			const CName innerTypeName = RED_NAME( typeNameStr );

			// get the proper type
			const CName fullTypeName = rtti::FormatHandleTypeName( innerTypeName );
			const rtti::IType* wrappedType = FindType( fullTypeName );
			if( nullptr != wrappedType )
			{
				// register with the old name
				REMAP( typeName, wrappedType->GetName() );
			}
			return wrappedType;
		}
		else if( typeNameStr[0] == '~' )
		{
			RED_FATAL( "SoftHandle type is not supported anymore!" );
		}

		// Not an old type
		return nullptr;
	}

	const rtti::IType* TypeSystemImpl::FindDuplicatedType( const rtti::IType* type )
	{
		RED_SCOPE_SHARED_LOCK( m_typesLock );

		if( type == nullptr )
		{
			return nullptr;
		}

		if( m_nativeTypes.KeyExist( type->GetName().GetHash() ) )
		{
			return m_nativeTypes[type->GetName().GetHash()];
		}
		else if( m_nameHashToTypeHashMap.KeyExist( type->GetName() ) )
		{
			TypeHash typeHash = m_nameHashToTypeHashMap[type->GetName()];
			if(m_nativeTypes.KeyExist( typeHash ) )
			{
				return m_nativeTypes[typeHash];
			}
		}

		return nullptr;
	}

	const rtti::IType* TypeSystemImpl::FindType( CName name )
	{
		// nullptr type?
		if ( !name )
		{
			return nullptr;
		}

		// Make sure all types are registered for sure
		RegisterPendingTypes();

		const rtti::IType* type = TryFindType( name );
		if ( type )
		{
			return type;
		}

		// Hack for remapping some old class names
		for ( const auto& expresion : m_classRemapFunction )
		{
			name = expresion( name );
		}

		type = TryFindType( name );
		if ( type )
		{
			return type;
		}

		// Consider old type names
		const rtti::IType* wrapperdType = HandleOldWrappedTypes( name );
		if ( wrapperdType != nullptr )
		{
			return wrapperdType;	// type was handled by the old naming
		}

		// Try the dynamic type (last resort)
		return CreateDynamicType( name );
	}

	const rtti::IType * TypeSystemImpl::TryFindType( CName name )
	{
		RED_SCOPE_SHARED_LOCK( m_typesLock );

		// Find type by name
		rtti::IType* type = nullptr;
		auto iter = m_types.Find( name );
		if( iter != m_types.End() )
		{
			return iter.Value();
		}

		THashesMap::const_iterator hashIter = m_nameHashToTypeHashMap.Find( name );
		if ( hashIter != m_nameHashToTypeHashMap.End() )
		{
			TypeHash typeHash = hashIter.Value();
			TNativeTypes::const_iterator typeIter = m_nativeTypes.Find( typeHash );
			if ( typeIter != m_nativeTypes.End() )
			{
				type = typeIter.Value();
			}
		}

		return type;
	}

	const rtti::IType* TypeSystemImpl::FindTypeByHash( TypeHash typeHashCode )
	{
		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		rtti::IType* rttiType;
		if( m_nativeTypes.Find( typeHashCode, rttiType ) )
			return rttiType;

		return nullptr;
	}

	const rtti::IType* TypeSystemImpl::CreateDynamicType( const CName name )
	{
		red::ScopedLock<red::Mutex> lock( m_dynamicTypeLock );

		const rtti::IType* type = TryFindType( name );
		if( type )
		{
			return type;
		}

		// Note about type name parsing:
		// The parsing is done manually in a very straighforward way (unsafe under some conditions).
		// I didn't want to use any heavy code here like tokenization, string splitting, etc. for performance reasons.
		// Note that the type names are generated automatically anyway so there's no direct risk of messing something up here.
		const char* typeNameStrOrg = name.AsChar();
		const char* typeNameStr = typeNameStrOrg; // this gets shifted by parsing

		for( auto iter = m_dynamicTypeCreator.Begin(), end = m_dynamicTypeCreator.End(); iter != end; ++iter )
		{
			const auto key = iter->first;
			// ctremblay this check might make debugging this harder than it is. Move responsibility to DynamicTypeCreationFunction worst case.
			if( GParseKeyword( typeNameStr, key.Data() ) )
			{
				DynamicTypeCreationFunction creator = iter->second;
				red::UniquePtr< rtti::IType > type = creator( typeNameStr, this );
				if( type )
				{
					// ctremblay HACK ? because of remapping table, this can return an already registered type ...
					const rtti::IType* afterRemapType = FindDuplicatedType( type.Get() );
					if( afterRemapType == nullptr )
					{
						return TryRegisterType( std::move( type ), GenerateUniqueTypeHash() );
					}

					return afterRemapType;
				}
			}
		}

		return nullptr;
	}

	void TypeSystemImpl::EnumNativeTypes( DynArray< const rtti::IType* >& types )
	{
		types.Reserve( m_nativeTypes.Size() );
		for( const auto& it : m_nativeTypes )
		{
			types.PushBack( it.Value() );
		}
	}

	void TypeSystemImpl::EnumGlobalFunctions( DynArray< const rtti::Function* >& functions )
	{
		// Global functions
		functions.Reserve( m_globalFunctions.Size() );
		for( const auto& it : m_globalFunctions )
			functions.PushBack( it.Value() );
	}

	void TypeSystemImpl::EnumGlobalFunctionsFromFamily( rtti::IFunctionCollector& collector )
	{
		for ( const auto& it : m_globalFunctions )
		{
			if ( !collector( it.Value() ) )
			{
				return;
			}
		}
	}

	void TypeSystemImpl::EnumFunctions( DynArray< const rtti::Function* >& functions )
	{
		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		for( const auto& it : m_globalFunctions )
			functions.PushBack( it.Value() );

		for( const auto& it : m_nativeTypes )
		{
			if( it.Value()->GetType() == RT_Class )
			{
				const auto* classType = static_cast<const rtti::ClassType*>( it.Value() );

				for( auto* func : classType->GetLocalFunctions() )
					functions.PushBack( func );
			}
		}
	}

	void TypeSystemImpl::EnumClasses( const rtti::ClassType* baseClass, DynArray< const rtti::ClassType* > &classes, Bool classFilter( const rtti::ClassType * ) /* = nullptr */, Bool allowAbstract /*= false*/ )
	{
		classes.Reserve( m_nativeTypes.Size() ); // Worst case scenario

		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		if( baseClass == nullptr )
		{
			for( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
			{
				const rtti::IType* type = i.Value();
				if( type->GetType() == RT_Class )
				{
					const rtti::ClassType* classType = static_cast<const rtti::ClassType*>( const_cast<const rtti::IType*>( type ) );
					if( classFilter == nullptr || classFilter( classType ) )
					{
						classes.PushBack( classType );
					}
				}
			}
		}
		else
		{
			for( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
			{
				const rtti::IType* type = i.Value();
				if( type->GetType() == RT_Class )
				{
					const rtti::ClassType* classType = static_cast<const rtti::ClassType*>( const_cast<const rtti::IType*>( type ) );
					if( classType->IsA( baseClass ) && ( allowAbstract || !classType->IsAbstract() ) &&
						( classFilter == nullptr || classFilter( classType ) ) )
					{
						classes.PushBack( classType );
					}
				}
			}
		}
	}

	void TypeSystemImpl::EnumDerivedClasses( const rtti::ClassType* baseClass, DynArray< const rtti::ClassType* > &classes )
	{
		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		for( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
		{
			const rtti::IType* type = i.Value();
			if( type->GetType() == RT_Class )
			{
				const rtti::ClassType* classType = static_cast<const rtti::ClassType*>( const_cast<const rtti::IType*>( type ) );
				if( classType->GetBaseClass() == baseClass )
				{
					classes.PushBack( classType );
				}
			}
		}
	}

	void TypeSystemImpl::EnumEnums( red::DynArray< const rtti::EnumType* >& enums, Bool onlyScripted )
	{
		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		for ( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
		{
			const rtti::IType* type = i.Value();
			if ( type->GetType() == RT_Enum )
			{
				const rtti::EnumType* enumType = static_cast< const rtti::EnumType* >( const_cast< const rtti::IType* >( type ) );
				if ( !onlyScripted || ( onlyScripted && enumType->IsScripted() ) )
				{
					enums.PushBack( enumType );
				}
			}
		}
	}

	void TypeSystemImpl::EnumBitFields( red::DynArray< const rtti::BitFieldType* >& bitFields, const Bool onlyScripted )
	{
		RegisterPendingTypes();

		RED_SCOPE_SHARED_LOCK( m_typesLock );

		for ( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
		{
			const rtti::IType* type = i.Value();
			if ( type->GetType() == RT_BitField )
			{
				const auto bitFieldType = static_cast< const rtti::BitFieldType* >( const_cast< const rtti::IType* >( type ) );
				if ( !onlyScripted || ( onlyScripted && bitFieldType->IsScripted() ) )
				{
					bitFields.PushBack( bitFieldType );
				}
			}
		}
	}

	const rtti::ClassType* TypeSystemImpl::CreateScriptedClass( CName className, Uint32 flags, const ClassType* baseClass )
	{
		// Get existing stub
		rtti::ClassType* classObject = const_cast<rtti::ClassType*>( FindClass( className ) );
		RED_FATAL_ASSERT( !classObject || classObject->IsScriptedType(), "Trying to convert native class '%hs' to scripted class", className.AsChar() );

		// Create new type
		if( !classObject )
		{
			//classObject = RED_NEW(\1);
			classObject = RED_NEW( ScriptedClassType )(className, flags);
			RegisterType( classObject, GenerateUniqueTypeHash() );
		}

		// Flush data
		classObject->ClearScriptData();
		classObject->ReuseScriptStub( flags );

		// setup base class
		if ( baseClass )
		{
			classObject->AddParentClass( baseClass );
		}

		classObject->MarkAsInitialized();

		return classObject;
	}

	const rtti::EnumType* TypeSystemImpl::CreateScriptedEnum( CName enumName, Uint32 enumSize, const red::DynArray<std::pair<CName, Int64>>& options )
	{
		// Get existing stub
		rtti::EnumType* enumObject = const_cast<rtti::EnumType*>( FindEnum( enumName ) );
		RED_FATAL_ASSERT( !enumObject || enumObject->IsScripted(), "Trying to convert native enum '%hs'to scripted enum", enumName.AsChar() );

		// Create new type
		if( !enumObject )
		{
			enumObject = RED_NEW( rtti::EnumType )( enumName, enumSize, true );

			// add options to enum type object
			RED_FATAL_ASSERT( !options.Empty(), "Invalid state" );
			for ( const auto& opt : options )
			{
				enumObject->Add( opt.first, opt.second );
			}

			RegisterType( enumObject, GenerateUniqueTypeHash() );
		}

		return enumObject;
	}

	const rtti::BitFieldType* TypeSystemImpl::CreateScriptedBitfield( CName bitfieldName, const red::DynArray<std::pair<CName, Uint64>>& options )
	{
		constexpr Uint32 bitfieldSize = 8;

		// Get existing stub
		rtti::BitFieldType* bitfieldObject = const_cast<rtti::BitFieldType*>( FindBitField( bitfieldName ) );
		RED_FATAL_ASSERT( !bitfieldObject || bitfieldObject->IsScripted(), "Trying to convert native enum '%hs' to scripted enum", bitfieldName.AsChar() );

		// Create new type
		if( !bitfieldObject )
		{
			bitfieldObject = RED_NEW( rtti::BitFieldType )( bitfieldName, bitfieldSize, true );

			// add options to enum type object
			RED_FATAL_ASSERT( !options.Empty(), "Invalid state" );
			for ( const auto& opt : options )
			{
				bitfieldObject->AddBit( opt.first, opt.second );
			}

			RegisterType( bitfieldObject, GenerateUniqueTypeHash() );
		}

		return bitfieldObject;
	}

	void TypeSystemImpl::RegisterGlobalFunction( rtti::Function* function )
	{
		RED_FATAL_ASSERT( function != nullptr, "Invalid function specified" );
		RED_FATAL_ASSERT( !FindGlobalFunction( function->GetName() ), "Global function '%hs' already defined", function->GetName().AsChar() );

		// Register
		m_globalFunctions.Insert( function->GetName(), function );
		m_hashGlobalFunctions.Insert( function->GetFunctionHash(), function );

		rep::IRTTIService::GetInstance().OnCoreGlobalFunctionRegistered( function );
	}

	void TypeSystemImpl::UnregisterGlobalFunction( const rtti::Function* function )
	{
		RED_FATAL_ASSERT( function != nullptr, "Invalid function specified" );
		m_globalFunctions.Remove( function->GetName() );
		m_hashGlobalFunctions.Remove( function->GetFunctionHash() );

		rep::IRTTIService::GetInstance().OnCoreGlobalFunctionUnregistered( function );
	}

	void TypeSystemImpl::ClearScriptedGlobalFunctions()
	{
		DynArray< const rtti::Function* > functionsToRemove{ red::PoolScript() };

		for( auto func : m_globalFunctions )
		{
			if( !func.Value()->IsNative() )
			{
				functionsToRemove.PushBack( func.Value() );
			}
		}

		for( const rtti::Function* func : functionsToRemove )
		{
			m_globalFunctions.Remove( func->GetName() );
			m_hashGlobalFunctions.Remove( func->GetFunctionHash() );
			rep::IRTTIService::GetInstance().OnCoreGlobalFunctionUnregistered( func );
		}
	}

	const rtti::Function* TypeSystemImpl::FindGlobalFunction( CName name )
	{
		rtti::Function* function = nullptr;
		if( m_globalFunctions.Find( name, function ) )
			return function;

		return nullptr;
	}

	const Function* TypeSystemImpl::FindGlobalFunction( Uint64 hash )
	{
		rtti::Function* function = nullptr;
		if(m_hashGlobalFunctions.Find( hash, function ))
			return function;

		return nullptr;
	}

	void TypeSystemImpl::LoadClassRemapTable()
	{
		// Clear mapping
		m_classRemapTable.Clear();

		// Manual remaps
		REMAP_CONSTEXPR( "DeferredDataBuffer", "serializationDeferredDataBuffer" );
		REMAP_CONSTEXPR( "baseGrenade", "BaseGrenade" ); // script refactor

		// #todo: remove after texture resave!
		REMAP_CONSTEXPR( "rendRenderTextureBlob", "rendIRenderTextureBlob" );
		REMAP_CONSTEXPR( "animAnimEvent_EffectLoop", "AnimEvent_EffectDuration" );

		REMAP_CONSTEXPR( "StringAnsi", "String" );
		REMAP_CONSTEXPR( "Uint", "Uint32" );
		REMAP_CONSTEXPR( "Int", "Int32" );
		REMAP_CONSTEXPR( "CGameTime", "GameTime" );
		REMAP_CONSTEXPR( "CGameTimeInterval", "GameTimeInterval" );
		REMAP_CONSTEXPR( "float", "Float" );
		REMAP_CONSTEXPR( "@*IMeshLODLevel", "@*CMeshLOD" );
		REMAP_CONSTEXPR( "*IMeshLODLevel", "*CMeshLOD" );
		REMAP_CONSTEXPR( "CMeshLODLevelCustomMesh", "CMeshLOD" );
		REMAP_CONSTEXPR( "CMeshLODLevelExplicitMesh", "CMeshLOD" );
		REMAP_CONSTEXPR( "CMeshLODLevelBaseMesh", "CMeshLOD" );
		REMAP_CONSTEXPR( "CMeshLODLevelHide", "CMeshLOD" );
		REMAP_CONSTEXPR( "CSoundComponent", "CWayPointComponent" );
		REMAP_CONSTEXPR( "CEffectComponent", "CWayPointComponent" );
		REMAP_CONSTEXPR( "CEffectMeshComponent", "CMeshComponent" );
		REMAP_CONSTEXPR( "CEnvironmentComponent", "CSpriteComponent" );
		REMAP_CONSTEXPR( "CExternalPhysicsSystemComponent", "CPhysicsSystemComponent" );
		REMAP_CONSTEXPR( "CEffectDummyPoint", "CEffectDummyComponent" );
		REMAP_CONSTEXPR( "CEnvAmbientOnlyParameters", "CEnvReflectionProbesGenParameters" );
		REMAP_CONSTEXPR( "CEnvSSAOParameters", "CEnvNVSSAOParameters" );
		REMAP_CONSTEXPR( "animAnim", "animAnimation" );
		REMAP_CONSTEXPR( "LongBitField", "BitSet64Dynamic" );

		REMAP_CONSTEXPR( "effectEffect", "worldEffect" );
		REMAP_CONSTEXPR( "sceneWorld", "worldWorld" );
		REMAP_CONSTEXPR( "scenePrefab", "worldPrefab" );
		REMAP_CONSTEXPR( "sceneNode", "worldNode" );
		REMAP_CONSTEXPR( "sceneStaticMeshNode", "worldStaticMeshNode" );
		REMAP_CONSTEXPR( "sceneStaticLightNode", "worldStaticLightNode" );
		REMAP_CONSTEXPR( "scenePrefabNode", "worldPrefabNode" );
		REMAP_CONSTEXPR( "sceneNodeTransform", "worldNodeTransform" );
		REMAP_CONSTEXPR( "sceneINodeGeometryPayload", "worldINodeGeometryPayload" );
		REMAP_CONSTEXPR( "sceneMeshPayload", "worldMeshPayload" );
		REMAP_CONSTEXPR( "scenePrefabInstancePayload", "worldPrefabInstancePayload" );
		REMAP_CONSTEXPR( "sceneLightPayload", "worldLightPayload" );
		REMAP_CONSTEXPR( "EBindingDirection", "entEBindingDirection" );
		REMAP_CONSTEXPR( "CEnvironmentDefinition", "worldEnvironmentDefinition" );
		REMAP_CONSTEXPR( "Vector", "Vector4" );
		REMAP_CONSTEXPR( "physicsQTransform", "Transform" );
		REMAP_CONSTEXPR( "SocketPlacement", "toolsSocketPlacement" );
		REMAP_CONSTEXPR( "SocketDirection", "toolsSocketDirection" );
		REMAP_CONSTEXPR( "SocketDrawStyle", "toolsSocketDrawStyle" );
		REMAP_CONSTEXPR( "physicsSystemObjectSimulationType", "physicsSimulationType" );
		REMAP_CONSTEXPR( "physicsMaterialResourceDefinition", "physicsMaterialResource" );
		REMAP_CONSTEXPR( "physicsMaterialReference", "CName" );
		REMAP_CONSTEXPR( "worlduiWidgetComponent", "WorldWidgetComponent" );
		// localization into loc namespace change
		REMAP_CONSTEXPR( "localizationLanguage", "locLanguage" );
		REMAP_CONSTEXPR( "localizationLanguageGameConfiguration", "locLanguageGameConfiguration" );
		REMAP_CONSTEXPR( "localizationVoiceoverContext", "locVoiceoverContext");
		REMAP_CONSTEXPR( "localizationVoiceoverExpression", "locVoiceoverExpression");
		REMAP_CONSTEXPR( "localizationVoiceTagList", "locVoiceTagListResource");
		REMAP_CONSTEXPR( "questIVarDBConditionType", "questIFactsDBConditionType");
		REMAP_CONSTEXPR( "questVarDBCondition", "questFactsDBCondition");
		REMAP_CONSTEXPR( "questVarDBChangeFunctor", "questFactsDBChangeFunctor");
		REMAP_CONSTEXPR( "questVarDBChangeListener", "questFactsDBChangeListener");
		REMAP_CONSTEXPR( "questBaseVarDBChangeListenerWrapper", "questBaseFactsDBChangeListenerWrapper");
		REMAP_CONSTEXPR( "questIVarDBManagerNodeType", "questIFactsDBManagerNodeType");
		REMAP_CONSTEXPR( "questVarDBManagerNodeDefinition", "questFactsDBManagerNodeDefinition");
		// ink framework
		REMAP_CONSTEXPR( "inkWidgetResource", "inkWidgetLibraryResource" );
		REMAP_CONSTEXPR( "inkButtonWidget", "inkOldButtonWidget" );
		REMAP_CONSTEXPR( "inkButtonCallback", "inkOldButtonCallback" );
		REMAP_CONSTEXPR( "inkCheckBoxWidget", "inkOldCheckBoxWidget" );
		REMAP_CONSTEXPR( "inkCheckBoxCallback", "inkOldCheckBoxCallback" );
		REMAP_CONSTEXPR( "inkListWidget", "inkOldListWidget" );
		REMAP_CONSTEXPR( "inkListSelectionChangedCallback", "inkOldListSelectionChangedCallback" );
		REMAP_CONSTEXPR( "inkSimpleShapeWidget", "inkBaseShapeWidget" );
		REMAP_CONSTEXPR( "inkSimpleRectWidget", "inkRectangleWidget" );
		REMAP_CONSTEXPR( "inkSimpleCircleWidget", "inkCircleWidget" );
		REMAP_CONSTEXPR( "inkBorder", "inkBorderWidget" );
		REMAP_CONSTEXPR( "inkOverlayWidget", "inkFlexWidget" );
		REMAP_CONSTEXPR( "inkEHorizontalAlignment", "inkEHorizontalAlign" );
		REMAP_CONSTEXPR( "inkEVerticalAlignment", "inkEVerticalAlign" );
		REMAP_CONSTEXPR( "inkLetterCase", "textLetterCase" );
		REMAP_CONSTEXPR( "inkBorderThickness", "inkMargin" );
		REMAP_CONSTEXPR( "worlduiHudWidgetSpawnEntry", "inkHudWidgetSpawnEntry" );
		REMAP_CONSTEXPR( "worlduiHudEntriesResource", "inkHudEntriesResource" );
		// game ui
		REMAP_CONSTEXPR( "FastTravelPointData", "gameFastTravelPointData" );

		REMAP_CONSTEXPR( "ActionPuppetsReference", "ActionEntityReference" );
		REMAP_CONSTEXPR( "questPuppetsReference", "gameEntityReference" );

		// Physics
		REMAP_CONSTEXPR( "entPhysicalDestructionShatterComponent", "entPhysicalDestructionComponent" );
		REMAP_CONSTEXPR( "entPhysicalDestructionListenerComponent", "gamePhysicalDestructionListenerComponent" );
		REMAP_CONSTEXPR( "entTriggerAreaDestructionComponent", "entPhysicalImpulseAreaComponent" );

		// scenes
		{
			// Merge functionality into one marker `scn::SceneMarkerData`.
			REMAP_CONSTEXPR( "worldSceneWorldMarker", "scnSceneMarker" );
			REMAP_CONSTEXPR( "toolsSceneLocalMarker", "scnSceneMarker" );
			REMAP_CONSTEXPR( "scnbSceneAnimationMarker", "scnSceneMarker" );
			REMAP_CONSTEXPR( "toolsSceneLocalMarkerInternalsEntry", "scnSceneMarkerInternalsEntry" );

			// move nodes from `namespace tools` to `namespace scnb`
			REMAP_CONSTEXPR( "toolsStartNodeDescriptor", "scnbStartNodeDescriptor" );
			REMAP_CONSTEXPR( "toolsEndNodeDescriptor", "scnbEndNodeDescriptor" );
			REMAP_CONSTEXPR( "toolsEndNodeNsType", "scnbEndNodeNsType" );
			// ChoiceSection
			REMAP_CONSTEXPR( "toolsChoiceNodeNsOperationMode", "scnbChoiceNodeNsOperationMode" );
			REMAP_CONSTEXPR( "toolsChoiceNodeNsSizePreset", "scnbChoiceNodeNsSizePreset" );
			REMAP_CONSTEXPR( "toolsChoiceNodeNsVisualizerStyle", "scnbChoiceNodeNsVisualizerStyle" );
			REMAP_CONSTEXPR( "toolsChoiceNodeNsChoiceType", "scnbChoiceNodeNsChoiceType" );
			REMAP_CONSTEXPR( "toolsChoiceSectionNodeDescriptorPersistentDialogLineConfig", "scnbChoiceSectionNodeDescriptorPersistentDialogLineConfig" );
			REMAP_CONSTEXPR( "toolsChoiceSectionNodeDescriptor", "scnbChoiceSectionNodeDescriptor" );

			// Actor
			REMAP_CONSTEXPR( "toolsSceneActor", "scnbSceneActor" );

			// Actor Acquisition Plan
			REMAP_CONSTEXPR( "scnActorAcquisitionPlan", "scnEntityAcquisitionPlan" );
			REMAP_CONSTEXPR( "scnFindActorInContextParams", "scnFindEntityInContextParams" );
			REMAP_CONSTEXPR( "scnFindActorInWorldParams", "scnFindEntityInWorldParams" );
			REMAP_CONSTEXPR( "scnSpawnDespawnActorParams", "scnSpawnDespawnEntityParams" );
			REMAP_CONSTEXPR( "scnbFindActorInWorld", "scnbFindActorInWorld_DEPRECATED" );

			// Props Acquisition Plan
			REMAP_CONSTEXPR( "scnbFindPropInActor", "scnbFindPropInPerformer" );


			// Events
			REMAP_CONSTEXPR( "scneventsAttachPropToActor", "scneventsAttachPropToPerformer" );

			// Interesting Conversation
			REMAP_CONSTEXPR( "scnInterestingConversation", "scnInterestingConversation_DEPRECATED" );
		}

		// AI
		REMAP_CONSTEXPR( "SignalHandlerComponent", "AISignalHandlerComponent" );

		REMAP_CONSTEXPR( "worldRuntimeSystemEntities", "worldRuntimeSystemEntity" );
		REMAP_CONSTEXPR( "entTemplateResource", "entEntityTemplate" );
		REMAP_CONSTEXPR( "NPCPuppetPS", "ScriptedPuppetPS" );

		// vehicles
		REMAP_CONSTEXPR( "gamevehicleVehicleMountableComponent", "vehicleVehicleMountableComponent" );
		REMAP_CONSTEXPR( "gamevehicleFormationType", "vehicleFormationType" );
		REMAP_CONSTEXPR( "gamevehicleFormation", "vehicleFormation" );
		REMAP_CONSTEXPR( "gamevehicleAudioEventAction", "vehicleAudioEventAction" );
		REMAP_CONSTEXPR( "gamevehicleGarageComponent", "vehicleGarageComponent" );
		REMAP_CONSTEXPR( "gamevehicleVehicleType", "vehicleVehicleType" );
		REMAP_CONSTEXPR( "gamevehicleGarageComponentVehicleData", "vehicleGarageComponentVehicleData" );
		REMAP_CONSTEXPR( "gamevehicleGarageComponentPS", "vehicleGarageComponentPS" );
		REMAP_CONSTEXPR( "gamevehiclePlayerVehicle", "vehiclePlayerVehicle" );
		REMAP_CONSTEXPR( "gamevehicleSummonLogic", "vehicleSummonLogic" );
		REMAP_CONSTEXPR( "gamevehicleFollowObject", "vehicleFollowObject" );
		REMAP_CONSTEXPR( "gamevehicleSplineSlot", "vehicleSplineSlot" );
		REMAP_CONSTEXPR( "gamevehicleTempComponent", "vehicleTempComponent" );
		REMAP_CONSTEXPR( "gamevehiclePlayerToAIInterpolationType", "vehiclePlayerToAIInterpolationType" );
		REMAP_CONSTEXPR( "gamevehiclePlayerToAIBlendInterpolator", "vehiclePlayerToAIBlendInterpolator" );
		REMAP_CONSTEXPR( "gamevehicleAudio", "vehicleAudio" );
		REMAP_CONSTEXPR( "gamevehicleAudioEvent", "vehicleAudioEvent" );
		REMAP_CONSTEXPR( "gamevehicleAutopilot", "vehicleAutopilot" );
		REMAP_CONSTEXPR( "gamevehicleAVBaseObject", "vehicleAVBaseObject" );
		REMAP_CONSTEXPR( "gamevehicleBaseObjectAutonomousData", "vehicleBaseObjectAutonomousData" );
		REMAP_CONSTEXPR( "gamevehicleBaseObject", "vehicleBaseObject" );
		REMAP_CONSTEXPR( "gamevehicleBikeBaseObject", "vehicleBikeBaseObject" );
		REMAP_CONSTEXPR( "gamevehicleCamera", "vehicleCamera" );
		REMAP_CONSTEXPR( "gamevehicleCarBaseObject", "vehicleCarBaseObject" );
		REMAP_CONSTEXPR( "gamevehicleController", "vehicleController" );
		REMAP_CONSTEXPR( "gamevehicleEState", "vehicleEState" );
		REMAP_CONSTEXPR( "gamevehicleELightMode", "vehicleELightMode" );
		REMAP_CONSTEXPR( "gamevehicleChangeStateEvent", "vehicleChangeStateEvent" );
		REMAP_CONSTEXPR( "gamevehicleChangeLightModeEvent", "vehicleChangeLightModeEvent" );
		REMAP_CONSTEXPR( "gamevehicleChangeAlarmEvent", "vehicleChangeAlarmEvent" );
		REMAP_CONSTEXPR( "gamevehicleControllerPS", "vehicleControllerPS" );
		REMAP_CONSTEXPR( "gamevehicleVehicleDriver", "vehicleDriver" );
		REMAP_CONSTEXPR( "gamevehicleCameraEvent", "vehicleCameraEvent" );
		REMAP_CONSTEXPR( "gamevehicleToggleBuoyancyEvent", "vehicleToggleBuoyancyEvent" );
		REMAP_CONSTEXPR( "gamevehicleStopDriveToPointEvent", "vehicleStopDriveToPointEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveToPointEvent", "vehicleDriveToPointEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveToGameObjectEvent", "vehicleDriveToGameObjectEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveToNodeRefEvent", "vehicleDriveToNodeRefEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveFollowSplineEvent", "vehicleDriveFollowSplineEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveFollowEvent", "vehicleDriveFollowEvent" );
		REMAP_CONSTEXPR( "gamevehicleDriveSplineReverseEvent", "vehicleDriveSplineReverseEvent" );
		REMAP_CONSTEXPR( "gamevehicleStartDynamicMovementEvent", "vehicleStartDynamicMovementEvent" );
		REMAP_CONSTEXPR( "gamevehicleToggleDoorOpenEvent", "vehicleToggleDoorOpenEvent" );
		REMAP_CONSTEXPR( "gamevehicleToggleBrokenTireEvent", "vehicleToggleBrokenTireEvent" );
		REMAP_CONSTEXPR( "gamevehicleToggleQuestForceBrakingEvent", "vehicleToggleQuestForceBrakingEvent" );
		REMAP_CONSTEXPR( "gamevehicleTeleportEvent", "vehicleTeleportEvent" );
		REMAP_CONSTEXPR( "gamevehicleAIMountedE3Hack", "vehicleAIMountedE3Hack" );
		REMAP_CONSTEXPR( "gamevehicleStartConvoyEvent", "vehicleStartConvoyEvent" );
		REMAP_CONSTEXPR( "gamevehicleAssignConvoyEvent", "vehicleAssignConvoyEvent" );
		REMAP_CONSTEXPR( "gamevehicleDetachPartEvent", "vehicleDetachPartEvent" );
		REMAP_CONSTEXPR( "gamevehicleDetachAllPartsEvent", "vehicleDetachAllPartsEvent" );
		REMAP_CONSTEXPR( "gamevehicleOnPartDetachedEvent", "vehicleOnPartDetachedEvent" );
		REMAP_CONSTEXPR( "gamevehicleGridDestructionEvent", "vehicleGridDestructionEvent" );
		REMAP_CONSTEXPR( "gamevehicleReadyToParkEvent", "vehicleReadyToParkEvent" );
		REMAP_CONSTEXPR( "gamevehicleParkedEvent", "vehicleParkedEvent" );
		REMAP_CONSTEXPR( "gamevehicleToggleRadioReceiverEvent", "vehicleToggleRadioReceiverEvent" );
		REMAP_CONSTEXPR( "gamevehicleChangeRadioReceiverStationEvent", "vehicleChangeRadioReceiverStationEvent" );
		REMAP_CONSTEXPR( "gamevehicleFX", "vehicleFX" );
		REMAP_CONSTEXPR( "gamevehicleLightComponent", "vehicleLightComponent" );
		REMAP_CONSTEXPR( "gamevehicleELightType", "vehicleELightType" );
		REMAP_CONSTEXPR( "gamevehicleEVehicleSpeedConditionType", "vehicleEVehicleSpeedConditionType" );
		REMAP_CONSTEXPR( "gamevehicleVisualPerception", "vehicleVisualPerception" );
		REMAP_CONSTEXPR( "gamevehicleAutopilotTransformProvider", "vehicleAutopilotTransformProvider" );

		// inventoryStructs.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "StatViewData", "gameStatViewData" );
		REMAP_CONSTEXPR( "ItemViewData", "gameItemViewData" );
		REMAP_CONSTEXPR( "SEquipSlot", "gameSEquipSlot" );
		REMAP_CONSTEXPR( "SEquipArea", "gameSEquipArea" );
		REMAP_CONSTEXPR( "SItemInfo", "gameSItemInfo" );
		REMAP_CONSTEXPR( "SEquipmentSet", "gameSEquipmentSet" );
		REMAP_CONSTEXPR( "SSlotInfo", "gameSSlotInfo" );
		REMAP_CONSTEXPR( "SVisualTagProcessing", "gameSVisualTagProcessing" );
		REMAP_CONSTEXPR( "SLastUsedWeapon", "gameSLastUsedWeapon" );
		REMAP_CONSTEXPR( "SSlotActiveItems", "gameSSlotActiveItems" );
		REMAP_CONSTEXPR( "SLoadout", "gameSLoadout" );
		REMAP_CONSTEXPR( "SPartSlots", "gameSPartSlots" );
		REMAP_CONSTEXPR( "SItemStackRequirementData", "gameSItemStackRequirementData" );
		REMAP_CONSTEXPR( "SItemStack", "gameSItemStack" );
		REMAP_CONSTEXPR( "InventoryItemAbility", "gameInventoryItemAbility" );

		// rpgManager.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "RPGManager", "gameRPGManager" );

		// uiItemsHelper.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "UIItemsHelper", "gameUIItemsHelper" );

		// uiLocalizationDataPackage.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "UILocalizationDataPackage", "gameUILocalizationDataPackage" );

		// deathMenuGameController.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "DeathMenuGameController", "gameuiDeathMenuGameController" );

		// inventoryEnums.cpp - moved from scripts to cpp
		REMAP_CONSTEXPR( "InventoryItemShape", "gameInventoryItemShape" );
		REMAP_CONSTEXPR( "InventoryItemAttachmentType", "gameInventoryItemAttachmentType" );
		REMAP_CONSTEXPR( "ItemIconGender", "gameItemIconGender" );
		REMAP_CONSTEXPR( "LootItemType", "gameLootItemType" );
		REMAP_CONSTEXPR( "EquipmentSetType", "gameEquipmentSetType" );
		REMAP_CONSTEXPR( "EHotkey", "gameEHotkey" );
		REMAP_CONSTEXPR( "ESlotState", "gameESlotState" );
		REMAP_CONSTEXPR( "EStatProviderDataSource", "gameEStatProviderDataSource" );
		REMAP_CONSTEXPR( "ItemComparisonState", "gameItemComparisonState" );
		REMAP_CONSTEXPR( "ItemDisplayContext", "gameItemDisplayContext" );

		TypeSystemImpl* ptr = this;
		m_classRemapFunction.PushBack( ( [ ptr ]( CName name )
		{
			ptr->m_classRemapTable.Find( name, name );
			return name;
		} ) );
		m_classRemapFunction.PushBack( ( []( CName name )
		{
			red::StringView stringName = name.AsStringView();
			Uint32 stringSize = stringName.Length();
			if ( stringSize > 4 && stringName.StartsWith( "phys" ) && stringName[4] != 'i' )
			{
				String newName = red::StrCat( "physics", stringName.SubView( 4 ) );
				name = RED_NAME( newName );
			}
			return name;
		} ) );
	}

	void TypeSystemImpl::RebuildRuntimeData()
	{
		red::StopWatch timer;

		// Cache properties in classes
		for( auto i = m_nativeTypes.Begin(); i != m_nativeTypes.End(); ++i )
		{
			rtti::IType* type = i.Value();
			if( type->GetType() == RT_Class )
			{
				rtti::ClassType* classType = static_cast<rtti::ClassType*>( type );
				classType->RecalculateAllCachedData();
			}
		}

		// Create struct data size
		Uint32 iteration = 0;
		Bool sizeChanging = true;
		while( sizeChanging )
		{
			// Check iteration count
			RED_LOG( "Core: Layout iteration #%i...", iteration );
			if( ++iteration > 100 )
			{
				RED_LOG_ERROR( "Core: INTERNAL ERROR: BuildDataLayout endless loop" );
				break;
			}

			// Let's hope this is the last iteration
			sizeChanging = false;

			// Update sizes
			for( const auto& it : m_nativeTypes )
			{
				if( it.Value()->GetType() == RT_Class )
				{
					rtti::ClassType* obj = static_cast<rtti::ClassType*>( it.Value() );
					if( obj )
					{
						const Uint32 prevSize = obj->GetSize();
						const Uint32 prevScriptSize = obj->GetScriptDataSize();

						obj->RecalculateClassDataSize();

						if( prevSize != obj->GetSize() || prevScriptSize != obj->GetScriptDataSize() )
							sizeChanging = true;
					}
				}
			}
		}

		// Build layout of class functions
		for( const auto& it : m_nativeTypes )
		{
			if( it.Value()->GetType() == RT_Class )
			{
				rtti::ClassType* obj = static_cast<rtti::ClassType*>( it.Value() );
				for( auto* func : obj->GetLocalFunctions() )
				{
					Function* editableFunc = const_cast< Function*>( func );
					editableFunc->CalcDataLayout();
				}

				// ctremblay: Register all scripted event connector.
				obj->Internal_RegisterScriptedEventConnectors();
			}
		}

		// Build layout of global functions
		for( const auto& it : m_globalFunctions )
		{
			auto* func = it.Value();
			if( func )
				func->CalcDataLayout();
		}

		// notify replication RTTI that RTTI rebuild is finished
		rep::IRTTIService::GetInstance().RebuildRuntimeData();

		RED_LOG( "Core: RTTI updated in %1.3fms", timer.GetDeltaMS() );
	}

	void TypeSystemImpl::RegisterDynamicTypeCreator( const red::StringView keyword, DynamicTypeCreationFunction creatorFunction )
	{
		auto iter = std::find_if(
			m_dynamicTypeCreator.Begin(),
			m_dynamicTypeCreator.End(),
			[=]( const TDynamicTypeCreators::value_type& type ) { return keyword == type.first; } );

		if( iter == m_dynamicTypeCreator.End() )
		{
			m_dynamicTypeCreator.PushBack( std::make_pair( keyword, creatorFunction ) );
		}
		else
		{
			RED_LOG_WARNING( "A Dynamic Type Creator for keyword '%s' already exist!", keyword );
		}
	}

	void TypeSystemImpl::RegisterScriptAlias( CName nativeName, CName scriptAlias )
	{
		RED_FATAL_ASSERT( !m_scriptAliasToNativeName.KeyExist( scriptAlias ), "Type '%hs' is already registered with script alias: '%hs'", m_scriptAliasToNativeName[ scriptAlias ].AsChar(), scriptAlias.AsChar() );
		RED_FATAL_ASSERT( !m_nativeNameToScriptAlias.KeyExist( nativeName ), "Script alias '%hs' is already registered for type '%hs'", m_nativeNameToScriptAlias[ nativeName ].AsChar(), nativeName.AsChar() );

		m_scriptAliasToNativeName[ scriptAlias ] = nativeName;
		m_nativeNameToScriptAlias[ nativeName ] = scriptAlias;
	}

	const rtti::ClassType* TypeSystemImpl::FindScriptClass( CName scriptAlias )
	{
		m_scriptAliasToNativeName.Find( scriptAlias, scriptAlias );
		return FindClass( scriptAlias );
	}

	const EnumType* TypeSystemImpl::FindScriptEnum( CName scriptAlias )
	{
		m_scriptAliasToNativeName.Find( scriptAlias, scriptAlias );
		return FindEnum( scriptAlias );
	}

	CName TypeSystemImpl::NativeNameToScriptAlias( CName nativeName ) const
	{
		TRemapTable::const_iterator it = m_nativeNameToScriptAlias.Find( nativeName );
		return it != m_nativeNameToScriptAlias.End() ? it.Value() : nativeName;
	}

	CName TypeSystemImpl::ScriptAliasToNativeName( CName scriptAlias ) const
	{
		TRemapTable::const_iterator it = m_scriptAliasToNativeName.Find( scriptAlias );
		return it != m_scriptAliasToNativeName.End() ? it.Value() : scriptAlias;
	}

	red::UniquePtr< rtti::IType > CreateDynArrayType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		//Parse memory class and pool types - optional
		// TODO: deprecated to some extent, please remove
		Uint32 memoryClassValue = 0;
		Uint32 memoryPoolValue = 0;
		auto typeNameString = typeName.Data();
		if( GParseInteger( typeNameString, memoryClassValue ) )
		{
			GParseKeyword( typeNameString, "," );
			if( GParseInteger( typeNameString, memoryPoolValue ) )
			{
				GParseKeyword( typeNameString, "," );
			}
		}

		// Here comes the actual inner type name, find it
		const rtti::IType* innerType = typeSystem->FindType( RED_NAME_NOREG( typeNameString ) );
		if( innerType )
		{
			return red::CreateUniquePtr< rtti::ArrayType >( innerType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateNativeArrayType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		// Size of the array
		Uint32 numElements = 0;
		auto typeNameString = typeName.Data();
		if( !GParseInteger( typeNameString, numElements ) )
		{
			return nullptr;
		}

		// end of element count
		if( !GParseKeyword( typeNameString, "]" ) )
		{
			return nullptr;
		}

		// Here comes the actual inner type name, find it
		const rtti::IType* innerType = typeSystem->FindType( RED_NAME_NOREG( typeNameString ) );
		if( innerType )
		{
			// Create final static array type wrapper and register it in the type array
			return red::CreateUniquePtr< rtti::NativeArrayType >( innerType, numElements );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateStaticArrayType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		// Size of the array
		Uint32 numElements = 0;
		auto typeNameString = typeName.Data();
		if( !GParseInteger( typeNameString, numElements ) )
		{
			return nullptr;
		}

		// end of element count
		if( !GParseKeyword( typeNameString, "," ) )
		{
			return nullptr;
		}

		// Here comes the actual inner type name, find it
		const rtti::IType* innerType = typeSystem->FindType( RED_NAME_NOREG( typeNameString ) );
		if( innerType )
		{
			return red::CreateUniquePtr< rtti::StaticArrayType >( innerType, numElements );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreatePointerType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		// Now comes the actual pointed type name, find it
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::PointerType >( pointedType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateHandleType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		// Now comes the actual pointed type name, find it
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::HandleType >( pointedType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateResourceReferenceType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::ResourceReferenceType >( pointedType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateAsyncResourceReferenceType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::ResourceAsyncReferenceType >( pointedType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateWeakHandleType( const red::StringView typeName, ITypeSystem * typeSystem )
	{
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::WeakHandleType >( pointedType );
		}

		return nullptr;
	}

	red::UniquePtr< rtti::IType > CreateScriptedReferenceType( const red::StringView typeName, ITypeSystem* typeSystem )
	{
		const rtti::IType* pointedType = typeSystem->FindType( RED_NAME_NOREG( typeName ) );
		if( pointedType )
		{
			return red::CreateUniquePtr< rtti::ScriptedReferenceType >( pointedType );
		}

		return nullptr;
	}

	void TypeSystemImpl::RegisterDefaultDynamicTypeCreator()
	{
		RegisterDynamicTypeCreator( red::StringView{ "array:" }, &CreateDynArrayType );
		RegisterDynamicTypeCreator( red::StringView{ "[" }, &CreateNativeArrayType );
		RegisterDynamicTypeCreator( red::StringView{ "static:" }, &CreateStaticArrayType );
		RegisterDynamicTypeCreator( red::StringView{ "ptr:" }, &CreatePointerType );
		RegisterDynamicTypeCreator( red::StringView{ "handle:" }, &CreateHandleType );
		RegisterDynamicTypeCreator( red::StringView{ "rRef:" }, &CreateResourceReferenceType );
		RegisterDynamicTypeCreator( red::StringView{ "raRef:" }, &CreateAsyncResourceReferenceType );
		RegisterDynamicTypeCreator( red::StringView{ "whandle:" }, &CreateWeakHandleType );
		RegisterDynamicTypeCreator( red::StringView{ "script_ref:" }, &CreateScriptedReferenceType );
	}

} // rtti
