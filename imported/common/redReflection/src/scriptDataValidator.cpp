/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "scriptDataObject.h"
#include "scriptDataEnvironment.h"
#include "scriptDataValidator.h"
#include "rttiSystem.h"
#include "rttiEnum.h"
#include "rttiBitField.h"
#include "rttiFunction.h"
#include "rttiClass.h"
#include "scriptable.h"
#include "rttiPointerTypes.h"
#include "rttiArrayTypes.h"

using red::DynArray;

namespace
{
	static RED_INLINE Bool CheckTypeName( CName scriptName, CName rttiName )
	{
		return scriptName == rttiName || GetRttiSystem().ScriptAliasToNativeName( scriptName ) == rttiName;
	}

	static Bool ValidateInternalTypes( const rtti::IType* basePropType, const CScriptedDataTypeRef* propType )
	{
		RED_ASSERT( basePropType != nullptr );
		RED_ASSERT( propType != nullptr );

		switch ( basePropType->GetType() )
		{
		case RT_Handle:
			{
				if ( propType->GetType() != CScriptedDataTypeRef::StrongHandle )
				{
					return false;
				}

				const rtti::IBasePointerType* basePropPointerType = static_cast< const rtti::IBasePointerType* >( basePropType );
				const CScriptedDataTypeRef* propInternalType = static_cast<const CScriptedDataTypeRef*>(propType->GetInternal());
				if (!propInternalType || !propInternalType->GetInternal())
				{
					return false;
				}

				return ValidateInternalTypes(basePropPointerType->GetPointedType(), propInternalType);
			}
			break;

		case RT_WeakHandle:
			{
				if ( propType->GetType() != CScriptedDataTypeRef::WeakHandle )
				{
					return false;
				}

				const rtti::IBasePointerType* basePropPointerType = static_cast< const rtti::IBasePointerType* >( basePropType );
				const CScriptedDataTypeRef* propInternalType = static_cast< const CScriptedDataTypeRef* >( propType->GetInternal() );
				if ( !propInternalType || !propInternalType->GetInternal() )
				{
					return false;
				}

				return ValidateInternalTypes( basePropPointerType->GetPointedType(), propInternalType );
			}
			break;

		case RT_Array:
			{
				if ( propType->GetType() != CScriptedDataTypeRef::DynArray )
				{
					return false;
				}

				const rtti::IBaseArrayType* arrayType = static_cast<const rtti::IBaseArrayType*>(basePropType);
				const rtti::IType* innterType = arrayType->ArrayGetInnerType();
				const CScriptedDataTypeRef* propInternalType = static_cast< const CScriptedDataTypeRef* >(propType->GetInternal());
				if (!propInternalType || !propInternalType->GetInternal())
				{
					return false;
				}

				return ValidateInternalTypes(innterType, propInternalType);
			}
			break;

		case RT_StaticArray:
			{
				if ( propType->GetType() != CScriptedDataTypeRef::StaticArray )
				{
					return false;
				}

				const rtti::IBaseArrayType* arrayType = static_cast<const rtti::IBaseArrayType*>( basePropType );
				const rtti::IType* innterType = arrayType->ArrayGetInnerType();
				const CScriptedDataTypeRef* propInternalType = static_cast< const CScriptedDataTypeRef* >( propType->GetInternal() );
				if ( !propInternalType || !propInternalType->GetInternal() )
				{
					return false;
				}

				return ValidateInternalTypes( innterType, propInternalType );
			}
			break;

		case RT_ScriptReference:
			{
				if ( propType->GetType() != CScriptedDataTypeRef::Reference )
				{
					return false;
				}

				const rtti::IBasePointerType* basePropPointerType = static_cast< const rtti::IBasePointerType* >( basePropType );
				const CScriptedDataTypeRef* propInternalType = static_cast< const CScriptedDataTypeRef* >( propType->GetInternal() );
				if ( !propInternalType || !propInternalType->GetInternal() )
				{
					return false;
				}

				return ValidateInternalTypes( basePropPointerType->GetPointedType(), propInternalType );
			}
			break;

		default:
			return CheckTypeName( propType->GetName(), basePropType->GetName() );
		}
	}
}

CScriptDataValidator::CScriptDataValidator()
{
}

Bool CScriptDataValidator::Validate( const class CScriptedDataEnvironment& env, class IScriptDataErrorReporter& err ) const
{
	/// get all object from the environment
	DynArray< const IScriptDataObject* > objects{ red::PoolScript() };
	env.GetAllObjects( objects, false );

	/// verify imported objects
	Uint32 numFailedTypes = 0;
	for ( const auto* obj : objects )
	{
		const bool isImported = obj->IsImported();

		// validate types
		const auto metaType = obj->GetMetaType();
		if ( metaType == IScriptDataObject::EMetaType::TypeRef )
		{
			if ( !ValidateTypeRef( static_cast< const CScriptedDataTypeRef& >( *obj ), err ) )
				numFailedTypes += 1;
		}
		else if ( isImported && metaType == IScriptDataObject::EMetaType::Enum )
		{
			if ( !ValidateEnum( static_cast< const CScriptedDataEnum& >( *obj ), err ) )
				numFailedTypes += 1;
		}
		else if ( isImported && metaType == IScriptDataObject::EMetaType::Bitfield )
		{
			if ( !ValidateBitfield( static_cast< const CScriptedDataBitfield& >( *obj ), err ) )
				numFailedTypes += 1;
		}
		else if ( isImported && metaType == IScriptDataObject::EMetaType::Class )
		{
			if ( !ValidateClass( static_cast< const CScriptedDataClass& >( *obj ), err ) )
				numFailedTypes += 1;
		}
		else if ( isImported && metaType == IScriptDataObject::EMetaType::Function )
		{
			if ( !ValidateFunction( static_cast< const CScriptedDataFunction& >( *obj ), err ) )
				numFailedTypes += 1;
		}
		else if ( isImported && metaType == IScriptDataObject::EMetaType::NamedValue )
		{
			/*if ( !ValidateNamedValue( static_cast< const CScriptedDataNamedValue& >( *obj ), err ) )
				numFailedTypes += 1;*/
		}
	}

	// final validation message
	if ( numFailedTypes > 0 )
	{
		err.ValidationError( numFailedTypes == 1 ? "Validation failed for %u type" : "Validation failed for %u types", numFailedTypes );
		return false;
	}

	// no validation errors
	return true;
}

Bool CScriptDataValidator::ValidateTypeRef( const class CScriptedDataTypeRef& obj, class IScriptDataErrorReporter& err ) const
{
	// named external type
	if ( obj.GetType() == CScriptedDataTypeRef::Named )
	{
		const rtti::IType* namedType = GetRttiSystem().FindType( obj.GetName() );
		if ( !namedType )
		{
			err.ValidationError( "Missing native type '%hs'", obj.GetName().AsChar() );
			return false;
		}
	}

	// valid type
	return true;
}

Bool CScriptDataValidator::ValidateEnum( const class CScriptedDataEnum& obj, class IScriptDataErrorReporter& err ) const
{
	// check object existence
	const rtti::EnumType* baseObj = GetRttiSystem().FindScriptEnum( obj.GetName() );
	if ( !baseObj )
	{
		err.ValidationError( "Missing native enum '%hs'", obj.GetName().AsChar() );
		return false;
	}

	// make sure it's not already scripted
	if ( baseObj->IsScripted() )
	{
		err.ValidationError( "Enum '%hs' is already scripted", obj.GetName().AsChar() );
		return false;
	}

	// check if sizes match
	if ( obj.GetSize() != baseObj->GetSize() )
	{
		err.ValidationError( "Imported enum '%hs' size (%d) is different than native enum size (%d)", obj.GetName().AsChar(), obj.GetSize(), baseObj->GetSize() );
		return false;
	}

	// check imported options
	for ( const auto* option : obj.GetValues() )
	{
		if ( option->IsImported() )
		{
			Int64 val = -1;
			if ( !baseObj->FindValue( option->GetName(), val ) )
			{
				err.ValidationError( "Missing native value '%hs' in enum '%hs'", 
					option->GetName().AsChar(), obj.GetName().AsChar() );
				return false;
			}
		}
	}

	// validated
	return true;
}

Bool CScriptDataValidator::ValidateBitfield( const class CScriptedDataBitfield& obj, class IScriptDataErrorReporter& err ) const
{
	// check object existence
	const rtti::BitFieldType* baseObj = GetRttiSystem().FindBitField( obj.GetName() );
	if ( !baseObj )
	{
		err.ValidationError( "Missing native bitfield '%hs'", obj.GetName().AsChar() );
		return false;
	}

	// make sure it's not already scripted
	if ( baseObj->IsScripted() )
	{
		err.ValidationError( "Bitfield '%hs' is already scripted", obj.GetName().AsChar() );
		return false;
	}

	// check imported options
	for ( const auto* option : obj.GetValues() )
	{
		if ( option->IsImported() )
		{
			Int32 val = -1;
			if ( baseObj->GetBitValue( option->GetName() ) == -1 )
			{
				err.ValidationError( "Missing native value '%hs' in bitfield '%hs'", 
					option->GetName().AsChar(), obj.GetName().AsChar() );
				return false;
			}
		}
	}

	// validated
	return true;
}

Bool CScriptDataValidator::ValidateClass( const class CScriptedDataClass& obj, class IScriptDataErrorReporter& err ) const
{
	// check class existence
	const rtti::ClassType* baseObj = GetRttiSystem().FindScriptClass( obj.GetName() );
	if ( !baseObj )
	{
		err.ValidationError( "Missing native class '%hs'", obj.GetName().AsChar() );
		return false;
	}

	// make sure it's exported
	Bool finalStatus = true;
	if ( !baseObj->IsNative() )
	{
		err.ValidationError( "Class '%hs' is not native", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// "importonly" class should be declared such in the scripts
	if ( baseObj->IsImportOnly() && !obj.IsImportOnly() )
	{
		err.ValidationError( "Class '%hs' has to be declared in scripts as 'importonly'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// make sure classes are imported as classes and structures as structures
	const Bool isIScriptable = baseObj->IsA< IScriptable >();
	if ( !isIScriptable && !obj.IsStructure() )
	{
		err.ValidationError( "Struct '%hs' has to be declared in scripts as 'struct'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	if ( isIScriptable && obj.IsStructure() )
	{
		err.ValidationError( "Class '%hs' has to be declared in scripts as 'class'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// "testonly" class should be declared such in the scripts
	if ( baseObj->IsTestOnly() && !obj.IsTestOnly() )
	{
		err.ValidationError( "Class '%hs' has to be declared in scripts as 'testonly'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// visibility check
	if ( baseObj->IsPrivate() && !obj.IsPrivate() )
	{
		err.ValidationError( "Class '%hs' has to be declared in scripts as 'private'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	if ( baseObj->IsProtected() && !obj.IsProtected() )
	{
		err.ValidationError( "Class '%hs' has to be declared in scripts as 'protected'", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// make sure that we know it's abstract (we can tell that non abstract class IS abstract but not the other way around)
	if ( baseObj->IsAbstract() && !obj.IsAbstract() )
	{
		err.ValidationError( "Native class '%hs' was not marked as abstract", obj.GetName().AsChar() );
		finalStatus = false;
	}

	// check the base class
	if ( obj.GetBaseClass() != nullptr )
	{	
		// base class should be imported
		if ( !obj.GetBaseClass()->IsImported() )
		{
			err.ValidationError( "Native class '%hs' has declared base class '%hs' that is not imported", 
				obj.GetName().AsChar(), obj.GetBaseClass()->GetName().AsChar() );
			finalStatus = false;
		}

		// the base class should be also imported
		const rtti::ClassType* baseBaseObj = GetRttiSystem().FindScriptClass( obj.GetBaseClass()->GetName() );
		if ( !baseBaseObj )
		{
			err.ValidationError( "Missing base class '%hs' of native class '%hs'", 
				obj.GetBaseClass()->GetName().AsChar(), obj.GetName().AsChar() );
			finalStatus = false;
		}

		// compare
		if ( baseBaseObj != baseObj->GetBaseClass() )
		{
			err.ValidationError( "Native class '%hs' has declared base class '%hs' that is different than current one '%hs'", 
				obj.GetName().AsChar(), obj.GetBaseClass()->GetName().AsChar(), 
				baseObj->GetBaseClass() ? baseObj->GetBaseClass()->GetName().AsChar() : "<none>" );
			finalStatus = false;
		}
	}
	else 
	{
		if ( baseObj->HasBaseClass() )
		{
			if ( !baseObj->IsA< IScriptable >() )
			{
				err.ValidationError( "Native class '%hs' has base class '%hs' that was not declared", 
					obj.GetName().AsChar(), baseObj->GetBaseClass()->GetName().AsChar() );
				finalStatus = false;
			}
		}
	}

	// make sure that we have ALL of the imported properties
	for ( const auto* prop : obj.GetProperties() )
	{
		if ( prop->IsImported() )
		{
			const rtti::Property* baseProp = baseObj->FindProperty( prop->GetName() );
			if ( baseProp == nullptr )
			{
				err.ValidationError( "Missing native property '%hs' in native class '%hs'",
					prop->GetName().AsChar(), obj.GetName().AsChar() );
				finalStatus = false;
				continue;
			}
			
			if ( !ValidatePropertyType( prop, baseProp ) )
			{
				err.ValidationError( "Imported property '%hs.%hs' type '%hs' does not match with the native one '%hs'.",
					obj.GetName().AsChar(), prop->GetName().AsChar(), prop->GetType()->GetName().AsChar(), baseProp->GetType()->GetName().AsChar() );
				finalStatus = false;
			}

			// TODO: check flags
		}
	}

	// check functions
	for ( const auto* func : obj.GetFunctions() )
	{
		if ( func->IsImported() )
		{
			const rtti::Function* baseFunc = baseObj->FindFunctionNonCached( func->GetName() );
			if ( baseFunc == nullptr )
			{
				err.ValidationError( "Missing native function '%hs' in native class '%hs'", func->GetName().AsChar(), obj.GetName().AsChar() );
				finalStatus = false;
			}

			// TODO: check type
			else if ( ( func->IsConst() && !baseFunc->IsConst() ) )
			{
				err.ValidationError( "Class '%hs' contains imported function '%hs' declared as const, but native function is defined as not const", obj.GetName().AsChar(), func->GetName().AsChar() );
				finalStatus = false;
			}
			else if ( !func->IsConst() && baseFunc->IsConst() )
			{
				err.ValidationError( "Class '%hs' contains imported function '%hs' declared as non-const, but native function is defined as const", obj.GetName().AsChar(), func->GetName().AsChar() );
				finalStatus = false;
			}
		}
	}

	// return final status
	return finalStatus;
}

Bool CScriptDataValidator::ValidateFunction( const class CScriptedDataFunction& obj, class IScriptDataErrorReporter& err ) const
{
	const rtti::Function* baseFunc = GetRttiSystem().FindGlobalFunction( obj.GetName() );
	if ( !baseFunc )
	{
		err.ValidationError( "Missing native global function '%hs'", obj.GetName().AsChar() );
		return false;
	}

	// TODO: check type

	return true;
}

Bool CScriptDataValidator::ValidatePropertyType( const CScriptedDataProperty* prop, const rtti::Property* baseProp ) const
{
	const CScriptedDataTypeRef* propType = prop->GetType();
	const rtti::IType* basePropType = baseProp->GetType();
	switch ( propType->GetType() )
	{
	case CScriptedDataTypeRef::EType::Named:
	{
		return CheckTypeName( propType->GetName(), basePropType->GetName() );
	}
	case CScriptedDataTypeRef::EType::Internal:
	{
		return CheckTypeName( propType->GetInternal()->GetName(), basePropType->GetName() );
	}
	case CScriptedDataTypeRef::EType::StrongHandle:
	{
		if ( basePropType->GetType() == RT_Handle )
		{
			const rtti::IBasePointerType* pointerType = static_cast< const rtti::IBasePointerType* >( basePropType );
			return CheckTypeName( propType->GetInternal()->GetName(), pointerType->GetPointedType()->GetName() );
		}
		return false;
	}
	case CScriptedDataTypeRef::EType::WeakHandle:
	{
		if ( basePropType->GetType() == RT_WeakHandle )
		{
			const rtti::IBasePointerType* pointerType = static_cast< const rtti::IBasePointerType* >( basePropType );
			return CheckTypeName( propType->GetInternal()->GetName(), pointerType->GetPointedType()->GetName() );
		}
		return false;
	}
	case CScriptedDataTypeRef::EType::DynArray:
	{
		if ( basePropType->GetType() == RT_Array )
		{
			const rtti::IBaseArrayType* arrayType = static_cast< const rtti::IBaseArrayType* >( basePropType );
			const CScriptedDataTypeRef* propInternalType = static_cast< const CScriptedDataTypeRef* >( propType->GetInternal() );
			return ValidateInternalTypes( arrayType->ArrayGetInnerType(), propInternalType );
		}
		return false;
	}
	case CScriptedDataTypeRef::EType::StaticArray:
	{
		if ( basePropType->GetType() == RT_StaticArray || basePropType->GetType() == RT_NativeArray )
		{
			const rtti::IBaseArrayType* arrayType = static_cast< const rtti::IBaseArrayType* >( basePropType );
			const CScriptedDataTypeRef* propInternalType = static_cast<const CScriptedDataTypeRef*>( propType->GetInternal() );
			return ValidateInternalTypes( arrayType->ArrayGetInnerType(), propInternalType );
		}
		return false;
	}
	default:
		return false;
	}
}