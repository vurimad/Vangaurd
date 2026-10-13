/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"

class IBackendData;
typedef THandle< IBackendData > IBackendDataPtr;

// base class for all backend only data that are being cooked away and not used in final game
class RED_REFLECTION_API IBackendData : public ISerializable
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

	RTTI_DECLARE_TYPE( IBackendData )
public:
	virtual void OnSerialize( IFile& file ) override;
};