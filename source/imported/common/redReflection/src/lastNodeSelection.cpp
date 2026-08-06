/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "lastNodeSelection.h"
#include "rttiClassBuilder.h"

using red::String;
using red::DynArray;


RTTI_BEGIN_TYPE_IN_NAMESPACE( LastNodeSelection, tools );
	RTTI_PROPERTY( m_editorName );
	RTTI_PROPERTY( m_selectedNodeIDPath );
RTTI_END_TYPE();


namespace tools
{
	LastNodeSelection::LastNodeSelection()
	{
	}

	LastNodeSelection::LastNodeSelection( const String& editorName, const EditorObjectIDPath& selectedNodeIDPath )
		: m_editorName( editorName )
		, m_selectedNodeIDPath( selectedNodeIDPath )
	{
	}

	LastNodeSelection::~LastNodeSelection()
	{
	}

	void LastNodeSelection::Clear()
	{
		m_editorName.Clear();
		m_selectedNodeIDPath = EditorObjectIDPath();
	}

} // tools
