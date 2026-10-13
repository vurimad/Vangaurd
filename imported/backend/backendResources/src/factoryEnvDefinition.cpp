/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/factory.h"
#include "../../../common/worldEnvironment/include/environmentDefinition.h"

/// Factory for environment definitions
class CEnvironmentDefinitionFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CEnvironmentDefinitionFactory );

public:
	CEnvironmentDefinitionFactory()
	{
		m_resourceClass = ClassID< world::EnvironmentDefinition >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto env = Cast< world::EnvironmentDefinition >( res );
		if( env )
		{
			return true;
		}
		return false;
	}
};

RTTI_BEGIN_TYPE( CEnvironmentDefinitionFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
