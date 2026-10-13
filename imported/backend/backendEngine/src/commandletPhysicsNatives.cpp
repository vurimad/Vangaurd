/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "commandletPhysicsNatives.h"

#include "../../common/physics/include/physXNatives.h"

namespace commandlet
{
	PhysicsNatives::PhysicsNatives() = default;
	PhysicsNatives::~PhysicsNatives() = default;

	Bool PhysicsNatives::Initialize()
	{
		return physics::CreateNatives();
	}

	void PhysicsNatives::Shutdown()
	{
		physics::DestroyNatives();
	}
}