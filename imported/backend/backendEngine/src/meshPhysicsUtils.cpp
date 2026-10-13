/**
* Copyright © 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshPhysicsUtils.h"
#include "../../../common/engine/include/mesh.h"
#include "../../../common/physics/include/physicsCollider.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/renderData/include/renderObject.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/debugMeshBuilder.h"
#include "meshPhysicsInertiaCalculator.h"
#include "hacd/VHACD.h"

BEGIN_NAMESPACE_CLASS_RTTI(ConvexDecompositionSetup, tools);
PROPERTY_CATEGORY("Basic");
PROPERTY(m_maxNumVerticesPerCH).editable().range(4, 64);
PROPERTY(m_minVolumePerCH).editable().range(0.0f,0.01f);
PROPERTY(m_skinWidth).editable().range(0.00f, 0.2f);
PROPERTY_CATEGORY("Advanced");
PROPERTY(m_concavity).editable().range(0.0f,1.0f);
PROPERTY(m_resolution).editable().range(10000.0f, 5000000.0f);
PROPERTY(m_depth).editable().range(1,32);
PROPERTY(m_planeDownsampling).editable().range(1,16);
PROPERTY(m_convexhullDownsampling).editable().range(1,16);
PROPERTY(m_convexhullApproximation).editable();
PROPERTY_CATEGORY("Magic");
PROPERTY(m_pca).editable();
PROPERTY(m_alpha).editable().range(0.0f,1.0f);
PROPERTY(m_beta).editable().range(0.0f, 1.0f);
PROPERTY(m_gamma).editable().range(0.0f, 1.0f);
END_CLASS_RTTI();

/*
msg << "       --input                     Wavefront .obj input file name" << endl;
msg << "       --output                    VRML 2.0 output file name" << endl;
msg << "       --log                       Log file name" << endl;
msg << "       --resolution                Maximum number of voxels generated during the voxelization stage (default=100,000, range=10,000-16,000,000)" << endl;
msg << "       --depth                     Maximum number of clipping stages. During each split stage, parts with a concavity higher than the user defined threshold are clipped according the \"best\" clipping plane (default=20, range=1-32)" << endl;
msg << "       --concavity                 Maximum allowed concavity (default=0.0025, range=0.0-1.0)" << endl;
msg << "       --planeDownsampling         Controls the granularity of the search for the \"best\" clipping plane (default=4, range=1-16)" << endl;
msg << "       --convexhullDownsampling    Controls the precision of the convex-hull generation process during the clipping plane selection stage (default=4, range=1-16)" << endl;
msg << "       --alpha                     Controls the bias toward clipping along symmetry planes (default=0.05, range=0.0-1.0)" << endl;
msg << "       --beta                      Controls the bias toward clipping along revolution axes (default=0.05, range=0.0-1.0)" << endl;
msg << "       --gamma                     Controls the maximum allowed concavity during the merge stage (default=0.00125, range=0.0-1.0)" << endl;
msg << "       --delta                     Controls the bias toward maximaxing local concavity (default=0.05, range=0.0-1.0)" << endl;
msg << "       --pca                       Enable/disable normalizing the mesh before applying the convex decomposition (default=0, range={0,1})" << endl;
msg << "       --mode                      0: voxel-based approximate convex decomposition, 1: tetrahedron-based approximate convex decomposition (default=0, range={0,1})" << endl;
msg << "       --maxNumVerticesPerCH       Controls the maximum number of triangles per convex-hull (default=64, range=4-1024)" << endl;
msg << "       --minVolumePerCH            Controls the adaptive sampling of the generated convex-hulls (default=0.0001, range=0.0-0.01)" << endl;
msg << "       --convexhullApproximation   Enable/disable approximation when computing convex-hulls (default=1, range={0,1})" << endl;
msg << "       --oclAcceleration           Enable/disable OpenCL acceleration (default=0, range={0,1})" << endl;
msg << "       --oclPlatformID             OpenCL platform id (default=0, range=0-# OCL platforms)" << endl;
msg << "       --oclDeviceID               OpenCL device id (default=0, range=0-# OCL devices)" << endl;
msg << "       --help                      Print usage" << endl*/

IMPLEMENT_ENGINE_CLASS_NAMESPACE(ConvexDecompositionSetup, tools);

namespace tools // TO BE MOVED TO BACKEND!!
{
	///----

	PointCache::PointCache(Uint32 bucketCount)
		: m_bucketCount(bucketCount)
		, m_bucketMask(bucketCount - 1)
	{
		RED_FATAL_ASSERT(red::IsPowerOf2(bucketCount), "Bucket count must be a power of 2");

		// bucket table
		m_cacheHeads = new Int32[m_bucketCount];
		for (Uint32 i = 0; i < bucketCount; ++i)
			m_cacheHeads[i] = -1;

		m_entiries.Reserve(m_bucketCount * 4);
	}

	PointCache::~PointCache()
	{
		delete[] m_cacheHeads;
		m_cacheHeads = NULL;
	}

	void PointCache::Clear()
	{
		m_entiries.Clear();
		m_entiries.Reserve(m_bucketCount * 4);

		for (Uint32 i = 0; i < m_bucketCount; ++i)
			m_cacheHeads[i] = -1;
	}

	const Uint32 PointCache::MapPoint(const Vector3& point)
	{
		// Calculate 3D hash
		const auto hash = CalcHash(point);

		// Check if such a point is already defined
		Int32 entryIndex = m_cacheHeads[hash];
		while (entryIndex != -1)
		{
			// Is it the same point ?
			const Entry& entry = m_entiries[entryIndex];
			if ((entry.m_point - point).IsAlmostZero())
				return entryIndex;

			// Go to the next entry with the same hash
			entryIndex = entry.m_next;
		}

		// Create new entry
		auto newEntryIndex = m_entiries.Size();
		Entry* newEntry = new (m_entiries) Entry(point);

		// Link the entry in the hash table
		newEntry->m_next = m_cacheHeads[hash];
		m_cacheHeads[hash] = newEntryIndex;

		// Return the entry index ( mapped point )
		return newEntryIndex;
	}

	///----

	THandle< phys::ICollider > BuildBoxFromMesh( const CMesh& mesh )
	{
		red::DynArray< Vector > allVertices;
		mesh.GetCollisionData( allVertices );
		if ( allVertices.Empty() )
			return nullptr;

		Box box;
		for ( auto v : allVertices )
			box.AddPoint(v);

		auto shape = phys::ColliderBox::Create( box.CalcExtents() );

		phys::QTransform pos;
		pos.m_position = box.CalcCenter();

		shape->SetLocalToBody( pos );
		return shape;
	}

	THandle< phys::ICollider > BuildSphereFromMesh( const CMesh& mesh )
	{
		red::DynArray< Vector > allVertices;
		mesh.GetCollisionData( allVertices );
		if ( allVertices.Empty() )
			return nullptr;

		Box box;
		for ( auto v : allVertices )
			box.AddPoint(v);

		Vector center = box.CalcCenter();

		Float radiusSquared = 0.0f;
		for ( auto v : allVertices )
			radiusSquared = std::max< Float >( radiusSquared, v.DistanceSquaredTo(center) );

		auto shape = phys::ColliderSphere::Create( sqrtf(radiusSquared) );

		phys::QTransform pos;
		pos.m_position = center;

		shape->SetLocalToBody( pos );
		return shape;
	}

	THandle< phys::ICollider > BuildCapsuleFromMesh( const CMesh& mesh )
	{
		red::DynArray< Vector > allVertices;
		mesh.GetCollisionData( allVertices );
		if ( allVertices.Empty() )
			return nullptr;

		Box box;
		for ( auto v : allVertices )
			box.AddPoint(v);

		Vector center = box.CalcCenter();
		Vector size = box.CalcSize();

		// Find which axis is longest. Since Max returns exact copies of the input values, we don't need to worry about
		// Float equality.
		Float majorAxisLength = Max( size.X, size.Y, size.Z );
		Uint32 majorAxis = ( size.X == majorAxisLength ? 0 : ( size.Y == majorAxisLength ? 1 : 2 ) );

		// 'radius' is the maximum of the two remaining axes. Halve it since we want radius, not diameter.
		Float radius = ( majorAxis == 0 ? Max( size.Y, size.Z ) : ( majorAxis == 1 ? Max( size.X, size.Z ) : Max( size.X, size.Y ) ) ) * 0.5f;
		// 'height' of the capsule is just the length of the largest axis, minus twice the radius. This puts the ends of the mesh at the ends of capsule
		Float height = std::max<Float>(0.0f, majorAxisLength - 2 * radius);

		auto shape = phys::ColliderCapsule::Create( radius, height );

		phys::QTransform pos;
		pos.m_position = center;

		if ( majorAxis == 0 )
			pos.m_rotation = Quaternion( Vector3::EY, M_PI / 2.0f );
		if ( majorAxis == 1 )
			pos.m_rotation = Quaternion( Vector3::EX, M_PI / 2.0f );

		shape->SetLocalToBody( pos );
		return shape;
	}

	THandle< phys::ICollider > BuildConvexFromMesh( const CMesh& mesh, const Float skinWidth /*= 0.02f*/)
	{
		red::DynArray< Vector > allVertices;
		mesh.GetCollisionData( allVertices );
		if ( allVertices.Empty() )
			return nullptr;

		Box box;
		for (auto v : allVertices)
			box.AddPoint(v);
		const auto center = box.CalcCenter();

		for (auto& v : allVertices)
			v -= center;

		auto shape = phys::ColliderConvex::Create((const Float*)allVertices.Data(), sizeof(Vector), allVertices.Size(), skinWidth);

		phys::QTransform pos;
		pos.m_position = center;

		shape->SetLocalToBody(pos);
		return shape;
	}

	THandle< phys::ICollider > BuildTriangleMeshFromMesh(const CMesh& mesh)
	{
		red::DynArray< Vector > allVertices;
		red::DynArray< Uint32 > allIndices;
		red::DynArray< Uint16 > allMaterialIndices;
		mesh.GetCollisionData(allVertices, allIndices, allMaterialIndices);
		if (allVertices.Empty() || allVertices.Size() >= 65535) // 0xFFFF is reserver as well
			return nullptr;

		red::DynArray< Uint16 > allIndices16(allIndices.Size());
		for (auto i : allIndices.Indices())
			allIndices16[i] = (Uint16)allIndices[i];

		Box box;
		for (auto v : allVertices)
			box.AddPoint(v);
		const auto center = box.CalcCenter();

		for (auto& v : allVertices)
			v -= center;

		auto shape = phys::ColliderMesh::Create((const Float*)allVertices.Data(), sizeof(Vector), allVertices.Size(), allIndices16.TypedData(), allIndices16.Size());

		phys::QTransform pos;
		pos.m_position = center;

		shape->SetLocalToBody(pos);
		return shape;
	}

	THandle< phys::SystemResource > BuildSystemFromCollider(THandle< phys::ICollider > singleCollider)
	{
		if (!singleCollider)
			return nullptr;

		phys::SystemBodyBuildData buildData;
		buildData.m_name = CName("body");
		buildData.m_params.m_simulation = phys::SystemObjectSimulationType::Simulated;
		buildData.m_collisionShapes.PushBack(singleCollider);

		// create body
		auto body = phys::SystemBody::Create(buildData);
		if (!body)
			return nullptr;

		// create system with one body
		auto physics = new phys::SystemResource();
		physics->AttachObject(body);

		// compute rough COM
		RecomputeCOM(*physics, 100);
		return physics;
	}

	namespace helper
	{
		class HACDUserLogger : public VHACD::IVHACD::IUserLogger{
		public:
			virtual void Log(const char* const msg)
			{
				RED_LOG("HACD: %hs", msg);
			}
		};

		class HACDProgress : public VHACD::IVHACD::IUserCallback {
		public:
			virtual void Update(const double overallProgress, const double stageProgress, const double operationProgress, const char* const stage, const char* const operation) override final
			{
			}
		};
	};

	THandle< phys::SystemResource > BuildConvexDecompositionMesh(const CMesh& mesh, const ConvexDecompositionSetup& setup)
	{
		red::DynArray< Vector > allVertices;
		red::DynArray< Uint32 > allIndices;
		red::DynArray< Uint16 > allMaterialIndices;
		mesh.GetCollisionData(allVertices, allIndices, allMaterialIndices);
		if (allVertices.Empty() || allVertices.Size() >= 65535) // 0xFFFF is reserver as well
			return nullptr;

		// Map vertices, we need to have unique pointers here
		PointCache cache(1024);
		red::DynArray<Uint32> mapping(allVertices.Size());
		for (Uint32 i : allVertices.Indices())
			mapping[i] = cache.MapPoint(allVertices[i]);

		// Copy glued points
		red::DynArray<Vector3> inputPoints(cache.GetNumPoints());
		for (Uint32 i = 0; i < cache.GetNumPoints(); ++i)
			inputPoints[i] = cache.GetPoint(i);


		// Copy triangles
		red::DynArray<Uint32> inputTriangles;
		inputTriangles.Reserve(allIndices.Size() / 3);
		for (Uint32 i = 0; i < allIndices.Size(); i += 3)
		{
			const auto a = mapping[allIndices[i + 0]];
			const auto b = mapping[allIndices[i + 1]];
			const auto c = mapping[allIndices[i + 2]];
			if (a != b && b != c && c != a)
			{
				inputTriangles.PushBack(a);
				inputTriangles.PushBack(b);
				inputTriangles.PushBack(c);
			}
		}

		RED_LOG("Mesh: Merged points %d->%d", allVertices.Size(), inputPoints.Size());
		RED_LOG("Mesh: Merged triangles %d->%d", allIndices.Size()/3, inputTriangles.Size());

		// Setup HACD
		VHACD::IVHACD::Parameters params;
		params.m_concavity = setup.m_concavity;
		params.m_alpha = setup.m_alpha;
		params.m_beta = setup.m_beta;
		params.m_gamma = setup.m_gamma;
		params.m_minVolumePerCH = setup.m_minVolumePerCH;
		params.m_resolution = setup.m_resolution;
		params.m_maxNumVerticesPerCH = setup.m_maxNumVerticesPerCH;
		params.m_depth = setup.m_depth;
		params.m_planeDownsampling = setup.m_planeDownsampling;
		params.m_convexhullDownsampling = setup.m_convexhullDownsampling;
		params.m_pca = setup.m_pca;
		params.m_convexhullApproximation = setup.m_convexhullApproximation;
		params.m_oclAcceleration = false;
		params.m_mode = 0;

		// Setup callback
		helper::HACDProgress progress;
		helper::HACDUserLogger logger;
		params.m_logger = &logger;
		params.m_callback = &progress;

		// create instance of the builder
		auto* interfaceVHACD = VHACD::CreateVHACD();
		if ( !interfaceVHACD )
		{
			RED_LOG_CATEGORY_ERROR(red::LoggerCategory_Physics, "Collsion: Failed to create VHAHC instance");
			return nullptr;
		}

		// build geometry
		if ( !interfaceVHACD->Compute((const float*)inputPoints.Data(), 3, inputPoints.Size(), 
			(const int*)inputTriangles.TypedData(), 3, inputTriangles.Size() / 3, params) )
		{ 
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collsion: Failed to compute convex decomposition");
			return nullptr;
		}

		// extract data
		const auto numShapes = interfaceVHACD->GetNConvexHulls();
		RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collsion: Found %d shapes", numShapes);

		phys::SystemBodyBuildData buildData;
		buildData.m_name = CName("hacd");
		buildData.m_params.m_simulation = phys::SystemObjectSimulationType::Simulated;

		// Generate convex parts
		for (Uint32 c = 0; c < numShapes; c++)
		{
			VHACD::IVHACD::ConvexHull ch;
			interfaceVHACD->GetConvexHull(c, ch);

			const auto numPoints = ch.m_nPoints;
			const auto numTriangles = ch.m_nTriangles;

			// Compute shape center
			Vector3 center(0, 0, 0);
			red::DynArray<Vector3> localVerts(numPoints);
			for (Uint32 i = 0; i < numPoints; ++i)
			{
				localVerts[i].X = (Float)ch.m_points[i*3+0];
				localVerts[i].Y = (Float)ch.m_points[i*3+1];
				localVerts[i].Z = (Float)ch.m_points[i*3+2];
				center += localVerts[i];
			}
			center /= (const Float)numPoints;

			for (Uint32 i = 0; i < numPoints; ++i)
				localVerts[i] -= center;

			// Create convex hull
			auto shape = phys::ColliderConvex::Create((const Float*)localVerts.TypedData(), sizeof(Vector3), localVerts.Size(), setup.m_skinWidth);
			if (shape)
			{
				RED_LOG("Collsion: Extracted convex %d faces", shape->GetNumFaces());

				phys::QTransform pos;
				pos.m_position = center;
				shape->SetLocalToBody(pos);

				buildData.m_collisionShapes.PushBack(shape);
			}
		}

		// no shapes
		if (buildData.m_collisionShapes.Empty())
		{
			RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collsion: Failed to convert HACD data to convex shapes");
			return nullptr;
		}

		// create body
		auto body = phys::SystemBody::Create(buildData);
		if (!body)
			return nullptr;

		// create system with one body
		auto physics = new phys::SystemResource();
		physics->AttachObject(body);
		RecomputeCOM(*physics, 100);

		// compute rough COM
		return physics;
	}

	//---

	const Bool RecomputeCOM(phys::SystemResource& system, const Uint32 gridSize)
	{
		// process every body in the shape
		for (auto object : system.GetObjects())
		{
			if (auto body = Cast< phys::SystemBody >(object))
			{
				bool hasTriMesh = false;
				for (auto shape : body->GetCollisionShapes())
				{
					if (shape->IsA< phys::ColliderMesh >())
					{
						RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collision: Body '%hs' has a triangle mesh shape. It's COM will not be computed.", body->GetName().AsChar());
						hasTriMesh = true;
						break;
					}
				}

				if (hasTriMesh)
					continue;

				// compute bounding box of the body
				Box bodyBox;
				for (const auto& shape : body->GetCollisionShapes())
				{
					phys::ExtractedCollisioGeometry geom;
					shape->ExtractGeometry(Matrix::IDENTITY, geom);

					for (const auto& v : geom.m_vertices)
						bodyBox.AddPoint(v);
				}

				// empty
				if (bodyBox.IsEmpty())
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collision: Shapes in body '%hs' have no volume. It's COM will not be computed.", body->GetName().AsChar());
					continue;
				}

				// create the computational grid
				mesh::InertiaCalculator calculator(gridSize);
				calculator.InitBoundsAndClear(bodyBox.Min, bodyBox.Max);

				// add the shapes
				for (const auto& shape : body->GetCollisionShapes())
				{
					if (const auto box = Cast<phys::ColliderBox>(shape))
					{
						calculator.AddBox(shape->GetLocalToBody().ToMatrix(), box->GetSize());
					}
					else if (const auto sphere = Cast<phys::ColliderSphere>(shape))
					{
						calculator.AddSphere(shape->GetLocalToBody().ToMatrix(), sphere->GetRadius());
					}
					else if (const auto capsule = Cast<phys::ColliderCapsule>(shape))
					{
						const Vector3 box(capsule->GetRadius(), capsule->GetRadius(), capsule->GetHeight());
						calculator.AddBox(shape->GetLocalToBody().ToMatrix(), box);
					}
					else if (const auto capsule = Cast<phys::ColliderCylinder>(shape))
					{
						const Vector3 box(capsule->GetRadius(), capsule->GetRadius(), capsule->GetHeight());
						calculator.AddBox(shape->GetLocalToBody().ToMatrix(), box);
					}
					else if (const auto convex = Cast<phys::ColliderConvex>(shape))
					{
						calculator.AddConvex(shape->GetLocalToBody().ToMatrix(), convex->GetHullData());
					}
				}

				// compute center of mass position and the shape volume
				Float volume = 1.0f;
				Vector3 com(0, 0, 0);
				if (!calculator.CalculateCentetOfMass(com, volume))
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collision: Unable to compute COM for '%hs'.", body->GetName().AsChar());
					continue;
				}

				body->GetParams().m_comOffset.m_position = com;
				body->GetParams().m_volume = std::max<Float>(0.0001f, volume);
				RED_LOG("Collsion: COM of '%hs': [%1.3f,%1.3f,%1.3f], volume %1.3f", body->GetName().AsChar(), com.X, com.Y, com.Z, volume);

				// compute inertia tensor
				mesh::InertiaCalculator::InertiaTensor tensor;
				if (!calculator.CalculateInertiaTensor(tensor, com))
				{
					RED_LOG_CATEGORY_ERROR( red::LoggerCategory_Physics, "Collision: Unable to compute interia tensor for '%hs'.", body->GetName().AsChar());
					continue;
				}

				// diagonalize the tensor
				Matrix comOrientation;
				tensor.Diagonalise(comOrientation, 0.1f, 32);

				// save the orientation of the COM reference frame and the diagonalized tensor
				body->GetParams().m_comOffset.m_rotation = comOrientation.ToQuat().Normalized();
				body->GetParams().m_inertia = Vector3((Float)tensor.m[0][0], (Float)tensor.m[1][1], (Float)tensor.m[2][2]);
			}
		}

		// done
		return true;
	}

	//---

	PhysicsVisualization::PhysicsVisualization()
		: m_debugMesh( nullptr )
	{}

	PhysicsVisualization::~PhysicsVisualization()
	{
		if ( m_debugMesh )
		{
			m_debugMesh->Release();
			m_debugMesh = nullptr;
		}
	}

	namespace helper
	{
		IRenderResource* CompileDebugMesh( const red::DynArray< phys::ICollider::RenderVertex >& renderGeometry )	
		{
			rend::DebugMeshBuilder builder;
			builder.DisableFakeLighting();

			for ( Uint32 i=0; i<renderGeometry.Size(); i += 3 )
			{
				const auto normal = (renderGeometry[i].m_triNormal * 0.5f) + Vector3(0.5f,0.5f,0.5f);
				builder.SetColor( Color( normal ) );

				builder.AddTriangle( renderGeometry[i].m_position, renderGeometry[i+1].m_position, renderGeometry[i+2].m_position );
			}

			return builder.CompileMesh();
		}
	}

	PhysicsVisualization::PhysicsVisualization( const THandle< phys::SystemResource >& res )
		: m_debugMesh( nullptr )
	{
		if ( !res )
			return;

		red::DynArray< phys::ICollider::RenderVertex > renderGeometry;

		for ( auto object : res->GetObjects() )
		{
			if ( const auto body = Cast< phys::SystemBody >( object ) )
			{
				DrawSphere sphere;
				sphere.m_pos = body->GetLocalToModel().m_position;
				sphere.m_color = Color::MAGENTA;
				sphere.m_radius = 0.02f;
				m_debugSpheres.PushBack( sphere );

				DrawText text;
				text.m_text = body->GetName().AsChar();
				text.m_pos = body->GetLocalToModel().m_position;
				m_debugText.PushBack( text );

				const Matrix bodyToModel = body->GetLocalToModel().ToMatrix();

				// extract shapes
				for ( auto shape : body->GetCollisionShapes() )
				{
					shape->ExtractRenderingMesh( bodyToModel, renderGeometry );

					DrawSphere sphere;
					sphere.m_pos = body->GetLocalToModel().ToMatrix().TransformPoint( shape->GetLocalToBody().m_position );
					sphere.m_color = Color::YELLOW;
					sphere.m_radius = 0.001f;
					m_debugSpheres.PushBack( sphere );
				}
			}
		}

		m_debugMesh = helper::CompileDebugMesh(renderGeometry);
	}

	PhysicsVisualization::PhysicsVisualization(  const THandle< phys::SystemBody >& body )
		: m_debugMesh( nullptr )
	{
		if ( !body )
			return;

		red::DynArray< phys::ICollider::RenderVertex > renderGeometry;

		const Matrix bodyToModel = body->GetLocalToModel().ToMatrix();
		for ( auto shape : body->GetCollisionShapes() )
		{
			shape->ExtractRenderingMesh( bodyToModel, renderGeometry );

			DrawSphere sphere;
			sphere.m_pos = body->GetLocalToModel().ToMatrix().TransformPoint( shape->GetLocalToBody().m_position );
			sphere.m_color = Color::YELLOW;
			sphere.m_radius = 0.002f;
			m_debugSpheres.PushBack( sphere );
		}

		m_debugMesh = helper::CompileDebugMesh(renderGeometry);
	}

	PhysicsVisualization::PhysicsVisualization(  const THandle< phys::SystemBody >& body, const THandle< phys::ICollider >& shape  )
		: m_debugMesh( nullptr )
	{
		if ( !body || !shape )
			return;

		const Matrix bodyToModel = body->GetLocalToModel().ToMatrix();

		red::DynArray< phys::ICollider::RenderVertex > renderGeometry;
		shape->ExtractRenderingMesh( bodyToModel, renderGeometry );

		m_debugMesh = helper::CompileDebugMesh(renderGeometry);
	}

	void PhysicsVisualization::Draw( rend::IDebugDrawer& dd, const Matrix& localToWorld )
	{
		rend::FragmentBuilder builder(dd);

		if ( m_debugMesh )
		{
			builder.BindTransform( localToWorld );
			builder.AddSolidMesh( m_debugMesh );
		}

		for ( const auto& info : m_debugSpheres )
		{
			builder.EnableOverlay( true );
			builder.SetColor( info.m_color );
			builder.AddWireSphere( info.m_pos, info.m_radius );
		}

		for ( const auto& info : m_debugText )
		{
			builder.EnableOverlay( false );
			builder.SetColor( Color::WHITE );
			builder.EnableTextBackground( Color::BLACK );
			builder.SetHorizontalTextAlignment(0);
			builder.SetVerticalTextAlignment(0);
			builder.AddWorldText( info.m_pos, 0, 0, info.m_text.AsChar() );
		}
	}

} // tools 