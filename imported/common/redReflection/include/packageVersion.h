/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	const Uint32 c_packageVersionSimpleLayout = 0;
	const Uint32 c_packageVersionAdaptativeLayout = 1;
	const Uint32 c_packageVersionRootObjectLayout = 2;
	const Uint32 c_packageVersionObjectIndex32bits = 3;
	const Uint32 c_packageVersionTweakDBIDHash = 4;

	const Uint32 c_packageCurrentVersion = c_packageVersionTweakDBIDHash;
}
