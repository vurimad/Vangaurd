/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class RenderViewport;
class IViewportWindow;
struct IViewportWindowMessageSink;
struct IViewportWindowActionListener;

using ViewportID = Uint64;

class IViewportWindow
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

public:
	virtual ~IViewportWindow() {}

public:
	virtual ViewportID GetViewportID() const = 0;

	virtual void AttachMessageSink( IViewportWindowMessageSink* sink ) = 0;

	virtual void RegisterActionListener( IViewportWindowActionListener* listener ) = 0;
	virtual void UnregisterActionListener( IViewportWindowActionListener* listener ) = 0;

	virtual void Bind( RenderViewport* viewport ) = 0;
};

struct IViewportWindowMessageSink
{
	//Return true if we handle this message and don't want DefWindowProc to process it.
	virtual Bool MessageProc( void* windowHandle, Uint32 uMsg, Uint64 wParam, Uint64 lParam ) = 0;
	virtual void OnAttach( void* windowHandle ) = 0;
}; 

struct IViewportWindowActionListener
{
	virtual ~IViewportWindowActionListener() {}

	virtual void OnSizeChanged( const IViewportWindow* window, Int32 width, Int32 height ) = 0;
	virtual void OnResizing( const IViewportWindow* window ) = 0;
	virtual void OnMoved( const IViewportWindow* window, Int32 x, Int32 y ) = 0;
	virtual void OnMoving( const IViewportWindow* window) = 0;
	virtual void OnClose( const IViewportWindow* window ) = 0;
	virtual void OnActivationChanged( const IViewportWindow* window, Bool isActive ) = 0;
	virtual void OnMaximized( const IViewportWindow* window ) = 0;
	virtual void OnMinimized( const IViewportWindow* window ) = 0;
	virtual void OnRestored( const IViewportWindow* window ) = 0;
};

using MessageSinks = red::DynArray< IViewportWindowMessageSink* >;
using ActionListeners = red::DynArray< IViewportWindowActionListener* >;
using ViewportWindowPtr = red::UniquePtr< IViewportWindow >;