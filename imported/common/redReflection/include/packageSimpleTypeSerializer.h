/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "packageCustomTypeSerializer.h"

namespace red
{
	class RED_REFLECTION_API PackageRUIDSerializer : public PackageCustomTypeSerializer
	{
		RTTI_DECLARE_TYPE( PackageRUIDSerializer );

	public:

		PackageRUIDSerializer();
		virtual ~PackageRUIDSerializer();

	private:

		virtual const rtti::IType * OnGetType() const override final;
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};

	class RED_REFLECTION_API PackageTweakDBIDSerializer : public PackageCustomTypeSerializer
	{
		RTTI_DECLARE_TYPE( PackageTweakDBIDSerializer );

	public:

		PackageTweakDBIDSerializer();
		virtual ~PackageTweakDBIDSerializer();

	private:

		virtual const rtti::IType * OnGetType() const override final;
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};

	class RED_REFLECTION_API PackageDataBufferSerializer : public PackageCustomTypeSerializer
	{
		RTTI_DECLARE_TYPE( PackageDataBufferSerializer );

	public:

		PackageDataBufferSerializer();
		virtual ~PackageDataBufferSerializer();

	private:

		virtual const rtti::IType * OnGetType() const override final;
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	};
}
