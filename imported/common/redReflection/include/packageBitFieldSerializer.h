/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageTypeSerializer.h"

namespace red
{
	class PackageBitFieldSerializer : public PackageTypeSerializer
	{
	public:
		PackageBitFieldSerializer();
		virtual ~PackageBitFieldSerializer();

	private:
	
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};
}
