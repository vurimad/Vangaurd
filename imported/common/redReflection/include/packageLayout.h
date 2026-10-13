/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	// ctremblay: DO NOT CHANGE ORDER OF VALUE. IF you do, you will need to bump version and manage change in PackageLoader.
	enum PackageTableType : Uint8
	{
		PackageTableType_Resource = 0,
		PackageTableType_String = 1,
		PackageTableType_Object = 2,
		PackageTableType_Count,
		PackageTableType_Max = 16
	};

	struct PackageLayoutEntry
	{
		Uint32 tableOffset;
		Uint32 dataOffset;
	};

	struct PackageLayoutHeader
	{
		Uint16 version;
		Uint16 tableMask;
		Uint32 rootObjectCount;
	};
}
