/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "editAction.h"

namespace tools
{

	/// Undo/Redo action wrapper for replace objects action
	class BACKEND_ENGINE_API Action_ReplaceObjects : public IEditAction
	{
	public:
		Action_ReplaceObjects(ObjectHandler& objectHandler, const red::DynArray<EditorObjectIDPath>& objectsToReplace, const red::SharedPtr< IObjectPersistentCollection >& objects, const Bool selectCreatedObjects);

		virtual red::String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

	private:
		ObjectHandler&									m_objectHandler;

		red::SharedPtr< IObjectPersistentCollection >	m_incomingObjects; // what to insert

		red::DynArray< EditorObjectIDPath >				m_objectsPathsToReplace; // list of objects to replace
		red::DynArray< EditorObjectIDPath >				m_createdObjectsPaths; // assigned new object paths
		red::SharedPtr< IObjectPersistentCollection >	m_createdObjects; // objects created

		red::SharedPtr< IObjectPersistentCollection >	m_capturedObjects; // objects deleted
		red::SharedPtr< SelectionState >				m_capturedOldSelection;
		Bool											m_changeSelection;

	};

} // tols