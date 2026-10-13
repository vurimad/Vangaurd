/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptDebuggerLocalsHandle.h"
#include "scriptDebuggerLocalsWeakHandle.h"
#include "scriptDebuggerLocalsObject.h"

namespace script { namespace debug {

WeakHandle::WeakHandle( const SerializableWeakHandle* handle, const rtti::IType* type, const String& name )
	: Handle( &m_handle, type, name )
	, m_handle( handle->ToHandle() )
{

}

} } // namespace script { namespace debug {
