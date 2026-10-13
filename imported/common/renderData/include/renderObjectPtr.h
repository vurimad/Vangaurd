/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _ENGINE_RENDER_OBJECT_PTR_H_
#define _ENGINE_RENDER_OBJECT_PTR_H_

#include "../../redMemory/include/sharedStorage.h"

#ifndef RED_CONFIGURATION_FINAL
#define RENDER_PTR_ENABLE_RACE_DETECTION
#endif

class RenderObjectOwnership
{
protected:

	RenderObjectOwnership();
	RenderObjectOwnership( void * pointer );

	RenderObjectOwnership( const RenderObjectOwnership & copyFrom );
	RenderObjectOwnership( RenderObjectOwnership && rvalue );

	void* Get() const;

	template< typename T >
	void Destroy();

	template< typename T >
	bool Release();

	template< typename T >
	void AddRef();

	void Swap( RenderObjectOwnership & swapWith );

private:

	void AssignRValue( RenderObjectOwnership && rvalue );

	void * m_pointee;

#ifdef RENDER_PTR_ENABLE_RACE_DETECTION
	mutable red::UpdateFlag m_updateFlag{ "Data race detected in RenderObjectOwnership" };
#endif
};

template< typename T >
class TRenderPtr : public red::SharedStorage< T, RenderObjectOwnership >
{
public:

	typedef red::SharedStorage< T, ::RenderObjectOwnership > ParentType;
	typedef typename ParentType::PtrType PtrType;

	TRenderPtr();
	TRenderPtr( std::nullptr_t );
	TRenderPtr( const TRenderPtr & copyFrom );
	TRenderPtr( TRenderPtr && rvalue );

	template< typename U >
	explicit TRenderPtr( U * pointer );	

	template< typename U >
	TRenderPtr( const TRenderPtr< U > & copyFrom );

	template< typename U >
	TRenderPtr( TRenderPtr< U > && rvalue );

	TRenderPtr & operator=( const TRenderPtr & copyFrom );
	TRenderPtr & operator=( TRenderPtr && rvalue );

	template< typename U >
	TRenderPtr & operator=( const TRenderPtr< U >  & copyFrom );

	template< typename U >
	TRenderPtr & operator=( TRenderPtr< U > && rvalue );

	// Created TRenderPtr and does ADD_REF to the objects
	template< typename U >
	static TRenderPtr	MakePtr( U * pointer );

};

#include "renderObjectPtr.inl"

#endif
