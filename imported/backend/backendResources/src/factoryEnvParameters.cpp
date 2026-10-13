/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/worldEnvironment/include/environmentAreaParameters.h"

/// Factory for area environment parameters
class CEnvironmentParametersFactory : public IFactory
{
	RTTI_DECLARE_TYPE(CEnvironmentParametersFactory);

public:
	CEnvironmentParametersFactory()
	{
		m_resourceClass = ClassID< world::EnvironmentAreaParameters >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto env = Cast< world::EnvironmentAreaParameters >( res );
		if( env )
		{
			env->Reset();
			return true;
		}
		return false;
	}
};

RTTI_BEGIN_TYPE( CEnvironmentParametersFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
