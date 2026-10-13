/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_CORE_WEAK_HANDLE_H_
#define _RED_CORE_WEAK_HANDLE_H_

#include "../../redMemory/include/weakStorage.h"
#include "handle.h"

template< typename T >
class WeakHandle : public red::WeakStorage< T, HandleSharedStorage >
{
public:

	typedef red::WeakStorage< T, HandleSharedStorage > ParentType;

	WeakHandle();
	WeakHandle( std::nullptr_t );
	WeakHandle( const WeakHandle & pointer );
	WeakHandle( WeakHandle && pointer );
	~WeakHandle();

	template< typename U >
	WeakHandle( const WeakHandle< U > & pointer );

	template< typename U >
	WeakHandle( WeakHandle< U > && pointer );

	template< typename U  >
	WeakHandle( const THandle< U> & pointer );

	WeakHandle & operator=( const WeakHandle & pointer );
	WeakHandle & operator=( WeakHandle && pointer );

	template< typename U >
	WeakHandle & operator=( const WeakHandle< U > & pointer );

	template< typename U >
	WeakHandle & operator=( WeakHandle< U > && pointer );

	template< typename U >
	WeakHandle & operator=( const THandle< U > & pointer );

	THandle< T > ToHandle() const;
	
private:
	// use ToHandle() instead!
	THandle< T > Lock() const;
};

template< typename T >
void operator<<( IFile& file,  WeakHandle< T >& weakHandle );

template< typename T >
struct TTypeName< WeakHandle< T > >
{
	static const CName GetTypeName();
};

// WeakHandle cast
template< class _DestType, class _SrcType >
WeakHandle< _DestType > Cast( const WeakHandle< _SrcType >& srcObj );

// WeakHandle safe cast
template< class _DestType, class _SrcType >
WeakHandle< _DestType > SafeCast( const WeakHandle< _SrcType >& srcObj );

#include "weakHandle.inl"

#endif
