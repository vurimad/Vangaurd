/**
* Copyright (c) 2009 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/renderData/include/font.h"

class FontFactory : public IFactory
{
	RTTI_DECLARE_TYPE( FontFactory );

public:
	FontFactory()
	{
		m_resourceClass = ClassID< rend::Font >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( FontFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
