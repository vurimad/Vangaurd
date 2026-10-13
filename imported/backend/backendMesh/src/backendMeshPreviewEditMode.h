/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/genericEditMode.h"
#include "backendMeshPanelSharedData.h"

namespace tools
{
    struct ClothEditorState;

    class BACKEND_MESH_API MeshPreviewEditMode : public GenericEditMode< MeshPanelSharedData >
	{
        RED_BASE_CLASS(GenericEditMode< MeshPanelSharedData >);
	public:
        MeshPreviewEditMode::MeshPreviewEditMode(const char* editModeName)
            : TBaseClass(editModeName)
        {}

        virtual void OnStateChanged(const ClothEditorState& state) = 0;
    };
}