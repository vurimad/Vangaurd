#include "build.h"
#include "importMeshREX_Cloth.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshParamCloth.h"
#include "../../../common/physics/include/physXNatives.h"

#include "../../../backend/backendMesh/include/meshDataView.h"

red::DynArray< reString > GetChannelNames( const ReFileMeshPtr& reMesh, const char* channel_prefix )
{
	red::DynArray< reString > result;
	const Uint32 numChunks = reMesh->getNumChunks();
	for( Uint32 chunk = 0; chunk < numChunks; ++chunk )
	{
		auto& chunkPtr = reMesh->getChunk( chunk );
		auto channelNames = chunkPtr->getDataChannelsNames();

		for( auto& name : channelNames )
		{
			red::StringView view( name.getData(), name.getLength() );
			if( view.Find( channel_prefix ) != red::StringView::npos )
				result.PushBack( name );
		}
	}

	red::alg::RemoveDuplicates( result );

	std::sort( red::begin( result ), red::end( result ), []( reString s1, reString s2 ) {
		auto view_1 = red::StringView( s1.getData(), s1.getLength() );
		auto view_2 = red::StringView( s2.getData(), s2.getLength() );

		auto sub_view1 = view_1.SubView( view_1.FindReverse( '_' ) + 1 );
		auto sub_view2 = view_2.SubView( view_2.FindReverse( '_' ) + 1 );

		const Uint32 as_u32_1 = atoi( sub_view1.Data() );
		const Uint32 as_u32_2 = atoi( sub_view2.Data() );

		return as_u32_1 < as_u32_2;
	} );

	return result;
}

void PushIndices( red::DynArray< Uint16 >& indices, const ReFileMeshChunkPtr& chunk, const reString& name )
{
	Int32* data = nullptr;
	Uint32 count = 0;
	if( chunk->getDataChannel( name, count, data ) )
	{
		for( Uint16 idx = 0; idx < count; ++idx )
		{
			if( data[ idx ] > 0 )
			{
				indices.PushBack( idx );
			}
		}

		red::alg::RemoveDuplicates( indices );
	}
}


void ImportGraphicalClothData( THandle< CMesh > meshData, std::vector< ReFileMeshPtr >& meshes )
{
	mesh::DataView view( *meshData );
	const Uint32 numChunks = view.GetNumChunks();

	auto param = meshData->CreateParameter< mesh::MeshParamCloth_Graphical >();
	param->m_chunks.Clear();
	param->m_lodChunkIndices.Clear();
	param->m_latchers.Clear();

	for( Uint32 i = 0; i < numChunks; ++i )
	{
		view.SetChunkVertexType( i, MVT_StaticMesh );
		auto& chunk = param->m_chunks.EmplaceBack();

		auto positionsStream = view.GetChunkStreamData( i, MST_Position_3F );
		auto indicesStream = view.GetChunkStreamData( i, MST_Index_U16 );
		auto skinWeightsStream = view.GetChunkStreamData( i, MST_SkinningWeights_4F );
		auto skinIndicesStream = view.GetChunkStreamData( i, MST_SkinningIndices_4U8 );
		auto skinWeightsExtStream = view.GetChunkStreamData( i, MST_SkinningWeightsExt_4F );
		auto skinIndicesExtStream = view.GetChunkStreamData( i, MST_SkinningIndicesExt_4U8 );

		chunk.m_positions = DataBuffer::Copy( positionsStream.m_data, positionsStream.m_count * positionsStream.m_stride, 4 );
		chunk.m_indices = DataBuffer::Copy( indicesStream.m_data, indicesStream.m_count * indicesStream.m_stride, 4 );
		chunk.m_skinWeights = DataBuffer::Copy( skinWeightsStream.m_data, skinWeightsStream.m_count * skinWeightsStream.m_stride, 4 );
		chunk.m_skinIndices = DataBuffer::Copy( skinIndicesStream.m_data, skinIndicesStream.m_count * skinIndicesStream.m_stride, 4 );

		if( !skinWeightsExtStream.Empty() && !skinIndicesExtStream.Empty() )
		{
			chunk.m_skinWeightsExt = DataBuffer::Copy( skinWeightsExtStream.m_data, skinWeightsExtStream.m_count * skinWeightsExtStream.m_stride, 4 );
			chunk.m_skinIndicesExt = DataBuffer::Copy( skinIndicesExtStream.m_data, skinIndicesExtStream.m_count * skinIndicesExtStream.m_stride, 4 );
		}
	}

	for( mesh::LODIndex i = 0; i < view.GetNumLODs(); ++i )
	{
		auto lodChunkIndices = view.GetLODChunkIndices( i );
		param->m_lodChunkIndices.EmplaceBack( lodChunkIndices );
	}

	for( auto& reMesh : meshes )
	{
		const Uint32 numChunks = reMesh->getChunkCount();
		for( Uint32 chunk = 0; chunk < numChunks; ++chunk )
		{
			PushIndices( param->m_chunks[ chunk ].m_simulation, reMesh->getChunk( chunk ), "simulation" );
		}
	}
}


void ImportPhysicalClothData( THandle< CMesh > meshData, std::vector< ReFileMeshPtr >& meshes )
{
	auto unpackChannel = []( DataBuffer& channel, Int32* data, Uint32 dataCount ) 
	{
		const auto dataSpan = red::MakeArraySpan( data, dataCount );
		std::transform(begin(dataSpan), end(dataSpan), static_cast<Uint8*>(channel.Data()), [](Uint32 value){ return static_cast<Uint8>(value); });
	};

	auto fill_span = [](DataBuffer& channel, Uint32 size, Uint8 value)
	{
		auto array_span = channel.GetSpan().AsArray<Uint8>(0, size);
		std::fill(begin(array_span), end(array_span), value);
	};

	auto mesh = meshes[ 0 ];
	const auto numChunks = mesh->getChunkCount();

	auto param = meshData->CreateParameter< mesh::MeshParamCloth >();

	param->m_rawData.m_maxDistanceChannel.Resize( numChunks );
	param->m_rawData.m_maxDistanceExtChannel.Resize( numChunks );
	param->m_rawData.m_backstopDistanceChannel.Resize( numChunks );
	param->m_rawData.m_backstopRadiusChannel.Resize( numChunks );
	param->m_rawData.m_selfCollisionChannel.Resize( numChunks );
	param->m_chunks.Resize( numChunks );

	param->m_lodChunkIndices.Clear();
	param->m_drivers.Clear();

	mesh::DataView view( *meshData );
	for( Uint32 i = 0; i < numChunks; ++i )
	{
		auto chunk = mesh->getChunk( i );

		Int32* mdData = nullptr;
		Int32* m2Data = nullptr;
		Int32* bdData = nullptr;
		Int32* brData = nullptr;
		Int32* scData = nullptr;

		Uint32 mdDataCount = 0;
		Uint32 m2DataCount = 0;
		Uint32 bdDataCount = 0;
		Uint32 brDataCount = 0;
		Uint32 scDataCount = 0;

		Int32 vtxCount = chunk->getNumVertices();

		auto& maxDistanceChannel = param->m_rawData.m_maxDistanceChannel[ i ];
		if( chunk->getDataChannel( "maxDistance", mdDataCount, mdData ) )
		{
			maxDistanceChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			unpackChannel( maxDistanceChannel, mdData, mdDataCount );
		}
		else if( maxDistanceChannel.Empty() || maxDistanceChannel.Size() != vtxCount )
		{
			maxDistanceChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
		}

		auto& maxDistanceExtChannel = param->m_rawData.m_maxDistanceExtChannel[ i ];
		if( chunk->getDataChannel( "maxDistanceExt", m2DataCount, m2Data ) )
		{
			maxDistanceExtChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			unpackChannel( maxDistanceExtChannel, m2Data, m2DataCount );
		}
		else if( maxDistanceExtChannel.Empty() || maxDistanceExtChannel.Size() != vtxCount )
		{
			maxDistanceExtChannel = DataBuffer::Copy( maxDistanceChannel );
		}

		auto& backstopChannel = param->m_rawData.m_backstopDistanceChannel[ i ];
		if( chunk->getDataChannel( "backstop", bdDataCount, bdData ) )
		{
			backstopChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			unpackChannel( backstopChannel, bdData, bdDataCount );
		}
		else if( backstopChannel.Empty() || backstopChannel.Size() != vtxCount )
		{
			backstopChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			fill_span(backstopChannel, vtxCount, 0xFF);
		}

		auto& backstopRadiusChannel = param->m_rawData.m_backstopRadiusChannel[ i ];
		if( chunk->getDataChannel( "backstopRadius", brDataCount, brData ) )
		{
			backstopRadiusChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			unpackChannel( backstopRadiusChannel, brData, brDataCount );
		}
		else if( backstopRadiusChannel.Empty() || backstopRadiusChannel.Size() != vtxCount )
		{
			backstopRadiusChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			fill_span(backstopRadiusChannel, vtxCount, 5);
		}

		auto& selfCollisionChannel = param->m_rawData.m_selfCollisionChannel[ i ];
		if( chunk->getDataChannel( "selfCollision", scDataCount, scData ) )
		{
			selfCollisionChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
			unpackChannel( selfCollisionChannel, scData, scDataCount );
		}
		else if( selfCollisionChannel.Empty() || selfCollisionChannel.Size() != vtxCount )
		{
			selfCollisionChannel = DataBuffer::Copy( DataBuffer( vtxCount, 4, red::PoolBackend() ) );
		}

		auto positions = mesh::MakeSpan< Vector3 >( view, i, MST_Position_3F );
		auto normals = mesh::MakeSpan< Vector3 >( view, i, MST_Normal_3F );
		auto indices = mesh::MakeSpan< Uint16 >( view, i, MST_Index_U16 );
		auto skinWeights = mesh::MakeSpan< Vector4 >( view, i, MST_SkinningWeights_4F );
		auto skinIndices = mesh::MakeSpan< Uint32 >( view, i, MST_SkinningIndices_4U8 );

		param->m_chunks[ i ].m_positions = DataBuffer::Copy( positions.Data(), positions.SizeInBytes(), red::PoolResource() );
		param->m_chunks[ i ].m_normals = DataBuffer::Copy( normals.Data(), normals.SizeInBytes(), red::PoolResource() );
		param->m_chunks[ i ].m_indices = DataBuffer::Copy( indices.Data(), indices.SizeInBytes(), red::PoolResource() );
		param->m_chunks[ i ].m_skinWeights = DataBuffer::Copy( skinWeights.Data(), skinWeights.SizeInBytes(), red::PoolResource() );
		param->m_chunks[ i ].m_skinIndices = DataBuffer::Copy( skinIndices.Data(), skinIndices.SizeInBytes(), red::PoolResource() );

		auto skinWeightsExt = mesh::MakeSpan< Vector4 >( view, i, MST_SkinningWeightsExt_4F );
		auto skinIndicesExt = mesh::MakeSpan< Uint32 >( view, i, MST_SkinningIndicesExt_4U8 );

		if( !skinWeightsExt.Empty() && !skinIndicesExt.Empty() )
		{
			param->m_chunks[ i ].m_skinWeightsExt = DataBuffer::Copy( skinWeightsExt.Data(), skinWeightsExt.SizeInBytes(), red::PoolResource() );
			param->m_chunks[ i ].m_skinIndicesExt = DataBuffer::Copy( skinIndicesExt.Data(), skinIndicesExt.SizeInBytes(), red::PoolResource() );
		}

		if( !param->m_chunks[ i ].m_cookedData.Empty() && physics::cloth::NeedsVersionUpdate(param->m_chunks[ i ].m_cookedData.GetView()) )
		{
			physics::cloth::Update(param->m_chunks[ i ].m_cookedData);
		}
	}

	for( Uint32 i = 0; i < numChunks; ++i )
	{
		view.SetChunkVertexType( i, MVT_StaticMesh );
	}

	for( mesh::LODIndex i = 0; i < view.GetNumLODs(); ++i )
	{
		auto lodIndices = view.GetLODChunkIndices( i );
		param->m_lodChunkIndices.EmplaceBack( lodIndices );
	}
}