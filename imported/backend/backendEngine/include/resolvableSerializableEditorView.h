/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redReflection/include/serializableEditorView.h"
#include "../../../common/redReflection/include/editorObjectIdPath.h"

namespace tools
{
	class ObjectHandler;

	// An editable view where edited object is resolved using editor object ID path every access
	class BACKEND_ENGINE_API ResolvableSerializableEditorView : public ISerializableEditorView
	{
	public:
		ResolvableSerializableEditorView( const ObjectHandler& objectHandler, const EditorObjectIDPath& path,
			const EditableViewPtr& parentView, const Bool isReadOnly, const rtti::ClassType* viewClass = nullptr, const THandle<ISerializable>& userContext = nullptr );

	private:
		// ISerializableEditorView interface
		virtual THandle< ISerializable > GetObject() const override final;

		// object access
		const ObjectHandler& m_objectHandler;
		const EditorObjectIDPath m_path;
	};

} // tools