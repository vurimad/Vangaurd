/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageBitFieldSerializer.h"
#include "rttiBitField.h"
#include "packageSerializer.h"
#include "packageReadWriteStream.h"
#include "../../redContainers/include/fixedArray.h"

namespace red
{	
	PackageBitFieldSerializer::PackageBitFieldSerializer()
	{}

	PackageBitFieldSerializer::~PackageBitFieldSerializer()
	{}

	void PackageBitFieldSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::BitFieldType * bitFieldType = static_cast< const rtti::BitFieldType * >( param.type );
		Uint64 value = 0;
		bitFieldType->ReadUint64( param.buffer, value );

		BitSet64< 64 > bitset;
		static_assert( sizeof( bitset ) == sizeof( value ), "Size mismatch. Data will be broken." );
		red::Memcpy( &bitset, &value, sizeof( value ) );
	
		Uint8 index = 0;
		red::FixedArray< CName, 64 > bitName;
		
		for( BitSetConstIterator< BitSet64< 64 > > iter( bitset ); iter.IsValid(); iter.FindNext() )
		{
			const CName name = bitFieldType->GetBitName( iter.GetIndex() );
			if( name != CName() )
			{
				bitName[ index++ ] = name;
			}
		}

		context.serializer << index;
		for( Uint32 nameIndex = 0; nameIndex != index; ++nameIndex )
		{
			context.serializer << bitName[ nameIndex ];
		}
	}

	void PackageBitFieldSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::BitFieldType * bitFieldType = static_cast< const rtti::BitFieldType * >( param.type );

		Uint64 bitValue = 0;
		Uint8 count = 0;
		context.serializer >> count;
		
		for( Uint32 index = 0; index != count; ++index )
		{
			CName bitName;
			context.serializer >> bitName;
			Int32 value = bitFieldType->GetBitValue( bitName );
			if( value != -1 )
			{
				bitValue |= RED_FLAG64( value );
			}
		}

		bitFieldType->WriteUint64( param.buffer, bitValue );
	}

	void PackageBitFieldSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		Uint8 count = 0;
		context.serializer >> count;

		for( Uint32 index = 0; index != count; ++index )
		{
			CName bitName;
			context.serializer >> bitName;
		}
	}
}
