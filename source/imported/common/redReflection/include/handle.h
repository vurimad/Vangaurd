/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_CORE_HANDLE_H_
#define _RED_CORE_HANDLE_H_

#include "../../redMemory/include/sharedStorage.h"
#include "../../redCore/include/names.h"

class ISerializable;
class HandleSharedStorage;
class IFile;

template< typename T >
class WeakHandle;

template< typename T >
class THandle : public red::SharedStorage< T, HandleSharedStorage >
{
public:

	typedef red::SharedStorage< T, HandleSharedStorage >ParentType;

	THandle();
	THandle( std::nullptr_t );
	THandle( const THandle & copyFrom );
	THandle( THandle && rvalue );
	~THandle();


	template< typename U >
	explicit THandle( const U * pointer );

	template< typename U >
	THandle( const THandle< U > & copyFrom );

	template< typename U >
	THandle( THandle< U > && rvalue );

	template< typename U >
	THandle( const WeakHandle< U > & copyFrom );

	template< typename U >
	THandle( red::UniquePtr< U > && rvalue );

	THandle & operator=( const THandle & copyFrom );
	THandle & operator=( THandle && rvalue );

	template< typename U >
	THandle & operator=( const THandle< U >  & copyFrom );

	template< typename U >
	THandle & operator=( THandle< U >  && rvalue );

	template< typename U >
	THandle & operator=( red::UniquePtr< U > && rvalue );
};

class RED_REFLECTION_API HandleSharedStorage
{
public:

	Uint32 CalcHash() const;

protected:

	HandleSharedStorage();
	explicit HandleSharedStorage( ISerializable * pointer );

	HandleSharedStorage( const HandleSharedStorage & copyFrom );
	HandleSharedStorage( HandleSharedStorage && rvalue );

	template< typename T >
	void Destroy();

	template< typename T >
	void AddRef();

	template< typename T >
	bool Release();

	void ReleaseWeak();		
	void AddRefWeak();

	// Returning void * instead of ISerializable * to allow THandle::Get() to compile without knowing actual type.
	void * Get() const;  

	RED_FORCE_INLINE Int32 GetRefCount() const
	{
		return m_refCount ? atomic::FetchValue32( &m_refCount->strong ) : 0;
	}

	RED_FORCE_INLINE Int32 GetWeakRefCount() const
	{
		return m_refCount ? atomic::FetchValue32( &m_refCount->weak ) : 0;
	}

	void Swap( HandleSharedStorage & other );

	void UpgradeFromWeakToStrong( HandleSharedStorage & upgradeFrom );

	const void* InternalGetRefCountStorage() const;

private:

	struct RefCount
	{
		RED_USE_MEMORY_POOL( red::PoolRefCount );

		typedef atomic::TAtomic32 Type;
		RED_INLINE RefCount() : strong( 1 ), weak( 1 ){}
		Type strong;
		Type weak;
	};

	void InternalAddRef();
	bool InternalRelease();
	bool InternalDestroyRequest();
	void InternalDestroy();

	void AssignRValue( HandleSharedStorage && rvalue );

	ISerializable * m_pointee;
	RefCount * m_refCount;
};

template< typename T >
void operator<<( IFile& file, THandle< T >& handle );

template< typename T, typename U >
bool operator==( const THandle< T > & handle, U * pointer );

template< typename T, typename U >
bool operator==( T * pointer, const THandle< U > & handle );

template< typename T, typename U >
bool operator==( const THandle< T > & left, const THandle< U > & right );


// Handle cast
template< class _DestType, class _SrcType >
THandle< _DestType > Cast( const THandle< _SrcType >& srcObj );

// Handle safe cast
template< class _DestType, class _SrcType >
THandle< _DestType > SafeCast( const THandle< _SrcType >& srcObj );

template< typename T, typename... Args >
THandle< T > CreateHandle( Args && ... args );

namespace rtti
{
	RED_REFLECTION_API const CName FormatHandleTypeName( const CName pointedTypeName );
}

template< typename T > struct TTypeName;

template< typename T >
struct TTypeName< THandle< T > >
{
	static const CName GetTypeName();
};

#include "handle.inl"

#endif
