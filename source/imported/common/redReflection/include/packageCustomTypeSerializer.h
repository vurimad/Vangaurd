/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "packageTypeSerializer.h"
#include "rttiClassDeclarationMacros.h"

namespace red
{
	class RED_REFLECTION_API PackageCustomTypeSerializer : public PackageTypeSerializer
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE( PackageCustomTypeSerializer );
		RED_USE_MEMORY_POOL( red::PoolEngine );
	
	public:
		PackageCustomTypeSerializer();
		virtual ~PackageCustomTypeSerializer();

		const rtti::IType * GetType() const;
		
	private:

		virtual const rtti::IType * OnGetType() const = 0;
		virtual void OnWriteValue( const WriteContext & context, const PackageSerializeTypeParameter & param ) const override = 0;
		virtual void OnReadValue( const ReadContext & context, const PackageSerializeTypeParameter & param ) const override = 0;
		virtual void OnRemapValue( const RemapContext& context, const PackageSerializeTypeParameter & param ) const override = 0;
	};
}

