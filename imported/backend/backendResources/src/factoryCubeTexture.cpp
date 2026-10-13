/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"

/// Factory for cubemaps instances
class CCubeTextureFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CCubeTextureFactory );

public:
	CCubeTextureFactory()
	{
		m_resourceClass = ClassID< CCubeTexture >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CCubeTextureFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
