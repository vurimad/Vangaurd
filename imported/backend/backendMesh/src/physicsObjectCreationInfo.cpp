/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "physicsObjectCreationInfo.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( SPhysicsBaseCreationInfo, tools );
RTTI_PROPERTY( m_type ).editable();
RTTI_PROPERTY( m_data ).editable();
RTTI_PROPERTY( m_isObstacle ).editable();
RTTI_PROPERTY( m_vertexLimit ).editable();
RTTI_PROPERTY( m_selectedVertices ).editable();
RTTI_END_TYPE();