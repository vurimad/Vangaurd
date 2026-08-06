/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageNameSerializer.h"
#include "packageSerializer.h"
#include "packageTable.h"
#include "packageReadStream.h"
#include "packageWriteStream.h"
#include "packageTableOfContent.h"

namespace red
{
	PackageNameSerializer::PackageNameSerializer()
	{}

	PackageNameSerializer::~PackageNameSerializer()
	{}

	void PackageNameSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		CName * name = static_cast< CName* >( param.buffer );
		const Uint16 index = context.table.MapName( *name );
		context.serializer << index;
	}

	void PackageNameSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		PackageTableOfContent::NameIndex index = ~0;
		context.serializer >> index;
		const CName name = context.table.UnmapName( index );
		*static_cast< CName * >( param.buffer ) = name; 
	}

	void PackageNameSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		// ctremblay: Absolutely dreadful. 
		// Serializer vs Stream vs TypeSerializer is kinda flawed for remapping mostly because of Differential Serialization.
		PackageTableOfContent::NameIndex index = ~0;
		context.inputStream.Read( &index, sizeof( index ) );
		PackageTableOfContent::NameIndex remappedIndex = context.table.RemapName( index );
		context.outputStream.Write( &remappedIndex, sizeof( remappedIndex ) );
	}
}
