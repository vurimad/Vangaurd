/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "editableCollectionCustomPropertyEditor.h"

namespace tools
{
	namespace prv
	{

		// Bone selector - drop down list with hierarchy of bones
		class EditControlCategorisedResource final : public tools::IEditableCustomProperty
		{
			RTTI_DECLARE_TYPE( EditControlCategorisedResource );

		public:
			EditControlCategorisedResource();

			virtual const Bool HandlesCustomType( const CName customTypeName, const EditableCollectionRootItem* root ) const override;
			virtual CName GetControlType() const override;
		};
	}
}
