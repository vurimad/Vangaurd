/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/gradient.h"

class CGradientFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CGradientFactory );

public:
	CGradientFactory()
	{
		m_resourceClass = ClassID< CGradient >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto profile = Cast< CGradient >( res );
		return profile != nullptr;
	}
};

RTTI_BEGIN_TYPE( CGradientFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
