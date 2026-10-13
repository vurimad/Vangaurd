/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshParamWaterPatchData.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MeshParamWaterPatchData, mesh );
	RTTI_PARENT_TYPE( mesh::MeshParameter );
	RTTI_PROPERTY( m_animLoop ).editable();
	RTTI_PROPERTY( m_animLength ).readOnly();
	RTTI_PROPERTY( m_nodes );
RTTI_END_TYPE();

namespace mesh
{

	MeshParamWaterPatchData::MeshParamWaterPatchData( const Uint32& dataSize, const Uint32& elemStride, const Float* dataBuffer )
	{	
		m_animLength = (Float)c_defaultWaterPatchFrameCount / c_playbackFPS;
		
		const Uint32 nodeSize = dataSize/c_defaultWaterPatchFrameCount;
		m_nodes.Resize( nodeSize < c_maxWaterPatchResolution ? nodeSize : c_maxWaterPatchResolution );

		for( Uint32 i=0; i<m_nodes.Size(); ++i )
		{
			for( Uint32 frame=0; frame < c_defaultWaterPatchFrameCount; ++frame )
			{
				m_nodes[i].PushBack( dataBuffer[ c_defaultWaterPatchResolution*c_defaultWaterPatchResolution*frame + i ] );
			}
		}
	}

	void MeshParamWaterPatchData::OnMeshChanged( const CMesh& mesh )
	{
	}

	const Float MeshParamWaterPatchData::QueryWaterLevel( const Float x, const Float y, const Float time, Uint32& tileX, Uint32& tileY, Uint32& frame )
	{
		tileX = (Uint32) floorf( math::Min( x, 0.99999f )*c_defaultWaterPatchResolution );
		tileY = (Uint32) floorf( math::Min( y, 0.99999f )*c_defaultWaterPatchResolution );

		frame = GetFrame( time );
		Uint32 controlFrame = (Uint32)floor((Float)frame / 4.0f);
		frame = controlFrame;

#ifndef RED_CONFIGURATION_FINAL
		// visualization of outer edge for object inspector only
		if (!(tileX*c_defaultWaterPatchResolution + tileY < m_nodes.Size()) || m_nodes[tileX*c_defaultWaterPatchResolution + tileY].Empty())
			return -1000.0f;
#endif
		RED_FATAL_ASSERT( tileX*c_defaultWaterPatchResolution + tileY < m_nodes.Size(), "Water Patch data is corrupted, debug immediately!" );
		RED_FATAL_ASSERT( !m_nodes[tileX*c_defaultWaterPatchResolution + tileY].Empty(), "Water Patch data is corrupted, debug immediately!" );

		const Uint32 localIndex = tileX*c_defaultWaterPatchResolution + tileY;

		return math::Lerp(MFract(c_playbackSpeed*time*0.25f), m_nodes[localIndex][controlFrame], m_nodes[localIndex][(controlFrame + 1) % c_defaultWaterPatchFrameCount]);
	}
	
	const Uint32 MeshParamWaterPatchData::GetFrame( const Float time, const Uint32 frameSeek ) const
	{
		return Clamp<Uint32>( (Uint32)(fmodf( c_playbackSpeed*time + (Float)frameSeek, (Float)c_defaultWaterPatchResolution )), 0, c_defaultWaterPatchResolution - 1 );
	}

} // mesh
