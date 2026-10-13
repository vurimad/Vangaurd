/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "visibilityOccluderResource.h"
#include "visibilityOccluderInstance.h"


RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IOccluderResource, vis )
{
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_resourceHash );
}
RTTI_END_TYPE();


namespace vis
{

}
