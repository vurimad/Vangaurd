/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "rttiSystem.h"
#include "rttiSystemImpl.h"

namespace rtti
{
	ITypeSystem::ITypeSystem()
	{
	}

	ITypeSystem::~ITypeSystem()
	{
	}

	ITypeSystem& ITypeSystem::GetInstance()
	{
		return TypeSystemImpl::GetInstance();
	}

} // rtti
