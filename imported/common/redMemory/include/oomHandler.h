/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OUT_OF_MEMORY_HANDLER_H_
#define _RED_MEMORY_OUT_OF_MEMORY_HANDLER_H_

#include "poolTypes.h"
#include "proxyTypeId.h"
#include "../../redSystem/include/readWriteSpinLock.h"

namespace red
{
namespace memory
{
	class PoolRegistry;
	class OOMHandlerController;

	class RED_MEMORY_API PoolOOMHandler
	{
	public:
		PoolOOMHandler();

		void HandlePoolAllocateFailure( PoolHandle poolHandle, ProxyTypeId allocatorId, u32 size, u32 alignment );

		void SetPoolRegistry( const PoolRegistry * registry );
		bool IsHandlingOOM() const;

	protected:
		~PoolOOMHandler();

	private:
		virtual void OnHandlePoolAllocateFailure( const char * poolName, const char * allocatorName, u32 size, u32 alignment ) = 0;

		OOMHandlerController& m_oomHandlerController;
		const PoolRegistry * m_poolRegistry;
	};

	RED_MEMORY_API void HandleAllocateFailure( PoolStorage & storage, u32 size, u32 alignment );
}
}

#endif
