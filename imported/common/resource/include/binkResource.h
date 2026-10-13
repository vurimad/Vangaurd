/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/stringRTTI.h"

// IMPORTANT: It is not regular Bink resource. The main purpose of this class is to be able to use bink as regular resource in the editor.
// PLEASE DO NOT USE IT FOR LOADING BINK VIDEOS.
class RED_RESOURCE_API Bink final : public CResource
{
	RTTI_DECLARE_TYPE( Bink );
	IMPLEMENT_RESOURCE_INTERFACE( "bk2", "bk2", "Bink" );
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	Bink();

	// Empty implementation, because Bink is never intended to be loaded through the regular resource loading mechanism.
	virtual void OnSerialize( IFile &file ) override;
};