/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageStringSerializer.h"
#include "packageSerializer.h"
#include "packageReadWriteStream.h"

#include "rttiType.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( PackageStringSerializer, red );
	RTTI_PARENT_TYPE( PackageCustomTypeSerializer )
RTTI_END_TYPE();

namespace red
{
	PackageStringSerializer::PackageStringSerializer()
		: m_stringType( GetTypeObject< String >() )
	{}

	PackageStringSerializer::~PackageStringSerializer()
	{}

	const rtti::IType * PackageStringSerializer::OnGetType() const
	{
		return m_stringType;
	}

	void PackageStringSerializer::OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const
	{
		String * str = static_cast< String* >( param.buffer );
		
		RED_FATAL_ASSERT( str->Length() < std::numeric_limits< Uint16 >::max(), "String are limited to 64k character." );
		
		const Uint16 length = str->Length();
		context.serializer << length;

		if( length )
		{
			context.serializer.Serialize( str->Data(), length );
		}
	}

	void PackageStringSerializer::OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const
	{
		Uint16 length = 0;		
		context.serializer >> length;
		String * str = static_cast< String* >( param.buffer );
		str->Resize( length );

		if( length )
		{
			context.serializer.Serialize( str->Data(), length );
		}
	}

	void PackageStringSerializer::OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const
	{
		Uint16 length = 0;
		context.serializer >> length;
		if( length )
		{
			context.serializer.Serialize( nullptr, length );
		}
	}
}
