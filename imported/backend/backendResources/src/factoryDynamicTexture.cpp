/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/dynamicTexture.h"

class DynamicTextureFactory : public IFactory
{
	RTTI_DECLARE_TYPE( DynamicTextureFactory );

public:
	DynamicTextureFactory()
	{
		m_resourceClass = ClassID< DynamicTexture >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( DynamicTextureFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
