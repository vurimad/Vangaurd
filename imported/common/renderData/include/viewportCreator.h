/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class IViewport;

namespace rend
{
	using ViewportHandle = red::SharedPtr< IViewport >;
	using ViewportID = Uint64;

	class EngineViewport;
	typedef red::SharedPtr< EngineViewport > EngineViewportPtr;

	enum class ViewportType
	{
		EngineViewport,
		GameViewport
	};

	class IViewportListener
	{
	public:
		virtual ~IViewportListener() {}

		virtual void OnViewportCreate( ViewportHandle viewport, ViewportType viewportType ) = 0;
		virtual void OnViewportClose( ViewportID viewportId ) = 0;
	};

	class IViewportCreator
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		virtual ~IViewportCreator() {}

		// Request a new engine viewport
		// windowContextName : e.g. the type of editor/game that this viewport displays, used for debug filters
		virtual EngineViewportPtr CreateEngineViewport( const Uint64 platformBaseHandle, const String &windowContextName ) = 0;

		// Request a game viewport, NOTE: resolution settings are read internally
		virtual EngineViewportPtr CreateGameViewport( const AnsiChar* gameTitle ) = 0;

		virtual void RegisterListener( IViewportListener* listener ) = 0;
		virtual void UnregisterListener( IViewportListener* listener ) = 0;
	};

}