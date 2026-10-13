/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageSimpleTypeSerializer.h"
#include "packageVersion.h"
#include "packageSerializer.h"
#include "packageReadWriteStream.h"
#include "packageWriteStream.h"
#include "packageReadStream.h"
#include "rttiClassBuilder.h"
#include "../../redSystem/include/ruid.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( PackageRUIDSerializer, red );
	RTTI_PARENT_TYPE( PackageCustomTypeSerializer );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PackageTweakDBIDSerializer, red );
	RTTI_PARENT_TYPE( PackageCustomTypeSerializer );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( PackageDataBufferSerializer, red );
	RTTI_PARENT_TYPE( PackageCustomTypeSerializer );
RTTI_END_TYPE();


namespace red
{
	PackageRUIDSerializer::PackageRUIDSerializer()
	{
	}

	PackageRUIDSerializer::~PackageRUIDSerializer()
	{
	}

	const rtti::IType * PackageRUIDSerializer::OnGetType() const
	{
		return GetTypeObject< red::RUID >();
	}

	void PackageRUIDSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const red::RUID * ruid = static_cast< red::RUID* >( param.buffer );
	
		RED_FATAL_ASSERT( !ruid->IsTransient(), "Cannot serialize transient RUID!" );

		context.serializer << ruid->m_value;
	}

	void PackageRUIDSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		red::RUID * ruid = static_cast< red::RUID* >( param.buffer );
		Uint64 value = 0;
		context.serializer >> value;
		*ruid = red::RUID( value );
	}

	void PackageRUIDSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		context.serializer.Serialize( nullptr, sizeof( Uint64 ) );
	}

	PackageTweakDBIDSerializer::PackageTweakDBIDSerializer()
	{
	}

	PackageTweakDBIDSerializer::~PackageTweakDBIDSerializer()
	{
	}

	const rtti::IType * PackageTweakDBIDSerializer::OnGetType() const
	{
		return GetTypeObject< TweakDBID >();
	}

	void PackageTweakDBIDSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const game::data::TweakDBID* tweakDBID = static_cast<game::data::TweakDBID*>( param.buffer );

		Uint64 hashNum = tweakDBID->ToNumber();
		context.serializer << hashNum;
	}

	void PackageTweakDBIDSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		game::data::TweakDBID* tweakDBID = static_cast<game::data::TweakDBID*>( param.buffer );

		// backwards compatibility for string based serialization
		if ( context.packageVersion < c_packageVersionTweakDBIDHash )
		{
			String str;
			context.serializer >> str;
			*tweakDBID = TDBID( str );
		}
		else
		{
			Uint64 id;
			context.serializer >> id;
			*tweakDBID = TweakDBID::FromNumber( id );
		}
	}

	void PackageTweakDBIDSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		game::data::TweakDBID tweakDBID;

		// backwards compatibility for string based serialization
		if ( context.packageVersion < c_packageVersionTweakDBIDHash )
		{
			Uint16 length = 0;
			context.inputStream.Read( &length, sizeof( length ) );
			String str;
			str.Resize( length );

			if( length )
			{
				context.inputStream.Read( str.Data(), length );
			}

			tweakDBID = TDBID( str );
			Uint64 hashNum = tweakDBID.ToNumber();
			context.outputStream.Write( &hashNum, sizeof( hashNum ) );
		}
		else
		{
			Uint64 hashNum;
			context.serializer >> hashNum; // Nothing to do here. Remap will simply skip.

			tweakDBID = TweakDBID::FromNumber( hashNum );
		}
	}

	PackageDataBufferSerializer::PackageDataBufferSerializer()
	{
	}

	PackageDataBufferSerializer::~PackageDataBufferSerializer()
	{
	}

	const rtti::IType * PackageDataBufferSerializer::OnGetType() const
	{
		return GetTypeObject< DataBuffer >();
	}

	void PackageDataBufferSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		DataBuffer * dataBuffer = static_cast< DataBuffer* >( param.buffer );
		Uint32 size = dataBuffer->Size();
		context.serializer << size;
		if( size )
		{
			context.serializer.Serialize( dataBuffer->Data(), size );
		}
	}

	void PackageDataBufferSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		DataBuffer * dataBuffer = static_cast< DataBuffer* >( param.buffer );
		Uint32 size = 0;
		context.serializer >> size;
		if( size )
		{
			auto& pool = dataBuffer->GetPool();
			red::UniqueBuffer buffer = red::CreateUniqueBuffer(pool, size, dataBuffer->GetAlignment());
			context.serializer.Serialize( buffer.Get(), size );
			*dataBuffer = DataBuffer( std::move( buffer ) );
		}	
	}

	void PackageDataBufferSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		Uint32 size = 0;
		context.serializer >> size;
		if( size )
		{
			context.serializer.Serialize( nullptr, size );
		}
	}
}
