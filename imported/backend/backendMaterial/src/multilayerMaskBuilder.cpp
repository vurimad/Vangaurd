/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "multilayerMaskBuilder.h"

#include "../../../common/renderData/include/renderMultilayer.h"
#include "../../../common/resourceMaterial/include/multilayerMask.h"

#include "../../../common/renderData/include/renderer.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redImage/include/image.h"
#include "../../../common/redImage/include/imageSaver.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"

// profiling
#include "../../../common/redSystem/include/stopWatch.h"

// cooking
#include "bitmapTextureCompilationSource.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../../common/core/include/message.h"

// #TODO: Store Uint8 mask values internally. Terrain and Substance are apparently both giving Uint8, and we aren't
// doing any resampling in here, so there's not much reason for using Float outside of the final Compress (which could
// probably be rewritten to use ints too).
// #TODO: If data is provided in native format (Float now, but should be Uint8), then give option to use the source data
// directly instead of copying it into our own arrays.


// Enable for some awesome debug code. Saves out various stages of the import into some temp folder. Bunch of
// assumptions made, not a lot of error checking going on there.
// #TODO: Might be useful to expose these as proper options, what to dump and where. Then don't need to #if it out.
#define ENABLE_PNG_DUMP 0
#if ENABLE_PNG_DUMP
#	define ENABLE_PNG_DUMP_SPLIT_LAYERS 1
#	define ENABLE_PNG_DUMP_RECONSTRUCTED_LAYERS 1
#endif


static constexpr Uint32 c_invalidLocation = -1;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BC4 ripped and slightly modified from DirectXTex. Modification allows endpoints to be found from separate set of
// values than just the ones being encoded. So can take neighboring blocks into consideration when compressing the edges
// of a tile, to have properly duplicated border texels.

constexpr Uint32 BLOCK_SIZE = 4;
constexpr Uint32 NUM_PIXELS_PER_BLOCK = 16;

//-------------------------------------------------------------------------------------
// Templates
//-------------------------------------------------------------------------------------
//#pragma warning(push)
//#pragma warning(disable : 4127)
static void OptimizeAlpha( Float *pX, Float *pY, const Float *pPoints, Uint32 numTexels, size_t cSteps )
{
	static const Float pC6[] = { 5.0f / 5.0f, 4.0f / 5.0f, 3.0f / 5.0f, 2.0f / 5.0f, 1.0f / 5.0f, 0.0f / 5.0f };
	static const Float pD6[] = { 0.0f / 5.0f, 1.0f / 5.0f, 2.0f / 5.0f, 3.0f / 5.0f, 4.0f / 5.0f, 5.0f / 5.0f };
	static const Float pC8[] = { 7.0f / 7.0f, 6.0f / 7.0f, 5.0f / 7.0f, 4.0f / 7.0f, 3.0f / 7.0f, 2.0f / 7.0f, 1.0f / 7.0f, 0.0f / 7.0f };
	static const Float pD8[] = { 0.0f / 7.0f, 1.0f / 7.0f, 2.0f / 7.0f, 3.0f / 7.0f, 4.0f / 7.0f, 5.0f / 7.0f, 6.0f / 7.0f, 7.0f / 7.0f };

	const Float *pC = (6 == cSteps) ? pC6 : pC8;
	const Float *pD = (6 == cSteps) ? pD6 : pD8;

	const Float MAX_VALUE = 1.0f;
	const Float MIN_VALUE = 0.0f;

	// Find Min and Max points, as starting point
	Float fX = MAX_VALUE;
	Float fY = MIN_VALUE;

	if ( 8 == cSteps )
	{
		for ( size_t iPoint = 0; iPoint < numTexels; iPoint++ )
		{
			if ( pPoints[iPoint] < fX )
				fX = pPoints[iPoint];

			if ( pPoints[iPoint] > fY )
				fY = pPoints[iPoint];
		}
	}
	else
	{
		for ( size_t iPoint = 0; iPoint < numTexels; iPoint++ )
		{
			if ( pPoints[iPoint] < fX && pPoints[iPoint] > MIN_VALUE )
				fX = pPoints[iPoint];

			if ( pPoints[iPoint] > fY && pPoints[iPoint] < MAX_VALUE )
				fY = pPoints[iPoint];
		}

		if ( fX == fY )
		{
			fY = MAX_VALUE;
		}
	}

	// Use Newton's Method to find local minima of sum-of-squares error.
	Float fSteps = (Float)(cSteps - 1);

	for ( size_t iIteration = 0; iIteration < 8; iIteration++ )
	{
		Float fScale;

		if ( (fY - fX) < (1.0f / 256.0f) )
			break;

		fScale = fSteps / (fY - fX);

		// Calculate new steps
		Float pSteps[8];

		for ( size_t iStep = 0; iStep < cSteps; iStep++ )
			pSteps[iStep] = pC[iStep] * fX + pD[iStep] * fY;

		if ( 6 == cSteps )
		{
			pSteps[6] = MIN_VALUE;
			pSteps[7] = MAX_VALUE;
		}

		// Evaluate function, and derivatives
		Float dX = 0.0f;
		Float dY = 0.0f;
		Float d2X = 0.0f;
		Float d2Y = 0.0f;

		for ( size_t iPoint = 0; iPoint < numTexels; iPoint++ )
		{
			Float fDot = (pPoints[iPoint] - fX) * fScale;

			size_t iStep;

			if ( fDot <= 0.0f )
				iStep = ((6 == cSteps) && (pPoints[iPoint] <= fX * 0.5f)) ? 6 : 0;
			else if ( fDot >= fSteps )
				iStep = ((6 == cSteps) && (pPoints[iPoint] >= (fY + 1.0f) * 0.5f)) ? 7 : (cSteps - 1);
			else
				iStep = static_cast<int32_t>(fDot + 0.5f);


			if ( iStep < cSteps )
			{
				// D3DX had this computation backwards (pPoints[iPoint] - pSteps[iStep])
				// this fix improves RMS of the alpha component
				Float fDiff = pSteps[iStep] - pPoints[iPoint];

				dX += pC[iStep] * fDiff;
				d2X += pC[iStep] * pC[iStep];

				dY += pD[iStep] * fDiff;
				d2Y += pD[iStep] * pD[iStep];
			}
		}

		// Move endpoints
		if ( d2X > 0.0f )
			fX -= dX / d2X;

		if ( d2Y > 0.0f )
			fY -= dY / d2Y;

		if ( fX > fY )
		{
			Float f = fX; fX = fY; fY = f;
		}

		if ( (dX * dX < (1.0f / 64.0f)) && (dY * dY < (1.0f / 64.0f)) )
			break;
	}

	*pX = (fX < MIN_VALUE) ? MIN_VALUE : (fX > MAX_VALUE) ? MAX_VALUE : fX;
	*pY = (fY < MIN_VALUE) ? MIN_VALUE : (fY > MAX_VALUE) ? MAX_VALUE : fY;
}
//#pragma warning(pop)

//------------------------------------------------------------------------------
static void FindEndPointsBC4U( const Float* theTexelsU, Uint32 numTexels, Uint8 &endpointU_0, Uint8 &endpointU_1 )
{
	// The boundary of codec for signed/unsigned format
	const Float MIN_NORM = 0.f;
	const Float MAX_NORM = 1.f;

	// Find max/min of input texels
	Float fBlockMax = theTexelsU[0];
	Float fBlockMin = theTexelsU[0];
	for ( size_t i = 0; i < numTexels; ++i )
	{
		if ( theTexelsU[i] < fBlockMin )
		{
			fBlockMin = theTexelsU[i];
		}
		else if ( theTexelsU[i] > fBlockMax )
		{
			fBlockMax = theTexelsU[i];
		}
	}

	//  If there are boundary values in input texels, should use 4 interpolated color values to guarantee
	//  the exact code of the boundary values.
	Bool bUsing4BlockCodec = (MIN_NORM == fBlockMin || MAX_NORM == fBlockMax);

	// Using Optimize
	Float fStart, fEnd;

	if ( !bUsing4BlockCodec )
	{
		// 6 interpolated color values
		OptimizeAlpha( &fStart, &fEnd, theTexelsU, numTexels, 8 );

		Uint8 iStart = static_cast<Uint8>(fStart * 255.0f);
		Uint8 iEnd = static_cast<Uint8>(fEnd * 255.0f);

		endpointU_0 = iEnd;
		endpointU_1 = iStart;
	}
	else
	{
		// 4 interpolated color values
		OptimizeAlpha( &fStart, &fEnd, theTexelsU, numTexels, 6 );

		Uint8 iStart = static_cast<Uint8>(fStart * 255.0f);
		Uint8 iEnd = static_cast<Uint8>(fEnd * 255.0f);

		endpointU_1 = iEnd;
		endpointU_0 = iStart;
	}
}



struct BC4_UNORM
{
	Float R( size_t uOffset ) const
	{
		size_t uIndex = GetIndex( uOffset );
		return DecodeFromIndex( uIndex );
	}

	Float DecodeFromIndex( size_t uIndex ) const
	{
		if ( uIndex == 0 )
			return red_0 / 255.0f;
		if ( uIndex == 1 )
			return red_1 / 255.0f;
		Float fred_0 = red_0 / 255.0f;
		Float fred_1 = red_1 / 255.0f;
		if ( red_0 > red_1 )
		{
			uIndex -= 1;
			return (fred_0 * (7 - uIndex) + fred_1 * uIndex) / 7.0f;
		}
		else
		{
			if ( uIndex == 6 )
				return 0.0f;
			if ( uIndex == 7 )
				return 1.0f;
			uIndex -= 1;
			return (fred_0 * (5 - uIndex) + fred_1 * uIndex) / 5.0f;
		}
	}

	size_t GetIndex( size_t uOffset ) const
	{
		return (size_t)((data >> (3 * uOffset + 16)) & 0x07);
	}

	void SetIndex( size_t uOffset, size_t uIndex )
	{
		data &= ~((Uint64)0x07 << (3 * uOffset + 16));
		data |= ((Uint64)uIndex << (3 * uOffset + 16));
	}

	union
	{
		struct
		{
			Uint8 red_0;
			Uint8 red_1;
			Uint8 indices[6];
		};
		Uint64 data;
	};
};


//------------------------------------------------------------------------------
static void FindClosestUNORM(
	BC4_UNORM* pBC,
	const Float theTexelsU[] )
{
	Float rGradient[8];
	for ( size_t i = 0; i < 8; ++i )
	{
		rGradient[i] = pBC->DecodeFromIndex( i );
	}

	for ( size_t i = 0; i < NUM_PIXELS_PER_BLOCK; ++i )
	{
		size_t uBestIndex = 0;
		Float fBestDelta = 100000;
		for ( size_t uIndex = 0; uIndex < 8; uIndex++ )
		{
			Float fCurrentDelta = fabsf( rGradient[uIndex] - theTexelsU[i] );
			if ( fCurrentDelta < fBestDelta )
			{
				uBestIndex = uIndex;
				fBestDelta = fCurrentDelta;
			}
		}
		pBC->SetIndex( i, uBestIndex );
	}
}

static void D3DXEncodeBC4U( Uint64* outBlock, const Float* inValues4x4, const Float* refValues, Uint32 numRefValues )
{
	RED_ASSERT( outBlock && inValues4x4, "" );
	static_assert(sizeof( BC4_UNORM ) == 8, "BC4_UNORM should be 8 bytes");

	memset( outBlock, 0, sizeof( BC4_UNORM ) );
	auto pBC4 = reinterpret_cast<BC4_UNORM*>(outBlock);

	FindEndPointsBC4U( refValues, numRefValues, pBC4->red_0, pBC4->red_1 );
	FindClosestUNORM( pBC4, inValues4x4 );
}


static void D3DXDecodeBC4U( Float *pColor, Uint64 pBC )
{
	RED_ASSERT( pColor, "" );
	static_assert(sizeof( BC4_UNORM ) == 8, "BC4_UNORM should be 8 bytes");

	auto pBC4 = reinterpret_cast<const BC4_UNORM*>(&pBC);

	for ( size_t i = 0; i < NUM_PIXELS_PER_BLOCK; ++i )
	{
		pColor[i] = pBC4->R( i );
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RED_INLINE Color EvaluateHeatmap( Float v )
{
	Vector4 c;
	if ( v < 0.25f )
	{
		c = math::Lerp<Vector4>( v / 0.25f, Color::WHITE().ToVector(), Color::BLUE().ToVector() );
	}
	else if ( v < 0.5f )
	{
		c = math::Lerp<Vector4>( (v - 0.25f) / 0.25f, Color::BLUE().ToVector(), Color::GREEN().ToVector() );
	}
	else if ( v < 0.75f )
	{
		c = math::Lerp<Vector4>( (v - 0.5f) / 0.25f, Color::GREEN().ToVector(), Color::YELLOW().ToVector() );
	}
	else
	{
		c = math::Lerp<Vector4>( (v - 0.75f) / 0.25f, Color::YELLOW().ToVector(), Color::RED().ToVector() );
	}
	c.W = 1.0f;
	return Color( c );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MultilayerMaskBuilder::AtlasTile
{
	Uint32 sourceLayer;			// Layer where this tile's data can be found
	Uint32 sourceTileIndex;		// Index in source layer
	Uint32 atlasLocation;		// Where in the final atlas this data will be
};

// Full mask for a single layer
struct MultilayerMaskBuilder::MaskLayer
{
	struct Tile
	{
		red::DynArray<Uint8> atlasPixels { red::PoolBackend() };
		red::DynArray<Uint64> atlasCompressedData { red::PoolBackend() };

		Uint8 minValue, maxValue;

		Uint32 atlasLocation;
	};

	red::DynArray<Uint8> pixels { red::PoolBackend() };		// Original image
	Uint32 width;
	Uint32 height;
	Uint32 widthInTiles;
	Uint32 heightInTiles;

	Uint32 widthShift;
	Uint32 heightShift;

	red::DynArray<Tile> tiles { red::PoolBackend() };
};

struct MultilayerMaskBuilder::VMaskTileLayer
{
	Uint32 layerIndex;
	Uint32 layerTileIndex;
	Uint32 atlasLocation;
};

// A tile in the virtual mask. Essentially just a stack of layers.
struct MultilayerMaskBuilder::VMaskTile
{
	red::DynArray<VMaskTileLayer> layers;

	VMaskTile() : layers( red::PoolBackend() ) {}
};



MultilayerMaskBuilder::MultilayerMaskBuilder()
	: m_numLayers( 0 )
	, m_maskTileSize( 0 )
	, m_maxMaskWidthHigh( 0 )
	, m_maxMaskHeightHigh( 0 )
	, m_maxMaskWidthLow( 0 )
	, m_maxMaskHeightLow( 0 )
	, m_maxMaskWidthInTilesHigh( 0 )
	, m_maxMaskHeightInTilesHigh( 0 )
	, m_maxMaskWidthInTilesLow( 0 )
	, m_maxMaskHeightInTilesLow( 0 )
	, m_atlasTileSize( 0 )
	, m_atlasWidth( 0 )
	, m_atlasHeight( 0 )
	, m_atlasNumTiles( 0 )
{
}

MultilayerMaskBuilder::~MultilayerMaskBuilder()
{
}


void MultilayerMaskBuilder::ReserveLayers( Uint32 numLayers )
{
	RED_ASSERT( numLayers > 0, "Cannot generate a mask with no layers" );

	if ( numLayers > m_maskLayers.Size() )
	{
		m_maskLayers.Resize( numLayers );
		m_maskLayerBiasesPC.Resize( numLayers );
		m_maskLayerBiasesConsole.Resize( numLayers );
	}
}


void MultilayerMaskBuilder::SetLayerSource( Uint32 layerIndex, const LayerSource& source, Bool useVerticalFlipOfSourceData )
{
	RED_ASSERT( layerIndex > 0, "Cannot set a source for layer 0" );

	if ( HasError() )
	{
		return;
	}

	m_numLayers = Max( layerIndex+1, m_numLayers );
	m_maskLayers.Resize( m_numLayers );
	m_maskLayerBiasesPC.Resize( m_numLayers );
	m_maskLayerBiasesConsole.Resize( m_numLayers );

	CopySingleLayer( layerIndex, source, useVerticalFlipOfSourceData );
	m_maskLayerBiasesPC[layerIndex] = source.bias;
	m_maskLayerBiasesConsole[layerIndex] = source.biasConsole;
}


void MultilayerMaskBuilder::SetLayerSources( red::ArraySpan<const LayerSource> sources, Bool useVerticalFlipOfSourceData )
{
	if ( sources.Empty() )
	{
		m_numLayers = 1;
		m_maskLayers.Resize( m_numLayers );
		m_maskLayerBiasesPC.Resize( m_numLayers );
		m_maskLayerBiasesConsole.Resize( m_numLayers );
	}
	else
	{
		m_numLayers = sources.Size() + 1;
		m_maskLayers.Resize( m_numLayers );
		m_maskLayerBiasesPC.Resize( m_numLayers );
		m_maskLayerBiasesConsole.Resize( m_numLayers );

		for ( Uint32 i = 0; i < sources.Size(); ++i )
		{
			if ( HasError() )
			{
				return;
			}

			const LayerSource& source = sources[i];

			// +1 -- We're being given data only for layers 1+, but our internal storage is for all layers. So need to
			// adjust indexing to account for that.

			CopySingleLayer( i+1, source, useVerticalFlipOfSourceData );

			m_maskLayerBiasesPC[i+1] = sources[i].bias;
			m_maskLayerBiasesConsole[i+1] = sources[i].biasConsole;
		}
	}
}

bool MultilayerMaskBuilder::GeneratePlatformData(GeneratedPlatformData& platformData, Uint32 platformBias)
{
	DEBUG_DumpSplitLayers(m_maskLayers, "original");

	DetailedProfilingData& prof = m_detailedProfiling[(Uint8)m_generationSettings.platform];

	// downsample layers according to biases + platformBias
	red::DynArray<MaskLayer> downsampledMaskLayers( m_maskLayers.Size(), red::PoolBackend() );

	{
		red::ScopedStopWatch sw(prof.downsampleLayers);

		// the 0 layer is always the same
		downsampledMaskLayers[0] = m_maskLayers[0];

		const red::DynArray< Uint32 >& platformBiases = ( m_generationSettings.platform == EDataGenerationPlatform::Consoles ) ? m_maskLayerBiasesConsole : m_maskLayerBiasesPC;

		// skipping the 0 layer
		for (Uint32 i = 1; i < m_maskLayers.Size(); ++i)
		{
			const auto& source = m_maskLayers[i];
			red::DynArray<Uint8> sourcePixels = m_maskLayers[i].pixels;
			auto& dest = downsampledMaskLayers[i];
			const Uint32 destBias = platformBias + platformBiases[i];

			if (destBias > 0)
			{
				if (((source.width >> destBias) << destBias) != source.width || ((source.height >> destBias) << destBias) != source.height)
				{
					m_error = String::Printf("Layer %u (%ux%u) cannot be biased by %u", i, source.width, source.height, destBias);
					return false;
				}

				dest.width = source.width >> destBias;
				dest.height = source.height >> destBias;

				{
					//downsample in place, we have our temporary sourcePixels copy
					Uint32 pitch = source.width;
					for (Uint32 bias = 1; bias <= destBias; ++bias)
					{
						Uint32 biasedWidth = source.width >> bias;
						Uint32 biasedHeight = source.height >> bias;

						for (Uint32 y = 0; y < biasedHeight; ++y)
						{
							for (Uint32 x = 0; x < biasedWidth; ++x)
							{
								Uint32 X0 = x * 2;
								Uint32 X1 = x * 2 + 1;
								Uint32 Y0 = y * 2;
								Uint32 Y1 = y * 2 + 1;

								// promoted to 16bits to not run out
								Uint16 pixTL = sourcePixels[Y0 * pitch + X0];
								Uint16 pixTR = sourcePixels[Y0 * pitch + X1];
								Uint16 pixBL = sourcePixels[Y1 * pitch + X0];
								Uint16 pixBR = sourcePixels[Y1 * pitch + X1];

								// bilinear ;)
								sourcePixels[y * pitch + x] = (pixTL + pixTR + pixBL + pixBR) / 4;
							}
						}
					}
				}

				// copy to the destination
				dest.pixels = red::DynArray<Uint8>(dest.width*dest.height, red::PoolBackend());
				for (Uint32 y = 0; y < dest.height; y++)
				{
					for (Uint32 x = 0; x < dest.width; x++)
					{
						dest.pixels[y * dest.width + x] = sourcePixels[y * source.width + x];
					}
				}
			}
			else
			{
				downsampledMaskLayers[i] = m_maskLayers[i];
			}
		}

	}

	DEBUG_DumpSplitLayers(downsampledMaskLayers, "downsampled");

	// Scan layer resolutions to pick appropriate High/Low sizes.
	// #TODO: Instead of just taking high res for High, and putting the rest in Low, could do it adaptively to find the
	// best balance for a particular asset. Can be done later, doesn't affect the data, just the importer.
	m_maxMaskWidthHigh = 0;
	m_maxMaskHeightHigh = 0;
	m_maxMaskWidthLow = 0;
	m_maxMaskHeightLow = 0;
	for ( const auto& source : downsampledMaskLayers )
	{
		if ( source.width > m_maxMaskWidthHigh )
		{
			m_maxMaskWidthLow = Max( m_maxMaskWidthLow, m_maxMaskWidthHigh );
			m_maxMaskHeightLow = Max( m_maxMaskHeightLow, m_maxMaskHeightHigh );

			m_maxMaskWidthHigh = Max( m_maxMaskWidthHigh, source.width );
			m_maxMaskHeightHigh = Max( m_maxMaskHeightHigh, source.height );
		}
		else if ( source.width < m_maxMaskWidthHigh )
		{
			m_maxMaskWidthLow = Max( m_maxMaskWidthLow, source.width );
			m_maxMaskHeightLow = Max( m_maxMaskHeightLow, source.height );
		}
	}

	// We have maximum layer size, so now go through and calculate the appropriate shift amount to go from the max size
	// to each layer.
	{
		for (auto& layer : downsampledMaskLayers)
		{
			layer.widthShift = red::BitUtils::Log2( m_maxMaskWidthHigh / layer.width );
			layer.heightShift = red::BitUtils::Log2( m_maxMaskHeightHigh / layer.height );
		}
	}

	//
	// Process the original mask images, generate a new atlas with just the tiles that have data.
	//

	// Our tile packing only allows 17 bits for location in the atlas. In some cases, with large source masks, before
	// any layers can be biased, we can end up exceeding that. So then the mlmask can't be imported and biases can't be
	// given at all. To work around that, if we have too many tiles then we'll just try increasing the tile size. This
	// increases the generation time in those overflow cases, but realistically we should only really need to try one
	// extra time (should cut the number of tiles roughly to 1/4).
	// #TODO: Instead of only doing it as a fallback, try different sizes just to see which results in smallest data?
	const Uint32 maxAtlasTileSize = 128;
	Uint32 nextAtlasTileSizeToTry = 16;
	do
	{
		if ( nextAtlasTileSizeToTry > maxAtlasTileSize )
		{
			m_error = "Failed to fit layers. Too much non-empty space, and too high resolution. Try reducing resolution, or reduce layer complexity.";
			return false;
		}

		m_atlasTileSize = nextAtlasTileSizeToTry;
		m_maskTileSize = ( m_atlasTileSize - 2 );

		nextAtlasTileSizeToTry += 16;

		m_vmaskTilesHigh.Clear();
		m_vmaskTilesLow.Clear();
		m_atlasTiles.Clear();
		m_atlasWidth = 0;
		m_atlasHeight = 0;
		m_atlasNumTiles = 0;

		{
			red::ScopedStopWatch sw(prof.prepareLayers);
			PrepareLayers(downsampledMaskLayers);
		}

		m_maxMaskWidthInTilesHigh = ( m_maxMaskWidthHigh + (m_maskTileSize-1) ) / m_maskTileSize;
		m_maxMaskHeightInTilesHigh = ( m_maxMaskHeightHigh + (m_maskTileSize-1) ) / m_maskTileSize;
		m_maxMaskWidthInTilesLow = ( m_maxMaskWidthLow + (m_maskTileSize-1) ) / m_maskTileSize;
		m_maxMaskHeightInTilesLow = ( m_maxMaskHeightLow + (m_maskTileSize-1) ) / m_maskTileSize;

		m_vmaskTilesHigh.Resize( m_maxMaskWidthInTilesHigh * m_maxMaskHeightInTilesHigh );
		m_vmaskTilesLow.Resize( m_maxMaskWidthInTilesLow * m_maxMaskHeightInTilesLow );

		{
			red::ScopedStopWatch sw(prof.filterTileLayers);
			FilterTileLayers(downsampledMaskLayers);
		}
	} while ( m_atlasTiles.Size() > (1<<17) );

	DEBUG_DumpLayerHeatMap("layers_heat");

	{
		red::ScopedStopWatch sw(prof.fitAtlasToTiles);
		FitAtlasToTiles();
	}

	if (m_atlasNumTiles > 0 && ( m_atlasWidth == 0 || m_atlasHeight == 0 ) )
	{
		m_error = String::Printf("Failed to fit mask to atlas. Mask is %ux%u with %u layers. Trying to fit %u %ux%u tiles. Reduce the mask resolution, or reduce the amount of overlap between layers",
			m_maxMaskWidthHigh, m_maxMaskHeightHigh, downsampledMaskLayers.Size(), m_atlasNumTiles, m_atlasTileSize, m_atlasTileSize);
		return false;
	}

	red::UniqueBuffer finalAtlas;
	{
		red::ScopedStopWatch sw(prof.generateAtlasImage);
		finalAtlas = GenerateAtlasImage(downsampledMaskLayers);
	}

	if (m_generationSettings.compressTextures)
	{
		DEBUG_DumpCompressedAtlas(finalAtlas.Get());
		DEBUG_DumpReconstructedLayers(downsampledMaskLayers, finalAtlas.Get());
	}

	{
		red::ScopedStopWatch sw(prof.fillPlatformData);

		platformData.atlasBuffer = serialization::BufferHandleRO(std::move(finalAtlas));

		{
			platformData.header.m_version = rend::RenderMultilayerMaskBlob::CURRENT_VERSION;
			platformData.header.m_flags = m_generationSettings.compressTextures ? rend::MLMask_UseMultiresolution : ( rend::MLMask_UseMultiresolution | rend::MLMask_Uncompressed );
			platformData.header.m_maskWidth = m_maxMaskWidthHigh;
			platformData.header.m_maskHeight = m_maxMaskHeightHigh;
			platformData.header.m_maskWidthLow = m_maxMaskWidthLow;
			platformData.header.m_maskHeightLow = m_maxMaskHeightLow;
			platformData.header.m_maskTileSize = m_maskTileSize;
			platformData.header.m_atlasWidth = m_atlasWidth;
			platformData.header.m_atlasHeight = m_atlasHeight;
			platformData.header.m_numLayers = m_numLayers;
		}

		{
			const Uint32 tilesCountHigh = m_maxMaskWidthInTilesHigh * m_maxMaskHeightInTilesHigh;
			const Uint32 tilesCountLow = m_maxMaskWidthInTilesLow * m_maxMaskHeightInTilesLow;
			platformData.tilesData.Resize( (tilesCountHigh + tilesCountLow)*2 );

			auto PackTiles = [this, &platformData, &downsampledMaskLayers](
				Uint32 baseTileIndex, Uint32 maxMaskWidthInTiles, Uint32 maxMaskHeightInTiles, red::DynArray<VMaskTile>& vmaskTiles )
			{
				const Uint32 atlasSizeInTilesX = m_atlasWidth / m_atlasTileSize;
				const Uint32 atlasWidthInTilesShift = MLog2( atlasSizeInTilesX );
				const Uint32 atlasWidthInTilesMask = (1<<atlasWidthInTilesShift)-1;

				for (Uint32 ty = 0; ty < maxMaskHeightInTiles; ++ty)
				{
					for (Uint32 tx = 0; tx < maxMaskWidthInTiles; ++tx)
					{
						const Uint32 tileIndex = (tx + ty * maxMaskWidthInTiles);
						const VMaskTile& maskTile = vmaskTiles[tileIndex];
						const Uint32 layersStart = platformData.tilesData.Size();
						const Uint32 layersCount = maskTile.layers.Size();

						platformData.tilesData[(baseTileIndex + tileIndex)*2+0] = layersStart;

						Uint32 layersMask = 0;
						for (Uint32 layer_i = 0; layer_i < layersCount; ++layer_i)
						{
							const VMaskTileLayer& layer = maskTile.layers[layer_i];
							const Uint32 layerIndex = layer.layerIndex;
							const Uint32 atlasLocation = layer.atlasLocation;
							const Uint32 atlasX = atlasLocation & atlasWidthInTilesMask;
							const Uint32 atlasY = atlasLocation >> atlasWidthInTilesShift;
							const Uint32 maskWidthShift = downsampledMaskLayers[layerIndex].widthShift;
							const Uint32 maskHeightShift = downsampledMaskLayers[layerIndex].heightShift;

							RED_ASSERT( atlasLocation < (1 << 17), "atlasLocation overflow (%u). Should have been caught while generating tiles." );
							RED_ASSERT( atlasX < (1 << 10) && atlasY < (1 << 10), "Tile has invalid position in atlas: %u, %u", atlasX, atlasY );
							RED_ASSERT( maskWidthShift < (1 << 4) );
							RED_ASSERT( maskHeightShift < (1 << 4) );
							RED_ASSERT( layerIndex < 32 );

							Uint32 packedLayer = 0;
							packedLayer |= (atlasX << 0);
							packedLayer |= (atlasY << 10);
							packedLayer |= (maskWidthShift << 20);
							packedLayer |= (maskHeightShift << 24);

							platformData.tilesData.PushBack( packedLayer );

							layersMask |= (1u << layerIndex);
						}

						platformData.tilesData[(baseTileIndex + tileIndex)*2+1] = layersMask;
					}
				}
			};

			PackTiles( 0, m_maxMaskWidthInTilesHigh, m_maxMaskHeightInTilesHigh, m_vmaskTilesHigh );
			PackTiles( tilesCountHigh, m_maxMaskWidthInTilesLow, m_maxMaskHeightInTilesLow, m_vmaskTilesLow );
		}
	}

	return true;
}

class CMultilayerMaskCompilationSource : public tools::ITextureCompilationSource, public red::NonCopyable
{
	Uint16 m_width;
	Uint16 m_height;
	STextureGroupSetup m_setup;
	res::ResourcePath m_path; // this shouldn't be needed but the interface demands it

public:

	CMultilayerMaskCompilationSource(Uint16 width, Uint16 height, ECookingPlatform platform, Bool isUncompressed)
		: tools::ITextureCompilationSource(platform)
		, m_width(width)
		, m_height(height)
	{
		//NOTE this should be enough information for the texture cooking, if anything changes in the cooking code this might need to be changed though
		m_setup.m_rawFormat = TRF_Grayscale;
		m_setup.m_compression = isUncompressed ? TCM_None : TCM_QualityR;
		m_setup.m_isGamma = false;
	}

	virtual const res::ResourcePath& GetTextureName() const override
	{
		RED_HALT("Not implemented");
		return m_path;
	}

	virtual Uint8 GetMaxMipCount() const override { return 1; }
	virtual Uint8 GetMipCount() const override { return 1; }
	virtual Uint8 GetStartMip() const override { return 0; }

	virtual Uint16 GetBaseWidth() const override { return m_width; }
	virtual Uint16 GetBaseHeight() const override { return m_height; }
	virtual Uint16 GetBaseDepth() const override { return 1; }
	virtual Uint32 GetDownscaleBias() const override { return 0; }

	virtual Uint16 GetSliceCount() const override { return 1; }
	virtual img::ImagePtr GetImage(Uint16 texSlice) const override
	{
		RED_HALT("Not implemented");
		return nullptr;
	}

	virtual GpuWrapApi::eTextureType GetTextureType() const override { return GpuWrapApi::TEXTYPE_2D; }
	virtual const STextureGroupSetup& GetTextureSetup() const override { return m_setup; }
};


void MultilayerMaskBuilder::CookPlatformData( ECookingPlatform platform, THandle<rend::RenderMultilayerMaskBlob> &renderBlob, GeneratedPlatformData &platformData, Bool fastPath)
{
	renderBlob->m_header = platformData.header;
	renderBlob->m_tilesData.Editor_SetContentDirect(platformData.tilesData.Data(), platformData.tilesData.DataSize());

	if (fastPath || platform == ECookingPlatform::PLATFORM_PC)
	{
		// On PC we don't have any platform specific tiling, we can just use the compressed data as is
		renderBlob->m_atlasData.Editor_SetContentDirect(platformData.atlasBuffer);
	}
	else
	{
		// cook the atlas texture
		Uint32 atlasWidth = renderBlob->m_header.m_atlasWidth;
		Uint32 atlasHeight = renderBlob->m_header.m_atlasHeight;

		GpuWrapApi::eTextureFormat format = platformData.header.IsUncompressed() ? GpuWrapApi::eTextureFormat::TEXFMT_R8_Unorm : GpuWrapApi::eTextureFormat::TEXFMT_BC4;

		CMultilayerMaskCompilationSource compilationSource(atlasWidth, atlasHeight, platform, platformData.header.IsUncompressed());
		tools::MipMap mipMap(atlasWidth, atlasHeight, 1, GpuWrapApi::CalculateUnpaddedTexturePitch(atlasWidth, format) );
		mipMap.m_data.Reallocate(platformData.atlasBuffer.GetSize());
		red::Memcpy(mipMap.m_data.Get(), platformData.atlasBuffer.Data(), platformData.atlasBuffer.GetSize());
		red::DynArray< tools::MipArray > mipsArray(1);
		mipsArray[0].PushBack(std::move(mipMap));
		rend::RenderTextureBlobHeader blobHeader;
		red::UniqueBuffer blob;

		Bool cooked = false;

		switch (platform)
		{
		case PLATFORM_PS4:			cooked = tools::RenderTextureBlobCompiler::CompileTextureFunc_PS4(compilationSource, mipsArray, blobHeader, blob);			break;
		case PLATFORM_XboxOne:		cooked = tools::RenderTextureBlobCompiler::CompileTextureFunc_XBoxOne(compilationSource, mipsArray, blobHeader, blob);		break;
		default:
			RED_LOG_WARNING("multilayer mask cooking: unexpected platform, setting data as is, without cooking");
			renderBlob->m_atlasData.Editor_SetContentDirect(platformData.atlasBuffer);
			return;
		}

		RED_ASSERT(cooked, "Failed to cook atlas data for MLMask");
		if (cooked)
		{
			renderBlob->m_atlasData.Editor_SetContentDirect(blob.Get(), blob.GetSize());
			renderBlob->m_header.m_flags |= rend::MLMask_Tiled;
		}
	}
}

void MultilayerMaskBuilder::Generate( THandle<Multilayer_Mask>& outMask, Bool setSourceData, Bool fastPath )
{
	// Validate all layer sources. They must all be a power-of-2 away from each other, with the same aspect ratios.
	{
		Uint32 maxWidth = 0;
		Uint32 maxHeight = 0;
		for ( Uint32 layerI = 1; layerI < m_maskLayers.Size(); ++layerI )
		{
			const MaskLayer& layer = m_maskLayers[layerI];
			maxWidth = Max( maxWidth, layer.width );
			maxHeight = Max( maxHeight, layer.height );
		}

		for ( Uint32 layerI = 1; layerI < m_maskLayers.Size(); ++layerI )
		{
			const MaskLayer& layer = m_maskLayers[layerI];

			Uint32 widthDiv = maxWidth / layer.width;
			Uint32 heightDiv = maxHeight / layer.height;

			if ( widthDiv != heightDiv )
			{
				m_error = "All layers must have the same aspect ratio";
				return;
			}
			if ( ((widthDiv * layer.width) != maxWidth) || ((heightDiv * layer.height) != maxHeight) )
			{
				m_error = "All layers must have dimensions that evenly divide each other";
				return;
			}
			if ( !red::IsPowerOf2( widthDiv ) )
			{
				m_error = "All layers must have dimensions that are a power-of-two away from the maximum";
				return;
			}
		}
	}

	m_generationSettings.compressTextures = !fastPath;
	m_generationSettings.generateConsoleData = !fastPath;
	m_generationSettings.outputProfilingData = false; // enable this to log profiling data
	m_generationSettings.useMemcpy = false; // enable this to log profiling data

	m_hlProfiling.Reset();
	m_detailedProfiling[0].Reset();
	m_detailedProfiling[1].Reset();

	if ( HasError() )
	{
		return;
	}

	Bool isNewMask = ( outMask == nullptr );
	if ( isNewMask )
	{
		outMask = CreateHandle<Multilayer_Mask>();
	}

	if ( m_numLayers > 255 )
	{
		m_error = "Mask has more than 255 layers. This is currently not supported.";
		return;
	}

	red::StopWatch sw_Total;

	if ( !m_maskLayers.Empty() )
	{
		m_maskLayers[0].width = 4;
		m_maskLayers[0].height = 4;
		m_maskLayers[0].pixels.Resize( 16, 255 );
		m_maskLayerBiasesPC[0] = 0;
		m_maskLayerBiasesConsole[0] = 0;
	}

	//
	// Set the source data
	// #TODO: when regenerating a mask with the same source data, this is just a waste. Probably want to pass in some
	// option to say whether or not to update source data.
	//
	if (!m_maskLayers.Empty())
	{
		red::ScopedStopWatch sw(m_hlProfiling.setSourceData);

		red::DynArray<THandle<CSourceTexture>> sourceTextures{ rend::PoolTexture() };
		sourceTextures.Reserve(m_maskLayers.Size() - 1);
		for (Uint8 i = 1; i < m_maskLayers.Size(); i++)
		{
			auto& maskLayer = m_maskLayers[i];
			auto sourceTexture = CreateHandle< CSourceTexture >();

			sourceTexture->Init(maskLayer.width, maskLayer.height, 1, ETextureRawFormat::TRF_Grayscale);

			img::ImagePtr image = img::Image::CreateEmpty(img::PixelFormat::Format_Uint8, img::Image::SizeInfo(1, maskLayer.width, maskLayer.height, 1));

			Uint8* destPixels = (Uint8*)image->GetPixelBuffer();
			const Uint8* srcPixels = maskLayer.pixels.TypedData();

			if (image->GetPixelBufferSize() == maskLayer.pixels.DataSize())
			{
				// If the image size matches the source exactly we can do a single memcpy for the entire thing.
				red::Memcpy(destPixels, srcPixels, image->GetPixelBufferSize());
			}
			else
			{
				// If they don't match, then the image added some padding and we need to do row-by-row copy.
				const Uint32 destRowPitch = image->GetLayout().m_rowPitch;
				const Uint32 srcRowPitch = maskLayer.width*sizeof(Uint8);
				RED_ASSERT( destRowPitch >= srcRowPitch, "Expect dest (%u) to be padded up compared to src (%u)", destRowPitch, srcRowPitch );
				for (Uint32 y = 0; y < maskLayer.height; ++y)
				{
					red::Memcpy(destPixels, srcPixels, srcRowPitch);
					destPixels += destRowPitch;
					srcPixels += srcRowPitch;
				}
			}

			sourceTexture->SetImage(image);
			sourceTextures.PushBack(sourceTexture);
		}

		outMask->SetSourceData(sourceTextures);

		// make sure the correct biases are set
		// as above, we are starting from index 1 because we have the 0 layer in the array but we don't need to set that in the resource
		for (Uint8 i = 1; i < m_maskLayerBiasesPC.Size(); i++)
		{
			outMask->SetLayerBias( i, ECookingPlatform::PLATFORM_PC, m_maskLayerBiasesPC[i] );
			outMask->SetLayerBias( i, ECookingPlatform::PLATFORM_PS4, m_maskLayerBiasesConsole[i] );
		}
	}

	//
	// Build the actual render blob objects
	//
	{
		m_generationSettings.platform = EDataGenerationPlatform::PC;
		red::ScopedStopWatch sw(m_hlProfiling.generatePlatformData_PC);
		GeneratedPlatformData platformData;
		if (GeneratePlatformData(platformData, 0))
		{
			red::ScopedStopWatch sw(m_hlProfiling.cookPlatformData_PC);
			THandle<rend::RenderMultilayerMaskBlob> renderBlobPC = rend::RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobPC();
			CookPlatformData(PLATFORM_PC, renderBlobPC, platformData, fastPath);
			outMask->InitRenderResourceBlob(PLATFORM_PC, renderBlobPC);
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( outMask->GetPath() ), "Failed to generate PC mask data. %", m_error.AsChar() );
		}
	}

	if (m_generationSettings.generateConsoleData)
	{
		m_generationSettings.platform = EDataGenerationPlatform::Consoles;
		red::ScopedStopWatch sw(m_hlProfiling.generatePlatformData_Consoles);
		GeneratedPlatformData platformData;
		bool generated = GeneratePlatformData(platformData, 0);
		if (generated)
		{
			// XB1 and PS4 uses the same generated data (same platform bias)
			{
				red::ScopedStopWatch sw(m_hlProfiling.cookPlatformData_XB1);
				THandle<rend::RenderMultilayerMaskBlob> renderBlobXboxOne = rend::RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobXboxOne();
				CookPlatformData(PLATFORM_XboxOne, renderBlobXboxOne, platformData, fastPath);
				outMask->InitRenderResourceBlob(PLATFORM_XboxOne, renderBlobXboxOne);
			}

			{
				red::ScopedStopWatch sw(m_hlProfiling.cookPlatformData_PS4);
				THandle<rend::RenderMultilayerMaskBlob> renderBlobPS4 = rend::RenderMultilayerMaskBlob::CreateRenderMultilayerMaskBlobPS4();
				CookPlatformData(PLATFORM_PS4, renderBlobPS4, platformData, fastPath);
				outMask->InitRenderResourceBlob(PLATFORM_PS4, renderBlobPS4);
			}
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( outMask->GetPath() ), "Failed to generate console mask data. %", m_error.AsChar() );
		}
	}

	if (m_generationSettings.outputProfilingData)
	{
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "MLMask Builder Profiling %s:", m_generationSettings.compressTextures ? "" : "fast path");
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "  Total time %.2llf msec.", sw_Total.GetDelta() * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "  SetSourceData took %.2llf msec.", m_hlProfiling.setSourceData * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "  GeneratePlatformData_PC took %.2llf msec.", m_hlProfiling.generatePlatformData_PC * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      DownsampleLayers_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].downsampleLayers * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      PrepareLayers_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].prepareLayers * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FilterTileLayers_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].filterTileLayers * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "        AddTileToAtlas_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].addTileToAtlas_Total * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "        CompressTile_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].compressTile_Total * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FitAtlasToTiles_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].fitAtlasToTiles * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      GenerateAtlasImage_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].generateAtlasImage * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FillPlatformData_PC took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::PC].fillPlatformData * 1000.0);
		RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "    CookPlatformData_PC took %.2llf msec.", m_hlProfiling.cookPlatformData_PC * 1000.0);

		if (m_generationSettings.generateConsoleData)
		{
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "  GeneratePlatformData_Consoles took %.2llf msec.", m_hlProfiling.generatePlatformData_Consoles * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      DownsampleLayers_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].downsampleLayers * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      PrepareLayers_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].prepareLayers * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FilterTileLayers_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].filterTileLayers * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "        AddTileToAtlas_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].addTileToAtlas_Total * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "        CompressTile_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].compressTile_Total * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FitAtlasToTiles_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].fitAtlasToTiles * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      GenerateAtlasImage_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].generateAtlasImage * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "      FillPlatformData_Consoles took %.2llf msec.", m_detailedProfiling[(Uint8)EDataGenerationPlatform::Consoles].fillPlatformData * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "    CookPlatformData_XB1 took %.2llf msec.", m_hlProfiling.cookPlatformData_XB1 * 1000.0);
			RED_LOG_CATEGORY_INFO(red::LoggerCategory_Resources, "    CookPlatformData_XPS4 took %.2llf msec.", m_hlProfiling.cookPlatformData_PS4 * 1000.0);
		}
	}
}


Uint32 MultilayerMaskBuilder::GetNumLayers() const
{
	return m_numLayers;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


THandle<Multilayer_Mask> MultilayerMaskBuilder::Generate( red::ArraySpan<const Uint8* const> layerMasks, Uint32 width, Uint32 height, Uint32 rowStride,
                                                          THandle<Multilayer_Mask> existingMask, Bool useVerticalFlipOfSourceData, Bool useFastPath )
{
	THandle<Multilayer_Mask> result = existingMask;

	MultilayerMaskBuilder builder;

	red::DynArray<LayerSource> layerSources( layerMasks.Size(), red::PoolBackend() );
	for ( Uint32 i = 0; i < layerMasks.Size(); ++i )
	{
		layerSources[i].imageData = layerMasks[i];
		layerSources[i].format = GpuWrapApi::TEXFMT_R8_Unorm;
		layerSources[i].width = width;
		layerSources[i].height = height;
		layerSources[i].rowStride = rowStride;
	}
	builder.SetLayerSources( layerSources, useVerticalFlipOfSourceData );
	builder.Generate( result, true, useFastPath );

	if ( builder.HasError() )
	{
		RED_LOG_ERROR( "Failed to generate multilayer mask: %hs", builder.GetErrorString().AsChar() );
		result = nullptr;
	}

	return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MultilayerMaskBuilder::CopySingleLayer( Uint32 layerIndex, const LayerSource& source, Bool useVerticalFlipOfSourceData )
{
	MaskLayer& maskLayer = m_maskLayers[layerIndex];
	maskLayer.width = source.width;
	maskLayer.height = source.height;

	maskLayer.pixels = red::DynArray<Uint8>( source.width*source.height, red::PoolBackend() );

	Uint8* pix = maskLayer.pixels.TypedData();

	Bool u8Element = false;
	Bool u16Element = false;
	Bool u32Element = false;
	Bool f16Element = false;
	Bool f32Element = false;
	switch ( source.format )
	{
	case GpuWrapApi::TEXFMT_A8_Unorm:
	case GpuWrapApi::TEXFMT_L8_Unorm:
	case GpuWrapApi::TEXFMT_R8_Unorm:
	case GpuWrapApi::TEXFMT_R8_Uint:
	case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:
	case GpuWrapApi::TEXFMT_R8G8_Unorm:
		u8Element = true;
		break;
	case GpuWrapApi::TEXFMT_R16_Unorm:
	case GpuWrapApi::TEXFMT_R16_Uint:
	case GpuWrapApi::TEXFMT_R16G16_Uint:
	case GpuWrapApi::TEXFMT_R16G16_Sint:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Uint:
		u16Element = true;
		break;
	case GpuWrapApi::TEXFMT_R32_Uint:
	case GpuWrapApi::TEXFMT_R32G32_Uint:
	case GpuWrapApi::TEXFMT_R32G32B32A32_Uint:
		u32Element = true;
		break;
	case GpuWrapApi::TEXFMT_R16G16_Float:
	case GpuWrapApi::TEXFMT_R16_Float:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Float:
		f16Element = true;
		break;
	case GpuWrapApi::TEXFMT_R32G32_Float:
	case GpuWrapApi::TEXFMT_R32G32B32A32_Float:
	case GpuWrapApi::TEXFMT_R32_Float:
		f32Element = true;
	default:
		m_error = "Unsupported texture format";
		return;
	}

	Uint32 elementCount = 0;
	switch ( source.format )
	{
	case GpuWrapApi::TEXFMT_A8_Unorm:
	case GpuWrapApi::TEXFMT_L8_Unorm:
	case GpuWrapApi::TEXFMT_R8_Unorm:
	case GpuWrapApi::TEXFMT_R8_Uint:
	case GpuWrapApi::TEXFMT_R16_Unorm:
	case GpuWrapApi::TEXFMT_R16_Uint:
	case GpuWrapApi::TEXFMT_R32_Uint:
	case GpuWrapApi::TEXFMT_R16_Float:
	case GpuWrapApi::TEXFMT_R32_Float:
		elementCount = 1;
		break;
	case GpuWrapApi::TEXFMT_R8G8_Unorm:
	case GpuWrapApi::TEXFMT_R16G16_Uint:
	case GpuWrapApi::TEXFMT_R16G16_Sint:
	case GpuWrapApi::TEXFMT_R32G32_Uint:
	case GpuWrapApi::TEXFMT_R16G16_Float:
	case GpuWrapApi::TEXFMT_R32G32_Float:
		elementCount = 2;
		break;
	case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Uint:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:
	case GpuWrapApi::TEXFMT_R16G16B16A16_Float:
	case GpuWrapApi::TEXFMT_R32G32B32A32_Uint:
	case GpuWrapApi::TEXFMT_R32G32B32A32_Float:
		elementCount = 4;
		break;
	default:
		m_error = "Unsupported texture format";
		return;
	}

	if ( source.imageData == nullptr )
	{
		for ( Uint32 y = 0; y < source.height; ++y )
		{
			for ( Uint32 x = 0; x < source.width; ++x )
			{
				pix[x + y * source.width] = 0;
			}
		}
		return;
	}

	if ( u8Element )
	{
		Uint8* pixLayers = (Uint8*)source.imageData;
		for ( Uint32 y = 0; y < source.height; ++y )
		{
			Uint32 destY = useVerticalFlipOfSourceData ? (source.height - 1u - y) : y;
			for ( Uint32 x = 0; x < source.width; ++x )
			{
				pix[x+destY*source.width] = pixLayers[(x*elementCount) + (y*source.rowStride)];
			}
		}
	}
	else if ( u16Element )
	{
		Uint16* pixLayers = (Uint16*)source.imageData;
		for ( Uint32 y = 0; y < source.height; ++y )
		{
			Uint32 destY = useVerticalFlipOfSourceData ? (source.height - 1u - y) : y;
			for ( Uint32 x = 0; x < source.width; ++x )
			{
				pix[x+destY*source.width] = Uint8( Double( pixLayers[(x*elementCount) + (y*source.rowStride/2)] ) / Double( 0xFF ) );
			}
		}
	}
	else if ( u32Element )
	{
		Uint32* pixLayers = (Uint32*)source.imageData;
		for ( Uint32 y = 0; y < source.height; ++y )
		{
			Uint32 destY = useVerticalFlipOfSourceData ? (source.height - 1u - y) : y;
			for ( Uint32 x = 0; x < source.width; ++x )
			{
				pix[x+destY*source.width] = Uint8( Double( pixLayers[(x*elementCount) + (y*source.rowStride/4)] ) / Double( 0xFFFFFF ) );
			}
		}
	}
	else if ( f32Element )
	{
		Float* pixLayers = (Float*)source.imageData;
		for ( Uint32 y = 0; y < source.height; ++y )
		{
			Uint32 destY = useVerticalFlipOfSourceData ? (source.height - 1u - y) : y;
			for ( Uint32 x = 0; x < source.width; ++x )
			{
				pix[x+destY*source.width] = Uint8(pixLayers[(x*elementCount) + (y*source.rowStride/4)] * 255);
			}
		}
	}
	else
	{
		m_error = "Float16 images are not currently supported";
	}
}


void MultilayerMaskBuilder::PrepareLayers(red::DynArray<MaskLayer>& maskLayers)
{
	RED_ASSERT( (m_atlasTileSize & 3) == 0, "We expect the output tile size to be nicely compressible. But have %u", m_atlasTileSize );
	RED_ASSERT( m_atlasTileSize > 4, "We expect to have atlas tiles bigger than 4x4. Otherwise compression won't really work." );

	for ( Uint32 layer_i = 0; layer_i < maskLayers.Size(); ++layer_i )
	{
		MaskLayer& maskLayer = maskLayers[layer_i];

		maskLayer.widthInTiles = ( maskLayer.width + (m_maskTileSize-1) ) / m_maskTileSize;
		maskLayer.heightInTiles = ( maskLayer.height + (m_maskTileSize-1) ) / m_maskTileSize;

		maskLayer.tiles.Clear();
		maskLayer.tiles.Reserve( maskLayer.widthInTiles*maskLayer.heightInTiles );

		for ( Uint32 y = 0; y < maskLayer.heightInTiles; ++y )
		{
			for ( Uint32 x = 0; x < maskLayer.widthInTiles; ++x )
			{
				MaskLayer::Tile tile;
				tile.atlasLocation = c_invalidLocation;
				tile.atlasPixels.Resize( m_atlasTileSize*m_atlasTileSize );

				if (layer_i > 0)
				{
					tile.minValue = 255;
					tile.maxValue = 0;

					if (m_generationSettings.useMemcpy)
					{
						for (Uint32 yy = 0; yy < m_atlasTileSize; ++yy)
						{
							Int32 sourceX = Int32(x*m_maskTileSize) - 1;
							Int32 sourceY = Int32(y*m_maskTileSize) + (Int32(yy) - 1);
							sourceX = Clamp<Int32>(sourceX, 0, maskLayer.width - 1);
							sourceY = Clamp<Int32>(sourceY, 0, maskLayer.height - 1);
							red::Memcpy(&tile.atlasPixels[yy * m_atlasTileSize], &maskLayer.pixels[sourceX + sourceY * Int32(maskLayer.width)], m_atlasTileSize);
						}

						for (Uint32 i = 0; i < tile.atlasPixels.Size(); ++i)
						{
							tile.minValue = Min(tile.minValue, tile.atlasPixels[i]);
							tile.maxValue = Max(tile.maxValue, tile.atlasPixels[i]);
						}
					}
					else
					{
						for (Uint32 yy = 0; yy < m_atlasTileSize; ++yy)
						{
							for (Uint32 xx = 0; xx < m_atlasTileSize; ++xx)
							{
								Uint8 v = LoadMask(maskLayers, x, y, Int32(xx) - 1, Int32(yy) - 1, layer_i);
								tile.atlasPixels[xx + yy * m_atlasTileSize] = v;
								tile.minValue = Min(tile.minValue, v);
								tile.maxValue = Max(tile.maxValue, v);
							}
						}
					}
				}
				else
				{
					tile.minValue = 255;
					tile.maxValue = 255;
					red::Memset(tile.atlasPixels.TypedData(), 255, tile.atlasPixels.Size());
				}

				maskLayer.tiles.PushBack( std::move(tile) );
			}
		}
	}
}

Uint8 MultilayerMaskBuilder::LoadMask(const red::DynArray<MaskLayer>& maskLayers, Uint32 tx, Uint32 ty, Int32 x, Int32 y, Uint32 layerIndex)
{
	if (layerIndex > 0)
	{
		const MaskLayer& layer = maskLayers[layerIndex];
		Int32 xx = Int32(tx*m_maskTileSize) + x;
		Int32 yy = Int32(ty*m_maskTileSize) + y;
		xx = Clamp<Int32>(xx, 0, layer.width - 1);
		yy = Clamp<Int32>(yy, 0, layer.height - 1);
		return layer.pixels[xx + yy * Int32(layer.width)];
	}
	else
	{
		return 255;
	}
}

void MultilayerMaskBuilder::FilterTileLayers(red::DynArray<MaskLayer>& maskLayers)
{
	// #TODO: When checking for non-zero contribution, only check pixels in the layer tile that cover the vtile.

	for ( Uint32 layer_i = 0; layer_i < m_numLayers; ++layer_i )
	{
		const MaskLayer& maskLayer = maskLayers[layer_i];
		if ( maskLayer.width >= m_maxMaskWidthHigh )
		{
			// Add to high vmask
			for ( Uint32 vty = 0; vty < m_maxMaskHeightInTilesHigh; ++vty )
			{
				for ( Uint32 vtx = 0; vtx < m_maxMaskWidthInTilesHigh; ++vtx )
				{
					VMaskTile &vtile = m_vmaskTilesHigh[vtx + vty*m_maxMaskWidthInTilesHigh];

					Uint32 tx = (vtx * maskLayer.width) / m_maxMaskWidthHigh;
					Uint32 ty = (vty * maskLayer.height) / m_maxMaskHeightHigh;

					const MaskLayer::Tile& tile = maskLayer.tiles[tx+ty*maskLayer.widthInTiles];
					if ( tile.maxValue > 0 )
					{
						VMaskTileLayer layer;
						layer.layerIndex = layer_i;
						layer.layerTileIndex = (tx + ty*maskLayer.widthInTiles);
						layer.atlasLocation = AddTileToAtlas(maskLayers, layer_i, tx, ty );
						vtile.layers.PushBack( layer );
					}
				}
			}
		}
		else
		{
			// Add to low vmask
			for ( Uint32 vty = 0; vty < m_maxMaskHeightInTilesLow; ++vty )
			{
				for ( Uint32 vtx = 0; vtx < m_maxMaskWidthInTilesLow; ++vtx )
				{
					VMaskTile &vtile = m_vmaskTilesLow[vtx + vty*m_maxMaskWidthInTilesLow];

					Uint32 tx = (vtx * maskLayer.width) / m_maxMaskWidthLow;
					Uint32 ty = (vty * maskLayer.height) / m_maxMaskHeightLow;

					Float maxValInTile = 0.0f;

					const MaskLayer::Tile& tile = maskLayer.tiles[tx+ty*maskLayer.widthInTiles];
					if ( tile.maxValue > 0 )
					{
						VMaskTileLayer layer;
						layer.layerIndex = layer_i;
						layer.layerTileIndex = (tx + ty*maskLayer.widthInTiles);
						layer.atlasLocation = AddTileToAtlas(maskLayers, layer_i, tx, ty );
						vtile.layers.PushBack( layer );
					}
				}
			}
		}
	}
}


Uint32 MultilayerMaskBuilder::AddTileToAtlas(red::DynArray<MaskLayer>& maskLayers, Uint32 layerIndex, Uint32 tx, Uint32 ty )
{
	red::StopWatch sw_Total;

	MaskLayer& maskLayer = maskLayers[layerIndex];
	Uint32 tileIndex = (tx + ty*maskLayer.widthInTiles);
	MaskLayer::Tile& maskLayerTile = maskLayer.tiles[tileIndex];

	if ( maskLayerTile.atlasLocation == c_invalidLocation )
	{
		Uint64 tileHash = 0;
		if (m_generationSettings.compressTextures)
		{
			// Compress this tile, making use of neighboring tiles to prevent seams.
			CompressTile(maskLayer, tileIndex);

			// Check for atlas tile with the same compressed data.
			// We calculate the hash after decompressing the data, in case some slightly different ref values from
			// neighboring tiles gave a slightly different encoding, while keeping the same final pixel values.
			tileHash = RED_FNV_OFFSET_BASIS64;
			for (Uint32 y = 0; y < m_atlasTileSize / 4; ++y)
			{
				for (Uint32 x = 0; x < m_atlasTileSize / 4; ++x)
				{
					Uint64 compressedBlock = maskLayerTile.atlasCompressedData[x + y * m_atlasTileSize / 4];
					Float colorFloats[16] = {};
					D3DXDecodeBC4U(colorFloats, compressedBlock);
					Uint8 color[16];
					for (Uint32 i = 0; i < 16; ++i)
					{
						color[i] = Uint8(colorFloats[i] * 255.0f);
					}

					tileHash = red::CalculateHash64(color, sizeof(color), tileHash);
				}
			}
		}
		else
		{
			tileHash = red::CalculateHash64(maskLayerTile.atlasPixels.TypedData(), maskLayerTile.atlasPixels.DataSize());
		}

		auto atlasIter = m_atlasTiles.Find( tileHash );
		if ( atlasIter == m_atlasTiles.End() )
		{
			// #TODO: Maybe check for hash collisions?

			AtlasTile atlasTile;
			atlasTile.atlasLocation = m_atlasNumTiles++;
			atlasTile.sourceLayer = layerIndex;
			atlasTile.sourceTileIndex = tileIndex;

			atlasIter = m_atlasTiles.Insert( tileHash, atlasTile ).Iterator();
		}

		maskLayerTile.atlasLocation = atlasIter.Value().atlasLocation;
	}

	m_detailedProfiling[(Uint8)m_generationSettings.platform].addTileToAtlas_Total += sw_Total.GetDelta();

	return maskLayerTile.atlasLocation;
}


void MultilayerMaskBuilder::FitAtlasToTiles()
{
	Uint32 bestWidth = 0;
	Uint32 bestHeight = 0;
	Uint32 bestWastedSpace = UINT32_MAX;

	if ( m_atlasNumTiles > 0 )
	{
		// Find optimal size, with least wasted space. When calculating wasted space, round up to a power of 2 in
		// both dimensions. Consoles are likely going to pad it out anyways, so we might as well minimize that.
		RED_ASSERT( red::IsPowerOf2( m_atlasTileSize ), "Expect tile size to be a power of 2, but have %u", m_atlasTileSize );
		Uint32 testWidth = 16384;
		while ( testWidth >= m_atlasTileSize )
		{
			Uint32 widthInTiles = (testWidth / m_atlasTileSize);
			Uint32 testHeight = ((m_atlasNumTiles + widthInTiles-1) / widthInTiles)*m_atlasTileSize;

			Uint32 width2 = red::RoundUpToPowerOf2( testWidth );
			Uint32 height2 = red::RoundUpToPowerOf2( testHeight );

			Uint32 wastedSpace = (width2*height2) - (m_atlasNumTiles*m_atlasTileSize*m_atlasTileSize);
			if ( wastedSpace <= bestWastedSpace )
			{
				bestWidth = testWidth;
				bestHeight = testHeight;
				bestWastedSpace = wastedSpace;
			}

			if ( testWidth > testHeight )
			{
				testWidth /= 2;
			}
			else
			{
				break;
			}
		}
	}

	if ( bestWidth <= GpuApi::MAX_TEXTURE_2D_SIZE && bestHeight <= GpuApi::MAX_TEXTURE_2D_SIZE )
	{
		m_atlasWidth = bestWidth;
		m_atlasHeight = bestHeight;
	}

	RED_ASSERT( ((m_atlasWidth&3) == 0) && ((m_atlasHeight&3) == 0), "Atlas dimensions should be multiple of 4 (%ux%u)", m_atlasWidth, m_atlasHeight );
}


red::UniqueBuffer MultilayerMaskBuilder::GenerateAtlasImage(const red::DynArray<MaskLayer>& maskLayers)
{
	red::UniqueBuffer atlasData;

	if ( m_atlasWidth > 0 && m_atlasHeight > 0 )
	{
		if (m_generationSettings.compressTextures)
		{
			RED_ASSERT(((m_atlasTileSize & 3) == 0), "Expect atlas tiles to be block compressible. Have size %u", m_atlasTileSize);
			RED_ASSERT(((m_atlasWidth & 3) == 0) && ((m_atlasHeight & 3) == 0), "Expect atlas to be block compressible. Have dimensions %ux%u", m_atlasWidth, m_atlasHeight);
			RED_ASSERT((m_atlasWidth % m_atlasTileSize == 0), "Expect atlas size (%u) to be multiple of atlas tile size (%u)", m_atlasWidth, m_atlasTileSize);

			const Uint32 tileSizeInBlocks = m_atlasTileSize / 4;
			const Uint32 atlasWidthInBlocks = m_atlasWidth / 4;
			const Uint32 atlasHeightInBlocks = m_atlasHeight / 4;
			const Uint32 atlasWidthInTiles = m_atlasWidth / m_atlasTileSize;

			atlasData = red::CreateUniqueBuffer<red::PoolBackend>(atlasWidthInBlocks*atlasHeightInBlocks * sizeof(Uint64), 16);

			for (const auto& atlasTileIter : m_atlasTiles)
			{
				const AtlasTile& atlasTile = atlasTileIter.Value();

				const MaskLayer& sourceLayer = maskLayers[atlasTile.sourceLayer];
				const MaskLayer::Tile& sourceTile = sourceLayer.tiles[atlasTile.sourceTileIndex];

				const Uint32 location = atlasTile.atlasLocation;
				const Uint32 tx = location % atlasWidthInTiles;
				const Uint32 ty = location / atlasWidthInTiles;
				const Uint32 bx = tx * tileSizeInBlocks;
				const Uint32 by = ty * tileSizeInBlocks;

				Uint64* destPtr = static_cast<Uint64*>(atlasData.Get()) + bx + by * atlasWidthInBlocks;
				for (Uint32 y = 0; y < tileSizeInBlocks; ++y)
				{
					const Uint64* srcPtr = &sourceTile.atlasCompressedData[y*tileSizeInBlocks];
					red::Memcpy(destPtr, srcPtr, tileSizeInBlocks * sizeof(Uint64));
					destPtr += atlasWidthInBlocks;
				}
			}
		}
		else
		{
			const Uint32 atlasWidthInTiles = m_atlasWidth / m_atlasTileSize;
			atlasData = red::CreateUniqueBuffer<red::PoolBackend>(m_atlasWidth*m_atlasHeight * sizeof(Uint8), 16);

			for (const auto& atlasTileIter : m_atlasTiles)
			{
				const AtlasTile& atlasTile = atlasTileIter.Value();

				const MaskLayer& sourceLayer = maskLayers[atlasTile.sourceLayer];
				const MaskLayer::Tile& sourceTile = sourceLayer.tiles[atlasTile.sourceTileIndex];

				const Uint32 location = atlasTile.atlasLocation;
				const Uint32 tx = location % atlasWidthInTiles;
				const Uint32 ty = location / atlasWidthInTiles;
				const Uint32 bx = tx * m_atlasTileSize;
				const Uint32 by = ty * m_atlasTileSize;

				Uint8* destPtr = static_cast<Uint8*>(atlasData.Get()) + bx + by * m_atlasWidth;
				for (Uint32 y = 0; y < m_atlasTileSize; ++y)
				{
					const Uint8* srcPtr = &sourceTile.atlasPixels[y * m_atlasTileSize];
					red::Memcpy(destPtr, srcPtr, m_atlasTileSize * sizeof(Uint8));
					destPtr += m_atlasWidth;
				}
			}
		}
	}
	else
	{
		RED_LOG_CATEGORY_WARNING( red::LoggerCategory_Default, "Generating Multilayer Mask with no data. Replacing with a minimal mask, but do you really need multilayer here?" );

		// If we have an empty atlas, still create a valid atlas so we don't get into trouble binding null later on.
		// But it can be very small and empty.

		m_vmaskTilesHigh.Resize( 1 );
		m_vmaskTilesHigh[0].layers.Resize( 1 );
		m_vmaskTilesHigh[0].layers[0].layerIndex = 0;
		m_vmaskTilesHigh[0].layers[0].atlasLocation = 0;
		m_vmaskTilesHigh[0].layers[0].layerTileIndex = 0;

		m_vmaskTilesLow.Resize( 1 );
		m_vmaskTilesLow[0].layers.Clear();

		m_numLayers = 1;
		m_maskTileSize = 2;
		m_maxMaskWidthHigh = 2;
		m_maxMaskHeightHigh = 2;
		m_maxMaskWidthLow = 1;
		m_maxMaskHeightLow = 1;
		m_maxMaskWidthInTilesHigh = 1;
		m_maxMaskHeightInTilesHigh = 1;
		m_maxMaskWidthInTilesLow = 1;
		m_maxMaskHeightInTilesLow = 1;
		m_atlasTileSize = 4;
		m_atlasWidth = 4;
		m_atlasHeight = 4;
		m_atlasNumTiles = 1;

		atlasData = red::CreateUniqueBuffer<red::PoolBackend>( sizeof(Uint64), 16 );
		red::Memzero( atlasData.Get(), atlasData.GetSize() );
	}

	return atlasData;
}


void MultilayerMaskBuilder::CompressTile( MaskLayer& layer, Uint32 tileIndex )
{
	red::StopWatch sw_Total;

	RED_ASSERT( (m_atlasTileSize % 4) == 0, "Expecting atlas tile size to be multiple of 4, but have %u. Compression gets much more complicated.", m_atlasTileSize );

	const Uint32 atlasTileSizeInBlocks = m_atlasTileSize/4;
	RED_ASSERT( atlasTileSizeInBlocks > 1, "Expecting to have more than one block in a tile. Need to change neighborhood gathering, and increase refValues size." );

	const Uint32 tx = ( tileIndex % layer.widthInTiles );
	const Uint32 ty = ( tileIndex / layer.widthInTiles );
	MaskLayer::Tile& layerTile = layer.tiles[tileIndex];

	layerTile.atlasCompressedData.Resize( atlasTileSizeInBlocks*atlasTileSizeInBlocks );

	const Bool haveLeftTile = (tx > 0);
	const Bool haveRightTile = (tx < layer.widthInTiles-1);
	const Bool haveUpTile = (ty > 0);
	const Bool haveDownTile = (ty < layer.heightInTiles-1);

	for ( Uint32 block_y = 0; block_y < atlasTileSizeInBlocks; ++block_y )
	{
		for ( Uint32 block_x = 0; block_x < atlasTileSizeInBlocks; ++block_x )
		{
			TRefValues refValues;
			Float blockValues[16];

			for ( Uint32 yy = 0; yy < 4; ++yy )
			{
				for ( Uint32 xx = 0; xx < 4; ++xx )
				{
					const Uint32 px = (block_x*4 + xx);
					const Uint32 py = (block_y*4 + yy);
					const Float v = Float( layerTile.atlasPixels[px + py*m_atlasTileSize] )/255.0f;
					blockValues[xx + yy*4] = v;
					refValues.PushBack( v );
				}
			}

			const Bool leftEdge = (block_x == 0);
			const Bool rightEdge = (block_x == atlasTileSizeInBlocks-1);
			const Bool upEdge = (block_y == 0);
			const Bool downEdge = (block_y == atlasTileSizeInBlocks-1);

			const Bool useLeftNeighbor = (haveLeftTile && leftEdge);
			const Bool useRightNeighbor = (haveRightTile && rightEdge);
			const Bool useUpNeighbor = (haveUpTile && upEdge);
			const Bool useDownNeighbor = (haveDownTile && downEdge);

			// Add ref values from neighboring blocks.
			if ( useLeftNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx-1, ty, atlasTileSizeInBlocks-1, block_y );
			}
			else if ( useRightNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx+1, ty, 0, block_y );
			}

			if ( useUpNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx, ty-1, block_x, atlasTileSizeInBlocks-1 );
			}
			else if ( useDownNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx, ty+1, block_x, 0 );
			}

			if ( useLeftNeighbor && useUpNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx-1, ty-1, atlasTileSizeInBlocks-1, atlasTileSizeInBlocks-1 );
			}
			else if ( useRightNeighbor && useUpNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx+1, ty-1, 0, atlasTileSizeInBlocks-1 );
			}
			else if ( useLeftNeighbor && useDownNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx-1, ty+1, atlasTileSizeInBlocks-1, 0 );
			}
			else if ( useRightNeighbor && useDownNeighbor )
			{
				AddRefValuesForBlock( refValues, layer, tx+1, ty+1, 0, 0 );
			}

			Uint64 compressedBlock;
			D3DXEncodeBC4U( &compressedBlock, blockValues, refValues.TypedData(), refValues.Size() );

			layerTile.atlasCompressedData[block_x + block_y*atlasTileSizeInBlocks] = compressedBlock;
		}
	}

	m_detailedProfiling[(Uint8)m_generationSettings.platform].compressTile_Total += sw_Total.GetDelta();
}

void MultilayerMaskBuilder::AddRefValuesForBlock( TRefValues& refValues, MaskLayer& layer, Uint32 tx, Uint32 ty, Uint32 block_x, Uint32 block_y )
{
	const MaskLayer::Tile& tile = layer.tiles[tx + ty*layer.widthInTiles];

	const Uint32 atlasWidthInTiles = (m_atlasWidth / m_atlasTileSize);

	// #TODO: Don't include the outer 2 texels. These are duplicated in neighboring tiles, so by including them like
	// this we end up giving them more weight than the others when finding end points. Similarly, don't want to
	// include values that were clamped to the edge of the source image as a result of the inflated tile size.
	for ( Uint32 yy = 0; yy < 4; ++yy )
	{
		for ( Uint32 xx = 0; xx < 4; ++xx )
		{
			const Uint32 px = (block_x*4 + xx);
			const Uint32 py = (block_y*4 + yy);
			const Float v = Float( tile.atlasPixels[px + py*m_atlasTileSize] )/255.0f;
			refValues.PushBack( v );
		}
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static red::AbsolutePath MakeDebugDumpPath( const char* nameFmt, ... )
{
	red::AbsolutePath path = red::paths::GetTempDirectory();
	path.AppendDirPath( "mlmask_dump" );

	char name[1024];
	va_list args;
	va_start( args, nameFmt );
	red::VSNPrintF( name, RED_ARRAY_COUNT( name ), nameFmt, args );
	va_end( args );

	path.AppendFilePath( name );
	return path;
}


void MultilayerMaskBuilder::DEBUG_DumpSplitLayers(const red::DynArray<MaskLayer>& maskLayers, const AnsiChar* postfix)
{
#if ENABLE_PNG_DUMP && ENABLE_PNG_DUMP_SPLIT_LAYERS
	for ( Uint32 layer_i = 0; layer_i < m_numLayers; ++layer_i )
	{
		const MaskLayer& layer = maskLayers[layer_i];
		img::ImagePtr layerMask = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, img::Image::SizeInfo( 4, layer.width, layer.height, 1 ) );
		RED_ASSERT( layerMask->GetLayout().m_rowPitch == layer.width*sizeof( Uint32 ), "" );
		RED_ASSERT( layerMask->GetLayout().m_pixelPitch == sizeof( Uint32 ), "" );

		Uint32* outPix = static_cast<Uint32*>(layerMask->GetPixelBuffer());
		const Uint8* pix = maskLayers[layer_i].pixels.TypedData();

		for ( Uint32 y = 0; y < layer.height; ++y )
		{
			for ( Uint32 x = 0; x < layer.width; ++x )
			{
				Uint32 v = pix[x + y*layer.width];
				outPix[x + y*layer.width] = (0xff000000 + (v*0x00010101));
			}
		}

		ImageWriter writer( MakeDebugDumpPath( "_layer_%u_%s.png", layer_i, postfix ) );
		img::BaseImageSaver::Save( writer, layerMask, "png" );
	}
#endif
}

void MultilayerMaskBuilder::DEBUG_DumpLayerHeatMap( const AnsiChar* tag )
{
#if ENABLE_PNG_DUMP
	if ( m_maxMaskWidthInTilesHigh > 0 && m_maxMaskHeightInTilesHigh > 0 )
	{
		img::ImagePtr layersetMap = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, img::Image::SizeInfo( 4, m_maxMaskWidthInTilesHigh, m_maxMaskHeightInTilesHigh, 1 ) );
		RED_ASSERT( layersetMap->GetLayout().m_pixelPitch == sizeof( Uint32 ), "" );

		Uint32* outPix = static_cast<Uint32*>(layersetMap->GetPixelBuffer());

		for ( Uint32 y = 0; y < m_maxMaskHeightInTilesHigh; ++y )
		{
			for ( Uint32 x = 0; x < m_maxMaskWidthInTilesHigh; ++x )
			{
				const VMaskTile& tile = m_vmaskTilesHigh[x + y*m_maxMaskWidthInTilesHigh];

				Uint32 numLayers = tile.layers.Size();

				Color color = EvaluateHeatmap( Float( numLayers ) / 20.0f );
				outPix[x+y*(layersetMap->GetLayout().m_rowPitch/4)] = color.ToUint32();
			}
		}

		ImageWriter writer( MakeDebugDumpPath( "__layers_heat_%s_high.png", tag ) );
		img::BaseImageSaver::Save( writer, layersetMap, "png" );
	}

	if ( m_maxMaskWidthInTilesLow > 0 && m_maxMaskHeightInTilesLow > 0 )
	{
		img::ImagePtr layersetMap = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, img::Image::SizeInfo( 4, m_maxMaskWidthInTilesLow, m_maxMaskHeightInTilesLow, 1 ) );
		RED_ASSERT( layersetMap->GetLayout().m_pixelPitch == sizeof( Uint32 ), "" );

		Uint32* outPix = static_cast<Uint32*>(layersetMap->GetPixelBuffer());

		for ( Uint32 y = 0; y < m_maxMaskHeightInTilesLow; ++y )
		{
			for ( Uint32 x = 0; x < m_maxMaskWidthInTilesLow; ++x )
			{
				const VMaskTile& tile = m_vmaskTilesLow[x + y*m_maxMaskWidthInTilesLow];

				Uint32 numLayers = tile.layers.Size();

				Color color = EvaluateHeatmap( Float( numLayers ) / 20.0f );
				outPix[x+y*(layersetMap->GetLayout().m_rowPitch/4)] = color.ToUint32();
			}
		}

		ImageWriter writer( MakeDebugDumpPath( "__layers_heat_%s_low.png", tag ) );
		img::BaseImageSaver::Save( writer, layersetMap, "png" );
	}
#endif
}

void MultilayerMaskBuilder::DEBUG_DumpCompressedAtlas( const void* compressedData )
{
#if ENABLE_PNG_DUMP
	img::ImagePtr packedCompressed = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, img::Image::SizeInfo( 1, m_atlasWidth, m_atlasHeight, 1 ) );
	RED_ASSERT( packedCompressed->GetLayout().m_rowPitch == m_atlasWidth, "Expected tightly packed image" );

	Uint64* srcPix = (Uint64*)compressedData;
	Uint8* compPix = (Uint8*)packedCompressed->GetPixelBuffer();

	for ( Uint32 y = 0; y < m_atlasHeight/4; ++y )
	{
		for ( Uint32 x = 0; x < m_atlasWidth/4; ++x )
		{
			Uint64 compressedBlock = srcPix[x+y*m_atlasWidth/4];

			Float values[16];
			D3DXDecodeBC4U( values, compressedBlock );

			for ( Uint32 yy = 0; yy < 4; ++yy )
			{
				for ( Uint32 xx = 0; xx < 4; ++xx )
				{
					compPix[x*4+xx + (y*4+yy)*m_atlasWidth] = Uint8( values[xx+yy*4]*255.0f );
				}
			}
		}
	}

	ImageWriter writer( MakeDebugDumpPath( "__compressed.png" ) );
	img::BaseImageSaver::Save( writer, packedCompressed, "png" );
#endif
}

void MultilayerMaskBuilder::DEBUG_DumpReconstructedLayers(const red::DynArray<MaskLayer>& maskLayers, const void* compressedData )
{
	// #TODO: Reconstruct entirely from final buffers, rather than using vmask info, basically doing what the shader
	// would do.

#if ENABLE_PNG_DUMP && ENABLE_PNG_DUMP_RECONSTRUCTED_LAYERS
	red::DynArray<img::ImagePtr> layerMasks( m_numLayers );
	for ( Uint32 i = 0; i < m_numLayers; ++i )
	{
		layerMasks[i] = img::Image::CreateEmpty( img::PixelFormat::Format_Uint8, img::Image::SizeInfo( 1, maskLayers[i].width, maskLayers[i].height, 1 ) );
		RED_ASSERT( layerMasks[i]->GetLayout().m_pixelPitch == sizeof( Uint8 ), "Expect tightly packed image" );
	}

	const Uint64* srcPix = (const Uint64*)compressedData;

	//#TODO: Alternate dump including the reconstructed borders, to see duplicated values.

	for ( Uint32 y = 0; y < m_maxMaskHeightInTilesHigh; ++y )
	{
		for ( Uint32 x = 0; x < m_maxMaskWidthInTilesHigh; ++x )
		{
			const Uint32 vtileIndex = x + y*m_maxMaskWidthInTilesHigh;
			const VMaskTile& vmaskTile = m_vmaskTilesHigh[vtileIndex];

			for ( Uint32 i = 0; i < vmaskTile.layers.Size(); ++i )
			{
				const VMaskTileLayer& vlayer = vmaskTile.layers[i];
				const Uint32 layer_i = vlayer.layerIndex;

				const MaskLayer& layerMask = maskLayers[layer_i];

				const Uint32 tx = vlayer.layerTileIndex % layerMask.widthInTiles;
				const Uint32 ty = vlayer.layerTileIndex / layerMask.heightInTiles;

				Uint8* outPix = static_cast<Uint8*>(layerMasks[layer_i]->GetPixelBuffer());
				Uint32 rowPitch = layerMasks[layer_i]->GetLayout().m_rowPitch;

				RED_ASSERT( rowPitch == layerMask.width, "" );

				if ( layer_i != 0 )
				{
					const Uint32 tileInAtlasIndex = vlayer.atlasLocation;
					const Uint32 ax = tileInAtlasIndex % (m_atlasWidth/m_atlasTileSize);
					const Uint32 ay = tileInAtlasIndex / (m_atlasWidth/m_atlasTileSize);

					for ( Uint32 by = 0; by < m_atlasTileSize/4; ++by )
					{
						for ( Uint32 bx = 0; bx < m_atlasTileSize/4; ++bx )
						{
							Uint64 compressedBlock = srcPix[(ax*m_atlasTileSize/4 + bx)+(ay*m_atlasTileSize/4 + by)*m_atlasWidth/4];

							Float values[16];
							D3DXDecodeBC4U( values, compressedBlock );

							for ( Uint32 yy = 0; yy < 4; ++yy )
							{
								for ( Uint32 xx = 0; xx < 4; ++xx )
								{
									Uint32 bxx = bx*4+xx - 1;
									Uint32 byy = by*4+yy - 1;
									if ( bxx < m_maskTileSize && byy < m_maskTileSize )
									{
										Uint32 mx = (tx*m_maskTileSize + bxx);
										Uint32 my = (ty*m_maskTileSize + byy);
										if ( mx < layerMask.width && my < layerMask.height )
										{
											outPix[mx + my*rowPitch] = Uint8( values[xx+yy*4]*255.0f );
										}
									}
								}
							}
						}
					}
				}
				else
				{
					const Uint32 tileInAtlasIndex = vlayer.atlasLocation;
					const Uint32 ax = tileInAtlasIndex % (m_atlasWidth/m_atlasTileSize);
					const Uint32 ay = tileInAtlasIndex / (m_atlasWidth/m_atlasTileSize);

					for ( Uint32 yy = 0; yy < m_maskTileSize; ++yy )
					{
						for ( Uint32 xx = 0; xx < m_maskTileSize; ++xx )
						{
							Uint32 mx = (tx*m_maskTileSize + xx);
							Uint32 my = (ty*m_maskTileSize + yy);
							if ( mx < layerMask.width && my < layerMask.height )
							{
								outPix[mx + my*rowPitch] = 255;
							}
						}
					}
				}
			}
		}
	}


	for ( Uint32 y = 0; y < m_maxMaskHeightInTilesLow; ++y )
	{
		for ( Uint32 x = 0; x < m_maxMaskWidthInTilesLow; ++x )
		{
			const Uint32 vtileIndex = x + y*m_maxMaskWidthInTilesLow;
			const VMaskTile& vmaskTile = m_vmaskTilesLow[vtileIndex];

			for ( Uint32 i = 0; i < vmaskTile.layers.Size(); ++i )
			{
				const VMaskTileLayer& vlayer = vmaskTile.layers[i];
				const Uint32 layer_i = vlayer.layerIndex;

				const MaskLayer& layerMask = maskLayers[layer_i];

				const Uint32 tx = vlayer.layerTileIndex % layerMask.widthInTiles;
				const Uint32 ty = vlayer.layerTileIndex / layerMask.heightInTiles;

				Uint8* outPix = static_cast<Uint8*>(layerMasks[layer_i]->GetPixelBuffer());
				Uint32 rowPitch = layerMasks[layer_i]->GetLayout().m_rowPitch;

				RED_ASSERT( rowPitch == layerMask.width, "" );

				if ( layer_i != 0 )
				{
					const Uint32 tileInAtlasIndex = vlayer.atlasLocation;
					const Uint32 ax = tileInAtlasIndex % (m_atlasWidth/m_atlasTileSize);
					const Uint32 ay = tileInAtlasIndex / (m_atlasWidth/m_atlasTileSize);

					for ( Uint32 by = 0; by < m_atlasTileSize/4; ++by )
					{
						for ( Uint32 bx = 0; bx < m_atlasTileSize/4; ++bx )
						{
							Uint64 compressedBlock = srcPix[(ax*m_atlasTileSize/4 + bx)+(ay*m_atlasTileSize/4 + by)*m_atlasWidth/4];

							Float values[16];
							D3DXDecodeBC4U( values, compressedBlock );

							for ( Uint32 yy = 0; yy < 4; ++yy )
							{
								for ( Uint32 xx = 0; xx < 4; ++xx )
								{
									Uint32 bxx = bx*4+xx - 1;
									Uint32 byy = by*4+yy - 1;
									if ( bxx < m_maskTileSize && byy < m_maskTileSize )
									{
										Uint32 mx = (tx*m_maskTileSize + bxx);
										Uint32 my = (ty*m_maskTileSize + byy);
										if ( mx < layerMask.width && my < layerMask.height )
										{
											outPix[mx + my*rowPitch] = Uint8( values[xx+yy*4]*255.0f );
										}
									}
								}
							}
						}
					}
				}
				else
				{
					const Uint32 tileInAtlasIndex = vlayer.atlasLocation;
					const Uint32 ax = tileInAtlasIndex % (m_atlasWidth/m_atlasTileSize);
					const Uint32 ay = tileInAtlasIndex / (m_atlasWidth/m_atlasTileSize);

					for ( Uint32 yy = 0; yy < m_maskTileSize; ++yy )
					{
						for ( Uint32 xx = 0; xx < m_maskTileSize; ++xx )
						{
							Uint32 mx = (tx*m_maskTileSize + xx);
							Uint32 my = (ty*m_maskTileSize + yy);
							if ( mx < layerMask.width && my < layerMask.height )
							{
								outPix[mx + my*rowPitch] = 255;
							}
						}
					}
				}
			}
		}
	}

	for ( Uint32 i = 0; i < m_numLayers; ++i )
	{
		ImageWriter writer( MakeDebugDumpPath( "reconstructed_%u.png", i ) );
		img::BaseImageSaver::Save( writer, layerMasks[i], "png" );
	}
#endif
}
