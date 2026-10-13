/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/redReflection/include/dlcManifest.h"


class DlcManifestFactory: public IFactory
{
	RTTI_DECLARE_TYPE( DlcManifestFactory );

public:
	DlcManifestFactory()
	{
		m_resourceClass = ClassID< res::DlcManifest >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( DlcManifestFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
