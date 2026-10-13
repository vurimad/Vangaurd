/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../backendEngine/include/editableCollectionCustomPropertyEditor.h"


namespace tools
{
	class CustomPropertyEditor_EMMMode : public IEditableCustomProperty
	{
		RTTI_DECLARE_TYPE( CustomPropertyEditor_EMMMode );

	public:
		CustomPropertyEditor_EMMMode();

		virtual const Bool HandlesCustomType( const CName customTypeName, const prv::EditableCollectionRootItem* root ) const override final;
		virtual Bool HasOptions() const override final;
		virtual const red::DynArray< String >& GetOptions() const override final;
		virtual CName GetControlType() const override final;

	private:
		red::DynArray< String > m_emmModes;
	};
} 


