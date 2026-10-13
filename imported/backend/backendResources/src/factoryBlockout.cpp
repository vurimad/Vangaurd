/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/world/include/worldPublic.h"
#include "../../../common/world/include/blockoutResource.h"
#include "../../../common/world/include/blockoutData.h"

/// Factory for the editor world
class CSceneBlockoutFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CSceneBlockoutFactory );

public:
	CSceneBlockoutFactory()
	{
		m_resourceClass = ClassID< world::BlockoutResource >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto blockout = Cast< world::BlockoutResource >( res );

		// create default blockout with one area
		blockout->m_blockoutData = CreateHandle<  world::BlockoutData >( 1 );
		if ( !blockout->m_blockoutData )
		{
			return false;
		}

		blockout->m_blockoutData->GetArea( 0 ).m_name = "area";
		return true;
	}
};

RTTI_BEGIN_TYPE( CSceneBlockoutFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
