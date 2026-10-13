/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptDataObject.h"
#include "scriptDataLoader.h"
#include "scriptDataSaver.h"
#include "scriptDataBinder.h"
#include "scriptDataEnvironment.h"
#include "scriptDataSaverPrivate.h"
#include "scriptDataLoaderPrivate.h"
#include "scriptDataValidator.h"
#include "../../redFileSystem/include/fileSys.h"
#include "stringRTTI.h"

#include "rttiType.h"
#include "scriptDataFormat.h"
#include "scriptDataTypes.h"
#include "variant.h"

using red::DynArray;

CScriptedDataEnvironment::CScriptedDataEnvironment()
	: m_symbolMap( red::PoolScript() )
	, m_functionMap( red::PoolScript() )
	, m_typeRefMap( red::PoolScript() )
	, m_scriptFiles( red::PoolScript() )
	, m_globalFunctions( red::PoolScript() )
	, m_globalEnums( red::PoolScript() )
	, m_globalBitfields( red::PoolScript() )
	, m_globalClasses( red::PoolScript() )
	, m_globalTypeRefs( red::PoolScript() )
{
}

CScriptedDataEnvironment::~CScriptedDataEnvironment()
{
	Clear();
}

void CScriptedDataEnvironment::Clear()
{
	m_symbolMap.Clear();
	m_functionMap.Clear();
	m_typeRefMap.Clear();

	red::alg::ClearPtr( m_scriptFiles );
	red::alg::ClearPtr( m_globalFunctions );
	red::alg::ClearPtr( m_globalEnums );
	red::alg::ClearPtr( m_globalBitfields );
	red::alg::ClearPtr( m_globalClasses );
	red::alg::ClearPtr( m_globalTypeRefs );
}

void CScriptedDataEnvironment::GetAllObjects( DynArray< const IScriptDataObject* >& allObjects, const Bool recursive ) const
{
	// reserve space
	{
		Uint32 totalCount = 0;
		totalCount += m_globalClasses.Size();
		totalCount += m_globalEnums.Size();
		totalCount += m_globalBitfields.Size();
		totalCount += m_globalFunctions.Size();
		totalCount += m_globalTypeRefs.Size();

		allObjects.Reserve( allObjects.Size() + totalCount );
	}

	// order here matters a little bit

	// typerefs go first
	for ( const auto* ptr : m_globalTypeRefs  )
	{
		allObjects.PushBack( ptr );
	}

	// next are classes
	for ( const auto* ptr : m_globalClasses )
	{
		allObjects.PushBack( ptr );

		if ( recursive )
		{
			for ( const auto* sub : ptr->GetProperties() )
				allObjects.PushBack( sub );

			for ( const auto* sub : ptr->GetOverridenProperties() )
				allObjects.PushBack( sub );

			for ( const auto* sub : ptr->GetFunctions() )
				allObjects.PushBack( sub );
		}
	}

	// then enums
	for ( const auto* ptr : m_globalEnums )
	{
		allObjects.PushBack( ptr );
	}

	// bitfields
	for ( const auto* ptr : m_globalBitfields )
	{
		allObjects.PushBack( ptr );
	}

	// and as the last bit - functions
	for ( const auto* ptr : m_globalFunctions )
	{
		allObjects.PushBack( ptr );
	}
}

void CScriptedDataEnvironment::GetAllEnums( DynArray< const CScriptedDataEnum* >& allEnums ) const
{
	allEnums.Reserve( allEnums.Size() + m_globalEnums.Size() );

	for ( const auto* ptr : m_globalEnums )
	{
		allEnums.PushBack( ptr );
	}
}

void CScriptedDataEnvironment::GetAllClasses( DynArray< const CScriptedDataClass* >& allClasses ) const
{
	allClasses.Reserve( allClasses.Size() + m_globalClasses.Size() );

	for ( const auto* ptr : m_globalClasses )
	{
		allClasses.PushBack( ptr );
	}
}

void CScriptedDataEnvironment::GetAllCastFunctions( DynArray< const CScriptedDataFunction* >& allFunctions ) const
{
	allFunctions.Reserve( allFunctions.Size() + m_globalFunctions.Size() );

	for ( const auto* ptr : m_globalFunctions )
	{
		if ( ptr->IsCast() )
		{
			allFunctions.PushBack( ptr );
		}
	}
}

void CScriptedDataEnvironment::GetAllGlobalFunctions( DynArray< const CScriptedDataFunction* >& allFunctions ) const
{
	allFunctions.Reserve( allFunctions.Size() + m_globalFunctions.Size() );

	for ( const auto* ptr : m_globalFunctions )
	{
		allFunctions.PushBack( ptr );
	}
}

void CScriptedDataEnvironment::GetAllOperatorFunctions( DynArray< const CScriptedDataFunction* >& allFunctions ) const
{
	allFunctions.Reserve( allFunctions.Size() + m_globalFunctions.Size() );

	for ( const auto* ptr : m_globalFunctions )
	{
		if ( ptr->IsOperator() )
		{
			allFunctions.PushBack( ptr );
		}
	}
}

void CScriptedDataEnvironment::GetAllScriptFiles( DynArray< const CScriptedDataFileInfo* >& allFiles ) const
{
	allFiles.Reserve( allFiles.Size() + m_scriptFiles.Size() );

	for ( const auto* ptr : m_scriptFiles )
	{
		allFiles.PushBack( ptr );
	}
}

Bool CScriptedDataEnvironment::Save( const red::AbsolutePath& absoluteFilePath ) const
{
	auto file = GFileManager->CreateFileWriter( absoluteFilePath, FOF_AbsolutePath | FOF_Buffered );
	if ( !file )
		return false;

	return Save( *file );
}

Bool CScriptedDataEnvironment::Save( IFile& file ) const
{
	// Prepare mapper
	CScriptDataFormat data;
	CScriptDataEnvironmentMapper mapper( data );

	// map all objects
	mapper << m_scriptFiles;
	mapper << m_globalEnums;
	mapper << m_globalBitfields;
	mapper << m_globalTypeRefs;
	mapper << m_globalClasses;
	mapper << m_globalFunctions;
	
	// Save the objects
	CScriptDataEnvironmentSaver saver( mapper );
	return saver.Save( file );
}

Bool CScriptedDataEnvironment::Load( const red::AbsolutePath& absoluteFilePath )
{
	auto file = GFileManager->CreateFileReader( absoluteFilePath, FOF_AbsolutePath | FOF_Buffered );
	if ( !file )
		return false;

	return Load( *file );
}

Bool CScriptedDataEnvironment::Load( IFile& file )
{
	// Prepare data holders
	CScriptDataFormat data;
	CScriptDataLoader loader( data );

	// Load data
	if ( !loader.Load( file ) )
		return false;

	// Clear current content
	Clear();
	
	CreateTypes( loader.GetObjects() );
	CreateTypeRefs( loader.GetObjects() );
	ResolveTypes( loader.GetObjects() );
	ResolveFunctionsCode();

	// Objects loaded
	return true;
}

Bool CScriptedDataEnvironment::ValidateHeader( const red::AbsolutePath& absoluteFilePath )
{
	auto file = GFileManager->CreateFileReader( absoluteFilePath, FOF_AbsolutePath | FOF_Buffered );
	if ( !file )
		return false;

	CScriptDataFormat data;
	CScriptDataLoader loader( data );

	return loader.ValidateHeader( *file );
}

void CScriptedDataEnvironment::CreateTypes( const DynArray< IScriptDataObject* >& objects )
{
	for ( auto* object : objects )
	{
		if ( !object )
			continue;

		const IScriptDataObject* const parent = object->GetParent();

		switch ( object->GetMetaType() )
		{
			// process single objects
			case IScriptDataObject::EMetaType::Enum:
			{
				const auto* const enumerator = static_cast< const CScriptedDataEnum* >( object );
				if ( CScriptedDataEnum* const addedEnum = AddEnum( enumerator->GetName(), enumerator->GetSize() ) )
				{
					*addedEnum = *enumerator;
					AddTypeRefEnum( addedEnum );
				}
			}
			break;
			
			case IScriptDataObject::EMetaType::Bitfield:
			{
				const auto* const bitfield = static_cast< const CScriptedDataBitfield* >( object );
				if ( CScriptedDataBitfield* const addedBitfield = AddBitfield( bitfield->GetName(), bitfield->GetSize() ) )
				{
					*addedBitfield = *bitfield;
					AddTypeRefBitfield( addedBitfield );
				}
			}
			break;

			case IScriptDataObject::EMetaType::Class:
			{
				const auto* const classObject = static_cast< const CScriptedDataClass* >( object );
				if ( CScriptedDataClass* const addedClass = AddClass( classObject->GetName() ) )
				{
					*addedClass = *classObject;
					AddTypeRefClass( addedClass );
				}
			}
			break;

			case IScriptDataObject::EMetaType::Function:
				{
					if ( !parent )
					{
						const CScriptedDataFunction* const func = static_cast< CScriptedDataFunction* >( object );
						if ( CScriptedDataFunction* const addedFunction = AddFunction( func->GetName(), func->GetFamilyName() ) )
						{
							*addedFunction = *func;
						}
					}
				}
				break;

			case IScriptDataObject::EMetaType::FileInfo:
				{
					const auto* const fileInfo = static_cast< const CScriptedDataFileInfo* >( object );				
					if ( CScriptedDataFileInfo* const addedFileInfo = AddFileInfo( fileInfo->GetHash() ) )
					{
						*addedFileInfo = *fileInfo;
					}
				}
				break;
		}
	}
}

void CScriptedDataEnvironment::CreateTypeRefs( const DynArray< IScriptDataObject* >& objects )
{
	for ( auto* object : objects )
	{
		if ( !object )
			continue;

		if ( object->GetMetaType() == IScriptDataObject::EMetaType::TypeRef )
		{
			ResolveTypeRef( static_cast< const CScriptedDataTypeRef* >( object ) );
		}
	}
}

void CScriptedDataEnvironment::ResolveTypes( const DynArray< IScriptDataObject* >& objects )
{
	for ( auto* object : objects )
	{
		if ( !object )
			continue;

		const IScriptDataObject* const parent = object->GetParent();

		switch ( object->GetMetaType() )
		{
		case IScriptDataObject::EMetaType::Class:
			{
				auto const classObject = static_cast< const CScriptedDataClass* >( object );
				ResolveClass( classObject );
			}
			break;

		case IScriptDataObject::EMetaType::Function:
			{
				if ( !parent )
				{
					auto const referenceFunc = static_cast< const CScriptedDataFunction* >( object );
					auto const addedFunc = FindFunction( referenceFunc->GetName() );
					ResolveFunction( addedFunc, referenceFunc );
				}
			}
			break;
		}
	}
}

void CScriptedDataEnvironment::ResolveFunction( CScriptedDataFunction* const addedFunction, const CScriptedDataFunction* const referenceFunction )
{
	RED_ASSERT( addedFunction );

	if ( const CScriptedDataTypeRef* const returnType = referenceFunction->GetReturnValue().GetType() )
	{
		addedFunction->GetReturnValue().SetType( ResolveTypeRef( returnType ) );
	}

	for ( auto& param : addedFunction->GetParams() )
	{
		param->SetType( ResolveTypeRef( referenceFunction->FindParam( param->GetName() )->GetType() ) );
	}

	for ( auto& local : addedFunction->GetLocals() )
	{
		local->SetType( ResolveTypeRef( referenceFunction->FindLocal( local->GetName() )->GetType() ) );
	}
}

void CScriptedDataEnvironment::ResolveClass( const CScriptedDataClass* const referenceClass )
{
	CScriptedDataClass* const addedClass = FindClass( referenceClass->GetName() );
	RED_ASSERT( addedClass );
	
	if ( const CScriptedDataClass* const baseClass = referenceClass->GetBaseClass() )
	{
		addedClass->SetBaseClass( FindClass( baseClass->GetName() ) );
	}

	for ( auto& prop : addedClass->GetProperties() )
	{
		prop->SetType( ResolveTypeRef( referenceClass->FindLocalProperty( prop->GetName() )->GetType() ) );
	}

	for ( auto& prop : addedClass->GetOverridenProperties() )
	{
		prop->SetType( ResolveTypeRef( referenceClass->FindProperty( prop->GetName() )->GetType() ) );
	}

	for ( auto& func : addedClass->GetFunctions() )
	{
		ResolveFunction( func, referenceClass->FindLocalFunction( func->GetName() ) );
	}
}

CScriptedDataTypeRef* CScriptedDataEnvironment::ResolveTypeRef( const CScriptedDataTypeRef* const typeref )
{
	CScriptedDataTypeRef* result = nullptr;
	const IScriptDataObject* const internalObj = typeref->GetInternal();

	switch ( typeref->GetType() )
	{
	case CScriptedDataTypeRef::EType::Named:
		result = AddTypeRefNamed( typeref->GetName() );
		break;

	case CScriptedDataTypeRef::EType::Internal:
		{
			const IScriptDataObject* existingObj = FindType( typeref->GetName() );
		
			switch ( existingObj->GetMetaType() )
			{
			case IScriptDataObject::EMetaType::Enum:
				{
					const CScriptedDataEnum* const existingEnum = FindEnum( existingObj->GetName() );
					RED_ASSERT( existingEnum );
					result = AddTypeRefEnum( existingEnum );
				}
				break;

			case IScriptDataObject::EMetaType::Bitfield:
				{
					const CScriptedDataBitfield* existingBitfield = FindBitfield( existingObj->GetName() );
					RED_ASSERT( existingBitfield );
					result = AddTypeRefBitfield( existingBitfield );
				}
				break;

			case IScriptDataObject::EMetaType::Class:
				{
					const CScriptedDataClass* existingClass = FindClass( existingObj->GetName() );
					RED_ASSERT( existingClass );
					result = AddTypeRefClass( existingClass );
				}
				break;
			}
		}
		break;

		case CScriptedDataTypeRef::EType::StrongHandle:
			{
				const CScriptedDataTypeRef* existingRef = FindTypeRef( internalObj->GetName() );
				RED_ASSERT( existingRef );
				result = AddTypeRefHandle( existingRef, false );
			}
			break;

		case CScriptedDataTypeRef::EType::WeakHandle:
			{
				const CScriptedDataTypeRef* existingRef = FindTypeRef( internalObj->GetName() );
				RED_ASSERT( existingRef );
				result = AddTypeRefHandle( existingRef, true );
			}
			break;

		case CScriptedDataTypeRef::EType::DynArray:
			{
				const CScriptedDataTypeRef* existingRef = FindTypeRef( internalObj->GetName() );
				RED_ASSERT( existingRef );
				result = AddTypeRefDynArray( existingRef );
			}
			break;

		case CScriptedDataTypeRef::EType::StaticArray:
			{
				const CScriptedDataTypeRef* existingRef = FindTypeRef( internalObj->GetName() );
				RED_ASSERT( existingRef );
				result = AddTypeRefStaticArray( existingRef, typeref->GetArrayCount() );
			}
			break;

		case CScriptedDataTypeRef::EType::Reference:
			{
				const CScriptedDataTypeRef* existingRef = FindTypeRef( internalObj->GetName() );
				RED_ASSERT( existingRef );
				result = AddTypeRefReference( existingRef );
			}
			break;
	}

	return result;
}

void CScriptedDataEnvironment::ResolveFunctionsCode()
{
	for ( auto* func : m_globalFunctions )
	{
		Helper::CodeRuntimeToRuntime transformer( this, (void*)func->GetCode(), func->GetCodeSize() );
		transformer.PerformTransform();
	}

	for ( auto* classObj : m_globalClasses )
	{
		for ( auto* func : classObj->GetFunctions() )
		{
			Helper::CodeRuntimeToRuntime transformer( this, (void*)func->GetCode(), func->GetCodeSize() );
			transformer.PerformTransform();
		}
	}
}

IScriptDataObject* CScriptedDataEnvironment::FindType( const CName name ) const
{
	IScriptDataObject* ret = nullptr;
	m_symbolMap.Find( name, ret );
	return ret;
}

CScriptedDataClass* CScriptedDataEnvironment::FindClass( const CName name ) const
{
	IScriptDataObject* ret = nullptr;
	m_symbolMap.Find( name, ret );

	if ( !ret || ret->GetMetaType() != IScriptDataObject::EMetaType::Class )
		return nullptr;

	return static_cast< CScriptedDataClass* >( ret );
}

CScriptedDataEnum* CScriptedDataEnvironment::FindEnum( const CName name ) const
{
	IScriptDataObject* ret = nullptr;
	m_symbolMap.Find( name, ret );

	if ( !ret || ret->GetMetaType() != IScriptDataObject::EMetaType::Enum )
		return nullptr;

	return static_cast< CScriptedDataEnum * >( ret );
}

CScriptedDataNamedValue* CScriptedDataEnvironment::FindEnumValue( const CName enumName, const CName valueName ) const
{
	const CScriptedDataEnum* enumPtr = FindEnum( enumName );
	if ( enumPtr != nullptr )
	{
		return enumPtr->FindValue( valueName );
	}

	return nullptr;
}

CScriptedDataBitfield* CScriptedDataEnvironment::FindBitfield( const CName name ) const
{
	IScriptDataObject* ret = nullptr;
	m_symbolMap.Find( name, ret );

	if ( !ret || ret->GetMetaType() != IScriptDataObject::EMetaType::Bitfield )
		return nullptr;

	return static_cast< CScriptedDataBitfield * >( ret );
}

CScriptedDataFunction* CScriptedDataEnvironment::FindFunction( const CName name ) const
{
	CScriptedDataFunction* ret = nullptr;
	m_functionMap.Find( name, ret );

	if ( !ret || ret->GetMetaType() != IScriptDataObject::EMetaType::Function )
		return nullptr;

	return static_cast< CScriptedDataFunction * >( ret );
}

void CScriptedDataEnvironment::EnumFunctionsFromFamily( const CName familyName, red::Map< CName, const CScriptedDataFunction* >& functions ) const
{
	for ( const auto& it : m_functionMap )
	{
		CScriptedDataFunction* f = it.Value();
		if ( f->GetMetaType() == IScriptDataObject::EMetaType::Function && f->GetFamilyName() == familyName )
		{
			// We need to use Insert instead of assignment, so that global functions don't "hide" local ones.
			functions.Insert( f->GetName(), f );
		}
	}
}

CScriptedDataClass* CScriptedDataEnvironment::AddClass( const CName name )
{
	if ( m_symbolMap.KeyExist( name ) )
	{
		RED_LOG_ERROR( "[CScriptedDataEnvironment] Duplicate Symbol: Class '%hs'", name.AsChar() );
		return nullptr;
	}

	CScriptedDataClass* obj = RED_NEW( CScriptedDataClass )( name );
	m_globalClasses.PushBack( obj );
	m_symbolMap.Insert( name, obj );
	return obj;
}

CScriptedDataEnum* CScriptedDataEnvironment::AddEnum( const CName name, const Uint32 size )
{
	if ( m_symbolMap.KeyExist( name ) )
	{
		RED_LOG_ERROR( "[CScriptedDataEnvironment] Duplicate Symbol: Enum '%hs'", name.AsChar() );
		return nullptr;
	}

	CScriptedDataEnum* obj = RED_NEW( CScriptedDataEnum )( name );
	obj->SetSize( size );

	m_globalEnums.PushBack( obj );
	m_symbolMap.Insert( name, obj );
	return obj;
}

CScriptedDataBitfield* CScriptedDataEnvironment::AddBitfield( const CName name, const Uint32 size )
{
	if ( m_symbolMap.KeyExist( name ) )
	{
		RED_LOG_ERROR( "[CScriptedDataEnvironment] Duplicate Symbol: Bitfield '%hs'", name.AsChar() );
		return nullptr;
	}

	CScriptedDataBitfield* obj = RED_NEW( CScriptedDataBitfield )( name );
	obj->SetSize( size );

	m_globalBitfields.PushBack( obj );
	m_symbolMap.Insert( name, obj );
	return obj;
}

CScriptedDataFunction* CScriptedDataEnvironment::AddFunction( const CName functionName, const CName familyName )
{
	if ( m_functionMap.KeyExist( functionName ) )
	{
		RED_LOG_ERROR( "[CScriptedDataEnvironment] Duplicate Symbol: Function '%hs'", functionName.AsChar() );
		return nullptr;
	}

	// global functions are always public
	CScriptedDataFunction* obj = RED_NEW( CScriptedDataFunction )( functionName, familyName, nullptr, IScriptDataObject::EVisibility::Public );
	m_globalFunctions.PushBack( obj );
	m_functionMap.Insert( functionName, obj );
	return obj;
}

CScriptedDataFileInfo* CScriptedDataEnvironment::AddFileInfo( const Uint32 hash )
{
	const CScriptedDataFileInfo* fileInfo = FindScriptFile( hash );
	if ( fileInfo )
	{
		RED_LOG_ERROR( "[CScriptedDataEnvironment] Duplicate script file: '%hs'", fileInfo->GetRelativePath().AsChar() );
		return nullptr;
	}

	CScriptedDataFileInfo* obj = RED_NEW( CScriptedDataFileInfo );
	obj->SetHash( hash );
	obj->SetFileIndex( m_scriptFiles.Size() );

	m_scriptFiles.PushBack( obj );

	return obj;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::FindTypeRef( const CName name ) const
{
	const CName searchName( CScriptDataTypes::TranslateFallbackTypes( name ) );
	return FindTypeRefInternal( searchName );
}

CScriptedDataTypeRef* CScriptedDataEnvironment::FindTypeRefInternal( const CName name ) const
{
	CScriptedDataTypeRef* typeRef = nullptr;
	red::ScopedSharedLock< Lock > lock( m_typesLock );
	m_typeRefMap.Find( name, typeRef );
	return typeRef;
}

CScriptedDataFileInfo* CScriptedDataEnvironment::FindScriptFile( const Uint32 hash ) const
{
	for ( auto* file : m_scriptFiles )
	{
		if ( file->GetHash() == hash )
			return file;
	}

	return nullptr;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefNamed( const CName name )
{
	const CName searchName( CScriptDataTypes::TranslateFallbackTypes( name ) );

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	if ( !CScriptDataTypes::IsBuiltInType( name ) )
	{
		return nullptr;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::Named, searchName );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefClass( const CScriptedDataClass* existingObject )
{
	RED_FATAL_ASSERT( existingObject != nullptr, "Invalid object" );

	const CName searchName = existingObject->GetName();

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::Internal, searchName, existingObject );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefEnum( const CScriptedDataEnum* existingObject )
{
	RED_FATAL_ASSERT( existingObject != nullptr, "Invalid object" );

	const CName searchName = existingObject->GetName();

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::Internal, searchName, existingObject );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefBitfield( const CScriptedDataBitfield* existingObject )
{
	RED_FATAL_ASSERT( existingObject != nullptr, "Invalid object" );

	const CName searchName = existingObject->GetName();

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::Internal, searchName, existingObject );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefDynArray( const CScriptedDataTypeRef* elementType )
{
	RED_FATAL_ASSERT( elementType != nullptr, "Array element type not specified" );

	String fullName( "array:" );
	fullName += elementType->GetName().AsChar();

	const CName searchName = RED_NAME( fullName );

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::DynArray, searchName, elementType );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefStaticArray( const CScriptedDataTypeRef* elementType, const Int32 count )
{
	RED_FATAL_ASSERT( elementType != nullptr, "Array element type not specified" );
	RED_FATAL_ASSERT( count >= 1, "Static array count must be at least 1" );

	String fullName( elementType->GetName().AsChar() );
	fullName += String::Printf( ("[%d]"), count );

	const CName searchName = RED_NAME( fullName );

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::StaticArray, searchName, elementType, count );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefHandle( const CScriptedDataTypeRef* elementType, Bool weak )
{
	RED_FATAL_ASSERT( elementType != nullptr, "Handle element not specified" );
	RED_FATAL_ASSERT( elementType->GetType() == CScriptedDataTypeRef::EType::Internal, "Handle inner type must be class" );
	RED_FATAL_ASSERT( elementType->GetInternal() != nullptr, "Handle inner type does not exist" );
	RED_FATAL_ASSERT( elementType->GetInternal()->GetMetaType() == IScriptDataObject::EMetaType::Class, "Handle inner type must be class" );

	String fullName = weak ? "wref:" : "ref:";
	fullName += elementType->GetName().AsStringView();

	const CName searchName = RED_NAME( fullName );

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( weak ? CScriptedDataTypeRef::EType::WeakHandle : CScriptedDataTypeRef::EType::StrongHandle, searchName, elementType );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

CScriptedDataTypeRef* CScriptedDataEnvironment::AddTypeRefReference( const CScriptedDataTypeRef* elementType )
{
	RED_FATAL_ASSERT( elementType != nullptr, "Reference element type not specified" );

	String fullName = "script_ref:";
	fullName += elementType->GetName().AsStringView();
	const CName searchName = RED_NAME( fullName );

	// find existing
	CScriptedDataTypeRef* typeRef = FindTypeRefInternal( searchName );
	if ( typeRef != nullptr )
	{
		return typeRef;
	}

	// register type reference
	typeRef = RED_NEW( CScriptedDataTypeRef )( CScriptedDataTypeRef::EType::Reference, searchName, elementType );
	AddTypeRefInternal( typeRef, searchName );
	return typeRef;
}

void CScriptedDataEnvironment::AddTypeRefInternal( CScriptedDataTypeRef* typeRef, const CName name )
{
	red::ScopedLock< Lock > lock( m_typesLock );
	m_globalTypeRefs.PushBack( typeRef );
	m_typeRefMap.Insert( name, typeRef );
}