/**
* Copyright © 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#define CONCATENATE(x,y) x##y

#define CONCATENATE2(x,y) CONCATENATE(x,y)

//FIXME>>>>>: On Clang __COUNTER__ is unique per translation unit. So for our current use, we need to make variables declared with it static in the .cpp
#define UNIQUE_NAME(prefix) CONCATENATE2(prefix,__COUNTER__)
