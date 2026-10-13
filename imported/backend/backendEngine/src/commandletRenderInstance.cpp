/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "commandletRenderInstance.h"

#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/gpuApi/include/gpuApiPublic.h"
#include "../../../common/gpuApi/include/gpuApiInterface.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/viewport.h"

namespace commandlet
{

RenderInstance::RenderInstance()
	: m_initialisedDevice(false)
{
}

RenderInstance::~RenderInstance()
{
	if ( m_initialisedDevice )
	{
		RED_FATAL_ASSERT( !GpuApi::IsInit(), "Failed to shutdown GpuApi" );
	}
}

bool RenderInstance::Initialise()
{
	if ( !GRender->InitWithoutViewport( 100, 100, EViewportWindowMode::VWM_Windowed, false ) )
	{
		RED_LOG_ERROR( "Failed to initialize commandlet rendering device" );
		return false;
	}

	m_initialisedDevice = true;
	return true;
}

void RenderInstance::Shutdown()
{
	if ( m_initialisedDevice )
	{
		m_initialisedDevice = false;
	}
}


} // namespace commandlet
