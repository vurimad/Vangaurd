/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "oomHandlerController.h"
#include "assert.h"

namespace red
{
#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
	extern thread_local Bool g_oom;
#endif

namespace memory
{

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
namespace dd
{
	static red::CrashData< Bool > s_oom{ "Engine", "OOM", false };
}
#endif

	OOMHandlerController::OOMHandlerController()
		: m_isHandlingFailure( false )
	{}

	bool OOMHandlerController::StartHandlingOOM()
	{
		m_monitor.Acquire();
		if ( m_isHandlingFailure )
		{
			return false;
		}

		m_isHandlingFailure = true;
#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
		red::g_oom = true;
#else
		dd::s_oom.Set( true );
#endif
		return true;
	}

	void OOMHandlerController::StopHandlingOOM()
	{
		if ( m_isHandlingFailure )
		{
			m_isHandlingFailure = false;
		}

		m_monitor.Release();
	}

	bool OOMHandlerController::IsHandlingOOM() const
	{
		RED_SCOPE_SHARED_LOCK( m_monitor );
		return m_isHandlingFailure;
	}
}
}