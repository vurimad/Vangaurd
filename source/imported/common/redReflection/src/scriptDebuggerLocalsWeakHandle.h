/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "scriptDebuggerLocalsBase.h"

namespace script
{
	namespace debug
	{
		class WeakHandle : public Handle
		{
		public:
			WeakHandle( const SerializableWeakHandle* handle, const rtti::IType* type, const String& name );

		private:
			SerializableHandle m_handle;
		};
	}
}
