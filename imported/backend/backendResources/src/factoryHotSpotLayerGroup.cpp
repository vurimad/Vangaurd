/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/factory.h"


//////////////////////////////////////////////////////////////////////////
// CHotSpotLayerGroupFactory
//////////////////////////////////////////////////////////////////////////
#if 0
static_assert( std::is_base_of< CResource, game::interactions::HotSpotLayerGroup >::value, "Not a resource class" );
class HotSpotLayerGroupFactory : public IFactory
{
	RTTI_DECLARE_TYPE(HotSpotLayerGroupFactory);

public:
	HotSpotLayerGroupFactory::HotSpotLayerGroupFactory()
	{
		m_resourceClass = ClassID< game::interactions::HotSpotLayerGroup >();
	}

	Bool DoCreate(THandle< CResource > res, const THandle< FactoryOptions >& options)
	{
		return true;
	}
};

RTTI_BEGIN_TYPE(HotSpotLayerGroupFactory);
RTTI_PARENT_TYPE(IFactory);
RTTI_END_TYPE();
#else
RED_NO_EMPTY_FILE();
#endif