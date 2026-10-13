/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "prefabProxyReconciler.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE(IPrefabProxyReconciler, tools);
RTTI_END_TYPE();

namespace tools
{
	IPrefabProxyReconciler::~IPrefabProxyReconciler() = default;
}

