/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "editorObjectIdPath.h"

namespace tools
{
	/// Used to track last selected node in any editor.
	class RED_REFLECTION_API LastNodeSelection
	{
		RTTI_DECLARE_TYPE( LastNodeSelection );

	public:
		LastNodeSelection();
		LastNodeSelection( const String& editorName, const EditorObjectIDPath& selectedNodeIDPath );
		~LastNodeSelection();

		/// Get name of the editor in which the selection happened. This might be like "editor2".
		RED_INLINE const String& GetEditorName() const { return m_editorName; }

		/// Get the editor object ID path to the selected node.
		RED_INLINE const EditorObjectIDPath& GetSelectedNodeIDPath() const { return m_selectedNodeIDPath; }

		/// Test if the selection is empty.
		RED_INLINE Bool Empty() const { return m_editorName.Empty() || m_selectedNodeIDPath.IsEmpty(); }

		/// Clear the last node selection.
		void Clear();

	private:
		String m_editorName;                       ///< Editor in which the selection happened.
		EditorObjectIDPath m_selectedNodeIDPath;   ///< ID path of selected node.
	};
} // tools
