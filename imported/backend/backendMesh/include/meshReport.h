/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once


class CMesh;
class IMaterial;
class ITexture;

namespace mesh
{
	class DataViewRO;
}

namespace tools
{
	struct MeshEditorReportInfo;

	/// Class generating HTML source of the mesh statistics report.
	/// Takes mesh as input, returns string as output.
	///
	/// Most of it's internal methods are created as static ones.
	/// This is so we can track the data dependencies of those calculations.
	class BACKEND_MESH_API MeshReport
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		struct MeshScoreRange;

		MeshReport( const THandle< CMesh >& mesh, Bool updateStats = true );
		MeshReport( const MeshReport& ) = delete;
		~MeshReport();

		MeshReport& operator=( const MeshReport& ) = delete;

		/// Load value range for the mesh score (from light to extreme) from a config file
		static bool LoadMeshScoreConfig( MeshScoreRange& scoreRangeBones, MeshScoreRange& scoreRangeChunks, MeshScoreRange& scoreRangeTrisToAreaLog, MeshScoreRange& scoreRangeColliderCount, MeshScoreRange& scoreRangeColliderMemory, const res::ResourcePath meshPath );

		/// Set new mesh and recalculate stats report.
		void SetMesh( const THandle< CMesh >& newMesh, Bool updateStats = true );

		/// Call when the mesh has changed. Is is called internally when from constructor and SetMesh.
		void UpdateStats();

		/// Same as `UpdateStats` but only used for ReportInfo
		void UpdateReportInfo();

		/// Get the string with HTML source for the mesh statistics report.
		RED_INLINE const String& GetReport() const { return m_report; }

		/// Get import path extracted from mesh metadata. This might be empty when metadata was not present.
		RED_INLINE const String& GetImportPath() const { return m_importPath; }

		/// Get basic report info.
		const MeshEditorReportInfo& GetReportInfo();

	private:
		struct MeshTextureInfo;
		struct MeshTextureGroupInfo;
		struct ProblemList;

		/// Test if given material is used by given chunk.
		static Bool IsMaterialUsedByChunk( const mesh::DataViewRO& meshData, Uint32 materialIndex, Uint32 chunkIndex );

		static Uint32 CalcMeshRenderDataSize( const mesh::DataViewRO& meshData );
		static Uint32 CalcMeshCollisionDataSize( const mesh::DataViewRO& meshData );
		static Uint32 CalcMeshRawDataSize( const mesh::DataViewRO& meshData );

		static Uint32 CalcMeshLodRenderDataSize( const mesh::DataViewRO& meshData, Uint32 lodIndex );

		static Uint32 CalcTextureDataSize( const ITexture* texture );

		static Double CalcChunkArea( const CMesh* mesh, Uint32 chunkIndex );
		static Double CalcChunkAverageTriangleArea( const CMesh* mesh, Uint32 chunkIndex );

		static String MemSizeToText( Uint32 memSize );
		static String RawCountToText( Uint32 count );

		static void CalcChunkAreas( const mesh::DataViewRO& meshData, Double& totalArea, red::DynArray< Double >& chunkAreas, ProblemList& problemList );

		static void GatherTexturesUsedByMaterial( IMaterial* material, Uint32 materialIndex, red::DynArray< MeshTextureInfo* >& textures );
		static void GatherTexturesUsedByMesh( const CMesh* mesh, red::DynArray< MeshTextureInfo* >& textures );

		static void AnalyzeTextures( const red::DynArray< MeshTextureInfo* >& textures, Uint32& textureDataSize, ProblemList& problemList );
		static Uint32 MeshReport::GetNumberOfCollisionShapes( const mesh::DataViewRO& meshView );
		static Uint32 MeshReport::GetCollisionMeshMemory( const mesh::DataViewRO& meshView );

		static void GatherMaterialsUsedByChunk( const mesh::DataViewRO& meshData, Uint32 chunkIndex, red::DynArray< Uint32 >& usedMaterials );

		static void UpdateStatsLODHeader( String& sourceCode, const mesh::DataViewRO& meshData, 
		                                  Uint32 lodIndex, const red::DynArray<Uint16>& lodChunkIndices, const red::DynArray< Uint32 >& lodTriangles,
		                                  Bool& isAnythingSkinned, Bool& isLODSkinned, ProblemList& problemList );

		static void UpdateStatsLODMaterials( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< MeshTextureInfo* >& usedTextures,
		                                     Uint32 lodIndex, const red::DynArray<Uint16>& lodChunkIndices, const red::DynArray< Double >& chunkAreas,
		                                     red::DynArray< Uint32 >& lodTriangles, red::DynArray< Double >& lodArea,
		                                     Bool& foundNonOptimalMaterials, ProblemList& problemList );

		static void UpdateStatsLODChunkInfo( String& sourceCode, const mesh::DataViewRO& meshData, Bool isSkinned,
		                                     Uint32 lodIndex, const red::DynArray<Uint16>& lodChunkIndices,
		                                     red::DynArray< Uint32 >& allUsedBones, ProblemList& problemList );

		static void CalcChunkSkinningHistogram( const mesh::DataViewRO& meshData, Uint32 chunkIndex, Uint32& s1, Uint32& s2, Uint32& s3, Uint32& s4 );
		static void CalcChunkSkinningUsedBones( const mesh::DataViewRO& meshData, Uint32 chunkIndex, red::DynArray< Uint32 >& allUsedBones );

		static void FindDuplicateChunks( CMesh* mesh, ProblemList& problemList );

		static void UpdateStatsImportInfo( String& sourceCode, String& outputImportPath, MeshEditorReportInfo& outReportInfo, const THandle< CMesh >& mesh );

		static Float CalcMeshScore( const Float value, const MeshScoreRange range );

		static void UpdateReportInfo( const mesh::DataViewRO& meshData, const res::ResourcePath meshPath, Uint32 texturesDataSize, MeshEditorReportInfo& outReportInfo );

		static void UpdateStatsTextures( String& sourceCode, const mesh::DataViewRO& meshData, red::DynArray< MeshTextureInfo* >& usedTextures );

		static void UpdateStatsTextureGroups( String& sourceCode, const mesh::DataViewRO& meshData,
		                                      const red::DynArray< MeshTextureInfo* >& usedTextures, ProblemList& problemList );

		static void UpdateStatsNotUsedMaterials( String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList );
		static void UpdateStatsSkeleton( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< Uint32 >& allUsedBones, ProblemList& problemList );
		static void UpdateStatsSkinning( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< Uint32 >& allUsedBones, ProblemList& problemList );
		static void UpdateStatsCollision( String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList );

		static void UpdateStatsBoundingBox(String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList);

		static void UpdateStatsTotals( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< MeshTextureInfo* >& usedTextures,
		                               Uint32 textureDataSize, ProblemList& problemList );


		THandle< CMesh > m_mesh;
		String m_report;               ///< This will be a HTML source for the page with mesh statistics report.

		String m_importPath;           ///< Extracted from mesh metadata, usually points to *.RE file. Can be empty when metadata is not present.

		red::UniquePtr< MeshEditorReportInfo > m_reportInfo;
	};
} // tools
