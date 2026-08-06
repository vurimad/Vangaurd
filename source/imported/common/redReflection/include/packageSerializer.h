/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiSystem.h"

namespace red
{
	class IType;

	struct PackageSerializeTypeParameter
	{
		void * buffer; 
		const rtti::IType* type;
		const void * referenceValue;
	};

	RED_REFLECTION_API bool operator==( const PackageSerializeTypeParameter & left, const PackageSerializeTypeParameter &right );

	class RED_REFLECTION_API PackageSerializer
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		
		PackageSerializer();
		virtual ~PackageSerializer();

		RED_MOCKABLE void Serialize( void * buffer, Uint64 size );
		RED_MOCKABLE bool SerializeType( const PackageSerializeTypeParameter & context );
		
	private:
	
		virtual void OnSerialize( void * buffer, Uint64 size ) = 0;
		virtual bool OnSerializeType( const PackageSerializeTypeParameter & context ) = 0;
	};

	template< typename T >
	RED_INLINE PackageSerializer & operator<<( PackageSerializer & serializer, T & data )
	{
		// ctremblay: const_cast here is because I didn't not want to duplicate all Serialize method.
		// This would made interface hard(er) to maintain. 
		serializer.SerializeType( { const_cast< typename std::remove_const< T >::type * >( &data ), GetTypeObject< T >(), nullptr } );
		return serializer;
	}

	template< typename T >
	RED_INLINE PackageSerializer& operator>>( PackageSerializer& serializer, T& data )
	{
		serializer.SerializeType( {&data, GetTypeObject< T >(), nullptr} );
		return serializer;
	}

	template< typename T, Int32 N >
	PackageSerializer & operator<<( PackageSerializer & serializer, T (&data)[N] );

	template< typename T, Int32 N >
	PackageSerializer & operator>>( PackageSerializer & serializer, T (&data)[N] );


	RED_INLINE void PackageSerializer::Serialize( void* buffer, Uint64 size )
	{
		RED_FATAL_ASSERT( size, "Cannot serialize from/to a 0 size buffer." );
		OnSerialize( buffer, size );
	}

	RED_INLINE bool PackageSerializer::SerializeType( const PackageSerializeTypeParameter& context )
	{
		RED_FATAL_ASSERT( context.type, "Cannot serialize from/to a null type." );
		return OnSerializeType( context );
	}
}

