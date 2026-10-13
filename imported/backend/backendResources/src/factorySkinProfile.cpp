/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/skinProfile.h"

class CSkinProfileFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CSkinProfileFactory );

public:
	CSkinProfileFactory()
	{
		m_resourceClass = ClassID< CSkinProfile >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto skinProfile = Cast< CSkinProfile >( res );
		return skinProfile != nullptr;
	}
};

RTTI_BEGIN_TYPE( CSkinProfileFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
