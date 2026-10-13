/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/renderData/include/viewport.h"

namespace commandlet
{

	// Initialises the full rendering device for use in commandlets
	// It should create everything required to support using the rendering engine
	// in command line tasks but not showing any viewports or windows to the user.
	// NOTE: Do not use this with commandlet::GpuApiOnly
	class BACKEND_ENGINE_API RenderInstance
	{
	public:
		RenderInstance();
		~RenderInstance();

		bool Initialise();
		void Shutdown();

	private:

		bool m_initialisedDevice;
	};

} // namespace commandlet
