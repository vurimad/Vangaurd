/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataTypes.h"

//------------------------------------------------------------------------------

RTTI_BEGIN_ENUM_IN_NAMESPACE( DepotLocation, data );
RTTI_ENUM_OPTION( Source );
RTTI_ENUM_OPTION( Cooked );
RTTI_ENUM_OPTION( Output );
RTTI_END_ENUM();

namespace data
{

} // data
