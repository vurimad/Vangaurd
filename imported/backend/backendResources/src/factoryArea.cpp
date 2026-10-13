/**
 * Copyright © 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../../common/redReflection/include/factory.h"
#include "../../../common/world/include/worldPublic.h"

/// Factory for the editor world
class CSceneAreaFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CSceneAreaFactory );

public:
	CSceneAreaFactory()
	{
		m_resourceClass = ClassID< world::Area >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const FactoryOptions& options ) override
	{
		auto area = Cast< world::Area >( res );
		area->Initialize();
		return true;
	}
};

RTTI_BEGIN_TYPE( CSceneAreaFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
