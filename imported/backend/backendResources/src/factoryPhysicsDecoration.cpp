/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/engine/include/physicsDecorationResource.h"

//////////////////////////////////////////////////////////////////////////
/// Factory for the physics system resource
//////////////////////////////////////////////////////////////////////////

class CPhysicsSystemResourceFactory : public IFactory
{
	RTTI_DECLARE_TYPE(CPhysicsSystemResourceFactory);

public:
	CPhysicsSystemResourceFactory()
	{
		m_resourceClass = ClassID< CPhysicsDecorationResource >();
	}

	virtual Bool DoCreate(THandle< CResource > res, const THandle< FactoryOptions >& options) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE(CPhysicsSystemResourceFactory)
RTTI_PARENT_TYPE(IFactory)
RTTI_END_TYPE()