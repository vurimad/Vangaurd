/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceListResource.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( ResourceListResource, red );
RTTI_PARENT_TYPE( CResource );
RTTI_PROPERTY( m_resources );
RTTI_PROPERTY( m_descriptions );
RTTI_END_TYPE();

namespace red
{

ResourceListResource::ResourceListResource()
	: m_resources{ red::PoolEngine() }
	, m_descriptions{ red::PoolString() }
{
}

ResourceListResource::~ResourceListResource() = default;

} // red
