/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "oomHandler.h"
#include "poolRegistry.h"
#include "vault.h"
#include "oomHandlerController.h"

namespace red
{
namespace memory
{
	PoolOOMHandler::PoolOOMHandler()
		: m_oomHandlerController( AcquireVault().AcquireOOMHandlerController() )
		, m_poolRegistry( &AcquireVault().AcquirePoolRegistry() )
	{}

	PoolOOMHandler::~PoolOOMHandler()
	{}

	void PoolOOMHandler::HandlePoolAllocateFailure( PoolHandle poolHandle, ProxyTypeId allocatorId, u32 size, u32 alignment )
	{
		if ( m_oomHandlerController.StartHandlingOOM() )
		{
			const char * poolName = m_poolRegistry ? m_poolRegistry->GetPoolName( poolHandle ) : "<Unknown Pool>";
			const char * allocatorName = GetAllocatorName( allocatorId );
			OnHandlePoolAllocateFailure( poolName, allocatorName, size, alignment );
		}

		m_oomHandlerController.StopHandlingOOM();
	}

	void PoolOOMHandler::SetPoolRegistry( const PoolRegistry * registry )
	{
		m_poolRegistry = registry;
	}

	bool PoolOOMHandler::IsHandlingOOM() const
	{
		return m_oomHandlerController.IsHandlingOOM();
	}

	void HandleAllocateFailure( PoolStorage & storage, u32 size, u32 alignment )
	{
		if( storage.oomHandler )
		{
			storage.oomHandler->HandlePoolAllocateFailure( storage.handle, storage.allocatorId, size, alignment );
		}
		else
		{
			AcquireVault().HandlePoolOOM( storage.handle, storage.allocatorId, size, alignment );
		}
	}
}
}
