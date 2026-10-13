/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "staticArrayAccessor.h"

namespace red {

void* StaticArrayAccessor::Data() const
{
	return reinterpret_cast< void *>( const_cast< StaticArrayAccessor* >( this ) );
}

Uint32& StaticArrayAccessor::SizeRef( Uint32 typeSize, Uint32 maxSize ) const
{
	const Uint32 offset = typeSize * maxSize;
	const Uint32 realOffset = AlignUp( offset, ( Uint32 )__alignof( Uint32 ) );
	const Uint64 address = red::memory::AddressOf( this ) + realOffset;
	return *reinterpret_cast< Uint32* >( address );
}

Uint32 StaticArrayAccessor::GetSize( Uint32 typeSize, Uint32 maxSize ) const
{
	return SizeRef( typeSize, maxSize );
}

void* StaticArrayAccessor::GetElement( Uint32 typeSize, Uint32 index )
{
	return reinterpret_cast< Uint8* >( this ) + typeSize * index;
}

const void* StaticArrayAccessor::GetElement( Uint32 typeSize, Uint32 index ) const
{
	return reinterpret_cast< const Uint8* >( this ) + typeSize * index;
}

//////////////////////////////////////////////////////////////////////////

void StaticArrayAccessor::Clear( Uint32 typeSize, Uint32 maxSize )
{
	SizeRef( typeSize, maxSize ) = 0;
}

Uint32 StaticArrayAccessor::Grow( Uint32 typeSize, Uint32 maxSize, Uint32 count )
{
	Uint32& size = SizeRef( typeSize, maxSize );
	RED_FATAL_ASSERT( size + count <= maxSize, "StaticArray cannot grow over maximum capacity" );
	const Uint32 index = size;
	size += count;
	return index;
}

void StaticArrayAccessor::Remove( Uint32 typeSize, Uint32 maxSize, Uint32 count )
{
	Uint32& size = SizeRef( typeSize, maxSize );
	RED_FATAL_ASSERT( count <= size, "StaticArray cannot remove more than size elements" );
	size -= count;
}

//////////////////////////////////////////////////////////////////////////

StaticArrayAccessor& StaticArrayAccessor::GetRef( const void* ptr )
{
	return *static_cast< StaticArrayAccessor* >( const_cast< void* >( ptr ) );
}

//////////////////////////////////////////////////////////////////////////

Uint32 StaticArrayAccessor::CalcTypeSize( Uint32 typeSize, Uint32 maxSize, Uint32 typeAlignment )
{
	Uint32 memSize = sizeof( Uint32 );
	memSize += typeSize * maxSize;
	return AlignUp( memSize, CalcTypeAlignment( typeAlignment ) );
}

Uint32 StaticArrayAccessor::CalcTypeAlignment( Uint32 typeAlignment )
{
	return std::max( static_cast< Uint32 >( __alignof( Uint32 ) ), typeAlignment );
}

} // red