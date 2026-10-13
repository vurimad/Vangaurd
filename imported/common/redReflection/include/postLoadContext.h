/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redReflection/include/util.h"

namespace serialization
{
	class LoadingContext;
}

struct PostLoadFlags
{
	bool skipUploadRenderData = false;	// Don't upload any data to renderer
};

struct PostLoadContext
{
	PostLoadFlags flags;
	const serialization::LoadingContext* hackLoadingContext{ nullptr }; // Hack to go with HACK_GetPostLoadWaitCounter()
};

struct PreSaveContext
{
	bool isCooking = false;
	bool isGenerator = false;
	bool isCloner = false;
	ECookingPlatform cookingPlatform = PLATFORM_None;
};
