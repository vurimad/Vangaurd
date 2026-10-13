/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../../common/renderData/include/renderViewportWindowInterface.h"
#include "../../../common/engine/include/externalMessage.h"

namespace rend
{
	class EngineViewportManager;
}
namespace tick
{
	struct Info;
}

namespace input
{
	class IInputContext;
}

namespace tools
{
	class RenderingWindow;
	typedef red::SharedPtr<RenderingWindow> RenderingWindowPtr;

	/// Manager for whole bunch of preview windows
	class BACKEND_ENGINE_API RenderingWindowManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		RenderingWindowManager( rend::EngineViewportManager* engineViewportManager, const MessageSinks& sinks );
		~RenderingWindowManager();

		// Request a new preview window
		// windowContextName : e.g. type of editor / game, used for debug filters
		RenderingWindowPtr RequestPreviewWindow( const Uint64 platformBaseHandle, const String &windowContextName );

		// Begin update and render windows
		void UpdateAndRender( const Float engineTimeDelta, const tick::Info& tickInfo );

		// Process input (active viewport only)
		void ProcessInput( input::IInputContext& context );

		// Process external engine messages (all viewports )
		void ProcessMessages( const red::DynArray<ExternalMessage>& messages );

		// Window is ignored, it doesn't process input and it's not updated
		void SetWindowTickDisabled( const Uint64 viewportID, Bool isDisabled );

	private:
		// engine side viewport manager
		rend::EngineViewportManager*		m_engineViewportManager;
		
		// managed windows
		red::DynArray< RenderingWindow* >		m_windows;			//!< created windows, not owned
		red::DynArray< RenderingWindow* >		m_windowsRemoved;	//!< windows removed from list, not owned
		red::HashSet< Uint64 >					m_windowsDisabled;	//!< disabled windows, not updated
		MessageSinks							m_sinks;			//!< A list of message sinks to attach to created windows

		void PurgeRemovedWindows();
		void RemoveWindow( RenderingWindow* window );
	};

} // tools