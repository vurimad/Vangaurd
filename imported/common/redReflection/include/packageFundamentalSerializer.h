/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageTypeSerializer.h"

namespace red
{
	class RED_REFLECTION_API PackageFundamentalSerializer : public PackageTypeSerializer
	{
	public:
		PackageFundamentalSerializer();
		virtual ~PackageFundamentalSerializer();

	private:

		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};
}
