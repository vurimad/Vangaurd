/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiType.h"
#include "rttiPointerTypes.h"
#include "textWriter.h"
#include "textReader.h"
#include "resource.h"
#include "rttiPathParser.h"
#include "serializableMap.h"
#include "resourceUtils.h"
#include "rttiValueHolder.h"
#include "../../redContainers/include/fundamentalStringConversion.h"
#include "rttiSingleValueHolder.h"
#include "rttiSystem.h"
#include "rttiAccessPath.h"

namespace rtti
{

	//////////////////////////////////////////////////////////////////////////
	IBasePointerType::~IBasePointerType()
	{}

	const Bool IBasePointerType::ReadValue( IRTTIContext& ctx, const void* data, const AccessPath& path, ValuePtr& outValue ) const
	{
		PathParser parser( path );
		if ( parser ) // still has path
			return ctx.ReportError( this, "Cannot cross pointer boundary using the RTTI wrappers" );

		// get the pointed value
		const auto ptr = GetPointer( data );

		// resource case
		if ( GetPointedType()->IsA< CResource >() )
		{
			auto* res = Cast< CResource >( ptr.GetSerializablePtr() );
			if ( res )
			{
				outValue = ValueHolder::CreateSingle( res->GetPath().ToString().AsChar() );
			}
			else
			{
				outValue = ValueHolder::CreateEmpty();
			}
			return true;
		}

		// serializable
		if ( GetPointedType()->IsA< ISerializable >() )
		{
			auto* object = ptr.GetSerializablePtr();
			if ( object )
			{
				outValue = ValueHolder::CreateHandle( ::ToStringDirect<Uint64>( object->GetID().Get() ).AsChar() );
			}
			else
			{
				outValue = ValueHolder::CreateEmpty();
			}
			return true;
		}

		// unknown pointer type
		return ctx.ReportError( this, "Unsupported pointer type" );
	}

	const Bool IBasePointerType::WriteValue( IRTTIContext& ctx, void* data, const AccessPath& path, const ValueHolder& newValue, bool clone ) const
	{
		PathParser parser( path );
		if ( parser ) // still has path
			return ctx.ReportError( this, "Cannot cross pointer boundary using the RTTI wrappers" );

		// null value
		if ( newValue.IsSingle() && newValue.GetSingleValue().EqualsNC( "null" ) )
		{
			SetPointer( data, rtti::Pointer() );
			return true;
		}

		// resource case
		if ( GetPointedType()->IsA< CResource >() )
		{
			// load the resource
			THandle< CResource > loadedResource;
			if ( newValue.IsSingle() )
			{
				const auto& path = newValue.GetSingle()->ToString();
				loadedResource = LoadResource_DEPRECATED< CResource >( path );
				if ( !loadedResource )
					return ctx.ReportError( this, "Failed to load resource '%hs'", path.AsChar() );

				if ( !loadedResource->IsA( GetPointedType() ) )
					return ctx.ReportError( this, "Resource loaded from '%hs' is not '%hs", path.AsChar(), GetPointedType()->GetName().AsChar() );
			}

			// set the pointer
			SetPointer( data, rtti::Pointer(loadedResource.Get()) );
			return true;
		}

		// serializable
		if ( GetPointedType()->IsA< ISerializable >() )
		{
			THandle< ISerializable > objectToSet;
			if ( newValue.IsHandle() )
			{
				const auto& data = newValue.GetHandle()->ToString();

				// ID of ISerializable object
				const Uint64 id = ::FromStringDirect< Uint64 >( String::CreateExternal( data.AsChar() ) );
				if ( !id )
					return ctx.ReportError( this, "Unable to parse valid object ID" );

				// Find ISerializable
				if ( !GSerializableMap->FindSerializable( SerializableID(id), objectToSet ) )
					return ctx.ReportError( this, "Object with given ID (%llu) not found", id );

				// Validate class
				if ( !objectToSet->IsA( GetPointedType() ) )
					return ctx.ReportError( this, "Object with ID (%llu) is '%hs' and not '%hs", id, 
						objectToSet->GetClass()->GetName().AsChar(), 
						GetPointedType()->GetName().AsChar() );
			}
			else if ( newValue.IsStructure() )
			{
				// class name ?
				const auto classNamePtr = newValue.GetStructElement( RED_NAME_CONSTEXPR("class") );
				const auto className = classNamePtr ? classNamePtr->GetSingleValue() : String();
				if ( !className.Empty() )
				{
					// New object, find class
					const auto* newClass = GetRttiSystem().FindClass( RED_NAME_NOREG( className ) );
					if ( !newClass )
						return ctx.ReportError( this, "Unknown object class '%hs", className.AsChar() );

					// Is class compatible ?
					if ( !newClass->IsA( GetPointedType() ) )
						return ctx.ReportError( this, "New class '%hs' is not compatible with required '%hs", 
							newClass->GetName().AsChar(), 
							GetPointedType()->GetName().AsChar() );

					// abstract class
					if ( newClass->IsAbstract() )
						return ctx.ReportError( this, "Class '%hs' is abstract", className.AsChar() );

					// Create new object
					objectToSet = newClass->CreateHandle< ISerializable >();
					if ( objectToSet == nullptr )
						return ctx.ReportError( this, "Failed to create object of class '%hs'", className.AsChar() );

					// continue applying values
					newClass->WriteValue( ctx, objectToSet.Get(), rtti::AccessPath(), newValue, clone );
				}
			}
			else
			{
				// set NULL
				objectToSet = nullptr;
			}

			if( clone && objectToSet )
			{
				const rtti::ClassType * classType = objectToSet->GetClass();
				rtti::ValuePtr outValue;
				if( classType->ReadValue( ctx, objectToSet.Get(), rtti::AccessPath(), outValue ) )
				{	
					SerializableHandle clone = classType->CreateHandle< ISerializable >();
					classType->WriteValue( ctx, clone.Get(), rtti::AccessPath(), *outValue, true );
					objectToSet = clone;
				}
			}
			
			
			SetPointer( data, rtti::Pointer( objectToSet.Get() ) );
			

			return true;
		}

		// unknown pointer type
		return ctx.ReportError( this, "Unsupported pointer type" );
	}

	Bool IBasePointerType::IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const
	{
		return GetPointedType()->IsPropertyReadOnly( ctx, path, outReadOnly );
	}

	const Bool IBasePointerType::SerializeToText( text::ITextWriter& writer, const void* data ) const
	{
		const rtti::Pointer pointer = GetPointer( data );

		// not a serializable, save NULL
		if ( !GetPointedType()->IsA< ISerializable >() )
		{
			writer.WriteValue( nullptr );
			return true;
		}

		// resource case - serialize as direct data
		if ( GetPointedType()->IsA< CResource >() )
			return rtti::IType::SerializeToText( writer, data );

		// save the pointer to object (internally, saves the ID)
		auto* ptr = pointer.GetSerializablePtr();
		writer.WriteValue( ptr );

		return true;
	}

	const Bool IBasePointerType::SerializeFromText( text::ITextReader& reader, void* data ) const
	{
		// not a serializable, save NULL
		if ( !GetPointedType()->IsA< ISerializable >() )
			return true; // unable to restore

		// resource case - serialize as direct data
		if ( GetPointedType()->IsA< CResource >() )
			return rtti::IType::SerializeFromText( reader, data );

		// load the pointer
		ISerializable* ptr = nullptr;
		if ( !reader.ReadValue( ptr ) )
			return false;

		// store the new pointer value
		SetPointer( data, rtti::Pointer(ptr) );
		return true;
	}

	//////////////////////////////////////////////////////////////////////////

} // rtti