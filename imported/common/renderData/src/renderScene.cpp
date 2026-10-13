/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderScene.h"
#include "renderStats.h"
#include "renderPublicEnums.h"
#include "renderProxy.h"
#include "renderSkinningData.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

QueryOnlyBoxId QueryOnlyBoxId::Invalid()
{
	return {UINT32_MAX};
}

class NoRenderScene : public IRenderScene
{

public:

	NoRenderScene()
		: IRenderScene( IRenderScene::NO_SCENE )
	{}

	~NoRenderScene()
	{}

	virtual void GetRenderingStats( RenderingStats& outStats ) const override { outStats = RenderingStats(); }

	virtual QueryOnlyBoxId CreateQueryOnlyBox( const QueryOnlyBoxCreationInfo& info ) override { return QueryOnlyBoxId::Invalid(); }
	virtual void DestroyQueryOnlyBox( QueryOnlyBoxId id ) override {}
	virtual Bool UpdateQueryOnlyBox( QueryOnlyBoxId id, const Box& bounds ) override { return false; }
	virtual Bool IsVisibleQueryOnlyBox( QueryOnlyBoxId id ) const override { return false; }
    virtual void RequestQueryOnlyBoxStats( Bool value ) override {};
    virtual bool ScheduleRelink( const ScheduleRelinkParameter & param ) override {return true; }
	virtual const AnsiChar* IsRenderingReady() const override { return nullptr; }
	virtual void TickWhileLoading( Bool isFirstFrame ) override {}
	virtual Int32 GetForcedLOD() const override { return -1; }
};


IRenderScene* IRenderScene::Null()
{
	static IRenderScene* scene = RED_NEW( NoRenderScene )();
	return scene;
}


IRenderScene::ScheduleRelinkParameter::~ScheduleRelinkParameter() = default;
IRenderScene::ScheduleRelinkParameter::ScheduleRelinkParameter( ScheduleRelinkParameter&& ) = default;

IRenderScene::ScheduleRelinkParameter::ScheduleRelinkParameter(
	IRenderProxy* proxy,
	const rend::RenderProxyTransform& localToWorld,
	const Box& worldBounds,
	IRenderSkinningData* skinningData,
	IRenderFloatTrackData* floatTrackData,
	Bool isTeleport )
	: proxy( proxy )
	, localToWorld( localToWorld )
	, worldBounds( worldBounds )
	, skinningData( skinningData )
	, floatTrackData( floatTrackData )
	, isTeleport( isTeleport )
{
}

IRenderScene::ScheduleRelinkParameter::ScheduleRelinkParameter(
	IRenderProxy* proxy,
	const rend::RenderProxyTransform& localToWorld,
	const Box& worldBounds,
	Bool isTeleport )
	: proxy( proxy )
	, localToWorld( localToWorld )
	, worldBounds( worldBounds )
	, skinningData( nullptr )
	, floatTrackData( nullptr )
	, isTeleport( isTeleport )
{
}

IRenderScene::ScheduleRelinkParameter::ScheduleRelinkParameter(
	IRenderProxy* proxy,
	const Box& worldBounds,
	const red::ArraySpan<const rend::RenderProxyTransform>& instances,
	Bool isTeleport )
	: proxy( proxy )
	, localToWorld( rend::RenderProxyTransform::IDENTITY() )
	, worldBounds( worldBounds )
	, skinningData( nullptr )
	, floatTrackData( nullptr )
	, instances( instances )
	, isTeleport( isTeleport )
{
}

IRenderScene::ScheduleRelinkParameter::ScheduleRelinkParameter(
	IRenderProxy* proxy,
	const Box& worldBounds,
	const red::ArraySpan<const Matrix>& instances,
	Bool isTeleport )
	: proxy( proxy )
	, localToWorld( rend::RenderProxyTransform::IDENTITY() )
	, worldBounds( worldBounds )
	, skinningData( nullptr )
	, floatTrackData( nullptr )
	, instancesMtx( instances )
	, isTeleport( isTeleport )
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -