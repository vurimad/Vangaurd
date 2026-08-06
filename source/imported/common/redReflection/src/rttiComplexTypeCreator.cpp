/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiComplexTypeCreator.h"
#include "rttiArrayTypesImpl.h"
#include "singleChannelCurve.h"
#include "multiChannelCurve.h"
#include "rttiPointerTypesImpl.h"

namespace rtti
{
	red::UniquePtr< rtti::IType > CreateArrayType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::ArrayType >( innerType );
	}

	red::UniquePtr< rtti::IType > CreateNativeArrayType( const rtti::IType * innerType, Uint32 count )
	{
		return red::CreateUniquePtr< rtti::NativeArrayType >( innerType, count );
	}
	
	red::UniquePtr< rtti::IType > CreateStaticArrayType( const rtti::IType * innerType, Uint32 count )
	{
		return red::CreateUniquePtr< rtti::StaticArrayType >( innerType, count );
	}
	
	red::UniquePtr< rtti::IType > CreateSingleChannelCurveType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< SingleChannelCurveType >( innerType );
	}

	red::UniquePtr< rtti::IType > CreateMultiChannelCurveType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< MultiChannelCurveType >( innerType );
	}
	
	red::UniquePtr< rtti::IType > CreateResourceReferenceType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::ResourceReferenceType >( innerType );
	}
	
	red::UniquePtr< rtti::IType > CreateResourceAsynReferenceType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::ResourceAsyncReferenceType >( innerType );
	}
	
	red::UniquePtr< rtti::IType > CreateHandleType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::HandleType >( innerType );
	}
	
	red::UniquePtr< rtti::IType > CreateWeakHandleType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::WeakHandleType >( innerType );
	}
	
	red::UniquePtr< rtti::IType > CreatePointerType( const rtti::IType * innerType )
	{
		return red::CreateUniquePtr< rtti::PointerType >( innerType );
	}
}
