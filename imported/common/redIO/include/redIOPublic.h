/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"

#include "../../redSystem/include/redThreadsCommon.h" // for EAsyncResult
#include "../../redSystem/include/utility.h"
#include "../../redSystem/include/log.h"


#include "redIOApi.h"
#include "redIOCommon.h"
#include "redIOAsyncReadToken.h"
#include "redIOFile.h"

namespace io
{

extern REDIO_API Bool Initialize( const InitSetup& initSetup = InitSetup{} );
extern REDIO_API void Shutdown();

}
