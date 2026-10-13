/**
* Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "meshPhysicsUtils.h"

#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshUtils.h"

#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemJoint.h"
#include "../../../common/physics/include/physicsSystemUtils.h"
#include "../../../common/physics/include/physXNatives.h"

RTTI_DECLARE_ENUM_IN_NAMESPACE(MeshSimpleCollisionType, tools);

RTTI_BEGIN_ENUM_IN_NAMESPACE(MeshSimpleCollisionType, tools);
	RTTI_ENUM_OPTION(Box);
	RTTI_ENUM_OPTION(Capsule);
	RTTI_ENUM_OPTION(Sphere);
	RTTI_ENUM_OPTION(Convex);
	RTTI_ENUM_OPTION(TriMesh);
RTTI_END_ENUM();

namespace tools
{
	///----
	void ExtractVerticesFromMesh(red::DynArray< Vector3 >& allVertices, const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex )
	{
		const mesh::DataViewRO& sourceView = mesh::DataViewRO(mesh);
		if( selectedVertices.Size() > 0 )
		{
			allVertices.Reserve( selectedVertices.Size() );
			for( const Vector3& vertex : selectedVertices )
			{
				allVertices.PushBack( vertex );
			}			
		}
		else if (boneIndex != -1)
		{
			// Compute number of triangles/vertices in the source geometry
			Uint32 numSourceTriangles = 0;
			Uint32 numSourceVertices = 0;

			red::DynArray< Uint32  > sourceChunks;
			for (Uint32 i = 0; i < sourceView.GetNumChunks(); ++i)
			{
				numSourceVertices += sourceView.GetChunkVertexCount(i);
				numSourceTriangles += sourceView.GetChunkIndexCount(i) / 3;
				sourceChunks.PushBack(i);
			}

			allVertices.Reserve(numSourceTriangles * 3);

			// build the mesh, encode the influences with colors
			for (const auto chunkIndex : sourceChunks)
			{
				const mesh::StreamViewRO<Vector3> dataPositions(sourceView.GetChunkStreamData(chunkIndex, MST_Position_3F));
				const mesh::StreamViewRO<Color> dataSkinIndices(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningIndices_4U8));
				const mesh::StreamViewRO<Color> dataSkinIndicesExt(sourceView.GetChunkStreamData(chunkIndex, MST_SkinningIndicesExt_4U8));
				for ( Uint16 i0 = 0; i0 < dataPositions.Size(); ++i0 )
				{
					const auto& p0 = dataPositions[i0];
					
					// compute normal and color
					if ( dataSkinIndices.IsValid() && dataSkinIndicesExt.IsValid() )
					{
						for (Uint32 i = 0; i < 4; ++i)
						{
							if (dataSkinIndices[i0].RGBA[i] == boneIndex || dataSkinIndicesExt[i0].RGBA[i] == boneIndex)
							{
								allVertices.EmplaceBack(p0);
								break;
							}
						}
						continue;
					}
					else if (dataSkinIndices.IsValid() )
					{
						for (Uint32 i = 0; i < 4; ++i)
						{
							if (dataSkinIndices[i0].RGBA[i] == boneIndex )
							{
								allVertices.EmplaceBack(p0);
								break;
							}
						}
						continue;
					}
				}
			}
		}
		else
		{
			mesh::utils::ExtractGeometry(sourceView, allVertices, nullptr);
		}
	}

	THandle< physics::ICollider > BuildBoxFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex )
	{
		red::DynArray< Vector3 > allVertices;
		ExtractVerticesFromMesh(allVertices, mesh, selectedVertices, boneIndex);
		if (allVertices.Empty())
			return nullptr;

		Box box;
		box.Clear();
		for (auto v : allVertices)
			box.AddPoint(v);

		auto shape = physics::ColliderBox::Create(box.CalcExtents());
		shape->m_localToBody = Transform( box.CalcCenter() );
		return shape;
	}

	THandle< physics::ICollider > BuildSphereFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex )
	{
		red::DynArray< Vector3 > allVertices;
		ExtractVerticesFromMesh(allVertices, mesh, selectedVertices, boneIndex);
		if (allVertices.Empty())
			return nullptr;

		Box box;
		box.Clear();
		for (auto v : allVertices)
			box.AddPoint(v);

		Vector3 center = box.CalcCenter();

		Float radiusSquared = 0.0f;
		for (auto v : allVertices)
			radiusSquared = std::max< Float >(radiusSquared, (v - center).SquareMag());

		auto shape = physics::ColliderSphere::Create(sqrtf(radiusSquared));
		shape->m_localToBody = Transform(center);
		return shape;
	}

	Bool ExtractCylinderParamsFromMesh( const CMesh& mesh, Float& height, Float& radius, Transform& pos, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex )
	{
		red::DynArray< Vector3 > allVertices;
		ExtractVerticesFromMesh(allVertices, mesh, selectedVertices, boneIndex);
		if (allVertices.Empty())
			return false;

		Box box;
		box.Clear();
		for ( auto v : allVertices )
			box.AddPoint( v );

		Vector4 center = box.CalcCenter();
		Vector4 size = box.CalcSize();

		// Find which axis is longest. Since Max returns exact copies of the input values, we don't need to worry about
		// Float equality.
		Float majorAxisLength = Max( size.X, size.Y, size.Z );
		Uint32 majorAxis = ( size.X == majorAxisLength ? 0 : ( size.Y == majorAxisLength ? 1 : 2 ) );

		// 'radius' is the maximum of the two remaining axes. Halve it since we want radius, not diameter.
		radius = ( majorAxis == 0 ? Max( size.Y, size.Z ) : ( majorAxis == 1 ? Max( size.X, size.Z ) : Max( size.X, size.Y ) ) ) * 0.5f;
		// 'height' of the cylinder is just the length of the largest axis
		height = std::max<Float>( 0.0f, majorAxisLength );

		pos.SetPosition(center);

		if ( majorAxis == 0 )
			pos.SetOrientation( Quaternion( Vector3::EY(), RED_PI / 2.0f ) );
		if ( majorAxis == 1 )
			pos.SetOrientation( Quaternion( Vector3::EX(), RED_PI / 2.0f ) );

		return true;
	}

	THandle< physics::ICollider > BuildCapsuleFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex )
	{
		Float height, radius;
		Transform xform;

		if ( !ExtractCylinderParamsFromMesh( mesh, height, radius, xform, selectedVertices, boneIndex ) )
			return nullptr;
				
		// 'height' of the capsule is just the length of the largest axis, minus twice the radius. This puts the ends of the mesh at the ends of capsule
		height = std::max<Float>(0.0f, height - 2 * radius);

		auto shape = physics::ColliderCapsule::Create(radius, height);
		shape->m_localToBody = xform;
		return shape;
	}

	THandle< physics::ICollider > BuildConvexFromMesh(const CMesh& mesh, red::ArraySpan<Vector3> selectedVertices, Int32 boneIndex, const Uint32 vertexLimit, Bool enablePlaneShifting )
	{
		red::DynArray< Vector3 > allVertices;
		ExtractVerticesFromMesh(allVertices, mesh, selectedVertices, boneIndex);
		if (allVertices.Empty())
			return nullptr;

		Box box;
		box.Clear();
		for (auto v : allVertices)
			box.AddPoint(v);
		const auto center = box.CalcCenter();

		for (auto& v : allVertices)
			v -= center;

		auto shape = physics::ColliderConvex::Create(allVertices.TypedData(), allVertices.Size(), vertexLimit, enablePlaneShifting );
		if( shape )
        {
			shape->m_localToBody = Transform(center);
        }

		return shape;
	}

	THandle< physics::SystemResource > BuildSystemFromCollider(THandle< physics::ICollider > singleCollider)
	{
		if (!singleCollider)
			return nullptr;

		physics::SystemBodyBuildData buildData;
		buildData.m_name = RED_NAME_CONSTEXPR("body");
		buildData.m_collisionShapes.PushBack(singleCollider);

		// create body
		auto body = physics::SystemBody::Create(buildData);
		if (!body)
			return nullptr;

		// create system with one body
		auto physics = CreateHandle< physics::SystemResource >();
		physics->AttachObject(body);

		// compute rough COM
        if( !singleCollider->IsA<physics::ColliderMesh>() )
        {
		    RecomputeCOM(*physics);
        }

		return physics;
	}

	const Bool RecomputeCOM(physics::SystemResource& system )
	{
		// process every body in the shape
		const auto& bodies = system.GetBodies();
		for( auto& body : bodies )
		{
			bool hasTriMesh = false;
			for( auto shape : body->m_collisionShapes )
			{
				if( shape->IsA< physics::ColliderMesh >() )
				{
					RED_LOG_ERROR("Collision: Body '%hs' has a triangle mesh shape. It's COM will not be computed.", body->GetName().AsChar());
					hasTriMesh = true;
					break;
				}
			}

			if( hasTriMesh )
				continue;

			// compute bounding box of the body
			Box bodyBox;
			bodyBox.Clear();
			for( const auto& shape : body->m_collisionShapes )
			{
				physics::ExtractedCollisionGeometry geom;
				shape->ExtractGeometry(Matrix::IDENTITY(), geom);

				for( const auto& v : geom.m_vertices )
					bodyBox.AddPoint(v);
			}

			// empty
			if( bodyBox.IsEmpty() )
			{
				RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collision: Shapes in body '%hs' have no volume. It's COM will not be computed.", body->GetName().AsChar());
				continue;
			}
			ComputeCOM( body->m_collisionShapes, body->m_params.m_comOffset, body->m_params.m_volume, body->m_params.m_mass, body->m_params.m_inertia, body->m_localToModel.ToMatrix() );
		}

		// done
		return true;
	}

	//---

	void CreateDefaultImportedTriMeshCollision(const CMesh& meshData, THandle< physics::SystemResource >& physics)
	{
		red::DynArray< Vector3 > allVertices;
		red::DynArray< Uint32 > allIndices;
		mesh::utils::ExtractGeometry(mesh::DataViewRO(meshData), allVertices, &allIndices);
		if (allVertices.Empty() || allVertices.Size() >= 65535) // 0xFFFF is reserver as well
			return;

		red::DynArray< Uint16 > allIndices16(allIndices.Size());
		for (auto i : allIndices.Indices())
			allIndices16[i] = (Uint16)allIndices[i];

		Vector4 center = Vector4::ZERO_3D_POINT();
		for (auto v : allVertices)
			center += v;
		center /= (Float)allVertices.Size();

		for (auto& v : allVertices)
			v -= center;

		auto shape = physics::ColliderMesh::Create((const Float*)allVertices.Data(), sizeof(Vector3), allVertices.Size(), allIndices16.TypedData(), allIndices16.Size(), true);

		if ( !shape )
			return;

		shape->m_localToBody = Transform(center);

		physics::SystemBodyBuildData buildData;
		buildData.m_name = RED_NAME_CONSTEXPR("body");
		buildData.m_collisionShapes.PushBack(shape);

		// create body
		auto body = physics::SystemBody::Create(buildData);
		if (!body)
			return;

		// create system with one body
		physics->AttachObject(body);
	}

	void CreateDefaultImportedConvexCollision(const CMesh& meshData, THandle< physics::SystemResource >& physics)
	{
		red::DynArray< Vector3 > allVertices;
		mesh::utils::ExtractGeometry(mesh::DataViewRO(meshData), allVertices, nullptr);
		if (allVertices.Empty()) // 0xFFFF is reserver as well
			return;

		Vector3 center = Vector3::ZEROS();
		for (auto v : allVertices)
			center += v;
		center /= (Float)allVertices.Size();

		for (auto& v : allVertices)
			v -= center;

		auto shape = physics::ColliderConvex::Create(allVertices.TypedData(), allVertices.Size());
		shape->m_localToBody = Transform(center);

		physics::SystemBodyBuildData buildData;
		buildData.m_name = RED_NAME_CONSTEXPR("body");
		buildData.m_collisionShapes.PushBack(shape);

		// create body
		auto body = physics::SystemBody::Create(buildData);
		if (!body)
			return;

		// create system with one body
		physics->AttachObject(body);

		// recompute rough COM
		RecomputeCOM( *physics );
	}
} // helper