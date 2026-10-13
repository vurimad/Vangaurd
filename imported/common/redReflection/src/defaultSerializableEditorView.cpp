/**
* Copyright (c) 2007-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "defaultSerializableEditorView.h"

namespace tools
{
	DefaultSerializableEditorView::DefaultSerializableEditorView( const EditableViewPtr& parentView, const THandle< ISerializable >& object, const Bool isReadOnly, const rtti::ClassType* viewClass,
		const THandle<ISerializable>& userContext )
		: ISerializableEditorView( parentView, isReadOnly, viewClass, userContext )
		, m_object( object )
	{
		RED_FATAL_ASSERT( m_object, "Invalid object" );
	}

	THandle< ISerializable > DefaultSerializableEditorView::GetObject() const
	{
		return m_object;
	}

} // tools
