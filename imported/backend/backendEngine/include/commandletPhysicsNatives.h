/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace commandlet
{
	class BACKEND_ENGINE_API PhysicsNatives
	{
	public:
		PhysicsNatives();
		~PhysicsNatives();

		Bool Initialize();
		void Shutdown();

	private:
		PhysicsNatives(const PhysicsNatives&) = delete;
		PhysicsNatives(PhysicsNatives&&) = delete;
		PhysicsNatives& operator=(PhysicsNatives&) = delete;
		PhysicsNatives& operator=(PhysicsNatives&&) = delete;
	};
}
