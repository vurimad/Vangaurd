/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "captureParameters.h"

#include "../../renderData/include/screenshotParameters.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redFileSystem/include/filePaths.h"
#include "resolutionParams.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( ScreenshotMode, rend )
	RTTI_ENUM_OPTION( NONE );
	RTTI_ENUM_OPTION( NORMAL );
	RTTI_ENUM_OPTION( LAYERED );
	RTTI_ENUM_OPTION( HIGH_RESOLUTION );
	RTTI_ENUM_OPTION( HIGH_RESOLUTION_LAYERED );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( ResolutionMultiplier, rend )
	RTTI_ENUM_OPTION( X1 );
	RTTI_ENUM_OPTION( X2 );
	RTTI_ENUM_OPTION( X4 );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( CaptureContextType, rend )
	RTTI_ENUM_OPTION( SceneGamedef );
	RTTI_ENUM_OPTION( AnimViewer );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE_IN_NAMESPACE( ScreenshotBatchData, rend );
	RTTI_PROPERTY( m_batchPositionsPath ).editable();
	RTTI_PROPERTY( m_delayTime ).editable().tooltip( "Time to wait after camera teleport before taking new capture, in seconds" );
	RTTI_PROPERTY( m_numberOfCoordinatesToDump ).editable().tooltip( "Dump coordinates instead of actual screenshots, for path recording" );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( GridGeneratorData, rend );
	RTTI_PROPERTY( m_startingPosition ).editable();
	RTTI_PROPERTY( m_rotation ).editable();
	RTTI_PROPERTY( m_xStep ).editable();
	RTTI_PROPERTY( m_yStep ).editable();
	RTTI_PROPERTY( m_numberOfXSteps ).editable();
	RTTI_PROPERTY( m_numberOfYSteps ).editable();
	RTTI_PROPERTY( m_orbitDistance ).editable();
	RTTI_PROPERTY( m_zoom ).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( SingleScreenShotData, rend );
	RTTI_PARENT_TYPE( ISerializable );
	RTTI_PROPERTY( m_mode ).editable();
	RTTI_PROPERTY( m_outputPath ).editable();
	RTTI_PROPERTY( m_resolution ).editable();
	RTTI_PROPERTY( m_resolutionMultiplier );
	RTTI_PROPERTY( m_emmModes );
	RTTI_PROPERTY( m_forceLOD0 );
	RTTI_PROPERTY( m_saveFormat );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE_IN_NAMESPACE( CaptureParameters, rend );
	RTTI_DESCRIPTIVE_NAME( "Enabled" );
	RTTI_PROPERTY( m_enable );
	RTTI_DESCRIPTIVE_NAME( "Layered Image Mode" );
	RTTI_PROPERTY( m_mode ).editable();
	RTTI_DESCRIPTIVE_NAME( "Capture Video" );
	RTTI_PROPERTY( m_videoRecordingMode ).editable();
	RTTI_DESCRIPTIVE_NAME( "Capture Audio" );
	RTTI_PROPERTY( m_audioRecordingMode ).editable();
	RTTI_DESCRIPTIVE_NAME( "Modes" );
	RTTI_PROPERTY( m_emmModes ).editable().inlined().resizable();
	RTTI_DESCRIPTIVE_NAME( "Initial Frame Number" );
	RTTI_PROPERTY( m_initialFrameNumber ).editable();
	RTTI_DESCRIPTIVE_NAME( "Output Directory Index" );
	RTTI_PROPERTY( m_outputDirectoryIndex ).editable();
	RTTI_DESCRIPTIVE_NAME( "Output Directory Name" );
	RTTI_PROPERTY( m_outputDirectoryName ).editable();
	RTTI_DESCRIPTIVE_NAME( "Output Directory Name Suffix" );
	RTTI_PROPERTY( m_outputDirectoryNameSuffix ).editable();
	RTTI_DESCRIPTIVE_NAME( "Recording FPS" );
	RTTI_PROPERTY( m_recordingFPS ).editable();
	RTTI_DESCRIPTIVE_NAME( "Use Custom Resolution" );
	RTTI_PROPERTY( m_customResolution ).editable();
	RTTI_PROPERTY( m_resolutionMultiplier ).editable();
	RTTI_DESCRIPTIVE_NAME( "Output Path" );
	RTTI_PROPERTY( m_outputPath ).editable();
	RTTI_DESCRIPTIVE_NAME( "Multiply FOV" );
	RTTI_PROPERTY( m_fovMultiplier ).editable();
	RTTI_DESCRIPTIVE_NAME( "Capture Context" );
	RTTI_PROPERTY( m_captureContextType ).editable();
	RTTI_PROPERTY( m_saveFormat ).editable();
RTTI_END_TYPE();

namespace rend
{
	ScreenshotBatchData::ScreenshotBatchData()
	{
		m_batchPositionsPath = GFileManager->GetGameRoot().AddFilePath( "\\depot\\non_production\\test\\batch_of_camera_coords.json" );
	}

	//////////////////////////////////////////////////////////////////////////

	SingleScreenShotData::SingleScreenShotData()
		: m_requested( false )
		, m_mode( ScreenshotMode::NONE )
		, m_output( nullptr )
		, m_resolution( dim::EPreset::eFULL_HD )
		, m_resolutionMultiplier( ResolutionMultiplier::X2 )
		, m_emmModes( red::PoolBackend() )
		, m_forceLOD0( false )
		, m_saveFormat( SF_PNG )
		, m_includeDebugOverlay( false )
		, m_isDeferredScreenshotGrab( false )
	{
		m_outputPath = red::paths::GetUserDirectory().AddDirPath( "screenshots" );
	}

	void SingleScreenShotData::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		if ( m_mode != ScreenshotMode::NONE )
		{
			rtti::ClassEditablePropertyInfo info;
			info.m_name = RED_NAME_CONSTEXPR( "saveFormat" );
			info.m_type = GetTypeObject< ESaveFormat >( );
			outRootProperties.Add( info );
		}

		if ( m_mode == ScreenshotMode::HIGH_RESOLUTION || m_mode == ScreenshotMode::HIGH_RESOLUTION_LAYERED )
		{
			rtti::ClassEditablePropertyInfo info;
			info.m_name = RED_NAME_CONSTEXPR( "resolutionMultiplier" );
			info.m_type = GetTypeObject< ResolutionMultiplier >();
			outRootProperties.Add( info );

			info.m_name = RED_NAME_CONSTEXPR( "forceLOD0" );
			info.m_type = GetTypeObject< Bool >();
			outRootProperties.Add( info );
		}

		if ( m_mode == ScreenshotMode::HIGH_RESOLUTION )
		{
			rtti::ClassEditablePropertyInfo info;
			info.m_name = RED_NAME_CONSTEXPR( "emmModes" );
			info.m_type = GetTypeObject< red::DynArray< EEnvManagerModifier > >();
			info.m_isResizable = true;
			outRootProperties.Add( info );
		}
	}

	void SingleScreenShotData::OnPropertyPostChange( const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& newValue )
	{
		ISerializable::OnPropertyPostChange( path, oldValue, newValue );

		if ( path.ToString().EndsWith( "mode" ) )
		{
			NotifyPropertyChanged( rtti::AccessPath() );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	CaptureParameters::CaptureParameters()
		: m_outputDirectoryName( "Scene" )
		, m_initialFrameNumber( 0 )
		, m_outputDirectoryIndex( 0 )
		, m_recordingFPS( 30 )
		, m_customResolution( Point( 0, 0 ) )
		, m_mode( ScreenshotMode::NONE )
		, m_emmModes( red::PoolBackend() )
		, m_resolutionMultiplier( ResolutionMultiplier::X2 )
		, m_fovMultiplier( 1.0f )
		, m_captureContextType( CaptureContextType::SceneGamedef )
		, m_framePurpose( EFramePurpose::FP_ScreenShot )
		, m_saveFormat( ESaveFormat::SF_PNG )
		, m_enable( true )
		, m_videoRecordingMode( false )
		, m_audioRecordingMode( false )
		, m_isInMemoryCapture( false )
	{
		m_outputPath = red::paths::GetUserDirectory().AddDirPath( "screenshots" );
	}
}