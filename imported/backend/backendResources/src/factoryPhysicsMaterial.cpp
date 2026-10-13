/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/physics/include/physicsMaterial.h"

class PhysicsMaterialResourceFactory : public IFactory
{
	RTTI_DECLARE_TYPE(PhysicsMaterialResourceFactory);

public:
	PhysicsMaterialResourceFactory()
	{
		m_resourceClass = ClassID< physics::MaterialResource >();
	}

	Bool DoCreate(THandle< CResource > res, const THandle< FactoryOptions >& options)
	{
		return true;
	}
};

RTTI_BEGIN_TYPE(PhysicsMaterialResourceFactory);
RTTI_PARENT_TYPE(IFactory);
RTTI_END_TYPE();