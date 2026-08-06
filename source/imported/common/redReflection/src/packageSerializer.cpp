/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "packageSerializer.h"
#include "rttiType.h"

namespace red
{
	PackageSerializer::PackageSerializer()
	{}
	
	PackageSerializer::~PackageSerializer()
	{}

	bool operator==( const PackageSerializeTypeParameter & left, const PackageSerializeTypeParameter &right )
	{
		if( left.type == right.type && left.referenceValue == right.referenceValue )
		{
			return left.type->Compare( left.buffer, right.buffer, 0 );
		}

		return false;
	}
}
