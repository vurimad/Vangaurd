/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTypeSerializer.h"

namespace red
{
	class RED_REFLECTION_API PackageHandleSerializer : public PackageTypeSerializer
	{
	public:
		PackageHandleSerializer();
		virtual ~PackageHandleSerializer();

	private:
	
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};

	class RED_REFLECTION_API PackageWeakHandleSerializer : public PackageTypeSerializer
	{
	public:
		PackageWeakHandleSerializer ();
		virtual ~PackageWeakHandleSerializer ();

	private:
	
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};
}
