/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "distantLightResource.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

RTTI_BEGIN_TYPE( CDistantLightsResource );
{
	RTTI_PARENT_TYPE( res::StreamedResource );

	RTTI_PROPERTY( m_data ).readOnly();
}
RTTI_END_TYPE();

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

CDistantLightsResource::CDistantLightsResource()
{

}

CDistantLightsResource::~CDistantLightsResource()
{

}

THandle< CDistantLightsResource > CDistantLightsResource::Create( red::ArraySpan< distantLight::Entry > lights )
{
	if( lights.Empty() )
	{
		return nullptr;
	}

	THandle< CDistantLightsResource > res( RED_NEW( CDistantLightsResource ) );

	res->m_data = DataBuffer( lights.SizeInBytes() );

	red::Memcpy( res->m_data.Data(), lights.Data(), lights.SizeInBytes() );

	return res;
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
