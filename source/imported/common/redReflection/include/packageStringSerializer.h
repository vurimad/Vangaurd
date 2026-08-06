/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageCustomTypeSerializer.h"

namespace red
{
	class RED_REFLECTION_API PackageStringSerializer : public PackageCustomTypeSerializer
	{
		RTTI_DECLARE_TYPE( PackageStringSerializer );

	public:
		PackageStringSerializer();
		virtual ~PackageStringSerializer();

	private:

		virtual const rtti::IType * OnGetType() const override final;
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override final;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override final;
	
		const rtti::IType * m_stringType;
	};
}
