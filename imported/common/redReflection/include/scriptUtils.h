/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "resourcePath.h"

#ifndef RED_CONFIGURATION_FINAL
RED_REFLECTION_API red::DynArray< res::ResourcePath > GetResourcePathsFromScriptsBlob( const red::AbsolutePath& scriptBlobPath );
#endif