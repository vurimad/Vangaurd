/**
 * Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/world/include/worldPublic.h"
#include "../../../common/world/include/worldWorld.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/worldEnvironment/include/environmentDefinition.h"

/// Factory for the editor world
class CSceneWorldFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CSceneWorldFactory );

public:
	CSceneWorldFactory()
	{
		m_resourceClass = ClassID< world::World >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto world = Cast< world::World >( res );

		// TODO: create blockout resource

		return true;
	}
};

RTTI_BEGIN_TYPE( CSceneWorldFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
