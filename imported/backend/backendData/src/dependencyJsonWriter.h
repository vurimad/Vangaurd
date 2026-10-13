/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dependencyWriter.h"

namespace dependency
{

WriterScope CreateJsonWriter( const red::AbsolutePath& path );

} // dependency
