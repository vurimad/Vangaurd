/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "selectionFwd.h"
#include "editAction.h"

namespace tools
{

	/// Undo/Redo action wrapper for pasting objects
	class BACKEND_ENGINE_API Action_PasteObjects : public IEditAction
	{
	public:
		Action_PasteObjects( ObjectHandler& objectHandler, const red::SharedPtr< IObjectPersistentCollection >& objects, const ObjectPlacementInfo& placement, const Bool selectCreatedObjects );

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

		const red::SharedPtr< IObjectPersistentCollection >& GetCapturedObjects() const { return m_objects; }

	private:
		ObjectHandler& m_objectHandler;
		red::SharedPtr< IObjectPersistentCollection > m_objects;
		red::SharedPtr< SelectionState > m_capturedOldSelection;
		red::DynArray< EditorObjectIDPath > m_objectPaths;
		ObjectPlacementInfo m_placementInfo;
		Bool m_changeSelection;
		Bool m_wasPasted;
	};

} // tols