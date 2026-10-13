/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once
#include "rttiClass.h"

namespace rtti
{

	// abstract class - asserted implementations of all operations
	class RED_REFLECTION_API AbstractClassType : public ClassType
	{
	public:
		AbstractClassType( const CName name, Uint32 size, Uint32 flags = 0 );

	private:
		virtual void OnConstruct( void * ) const override final;
		virtual void OnDestruct( void * ) const override final;
		virtual Bool Compare( const void*, const void*, Uint32 ) const override final;
		virtual void Copy( void*, const void* ) const override final;
		virtual void * AllocateClassBuffer() const override final;

		virtual const red::memory::Pool& GetInnerTypeMemoryPool() const override = 0;
	};

	template< class Type >
	class TAbstractClassType : public AbstractClassType
	{
	public:
		TAbstractClassType( const CName name, Uint32 size, Uint32 flags = 0 );

	private:
		virtual const red::memory::Pool& GetInnerTypeMemoryPool() const override final;
		
	};

} // rtti

#include "rttiAbstractClass.inl"