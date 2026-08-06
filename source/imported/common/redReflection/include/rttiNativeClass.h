/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "rttiClass.h"

namespace rtti
{

	template< class _Type >
	class TNativeClass : public ClassType
	{
	public:
		TNativeClass( const CName name, Uint32 size, Uint32 flags = 0 );
	
	private:
		virtual void OnConstruct( void * buffer ) const override final;
		virtual void OnDestruct( void * buffer ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;
		virtual void * AllocateClassBuffer() const override final;
	};

	template< class _Type  >
	class TNativeClassNoCopy : public ClassType
	{
	public:
		TNativeClassNoCopy( const CName name, Uint32 size, Uint32 flags = 0 );
	
	private:
		virtual void OnConstruct( void * buffer ) const override final;
		virtual void OnDestruct( void * buffer ) const override final;
		virtual Bool Compare( const void* data1, const void* data2, Uint32 flags ) const override final;
		virtual void Copy( void* dest, const void* src ) const override final;
		virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final;
		virtual void * AllocateClassBuffer() const override final;
	};

} // rtti

#include "rttiNativeClass.inl"