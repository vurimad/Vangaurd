/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageArraySerializer.h"
#include "rttiArrayTypesImpl.h"
#include "packageSerializer.h"
#include "packageReadWriteStream.h"
#include "../../redContainers/include/dynArrayAccessor.h"

namespace red
{
	PackageArraySerializer::PackageArraySerializer()
	{
	}

	PackageArraySerializer::~PackageArraySerializer()
	{
	}

	void PackageArraySerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::IBaseArrayType * arrayType = static_cast< const rtti::ArrayType* >( param.type );
		const Uint32 elementCount = arrayType->ArrayGetArraySize( param.buffer );
		context.serializer << elementCount;

		if( elementCount )
		{
			const rtti::IType * innerType = arrayType->ArrayGetInnerType(); 

			if( innerType->GetType() == RT_Fundamental )
			{
				const Uint32 elementSize = innerType->GetSize();
				void * data = arrayType->ArrayGetArrayElement( param.buffer, 0 );
				context.serializer.Serialize( data, elementCount * elementSize );
			}
			else
			{
				for( Uint32 index = 0; index != elementCount; ++index )
				{
					void * element = arrayType->ArrayGetArrayElement( param.buffer, index );
					PackageSerializeTypeParameter elementParam = { element, innerType, nullptr };
					context.serializer.SerializeType( elementParam );
				}
			}
		}
	}

	void PackageArraySerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		Uint32 elementCount = 0;
		context.serializer >> elementCount;

		const rtti::IBaseArrayType * arrayType = static_cast< const rtti::ArrayType* >( param.type );
		// ctremblay: If we got here, it means the array was different than reference. So whatever happen, array will be overridden. Destroy old content.
		arrayType->Destruct( param.buffer );

		if( elementCount )
		{
			elementCount = arrayType->TryResizingArray( param.buffer, elementCount, context.pool );

			const rtti::IType * innerType = arrayType->ArrayGetInnerType(); 			
			const Uint32 elementSize = innerType->GetSize();
	
			if( innerType->GetType() == RT_Fundamental )
			{
				void * data = arrayType->ArrayGetArrayElement( param.buffer, 0 );
				context.serializer.Serialize( data, elementCount * elementSize );
			}
			else
			{
				for( Uint32 index = 0; index != elementCount; ++index )
				{
					void * element = arrayType->ArrayGetArrayElement( param.buffer, index );
					innerType->Construct( element );
					PackageSerializeTypeParameter elementParam = { element, innerType, nullptr };
					context.serializer.SerializeType( elementParam );
				}
			}
		}
	}
	
	void PackageArraySerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const 
	{
		Uint32 elementCount = 0;
		context.serializer >> elementCount;
		if( elementCount )
		{
			const rtti::IBaseArrayType * arrayType = static_cast< const rtti::IBaseArrayType* >( param.type );
			const rtti::IType * innerType = arrayType->ArrayGetInnerType();

			if( innerType->GetType() == RT_Fundamental )
			{
				const Uint32 elementSize = innerType->GetSize();
				context.serializer.Serialize( nullptr, elementCount * elementSize );
			}
			else
			{
				for( Uint32 index = 0; index != elementCount; ++index )
				{
					PackageSerializeTypeParameter elementParam = { nullptr, innerType, nullptr };
					context.serializer.SerializeType( elementParam );
				}
			}
		}
	}
}
