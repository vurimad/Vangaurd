/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../renderData/include/renderSelectionResult.h"
#include "../../redImage/include/image.h"
#include "../../redContainers/include/queue.h"

namespace rend
{
	namespace screenshot
	{
		enum class SamplingType
		{
			TYPE_REGULAR,
			TYPE_HAMMERSLEY,
			TYPE_HALTON,
			MAX
		};
	}

	class RENDER_DATA_API HighResolutionScreenshotSamples
	{
	public:

		inline HighResolutionScreenshotSamples( Uint16 samplesPerAxis, Uint32 screenWidth, Uint32 screenHeight, Uint32 highResWidth, Uint32 highResHeight )
			: m_samplesPerAxis( samplesPerAxis )
			, m_samplesWidth( screenWidth )
			, m_samplesHeight( screenHeight )
			, m_highResWidth( highResWidth )
			, m_highResHeight( highResHeight )
			, m_groupsPerAxis( Max( static_cast<Uint32>(MCeil( static_cast<Float> (m_highResWidth) / static_cast<Float> (screenWidth) )), static_cast<Uint32>(MCeil( static_cast<Float> (m_highResHeight) / static_cast<Float>(screenHeight) )) ) )
			, m_groupsPixelShift( Max( screenHeight / Float( highResHeight ), screenWidth / Float( highResWidth ) ) )
		{}

		using GroupsOffsetsToSamplesMap = red::DynArray< red::DynArray< Vector2 > >;

		// Sampling acquisition.
		GroupsOffsetsToSamplesMap GenerateSampling( screenshot::SamplingType type );

	protected:

		Uint16 m_samplesPerAxis;
		Uint32 m_samplesWidth;
		Uint32 m_samplesHeight;
		Uint32 m_highResWidth;
		Uint32 m_highResHeight;
		Uint32 m_groupsPerAxis;
		Float m_groupsPixelShift;

	private:

		// Sampling-related methods and definitions.
		using SamplesGenerator = const std::function< void( red::DynArray< Vector2 >& samplesPerGroup, Uint32 xOffset, Uint32 yOffset ) >&;
		Float GetVanDerCorputNumber( Uint32 n, Uint32 base = 2 );
		void GenerateSamplesFromLambda( GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap, SamplesGenerator genSamplesFunc );
		void GenerateRegularSampling( GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap );
		void GenerateHammersleySampling( GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap );
		void GenerateHaltonSampling( GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap );

	};

	class RENDER_DATA_API CCaptureHighResolutionScreenshot 
		: public rend::IColorCapture
		, public HighResolutionScreenshotSamples
	{
	public:
		CCaptureHighResolutionScreenshot( Uint16 samplesPerAxis, Uint32 screenWidth, Uint32 screenHeight, Uint32 highResWidth, Uint32 highResHeight )
			: HighResolutionScreenshotSamples( samplesPerAxis, screenWidth, screenHeight, highResWidth, highResHeight )
		{}

		struct HighResScreenshotDumpInfo
		{
			HighResScreenshotDumpInfo() = default;
			HighResScreenshotDumpInfo( const img::ImagePtr& imagePtr, const String& nameExtenstion )
				: m_imagePtr( imagePtr )
				, m_nameExtenstion( nameExtenstion )
			{}
			RED_INLINE Bool IsHDR() { return m_imagePtr->GetPixelFormat() == img::PixelFormat::Format_Float32; };

			img::ImagePtr m_imagePtr;
			String m_nameExtenstion;

		};

		virtual Bool ProcessResults( ColorResult* result, EEnvManagerModifier capType = EMM_None )  override;
		virtual Bool ProcessResults( HdrResult* results, EEnvManagerModifier capType = EMM_None ) override;
		virtual void SetupVideoCapture( const Uint32 frameWidth, const Uint32 frameHeight )  override {};

		RED_INLINE void SetSamplesPerAxis( Uint16 samplesPerAxis ) { m_samplesPerAxis = samplesPerAxis; };

		void AddSamplingAccumulationBuffer( EEnvManagerModifier capType = EMM_None );
		Bool AverageCurrentAccumulationBuffer( EEnvManagerModifier capType );

		Bool RequestHighResolutionScreenshotDump( EEnvManagerModifier capType, Bool flush = true );
		Bool PopHighResolutionScreenshotDump( HighResScreenshotDumpInfo& imagePtrWithNameExt );

		Uint8 GetTextureAnisotropy() const;
		Float GetTextureMipBias() const;
	private:
		struct ImageAccumulator
		{
			ImageAccumulator() = default;
			ImageAccumulator(const img::ImagePtr& imagePtr, EEnvManagerModifier capType )
				: m_imagePtr( imagePtr )
				, m_type( capType )
				, m_numSamples( 0 )
			{}

			img::ImagePtr		m_imagePtr;
			EEnvManagerModifier m_type;
			Uint32				m_numSamples;
		};
		
		// Interleave particular accumulators and prepare final image.
		Bool RequestHighResolutionScreenshotDump( const red::DynArray< ImageAccumulator >& samplesAccumulator, red::Queue< HighResScreenshotDumpInfo >& specializedRequestQueue, const String& nameExt /*=""*/);

		String MapEmmToTrailingString(EEnvManagerModifier emmAsEnum );

		// Accumulators - internal bits setup depends on accumulator type. For now it is Float32 as we only handle HDR.
		red::DynArray< ImageAccumulator > m_hdrResultsSamplesAccumulator{ rend::PoolImageProcessing() };
		red::Queue< HighResScreenshotDumpInfo > m_dumpRequests{ rend::PoolImageProcessing() };
	};

}
