/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "meshParameter.h"


// keep sync with .fx shader!
static const Uint32	c_defaultWaterPatchResolution = 64;
static const Uint32	c_maxWaterPatchResolution = 64*64;
static const Uint32	c_defaultWaterPatchFrameCount = 16;
static const Float	c_playbackFPS = 24.0f;
static const Float	c_playbackSpeed = 4.0f;

using WaterPatchInterpolationNode = red::StaticArray< Float, c_defaultWaterPatchFrameCount >;

namespace mesh
{
	class RESOURCEMESH_API MeshParamWaterPatchData : public MeshParameter
	{
		RTTI_DECLARE_TYPE( MeshParamWaterPatchData );

	public:
		MeshParamWaterPatchData() {};
		MeshParamWaterPatchData( const Uint32& dataSize, const Uint32& elemStride, const Float* dataBuffer );

		const Float QueryWaterLevel( const Float x, const Float y, const Float time, Uint32& tileX, Uint32& tileY, Uint32& frame );
		
	private:
		virtual void OnMeshChanged(const CMesh& mesh) override;
		
		const Uint32 GetFrame( const Float time, const Uint32 frameSeek = 0u ) const;
		
		Bool m_animLoop;
		Float m_animLength;
		red::StaticArray< WaterPatchInterpolationNode, c_maxWaterPatchResolution >	m_nodes;
		
	};
}
