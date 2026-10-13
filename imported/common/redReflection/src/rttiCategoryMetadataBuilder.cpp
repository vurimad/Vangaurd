/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "rttiCategoryMetadataBuilder.h"

namespace rtti
{
	void CategoryMetadataBuilder::ChangeContext( CName categoryName )
	{
		const auto name = categoryName.AsStringView();
		m_stringBuilder.Reset();
		m_stringBuilder.Append( name.Data(), name.Length() );
		m_stringBuilder.Append( ':' );
	}

	CName CategoryMetadataBuilder::GetCategory() const
	{
		if( m_stringBuilder.Empty() )
		{
			return CName::NONE();
		}
		red::StringView outCategory = { m_stringBuilder.AsChar(), m_stringBuilder.GetLength() - 1 };
		return RED_NAME( outCategory );
	}

	CategoryMetadataBuilder& CategoryMetadataBuilder::collapsed()
	{
		m_stringBuilder.Append( "IgnoreAutoExpand;" );
		return *this;
	}

	CategoryMetadataBuilder& CategoryMetadataBuilder::order( const OrderPriority order )
	{
		m_stringBuilder.Appendf( "Order=%u;", static_cast<Uint32>( order ) );
		return *this;
	}

}