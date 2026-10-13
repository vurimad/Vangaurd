/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderingWindow.h"
#include "renderingWindowManager.h"

#include "../../../common/engine/include/renderEngineViewport.h"
#include "../../../common/input/include/inputContext.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../../common/renderData/include/renderCommandHandler.h"

namespace tools
{
	namespace utils
	{
		// THIS IS TEMPORARY - will be removed from here
		void GetSizeForViewport( Uint64& inOutHandle, Uint32& outWidth, Uint32& outHeight )
		{
#ifdef RED_PLATFORM_WINPC
			if ( inOutHandle != 0 && ::IsWindow((HWND) inOutHandle) )
			{
				RECT rect;
				::GetClientRect( (HWND)inOutHandle, &rect );

				outWidth = rect.right - rect.left;
				outHeight = rect.bottom - rect.top;
				return;
			}
#endif

			inOutHandle = 0;
			outWidth = 800;
			outHeight = 600;
		}
	}

	RenderingWindowManager::RenderingWindowManager( rend::EngineViewportManager* engineViewportManager, const MessageSinks& sinks )
		: m_engineViewportManager( engineViewportManager )
		, m_sinks(sinks)
	{
	}

	RenderingWindowManager::~RenderingWindowManager()
	{
		PurgeRemovedWindows();

		for ( auto* window : m_windows )
			window->m_externalCloseFunc = nullptr;

		m_windows.Clear();
	}

	RenderingWindowPtr RenderingWindowManager::RequestPreviewWindow( const Uint64 platformBaseHandle, const String &windowContextName )
	{
		// create engine viewport
		auto engineViewport = m_engineViewportManager->CreateEngineViewport( platformBaseHandle, windowContextName );
		if ( !engineViewport )
		{
			RED_LOG_ERROR( "Engine: Failed to create new rendering window" );
			return RenderingWindowPtr();
		}

		// create new preview window
		auto window = RED_NEW( RenderingWindow )( engineViewport, [this](RenderingWindow* window) { RemoveWindow(window); } );
		m_windows.PushBack( window );

		for( auto* sink : m_sinks )
		{
			window->GetEngineViewport()->AttachViewportSink(sink);
			sink->OnAttach(reinterpret_cast<void*>(window->GetViewportID()));
		}

		// return the window handle - it's an unique pointer that the calling side is responsible for
		return RenderingWindowPtr( window );
	}

	void RenderingWindowManager::PurgeRemovedWindows()
	{
		auto windowsToRemove = std::move(m_windowsRemoved);
		for ( auto* it : windowsToRemove )
			m_windows.Remove( it );
	}

	void RenderingWindowManager::RemoveWindow( RenderingWindow* window )
	{
		m_windowsRemoved.PushBack( window );
		m_windowsDisabled.Remove( window->GetViewportID() );
	}

	void RenderingWindowManager::UpdateAndRender( const Float engineTimeDelta, const tick::Info& tickInfo )
	{
		// ctremblay: Hack for now. If there is no window open, sleep. Else, app will take a full core. This need to be revisited when we move to our own UI framework. 
		if( m_windows.Empty() )
		{
			red::SleepOnCurrentThread( 100 );
		}

		red::DynArray<RenderingWindow::RenderInfo> renderInfos{ red::PoolFrame() };
		renderInfos.Reserve( m_windows.Size() );

		for ( auto* window : m_windows )
		{
			// Filter out windows that closed
			if ( !m_windowsRemoved.Exist( window ) && !m_windowsDisabled.Exist( window->GetViewportID() ) )
			{
				renderInfos.PushBack( window->UpdateAndRender( engineTimeDelta, tickInfo ) );
			}
		}

        {
            PC_SCOPE( RenderingWindowManager_SubmitRenders );

            GRender->GetCommands()->FlushPreviousFrameCommandsProcessing();

            red::DynArray<TRenderPtr<IRenderScene> > scenesForUpdate{ red::PoolFrame() };
            for( auto& info : renderInfos )
            {
                GatherScenesForUpdate( &scenesForUpdate, info.frameInfo );
            }

			GRender->GetCommands()->FrameTick( scenesForUpdate );

			for ( auto& info : renderInfos )
			{
				info.viewport->SubmitFrame( info.frameInfo, info.cameraInfo );
			}
		}

		// cleanup removed windows
		{
			PC_SCOPE( RenderingWindowManager_PurgeRemovedWindows );
			PurgeRemovedWindows();
		}
	}

	namespace helper
	{
		class PerViewportInputContext : public input::IInputContext
		{
		public:
			input::InputCollection m_bufferedInputData;
			input::IInputContext&  m_parent;

			PerViewportInputContext( input::IInputContext& parentContext, input::ViewportID viewportId )
				: m_parent( parentContext )
			{
				m_bufferedInputData = input::GetSubDataByViewportID(m_parent.GetBufferedInputData(), viewportId);
			}

			~PerViewportInputContext() = default;

			input::InputCollection& GetBufferedInputData() override final
			{
				return m_bufferedInputData;
			}

            void FlushConsumedEvents() override final
            {
                input::FlushConsumedEvents(m_bufferedInputData);
            }

			input::InputCollection GetSubDataByViewportID(input::ViewportID viewportID) override final
			{
				return input::GetSubDataByViewportID(m_bufferedInputData, viewportID);
			}

			input::InputCollection GetSubDataByDeviceID(input::DeviceID deviceID) override final
			{
				return input::GetSubDataByViewportID(m_bufferedInputData, deviceID);
			}

			void RequestReset() override final
			{
				m_parent.RequestReset();
			}

			void RefreshDevices() override final
			{
				m_parent.RefreshDevices();
			}

			bool IsDeviceConnected(input::DeviceID deviceID) const override final
			{
				return m_parent.IsDeviceConnected(deviceID);
			}

			void SetRumble(input::DeviceID id, Float left, Float right) override final
			{
				m_parent.SetRumble(id, left,right);
			}

			void SetBacklightColor(input::DeviceID id, const Color& color) override final
			{
				m_parent.SetBacklightColor(id, color);
			}

			void ResetBacklightColor(input::DeviceID id) override final
			{
				m_parent.ResetBacklightColor(id);
			}

			Bool IsCursorForced() override final
			{
				return m_parent.IsCursorForced();
			}

			void ForceCursor( const CName reason, Bool show ) override final
			{
				m_parent.ForceCursor( reason, show );
			}

			Bool IsCursorVisible() const override final
			{
				return m_parent.IsCursorVisible();
			}

			void ShowCursor(const CName reason) override final
			{
				m_parent.ShowCursor(reason);
			}

			void HideCursor(const CName reason) override final
			{
				m_parent.HideCursor(reason);
			}

			void GetCursorPos(const input::ViewportID id, Int32& x, Int32& y) const override final
			{
				m_parent.GetCursorPos(id, x, y);
			}

			void GetCursorPos(const input::ViewportID id, Float& x, Float& y) const override final
			{
				m_parent.GetCursorPos(id, x, y);
			}

			void SetCursorPos( const input::ViewportID id, Int32 x, Int32 y ) const override final
			{
				m_parent.SetCursorPos( id, x, y );
			}

			CName GetDeviceName(input::DeviceID id) const override final
			{
				return m_parent.GetDeviceName(id);
			}

			input::EInputDevice GetDeviceType(input::DeviceID id) const override final
			{
				return m_parent.GetDeviceType(id);
			}

			Bool GetKeyState( EInputKey key ) const override final
			{
				return m_parent.GetKeyState( key );
			}

			Vector2 GetAxisState( Bool isLeftStick ) const override final 
			{
				return m_parent.GetAxisState( isLeftStick );
			}

			ViewportID GetLastInputViewportID() const override final
			{
				return m_parent.GetLastInputViewportID();
			}
		};
	}

	void RenderingWindowManager::ProcessInput( input::IInputContext& context )
	{
		// process input only on the active window
		for ( auto* window : m_windows )
		{
			if ( !m_windowsRemoved.Exist( window ) && !m_windowsDisabled.Exist( window->GetViewportID() ) )
			{
				// create scoped input context with the input just for this viewport
				helper::PerViewportInputContext viewportInput( context, window->GetViewportID() );
				window->HandleInput( viewportInput );
			}
		}

		// cleanup removed windows
		PurgeRemovedWindows();
	}

	void RenderingWindowManager::ProcessMessages( const red::DynArray<ExternalMessage>& messages )
	{
		// process messages for windows (usually focus gained or lost messages)
		for ( auto* window : m_windows )
		{
			if( !m_windowsRemoved.Exist(window ) )
			{
				for( auto& msg : messages )
					window->HandleMessage(msg);	
			}
		}

		// cleanup removed windows
		PurgeRemovedWindows();
	}
	
	void RenderingWindowManager::SetWindowTickDisabled( const Uint64 viewportID, Bool isDisabled )
	{
		if ( isDisabled )
		{
			m_windowsDisabled.Insert( viewportID );
		}
		else
		{
			m_windowsDisabled.Remove( viewportID );
		}
	}

} // tools
