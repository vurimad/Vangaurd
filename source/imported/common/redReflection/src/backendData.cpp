/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendData.h"
#include "rttiClassBuilder.h"

RTTI_BEGIN_ABSTRACT_TYPE( IBackendData )
	RTTI_PARENT_TYPE( ISerializable )
RTTI_END_TYPE()

void IBackendData::OnSerialize( IFile& file )
{
#ifdef RED_CONFIGURATION_FINAL
	RED_FATAL_ASSERT( false, "Backend only data is being loaded." );
#endif

	ISerializable::OnSerialize( file );
}
