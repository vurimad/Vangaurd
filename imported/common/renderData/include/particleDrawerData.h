/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//----------------------------------------

enum EMeshParticleOrientationMode : Int32;

union UDrawerData
{
	struct
	{
		Float	m_pivotOffset;
		Float	m_stretchPerVelocity;
	} motionBlur;
	struct
	{
		Float m_texturesPerUnit;
		Bool  m_dynamicTexCoords;
		Float m_minSegmentsPer360Degrees;
		Bool m_ribbonize;
		Float m_ribbonTesselationDelta;
	} trail;
	struct
	{
		Float	m_texturesPerUnit;
		Bool	m_dynamicTexCoords;
		Float	m_transparencyOffset;
		Float	m_transparencyLength;
		Uint32	m_numSegments;
		Float	m_sourceTangent[3];
		Float	m_targetTangent[3];
		Float	m_debugTargetTranslation[3];
		Float	m_rotation;
	} beam;
	struct
	{
		EMeshParticleOrientationMode	m_orientationMode : 8;	//<! Orientation mode for meshes
	} mesh;
	struct
	{
		Float m_pivotOffset;
		Float m_orientation[3];
	} orientation;
	struct
	{
		Float m_pivotOffset;
	} simple;
};

struct RenderEmitterDrawerData
{
	RED_USE_MEMORY_POOL( rend::PoolParticleData );

	RenderEmitterDrawerData()
	{}

	UDrawerData							m_drawerData;
	red::DynArray< TRenderPtr<IRenderResource> >	m_renderMeshResources{ rend::PoolParticleData() };
};