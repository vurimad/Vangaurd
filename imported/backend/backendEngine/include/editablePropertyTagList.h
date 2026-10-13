/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"

namespace tools
{
	class TagListEditableCustomProperty : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( TagListEditableCustomProperty );

	public:
		TagListEditableCustomProperty();

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override;
		virtual CName GetControlType() const override;

		virtual Bool HasUnlistedChildren() const override;
		virtual interop::ObjectPtr GetUnlistedChild( interop::IContext& ctx, const String& name ) const override;

		virtual Bool IsCompound() const { return true; }

	private:
		void AddTag( interop::IContext& ctx, const CName tagName );
		void RemoveTag( interop::IContext& ctx, const CName tagName );
	};
}