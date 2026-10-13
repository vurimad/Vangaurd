/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/engine/include/curveResourceSetResource.h"

class CurveResourceSetFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CurveResourceSetFactory );

public:
	CurveResourceSetFactory()
	{
		m_resourceClass = ClassID< CurveResourceSet >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CurveResourceSetFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
