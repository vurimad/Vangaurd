/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{
	class IPreviewEditMode;
	struct EditModeViewport;
	class IPreviewInputListener;
	class SelectionInputListener;
	class PreviewPanel;
	class PreviewPanelSharedData;
	class GenericPreviewPanelSharedData;
	class IPreviewInputHandler;
	class IPreviewDragDropHandler;
	typedef red::SharedPtr<GenericPreviewPanelSharedData> GenericPreviewPanelSharedDataPtr;
} // tools
