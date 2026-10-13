/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class CMesh;

namespace houdini_legacy
{
	class HoudiniEngineSession;
}

namespace mesh
{
	class LODGenerationUtils final
	{
	public:
		struct GenerationInfo 
		{
			THandle< CMesh > mesh;
			red::DynArray< Float > distances;
			Uint32 numClothLODs;
			Bool skipUploadRenderData;
		};

		// Param names used by: lod_mesh_generator.hda
		struct HDAParamNames
		{
			// not mentioned here: lod1, lod2, lod3, lod1percent, lod2percent, lod3percent
			String fuseDistance;
			String hardSplitAngle;
			String materialsIntact;
			String materialsO1;
			String materialsO2;
			String materialsO3;
			String mergeCoplanar;
			String normalsAutoSmooth;
			String polycountLimit;
			String preserveLOD1;
			String preserveNormals;
			String skinningType;
			String splitChunk;
			String useHardsurfaceMode;
			String useLOD1Skinning;
			String pushDiodeShader;
			String pushDiodeValue;
		};

		//

		static const HDAParamNames s_hdaParamNames;
		static const String s_hdaPath;
		static constexpr Uint32 s_maxNumLODs{ 3 };

		// Generate LODs in Houdini Engine
		static Bool GenerateLODsHE( const GenerationInfo& generationInfo, houdini_legacy::HoudiniEngineSession* session, const CName& appearanceName );

	private:
		static Bool ShouldSkipLOD1( houdini_legacy::HoudiniEngineSession* session );
		static Bool IsStreamEmpty( const red::DynArray< Uint32 >& values );

		template< typename T >
		static Bool IsVectorStreamEmpty( const red::DynArray< T >& values )
		{
			static_assert( std::is_base_of< Vector2, T >::value || std::is_base_of< Vector3, T >::value || std::is_base_of< Vector4, T >::value, "Argument must be a Vector type" );
			return red::alg::NoneOf( values, 
				[]( auto& val )
			{ 
				return !val.IsZero(); }
			);
		}
	};
} // mesh
