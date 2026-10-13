/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "renderObject.h"

struct SRenderNodeImplContext;

struct VideoSummary
{
	Uint32 width;
	Uint32 height;
	Uint32 currentTimeMs;
	Uint32 totalTimeMs;
	Uint32 currentFrame;
	Uint32 totalFrames;
	Uint32 frameRate;
	Uint32 frameRateDiv;
};

 /// Render video
class IRenderVideo: public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolVideoPlayer );

public:

	enum class UploadResult
	{
		NotNeeded,
		Uploaded,
		OutOfBudget,
		TooBigForBudget,
	};

	struct InitContext
	{
		CName canvasName;
		Bool passiveMode;
		Bool bufferVideo;
		Int32 audioEventId;
		TRenderPtr< IRenderVideo > renderVideo;
		red::FixedSizeFunction< void() > loopCallback;
		red::FixedSizeFunction< void( Bool ) > initCallback;
	};

public:

	virtual Bool Init( const CName& canvasName, Bool passiveMode, Int32 audioEventId, const red::FixedSizeFunction< void() >& loopCallback ) = 0;
	virtual void Init( const InitContext& ctx ) = 0;
	virtual void ReleaseCanvas() = 0;

	//! Render video from renderer tick
	virtual void Update() = 0;
	virtual void RenderFullscreen( const SRenderNodeImplContext& rctx ) = 0;
	virtual UploadResult UploadTextures( Uint32 maxAllowedUpload, Uint32& accumUploadedSize ) = 0;

	virtual const res::ResourcePath& GetVideoPath() const = 0;

	virtual Bool IsInitializing() const = 0;
	virtual Bool IsValid() const = 0;

	virtual Bool IsLooped() const = 0;
	virtual void Loop( Bool loop ) = 0;

	virtual Bool IsVideoFrameRateForced() const = 0;
	virtual void ForceVideoFrameRate( Bool force ) = 0;

	virtual Bool IsPaused() const = 0;
	virtual void Pause( Bool pause ) = 0;

	virtual void SkipFrames( Int32 numberOfFrames ) = 0;
	virtual void SkipFramePercentage( Int32 framePercentage ) = 0;

	virtual Int32 GetAudioEventId() const = 0;

	virtual void KeepInSyncWithExternal( Float time, Bool enableSkipBackwards = false ) = 0;

	virtual void GetVideoSummary( VideoSummary& summary ) const = 0;

	virtual void EnablePassiveUpdate( Bool enable ) = 0;

	virtual Bool IsDecodingFrame() const = 0;
};