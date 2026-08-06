/**
* Copyright (c) 2007-2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "scriptDataEnvironment.h"
#include "scriptDataObject.h"
#include "scriptDataBinder.h"
#include "rttiEnum.h"
#include "rttiBitField.h"
#include "scriptable.h"

using red::DynArray;

namespace
{
	struct PropertyDefaultValue
	{
		RED_USE_MEMORY_POOL( red::PoolScript );

		CName propertyName;
		red::String propertyValue;
	};

	red::Map< CName, DynArray< PropertyDefaultValue > > s_overridenDefaultValues{ red::PoolScript() };

	Uint32 ParseAttributeValue( const char* propertyName, const char* className, const char* attributeName, const red::String& attributeValue, class IScriptDataErrorReporter& err, Bool& result )
	{
		char* end = nullptr;
		Uint32 value = 0;

		if ( !attributeValue.Empty() )
		{
			red::StringToInt( value, attributeValue.AsChar(), &end, red::Base::BaseTen );
			if ( !end || *end != '\0' )
			{
				err.BindingError( "Property '%hs' from class '%hs' has invalid '%hs' attribute value '%s'", propertyName, className, attributeName, attributeValue.AsChar() );
				result = false;
			}
		}

		return value;
	}

	Int32 c_invalidCodeOffset = -1;
}

CScriptDataBinder::CScriptDataBinder( rtti::ITypeSystem& rtti, const TScriptFileBinder& fileBinder)
	: m_rtti( &rtti )
	, m_fileBinder(fileBinder)
{
	RED_FATAL_ASSERT( m_fileBinder );
}

Bool CScriptDataBinder::BindScripts( const class CScriptedDataEnvironment& env, class IScriptDataErrorReporter& err ) const
{
	// Create files
	DynArray< const CScriptedDataFileInfo* > allFiles{ red::PoolScript() };
	env.GetAllScriptFiles( allFiles );
	CreateFiles( allFiles );

	// notify replication RTTI that RTTI rebuild is on its way
	rep::IRTTIService::GetInstance().InvalidateRuntimeData();

	// get all objects to resolve
	DynArray< const IScriptDataObject* > allObjects{ red::PoolScript() };
	env.GetAllObjects( allObjects, true );

	// resolve the imports
	if ( !ResolveImports( allObjects, err ) )
	{
		return false;
	}

	// create our objects (classes, enums, bitfields)
	if ( !CreateTypes( allObjects, err ) )
	{
		return false;
	}

	// resolve type references - all objects should be known
	ResolveTypeRefs( allObjects );

	// create class properties (we need all the types to be there)
	if ( !CreateClassProperties( allObjects, err ) )
	{
		return false;
	}

	// create functions (we need all the types + all the class properties)
	if ( !CreateFunctions( allObjects, err ) )
	{
		return false;
	}

	// convert opcodes
	LoadOpcodes( allObjects );

	// rebuild runtime data (e.g. reindex classes to again have coherent RTTI class tree)
	m_rtti->RebuildRuntimeData();

	return true;
}

void CScriptDataBinder::ResolveTypeRefs( const DynArray< const IScriptDataObject* >& allObjects ) const
{
	// resolve imported stuff first
	for ( const IScriptDataObject* obj : allObjects )
	{
		// only typerefs here
		if ( obj->GetMetaType() != IScriptDataObject::EMetaType::TypeRef )
			continue;

		// the way we resolve this depends on the type
		const auto* typeRef = static_cast< const CScriptedDataTypeRef* >( obj );
		switch ( typeRef->GetType() )
		{
			// named type - used for basic simple types and other "common knowledge" types
			case CScriptedDataTypeRef::EType::Named:
			{
				const CName typeName( obj->GetName() );
				typeRef->m_resolvedPtr = m_rtti->FindType( typeName );
				RED_FATAL_ASSERT( typeRef->m_resolvedPtr != nullptr, "Type '%hs' not resolvable. This should not pass validation step", typeName.AsChar() );
				break;
			}

			// internal type - should be already created or imported
			case CScriptedDataTypeRef::EType::Internal:
			{
				RED_FATAL_ASSERT( typeRef->GetInternal() != nullptr, "No internal object" );

				// how we resolve this type depends on what it is
				switch ( typeRef->GetInternal()->GetMetaType() )
				{
					case IScriptDataObject::EMetaType::Class:
					{
						const auto* classRef = static_cast< const CScriptedDataClass* >( typeRef->GetInternal() );
						RED_FATAL_ASSERT( classRef->m_resolvedPtr != nullptr, "Class '%hs' is not resolved. This should not pass validation step", classRef->GetName().AsChar() );
						typeRef->m_resolvedPtr = classRef->m_resolvedPtr;
						break;
					}

					case IScriptDataObject::EMetaType::Enum:
					{
						const auto* enumRef = static_cast< const CScriptedDataEnum* >( typeRef->GetInternal() );
						RED_FATAL_ASSERT( enumRef->m_resolvedPtr != nullptr, "Enum '%hs' is not resolved. This should not pass validation step", enumRef->GetName().AsChar() );
						typeRef->m_resolvedPtr = enumRef->m_resolvedPtr;
						break;
					}

					case IScriptDataObject::EMetaType::Bitfield:
					{
						const auto* bitfieldRef = static_cast< const CScriptedDataBitfield* >( typeRef->GetInternal() );
						RED_FATAL_ASSERT( bitfieldRef->m_resolvedPtr != nullptr, "Bitfield '%hs' is not resolved. This should not pass validation step", bitfieldRef->GetName().AsChar() );
						typeRef->m_resolvedPtr = bitfieldRef->m_resolvedPtr;
						break;
					}

					default:
					{
						RED_FATAL( "Type '%hs' referenced internal object '%hs' that cannot be used as a type. This should not pass validation step.", typeRef->GetInternal()->GetName().AsChar(), obj->GetName().AsChar() );
					}
				}

				break;
			}

			// dynamic array
			case CScriptedDataTypeRef::EType::DynArray:
			{
				RED_FATAL_ASSERT( typeRef->GetInternal() != nullptr, "No internal object" );

				// dynarray has internal object that is another type ref
				const auto* internalTypeRef = static_cast< const CScriptedDataTypeRef* >( typeRef->GetInternal() );
				RED_FATAL_ASSERT( internalTypeRef->GetMetaType() == IScriptDataObject::EMetaType::TypeRef, "Dynamic array should have another type ref internally. This should not pass validation step." );
				RED_FATAL_ASSERT( internalTypeRef->m_resolvedPtr != nullptr, "Dynamic array internal type is not resolved. This should not pass validation step." );

				// build dynamic array around that type
				// ctremblay Legacy pool id. Refactor.
				typeRef->m_resolvedPtr = m_rtti->FindType( rtti::FormatDynArrayTypeName( internalTypeRef->m_resolvedPtr->GetName() ) );
				RED_FATAL_ASSERT( typeRef->m_resolvedPtr != nullptr, "Failed to resolve dynamic array type for '%hs'. This should not pass validation step", typeRef->GetName().AsChar() );

				break;
			}

			// static array
			case CScriptedDataTypeRef::EType::StaticArray:
			{
				RED_FATAL_ASSERT( typeRef->GetInternal() != nullptr, "No internal object" );

				// dynarray has internal object that is another type ref
				const auto* internalTypeRef = static_cast< const CScriptedDataTypeRef* >( typeRef->GetInternal() );
				RED_FATAL_ASSERT( internalTypeRef->GetMetaType() == IScriptDataObject::EMetaType::TypeRef, "Static array should have another type ref internally. This should not pass validation step." );
				RED_FATAL_ASSERT( internalTypeRef->m_resolvedPtr != nullptr, "Static array internal type is not resolved. This should not pass validation step." );
				RED_FATAL_ASSERT( typeRef->GetArrayCount() >= 1, "Static array array count is zero. This should not pass validation step." );

				// build dynamic array around that type
				typeRef->m_resolvedPtr = m_rtti->FindType( rtti::FormatNativeArrayTypeName( internalTypeRef->m_resolvedPtr->GetName(), typeRef->GetArrayCount() ) );
				RED_FATAL_ASSERT( typeRef->m_resolvedPtr != nullptr, "Failed to resolve static array type for '%hs'. This should not pass validation step", typeRef->GetName().AsChar() );

				break;
			}

			// handle
			case CScriptedDataTypeRef::EType::StrongHandle:
			case CScriptedDataTypeRef::EType::WeakHandle:
			{
				RED_FATAL_ASSERT( typeRef->GetInternal() != nullptr, "No internal object" );

				// handle has internal object that is another type ref to a class
				const auto* internalTypeRef = static_cast< const CScriptedDataTypeRef* >( typeRef->GetInternal() );
				RED_FATAL_ASSERT( internalTypeRef->GetMetaType() == IScriptDataObject::EMetaType::TypeRef, "Handle should have another type ref internally. This should not pass validation step." );
				RED_FATAL_ASSERT( internalTypeRef->m_resolvedPtr != nullptr, "Handle internal type is not resolved. This should not pass validation step." );
				RED_FATAL_ASSERT( internalTypeRef->m_resolvedPtr->GetType() == RT_Class, "Handle internal type is not a class. This should not pass validation step." );

				// build handle around that type
				CName handleTypeName = typeRef->GetType() ==  CScriptedDataTypeRef::EType::StrongHandle ? rtti::FormatHandleTypeName( internalTypeRef->m_resolvedPtr->GetName() )
																										: rtti::FormatWeakHandleTypeName( internalTypeRef->m_resolvedPtr->GetName() );
				typeRef->m_resolvedPtr = m_rtti->FindType( handleTypeName );
				RED_FATAL_ASSERT( typeRef->m_resolvedPtr != nullptr, "Failed to resolve handle type for '%hs'. This should not pass validation step", typeRef->GetName().AsChar() );

				break;
			}

			// reference
			case CScriptedDataTypeRef::EType::Reference:
			{
				RED_FATAL_ASSERT( typeRef->GetInternal() != nullptr, "No internal object" );

				const auto* internalTypeRef = static_cast< const CScriptedDataTypeRef* >( typeRef->GetInternal() );
				RED_FATAL_ASSERT( internalTypeRef->GetMetaType() == IScriptDataObject::EMetaType::TypeRef, "Reference should have another type ref internally. This should not pass validation step." );
				RED_FATAL_ASSERT( internalTypeRef->m_resolvedPtr != nullptr, "Reference internal type is not resolved. This should not pass validation step." );

				// build reference around that type
				CName referenceTypeName = rtti::FormatScriptedReferenceTypeName( internalTypeRef->m_resolvedPtr->GetName() );

				typeRef->m_resolvedPtr = m_rtti->FindType( referenceTypeName );
				RED_FATAL_ASSERT( typeRef->m_resolvedPtr != nullptr, "Failed to resolve reference type for '%s'. This should not pass validation step", typeRef->GetName().AsChar() );

				break;
			}
		}
	}
}

void CScriptDataBinder::CreateFiles(const DynArray< const CScriptedDataFileInfo *>& allFiles ) const
{
	for ( auto* file : allFiles )
	{		
		CScriptFile* scriptFile = m_fileBinder(file->GetFileIndex());
		scriptFile->SetPath( file->GetRelativePath() );
		scriptFile->SetHashedPath( file->GetHash() );
		scriptFile->SetCRC( file->GetCRC() );
	}
}

Bool CScriptDataBinder::ResolveImports( const DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const
{
	// resolve imported stuff first
	for ( const IScriptDataObject* obj : allObjects )
	{
		if ( !obj->IsImported() )
			continue;

		const rtti::ClassType* parentClass = nullptr;
		if ( obj->GetParent() != nullptr && obj->GetParent()->GetMetaType() == IScriptDataObject::EMetaType::Class )
		{
			const CScriptedDataClass* parentObj = static_cast< const CScriptedDataClass * >( obj->GetParent() );
			parentClass = parentObj->m_resolvedPtr;
		}

		if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Enum )
		{
			const CScriptedDataEnum* baseObj = static_cast< const CScriptedDataEnum* >( obj );
			baseObj->m_resolvedPtr = m_rtti->FindScriptEnum( obj->GetName() );
			if ( !baseObj->m_resolvedPtr )
			{
				err.BindingError( "Missing native type '%hs'", obj->GetName().AsChar() );
				return false;
			}

			// resolve options
			for ( const auto* valueObj : baseObj->GetValues() )
			{
				Int64 value = -1;
				baseObj->m_resolvedPtr->FindValue( valueObj->GetName(), value );
				valueObj->m_resolvedValue = value;
			}
		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Bitfield )
		{
			const CScriptedDataBitfield* baseObj = static_cast< const CScriptedDataBitfield * >( obj );
			baseObj->m_resolvedPtr = m_rtti->FindBitField( obj->GetName() );
			if ( !baseObj->m_resolvedPtr )
			{
				err.BindingError( "Missing native type '%hs'", obj->GetName().AsChar() );
				return false;
			}
		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Class )
		{
			const CScriptedDataClass* baseObj = static_cast< const CScriptedDataClass * >( obj );

			if ( parentClass == nullptr )
			{
				baseObj->m_resolvedPtr = m_rtti->FindScriptClass( obj->GetName() );
				if ( !baseObj->m_resolvedPtr )
				{
					err.BindingError( "Missing native type '%hs'", obj->GetName().AsChar() );
					return false;
				}
			}
			else
			{
				// nested classes
			}
		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Function )
		{
			const CScriptedDataFunction* baseObj = static_cast< const CScriptedDataFunction * >( obj );
			const CName lookupName = baseObj->GetName();
			if ( parentClass == nullptr )
			{
				baseObj->m_resolvedPtr = m_rtti->FindGlobalFunction( lookupName );
				if ( !baseObj->m_resolvedPtr )
				{
					err.BindingError( "Missing native type '%hs'", lookupName.AsChar() );
					return false;
				}
			}
			else
			{
				baseObj->m_resolvedPtr = (const rtti::Function*) parentClass->FindFunctionNonCached( lookupName );
				if ( !baseObj->m_resolvedPtr )
				{
					err.BindingError( "Missing native type '%hs'", lookupName.AsChar() );
					return false;
				}
			}
		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Property )
		{
			const CScriptedDataProperty* baseObj = static_cast< const CScriptedDataProperty* >( obj );

			if ( parentClass == nullptr )
			{
				// globals ?
			}
			else
			{
				baseObj->m_resolvedPtr = parentClass->FindProperty( obj->GetName() );
				if ( !baseObj->m_resolvedPtr )
				{
					err.BindingError( "Missing native type '%hs'", obj->GetName().AsChar() );
					return false;
				}
			}
		}
	}

	return true;
}

Bool CScriptDataBinder::CreateTypes( const DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const
{
	Uint32 numFailedTypes = 0;

	for ( const IScriptDataObject* obj : allObjects )
	{
		// imported types already handled
		if ( obj->IsImported() )
		{
			// Native (imported) classes can still have scripted properties and methods (which will be populated later)
			// but since we're not entering CreateClass() below (since this is not a scripted class), we need to clean up
			// anything script related for the class here
			if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Class )
			{
				rtti::ClassType* classType = const_cast<rtti::ClassType*>( GetRttiSystem().FindScriptClass( obj->GetName() ) );

				// Clear out all scripted properties/functions etc that have been added to this native class
				classType->ClearScriptData();

				// Re-set the initialised flag, which is normally set by the rtti system at startup for native classes (and reset by ClearScriptData())
				// For scripted classes, this is set by CreateClass(), called below
				classType->MarkAsInitialized();

				// Notify replication of a class change
				rep::IRTTIService::GetInstance().ResetClass( classType );
			}

			continue;
		}

		// process single objects
		if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Enum )
		{
			const auto* baseObj = static_cast< const CScriptedDataEnum* >( obj );
			if ( !CreateEnum( *baseObj, err ) )
			{
				++numFailedTypes;
			}

		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Bitfield )
		{
			const auto* baseObj = static_cast< const CScriptedDataBitfield * >( obj );
			if ( !CreateBitfield( *baseObj, err ) )
			{
				++numFailedTypes;
			}
		}
		else if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Class )
		{
			const auto* baseObj = static_cast< const CScriptedDataClass * >( obj );
			if ( !CreateClass( *baseObj, err ) )
			{
				++numFailedTypes;
			}
		}
	}

	if ( numFailedTypes > 0 )
	{
		err.BindingError( numFailedTypes == 1 ? "Binding failed for %lu type" : "Binding failed for %lu types", numFailedTypes );
		return false;
	}

	return true;
}

Bool CScriptDataBinder::CreateFunctions( const DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const
{
	Uint32 numFailedFunctions = 0;

	for ( const IScriptDataObject* obj : allObjects )
	{
		if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Function )
		{
			const auto* baseObj = static_cast< const CScriptedDataFunction * >( obj );
			if ( !CreateFunction( *baseObj, err ) )
			{
				++numFailedFunctions;
			}
		}
	}

	if ( numFailedFunctions > 0 )
	{
		err.BindingError( numFailedFunctions == 1 ? "Binding failed for %lu function" : "Binding failed for %lu functions", numFailedFunctions );
		return false;
	}

	return true;
}

Bool CScriptDataBinder::CreateClassProperties( const DynArray< const IScriptDataObject* >& allObjects, class IScriptDataErrorReporter& err ) const
{
	Uint32 numFailedClasses = 0;

	for ( const IScriptDataObject* obj : allObjects )
	{
		if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Class )
		{
			const auto* baseObj = static_cast< const CScriptedDataClass * >( obj );
			if ( !CreateClassProperties( *baseObj, err ) )
			{
				++numFailedClasses;
			}
		}
	}

	if ( numFailedClasses > 0 )
	{
		err.BindingError( numFailedClasses == 1 ? "Binding failed for %lu class" : "Binding failed for %lu classes", numFailedClasses );
		return false;
	}

	return true;
}

Bool CScriptDataBinder::CreateEnum( const class CScriptedDataEnum& obj, class IScriptDataErrorReporter& err ) const
{
	if ( obj.m_resolvedPtr != nullptr )
	{
		err.BindingError( "Enum '%hs' is already resolved", obj.GetName().AsChar() );
		return false;
	}

	// prepare container with enum options
	DynArray<std::pair<CName, Int64>> enumOptions{ red::PoolScript() };
	enumOptions.Reserve( obj.GetValues().Size() );
	for ( const auto* ptr : obj.GetValues() )
	{
		enumOptions.PushBack( std::make_pair( ptr->GetName(), ptr->GetValue() ) );
	}

	// create runtime object
	auto* ret = m_rtti->CreateScriptedEnum( obj.GetName(), obj.GetSize(), enumOptions );
	if ( !ret )
	{
		err.BindingError( "Failed to create enum '%hs' type", obj.GetName().AsChar() );
		return false;
	}

	obj.m_resolvedPtr = ret;

	return true;
}

Bool CScriptDataBinder::CreateBitfield( const class CScriptedDataBitfield& obj, class IScriptDataErrorReporter& err ) const
{
	if ( obj.m_resolvedPtr )
	{
		err.BindingError( "Bitfield '%hs' is already resolved", obj.GetName().AsChar() );
		return false;
	}

	// prepare container with bitfield options
	DynArray<std::pair<CName, Uint64>> bitfieldOptions{ red::PoolScript() };
	bitfieldOptions.Reserve( obj.GetValues().Size() );
	for ( const auto* ptr : obj.GetValues() )
	{
		bitfieldOptions.PushBack( std::make_pair( ptr->GetName(), ptr->GetValue() ) );
	}

	// create runtime object
	const rtti::BitFieldType* ret = m_rtti->CreateScriptedBitfield( obj.GetName(), bitfieldOptions );
	if ( !ret )
	{
		err.BindingError( "Failed to create bitfield '%hs' type", obj.GetName().AsChar() );
		return false;
	}

	obj.m_resolvedPtr = ret;

	return true;
}

Bool CScriptDataBinder::CreateClass( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const
{
	Uint32 classFlags = 0;
	if ( obj.IsAbstract() ) 
		classFlags |= CF_Abstract;
	if( obj.IsStructure() ) 
		classFlags |= CF_ScriptedStruct;
	else
		classFlags |= CF_ScriptedClass;

	// get parent (scope)
	const rtti::ClassType* parentScope = nullptr;
	if ( obj.GetParent() != nullptr )
	{
		if ( obj.GetParent()->GetMetaType() != IScriptDataObject::EMetaType::Class )
		{
			err.BindingError( "Parent type of class '%hs' has to be a class", obj.GetName().AsChar() );
			return false;
		}

		parentScope = static_cast< const CScriptedDataClass* >( obj.GetParent() )->m_resolvedPtr;

		if ( !parentScope )
		{
			err.BindingError( "Failed to resolve parent class type of class '%hs'", obj.GetName().AsChar() );
			return false;
		}
	}

	// get base class
	const rtti::ClassType* baseClass = nullptr;
	if ( obj.GetBaseClass() != nullptr )
	{
		baseClass = obj.GetBaseClass()->m_resolvedPtr;
		if ( !baseClass )
		{
			err.BindingError( "Base class type of class '%hs' is not resolved", obj.GetName().AsChar() );
			return false;
		}
	}

	// create class
	RED_ASSERT( parentScope == nullptr, "No scoping for classes, yet" );
	const rtti::ClassType* ret = m_rtti->CreateScriptedClass( obj.GetName(), classFlags, baseClass );
	if ( obj.m_resolvedPtr && obj.m_resolvedPtr != ret )
	{
		err.BindingError( "Instance of scripted class %hs should have pointer value %p, not %p", obj.GetName().AsChar(), obj.m_resolvedPtr, ret );
		return false;
	}

	obj.m_resolvedPtr = ret;

	return true;
}

Bool CScriptDataBinder::CreateFunction( const class CScriptedDataFunction& obj, class IScriptDataErrorReporter& err ) const
{
	Uint32 functionFlags = 0;
	if ( obj.IsStatic() ) functionFlags |= FF_StaticFunction;
	if ( obj.IsFinal() ) functionFlags |= FF_FinalFunction;
	if ( obj.IsEvent() ) functionFlags |= FF_EventFunction;
	if ( obj.IsExec() ) functionFlags |= FF_ExecFunction;
	if ( obj.IsTimer() ) functionFlags |= FF_TimerFunction;
	if ( obj.IsConst() )  functionFlags |= FF_ConstFunction;
	if ( obj.IsThreadSafe() ) functionFlags |= FF_ThreadSafeFunction;
	if ( obj.IsQuest() )  functionFlags |= FF_QuestFunction;

	// set visibility
	if( obj.GetVisibility() == IScriptDataObject::EVisibility::Public ) functionFlags |= FF_PublicFunction;
	if( obj.GetVisibility() == IScriptDataObject::EVisibility::Protected ) functionFlags |= FF_ProtectedFunction;
	if( obj.GetVisibility() == IScriptDataObject::EVisibility::Private ) functionFlags |= FF_PrivateFunction;

	// create function object
	const rtti::Function* func = obj.m_resolvedPtr;
	if ( !func )
	{
		// don't recreate missing import functions
		if ( obj.IsImported() )
			return true;

		// get parent scope
		const rtti::ClassType* parentScope = nullptr;
		if ( obj.GetParent() != nullptr )
		{
			if ( obj.GetParent()->GetMetaType() != IScriptDataObject::EMetaType::Class )
			{
				err.BindingError( "Parent type of function '%hs' has to be a class", obj.GetName().AsChar() );
				return false;
			}

			parentScope = static_cast< const CScriptedDataClass* >( obj.GetParent() )->m_resolvedPtr;

			if ( !parentScope )
			{
				err.BindingError( "Failed to resolve parent class type of class '%hs'", obj.GetName().AsChar() );
				return false;
			}
		}

		// create function
		if ( parentScope == nullptr )
		{
			// use existing
			func = m_rtti->FindGlobalFunction( obj.GetName() );

			// not there, recreate
			if ( !func )
			{
				rtti::Function* newFunc = RED_NEW( rtti::ScriptedMemberFunction )( parentScope, obj.GetName(), obj.GetFamilyName(), functionFlags );
				m_rtti->RegisterGlobalFunction( newFunc );
				func = newFunc;
			}
		}
		else
		{
			// use existing
			func = parentScope->FindLocalFunction( obj.GetName() );

			// not there, recreate
			if ( !func )
			{
				if ( obj.IsStatic() )
				{
					const String fullNameStr = String::Printf( "%hs::%hs", parentScope->GetName().AsChar(), obj.GetName().AsChar() );
					const CName fullName = RED_NAME( fullNameStr );
					
					rtti::Function* newFunc = RED_NEW( rtti::ScriptedMemberFunction )( nullptr, fullName, obj.GetFamilyName(), functionFlags );
					m_rtti->RegisterGlobalFunction( newFunc );
					func = newFunc;
				}
				else
				{
					rtti::Function* newFunc = RED_NEW( rtti::ScriptedMemberFunction )( parentScope, obj.GetName(), obj.GetFamilyName(), functionFlags );
					rtti::ClassType* editableParentScope = const_cast<rtti::ClassType*>(parentScope);
					editableParentScope->AddFunction( newFunc );
					func = newFunc;
				}
			}
		}

		// bind newly created function
		if ( !func )
		{
			err.BindingError( "Failed to create function '%hs'", obj.GetName().AsChar() );
			return false;
		}

		obj.m_resolvedPtr = func;
	}

	// set up function replication
	if ( obj.IsReplicable() )
	{
		SetupFunctionReplication( obj );
	}

	rtti::Function* editableFunc = const_cast<rtti::Function*>( func);

	// clear all existing stuff from the function
	// this clears both scripted and native functions
	editableFunc->Reset();

	// set return property
	if ( const CScriptedDataTypeRef* returnTypeRef = obj.GetReturnValue().GetType() )
	{
		const auto* retType = returnTypeRef->m_resolvedPtr;
		if ( !retType )
		{
			err.BindingError( "Unresolved return type '%hs' for function '%hs'", returnTypeRef->ToString().AsChar(), obj.GetName().AsChar() );
			return false;
		}

		editableFunc->SetReturnType( retType );
	}

	// create function parameters
	for ( const auto* ptr : obj.GetParams() )
	{
		const auto* type = ptr->GetType()->m_resolvedPtr;
		if ( !type )
		{
			err.BindingError( "Unresolved parameter type '%hs' for function '%hs'", ptr->GetType()->ToString().AsChar(), obj.GetName().AsChar() );
			return false;
		}

		RED_ASSERT( ptr->m_resolvedPtr == nullptr );
		editableFunc->AddParameter( ptr->GetName(), type, ptr->IsOptional(), ptr->IsSkipped(), ptr->IsReference(), ptr->m_resolvedPtr );
		RED_ASSERT( ptr->m_resolvedPtr != nullptr );

		if ( editableFunc->IsReplicable() )
		{
			rep::IRTTIService::GetInstance().AddFunctionParam( editableFunc, ptr->m_resolvedPtr, type->GetDefaultReplicatedType() );
		}
	}

	// create function local variables
	for ( const auto* ptr : obj.GetLocals() )
	{
		const auto* type = ptr->GetType()->m_resolvedPtr;
		if ( !type )
		{
			err.BindingError( "Unresolved local variable type '%hs' for function '%hs'", ptr->GetType()->ToString().AsChar(), obj.GetName().AsChar() );
			return false;
		}

		RED_ASSERT( ptr->m_resolvedPtr == nullptr );
		editableFunc->AddLocal( ptr->GetName(), type, ptr->m_resolvedPtr );
		RED_ASSERT( ptr->m_resolvedPtr != nullptr );
	}

	// recalculate function data data layout

	return true;
}

void CScriptDataBinder::SetupFunctionReplication( const CScriptedDataFunction& obj ) const
{
	RED_ASSERT( obj.IsReplicable() );

	const rtti::Function* function = obj.m_resolvedPtr;

	rep::IRTTIService::GetInstance().MakeFunctionReplicable( function, obj.IsStatic() );

	if ( obj.IsReliable() )
	{
		rep::IRTTIService::GetInstance().SetFunctionReliable( function );
	}

	EFunctionExecutionTarget target = FET_Client;
	if ( obj.IsMulticast() )
	{
		target = FET_Multicast;
	}
	else if ( obj.IsHost() )
	{
		target = FET_Host;
	}
	else if ( obj.IsClient() )
	{
		target = FET_Client;
	}
	rep::IRTTIService::GetInstance().SetFunctionExecutionTarget( function, target );
}

Bool CScriptDataBinder::CreateClassProperties( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const
{
	Bool result = true;

	// get created class
	rtti::ClassType* editableClass = const_cast<rtti::ClassType*>( obj.m_resolvedPtr );
	if ( !editableClass )
	{
		err.BindingError( "Class '%hs' is not resolved", obj.GetName().AsChar() );
		return false;
	}

	// make sure we have NO script properties
	for ( const auto* prop : editableClass->GetLocalProperties() )
	{
		if ( prop->IsScripted() )
		{
			err.BindingError( "Class '%hs' has pre-existing scripted property '%hs'", prop->GetName().AsChar() );
			result = false;
		}
	}

	const auto className = obj.GetName();

	// start adding properties
	const Bool isScriptable = editableClass->IsA< IScriptable >();
	const Bool isScriptedStruct = editableClass->IsScriptedStruct();

	// process properties
	for ( const auto* prop : obj.GetProperties() )
	{
		// don't bother with imported props
		if ( prop->IsImported() )
			continue;

		if ( editableClass->FindProperty( prop->GetName() ) )
		{
			err.BindingError( "The property with name '%s' already exists in class '%s'", prop->GetName().AsChar(), className.AsChar() );
			result = false;
			continue;
		}

		// resolve flags
		Uint64 flags = PF_Scripted | PF_Browsable; // always!
		if ( isScriptable ) flags |= PF_SecondBuffer; // place scripted properties in secondary memory buffer
		if ( prop->IsEditable() ) flags |= PF_Editable;
		if ( prop->IsInlined() ) flags |= PF_Inlined;
		if ( prop->IsInstanceEditable() ) flags |= PF_InstanceEditable;
		if ( !prop->IsBrowsable() )
		{
			flags &= ~PF_Browsable;
		}

		switch ( prop->GetVisibility() )
		{
		case IScriptDataObject::EVisibility::Private:
			flags |= PF_Private;
			break;

		case IScriptDataObject::EVisibility::Protected:
			flags |= PF_Protected;
			break;

		case IScriptDataObject::EVisibility::Public:
		default:
			flags |= PF_Public;
			break;
		}

		// resolve type
		const rtti::IType* type = prop->GetType()->m_resolvedPtr;

		// resolve persistency flag (need to check type first, as not every type is supported)
		if ( prop->IsPersistent() ) 
		{
			if ( !rtti::PropertyBuilder::IsPersistentTypeSupported( type ) )
			{
				err.BindingError( "Property '%hs' of class '%hs' cannot be persistent. Type '%hs' is not supported.", prop->GetName().AsChar(), className.AsChar(), type->GetName().AsChar() );
				result = false;
			}

			flags |= PF_Persistent;
		}

		if( type->GetType() == RT_Array )
		{
			flags |= PF_Select;
			flags |= PF_Resizable;
			flags |= PF_CanAdd;
		}

		if( type->GetType() == RT_Handle )
		{
			flags |= PF_Select;
		}

		// acquire property attributes
		const CName propertyCategory = RED_NAME( prop->GetAttributeValue( "category" ) );
		const red::String& customInnerTypeEditor = prop->GetAttributeValue( "customInnerTypeEditor" );
		const red::String& customEditor = customInnerTypeEditor.Empty() ? prop->GetAttributeValue( "customEditor" ) : customInnerTypeEditor;
		const red::String& rangeMinStr = prop->GetAttributeValue( "rangeMin" );
		const red::String& rangeMaxStr = prop->GetAttributeValue( "rangeMax" );
		const Float rangeMin = rangeMaxStr.Empty() ? 0.f : static_cast< Float >( red::StringToDouble( rangeMinStr.AsChar() ) );
		const Float rangeMax = rangeMaxStr.Empty() ? 0.f : static_cast< Float >( red::StringToDouble( rangeMaxStr.AsChar() ) );
		const red::String& minSizeStr = prop->GetAttributeValue( "minSize" );
		const red::String& maxSizeStr = prop->GetAttributeValue( "maxSize" );

		const red::String& unsavableValue = prop->GetAttributeValue( "unsavable" );
		const Bool isUnsavable = unsavableValue == "true" || unsavableValue == "1";
		if( !( flags & PF_Persistent ) && isUnsavable )
		{
			err.BindingError( "Property '%hs' of class '%hs' cannot unsavable because is not persistent", prop->GetName().AsChar(), className.AsChar() );
			result = false;
		}
		if( isUnsavable )
		{
			flags |= PF_Unsavable;
		}

		// add new property
		rtti::Property* newProperty = RED_NEW( rtti::Property )(
			const_cast< const rtti::IType* >( type ),
			editableClass,
			0,
			prop->GetName(),
			flags,
			prop->GetAttributeValue( "tooltip" ),
			propertyCategory,
			customEditor,
			rangeMin,
			rangeMax );

		// set property attributes
		if ( !customInnerTypeEditor.Empty() )
		{
			newProperty->SetCustomEditorMode( rtti::CustomEditorMode::InnerType );
		}

		if ( !minSizeStr.Empty() || !maxSizeStr.Empty() )
		{
			const char* propName = prop->GetName().AsChar();
			const char* classNameStr = className.AsChar();

			const Uint32 minSize = ParseAttributeValue( propName, classNameStr, "minSize", minSizeStr, err, result );
			const Uint32 maxSize = ParseAttributeValue( propName, classNameStr, "maxSize", maxSizeStr, err, result );
			
			newProperty->SetSizeConstraints( minSize, maxSize );
		}

		editableClass->AddProperty( newProperty );
		prop->m_resolvedPtr = newProperty;

		// handle replicated property
		if ( prop->IsReplicated() )
		{
			AddReplicatedProperty( editableClass, prop->m_resolvedPtr, *prop );
		}

		// set default values for class&struct properties
		if ( ( isScriptable || isScriptedStruct ) && prop->HasDefaultValue() )
		{
			const auto& defaultValues = prop->GetDefaultValues();
			for ( const auto& defaultValue : defaultValues )
			{
				if ( className == RED_NAME_NOREG( defaultValue.Key() ) )
				{
					editableClass->AddDefaultValue( prop->GetName(), defaultValue.Value() );
				}
				else
				{
					s_overridenDefaultValues.GetRef( RED_NAME( defaultValue.Key() ), red::PoolScript() ).PushBack( { prop->GetName(), defaultValue.Value() } );
				}
			}
		}
	}

	// process overriden properties
	for ( const auto* prop : obj.GetOverridenProperties() )
	{
		auto* exitingProp = editableClass->FindProperty( prop->GetName() );
		if ( !exitingProp )
		{
			err.BindingError( "Could not find property with name '%s' in class '%s'", prop->GetName().AsChar(), className.AsChar() );
			result = false;
			continue;
		}

		if ( editableClass->FindPropertyOverride( prop->GetName() ) )
		{
			err.BindingError( "The overriden property with name '%s' already exists in class '%s'", prop->GetName().AsChar(), className.AsChar() );
			result = false;
			continue;
		}

		// resolve flags
		if ( prop->IsBrowsable() != exitingProp->IsBrowsable() )
		{
			rtti::PropertyOverrideBuilder builder( editableClass, prop->GetName() );
			builder.browsable( prop->IsBrowsable() );
			builder.AddPropertyOverrideToClass();
		}
	}

	// search for overridden default properties
	auto it = s_overridenDefaultValues.Find( className );
	if ( it != s_overridenDefaultValues.End() )
	{
		auto& properties = it.Value();
		for ( auto propIt = properties.Begin(); propIt != properties.End(); ++propIt )
		{
			editableClass->AddDefaultValue( propIt->propertyName, propIt->propertyValue );
		}

		properties.Clear();
	}

	editableClass->RecalculateCachedProperties();
	editableClass->RecalculateCachedPersistentProperties();
	editableClass->ResolvePropertyOverrides( true );

	return result;
}

void CScriptDataBinder::AddReplicatedProperty( const rtti::ClassType* cls, const rtti::Property* property, const CScriptedDataProperty& scriptProperty ) const
{
	const rep::SimpleTypeDesc repTypeDesc( property->GetType() );
	rep::PropertyFlags flags;
	CName tag;
	rep::IRTTIService::GetInstance().AddProperty( cls, property, repTypeDesc, flags, tag );
}

void CScriptDataBinder::LoadOpcodes( const DynArray< const IScriptDataObject* >& allObjects ) const
{
	for ( const IScriptDataObject* obj : allObjects )
	{
		if ( obj->GetMetaType() == IScriptDataObject::EMetaType::Function )
		{
			const auto* baseObj = static_cast< const CScriptedDataFunction * >( obj );
			LoadOpcodes( *baseObj );
		}
	}
}

void CScriptDataBinder::LoadOpcodes( const class CScriptedDataFunction& obj ) const
{
	if ( obj.GetCodeSize() )
	{
		RED_FATAL_ASSERT( obj.m_resolvedPtr != nullptr, "Target function not created" );
			
		// upload code to function
		rtti::Function* editableFunc = const_cast<rtti::Function*>(obj.m_resolvedPtr);
		CScriptCompiledCode& runtimeCode = editableFunc->GetCode();

		Int32 fileIndex = obj.GetFileInfo()->GetFileIndex();
		RED_FATAL_ASSERT( obj.IsImported() || fileIndex >= 0, "Invalid file index of a script function." );

		runtimeCode.Initialize( fileIndex, obj.GetSourceLine(), obj.GetCode(), obj.GetCodeSize() );

		// transform it from memory to runtime format
		Helper::CodeMemoryToRuntime transformer( (void*)runtimeCode.GetCode(), obj.GetCodeSize() );
		transformer.PerformTransform();

		CScriptFile* scriptFile = m_fileBinder( fileIndex );
		auto breakpoints = std::move( transformer.GetBreakpoints() );

#ifndef RED_CONFIGURATION_FINAL
		for ( script::RuntimeBreakpoint& breakpoint : breakpoints )
		{
			breakpoint.SetFunction( editableFunc );
			scriptFile->AddBreakpoint( breakpoint );
		}

		// AddBreakpoint uses InsertUnsorted(), so we need to sort once all breakpoints are added
		scriptFile->SortBreakpoints();
#endif

#ifdef USE_PROFILER
		const Int32 profileCodeOffset = transformer.GetProfileCodeOffset();
		if ( profileCodeOffset != c_invalidCodeOffset )
		{
			scriptFile->AddInstrumentationObject( profileCodeOffset, editableFunc );
		}
#endif
	}
}

void Helper::CodeMemoryToRuntime::ProcessBreakpoint()
{
	Uint32 codeOffset = m_readPos - 1;

	// Line
	Uint16 line = Read< Uint16 >();
	Write( line );

	// Line position
	Uint32 start = Read< Uint32 >();
	Write( start );

	// Start "column"
	Uint16 offset = Read< Uint16 >();
	Write( offset );

	// Length
	Uint16 length = Read< Uint16 >();
	Write( length );

	// Breakpoint toggle
	ProcessUint8();

	// Condition
	Write< intptr_t >( Read< intptr_t >() );

	// New
	script::RuntimeBreakpoint breakpoint( codeOffset, start, offset, length, line );
	m_breakpoints.PushBack( breakpoint );
}

void Helper::CodeMemoryToRuntime::ProcessStartProfile()
{
	Uint32 codeOffset = m_readPos - 1;

	// Function name
	ProcessStaticString();

	// Instrumentation object
	Write< intptr_t >( Read< intptr_t >() );

	// Function was explicitly marked for profile
	Write< Int8 >( Read< Int8 >() );

	// New
	RED_FATAL_ASSERT( m_profileCodeOffset == c_invalidCodeOffset );
	m_profileCodeOffset = codeOffset;
}

void Helper::CodeMemoryToRuntime::ProcessTypeRef()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataTypeRef* >();
	RED_FATAL_ASSERT( !ptr || ptr->m_resolvedPtr, "Using unresolved type" );
	Write( ptr->m_resolvedPtr );
}

void Helper::CodeMemoryToRuntime::ProcessEnum()
{
	// translate placeholder references to actual runtime resolved references
	auto ptr = Read< const CScriptedDataTypeRef* >();
	RED_FATAL_ASSERT(!ptr || ptr->m_resolvedPtr, "Using unresolved type");
	Write(ptr->m_resolvedPtr);
}

void Helper::CodeMemoryToRuntime::ProcessPointer()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataTypeRef* >();
	RED_FATAL_ASSERT(!ptr || ptr->m_resolvedPtr, "Using unresolved type");
	Write(ptr->m_resolvedPtr);
}

void Helper::CodeMemoryToRuntime::ProcessPropertyRef()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataProperty* >();
	RED_FATAL_ASSERT( !ptr || ptr->m_resolvedPtr, "Using unresolved property" );
	Write( ptr->m_resolvedPtr );
}

void Helper::CodeMemoryToRuntime::ProcessLocalPropertyRef()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataFunctionLocal* >();
	RED_FATAL_ASSERT(!ptr || ptr->m_resolvedPtr, "Using unresolved property");
	Write(ptr->m_resolvedPtr);
}

void Helper::CodeMemoryToRuntime::ProcessParamPropertyRef()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataFunctionParam* >();
	RED_FATAL_ASSERT(!ptr || ptr->m_resolvedPtr, "Using unresolved property");
	Write(ptr->m_resolvedPtr);
}

void Helper::CodeMemoryToRuntime::ProcessFunctionRef()
{
	const auto* ptr = Read< const CScriptedDataFunction* >();
	RED_FATAL_ASSERT( !ptr || ptr->m_resolvedPtr, "Using unresolved function" );
	Write( ptr->m_resolvedPtr );
}

void Helper::CodeMemoryToRuntime::ProcessNamedValueRef()
{
	const auto* ptr = Read< const CScriptedDataNamedValue* >();
	RED_FATAL_ASSERT( ptr != nullptr, "Using unresolved named value" );

	// write the resolved or original value instead of the pointer
	// TODO: if we add CEnumOption object to the RTTI we can make this code a little bit better
	if ( ptr->IsImported() )
		Write<Int64>( ptr->m_resolvedValue );
	else
		Write<Int64>( ptr->GetValue() );
}

void Helper::CodeRuntimeToRuntime::ProcessBreakpoint()
{
	// Line
	ProcessUint16();

	// Line position
	ProcessUint32();

	// "Column"
	ProcessUint16();

	// Length
	ProcessUint16();

	// Breakpoint toggle
	ProcessUint8();

	// Condition
	Write< intptr_t >( Read< intptr_t >() );
}

void Helper::CodeRuntimeToRuntime::ProcessStartProfile()
{
	// Function name
	ProcessStaticString();

	// Instrumentation object
	Write< intptr_t >( Read< intptr_t >() );

	// Function was explicitly marked for profile
	Write< Int8 >( Read< Int8 >() );
}

void Helper::CodeRuntimeToRuntime::ProcessTypeRef()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataTypeRef* >();
	RED_FATAL_ASSERT( ptr, "Using unresolved type" );

	const auto* ptrReal = m_owner->FindTypeRef( ptr->GetName() );
	RED_FATAL_ASSERT( ptrReal, "Type not found!" );

	Write( ptrReal );
}

void Helper::CodeRuntimeToRuntime::ProcessEnum()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataEnum* >();
	RED_FATAL_ASSERT(ptr, "Using unresolved type");

	const auto* ptrReal = m_owner->FindTypeRef(ptr->GetName());
	RED_FATAL_ASSERT(ptrReal, "Type not found!");

	Write(ptrReal);
}

void Helper::CodeRuntimeToRuntime::ProcessPointer()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const CScriptedDataClass* >();
	RED_FATAL_ASSERT(ptr, "Using unresolved type");

	const auto* ptrReal = m_owner->FindTypeRef(ptr->GetName());
	RED_FATAL_ASSERT(ptrReal, "Type not found!");

	Write(ptrReal);
}

template< typename TScriptedData >
void Helper::CodeRuntimeToRuntime::ProcessProperty()
{
	// translate placeholder references to actual runtime resolved references
	const auto* ptr = Read< const TScriptedData* >();
	RED_FATAL_ASSERT( ptr && ptr->GetParent(), "Using unresolved type" );

	const IScriptDataObject* ptrReal = nullptr;

	switch ( ptr->GetMetaType() )
	{
	case IScriptDataObject::EMetaType::Property:
		{
			const auto* ptrClass = m_owner->FindClass( ptr->GetParent()->GetName() );
			RED_FATAL_ASSERT( ptrClass, "Parent class of the property not found!" );

			ptrReal = ptrClass->FindProperty( ptr->GetName() );
		}
		break;

	case IScriptDataObject::EMetaType::FunctionParam:
	case IScriptDataObject::EMetaType::FunctionLocal:
		{
			CScriptedDataFunction* ptrFunction = nullptr;
			if ( ptr->GetParent()->GetParent() != nullptr )
			{
				// class function
				const auto* ptrClass = m_owner->FindClass( ptr->GetParent()->GetParent()->GetName() );
				RED_FATAL_ASSERT( ptrClass, "Parent class of the function not found!" );
				ptrFunction = ptrClass->FindLocalFunction( ptr->GetParent()->GetName() );
			}
			else
			{
				// global function
				ptrFunction = m_owner->FindFunction( ptr->GetParent()->GetName() );
			}

			RED_FATAL_ASSERT( ptrFunction, "Function not found!" );

			ptrReal = ptrFunction->FindParam( ptr->GetName() );
			if ( !ptrReal )
			{
				ptrReal = ptrFunction->FindLocal( ptr->GetName() );
			}
		}
		break;
	}

	RED_FATAL_ASSERT( ptrReal, "Type not found!" );
	Write( ptrReal );
}

void Helper::CodeRuntimeToRuntime::ProcessPropertyRef()
{
	ProcessProperty< CScriptedDataProperty >();
}

void Helper::CodeRuntimeToRuntime::ProcessLocalPropertyRef()
{
	ProcessProperty< CScriptedDataFunctionLocal >();
}

void Helper::CodeRuntimeToRuntime::ProcessParamPropertyRef()
{
	ProcessProperty< CScriptedDataFunctionParam >();
}

void Helper::CodeRuntimeToRuntime::ProcessFunctionRef()
{
	const auto* ptr = Read< const CScriptedDataFunction* >();
	RED_FATAL_ASSERT( ptr, "Using unresolved type" );

	const CScriptedDataFunction* ptrReal = nullptr;
	if ( ptr->GetParent() )
	{
		const auto* ptrClass = m_owner->FindClass( ptr->GetParent()->GetName() );
		RED_FATAL_ASSERT( ptrClass, "Class not found!" );

		ptrReal = ptrClass->FindLocalFunction( ptr->GetName() );
	}
	else
	{
		ptrReal = m_owner->FindFunction( ptr->GetName() );
	}

	RED_FATAL_ASSERT( ptrReal, "Using unresolved function" );
	Write( ptrReal );
}

void Helper::CodeRuntimeToRuntime::ProcessNamedValueRef()
{
	const auto* ptr = Read< const CScriptedDataNamedValue* >();
	RED_FATAL_ASSERT( ptr, "Using unresolved named value" );
	RED_FATAL_ASSERT( ptr->GetParent(), "Named value has no parent" );

	const auto* ptrReal = m_owner->FindEnumValue( ptr->GetParent()->GetName(), ptr->GetName() );
	RED_FATAL_ASSERT( ptrReal, "Type not found!" );

	Write( ptrReal );
}
