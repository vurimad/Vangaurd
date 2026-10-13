/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

/// Factory for bitmap texture instances
class CBitmapTextureFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CBitmapTextureFactory );

public:
	CBitmapTextureFactory()
	{
		m_resourceClass = ClassID< CBitmapTexture >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CBitmapTextureFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
