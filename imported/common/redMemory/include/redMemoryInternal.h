/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_MEMORY_INTERNAL_H_
#define _RED_MEMORY_MEMORY_INTERNAL_H_

#ifndef _HAS_EXCEPTIONS 
#define _HAS_EXCEPTIONS 0
#endif // !_HAS_EXCEPTIONS 

#include "../../redSystem/include/redSystemPublic.h"

#include <cstddef>

#include <algorithm>
#include <new>

RED_DISABLE_WARNING_MSC( 4251 ) // warning C4251: 'X: class 'Y' needs to have dll-interface to be used by clients of class 'Z'
RED_DISABLE_WARNING_MSC( 4275 ) // warning C4275: non dll-interface class 'X' used as base for dll-interface class 'Y'

#include "redMemoryApi.h"
#include "settings.h"
#include "types.h"
#include "poolRoot.h"
#include "serializer.h"
#include "deserializer.h"
#include "allocatorIdentifiersSerializer.h"
#include "simpleArray.h"

#endif
