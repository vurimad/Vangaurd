/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "objectFwd.h"
#include "editAction.h"

namespace tools
{

	// Undo/Redo action wrapper for generic object property change
	// Note: Similar to Action_SetPropertyValue, but does not require resolved view and finds and object by EditorObjectIDPath
	class BACKEND_ENGINE_API Action_ChangeProperty : public IEditAction
	{
	public:
		Action_ChangeProperty( ObjectHandler& objectHandler, const EditorObjectIDPath& objectPath, const rtti::AccessPath& propertyPath, const rtti::ValuePtr& newValue );

		virtual String GetDescription() const override final;
		virtual Bool Do() override final;
		virtual Bool Undo() override final;
		virtual Bool IsModification() const override final;
		virtual void GetAffectedResources( red::Set< res::ResourcePath >& outResourcePaths ) const override final;

	private:
		Bool WriteValue( const rtti::ValuePtr& value ) const;

		ObjectHandler& m_objectHandler;
		const EditorObjectIDPath m_objectPath;
		const rtti::AccessPath m_propertyPath;
		const rtti::ValuePtr m_newValue;
		rtti::ValuePtr m_oldValue;
	};

} // tols