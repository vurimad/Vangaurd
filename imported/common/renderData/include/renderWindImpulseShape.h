/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace rend
{
	static const Float MIN_WIND_IMPULSE_SHAPE_SIZE = 0.5f;

	enum class WindShapeAnchorPointVert
	{
		AP_CENTER,
		AP_TOP,
		AP_BOTTOM
	};

	enum class WindShapeAnchorPointHorz
	{
		AP_CENTER,
		AP_RIGHT,
		AP_LEFT
	};

	enum class WindShapeAnchorPointDepth
	{
		AP_CENTER,
		AP_FRONT,
		AP_BACK
	};

	//Must be the same like in windImpulseVolumeUpdate.fx
	//Make sure not to add any virtual methods to any of the Shape classes (we do not want the vptr field in those classes, 
	//because we want to be able to send them directly to GPU)
	//Watch out for correct padding of structures (structure elements must be padded to Float4)
	struct RENDER_DATA_API WindImpulseShape
	{
		Matrix m_transformation;
		Vector3 m_localPos;
		Float m_pad;
		Float m_lifeTimeNormalized;
		Float m_totalLifeTime;
		Float m_timeSinceCreationNormalized;
		Uint32 m_componentId; // used just for CPU tracking of particles
		Vector3 m_bounds; //extents of the shape in xyz
		Float m_bendIntensity;

		//don't use this constructor, it's just for DynArray
		WindImpulseShape();
		WindImpulseShape( Matrix transformation, Float lifeTime, Vector3 bounds, Float bendIntensity, Uint32 componentId = -1 );
	};

	struct RENDER_DATA_API WindImpulseSphere
	{
		WindImpulseShape m_base;

		//don't use this constructor, it's just for DynArray
		WindImpulseSphere();

		WindImpulseSphere( Matrix transformation, Float lifeTime, Float radius, Float bendIntensity,
						   WindShapeAnchorPointVert anchorPointVert = WindShapeAnchorPointVert::AP_CENTER,
						   WindShapeAnchorPointHorz anchorPointHorz = WindShapeAnchorPointHorz::AP_CENTER,
						   WindShapeAnchorPointDepth anchorPointDepth = WindShapeAnchorPointDepth::AP_CENTER,
						   Uint32 componentId = -1 );

		Vector3 ResolveAnchorPointLocation( WindShapeAnchorPointVert anchorPointVert, WindShapeAnchorPointHorz anchorPointHorz, WindShapeAnchorPointDepth anchorPointDepth );

		// Sort by ComponentId
		Bool operator<( const rend::WindImpulseSphere& other ) const
		{
			return m_base.m_componentId < other.m_base.m_componentId;
		}
	};

	//capsule with its bottom sphere centered around origin in local space
	struct RENDER_DATA_API WindImpulseCapsule
	{
		WindImpulseShape m_base;

		//don't use this constructor, it's just for DynArray
		WindImpulseCapsule();

		WindImpulseCapsule( Matrix transformation, Float lifeTime, Float radius, Float height, Float bendIntensity,
							WindShapeAnchorPointVert anchorPointVert = WindShapeAnchorPointVert::AP_CENTER,
							WindShapeAnchorPointHorz anchorPointHorz = WindShapeAnchorPointHorz::AP_CENTER,
							WindShapeAnchorPointDepth anchorPointDepth = WindShapeAnchorPointDepth::AP_CENTER, Uint32 componentId = -1 );

		Vector3 ResolveAnchorPointLocation( WindShapeAnchorPointVert anchorPointVert, WindShapeAnchorPointHorz anchorPointHorz, WindShapeAnchorPointDepth anchorPointDepth );

		// Sort by ComponentId
		Bool operator<( const rend::WindImpulseCapsule& other ) const
		{
			return m_base.m_componentId < other.m_base.m_componentId;
		}
	};
} // rend