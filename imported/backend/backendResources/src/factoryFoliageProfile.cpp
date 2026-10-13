/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/foliageProfile.h"

class CFoliageProfileFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CFoliageProfileFactory );

public:
	CFoliageProfileFactory()
	{
		m_resourceClass = ClassID< CFoliageProfile >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto foliageProfile = Cast< CFoliageProfile >( res );
		return foliageProfile != nullptr;
	}
};

RTTI_BEGIN_TYPE( CFoliageProfileFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
