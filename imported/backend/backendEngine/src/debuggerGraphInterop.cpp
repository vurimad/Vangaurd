/**
* Copyright (c) 2017-2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debuggerGraphInterop.h"
#include "editorGraphInterop.h"
#include "graphDescriptorEditor.h"
#include "resourceEditor.h"

namespace tools
{
	DebuggerGraphInteropContext::DebuggerGraphInteropContext() = default;

	DebuggerGraphInteropContext::~DebuggerGraphInteropContext() = default;

	//////////////////////////////////////////////////////////////////////////

	DebuggerGraphInterop::DebuggerGraphInterop()
	{
	}

	DebuggerGraphInterop::~DebuggerGraphInterop()
	{
	}

	interop::ObjectPtr DebuggerGraphInterop::AsInteropObject()
	{
		return m_editorGraphInterop->CreateInterop();
	}

	Bool DebuggerGraphInterop::Init( const DebuggerGraphInteropContext& ctx )
	{
		m_editorGraph = red::CreateSharedPtr< tools::GraphDescriptorEditor >( nullptr );
		if ( !m_editorGraph->Initialize( ctx.m_resource, ctx.m_graphDescriptor ) )
		{
			return false;
		}

		tools::EditorGraphInteropCreationInfo info 
		{
			ctx.m_resourceEditor,
			m_editorGraph,
			ctx.m_sharedObjectCollection
		};

		m_editorGraphInterop.Reset( RED_NEW( tools::EditorGraphInterop )( info ) );
		m_editorGraphInterop->SetUserContext( ctx.m_userContext );

		return true;
	}

} // tools