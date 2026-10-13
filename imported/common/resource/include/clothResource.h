/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/resource.h"

class RED_RESOURCE_API ClothResource final : public CResource
{
	RTTI_DECLARE_TYPE( ClothResource );
    RED_BASE_CLASS(CResource);
	IMPLEMENT_RESOURCE_INTERFACE( "", "cloth", "Physical Cloth resource" );

public:
    ClothResource();
    ~ClothResource();
};