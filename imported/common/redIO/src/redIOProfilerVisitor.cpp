/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redIOProfilerVisitor.h"

#ifdef RED_PLATFORM_ORBIS

namespace io
{
namespace orbis
{

IProfileStreamVisitor::IProfileStreamVisitor()
{
}

IProfileStreamVisitor::~IProfileStreamVisitor()
{
}

} // orbis
} // io

#else
RED_NO_EMPTY_FILE();
#endif // RED_PLATFORM_ORBIS
