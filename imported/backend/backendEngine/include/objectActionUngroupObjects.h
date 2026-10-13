/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editAction.h"
#include "objectHandler.h"

namespace tools
{
	class SelectionHandler;
	class SelectionState;

	/// Undo/Redo action wrapper for object ungrouping
	class BACKEND_ENGINE_API Action_UngroupObjects : public IEditAction
	{
	public:
		Action_UngroupObjects( ObjectHandler& objectHandler, const red::ArraySpan< const EditorObjectIDPath >& paths, const red::DynArray< Uint64 >& groupIds );
		~Action_UngroupObjects();

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

	private:
		ObjectHandler& m_objectHandler;
		red::DynArray< EditorObjectIDPath > m_paths;
		red::DynArray< Uint64 > m_groupIds;
	};

} // tols