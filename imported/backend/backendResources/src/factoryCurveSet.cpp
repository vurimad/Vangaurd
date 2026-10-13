/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/engine/include/curveSetResource.h"

class CurveSetFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CurveSetFactory );

public:
	CurveSetFactory()
	{
		m_resourceClass = ClassID< CurveSet >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CurveSetFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
