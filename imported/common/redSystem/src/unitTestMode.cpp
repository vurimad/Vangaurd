/**
* Copyright (c) 2014 CDProjekt Red, Inc. All Rights Reserved.
*/
#include "build.h"
#include "unitTestMode.h"

namespace red
{
	static bool unitTestMode = false;

	bool UnitTestMode()
	{
		return unitTestMode;
	}

	void SetUnitTestMode()
	{
		unitTestMode = true;
	}
}

