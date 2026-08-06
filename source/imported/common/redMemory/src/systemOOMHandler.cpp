/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "systemOOMHandler.h"
#include "vault.h"
#include "oomHandlerController.h"

namespace red
{
namespace memory
{
	SystemOOMHandler::SystemOOMHandler()
		: m_oomHandlerController( AcquireVault().AcquireOOMHandlerController() )
	{}

	SystemOOMHandler::~SystemOOMHandler()
	{}

	void SystemOOMHandler::HandleProxyAllocateFailure( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment )
	{
		if ( m_oomHandlerController.StartHandlingOOM() )
		{
			OnHandleProxyAllocateFailure( proxyId, proxy, size, alignment );
		}

		m_oomHandlerController.StopHandlingOOM();
	}

	void SystemOOMHandler::HandleSystemCommitFailure( u64 size, u32 alignment )
	{
		if ( m_oomHandlerController.StartHandlingOOM() )
		{
			OnHandleSystemCommitFailure( size, alignment );
		}

		m_oomHandlerController.StopHandlingOOM();
	}

	void SystemOOMHandler::HandleSystemReservePagesFailure( u64 size, u32 alignment, u32 pageSize )
	{
		if ( m_oomHandlerController.StartHandlingOOM() )
		{
			OnHandleSystemReservePagesFailure( size, alignment, pageSize );
		}

		m_oomHandlerController.StopHandlingOOM();
	}

	bool SystemOOMHandler::IsHandlingOOM() const
	{
		return m_oomHandlerController.IsHandlingOOM();
	}

	void HandleAllocateFailure( ProxyTypeId proxyTypeId, void* proxy, u32 size, u32 alignment )
	{
		AcquireVault().HandleProxyOOM( proxyTypeId, proxy, size, alignment );
	}
}
}