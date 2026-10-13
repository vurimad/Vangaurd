/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class IRenderObject;

template< typename T >
RED_INLINE TRenderPtr< T >::TRenderPtr()
{
	static_assert( std::is_base_of< IRenderObject, T >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
RED_INLINE TRenderPtr< T >::TRenderPtr( std::nullptr_t )
{
}

template< typename T >
RED_INLINE TRenderPtr< T >::TRenderPtr( const TRenderPtr & copyFrom )
	: ParentType( copyFrom )
{
	static_assert( std::is_base_of< IRenderObject, T >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
RED_INLINE TRenderPtr< T >::TRenderPtr( TRenderPtr && rvalue )
	: ParentType( std::forward< TRenderPtr >( rvalue ) )
{
	static_assert( std::is_base_of< IRenderObject, T >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr< T >::TRenderPtr( U * pointer )
	: ParentType( pointer )
{
	static_assert( std::is_base_of< IRenderObject, U >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr< T >::TRenderPtr( const TRenderPtr< U > & copyFrom )
	: ParentType( copyFrom )
{
	static_assert( std::is_base_of< IRenderObject, T >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr< T >::TRenderPtr( TRenderPtr< U > && rvalue )
	: ParentType( std::forward< TRenderPtr< U > >( rvalue ) )
{
	static_assert( std::is_base_of< IRenderObject, T >::value, "TRenderPtr can only be use with IRenderObject" );
}

template< typename T >
RED_INLINE TRenderPtr< T > & TRenderPtr< T >::operator=( const TRenderPtr & copyFrom )
{
	ParentType::operator =( copyFrom );
	return *this;
}

template< typename T >
RED_INLINE TRenderPtr< T > & TRenderPtr< T >::operator=( TRenderPtr && rvalue )
{
	ParentType::operator =( std::forward< TRenderPtr >( rvalue ) );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr< T > & TRenderPtr< T >::operator=( const TRenderPtr< U > & copyFrom )
{
	ParentType::operator =( copyFrom );
	return *this;
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr<T>	TRenderPtr<T>::MakePtr( U * pointer )
{
	static_assert( std::is_base_of< IRenderObject, U >::value, "TRenderPtr can only be use with IRenderObject" );
	TRenderPtr< T > ret( static_cast<T*>( pointer ) );
	ret.ParentType::template AddRef<T>();
	return std::move( ret );
}

template< typename T >
template< typename U >
RED_INLINE TRenderPtr< T > & TRenderPtr< T >::operator=( TRenderPtr< U > && rvalue )
{
	ParentType::operator =(  std::forward< TRenderPtr< U > >( rvalue ) );
	return *this;
}

RED_INLINE RenderObjectOwnership::RenderObjectOwnership()
	: m_pointee( nullptr )
{}

RED_INLINE RenderObjectOwnership::RenderObjectOwnership( void* pointer )
	: m_pointee( pointer )
{}

RED_INLINE RenderObjectOwnership::RenderObjectOwnership( const RenderObjectOwnership & copyFrom )
	: m_pointee( copyFrom.m_pointee )
{}

RED_INLINE RenderObjectOwnership::RenderObjectOwnership( RenderObjectOwnership && rvalue )
	: m_pointee( nullptr )
{
	AssignRValue( std::forward< RenderObjectOwnership >( rvalue ) );
}

template< typename T >
RED_INLINE void RenderObjectOwnership::Destroy()
{
	// pointer is destroyed by release function.
}

template< typename T >
RED_INLINE bool RenderObjectOwnership::Release()
{
#ifdef RENDER_PTR_ENABLE_RACE_DETECTION
	RED_SCOPE_FLAG_GUARD( m_updateFlag, Inclusive );
#endif

	T * typedPtr = static_cast< T* >( m_pointee );

	if( typedPtr )
	{
		typedPtr->Release();
	}

	return false;
}

template< typename T >
RED_INLINE void RenderObjectOwnership::AddRef()
{
#ifdef RENDER_PTR_ENABLE_RACE_DETECTION
	RED_SCOPE_FLAG_GUARD( m_updateFlag, Inclusive );
#endif

	T * typedPtr = static_cast< T* >( m_pointee );

	if( typedPtr )
	{
		typedPtr->AddRef();
	}
}

RED_INLINE void RenderObjectOwnership::Swap( RenderObjectOwnership & swapWith )
{
#ifdef RENDER_PTR_ENABLE_RACE_DETECTION
	RED_SCOPE_FLAG_GUARD( m_updateFlag, Exclusive );
	RED_SCOPE_FLAG_GUARD( swapWith.m_updateFlag, Exclusive );
#endif
	::Swap( m_pointee, swapWith.m_pointee );
}

RED_INLINE void* RenderObjectOwnership::Get() const
{
	return m_pointee;
}

RED_INLINE void RenderObjectOwnership::AssignRValue( RenderObjectOwnership && rvalue )
{
	if( this != &rvalue )
	{
		Swap( rvalue );
	}
}
