/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/hairProfile.h"

class CHairProfileFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CHairProfileFactory );

public:
	CHairProfileFactory()
	{
		m_resourceClass = ClassID< CHairProfile >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto profile = Cast< CHairProfile >( res );
		return profile != nullptr;
	}
};

RTTI_BEGIN_TYPE( CHairProfileFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
