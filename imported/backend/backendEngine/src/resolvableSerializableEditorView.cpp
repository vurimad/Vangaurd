/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resolvableSerializableEditorView.h"
#include "objectHandler.h"

namespace tools
{

	namespace helper
	{
		const rtti::ClassType* FindViewClass( const rtti::ClassType* viewClass, const ObjectHandler& objectHandler, const EditorObjectIDPath& path )
		{
			if ( viewClass )
			{
				return viewClass;
			}

			const THandle< ISerializable > object = objectHandler.ResolveObject( path );
			RED_FATAL_ASSERT( object, "Failed to resolve object" );
			return object->GetClass();
		}
	}

	ResolvableSerializableEditorView::ResolvableSerializableEditorView( const ObjectHandler& objectHandler, const EditorObjectIDPath& path,
		const EditableViewPtr& parentView, const Bool isReadOnly, const rtti::ClassType* viewClass, const THandle<ISerializable>& userContext )
		: ISerializableEditorView( parentView, isReadOnly, helper::FindViewClass( viewClass, objectHandler, path ), userContext )
		, m_objectHandler( objectHandler )
		, m_path( path )
	{
		RED_FATAL_ASSERT( m_path.IsValid(), "Invalid object path" );
	}

	THandle< ISerializable > ResolvableSerializableEditorView::GetObject() const
	{
		return m_objectHandler.ResolveObject( m_path );
	}

} // tools
