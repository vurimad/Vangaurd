/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redFileSystem/include/system.h"

#if defined( RED_FILE_SYNC_SERVICE_ENABLED )

#include "resourceEditorDepotImpl.h"

namespace res
{
namespace prv
{
// Resource editor depot modified to work with (multiplayer) file sync
class RED_RESOURCE_API FileSyncResourceEditorDepot : public ResourceEditorDepot
{
public:
	FileSyncResourceEditorDepot( const DepotInitInfo& initInfo );
	red::SharedPtr< serialization::IAsyncSource > CreateResourceAsyncSource( const res::ResourcePath resourcePath, const res::CreateResourceAsyncSourceParams& params ) override;
};
} // prv
} // res

#endif // RED_FILE_SYNC_SERVICE_ENABLED