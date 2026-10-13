/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OUT_OF_MEMORY_HANDLER_CONTROLLER_H_
#define _RED_MEMORY_OUT_OF_MEMORY_HANDLER_CONTROLLER_H_

#include "../../redSystem/include/readWriteSpinLock.h"

namespace red
{
namespace memory
{
	class OOMHandlerController
	{
	public:
		OOMHandlerController();

		bool StartHandlingOOM();
		void StopHandlingOOM();

		bool IsHandlingOOM() const;

	private:
		friend class ScopedOOMHandlerController;
		mutable red::RWSpinLock m_monitor; // Only one thread at a time can be handled.
		Bool m_isHandlingFailure; // Making sure no recursion.
	};
}
}

#endif