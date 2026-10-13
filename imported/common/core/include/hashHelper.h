/**
* Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redCore/include/crt.h"


// Some helper functions for hashing files and storing the results between sessions
namespace red {

RED_CORE_API THash64 CalculateHash64ForFile( const red::AbsolutePath& path, THash64 hash = HASH64_BASE );
RED_CORE_API THash64 CalculateHash64ForFile( const red::AbsolutePath& path, red::Blob& buffer, THash64 hash = HASH64_BASE );
RED_CORE_API THash64 CalculateHash64ForDirectory( const red::AbsolutePath& path, THash64 hash = HASH64_BASE );
RED_CORE_API THash64 CalculateHash64ForDirectory( const red::AbsolutePath& path, const char* filter, THash64 hash = HASH64_BASE );

RED_CORE_API Bool WriteHash64ToFile( const red::AbsolutePath& destination, THash64 hash );
RED_CORE_API Bool ReadHash64FromFile( const red::AbsolutePath& source, THash64& hash );

} // namespace red
