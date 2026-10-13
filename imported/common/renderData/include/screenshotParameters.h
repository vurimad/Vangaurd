/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "resolutionParams.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// Save screenshot width and height. Don't change those values as on PS4
// copying screenshot to buffer assumes they are like that when calculating 
// strides and offsets for texture pixels. 
// Not my fault, it was like that before, just didn't have time to fix that yet.
#define SAVE_SCREENSHOT_WIDTH		228
#define SAVE_SCREENSHOT_HEIGHT		128

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

namespace rend
{
	using OnScreenshotCreatedCallback = red::FixedSizeFunction< void(const red::String&, bool),72U >;
}

enum ESaveFormat : Int32
{
	SF_BMP = RED_FLAG( 0 ),
	SF_PNG = RED_FLAG( 1 ),
	SF_JPG = RED_FLAG( 2 ),
	SF_DDS = RED_FLAG( 3 ),
	SF_RAW = RED_FLAG( 4 ),
	SF_EXR = RED_FLAG( 5 ),
	SF_PNG_24 = RED_FLAG( 6 ),

	SF_PNG_AND_EXR = SF_PNG | SF_EXR
};

RTTI_DECLARE_ENUM( ESaveFormat );

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
	
struct SScreenshotParameters
{
	Uint32		m_width;
	Uint32		m_height;
	Uint32		m_superSamplingSize;
	//Int32		m_pitch;
	//Int32		m_yaw;
	//Float		m_fov;
	//Float		m_dof;
	red::AbsolutePath		m_absoluteFilePath;
	IFile*					m_outputFile;
	ESaveFormat				m_saveFormat;
	Uint32*					m_buffer;				//!< Buffer, that will be filled with data
	size_t					m_bufferSize;
	size_t*					m_bufferSizeWritten;
	red::Atomic< Bool >*	m_completionFlag;
	Bool					m_noWatermark;
	rend::OnScreenshotCreatedCallback m_onScreenshotCreated;

	SScreenshotParameters( Uint32 width = rend::dim::Preset<rend::dim::eFULL_HD>::W, 
						   Uint32 height = rend::dim::Preset<rend::dim::eFULL_HD>::H, 
						   ESaveFormat saveFormat = SF_BMP, 
						   Uint32 superSamplingSize = 4, 
						   Uint32* buffer = nullptr,
						   Bool noWatermark = false )
		: m_width( width )
		, m_height( height )
		, m_superSamplingSize( superSamplingSize )
		, m_saveFormat( saveFormat )
		, m_outputFile( nullptr )
		, m_buffer( buffer )
		, m_bufferSize( 0 )
		, m_bufferSizeWritten( nullptr )
		, m_completionFlag( nullptr )
		, m_noWatermark( noWatermark )
	{
	}

	/*
	SScreenshotParameters( Uint32 width = 1920, Uint32 height = 1080, const red::String& fileName = (""), ESaveFormat saveFormat = SF_BMP, Uint32 superSamplingSize = 4, Int32 pitch = 1, Int32 yaw = 1, Float fov = 70.0f, Float dof = 1.0f )
		: m_width( width )
		, m_height( height )
		, m_fileName( fileName )
		, m_saveFormat( saveFormat )
		, m_superSamplingSize( superSamplingSize )
		, m_pitch( pitch )
		, m_yaw( yaw )
		, m_fov( fov )
		, m_dof( dof )
	{
	}
	*/
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
