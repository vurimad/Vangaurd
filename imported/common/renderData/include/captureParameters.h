/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "resolutionParams.h"
#include "renderPublicEnums.h"

namespace rend
{
	enum class ScreenshotMode
	{
		NONE,
		NORMAL,
		GAMESAVE_THUMBNAIL,
		LAYERED,
		HIGH_RESOLUTION,
		HIGH_RESOLUTION_LAYERED
	};

	enum class ResolutionMultiplier
	{
		X1 = 1,
		X2 = 2,
		X4 = 4
	};

	using OnScreenshotCreatedCallback = red::FixedSizeFunction<void(const red::String&, bool),72U>;
	enum class CaptureContextType
	{
		SceneGamedef,
		AnimViewer,
		SceneSection
	};

	struct RENDER_DATA_API ScreenshotBatchData : ISerializable
	{
		RTTI_DECLARE_TYPE( ScreenshotBatchData );

	public:
		ScreenshotBatchData();

		AbsolutePathSerializable m_batchPositionsPath;
		Float m_delayTime = 1.f;
		Uint32 m_numberOfCoordinatesToDump = 0;
	};

	struct RENDER_DATA_API GridGeneratorData : ISerializable
	{
		RTTI_DECLARE_TYPE( GridGeneratorData );

	public:
		Vector3 m_startingPosition = Vector3( 0, 0, 50 );
		EulerAngles m_rotation;
		Float m_xStep = 88.9f;
		Float m_yStep = 50;
		Uint32 m_numberOfXSteps = 10;
		Uint32 m_numberOfYSteps = 10;

		Float m_orbitDistance = 1.f;
		Float m_zoom = 50.f;
	};

	struct RENDER_DATA_API SingleScreenShotData : public ISerializable
	{
		RTTI_DECLARE_TYPE( SingleScreenShotData );

	public:
		SingleScreenShotData();

		virtual void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const override;
		virtual void OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue ) override;

		Bool m_requested;
		rend::ScreenshotMode m_mode;
		AbsolutePathSerializable m_outputPath;
		String m_filePrefix;
		IFile* m_output;
		dim::EPreset m_resolution;
		ResolutionMultiplier m_resolutionMultiplier;
		Bool m_forceLOD0;
		ESaveFormat m_saveFormat;
		red::DynArray< EEnvManagerModifier > m_emmModes;
		OnScreenshotCreatedCallback m_onCreatedCallback;
		Bool m_includeDebugOverlay;
		Bool m_isDeferredScreenshotGrab;
	};

	struct RENDER_DATA_API CaptureParameters
	{
		RTTI_DECLARE_TYPE( CaptureParameters );

	public:
		CaptureParameters();

		ScreenshotMode m_mode;
		String m_outputDirectoryName;
		String m_outputDirectoryNameSuffix;
		Uint32 m_initialFrameNumber;
		Uint32 m_outputDirectoryIndex;
		Uint32 m_recordingFPS;
		Point m_customResolution;
		ResolutionMultiplier m_resolutionMultiplier;
		AbsolutePathSerializable m_outputPath;
		red::DynArray< EEnvManagerModifier > m_emmModes;
		Float m_fovMultiplier;
		EFramePurpose m_framePurpose;	// modify to allow debug draws
		ESaveFormat m_saveFormat;
		CaptureContextType m_captureContextType;
		Bool m_enable;
		Bool m_videoRecordingMode;
		Bool m_audioRecordingMode;
		Bool m_isInMemoryCapture;
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( ScreenshotMode, rend )
RTTI_DECLARE_ENUM_IN_NAMESPACE( ResolutionMultiplier, rend )