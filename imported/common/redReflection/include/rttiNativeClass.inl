/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiClass.h"
#include "rttiProperty.h"

namespace rtti
{
	template< class _Type >
	TNativeClass<_Type>::TNativeClass( const CName name, Uint32 size, Uint32 flags /* = 0 */ )
		: ClassType( name, size, flags | CF_Native )
	{}

	template< class _Type >
	void TNativeClass<_Type>::OnConstruct( void * buffer ) const 
	{
		RED_FATAL_ASSERT( red::memory::IsAligned( buffer, alignof( _Type ) ), "Buffer is not properly aligned" );
		::new ( buffer ) _Type;
	}

	template< class _Type >
	void TNativeClass<_Type>::OnDestruct( void * buffer ) const 
	{
		((_Type*) buffer )->_Type::~_Type();
	}

	template< class _Type >
	Bool TNativeClass<_Type>::Compare( const void* data1, const void* data2, Uint32 flags ) const 
	{
		return ClassType::DeepCompare( data1, data2, flags );
	}

#if defined(RED_PLATFORM_DURANGO)
	// Tad: This has been added to workaround the PGO issue in Final for Xbox where we get a
	// template generation error for _Type <funcOperatorLogicNot_Bool_Bool>
	// src\common\redreflection\include\rttinativeclass.inl(40) : warning C4789:
	//		<rtti::Function::CallNative> buffer 'rtti::Function::CallNative' of size 1 bytes will be overrun; 16 bytes will be written starting at offset 0
	#pragma warning( push )
	#pragma warning( disable : 4789 )
#endif
	template< class _Type >
	void TNativeClass<_Type>::Copy( void* dest, const void* src ) const 
	{
		RED_FATAL_ASSERT( red::memory::IsAligned( src, alignof( _Type ) ), "Source buffer is not properly aligned" );
		RED_FATAL_ASSERT( red::memory::IsAligned( dest, alignof( _Type ) ), "Destination buffer is not properly aligned" );
		*((_Type*)dest) = *((const _Type*)src);
	}
#if defined(RED_PLATFORM_DURANGO)
	#pragma warning( pop )
#endif

	template< class _Type >
	const red::memory::Pool & TNativeClass<_Type>::GetInnerTypeMemoryPool() const
	{
		typedef typename red::memory::PoolResolver< _Type, red::PoolRTTI >::PoolType PoolType;
		return PoolType::GetInstance();
	}

	template< class _Type >
	void * TNativeClass<_Type>::AllocateClassBuffer() const
	{
		const Uint32 alignment = GetAlignment();
		Uint32 bufferSize = red::memory::RoundUp( GetSize(), alignment );
		void * buffer = RED_ALLOCATE_ALIGNED( GetInnerTypeMemoryPool(), bufferSize, alignment );
		return buffer;
	}

	template< class _Type  >
	TNativeClassNoCopy<_Type>::TNativeClassNoCopy( const CName name, Uint32 size, Uint32 flags /* = 0 */ )
		: ClassType( name, size, flags | CF_Native )
	{
	}

	template< class _Type  >
	void TNativeClassNoCopy<_Type>::OnConstruct( void * buffer ) const 
	{
		RED_FATAL_ASSERT( red::memory::IsAligned( buffer, alignof( _Type ) ), "Buffer is not properly aligned" );
		::new ( buffer ) _Type;
	}

	template< class _Type  >
	void TNativeClassNoCopy<_Type>::OnDestruct( void * buffer ) const 
	{
		((_Type*)buffer)->~_Type();
	}

	template< class _Type  >
	Bool TNativeClassNoCopy<_Type>::Compare( const void* data1, const void* data2, Uint32 flags ) const 
	{
		return ClassType::DeepCompare( data1, data2, flags );
	}

	template< class _Type  >
	void TNativeClassNoCopy<_Type>::Copy( void* dest, const void* src ) const 
	{
		RED_UNUSED( dest );
		RED_UNUSED( src );
		// empty 
	}

	template< class _Type >
	const red::memory::Pool & TNativeClassNoCopy<_Type>::GetInnerTypeMemoryPool() const
	{
		typedef typename red::memory::PoolResolver< _Type, red::PoolRTTI >::PoolType PoolType;
		return PoolType::GetInstance();
	}

	template< class _Type >
	void * TNativeClassNoCopy<_Type>::AllocateClassBuffer() const
	{
		const Uint32 alignment = GetAlignment();
		Uint32 bufferSize = red::memory::RoundUp( GetSize(), alignment );
		void * buffer = RED_ALLOCATE_ALIGNED( GetInnerTypeMemoryPool(), bufferSize, alignment );
		return buffer;
	}

} // rtti
