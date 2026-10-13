/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "editAction.h"
#include "objectFwd.h"
#include "selectionFwd.h"

namespace tools
{

	/// Undo/Redo action wrapper for object creation
	class BACKEND_ENGINE_API Action_CreateObject : public IEditAction
	{
	public:
		Action_CreateObject( ObjectHandler& objectHandler, const red::DynArray<ObjectCreationInfo>& initInfos, const String& desc, Bool refreshSelection = false );
		virtual ~Action_CreateObject() override;

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

	private:
		ObjectHandler& m_objectHandler;
		red::DynArray<ObjectCreationInfo> m_infos;
		const String m_desc; // fluff
		const Bool m_refreshSelection;
		red::SharedPtr< IObjectPersistentCollection > m_createdObjects;
		const SelectionStatePtr m_capturedOldSelection;
		SelectionStatePtr m_newSelection;
		red::DynArray< EditorObjectIDPath > m_createdObjectPaths;
	};

} // tols