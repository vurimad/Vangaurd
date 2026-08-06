/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageCustomTypeSerializer.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( PackageCustomTypeSerializer, red )
RTTI_END_TYPE();

namespace red
{
	PackageCustomTypeSerializer::PackageCustomTypeSerializer()
	{}

	PackageCustomTypeSerializer::~PackageCustomTypeSerializer()
	{}

	const rtti::IType * PackageCustomTypeSerializer::GetType() const
	{
		return OnGetType();
	}
}
