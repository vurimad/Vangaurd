/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redMemory/include/sharedPtr.h"
#include "../../../common/redMemory/include/weakPtr.h"

namespace tools
{
	class IResourceEditor;
	typedef red::SharedPtr< IResourceEditor > ResourceEditorPtr;
	typedef red::WeakPtr< IResourceEditor > ResourceEditorWeakPtr;

	class ResourceEditorCreator;
	typedef red::SharedPtr< ResourceEditorCreator > ResourceEditorCreatorPtr;
}
