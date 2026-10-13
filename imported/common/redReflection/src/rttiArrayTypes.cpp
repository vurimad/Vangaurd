/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiType.h"
#include "rttiArrayTypes.h"
#include "rttiPathParser.h"
#include "textReader.h"
#include "textWriter.h"
#include "rttiValueHolder.h"
#include "rttiAccessPath.h"

namespace rtti
{

	const Bool IBaseArrayType::ReadFullArray( IRTTIContext& ctx, const void* data, ValuePtr& outValue ) const
	{
		// sanity check
		if ( !ArrayGetInnerType() )
			return ctx.ReportError( this, "Array does not define an inner type" );

		// create array for the values
			outValue = ValueHolder::CreateArray();

		// capture the values
		const Uint32 count = ArrayGetArraySize( data );
		for ( Uint32 i=0; i<count; ++i )
		{
			const void* elementData = ArrayGetArrayElement( data, i );
			if ( !elementData )
				return ctx.ReportError( this, "Unable to retrieve value pointer for element %d", i );

			// convert to value holder
				ValuePtr elementValue;
				if ( !ArrayGetInnerType()->ReadValue( ctx, elementData, AccessPath(), elementValue ) )
				return ctx.ReportError( this, "Unable to retrieve value for element %d", i );

			// store in array
			outValue->AddArrayElement( elementValue );
		}

		// extracted
		return true;
	}


	Bool IBaseArrayType::ToString(const void* data, String& valueString) const
	{
		if (!data) return false;

		const Uint32 count = ArrayGetArraySize(data);
		valueString = "Array[";
		for (Uint32 i = 0; i < count; ++i)
		{
			String elementString;
			const void* elementData = ArrayGetArrayElement(data, i);
			if (elementData != nullptr)	
			{
				bool success = ArrayGetInnerType()->ToString(elementData, elementString);
				if (!success)
				{
					elementString = String(ArrayGetInnerType()->GetName().AsChar()) + "(" + ArrayGetInnerType()->GetERTTITypeString() + ")";
				}
			}
			else
			{
				elementString = "NULL";
			}
			valueString += count == 0 ? ", " : " ";
			valueString += elementString;
		}
		valueString += "]";

		return true;
	}

	const Bool IBaseArrayType::WriteFullArray( IRTTIContext& ctx, void* data, const ValueHolder& newValue, bool clone ) const
	{
		// sanity check
		if ( !ArrayGetInnerType() )
			return ctx.ReportError( this, "Array does not define an inner type" );

		// empty array
		if ( newValue.IsEmpty() || newValue.GetNumElements() == 0 )
		{
			// we resize array that are not resizable
			if ( !ArrayIsResizable() )
				return ctx.ReportError( this, "Cannot clear non-resizable array" );

			// delete all elements
			const Uint32 count = ArrayGetArraySize( data );
			for ( Int32 i = (Int32)count-1; i>=0; --i )
				ArrayDeleteElement( data, i );

			// is array empied ?
			const Uint32 newCount = ArrayGetArraySize( data );
			if ( 0 != newCount )
				return ctx.ReportError( this, "Array still contains elements after clear operation" );

			return true;
		}

		// Resize array
		const Uint32 neededCount = newValue.GetNumElements();
		const Uint32 curCount = ArrayGetArraySize( data );
		if ( curCount != neededCount )
		{
			if ( !ArrayIsResizable() )
				return ctx.ReportError( this, "Cannot change size of non-resizable array" );

			// make sure we are not asking for to much
			const Uint32 capacity = ArrayGetArrayCapacity( data );
			if ( neededCount > capacity )
				return ctx.ReportError( this, "Array cannot hold %d elements (max size: %d)", neededCount, capacity );

			// resize array in a very very lame way
			if ( neededCount > curCount )
			{
				const Uint32 toAdd = neededCount - curCount;
				const Int32 startIndex = ArrayAddElement( data, toAdd );
				if ( startIndex == -1 )
					return ctx.ReportError( this, "Unable to resize array %d -> %d", curCount, neededCount );
			}
			while ( neededCount < ArrayGetArraySize(data) )
			{
				ArrayDeleteElement( data, ArrayGetArraySize(data) - 1 ); // delete last element
			}
		}

		// array has proper size by now
		const Uint32 newCount = ArrayGetArraySize(data);
		if ( newCount != neededCount )
			return ctx.ReportError( this, "Array still contains invalid number of elements after resize (%d -> %d) current: %d", curCount, neededCount, newCount );

		// set elements
		for ( Uint32 i=0; i<neededCount; ++i )
		{
			if ( const ValuePtr valueToSet = newValue.GetArrayElement(i) )
			{
				void* elementData = ArrayGetArrayElement( data, i );
				if ( !elementData )
				{
					return ctx.ReportError( this, "Unable to retrieve value pointer for element %d", i );
				}

				if ( ArrayGetInnerType()->GetType() == RT_Class && valueToSet->IsEmpty() )
				{
					continue;
				}

				if ( !ArrayGetInnerType()->WriteValue( ctx, elementData, AccessPath(), *valueToSet, clone ) )
				{
					return ctx.ReportError( this, "Unable to set value for element %d", i );
				}
			}
		}

		return true;
	}

	const Bool IBaseArrayType::ReadValue( IRTTIContext& ctx, const void* data, const AccessPath& path, ValuePtr& outValue ) const
	{
		if ( !data )
			return ctx.ReportError( this, "Trying to access NULL array" );

		// if there's no path left we are trying to read the full array value
		PathParser parser( path );
		if ( !parser )
			return ReadFullArray( ctx, data, outValue );

		// sanity check
		if ( !ArrayGetInnerType() )
			return ctx.ReportError( this, "Array does not define an inner type" );

		// follow the index
		Int32 index = 0;
		if ( !parser.EatIndex( index ) )
			return ctx.ReportError( this, "Expected array index" );

		// check bounds
		const Uint32 count = ArrayGetArraySize( data );
		if ( index >= (Int32)count )
			return ctx.ReportError( this, "Out of bounds array access (%d >= %d)", index, count );

		// get data for the element
		const void* elementData = ArrayGetArrayElement( data, index );
		if ( !elementData )
			return ctx.ReportError( this, "Unable to retrieve value pointer for element %d", index );

		// continue
		return ArrayGetInnerType()->ReadValue( ctx, elementData, parser.GetUneatenPath(), outValue );
	}


	const Bool IBaseArrayType::WriteValue( IRTTIContext& ctx, void* data, const AccessPath& path, const ValueHolder& newValue, bool clone ) const
	{
		if ( !data )
			return ctx.ReportError( this, "Trying to access NULL array" );

		// if there's no path left we are trying to read the full array value
		PathParser parser( path );
		if ( !parser )
			return WriteFullArray( ctx, data, newValue, clone );

		// sanity check
		if ( !ArrayGetInnerType() )
			return ctx.ReportError( this, "Array does not define an inner type" );

		// follow the index
		Int32 index = 0;
		if ( !parser.EatIndex( index ) )
			return ctx.ReportError( this, "Expected array index" );

		// check bounds
		const Uint32 count = ArrayGetArraySize( data );
		if ( index >= (Int32)count )
			return ctx.ReportError( this, "Out of bounds array access (%d >= %d)", index, count );

		// get data for the element
		void* elementData = ArrayGetArrayElement( data, index );
		if ( !elementData )
			return ctx.ReportError( this, "Unable to retrieve value pointer for element %d", index );

		// continue
		return ArrayGetInnerType()->WriteValue( ctx, elementData, parser.GetUneatenPath(), newValue, clone );
	}

	Bool IBaseArrayType::IsPropertyReadOnly( IRTTIContext& ctx, const rtti::AccessPath& path, Bool& outReadOnly ) const
	{
		// sanity check
		if ( !ArrayGetInnerType() )
			return ctx.ReportError( this, "Array does not define an inner type" );

		// follow the index
		PathParser parser( path );
		Int32 index = 0;
		if ( !parser.EatIndex( index ) )
			return ctx.ReportError( this, "Expected array index" );

		return ArrayGetInnerType()->IsPropertyReadOnly( ctx, parser.GetUneatenPath(), outReadOnly );
	}

	const Bool IBaseArrayType::SerializeToText( text::ITextWriter& writer, const void* data ) const
	{
		writer.BeginArray();

		const auto count = ArrayGetArraySize( data );
		for ( Uint32 i=0; i<count; ++i )
		{
			writer.BeginArrayElement();

			const void* itemData = ArrayGetArrayElement( data, i );
			if ( !ArrayGetInnerType()->SerializeToText( writer, itemData ) )
				return false;

			writer.EndArrayElement();
		}

		writer.EndArray();
		return true;
	}

	const Bool IBaseArrayType::SerializeFromText( text::ITextReader& reader, void* data ) const
	{
		// reset array
		Destruct(data);
		Construct(data);

		// empty array may be not saved
		if (reader.BeginArray())
		{
			for (;;)
			{
				if (!reader.BeginArrayElement())
					break;

				Int32 elementIndex = ArrayAddElement(data);
				if (elementIndex == -1)
					return false;

				void* itemData = ArrayGetArrayElement(data, elementIndex);
				if (itemData == nullptr)
					return false;

				if (!ArrayGetInnerType()->SerializeFromText(reader, itemData))
					return false;

				reader.EndArrayElement();
			}

			reader.EndArray();
		}

		return true;
	}

	void IBaseArrayType::RebuildParentHierarchy( void * arrayData, ISerializable * parent ) const
	{
		const rtti::IType * innerType = ArrayGetInnerType();

		for( Uint32 index = 0, end = ArrayGetArraySize( arrayData ); index != end; ++index )
		{
			void * element = ArrayGetArrayElement( arrayData, index );
			innerType->RebuildParentHierarchy( element, parent );
		}
	}

} // rtti
