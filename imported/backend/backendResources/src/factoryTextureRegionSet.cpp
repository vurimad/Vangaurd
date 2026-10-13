/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resourceMaterial/include/textureRegionSet.h"

class CTextureRegionSetFactory : public IFactory
{
	RTTI_DECLARE_TYPE( CTextureRegionSetFactory );

public:
	CTextureRegionSetFactory()
	{
		m_resourceClass = ClassID< CTextureRegionSet >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( CTextureRegionSetFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
