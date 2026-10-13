/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageEnumSerializer.h"
#include "rttiEnum.h"
#include "packageSerializer.h"

namespace red
{
	PackageEnumSerializer::PackageEnumSerializer()
	{}

	PackageEnumSerializer::~PackageEnumSerializer()
	{}

	void PackageEnumSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		const rtti::EnumType * enumType = static_cast< const rtti::EnumType * >( param.type );
		const CName enumName = enumType->GetName( param.buffer );
		context.serializer << enumName;
	}
	
	void PackageEnumSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		CName enumName;
		context.serializer >> enumName;
		const rtti::EnumType * enumType = static_cast< const rtti::EnumType * >( param.type );
		enumType->SetValue( param.buffer, enumName );
	}

	void PackageEnumSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		CName dummyName;
		context.serializer >> dummyName;
	}
}