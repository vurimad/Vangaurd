/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "fbxMeshBuilder.h"

using red::DynArray;

FBXMeshBuilder::LODLevel::LODLevel(const Uint32 index)
	: m_lodIndex(index)
{
}

FBXMeshBuilder::LODLevel::~LODLevel()
{
}

void FBXMeshBuilder::LODLevel::GenerateConnectivity()
{
	const Uint32 numPoints = m_points.GetNumPoints();
	m_shared.Resize(numPoints);

	const Uint32 numTriangles = m_triangles.Size();
	for (Uint32 i=0; i<numTriangles; ++i)
	{
		for (Uint32 j=0; j<3; ++j)
		{
			const Vertex& v = m_triangles[i].m_v[j];
			m_shared[v.m_point].PushBack(i);
		}
	}
}

static inline Float Vec3Dot(const Vector3& a, const Vector3& b)
{
	return (a.X*b.X) + (a.Y*b.Y) + (a.Z*b.Z);
}

void FBXMeshBuilder::LODLevel::CalculateNormals()
{
	const Uint32 numTriangles = m_triangles.Size();
	for (Uint32 i=0; i<numTriangles; ++i)
	{
		Triangle& srcTri = m_triangles[i];
		if (srcTri.m_validNormals)
		{
			continue;
		}

		// average normals
		for (Uint32 j=0; j<3; ++j)
		{
			Vertex& srcV = srcTri.m_v[j];

			srcV.m_normal = Vector3::ZEROS();//srcTri.m_normal;

			const DynArray<Uint32>& shared = m_shared[srcV.m_point];
			for (Uint32 k=0; k<shared.Size(); ++k)
			{
				const Uint32 otherTriIndex = shared[k];
				const Vector3& otherTriNormal = m_triangles[otherTriIndex].m_normal;
				srcV.m_normal += otherTriNormal;
			}

			srcV.m_normal.Normalize();
		}

		srcTri.m_validNormals = 1;
	}
}

void FBXMeshBuilder::LODLevel::CalculateTangentSpace()
{
	const Uint32 numTriangles = m_triangles.Size();
	for (Uint32 i=0; i<numTriangles; ++i)
	{
		Triangle& srcTri = m_triangles[i];
		if (srcTri.m_validTangents || !srcTri.m_validNormals)
		{
			continue;
		}

		// average tangents
		for (Uint32 j=0; j<3; ++j)
		{
			Vertex& srcV = srcTri.m_v[j];

			srcV.m_tangent = Vector3::ZEROS();//srcTri.m_tangent;
			srcV.m_bitangent = Vector3::ZEROS();//srcTri.m_bitangent;

			const DynArray<Uint32>& shared = m_shared[srcV.m_point];
			for (Uint32 k=0; k<shared.Size(); ++k)
			{
				const Uint32 otherTriIndex = shared[k];
				const Vector3& otherTriTangent = m_triangles[otherTriIndex].m_tangent;
				const Vector3& otherTriBitangent = m_triangles[otherTriIndex].m_bitangent;

				srcV.m_tangent += otherTriTangent;
				srcV.m_bitangent += otherTriBitangent;
			}

			srcV.m_tangent.Normalize();
			srcV.m_bitangent.Normalize();
		}

		srcTri.m_validTangents = 1;
	}
}

void FBXMeshBuilder::LODLevel::BuildTriangleLists()
{
	for (Uint32 i = 0; i < m_triangles.Size(); ++i)
	{
		Triangle* tri = &m_triangles[i];
		tri->m_material->m_flag = -1;
	}

	for (Uint32 i = 0; i < m_triangles.Size(); ++i)
	{
		Triangle* tri = &m_triangles[i];
		if (tri->m_material->m_flag == -1)
		{
			tri->m_material->m_flag = m_materialTriangles.Size();
			m_materialTriangles.PushBack(nullptr);
		}

		tri->m_next = m_materialTriangles[tri->m_material->m_flag];
		m_materialTriangles[tri->m_material->m_flag] = tri;
	}
}

FBXMeshBuilder::Triangle* FBXMeshBuilder::LODLevel::AddTriangle(FBXMeshBuilder::Material* material, const Vertex& a, const Vertex& b, const Vertex& c)
{
	const Uint32 pointA = m_points.MapPoint(a.m_pos);
	const Uint32 pointB = m_points.MapPoint(b.m_pos);
	const Uint32 pointC = m_points.MapPoint(c.m_pos);

	// skip degenerated triangles
	if (pointA == pointB || pointB == pointC || pointA == pointC)
		return NULL;

	Triangle& newTri = m_triangles.EmplaceBack(material);
	newTri.m_v[0] = a;
	newTri.m_v[1] = b;
	newTri.m_v[2] = c;

	// map points
	newTri.m_v[0].m_point = pointA;
	newTri.m_v[1].m_point = pointB;
	newTri.m_v[2].m_point = pointC;

	// calculate normal
	const Vector4 aPos = a.m_pos;
	const Vector4 bPos = b.m_pos;
	const Vector4 cPos = c.m_pos;
	newTri.m_normal = Vector4::Cross( bPos-aPos, cPos-aPos);

	// calculate tangent&bitangnet vectors
	{
		const Vector3& v1 = a.m_pos;
		const Vector3& v2 = b.m_pos;
		const Vector3& v3 = c.m_pos;

		const Vector2& w1 = a.m_uv0;
		const Vector2& w2 = b.m_uv0;
		const Vector2& w3 = c.m_uv0;

		const Double x1 = v2.X - v1.X;
		const Double x2 = v3.X - v1.X;
		const Double y1 = v2.Y - v1.Y;
		const Double y2 = v3.Y - v1.Y;
		const Double z1 = v2.Z - v1.Z;
		const Double z2 = v3.Z - v1.Z;

		const Double s1 = w2.X - w1.X;
		const Double s2 = w3.X - w1.X;
		const Double t1 = w2.Y - w1.Y;
		const Double t2 = w3.Y - w1.Y;

		const Double ir = (s1 * t2 - s2 * t1);
		if ( abs(ir) > 0.0000001)
		{
			newTri.m_tangent.X = (Float)((t2 * x1 - t1 * x2) / ir);
			newTri.m_tangent.Y = (Float)((t2 * y1 - t1 * y2) / ir);
			newTri.m_tangent.Z = (Float)((t2 * z1 - t1 * z2) / ir);

			newTri.m_bitangent.X = (Float)((s1 * x2 - s2 * x1) / ir);
			newTri.m_bitangent.Y = (Float)((s1 * y2 - s2 * y1) / ir);
			newTri.m_bitangent.Z = (Float)((s1 * z2 - s2 * z1) / ir);
		}
		else
		{
			newTri.m_tangent.X = 0.0f;
			newTri.m_tangent.Y = 0.0f;
			newTri.m_tangent.Z = 0.0f;
			newTri.m_bitangent.X = 0.0f;
			newTri.m_bitangent.Y = 0.0f;
			newTri.m_bitangent.Z = 0.0f;
		}
	}

	return &newTri;
}

FBXMeshBuilder::FBXMeshBuilder()
	: m_bindPose(NULL)
{
	m_materials.Reserve(256);
}

FBXMeshBuilder::~FBXMeshBuilder()
{
	red::alg::ClearPtr( m_lods );
	red::alg::ClearPtr( m_collision );
}

Uint32 FBXMeshBuilder::AddMaterial(const FbxSurfaceMaterial* material, const FbxNode* owner)
{
	const String materialName( material ? material->GetName() : "DefaultMaterial" );

	for (Uint32 i=0; i<m_materials.Size(); ++i)
	{
		const Material& m = m_materials[i];
		if (m.m_name == materialName)
		{
			return i;
		}
	}

	const Uint32 newMaterialIndex = m_materials.Size();
	Material& m = m_materials.EmplaceBack();
	m.m_name = materialName;
	return newMaterialIndex;
}

Matrix FBXMeshBuilder::MakeRotation( const FbxDouble3& rot, EFbxRotationOrder rotOrder )
{
	// Build rotation matrices
	Matrix rotX, rotY, rotZ;
	rotX.SetIdentity();
	rotY.SetIdentity();
	rotZ.SetIdentity();
	rotX.SetRotX33(DEG2RAD((float)rot[0]));
	rotY.SetRotY33(DEG2RAD((float)rot[1]));
	rotZ.SetRotZ33(DEG2RAD((float)rot[2]));

	// Calculate rotation, depends on the order of rotation	
	Matrix ret;
	if ( rotOrder == eEulerXYZ )
	{
		ret = rotX * rotY * rotY;
	}
	else if ( rotOrder == eEulerXZY )
	{
		ret = rotX * rotZ * rotY;
	}
	else if ( rotOrder == eEulerYXZ )
	{
		ret = rotY * rotX * rotZ;
	}
	else if ( rotOrder == eEulerYZX )
	{
		ret = rotY * rotZ * rotX;
	}
	else if ( rotOrder == eEulerZXY )
	{
		ret = rotZ * rotX * rotY;
	}
	else if ( rotOrder == eEulerZYX )
	{
		ret = rotZ * rotY * rotX;
	}

	return ret;
}

Matrix FBXMeshBuilder::MakeTranslation( const FbxDouble3& pos )
{
	Matrix trans;
	trans.SetIdentity();
	trans.SetTranslation((Float)pos[0], (Float)pos[1], (Float)pos[2]);
	return trans;
}

template<class T>
static Matrix ToMatrix( const T& matrix )
{
	Matrix localToWorld;
	localToWorld.SetIdentity();
	localToWorld[0].X = (Float)matrix.Get(0,0);
	localToWorld[0].Y = (Float)matrix.Get(0,1);
	localToWorld[0].Z = (Float)matrix.Get(0,2);
	localToWorld[1].X = (Float)matrix.Get(1,0);
	localToWorld[1].Y = (Float)matrix.Get(1,1);
	localToWorld[1].Z = (Float)matrix.Get(1,2);
	localToWorld[2].X = (Float)matrix.Get(2,0);
	localToWorld[2].Y = (Float)matrix.Get(2,1);
	localToWorld[2].Z = (Float)matrix.Get(2,2);
	localToWorld[3].X = (Float)matrix.Get(3,0);
	localToWorld[3].Y = (Float)matrix.Get(3,1);
	localToWorld[3].Z = (Float)matrix.Get(3,2);
	return localToWorld;
}

Bool FBXMeshBuilder::GetBindPoseLocalToWorld(const FbxNode* node, Matrix& localToWorld)
{
	// no bind pose, find it
	if (NULL == m_bindPose)
	{
		const Uint32 numPoses = node->GetScene()->GetPoseCount();
		for ( Uint32 i=0; i<numPoses; ++i )
		{
			const FbxPose* pose = node->GetScene()->GetPose(i);
			if ( pose->IsBindPose() )
			{
				RED_LOG_SPAM( "FBXMeshBuilder: Found bind pose: '%hs', %d nodes", pose->GetName(), pose->GetCount() );
				for ( Uint32 j=0; j<(Uint32)pose->GetCount(); ++j)
				{
					RED_LOG_SPAM( "FBXMeshBuilder: PoseBone[%d]: %hs", j, pose->GetNodeName(j).GetCurrentName() );
				}
				m_bindPose = pose;
				break;
			}

		}
	}

	// find bone in the bind pose
	if (NULL != m_bindPose)
	{
		const Uint32 numNodes = m_bindPose->GetCount();
		for ( Uint32 i=0; i<numNodes; ++i )
		{
			if ( node == m_bindPose->GetNode(i) )
			{
				const FbxMatrix matrix = m_bindPose->GetMatrix(i);
				localToWorld = ToMatrix(matrix);
				return true;				
			}
		}

		RED_LOG_SPAM( "FBXMeshBuilder: Node '%hs' not found in bond pose", node->GetName() );
	}

	// default case
	GetLocalToWorld(node, localToWorld);
	return false;
}

void FBXMeshBuilder::GetLocalToWorld(const FbxNode* node, Matrix& localToWorld)
{
	const FbxAMatrix matrix = ((FbxNode*)node)->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE, FbxNode::eSourcePivot, false, true);
	localToWorld = ToMatrix(matrix);
}

Int32 FBXMeshBuilder::AddBone(const FbxNode* node)
{
	// Root node, not mapped
	if (!node || node == node->GetScene()->GetRootNode())
	{
		return -1;
	}

	// Try to use already mapped bone
	for (Uint32 i=0; i<m_bones.Size(); ++i)
	{
		const Bone& b = m_bones[i];
		if (b.m_source == node)
		{
			return i;
		}
	}

	// Map parent first
	Int32 parentNode = AddBone(node->GetParent());

	// Create node
	const Uint32 boneIndex = m_bones.Size();
	Bone& bone = m_bones.EmplaceBack( parentNode, node->GetName() );
	bone.m_source = node;
	GetLocalToWorld(node, bone.m_localToWorld);

	return boneIndex;
}

Int32 FBXMeshBuilder::AddBone(const FbxNode* node, const FbxAMatrix& matrix)
{
	// Root node, not mapped
	if (!node || node == node->GetScene()->GetRootNode())
	{
		return -1;
	}

	// Try to use already mapped bone
	for (Uint32 i=0; i<m_bones.Size(); ++i)
	{
		const Bone& b = m_bones[i];
		if (b.m_source == node)
		{
			return i;
		}
	}

	// Map parent first
	Int32 parentNode = AddBone(node->GetParent());

	// Create node
	const Uint32 boneIndex = m_bones.Size();
	Bone& bone = m_bones.EmplaceBack( parentNode, node->GetName());
	bone.m_source = node;
	bone.m_localToWorld = ToMatrix(matrix);

	return boneIndex;
}

static Vector4 ToVector(const FbxVector4& src)
{
	return Vector4((float)src[0], (float)src[1], (float)src[2], (float)src[3]);
}

FBXMeshBuilder::LODLevel* FBXMeshBuilder::GetLodLevel(const Uint32 lodIndex)
{
	for (Uint32 i=0; i<m_lods.Size(); ++i)
	{
		if (m_lods[i]->m_lodIndex == lodIndex)
		{
			return m_lods[i];
		}
	}

	FBXMeshBuilder::LODLevel* lodLevel = RED_NEW( FBXMeshBuilder::LODLevel )(lodIndex);
	m_lods.PushBack(lodLevel);
	return lodLevel;
}

struct VertexInfluence
{
	Uint32 m_indices[4];
	Float m_weights[4];
	Uint8 m_numBones;

	inline void AddInfluence(const Uint32 index, const Float weight)
	{
		if (m_numBones < ARRAYSIZE(m_indices))
		{
			m_indices[m_numBones] = index;
			m_weights[m_numBones] = weight;
			m_numBones += 1;
		}
		else
		{
			Uint32 smallest = 0;
			for (Uint32 i=1; i<ARRAYSIZE(m_indices); ++i)
			{
				if (m_weights[i] < m_weights[smallest])
				{
					smallest = i;
				}
			}

			if (weight > m_weights[smallest])
			{
				m_indices[smallest] = index;
				m_weights[smallest] = weight;
			}
		}
	}

	inline void Normalize()
	{
		Float weightSum = 0.0f;
		for (Uint32 i=0; i<ARRAYSIZE(m_indices); ++i)
		{
			weightSum += m_weights[i];
		}

		if (weightSum > 0)
		{
			for (Uint32 i=0; i<ARRAYSIZE(m_indices); ++i)
			{
				m_weights[i] /= weightSum;
			}
		}
	}
};

void FBXMeshBuilder::ExtractCollisionMesh( const FbxMesh* pMesh, const Matrix& localToWorld, CollisionMesh::ECollisionType collisionType )
{
	// Vertices
	const Uint32 numVertices = pMesh->GetControlPointsCount();
	const FbxVector4* pControlPoints = pMesh->GetControlPoints();
	RED_LOG_SPAM( "FBXMeshBuilder: Found %d control points (vertices) in collision mesh", numVertices);

	// Polygons
	const Uint32 numPolygons = pMesh->GetPolygonCount();
	RED_LOG_SPAM( "FBXMeshBuilder: Found %d polygons in collision mesh", numPolygons);

	// Create collision mesh data
	CollisionMesh* colMesh = RED_NEW( CollisionMesh )();
	colMesh->m_collisionType = collisionType;

	m_collision.PushBack(colMesh);

	Bool bIsAllSame = true;
	Uint32 meshMaterialID = 0;
	if ( pMesh->GetElementMaterialCount() > 0 )
	{
		const FbxGeometryElementMaterial* pMaterialElement = pMesh->GetElementMaterial(0);
		if (pMaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon) 
		{
			RED_LOG_SPAM( "FBXMeshBuilder: HasMaterialMapping: true" );
			bIsAllSame = false;
		}
		else if ( pMaterialElement && pMaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame ) 
		{
			FbxSurfaceMaterial* pMaterial = pMesh->GetNode()->GetMaterial( pMaterialElement->GetIndexArray().GetAt(0) );    
			meshMaterialID = pMaterialElement->GetIndexArray().GetAt(0);
			RED_LOG_SPAM( "FBXMeshBuilder: HasMaterialMapping: false, Material=%d", meshMaterialID );
		}
	}
	DynArray<Int32> materialMapping;
	materialMapping.Resize(pMesh->GetNode()->GetMaterialCount()+1);
	for (Uint32 i=0; i<materialMapping.Size(); ++i)
	{
		materialMapping[i] = -1;
	}

	// Extract vertices
	colMesh->m_vertices.Resize(numVertices);
	for (Uint32 i=0; i<numVertices; i++)
	{
		colMesh->m_vertices[i] = localToWorld.TransformPoint(ToVector(pControlPoints[i]));
	}

	// Extract indices
	for (Uint32 i=0; i<numPolygons; i++)
	{
		// Extract polygon vertices
		const Uint32 polygonSize = pMesh->GetPolygonSize(i);
		for (Uint32 j=2; j<polygonSize; j++)
		{
			const Uint32 a = pMesh->GetPolygonVertex(i, 0);
			const Uint32 b = pMesh->GetPolygonVertex(i, j-1);
			const Uint32 c = pMesh->GetPolygonVertex(i, j);

			colMesh->m_indices.PushBack(a);
			colMesh->m_indices.PushBack(b);
			colMesh->m_indices.PushBack(c);
		}

		// Get Material index
		Uint32 polygonMaterialID = meshMaterialID;
		if (!bIsAllSame)
		{
			const FbxGeometryElementMaterial* pMaterialElement = pMesh->GetElementMaterial(0);
			polygonMaterialID = pMaterialElement->GetIndexArray().GetAt(i);
		}

		// Map polygon material
		Int32 realPolygonMaterialId = materialMapping[polygonMaterialID];
		if (realPolygonMaterialId == -1)
		{
			FbxSurfaceMaterial* pMaterial = pMesh->GetNode()->GetMaterial(polygonMaterialID);
			const CName materialName = pMaterial ? RED_NAME( pMaterial->GetName() ) : RED_NAME_CONSTEXPR( "DefaultMaterial" );

			for ( Uint32 i = 0; i < colMesh->m_materialNames.Size(); ++i )
			{
				if ( colMesh->m_materialNames[i] == materialName )
				{
					realPolygonMaterialId = i;
					break;
				}
			}

			if ( realPolygonMaterialId == -1 )
			{
				realPolygonMaterialId = colMesh->m_materialNames.Size();
				colMesh->m_materialNames.PushBack( materialName );
			}
			materialMapping[polygonMaterialID] = realPolygonMaterialId;
		}
		colMesh->m_materialIndexes.PushBack( realPolygonMaterialId );
	}
}

void FBXMeshBuilder::ExtractMesh(const Uint32 lodIndex, const FbxMesh* pMesh, const Matrix& localToWorld, const FbxNode* forceSkinNode/*=NULL*/)
{
	// Vertices
	const Uint32 numVertices = pMesh->GetControlPointsCount();
	const FbxVector4* pControlPoints = pMesh->GetControlPoints();
	RED_LOG_SPAM( "FBXMeshBuilder: Found %d control points (vertices)", numVertices);

	// Polygons
	const Uint32 numPolygons = pMesh->GetPolygonCount();
	RED_LOG_SPAM( "FBXMeshBuilder: Found %d polygons", numPolygons);

	// Get LOD mesh
	LODLevel* lodLevel = GetLodLevel(lodIndex);
	if (NULL == lodLevel)
	{
		return;
	}

	// Flags
	const Bool bHasNormal = (pMesh->GetElementNormalCount() >= 1);
	const Bool bHasTangents = (pMesh->GetElementTangentCount() >= 1);
	const Bool bHasBitangents = (pMesh->GetElementBinormalCount() >= 1);
	const Bool bHasUV0 = (pMesh->GetElementUVCount() >= 1);
	const Bool bHasUV1 = (pMesh->GetElementUVCount() >= 2);
	const Bool bHasColor = (pMesh->GetElementVertexColorCount() >= 1);
	RED_LOG_SPAM( "FBXMeshBuilder: HasNormals: %s", bHasNormal ? TXT("true") : TXT("false"));
	RED_LOG_SPAM( "FBXMeshBuilder: HasTangents: %s", bHasTangents ? TXT("true") : TXT("false"));
	RED_LOG_SPAM( "FBXMeshBuilder: HasBitangents: %s", bHasBitangents ? TXT("true") : TXT("false"));
	RED_LOG_SPAM( "FBXMeshBuilder: HasUV0: %s", bHasUV0 ? TXT("true") : TXT("false"));
	RED_LOG_SPAM( "FBXMeshBuilder: HasUV1: %s", bHasUV1 ? TXT("true") : TXT("false"));
	RED_LOG_SPAM( "FBXMeshBuilder: HasColor: %s", bHasColor ? TXT("true") : TXT("false"));

	// Skinning
	DynArray<VertexInfluence> skinInfluences;
	if ( forceSkinNode )
	{
		RED_LOG_SPAM( "FBXMeshBuilder: HasSkinning: true (NODE '%hs')", forceSkinNode->GetName() );

		// prepare influence table
		skinInfluences.Resize(numVertices);
		red::Memset(skinInfluences.Data(), 0, skinInfluences.DataSize());

		// map the single bone
		Uint32 boneIndex = AddBone(forceSkinNode);

		// all influences reference single bone
		for (Uint32 i=0; i<numVertices; ++i)
		{
			skinInfluences[i].AddInfluence(boneIndex, 1.0f);
		}
	}
	else if ( pMesh->GetDeformerCount(FbxDeformer::eSkin) > 0 )
	{
		const FbxSkin* pSkin = pSkin = (const FbxSkin*) pMesh->GetDeformer(0, FbxDeformer::eSkin);
		RED_LOG_SPAM( "FBXMeshBuilder: HasSkinning: true (%d clusters)", pSkin->GetClusterCount());

		// prepare influence table
		skinInfluences.Resize(numVertices);
		red::Memset(skinInfluences.Data(), 0, skinInfluences.DataSize());

		// load influences
		Uint32 numTotalInfluences = 0;
		const Uint32 numClusters =  pSkin->GetClusterCount();
		for (Uint32 i=0; i<numClusters; ++i)
		{
			const FbxCluster* pCluster = pSkin->GetCluster(i);

			const FbxNode* pLinkNode = pCluster->GetLink();
			if (NULL == pLinkNode)
			{
				continue;
			}

			FbxAMatrix linkMatrix;
			pCluster->GetTransformLinkMatrix(linkMatrix);

			const Uint32 globalBoneIndex = AddBone(pLinkNode, linkMatrix);
			//LOG(TXT("Cluster %d link to '%s' mapped as %d"), i, ANSI_TO_UNICODE(pLinkNode->GetName()), globalBoneIndex);

			const Uint32 numInfluences = pCluster->GetControlPointIndicesCount();
			const int* pIndices = pCluster->GetControlPointIndices();
			const double* pWeights = pCluster->GetControlPointWeights();
			for (Uint32 j=0; j<numInfluences; ++j)
			{
				const Uint32 pointIndex = pIndices[j];
				const Float weight = (Float)pWeights[j];
				skinInfluences[pointIndex].AddInfluence(globalBoneIndex, weight);
				numTotalInfluences += 1;
			}
		}

		RED_LOG_SPAM( "FBXMeshBuilder: %d total influences loaded", numTotalInfluences );
	}

	// Materials
	bool bIsAllSame = true;
	if ( pMesh->GetElementMaterialCount() > 0 )
	{
		const FbxGeometryElementMaterial* pMaterialElement = pMesh->GetElementMaterial(0);
		if (pMaterialElement->GetMappingMode() == FbxGeometryElement::eByPolygon) 
		{
			RED_LOG_SPAM( "FBXMeshBuilder: HasMaterialMapping: true" );
			bIsAllSame = false;
		}
	}

	// The same material is used for the whole mesh
	Uint32 meshMaterialID = 0;
	if (bIsAllSame)
	{
		const FbxGeometryElementMaterial* lMaterialElement = pMesh->GetElementMaterial(0);
		if ( lMaterialElement && lMaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame ) 
		{
			FbxSurfaceMaterial* lMaterial = pMesh->GetNode()->GetMaterial( lMaterialElement->GetIndexArray().GetAt(0) );    
			meshMaterialID = lMaterialElement->GetIndexArray().GetAt(0);
			RED_LOG_SPAM( "FBXMeshBuilder: HasMaterialMapping: false, Material=%d", meshMaterialID );
		}
	}

	// Streams
	const FbxGeometryElementNormal* pNormals = bHasNormal ? pMesh->GetElementNormal(0) : NULL;
	const FbxGeometryElementTangent* pTangents = bHasTangents ? pMesh->GetElementTangent(0) : NULL;
	const FbxGeometryElementBinormal* pBitangents = bHasBitangents ? pMesh->GetElementBinormal(0) : NULL;
	const FbxGeometryElementVertexColor* pColors = bHasColor ? pMesh->GetElementVertexColor(0) : NULL;
	const FbxGeometryElementUV* pUV0 = bHasUV0 ? pMesh->GetElementUV(0) : NULL;
	const FbxGeometryElementUV* pUV1 = bHasUV1 ? pMesh->GetElementUV(1) : NULL;

	// Temp vertices storage
	DynArray<Vertex> tempVertices;

	// Material mapping
	DynArray<Int32> materialMapping;
	materialMapping.Resize(pMesh->GetNode()->GetMaterialCount()+1);
	for (Uint32 i=0; i<materialMapping.Size(); ++i)
	{
		materialMapping[i] = -1;
	}

	// Extract polygons (and create triangles)
	Uint32 verteIndex = 0;
	Uint32 numTriangles = 0;
	for (Uint32 i=0; i<numPolygons; i++)
	{
		// Extract polygon vertices
		const Uint32 polygonSize = pMesh->GetPolygonSize(i);
		if (polygonSize > tempVertices.Size())
		{
			tempVertices.Resize(polygonSize);
		}

		// Get Material index
		Uint32 polygonMaterialID = meshMaterialID;
		if (!bIsAllSame)
		{
			const FbxGeometryElementMaterial* pMaterialElement = pMesh->GetElementMaterial(0);
			polygonMaterialID = pMaterialElement->GetIndexArray().GetAt(i);
		}

		// Map polygon material
		Int32 realPolygonMaterialId = materialMapping[polygonMaterialID];
		if (realPolygonMaterialId == -1)
		{
			FbxSurfaceMaterial* pMaterial = pMesh->GetNode()->GetMaterial(polygonMaterialID);
			realPolygonMaterialId = AddMaterial(pMaterial, pMesh->GetNode());
			materialMapping[polygonMaterialID] = realPolygonMaterialId;
		}

		// Extract polygon vertices
		for (Uint32 j=0; j<polygonSize; j++, verteIndex++)
		{
			const Uint32 controlPointIndex = pMesh->GetPolygonVertex(i, j);

			// extract position
			Vertex& v = tempVertices[j];
			v.m_pos = localToWorld.TransformPoint(ToVector(pControlPoints[controlPointIndex]));

			// skinning
			if (!skinInfluences.Empty())
			{
				for (Uint32 k=0; k<4; ++k)
				{
					v.m_indices[k] = skinInfluences[controlPointIndex].m_indices[k];
					v.m_weights[k] = skinInfluences[controlPointIndex].m_weights[k];
				}
			}

			// extract first normal
			if (bHasNormal)
			{
				FbxVector4 normalData(0,0,0,0);

				if (pNormals->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pNormals->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							normalData = pNormals->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pNormals->GetIndexArray().GetAt(controlPointIndex);
							normalData = pNormals->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pNormals->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (pNormals->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							normalData = pNormals->GetDirectArray().GetAt(verteIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pNormals->GetIndexArray().GetAt(verteIndex);
							normalData = pNormals->GetDirectArray().GetAt(id);
							break;
						}
					}
				}

				v.m_normal = localToWorld.TransformVector(ToVector(normalData)).Normalized3();
			}

			// extract tangents
			if (bHasTangents && bHasBitangents)
			{
				FbxVector4 tangentData(0,0,0,0);

				if (pTangents->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (pTangents->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							tangentData = pTangents->GetDirectArray().GetAt(verteIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pTangents->GetIndexArray().GetAt(verteIndex);
							tangentData = pTangents->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pTangents->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pTangents->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							tangentData = pTangents->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pTangents->GetIndexArray().GetAt(controlPointIndex);
							tangentData = pTangents->GetDirectArray().GetAt(id);
							break;
						}
					}
				}

				FbxVector4 bitangentData(0,0,0,0);
				if (pBitangents->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (pBitangents->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							bitangentData = pBitangents->GetDirectArray().GetAt(verteIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pBitangents->GetIndexArray().GetAt(verteIndex);
							bitangentData = pBitangents->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pBitangents->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pBitangents->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							bitangentData = pBitangents->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pBitangents->GetIndexArray().GetAt(controlPointIndex);
							bitangentData = pBitangents->GetDirectArray().GetAt(id);
							break;
						}
					}
				}

				// Store
				v.m_tangent = localToWorld.TransformVector(ToVector(tangentData)).Normalized3();
				v.m_bitangent = localToWorld.TransformVector(ToVector(bitangentData)).Normalized3();
			}

			// exctract UV0
			if (bHasUV0)
			{
				FbxVector2 uvData(0,0);

				if (pUV0->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pUV0->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:							
						{
							uvData = pUV0->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pUV0->GetIndexArray().GetAt(controlPointIndex);
							uvData = pUV0->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pUV0->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					const Uint32 textureUVIndex = ((FbxMesh*)pMesh)->GetTextureUVIndex(i, j);
					if (pUV0->GetReferenceMode() == FbxGeometryElement::eDirect || 
						pUV0->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
					{
						uvData = pUV0->GetDirectArray().GetAt(textureUVIndex);
					}
				}

				v.m_uv0 = Vector2((float)uvData[0], 1.0f - (float)uvData[1]);
			}

			// extract UV1
			if (bHasUV1)
			{
				FbxVector2 uvData(0,0);

				if (pUV1->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pUV1->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:							
						{
							uvData = pUV1->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pUV1->GetIndexArray().GetAt(controlPointIndex);
							uvData = pUV1->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pUV1->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					const Uint32 textureUVIndex = ((FbxMesh*)pMesh)->GetTextureUVIndex(i, j);
					if (pUV1->GetReferenceMode() == FbxGeometryElement::eDirect || 
						pUV1->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
					{
						uvData = pUV1->GetDirectArray().GetAt(textureUVIndex);
					}
				}

				v.m_uv1 = Vector2((float)uvData[0], 1.0f - (float)uvData[1]);
			}

			// extract vertex color
			if (bHasColor)
			{
				FbxColor colorData(1,1,1,1);

				if (pColors->GetMappingMode() == FbxGeometryElement::eByControlPoint)
				{
					switch (pColors->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							colorData = pColors->GetDirectArray().GetAt(controlPointIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pColors->GetIndexArray().GetAt(controlPointIndex);
							colorData = pColors->GetDirectArray().GetAt(id);
							break;
						}
					}
				}
				else if (pColors->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (pColors->GetReferenceMode())
					{
					case FbxGeometryElement::eDirect:
						{
							colorData = pColors->GetDirectArray().GetAt(verteIndex);
							break;
						}

					case FbxGeometryElement::eIndexToDirect:
						{
							const Uint32 id = pColors->GetIndexArray().GetAt(verteIndex);
							colorData = pColors->GetDirectArray().GetAt(id);
							break;
						}
					}
				}

				v.m_color.X = (float)colorData.mRed;
				v.m_color.Y = (float)colorData.mGreen;
				v.m_color.Z = (float)colorData.mBlue;
				v.m_color.W = (float)colorData.mAlpha;
			}
		}

		// Triangulate
		for (Uint32 j=2; j<polygonSize; j++)
		{
			FBXMeshBuilder::Material* material = &m_materials[realPolygonMaterialId];
			FBXMeshBuilder::Triangle* tri = lodLevel->AddTriangle(material, tempVertices[0], tempVertices[j-1], tempVertices[j]);
			if (NULL != tri)
			{
				tri->m_validNormals = bHasNormal;
				tri->m_validTangents = bHasTangents;
				tri->m_validColor = bHasColor;
				tri->m_validUV0 = bHasUV0;
				tri->m_validUV1 = bHasUV1;

				++numTriangles;
			}
		}
	}

	// Done
	RED_LOG_SPAM( "FBXMeshBuilder: Extracted %d vertices and %d triangles", verteIndex, numTriangles );
}
