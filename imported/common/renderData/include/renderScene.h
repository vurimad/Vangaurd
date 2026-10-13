/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "renderObject.h"
#include "renderPublicTypes.h"
#include "renderWorldTransform.h"

class IRenderFloatTrackData;
struct RenderingStats;

/// Proxy to rendering scene
class IRenderScene : public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolRenderScene );

public:

	enum Type
	{
		WORLD,
		PREVIEW,
		THUMBNAIL,
		NO_SCENE
	};

private:

	Type			m_type;

public:

	RED_FORCE_INLINE IRenderScene( Type type )
		: m_type( type )
	{}

	virtual ~IRenderScene() {};

	//! Get current render stats
	virtual void GetRenderingStats( RenderingStats& outStats ) const = 0;

	// 
	virtual QueryOnlyBoxId CreateQueryOnlyBox( const QueryOnlyBoxCreationInfo& info ) = 0;
	virtual void DestroyQueryOnlyBox( QueryOnlyBoxId id ) = 0;
	virtual Bool UpdateQueryOnlyBox( QueryOnlyBoxId id, const Box& bounds ) = 0;
	virtual Bool IsVisibleQueryOnlyBox( QueryOnlyBoxId id ) const = 0;
    virtual void RequestQueryOnlyBoxStats( Bool value ) = 0;


	// Checks if scene is ready for rendering. Return reason when not ready, nullptr when ready
	virtual const AnsiChar* IsRenderingReady() const = 0;

	// Tick scene while under loading screen
	virtual void TickWhileLoading( Bool isFirstFrame ) = 0;
    
    // This is called once per frame just before kick scene render jobs.
    // It is a good place to sync data between scene and engine side stuff.
    // Should be as lightweight as possible.
    virtual void PrepareForUpdate( Uint32 tickCounter ) {}

	RED_FORCE_INLINE Type GetType() const { return m_type; }

	virtual Int32 GetForcedLOD() const = 0;

    virtual void SetDissolveTime( Float value ) {}
    virtual Float GetDissolveTime() const { return 0.5f; }

public:

	// Return dummy scene, this function does NOT AddRef, manage refCount on your own
	// This scene has GetType() as NO_SCENE 
	RENDER_DATA_API static IRenderScene*	Null();

	static RED_FORCE_INLINE Bool IsValid( const IRenderScene* scene )
	{
		return scene && scene->GetType() != NO_SCENE;
	}

	struct RENDER_DATA_API ScheduleRelinkParameter
	{
		IRenderProxy*										proxy;
		IRenderSkinningData*								skinningData;
		rend::RenderProxyTransform							localToWorld;
		Box													worldBounds;
		red::ArraySpan<const rend::RenderProxyTransform>	instances;
		red::ArraySpan<const Matrix>						instancesMtx;
		IRenderFloatTrackData*								floatTrackData;
		Bool												isTeleport;

		ScheduleRelinkParameter(
			IRenderProxy* proxy,
			const rend::RenderProxyTransform& localToWorld,
			const Box& worldBounds,
			IRenderSkinningData* skinningData,
			IRenderFloatTrackData* floatTrackData,
			Bool isTeleport );

		ScheduleRelinkParameter(
			IRenderProxy* proxy,
			const rend::RenderProxyTransform& localToWorld,
			const Box& worldBounds,
			Bool isTeleport );

		ScheduleRelinkParameter(
			IRenderProxy* proxy,
			const Box& worldBounds,
			const red::ArraySpan<const rend::RenderProxyTransform>& instances,
			Bool isTeleport );

		ScheduleRelinkParameter(
			IRenderProxy* proxy,
			const Box& worldBounds,
			const red::ArraySpan<const Matrix>& instances,
			Bool isTeleport );

		~ScheduleRelinkParameter();
		ScheduleRelinkParameter(ScheduleRelinkParameter&&);
	};

	virtual bool ScheduleRelink( const ScheduleRelinkParameter & param ) = 0;
	
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
