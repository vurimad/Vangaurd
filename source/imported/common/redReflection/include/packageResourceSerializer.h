/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageTypeSerializer.h"

namespace red
{
	class RED_REFLECTION_API PackageResourceSerializer : public PackageTypeSerializer
	{
	public:
		PackageResourceSerializer();
		virtual ~PackageResourceSerializer();

	private:
	
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};

	class RED_REFLECTION_API PackageAsyncResourceSerializer : public PackageTypeSerializer
	{
	public:
		PackageAsyncResourceSerializer();
		virtual ~PackageAsyncResourceSerializer();

	private:
	
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};
}
