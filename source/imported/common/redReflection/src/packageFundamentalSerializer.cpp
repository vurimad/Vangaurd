/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageFundamentalSerializer.h"
#include "packageSerializer.h"
#include "rttiType.h"
#include "packageReadWriteStream.h"

namespace red
{
	PackageFundamentalSerializer::PackageFundamentalSerializer()
	{}

	PackageFundamentalSerializer::~PackageFundamentalSerializer()
	{}

	void PackageFundamentalSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const Uint32 size = param.type->GetSize();
		context.serializer.Serialize( param.buffer, size );
	}

	void PackageFundamentalSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const Uint32 size = param.type->GetSize();
		context.serializer.Serialize( param.buffer, size );
	}

	void PackageFundamentalSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		const Uint32 size = param.type->GetSize();
		context.serializer.Serialize( param.buffer, size );
	}
}
