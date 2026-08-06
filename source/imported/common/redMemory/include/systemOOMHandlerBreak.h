/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SYSTEM_OUT_OF_MEMORY_HANDLER_BREAK_H_
#define _RED_MEMORY_SYSTEM_OUT_OF_MEMORY_HANDLER_BREAK_H_

#include "systemOOMHandler.h"

namespace red
{
namespace memory
{
	class Reporter;

	class SystemOOMHandlerBreak final : public SystemOOMHandler
	{
	public:
		SystemOOMHandlerBreak();
		~SystemOOMHandlerBreak();

		void Initialize( const Reporter * reporter );

	private:
		virtual void OnHandleProxyAllocateFailure( ProxyTypeId proxyId, void* proxy, u32 size, u32 alignment ) override;
		virtual void OnHandleSystemCommitFailure( u64 size, u32 alignment ) override;
		virtual void OnHandleSystemReservePagesFailure( u64 size, u32 alignment, u32 pageSize ) override;

		const Reporter * m_reporter;
	};
}
}

#endif