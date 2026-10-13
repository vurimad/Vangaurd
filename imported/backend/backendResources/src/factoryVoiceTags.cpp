/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendEngine/include/factory.h"
#include "../../../common/soundMetadata/include/localizationMetadata.h"

class VoiceTagsFactory : public IFactory
{
	RTTI_DECLARE_TYPE( VoiceTagsFactory );

public:
	VoiceTagsFactory()
	{
		m_resourceClass = ClassID< loc::VoiceTagListResource >();
	}

	virtual Bool DoCreate( THandle< CResource > res, const THandle< FactoryOptions >& options ) override
	{
		return true;
	}
};

RTTI_BEGIN_TYPE( VoiceTagsFactory );
	RTTI_PARENT_TYPE( IFactory );
RTTI_END_TYPE();
