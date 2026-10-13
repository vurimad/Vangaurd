#pragma once

#include "fbxCommon.h"
//-----------------------------------------------------------------------------

class FBXMeshBuilder
{
public:
	struct Material
	{
		red::String m_name;
		Int32 m_flag;

		inline Material()
			: m_flag( -1 )
		{}
	};

	struct Vertex
	{
		Uint32 m_point{ 0 };
		Vector3 m_pos{ 0.f };
		Vector3 m_normal{ 0.f };
		Vector3 m_tangent{ 0.f };
		Vector3 m_bitangent{ 0.f };
		Vector2 m_uv0{ 0.f };
		Vector2 m_uv1{ 0.f };
		Vector4 m_color{ 1, 1, 1, 1 };
		Float m_weights[4]{};
		Uint32 m_indices[4]{}; // global bone table
	};

	struct Triangle
	{
		Material* m_material;
		Vertex m_v[3];
		Vector3 m_normal;
		Vector3 m_tangent;
		Vector3 m_bitangent;
		Triangle* m_next;
		Uint8 m_validTangents:1;
		Uint8 m_validNormals:1;
		Uint8 m_validColor:1;
		Uint8 m_validUV0:1;
		Uint8 m_validUV1:1;

		inline Triangle(Material* material)
			: m_material(material)
			, m_normal(0,0,0)
			, m_tangent(0,0,0)
			, m_bitangent(0,0,0)
			, m_next(nullptr)
			, m_validTangents(0)
			, m_validNormals(0)
			, m_validColor(0)
			, m_validUV0(0)
			, m_validUV1(0)
		{}
	};

	struct LODLevel
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		Uint32 m_lodIndex;
		red::DynArray<Triangle>	m_triangles;
		FBXPointCache<2048> m_points;		
		red::DynArray<red::DynArray<Uint32>> m_shared;

		red::DynArray<Triangle*> m_materialTriangles;

		LODLevel(const Uint32 index);
		~LODLevel();

		void GenerateConnectivity();

		void CalculateNormals();

		void CalculateTangentSpace();

		void BuildTriangleLists();

		FBXMeshBuilder::Triangle* AddTriangle(FBXMeshBuilder::Material* material, const Vertex& a, const Vertex& b, const Vertex& c);
	};

	struct CollisionMesh
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		enum ECollisionType
		{
			CT_Convex,
			CT_Trimesh
		};

		red::DynArray< Vector4 > m_vertices;
		red::DynArray< Uint32 > m_indices;
		ECollisionType m_collisionType;
		red::DynArray<CName> m_materialNames;
		red::DynArray<unsigned short> m_materialIndexes;
	};

	struct Bone
	{
		const FbxNode* m_source;
		red::String m_name;
		Int32 m_parent;
		Matrix m_localToWorld;

		inline Bone(const Int32 parent, const red::String& name)
			: m_source(nullptr)
			, m_name(name)
			, m_parent(parent)
		{};
	};

public:
	red::DynArray<Material> m_materials;
	red::DynArray<LODLevel*> m_lods;
	red::DynArray<CollisionMesh*> m_collision;
	red::DynArray<Bone> m_bones;

	const FbxPose* m_bindPose;

	FBXMeshBuilder();
	~FBXMeshBuilder();

	LODLevel* GetLodLevel(const Uint32 lodIndex);

	Int32 AddBone(const FbxNode* node);

	Int32 AddBone(const FbxNode* node, const FbxAMatrix& matrix);

	Uint32 AddMaterial(const FbxSurfaceMaterial* material, const FbxNode* owner);

	void GetLocalToWorld(const FbxNode* node, Matrix& localToWorld);

	Bool GetBindPoseLocalToWorld(const FbxNode* node, Matrix& localToWorld);

	void ExtractMesh(const Uint32 lodIndex, const FbxMesh* mesh, const Matrix& localToWorld, const FbxNode* forceSkinNode = nullptr);

	void ExtractCollisionMesh(const FbxMesh* mesh, const Matrix& localToWorld, CollisionMesh::ECollisionType collisionType);

	static Matrix MakeRotation( const FbxDouble3& rot, EFbxRotationOrder rotOrder );

	static Matrix MakeTranslation( const FbxDouble3& pos );
};

//-----------------------------------------------------------------------------
