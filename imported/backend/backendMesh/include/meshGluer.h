#pragma once

class CMesh;

namespace mesh
{
	class DataViewRO;
}

namespace physics
{
	class SystemResource;
}

namespace mesh
{
	struct MeshParts;
	
	struct MeshInput
	{
		// Mesh to glue
		const CMesh* m_mesh;

		// Optional custom physics resource to merge, gluer WILL change and modify those to produce
		// merged custom physics resource
		THandle< physics::SystemResource > m_customPhysics;

		// Optional transform applied to input mesh chunks
		Matrix m_transform;

		// Mesh appearance to merge into the final merged mesh
		CName m_meshAppearanceToMerge;
	};

	struct MeshGluerInput
	{
		// List of meshes to coalesce.
		red::DynArray<MeshInput> m_meshes;

		// If specified, only chosen LOD level will make it into merged mesh (as LOD0)
		Int32 m_lodToMerge = -1;

		// Should each of the source meshes get its unique transform.
		Bool m_transform = false;

		// Assign a bone to each input mesh. Destroys existing skinning data if present.
		Bool m_generateSkinning = false;

		// Removes skinning data from merged mesh
		Bool m_deleteSkinning = false;

		// Optimize mesh by removing never visible triangles ( DOES NOT ACCOUNT FOR TRANSPARENCY )
		Bool m_optimizeMesh = false;
	};

	using MeshPartsPtr = red::SharedPtr< MeshParts >;

	class BACKEND_MESH_API MeshGluer
	{
	public:
		// Coalesce meshes according to passed parametrization
		Bool Process(const MeshGluerInput& input, CMesh* output, job::Builder& builder);
		Bool Process_Sync(const MeshGluerInput& input, CMesh* output);
	private:
		// Verify raw input to the merger
		Bool VerifyInput(const MeshGluerInput& input);

		// Check if input data is valid and can be merged
		Bool VerifyMeshParts( MeshPartsPtr meshParts);

		// Break down mesh into it's basic parts
		void DisassembleMeshes(const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts, Int32 lodToMerge );

		void DisassembleMaterials( const MeshInput* mesh, MeshPartsPtr meshParts, Int32 lodToMerge );

		// Break down chunks
		void DisassembleChunks(const MeshInput* mesh, Uint32 meshIndex, MeshPartsPtr meshParts, Int32 lodToMerge );
		void DisassembleLODs( const MeshInput* mesh, Uint32 meshIndex, MeshPartsPtr meshParts, Int32 lodToMerge );

		// Break down bones
		void DisassembleBones(const MeshInput* mesh, Uint32 meshIndex, MeshPartsPtr meshParts);

		// Break down parameters
		void DisassembleParameters( const MeshInput* mesh, red::DynArray<const rtti::ClassType *> &meshParamClasses, MeshPartsPtr meshParts );

		// Apply transformation to the chunk data
		void TransformChunks(const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts);

		// Merge chunks according to passed parameters
		void AssembleMesh( MeshPartsPtr meshParts, CMesh* output, const MeshGluerInput& input, job::Builder& builder);
		void AssembleMesh_Sync(MeshPartsPtr meshParts, CMesh* output, const MeshGluerInput& input );

		// Generate skinning for a collection of static meshes
		void GenerateSkinning(const red::DynArray<MeshInput>& meshes, MeshPartsPtr meshParts);
		void DeleteSkinning( const red::DynArray<MeshInput>& m_meshes, MeshPartsPtr meshParts );
	};
}
