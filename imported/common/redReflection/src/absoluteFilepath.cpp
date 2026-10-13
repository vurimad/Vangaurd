/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "absoluteFilepath.h"
#include "rttiClassBuilder.h"
#include "rttiRegistration.h"

RTTI_BEGIN_NODEFAULT_TYPE( AbsolutePathSerializable );
	RTTI_PROPERTY( m_path ).setName( "Path" ).editable();
RTTI_END_TYPE();

void RegisterFileTypeAliases()
{
	RTTI_REGISTER_TYPE_WRAPPER( red::AbsolutePath, AbsolutePathSerializable );
}
