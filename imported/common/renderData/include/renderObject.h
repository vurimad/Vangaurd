/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderObjectPtr.h"
#include "renderingMemory.h"

/// Render object safe access macros
#define SAFE_COPY( x, y ) { if ( (x) != (y) ) { if ( x ) { (x)->Release(); }; x = y; if ( x ) { (x)->AddRef(); } } }

/// Render object, ref counted shit
class RENDER_DATA_API IRenderObject : red::NonCopyable
{
	RED_USE_POLYMORPHIC_MEMORY_POOL( rend::PoolRenderData );

	friend class RenderObjectOwnership;

protected:
	red::Atomic< Int32 >		m_refCount;		//!< Reference count

	//! Returns true if refcount is zero.
	//  KEEP THIS FUNCTION OUT OF PUBLIC INTERFACE!!!
	Bool IsRefCountZero() const;

	//! Destroys self if reference count is zero.
	//  KEEP THIS FUNCTION OUT OF PUBLIC INTERFACE!!!
	virtual void DestroySelfIfRefCountZero();

	//! Add internal reference
	void AddRef();

	//! Release internal reference
	void Release();

public:
	IRenderObject();
	virtual ~IRenderObject();

	RED_FORCE_INLINE Int32 GetRefCount() const { return m_refCount.GetValue(); }
};

typedef TRenderPtr< IRenderObject > RenderObjectHandle;

