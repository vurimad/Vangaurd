/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyReader.h"

namespace dependency
{

ReaderScope CreateJsonReader( const red::AbsolutePath& path );

} // dependency
