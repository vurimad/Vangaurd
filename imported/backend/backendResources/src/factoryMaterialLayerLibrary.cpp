/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"

/// Factory for material layer library
class CMatlibFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CMatlibFactory );

public:
	CMatlibFactory()
	{
		m_resourceClass = ClassID< CMaterialLayerLibrary >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CMatlibFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
