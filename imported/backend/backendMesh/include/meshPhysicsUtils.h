/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class CMesh;

namespace physics
{
	class SystemResource;
	class SystemJoint;
	class ICollider;

    struct DeferredCollection;
	struct SystemBody;
	struct PhysicsJointAngularLimitPair;
	struct PhysicsJointLimitConePair;
}

namespace anim
{
	class Rig;
}

namespace tools
{
	enum class MeshSimpleCollisionType
	{
		Box,
		Convex,
		Capsule,
		Sphere,
		TriMesh,
	};

	///
	extern BACKEND_MESH_API void ExtractVerticesFromMesh( red::DynArray< Vector3 >& allVertices, const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1);
	extern BACKEND_MESH_API Bool ExtractCylinderParamsFromMesh(const CMesh& mesh, Float& height, Float& radius, Transform& pos, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1);

	/// build a simple box collision for the whole mesh
	extern BACKEND_MESH_API THandle< physics::ICollider > BuildBoxFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1);

	/// build a simple sphere collision for the whole mesh
	extern BACKEND_MESH_API THandle< physics::ICollider > BuildSphereFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1);

	/// build a simple capsule collision for the whole mesh
	extern BACKEND_MESH_API THandle< physics::ICollider > BuildCapsuleFromMesh( const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1);

	/// build a simple convex collision for the whole mesh
	extern BACKEND_MESH_API THandle< physics::ICollider > BuildConvexFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices = red::ArraySpan<Vector3>(), Int32 boneIndex = -1, const Uint32 vertexLimit = 64, Bool enablePlaneShifting = false );

	/// build a physical system from one single collider
	extern BACKEND_MESH_API THandle< physics::SystemResource > BuildSystemFromCollider(THandle< physics::ICollider > singleCollider);

	/// recompute COM for the whole system bodies
	extern BACKEND_MESH_API const Bool RecomputeCOM(physics::SystemResource& system );

	/// very high-level helper for mesh importers
	/// this generates a triangle mesh based collision and sets it as imported collision
	extern BACKEND_MESH_API void CreateDefaultImportedTriMeshCollision(const CMesh& mesh, THandle< physics::SystemResource >& physics);

	/// very high-level helper for mesh importers
	/// this generates a single convex based collision and sets it as imported collision
	extern BACKEND_MESH_API void CreateDefaultImportedConvexCollision(const CMesh& mesh, THandle< physics::SystemResource >& physics);

} // tools