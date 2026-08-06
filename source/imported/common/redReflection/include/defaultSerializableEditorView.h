/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "serializableEditorView.h"

namespace tools
{
	// Basic ISerializableEditorView implementation - the edited object is kept always via handle
	class RED_REFLECTION_API DefaultSerializableEditorView : public ISerializableEditorView
	{
	public:
		DefaultSerializableEditorView( const EditableViewPtr& parentView, const THandle< ISerializable >& object, const Bool isReadOnly, const rtti::ClassType* viewClass = nullptr,
			const THandle<ISerializable>& userContext = nullptr );

	private:
		// ISerializableEditorView interface
		virtual THandle< ISerializable > GetObject() const override final;

		// object access
		THandle< ISerializable > m_object;
	};

} // tools