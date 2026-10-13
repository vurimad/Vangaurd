/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace tools
{

	class ObjectCollection;
	class IObjectClipboard;

	class ObjectHandler;
	class IObjectResolver;

	class IObjectPreview;
	struct ObjectCreationInfo;
	struct ObjectPlacementInfo;
	class ObjectDragDropListener;

	class IObjectCapturedCollection;
	class IObjectPersistentCollection;
	typedef red::SharedPtr< IObjectPersistentCollection > ObjectPersistentCollectionPtr;
	class ObjectSerializablePersistentCollection;

} // tools