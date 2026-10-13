#include "build.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/renderBackend/typeRegistryFuncImpl.h"

RED_MODULE(renderBackend)
{
#ifdef RED_PLATFORM_WINPC
	RegisterRenderBackendRTTI();
#endif
}
