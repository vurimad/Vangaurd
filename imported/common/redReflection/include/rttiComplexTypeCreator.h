/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

template< class T >
struct TSingleChannelCurve;

template< class T >
struct TMultiChannelCurve;

template< typename T >
class THandle;

template< typename T >
class WeakHandle;

template< class T >
class TResRef;

template< class T >
class TResAsyncRef;

namespace rtti
{
	class IType;

	template< typename T >
	struct ComplexTypeCreator
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return nullptr;
		}
	};

	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateArrayType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateNativeArrayType( const rtti::IType * innerType, Uint32 count );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateStaticArrayType( const rtti::IType * innerType, Uint32 count );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateSingleChannelCurveType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateMultiChannelCurveType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateResourceReferenceType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateResourceAsynReferenceType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateHandleType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreateWeakHandleType( const rtti::IType * innerType );
	RED_REFLECTION_API red::UniquePtr< rtti::IType > CreatePointerType( const rtti::IType * innerType );

	template< typename T >
	struct ComplexTypeCreator< red::DynArray< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateArrayType( innerType );
		}
	};

	template<class T, Uint32 ElemCount>
	struct ComplexTypeCreator<T[ElemCount]>
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateNativeArrayType( innerType, ElemCount );
		}
	};

	template<typename T, Uint32 ElemCount >
	struct ComplexTypeCreator< red::StaticArray< T, ElemCount > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateStaticArrayType( innerType, ElemCount );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< TSingleChannelCurve< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateSingleChannelCurveType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< TMultiChannelCurve< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateMultiChannelCurveType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< T* >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreatePointerType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< THandle< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateHandleType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< WeakHandle< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateWeakHandleType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< TResRef< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateResourceReferenceType( innerType );
		}
	};

	template< typename T >
	struct ComplexTypeCreator< TResAsyncRef< T > >
	{
		static red::UniquePtr< rtti::IType > Create( const rtti::IType* innerType )
		{
			return CreateResourceAsynReferenceType( innerType );
		}
	};
}
