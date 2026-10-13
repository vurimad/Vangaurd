/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"

/// Factory for material instances
class CMaterialInstanceFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CMaterialInstanceFactory );

public:
	CMaterialInstanceFactory()
	{
		m_resourceClass = ClassID< CMaterialInstance >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CMaterialInstanceFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
