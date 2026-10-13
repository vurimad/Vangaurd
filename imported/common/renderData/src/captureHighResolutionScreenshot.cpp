/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "captureHighResolutionScreenshot.h"

// #include "imageProcessing.h"
// #pragma optimize("",off)

Bool rend::CCaptureHighResolutionScreenshot::ProcessResults(ColorResult* result, EEnvManagerModifier capType )
{
	// For now we only handle HdrResults.
	return true;
}

Bool rend::CCaptureHighResolutionScreenshot::ProcessResults( HdrResult* results, EEnvManagerModifier capType )
{
	ASSERT( results );

	if(m_hdrResultsSamplesAccumulator.Empty())
	{
		return false;
	}

	auto& accImagePtr = m_hdrResultsSamplesAccumulator.Back();

	RED_FATAL_ASSERT(accImagePtr.m_imagePtr);

	// First iteration, simple mempcy will do
	if(accImagePtr.m_numSamples == 0)
	{
		red::Memcpy( accImagePtr.m_imagePtr->GetPixelBuffer(), results->GetWriteData(), accImagePtr.m_imagePtr->GetPixelBufferSize() );
	}
	// Need samples accumulation
	else
	{
		const Float* srcData = results->GetWriteData();
		Float* dstData = (Float*)accImagePtr.m_imagePtr->GetPixelBuffer();
		Float* endData = (Float*)((Uint8*)dstData + accImagePtr.m_imagePtr->GetPixelBufferSize());

		// If memory layout allows, use SIMD accumulation
		if( 0 == ((Uint64)dstData & 0xF) && 0 == ((Uint64)srcData & 0xF) )
		{
			const __m128* src = (const __m128*)srcData;
			__m128* dst = (__m128*)dstData;
			const __m128* end = (const __m128*)endData;

			while(dst < end)
			{
				*dst = _mm_add_ps( *dst, *src );	// Add 4 floats at once
				++dst;
				++src;
			}
		}
		// Use FPU instead
		else
		{
			while(dstData < endData)
			{
				dstData[0] += srcData[0];
				dstData[1] += srcData[1];
				dstData[2] += srcData[2];
				dstData[3] += srcData[3];

				dstData += 4;
				srcData += 4;
			}
		}
	}

#if 0
	{
		static Uint32 index[EEnvManagerModifier::EMM_MAX];

		const String capName = MapEmmToTrailingString( capType );

		AnsiChar buf[512];
		sprintf( buf, "C:\\test\\%s_%u.exr", capName.AsChar(), index[capType]++ );

		GpuWrapApi::SaveImageAsEXR( results->GetWriteData() , results->GetWidth(), results->GetHeight(), 4, buf );
	}
#endif

	++accImagePtr.m_numSamples;

	return true;
}

Uint8 rend::CCaptureHighResolutionScreenshot::GetTextureAnisotropy() const
{
	return 8;
}

Float rend::CCaptureHighResolutionScreenshot::GetTextureMipBias() const
{
	return -( (Float)std::log2( m_samplesPerAxis * m_samplesPerAxis ) + 1.0f );
}

void rend::CCaptureHighResolutionScreenshot::AddSamplingAccumulationBuffer( EEnvManagerModifier capType )
{
	const img::Image::SizeInfo imageSizeInfo( 4, static_cast< Uint16 > ( m_samplesWidth), static_cast< Uint16 > ( m_samplesHeight ) );
	m_hdrResultsSamplesAccumulator.PushBack
	(
		rend::CCaptureHighResolutionScreenshot::ImageAccumulator
		( 
			img::Image::CreateEmpty( img::PixelFormat::Format_Float32, imageSizeInfo ), 
			capType
		) 
	);
}


Bool rend::CCaptureHighResolutionScreenshot::AverageCurrentAccumulationBuffer( EEnvManagerModifier capType )
{
	// Removed for now. Will be back when we will anti-alias things.
	return true;
}

Bool rend::CCaptureHighResolutionScreenshot::RequestHighResolutionScreenshotDump( EEnvManagerModifier capType, Bool flush /* = true */)
{
	if ( !RequestHighResolutionScreenshotDump(m_hdrResultsSamplesAccumulator, m_dumpRequests, MapEmmToTrailingString( capType ) ) )
	{
		return false;
	}

	if ( flush )
	{
		m_hdrResultsSamplesAccumulator.Clear();
	}

	return true;
}

String rend::CCaptureHighResolutionScreenshot::MapEmmToTrailingString(EEnvManagerModifier emmAsEnum)
{
	auto rttiType = static_cast<const rtti::EnumType*>( ::GetTypeObject<EEnvManagerModifier>() );

	CName name;
	RED_VERIFY( rttiType->FindName( emmAsEnum, name ) );

	// Skip 'EMM_' part if found
	return name.AsChar() + 4;
}

Bool rend::CCaptureHighResolutionScreenshot::PopHighResolutionScreenshotDump( rend::CCaptureHighResolutionScreenshot::HighResScreenshotDumpInfo& dumpInfo)
{
	if ( m_dumpRequests.Empty() )
	{
		return false;
	}

	dumpInfo = m_dumpRequests.Front();
	m_dumpRequests.Pop();

	return true;
}

rend::HighResolutionScreenshotSamples::GroupsOffsetsToSamplesMap rend::HighResolutionScreenshotSamples::GenerateSampling( screenshot::SamplingType type )
{
	GroupsOffsetsToSamplesMap groupsOffsetsToSamplesMap{ rend::PoolImageProcessing() };

	if ( type == screenshot::SamplingType::TYPE_REGULAR )
	{
		GenerateRegularSampling( groupsOffsetsToSamplesMap );
	}
	else if ( type == screenshot::SamplingType::TYPE_HAMMERSLEY )
	{
		GenerateHammersleySampling( groupsOffsetsToSamplesMap );
	}
	else if ( type == screenshot::SamplingType::TYPE_HALTON )
	{
		GenerateHaltonSampling( groupsOffsetsToSamplesMap );
	}

	return groupsOffsetsToSamplesMap;
}

Float rend::HighResolutionScreenshotSamples::GetVanDerCorputNumber( Uint32 n, Uint32 base )
{
	Float result = 0;
	Float denominator = 1;
	const Float inverseBase = 1.f / base;
	while ( n ) {
		denominator *= base;
		result += (n % base) / denominator;
		n = static_cast< Uint32 > ( static_cast<Float> ( n ) * inverseBase );
	}
	return result;
}

void rend::HighResolutionScreenshotSamples::GenerateSamplesFromLambda( GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap, SamplesGenerator genSamplesFunc )
{
	for ( Uint32 yOffset = 0; yOffset < m_groupsPerAxis; ++yOffset )
	{
		for ( Uint32 xOffset = 0; xOffset < m_groupsPerAxis; ++xOffset )
		{
			red::DynArray< Vector2 > samplesPerGroup{ rend::PoolImageProcessing() };
			genSamplesFunc( samplesPerGroup, xOffset, yOffset );
			groupsOffsetsToSamplesMap.PushBack( samplesPerGroup );
		}
	}
}

void rend::HighResolutionScreenshotSamples::GenerateRegularSampling(GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap)
{
	const auto regularSamplesPerGroupGen = [&](red::DynArray<Vector2>& samplesPerGroup, Uint32 xOffset, Uint32 yOffset) 
	{
		const Float samplesDelta = m_groupsPixelShift / static_cast<Float> (m_samplesPerAxis);

		for (Uint32 ySample = 0; ySample < m_samplesPerAxis; ++ySample)
		{
			for (Uint32 xSample = 0; xSample < m_samplesPerAxis; ++xSample)
			{
				Vector2 sample;
				sample.X = static_cast<Float>(xOffset) * m_groupsPixelShift + static_cast<Float>(xSample) * samplesDelta;
				sample.Y = static_cast<Float>(yOffset) * m_groupsPixelShift + static_cast<Float>(ySample) * samplesDelta;
				samplesPerGroup.PushBack(sample);
			}
		}
	};

	GenerateSamplesFromLambda(groupsOffsetsToSamplesMap, regularSamplesPerGroupGen);
}

void rend::HighResolutionScreenshotSamples::GenerateHammersleySampling(GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap)
{
	const auto hammersleySamplesPerGroupGen = [&](red::DynArray<Vector2>& samplesPerGroup, Uint32 xOffset, Uint32 yOffset) 
	{
		const Uint32 totalNumberOfSamples = m_samplesPerAxis * m_samplesPerAxis;
		for (Uint32 n = 0; n < totalNumberOfSamples; ++n)
		{
			Vector2 sample;
			// van Der Corput numbers will have values from <0; 1), we need to shift it to <0; m_groupsPixelShift)
			sample.X = static_cast<Float>(xOffset) * m_groupsPixelShift + GetVanDerCorputNumber(n, 2) * m_groupsPixelShift;
			// (n/totalNumberOfSamples) will have values from <0; 1), we need to restrict it to <0; m_groupsPixelShift).
			sample.Y = static_cast<Float>(yOffset) * m_groupsPixelShift + (static_cast<Float> (n) / static_cast<Float>(totalNumberOfSamples)) * m_groupsPixelShift;
			samplesPerGroup.PushBack(sample);
		}
	};

	GenerateSamplesFromLambda(groupsOffsetsToSamplesMap, hammersleySamplesPerGroupGen);
}

void rend::HighResolutionScreenshotSamples::GenerateHaltonSampling(GroupsOffsetsToSamplesMap& groupsOffsetsToSamplesMap)
{
	const auto haltonSamplesPerGroupGen = [&](red::DynArray<Vector2>& samplesPerGroup, Uint32 xOffset, Uint32 yOffset) 
	{
		const Uint32 totalNumberOfSamples = m_samplesPerAxis * m_samplesPerAxis;
		for (Uint32 n = 0; n < totalNumberOfSamples; ++n)
		{
			Vector2 sample;
			// van Der Corput numbers will have values from <0; 1), we need to shift it to <0; m_groupsPixelShift)
			sample.X = static_cast<Float>(xOffset) * m_groupsPixelShift + GetVanDerCorputNumber(n, 2) * m_groupsPixelShift;
			sample.Y = static_cast<Float>(yOffset) * m_groupsPixelShift + GetVanDerCorputNumber(n, 3) * m_groupsPixelShift;
			samplesPerGroup.PushBack(sample);
		}
	};

	GenerateSamplesFromLambda(groupsOffsetsToSamplesMap, haltonSamplesPerGroupGen);
}

Bool rend::CCaptureHighResolutionScreenshot::RequestHighResolutionScreenshotDump(const red::DynArray< ImageAccumulator >& samplesAccumulator, red::Queue<HighResScreenshotDumpInfo>& specializedRequestQueue, const String& nameExt /*=""*/)
{
	if ( samplesAccumulator.Empty() )
	{
		return false;
	}

	// Helper struct for storing raw pointers and iterating over them.
	red::StaticArray< Float*, 64 > pointersDataFromGroupsImages;
	for ( const auto& i : samplesAccumulator )
	{
		Float* srcPtr = reinterpret_cast<Float*>(i.m_imagePtr->GetPixelBuffer());
		Float* endPtr = reinterpret_cast<Float*>( (Uint8*)srcPtr + i.m_imagePtr->GetPixelBufferSize());

		pointersDataFromGroupsImages.PushBack( srcPtr );

		if(i.m_numSamples > 1)
		{
			const Float invScale = 1.0f / Float( i.m_numSamples );

			// Try rescale samples using SIMD
			if(0 == ((Uint64)srcPtr & 0xF))
			{
				__m128 scale = _mm_set_ps1( invScale );
				__m128*	src = (__m128*)srcPtr;
				__m128*	end = (__m128*)endPtr;

				while( src < end )
				{ 
					*src = _mm_mul_ps( *src, scale );
					++src;
				}
			}
			// Rescale with FPU
			else
			{
				while(srcPtr < endPtr)
				{
					srcPtr[0] *= invScale;
					srcPtr[1] *= invScale;
					srcPtr[2] *= invScale;
					srcPtr[3] *= invScale;
					srcPtr += 4;
				}
			}

		}
	}

	// Prepare final high-res image.
	const img::Image::SizeInfo imageSizeInfo( 4, m_highResWidth, m_highResHeight );
	auto dumpImgPtr = img::Image::CreateEmpty( img::PixelFormat::Format_Float32, imageSizeInfo ) ;
	Float* dstCol = reinterpret_cast< Float* >( dumpImgPtr->GetPixelBuffer() );

	// Dump data.
	for ( Uint32 y = 0; y < m_highResHeight; ++y )
	{
		const int multY = y % m_groupsPerAxis;
		for ( Uint32 x = 0; x < m_highResWidth; ++x )
		{
			const int multX = x % m_groupsPerAxis;
			const int index = m_groupsPerAxis * multY + multX;

			// In order to correctly interleave the data, pointers in the array are also incremented.
			Float*& currPixelFromAccumulation = pointersDataFromGroupsImages[index];
			dstCol[ 0 ] = currPixelFromAccumulation[ 0 ];
			dstCol[ 1 ] = currPixelFromAccumulation[ 1 ];
			dstCol[ 2 ] = currPixelFromAccumulation[ 2 ];
			dstCol[ 3 ] = currPixelFromAccumulation[ 3 ];

			dstCol += 4;
			currPixelFromAccumulation += 4;
		}
	}

	specializedRequestQueue.Push( HighResScreenshotDumpInfo( dumpImgPtr, nameExt ) );

	return true;
}
