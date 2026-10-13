/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editAction.h"
#include "objectHandler.h"

namespace tools
{
	class SelectionHandler;
	class SelectionState;

	/// Undo/Redo action wrapper for object deletion
	class BACKEND_ENGINE_API Action_DeleteObjects : public IEditAction
	{
	public:
		Action_DeleteObjects( ObjectHandler& objectHandler, const red::ArraySpan< const tools::EditorObjectIDPath >& paths );
		~Action_DeleteObjects();

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

	private:
		ObjectHandler& m_objectHandler;
		red::DynArray< tools::EditorObjectIDPath > m_paths;

		red::SharedPtr< IObjectPersistentCollection > m_capturedObjects;
		red::SharedPtr< SelectionState > m_capturedOldSelection;
	};

} // tols