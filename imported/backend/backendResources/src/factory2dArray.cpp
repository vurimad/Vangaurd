/**
* Copyright (c) 2009 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/resource/include/2darray.h"


/// Factory for 2d array
class C2dArrayFactory : public IFactory
{
	RTTI_DECLARE_TYPE( C2dArrayFactory );

public:
	C2dArrayFactory()
	{
		m_resourceClass = ClassID< C2dArray >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		auto arr = Cast< C2dArray >( res );
		arr->AddColumn( "Index", String::EMPTY() );
		arr->AddRow();
		return true;
	}
};

RTTI_BEGIN_TYPE( C2dArrayFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
