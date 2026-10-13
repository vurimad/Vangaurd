/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "rttiType.h"

namespace rtti
{

	// Base class for array types, just to get the inner type
	// The interface functions have different names that the actual implemnetation 
	// just to save few cycles in cases when we know the exact type (manual devirtualization)
	class RED_REFLECTION_API IBaseArrayType : public rtti::IType
	{
	public:
		//! Get type of array element
		virtual const rtti::IType* ArrayGetInnerType() const  = 0;

		//! Is this array type resizable ?
		virtual Bool ArrayIsResizable() const = 0;

		//! Get number of elements in the array
		virtual Uint32 ArrayGetArraySize( const void* arrayData ) const = 0;

		//! Get maximum number of elements in the array
		virtual Uint32 ArrayGetArrayCapacity( const void* arrayData ) const = 0;

		//! Get pointer to array inner element (writable)
		virtual void* ArrayGetArrayElement( void* arrayData, Uint32 index ) const = 0;

		//! Get pointer to array inner element (read-only)
		virtual const void* ArrayGetArrayElement( const void* arrayData, Uint32 index ) const = 0;

		//! Add element to array, returs array index of the added element
		virtual Int32 ArrayAddElement( void* arrayData, Uint32 count=1 ) const = 0;

		//! Remove element from array at given index
		virtual Bool ArrayDeleteElement( void* arrayData, Int32 itemIndex ) const = 0;

		//! Insert element into the array at given index
		virtual Bool ArrayInsertElement( void* arrayData, Int32 itemIndex ) const = 0;

		// Retrieve the value in the universal value holder for recursive property data
		// If an index expression ([x]) is encountered it follows the array
		// Out of bounds array access will return false
		virtual const Bool ReadValue( IRTTIContext& ctx, const void* data, const rtti::AccessPath& path, rtti::ValuePtr& outValue ) const override final;

		// Retrieve the value in the universal value holder for recursive property data
		// If an index expression ([x]) is encountered it follows the array
		// Out of bounds array access will return false unless the whole array is replaces
		virtual const Bool WriteValue( IRTTIContext& ctx, void* data, const rtti::AccessPath& path, const rtti::ValueHolder& newValue, bool clone ) const override final;

		virtual Bool IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const override;

		// Implementation of IType::ToString. Iterates over array elements and calls their respective ToString
		virtual Bool ToString(const void* data, String& valueString) const override;

		/// Read full array into a value holder
		const Bool ReadFullArray( IRTTIContext& ctx, const void* data, rtti::ValuePtr& outValue ) const;

		/// Write full array into a value holder
		/// Elements that don't fit array capacity (for static/native arrays) will be stripped
		const Bool WriteFullArray( IRTTIContext& ctx, void* data, const rtti::ValueHolder& newValue, bool clone ) const;

		/// Serialize value of this type using the generic text writer
		virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const;

		/// Deserialize value of this type using the generic text reader
		virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const;

		void RebuildParentHierarchy( void * arrayData, ISerializable * parent ) const override final;
	
		virtual Uint32 TryResizingArray( const void* arrayData, Uint32 size, const red::memory::Pool & pool ) const = 0;
	};

} // rtti