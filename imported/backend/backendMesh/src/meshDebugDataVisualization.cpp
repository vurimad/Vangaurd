#include "build.h"
#include "meshDebugDataVisualization.h"
#include "../../../common/engine/include/mesh.h"
#include "../../../common/engine/include/collisionShape.h"
#include "../../../common/engine/include/meshDataBuilder.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/engine/include/collisionMesh.h"

using red::DynArray;

void AppendVertex( const Float* start, const Float* end, Float bboxSize, DynArray< DebugVertex >& vertices, const Color& color )
{
	Vector vertexPos;
	vertexPos.Set3( start );
	vertexPos.W =  1.0f;

	Vector arrowPoint;
	arrowPoint.Set3( end );
	arrowPoint *= bboxSize / 40.f;
	arrowPoint += vertexPos;
	arrowPoint.W = 1.0f;

	vertices.PushBack( DebugVertex( vertexPos, color.ToUint32() ) );
	vertices.PushBack( DebugVertex( arrowPoint, color.ToUint32() ) );
}

Bool MeshDebugGeometry::HasCollision() const
{
	return (m_mesh->GetActivePhysics() != nullptr);
}

void MeshDebugGeometry::GetCollisionShapes( DynArray< MeshDesc >& collisions ) const
{
	auto physics = m_mesh->GetActivePhysics();
	if ( physics )
	{
		/*for ( const auto collision : m_mesh->GetCollisionMesh()->GetShapes() )
		{
			DynArray< DebugVertex > verts;
			DynArray< Uint32 > inds;

			collision->GenerateDebugMesh( verts, inds );
			collisions.PushBack( MeshDesc( verts, inds ) );
		}*/
	}
}

const Box& MeshDebugGeometry::GetBoundingBox() const
{
	return m_mesh->GetBoundingBox();
}

void MeshDebugGeometry::GetNormals( LinesDesc& normalsDesc ) const
{
	const CMeshData meshData( m_mesh.Get() );
	const auto& chunkArr = meshData.GetChunks();

	Int32 lod = 0; // need to find forcedLod here
	const CMesh::TChunkArray& meshChunks = m_mesh->GetMeshLODLevels()[lod].m_chunks;
	const Uint32 numCh = meshChunks.Size();

	Uint32 totalNormalsVerticesCount = 0;
	for ( Uint32 ch = 0; ch < numCh; ++ch )
	{
		// 2 points per vertex
		totalNormalsVerticesCount += chunkArr[meshChunks[ch]].m_numVertices * 2;
	}

	DynArray< DebugVertex > vertices;
	vertices.Reserve( totalNormalsVerticesCount );
	Float size = m_mesh->GetBoundingBox().CalcSize().Mag3();

	for ( Uint32 ch = 0, indiceNum = 0; ch < numCh; ++ch )
	{
		Uint16 chunkId = meshChunks[ch];

		const DynArray< SMeshVertex >& chunkVertices = chunkArr[chunkId].m_vertices;
		const Uint32 numVerts = chunkArr[chunkId].m_numVertices;

		for ( Uint32 v=0; v <numVerts; ++v )
		{
			const SMeshVertex& vertex = chunkVertices[v];
			AppendVertex( vertex.m_position, vertex.m_normal, size, vertices, Color::BLUE );
		}
	}
	normalsDesc.SetVertices( vertices );
}

void MeshDebugGeometry::GetTangents( LinesDesc& tangentsDesc ) const
{
	const CMeshData meshData( m_mesh.Get() );
	const auto& chunkArr = meshData.GetChunks();

	Int32 lod = 0; // need to find forcedLod here
	const CMesh::TChunkArray& meshChunks = m_mesh->GetMeshLODLevels()[lod].m_chunks;
	const Uint32 numCh = meshChunks.Size();

	Uint32 totalTangentsVerticesCount = 0;
	for ( Uint32 ch = 0; ch < numCh; ++ch )
	{
		// 2 points per vertex
		totalTangentsVerticesCount += chunkArr[meshChunks[ch]].m_numVertices * 2;
	}

	DynArray< DebugVertex > vertices;
	vertices.Reserve( totalTangentsVerticesCount );
	Float size = m_mesh->GetBoundingBox().CalcSize().Mag3();

	for ( Uint32 ch = 0, indiceNum = 0; ch < numCh; ++ch )
	{
		Uint16 chunkId = meshChunks[ch];

		const DynArray< SMeshVertex >& chunkVertices = chunkArr[chunkId].m_vertices;
		const Uint32 numVerts = chunkArr[chunkId].m_numVertices;

		for ( Uint32 v=0; v <numVerts; ++v )
		{
			const SMeshVertex& vertex = chunkVertices[v];
			AppendVertex( vertex.m_position, vertex.m_tangent, size, vertices, Color::GREEN );
		}
	}
	tangentsDesc.SetVertices( vertices );
}

void MeshDebugGeometry::GetBitangents( LinesDesc& bitangentsDesc ) const
{
	const CMeshData meshData( m_mesh.Get() );
	const auto& chunkArr = meshData.GetChunks();

	Int32 lod = 0; // need to find forcedLod here
	const CMesh::TChunkArray& meshChunks = m_mesh->GetMeshLODLevels()[lod].m_chunks;
	const Uint32 numCh = meshChunks.Size();

	Uint32 totalBitengentsVerticesCount = 0;
	for ( Uint32 ch = 0; ch < numCh; ++ch )
	{
		// 2 points per vertex
		totalBitengentsVerticesCount += chunkArr[meshChunks[ch]].m_numVertices * 2;
	}

	DynArray< DebugVertex > vertices;
	vertices.Reserve( totalBitengentsVerticesCount );
	Float size = m_mesh->GetBoundingBox().CalcSize().Mag3();

	for ( Uint32 ch = 0, indiceNum = 0; ch < numCh; ++ch )
	{
		Uint16 chunkId = meshChunks[ch];

		const DynArray< SMeshVertex >& chunkVertices = chunkArr[chunkId].m_vertices;
		const Uint32 numVerts = chunkArr[chunkId].m_numVertices;

		for ( Uint32 v=0; v <numVerts; ++v )
		{
			const SMeshVertex& vertex = chunkVertices[v];
			AppendVertex( vertex.m_position, vertex.m_binormal, size, vertices, Color::RED );
		}
	}
	bitangentsDesc.SetVertices( vertices );
}

void MeshDebugGeometry::GetBonesPositions( DynArray< Vector >& bones ) const
{
	Uint32 bonesCount = m_mesh->GetBoneCount();
	const Matrix* bonesMatrices = m_mesh->GetBoneRigMatrices();

	DynArray< DebugVertex > verts;
	DynArray< Uint32 > inds;

	for ( Uint32 i = 0; i < bonesCount; ++i )
	{
		Matrix mtx = bonesMatrices[i];
		bones.PushBack( mtx.GetTranslation() );
	}
}

MeshVisualizationContentSupplier::MeshVisualizationContentSupplier( const THandle< CMesh > mesh )
	: IDebugVisualizationContentSupplier( true )
{
	// for a world scene code below would be in 'for' statement
	CGUID objectId = CGUID::CreateFromUint64( mesh->GetObjectId().Get() );
	VisualizationContentView* objectView = GetObjectView( objectId );
	if ( objectView == nullptr )
	{
		objectView = InsertObjectView( objectId, new VisualizationContentView( new MeshDebugGeometry( mesh ), Matrix::IDENTITY ) );
	}
}