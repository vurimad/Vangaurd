/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/renderData/include/renderMultilayerMaskBlob.h" //we need rend::RenderMultilayerMaskBlobHeader

class Multilayer_Mask;
namespace rend
{
	class RenderMultilayerMaskBlob;
}


class BACKEND_MATERIAL_API MultilayerMaskBuilder : public red::NonCopyable
{
public:
	struct LayerSource
	{
		// If null, then layer is all black.
		const void* imageData;
		Uint32 width;
		Uint32 height;
		Uint32 rowStride;
		Uint32 bias = 0;
		Uint32 biasConsole = 0;
		GpuWrapApi::eTextureFormat format;
	};


	// Simple helper that generates a Multilayer_Mask from a set of mask images with all the same size.
	static THandle<Multilayer_Mask> Generate( red::ArraySpan<const Uint8* const> layerMasks, Uint32 width, Uint32 height, Uint32 rowStride,
	                                          THandle<Multilayer_Mask> existingMask = nullptr, Bool useVerticalFlipOfSourceData = false, Bool useFastPath = false);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	MultilayerMaskBuilder();
	~MultilayerMaskBuilder();


	RED_INLINE const String& GetErrorString() const { return m_error; }
	RED_INLINE Bool HasError() const { return !m_error.Empty(); }


	void ReserveLayers( Uint32 numLayers );

	// Set a single layer's source data. Internal data will be resized as needed, with empty layers inserted if no data
	// has been given yet for lower layers.
	// #NOTE: Setting layer 0 is not allowed, it is assumed to be always full white.
	// #TEMP: All layers must have the same dimensions.
	void SetLayerSource( Uint32 layerIndex, const LayerSource& source, Bool useVerticalFlipOfSourceData = false );

	// Set data for layers 1-N, resizes the mask to fit exactly those layers. Any previously set data will be lost.
	// #NOTE: Does not include layer 0, it is assumed to be always full white.
	// #TEMP: All layers must have the same dimensions.
	void SetLayerSources( red::ArraySpan<const LayerSource> sources, Bool useVerticalFlipOfSourceData = false );

	// If outMask is null, a new Multilayer_Mask will be created.
	void Generate( THandle<Multilayer_Mask>& outMask, Bool setSourceData = true, Bool fastPath = false);

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Some additional utilities, generally for debug use.
	Uint32 GetNumLayers() const;

private:
	struct AtlasTile;
	struct MaskLayer;
	struct VMaskTileLayer;
	struct VMaskTile;

	struct GeneratedPlatformData
	{
		rend::RenderMultilayerMaskBlobHeader header;
		red::DynArray<Uint32> tilesData;
		serialization::BufferHandleRO atlasBuffer;
	};

	enum class EDataGenerationPlatform : Uint8
	{
		PC = 0,
		Consoles = 1,
		Count
	};

	struct GenerationSettings
	{
		Bool generateConsoleData;
		Bool compressTextures;
		Bool outputProfilingData;
		Bool useMemcpy;
		EDataGenerationPlatform platform;
	};

	// Reference values when compressing atlas. Can have current block, and up to 3 neighboring blocks.
	typedef red::StaticArray<Float, 16*4> TRefValues;

	void CopySingleLayer( Uint32 layerIndex, const LayerSource& source, Bool useVerticalFlipOfSourceData = false );

	void PrepareLayers(red::DynArray<MaskLayer>& maskLayers);
	void FilterTileLayers(red::DynArray<MaskLayer>& maskLayers);
	Uint32 AddTileToAtlas(red::DynArray<MaskLayer>& maskLayers, Uint32 layerIndex, Uint32 tx, Uint32 ty );
	void FitAtlasToTiles();
	red::UniqueBuffer GenerateAtlasImage(const red::DynArray<MaskLayer>& maskLayers);

	void CompressTile( MaskLayer& layer, Uint32 tileIndex );
	void AddRefValuesForBlock( TRefValues& refValues, MaskLayer& layer, Uint32 tx, Uint32 ty, Uint32 block_x, Uint32 block_y );

	Uint8 LoadMask(const red::DynArray<MaskLayer>& maskLayers, Uint32 tx, Uint32 ty, Int32 x, Int32 y, Uint32 layerIndex );

	void CookPlatformData(ECookingPlatform platform, THandle<rend::RenderMultilayerMaskBlob> &renderBlob, GeneratedPlatformData &platformData, Bool fastPath);
	bool GeneratePlatformData(GeneratedPlatformData& platformData, Uint32 bias);

	void DEBUG_DumpSplitLayers(const red::DynArray<MaskLayer>& maskLayers, const AnsiChar* postfix);
	void DEBUG_DumpLayerHeatMap( const AnsiChar* tag );
	void DEBUG_DumpCompressedAtlas( const void* compressedData );
	void DEBUG_DumpReconstructedLayers(const red::DynArray<MaskLayer>& maskLayers, const void* compressedData );

	String m_error;

	red::DynArray<MaskLayer> m_maskLayers { red::PoolBackend() };			// One for each layer in the mask
	red::DynArray<Uint32> m_maskLayerBiasesPC{ red::PoolBackend() };			// One for each layer in the mask
	red::DynArray<Uint32> m_maskLayerBiasesConsole{ red::PoolBackend() };			// One for each layer in the mask

	red::DynArray<VMaskTile> m_vmaskTilesHigh { red::PoolBackend() };		// Tiles in the virtual mask
	red::DynArray<VMaskTile> m_vmaskTilesLow { red::PoolBackend() };		// Tiles in the virtual mask
	red::HashMap<Uint64, AtlasTile> m_atlasTiles { red::PoolBackend() };	// Tile data hash -> data and location in atlas


	Uint32 m_numLayers;

	Uint32 m_maskTileSize;
	Uint32 m_maxMaskWidthHigh;
	Uint32 m_maxMaskHeightHigh;
	Uint32 m_maxMaskWidthLow;
	Uint32 m_maxMaskHeightLow;
	Uint32 m_maxMaskWidthInTilesHigh;
	Uint32 m_maxMaskHeightInTilesHigh;
	Uint32 m_maxMaskWidthInTilesLow;
	Uint32 m_maxMaskHeightInTilesLow;

	Uint32 m_atlasTileSize;
	Uint32 m_atlasWidth;
	Uint32 m_atlasHeight;
	Uint32 m_atlasNumTiles;

	GenerationSettings m_generationSettings;

	// profiling
	struct HighLevelProfilingData
	{
		Double setSourceData;
		Double generatePlatformData_PC;
		Double cookPlatformData_PC;
		Double generatePlatformData_Consoles;
		Double cookPlatformData_XB1;
		Double cookPlatformData_PS4;

		void Reset()
		{
			setSourceData = 0.0;
			generatePlatformData_PC = 0.0;
			cookPlatformData_PC = 0.0;
			generatePlatformData_Consoles = 0.0;
			cookPlatformData_XB1 = 0.0;
			cookPlatformData_PS4 = 0.0;
		}
	};

	HighLevelProfilingData m_hlProfiling;

	struct DetailedProfilingData
	{
		Double downsampleLayers;
		Double prepareLayers;
		Double filterTileLayers;
		Double addTileToAtlas_Total;
		Double compressTile_Total;
		Double fitAtlasToTiles;
		Double generateAtlasImage;
		Double fillPlatformData;

		void Reset()
		{
			downsampleLayers = 0.0;
			prepareLayers = 0.0;
			filterTileLayers = 0.0;
			addTileToAtlas_Total = 0.0;
			compressTile_Total = 0.0;
			fitAtlasToTiles = 0.0;
			generateAtlasImage = 0.0;
			fillPlatformData = 0.0;
		}
	};

	// 2 sets of data, one for PC and one for the consoles
	DetailedProfilingData m_detailedProfiling[(Uint8)EDataGenerationPlatform::Count];

};
