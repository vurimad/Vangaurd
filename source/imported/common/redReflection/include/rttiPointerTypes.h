/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiPointer.h"
#include "rttiType.h"

namespace rtti
{

	// a special abstract wrapper for pointer types
	class RED_REFLECTION_API IBasePointerType : public rtti::IType
	{
	public:

		virtual ~IBasePointerType();

		//! Get type of the pointed data (right now always a const rtti::ClassType, but not always a ISerializable class)
		virtual const rtti::ClassType* GetPointedType() const = 0;

		//! Get a universal pointer to the pointed data
		virtual rtti::Pointer GetPointer( const void* data ) const = 0;

		//! Set new pointer to data
		virtual void SetPointer( void* data, const rtti::Pointer & ptr ) const = 0;
		virtual void ClonePointer( void* data, const rtti::Pointer & ptr  ) const = 0;

		/// Serialize value of this type using the generic text writer
		/// Saves pointers as objectIDs
		virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const;

		/// Deserialize value of this type using the generic text reader
		/// Restores pointers from objectIDs
		virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const;

		// Retrieve the value in the universal value holder for recursive property data
		// NOTE: we will never cross to the pointed object
		virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override final;

		// Retrieve the value in the universal value holder for recursive property data
		// NOTE: we will never cross to the pointed object
		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override final;

		virtual Bool IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const override final;
	};

} // rtti