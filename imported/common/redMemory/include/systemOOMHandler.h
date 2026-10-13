/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SYSTEM_OUT_OF_MEMORY_HANDLER_H_
#define _RED_MEMORY_SYSTEM_OUT_OF_MEMORY_HANDLER_H_

#include "types.h"
#include "proxyTypeId.h"

namespace red
{
namespace memory
{
	class OOMHandlerController;

	class RED_MEMORY_API SystemOOMHandler
	{
	public:
		SystemOOMHandler();

		void HandleProxyAllocateFailure( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment );
		void HandleSystemCommitFailure( u64 size, u32 alignment );
		void HandleSystemReservePagesFailure( u64 size, u32 alignment, u32 pageSize );
	
		bool IsHandlingOOM() const;

	protected:
		~SystemOOMHandler();

	private:
		virtual void OnHandleProxyAllocateFailure( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment ) = 0;
		virtual void OnHandleSystemCommitFailure( u64 size, u32 alignment ) = 0;
		virtual void OnHandleSystemReservePagesFailure( u64 size, u32 alignment, u32 pageSize ) = 0;

		OOMHandlerController& m_oomHandlerController;
	};

	RED_MEMORY_API void HandleAllocateFailure( ProxyTypeId proxyTypeId, void* proxy, u32 size, u32 alignment );
}
}

#endif