/**
* Copyright (c) 2012 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/textureArray.h"


/// Factory for texture array instances
class CTextureArrayFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CTextureArrayFactory );

public:
	CTextureArrayFactory()
	{
		m_resourceClass = ClassID< CTextureArray >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto texture = Cast< CTextureArray >( res );
		return static_cast< Bool >( texture );
	}
};

RTTI_BEGIN_TYPE( CTextureArrayFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
