/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class CMesh;
class IRenderResource;
enum EMeshStreamType : Uint32;

namespace physics
{
	class SystemResource;
	class ICollider;
}
namespace vis
{
	class IOccluderResource;
}

namespace mesh
{
	class DataViewRO;
	struct ChunkStreamDataRO;

	namespace utils
	{		
		extern RESOURCEMESH_API Float GetGlobalAutoHideDistance( const Box& bounds, const Vector3& surfaceAreaPerAxis = Vector3::ZEROS(), const Vector3& scale = Vector3::ONES() );

		class RESOURCEMESH_API IGeometryIterator
		{
		public:

			IGeometryIterator() {}
			virtual ~IGeometryIterator() {}

			virtual void OnTriangle( const Vector3* triangle, Uint32 numTris ) const = 0;
			virtual Bool Touches( const Box& b ) const = 0;

		};

		/// Mesh rendering style, for debug stuff
		enum class MeshDebugStyle
		{
			FaceNormals,			// XYZ encoded normals PER FACE
			FaceFakeLighting,		// Fake lighting from directional light, PER FACE NORMAL
			Normals,				// XYZ encoded normals from vertices
			FakeLighting,			// Fake lighting from directional light, normals taken from vertices
			MaterialIDs,			// Color coded material IDs
			SolidColor,				// Solid color mesh
			Wireframe,				// Wireframe mesh
		};

		/// Sometimes, the elegant implementation is just a function.  Not a method.  Not a class.  Not a framework.  Just a function.
		///  		John Carmack

		// Get list of chunks in given LOD
		typedef red::StaticArray<Uint16, 256 > TChunkIndices;
		extern RESOURCEMESH_API void GetLODChunks(const DataViewRO& view, const Uint32 lodIndex, TChunkIndices& outChunks);

		// Count all triangles in the mesh at given LOD level
		extern RESOURCEMESH_API const Uint32 CountLODTriangles(const DataViewRO& view, const Uint32 lodIndex);

		// Count all vertices in the mesh at given LOD level
		extern RESOURCEMESH_API const Uint32 CountLODVertices(const DataViewRO& view, const Uint32 lodIndex);

		// Count all chunks in the mesh at given LOD level
		extern RESOURCEMESH_API const Uint32 CountLODChunks(const DataViewRO& view, const Uint32 lodIndex);

		// Count all unqiue materials used in the mesh at given LOD level
		extern RESOURCEMESH_API const Uint32 CountLODMaterials(const DataViewRO& view, const Uint32 lodIndex);

		// Find first non-empty LOD, returns -1 if not found
		extern RESOURCEMESH_API const Int32 FindFirstNonEmptyLOD(const DataViewRO& view);

		// Find last non-empty LOD, returns -1 if not found
		extern RESOURCEMESH_API const Int32 FindLastNonEmptyLOD(const DataViewRO& view);

		// Find material index for given material name
		extern RESOURCEMESH_API const Int32 FindMaterialIndex(const DataViewRO& view, const CName materialName);

		//----

		/// iterates over mesh geometry without any memory allocation
		extern RESOURCEMESH_API void IterateGeometry( const DataViewRO& view, const IGeometryIterator* iterator, const Uint32 lodIndex = 0 );

		/// extract vertices and indices from the mesh, very slow function
		/// cannot be called on job (because of the DeferedDataBuffers in the streams)
		extern RESOURCEMESH_API void ExtractGeometry(const DataViewRO& view, red::DynArray< Vector3 >& outVertices, red::DynArray< Uint32 >* outIndices, const Matrix& localToWorld = Matrix::IDENTITY(), const Uint32 lodIndex = 0);

        extern RESOURCEMESH_API void ExtractNormals(const DataViewRO& view, red::DynArray< Vector3 >& outNormals, const Matrix& localToWorld = Matrix::IDENTITY(), const Uint32 lodIndex = 0);

        extern RESOURCEMESH_API void ExtractUVs(const DataViewRO& view, red::DynArray< Vector2 >& outUVs, const Uint32 lodIndex = 0);

		/// get the position of bones in the mesh
		/// the position of bone is computed for it's rig matrix (inverse local2model matrix for a given bone)
		extern RESOURCEMESH_API void GetBonePositions(const DataViewRO& view, red::DynArray< Vector3 >& outBonePositions);

		// Compile debug mesh for easier display
		extern RESOURCEMESH_API TRenderPtr<IRenderResource> CompileDebugMesh(const DataViewRO& view, const MeshDebugStyle renderStyle, const Uint32 lodLevel, const Color& debugColor);

		//----

		/// get the active physics for the mesh
		/// if the mesh has custom physics parameter defined than it's used over the imported physics
		extern RESOURCEMESH_API THandle< physics::SystemResource > GetActivePhysics(const CMesh& meshData);
		extern RESOURCEMESH_API THandle< physics::ICollider > GetObstacle(const CMesh& meshData);
		extern RESOURCEMESH_API Bool HasDestructibleData(const CMesh& meshData);

		//----
		
		extern RESOURCEMESH_API THandle< vis::IOccluderResource > GetOccluderResource(const CMesh& meshData);

		//----

		/// Find stream of specified type in mesh chunk's stream array
		extern RESOURCEMESH_API ChunkStreamDataRO* FindStream( red::DynArray<ChunkStreamDataRO>& streams, EMeshStreamType type );
		
		//----

        extern RESOURCEMESH_API Bool HasSkinnedStreams( const DataViewRO& view );

	} // utils

} // mesh