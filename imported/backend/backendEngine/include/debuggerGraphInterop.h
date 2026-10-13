/**
* Copyright (c) 2017-2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/interop/include/interopBase.h"
#include "resourceEditorFwd.h"

class CResource;

namespace tools
{
	class IGraphDescriptor;
	class GraphDescriptorEditor;
	class EditorGraphInterop;
	class ObjectCollection;

	struct BACKEND_ENGINE_API DebuggerGraphInteropContext
	{
		DebuggerGraphInteropContext();
		virtual ~DebuggerGraphInteropContext();

		ResourceEditorPtr m_resourceEditor;
		THandle< CResource > m_resource;
		THandle< tools::IGraphDescriptor > m_graphDescriptor;
		tools::ObjectCollection* m_sharedObjectCollection;
		THandle< ISerializable > m_userContext;
	};

	// Simple wrapper for graph editor and its interop version (along with all required data).
	// It allows to expose interop interface for a graph for debug (visualization) purposes.
	// Important: this class assumes that backend data for the resource is already initialized
	// (global data is propagated, etc.), which means that resource was "stolen" from opened editor.
	class BACKEND_ENGINE_API DebuggerGraphInterop
	{
	public:
		DebuggerGraphInterop();
		virtual ~DebuggerGraphInterop();

		interop::ObjectPtr AsInteropObject();
		virtual Bool Init( const DebuggerGraphInteropContext& ctx );

		RED_INLINE tools::GraphDescriptorEditor* GetGraphEditor() const { return m_editorGraph.Get(); }

	protected:
		red::SharedPtr< tools::GraphDescriptorEditor > m_editorGraph;
		red::SharedPtr< tools::EditorGraphInterop > m_editorGraphInterop;
	};

} // tools